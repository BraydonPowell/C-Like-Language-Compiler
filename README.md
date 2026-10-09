# Kestrel

A small compiler written in C. It takes a statically typed, C-like language and
produces x86-64 assembly, then hands it to the system toolchain to build an
executable.

```
fn gcd(a: int, b: int) -> int {
    while (b != 0) {
        let t = b;
        b = a % b;
        a = t;
    }
    return a;
}

fn main() -> int {
    print(gcd(48, 18));   // 6
    return 0;
}
```

## Building and running

```sh
make
./kestrel examples/fib.kes -o fib && ./fib
./kestrel -S examples/fib.kes -o fib.s      # inspect the assembly
make test
```



## The language

- Types: `int` (64-bit), `bool`, and `void` for functions that return nothing.
- Declarations: `let x = 1;` or `let x: int = 1;`, block scoped, shadowing allowed.
- Control flow: `if`/`else if`/`else`, `while`, `for (init; cond; step)`,
  `break`, `continue`, `return`.
- Operators: `+ - * / %`, `< <= > >=`, `== !=`, `&& || !`, unary `-`.
  `&&` and `||` short-circuit.
- Functions: `fn name(a: int, b: bool) -> int { ... }`, up to six parameters,
  recursion and forward references both work. The return type may be omitted
  for `void`.
- `print(expr);` writes an `int` or `bool` followed by a newline.
- Every program needs `fn main() -> int`; its result becomes the exit code.

## How it works

| Stage | File | Notes |
| --- | --- | --- |
| Lexer | `src/lexer.c` | Hand-written scanner, tracks line and column |
| Parser | `src/parser.c` | Recursive descent; precedence climbing for binary operators |
| Symbol table | `src/symtab.c` | Chain of scopes, one per block |
| Checker | `src/check.c` | Name resolution, type checking, stack slot assignment, return-path analysis |
| Code generator | `src/codegen.c` | Stack-machine style x86-64, System V calling convention |
| Diagnostics | `src/diag.c` | `file:line:col` messages with the offending source line |

The parser recovers from errors by skipping to the next statement boundary, and
the checker keeps going after a type error, so one run reports as many problems
as it can:

```
bad.kes:3:16: error: operator '+' expects int operands, got int and bool
    3 |     let ok = 1 + false;
      |                ^
bad.kes:5:11: error: undefined variable 'y'
    5 |     print(y);
      |           ^
2 errors generated.
```

Code generation is deliberately simple: every expression leaves its result in
`%rax`, and intermediate values go through the stack. The generator tracks how
many pushes are outstanding so the stack is 16-byte aligned at every `call`.
There is no optimization pass.

## Tests

`make test` compiles and runs each program in `tests/run` and compares the
output against its `.out` file. Each file in `tests/errors` must produce
exactly the diagnostics listed in its `.err` file.
