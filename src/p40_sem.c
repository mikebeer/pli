/* p40_sem.c -- symbols, scopes, declarations and storage */

static Scope *root = 0;
static Sb gdecl, gproto, gfun;
static int idc = 0;
static Fn *cur = 0;
static Scope *csc = 0;
static Fn *fnall[512];
static int nfn = 0;
static Sym *statinit[2048];
static int nstat = 0;
static int opt_subs = 0;
static int modmode = 0;         /* -M: separately compiled module */
static char *modbase = "";      /* module name (init function) */
static char *withmods[32];      /* names of the other modules */
static int nwith = 0;

static char *cident(const char *name);

/* the C name of an external procedure or variable */
static char *extname(const char *name, const char *xname, int line)
{
    char *n = xname ? xdup(xname) : cident(name);
    char *q;
    if (!xname)
        for (q = n; *q; q++) *q = (char)tolower((unsigned char)*q);
#ifdef PLI_CMS
    if (strlen(n) > 8)
        perr(line, "external name longer than 8 characters:", n);
#else
    (void)line;
#endif
    return n;
}

static Scope *newscope(Scope *par, Fn *fn, Node *n)
{
    Scope *s = (Scope *)xalloc((long)sizeof(Scope));
    s->par = par;
    s->fn = fn;
    s->n = n;
    return s;
}

static Fn *newfn(char *name, Fn *par, int rec)
{
    Fn *f = (Fn *)xalloc((long)sizeof(Fn));
    f->name = name;
    f->par = par;
    f->rec = rec;
    f->id = ++idc;
    if (nfn >= 512) die("too many procedures");
    fnall[nfn++] = f;
    return f;
}

static char *cident(const char *s)
{
    char *p = xdup(s);
    char *q;
    for (q = p; *q; q++)
        if (!isalnum((unsigned char)*q) && *q != '_') *q = '_';
    return p;
}

static char *mkcn(const char *pre, int id, const char *name)
{
    char b[24];
    char *n = cident(name);
    if (strlen(n) > 24) n[24] = 0;
    sprintf(b, "%s%d_", pre, id);
    return cat2(b, n);
}

static Sym *newsym(Scope *sc, const char *name, int cls)
{
    Sym *s = (Sym *)xalloc((long)sizeof(Sym));
    s->name = xdup(name);
    s->cls = cls;
    s->id = ++idc;
    s->sc = sc;
    s->owner = sc ? sc->fn : 0;
    s->lo[0] = 1;
    if (sc) {
        s->link = sc->syms;
        sc->syms = s;
    }
    return s;
}

/* find a name in a scope chain; top-level variables win over
 * structure members of the same scope */
static Sym *lookupc(Scope *sc, const char *name, int cls)
{
    Sym *s, *m;
    for (; sc; sc = sc->par) {
        m = 0;
        for (s = sc->syms; s; s = s->link) {
            if (strcmp(s->name, name)) continue;
            if (cls == C_LABEL) {
                if (s->cls == C_LABEL) return s;
                continue;
            }
            if (s->cls == C_LABEL) continue;
            if (!s->par) return s;
            if (!m) m = s;
        }
        if (m) return m;
    }
    return 0;
}

static Sym *lookup(const char *name)
{
    return lookupc(csc, name, C_VAR);
}

static int is_long(Sym *s)
{
    return (s->ty == TY_FXB && s->p <= 31) ||
           (s->ty == TY_FXD && s->q == 0 && s->p <= 9);
}

/* functions declared by the C headers that plirt.h includes: a
   prototype of our own would conflict with the real one */
static int is_libc(const char *n)
{
    static const char *names[] = {
        "remove", "rename", "tmpfile", "tmpnam", "fclose", "fflush",
        "fopen", "freopen", "setbuf", "setvbuf", "fprintf", "fscanf",
        "printf", "scanf", "sprintf", "sscanf", "vfprintf", "vprintf",
        "vsprintf", "fgetc", "fgets", "fputc", "fputs", "getc",
        "getchar", "gets", "putc", "putchar", "puts", "ungetc",
        "fread",
        "fwrite", "fgetpos", "fseek", "fsetpos", "ftell", "rewind",
        "clearerr", "feof", "ferror", "perror", "atof", "atoi", "atol",
        "strtod", "strtol", "strtoul", "rand", "srand", "calloc",
        "free", "malloc", "realloc", "abort", "atexit", "exit",
        "getenv", "system", "bsearch", "qsort", "abs", "div", "labs",
        "ldiv", "memcpy", "memmove", "strcpy", "strncpy", "strcat",
        "strncat", "memcmp", "strcmp", "strcoll", "strncmp", "strxfrm",
        "memchr", "strchr", "strcspn", "strpbrk", "strrchr", "strspn",
        "strstr", "strtok", "memset", "strerror", "strlen", "acos",
        "asin", "atan", "atan2", "cos", "sin", "tan", "cosh", "sinh",
        "tanh", "exp", "frexp", "ldexp", "log", "log10", "modf", "pow",
        "sqrt", "ceil", "fabs", "floor", "fmod", "clock", "difftime",
        "mktime", "time", "asctime", "ctime", "gmtime", "localtime",
        "strftime", "isalnum", "isalpha", "iscntrl", "isdigit",
        "isgraph", "islower", "isprint", "ispunct", "isspace",
        "isupper", "isxdigit", "tolower", "toupper", "longjmp", 0
    };
    int i;
    for (i = 0; names[i]; i++)
        if (!strcmp(names[i], n)) return 1;
    return 0;
}

/* C type of a scalar passed by value to or from a C function */
static const char *ctype_of(Sym *s)
{
    switch (s->ty) {
    case TY_FXB:
        return s->p <= 15 ? "short" : (s->p <= 31 ? "int" : "double");
    case TY_FXD:
        return (s->q == 0 && s->p <= 9) ? "int" : "double";
    case TY_FLT: return s->p <= 7 ? "float" : "double";
    case TY_PTR: return "void *";
    case TY_CHR: case TY_BIT: return "const char *";
    case TY_VOID: return "void";
    }
    return "double";
}

static int is_num(Sym *s)
{
    return s->ty == TY_FXB || s->ty == TY_FXD || s->ty == TY_FLT;
}

static int is_str(Sym *s)
{
    return s->ty == TY_CHR || s->ty == TY_BIT;
}

static long elems(Sym *s)
{
    long n = 1;
    int i;
    for (i = 0; i < s->nd; i++) n *= s->hi[i] - s->lo[i] + 1;
    return n;
}

/* number of elements as C text (run-time for a CONTROLLED array) */
static char *elem_text(Sym *s)
{
    char *t;
    int i;
    long m = 1;
    if (!s->lbv) return itoa_(elems(s));
    t = cat5("(", s->hbv, " - ", s->lbv, " + 1)");
    for (i = 1; i < s->nd; i++) m *= s->hi[i] - s->lo[i] + 1;
    if (m != 1) t = cat3(t, " * ", itoa_(m));
    return t;
}

static void set_default_type(Sym *s)
{
    char c = s->name[0];
    if (c >= 'i' && c <= 'n') {
        s->ty = TY_FXB;
        s->p = 15;
        s->q = 0;
    } else {
        s->ty = TY_FLT;
        s->p = 6;
        s->q = 0;
    }
}

/* upper case, with repetition factors (3)9 written out */
static char *pic_expand(const char *p)
{
    Sb b;
    memset(&b, 0, sizeof b);
    while (*p) {
        if (*p == '(' && isdigit((unsigned char)p[1])) {
            int n = atoi(p + 1);
            while (*p && *p != ')') p++;
            if (*p) p++;
            if (*p) {
                char c = (char)toupper((unsigned char)*p++);
                while (n-- > 0) sb_c(&b, c);
            }
            continue;
        }
        sb_c(&b, (char)toupper((unsigned char)*p++));
    }
    return xdup(sb_str(&b));
}

static int pic_width(const char *p)
{
    int n = 0;
    for (; *p; p++) {
        if (*p == 'V') continue;
        if ((p[0] == 'C' && p[1] == 'R') ||
            (p[0] == 'D' && p[1] == 'B')) {
            p++;
            n++;
        }
        n++;
    }
    return n;
}

static void apply_type(Sym *s, Attr *a)
{
    s->ext = a->ext;
    if (a->pic) {
        s->pic = pic_expand(a->pic);
        s->picnum = strpbrk(s->pic, "AX") == 0;
        s->ty = TY_CHR;
        s->vary = 0;
        s->len = pic_width(s->pic);
        if (s->len < 1) perr(0, "empty picture for", s->name);
        return;
    }
    if (a->ty == TY_CHR || a->ty == TY_BIT) {
        s->ty = a->ty;
        s->len = a->len == -2 ? 1 : a->len;
        s->vary = a->vary;
    } else if (a->ty) {
        s->ty = a->ty;
        if (a->ty == TY_AREA) s->len = a->len;
    } else if (a->fix || a->base) {
        int fix = a->fix, base = a->base;
        if (!fix) fix = 2;
        if (!base) base = 2;
        if (fix == 1) {
            if (base == 1) {
                s->ty = TY_FXB;
                s->p = a->p < 0 ? 15 : a->p;
                if (s->p > 63) perr(0, "FIXED BINARY precision > 63",
                                    s->name);
            } else {
                s->ty = TY_FXD;
                s->p = a->p < 0 ? 5 : a->p;
                if (s->p > 31) perr(0, "FIXED DECIMAL precision > 31",
                                    s->name);
            }
            s->q = a->q < 0 ? 0 : a->q;
            if (s->ty == TY_FXB && s->q) 
                perr(0, "FIXED BINARY scale unsupported", s->name);
        } else {
            s->ty = TY_FLT;
            s->p = a->p < 0 ? (base == 1 ? 21 : 6) : a->p;
            if (base == 1) s->p = (s->p * 3 + 9) / 10;
            if (s->p < 1) s->p = 1;
        }
        if (a->cpx) s->ty = TY_CPX;
    } else if (a->isent) {
        s->ty = TY_ENT;
    } else if (a->cpx) {
        s->ty = TY_CPX;
        s->p = 6;
    }
}

static void copy_kids(Sym *dst, Sym *src, Scope *sc)
{
    Sym *k, *n, *t = 0;
    for (k = src->kid; k; k = k->nxt) {
        n = newsym(sc, k->name, C_VAR);
        n->ty = k->ty; n->p = k->p; n->q = k->q;
        n->len = k->len; n->vary = k->vary;
        n->pic = k->pic; n->picnum = k->picnum;
        n->nd = k->nd; n->star = k->star;
        memcpy(n->lo, k->lo, sizeof n->lo);
        memcpy(n->hi, k->hi, sizeof n->hi);
        n->init = k->init;
        n->par = dst;
        n->level = k->level;
        if (k->kid) copy_kids(n, k, sc);
        if (t) t->nxt = n; else dst->kid = n;
        t = n;
    }
}

static void set_dims(Sym *s, Node *b)
{
    int i = 0;
    for (; b; b = b->next) {
        if (i >= MAXD) perr(b->line, "too many dimensions", s->name);
        if (b->fl) {
            if (i > 0)
                perr(b->line, "only the first bound may be *", 0);
            s->star = 1;
            s->lo[i] = 1;
            s->hi[i] = 0;
        } else {
            s->lo[i] = b->a ? (long)cval(b->a) : 1;
            s->hi[i] = (long)cval(b->b);
            if (s->hi[i] < s->lo[i])
                perr(b->line, "empty array bounds", s->name);
        }
        i++;
    }
    s->nd = i;
}

static char *membname(Sym *s)
{
    return cat2("m_", cident(s->name));
}

static char *base_text(Sym *s);

static char *dimtext(Sym *s)
{
    Sb b;
    int i;
    char t[40];
    memset(&b, 0, sizeof b);
    for (i = 0; i < s->nd; i++) {
        sprintf(t, "[%ld]", (i == 0 && s->stor == ST_CTL) ? 1L :
                s->hi[i] - s->lo[i] + 1);
        sb_s(&b, t);
    }
    return xdup(sb_str(&b));
}

/* C declaration text of symbol s, with declarator name nm */
static char *decl_text(Sym *s, const char *nm)
{
    char t[40];
    char *base;
    if (s->ty == TY_CHR || s->ty == TY_BIT) {
        if (s->len < 0)
            perr(0, "string length * not allowed", s->name);
        if (s->vary) {
            sprintf(t, "[%d]", s->len + 1);
            base = cat3("struct { long n; char s", t, "; }");
            return cat4(base, " ", nm, dimtext(s));
        }
        sprintf(t, "[%d]", s->len + 1);
        return cat4("char ", nm, dimtext(s), t);
    }
    base = base_text(s);
    return cat4(base, " ", nm, dimtext(s));
}

static char *base_text(Sym *s)
{
    Sym *k;
    Sb b;
    char *nm;
    switch (s->ty) {
    case TY_FXB: return is_long(s) ? "long" : "double";
    case TY_FXD: return is_long(s) ? "long" : "double";
    case TY_FLT: return "double";
    case TY_CPX: return "PC";
    case TY_AREA:
        return cat3("struct { long used; long pad; char d[",
                    itoa_((long)s->len), "]; }");
    case TY_PTR: return "void *";
    case TY_FILE: return "PLF";
    case TY_STRUCT:
        memset(&b, 0, sizeof b);
        sb_s(&b, "struct { ");
        for (k = s->kid; k; k = k->nxt) {
            nm = membname(k);
            k->cn = nm;
            sb_s(&b, decl_text(k, nm));
            sb_s(&b, "; ");
        }
        sb_s(&b, "}");
        return xdup(sb_str(&b));
    }
    perr(0, "cannot declare storage for", s->name);
    return "";
}

static int is_sysfile(Sym *s)
{
    return s->ty == TY_FILE &&
           (!strcmp(s->name, "sysin") || !strcmp(s->name, "sysprint"));
}

static void emit_storage(Sym *s)
{
    char *line;
    s->tn = mkcn("T", s->id, "");
    s->tn[strlen(s->tn) - 1] = 0;
    if (s->ty == TY_FILE) {
        if (is_sysfile(s)) {
            s->cn = !strcmp(s->name, "sysin") ? "pl_sysin"
                                              : "pl_sysprt";
            return;
        }
        s->cn = mkcn("f", s->id, s->name);
        sb_s(&gdecl, cat3("static PLF ", s->cn, ";\n"));
        if (nstat < 2048) statinit[nstat++] = s;
        return;
    }
    s->cn = mkcn("v", s->id, s->name);
    if (modmode && s->ext && s->stor != ST_BASED &&
        s->stor != ST_CTL && s->ty != TY_STRUCT) {
        /* EXTERNAL variable: defined at the outermost level of one
           module, referred to (extern) from inside procedures */
        s->cn = extname(s->name, s->xname, 0);
        sb_s(&gdecl, cat4("typedef ", decl_text(s, s->tn), ";", "\n"));
        if (s->isextv) {
            sb_s(&gdecl, cat5("extern ", s->tn, " ", s->cn, ";\n"));
            return;
        }
        sb_s(&gdecl, cat5("", s->tn, " ", s->cn, ";\n"));
        if (nstat >= 2048) die("too many variables");
        statinit[nstat++] = s;
        return;
    }
    sb_s(&gdecl, cat4("typedef ", decl_text(s, s->tn), ";", "\n"));
    if (s->defbase) {
        Sym *b = lookupc(s->sc ? s->sc : csc, s->defbase,
                         C_VAR);
        char *at;
        if (!b || b->cls != C_VAR || !b->cn || b->stor == ST_BASED ||
            b->stor == ST_CTL || b->isparam || b->ty == TY_FILE)
            perr(0, "bad DEFINED base for", s->name);
        if (b->defbase)
            perr(0, "DEFINED on a DEFINED variable:", s->name);
        if (is_str(s)) {
            if (!is_str(b) || b->vary || s->vary || s->nd ||
                b->nd || s->defpos < 1 ||
                s->defpos - 1 + s->len != b->len)
                perr(0, "DEFINED string must end where its base "
                     "ends:", s->name);
            at = cat3("((char *)&", b->cn, cat3(" + ",
                      itoa_(s->defpos - 1), ")"));
        } else {
            at = cat2("&", b->cn);
        }
        s->cn = cat5("(*(", s->tn, " *)", at, ")");
        return;
    }
    if (s->stor == ST_BASED) return;
    if (s->stor == ST_CTL) {
        s->hptr = newsym(0, s->name, C_VAR);
        s->hptr->cn = cat2(s->cn, "_p");
        sb_s(&gdecl, cat3("static void *", s->hptr->cn, ";\n"));
        if (s->nd) {
            s->lbv = cat2(s->cn, "_lb");
            s->hbv = cat2(s->cn, "_hb");
            sb_s(&gdecl, cat5("static long ", s->lbv, ", ", s->hbv,
                              ";\n"));
        }
        return;
    }
    line = cat4(s->tn, " ", s->cn, ";\n");
    if (s->owner && s->owner->rec && s->stor != ST_STAT)
        sb_s(&s->owner->decl, cat2("    ", line));
    else
        sb_s(&gdecl, cat2("static ", line));
    if (s->stor == ST_STAT || !(s->owner && s->owner->rec)) {
        if (nstat >= 2048) die("too many variables");
        statinit[nstat++] = s;
    }
}

static void setup_sym(Sym *s, Node *it, Scope *sc)
{
    Attr *a = it->at;
    apply_type(s, a);
    s->stor = a->stor;
    s->init = a->init;
    s->based = a->based;
    s->fprt = a->prt;
    s->frec = a->rec;
    s->fmode = a->mode;
    s->fkeyed = a->keyed;
    s->defbase = a->defbase;
    s->defpos = a->defpos ? (long)cval(a->defpos) : 1;
    if (it->b) set_dims(s, it->b);
    if (a->like) {
        Sym *l = lookupc(sc, a->like, C_VAR);
        if (!l || l->ty != TY_STRUCT)
            perr(it->line, "LIKE needs a structure:", a->like);
        s->ty = TY_STRUCT;
        copy_kids(s, l, sc);
    }
    s->xname = a->xname;
    if (a->isent) {
        s->cls = C_PROC;
        s->pn = 0;
        s->isextp = 1;
        s->copt = a->copt;
        s->cn = extname(s->name, a->xname, it->line);
        if (a->hasdsc) {
            Scope *ns = newscope(sc, 0, 0);
            Attr *d;
            Sym *p, *pt = 0;
            char nm[16];
            int i = 0;
            s->hasdsc = 1;
            for (d = a->dsc; d; d = d->dnext) {
                sprintf(nm, "d%d", ++i);
                p = newsym(ns, nm, C_VAR);
                p->isparam = 1;
                p->stor = ST_PARAM;
                p->level = 1;
                apply_type(p, d);
                if (d->dims) set_dims(p, d->dims);
                if (!p->ty) set_default_type(p);
                if (pt) pt->params = p; else s->params = p;
                pt = p;
            }
        }
        if (a->ret) {
            Sym t;
            memset(&t, 0, sizeof t);
            t.name = s->name;
            apply_type(&t, a->ret);
            if (!t.ty) set_default_type(&t);
            s->ty = t.ty; s->p = t.p; s->q = t.q;
            s->len = t.len; s->vary = t.vary;
            s->isfun = 1;
        } else {
            s->ty = TY_VOID;
        }
    }
}

static Sym *find_in_scope(Scope *sc, const char *name)
{
    Sym *s;
    for (s = sc->syms; s; s = s->link)
        if (s->cls != C_LABEL && !s->par && !strcmp(s->name, name))
            return s;
    return 0;
}

static void finish_sym(Sym *s)
{
    if (s->isparam) {
        if (s->ty == TY_STRUCT) {
            s->tn = mkcn("T", s->id, "");
            s->tn[strlen(s->tn) - 1] = 0;
            sb_s(&gdecl, cat4("typedef ", decl_text(s, s->tn),
                              ";", "\n"));
        }
        return;
    }
    if (s->cls != C_VAR) return;
    if (!s->ty) set_default_type(s);
    if (s->ty == TY_ENT) return;
    if (s->ty == TY_LBL) {
        s->cn = mkcn("v", s->id, s->name);
        s->tn = "long";
        sb_s(&gdecl, cat3("static long ", s->cn, ";\n"));
        return;
    }
    emit_storage(s);
}

static void decl_items(Scope *sc, Node *it)
{
    Sym *stk[16];
    int sp = 0, lvl;
    Sym *top = 0, *s, *par, *last;
    for (; it; it = it->next) {
        lvl = it->n;
        if (lvl <= 0) lvl = 1;
        if (lvl == 1 && it->at && it->at->ext && !it->at->isent &&
            !it->at->isfile && strcmp(it->s, "*") &&
            !(it->next && it->next->n > 1)) {
            /* EXTERNAL variable: one copy, shared by all modules */
            s = find_in_scope(root, it->s);
            if (s && s->ext && s->cls == C_VAR) {
                if (it->at->init && !s->init) s->init = it->at->init;
                if (modmode && sc == root && s->isextv) {
                    /* the definition follows a reference */
                    s->isextv = 0;
                    emit_storage(s);
                }
                continue;
            }
            if (sc != root) {
                s = newsym(root, it->s, C_VAR);
                s->level = 1;
                s->isextv = modmode;
                setup_sym(s, it, root);
                finish_sym(s);
                continue;
            }
        }
        if (!strcmp(it->s, "*")) {
            s = newsym(0, "*", C_VAR);
        } else if (lvl == 1 && (s = find_in_scope(sc, it->s)) &&
                   s->isparam && !s->ty) {
            /* parameter being given its attributes */
        } else {
            if (lvl == 1 && find_in_scope(sc, it->s))
                perr(it->line, "multiple declaration of", it->s);
            s = newsym(sc, it->s, C_VAR);
        }
        s->level = lvl;
        setup_sym(s, it, sc);
        if (lvl == 1) {
            if (top) finish_sym(top);
            top = s;
            sp = 0;
            stk[sp++] = s;
        } else {
            while (sp > 0 && stk[sp - 1]->level >= lvl) sp--;
            if (sp == 0) perr(it->line, "bad structure level", it->s);
            par = stk[sp - 1];
            par->ty = TY_STRUCT;
            s->par = par;
            s->owner = par->owner;
            if (!s->ty) set_default_type(s);
            last = par->last;
            if (last) last->nxt = s; else par->kid = s;
            par->last = s;
            if (sp < 15) stk[sp++] = s;
        }
        if (s->ty == TY_FXB && s->p == 0) s->p = 15;
    }
    if (top) finish_sym(top);
}

static void fix_params(Scope *sc, Sym *ps)
{
    Sym *p;
    for (p = ps; p; p = p->params) {
        if (!p->ty) set_default_type(p);
    }
    (void)sc;
}

static int name_is_builtin(const char *n);

static void scan_stmts(Scope *sc, Node *s, Fn *fn);

static void scan_one(Scope *sc, Node *s, Fn *fn)
{
    Node *l, *w;
    Sym *y;
    for (l = s->lab; l; l = l->next) {
        if (s->k == S_PROC) continue;
        if (lookupc(sc, l->s, C_LABEL) &&
            lookupc(sc, l->s, C_LABEL)->sc == sc)
            perr(l->line, "duplicate label", l->s);
        y = newsym(sc, l->s, C_LABEL);
        y->cn = mkcn("L", y->id, l->s);
        y->owner = fn;
    }
    switch (s->k) {
    case S_DECL:
        decl_items(sc, s->a);
        break;
    case S_PROC: {
        Fn *f;
        Scope *ns;
        Sym *ps, *p, *pt = 0;
        Node *q;
        char *nm = s->lab->s;
        int rec = (s->fl & 1) != 0;
        ps = find_in_scope(sc, nm);
        if (ps && ps->cls == C_PROC && ps->pn)
            perr(s->line, "duplicate procedure", nm);
        if (!ps || ps->cls != C_PROC) ps = newsym(sc, nm, C_PROC);
        ps->pn = s;
        ps->cn = mkcn("P", ps->id, nm);
        ps->xname = s->at->xname;
        ps->copt = (s->fl & 4) != 0;
        if (modmode && sc == root && !(s->fl & 2)) {
            ps->isextp = 1;
            ps->cn = extname(nm, ps->xname, s->line);
        }
        f = newfn(ps->cn, fn, rec);
        f->ext = ps->isextp && modmode && sc == root;
        f->ps = ps;
        f->node = s;
        ps->rec = rec;
        ns = newscope(sc, f, s);
        s->sc = ns;
        s->sym = ps;
        for (q = s->a; q; q = q->next) {
            p = newsym(ns, q->s, C_VAR);
            p->isparam = 1;
            p->stor = ST_PARAM;
            p->level = 1;
            if (pt) pt->params = p; else ps->params = p;
            pt = p;
        }
        if (s->at->ret) {
            Sym t;
            memset(&t, 0, sizeof t);
            t.name = nm;
            apply_type(&t, s->at->ret);
            if (!t.ty) set_default_type(&t);
            ps->ty = t.ty; ps->p = t.p; ps->q = t.q;
            ps->len = t.len; ps->vary = t.vary;
            ps->isfun = 1;
        } else if (!ps->ty) {
            ps->ty = TY_VOID;
        }
        scan_stmts(ns, s->b, f);
        for (p = ps->params; p; p = p->params) {
            if (!p->ty) set_default_type(p);
            p->owner = f;
        }
        fix_params(ns, ps->params);
        break;
    }
    case S_BEGIN: {
        Scope *ns = newscope(sc, fn, s);
        s->sc = ns;
        scan_stmts(ns, s->b, fn);
        break;
    }
    case S_DO:
        scan_stmts(sc, s->c, fn);
        break;
    case S_IF:
        scan_one(sc, s->b, fn);
        if (s->c) scan_one(sc, s->c, fn);
        break;
    case S_SELECT:
        for (w = s->b; w; w = w->next) scan_one(sc, w->b, fn);
        if (s->c) scan_one(sc, s->c, fn);
        break;
    case S_ON:
        if (s->b) {
            Fn *f = newfn(0, fn, 0);
            Scope *ns = newscope(sc, f, s);
            f->ison = 1;
            f->node = s;
            f->name = mkcn("on", f->id, "");
            s->sc = ns;
            scan_one(ns, s->b, f);
        }
        break;
    }
}

static void scan_stmts(Scope *sc, Node *s, Fn *fn)
{
    for (; s; s = s->next) scan_one(sc, s, fn);
}
