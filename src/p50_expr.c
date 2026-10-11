/* p50_expr.c -- expression translation */

enum { K_N = 1, K_S, K_B, K_P, K_V };

static int ge_q = -1, ge_f = 0, uses_tmp = 0;
static int subs_chk = 0;

typedef struct { char *t; Sym *s; int nu; } LV;

static char *ge(Node *e, int *k);
static LV rl(Node *r);
static char *gcall(Sym *p, Node *e, int *k);
static char *gbuiltin(Node *e, int *k);

static char *newtemp(const char *ct)
{
    char *nm;
    if (!cur) die("internal: temp outside procedure");
    nm = mkcn("t", ++idc, "");
    sb_s(&cur->decl, cat5("    ", ct, " ", nm,
         !strcmp(ct, "PLF") ? " = { 0 };\n" : ";\n"));
    return nm;
}

static char *clit(const char *s, long n)
{
    Sb b;
    long i;
    char o[8];
    int c;
    memset(&b, 0, sizeof b);
    sb_c(&b, '"');
    for (i = 0; i < n; i++) {
        c = (unsigned char)s[i];
        if (i > 0 && i % 24 == 0) sb_s(&b, "\" \"");
        if (c == '"' || c == '\\' || c == '?') {
            sb_c(&b, '\\');
            sb_c(&b, c);
        } else if (isprint(c)) {
            sb_c(&b, c);
        } else {
            sprintf(o, "\\%03o", c & 255);
            sb_s(&b, o);
        }
    }
    sb_c(&b, '"');
    return xdup(sb_str(&b));
}

static char *cnum(Node *e);
static char *cstr(Node *e);

static char *conv_num(char *t, int k, int line)
{
    if (k == K_N) return t;
    if (k == K_S) { uses_tmp = 1; return cat3("pl_s2n(", t, ")"); }
    if (k == K_B) return cat3("pl_b2n(", t, ")");
    perr(line, "numeric value expected", 0);
    return t;
}

static char *cnum(Node *e)
{
    int k, q, f;
    char *t = ge(e, &k);
    q = ge_q;
    f = ge_f;
    t = conv_num(t, k, e->line);
    if (k != K_N) { q = -1; f = 0; }
    ge_q = q;
    ge_f = f;
    return t;
}

static char *cstr(Node *e)
{
    int k;
    char *t = ge(e, &k);
    if (k == K_S || k == K_B) return t;
    if (k == K_N) {
        uses_tmp = 1;
        return cat5("pl_n2s(", t, ", ", cat3(itoa_(ge_q), ", ",
                    itoa_(ge_f)), ")");
    }
    perr(e->line, "string value expected", 0);
    return t;
}

static char *cbit(Node *e)
{
    int k;
    char *t = ge(e, &k);
    if (k == K_B || k == K_S) return t;
    if (k == K_N) {
        uses_tmp = 1;
        return cat3("pl_n2b(", t, ", 1)");
    }
    perr(e->line, "bit value expected", 0);
    return t;
}

static char *cptr(Node *e)
{
    int k;
    char *t = ge(e, &k);
    if (k != K_P) perr(e->line, "pointer value expected", 0);
    return t;
}

/* truth value of an expression as a C int expression */
static char *cond(Node *e)
{
    int k;
    char *t = ge(e, &k);
    if (k == K_N) return cat3("(", t, " != 0.0)");
    if (k == K_B || k == K_S) return cat3("pl_tb(", t, ")");
    if (k == K_P) return cat3("(", t, " != 0)");
    perr(e->line, "condition expected", 0);
    return t;
}

/* ---- references ---- */

static Sym *implicit(const char *name)
{
    Sym *s = newsym(root, name, C_VAR);
    s->level = 1;
    set_default_type(s);
    finish_sym(s);
    return s;
}

static char *top_text(Sym *s, Node *pe)
{
    char *ptr;
    if (s->stor == ST_BASED) {
        if (pe) ptr = cptr(pe);
        else if (s->based) ptr = cptr(s->based);
        else perr(0, "BASED variable needs a pointer:", s->name);
        return cat5("(*(", s->tn, " *)(", ptr, "))");
    }
    if (s->stor == ST_CTL)
        return cat5("(*(", s->tn, " *)(", s->hptr->cn, "))");
    if (s->isparam) {
        if (s->ty == TY_STRUCT) return cat3("(*", s->cn, ")");
        return s->cn;
    }
    return s->cn;
}

static char *path_text(Sym *s, Node *pe, int line)
{
    char *t;
    if (!s->par) return top_text(s, pe);
    if (s->par->nd)
        perr(line, "subscript needed for structure", s->par->name);
    t = path_text(s->par, pe, line);
    return cat3(t, ".", s->cn ? s->cn : membname(s));
}

static char *subtext(Sym *s, Node *args, int line, int *nu, char *t)
{
    int i, n = 0;
    Node *a;
    char *ix, *lo;
    *nu = 0;
    if (!s->nd) {
        if (args) perr(line, "not an array:", s->name);
        return t;
    }
    if (!args) {
        *nu = s->nd;
        return t;
    }
    for (a = args; a; a = a->next) n++;
    if (n != s->nd) perr(line, "wrong number of subscripts:", s->name);
    for (i = 0, a = args; a; a = a->next, i++) {
        ix = cnum(a);
        lo = itoa_(s->lo[i]);
        if (subs_chk && !s->star && !s->isparam)
            ix = cat5("pl_ix(", ix, ", ", cat3(lo, ", ",
                      itoa_(s->hi[i])), ")");
        else
            ix = cat3("(long)(", ix, ")");
        if (s->lo[i] != 0) ix = cat3(ix, " - ", lo);
        t = cat4(t, "[", ix, "]");
    }
    return t;
}

/* find member nm below s; *path receives the C path to it */
static Sym *findmember(Sym *s, const char *nm, char **path)
{
    Sym *k, *r;
    for (k = s->kid; k; k = k->nxt) {
        if (!k->cn) k->cn = membname(k);
        if (!strcmp(k->name, nm)) {
            *path = k->cn;
            return k;
        }
    }
    for (k = s->kid; k; k = k->nxt) {
        if (k->ty == TY_STRUCT && !k->nd) {
            char *sub;
            r = findmember(k, nm, &sub);
            if (r) {
                *path = cat3(k->cn, ".", sub);
                return r;
            }
        }
    }
    return 0;
}

static LV rl(Node *r)
{
    LV v;
    Sym *s, *top, *m;
    char *t, *mp;
    Node *q;
    Node *pe = r->c;
    int nu;
    s = lookup(r->s);
    if (!s) {
        if (r->c || r->fl) perr(r->line, "undeclared name", r->s);
        s = implicit(r->s);
    }
    if (s->cls != C_VAR) perr(r->line, "not a variable:", r->s);
    if (pe) {
        for (top = s; top->par; top = top->par) ;
        if (top->stor != ST_BASED)
            perr(r->line, "not a BASED variable:", r->s);
    }
    t = path_text(s, pe, r->line);
    t = subtext(s, r->a, r->line, &nu, t);
    for (q = r->b; q; q = q->b) {
        if (nu) perr(q->line, "subscripts needed before .", 0);
        m = findmember(s, q->s, &mp);
        if (!m) perr(q->line, "no such member:", q->s);
        t = cat3(t, ".", mp);
        s = m;
        t = subtext(s, q->a, q->line, &nu, t);
    }
    v.t = t;
    v.s = s;
    v.nu = nu;
    return v;
}

static char *pvtxt(LV v)
{
    Sym *s = v.s;
    char *t = v.t;
    if (s->ispv && !s->par && t == s->cn) return s->cn;
    if (s->len < 0) perr(0, "string of unknown length:", s->name);
    if (s->vary)
        return cat5("pl_pv(&", t, ".n, ", cat2(t, ".s, "),
                    cat2(itoa_(s->len), ")"));
    return cat5("pl_pf(", t, ", ", itoa_(s->len), ")");
}

static char *ref_value(Node *e, int *k)
{
    LV v = rl(e);
    Sym *s = v.s;
    ge_q = -1;
    ge_f = 0;
    if (v.nu)
        perr(e->line, "array used as a value:", s->name);
    switch (s->ty) {
    case TY_FXB: case TY_FXD: case TY_FLT:
        *k = K_N;
        if (s->ty == TY_FLT) ge_f = s->p;
        else ge_q = s->q;
        return is_long(s) ? cat3("((double)", v.t, ")") : v.t;
    case TY_CHR:
        *k = K_S;
        return cat3("pl_ps(", pvtxt(v), ")");
    case TY_BIT:
        *k = K_B;
        return cat3("pl_ps(", pvtxt(v), ")");
    case TY_PTR:
        *k = K_P;
        return v.t;
    }
    perr(e->line, "cannot use in an expression:", s->name);
    return "";
}

/* ---- calls ---- */

static int same_store(Sym *a, Sym *b)
{
    return a->ty == b->ty && is_long(a) == is_long(b);
}

static char *argp(Sym *pm, Node *a)
{
    LV v;
    int k;
    Sym *x = 0;
    if (a->k == X_REF && !a->c && lookup(a->s) &&
        lookup(a->s)->cls == C_VAR) {
        v = rl(a);
        x = v.s;
        if (v.nu && !pm->nd)
            perr(a->line, "array passed to scalar:", pm->name);
    }
    if (pm->nd) {
        if (!x || !v.nu) perr(a->line, "array argument expected", 0);
        return cat3("(void *)", v.t, "");
    }
    if (pm->ty == TY_STRUCT) {
        if (!x || x->ty != TY_STRUCT)
            perr(a->line, "structure argument expected", 0);
        return cat2("(void *)&", v.t);
    }
    if (is_str(pm)) {
        if (x && is_str(x) && !v.nu) return pvtxt(v);
        uses_tmp = 1;
        return cat3("pl_tmp(", cstr(a), ")");
    }
    if (pm->ty == TY_PTR) {
        if (x && x->ty == TY_PTR && !v.nu) return cat2("&", v.t);
        uses_tmp = 1;
        return cat3("pl_tpp(", cptr(a), ")");
    }
    if (is_num(pm)) {
        if (x && is_num(x) && !v.nu && same_store(x, pm) &&
            x->p == pm->p && x->q == pm->q)
            return cat2("&", v.t);
        uses_tmp = 1;
        if (is_long(pm)) return cat3("pl_tlp(", cnum(a), ")");
        return cat3("pl_tdp(", cnum(a), ")");
    }
    (void)k;
    perr(a->line, "unsupported argument type for", pm->name);
    return "";
}

static char *gcall(Sym *p, Node *e, int *k)
{
    Sb b;
    Sym *pm;
    Node *a;
    int n = 0, first = 1;
    if (!p->pn) perr(e->line, "procedure has no definition:", p->name);
    memset(&b, 0, sizeof b);
    sb_s(&b, p->cn);
    sb_c(&b, '(');
    a = e->a;
    for (pm = p->params; pm; pm = pm->params) {
        if (!a) perr(e->line, "too few arguments to", p->name);
        if (!first) sb_s(&b, ", ");
        first = 0;
        sb_s(&b, argp(pm, a));
        a = a->next;
        n++;
    }
    if (a) perr(e->line, "too many arguments to", p->name);
    sb_c(&b, ')');
    ge_q = -1;
    ge_f = 0;
    switch (p->ty) {
    case TY_FXB: case TY_FXD: case TY_FLT:
        *k = K_N;
        if (p->ty == TY_FLT) ge_f = p->p; else ge_q = p->q;
        break;
    case TY_CHR: *k = K_S; uses_tmp = 1; break;
    case TY_BIT: *k = K_B; uses_tmp = 1; break;
    case TY_PTR: *k = K_P; break;
    default: *k = K_V; break;
    }
    return xdup(sb_str(&b));
}

/* ---- operators ---- */

static char *cmpop(const char *o)
{
    if (!strcmp(o, "=")) return "==";
    return (char *)o;
}

static int iscmp_s(const char *o)
{
    return !strcmp(o, "<") || !strcmp(o, ">") || !strcmp(o, "=") ||
           !strcmp(o, "<=") || !strcmp(o, ">=") || !strcmp(o, "!=");
}

static char *gbin(Node *e, int *k)
{
    char *o = e->s;
    char *a, *b;
    int ka, kb, q1, q2, f1, f2;
    if (!strcmp(o, "||")) {
        a = ge(e->a, &ka);
        b = ge(e->b, &kb);
        if (ka == K_N) a = cstr(e->a);
        if (kb == K_N) b = cstr(e->b);
        uses_tmp = 1;
        *k = (ka == K_B && kb == K_B) ? K_B : K_S;
        ge_q = -1;
        ge_f = 0;
        return cat5("pl_cat(", a, ", ", b, ")");
    }
    if (!strcmp(o, "&") || !strcmp(o, "|")) {
        a = ge(e->a, &ka);
        b = ge(e->b, &kb);
        if (ka == K_N && kb == K_N) {
            *k = K_N;
            ge_q = -1;
            ge_f = 0;
            return cat5("((", a, o[0] == '&' ? " != 0.0) && (" :
                        " != 0.0) || (", b, " != 0.0))");
        }
        if (ka == K_N) a = cbit(e->a);
        if (kb == K_N) b = cbit(e->b);
        uses_tmp = 1;
        *k = K_B;
        ge_q = -1;
        ge_f = 0;
        return cat5("pl_bop(", a, ", ", b, o[0] == '&' ? ", 0)" :
                    ", 1)");
    }
    if (iscmp_s(o)) {
        a = ge(e->a, &ka);
        b = ge(e->b, &kb);
        *k = K_N;
        ge_q = -1;
        ge_f = 0;
        if (ka == K_P || kb == K_P) {
            if (ka != K_P || kb != K_P || (o[0] != '=' && o[0] != '!'))
                perr(e->line, "bad pointer comparison", 0);
            return cat5("(", a, o[0] == '=' ? " == " : " != ", b, ")");
        }
        if (ka == K_N || kb == K_N) {
            a = conv_num(a, ka, e->line);
            b = conv_num(b, kb, e->line);
            return cat5("(", a, cat3(" ", cmpop(o), " "), b, ")");
        }
        uses_tmp = 1;
        return cat5("(pl_cmp(", a, ", ", b,
                    cat3(") ", cmpop(o), " 0)"));
    }
    /* arithmetic */
    a = cnum(e->a);
    q1 = ge_q; f1 = ge_f;
    b = cnum(e->b);
    q2 = ge_q; f2 = ge_f;
    *k = K_N;
    ge_q = -1;
    ge_f = 0;
    if (o[0] == '+' || o[0] == '-') {
        if (q1 >= 0 && q2 >= 0) ge_q = q1 > q2 ? q1 : q2;
        else if (f1 || f2) ge_f = f1 > f2 ? f1 : f2;
        return cat5("(", a, cat3(" ", o, " "), b, ")");
    }
    if (o[0] == '*' && o[1] == '*') {
        if (f1 || f2) ge_f = f1 > f2 ? f1 : f2;
        return cat5("pl_pow(", a, ", ", b, ")");
    }
    if (o[0] == '*') {
        if (q1 >= 0 && q2 >= 0) ge_q = q1 + q2;
        else if (f1 || f2) ge_f = f1 > f2 ? f1 : f2;
        return cat5("(", a, " * ", b, ")");
    }
    if (f1 || f2) ge_f = f1 > f2 ? f1 : f2;
    return cat5("pl_div(", a, ", ", b, ")");
}

static char *ge(Node *e, int *k)
{
    char *t, *a;
    int ka;
    switch (e->k) {
    case X_NUM:
        *k = K_N;
        ge_q = e->fl ? -1 : e->n;
        ge_f = e->fl ? (e->n > 0 ? e->n : 1) : 0;
        return dtoa_(e->dv);
    case X_STR:
        *k = e->fl ? K_B : K_S;
        ge_q = -1;
        ge_f = 0;
        return cat5("pl_mk(", clit(e->s, (long)strlen(e->s)), ", ",
                    itoa_((long)strlen(e->s)), ")");
    case X_UN:
        if (e->s[0] == '!') {
            a = ge(e->a, &ka);
            if (ka == K_N) {
                *k = K_N;
                ge_q = -1; ge_f = 0;
                return cat3("(!(", a, " != 0.0))");
            }
            if (ka == K_B || ka == K_S) {
                uses_tmp = 1;
                *k = K_B;
                return cat3("pl_bnot(", a, ")");
            }
            perr(e->line, "bad operand for NOT", 0);
        }
        a = cnum(e->a);
        *k = K_N;
        return e->s[0] == '-' ? cat3("(-", a, ")") : a;
    case X_BIN:
        return gbin(e, k);
    case X_REF:
        if (!e->c) {
            Sym *s = lookup(e->s);
            if (s && s->cls == C_PROC) return gcall(s, e, k);
            if (!s && name_is_builtin(e->s))
                return gbuiltin(e, k);
            if (!s && e->fl)
                perr(e->line, "unknown function", e->s);
        }
        t = ref_value(e, k);
        return t;
    case X_STAR:
        perr(e->line, "* not allowed here", 0);
    }
    perr(e->line, "bad expression", 0);
    return "";
}
