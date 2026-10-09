#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* Bump allocator. Everything the compiler builds lives until arena_free_all(). */
void *arena_alloc(size_t size);
char *arena_strndup(const char *s, size_t n);
void arena_free_all(void);

void die(const char *fmt, ...);

#endif
