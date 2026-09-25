SHELL := /bin/sh

CFLAGS := -std=c17 -Wall -Wextra -Wpedantic
DEBUG_CFLAGS := ${CFLAGS} -g
CC := gcc
CLANG_FORMAT := clang-format

gg: editor.c
	${CC} ${CFLAGS} editor.c -o gg

.PHONY: debug clean format format-check test
debug: editor.c
	${CC} ${DEBUG_CFLAGS} editor.c -o gg-debug

clean:
	rm -f gg gg-debug tests

format:
	${CLANG_FORMAT} -i editor.c tests.c

format-check:
	${CLANG_FORMAT} --dry-run --Werror editor.c tests.c

test: tests
	./tests

tests: tests.c editor.c
	${CC} ${CFLAGS} tests.c -o tests
