/* p55_bi.c -- built-in functions */

static const char *bi_names[] = {
    "abs", "sqrt", "exp", "log", "log10", "log2", "sin", "cos", "tan",
    "asin", "acos", "atan", "sinh", "cosh", "tanh", "floor", "ceil",
    "trunc", "sign", "mod", "rem", "round", "max", "min", "sum",
    "prod",
    "dim", "lbound", "hbound", "length", "index", "verify", "substr",
    "translate", "repeat", "reverse", "trim", "uppercase", "lowercase",
    "string", "char", "bit", "fixed", "float", "binary", "decimal",
    "precision", "add", "multiply", "divide", "addr", "null", "date",
    "time", "oncode", "random", "allocation", "lineno", "low", "high",
    "left", "right", "center", "real", "unspec", "complex",
    "conjg", "imag", "sind", "cosd", "tand", "asind", "acosd",
    "atand", "asinh", "acosh", "atanh", "verifyr", "search",
    "searchr", "tally", "rank", "byte", "maxlength", "ordinalname",
    "ordinalpred", "ordinalsucc", 0
};

static int name_is_builtin(const char *n)
{
    int i;
    for (i = 0; bi_names[i]; i++)
        if (!strcmp(bi_names[i], n)) return 1;
    return 0;
}

static char *gfile(Node *f);

static int nargs(Node *e)
{
    int n = 0;
    Node *a;
    for (a = e->a; a; a = a->next) n++;
    return n;
}

static Node *argn(Node *e, int i)
{
    Node *a = e->a;
    while (a && i-- > 0) a = a->next;
    return a;
}

static void need(Node *e, int lo, int hi)
{
    int n = nargs(e);
    if (n < lo || n > hi)
        perr(e->line, "wrong number of arguments to", e->s);
}

static const char *math1[][2] = {
    { "sqrt", "pl_sqrt" }, { "exp", "exp" }, { "log", "pl_log" },
    { "log10", "pl_log10" }, { "log2", "pl_log2" }, { "sin", "sin" },
    { "cos", "cos" }, { "tan", "tan" }, { "asin", "asin" },
    { "acos", "acos" }, { "sinh", "sinh" }, { "cosh", "cosh" },
    { "tanh", "tanh" }, { "sind", "pl_sind" }, { "cosd", "pl_cosd" },
    { "tand", "pl_tand" }, { "asind", "pl_asind" },
    { "acosd", "pl_acosd" }, { "asinh", "pl_asinh" },
    { "acosh", "pl_acosh" }, { "atanh", "pl_atanh" }, { 0, 0 }
};

static const char *int1[][2] = {
    { "floor", "floor" }, { "ceil", "ceil" }, { "trunc", "pl_trunc" },
    { "sign", "pl_sign" }, { 0, 0 }
};

/* array argument: element pointer cast and count */
static int arrarg(Node *a, char **ptr, char **cnt, int *isl)
{
    LV v;
    Sym *s;
    if (a->k != X_REF || a->c) return 0;
    s = lookup(a->s);
    if (!s || s->cls != C_VAR || !s->nd || a->a) return 0;
    v = rl(a);
    if (!is_num(s) || s->star || s->isparam || is_wide(s))
        perr(a->line, "numeric array expected", a->s);
    *isl = is_long(s);
    *ptr = cat3(*isl ? "(const long *)" : "(const double *)", v.t, "");
    *cnt = elem_text(s);
    return 1;
}

static Sym *arr_sym(Node *a)
{
    Sym *s;
    if (a->k != X_REF || a->c) perr(a->line, "array name expected", 0);
    s = lookup(a->s);
    if (!s || s->cls != C_VAR || !s->nd)
        perr(a->line, "array name expected", a->s);
    return s;
}

static char *gbuiltin(Node *e, int *k)
{
    char *n = e->s, *a, *b, *c;
    int i, kk, q, f, isl;
    char *ptr, *cnt;
    *k = K_N;
    ge_q = -1;
    ge_f = 0;
    if (!strcmp(n, "complex")) {
        need(e, 2, 2);
        *k = K_C;
        return cat5("pl_cx(", cnum(argn(e, 0)), ", ",
                    cnum(argn(e, 1)), ")");
    }
    if (!strcmp(n, "conjg")) {
        need(e, 1, 1);
        *k = K_C;
        return cat3("pl_cconj(", ccx(argn(e, 0)), ")");
    }
    if (!strcmp(n, "imag")) {
        need(e, 1, 1);
        return cat3("pl_cim(", ccx(argn(e, 0)), ")");
    }
    if (nargs(e) == 1 && cpx_node(argn(e, 0))) {
        a = ccx(argn(e, 0));
        if (!strcmp(n, "real")) return cat3("pl_cre(", a, ")");
        if (!strcmp(n, "abs")) return cat3("pl_cabs(", a, ")");
        *k = K_C;
        if (!strcmp(n, "sqrt")) return cat3("pl_csqrt(", a, ")");
        if (!strcmp(n, "exp")) return cat3("pl_cexp(", a, ")");
        if (!strcmp(n, "log")) return cat3("pl_clog(", a, ")");
        if (!strcmp(n, "sin")) return cat3("pl_csin(", a, ")");
        if (!strcmp(n, "cos")) return cat3("pl_ccos(", a, ")");
        perr(e->line, "COMPLEX argument not allowed for", n);
    }
    if (!strcmp(n, "abs")) {
        need(e, 1, 1);
        a = cnum(argn(e, 0));
        return cat3("pl_abs(", a, ")");
    }
    for (i = 0; math1[i][0]; i++)
        if (!strcmp(n, math1[i][0])) {
            need(e, 1, 1);
            a = cnum(argn(e, 0));
            ge_q = -1;
            ge_f = 6;
            return cat4(math1[i][1], "(", a, ")");
        }
    for (i = 0; int1[i][0]; i++)
        if (!strcmp(n, int1[i][0])) {
            need(e, 1, 1);
            a = cnum(argn(e, 0));
            ge_q = 0;
            ge_f = 0;
            return cat4(int1[i][1], "(", a, ")");
        }
    if (!strcmp(n, "atan")) {
        need(e, 1, 2);
        a = cnum(argn(e, 0));
        if (nargs(e) == 1) {
            ge_q = -1;
            ge_f = 6;
            return cat3("atan(", a, ")");
        }
        b = cnum(argn(e, 1));
        ge_q = -1;
        ge_f = 6;
        return cat5("atan2(", a, ", ", b, ")");
    }
    if (!strcmp(n, "atand")) {
        need(e, 1, 2);
        a = cnum(argn(e, 0));
        ge_q = -1;
        ge_f = 6;
        if (nargs(e) == 1) return cat3("pl_atand(", a, ")");
        return cat5("pl_atan2d(", a, ", ", cnum(argn(e, 1)), ")");
    }
    if (!strcmp(n, "mod") || !strcmp(n, "rem")) {
        need(e, 2, 2);
        a = cnum(argn(e, 0));
        q = ge_q;
        b = cnum(argn(e, 1));
        ge_q = q >= 0 && ge_q >= 0 ? (q > ge_q ? q : ge_q) : -1;
        return cat5(n[0] == 'm' ? "pl_mod(" : "pl_rem(", a, ", ", b,
                    ")");
    }
    if (!strcmp(n, "round")) {
        Node *x = argn(e, 1);
        need(e, 2, 2);
        a = cnum(argn(e, 0));
        b = cnum(x);
        ge_q = (x->k == X_NUM && x->dv >= 0) ? (int)x->dv : -1;
        return cat5("pl_round(", a, ", ", b, ")");
    }
    if (!strcmp(n, "max") || !strcmp(n, "min")) {
        char *fn = n[1] == 'a' ? "pl_max" : "pl_min";
        int cnt2 = nargs(e);
        need(e, 1, 32);
        if (cnt2 == 1 && arrarg(argn(e, 0), &ptr, &cnt, &isl))
            perr(e->line, "MAX/MIN of an array unsupported", 0);
        a = cnum(argn(e, 0));
        q = ge_q;
        f = ge_f;
        for (i = 1; i < cnt2; i++) {
            b = cnum(argn(e, i));
            if (ge_q != q) q = -1;
            if (ge_f > f) f = ge_f;
            a = cat5(fn, "(", a, ", ", cat2(b, ")"));
        }
        ge_q = q;
        ge_f = f;
        return a;
    }
    if (!strcmp(n, "sum") || !strcmp(n, "prod")) {
        need(e, 1, 1);
        if (!arrarg(argn(e, 0), &ptr, &cnt, &isl))
            perr(e->line, "array argument expected", n);
        return cat5(n[0] == 's' ? (isl ? "pl_suml(" : "pl_sumd(") :
                    (isl ? "pl_prodl(" : "pl_prodd("), ptr, ", ", cnt,
                    ")");
    }
    if (!strcmp(n, "dim") || !strcmp(n, "lbound") ||
        !strcmp(n, "hbound")) {
        Sym *s;
        Node *x;
        int d = 0;
        need(e, 1, 2);
        s = arr_sym(argn(e, 0));
        x = argn(e, 1);
        if (x) d = (int)cval(x) - 1;
        else if (s->nd > 1)
            perr(e->line, "dimension number needed", 0);
        if (d < 0 || d >= s->nd) perr(e->line, "bad dimension", 0);
        ge_q = 0;
        if (d == 0 && s->lbv) {
            if (n[0] == 'd')
                return cat5("((double)(", s->hbv, " - ", s->lbv,
                            " + 1))");
            return cat3("((double)",
                        n[0] == 'l' ? s->lbv : s->hbv, ")");
        }
        if (n[0] == 'd') return itoa_(s->hi[d] - s->lo[d] + 1);
        return itoa_(n[0] == 'l' ? s->lo[d] : s->hi[d]);
    }
    if (!strcmp(n, "length")) {
        need(e, 1, 1);
        a = cstr(argn(e, 0));
        ge_q = 0;
        return cat3("pl_len(", a, ")");
    }
    if (!strcmp(n, "index") || !strcmp(n, "verify")) {
        need(e, 2, 2);
        a = cstr(argn(e, 0));
        b = cstr(argn(e, 1));
        ge_q = 0;
        return cat5(n[0] == 'i' ? "pl_idx(" : "pl_ver(", a, ", ", b,
                    ")");
    }
    if (!strcmp(n, "verifyr") || !strcmp(n, "search") ||
        !strcmp(n, "searchr") || !strcmp(n, "tally")) {
        need(e, 2, 2);
        a = cstr(argn(e, 0));
        b = cstr(argn(e, 1));
        ge_q = 0;
        if (n[0] == 'v') return cat5("pl_verr(", a, ", ", b, ")");
        if (n[0] == 't') return cat5("pl_tally(", a, ", ", b, ")");
        return cat5("pl_srch(", a, ", ", b, n[6] ? ", 1)" : ", 0)");
    }
    if (!strcmp(n, "ordinalname") || !strcmp(n, "ordinalpred") ||
        !strcmp(n, "ordinalsucc")) {
        static char done[128];
        LV v;
        int oi = 0;
        TypeDef *t;
        int j;
        need(e, 1, 1);
        if (argn(e, 0)->k == X_NUM && argn(e, 0)->s) {
            oi = find_type(argn(e, 0)->s) + 1;
        } else if (argn(e, 0)->k == X_REF) {
            v = rl(argn(e, 0));
            oi = v.nu ? 0 : v.s->ord;
        }
        if (oi < 1) perr(e->line, "ORDINAL value expected in", n);
        t = &types[oi - 1];
        a = cnum(argn(e, 0));
        if (!done[oi - 1]) {
            Sb sn, sv;
            memset(&sn, 0, sizeof sn);
            memset(&sv, 0, sizeof sv);
            for (j = 0; j < t->nord; j++) {
                sb_s(&sn, cat3(j ? ", \"" : "\"", t->onm[j], "\""));
                sb_s(&sv, cat2(j ? ", " : "", itoa_(t->ov[j])));
            }
            sb_s(&gdecl, cat5("static const char *ordn", itoa_(oi),
                              "[] = { ", sb_str(&sn), " };\n"));
            sb_s(&gdecl, cat5("static const long ordv", itoa_(oi),
                              "[] = { ", sb_str(&sv), " };\n"));
            done[oi - 1] = 1;
        }
        if (n[7] != 'n') {
            ge_q = 0;
            return cat5("pl_ostep(", a, ", ordv", itoa_(oi),
                        cat5(", ", itoa_(t->nord),
                             n[7] == 'p' ? ", -1)" : ", 1)", "", ""));
        }
        *k = K_S;
        return cat5("pl_ordn(", a, cat3(", ordn", itoa_(oi), ", "),
                    cat3("ordv", itoa_(oi), ", "),
                    cat2(itoa_(t->nord), ")"));
    }
    if (!strcmp(n, "rank")) {
        need(e, 1, 1);
        ge_q = 0;
        return cat3("pl_rank(", cstr(argn(e, 0)), ")");
    }
    if (!strcmp(n, "byte")) {
        need(e, 1, 1);
        *k = K_S;
        return cat3("pl_byte(", cnum(argn(e, 0)), ")");
    }
    if (!strcmp(n, "maxlength")) {
        LV v;
        need(e, 1, 1);
        if (argn(e, 0)->k != X_REF)
            perr(e->line, "MAXLENGTH needs a string variable", 0);
        v = rl(argn(e, 0));
        if ((v.s->ty != TY_CHR && v.s->ty != TY_BIT) || v.s->len < 0)
            perr(e->line, "MAXLENGTH needs a sized string", 0);
        ge_q = 0;
        return itoa_(v.s->len);
    }
    if (!strcmp(n, "substr")) {
        Node *s0 = argn(e, 0);
        need(e, 2, 3);
        a = ge(s0, &kk);
        if (kk == K_N) { a = cstr(s0); kk = K_S; }
        if (kk != K_S && kk != K_B)
            perr(e->line, "string expected in SUBSTR", 0);
        b = cnum(argn(e, 1));
        *k = kk;
        ge_q = -1;
        ge_f = 0;
        if (nargs(e) == 2) return cat5("pl_sub2(", a, ", ", b, ")");
        c = cnum(argn(e, 2));
        ge_q = -1;
        return cat5("pl_sub(", a, ", ", cat3(b, ", ", c), ")");
    }
    if (!strcmp(n, "translate")) {
        need(e, 3, 3);
        a = cstr(argn(e, 0));
        b = cstr(argn(e, 1));
        c = cstr(argn(e, 2));
        *k = K_S;
        return cat5("pl_trn(", a, ", ", b, cat3(", ", c, ")"));
    }
    if (!strcmp(n, "repeat")) {
        need(e, 2, 2);
        a = cstr(argn(e, 0));
        b = cnum(argn(e, 1));
        *k = K_S;
        return cat5("pl_rpt(", a, ", ", b, ")");
    }
    if (!strcmp(n, "reverse")) {
        need(e, 1, 1);
        *k = K_S;
        return cat3("pl_rev(", cstr(argn(e, 0)), ")");
    }
    if (!strcmp(n, "trim")) {
        need(e, 1, 1);
        *k = K_S;
        return cat3("pl_trim(", cstr(argn(e, 0)), ", 1, 1)");
    }
    if (!strcmp(n, "uppercase") || !strcmp(n, "lowercase")) {
        need(e, 1, 1);
        *k = K_S;
        return cat3(n[0] == 'u' ? "pl_case(" : "pl_case(",
                    cstr(argn(e, 0)), n[0] == 'u' ? ", 1)" : ", 0)");
    }
    if (!strcmp(n, "left") || !strcmp(n, "right") ||
        !strcmp(n, "center")) {
        need(e, 2, 2);
        a = cstr(argn(e, 0));
        b = cnum(argn(e, 1));
        *k = K_S;
        return cat5("pl_just(", a, ", ", b, n[0] == 'l' ? ", 0)" :
                    (n[0] == 'r' ? ", 1)" : ", 2)"));
    }
    if (!strcmp(n, "string")) {
        need(e, 1, 1);
        a = ge(argn(e, 0), &kk);
        if (kk == K_N) { a = cstr(argn(e, 0)); kk = K_S; }
        *k = kk;
        return a;
    }
    if (!strcmp(n, "char")) {
        need(e, 1, 2);
        *k = K_S;
        a = cstr(argn(e, 0));
        if (nargs(e) == 1) return a;
        return cat5("pl_chrn(", a, ", ", cnum(argn(e, 1)), ")");
    }
    if (!strcmp(n, "unspec")) {
        LV v;
        Sym *s;
        need(e, 1, 1);
        if (argn(e, 0)->k != X_REF)
            perr(e->line, "UNSPEC needs a variable", 0);
        v = rl(argn(e, 0));
        s = v.s;
        if (v.nu || s->ty == TY_STRUCT)
            perr(e->line, "UNSPEC of an aggregate unsupported", 0);
        *k = K_B;
        uses_tmp = 1;
        if (s->ty == TY_FXB && s->p <= 31)
            return cat3("pl_ubits((unsigned long)(", v.t,
                        s->p <= 15 ? "), 16)" : "), 32)");
        if (s->ty == TY_FLT) return cat3("pl_ubits_d(", v.t, ")");
        if (s->ty == TY_CHR)
            return cat3("pl_ubits_s(pl_ps(", pvtxt(v), "))");
        if (s->ty == TY_BIT) return cat3("pl_ps(", pvtxt(v), ")");
        perr(e->line, "UNSPEC of this type unsupported", s->name);
        return "";
    }
    if (!strcmp(n, "bit")) {
        need(e, 1, 2);
        *k = K_B;
        if (nargs(e) == 1) return cbit(argn(e, 0));
        return cat5("pl_n2b(", cnum(argn(e, 0)), ", ",
                    cnum(argn(e, 1)), ")");
    }
    if (!strcmp(n, "fixed") || !strcmp(n, "decimal") ||
        !strcmp(n, "binary") || !strcmp(n, "precision")) {
        Node *qn = argn(e, 2);
        need(e, 1, 3);
        a = cnum(argn(e, 0));
        q = ge_q;
        f = ge_f;
        if (qn) {
            ge_q = (int)cval(qn);
            ge_f = 0;
            return cat5("pl_fx(", a, ", ", itoa_(ge_q), ")");
        }
        if (n[0] == 'f' && q < 0 && !f) {
            ge_q = 0;
            return cat3("pl_fx(", a, ", 0)");
        }
        ge_q = q;
        ge_f = f;
        return a;
    }
    if (!strcmp(n, "float") || !strcmp(n, "real")) {
        need(e, 1, 2);
        a = cnum(argn(e, 0));
        ge_q = -1;
        ge_f = 6;
        return a;
    }
    if (!strcmp(n, "add") || !strcmp(n, "multiply") ||
        !strcmp(n, "divide")) {
        need(e, 3, 4);
        a = cnum(argn(e, 0));
        b = cnum(argn(e, 1));
        ge_q = -1;
        if (n[0] == 'a') return cat5("(", a, " + ", b, ")");
        if (n[0] == 'm') return cat5("(", a, " * ", b, ")");
        return cat5("pl_div(", a, ", ", b, ")");
    }
    if (!strcmp(n, "addr")) {
        LV v;
        Node *x = argn(e, 0);
        need(e, 1, 1);
        *k = K_P;
        v = rl(x);
        if (is_str(v.s) && v.s->ispv && v.t == v.s->cn)
            return cat3("(void *)(", v.t, ".p)");
        return cat3("(void *)&", v.t, "");
    }
    if (!strcmp(n, "null")) {
        *k = K_P;
        return "((void *)0)";
    }
    if (!strcmp(n, "date")) {
        *k = K_S;
        uses_tmp = 1;
        return "pl_date()";
    }
    if (!strcmp(n, "time")) {
        *k = K_S;
        uses_tmp = 1;
        return "pl_time()";
    }
    if (!strcmp(n, "oncode")) {
        ge_q = 0;
        return "((double)pl_oncode)";
    }
    if (!strcmp(n, "random")) {
        need(e, 0, 1);
        ge_f = 6;
        if (nargs(e) == 0) return "pl_rnd(0.0)";
        return cat3("pl_rnd(", cnum(argn(e, 0)), ")");
    }
    if (!strcmp(n, "allocation")) {
        Sym *s;
        need(e, 1, 1);
        s = lookup(argn(e, 0)->s);
        if (!s || s->stor != ST_CTL)
            perr(e->line, "CONTROLLED variable expected", 0);
        ge_q = 0;
        return cat3("((double)pl_gens(", s->hptr->cn, "))");
    }
    if (!strcmp(n, "lineno")) {
        need(e, 1, 1);
        ge_q = 0;
        return cat4("((double)", gfile(argn(e, 0)), "->line", ")");
    }
    if (!strcmp(n, "low") || !strcmp(n, "high")) {
        need(e, 1, 1);
        *k = K_S;
        return cat4("pl_hl(", cnum(argn(e, 0)),
                    n[0] == 'l' ? ", 0" : ", 255", ")");
    }
    perr(e->line, "built-in function not supported:", n);
    return "";
}
