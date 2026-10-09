#include "check.h"
#include "codegen.h"
#include "diag.h"
#include "parser.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#define HOST_TARGET TARGET_MACOS
#else
#define HOST_TARGET TARGET_LINUX
#endif

static void usage(void) {
    fputs("usage: kestrel [-S] [-o output] [--target=macos|linux] file.kes\n"
          "  -S        write assembly instead of building an executable\n"
          "  -o FILE   output path (default: a.out, or <input>.s with -S)\n",
          stderr);
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        die("cannot open '%s'", path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc(size + 1);
    if (!buf)
        die("out of memory");
    if (fread(buf, 1, size, f) != (size_t)size)
        die("failed to read '%s'", path);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

static int run_cc(const char *asm_path, const char *exe_path) {
    pid_t pid = fork();
    if (pid < 0)
        die("fork failed");
    if (pid == 0) {
#ifdef __APPLE__
        execlp("cc", "cc", "-arch", "x86_64", "-o", exe_path, asm_path, (char *)NULL);
#else
        execlp("cc", "cc", "-o", exe_path, asm_path, (char *)NULL);
#endif
        perror("kestrel: cc");
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void report_errors(void) {
    int n = diag_count();
    fprintf(stderr, "%d error%s generated.\n", n, n == 1 ? "" : "s");
}

int main(int argc, char **argv) {
    const char *input = NULL, *output = NULL;
    int asm_only = 0, target_given = 0;
    Target target = HOST_TARGET;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-S") == 0) {
            asm_only = 1;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(argv[i], "--target=macos") == 0) {
            target = TARGET_MACOS;
            target_given = 1;
        } else if (strcmp(argv[i], "--target=linux") == 0) {
            target = TARGET_LINUX;
            target_given = 1;
        } else if (argv[i][0] == '-' || input) {
            usage();
            return 2;
        } else {
            input = argv[i];
        }
    }
    if (!input) {
        usage();
        return 2;
    }
    if (target_given && target != HOST_TARGET && !asm_only)
        die("--target differs from this machine; use -S to emit assembly only");

    char *source = read_file(input);
    diag_init(input, source);

    Program *prog = parse_program(source);
    if (diag_count() > 0) {
        report_errors();
        return 1;
    }
    if (!check_program(prog)) {
        report_errors();
        return 1;
    }

    char asm_path[4096];
    if (asm_only)
        snprintf(asm_path, sizeof asm_path, "%s", output ? output : "a.s");
    else
        snprintf(asm_path, sizeof asm_path, "%s.s", output ? output : "a.out");

    FILE *out = fopen(asm_path, "w");
    if (!out)
        die("cannot write '%s'", asm_path);
    codegen_program(out, prog, target);
    fclose(out);

    int ok = 1;
    if (!asm_only) {
        ok = run_cc(asm_path, output ? output : "a.out");
        remove(asm_path);
        if (!ok)
            fprintf(stderr, "kestrel: assembling/linking failed\n");
    }

    arena_free_all();
    free(source);
    return ok ? 0 : 1;
}
