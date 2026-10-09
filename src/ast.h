#ifndef AST_H
#define AST_H

#include "lexer.h"

#include <stdint.h>

typedef enum { TY_VOID, TY_INT, TY_BOOL, TY_ERROR } Type;

const char *type_name(Type t);

struct Symbol;

typedef enum { EX_INT, EX_BOOL, EX_VAR, EX_UNARY, EX_BINARY, EX_CALL } ExprKind;

typedef struct Expr Expr;
struct Expr {
    ExprKind kind;
    int line, col; /* binary expressions point at the operator */
    Type type;     /* filled in by the checker */
    Expr *next;    /* next argument when this expr sits in a call */
    union {
        int64_t value;
        struct { char *name; struct Symbol *sym; } var;
        struct { TokenKind op; Expr *operand; } unary;
        struct { TokenKind op; Expr *lhs, *rhs; } binary;
        struct { char *name; struct Symbol *sym; Expr *args; int nargs; } call;
    } as;
};

typedef enum {
    ST_LET, ST_ASSIGN, ST_IF, ST_WHILE, ST_FOR, ST_RETURN,
    ST_PRINT, ST_EXPR, ST_BLOCK, ST_BREAK, ST_CONTINUE
} StmtKind;

typedef struct Stmt Stmt;
struct Stmt {
    StmtKind kind;
    int line, col;
    Stmt *next;
    union {
        struct { char *name; int has_type; Type type; Expr *init; struct Symbol *sym; } let;
        struct { char *name; Expr *value; struct Symbol *sym; } assign;
        struct { Expr *cond; Stmt *then_body; Stmt *else_body; } if_;
        struct { Expr *cond; Stmt *body; } while_;
        struct { Stmt *init; Expr *cond; Stmt *step; Stmt *body; } for_;
        Expr *expr; /* return (may be NULL), print, expression statement */
        Stmt *body; /* block */
    } as;
};

typedef struct Param {
    char *name;
    Type type;
    int line, col;
    struct Symbol *sym;
    struct Param *next;
} Param;

typedef struct Func {
    char *name;
    Param *params;
    int nparams;
    Type ret;
    Stmt *body;
    int line, col;
    int frame_size; /* bytes of locals, rounded to 16 */
    struct Func *next;
} Func;

typedef struct {
    Func *funcs;
} Program;

#endif
