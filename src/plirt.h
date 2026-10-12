/* plirt.h -- run-time support for PL/I programs translated by pli.
 *
 * Portable C89.  Everything is static, so a translated program is a
 * single C file plus this header and needs no separate link step.
 * Source lines are kept to 71 columns for VM/370 CMS.
 */
#ifndef PLIRT_H
#define PLIRT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <setjmp.h>

/* ---- string values and variables ---- */
typedef struct { const char *p; long n; } PS;       /* value    */
typedef struct { char *p; long *np; long max; } PV; /* variable */

/* ---- conditions ---- */
#define PLC_ERR  1
#define PLC_ZDIV 2
#define PLC_OFL  3
#define PLC_UFL  4
#define PLC_SIZE 5
#define PLC_CONV 6
#define PLC_SUBS 7
#define PLC_STR  8
#define PLC_FIN  9
#define PLC_KEY  10
#define PLC_TRN  11
#define PLC_PAGE 12
#define PLC_N    13

static void (*pl_hd[PLC_N])(void);
static int pl_hb[PLC_N];
static long pl_oncode = 0;
static int pl_gtsel = 0;
static int pl_argc = 0;
static char **pl_argv = 0;

/* ---- files ---- */
typedef struct PLF {
    FILE *fp;
    const char *dd;
    int mode;           /* 0 closed, 1 input, 2 output, 3 update */
    int prt, rec, mem, eofs;
    long col, line, lsz, psz;
    void (*eof)(void);
    int eb;
    char *lb; long ln, lcap, lp; int lv;
    char *mb; long mn, mcap, mp;
    const char *fs, *fc;
    int xs;
} PLF;

#define PLF_IN  1
#define PLF_OUT 2
#define PLF_UPD 3
#define PLF_PRT 4
#define PLF_REC 8

static PLF pl_sysin;
static PLF pl_sysprt;
static PLF *pl_fl[64];
static int pl_nfl = 0;

/* ---- temporary string arena ---- */
typedef struct plch {
    struct plch *pv;
    long u, cap;
    double d[1];
} plch;
typedef struct { plch *c; long u; } PLM;
static plch *pl_ar = 0;

static void pl_stop(int rc);
static void pl_sig(int c, const char *m);

static void pl_die(const char *m)
{
    fflush(stdout);
    fprintf(stderr, "PLI: %s\n", m);
    pl_stop(12);
}

static void *pl_mem(long n)
{
    void *p = calloc(1, (size_t)(n > 0 ? n : 1));
    if (!p) pl_die("out of storage");
    return p;
}

static plch *pl_newch(long n)
{
    long cap = 32000;
    plch *c;
    if (n > cap) cap = n;
    c = (plch *)pl_mem((long)sizeof(plch) + cap);
    c->cap = cap;
    return c;
}

static void *pl_ta(long n)
{
    plch *c;
    void *r;
    n = (n + 7) & ~7L;
    if (!pl_ar || pl_ar->u + n > pl_ar->cap) {
        c = pl_newch(n);
        c->pv = pl_ar;
        pl_ar = c;
    }
    r = (char *)pl_ar->d + pl_ar->u;
    pl_ar->u += n;
    return r;
}

static PLM pl_mark(void)
{
    PLM m;
    if (!pl_ar) { pl_ar = pl_newch(0); }
    m.c = pl_ar;
    m.u = pl_ar->u;
    return m;
}

static void pl_rel(PLM m)
{
    plch *c;
    while (pl_ar != m.c) {
        c = pl_ar;
        pl_ar = c->pv;
        free(c);
    }
    pl_ar->u = m.u;
}

/* ---- conditions ---- */
static void pl_on(int c, void (*h)(void))
{
    pl_hd[c] = h;
}

static void pl_onf(PLF *f, void (*h)(void))
{
    f->eof = h;
}

static void pl_fin(void)
{
    static int done = 0;
    if (!done && pl_hd[PLC_FIN]) {
        done = 1;
        pl_hd[PLC_FIN]();
    }
}

static const char *pl_cn(int c)
{
    static const char *nm[] = { "?", "ERROR", "ZERODIVIDE",
        "OVERFLOW", "UNDERFLOW", "SIZE", "CONVERSION",
        "SUBSCRIPTRANGE", "STRINGRANGE", "FINISH", "KEY",
        "TRANSMIT", "ENDPAGE" };
    return (c > 0 && c < PLC_N) ? nm[c] : "?";
}

static void pl_sig(int c, const char *m)
{
    static const long oc[] = { 0, 3, 320, 300, 330, 340, 600, 520,
        560, 4, 50, 40, 90 };
    void (*h)(void);
    pl_oncode = oc[c];
    h = pl_hd[c];
    if (h && !pl_hb[c]) {
        pl_hb[c] = 1;
        h();
        pl_hb[c] = 0;
        if (c == PLC_ERR) pl_stop(12);
        return;
    }
    fflush(stdout);
    fprintf(stderr, "PLI: %s condition raised%s%s\n", pl_cn(c),
            m ? ": " : "", m ? m : "");
    if (c == PLC_ERR || c == PLC_FIN) pl_stop(12);
    pl_sig(PLC_ERR, 0);
}

/* ---- numbers ---- */
static long pl_tol(double x)
{
    if (x >= 2147483647.0) return 2147483647L;
    if (x <= -2147483647.0) return -2147483647L;
    return (long)x;
}

static double pl_p10(int q)
{
    double s = 1.0;
    while (q-- > 0) s *= 10.0;
    return s;
}

static double pl_fx(double x, int q)
{
    double s = pl_p10(q), t;
    int neg = x < 0.0;
    if (neg) x = -x;
    t = floor(x * s + 1e-9 + x * s * 1e-14);
    t /= s;
    return neg ? -t : t;
}

static double pl_div(double a, double b)
{
    if (b == 0.0) {
        pl_sig(PLC_ZDIV, 0);
        return 0.0;
    }
    return a / b;
}

static double pl_pow(double a, double b)
{
    double r = 1.0;
    long i, n;
    if (a == 0.0 && b < 0.0) {
        pl_sig(PLC_ZDIV, 0);
        return 0.0;
    }
    if (b == floor(b) && b >= -64.0 && b <= 64.0) {
        n = (long)(b < 0 ? -b : b);
        for (i = 0; i < n; i++) r *= a;
        return b < 0.0 ? 1.0 / r : r;
    }
    return pow(a, b);
}

static double pl_mod(double a, double b)
{
    double r;
    if (b == 0.0) {
        pl_sig(PLC_ZDIV, 0);
        return 0.0;
    }
    r = fmod(a, b);
    if (r != 0.0 && ((r < 0.0) != (b < 0.0))) r += b;
    return r;
}

static double pl_rem(double a, double b)
{
    if (b == 0.0) {
        pl_sig(PLC_ZDIV, 0);
        return 0.0;
    }
    return fmod(a, b);
}

static double pl_round(double x, double n)
{
    double s = pl_p10((int)(n < 0 ? -n : n)), t;
    int neg = x < 0.0;
    if (neg) x = -x;
    t = (n >= 0) ? floor(x * s + 0.5 + 1e-9)
                 : floor(x / s + 0.5) * s;
    if (n >= 0) t /= s;
    return neg ? -t : t;
}

static double pl_trunc(double x)
{
    return x < 0.0 ? ceil(x) : floor(x);
}

static double pl_sign(double x)
{
    return x < 0.0 ? -1.0 : (x > 0.0 ? 1.0 : 0.0);
}

static double pl_max(double a, double b) { return a > b ? a : b; }
static double pl_min(double a, double b) { return a < b ? a : b; }
static double pl_abs(double a) { return a < 0.0 ? -a : a; }

static double pl_sqrt(double x)
{
    if (x < 0.0) {
        pl_sig(PLC_ERR, "SQRT of negative value");
        return 0.0;
    }
    return sqrt(x);
}

static double pl_log(double x)
{
    if (x <= 0.0) {
        pl_sig(PLC_ERR, "LOG of non-positive value");
        return 0.0;
    }
    return log(x);
}

static double pl_log10(double x)
{
    return pl_log(x) / 2.302585092994046;
}

static double pl_log2(double x)
{
    return pl_log(x) / 0.6931471805599453;
}

static long pl_ix(double v, long lo, long hi)
{
    long i = pl_tol(v);
    if (i < lo || i > hi) {
        pl_sig(PLC_SUBS, 0);
        return lo;
    }
    return i;
}

static double pl_sumd(const double *a, long n)
{
    double s = 0.0;
    while (n-- > 0) s += *a++;
    return s;
}

static double pl_suml(const long *a, long n)
{
    double s = 0.0;
    while (n-- > 0) s += (double)*a++;
    return s;
}

static double pl_prodd(const double *a, long n)
{
    double s = 1.0;
    while (n-- > 0) s *= *a++;
    return s;
}

static double pl_prodl(const long *a, long n)
{
    double s = 1.0;
    while (n-- > 0) s *= (double)*a++;
    return s;
}

static double pl_rnd(double seed)
{
    static unsigned long st = 12345UL;
    if (seed > 0.0) st = (unsigned long)seed % 2147483647UL + 1UL;
    st = (st * 1103515245UL + 12345UL) & 0x7fffffffUL;
    return (double)st / 2147483648.0;
}

/* temporary numeric cells for by-reference arguments */
static double *pl_tdp(double v)
{
    double *p = (double *)pl_ta((long)sizeof(double));
    *p = v;
    return p;
}

static long *pl_tlp(double v)
{
    long *p = (long *)pl_ta((long)sizeof(long));
    *p = pl_tol(v);
    return p;
}

static void **pl_tpp(void *v)
{
    void **p = (void **)pl_ta((long)sizeof(void *));
    *p = v;
    return p;
}

/* ---- strings ---- */
static PS pl_ls(const char *s)
{
    PS r;
    r.p = s;
    r.n = (long)strlen(s);
    return r;
}

static PS pl_mk(const char *s, long n)
{
    PS r;
    r.p = s;
    r.n = n;
    return r;
}

static PS pl_ps(PV v)
{
    PS r;
    r.p = v.p;
    r.n = v.np ? *v.np : v.max;
    return r;
}

static PV pl_pf(char *p, long n)
{
    PV v;
    v.p = p;
    v.np = 0;
    v.max = n;
    return v;
}

static PV pl_pv(long *np, char *p, long max)
{
    PV v;
    v.p = p;
    v.np = np;
    v.max = max;
    return v;
}

static PV pl_tmp(PS s)
{
    PV v;
    v.p = (char *)pl_ta(s.n + 1);
    memcpy(v.p, s.p, (size_t)s.n);
    v.p[s.n] = 0;
    v.np = 0;
    v.max = s.n;
    return v;
}

static PS pl_keep(PS s)
{
    PS r;
    char *p = (char *)pl_ta(s.n + 1);
    memcpy(p, s.p, (size_t)s.n);
    p[s.n] = 0;
    r.p = p;
    r.n = s.n;
    return r;
}

static void pl_asg(PV d, PS s, int ch)
{
    long n = s.n > d.max ? d.max : s.n;
    if (n > 0) memmove(d.p, s.p, (size_t)n);
    if (d.np) {
        *d.np = n;
    } else {
        while (n < d.max) d.p[n++] = ch;
    }
    d.p[d.np ? *d.np : d.max] = 0;
}

static void pl_as(PV d, PS s) { pl_asg(d, s, ' '); }
static void pl_ab(PV d, PS s) { pl_asg(d, s, '0'); }

static PS pl_cat(PS a, PS b)
{
    PS r;
    char *p = (char *)pl_ta(a.n + b.n + 1);
    memcpy(p, a.p, (size_t)a.n);
    memcpy(p + a.n, b.p, (size_t)b.n);
    p[a.n + b.n] = 0;
    r.p = p;
    r.n = a.n + b.n;
    return r;
}

static int pl_cmp(PS a, PS b)
{
    long i, n = a.n > b.n ? a.n : b.n;
    int x, y;
    for (i = 0; i < n; i++) {
        x = i < a.n ? (unsigned char)a.p[i] : ' ';
        y = i < b.n ? (unsigned char)b.p[i] : ' ';
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

static PS pl_n2s(double x, int q, int fp)
{
    char b[80];
    if (q >= 0) {
        sprintf(b, "%.*f", q > 20 ? 20 : q, x);
        if (b[0] == '-' && strspn(b + 1, "0.") == strlen(b + 1))
            memmove(b, b + 1, strlen(b));
    } else if (fp > 0) {
        sprintf(b, "%.*E", fp > 17 ? 16 : fp - 1, x);
    } else if (x == floor(x) && x > -1e15 && x < 1e15) {
        sprintf(b, "%.0f", x);
        if (!strcmp(b, "-0")) strcpy(b, "0");
    } else {
        sprintf(b, "%.15g", x);
    }
    return pl_keep(pl_ls(b));
}

static double pl_s2n(PS s)
{
    char b[80], *e;
    long a = 0, z = s.n, n;
    double v;
    while (a < z && s.p[a] == ' ') a++;
    while (z > a && s.p[z - 1] == ' ') z--;
    n = z - a;
    if (n == 0) return 0.0;
    if (n > 78) n = 78;
    memcpy(b, s.p + a, (size_t)n);
    b[n] = 0;
    v = strtod(b, &e);
    if (*e) {
        pl_sig(PLC_CONV, b);
        return 0.0;
    }
    return v;
}

static PS pl_n2b(double x, long n)
{
    char *p;
    unsigned long v;
    long i;
    if (n <= 0) n = 1;
    p = (char *)pl_ta(n + 1);
    v = (unsigned long)(pl_tol(x < 0.0 ? -x : x));
    for (i = n - 1; i >= 0; i--) {
        p[i] = (v & 1UL) ? '1' : '0';
        v >>= 1;
    }
    p[n] = 0;
    return pl_mk(p, n);
}

static double pl_b2n(PS s)
{
    double v = 0.0;
    long i;
    for (i = 0; i < s.n; i++) v = v * 2.0 + (s.p[i] == '1');
    return v;
}

static int pl_tb(PS s)
{
    long i;
    for (i = 0; i < s.n; i++) if (s.p[i] == '1') return 1;
    return 0;
}

static PS pl_bop(PS a, PS b, int op)
{
    long i, n = a.n > b.n ? a.n : b.n;
    char *p = (char *)pl_ta(n + 1);
    int x, y;
    for (i = 0; i < n; i++) {
        x = i < a.n && a.p[i] == '1';
        y = i < b.n && b.p[i] == '1';
        p[i] = (op == 0 ? (x && y) : (op == 1 ? (x || y) : x != y))
               ? '1' : '0';
    }
    p[n] = 0;
    return pl_mk(p, n);
}

static PS pl_bnot(PS a)
{
    long i;
    char *p = (char *)pl_ta(a.n + 1);
    for (i = 0; i < a.n; i++) p[i] = a.p[i] == '1' ? '0' : '1';
    p[a.n] = 0;
    return pl_mk(p, a.n);
}

static double pl_len(PS s) { return (double)s.n; }

static PS pl_sub(PS s, double st, double ln)
{
    long a = pl_tol(st), n = pl_tol(ln);
    if (a < 1 || n < 0 || a + n - 1 > s.n) {
        pl_sig(PLC_STR, 0);
        if (a < 1) a = 1;
        if (a > s.n + 1) a = s.n + 1;
        if (n < 0) n = 0;
        if (a + n - 1 > s.n) n = s.n - a + 1;
    }
    return pl_mk(s.p + a - 1, n);
}

static PS pl_sub2(PS s, double st)
{
    return pl_sub(s, st, (double)(s.n - pl_tol(st) + 1));
}

static void pl_subas(PV d, double st, double ln, PS s)
{
    long a = pl_tol(st), n = pl_tol(ln), i, cur;
    cur = d.np ? *d.np : d.max;
    if (a < 1 || n < 0 || a + n - 1 > cur) {
        pl_sig(PLC_STR, 0);
        return;
    }
    for (i = 0; i < n; i++)
        d.p[a - 1 + i] = i < s.n ? s.p[i] : ' ';
}

static double pl_idx(PS s, PS t)
{
    long i;
    if (t.n == 0) return 0.0;
    for (i = 0; i + t.n <= s.n; i++)
        if (memcmp(s.p + i, t.p, (size_t)t.n) == 0)
            return (double)(i + 1);
    return 0.0;
}

static double pl_ver(PS s, PS t)
{
    long i, j;
    for (i = 0; i < s.n; i++) {
        for (j = 0; j < t.n; j++) if (s.p[i] == t.p[j]) break;
        if (j == t.n) return (double)(i + 1);
    }
    return 0.0;
}

static PS pl_trn(PS s, PS to, PS from)
{
    long i, j;
    char *p = (char *)pl_ta(s.n + 1);
    for (i = 0; i < s.n; i++) {
        p[i] = s.p[i];
        for (j = 0; j < from.n; j++)
            if (s.p[i] == from.p[j]) {
                p[i] = j < to.n ? to.p[j] : ' ';
                break;
            }
    }
    p[s.n] = 0;
    return pl_mk(p, s.n);
}

static PS pl_rev(PS s)
{
    long i;
    char *p = (char *)pl_ta(s.n + 1);
    for (i = 0; i < s.n; i++) p[i] = s.p[s.n - 1 - i];
    p[s.n] = 0;
    return pl_mk(p, s.n);
}

static PS pl_rpt(PS s, double k)
{
    long i, n = pl_tol(k) + 1;
    char *p;
    if (n < 1) n = 1;
    p = (char *)pl_ta(s.n * n + 1);
    for (i = 0; i < n; i++) memcpy(p + i * s.n, s.p, (size_t)s.n);
    p[s.n * n] = 0;
    return pl_mk(p, s.n * n);
}

static PS pl_trim(PS s, int lead, int trail)
{
    long a = 0, z = s.n;
    if (lead) while (a < z && s.p[a] == ' ') a++;
    if (trail) while (z > a && s.p[z - 1] == ' ') z--;
    return pl_mk(s.p + a, z - a);
}

static PS pl_case(PS s, int up)
{
    long i;
    char *p = (char *)pl_ta(s.n + 1);
    for (i = 0; i < s.n; i++)
        p[i] = (char)(up ? toupper((unsigned char)s.p[i])
                         : tolower((unsigned char)s.p[i]));
    p[s.n] = 0;
    return pl_mk(p, s.n);
}

static PS pl_just(PS s, double w, int how)
{
    long n = pl_tol(w), k, l;
    char *p;
    if (n < 0) n = 0;
    p = (char *)pl_ta(n + 1);
    memset(p, ' ', (size_t)n);
    k = s.n > n ? n : s.n;
    l = how == 0 ? 0 : (how == 1 ? n - k : (n - k) / 2);
    memcpy(p + l, s.p, (size_t)k);
    p[n] = 0;
    return pl_mk(p, n);
}

static PS pl_date(void)
{
    char b[16];
    time_t t = time(0);
    strftime(b, sizeof b, "%y%m%d", localtime(&t));
    return pl_keep(pl_ls(b));
}

static PS pl_time(void)
{
    char b[16];
    time_t t = time(0);
    strftime(b, sizeof b, "%H%M%S000", localtime(&t));
    return pl_keep(pl_ls(b));
}

static PS pl_hl(double n, int ch)
{
    long k = pl_tol(n);
    char *p;
    if (k < 0) k = 0;
    p = (char *)pl_ta(k + 1);
    memset(p, ch, (size_t)k);
    p[k] = 0;
    return pl_mk(p, k);
}

static const char *pl_cs(PS s)
{
    long n = s.n;
    char *p;
    while (n > 0 && s.p[n - 1] == ' ') n--;
    p = (char *)pl_ta(n + 1);
    memcpy(p, s.p, (size_t)n);
    p[n] = 0;
    return p;
}

static PS pl_chrn(PS s, double w)
{
    long n = pl_tol(w);
    return n >= s.n ? pl_just(s, w, 0) : pl_mk(s.p, n);
}

/* ---- files ---- */
static void pl_reg(PLF *f)
{
    int i;
    for (i = 0; i < pl_nfl; i++) if (pl_fl[i] == f) return;
    if (pl_nfl < 64) pl_fl[pl_nfl++] = f;
}

static void pl_close(PLF *f)
{
    if (f->mem) return;
    if (f->mode == PLF_OUT && f->col > 0 && f->prt && f->fp)
        putc('\n', f->fp);
    if (f->fp && f->fp != stdin && f->fp != stdout) fclose(f->fp);
    else if (f->fp) fflush(f->fp);
    f->fp = 0;
    f->mode = 0;
    f->col = 0;
    f->line = 0;
    f->lv = 0;
}

static void pl_open(PLF *f, const char *dd, const char *title,
                    int fl, long lsz, long psz)
{
    char nm[200];
    const char *name = title, *e;
    const char *md;
    f->dd = dd;
    f->mode = fl & 3;
    f->prt = (fl & PLF_PRT) != 0;
    f->rec = (fl & PLF_REC) != 0;
    f->lsz = lsz > 0 ? lsz : 120;
    f->psz = psz > 0 ? psz : 60;
    f->col = 0;
    f->line = 0;
    f->lv = 0;
    f->eofs = 0;
    if (!name) {
        sprintf(nm, "DD_%s", dd);
        e = getenv(nm);
        if (e) {
            name = e;
        } else {
            long i = 0;
            while (dd[i] && i < 190) {
                nm[i] = (char)tolower((unsigned char)dd[i]);
                i++;
            }
            nm[i] = 0;
            name = nm;
        }
    }
    md = f->mode == PLF_IN ? "r" : (f->mode == PLF_OUT ? "w" : "r+");
    f->fp = fopen(name, md);
    if (!f->fp) {
        fflush(stdout);
        fprintf(stderr, "PLI: cannot open %s (%s)\n", dd, name);
        pl_sig(PLC_ERR, "OPEN failed");
        return;
    }
    pl_reg(f);
}

static void pl_chk(PLF *f, const char *dd, int mode)
{
    if (f->mode == 0 && !f->mem)
        pl_open(f, dd, 0, mode | (f->prt ? PLF_PRT : 0), 0, 0);
}

static void pl_start(int argc, char **argv)
{
    pl_argc = argc;
    pl_argv = argv;
    pl_sysin.fp = stdin;
    pl_sysin.mode = PLF_IN;
    pl_sysin.dd = "SYSIN";
    pl_sysprt.fp = stdout;
    pl_sysprt.mode = PLF_OUT;
    pl_sysprt.prt = 1;
    pl_sysprt.lsz = 120;
    pl_sysprt.psz = 60;
    pl_sysprt.dd = "SYSPRINT";
    pl_reg(&pl_sysin);
    pl_reg(&pl_sysprt);
    pl_mark();
}

static void pl_stop(int rc)
{
    int i;
    pl_fin();
    for (i = 0; i < pl_nfl; i++)
        if (pl_fl[i]->mode) pl_close(pl_fl[i]);
    fflush(stdout);
    exit(rc);
}

/* ---- output ---- */
static void pl_ob(PLF *f, int c)
{
    if (f->mem) {
        if (f->mn + 2 > f->mcap) {
            char *nb;
            f->mcap = f->mcap * 2 + 256;
            nb = (char *)pl_mem(f->mcap);
            if (f->mn) memcpy(nb, f->mb, (size_t)f->mn);
            free(f->mb);
            f->mb = nb;
        }
        f->mb[f->mn++] = (char)c;
        f->col++;
        return;
    }
    if (f->line == 0) f->line = 1;
    putc(c, f->fp);
    if (c == '\n') {
        f->col = 0;
        f->line++;
    } else {
        f->col++;
    }
}

static void pl_os(PLF *f, PS s)
{
    long i;
    for (i = 0; i < s.n; i++) pl_ob(f, (unsigned char)s.p[i]);
}

static void pl_pskip(PLF *f, long n)
{
    long k;
    if (f->mem) return;
    if (n < 1) n = 1;
    k = f->line == 0 ? n - 1 : n;
    while (k-- > 0) pl_ob(f, '\n');
    if (f->line == 0) f->line = n;
    f->col = 0;
}

static void pl_ppage(PLF *f)
{
    if (f->mem) return;
    if (f->col > 0) pl_ob(f, '\n');
    putc('\f', f->fp);
    f->line = 1;
    f->col = 0;
}

static void pl_pline(PLF *f, long n)
{
    if (f->mem) return;
    if (f->col > 0) pl_ob(f, '\n');
    if (n <= f->line) {
        pl_ppage(f);
        if (f->line == 1 && n > 1) pl_pskip(f, n - 1);
        return;
    }
    while (f->line < n) pl_ob(f, '\n');
}

static void pl_pcol(PLF *f, long n)
{
    if (f->col > n - 1) pl_ob(f, '\n');
    while (f->col < n - 1) pl_ob(f, ' ');
}

static void pl_li(PLF *f, PS s)
{
    long nx;
    if (f->col > 0) {
        nx = (f->col / 24 + 1) * 24;
        if (!f->mem && nx + s.n > f->lsz) {
            pl_ob(f, '\n');
        } else {
            while (f->col < nx) pl_ob(f, ' ');
        }
    }
    pl_os(f, s);
}

static void pl_li_n(PLF *f, double x, int q, int fp)
{
    pl_li(f, pl_n2s(x, q, fp));
}

static void pl_pd_n(PLF *f, const char *nm, PS v, int str)
{
    if (f->xs++) pl_ob(f, ' ');
    pl_os(f, pl_ls(nm));
    pl_ob(f, '=');
    if (str) pl_ob(f, '\'');
    pl_os(f, v);
    if (str) pl_ob(f, '\'');
}

static void pl_pd_end(PLF *f)
{
    pl_ob(f, ';');
    f->xs = 0;
}

/* ---- edit format interpreter ---- */
static long pl_fnum(const char **p)
{
    long v = 0;
    while (isdigit((unsigned char)**p)) v = v * 10 + (*(*p)++ - '0');
    return v;
}

/* fetch next format item: code, a, b.  returns 0 at end of spec */
static int pl_fitem(PLF *f, long *a, long *b, int *c)
{
    const char *p = f->fc;
    if (!*p) return 0;
    *c = *p++;
    *a = -1;
    *b = -1;
    if (*p == '(') {
        p++;
        *a = pl_fnum(&p);
        if (*p == ',') { p++; *b = pl_fnum(&p); }
        if (*p == ',') { p++; (void)pl_fnum(&p); }
        if (*p == ')') p++;
    }
    if (*p == ',') p++;
    f->fc = p;
    return 1;
}

static void pl_ebeg(PLF *f, const char *spec)
{
    f->fs = spec;
    f->fc = spec;
}

static int pl_pctl(PLF *f, int c, long a)
{
    switch (c) {
    case 'X': while (a-- > 0) pl_ob(f, ' '); return 1;
    case 'S': pl_pskip(f, a < 0 ? 1 : a); return 1;
    case 'C': pl_pcol(f, a); return 1;
    case 'L': pl_pline(f, a); return 1;
    case 'P': pl_ppage(f); return 1;
    }
    return 0;
}

static int pl_enext(PLF *f, long *a, long *b, int *c)
{
    int wrapped = 0;
    for (;;) {
        if (!pl_fitem(f, a, b, c)) {
            if (wrapped) pl_die("PUT/GET EDIT: no data format");
            wrapped = 1;
            f->fc = f->fs;
            continue;
        }
        if (f->mode == PLF_IN || !pl_pctl(f, *c, *a)) return 1;
    }
}

static void pl_eend(PLF *f)
{
    long a, b;
    int c;
    while (f->mode != PLF_IN && f->fc && *f->fc) {
        if (!pl_fitem(f, &a, &b, &c)) break;
        if (!pl_pctl(f, c, a)) break;
    }
}

static void pl_padw(PLF *f, const char *s, long n, long w, int left)
{
    long k;
    if (w < 0) w = n;
    if (n > w) {
        for (k = 0; k < w; k++) pl_ob(f, '*');
        return;
    }
    if (!left) for (k = n; k < w; k++) pl_ob(f, ' ');
    for (k = 0; k < n; k++) pl_ob(f, (unsigned char)s[k]);
    if (left) for (k = n; k < w; k++) pl_ob(f, ' ');
}

static void pl_en(PLF *f, double x)
{
    long a, b;
    int c;
    char t[100];
    pl_enext(f, &a, &b, &c);
    if (c == 'F') {
        if (b < 0) b = 0;
        sprintf(t, "%.*f", (int)(b > 40 ? 40 : b),
                pl_round(x, (double)b));
        if (t[0] == '-' && strspn(t + 1, "0.") == strlen(t + 1))
            memmove(t, t + 1, strlen(t));
        pl_padw(f, t, (long)strlen(t), a, 0);
    } else if (c == 'E') {
        if (b < 0) b = 5;
        sprintf(t, "%.*E", (int)(b > 40 ? 40 : b), x);
        pl_padw(f, t, (long)strlen(t), a, 0);
    } else if (c == 'B') {
        PS s = pl_n2b(x, a < 0 ? 8 : a);
        pl_padw(f, s.p, s.n, a, 0);
    } else {
        PS s = pl_n2s(x, -1, 0);
        pl_padw(f, s.p, s.n, a, 0);
    }
}

static void pl_es(PLF *f, PS s)
{
    long a, b;
    int c;
    pl_enext(f, &a, &b, &c);
    if (c == 'A' || c == 'B') {
        if (a < 0) a = s.n;
        if (s.n > a) s.n = a;
        pl_padw(f, s.p, s.n, a, 1);
    } else {
        PS t = pl_n2s(pl_s2n(s), -1, 0);
        pl_padw(f, t.p, t.n, a, 0);
    }
}

/* ---- input ---- */
static void pl_eofsig(PLF *f)
{
    f->eofs = 1;
    pl_oncode = 70;
    if (f->eof) {
        f->eof();
        return;
    }
    fflush(stdout);
    fprintf(stderr, "PLI: ENDFILE condition raised on %s\n",
            f->dd ? f->dd : "file");
    pl_sig(PLC_ERR, 0);
}

static int pl_rdline(PLF *f)
{
    int c;
    long n = 0;
    if (f->mem) return 0;
    if (!f->fp || f->mode != PLF_IN) return 0;
    for (;;) {
        c = getc(f->fp);
        if (c == EOF) break;
        if (n + 2 > f->lcap) {
            char *nb;
            f->lcap = f->lcap * 2 + 128;
            nb = (char *)pl_mem(f->lcap);
            if (n) memcpy(nb, f->lb, (size_t)n);
            free(f->lb);
            f->lb = nb;
        }
        if (c == '\n') { f->lb[n] = 0; break; }
        if (c == '\r') continue;
        f->lb[n++] = (char)c;
    }
    if (c == EOF && n == 0) return 0;
    f->lb[n] = 0;
    f->ln = n;
    f->lp = 0;
    f->lv = 1;
    return 1;
}

static int pl_need(PLF *f)
{
    if (f->lv) return 1;
    if (f->mem) return 0;
    if (pl_rdline(f)) return 1;
    pl_eofsig(f);
    return 0;
}

static void pl_gskip(PLF *f, long n)
{
    if (n < 1) n = 1;
    f->lv = 0;
    n--;
    while (n-- > 0) {
        if (!pl_rdline(f)) { pl_eofsig(f); return; }
        f->lv = 0;
    }
}

static void pl_gstr(PLF *f, PS s)
{
    memset(f, 0, sizeof *f);
    f->mem = 1;
    f->mode = PLF_IN;
    f->lb = (char *)pl_mem(s.n + 2);
    memcpy(f->lb, s.p, (size_t)s.n);
    f->ln = s.n;
    f->lp = 0;
    f->lv = 1;
}

static PS pl_gtok(PLF *f, int *quo)
{
    char *p;
    long a, n;
    PS r;
    *quo = 0;
    for (;;) {
        if (!pl_need(f)) return pl_mk("", 0);
        while (f->lp < f->ln && (f->lb[f->lp] == ' ' ||
               f->lb[f->lp] == '\t' || f->lb[f->lp] == ','))
            f->lp++;
        if (f->lp < f->ln) break;
        f->lv = 0;
        if (f->mem) { pl_eofsig(f); return pl_mk("", 0); }
    }
    if (f->lb[f->lp] == '\'') {
        *quo = 1;
        f->lp++;
        p = (char *)pl_ta(f->ln - f->lp + 2);
        n = 0;
        while (f->lp < f->ln) {
            if (f->lb[f->lp] == '\'') {
                if (f->lp + 1 < f->ln && f->lb[f->lp + 1] == '\'') {
                    p[n++] = '\'';
                    f->lp += 2;
                    continue;
                }
                f->lp++;
                break;
            }
            p[n++] = f->lb[f->lp++];
        }
        p[n] = 0;
        return pl_mk(p, n);
    }
    a = f->lp;
    while (f->lp < f->ln && f->lb[f->lp] != ' ' &&
           f->lb[f->lp] != '\t' && f->lb[f->lp] != ',')
        f->lp++;
    r.p = f->lb + a;
    r.n = f->lp - a;
    return pl_keep(r);
}

static void pl_gn_l(PLF *f, double *x)
{
    int q;
    PS t = pl_gtok(f, &q);
    if (f->eofs) { f->eofs = 0; return; }
    *x = pl_s2n(t);
}

static void pl_gs_l(PLF *f, PV d, int bit)
{
    int q;
    PS t = pl_gtok(f, &q);
    if (f->eofs) { f->eofs = 0; return; }
    if (bit) pl_ab(d, t); else pl_as(d, t);
}

static PS pl_fld(PLF *f, long w)
{
    PS r;
    if (!pl_need(f)) return pl_mk("", 0);
    if (w < 0 || f->lp + w > f->ln) w = f->ln - f->lp;
    r.p = f->lb + f->lp;
    r.n = w;
    f->lp += w;
    return pl_keep(r);
}

static int pl_gctl(PLF *f, int c, long a)
{
    switch (c) {
    case 'X': f->lp += a; if (f->lp > f->ln) f->lp = f->ln; return 1;
    case 'S': pl_gskip(f, a < 0 ? 1 : a); return 1;
    case 'C': if (pl_need(f)) f->lp = a - 1 < f->ln ? a - 1 : f->ln;
              return 1;
    }
    return 0;
}

static int pl_gnext(PLF *f, long *a, long *b, int *c)
{
    int wrapped = 0;
    for (;;) {
        if (!pl_fitem(f, a, b, c)) {
            if (wrapped) pl_die("GET EDIT: no data format");
            wrapped = 1;
            f->fc = f->fs;
            continue;
        }
        if (!pl_gctl(f, *c, *a)) return 1;
    }
}

static void pl_gn(PLF *f, double *x)
{
    long a, b;
    int c;
    PS t;
    double v;
    pl_gnext(f, &a, &b, &c);
    t = pl_fld(f, a);
    if (f->eofs) { f->eofs = 0; return; }
    v = pl_s2n(t);
    if (c == 'F' && b > 0 && !memchr(t.p, '.', (size_t)t.n))
        v /= pl_p10((int)b);
    *x = v;
}

static void pl_gs(PLF *f, PV d, int bit)
{
    long a, b;
    int c;
    PS t;
    pl_gnext(f, &a, &b, &c);
    if (a < 0) a = d.max;
    t = pl_fld(f, a);
    if (f->eofs) { f->eofs = 0; return; }
    if (bit) pl_ab(d, t); else pl_as(d, t);
}

/* ---- PUT/GET STRING ---- */
static void pl_pstr(PLF *f)
{
    char *mb = f->mb;
    long mc = f->mcap;
    memset(f, 0, sizeof *f);
    f->mb = mb;
    f->mcap = mc;
    f->mem = 1;
    f->mode = PLF_OUT;
    f->mn = 0;
    f->col = 0;
    f->xs = 0;
}

static void pl_pend(PLF *f, PV d, int bit)
{
    PS s;
    s.p = f->mb ? f->mb : "";
    s.n = f->mn;
    if (bit) pl_ab(d, s); else pl_as(d, s);
    f->mn = 0;
}

/* ---- record i/o ---- */
static void pl_rdrec(PLF *f, PV d)
{
    PS s;
    if (!pl_rdline(f)) { pl_eofsig(f); return; }
    s.p = f->lb;
    s.n = f->ln;
    f->lv = 0;
    pl_as(d, s);
}

static void pl_wrrec(PLF *f, PS s)
{
    long i;
    for (i = 0; i < s.n; i++) putc((unsigned char)s.p[i], f->fp);
    putc('\n', f->fp);
}

static void pl_rdbin(PLF *f, void *p, long n)
{
    if (fread(p, 1, (size_t)n, f->fp) != (size_t)n) pl_eofsig(f);
}

static void pl_wrbin(PLF *f, const void *p, long n)
{
    fwrite(p, 1, (size_t)n, f->fp);
}

/* ---- misc statements ---- */
static void pl_disp(PS s)
{
    PLF *f = &pl_sysprt;
    if (f->col > 0) pl_ob(f, '\n');
    pl_os(f, s);
    fflush(stdout);
}

static void pl_reply(PV d)
{
    char b[256];
    long n;
    PLF *f = &pl_sysprt;
    if (f->col > 0) pl_ob(f, '\n');
    fflush(stdout);
    if (!fgets(b, sizeof b, stdin)) b[0] = 0;
    n = (long)strlen(b);
    while (n > 0 && (b[n - 1] == '\n' || b[n - 1] == '\r')) n--;
    pl_as(d, pl_mk(b, n));
}

static void *pl_alloc(long n)
{
    return pl_mem(n);
}

static void pl_free(void *p)
{
    free(p);
}

#endif
