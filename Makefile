SHELL := /bin/sh

CFLAGS := -std=c17 -Wall -Wextra -Wpedantic
DEBUG_CFLAGS := ${CFLAGS} -g
CC := gcc

gg: editor.c
	${CC} ${CFLAGS} editor.c -o gg

.PHONY: debug clean
debug: editor.c
	${CC} ${DEBUG_CFLAGS} editor.c -o gg-debug

clean:
	rm -f gg gg-debug
