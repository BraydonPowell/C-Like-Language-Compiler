#include "check.h"

#include "diag.h"
#include "symtab.h"

#include <string.h>

#define MAX_REG_ARGS 6

static SymTab tab;
static Func *cur_func;
static int next_offset;
static int loop_depth;

static int is_arith(TokenKind op) {
    return op == T_PLUS || op == T_MINUS || op == T_STAR || op == T_SLASH || op == T_PERCENT;
}

static int is_relational(TokenKind op) {
    return op == T_LT || op == T_LE || op == T_GT || op == T_GE;
}

static int is_equality(TokenKind op) {
    return op == T_EQ || op == T_NE;
}

static Type check_expr(Expr *e);

static Type check_unary(Expr *e) {
    TokenKind op = e->as.unary.op;
    Type want = op == T_MINUS ? TY_INT : TY_BOOL;
    Type got = check_expr(e->as.unary.operand);
    if (got != TY_ERROR && got != want)
        diag_error(e->line, e->col, "operator %s expects %s, got %s",
                   token_kind_name(op), type_name(want), type_name(got));
    return want;
}

static Type check_binary(Expr *e) {
    TokenKind op = e->as.binary.op;
    Type lt = check_expr(e->as.binary.lhs);
    Type rt = check_expr(e->as.binary.rhs);
    int known = lt != TY_ERROR && rt != TY_ERROR;

    if (is_equality(op)) {
        if (known && (lt != rt || lt == TY_VOID))
            diag_error(e->line, e->col, "cannot compare %s with %s", type_name(lt), type_name(rt));
        return TY_BOOL;
    }

    Type operand = (is_arith(op) || is_relational(op)) ? TY_INT : TY_BOOL;
    if (known && (lt != operand || rt != operand))
        diag_error(e->line, e->col, "operator %s expects %s operands, got %s and %s",
                   token_kind_name(op), type_name(operand), type_name(lt), type_name(rt));

    if ((op == T_SLASH || op == T_PERCENT) &&
        e->as.binary.rhs->kind == EX_INT && e->as.binary.rhs->as.value == 0)
        diag_error(e->as.binary.rhs->line, e->as.binary.rhs->col, "division by zero");

    return is_arith(op) ? TY_INT : TY_BOOL;
}

static Type check_call(Expr *e) {
    const char *name = e->as.call.name;
    Symbol *sym = symtab_lookup(&tab, name);

    /* Check the arguments regardless, so errors inside them still surface. */
    for (Expr *a = e->as.call.args; a; a = a->next)
        check_expr(a);

    if (!sym) {
        diag_error(e->line, e->col, "undefined function '%s'", name);
        return TY_ERROR;
    }
    if (sym->kind != SYM_FUNC) {
        diag_error(e->line, e->col, "'%s' is not a function", name);
        return TY_ERROR;
    }
    e->as.call.sym = sym;

    Func *f = sym->func;
    if (e->as.call.nargs != f->nparams) {
        diag_error(e->line, e->col, "'%s' expects %d argument%s, got %d", name, f->nparams,
                   f->nparams == 1 ? "" : "s", e->as.call.nargs);
        return sym->type;
    }

    int i = 1;
    Param *prm = f->params;
    for (Expr *a = e->as.call.args; a; a = a->next, prm = prm->next, i++)
        if (a->type != TY_ERROR && a->type != prm->type)
            diag_error(a->line, a->col, "argument %d of '%s' expects %s, got %s",
                       i, name, type_name(prm->type), type_name(a->type));
    return sym->type;
}

static Type check_expr(Expr *e) {
    switch (e->kind) {
    case EX_INT:
        e->type = TY_INT;
        break;
    case EX_BOOL:
        e->type = TY_BOOL;
        break;
    case EX_VAR: {
        Symbol *sym = symtab_lookup(&tab, e->as.var.name);
        if (!sym) {
            diag_error(e->line, e->col, "undefined variable '%s'", e->as.var.name);
            e->type = TY_ERROR;
        } else if (sym->kind != SYM_VAR) {
            diag_error(e->line, e->col, "'%s' is a function, not a variable", e->as.var.name);
            e->type = TY_ERROR;
        } else {
            e->as.var.sym = sym;
            e->type = sym->type;
        }
        break;
    }
    case EX_UNARY:
        e->type = check_unary(e);
        break;
    case EX_BINARY:
        e->type = check_binary(e);
        break;
    case EX_CALL:
        e->type = check_call(e);
        break;
    }
    return e->type;
}

static Symbol *declare_local(const char *name, Type type, int line, int col) {
    Symbol *prev = symtab_lookup_local(&tab, name);
    if (prev) {
        diag_error(line, col, "'%s' is already declared in this scope (line %d)", name, prev->line);
        return NULL;
    }
    Symbol *sym = symtab_declare(&tab, name, SYM_VAR, type, line);
    next_offset += 8;
    sym->offset = -next_offset;
    return sym;
}

static void check_condition(Expr *cond, const char *what) {
    Type t = check_expr(cond);
    if (t != TY_ERROR && t != TY_BOOL)
        diag_error(cond->line, cond->col, "%s condition must be bool, got %s", what, type_name(t));
}

static void check_stmt(Stmt *s);

static void check_block(Stmt *block) {
    symtab_enter(&tab);
    for (Stmt *s = block->as.body; s; s = s->next)
        check_stmt(s);
    symtab_leave(&tab);
}

static void check_let(Stmt *s) {
    /* The initializer is checked first so `let x = x;` sees the outer x. */
    Type init = check_expr(s->as.let.init);
    Type type = init;

    if (s->as.let.has_type) {
        type = s->as.let.type;
        if (type == TY_VOID) {
            diag_error(s->line, s->col, "variable '%s' cannot have type void", s->as.let.name);
            type = TY_ERROR;
        } else if (init != TY_ERROR && init != type) {
            diag_error(s->as.let.init->line, s->as.let.init->col,
                       "cannot initialize '%s' of type %s with a value of type %s",
                       s->as.let.name, type_name(type), type_name(init));
        }
    } else if (init == TY_VOID) {
        diag_error(s->as.let.init->line, s->as.let.init->col,
                   "cannot initialize '%s' with a void value", s->as.let.name);
        type = TY_ERROR;
    }
    s->as.let.sym = declare_local(s->as.let.name, type, s->line, s->col);
}

static void check_assign(Stmt *s) {
    Type value = check_expr(s->as.assign.value);
    if (!s->as.assign.name)
        return; /* parser already complained about the target */

    Symbol *sym = symtab_lookup(&tab, s->as.assign.name);
    if (!sym) {
        diag_error(s->line, s->col, "undefined variable '%s'", s->as.assign.name);
    } else if (sym->kind != SYM_VAR) {
        diag_error(s->line, s->col, "cannot assign to function '%s'", s->as.assign.name);
    } else {
        s->as.assign.sym = sym;
        if (value != TY_ERROR && sym->type != TY_ERROR && value != sym->type)
            diag_error(s->as.assign.value->line, s->as.assign.value->col,
                       "cannot assign %s to '%s' of type %s",
                       type_name(value), s->as.assign.name, type_name(sym->type));
    }
}

static void check_return(Stmt *s) {
    Expr *v = s->as.expr;
    if (cur_func->ret == TY_VOID) {
        if (v) {
            check_expr(v);
            diag_error(v->line, v->col, "void function '%s' cannot return a value", cur_func->name);
        }
        return;
    }
    if (!v) {
        diag_error(s->line, s->col, "function '%s' must return %s",
                   cur_func->name, type_name(cur_func->ret));
        return;
    }
    Type t = check_expr(v);
    if (t != TY_ERROR && t != cur_func->ret)
        diag_error(v->line, v->col, "function '%s' returns %s, but this expression is %s",
                   cur_func->name, type_name(cur_func->ret), type_name(t));
}

static void check_stmt(Stmt *s) {
    switch (s->kind) {
    case ST_LET:
        check_let(s);
        break;
    case ST_ASSIGN:
        check_assign(s);
        break;
    case ST_IF:
        check_condition(s->as.if_.cond, "if");
        check_block(s->as.if_.then_body);
        if (s->as.if_.else_body) {
            if (s->as.if_.else_body->kind == ST_IF)
                check_stmt(s->as.if_.else_body);
            else
                check_block(s->as.if_.else_body);
        }
        break;
    case ST_WHILE:
        check_condition(s->as.while_.cond, "while");
        loop_depth++;
        check_block(s->as.while_.body);
        loop_depth--;
        break;
    case ST_FOR:
        symtab_enter(&tab); /* the loop variable lives in its own scope */
        if (s->as.for_.init)
            check_stmt(s->as.for_.init);
        if (s->as.for_.cond)
            check_condition(s->as.for_.cond, "for");
        if (s->as.for_.step)
            check_stmt(s->as.for_.step);
        loop_depth++;
        check_block(s->as.for_.body);
        loop_depth--;
        symtab_leave(&tab);
        break;
    case ST_RETURN:
        check_return(s);
        break;
    case ST_PRINT: {
        Type t = check_expr(s->as.expr);
        if (t == TY_VOID)
            diag_error(s->as.expr->line, s->as.expr->col, "cannot print a void value");
        break;
    }
    case ST_EXPR:
        check_expr(s->as.expr);
        break;
    case ST_BLOCK:
        check_block(s);
        break;
    case ST_BREAK:
    case ST_CONTINUE:
        if (loop_depth == 0)
            diag_error(s->line, s->col, "'%s' outside of a loop",
                       s->kind == ST_BREAK ? "break" : "continue");
        break;
    }
}

/* Conservative: true only when every path through the statement returns. */
static int always_returns(const Stmt *s) {
    switch (s->kind) {
    case ST_RETURN:
        return 1;
    case ST_BLOCK:
        for (const Stmt *c = s->as.body; c; c = c->next)
            if (always_returns(c))
                return 1;
        return 0;
    case ST_IF:
        return s->as.if_.else_body && always_returns(s->as.if_.then_body) &&
               always_returns(s->as.if_.else_body);
    default:
        return 0;
    }
}

static void check_func(Func *f) {
    cur_func = f;
    next_offset = 0;
    loop_depth = 0;

    symtab_enter(&tab);
    for (Param *p = f->params; p; p = p->next) {
        if (p->type == TY_VOID) {
            diag_error(p->line, p->col, "parameter '%s' cannot have type void", p->name);
            p->type = TY_ERROR;
        }
        p->sym = declare_local(p->name, p->type, p->line, p->col);
    }

    check_block(f->body);
    symtab_leave(&tab);

    if (f->ret != TY_VOID && f->ret != TY_ERROR && !always_returns(f->body))
        diag_error(f->line, f->col, "function '%s' does not return a value on every path", f->name);

    f->frame_size = (next_offset + 15) & ~15;
}

static void declare_functions(Program *prog) {
    for (Func *f = prog->funcs; f; f = f->next) {
        Symbol *prev = symtab_lookup_local(&tab, f->name);
        if (prev) {
            diag_error(f->line, f->col, "function '%s' is already defined (line %d)", f->name, prev->line);
            continue;
        }
        Symbol *sym = symtab_declare(&tab, f->name, SYM_FUNC, f->ret, f->line);
        sym->func = f;
        if (f->nparams > MAX_REG_ARGS)
            diag_error(f->line, f->col, "'%s' has %d parameters; the limit is %d",
                       f->name, f->nparams, MAX_REG_ARGS);
    }

    Symbol *entry = symtab_lookup_local(&tab, "main");
    if (!entry) {
        diag_error(0, 0, "program has no 'main' function");
    } else if (entry->func->ret != TY_INT || entry->func->nparams != 0) {
        diag_error(entry->line, entry->func->col, "'main' must be declared as fn main() -> int");
    }
}

int check_program(Program *prog) {
    int before = diag_count();
    symtab_init(&tab);
    declare_functions(prog);
    for (Func *f = prog->funcs; f; f = f->next)
        check_func(f);
    return diag_count() == before;
}
