#ifndef LEXER_H
#define LEXER_H

#include <stdint.h>

typedef enum {
    T_EOF,
    T_INT_LIT,
    T_IDENT,

    T_FN, T_LET, T_IF, T_ELSE, T_WHILE, T_FOR, T_RETURN, T_PRINT,
    T_BREAK, T_CONTINUE, T_TRUE, T_FALSE,
    T_KW_INT, T_KW_BOOL, T_KW_VOID,

    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE,
    T_COMMA, T_SEMI, T_COLON, T_ARROW,

    T_ASSIGN,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE,
    T_AND, T_OR, T_NOT
} TokenKind;

typedef struct {
    TokenKind kind;
    int line, col;
    const char *start;
    int len;
    int64_t value; /* T_INT_LIT only */
} Token;

typedef struct {
    const char *src;
    int pos;
    int line, col;
} Lexer;

void lexer_init(Lexer *lx, const char *src);
Token lexer_next(Lexer *lx);

/* Printable name for diagnostics, quoted where it makes sense: "';'", "'while'". */
const char *token_kind_name(TokenKind kind);

#endif
