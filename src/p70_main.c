/* p70_main.c -- function assembly, output, command line */

static int c_conv = 0;          /* parameters in the C convention */

/* C type of a by-reference scalar parameter's local copy */
static const char *ptype(Sym *pm)
{
    if (is_long(pm)) return "long ";
    if (pm->ty == TY_PTR) return "void *";
    if (pm->ty == TY_CPX) return "PC ";
    return "double ";
}

static char *rettype(Sym *p)
{
    if (p->copt) return (char *)ctype_of(p);
    switch (p->ty) {
    case TY_FXB: case TY_FXD: case TY_FLT: return "double";
    case TY_CHR: case TY_BIT: return "PS";
    case TY_PTR: return "void *";
    case TY_CPX: return "PC";
    }
    return "void";
}

static char *param_decl(Sym *pm, Fn *f)
{
    char *nm = mkcn("a", pm->id, pm->name);
    char t[40];
    int i;
    Sb b;
    pm->isparam = 1;
    if (pm->ty == TY_CPX && (pm->nd || c_conv))
        perr(0, "COMPLEX array or C parameter unsupported:", pm->name);
    if (pm->ty == TY_FILE) {
        pm->cn = nm;
        return cat2("PLF *", nm);
    }
    if (pm->nd) {
        char *el;
        if (pm->ty == TY_STRUCT)
            perr(0, "array of structures parameter", pm->name);
        pm->cn = nm;
        memset(&b, 0, sizeof b);
        for (i = 1; i < pm->nd; i++) {
            if (pm->star) { if (i > 0 && pm->hi[i] == 0)
                perr(0, "inner bounds needed for", pm->name); }
            sprintf(t, "[%ld]", pm->hi[i] - pm->lo[i] + 1);
            sb_s(&b, t);
        }
        if (is_str(pm)) {
            sprintf(t, "[%d]", pm->len + 1);
            sb_s(&b, t);
            el = pm->vary ? "struct { long n; char s[1]; }" : "char";
            if (pm->vary) perr(0, "VARYING array parameter", pm->name);
        } else {
            el = is_long(pm) ? "long" : (pm->ty == TY_PTR ?
                                         "void *" : "double");
        }
        if (pm->star && c_conv && f)
            perr(0, "C-convention array needs explicit bounds:",
                 pm->name);
        if (pm->star && !c_conv) {
            pm->lbv = mkcn("lb", pm->id, pm->name);
            pm->hbv = mkcn("hb", pm->id, pm->name);
        }
        t[0] = 0;
        if (pm->lbv)
            sprintf(t, ", long %s, long %s", pm->lbv, pm->hbv);
        if (pm->nd == 1)
            return cat2(is_str(pm) ?
                cat4(el, " (*", nm, cat2(")", sb_str(&b))) :
                cat3(el, " *", nm), t);
        return cat2(cat5(el, " (*", nm, ")", sb_str(&b)), t);
    }
    if (pm->ty == TY_STRUCT) {
        pm->cn = nm;
        return cat4(pm->tn, " *", nm, "");
    }
    if (c_conv && is_str(pm)) {
        char *cp = mkcn("c", pm->id, pm->name);
        pm->cn = nm;
        pm->ispv = 1;
        if (f) {
            pm->cpar = cp;
            sb_s(&f->decl, cat3("    long nl", itoa_(pm->id), ";\n"));
            sb_s(&f->decl, cat3("    PV ", nm, ";\n"));
        }
        return cat2("const char *", cp);
    }
    if (is_str(pm)) {
        pm->cn = nm;
        pm->ispv = 1;
        return cat2("PV ", nm);
    }
    if (c_conv && (is_num(pm) || pm->ty == TY_PTR)) {
        char *cp = mkcn("c", pm->id, pm->name);
        pm->pcn = 0;
        pm->cn = mkcn("v", pm->id, pm->name);
        if (f) {
            pm->cpar = cp;
            if (f->rec)
                sb_s(&f->decl, cat5("    ", ptype(pm),
                     pm->cn, ";", "\n"));
            else
                sb_s(&gdecl, cat5("static ", ptype(pm),
                     pm->cn, ";", "\n"));
        }
        return cat3(ctype_of(pm), " ", cp);
    }
    pm->pcn = nm;
    pm->cn = mkcn("v", pm->id, pm->name);
    if (!f) {
        /* prototype only */
    } else if (f->rec)
        sb_s(&f->decl, cat5("    ", ptype(pm), pm->cn, ";",
             "\n"));
    else
        sb_s(&gdecl, cat5("static ", ptype(pm), pm->cn, ";",
             "\n"));
    return cat3(ptype(pm), "*", nm);
}

/* extern prototype of an ENTRY that is declared in this module and
   defined in another */
static char *entry_proto(Sym *p)
{
    Sb b;
    Sym *pm;
    int first = 1;
    memset(&b, 0, sizeof b);
    c_conv = p->copt;
    sb_s(&b, cat4("extern ", rettype(p), " ", p->cn));
    sb_c(&b, '(');
    for (pm = p->params; pm; pm = pm->params) {
        if (!first) sb_s(&b, ", ");
        first = 0;
        sb_s(&b, param_decl(pm, 0));
    }
    if (first) sb_s(&b, "void");
    sb_s(&b, ");\n");
    c_conv = 0;
    return xdup(sb_str(&b));
}

static void gen_fn(Fn *f)
{
    Sb hdr, pro, ep;
    Sym *pm;
    Node *n = f->node;
    char *h;
    int first = 1;
    memset(&hdr, 0, sizeof hdr);
    memset(&pro, 0, sizeof pro);
    memset(&ep, 0, sizeof ep);
    cur = f;
    nlp = 0;
    ind = 1;
    csc = n->sc;
    if (f->ison) {
        sb_s(&hdr, cat3("static void ", f->name, "(void)"));
        sb_s(&gproto, cat2(sb_str(&hdr), ";\n"));
        scope_inits(csc);
        gs(n->b);
    } else {
        Sym *ps = f->ps;
        sb_s(&hdr, cat4(f->ext ? "" : "static ", rettype(ps), " ",
                        f->name));
        sb_c(&hdr, '(');
        for (pm = ps->params; pm; pm = pm->params) {
            if (!first) sb_s(&hdr, ", ");
            first = 0;
            sb_s(&hdr, pm->pdecl);
        }
        if (first) sb_s(&hdr, "void");
        sb_c(&hdr, ')');
        sb_s(&gproto, cat2(sb_str(&hdr), ";\n"));
        f->hason = has_on(n->b);
        if (f->hason) {
            sb_s(&f->decl, "    PLH h_;\n");
            sb_s(&pro, "    pl_hsave(&h_);\n");
        }
        for (pm = ps->params; pm; pm = pm->params) {
            if (pm->pcn)
                sb_s(&pro, cat5("    ", pm->cn, " = *", pm->pcn,
                                ";\n"));
            else if (pm->cpar && pm->ispv) {
                char *l = cat2("nl", itoa_(pm->id));
                sb_s(&pro, cat5("    ", l, " = (long)strlen(",
                                pm->cpar, ");\n"));
                sb_s(&pro, cat5("    ", pm->cn, ".p = (char *)",
                                pm->cpar, ";\n"));
                sb_s(&pro, cat5("    ", pm->cn, ".np = &", l, ";\n"));
                sb_s(&pro, cat5("    ", pm->cn, ".max = ", l, ";\n"));
            } else if (pm->cpar)
                sb_s(&pro, cat5("    ", pm->cn, " = ", pm->cpar,
                                ";\n"));
        }
        {
            Sb save = f->body;
            memset(&f->body, 0, sizeof f->body);
            scope_inits(csc);
            subs_chk = opt_subs || (n->pre & PRE_SUBS);
            size_chk = (n->pre & PRE_SIZE) != 0;
            gsl(n->b);
            sb_s(&pro, sb_str(&f->body));
            f->body = save;
        }
        if (f->hason && ps->ty == TY_VOID)
            sb_s(&ep, "    pl_hrest(&h_);\n");
        for (pm = ps->params; pm; pm = pm->params)
            if (pm->pcn)
                sb_s(&ep, cat5("    *", pm->pcn, " = ", pm->cn,
                               ";\n"));
        if (ps->ty != TY_VOID)
            sb_s(&ep, "    pl_die(\"no RETURN executed\");\n");
    }
    (void)h;
    f->hdr = hdr;
    f->pro = pro;
    f->ep = ep;
}

static void asm_fn(Fn *f)
{
    sb_s(&gfun, sb_str(&f->hdr));
    sb_s(&gfun, "\n{\n");
    sb_s(&gfun, sb_str(&f->decl));
    if (f->jb) {
        int i;
        sb_s(&gdecl, cat3("static jmp_buf jb", itoa_(f->id), ";\n"));
        sb_s(&gfun, cat3("    if (setjmp(jb", itoa_(f->id), ")) {\n"));
        sb_s(&gfun, "        switch (pl_gtsel) {\n");
        for (i = 0; i < f->nlg; i++)
            sb_s(&gfun, cat5("        case ", itoa_(f->lgid[i]),
                 ": goto ", f->lg[i]->cn, ";\n"));
        sb_s(&gfun, "        }\n    }\n");
    }
    if (f->ison) sb_s(&gfun, sb_str(&f->body));
    else sb_s(&gfun, sb_str(&f->pro));
    sb_s(&gfun, sb_str(&f->ep));
    sb_s(&gfun, "}\n\n");
}

static void emit_wrapped(FILE *o, const char *text)
{
    const char *p = text, *e;
    char line[1024];
    long n, i, lead, brk, col;
    int instr, last;
    while (*p) {
        e = strchr(p, '\n');
        n = e ? (long)(e - p) : (long)strlen(p);
        if (n > 1000) n = 1000;
        memcpy(line, p, (size_t)n);
        line[n] = 0;
        p += n;
        if (*p == '\n') p++;
        lead = 0;
        while (line[lead] == ' ') lead++;
        while (n > 71) {
            instr = 0;
            brk = -1;
            for (i = 0; i < n && i < 71; i++) {
                if (line[i] == '\\' && instr) { i++; continue; }
                if (line[i] == '"') instr = !instr;
                if (!instr && i > lead + 8 &&
                    (line[i] == ' ' || line[i] == ',') &&
                    i + 1 < n) {
                    brk = line[i] == ',' ? i + 1 : i;
                }
            }
            if (brk < 0) {
                /* look further right for any break point */
                instr = 0;
                for (i = 0; i < n; i++) {
                    if (line[i] == '\\' && instr) { i++; continue; }
                    if (line[i] == '"') instr = !instr;
                    if (!instr && i > lead + 8 && line[i] == ' ') {
                        brk = i;
                        break;
                    }
                }
                if (brk < 0) break;
            }
            col = brk;
            fwrite(line, 1, (size_t)col, o);
            fputc('\n', o);
            last = 0;
            while (line[brk] == ' ') brk++;
            (void)last;
            memmove(line + lead + 4, line + brk,
                    (size_t)(n - brk + 1));
            for (i = 0; i < lead + 4; i++) line[i] = ' ';
            n = lead + 4 + (n - brk);
        }
        fwrite(line, 1, (size_t)n, o);
        fputc('\n', o);
    }
}

static Fn initfn;

static void gen_init_fn(void)
{
    int i;
    Sym *s;
    cur = &initfn;
    nlp = 0;
    ind = 1;
    for (i = 0; i < nstat; i++) {
        s = statinit[i];
        csc = s->sc ? s->sc : root;
        if (s->ty == TY_FILE) {
            if (s->fprt) ln(cat2(s->cn, ".prt = 1;"));
            continue;
        }
        gen_fill(s, s->cn);
    }
    for (i = 0; i < nstat; i++) {
        s = statinit[i];
        csc = s->sc ? s->sc : root;
        if (s->ty != TY_FILE && s->stor == ST_STAT)
            gen_init(s, s->cn);
    }
}

static char *outname(const char *in)
{
    char *o = xdup(in), *dot, *sl;
#ifdef PLI_CMS
    dot = strchr(o, ' ');
    if (dot) *dot = 0;
    return cat2(o, " c");
#else
    sl = strrchr(o, '/');
    dot = strrchr(o, '.');
    if (dot && (!sl || dot > sl)) *dot = 0;
    return cat2(o, ".c");
#endif
}


/* ---- -c: translate, then build the program with a C compiler ---- */

#ifdef _WIN32
#include <windows.h>
#define EXE_SFX ".exe"
#define NULDEV " >NUL 2>&1"
#define LIBM ""
#define DIRSEP '\\'
#else
#define EXE_SFX ""
#define NULDEV " >/dev/null 2>&1"
#define LIBM " -lm"
#define DIRSEP '/'
#endif

static char *self_dir(const char *argv0)
{
    char *d;
    char *sl;
#ifdef _WIN32
    char buf[520];
    DWORD n = GetModuleFileNameA(0, buf, 512);
    if (n > 0 && n < 512) argv0 = buf;
#endif
    d = xdup(argv0);
    sl = strrchr(d, DIRSEP);
#ifdef _WIN32
    if (!sl) sl = strrchr(d, '/');
#endif
    if (!sl) return xdup(".");
    *sl = 0;
    return d;
}

static int works(const char *cmd)
{
    return system(cat2(cmd, NULDEV)) == 0;
}

static int file_ok(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* choose a compiler: -cc name, PLI_CC, a C compiler on the path,
   and as the last resort the bundled TinyCC next to pli */
static char *pick_cc(const char *want, const char *dir, int *kind)
{
    static const char *names[] = { "gcc", "cc", "clang", "tcc", 0 };
    char *b;
    int i;
    *kind = 0;
    if (!want) want = getenv("PLI_CC");
    b = cat4("\"", dir, "/tcc/tcc" EXE_SFX, "\"");
    if (want && !strcmp(want, "bundled")) {
        *kind = 1;
        return b;
    }
    if (want) {
        *kind = !strcmp(want, "cl") ? 2 : 0;
        return xdup(want);
    }
    for (i = 0; names[i]; i++)
        if (works(cat2(names[i], " --version"))) return xdup(names[i]);
#ifdef _WIN32
    if (works("cl /? ")) { *kind = 2; return xdup("cl"); }
#endif
    if (file_ok(cat2(dir, "/tcc/tcc" EXE_SFX))) {
        *kind = 1;
        return b;
    }
    return 0;
}

static char *self_path(const char *argv0)
{
#ifdef _WIN32
    char buf[520];
    DWORD n = GetModuleFileNameA(0, buf, 512);
    if (n > 0 && n < 512) return xdup(buf);
#endif
    return xdup(argv0);
}

static int has_text(const char *path, const char *text)
{
    char line[256];
    FILE *f = fopen(path, "r");
    int n = 0, found = 0;
    if (!f) return 0;
    while (n++ < 12 && fgets(line, sizeof line, f))
        if (strstr(line, text)) found = 1;
    fclose(f);
    return found;
}

/* compile (mode 2) or compile and link (mode 1) C files and objects */
static int build_exe(char **files, int nf, const char *exe,
                     const char *want, const char *argv0, int mode)
{
    char *dir = self_dir(argv0);
    char *cc;
    char *cmd;
    char *inc;
    char *list = xdup("");
    int kind, rc, i;
    cc = pick_cc(want, dir, &kind);
    if (!cc) {
        fprintf(stderr, "pli: no C compiler found; install gcc, "
                "clang or tcc, or use -cc name\n");
        return 8;
    }
    inc = cat5("-I\"", dir, "\" -I\"", dir, "/src\" -I\"");
    inc = cat5(inc, dir, "/../share/pli\" -I/usr/share/pli ",
               "-I/usr/local/share/pli", "");
    for (i = 0; i < nf; i++)
        list = cat4(list, " \"", files[i], "\"");
    if (kind == 2) {
        cmd = cat5(cc, " /nologo /I\"", dir, "\"",
                   mode == 2 ? " /c" : cat3(" /Fe\"", exe, "\""));
    } else {
        cmd = cat3(cc, " ", inc);
        if (mode == 2) cmd = cat2(cmd, " -c");
        else cmd = cat4(cmd, " -o \"", exe, "\"");
    }
    cmd = cat3(cmd, list, kind != 2 && mode == 1 ? LIBM : "");
#ifdef _WIN32
    cmd = cat3("\"", cmd, "\"");
#endif
    rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "pli: C compiler failed: %s\n", cmd);
        return 8;
    }
    return 0;
}

/* a source name without a file type gets the type PLI:
   matrix -> matrix.pli (CMS: "matrix pli") when that file exists */
static char *defext(char *n)
{
    char *base = strrchr(n, '/');
    char *t;
    char *back = strrchr(n, '\\');
    if (back && (!base || back > base)) base = back;
    base = base ? base + 1 : n;
    if (strchr(base, '.') || strchr(base, ' ')) return n;
#ifdef PLI_CMS
    t = cat2(n, " pli");
#else
    t = cat2(n, ".pli");
#endif
    return file_ok(t) ? t : n;
}

/* module name: file name without directory and type */
static char *modname(const char *in)
{
    char *o = xdup(in), *b, *d, *q;
    b = strrchr(o, '/');
    q = strrchr(o, '\\');
    if (q && (!b || q > b)) b = q;
    b = b ? b + 1 : o;
    d = strchr(b, '.');
    if (d) *d = 0;
    d = strchr(b, ' ');
    if (d) *d = 0;
    for (q = b; *q; q++) {
        if (isalnum((unsigned char)*q))
            *q = (char)tolower((unsigned char)*q);
        else *q = '_';
    }
    return b;
}

static char *exename(const char *in)
{
    char *o = xdup(in), *dot, *sl;
    sl = strrchr(o, '/');
    dot = strrchr(o, '.');
    if (dot && (!sl || dot > sl)) *dot = 0;
    return cat2(o, EXE_SFX);
}

static void usage(void)
{
    fprintf(stderr, "pli %s -- PL/I to C89 translator\n", VERSION);
    fprintf(stderr,
        "usage: pli [-c|-C] [-M] [-o out] [-m l,r] [-s] src ...\n");
    fprintf(stderr, "  -o f    output file (- for stdout)\n");
    fprintf(stderr, "  -c      build the program (needs a C\n");
    fprintf(stderr, "          compiler); -o names the exe;\n");
    fprintf(stderr, "          .c and .o files given are linked\n");
    fprintf(stderr, "  -C      compile to objects, no link\n");
    fprintf(stderr, "  -M      module: separately compiled, each\n");
    fprintf(stderr, "          source file gives its own C file\n");
    fprintf(stderr, "  -n m    module name (default: file name)\n");
    fprintf(stderr, "  -with a,b  other modules, for -M\n");
    fprintf(stderr, "  -cc c   compiler command, or 'bundled' for\n");
    fprintf(stderr, "          the TinyCC next to pli\n");
    fprintf(stderr, "  -m l,r  source margins, e.g. -m 2,72\n");
    fprintf(stderr, "  -s      check subscripts at run time\n");
    exit(4);
}

/* is this a PL/I source (or a bare name that is one)? */
static int is_pli(const char *n)
{
    const char *d = strrchr(n, '.');
    const char *sl = strrchr(n, '/');
    if (!d || (sl && d < sl)) return 1;
    d++;
    return !strcmp(d, "pli") || !strcmp(d, "pl1") || !strcmp(d, "pl");
}

/* translate the PL/I files given into one C file; returns 1 when the
   C file defines main (or the shared run-time state) */
static int translate(char **pf, int npf, char *out, int tostdout)
{
    int i, a;
    Node *prog, *s, *mainp = 0;
    FILE *o;
#ifdef PLI_CMS
    if (modmode && strlen(modbase) > 6)
        perr(0, "module name longer than 6 characters:", modbase);
#endif
    for (i = 0; i < npf; i++) lexfile(pf[i]);
    lexend();
    prog = p_program();
    root = newscope(0, 0, 0);
    csc = root;
    scan_stmts(root, prog, 0);
    for (s = prog; s; s = s->next) {
        if (s->k == S_PROC) {
            if ((s->fl & 2) && mainp && (mainp->fl & 2))
                perr(s->line, "second OPTIONS(MAIN) procedure", 0);
            if (!mainp || (s->fl & 2)) {
                if (!mainp || !(mainp->fl & 2)) mainp = s;
            }
        } else if (s->k != S_DECL && s->k != S_NULL) {
            perr(s->line, "only PROCEDURE or DECLARE allowed here", 0);
        }
    }
    if (modmode && mainp && !(mainp->fl & 2)) mainp = 0;
    if (!mainp && !modmode) perr(1, "no main procedure", 0);
    for (a = 0; a < nfn; a++)
        if (!fnall[a]->ison) {
            Sym *pm;
            c_conv = fnall[a]->ps->copt;
            if (c_conv && is_str(fnall[a]->ps))
                perr(fnall[a]->node->line,
                     "OPTIONS(C) procedure cannot return a string", 0);
            for (pm = fnall[a]->ps->params; pm; pm = pm->params)
                pm->pdecl = param_decl(pm, fnall[a]);
            c_conv = 0;
        }
    for (a = 0; a < nfn; a++) gen_fn(fnall[a]);
    for (a = 0; a < nfn; a++) asm_fn(fnall[a]);
    gen_init_fn();
    o = tostdout ? stdout : fopen(out, "w");
    if (!o) die("cannot open output file");
    fprintf(o, "/* generated by pli %s from %s */\n", VERSION, pf[0]);
    if (modmode) {
        fprintf(o, "#define PLI_MODULE\n");
        if (mainp) fprintf(o, "#define PLI_DEFG\n");
    }
    fprintf(o, "#include \"plirt.h\"\n\n");
    emit_wrapped(o, sb_str(&gdecl));
    fprintf(o, "\n");
    emit_wrapped(o, sb_str(&gproto));
    fprintf(o, "\n");
    emit_wrapped(o, sb_str(&gfun));
    fprintf(o, "static void pl_init(void)\n{\n");
    emit_wrapped(o, sb_str(&initfn.decl));
    emit_wrapped(o, sb_str(&initfn.body));
    fprintf(o, "}\n\n");
    if (modmode && !mainp) {
        fprintf(o, "void %s_i(void)\n{\n", modbase);
        fprintf(o, "    static int done = 0;\n");
        fprintf(o, "    if (done) return;\n    done = 1;\n");
        fprintf(o, "    pl_init();\n}\n");
    } else {
        for (i = 0; i < nwith; i++)
            fprintf(o, "extern void %s_i(void);\n", withmods[i]);
        fprintf(o, "int main(int argc, char **argv)\n{\n");
        fprintf(o, "    pl_start(argc, argv);\n");
        for (i = 0; i < nwith; i++)
            fprintf(o, "    %s_i();\n", withmods[i]);
        fprintf(o, "    pl_init();\n");
        fprintf(o, "    %s();\n    pl_stop(0);\n    return 0;\n}\n",
                mainp->sym->cn);
    }
    if (o != stdout) fclose(o);
    return mainp != 0;
}

int main(int argc, char **argv)
{
    char *out = 0, *ccname = 0, *margs = 0;
    char *pf[MAXSRC], *oth[MAXSRC], *cf[MAXSRC + 2];
    int npf = 0, noth = 0, ncf = 0;
    int build = 0, objonly = 0, i, rc, hasmain = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "-s")) opt_subs = 1;
        else if (!strcmp(argv[i], "-c")) build = 1;
        else if (!strcmp(argv[i], "-C")) objonly = 1;
        else if (!strcmp(argv[i], "-M")) modmode = 1;
        else if (!strcmp(argv[i], "-n") && i + 1 < argc)
            modbase = argv[++i];
        else if (!strcmp(argv[i], "-with") && i + 1 < argc) {
            char *w = xdup(argv[++i]);
            char *c;
            for (c = strtok(w, ","); c && nwith < 32;
                 c = strtok(0, ",")) withmods[nwith++] = c;
        } else if (!strcmp(argv[i], "-cc") && i + 1 < argc)
            ccname = argv[++i];
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) {
            margs = argv[++i];
            if (sscanf(margs, "%d,%d", &lmar, &rmar) != 2) usage();
        } else if (argv[i][0] == '-' && argv[i][1]) usage();
        else if (is_pli(argv[i])) {
            if (npf >= MAXSRC - 1) usage();
            pf[npf++] = defext(argv[i]);
        } else {
            if (noth >= MAXSRC - 1) usage();
            oth[noth++] = argv[i];
        }
    }
    if (!npf) usage();
    if (build || objonly) {
        /* the C files are written next to the sources */
    }
    if (modmode && npf > 1) {
        /* one translation per source file, in separate processes */
        char *self = self_path(argv[0]);
        char *others, *cmd;
        int k, j;
        for (k = 0; k < npf; k++) {
            others = xdup("");
            for (j = 0; j < npf; j++)
                if (j != k)
                    others = cat3(others, *others ? "," : "",
                                  modname(pf[j]));
            cmd = cat5("\"", self, "\" -M -with \"", others, "\"");
            if (!*others) cmd = cat3("\"", self, "\" -M");
            if (opt_subs) cmd = cat2(cmd, " -s");
            if (margs) cmd = cat3(cmd, " -m ", margs);
            cmd = cat4(cmd, " \"", pf[k], "\"");
#ifdef _WIN32
            cmd = cat3("\"", cmd, "\"");
#endif
            if (system(cmd) != 0) return 8;
            cf[ncf++] = outname(pf[k]);
            if (has_text(cf[ncf - 1], "PLI_DEFG")) hasmain = 1;
        }
    } else {
        char *oc;
        if (modmode && !*modbase) modbase = modname(pf[0]);
        oc = (build || objonly || !out) ? outname(pf[0]) : out;
        hasmain = translate(pf, npf, oc,
                            !build && !objonly && out &&
                            !strcmp(out, "-"));
        cf[ncf++] = oc;
    }
    if (!build && !objonly) return 0;
    for (i = 0; i < noth; i++) cf[ncf++] = oth[i];
    if (build && modmode && !hasmain) {
        /* no PL/I main program: add the run-time state */
        FILE *f = fopen("plrt_.c", "w");
        if (!f) die("cannot write plrt_.c");
        fputs("#define PLI_MODULE\n#define PLI_DEFG\n", f);
        fputs("#include \"plirt.h\"\n", f);
        fputs("void plstart(int c, char **v)\n", f);
        fputs("{ pl_start(c, v); }\n", f);
        fputs("void plstop(int rc) { pl_stop(rc); }\n", f);
        fclose(f);
        cf[ncf++] = "plrt_.c";
    }
    rc = build_exe(cf, ncf, out ? out : exename(pf[0]), ccname,
                   argv[0], objonly ? 2 : 1);
    if (build && modmode && !hasmain) remove("plrt_.c");
    return rc;
}
