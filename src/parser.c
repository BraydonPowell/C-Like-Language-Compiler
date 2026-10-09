#include "parser.h"

#include "diag.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>

typedef struct {
    Lexer lx;
    Token cur, prev;
    int panic;       /* set after an error until we resync at a statement boundary */
    long consumed;   /* tokens consumed so far, used to guarantee loop progress */
} Parser;

static Stmt *parse_stmt(Parser *p);
static Stmt *parse_block(Parser *p);
static Expr *parse_expr(Parser *p);

static void advance(Parser *p) {
    int errors_before = diag_count();
    p->prev = p->cur;
    p->cur = lexer_next(&p->lx);
    p->consumed++;
    /* A bad character already got its own message; don't pile a parse error on top. */
    if (diag_count() > errors_before)
        p->panic = 1;
}

static int check(const Parser *p, TokenKind k) {
    return p->cur.kind == k;
}

static int match(Parser *p, TokenKind k) {
    if (!check(p, k))
        return 0;
    advance(p);
    return 1;
}

static void error_at(Parser *p, int line, int col, const char *fmt, ...) {
    if (p->panic)
        return;
    p->panic = 1;
    va_list ap;
    va_start(ap, fmt);
    diag_verror(line, col, fmt, ap);
    va_end(ap);
}

/* A missing ';' reads better when it points just past the previous token
 * instead of at whatever happens to follow. */
static void consume(Parser *p, TokenKind k, const char *msg) {
    if (match(p, k))
        return;
    if (k == T_SEMI)
        error_at(p, p->prev.line, p->prev.col + p->prev.len, "%s", msg);
    else
        error_at(p, p->cur.line, p->cur.col, "%s", msg);
}

static Token consume_ident(Parser *p, const char *msg) {
    if (check(p, T_IDENT)) {
        advance(p);
        return p->prev;
    }
    error_at(p, p->cur.line, p->cur.col, "%s", msg);
    return p->cur;
}

static void synchronize(Parser *p) {
    p->panic = 0;
    while (!check(p, T_EOF)) {
        if (p->prev.kind == T_SEMI)
            return;
        switch (p->cur.kind) {
        case T_LET: case T_IF: case T_WHILE: case T_FOR: case T_RETURN:
        case T_PRINT: case T_BREAK: case T_CONTINUE: case T_RBRACE: case T_FN:
            return;
        default:
            advance(p);
        }
    }
}

static char *token_text(const Token *t) {
    return arena_strndup(t->start, t->len);
}

static Expr *new_expr(ExprKind kind, int line, int col) {
    Expr *e = arena_alloc(sizeof *e);
    e->kind = kind;
    e->line = line;
    e->col = col;
    return e;
}

static Stmt *new_stmt(StmtKind kind, int line, int col) {
    Stmt *s = arena_alloc(sizeof *s);
    s->kind = kind;
    s->line = line;
    s->col = col;
    return s;
}

/* ---- types ---- */

static int parse_type(Parser *p, Type *out) {
    if (match(p, T_KW_INT)) *out = TY_INT;
    else if (match(p, T_KW_BOOL)) *out = TY_BOOL;
    else if (match(p, T_KW_VOID)) *out = TY_VOID;
    else {
        error_at(p, p->cur.line, p->cur.col, "expected a type ('int', 'bool' or 'void')");
        *out = TY_ERROR;
        return 0;
    }
    return 1;
}

/* ---- expressions ---- */

static Expr *parse_call_args(Parser *p, Token name) {
    Expr *call = new_expr(EX_CALL, name.line, name.col);
    call->as.call.name = token_text(&name);

    Expr *tail = NULL;
    if (!check(p, T_RPAREN)) {
        do {
            Expr *arg = parse_expr(p);
            if (tail)
                tail->next = arg;
            else
                call->as.call.args = arg;
            tail = arg;
            call->as.call.nargs++;
        } while (match(p, T_COMMA));
    }
    consume(p, T_RPAREN, "expected ')' after arguments");
    return call;
}

static Expr *parse_primary(Parser *p) {
    Token t = p->cur;

    if (match(p, T_INT_LIT)) {
        Expr *e = new_expr(EX_INT, t.line, t.col);
        e->as.value = t.value;
        return e;
    }
    if (match(p, T_TRUE) || match(p, T_FALSE)) {
        Expr *e = new_expr(EX_BOOL, t.line, t.col);
        e->as.value = t.kind == T_TRUE;
        return e;
    }
    if (match(p, T_IDENT)) {
        if (match(p, T_LPAREN))
            return parse_call_args(p, t);
        Expr *e = new_expr(EX_VAR, t.line, t.col);
        e->as.var.name = token_text(&t);
        return e;
    }
    if (match(p, T_LPAREN)) {
        Expr *e = parse_expr(p);
        consume(p, T_RPAREN, "expected ')' after expression");
        return e;
    }

    error_at(p, t.line, t.col, "expected an expression");
    return new_expr(EX_INT, t.line, t.col); /* placeholder so callers never see NULL */
}

static Expr *parse_unary(Parser *p) {
    Token t = p->cur;
    if (match(p, T_MINUS) || match(p, T_NOT)) {
        Expr *e = new_expr(EX_UNARY, t.line, t.col);
        e->as.unary.op = t.kind;
        e->as.unary.operand = parse_unary(p);
        return e;
    }
    return parse_primary(p);
}

static int precedence(TokenKind k) {
    switch (k) {
    case T_OR: return 1;
    case T_AND: return 2;
    case T_EQ: case T_NE: return 3;
    case T_LT: case T_LE: case T_GT: case T_GE: return 4;
    case T_PLUS: case T_MINUS: return 5;
    case T_STAR: case T_SLASH: case T_PERCENT: return 6;
    default: return 0;
    }
}

/* All binary operators are left-associative. */
static Expr *parse_binary(Parser *p, int min_prec) {
    Expr *lhs = parse_unary(p);
    for (;;) {
        int prec = precedence(p->cur.kind);
        if (prec == 0 || prec < min_prec)
            return lhs;
        Token op = p->cur;
        advance(p);
        Expr *rhs = parse_binary(p, prec + 1);

        Expr *e = new_expr(EX_BINARY, op.line, op.col);
        e->as.binary.op = op.kind;
        e->as.binary.lhs = lhs;
        e->as.binary.rhs = rhs;
        lhs = e;
    }
}

static Expr *parse_expr(Parser *p) {
    return parse_binary(p, 1);
}

/* ---- statements ---- */

/* `let` without the trailing ';' so `for` headers can reuse it. */
static Stmt *parse_let(Parser *p) {
    Token kw = p->cur;
    advance(p);
    Stmt *s = new_stmt(ST_LET, kw.line, kw.col);

    Token name = consume_ident(p, "expected a variable name after 'let'");
    s->line = name.line;
    s->col = name.col;
    s->as.let.name = token_text(&name);

    if (match(p, T_COLON)) {
        s->as.let.has_type = 1;
        parse_type(p, &s->as.let.type);
    }
    consume(p, T_ASSIGN, "expected '=' in variable declaration");
    s->as.let.init = parse_expr(p);
    return s;
}

/* Either `name = expr` or a bare expression, again without the ';'. */
static Stmt *parse_assign_or_expr(Parser *p) {
    Expr *e = parse_expr(p);
    if (check(p, T_ASSIGN)) {
        Token eq = p->cur;
        advance(p);
        Stmt *s = new_stmt(ST_ASSIGN, e->line, e->col);
        if (e->kind != EX_VAR) {
            error_at(p, eq.line, eq.col, "left side of '=' must be a variable");
        } else {
            s->as.assign.name = e->as.var.name;
        }
        s->as.assign.value = parse_expr(p);
        return s;
    }
    Stmt *s = new_stmt(ST_EXPR, e->line, e->col);
    s->as.expr = e;
    return s;
}

static Stmt *parse_if(Parser *p) {
    Token kw = p->cur;
    advance(p);
    Stmt *s = new_stmt(ST_IF, kw.line, kw.col);
    consume(p, T_LPAREN, "expected '(' after 'if'");
    s->as.if_.cond = parse_expr(p);
    consume(p, T_RPAREN, "expected ')' after condition");
    s->as.if_.then_body = parse_block(p);
    if (match(p, T_ELSE))
        s->as.if_.else_body = check(p, T_IF) ? parse_if(p) : parse_block(p);
    return s;
}

static Stmt *parse_while(Parser *p) {
    Token kw = p->cur;
    advance(p);
    Stmt *s = new_stmt(ST_WHILE, kw.line, kw.col);
    consume(p, T_LPAREN, "expected '(' after 'while'");
    s->as.while_.cond = parse_expr(p);
    consume(p, T_RPAREN, "expected ')' after condition");
    s->as.while_.body = parse_block(p);
    return s;
}

static Stmt *parse_for(Parser *p) {
    Token kw = p->cur;
    advance(p);
    Stmt *s = new_stmt(ST_FOR, kw.line, kw.col);
    consume(p, T_LPAREN, "expected '(' after 'for'");

    if (!check(p, T_SEMI))
        s->as.for_.init = check(p, T_LET) ? parse_let(p) : parse_assign_or_expr(p);
    consume(p, T_SEMI, "expected ';' after loop initializer");

    if (!check(p, T_SEMI))
        s->as.for_.cond = parse_expr(p);
    consume(p, T_SEMI, "expected ';' after loop condition");

    if (!check(p, T_RPAREN))
        s->as.for_.step = parse_assign_or_expr(p);
    consume(p, T_RPAREN, "expected ')' after for clauses");

    s->as.for_.body = parse_block(p);
    return s;
}

static Stmt *parse_stmt(Parser *p) {
    Token t = p->cur;
    Stmt *s;

    switch (t.kind) {
    case T_LBRACE:
        return parse_block(p);
    case T_IF:
        return parse_if(p);
    case T_WHILE:
        return parse_while(p);
    case T_FOR:
        return parse_for(p);
    case T_LET:
        s = parse_let(p);
        consume(p, T_SEMI, "expected ';' after variable declaration");
        return s;
    case T_RETURN:
        advance(p);
        s = new_stmt(ST_RETURN, t.line, t.col);
        if (!check(p, T_SEMI))
            s->as.expr = parse_expr(p);
        consume(p, T_SEMI, "expected ';' after return");
        return s;
    case T_PRINT:
        advance(p);
        s = new_stmt(ST_PRINT, t.line, t.col);
        consume(p, T_LPAREN, "expected '(' after 'print'");
        s->as.expr = parse_expr(p);
        consume(p, T_RPAREN, "expected ')' after print argument");
        consume(p, T_SEMI, "expected ';' after print");
        return s;
    case T_BREAK:
    case T_CONTINUE:
        advance(p);
        s = new_stmt(t.kind == T_BREAK ? ST_BREAK : ST_CONTINUE, t.line, t.col);
        consume(p, T_SEMI, "expected ';' after loop control statement");
        return s;
    default:
        s = parse_assign_or_expr(p);
        consume(p, T_SEMI, "expected ';' after statement");
        return s;
    }
}

static Stmt *parse_block(Parser *p) {
    Token open = p->cur;
    Stmt *block = new_stmt(ST_BLOCK, open.line, open.col);
    if (!match(p, T_LBRACE)) {
        error_at(p, open.line, open.col, "expected '{'");
        return block;
    }

    Stmt *tail = NULL;
    while (!check(p, T_RBRACE) && !check(p, T_EOF) && !check(p, T_FN)) {
        long before = p->consumed;
        Stmt *s = parse_stmt(p);
        if (p->panic)
            synchronize(p);
        if (p->consumed == before)
            advance(p);

        if (tail)
            tail->next = s;
        else
            block->as.body = s;
        tail = s;
    }
    consume(p, T_RBRACE, "expected '}' to close block");
    return block;
}

/* ---- declarations ---- */

static Func *parse_func(Parser *p) {
    advance(p); /* fn */
    Func *f = arena_alloc(sizeof *f);
    f->ret = TY_VOID;

    Token name = consume_ident(p, "expected a function name after 'fn'");
    f->name = token_text(&name);
    f->line = name.line;
    f->col = name.col;

    consume(p, T_LPAREN, "expected '(' after function name");
    Param *tail = NULL;
    if (!check(p, T_RPAREN) && !p->panic) {
        do {
            Param *prm = arena_alloc(sizeof *prm);
            Token pn = consume_ident(p, "expected a parameter name");
            prm->name = token_text(&pn);
            prm->line = pn.line;
            prm->col = pn.col;
            consume(p, T_COLON, "expected ':' after parameter name");
            parse_type(p, &prm->type);

            if (tail)
                tail->next = prm;
            else
                f->params = prm;
            tail = prm;
            f->nparams++;
        } while (!p->panic && match(p, T_COMMA));
    }
    consume(p, T_RPAREN, "expected ')' after parameters");

    if (match(p, T_ARROW))
        parse_type(p, &f->ret);

    /* A broken header shouldn't hide errors in the body, so skip ahead to it. */
    if (p->panic) {
        while (!check(p, T_LBRACE) && !check(p, T_FN) && !check(p, T_EOF))
            advance(p);
        p->panic = 0;
    }
    f->body = parse_block(p);
    if (p->panic)
        synchronize(p);
    return f;
}

Program *parse_program(const char *source) {
    Parser p = {0};
    lexer_init(&p.lx, source);
    advance(&p);

    Program *prog = arena_alloc(sizeof *prog);
    Func *tail = NULL;

    while (!check(&p, T_EOF)) {
        if (!check(&p, T_FN)) {
            error_at(&p, p.cur.line, p.cur.col, "expected a function declaration");
            while (!check(&p, T_FN) && !check(&p, T_EOF))
                advance(&p);
            p.panic = 0;
            continue;
        }
        Func *f = parse_func(&p);
        if (tail)
            tail->next = f;
        else
            prog->funcs = f;
        tail = f;
    }
    return prog;
}
