# PL/I to C89 translator, portable C89.
CC      ?= cc
CFLAGS  ?= -O2 -std=c89 -pedantic -Wall -Wextra
LIBS    = -lm
PARTS   = src/p00_defs.h src/p10_util.c src/p20_lex.c src/p30_parse.c \
          src/p40_sem.c src/p50_expr.c src/p55_bi.c src/p60_stmt.c \
          src/p65_io.c src/p70_main.c

all: pli

pli: pli_all.c
	$(CC) $(CFLAGS) -o $@ pli_all.c $(LIBS)

# the single-file form is generated from src/ (this is what to ship to
# platforms that want one source file, e.g. VM/370 CMS)
pli_all.c: $(PARTS) tools/amalg.sh
	sh tools/amalg.sh > pli_all.c

test: pli
	PLI=./pli sh tests/run.sh

lines:
	sh tools/linelen.sh src/*.c src/*.h tools/*.sh tests/*.pli \
	   examples/*.pli pli_all.c

clean:
	rm -f pli pli_all.c

.PHONY: all test lines clean
