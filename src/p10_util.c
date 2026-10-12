/* p10_util.c -- memory, errors, string builder */

static char *progname = "pli";

static void die(const char *m)
{
    fprintf(stderr, "%s: %s\n", progname, m);
    exit(12);
}

#define MAXSRC 32
#define LINEMUL 1000000
static const char *srcfiles[MAXSRC];
static int nsrc = 0;

static void perr(int line, const char *m, const char *a)
{
    const char *f = 0;
    if (line >= LINEMUL && line / LINEMUL <= nsrc) {
        f = srcfiles[line / LINEMUL - 1];
        line %= LINEMUL;
    }
    if (f)
        fprintf(stderr, "%s: %s: line %d: %s%s%s\n", progname, f,
                line, m, a ? " " : "", a ? a : "");
    else
        fprintf(stderr, "%s: line %d: %s%s%s\n", progname, line, m,
                a ? " " : "", a ? a : "");
    exit(8);
}

static void *xalloc(long n)
{
    void *p = calloc(1, (size_t)(n > 0 ? n : 1));
    if (!p) die("out of memory");
    return p;
}

static char *xdup(const char *s)
{
    char *p = (char *)xalloc((long)strlen(s) + 1);
    strcpy(p, s);
    return p;
}

static void sb_grow(Sb *b, long k)
{
    char *p;
    if (b->n + k + 1 <= b->cap) return;
    b->cap = (b->cap + k + 1) * 2;
    p = (char *)xalloc(b->cap);
    if (b->n) memcpy(p, b->s, (size_t)b->n);
    b->s = p;
}

static void sb_c(Sb *b, int c)
{
    sb_grow(b, 1);
    b->s[b->n++] = (char)c;
    b->s[b->n] = 0;
}

static void sb_s(Sb *b, const char *s)
{
    long k = (long)strlen(s);
    sb_grow(b, k);
    memcpy(b->s + b->n, s, (size_t)k);
    b->n += k;
    b->s[b->n] = 0;
}

static char *sb_str(Sb *b)
{
    return b->s ? b->s : "";
}

static char *cat2(const char *a, const char *b)
{
    char *p = (char *)xalloc((long)(strlen(a) + strlen(b)) + 1);
    strcpy(p, a);
    strcat(p, b);
    return p;
}

static char *cat3(const char *a, const char *b, const char *c)
{
    char *t = cat2(a, b);
    return cat2(t, c);
}

static char *cat4(const char *a, const char *b, const char *c,
                  const char *d)
{
    char *t = cat3(a, b, c);
    return cat2(t, d);
}

static char *cat5(const char *a, const char *b, const char *c,
                  const char *d, const char *e)
{
    char *t = cat4(a, b, c, d);
    return cat2(t, e);
}

static char *itoa_(long v)
{
    char b[40];
    sprintf(b, "%ld", v);
    return xdup(b);
}

/* C text of a double constant (always contains '.' or 'e') */
static char *dtoa_(double v)
{
    char b[64];
    if (v == (double)(long)v && v > -1e9 && v < 1e9)
        sprintf(b, "%ld.0", (long)v);
    else
        sprintf(b, "%.17g", v);
    if (!strchr(b, '.') && !strchr(b, 'e') && !strchr(b, 'E'))
        strcat(b, ".0");
    return xdup(b);
}

static Node *newnode(int k, int line)
{
    Node *n = (Node *)xalloc((long)sizeof(Node));
    n->k = k;
    n->line = line;
    return n;
}

static void tv_add(TV *v, Tok t)
{
    if (v->n >= v->cap) {
        Tok *p;
        v->cap = v->cap ? v->cap * 2 : 1024;
        p = (Tok *)xalloc(v->cap * (long)sizeof(Tok));
        if (v->n) memcpy(p, v->a, (size_t)v->n * sizeof(Tok));
        v->a = p;
    }
    v->a[v->n++] = t;
}
