/* p70_main.c -- function assembly, output, command line */

static char *rettype(Sym *p)
{
    switch (p->ty) {
    case TY_FXB: case TY_FXD: case TY_FLT: return "double";
    case TY_CHR: case TY_BIT: return "PS";
    case TY_PTR: return "void *";
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
    if (pm->ty == TY_FILE) perr(0, "FILE parameters unsupported", 0);
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
        if (pm->star) {
            pm->lbv = mkcn("lb", pm->id, pm->name);
            pm->hbv = mkcn("hb", pm->id, pm->name);
        }
        t[0] = 0;
        if (pm->star)
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
    if (is_str(pm)) {
        pm->cn = nm;
        pm->ispv = 1;
        return cat2("PV ", nm);
    }
    pm->pcn = nm;
    pm->cn = mkcn("v", pm->id, pm->name);
    if (f->rec)
        sb_s(&f->decl, cat5("    ", is_long(pm) ? "long " :
             (pm->ty == TY_PTR ? "void *" : "double "), pm->cn, ";",
             "\n"));
    else
        sb_s(&gdecl, cat5("static ", is_long(pm) ? "long " :
             (pm->ty == TY_PTR ? "void *" : "double "), pm->cn, ";",
             "\n"));
    return cat3(is_long(pm) ? "long *" : (pm->ty == TY_PTR ?
                "void **" : "double *"), "", nm);
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
        sb_s(&hdr, cat4("static ", rettype(ps), " ", f->name));
        sb_c(&hdr, '(');
        for (pm = ps->params; pm; pm = pm->params) {
            if (!first) sb_s(&hdr, ", ");
            first = 0;
            sb_s(&hdr, pm->pdecl);
        }
        if (first) sb_s(&hdr, "void");
        sb_c(&hdr, ')');
        sb_s(&gproto, cat2(sb_str(&hdr), ";\n"));
        for (pm = ps->params; pm; pm = pm->params)
            if (pm->pcn)
                sb_s(&pro, cat5("    ", pm->cn, " = *", pm->pcn,
                                ";\n"));
        {
            Sb save = f->body;
            memset(&f->body, 0, sizeof f->body);
            scope_inits(csc);
            subs_chk = opt_subs || (n->pre & PRE_SUBS);
            gsl(n->b);
            sb_s(&pro, sb_str(&f->body));
            f->body = save;
        }
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
    sl = strrchr(o, '/');
    dot = strrchr(o, '.');
    if (dot && (!sl || dot > sl)) *dot = 0;
    return cat2(o, ".c");
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

static int build_exe(const char *cfile, const char *exe,
                     const char *want, const char *argv0)
{
    char *dir = self_dir(argv0);
    char *cc;
    char *cmd;
    char *inc;
    int kind, rc;
    cc = pick_cc(want, dir, &kind);
    if (!cc) {
        fprintf(stderr, "pli: no C compiler found; install gcc, "
                "clang or tcc, or use -cc name\n");
        return 8;
    }
    inc = cat5("-I\"", dir, "\" -I\"", dir, "/src\" -I\"");
    inc = cat5(inc, dir, "/../share/pli\" -I/usr/share/pli ",
               "-I/usr/local/share/pli", "");
    if (kind == 2)
        cmd = cat5(cc, " /nologo /I\"", dir, "\" /Fe\"", exe);
    else
        cmd = cat5(cc, " ", inc, " -o \"", exe);
    cmd = cat4(cmd, "\" \"", cfile, kind == 2 ? "\"" : "\"" LIBM);
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
        "usage: pli [-c] [-o out] [-m l,r] [-s] [-cc cc] src ...\n");
    fprintf(stderr, "  -o f    output file (- for stdout)\n");
    fprintf(stderr, "  -c      also build the program (needs a C\n");
    fprintf(stderr, "          compiler; -o names the exe)\n");
    fprintf(stderr, "  -cc c   compiler for -c: a command, or\n");
    fprintf(stderr, "          'bundled' for TinyCC next to pli\n");
    fprintf(stderr, "  -m l,r  source margins, e.g. -m 2,72\n");
    fprintf(stderr, "  -s      check subscripts at run time\n");
    exit(4);
}

int main(int argc, char **argv)
{
    char *in = 0, *out = 0, *ccname = 0;
    char *more[MAXSRC];
    int nmore = 0;
    int build = 0;
    int i, a;
    Node *prog, *s, *mainp = 0;
    FILE *o;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "-s")) opt_subs = 1;
        else if (!strcmp(argv[i], "-c")) build = 1;
        else if (!strcmp(argv[i], "-cc") && i + 1 < argc)
            ccname = argv[++i];
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) {
            if (sscanf(argv[++i], "%d,%d", &lmar, &rmar) != 2)
                usage();
        } else if (argv[i][0] == '-' && argv[i][1]) usage();
        else if (!in) in = argv[i];
        else if (nmore < MAXSRC - 1) more[nmore++] = argv[i];
        else usage();
    }
    if (!in) usage();
    lexfile(in);
    for (i = 0; i < nmore; i++) lexfile(more[i]);
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
    if (!mainp) perr(1, "no main procedure", 0);
    for (a = 0; a < nfn; a++)
        if (!fnall[a]->ison) {
            Sym *pm;
            for (pm = fnall[a]->ps->params; pm; pm = pm->params)
                pm->pdecl = param_decl(pm, fnall[a]);
        }
    for (a = 0; a < nfn; a++) gen_fn(fnall[a]);
    for (a = 0; a < nfn; a++) asm_fn(fnall[a]);
    gen_init_fn();
    o = (!build && out && !strcmp(out, "-")) ? stdout :
        fopen(build || !out ? outname(in) : out, "w");
    if (!o) die("cannot open output file");
    fprintf(o, "/* generated by pli %s from %s */\n", VERSION, in);
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
    fprintf(o, "int main(int argc, char **argv)\n{\n");
    fprintf(o, "    pl_start(argc, argv);\n    pl_init();\n");
    fprintf(o, "    %s();\n    pl_stop(0);\n    return 0;\n}\n",
            mainp->sym->cn);
    if (o != stdout) fclose(o);
    if (build)
        return build_exe(outname(in), out ? out : exename(in),
                         ccname, argv[0]);
    return 0;
}
