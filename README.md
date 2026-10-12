# pli -- PL/I to C89 translator

A translator from PL/I to ANSI C (C89), written in C89. It builds with
GCC on VM/370 CMS (VM/370plus) and with any C compiler on Linux. All
source lines are 71 columns or shorter.

    make            # builds ./pli
    make test       # runs tests/*.pli
    ./pli examples/sieve.pli -o sieve.c
    cc -std=c89 -Isrc -o sieve sieve.c -lm && ./sieve

`pli -c prog.pli` also builds the program: it uses gcc/cc/clang/tcc if
found, else the bundled TinyCC (Windows zips; `-cc name|bundled`).

Several source files make one program: `pli -c main.pli util.pli`
(ENTRY declarations and EXTERNAL variables work across files).

A source name without a type gets `.pli` (`pli matrix` = matrix.pli;
on CMS `matrix` = MATRIX PLI).

Separate compilation: `pli -M -c main.pli util.pli` translates and
compiles every file as a module and links them; `-C` stops at object
files, `-n name` sets the module name, `-with a,b` names the other
modules. C interop: `ENTRY ... OPTIONS(C)` calls C functions (numbers
by value, strings as char*); a procedure with `OPTIONS(C)` can be
called from C, see examples/cinterop (C main: plstart, <mod>_i,
plstop). See the manual, chapter 4.

Options: `-o file`, `-m l,r` (source margins), `-s` (subscript checks).

Windows (32 and 64 bit): `make win` cross-builds `pli-i686.exe` and
`pli-x86_64.exe` with MinGW-w64; `tools/wintest.sh ARCH` runs the tests
in Wine. Any Windows C compiler (MinGW, MSVC) compiles the output.
Linux: any distribution with a C compiler (`make install`); `make dist`
also builds a static x86_64 binary, an rpm and a deb. The spec file
`pli.spec` builds from source on RPM distributions.

For CMS, compile `pli_all.c` (made by `tools/amalg.sh`) and keep
`plirt.h` on an accessed disk. The CMS build is not yet tested.

It implements a large subset of PL/I with more than PL/I (F): SELECT,
LEAVE/ITERATE, DO UNTIL, TRIM/REVERSE/UPPERCASE/LOWERCASE, COMPLEX,
PICTURE, DEFINED, AREA, keyed record files, the full preprocessor
(%IF, %DO, %PROCEDURE, -D) and free-format source. From Enterprise
PL/I it has DEFINE STRUCTURE/ALIAS/ORDINAL, TYPE, HANDLE, UNSIGNED,
ASSERT and exact 64-bit integers. It lacks GENERIC, multitasking,
VSAM and WIDECHAR.

The manual is `docs/PLI-User-Guide.docx` (build: `sh docs/make_manual.sh`).
