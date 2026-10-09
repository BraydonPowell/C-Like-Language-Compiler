#ifndef DIAG_H
#define DIAG_H

#include <stdarg.h>

void diag_init(const char *filename, const char *source);

/* A line of 0 means the error has no useful source position. */
void diag_error(int line, int col, const char *fmt, ...);
void diag_verror(int line, int col, const char *fmt, va_list ap);

int diag_count(void);

#endif
