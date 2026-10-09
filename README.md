# Kestrel

A compiler I wrote in C for a tiny C-like language I also made up. It turns your code into x86-64 assembly, then lets `cc` do the boring part.

```
fn fib(n: int) -> int {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}

fn main() -> int {
    print(fib(10));   // 55
    return 0;
}
```

## Run it

```sh
make
./kestrel examples/fib.kes -o fib && ./fib
./kestrel -S examples/fib.kes -o fib.s   # just the assembly
make test
```

Output is x86-64, so on an M-series Mac it runs through Rosetta. Yes, really.

## What's in the language

`int`, `bool`, functions, recursion, `let`, `if`/`else`, `while`, `for`, `break`, `continue`, `print`. No strings, no arrays, no regrets.

## What's under the hood

Hand-written lexer, recursive descent parser, symbol table with scopes, a type checker, and a code generator that keeps everything on the stack because registers are hard.

Mess up and it tells you where, with line numbers and a little arrow:

```
bad.kes:3:16: error: operator '+' expects int operands, got int and bool
    3 |     let ok = 1 + false;
      |                ^
```

It keeps going after the first error, so you get to feel bad about all of them at once.

## Tests

`make test` runs everything in `tests/`. Programs have to print the right thing, and the broken ones have to fail in exactly the right way.
