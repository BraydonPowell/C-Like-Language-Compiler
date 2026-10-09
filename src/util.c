#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK_SIZE (64 * 1024)

typedef struct Chunk {
    struct Chunk *next;
    size_t used;
    size_t cap;
    char data[];
} Chunk;

static Chunk *chunks;

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "kestrel: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

void *arena_alloc(size_t size) {
    size = (size + 7) & ~(size_t)7;
    if (!chunks || chunks->used + size > chunks->cap) {
        size_t cap = size > CHUNK_SIZE ? size : CHUNK_SIZE;
        Chunk *c = malloc(sizeof *c + cap);
        if (!c)
            die("out of memory");
        c->next = chunks;
        c->used = 0;
        c->cap = cap;
        chunks = c;
    }
    void *p = chunks->data + chunks->used;
    chunks->used += size;
    return memset(p, 0, size);
}

char *arena_strndup(const char *s, size_t n) {
    char *p = arena_alloc(n + 1);
    memcpy(p, s, n);
    return p;
}

void arena_free_all(void) {
    while (chunks) {
        Chunk *next = chunks->next;
        free(chunks);
        chunks = next;
    }
}
