CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
CFLAGS  += -D_POSIX_C_SOURCE=200809L

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o)

kestrel: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

build/%.o: src/%.c $(wildcard src/*.h) | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

test: kestrel
	./tests/run_tests.sh

clean:
	rm -rf build kestrel

.PHONY: test clean
