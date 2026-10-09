#!/bin/sh
# Runs every program under tests/run and compares its output, then checks that
# each file under tests/errors produces exactly the diagnostics in its .err file.

cd "$(dirname "$0")/.." || exit 1
bin=./kestrel
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
pass=0
fail=0

report() {
    if [ "$1" = ok ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL: $2"
    fi
}

for src in tests/run/*.kes; do
    name=$(basename "$src" .kes)
    if $bin "$src" -o "$tmp/$name" 2>"$tmp/$name.log"; then
        "$tmp/$name" >"$tmp/$name.out"
        if diff -u "tests/run/$name.out" "$tmp/$name.out" >"$tmp/$name.diff"; then
            report ok
        else
            report bad "$src (output differs)"
            cat "$tmp/$name.diff"
        fi
    else
        report bad "$src (did not compile)"
        cat "$tmp/$name.log"
    fi
done

for src in tests/errors/*.kes; do
    name=$(basename "$src" .kes)
    if $bin "$src" -S -o "$tmp/$name.s" 2>"$tmp/$name.log"; then
        report bad "$src (expected errors, but it compiled)"
        continue
    fi
    grep 'error:' "$tmp/$name.log" | sed 's|^.*/||' >"$tmp/$name.got"
    if diff -u "tests/errors/$name.err" "$tmp/$name.got" >"$tmp/$name.diff"; then
        report ok
    else
        report bad "$src (diagnostics differ)"
        cat "$tmp/$name.diff"
    fi
done

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
