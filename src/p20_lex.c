/* p20_lex.c -- tokenizer and the %-statement preprocessor */

static int lmar = 0, rmar = 0;
static char *curfile = "";


static char *rdsrc(const char *name)
{
    FILE *f = fopen(name, "r");
    char line[2048];
    Sb b;
    long n, i;
    if (!f) return 0;
    memset(&b, 0, sizeof b);
    while (fgets(line, sizeof line, f)) {
        n = (long)strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            n--;
        line[n] = 0;
        if (lmar > 0 || rmar > 0) {
            long l = lmar > 0 ? lmar - 1 : 0;
            long r = rmar > 0 && rmar < n ? rmar : n;
            if (l > r) l = r;
            for (i = l; i < r; i++) sb_c(&b, line[i]);
        } else {
            sb_s(&b, line);
        }
        sb_c(&b, '\n');
    }
    fclose(f);
    return sb_str(&b);
}

static int idch(int c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '$' ||
           c == '@' || c == '#';
}

static void addtok(TV *v, int k, const char *s, int line)
{
    Tok t;
    memset(&t, 0, sizeof t);
    t.k = k;
    t.s = xdup(s);
    t.line = line;
    t.file = curfile;
    tv_add(v, t);
}

static void lexnum(TV *v, const char **pp, int line)
{
    const char *p = *pp, *st = p;
    double val;
    int fl = 0, q = 0, dig = 0;
    char b[80], *e;
    long n;
    Tok t;
    while (isdigit((unsigned char)*p)) { p++; dig++; }
    if (*p == '.' && isdigit((unsigned char)p[1])) {
        p++;
        while (isdigit((unsigned char)*p)) { p++; q++; dig++; }
    } else if (*p == '.' && !idch(p[1]) && p[1] != '.' &&
               p[1] != '(' && dig > 0) {
        p++;
    }
    if ((*p == 'e' || *p == 'E') &&
        (isdigit((unsigned char)p[1]) ||
         ((p[1] == '+' || p[1] == '-') &&
          isdigit((unsigned char)p[2])))) {
        p += 2;
        while (isdigit((unsigned char)*p)) p++;
        fl = 1;
    }
    n = (long)(p - st);
    if (n > 70) n = 70;
    memcpy(b, st, (size_t)n);
    b[n] = 0;
    /* binary constant such as 101B */
    if ((*p == 'b' || *p == 'B') && !idch(p[1]) && !fl && !q &&
        strspn(b, "01") == (size_t)n) {
        val = 0;
        for (e = b; *e; e++) val = val * 2 + (*e - '0');
        p++;
    } else {
        val = strtod(b, &e);
    }
    memset(&t, 0, sizeof t);
    if ((*p == 'i' || *p == 'I') && !idch(p[1])) {
        t.im = 1;
        p++;
    }
    t.k = T_NUM;
    t.s = xdup(b);
    t.dv = val;
    t.fl = fl;
    t.q = fl ? dig : q;
    t.line = line;
    t.file = curfile;
    tv_add(v, t);
    *pp = p;
}

static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower((unsigned char)c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static void lexstr(TV *v, const char **pp, int line)
{
    const char *p = *pp + 1;
    Sb b;
    Tok t;
    int bit = 0, hex = 0;
    memset(&b, 0, sizeof b);
    for (;;) {
        if (!*p || *p == '\n') perr(line, "unterminated string", 0);
        if (*p == '\'') {
            if (p[1] == '\'') { sb_c(&b, '\''); p += 2; continue; }
            p++;
            break;
        }
        sb_c(&b, *p++);
    }
    if ((*p == 'b' || *p == 'B') && !idch(p[1])) { bit = 1; p++; }
    else if ((*p == 'x' || *p == 'X') && !idch(p[1])) {
        char *h = sb_str(&b), *o;
        long i, n = (long)strlen(h);
        Sb c;
        memset(&c, 0, sizeof c);
        for (i = 0; i + 1 < n + 1; i += 2) {
            int x = hexv(h[i]), y = i + 1 < n ? hexv(h[i + 1]) : 0;
            if (x < 0 || y < 0) perr(line, "bad hex string", 0);
            sb_c(&c, x * 16 + y);
        }
        b = c;
        hex = 1;
        o = 0;
        (void)o;
        p++;
    }
    (void)hex;
    memset(&t, 0, sizeof t);
    t.k = T_STR;
    t.s = xdup(sb_str(&b));
    t.fl = bit;
    t.line = line;
    t.file = curfile;
    tv_add(v, t);
    *pp = p;
}

static void lexbuf(TV *v, const char *p)
{
    int line = 1;
    char tmp[4];
    const char *ops2[] = { "||", "**", "->", "<=", ">=", "<>", "!!",
                           "!=", 0 };
    int i, c;
    while (*p) {
        c = (unsigned char)*p;
        if (c == '\n') { line++; p++; continue; }
        if (isspace(c)) { p++; continue; }
        if (c == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\n') line++;
                p++;
            }
            if (!*p) perr(line, "unterminated comment", 0);
            p += 2;
            continue;
        }
        if (isalpha(c) || c == '_' || c == '$' || c == '@' ||
            c == '#') {
            Sb b;
            memset(&b, 0, sizeof b);
            while (idch((unsigned char)*p)) {
                sb_c(&b, tolower((unsigned char)*p));
                p++;
            }
            addtok(v, T_ID, sb_str(&b), line);
            continue;
        }
        if (isdigit(c) || (c == '.' && isdigit((unsigned char)p[1]))) {
            lexnum(v, &p, line);
            continue;
        }
        if (c == '\'') { lexstr(v, &p, line); continue; }
        /* the not sign: UTF-8 (C2 AC) or Latin-1 (AC) */
        if (c == 0xC2 && (unsigned char)p[1] == 0xAC) {
            p += 2;
            if (*p == '=') { p++; addtok(v, T_OP, "!=", line); }
            else addtok(v, T_OP, "!", line);
            continue;
        }
        if (c == 0xAC || c == '^' || c == '\\') {
            p++;
            if (*p == '=') { p++; addtok(v, T_OP, "!=", line); }
            else if (*p == '<') { p++; addtok(v, T_OP, ">=", line); }
            else if (*p == '>') { p++; addtok(v, T_OP, "<=", line); }
            else addtok(v, T_OP, "!", line);
            continue;
        }
        for (i = 0; ops2[i]; i++)
            if (p[0] == ops2[i][0] && p[1] == ops2[i][1]) break;
        if (ops2[i]) {
            const char *o = ops2[i];
            if (!strcmp(o, "<>")) o = "!=";
            if (!strcmp(o, "!!")) o = "||";
            addtok(v, T_OP, o, line);
            p += 2;
            continue;
        }
        tmp[0] = (char)c;
        tmp[1] = 0;
        addtok(v, T_OP, tmp, line);
        p++;
    }
}

