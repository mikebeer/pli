/* pli -- a PL/I to C89 translator.  Portable C89 for VM/370 CMS.
 * All source lines are kept within 71 columns.
 * This file holds the common definitions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAXD 7
#define VERSION "1.0"

typedef struct Node Node;
typedef struct Sym Sym;
typedef struct Scope Scope;
typedef struct Attr Attr;
typedef struct Fn Fn;

/* string builder */
typedef struct { char *s; long n, cap; } Sb;

/* tokens */
enum { T_EOF, T_ID, T_NUM, T_STR, T_OP };
typedef struct {
    int k;
    char *s;
    double dv;
    int fl;             /* STR: 1 bit, NUM: 1 float */
    int q;              /* NUM: decimals / mantissa digits */
    int line;
    char *file;
} Tok;

typedef struct { Tok *a; long n, cap; } TV;

/* node kinds */
enum {
    X_NUM = 1, X_STR, X_REF, X_BIN, X_UN, X_STAR,
    S_ASSIGN = 20, S_IF, S_DO, S_BEGIN, S_PROC, S_CALL, S_RETURN,
    S_GOTO, S_LEAVE, S_ITER, S_SELECT, S_WHEN, S_STOP, S_NULL,
    S_DECL, S_PUT, S_GET, S_OPEN, S_CLOSE, S_READ, S_WRITE,
    S_ALLOC, S_FREE, S_ON, S_REVERT, S_SIGNAL, S_DISPLAY,
    S_END, S_REWRITE, S_DELETE,
    N_SPEC = 60, N_ITEM, N_DO, N_FMT, N_BND, N_OPT
};

struct Attr {
    int ty;             /* 0 none, else TY_* */
    int fix;            /* 1 FIXED, 2 FLOAT */
    int base;           /* 1 BINARY, 2 DECIMAL */
    int p, q;
    int len, vary;
    int stor;           /* ST_* */
    int ext, isent, isfile, rec, prt, mode, keyed;
    Node *based, *init;
    char *like;
    Attr *ret;
    char *xname;        /* EXTERNAL('name') */
    int copt, hasdsc;   /* OPTIONS(C); ENTRY has a parameter list */
    Attr *dsc, *dnext;  /* ENTRY parameter descriptions */
    Node *dims;         /* bounds of a described parameter */
};

struct Node {
    int k, line;
    char *s;
    double dv;
    int n, fl;
    Node *a, *b, *c, *d, *e;
    Node *next;
    Node *lab;
    Attr *at;
    Scope *sc;
    int pre;            /* condition prefix bits */
    Sym *sym;
};

enum { TY_NONE, TY_FXB, TY_FXD, TY_FLT, TY_CHR, TY_BIT, TY_PTR,
       TY_FILE, TY_ENT, TY_LBL, TY_STRUCT, TY_VOID };
enum { ST_NONE, ST_AUTO, ST_STAT, ST_BASED, ST_CTL, ST_PARAM };
enum { C_VAR, C_PROC, C_BUILTIN, C_LABEL };

struct Sym {
    char *name, *cn, *tn, *pcn;
    int cls, stor, level, ty, p, q, len, vary;
    int nd;
    long lo[MAXD], hi[MAXD];
    int star;           /* unknown extent */
    Sym *kid, *nxt, *par, *link, *last;
    Node *init, *based, *pn;
    Sym *hptr;          /* hidden pointer for CONTROLLED */
    Sym *params;
    Fn *owner;
    Scope *sc;
    int isparam, isref, rec, id, used, isfun, ext, isin;
    char *pdecl;        /* C declaration of the parameter */
    char *lbv, *hbv;    /* run-time bounds of a (*) parameter */
    int ispv;           /* parameter held as PV */
    int hasdim;
    int lblfn;
    int fprt, frec, fmode, fkeyed;
    char *xname;        /* external C name given by EXTERNAL('x') */
    int copt;           /* OPTIONS(C): C calling convention */
    int isextp;         /* external procedure (module mode) */
    int isextv;         /* reference to an external variable */
    int protodone;      /* prototype written */
    int hasdsc;         /* ENTRY with a parameter list */
    char *cpar;         /* C parameter of an OPTIONS(C) procedure */
};

struct Scope {
    Scope *par;
    Sym *syms;
    Fn *fn;
    Node *n;
};

struct Fn {
    char *name;
    Sb decl, body, pro, hdr, ep;
    int rec, id, depth, ison;
    int jb;             /* needs setjmp */
    int ext;            /* exported (module mode) */
    Sym *ps;            /* procedure symbol */
    Node *node;
    Fn *par;
    int nlg;            /* non-local goto labels */
    Sym *lg[32];
    int lgid[32];
    int nloop;
};

/* condition-prefix bits */
#define PRE_SUBS 1
#define PRE_NOCHK 2
