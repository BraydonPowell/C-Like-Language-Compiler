#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

#include <stdio.h>

typedef enum { TARGET_MACOS, TARGET_LINUX } Target;

/* Emits x86-64 assembly (AT&T syntax) for a program that passed the checker. */
void codegen_program(FILE *out, const Program *prog, Target target);

#endif
