#include "diag.h"

#include <stdio.h>
#include <string.h>

static const char *file_name;
static const char *source;
static int error_count;

void diag_init(const char *filename, const char *src) {
    file_name = filename;
    source = src;
    error_count = 0;
}

int diag_count(void) {
    return error_count;
}

static void print_source_line(int line, int col) {
    const char *s = source;
    for (int i = 1; i < line; i++) {
        s = strchr(s, '\n');
        if (!s)
            return;
        s++;
    }
    const char *end = strchr(s, '\n');
    int len = end ? (int)(end - s) : (int)strlen(s);
    if (len > 0 && s[len - 1] == '\r')
        len--;

    fprintf(stderr, "%5d | %.*s\n", line, len, s);
    fprintf(stderr, "      | ");
    for (int i = 1; i < col && i <= len; i++)
        fputc(s[i - 1] == '\t' ? '\t' : ' ', stderr);
    fputs("^\n", stderr);
}

void diag_verror(int line, int col, const char *fmt, va_list ap) {
    error_count++;
    if (line > 0)
        fprintf(stderr, "%s:%d:%d: error: ", file_name, line, col);
    else
        fprintf(stderr, "%s: error: ", file_name);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    if (line > 0)
        print_source_line(line, col);
}

void diag_error(int line, int col, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    diag_verror(line, col, fmt, ap);
    va_end(ap);
}
