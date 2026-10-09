#ifndef PARSER_H
#define PARSER_H

#include "ast.h"

/* Parses the whole source. Syntax errors are reported through diag; callers
 * should check diag_count() before using the result. */
Program *parse_program(const char *source);

#endif
