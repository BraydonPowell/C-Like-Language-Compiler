#ifndef SYMTAB_H
#define SYMTAB_H

#include "ast.h"

typedef enum { SYM_VAR, SYM_FUNC } SymKind;

typedef struct Symbol {
    char *name;
    SymKind kind;
    Type type;          /* variable type, or return type for functions */
    int line;           /* where it was declared */
    int offset;         /* variables: rbp-relative stack slot */
    struct Func *func;  /* functions: the declaration, for parameter checks */
    struct Symbol *next;
} Symbol;

typedef struct Scope {
    Symbol *syms;
    struct Scope *parent;
} Scope;

typedef struct {
    Scope *current;
} SymTab;

void symtab_init(SymTab *st);
void symtab_enter(SymTab *st);
void symtab_leave(SymTab *st);

/* Returns NULL if the name is already bound in the innermost scope. */
Symbol *symtab_declare(SymTab *st, const char *name, SymKind kind, Type type, int line);
Symbol *symtab_lookup(const SymTab *st, const char *name);
Symbol *symtab_lookup_local(const SymTab *st, const char *name);

#endif
