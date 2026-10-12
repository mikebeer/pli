/* p30_parse.c -- recursive-descent parser: tokens to syntax tree */

#define TK (tokens.a[ptk])
#define TK1 (tokens.a[ptk + 1])
#define TK2 (tokens.a[ptk + 2])

static int isop(const char *s) { return tisop(&TK, s); }
static int isid(const char *s) { return tisid(&TK, s); }
static int isop1(const char *s) { return tisop(&TK1, s); }

static void nxt(void)
{
    if (TK.k != T_EOF) ptk++;
}

static void expop(const char *s)
{
    if (!isop(s)) perr(TK.line, "expected", s);
    nxt();
}

static void expkw(const char *s)
{
    if (!isid(s)) perr(TK.line, "expected", s);
    nxt();
}

static char *expid(void)
{
    char *s;
    if (TK.k != T_ID) perr(TK.line, "name expected near", TK.s);
    s = TK.s;
    nxt();
    return s;
}

/* index of the ')' matching the '(' at token i, or -1 */
static long matchp(long i)
{
    int d = 0;
    for (; tokens.a[i].k != T_EOF; i++) {
        if (tisop(&tokens.a[i], "(")) d++;
        else if (tisop(&tokens.a[i], ")") && --d == 0) return i;
    }
    return -1;
}

static Node *p_expr(void);
static Node *p_stmt(void);

static Node *p_args(void)
{
    Node *h = 0, *t = 0, *e;
    expop("(");
    if (isop(")")) { nxt(); return 0; }
    for (;;) {
        if (isop("*") && (isop1(",") || isop1(")"))) {
            e = newnode(X_STAR, TK.line);
            nxt();
        } else {
            e = p_expr();
        }
        if (t) t->next = e; else h = e;
        t = e;
        if (isop(",")) { nxt(); continue; }
        break;
    }
    expop(")");
    return h;
}

static Node *p_qual(void)
{
    Node *r = newnode(X_REF, TK.line), *q;
    r->s = expid();
    if (isop("(")) { r->fl = 1; r->a = p_args(); }
    q = r;
    while (isop(".") && TK1.k == T_ID) {
        Node *m;
        nxt();
        m = newnode(X_REF, TK.line);
        m->s = expid();
        if (isop("(")) { m->fl = 1; m->a = p_args(); }
        q->b = m;
        q = m;
    }
    return r;
}

static Node *p_ref(void)
{
    Node *r = p_qual(), *m;
    while (isop("->")) {
        nxt();
        m = p_qual();
        m->c = r;
        r = m;
    }
    return r;
}

static Node *p_prim(void)
{
    Node *n;
    if (TK.k == T_NUM) {
        n = newnode(X_NUM, TK.line);
        n->dv = TK.dv;
        n->fl = TK.fl;
        n->im = TK.im;
        n->s = TK.ord;
        if (!TK.fl && !TK.q && !TK.im && TK.dv >= 1e12 &&
            strlen(TK.s) >= 13) {
            n->a = newnode(X_STR, TK.line);
            n->a->s = TK.s;
        }
        n->n = TK.q;
        nxt();
        return n;
    }
    if (TK.k == T_STR) {
        n = newnode(X_STR, TK.line);
        n->s = TK.s;
        n->fl = TK.fl;
        nxt();
        return n;
    }
    if (isop("(")) {
        nxt();
        n = p_expr();
        expop(")");
        return n;
    }
    if (TK.k == T_ID) return p_ref();
    perr(TK.line, "expression expected near", TK.s);
    return 0;
}

static Node *p_un(void);

static Node *p_pow(void)
{
    Node *l = p_prim(), *n;
    if (isop("**")) {
        n = newnode(X_BIN, TK.line);
        nxt();
        n->s = "**";
        n->a = l;
        n->b = p_un();
        return n;
    }
    return l;
}

static Node *p_un(void)
{
    Node *n;
    if (isop("-") || isop("+") || isop("!")) {
        n = newnode(X_UN, TK.line);
        n->s = TK.s;
        nxt();
        n->a = p_un();
        return n;
    }
    return p_pow();
}

static Node *binop(Node *l, char *op, Node *r, int line)
{
    Node *n = newnode(X_BIN, line);
    n->s = op;
    n->a = l;
    n->b = r;
    return n;
}

static Node *p_mul(void)
{
    Node *l = p_un();
    while (isop("*") || isop("/")) {
        char *o = TK.s;
        int ln = TK.line;
        nxt();
        l = binop(l, o, p_un(), ln);
    }
    return l;
}

static Node *p_add(void)
{
    Node *l = p_mul();
    while (isop("+") || isop("-")) {
        char *o = TK.s;
        int ln = TK.line;
        nxt();
        l = binop(l, o, p_mul(), ln);
    }
    return l;
}

static Node *p_cat(void)
{
    Node *l = p_add();
    while (isop("||")) {
        int ln = TK.line;
        nxt();
        l = binop(l, "||", p_add(), ln);
    }
    return l;
}

static int iscmp(void)
{
    return isop("<") || isop(">") || isop("=") || isop("<=") ||
           isop(">=") || isop("!=");
}

static Node *p_cmp(void)
{
    Node *l = p_cat();
    while (iscmp()) {
        char *o = TK.s;
        int ln = TK.line;
        nxt();
        l = binop(l, o, p_cat(), ln);
    }
    return l;
}

static Node *p_and(void)
{
    Node *l = p_cmp();
    while (isop("&")) {
        int ln = TK.line;
        nxt();
        l = binop(l, "&", p_cmp(), ln);
    }
    return l;
}

static Node *p_expr(void)
{
    Node *l = p_and();
    while (isop("|")) {
        int ln = TK.line;
        nxt();
        l = binop(l, "|", p_and(), ln);
    }
    return l;
}

/* constant expression value */
static double cval(Node *e)
{
    double a, b;
    if (e->k == X_NUM) return e->dv;
    if (e->k == X_UN) {
        a = cval(e->a);
        return e->s[0] == '-' ? -a : a;
    }
    if (e->k == X_BIN) {
        a = cval(e->a);
        b = cval(e->b);
        switch (e->s[0]) {
        case '+': return a + b;
        case '-': return a - b;
        case '*':
            if (e->s[1] == '*') {
                double r = 1;
                long i, n = (long)b;
                for (i = 0; i < n; i++) r *= a;
                return r;
            }
            return a * b;
        case '/':
            if (b == 0) perr(e->line, "division by zero", 0);
            return a / b;
        }
    }
    perr(e->line, "constant expression expected", 0);
    return 0;
}

/* ---------- declarations ---------- */

static Node *p_dims(void)
{
    Node *h = 0, *t = 0, *b;
    expop("(");
    for (;;) {
        b = newnode(N_BND, TK.line);
        if (isop("*")) {
            nxt();
            b->fl = 1;
        } else {
            b->b = p_expr();
            if (isop(":")) { nxt(); b->a = b->b; b->b = p_expr(); }
        }
        if (t) t->next = b; else h = b;
        t = b;
        if (isop(",")) { nxt(); continue; }
        break;
    }
    expop(")");
    return h;
}

static void skipparens(void)
{
    long m;
    if (!isop("(")) return;
    m = matchp(ptk);
    if (m < 0) perr(TK.line, "unbalanced parentheses", 0);
    ptk = m + 1;
}

static void p_prec(Attr *a)
{
    if (!isop("(")) return;
    nxt();
    a->p = (int)cval(p_expr());
    if (isop(",")) { nxt(); a->q = (int)cval(p_expr()); }
    expop(")");
}

static Node *p_initlist(void)
{
    Node *h = 0, *t = 0, *it, *e;
    expop("(");
    for (;;) {
        it = newnode(N_ITEM, TK.line);
        it->n = 1;
        if (isop("*")) {
            nxt();
            it->a = 0;
            it->fl = 1;
        } else {
            e = p_expr();
            if (TK.k == T_NUM || TK.k == T_STR || TK.k == T_ID ||
                isop("(") || isop("-") || isop("+")) {
                it->n = (int)cval(e);
                e = p_expr();
            }
            it->a = e;
        }
        if (t) t->next = it; else h = it;
        t = it;
        if (isop(",")) { nxt(); continue; }
        break;
    }
    expop(")");
    return h;
}

static void p_attrs(Attr *a);
static Node *p_decl(void);

/* ENTRY(desc, desc, ...): one attribute list per parameter */
static void p_entry_desc(Attr *a)
{
    Attr *d, *last = 0;
    expop("(");
    a->hasdsc = 1;
    if (isop(")")) { nxt(); return; }
    for (;;) {
        d = (Attr *)xalloc((long)sizeof(Attr));
        d->len = -2;
        d->p = -1;
        d->q = -1;
        if (isop("*")) {
            nxt();
        } else {
            if (isop("(")) d->dims = p_dims();
            p_attrs(d);
        }
        if (last) last->dnext = d; else a->dsc = d;
        last = d;
        if (isop(",")) { nxt(); continue; }
        break;
    }
    expop(")");
}

/* OPTIONS(...) : only C and MAIN are looked at */
static int p_options(void)
{
    int fl = 0;
    expop("(");
    while (!isop(")")) {
        if (isid("main")) fl |= 2;
        if (isid("c")) fl |= 4;
        if (TK.k == T_EOF) perr(TK.line, "expected )", 0);
        nxt();
    }
    nxt();
    return fl;
}

/* ---- DEFINE STRUCTURE / ALIAS / ORDINAL ---- */

typedef struct {
    char *name;
    int kind;           /* 0 structure, 1 alias, 2 ordinal */
    Node *items;
    Attr *al;
    int nord, prec;
    char **onm;
    long *ov;
} TypeDef;
static TypeDef types[128];
static int ntypes = 0;

static Attr *newattr(void)
{
    Attr *a = (Attr *)xalloc((long)sizeof(Attr));
    a->len = -2;
    a->p = -1;
    a->q = -1;
    return a;
}

static int find_type(const char *nm)
{
    int i;
    for (i = 0; i < ntypes; i++)
        if (!strcmp(types[i].name, nm)) return i;
    return -1;
}

static TypeDef *new_type(const char *nm, int kind, int line)
{
    TypeDef *t;
    if (find_type(nm) >= 0)
        perr(line, "type already defined:", nm);
    if (ntypes >= 128) perr(line, "too many type definitions", 0);
    t = &types[ntypes++];
    memset(t, 0, sizeof *t);
    t->name = xdup(nm);
    t->kind = kind;
    return t;
}

static void apply_alias(Attr *a, Attr *al)
{
    if (al->ty) a->ty = al->ty;
    if (al->fix) a->fix = al->fix;
    if (al->base) a->base = al->base;
    if (al->p != -1) a->p = al->p;
    if (al->q != -1) a->q = al->q;
    if (al->len != -2) a->len = al->len;
    a->vary |= al->vary;
    a->cpx |= al->cpx;
    a->isfile |= al->isfile;
    a->uns |= al->uns;
    if (al->pic) a->pic = al->pic;
    if (al->ord) a->ord = al->ord;
    if (al->stype) a->stype = al->stype;
    if (al->htype) a->htype = al->htype;
}

/* the member items of a structure type at level base */
static Node *copy_members(Node *m, int base, Node **tail)
{
    Node *h = 0, *t = 0, *c;
    for (; m; m = m->next) {
        c = newnode(N_ITEM, m->line);
        *c = *m;
        c->next = 0;
        c->n = base + m->n - 1;
        if (t) t->next = c; else h = c;
        t = c;
    }
    *tail = t;
    return h;
}

/* a hidden BASED copy of the structure, for HANDLE */
static Node *hidden_struct(int ti, char **hname)
{
    Node *h, *tl;
    char b[100];
    sprintf(b, "hnd_%.80s", types[ti].name);
    h = copy_members(types[ti].items, 1, &tl);
    h->s = xdup(b);
    h->at = newattr();
    *h->at = *types[ti].items->at;
    h->at->stor = ST_BASED;
    *hname = h->s;
    return h;
}

static void p_define(int line)
{
    char *kind = expid(), *nm;
    TypeDef *t;
    if (!strcmp(kind, "alias")) {
        Attr *al = newattr();
        nm = expid();
        p_attrs(al);
        t = new_type(nm, 1, line);
        t->al = al;
    } else if (!strcmp(kind, "structure")) {
        Node *it;
        if (TK.k != T_NUM || TK1.k != T_ID)
            perr(line, "DEFINE STRUCTURE needs a level 1 name", 0);
        t = new_type(TK1.s, 0, line);
        it = p_decl();
        if (it->n > 1)
            perr(line, "DEFINE STRUCTURE needs a level 1 name", 0);
        t->items = it;
    } else if (!strcmp(kind, "ordinal")) {
        long v = 0, i;
        nm = expid();
        t = new_type(nm, 2, line);
        t->prec = 31;
        t->onm = (char **)xalloc(256 * (long)sizeof(char *));
        t->ov = (long *)xalloc(256 * (long)sizeof(long));
        expop("(");
        for (;;) {
            if (t->nord >= 256) perr(line, "ORDINAL too long", 0);
            t->onm[t->nord] = expid();
            if (isid("value")) {
                nxt();
                expop("(");
                v = (long)cval(p_expr());
                expop(")");
            }
            t->ov[t->nord++] = v++;
            if (isop(",")) { nxt(); continue; }
            break;
        }
        expop(")");
        while (TK.k == T_ID) {
            if (isid("prec") || isid("precision")) {
                nxt();
                expop("(");
                t->prec = (int)cval(p_expr());
                expop(")");
            } else if (isid("unsigned") || isid("signed")) {
                nxt();
            } else break;
        }
        /* the names are constants from here on */
        for (i = ptk; i < tokens.n; i++) {
            long j;
            Tok *x = &tokens.a[i];
            if (x->k != T_ID) continue;
            if (i > 0 && tisop(&tokens.a[i - 1], ".")) continue;
            for (j = 0; j < t->nord; j++)
                if (!strcmp(x->s, t->onm[j])) {
                    char b[40];
                    sprintf(b, "%ld", t->ov[j]);
                    x->k = T_NUM;
                    x->s = xdup(b);
                    x->dv = (double)t->ov[j];
                    x->q = 0;
                    x->fl = 0;
                    x->ord = t->name;
                }
        }
    } else {
        perr(line, "DEFINE needs ALIAS, STRUCTURE or ORDINAL", 0);
    }
}

static void p_attrs(Attr *a)
{
    char *k;
    while (TK.k == T_ID) {
        k = TK.s;
        if (!strcmp(k, "fixed")) { nxt(); a->fix = 1; p_prec(a); }
        else if (!strcmp(k, "float")) { nxt(); a->fix = 2; p_prec(a); }
        else if (!strcmp(k, "binary") || !strcmp(k, "bin")) {
            nxt(); a->base = 1; p_prec(a);
        } else if (!strcmp(k, "decimal") || !strcmp(k, "dec")) {
            nxt(); a->base = 2; p_prec(a);
        } else if (!strcmp(k, "character") || !strcmp(k, "char")) {
            nxt(); a->ty = TY_CHR;
            if (isop("(")) {
                nxt();
                if (isop("*")) { nxt(); a->len = -1; }
                else a->len = (int)cval(p_expr());
                expop(")");
            }
        } else if (!strcmp(k, "bit")) {
            nxt(); a->ty = TY_BIT;
            if (isop("(")) {
                nxt();
                if (isop("*")) { nxt(); a->len = -1; }
                else a->len = (int)cval(p_expr());
                expop(")");
            }
        } else if (!strcmp(k, "varying") || !strcmp(k, "var")) {
            nxt(); a->vary = 1;
        } else if (!strcmp(k, "pointer") || !strcmp(k, "ptr")) {
            nxt(); a->ty = TY_PTR;
        } else if (!strcmp(k, "offset")) {
            nxt(); a->ty = TY_PTR; skipparens();
        } else if (!strcmp(k, "area")) {
            nxt();
            a->ty = TY_AREA;
            a->len = 1000;
            if (isop("(")) {
                nxt();
                a->len = (int)cval(p_expr());
                expop(")");
            }
        } else if (!strcmp(k, "file")) {
            nxt(); a->ty = TY_FILE; a->isfile = 1;
        } else if (!strcmp(k, "label")) {
            nxt(); a->ty = TY_LBL; skipparens();
        } else if (!strcmp(k, "entry")) {
            nxt(); a->isent = 1;
            if (isop("(")) p_entry_desc(a);
        } else if (!strcmp(k, "returns")) {
            nxt(); expop("(");
            a->ret = (Attr *)xalloc((long)sizeof(Attr));
            p_attrs(a->ret);
            expop(")");
        } else if (!strcmp(k, "static")) { nxt(); a->stor = ST_STAT; }
        else if (!strcmp(k, "automatic") || !strcmp(k, "auto")) {
            nxt(); a->stor = ST_AUTO;
        } else if (!strcmp(k, "controlled") || !strcmp(k, "ctl")) {
            nxt(); a->stor = ST_CTL;
        } else if (!strcmp(k, "based")) {
            nxt(); a->stor = ST_BASED;
            if (isop("(")) {
                nxt();
                a->based = p_ref();
                expop(")");
            }
        } else if (!strcmp(k, "external") || !strcmp(k, "ext")) {
            nxt(); a->ext = 1;
            if (isop("(")) {
                if (tokens.a[ptk + 1].k == T_STR) {
                    a->xname = xdup(tokens.a[ptk + 1].s);
                    ptk += 2;
                    expop(")");
                } else {
                    skipparens();
                }
            }
        } else if (!strcmp(k, "internal") || !strcmp(k, "int")) {
            nxt();
        } else if (!strcmp(k, "initial") || !strcmp(k, "init")) {
            nxt();
            if (isid("call"))
                perr(TK.line, "INITIAL CALL unsupported", 0);
            a->init = p_initlist();
        } else if (!strcmp(k, "complex") || !strcmp(k, "cplx")) {
            nxt(); a->cpx = 1;
        } else if (!strcmp(k, "picture") || !strcmp(k, "pic")) {
            nxt();
            if (TK.k != T_STR)
                perr(TK.line, "picture string expected", 0);
            a->pic = TK.s;
            nxt();
        } else if (!strcmp(k, "defined") || !strcmp(k, "def")) {
            nxt();
            if (isop("(")) { nxt(); a->defbase = expid(); expop(")"); }
            else a->defbase = expid();
        } else if (!strcmp(k, "position") || !strcmp(k, "pos")) {
            nxt(); expop("("); a->defpos = p_expr(); expop(")");
        } else if (!strcmp(k, "type") || !strcmp(k, "ordinal") ||
                   !strcmp(k, "handle")) {
            int hd = k[0] == 'h', ti;
            char *tn;
            nxt();
            tn = expid();
            ti = find_type(tn);
            if (ti < 0) perr(TK.line, "unknown type:", tn);
            if (hd) {
                if (types[ti].kind != 0)
                    perr(TK.line, "HANDLE needs a structure type", 0);
                a->ty = TY_PTR;
                a->htype = ti + 1;
            } else if (types[ti].kind == 0) {
                a->stype = ti + 1;
            } else if (types[ti].kind == 1) {
                apply_alias(a, types[ti].al);
            } else {
                a->fix = 1;
                a->base = 1;
                a->p = types[ti].prec;
                a->ord = ti + 1;
            }
        } else if (!strcmp(k, "unsigned")) {
            nxt(); a->uns = 1;
        } else if (!strcmp(k, "signed")) {
            nxt();
        } else if (!strcmp(k, "value")) {
            nxt();
            a->init = p_initlist();
        } else if (!strcmp(k, "like")) {
            nxt(); a->like = expid();
        } else if (!strcmp(k, "stream") || !strcmp(k, "sequential") ||
                   !strcmp(k, "buffered") ||
                   !strcmp(k, "unbuffered") || !strcmp(k, "aligned") ||
                   !strcmp(k, "unaligned") || !strcmp(k, "packed") ||
                   !strcmp(k, "real") || !strcmp(k, "builtin") ||
                   !strcmp(k, "recursive") ||
                   !strcmp(k, "reducible") ||
                   !strcmp(k, "irreducible") ||
                   !strcmp(k, "nonvarying")) {
            nxt();
        } else if (!strcmp(k, "keyed") || !strcmp(k, "direct")) {
            nxt(); a->keyed = 1; a->rec = 1;
        } else if (!strcmp(k, "record")) { nxt(); a->rec = 1; }
        else if (!strcmp(k, "print")) { nxt(); a->prt = 1; }
        else if (!strcmp(k, "input")) { nxt(); a->mode = 1; }
        else if (!strcmp(k, "output")) { nxt(); a->mode = 2; }
        else if (!strcmp(k, "update")) { nxt(); a->mode = 3; }
        else if (!strcmp(k, "options")) {
            nxt();
            if (p_options() & 4) a->copt = 1;
        } else if (!strcmp(k, "environment") || !strcmp(k, "env")) {
            nxt(); skipparens();
        } else if (!strcmp(k, "complex") || !strcmp(k, "cplx") ||
                   !strcmp(k, "defined") || !strcmp(k, "def") ||
                   !strcmp(k, "picture") || !strcmp(k, "pic") ||
                   !strcmp(k, "area") || !strcmp(k, "offset") ||
                   !strcmp(k, "generic")) {
            perr(TK.line, "attribute not supported:", k);
        } else if (!strcmp(k, "dimension") || !strcmp(k, "dim")) {
            perr(TK.line, "use (bounds) after the name instead", 0);
        } else {
            perr(TK.line, "unknown attribute", k);
        }
    }
    if (a->uns && a->fix == 1 && a->base == 1 && a->p > 0 &&
        a->p < 63) {
        a->p++;
        a->uns = 0;
    }
}

static Node *p_decl(void)
{
    Node *h = 0, *t = 0, *it, *dims;
    char *names[64];
    int nn, i, lvl;
    Attr *at;
    for (;;) {
        lvl = 0;
        if (TK.k == T_NUM) { lvl = (int)TK.dv; nxt(); }
        nn = 0;
        if (isop("(")) {
            nxt();
            for (;;) {
                if (nn >= 64) perr(TK.line, "name list too long", 0);
                if (isop("*")) { nxt(); names[nn++] = "*"; }
                else names[nn++] = expid();
                if (isop(",")) { nxt(); continue; }
                break;
            }
            expop(")");
        } else if (isop("*")) {
            nxt();
            names[nn++] = "*";
        } else {
            names[nn++] = expid();
        }
        dims = 0;
        if (isop("(")) dims = p_dims();
        at = (Attr *)xalloc((long)sizeof(Attr));
        at->len = -2;
        at->p = -1;
        at->q = -1;
        p_attrs(at);
        for (i = 0; i < nn; i++) {
            it = newnode(N_ITEM, TK.line);
            it->s = names[i];
            it->n = lvl;
            it->b = dims;
            it->at = (Attr *)xalloc((long)sizeof(Attr));
            *it->at = *at;
            if (t) t->next = it; else h = it;
            t = it;
            if (at->stype) {
                Node *tl, *mem;
                mem = copy_members(types[at->stype - 1].items->next,
                                   (lvl > 0 ? lvl : 1), &tl);
                if (mem) {
                    t->next = mem;
                    t = tl;
                }
            }
        }
        if (isop(",")) { nxt(); continue; }
        break;
    }
    return h;
}

/* ---------- I/O lists and formats ---------- */

static Node *p_ditem(void);

static Node *p_dlist(void)
{
    Node *h = 0, *t = 0, *e;
    expop("(");
    for (;;) {
        e = p_ditem();
        if (t) t->next = e; else h = e;
        t = e;
        if (isop(",")) { nxt(); continue; }
        break;
    }
    expop(")");
    return h;
}

static int impdo(void)
{
    long m, i;
    int d = 0;
    if (!isop("(")) return 0;
    m = matchp(ptk);
    if (m < 0) return 0;
    for (i = ptk; i < m; i++) {
        if (tisop(&tokens.a[i], "(")) d++;
        else if (tisop(&tokens.a[i], ")")) d--;
        else if (d == 1 && tisid(&tokens.a[i], "do")) return 1;
    }
    return 0;
}

static Node *p_ditem(void)
{
    Node *n, *h, *t, *e;
    if (impdo()) {
        n = newnode(N_DO, TK.line);
        nxt();
        h = 0;
        t = 0;
        while (!isid("do")) {
            e = p_ditem();
            if (t) t->next = e; else h = e;
            t = e;
            if (isop(",")) nxt();
        }
        nxt();
        n->a = h;
        n->b = p_ref();
        expop("=");
        n->c = p_expr();
        if (isid("to")) { nxt(); n->d = p_expr(); }
        if (isid("by")) { nxt(); n->e = p_expr(); }
        expop(")");
        return n;
    }
    n = newnode(N_ITEM, TK.line);
    n->a = p_expr();
    return n;
}

static void p_fitem(Sb *o);

static void p_flist(Sb *o)
{
    for (;;) {
        p_fitem(o);
        if (isop(",")) { nxt(); sb_c(o, ','); continue; }
        break;
    }
}

static void p_farg(Sb *o)
{
    if (!isop("(")) return;
    nxt();
    sb_c(o, '(');
    sb_s(o, itoa_((long)cval(p_expr())));
    while (isop(",")) {
        nxt();
        sb_c(o, ',');
        sb_s(o, itoa_((long)cval(p_expr())));
    }
    expop(")");
    sb_c(o, ')');
}

static void p_fitem(Sb *o)
{
    char *k;
    if (isop("(")) {
        long m = matchp(ptk);
        Tok *a = &tokens.a[m + 1];
        if (a->k == T_ID || tisop(a, "(")) {
            /* repetition factor */
            Sb sub;
            long n, i;
            memset(&sub, 0, sizeof sub);
            nxt();
            n = (long)cval(p_expr());
            expop(")");
            if (n < 1 || n > 1000) perr(TK.line, "bad repeat", 0);
            p_fitem(&sub);
            for (i = 0; i < n; i++) {
                if (i) sb_c(o, ',');
                sb_s(o, sb_str(&sub));
            }
            return;
        }
        nxt();
        p_flist(o);
        expop(")");
        return;
    }
    k = expid();
    if (!strcmp(k, "a")) { sb_c(o, 'A'); p_farg(o); }
    else if (!strcmp(k, "f")) { sb_c(o, 'F'); p_farg(o); }
    else if (!strcmp(k, "e")) { sb_c(o, 'E'); p_farg(o); }
    else if (!strcmp(k, "b")) { sb_c(o, 'B'); p_farg(o); }
    else if (!strcmp(k, "x")) { sb_c(o, 'X'); p_farg(o); }
    else if (!strcmp(k, "skip")) { sb_c(o, 'S'); p_farg(o); }
    else if (!strcmp(k, "column") || !strcmp(k, "col")) {
        sb_c(o, 'C'); p_farg(o);
    } else if (!strcmp(k, "line")) { sb_c(o, 'L'); p_farg(o); }
    else if (!strcmp(k, "page")) { sb_c(o, 'P'); }
    else perr(TK.line, "format item not supported:", k);
}

static char *p_format(void)
{
    Sb s;
    memset(&s, 0, sizeof s);
    expop("(");
    p_flist(&s);
    expop(")");
    return xdup(sb_str(&s));
}

static Node *p_io(int k, int line)
{
    Node *n = newnode(k, line), *o, *ot = 0, *f, *ft = 0;
    char *w;
    while (!isop(";")) {
        if (TK.k != T_ID) perr(TK.line, "unexpected", TK.s);
        w = TK.s;
        nxt();
        if (!strcmp(w, "file")) {
            expop("("); n->a = p_ref(); expop(")");
        } else if (!strcmp(w, "string")) {
            expop("("); n->e = p_expr(); expop(")");
        } else if (!strcmp(w, "skip") || !strcmp(w, "line") ||
                   !strcmp(w, "page") || !strcmp(w, "column") ||
                   !strcmp(w, "col")) {
            o = newnode(N_OPT, line);
            o->s = w[0] == 'c' ? "col" : w;
            if (isop("(")) { nxt(); o->a = p_expr(); expop(")"); }
            if (ot) ot->next = o; else n->b = o;
            ot = o;
        } else if (!strcmp(w, "list")) {
            n->n = 1;
            n->c = p_dlist();
        } else if (!strcmp(w, "data")) {
            n->n = 3;
            if (isop("(")) n->c = p_dlist();
            else perr(line, "DATA needs a name list", 0);
        } else if (!strcmp(w, "edit")) {
            n->n = 2;
            while (isop("(")) {
                f = newnode(N_FMT, line);
                f->a = p_dlist();
                f->s = p_format();
                if (ft) ft->next = f; else n->c = f;
                ft = f;
            }
        } else {
            perr(line, "unknown I/O option", w);
        }
    }
    return n;
}

/* ---------- statements ---------- */

static int iskw(const char *s)
{
    static const char *kw[] = { "if", "do", "begin", "procedure",
        "proc", "call", "return", "goto", "go", "leave", "iterate",
        "select", "stop", "exit", "declare", "dcl", "put", "get",
        "open", "close", "read", "write", "rewrite", "delete",
        "allocate", "define", "assert",
        "alloc", "free", "on", "revert", "signal", "display", "end",
        0 };
    int i;
    for (i = 0; kw[i]; i++) if (!strcmp(kw[i], s)) return 1;
    return 0;
}

static Node *p_stmts(Node **head)
{
    Node *h = 0, *t = 0, *s;
    for (;;) {
        if (TK.k == T_EOF) perr(TK.line, "missing END", 0);
        s = p_stmt();
        if (s->k == S_END) { *head = h; return s; }
        if (t) t->next = s; else h = s;
        t = s;
    }
}

static Node *p_dospec(void)
{
    Node *sp = newnode(N_SPEC, TK.line);
    sp->a = p_expr();
    for (;;) {
        if (isid("to")) { nxt(); sp->b = p_expr(); }
        else if (isid("by")) { nxt(); sp->c = p_expr(); }
        else if (isid("while")) {
            nxt(); expop("("); sp->d = p_expr(); expop(")");
        } else if (isid("until")) {
            nxt(); expop("("); sp->e = p_expr(); expop(")");
        } else break;
    }
    return sp;
}

static Node *p_do(int line)
{
    Node *n = newnode(S_DO, line), *sp, *t = 0, *e;
    if (isop(";")) {
        nxt();
    } else if (isid("while") || isid("until")) {
        sp = newnode(N_SPEC, line);
        if (isid("while")) {
            nxt(); expop("("); sp->d = p_expr(); expop(")");
            if (isid("until")) {
                nxt(); expop("("); sp->e = p_expr(); expop(")");
            }
        } else {
            nxt(); expop("("); sp->e = p_expr(); expop(")");
            if (isid("while")) {
                nxt(); expop("("); sp->d = p_expr(); expop(")");
            }
        }
        n->a = sp;
        expop(";");
    } else {
        n->b = p_ref();
        expop("=");
        for (;;) {
            sp = p_dospec();
            if (t) t->next = sp; else n->a = sp;
            t = sp;
            if (isop(",")) { nxt(); continue; }
            break;
        }
        expop(";");
    }
    e = p_stmts(&n->c);
    (void)e;
    return n;
}

static Node *p_select(int line)
{
    Node *n = newnode(S_SELECT, line), *w, *wt = 0, *s, *x, *xt;
    if (isop("(")) {
        nxt();
        n->a = p_expr();
        expop(")");
    }
    expop(";");
    for (;;) {
        if (isid("when")) {
            nxt();
            w = newnode(S_WHEN, TK.line);
            expop("(");
            xt = 0;
            for (;;) {
                x = p_expr();
                if (xt) xt->next = x; else w->a = x;
                xt = x;
                if (isop(",")) { nxt(); continue; }
                break;
            }
            expop(")");
            w->b = p_stmt();
            if (wt) wt->next = w; else n->b = w;
            wt = w;
        } else if (isid("otherwise")) {
            nxt();
            n->c = p_stmt();
        } else if (isid("end")) {
            nxt();
            if (TK.k == T_ID) nxt();
            expop(";");
            break;
        } else {
            perr(TK.line, "WHEN, OTHERWISE or END expected", 0);
        }
    }
    (void)s;
    return n;
}

static Node *p_proc(int line)
{
    Node *n = newnode(S_PROC, line), *p, *pt = 0;
    Attr *at;
    char *k;
    at = (Attr *)xalloc((long)sizeof(Attr));
    at->len = -2;
    at->p = -1;
    at->q = -1;
    n->at = at;
    if (isop("(")) {
        nxt();
        for (;;) {
            p = newnode(N_ITEM, TK.line);
            p->s = expid();
            if (pt) pt->next = p; else n->a = p;
            pt = p;
            if (isop(",")) { nxt(); continue; }
            break;
        }
        expop(")");
    }
    while (!isop(";")) {
        k = expid();
        if (!strcmp(k, "recursive")) n->fl |= 1;
        else if (!strcmp(k, "options")) {
            n->fl |= p_options();
        } else if (!strcmp(k, "returns")) {
            expop("(");
            at->ret = (Attr *)xalloc((long)sizeof(Attr));
            at->ret->len = -2;
            at->ret->p = -1;
            at->ret->q = -1;
            p_attrs(at->ret);
            expop(")");
        } else if (!strcmp(k, "external") && isop("(") &&
                   TK1.k == T_STR) {
            at->ext = 1;
            at->xname = xdup(TK1.s);
            ptk += 2;
            expop(")");
        } else if (!strcmp(k, "external") || !strcmp(k, "internal") ||
                   !strcmp(k, "reorder") || !strcmp(k, "order") ||
                   !strcmp(k, "reentrant") || !strcmp(k, "ext") ||
                   !strcmp(k, "int")) {
            skipparens();
        } else {
            perr(line, "unknown PROCEDURE option", k);
        }
    }
    nxt();
    p_stmts(&n->b);
    return n;
}

static Node *p_stmt(void)
{
    Node *lab = 0, *lt = 0, *n, *l, *t;
    int line, pre = 0;
    char *k;
    /* labels and condition prefixes */
    for (;;) {
        if (TK.k == T_ID && isop1(":") && !iskw(TK.s) ) {
            l = newnode(N_ITEM, TK.line);
            l->s = TK.s;
            nxt(); nxt();
            if (lt) lt->next = l; else lab = l;
            lt = l;
            continue;
        }
        if (TK.k == T_ID && isop1(":")) {
            l = newnode(N_ITEM, TK.line);
            l->s = TK.s;
            nxt(); nxt();
            if (lt) lt->next = l; else lab = l;
            lt = l;
            continue;
        }
        if (isop("(")) {
            long m = matchp(ptk);
            if (m > 0 && tisop(&tokens.a[m + 1], ":")) {
                long i;
                for (i = ptk + 1; i < m; i++) {
                    if (tisid(&tokens.a[i], "subscriptrange"))
                        pre |= PRE_SUBS;
                    if (tisid(&tokens.a[i], "nosubscriptrange"))
                        pre |= PRE_NOCHK;
                    if (tisid(&tokens.a[i], "size"))
                        pre |= PRE_SIZE;
                    if (tisid(&tokens.a[i], "nosize"))
                        pre |= PRE_NOSZ;
                }
                ptk = m + 2;
                continue;
            }
        }
        break;
    }
    line = TK.line;
    if (isop(";")) {
        nxt();
        n = newnode(S_NULL, line);
        n->lab = lab;
        return n;
    }
    if (TK.k != T_ID) perr(line, "statement expected near", TK.s);
    k = TK.s;
    n = 0;
    if (iskw(k) && !isop1("=") && !(isop1("(") && !strcmp(k, "on") )) {
        nxt();
        if (!strcmp(k, "if")) {
            n = newnode(S_IF, line);
            n->a = p_expr();
            expkw("then");
            n->b = p_stmt();
            if (isid("else")) { nxt(); n->c = p_stmt(); }
        } else if (!strcmp(k, "do")) {
            n = p_do(line);
        } else if (!strcmp(k, "begin")) {
            n = newnode(S_BEGIN, line);
            expop(";");
            p_stmts(&n->b);
        } else if (!strcmp(k, "procedure") || !strcmp(k, "proc")) {
            n = p_proc(line);
            if (!lab) perr(line, "PROCEDURE needs a label", 0);
        } else if (!strcmp(k, "call")) {
            n = newnode(S_CALL, line);
            n->a = p_ref();
            expop(";");
        } else if (!strcmp(k, "return")) {
            n = newnode(S_RETURN, line);
            if (isop("(")) { nxt(); n->a = p_expr(); expop(")"); }
            expop(";");
        } else if (!strcmp(k, "goto") || !strcmp(k, "go")) {
            if (!strcmp(k, "go")) expkw("to");
            n = newnode(S_GOTO, line);
            n->s = expid();
            expop(";");
        } else if (!strcmp(k, "leave") || !strcmp(k, "iterate")) {
            n = newnode(k[0] == 'l' ? S_LEAVE : S_ITER, line);
            if (TK.k == T_ID) n->s = expid();
            expop(";");
        } else if (!strcmp(k, "select")) {
            n = p_select(line);
        } else if (!strcmp(k, "stop") || !strcmp(k, "exit")) {
            n = newnode(S_STOP, line);
            expop(";");
        } else if (!strcmp(k, "define")) {
            p_define(line);
            expop(";");
            n = newnode(S_NULL, line);
        } else if (!strcmp(k, "assert")) {
            char *w = expid();
            n = newnode(S_ASSERT, line);
            if (!strcmp(w, "unreachable")) {
                n->fl = 2;
            } else if (!strcmp(w, "true") || !strcmp(w, "false")) {
                n->fl = w[0] == 'f';
                expop("(");
                n->a = p_expr();
                expop(")");
            } else {
                perr(line, "ASSERT needs TRUE, FALSE or UNREACHABLE", 0);
            }
            if (isid("text")) {
                nxt();
                expop("(");
                n->b = p_expr();
                expop(")");
            }
            expop(";");
        } else if (!strcmp(k, "declare") || !strcmp(k, "dcl")) {
            n = newnode(S_DECL, line);
            n->a = p_decl();
            expop(";");
        } else if (!strcmp(k, "put")) {
            n = p_io(S_PUT, line);
            expop(";");
        } else if (!strcmp(k, "get")) {
            n = p_io(S_GET, line);
            expop(";");
        } else if (!strcmp(k, "open") || !strcmp(k, "close")) {
            Node *sp, *st = 0;
            int op = k[1] == 'p';
            n = newnode(op ? S_OPEN : S_CLOSE, line);
            for (;;) {
                sp = newnode(N_SPEC, line);
                expkw("file");
                expop("(");
                sp->a = p_ref();
                expop(")");
                while (TK.k == T_ID && !isop(";")) {
                    char *w = TK.s;
                    if (!strcmp(w, "title")) {
                        nxt(); expop("("); sp->b = p_expr();
                        expop(")");
                    } else if (!strcmp(w, "linesize")) {
                        nxt(); expop("("); sp->c = p_expr();
                        expop(")");
                    } else if (!strcmp(w, "pagesize")) {
                        nxt(); expop("("); sp->d = p_expr();
                        expop(")");
                    } else if (!strcmp(w, "input")) {
                        nxt(); sp->n = 1;
                    } else if (!strcmp(w, "output")) {
                        nxt(); sp->n = 2;
                    } else if (!strcmp(w, "update")) {
                        nxt(); sp->n = 3;
                    } else if (!strcmp(w, "print")) {
                        nxt(); sp->fl |= 1;
                    } else if (!strcmp(w, "record")) {
                        nxt(); sp->fl |= 2;
                    } else if (!strcmp(w, "keyed") ||
                               !strcmp(w, "direct")) {
                        nxt(); sp->fl |= 6;
                    }
                    else if (!strcmp(w, "stream") ||
                             !strcmp(w, "sequential") ||
                             !strcmp(w, "buffered") ||
                             !strcmp(w, "unbuffered")) nxt();
                    else break;
                }
                if (st) st->next = sp; else n->a = sp;
                st = sp;
                if (isop(",")) { nxt(); continue; }
                break;
            }
            expop(";");
        } else if (!strcmp(k, "read") || !strcmp(k, "write") ||
                   !strcmp(k, "rewrite") || !strcmp(k, "delete")) {
            n = newnode(k[0] == 'r' && k[2] == 'a' ? S_READ : S_WRITE,
                        line);
            if (!strcmp(k, "rewrite")) n->k = S_REWRITE;
            if (!strcmp(k, "delete")) n->k = S_DELETE;
            while (!isop(";")) {
                char *w = expid();
                expop("(");
                if (!strcmp(w, "file")) n->a = p_ref();
                else if (!strcmp(w, "into")) n->b = p_ref();
                else if (!strcmp(w, "from")) n->b = p_expr();
                else if (!strcmp(w, "key")) n->c = p_expr();
                else if (!strcmp(w, "keyfrom")) n->d = p_expr();
                else if (!strcmp(w, "keyto")) n->e = p_ref();
                else perr(line, "READ/WRITE option unsupported", w);
                expop(")");
            }
            expop(";");
        } else if (!strcmp(k, "allocate") || !strcmp(k, "alloc") ||
                   !strcmp(k, "free")) {
            int fr = k[0] == 'f';
            n = newnode(fr ? S_FREE : S_ALLOC, line);
            t = 0;
            for (;;) {
                l = newnode(N_ITEM, line);
                l->s = expid();
                if (isop("->")) {
                    Node *pn = newnode(X_REF, line);
                    pn->s = l->s;
                    nxt();
                    l->s = expid();
                    l->a = pn;
                }
                if (!fr && isop("(")) l->b = p_dims();
                while (isid("set") || isid("in")) {
                    int isin = isid("in");
                    nxt(); expop("(");
                    if (isin) l->c = p_ref();
                    else if (!fr) l->a = p_ref();
                    else perr(TK.line, "SET not allowed in FREE", 0);
                    expop(")");
                }
                if (t) t->next = l; else n->a = l;
                t = l;
                if (isop(",")) { nxt(); continue; }
                break;
            }
            expop(";");
        } else if (!strcmp(k, "on")) {
            n = newnode(S_ON, line);
            n->s = expid();
            if (isop("(")) { nxt(); n->a = p_ref(); expop(")"); }
            if (isid("snap")) nxt();
            if (isid("system")) { nxt(); n->fl = 1; expop(";"); }
            else n->b = p_stmt();
        } else if (!strcmp(k, "revert") || !strcmp(k, "signal")) {
            n = newnode(k[0] == 'r' ? S_REVERT : S_SIGNAL, line);
            n->s = expid();
            if (isop("(")) { nxt(); n->a = p_ref(); expop(")"); }
            expop(";");
        } else if (!strcmp(k, "display")) {
            n = newnode(S_DISPLAY, line);
            expop("(");
            n->a = p_expr();
            expop(")");
            if (isid("reply")) {
                nxt(); expop("("); n->b = p_ref(); expop(")");
            }
            expop(";");
        } else if (!strcmp(k, "end")) {
            n = newnode(S_END, line);
            if (TK.k == T_ID) nxt();
            expop(";");
        }
    } else {
        /* assignment */
        Node *tg, *tt;
        n = newnode(S_ASSIGN, line);
        tg = p_ref();
        n->a = tg;
        tt = tg;
        while (isop(",")) {
            nxt();
            tg = p_ref();
            tt->next = tg;
            tt = tg;
        }
        expop("=");
        n->b = p_expr();
        if (isop(","))
            perr(line, "BY NAME/BY DIMENSION unsupported", 0);
        expop(";");
    }
    if (!n) perr(line, "cannot parse statement", k);
    n->lab = lab;
    n->pre = pre;
    return n;
}

static Node *p_program(void)
{
    Node *h = 0, *t = 0, *s;
    while (TK.k != T_EOF) {
        s = p_stmt();
        if (s->k == S_END) perr(s->line, "unmatched END", 0);
        if (t) t->next = s; else h = s;
        t = s;
    }
    return h;
}
