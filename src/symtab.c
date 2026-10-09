#include "symtab.h"

#include "util.h"

#include <string.h>

void symtab_init(SymTab *st) {
    st->current = NULL;
    symtab_enter(st);
}

void symtab_enter(SymTab *st) {
    Scope *s = arena_alloc(sizeof *s);
    s->parent = st->current;
    st->current = s;
}

void symtab_leave(SymTab *st) {
    st->current = st->current->parent;
}

Symbol *symtab_lookup_local(const SymTab *st, const char *name) {
    for (Symbol *s = st->current->syms; s; s = s->next)
        if (strcmp(s->name, name) == 0)
            return s;
    return NULL;
}

Symbol *symtab_lookup(const SymTab *st, const char *name) {
    for (Scope *sc = st->current; sc; sc = sc->parent)
        for (Symbol *s = sc->syms; s; s = s->next)
            if (strcmp(s->name, name) == 0)
                return s;
    return NULL;
}

Symbol *symtab_declare(SymTab *st, const char *name, SymKind kind, Type type, int line) {
    if (symtab_lookup_local(st, name))
        return NULL;
    Symbol *s = arena_alloc(sizeof *s);
    s->name = arena_strndup(name, strlen(name));
    s->kind = kind;
    s->type = type;
    s->line = line;
    s->next = st->current->syms;
    st->current->syms = s;
    return s;
}
