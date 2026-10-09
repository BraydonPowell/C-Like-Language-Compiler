#include "codegen.h"

#include "symtab.h"

#include <stdarg.h>

/*
 * Every expression leaves its value in %rax. Binary operators park the left
 * operand on the stack while the right one is evaluated. Calls follow the
 * System V convention, so the stack must be 16-byte aligned at each `call`;
 * we track how many 8-byte pushes are outstanding and pad when it is odd.
 */

typedef struct {
    FILE *out;
    Target target;
    int next_label;
    int depth; /* outstanding 8-byte pushes in the current function */
} Cg;

typedef struct LoopCtx {
    int break_label, continue_label;
} LoopCtx;

static const char *arg_regs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};

static void emit(Cg *cg, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputc('\t', cg->out);
    vfprintf(cg->out, fmt, ap);
    fputc('\n', cg->out);
    va_end(ap);
}

static void emit_label(Cg *cg, int id) {
    fprintf(cg->out, ".L%d:\n", id);
}

static int new_label(Cg *cg) {
    return cg->next_label++;
}

static const char *sym_prefix(const Cg *cg) {
    return cg->target == TARGET_MACOS ? "_" : "";
}

static void push(Cg *cg, const char *reg) {
    emit(cg, "pushq %s", reg);
    cg->depth++;
}

static void pop(Cg *cg, const char *reg) {
    emit(cg, "popq %s", reg);
    cg->depth--;
}

/* Returns the padding added so the caller can undo it after the call. */
static int align_for_call(Cg *cg) {
    if (cg->depth % 2 == 0)
        return 0;
    emit(cg, "subq $8, %%rsp");
    cg->depth++;
    return 1;
}

static void unalign_after_call(Cg *cg, int padded) {
    if (!padded)
        return;
    emit(cg, "addq $8, %%rsp");
    cg->depth--;
}

static void call_libc(Cg *cg, const char *name) {
    if (cg->target == TARGET_MACOS)
        emit(cg, "call _%s", name);
    else
        emit(cg, "call %s@PLT", name);
}

static void gen_expr(Cg *cg, const Expr *e);

static void gen_compare(Cg *cg, TokenKind op) {
    const char *set;
    switch (op) {
    case T_EQ: set = "sete"; break;
    case T_NE: set = "setne"; break;
    case T_LT: set = "setl"; break;
    case T_LE: set = "setle"; break;
    case T_GT: set = "setg"; break;
    default:   set = "setge"; break;
    }
    emit(cg, "cmpq %%rcx, %%rax");
    emit(cg, "%s %%al", set);
    emit(cg, "movzbl %%al, %%eax");
}

static void gen_binary(Cg *cg, const Expr *e) {
    TokenKind op = e->as.binary.op;

    /* && and || skip the right side; the left value is already the answer. */
    if (op == T_AND || op == T_OR) {
        int end = new_label(cg);
        gen_expr(cg, e->as.binary.lhs);
        emit(cg, "testq %%rax, %%rax");
        emit(cg, "%s .L%d", op == T_AND ? "je" : "jne", end);
        gen_expr(cg, e->as.binary.rhs);
        emit_label(cg, end);
        return;
    }

    gen_expr(cg, e->as.binary.lhs);
    push(cg, "%rax");
    gen_expr(cg, e->as.binary.rhs);
    emit(cg, "movq %%rax, %%rcx");
    pop(cg, "%rax");

    switch (op) {
    case T_PLUS:  emit(cg, "addq %%rcx, %%rax"); break;
    case T_MINUS: emit(cg, "subq %%rcx, %%rax"); break;
    case T_STAR:  emit(cg, "imulq %%rcx, %%rax"); break;
    case T_SLASH:
        emit(cg, "cqto");
        emit(cg, "idivq %%rcx");
        break;
    case T_PERCENT:
        emit(cg, "cqto");
        emit(cg, "idivq %%rcx");
        emit(cg, "movq %%rdx, %%rax");
        break;
    default:
        gen_compare(cg, op);
    }
}

static void gen_call(Cg *cg, const Expr *e) {
    int nargs = e->as.call.nargs;
    int padded = align_for_call(cg);

    for (const Expr *a = e->as.call.args; a; a = a->next) {
        gen_expr(cg, a);
        push(cg, "%rax");
    }
    for (int i = nargs - 1; i >= 0; i--)
        pop(cg, arg_regs[i]);

    emit(cg, "call %skes_%s", sym_prefix(cg), e->as.call.name);
    unalign_after_call(cg, padded);
}

static void gen_expr(Cg *cg, const Expr *e) {
    switch (e->kind) {
    case EX_INT:
    case EX_BOOL:
        emit(cg, "movq $%lld, %%rax", (long long)e->as.value);
        break;
    case EX_VAR:
        emit(cg, "movq %d(%%rbp), %%rax", e->as.var.sym->offset);
        break;
    case EX_UNARY:
        gen_expr(cg, e->as.unary.operand);
        if (e->as.unary.op == T_MINUS)
            emit(cg, "negq %%rax");
        else
            emit(cg, "xorq $1, %%rax");
        break;
    case EX_BINARY:
        gen_binary(cg, e);
        break;
    case EX_CALL:
        gen_call(cg, e);
        break;
    }
}

static void gen_print(Cg *cg, const Expr *e) {
    gen_expr(cg, e);
    int padded = align_for_call(cg);
    if (e->type == TY_BOOL) {
        emit(cg, "testq %%rax, %%rax");
        emit(cg, "leaq .Lfalse(%%rip), %%rsi");
        emit(cg, "leaq .Ltrue(%%rip), %%rdx");
        emit(cg, "cmovne %%rdx, %%rsi");
        emit(cg, "leaq .Lfmt_str(%%rip), %%rdi");
    } else {
        emit(cg, "movq %%rax, %%rsi");
        emit(cg, "leaq .Lfmt_int(%%rip), %%rdi");
    }
    emit(cg, "xorl %%eax, %%eax"); /* no vector registers used (variadic call) */
    call_libc(cg, "printf");
    unalign_after_call(cg, padded);
}

static void gen_stmt(Cg *cg, const Stmt *s, const LoopCtx *loop);

static void gen_body(Cg *cg, const Stmt *block, const LoopCtx *loop) {
    for (const Stmt *s = block->as.body; s; s = s->next)
        gen_stmt(cg, s, loop);
}

static void emit_epilogue(Cg *cg) {
    emit(cg, "leave");
    emit(cg, "ret");
}

static void gen_stmt(Cg *cg, const Stmt *s, const LoopCtx *loop) {
    switch (s->kind) {
    case ST_LET:
        gen_expr(cg, s->as.let.init);
        emit(cg, "movq %%rax, %d(%%rbp)", s->as.let.sym->offset);
        break;
    case ST_ASSIGN:
        gen_expr(cg, s->as.assign.value);
        emit(cg, "movq %%rax, %d(%%rbp)", s->as.assign.sym->offset);
        break;
    case ST_IF: {
        int else_label = new_label(cg);
        int end_label = new_label(cg);
        gen_expr(cg, s->as.if_.cond);
        emit(cg, "testq %%rax, %%rax");
        emit(cg, "je .L%d", else_label);
        gen_body(cg, s->as.if_.then_body, loop);
        if (s->as.if_.else_body) {
            emit(cg, "jmp .L%d", end_label);
            emit_label(cg, else_label);
            gen_stmt(cg, s->as.if_.else_body, loop);
            emit_label(cg, end_label);
        } else {
            emit_label(cg, else_label);
            emit_label(cg, end_label);
        }
        break;
    }
    case ST_WHILE: {
        LoopCtx ctx = {new_label(cg), new_label(cg)};
        emit_label(cg, ctx.continue_label);
        gen_expr(cg, s->as.while_.cond);
        emit(cg, "testq %%rax, %%rax");
        emit(cg, "je .L%d", ctx.break_label);
        gen_body(cg, s->as.while_.body, &ctx);
        emit(cg, "jmp .L%d", ctx.continue_label);
        emit_label(cg, ctx.break_label);
        break;
    }
    case ST_FOR: {
        int top = new_label(cg);
        LoopCtx ctx = {new_label(cg), new_label(cg)};
        if (s->as.for_.init)
            gen_stmt(cg, s->as.for_.init, loop);
        emit_label(cg, top);
        if (s->as.for_.cond) {
            gen_expr(cg, s->as.for_.cond);
            emit(cg, "testq %%rax, %%rax");
            emit(cg, "je .L%d", ctx.break_label);
        }
        gen_body(cg, s->as.for_.body, &ctx);
        emit_label(cg, ctx.continue_label);
        if (s->as.for_.step)
            gen_stmt(cg, s->as.for_.step, loop);
        emit(cg, "jmp .L%d", top);
        emit_label(cg, ctx.break_label);
        break;
    }
    case ST_RETURN:
        if (s->as.expr)
            gen_expr(cg, s->as.expr);
        emit_epilogue(cg);
        break;
    case ST_PRINT:
        gen_print(cg, s->as.expr);
        break;
    case ST_EXPR:
        gen_expr(cg, s->as.expr);
        break;
    case ST_BLOCK:
        gen_body(cg, s, loop);
        break;
    case ST_BREAK:
        emit(cg, "jmp .L%d", loop->break_label);
        break;
    case ST_CONTINUE:
        emit(cg, "jmp .L%d", loop->continue_label);
        break;
    }
}

static void gen_func(Cg *cg, const Func *f) {
    fprintf(cg->out, "%skes_%s:\n", sym_prefix(cg), f->name);
    emit(cg, "pushq %%rbp");
    emit(cg, "movq %%rsp, %%rbp");
    if (f->frame_size > 0)
        emit(cg, "subq $%d, %%rsp", f->frame_size);
    cg->depth = 0;

    int i = 0;
    for (const Param *p = f->params; p; p = p->next, i++)
        emit(cg, "movq %s, %d(%%rbp)", arg_regs[i], p->sym->offset);

    gen_body(cg, f->body, NULL);

    /* Falls off the end only for void functions; the checker guarantees the rest return. */
    emit(cg, "xorl %%eax, %%eax");
    emit_epilogue(cg);
    fputc('\n', cg->out);
}

void codegen_program(FILE *out, const Program *prog, Target target) {
    Cg cg = {.out = out, .target = target};
    const char *pfx = sym_prefix(&cg);

    fprintf(out, "\t.text\n\n");
    for (const Func *f = prog->funcs; f; f = f->next)
        gen_func(&cg, f);

    /* C entry point: run the program's main and hand its result to the OS. */
    fprintf(out, "\t.globl %smain\n%smain:\n", pfx, pfx);
    emit(&cg, "pushq %%rbp");
    emit(&cg, "movq %%rsp, %%rbp");
    emit(&cg, "call %skes_main", pfx);
    emit(&cg, "popq %%rbp");
    emit(&cg, "ret");

    if (target == TARGET_MACOS)
        fprintf(out, "\n\t.section __TEXT,__cstring,cstring_literals\n");
    else
        fprintf(out, "\n\t.section .rodata\n");
    fprintf(out, ".Lfmt_int:\n\t.asciz \"%%ld\\n\"\n");
    fprintf(out, ".Lfmt_str:\n\t.asciz \"%%s\\n\"\n");
    fprintf(out, ".Ltrue:\n\t.asciz \"true\"\n");
    fprintf(out, ".Lfalse:\n\t.asciz \"false\"\n");

    if (target == TARGET_LINUX)
        fprintf(out, "\n\t.section .note.GNU-stack,\"\",@progbits\n");
}
