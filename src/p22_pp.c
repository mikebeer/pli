/* p22_pp.c -- the PL/I preprocessor: % statements, compile-time
   variables, %IF, %DO, %PROCEDURE, built-in functions, %INCLUDE */

enum { V_VAR, V_PROC, V_TOK };

/* ty 0 fixed, 1 char */
typedef struct { int ty; long n; char *s; } PPV;

typedef struct {
    char *name;
    int kind, dty, active, resc;
    PPV v;
    Tok t;                    /* V_TOK: the replacement token */
    TV *in;                   /* V_PROC: the token stream */
    long body;
    int np, rty;
    char *pn[8];
} PVar;

static PVar pvs[512];
static int npv = 0;
static int pp_ret = 0;
static PPV pp_rv;
static PVar *pp_exp[40];
static int pp_nexp = 0;

static int tisid(Tok *t, const char *s)
{
    return t->k == T_ID && !strcmp(t->s, s);
}

static int tisop(Tok *t, const char *s)
{
    return t->k == T_OP && !strcmp(t->s, s);
}

static Tok pp_eoft;

static Tok *pp_tk(TV *in, long i)
{
    if (i < in->n) return &in->a[i];
    pp_eoft.k = T_EOF;
    pp_eoft.s = "";
    pp_eoft.line = in->n ? in->a[in->n - 1].line : 1;
    return &pp_eoft;
}

static PPV pp_fx(long n)
{
    PPV v;
    v.ty = 0;
    v.n = n;
    v.s = 0;
    return v;
}

static PPV pp_ch(const char *s)
{
    PPV v;
    v.ty = 1;
    v.n = 0;
    v.s = xdup(s);
    return v;
}

static char *pp_str(PPV v)
{
    char b[40];
    if (v.ty) return v.s;
    sprintf(b, "%ld", v.n);
    return xdup(b);
}

static long pp_num(PPV v, int line)
{
    char *e;
    long n;
    if (!v.ty) return v.n;
    n = strtol(v.s, &e, 10);
    while (*e == ' ') e++;
    if (e == v.s || *e) perr(line, "preprocessor: not a number", v.s);
    return n;
}

static PVar *pp_find(const char *nm)
{
    int i;
    for (i = npv - 1; i >= 0; i--)
        if (!strcmp(pvs[i].name, nm)) return &pvs[i];
    return 0;
}

static PVar *pp_new(const char *nm, int kind, int line)
{
    PVar *v;
    if (npv >= 512) perr(line, "too many preprocessor names", nm);
    v = &pvs[npv++];
    memset(v, 0, sizeof *v);
    v->name = xdup(nm);
    v->kind = kind;
    v->dty = -1;
    v->resc = 1;
    v->v = pp_fx(0);
    return v;
}

static void pp_assign(PVar *v, PPV x, int line)
{
    if (v->kind != V_VAR)
        perr(line, "preprocessor: cannot assign to", v->name);
    if (v->dty == 0) v->v = pp_fx(pp_num(x, line));
    else if (v->dty == 1) v->v = pp_ch(pp_str(x));
    else v->v = x;
}

static int pp_true(PPV v, int line)
{
    return pp_num(v, line) != 0;
}

/* ---- compile-time expressions ---- */

static PPV pp_expr(TV *in, long *pi);
static PPV pp_call(PVar *v, PPV *a, int na, int line);

static int pp_args(TV *in, long *pi, PPV *a, int max)
{
    int n = 0;
    int line = pp_tk(in, *pi)->line;
    (*pi)++;                              /* the ( */
    if (tisop(pp_tk(in, *pi), ")")) { (*pi)++; return 0; }
    for (;;) {
        if (n >= max) perr(line, "too many preprocessor arguments", 0);
        a[n++] = pp_expr(in, pi);
        if (tisop(pp_tk(in, *pi), ",")) { (*pi)++; continue; }
        if (tisop(pp_tk(in, *pi), ")")) { (*pi)++; break; }
        perr(line, "bad preprocessor argument list", 0);
    }
    return n;
}

static PPV pp_bif(const char *nm, PPV *a, int n, int line)
{
    char *s, *u;
    long i, k, l;
    Sb b;
    if (!strcmp(nm, "length") && n == 1)
        return pp_fx((long)strlen(pp_str(a[0])));
    if (!strcmp(nm, "substr") && (n == 2 || n == 3)) {
        s = pp_str(a[0]);
        l = (long)strlen(s);
        i = pp_num(a[1], line);
        k = n == 3 ? pp_num(a[2], line) : l - i + 1;
        if (i < 1 || k < 0 || i - 1 + k > l)
            perr(line, "preprocessor: bad SUBSTR range", 0);
        u = (char *)xalloc(k + 1);
        memcpy(u, s + i - 1, (size_t)k);
        u[k] = 0;
        return pp_ch(u);
    }
    if (!strcmp(nm, "index") && n == 2) {
        s = pp_str(a[0]);
        u = strstr(s, pp_str(a[1]));
        return pp_fx(u ? (long)(u - s) + 1 : 0);
    }
    if (!strcmp(nm, "verify") && n == 2) {
        s = pp_str(a[0]);
        u = pp_str(a[1]);
        for (i = 0; s[i]; i++)
            if (!strchr(u, s[i])) return pp_fx(i + 1);
        return pp_fx(0);
    }
    if (!strcmp(nm, "trim") && n == 1) {
        s = pp_str(a[0]);
        while (*s == ' ') s++;
        l = (long)strlen(s);
        while (l > 0 && s[l - 1] == ' ') l--;
        u = (char *)xalloc(l + 1);
        memcpy(u, s, (size_t)l);
        u[l] = 0;
        return pp_ch(u);
    }
    if (!strcmp(nm, "translate") && n == 3) {
        char *to = pp_str(a[1]), *fr = pp_str(a[2]);
        s = xdup(pp_str(a[0]));
        for (i = 0; s[i]; i++) {
            char *q = strchr(fr, s[i]);
            if (q && (size_t)(q - fr) < strlen(to))
                s[i] = to[q - fr];
        }
        return pp_ch(s);
    }
    if (!strcmp(nm, "repeat") && n == 2) {
        s = pp_str(a[0]);
        k = pp_num(a[1], line);
        memset(&b, 0, sizeof b);
        for (i = 0; i <= k; i++) sb_s(&b, s);
        return pp_ch(sb_str(&b));
    }
    if (!strcmp(nm, "uppercase") && n == 1) {
        s = xdup(pp_str(a[0]));
        for (i = 0; s[i]; i++)
            s[i] = (char)toupper((unsigned char)s[i]);
        return pp_ch(s);
    }
    if (!strcmp(nm, "lowercase") && n == 1) {
        s = xdup(pp_str(a[0]));
        for (i = 0; s[i]; i++)
            s[i] = (char)tolower((unsigned char)s[i]);
        return pp_ch(s);
    }
    if ((!strcmp(nm, "min") || !strcmp(nm, "max")) && n >= 1) {
        k = pp_num(a[0], line);
        for (i = 1; i < n; i++) {
            l = pp_num(a[i], line);
            if (nm[1] == 'i' ? l < k : l > k) k = l;
        }
        return pp_fx(k);
    }
    if (!strcmp(nm, "mod") && n == 2) {
        k = pp_num(a[1], line);
        if (k == 0) perr(line, "preprocessor: MOD by zero", 0);
        l = pp_num(a[0], line) % k;
        if (l < 0) l += k < 0 ? -k : k;
        return pp_fx(l);
    }
    if (!strcmp(nm, "abs") && n == 1) {
        k = pp_num(a[0], line);
        return pp_fx(k < 0 ? -k : k);
    }
    if (!strcmp(nm, "sign") && n == 1) {
        k = pp_num(a[0], line);
        return pp_fx(k < 0 ? -1 : k > 0);
    }
    if ((!strcmp(nm, "char") || !strcmp(nm, "character")) && n == 1)
        return pp_ch(pp_str(a[0]));
    if (!strcmp(nm, "fixed") && n == 1)
        return pp_fx(pp_num(a[0], line));
    perr(line, "unknown preprocessor function", nm);
    return pp_fx(0);
}

static int pp_isbif(const char *s)
{
    static const char *nm[] = { "length", "substr", "index", "verify",
        "trim", "translate", "repeat", "uppercase", "lowercase",
        "min", "max", "mod", "abs", "sign", "char", "character",
        "fixed", 0 };
    int i;
    for (i = 0; nm[i]; i++)
        if (!strcmp(nm[i], s)) return 1;
    return 0;
}

static PPV pp_tokval(Tok *t)
{
    if (t->k == T_NUM) return pp_fx((long)t->dv);
    return pp_ch(t->s);
}

static PPV pp_name(TV *in, long *pi, Tok *t)
{
    PPV a[8];
    int na = 0;
    PVar *v;
    int paren = tisop(pp_tk(in, *pi), "(");
    if (!strcmp(t->s, "sysversion") && !paren)
        return pp_ch("PLI 1.0");
    if (paren && pp_isbif(t->s) &&
        (!(v = pp_find(t->s)) || v->kind != V_PROC)) {
        na = pp_args(in, pi, a, 8);
        return pp_bif(t->s, a, na, t->line);
    }
    v = pp_find(t->s);
    if (!v) perr(t->line, "unknown preprocessor name", t->s);
    if (v->kind == V_TOK) return pp_tokval(&v->t);
    if (v->kind == V_PROC) {
        if (paren) na = pp_args(in, pi, a, 8);
        return pp_call(v, a, na, t->line);
    }
    return v->v;
}

static PPV pp_prim(TV *in, long *pi)
{
    Tok *t = pp_tk(in, *pi);
    PPV v;
    if (t->k == T_NUM) { (*pi)++; return pp_fx((long)t->dv); }
    if (t->k == T_STR) { (*pi)++; return pp_ch(t->s); }
    if (tisop(t, "(")) {
        (*pi)++;
        v = pp_expr(in, pi);
        if (!tisop(pp_tk(in, *pi), ")"))
            perr(t->line, "missing ) in preprocessor expression", 0);
        (*pi)++;
        return v;
    }
    if (t->k == T_ID) {
        (*pi)++;
        return pp_name(in, pi, t);
    }
    perr(t->line, "bad preprocessor expression", 0);
    return pp_fx(0);
}

static PPV pp_unary(TV *in, long *pi);

static PPV pp_pow(TV *in, long *pi)
{
    PPV a = pp_prim(in, pi), b;
    long r, e, k;
    Tok *t = pp_tk(in, *pi);
    if (tisop(t, "**")) {
        (*pi)++;
        b = pp_unary(in, pi);
        e = pp_num(b, t->line);
        r = 1;
        for (k = 0; k < e; k++) r *= pp_num(a, t->line);
        return pp_fx(r);
    }
    return a;
}

static PPV pp_unary(TV *in, long *pi)
{
    Tok *t = pp_tk(in, *pi);
    PPV v;
    if (tisop(t, "-") || tisop(t, "+") || tisop(t, "!")) {
        (*pi)++;
        v = pp_unary(in, pi);
        if (t->s[0] == '-') return pp_fx(-pp_num(v, t->line));
        if (t->s[0] == '!') return pp_fx(!pp_true(v, t->line));
        return pp_fx(pp_num(v, t->line));
    }
    return pp_pow(in, pi);
}

static PPV pp_mul(TV *in, long *pi)
{
    PPV a = pp_unary(in, pi), b;
    Tok *t;
    long x, y;
    for (;;) {
        t = pp_tk(in, *pi);
        if (!tisop(t, "*") && !tisop(t, "/")) return a;
        (*pi)++;
        b = pp_unary(in, pi);
        x = pp_num(a, t->line);
        y = pp_num(b, t->line);
        if (t->s[0] == '*') a = pp_fx(x * y);
        else {
            if (y == 0) perr(t->line, "preprocessor: divide by 0", 0);
            a = pp_fx(x / y);
        }
    }
}

static PPV pp_add(TV *in, long *pi)
{
    PPV a = pp_mul(in, pi), b;
    Tok *t;
    for (;;) {
        t = pp_tk(in, *pi);
        if (!tisop(t, "+") && !tisop(t, "-")) return a;
        (*pi)++;
        b = pp_mul(in, pi);
        if (t->s[0] == '+')
            a = pp_fx(pp_num(a, t->line) + pp_num(b, t->line));
        else
            a = pp_fx(pp_num(a, t->line) - pp_num(b, t->line));
    }
}

static PPV pp_cat(TV *in, long *pi)
{
    PPV a = pp_add(in, pi), b;
    Sb s;
    while (tisop(pp_tk(in, *pi), "||")) {
        (*pi)++;
        b = pp_add(in, pi);
        memset(&s, 0, sizeof s);
        sb_s(&s, pp_str(a));
        sb_s(&s, pp_str(b));
        a = pp_ch(sb_str(&s));
    }
    return a;
}

static int pp_cmpv(PPV a, PPV b)
{
    char *x, *y;
    size_t i, n;
    if (!a.ty && !b.ty) return a.n < b.n ? -1 : a.n > b.n;
    x = pp_str(a);
    y = pp_str(b);
    n = strlen(x) > strlen(y) ? strlen(x) : strlen(y);
    for (i = 0; i < n; i++) {
        int c = i < strlen(x) ? (unsigned char)x[i] : ' ';
        int d = i < strlen(y) ? (unsigned char)y[i] : ' ';
        if (c != d) return c < d ? -1 : 1;
    }
    return 0;
}

static PPV pp_cmp(TV *in, long *pi)
{
    PPV a = pp_cat(in, pi), b;
    Tok *t = pp_tk(in, *pi);
    const char *o;
    int c, r;
    if (t->k != T_OP) return a;
    o = t->s;
    if (strcmp(o, "=") && strcmp(o, "!=") && strcmp(o, "<") &&
        strcmp(o, ">") && strcmp(o, "<=") && strcmp(o, ">="))
        return a;
    (*pi)++;
    b = pp_cat(in, pi);
    c = pp_cmpv(a, b);
    if (!strcmp(o, "=")) r = c == 0;
    else if (!strcmp(o, "!=")) r = c != 0;
    else if (!strcmp(o, "<")) r = c < 0;
    else if (!strcmp(o, ">")) r = c > 0;
    else if (!strcmp(o, "<=")) r = c <= 0;
    else r = c >= 0;
    return pp_fx(r);
}

static PPV pp_and(TV *in, long *pi)
{
    PPV a = pp_cmp(in, pi), b;
    Tok *t;
    while (tisop(t = pp_tk(in, *pi), "&")) {
        (*pi)++;
        b = pp_cmp(in, pi);
        a = pp_fx(pp_true(a, t->line) & pp_true(b, t->line));
    }
    return a;
}

static PPV pp_expr(TV *in, long *pi)
{
    PPV a = pp_and(in, pi), b;
    Tok *t;
    while (tisop(t = pp_tk(in, *pi), "|")) {
        (*pi)++;
        b = pp_and(in, pi);
        a = pp_fx(pp_true(a, t->line) | pp_true(b, t->line));
    }
    return a;
}

/* ---- text substitution ---- */

static long pp_text(TV *in, long i, TV *out);

static void pp_emit(PPV v, Tok *at, TV *out, int resc, PVar *src)
{
    TV sub;
    Tok t;
    long k;
    if (!v.ty) {
        memset(&t, 0, sizeof t);
        t.line = at->line;
        t.file = at->file;
        if (v.n < 0) {
            t.k = T_OP;
            t.s = "-";
            tv_add(out, t);
        }
        t.k = T_NUM;
        t.dv = (double)(v.n < 0 ? -v.n : v.n);
        t.s = xdup(pp_str(pp_fx(v.n < 0 ? -v.n : v.n)));
        tv_add(out, t);
        return;
    }
    memset(&sub, 0, sizeof sub);
    lexbuf(&sub, v.s);
    for (k = 0; k < sub.n; k++) {
        sub.a[k].line = at->line;
        sub.a[k].file = at->file;
    }
    if (!resc) {
        for (k = 0; k < sub.n; k++) tv_add(out, sub.a[k]);
        return;
    }
    if (pp_nexp >= 40)
        perr(at->line, "preprocessor rescan too deep", 0);
    pp_exp[pp_nexp++] = src;
    k = 0;
    while (k < sub.n) k = pp_text(&sub, k, out);
    pp_nexp--;
}

static int pp_isexp(PVar *v)
{
    int i;
    for (i = 0; i < pp_nexp; i++)
        if (pp_exp[i] == v) return 1;
    return 0;
}

static long pp_text(TV *in, long i, TV *out)
{
    Tok *t = &in->a[i];
    PVar *v;
    PPV a[8], r;
    long j;
    int na;
    if (t->k == T_ID && (v = pp_find(t->s)) != 0 && v->active &&
        !pp_isexp(v)) {
        if (v->kind == V_TOK) {
            Tok r2 = v->t;
            r2.line = t->line;
            r2.file = t->file;
            tv_add(out, r2);
            return i + 1;
        }
        if (v->kind == V_VAR) {
            pp_emit(v->v, t, out, v->resc, v);
            return i + 1;
        }
        j = i + 1;
        if (tisop(pp_tk(in, j), "(")) na = pp_args(in, &j, a, 8);
        else na = 0;
        r = pp_call(v, a, na, t->line);
        pp_emit(r, t, out, v->resc, v);
        return j;
    }
    tv_add(out, *t);
    return i + 1;
}

/* ---- statements ---- */

static long pp_seq(TV *in, long i, int act, TV *out);
static void preproc(TV *in, TV *out, int depth);

static long pp_semi(TV *in, long j)
{
    while (j < in->n && !tisop(&in->a[j], ";")) j++;
    return j < in->n ? j + 1 : j;
}

/* from the % of a %END: past the closing semicolon */
static long pp_endpos(TV *in, long e, int line)
{
    long j = e + 2;
    if (e >= in->n) perr(line, "missing %END", 0);
    if (pp_tk(in, j)->k == T_ID) j++;
    if (!tisop(pp_tk(in, j), ";")) perr(line, "bad %END", 0);
    return j + 1;
}

static PPV pp_call(PVar *v, PPV *a, int na, int line)
{
    int save = npv, p;
    TV junk;
    PPV res;
    if (na != v->np)
        perr(line, "wrong number of preprocessor arguments", v->name);
    for (p = 0; p < na; p++) {
        PVar *q = pp_new(v->pn[p], V_VAR, line);
        q->v = a[p];
    }
    pp_ret = 0;
    pp_rv = v->rty == 1 ? pp_ch("") : pp_fx(0);
    memset(&junk, 0, sizeof junk);
    pp_seq(v->in, v->body, 1, &junk);
    res = pp_rv;
    pp_ret = 0;
    npv = save;
    return v->rty == 1 ? pp_ch(pp_str(res)) : pp_fx(pp_num(res, line));
}

static void pp_dcl(TV *in, long *pi, int line)
{
    long j = *pi;
    int base = npv, names[16], nn, k, ty, i;
    char *nm[16];
    (void)base;
    for (;;) {
        nn = 0;
        if (tisop(pp_tk(in, j), "(")) {
            j++;
            while (!tisop(pp_tk(in, j), ")")) {
                if (pp_tk(in, j)->k == T_ID && nn < 16)
                    nm[nn++] = pp_tk(in, j)->s;
                j++;
                if (pp_tk(in, j)->k == T_EOF)
                    perr(line, "bad %DECLARE", 0);
            }
            j++;
        } else if (pp_tk(in, j)->k == T_ID) {
            nm[nn++] = pp_tk(in, j++)->s;
        } else {
            perr(line, "bad %DECLARE", 0);
        }
        ty = -1;
        while (pp_tk(in, j)->k == T_ID || tisop(pp_tk(in, j), "(")) {
            Tok *t = pp_tk(in, j);
            if (tisop(t, "(")) {
                int d = 0;
                do {
                    if (tisop(pp_tk(in, j), "(")) d++;
                    if (tisop(pp_tk(in, j), ")")) d--;
                    j++;
                } while (d > 0 && pp_tk(in, j)->k != T_EOF);
                continue;
            }
            if (!strcmp(t->s, "fixed")) ty = 0;
            else if (!strcmp(t->s, "char") ||
                     !strcmp(t->s, "character")) ty = 1;
            j++;
        }
        (void)names;
        for (k = 0; k < nn; k++) {
            PVar *v = 0;
            for (i = npv - 1; i >= 0 && !v; i--)
                if (!strcmp(pvs[i].name, nm[k]) &&
                    pvs[i].kind == V_VAR) v = &pvs[i];
            if (!v) v = pp_new(nm[k], V_VAR, line);
            v->dty = ty;
            v->v = ty == 1 ? pp_ch("") : pp_fx(0);
        }
        if (tisop(pp_tk(in, j), ",")) { j++; continue; }
        break;
    }
    if (!tisop(pp_tk(in, j), ";")) perr(line, "bad %DECLARE", 0);
    *pi = j + 1;
}

static void pp_activate(TV *in, long *pi, int on, int line)
{
    long j = *pi;
    PVar *v;
    for (;;) {
        Tok *t = pp_tk(in, j);
        if (t->k != T_ID) perr(line, "bad %ACTIVATE", 0);
        v = pp_find(t->s);
        if (!v) perr(line, "undeclared preprocessor name", t->s);
        v->active = on;
        j++;
        if (tisid(pp_tk(in, j), "rescan")) { v->resc = 1; j++; }
        else if (tisid(pp_tk(in, j), "norescan")) { v->resc = 0; j++; }
        if (tisop(pp_tk(in, j), ",")) { j++; continue; }
        break;
    }
    if (!tisop(pp_tk(in, j), ";")) perr(line, "bad %ACTIVATE", 0);
    *pi = j + 1;
}

static void pp_replace(TV *in, long *pi, int line)
{
    long i = *pi;
    while (i < in->n && !tisop(&in->a[i], ";")) {
        PVar *v;
        if (in->a[i].k != T_ID || i + 2 >= in->n ||
            !tisid(&in->a[i + 1], "by"))
            perr(line, "bad %REPLACE", 0);
        v = pp_new(in->a[i].s, V_TOK, line);
        v->t = in->a[i + 2];
        v->active = 1;
        i += 3;
        if (tisop(pp_tk(in, i), ",")) i++;
    }
    *pi = i + 1;
}

static void doinclude(TV *out, const char *nm, int line, int depth)
{
    static const char *ext[] = { "", ".inc", ".pli", 0 };
    char fn[400];
    char *src;
    TV *sub;
    int i;
    char *save = curfile;
    if (depth > 8) perr(line, "%INCLUDE nested too deeply", 0);
    src = 0;
    for (i = 0; ext[i] && !src; i++) {
        sprintf(fn, "%.250s%s", nm, ext[i]);
        src = rdsrc(fn);
        if (!src && strrchr(curfile, '/')) {
            char dir[300];
            long dl = (long)(strrchr(curfile, '/') - curfile) + 1;
            if (dl > 200) dl = 200;
            memcpy(dir, curfile, (size_t)dl);
            dir[dl] = 0;
            sprintf(fn, "%s%.80s%s", dir, nm, ext[i]);
            src = rdsrc(fn);
        }
    }
    if (!src) perr(line, "cannot open include file", nm);
    sub = (TV *)xalloc((long)sizeof *sub);
    memset(sub, 0, sizeof *sub);
    curfile = xdup(fn);
    lexbuf(sub, src);
    preproc(sub, out, depth + 1);
    curfile = save;
}

static int pp_incdepth = 0;

static void pp_proc(TV *in, long j, int line, int act, long *pend)
{
    PVar *v = 0;
    long k;
    char *nm = in->a[j].s;
    int np = 0;
    char *pn[8];
    int rty = 0;
    j += 2;                                   /* the label and colon */
    if (!tisid(pp_tk(in, j), "procedure") &&
        !tisid(pp_tk(in, j), "proc"))
        perr(line, "unsupported preprocessor label", nm);
    j++;
    if (tisop(pp_tk(in, j), "(")) {
        j++;
        while (!tisop(pp_tk(in, j), ")")) {
            if (pp_tk(in, j)->k == T_ID && np < 8)
                pn[np++] = pp_tk(in, j)->s;
            else if (!tisop(pp_tk(in, j), ",") || np >= 8)
                perr(line, "bad %PROCEDURE parameters", 0);
            j++;
        }
        j++;
    }
    while (!tisop(pp_tk(in, j), ";")) {
        if (tisid(pp_tk(in, j), "returns")) {
            k = j + 1;
            while (!tisop(pp_tk(in, k), ")") &&
                   pp_tk(in, k)->k != T_EOF) {
                if (tisid(pp_tk(in, k), "char") ||
                    tisid(pp_tk(in, k), "character")) rty = 1;
                k++;
            }
            j = k;
        }
        if (pp_tk(in, j)->k == T_EOF) perr(line, "bad %PROCEDURE", 0);
        j++;
    }
    j++;
    if (act) {
        v = pp_new(nm, V_PROC, line);
        v->in = in;
        v->body = j;
        v->np = np;
        v->rty = rty;
        for (k = 0; k < np; k++) v->pn[k] = pn[k];
    }
    *pend = pp_seq(in, j, 0, 0);
}

static long pp_do(TV *in, long i, int act, TV *out)
{
    long j = i + 2, bs, e, ws = 0, lim = 0;
    int line = in->a[i].line, kind;
    PVar *lv = 0;
    PPV st, to, by;
    long cnt = 0;
    if (tisop(pp_tk(in, j), ";")) kind = 0;
    else if (tisid(pp_tk(in, j), "while")) { kind = 1; ws = j + 1; }
    else if (pp_tk(in, j)->k == T_ID && tisop(pp_tk(in, j + 1), "="))
        kind = 2;
    else { perr(line, "bad %DO", 0); kind = 0; }
    if (kind == 2 && act) {
        lv = pp_find(in->a[j].s);
        if (!lv || lv->kind != V_VAR)
            perr(line, "undeclared %DO variable", in->a[j].s);
        j += 2;
        st = pp_expr(in, &j);
        to = pp_fx(0);
        by = pp_fx(1);
        for (;;) {
            if (tisid(pp_tk(in, j), "to")) {
                j++;
                to = pp_expr(in, &j);
                cnt |= 1;
            } else if (tisid(pp_tk(in, j), "by")) {
                j++;
                by = pp_expr(in, &j);
            } else break;
        }
        if (!(cnt & 1)) perr(line, "%DO needs TO", 0);
        if (!tisop(pp_tk(in, j), ";")) perr(line, "bad %DO", 0);
    } else {
        j = pp_semi(in, j) - 1;
    }
    bs = j + 1;
    e = pp_seq(in, bs, 0, 0);                 /* find the end */
    if (!act) return pp_endpos(in, e, line);
    if (kind == 0) {
        pp_seq(in, bs, 1, out);
    } else if (kind == 1) {
        for (;;) {
            long k = ws;
            if (!pp_true(pp_expr(in, &k), line)) break;
            pp_seq(in, bs, 1, out);
            if (pp_ret) break;
            if (++lim > 1000000) perr(line, "%DO WHILE runs away", 0);
        }
    } else {
        long v = pp_num(st, line), t = pp_num(to, line);
        long s = pp_num(by, line);
        if (s == 0) perr(line, "%DO BY 0", 0);
        for (; s > 0 ? v <= t : v >= t; v += s) {
            pp_assign(lv, pp_fx(v), line);
            pp_seq(in, bs, 1, out);
            if (pp_ret) break;
            v = pp_num(lv->v, line);
            if (++lim > 1000000) perr(line, "%DO runs away", 0);
        }
    }
    return pp_endpos(in, e, line);
}

static long pp_branch(TV *in, long j, int act, TV *out);

static long pp_pst(TV *in, long i, int act, TV *out)
{
    long j = i + 1, n = in->n;
    Tok *t = &in->a[i];
    char *d;
    int line = t->line;
    if (j < n && tisop(&in->a[j], ";")) return j + 1;
    if (j >= n || in->a[j].k != T_ID) {
        if (act) tv_add(out, *t);
        return i + 1;
    }
    d = in->a[j].s;
    if (!strcmp(d, "if")) {
        int c = 0;
        j++;
        if (act) c = pp_true(pp_expr(in, &j), line);
        else while (j < n && !tisop(&in->a[j], "%")) j++;
        if (!(tisop(pp_tk(in, j), "%") &&
              tisid(pp_tk(in, j + 1), "then")))
            perr(line, "missing %THEN", 0);
        j = pp_branch(in, j + 2, act && c, out);
        if (tisop(pp_tk(in, j), "%") &&
            tisid(pp_tk(in, j + 1), "else"))
            j = pp_branch(in, j + 2, act && !c, out);
        return j;
    }
    if (!strcmp(d, "do")) return pp_do(in, i, act, out);
    if (!strcmp(d, "end")) perr(line, "unmatched %END", 0);
    if (!strcmp(d, "declare") || !strcmp(d, "dcl")) {
        j++;
        if (act) pp_dcl(in, &j, line);
        else j = pp_semi(in, j);
        return j;
    }
    if (!strcmp(d, "activate") || !strcmp(d, "deactivate")) {
        j++;
        if (act) pp_activate(in, &j, d[0] == 'a', line);
        else j = pp_semi(in, j);
        return j;
    }
    if (!strcmp(d, "replace")) {
        j++;
        if (act) pp_replace(in, &j, line);
        else j = pp_semi(in, j);
        return j;
    }
    if (!strcmp(d, "include")) {
        char *nm;
        j++;
        if (tisop(pp_tk(in, j), "(")) j++;
        nm = pp_tk(in, j)->s;
        j = pp_semi(in, j);
        if (act) doinclude(out, nm, line, pp_incdepth);
        return j;
    }
    if (!strcmp(d, "return")) {
        j++;
        if (act) {
            if (tisop(pp_tk(in, j), "(")) j++;
            pp_rv = pp_expr(in, &j);
            pp_ret = 1;
        }
        return pp_semi(in, j);
    }
    if (!strcmp(d, "note")) {
        j++;
        if (act) {
            PPV m, c;
            if (tisop(pp_tk(in, j), "(")) j++;
            m = pp_expr(in, &j);
            c = pp_fx(0);
            if (tisop(pp_tk(in, j), ",")) {
                j++;
                c = pp_expr(in, &j);
            }
            fprintf(stderr, "%s: note: %s\n", progname, pp_str(m));
            if (pp_num(c, line) >= 8)
                perr(line, "preprocessor note", pp_str(m));
        }
        return pp_semi(in, j);
    }
    if (!strcmp(d, "page") || !strcmp(d, "skip") ||
        !strcmp(d, "noprint") || !strcmp(d, "print") ||
        !strcmp(d, "push") || !strcmp(d, "pop"))
        return pp_semi(in, j);
    if (j + 1 < n && tisop(&in->a[j + 1], ":")) {
        long e;
        pp_proc(in, j, line, act, &e);
        return pp_endpos(in, e, line);
    }
    if (j + 1 < n && tisop(&in->a[j + 1], "=")) {
        if (act) {
            PVar *v = pp_find(d);
            PPV x;
            if (!v) perr(line, "undeclared preprocessor name", d);
            j += 2;
            x = pp_expr(in, &j);
            pp_assign(v, x, line);
            if (!tisop(pp_tk(in, j), ";"))
                perr(line, "bad preprocessor assignment", 0);
            return j + 1;
        }
        return pp_semi(in, j);
    }
    perr(line, "unsupported preprocessor statement %", d);
    return j;
}

/* the unit after %THEN or %ELSE: a % statement or text up to a ; */
static long pp_branch(TV *in, long j, int act, TV *out)
{
    int semi;
    if (tisop(pp_tk(in, j), "%")) return pp_pst(in, j, act, out);
    do {
        if (j >= in->n) break;
        semi = tisop(&in->a[j], ";");
        j = act ? pp_text(in, j, out) : j + 1;
    } while (!semi);
    return j;
}

static long pp_seq(TV *in, long i, int act, TV *out)
{
    while (i < in->n) {
        Tok *t = &in->a[i];
        int a = act && !pp_ret;
        if (tisop(t, "%") && tisid(pp_tk(in, i + 1), "end")) return i;
        if (tisop(t, "%")) i = pp_pst(in, i, a, out);
        else if (a) i = pp_text(in, i, out);
        else i++;
    }
    return i;
}

static void preproc(TV *in, TV *out, int depth)
{
    long i;
    int save = pp_incdepth;
    pp_incdepth = depth;
    i = pp_seq(in, 0, 1, out);
    if (i < in->n) perr(in->a[i].line, "unmatched %END", 0);
    pp_incdepth = save;
}

/* -Dname[=value] on the command line */
static void pp_define(const char *def)
{
    char *nm = xdup(def), *eq = strchr(nm, '=');
    PVar *v;
    char *e;
    long n;
    int i;
    if (eq) *eq++ = 0;
    for (i = 0; nm[i]; i++)
        nm[i] = (char)tolower((unsigned char)nm[i]);
    v = pp_new(nm, V_VAR, 0);
    v->active = 1;
    if (!eq) { v->v = pp_fx(1); return; }
    n = strtol(eq, &e, 10);
    if (*eq && !*e) v->v = pp_fx(n);
    else v->v = pp_ch(eq);
}

static TV tokens;
static long ptk = 0;
static int lastline = 1;

static void lexfile(const char *name)
{
    char *src = rdsrc(name);
    TV raw;
    long i;
    if (!src) perr(0, "cannot open source file", name);
    if (nsrc >= MAXSRC) perr(0, "too many source files", 0);
    memset(&raw, 0, sizeof raw);
    curfile = xdup(name);
    srcfiles[nsrc++] = curfile;
    lexbuf(&raw, src);
    for (i = 0; i < raw.n; i++) raw.a[i].line += nsrc * LINEMUL;
    preproc(&raw, &tokens, 0);
    lastline = raw.n ? raw.a[raw.n - 1].line : 1;
}

/* end of all source files: add the end marks */
static void lexend(void)
{
    Tok e;
    memset(&e, 0, sizeof e);
    e.k = T_EOF;
    e.s = "";
    e.line = lastline;
    tv_add(&tokens, e);
    tv_add(&tokens, e);
    tv_add(&tokens, e);
}
