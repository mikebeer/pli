# pli -- PL/I to C89 translator

A translator from PL/I to ANSI C (C89), written in C89. It builds with
GCC on VM/370 CMS (VM/370plus) and with any C compiler on Linux. All
source lines are 71 columns or shorter.

    make            # builds ./pli
    make test       # runs tests/*.pli
    ./pli examples/sieve.pli -o sieve.c
    cc -std=c89 -Isrc -o sieve sieve.c -lm && ./sieve

Options: `-o file`, `-m l,r` (source margins), `-s` (subscript checks).

For CMS, compile `pli_all.c` (made by `tools/amalg.sh`) and keep
`plirt.h` on an accessed disk. The CMS build is not yet tested.

It implements a large subset of PL/I with more than PL/I (F): SELECT,
LEAVE/ITERATE, DO UNTIL, TRIM/REVERSE/UPPERCASE/LOWERCASE, %REPLACE
and free-format source. It lacks COMPLEX, PICTURE, DEFINED, AREA,
GENERIC, multitasking and the full preprocessor.

The manual is `docs/PLI-User-Guide.docx` (build: `sh docs/make_manual.sh`).
