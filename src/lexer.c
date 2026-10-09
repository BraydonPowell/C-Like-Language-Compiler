#include "lexer.h"

#include "diag.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    const char *text;
    TokenKind kind;
} keywords[] = {
    {"fn", T_FN},       {"let", T_LET},       {"if", T_IF},
    {"else", T_ELSE},   {"while", T_WHILE},   {"for", T_FOR},
    {"return", T_RETURN}, {"print", T_PRINT}, {"break", T_BREAK},
    {"continue", T_CONTINUE}, {"true", T_TRUE}, {"false", T_FALSE},
    {"int", T_KW_INT},  {"bool", T_KW_BOOL},  {"void", T_KW_VOID},
};

void lexer_init(Lexer *lx, const char *src) {
    lx->src = src;
    lx->pos = 0;
    lx->line = 1;
    lx->col = 1;
}

static char peek(const Lexer *lx) {
    return lx->src[lx->pos];
}

static char peek_next(const Lexer *lx) {
    return lx->src[lx->pos] ? lx->src[lx->pos + 1] : '\0';
}

static char advance(Lexer *lx) {
    char c = lx->src[lx->pos++];
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    return c;
}

static void skip_trivia(Lexer *lx) {
    for (;;) {
        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lx);
        } else if (c == '/' && peek_next(lx) == '/') {
            while (peek(lx) && peek(lx) != '\n')
                advance(lx);
        } else if (c == '/' && peek_next(lx) == '*') {
            int line = lx->line, col = lx->col;
            advance(lx);
            advance(lx);
            while (peek(lx) && !(peek(lx) == '*' && peek_next(lx) == '/'))
                advance(lx);
            if (!peek(lx)) {
                diag_error(line, col, "unterminated block comment");
            } else {
                advance(lx);
                advance(lx);
            }
        } else {
            return;
        }
    }
}

static Token make_token(Lexer *lx, TokenKind kind, int line, int col, int start) {
    Token t;
    t.kind = kind;
    t.line = line;
    t.col = col;
    t.start = lx->src + start;
    t.len = lx->pos - start;
    t.value = 0;
    return t;
}

static Token lex_number(Lexer *lx, int line, int col, int start) {
    while (isdigit((unsigned char)peek(lx)))
        advance(lx);
    if (isalpha((unsigned char)peek(lx)) || peek(lx) == '_') {
        diag_error(lx->line, lx->col, "invalid character '%c' in number", peek(lx));
        while (isalnum((unsigned char)peek(lx)) || peek(lx) == '_')
            advance(lx);
    }

    Token t = make_token(lx, T_INT_LIT, line, col, start);
    char buf[32];
    if (t.len >= (int)sizeof buf) {
        diag_error(line, col, "integer literal is too large");
        return t;
    }
    memcpy(buf, t.start, t.len);
    buf[t.len] = '\0';
    errno = 0;
    long long v = strtoll(buf, NULL, 10);
    if (errno == ERANGE)
        diag_error(line, col, "integer literal is too large");
    else
        t.value = v;
    return t;
}

static Token lex_word(Lexer *lx, int line, int col, int start) {
    while (isalnum((unsigned char)peek(lx)) || peek(lx) == '_')
        advance(lx);
    Token t = make_token(lx, T_IDENT, line, col, start);
    for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
        if ((int)strlen(keywords[i].text) == t.len &&
            memcmp(keywords[i].text, t.start, t.len) == 0) {
            t.kind = keywords[i].kind;
            break;
        }
    }
    return t;
}

Token lexer_next(Lexer *lx) {
    for (;;) {
        skip_trivia(lx);
        int line = lx->line, col = lx->col, start = lx->pos;
        char c = peek(lx);

        if (!c)
            return make_token(lx, T_EOF, line, col, start);
        if (isdigit((unsigned char)c))
            return lex_number(lx, line, col, start);
        if (isalpha((unsigned char)c) || c == '_')
            return lex_word(lx, line, col, start);

        advance(lx);
        char next = peek(lx);
        TokenKind kind;
        switch (c) {
        case '(': kind = T_LPAREN; break;
        case ')': kind = T_RPAREN; break;
        case '{': kind = T_LBRACE; break;
        case '}': kind = T_RBRACE; break;
        case ',': kind = T_COMMA; break;
        case ';': kind = T_SEMI; break;
        case ':': kind = T_COLON; break;
        case '+': kind = T_PLUS; break;
        case '*': kind = T_STAR; break;
        case '/': kind = T_SLASH; break;
        case '%': kind = T_PERCENT; break;
        case '-':
            if (next == '>') { advance(lx); kind = T_ARROW; }
            else kind = T_MINUS;
            break;
        case '=':
            if (next == '=') { advance(lx); kind = T_EQ; }
            else kind = T_ASSIGN;
            break;
        case '!':
            if (next == '=') { advance(lx); kind = T_NE; }
            else kind = T_NOT;
            break;
        case '<':
            if (next == '=') { advance(lx); kind = T_LE; }
            else kind = T_LT;
            break;
        case '>':
            if (next == '=') { advance(lx); kind = T_GE; }
            else kind = T_GT;
            break;
        case '&':
            if (next == '&') { advance(lx); kind = T_AND; break; }
            diag_error(line, col, "unexpected character '&' (did you mean '&&'?)");
            continue;
        case '|':
            if (next == '|') { advance(lx); kind = T_OR; break; }
            diag_error(line, col, "unexpected character '|' (did you mean '||'?)");
            continue;
        default:
            if (isprint((unsigned char)c))
                diag_error(line, col, "unexpected character '%c'", c);
            else
                diag_error(line, col, "unexpected character 0x%02x", (unsigned char)c);
            continue;
        }
        return make_token(lx, kind, line, col, start);
    }
}

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
    case T_EOF: return "end of file";
    case T_INT_LIT: return "number";
    case T_IDENT: return "identifier";
    case T_FN: return "'fn'";
    case T_LET: return "'let'";
    case T_IF: return "'if'";
    case T_ELSE: return "'else'";
    case T_WHILE: return "'while'";
    case T_FOR: return "'for'";
    case T_RETURN: return "'return'";
    case T_PRINT: return "'print'";
    case T_BREAK: return "'break'";
    case T_CONTINUE: return "'continue'";
    case T_TRUE: return "'true'";
    case T_FALSE: return "'false'";
    case T_KW_INT: return "'int'";
    case T_KW_BOOL: return "'bool'";
    case T_KW_VOID: return "'void'";
    case T_LPAREN: return "'('";
    case T_RPAREN: return "')'";
    case T_LBRACE: return "'{'";
    case T_RBRACE: return "'}'";
    case T_COMMA: return "','";
    case T_SEMI: return "';'";
    case T_COLON: return "':'";
    case T_ARROW: return "'->'";
    case T_ASSIGN: return "'='";
    case T_PLUS: return "'+'";
    case T_MINUS: return "'-'";
    case T_STAR: return "'*'";
    case T_SLASH: return "'/'";
    case T_PERCENT: return "'%'";
    case T_EQ: return "'=='";
    case T_NE: return "'!='";
    case T_LT: return "'<'";
    case T_LE: return "'<='";
    case T_GT: return "'>'";
    case T_GE: return "'>='";
    case T_AND: return "'&&'";
    case T_OR: return "'||'";
    case T_NOT: return "'!'";
    }
    return "?";
}
