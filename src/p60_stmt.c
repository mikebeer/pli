/* p60_stmt.c -- statement translation */

static int ind = 1;

typedef struct { int id; char *lab; int isloop, usedb, usedc; } Loop;
static Loop lps[64];
static int nlp = 0;

static void ln(const char *s)
{
    int i;
    for (i = 0; i < ind; i++) sb_s(&cur->body, "    ");
    sb_s(&cur->body, s);
    sb_c(&cur->body, '\n');
}

static void gs(Node *s);
static void gsl(Node *s);
static char *gfile(Node *f);

static char *lbl(const char *p, int id)
{
    return cat2(p, itoa_((long)id));
}

/* store a numeric value text into symbol s at lvalue lv */
static char *numstore(Sym *s, const char *lv, const char *rv)
{
    if (is_long(s)) return cat5(lv, " = pl_tol(", rv, ")", ";");
    if (s->ty == TY_FXD)
        return cat5(lv, " = pl_fx(", rv, cat3(", ", itoa_(s->q), ")"),
                    ";");
    return cat4(lv, " = ", rv, ";");
}

/* nested loops over all elements of array symbol s */
typedef void (*elemfn)(Sym *, char *, void *);

static void forall(Sym *s, char *base, elemfn fn, void *arg)
{
    char *iv[MAXD];
    char *t = base;
    int i, d = ind;
    char buf[200];
    for (i = 0; i < s->nd; i++) {
        iv[i] = newtemp("long");
        sprintf(buf, "for (%s = 0; %s < %ld; %s++) {", iv[i], iv[i],
                s->hi[i] - s->lo[i] + 1, iv[i]);
        ln(buf);
        ind++;
        t = cat4(t, "[", iv[i], "]");
    }
    fn(s, t, arg);
    for (i = 0; i < s->nd; i++) {
        ind--;
        ln("}");
    }
    ind = d;
}

typedef struct { Node *rhs; char *val; int kind; } StoreArg;

static void store_one(Sym *s, char *lv, char *val)
{
    LV v;
    if (is_num(s)) ln(numstore(s, lv, val));
    else if (is_str(s)) {
        v.t = lv; v.s = s; v.nu = 0;
        ln(cat5(s->ty == TY_BIT ? "pl_ab(" : "pl_as(", pvtxt(v), ", ",
                val, ");"));
    } else if (s->ty == TY_PTR) {
        ln(cat4(lv, " = ", val, ";"));
    }
}

static void store_el(Sym *s, char *lv, void *arg)
{
    store_one(s, lv, ((StoreArg *)arg)->val);
}

static char *rhs_val(Sym *s, Node *rhs)
{
    if (is_num(s)) return cnum(rhs);
    if (s->ty == TY_BIT) return cbit(rhs);
    if (s->ty == TY_CHR) return cstr(rhs);
    if (s->ty == TY_PTR) return cptr(rhs);
    perr(rhs->line, "cannot assign to", s->name);
    return "";
}

static void store_expr(Sym *s, char *lv, Node *rhs)
{
    store_one(s, lv, rhs_val(s, rhs));
}

static int is_pseudo(Node *t)
{
    return t->k == X_REF && !t->c && !t->b && !lookup(t->s) &&
           !strcmp(t->s, "substr");
}

static void g_store(Node *tgt, Node *rhs)
{
    LV v;
    Sym *s;
    StoreArg sa;
    char *val, *tmp;
    if (tgt->k != X_REF) perr(tgt->line, "bad assignment target", 0);
    if (is_pseudo(tgt)) {
        LV sv;
        Node *a0 = tgt->a, *a1, *a2;
        int n = 0;
        Node *x;
        for (x = tgt->a; x; x = x->next) n++;
        if (n < 2 || n > 3 || a0->k != X_REF)
            perr(tgt->line, "bad SUBSTR target", 0);
        a1 = a0->next;
        a2 = a1->next;
        sv = rl(a0);
        if (!is_str(sv.s) || sv.nu)
            perr(tgt->line, "SUBSTR target must be a string", 0);
        if (a2)
            ln(cat5("pl_subas(", pvtxt(sv), ", ", cnum(a1),
                    cat5(", ", cnum(a2), ", ", cstr(rhs), ");")));
        else
            ln(cat5("pl_subas(", pvtxt(sv), ", ", cnum(a1),
                    cat4(", 1e9, ", cstr(rhs), "", ");")));
        return;
    }
    v = rl(tgt);
    s = v.s;
    if (s->ty == TY_STRUCT && !v.nu) {
        LV r;
        if (rhs->k != X_REF) perr(rhs->line, "structure expected", 0);
        r = rl(rhs);
        if (r.s->ty != TY_STRUCT || r.nu)
            perr(rhs->line, "structure expected", 0);
        ln(cat5("memcpy(&", v.t, ", &", r.t,
                cat3(", sizeof ", v.t, ");")));
        return;
    }
    if (v.nu) {
        if (s->ty == TY_STRUCT)
            perr(tgt->line, "array of structures unsupported", 0);
        if (rhs->k == X_REF && !rhs->c && lookup(rhs->s) &&
            lookup(rhs->s)->nd && !rhs->a) {
            LV r = rl(rhs);
            if (r.nu != v.nu || elems(r.s) != elems(s))
                perr(rhs->line, "array shapes differ", 0);
            if (is_str(s) != is_str(r.s) || (is_num(s) &&
                (is_long(s) != is_long(r.s) || s->q != r.s->q)))
                perr(rhs->line, "array types differ", 0);
            if (is_str(s) && (s->len != r.s->len ||
                              s->vary != r.s->vary))
                perr(rhs->line, "array types differ", 0);
            ln(cat5("memcpy(", v.t, ", ", r.t,
                    cat3(", sizeof ", v.t, ");")));
            return;
        }
        val = rhs_val(s, rhs);
        tmp = newtemp(is_num(s) ? "double" : (s->ty == TY_PTR ?
                      "void *" : "PS"));
        ln(cat4(tmp, " = ", val, ";"));
        sa.val = tmp;
        forall(s, v.t, store_el, &sa);
        return;
    }
    store_expr(s, v.t, rhs);
}

static void g_assign(Node *s)
{
    Node *t;
    for (t = s->a; t; t = t->next) g_store(t, s->b);
}

/* ---- DO loops ---- */

static Loop *pushloop(Node *s, int isloop)
{
    Loop *l;
    if (nlp >= 64) perr(s->line, "loops nested too deeply", 0);
    l = &lps[nlp++];
    l->id = ++idc;
    l->lab = s->lab ? s->lab->s : 0;
    l->isloop = isloop;
    l->usedb = l->usedc = 0;
    return l;
}

static void endbrk(Loop *l)
{
    if (l->usedb) ln(cat2(lbl("lb", l->id), ": ;"));
}

typedef void (*bodyfn)(void *);

static void forloop(Node *ctrl, Node *from, Node *to, Node *by,
                    Node *wh, Node *un, Loop *l, bodyfn body,
                    void *arg)
{
    LV cv;
    char *cvt = 0, *tb = 0, *ts = 0, *cur_v, *byc = 0;
    int dirk = 0;
    int d = ind;
    if (ctrl) {
        cv = rl(ctrl);
        if (!is_num(cv.s) || cv.nu)
            perr(ctrl->line, "loop variable must be numeric", 0);
        cvt = cv.t;
        cur_v = is_long(cv.s) ? cat3("((double)", cvt, ")") : cvt;
        ln(numstore(cv.s, cvt, cnum(from)));
        if (to) {
            tb = newtemp("double");
            ln(cat4(tb, " = ", cnum(to), ";"));
        }
        if (by) {
            if (by->k == X_NUM) {
                dirk = by->dv < 0 ? -1 : 1;
                byc = dtoa_(by->dv);
            } else {
                ts = newtemp("double");
                ln(cat4(ts, " = ", cnum(by), ";"));
                byc = ts;
            }
        } else {
            dirk = 1;
            byc = "1.0";
        }
    }
    ln("for (;;) {");
    ind++;
    if (ctrl && tb) {
        if (dirk > 0)
            ln(cat5("if (", cur_v, " > ", tb, ") break;"));
        else if (dirk < 0)
            ln(cat5("if (", cur_v, " < ", tb, ") break;"));
        else
            ln(cat5("if (", byc, " >= 0.0 ? ", cur_v,
                    cat5(" > ", tb, " : ", cur_v, cat3(" < ", tb,
                    ") break;"))));
    }
    if (wh) ln(cat3("if (!", cond(wh), ") break;"));
    body(arg);
    if (l && l->usedc) ln(cat2(lbl("lc", l->id), ": ;"));
    if (un) ln(cat3("if (", cond(un), ") break;"));
    if (ctrl && !to && !by && !wh && !un) ln("break;");
    else if (ctrl)
        ln(numstore(cv.s, cvt, cat5("(", cur_v, " + ", byc, ")")));
    ind--;
    ln("}");
    ind = d;
}

typedef struct { Node *body; } DoBody;

static void do_body(void *a)
{
    Node *s;
    for (s = ((DoBody *)a)->body; s; s = s->next) gs(s);
}

static int has_labels(Node *s)
{
    for (; s; s = s->next) {
        if (s->lab) return 1;
        if (s->k == S_DO && has_labels(s->c)) return 1;
        if (s->k == S_IF && (has_labels(s->b) ||
                             (s->c && has_labels(s->c)))) return 1;
        if (s->k == S_BEGIN && has_labels(s->b)) return 1;
    }
    return 0;
}

static void g_do(Node *s)
{
    Node *sp;
    Loop *l;
    DoBody db;
    int n = 0;
    db.body = s->c;
    for (sp = s->a; sp; sp = sp->next) n++;
    l = pushloop(s, n > 0);
    ln("{");
    ind++;
    if (n == 0) {
        do_body(&db);
    } else {
        if (n > 1 && has_labels(s->c))
            perr(s->line, "labels in a DO with several specs", 0);
        for (sp = s->a; sp; sp = sp->next)
            forloop(s->b, sp->a, sp->b, sp->c, sp->d, sp->e, l,
                    do_body, &db);
    }
    endbrk(l);
    ind--;
    ln("}");
    nlp--;
}

static Loop *findloop(Node *s, int needloop)
{
    int i;
    for (i = nlp - 1; i >= 0; i--) {
        if (s->s && (!lps[i].lab || strcmp(lps[i].lab, s->s)))
            continue;
        if (needloop && !lps[i].isloop) continue;
        return &lps[i];
    }
    perr(s->line, "no enclosing DO for LEAVE/ITERATE", 0);
    return 0;
}

/* ---- control ---- */

static void g_if(Node *s)
{
    char *c = cond(s->a);
    ln(cat3("if (", c, ") {"));
    ind++;
    gs(s->b);
    ind--;
    if (s->c) {
        ln("} else {");
        ind++;
        gs(s->c);
        ind--;
    }
    ln("}");
}

static void g_select(Node *s)
{
    Node *w, *x;
    char *sel = 0, *c, *wc;
    int k = 0, first = 1;
    if (s->a) {
        char *v = ge(s->a, &k);
        if (k == K_N) {
            sel = newtemp("double");
        } else if (k == K_S || k == K_B) {
            sel = newtemp("PS");
        } else perr(s->line, "bad SELECT expression", 0);
        ln(cat4(sel, " = ", v, ";"));
    }
    for (w = s->b; w; w = w->next) {
        Sb cb;
        memset(&cb, 0, sizeof cb);
        for (x = w->a; x; x = x->next) {
            if (sel) {
                if (k == K_N)
                    wc = cat5("(", sel, " == ", cnum(x), ")");
                else
                    wc = cat5("(pl_cmp(", sel, ", ", cstr(x),
                              ") == 0)");
            } else {
                wc = cond(x);
            }
            if (cb.n) sb_s(&cb, " || ");
            sb_s(&cb, wc);
        }
        c = xdup(sb_str(&cb));
        ln(cat4(first ? "if (" : "} else if (", c, ") {", ""));
        first = 0;
        ind++;
        gs(w->b);
        ind--;
    }
    if (first) {
        if (s->c) gs(s->c);
        return;
    }
    ln("} else {");
    ind++;
    if (s->c) gs(s->c);
    else ln("pl_sig(PLC_ERR, \"no WHEN matched in SELECT\");");
    ind--;
    ln("}");
}

static void g_return(Node *s)
{
    Sym *p = cur->ps;
    Sym *pm;
    char *v;
    if (cur->ison || !p) {
        ln("return;");
        return;
    }
    if (p->ty == TY_VOID) {
        if (s->a)
            perr(s->line, "RETURN with value in a subroutine", 0);
    } else if (!s->a) {
        perr(s->line, "RETURN needs a value in function", p->name);
    }
    if (p->ty != TY_VOID) {
        if (is_num(p)) {
            char *t = newtemp("double");
            v = cnum(s->a);
            if (is_long(p)) v = cat3("(double)pl_tol(", v, ")");
            else if (p->ty == TY_FXD)
                v = cat5("pl_fx(", v, ", ", itoa_(p->q), ")");
            ln(cat4(t, " = ", v, ";"));
            v = t;
        } else if (is_str(p)) {
            v = cat3("pl_keep(", p->ty == TY_BIT ? cbit(s->a) :
                     cstr(s->a), ")");
        } else {
            v = cptr(s->a);
        }
        for (pm = p->params; pm; pm = pm->params)
            if ((is_num(pm) || pm->ty == TY_PTR) && !pm->nd)
                ln(cat5("*", pm->pcn, " = ", pm->cn, ";"));
        ln(cat3("return ", v, ";"));
        return;
    }
    for (pm = p->params; pm; pm = pm->params)
        if ((is_num(pm) || pm->ty == TY_PTR) && !pm->nd)
            ln(cat5("*", pm->pcn, " = ", pm->cn, ";"));
    ln("return;");
}

static void g_goto(Node *s)
{
    Sym *l = lookupc(csc, s->s, C_LABEL);
    Fn *o;
    int i;
    if (!l) perr(s->line, "undefined label", s->s);
    o = l->owner;
    if (o == cur) {
        ln(cat3("goto ", l->cn, ";"));
        return;
    }
    if (o->rec) perr(s->line, "GOTO into a RECURSIVE procedure", 0);
    for (i = 0; i < o->nlg; i++) if (o->lg[i] == l) break;
    if (i == o->nlg) {
        if (o->nlg >= 32) perr(s->line, "too many non-local GOTOs", 0);
        o->lg[o->nlg] = l;
        o->lgid[o->nlg] = o->nlg + 1;
        o->nlg++;
    }
    o->jb = 1;
    ln(cat3("pl_gtsel = ", itoa_(o->lgid[i]), ";"));
    ln(cat3("longjmp(jb", itoa_(o->id), ", 1);"));
}

static void g_call(Node *s)
{
    Sym *p;
    int k;
    Node *r = s->a;
    p = r->c ? 0 : lookup(r->s);
    if (!p || p->cls != C_PROC)
        perr(s->line, "CALL of something that is not a procedure:",
             r->s);
    ln(cat2(gcall(p, r, &k), ";"));
}

static int condid(Node *s, char **fl)
{
    char *n = s->s;
    *fl = 0;
    if (!strcmp(n, "endfile")) {
        if (!s->a) perr(s->line, "ENDFILE needs a file", 0);
        *fl = gfile(s->a);
        return 0;
    }
    if (!strcmp(n, "error") || !strcmp(n, "undefinedfile") ||
        !strcmp(n, "name") || !strcmp(n, "record")) return 1;
    if (!strcmp(n, "zerodivide")) return 2;
    if (!strcmp(n, "overflow") || !strcmp(n, "fixedoverflow"))
        return 3;
    if (!strcmp(n, "underflow")) return 4;
    if (!strcmp(n, "size")) return 5;
    if (!strcmp(n, "conversion")) return 6;
    if (!strcmp(n, "subscriptrange")) return 7;
    if (!strcmp(n, "stringrange") || !strcmp(n, "stringsize"))
        return 8;
    if (!strcmp(n, "finish")) return 9;
    if (!strcmp(n, "key")) return 10;
    if (!strcmp(n, "transmit")) return 11;
    if (!strcmp(n, "endpage")) return 12;
    perr(s->line, "condition not supported:", n);
    return 0;
}

static void g_on(Node *s, int revert)
{
    char *fl;
    int c = condid(s, &fl);
    char *h = "0";
    if (!revert && !s->fl && s->b) {
        Fn *f = 0;
        int i;
        for (i = 0; i < nfn; i++)
            if (fnall[i]->node == s) f = fnall[i];
        h = f->name;
    }
    if (fl) ln(cat5("pl_onf(", fl, ", ", h, ");"));
    else ln(cat5("pl_on(", itoa_(c), ", ", h, ");"));
}

static void g_signal(Node *s)
{
    char *fl;
    int c = condid(s, &fl);
    if (fl) ln(cat3("pl_eofsig(", fl, ");"));
    else ln(cat3("pl_sig(", itoa_(c), ", 0);"));
}

static void gen_fill(Sym *s, char *t);

static int needs_fill(Sym *s)
{
    Sym *k;
    if (s->ty == TY_STRUCT) {
        for (k = s->kid; k; k = k->nxt) if (needs_fill(k)) return 1;
        return 0;
    }
    return is_str(s) && !s->vary && s->len > 0;
}

/* blank-fill one element (no dimensions) of symbol s at lvalue t */
static void fill_elem(Sym *s, char *t)
{
    Sym *k;
    if (s->ty == TY_STRUCT) {
        for (k = s->kid; k; k = k->nxt) {
            if (!k->cn) k->cn = membname(k);
            gen_fill(k, cat3(t, ".", k->cn));
        }
        return;
    }
    ln(cat5("memset(", t, cat2(", ", s->ty == TY_BIT ? "'0'" : "' '"),
            cat3(", ", itoa_(s->len), ");"), ""));
}

static void fill_el(Sym *s, char *t, void *a)
{
    (void)a;
    fill_elem(s, t);
}

/* blank-fill fixed strings inside lvalue t of symbol s */
static void gen_fill(Sym *s, char *t)
{
    if (!needs_fill(s)) return;
    if (s->nd) forall(s, t, fill_el, 0);
    else fill_elem(s, t);
}

static void g_alloc(Node *s, int fr)
{
    Node *it;
    Sym *y, *top;
    LV pv;
    char *ptr;
    for (it = s->a; it; it = it->next) {
        y = lookup(it->s);
        if (!y || y->par || (y->stor != ST_BASED && y->stor != ST_CTL))
            perr(s->line, "ALLOCATE/FREE needs BASED or CONTROLLED:",
                 it->s);
        top = y;
        if (y->stor == ST_CTL) {
            ptr = y->hptr->cn;
        } else if (it->a) {
            pv = rl(it->a);
            ptr = pv.t;
        } else if (y->based) {
            pv = rl(y->based);
            ptr = pv.t;
        } else {
            perr(s->line, "no pointer for", it->s);
            ptr = "";
        }
        if (fr) {
            ln(cat3("pl_free(", ptr, ");"));
            ln(cat2(ptr, " = 0;"));
        } else {
            ln(cat5(ptr, " = pl_alloc(sizeof(", top->tn, "));", ""));
            gen_fill(top, cat5("(*(", top->tn, " *)(", ptr, "))"));
        }
    }
}

/* initial values */
static void elem_lv(Sym *s, char *base, long lin, char **out)
{
    long sz[MAXD];
    long idx[MAXD];
    int i;
    char *t = base;
    for (i = 0; i < s->nd; i++) sz[i] = s->hi[i] - s->lo[i] + 1;
    for (i = s->nd - 1; i >= 0; i--) {
        idx[i] = lin % sz[i];
        lin /= sz[i];
    }
    for (i = 0; i < s->nd; i++) t = cat4(t, "[", itoa_(idx[i]), "]");
    *out = t;
}

static void gen_init(Sym *s, char *t)
{
    Sym *k;
    Node *it;
    long slot = 0, total, r;
    char *el;
    if (s->ty == TY_STRUCT) {
        if (s->nd) return;
        for (k = s->kid; k; k = k->nxt) {
            if (!k->cn) k->cn = membname(k);
            if (k->init || k->ty == TY_STRUCT)
                gen_init(k, cat3(t, ".", k->cn));
        }
        return;
    }
    if (!s->init) return;
    total = s->nd ? elems(s) : 1;
    for (it = s->init; it; it = it->next) {
        for (r = 0; r < it->n; r++) {
            if (slot >= total)
                perr(it->line, "too many initial values for", s->name);
            if (!it->fl) {
                if (s->nd) elem_lv(s, t, slot, &el); else el = t;
                store_expr(s, el, it->a);
            }
            slot++;
        }
    }
}

static void scope_inits(Scope *sc)
{
    Sym *a[512], *y;
    int n = 0, i;
    for (y = sc->syms; y && n < 512; y = y->link) a[n++] = y;
    for (i = n - 1; i >= 0; i--) {
        y = a[i];
        if (y->cls != C_VAR || y->par || y->isparam) continue;
        if (y->stor == ST_STAT || y->stor == ST_BASED ||
            y->stor == ST_CTL) continue;
        if (y->ty == TY_FILE || !y->cn) continue;
        if (y->owner && y->owner->rec) gen_fill(y, y->cn);
        if (y->init || y->ty == TY_STRUCT) gen_init(y, y->cn);
    }
}

static void gs_block(Node *s)
{
    Scope *save = csc;
    csc = s->sc;
    ln("{");
    ind++;
    scope_inits(csc);
    gsl(s->b);
    ind--;
    ln("}");
    csc = save;
}

static void gsl(Node *s)
{
    for (; s; s = s->next) gs(s);
}

static void g_io(Node *s);
static void g_file(Node *s);

static void gs_one(Node *s)
{
    switch (s->k) {
    case S_ASSIGN: g_assign(s); break;
    case S_IF: g_if(s); break;
    case S_DO: g_do(s); break;
    case S_BEGIN: {
        Loop *l = pushloop(s, 0);
        gs_block(s);
        endbrk(l);
        nlp--;
        break;
    }
    case S_PROC: break;
    case S_CALL: g_call(s); break;
    case S_RETURN: g_return(s); break;
    case S_GOTO: g_goto(s); break;
    case S_LEAVE: {
        Loop *l = findloop(s, 0);
        l->usedb = 1;
        ln(cat3("goto ", lbl("lb", l->id), ";"));
        break;
    }
    case S_ITER: {
        Loop *l = findloop(s, 1);
        l->usedc = 1;
        ln(cat3("goto ", lbl("lc", l->id), ";"));
        break;
    }
    case S_SELECT: g_select(s); break;
    case S_STOP: ln("pl_stop(0);"); break;
    case S_NULL: break;
    case S_DECL: break;
    case S_PUT: case S_GET: g_io(s); break;
    case S_OPEN: case S_CLOSE: case S_READ: case S_WRITE:
    case S_REWRITE: g_file(s); break;
    case S_ALLOC: g_alloc(s, 0); break;
    case S_FREE: g_alloc(s, 1); break;
    case S_ON: g_on(s, 0); break;
    case S_REVERT: g_on(s, 1); break;
    case S_SIGNAL: g_signal(s); break;
    case S_DISPLAY:
        ln(cat3("pl_disp(", cstr(s->a), ");"));
        if (s->b) {
            LV v = rl(s->b);
            ln(cat3("pl_reply(", pvtxt(v), ");"));
        }
        break;
    default:
        perr(s->line, "statement not supported", 0);
    }
}

static void gs(Node *s)
{
    Node *l;
    int save = uses_tmp, mine;
    Sb saved;
    int sub = 0;
    for (l = s->lab; l && s->k != S_PROC; l = l->next) {
        Sym *y = lookupc(csc, l->s, C_LABEL);
        if (y) ln(cat2(y->cn, ": ;"));
    }
    if (s->pre & PRE_SUBS) { sub = subs_chk; subs_chk = 1; }
    if (s->pre & PRE_NOCHK) { sub = subs_chk; subs_chk = 0; }
    uses_tmp = 0;
    saved = cur->body;
    memset(&cur->body, 0, sizeof cur->body);
    gs_one(s);
    mine = uses_tmp;
    {
        Sb got = cur->body;
        cur->body = saved;
        if (mine && s->k != S_RETURN) {
            int i;
            for (i = 0; i < ind; i++) sb_s(&cur->body, "    ");
            sb_s(&cur->body, "{ PLM m_ = pl_mark();\n");
            sb_s(&cur->body, sb_str(&got));
            for (i = 0; i < ind; i++) sb_s(&cur->body, "    ");
            sb_s(&cur->body, "pl_rel(m_); }\n");
        } else {
            sb_s(&cur->body, sb_str(&got));
        }
    }
    if (s->pre) subs_chk = sub;
    uses_tmp = save;
}
