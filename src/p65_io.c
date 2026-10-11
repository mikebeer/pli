/* p65_io.c -- PUT, GET, OPEN, CLOSE, READ, WRITE */

typedef struct { char *ft; int mode; int get; } IoArg;

static char *upname(const char *s)
{
    char *p = xdup(s), *q;
    for (q = p; *q; q++) *q = (char)toupper((unsigned char)*q);
    return p;
}

static char *gfile(Node *f)
{
    Sym *s;
    if (f->k != X_REF) perr(f->line, "file name expected", 0);
    s = lookup(f->s);
    if (!s) {
        if (!strcmp(f->s, "sysin")) return "(&pl_sysin)";
        if (!strcmp(f->s, "sysprint")) return "(&pl_sysprt)";
        perr(f->line, "undeclared file", f->s);
    }
    if (s->ty != TY_FILE) perr(f->line, "not a file:", f->s);
    return cat3("(&", s->cn, ")");
}

static char *ddname(Node *f)
{
    return upname(f->s);
}

static void put_el(Sym *s, char *lv, void *arg)
{
    IoArg *ia = (IoArg *)arg;
    LV v;
    v.t = lv; v.s = s; v.nu = 0;
    if (is_num(s)) {
        char *val = is_long(s) ? cat3("(double)", lv, "") : lv;
        int q = s->ty == TY_FLT ? -1 : s->q;
        int f = s->ty == TY_FLT ? s->p : 0;
        if (ia->mode == 2)
            ln(cat5("pl_en(", ia->ft, ", ", val, ");"));
        else
            ln(cat5("pl_li_n(", ia->ft, ", ", val, cat5(", ",
                    itoa_(q), ", ", itoa_(f), ");")));
    } else if (is_str(s)) {
        ln(cat5(ia->mode == 2 ? "pl_es(" : "pl_li(", ia->ft, ", ",
                cat3("pl_ps(", pvtxt(v), ")"), ");"));
    } else {
        perr(0, "cannot output", s->name);
    }
}

static void get_el(Sym *s, char *lv, void *arg)
{
    IoArg *ia = (IoArg *)arg;
    LV v;
    v.t = lv; v.s = s; v.nu = 0;
    if (is_num(s)) {
        char *d = newtemp("double");
        ln(cat4(d, " = ", is_long(s) ? cat3("(double)", lv, "") : lv,
                ";"));
        ln(cat5(ia->mode == 2 ? "pl_gn(" : "pl_gn_l(", ia->ft,
                ", &", d, ");"));
        ln(numstore(s, lv, d));
    } else if (is_str(s)) {
        ln(cat5(ia->mode == 2 ? "pl_gs(" : "pl_gs_l(", ia->ft, ", ",
                pvtxt(v), cat3(", ", s->ty == TY_BIT ? "1" : "0",
                ");")));
    } else {
        perr(0, "cannot input", s->name);
    }
}

static void do_item(Node *it, IoArg *ia);

typedef struct { Node *items; IoArg *ia; } ItemsArg;

static void items_body(void *a)
{
    ItemsArg *x = (ItemsArg *)a;
    Node *i;
    for (i = x->items; i; i = i->next) do_item(i, x->ia);
}

static void do_item(Node *it, IoArg *ia)
{
    Node *e;
    int k;
    char *v;
    if (it->k == N_DO) {
        ItemsArg x;
        x.items = it->a;
        x.ia = ia;
        ln("{");
        ind++;
        forloop(it->b, it->c, it->d, it->e, 0, 0, 0, items_body, &x);
        ind--;
        ln("}");
        return;
    }
    e = it->a;
    if (ia->get) {
        LV l;
        if (e->k != X_REF) perr(e->line, "variable expected", 0);
        l = rl(e);
        if (l.nu) {
            forall(l.s, l.t, get_el, ia);
        } else {
            get_el(l.s, l.t, ia);
        }
        return;
    }
    if (e->k == X_REF && !e->c) {
        Sym *s = lookup(e->s);
        if (s && s->cls == C_VAR && s->nd && !e->a) {
            LV l = rl(e);
            forall(l.s, l.t, put_el, ia);
            return;
        }
    }
    {
        int q, f;
        v = ge(e, &k);
        q = ge_q;
        f = ge_f;
        if (ia->mode == 3) return;
        if (k == K_N) {
            if (ia->mode == 2)
                ln(cat5("pl_en(", ia->ft, ", ", v, ");"));
            else
                ln(cat5("pl_li_n(", ia->ft, ", ", v, cat5(", ",
                        itoa_(q), ", ", itoa_(f), ");")));
        } else if (k == K_S || k == K_B) {
            ln(cat5(ia->mode == 2 ? "pl_es(" : "pl_li(", ia->ft, ", ",
                    v, ");"));
        } else {
            perr(e->line, "cannot output this value", 0);
        }
    }
}

static void do_data(Node *it, IoArg *ia)
{
    LV v;
    Node *e = it->a;
    char *x;
    int k, q, f;
    if (e->k != X_REF) perr(e->line, "name expected in DATA list", 0);
    v = rl(e);
    if (v.nu || !(is_num(v.s) || is_str(v.s)))
        perr(e->line, "scalar expected in DATA list", 0);
    x = ge(e, &k);
    q = ge_q;
    f = ge_f;
    if (k == K_N)
        ln(cat5("pl_pd_n(", ia->ft, cat3(", \"", upname(e->s), "\", "),
                cat5("pl_n2s(", x, ", ", cat3(itoa_(q), ", ",
                itoa_(f)), ")"), ", 0);"));
    else
        ln(cat5("pl_pd_n(", ia->ft, cat3(", \"", upname(e->s), "\", "),
                x, ", 1);"));
}

static char *lcount(Node *o)
{
    if (!o) return "1";
    return cat3("(long)(", cnum(o), ")");
}

static void g_io(Node *s)
{
    int put = s->k == S_PUT;
    IoArg ia;
    Node *o, *f, *it;
    char *tmp = 0, *dd = "SYSPRINT";
    ia.get = !put;
    ia.mode = s->n;
    if (s->e) {
        tmp = newtemp("PLF");
        ia.ft = cat3("(&", tmp, ")");
        if (put) {
            ln(cat3("pl_pstr(&", tmp, ");"));
        } else {
            ln(cat5("pl_gstr(&", tmp, ", ", cstr(s->e), ");"));
        }
    } else {
        ia.ft = s->a ? gfile(s->a) : (put ? "(&pl_sysprt)"
                                          : "(&pl_sysin)");
        if (s->a) dd = ddname(s->a);
        else if (!put) dd = "SYSIN";
        ln(cat5("pl_chk(", ia.ft, ", \"", dd, put ? "\", 2);" :
                "\", 1);"));
    }
    for (o = s->b; o; o = o->next) {
        if (!strcmp(o->s, "skip"))
            ln(cat5(put ? "pl_pskip(" : "pl_gskip(", ia.ft, ", ",
                    lcount(o->a), ");"));
        else if (!put) continue;
        else if (!strcmp(o->s, "page"))
            ln(cat3("pl_ppage(", ia.ft, ");"));
        else if (!strcmp(o->s, "line"))
            ln(cat5("pl_pline(", ia.ft, ", ", lcount(o->a), ");"));
        else if (!strcmp(o->s, "col"))
            ln(cat5("pl_pcol(", ia.ft, ", ", lcount(o->a), ");"));
    }
    if (s->n == 0) {
        if (put) return;
        perr(s->line, "GET needs LIST, EDIT or DATA", 0);
    }
    if (s->n == 1) {
        for (it = s->c; it; it = it->next) do_item(it, &ia);
    } else if (s->n == 2) {
        for (f = s->c; f; f = f->next) {
            ln(cat5("pl_ebeg(", ia.ft, ", ", clit(f->s,
                    (long)strlen(f->s)), ");"));
            for (it = f->a; it; it = it->next) do_item(it, &ia);
            if (put) ln(cat3("pl_eend(", ia.ft, ");"));
        }
    } else {
        if (!put) perr(s->line, "GET DATA is not supported", 0);
        for (it = s->c; it; it = it->next) do_data(it, &ia);
        ln(cat3("pl_pd_end(", ia.ft, ");"));
    }
    if (tmp && put) {
        LV v = rl(s->e);
        ln(cat5("pl_pend(&", tmp, ", ", pvtxt(v),
                cat3(", ", v.s->ty == TY_BIT ? "1" : "0", ");")));
    }
}

static void g_file(Node *s)
{
    Node *sp;
    char *ft, *dd;
    LV v;
    if (s->k == S_OPEN || s->k == S_CLOSE) {
        for (sp = s->a; sp; sp = sp->next) {
            Sym *fs = lookup(sp->a->s);
            int mode, prt, rec;
            ft = gfile(sp->a);
            if (s->k == S_CLOSE) {
                ln(cat3("pl_close(", ft, ");"));
                continue;
            }
            dd = ddname(sp->a);
            mode = sp->n ? sp->n : (fs ? fs->fmode : 0);
            prt = (sp->fl & 1) || (fs && fs->fprt);
            rec = (sp->fl & 2) || (fs && fs->frec);
            if (!mode) mode = prt ? 2 : 1;
            ln(cat5("pl_open(", ft, cat3(", \"", dd, "\", "),
                    sp->b ? cat3("pl_cs(", cstr(sp->b), ")") : "0",
                    cat5(", ", itoa_((long)(mode | (prt ? 4 : 0) |
                    (rec ? 8 : 0))), ", ", sp->c ? lcount(sp->c) :
                    "0", cat3(", ", sp->d ? lcount(sp->d) : "0",
                    ");"))));
        }
        return;
    }
    if (s->k == S_REWRITE)
        perr(s->line, "REWRITE is not supported", 0);
    if (!s->a || !s->b) perr(s->line, "FILE and INTO/FROM needed", 0);
    ft = gfile(s->a);
    dd = ddname(s->a);
    ln(cat5("pl_chk(", ft, ", \"", dd, s->k == S_READ ?
            "\", 9);" : "\", 10);"));
    if (s->k == S_READ) {
        v = rl(s->b);
        if (v.nu)
            perr(s->line, "READ INTO an array is unsupported", 0);
        if (v.s->ty == TY_CHR)
            ln(cat5("pl_rdrec(", ft, ", ", pvtxt(v), ");"));
        else
            ln(cat5("pl_rdbin(", ft, ", &", v.t,
                    cat3(", (long)sizeof ", v.t, ");")));
        return;
    }
    {
        int k;
        Node *e = s->b;
        if (e->k == X_REF && !e->c && lookup(e->s) &&
            lookup(e->s)->cls == C_VAR) {
            v = rl(e);
            if (v.s->ty == TY_CHR && !v.nu) {
                ln(cat5("pl_wrrec(", ft, ", pl_ps(", pvtxt(v), "));"));
                return;
            }
            ln(cat5("pl_wrbin(", ft, ", &", v.t,
                    cat3(", (long)sizeof ", v.t, ");")));
            return;
        }
        (void)ge(e, &k);
        ln(cat5("pl_wrrec(", ft, ", ", cstr(e), ");"));
    }
}
