#ifndef CHECK_H
#define CHECK_H

#include "ast.h"

/* Resolves names, assigns stack slots and type-checks the program.
 * Returns 1 if no semantic errors were found. */
int check_program(Program *prog);

#endif
