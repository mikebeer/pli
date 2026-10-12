# PL/I to C89 translator, portable C89.
CC      ?= cc
CFLAGS  ?= -O2 -std=c89 -pedantic -Wall -Wextra
LIBS    = -lm
PARTS   = src/p00_defs.h src/p10_util.c src/p20_lex.c src/p22_pp.c \
         src/p30_parse.c \
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
	rm -rf dist

PREFIX  ?= /usr/local
VERSION  = 1.0

install: pli
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/pli
	cp pli $(DESTDIR)$(PREFIX)/bin/
	cp src/plirt.h $(DESTDIR)$(PREFIX)/share/pli/
	cp -r examples $(DESTDIR)$(PREFIX)/share/pli/

# Windows (needs MinGW-w64: i686-w64-mingw32-gcc, x86_64-w64-mingw32-gcc)
win: pli_all.c
	mkdir -p dist
	i686-w64-mingw32-gcc -O2 -std=c89 -o dist/pli-i686.exe \
	  pli_all.c -lm -static
	x86_64-w64-mingw32-gcc -O2 -std=c89 -o dist/pli-x86_64.exe \
	  pli_all.c -lm -static

# release packages in dist/: Windows zips, Linux tar.gz, rpm, deb
dist: all win
	sh tools/package.sh $(VERSION)

.PHONY: all test lines clean install win dist
