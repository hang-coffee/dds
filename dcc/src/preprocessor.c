#include "preprocessor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#define PP_MAX_INCLUDE_DEPTH 64
#define PP_MAX_EXPAND_DEPTH 64

typedef struct Macro {
    char *name;
    int is_func;
    int is_variadic;
    char **params;
    int nparams;
    char *body;
    int expanding;
} Macro;

typedef struct CondFrame {
    int parent_active;
    int active;
    int taken;
} CondFrame;

typedef struct {
    PreprocessOptions *opt;
    Macro *macros;
    int nmacros, capmacros;
    CondFrame *conds;
    int nconds, capconds;
    int include_depth;
    char **headers;
    int nheaders, capheaders;
    char err[1024];
    int err_set;
} PP;

typedef struct {
    char *data;
    size_t len, cap;
} Buf;

static void buf_init(Buf *b) { memset(b, 0, sizeof(*b)); }
static void buf_free(Buf *b) { free(b->data); memset(b, 0, sizeof(*b)); }

static void buf_putc(Buf *b, char c) {
    if (b->len + 2 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 256;
        b->data = (char *)realloc(b->data, ncap);
        b->cap = ncap;
    }
    b->data[b->len++] = c;
    b->data[b->len] = 0;
}

static void buf_puts(Buf *b, const char *s) {
    if (!s) return;
    while (*s) buf_putc(b, *s++);
}

static char *xstrdup(const char *s);

static void pp_add_header(PP *pp, const char *path) {
    for (int i = 0; i < pp->nheaders; i++)
        if (strcmp(pp->headers[i], path) == 0) return;
    if (pp->nheaders >= pp->capheaders) {
        pp->capheaders = pp->capheaders ? pp->capheaders * 2 : 16;
        pp->headers = (char **)realloc(pp->headers, (size_t)pp->capheaders * sizeof(char *));
    }
    pp->headers[pp->nheaders++] = xstrdup(path);
}

static void set_err(PP *pp, const char *fmt, ...) {
    if (pp->err_set) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(pp->err, sizeof(pp->err), fmt, ap);
    va_end(ap);
    pp->err_set = 1;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static char *xstrndup(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (p) { memcpy(p, s, n); p[n] = 0; }
    return p;
}

static int is_ident_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_ident_part(char c) { return isalnum((unsigned char)c) || c == '_'; }

static int path_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static char *path_join(const char *dir, const char *name) {
    size_t dl = strlen(dir), nl = strlen(name);
    int need_slash = dl > 0 && dir[dl - 1] != '/' && dir[dl - 1] != '\\';
    char *p = (char *)malloc(dl + (need_slash ? 1 : 0) + nl + 1);
    if (!p) return NULL;
    memcpy(p, dir, dl);
    size_t o = dl;
    if (need_slash) p[o++] = '/';
    memcpy(p + o, name, nl);
    p[o + nl] = 0;
    return p;
}

static char *dir_name_of(const char *path) {
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bs = strrchr(path, '\\');
    if (!slash || (bs && bs > slash)) slash = bs;
#endif
    if (!slash) return xstrdup(".");
    size_t n = (size_t)(slash - path);
    if (n == 0) return xstrdup("/");
    return xstrndup(path, n);
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    if (out_len) *out_len = rd;
    return buf;
}

static Macro *find_macro(PP *pp, const char *name) {
    for (int i = 0; i < pp->nmacros; i++)
        if (strcmp(pp->macros[i].name, name) == 0) return &pp->macros[i];
    return NULL;
}

static int macro_defined(PP *pp, const char *name) {
    return find_macro(pp, name) != NULL;
}

static void add_macro(PP *pp, const char *name, int is_func, int is_variadic, char **params, int nparams, const char *body) {
    Macro *m = find_macro(pp, name);
    if (!m) {
        if (pp->nmacros >= pp->capmacros) {
            pp->capmacros = pp->capmacros ? pp->capmacros * 2 : 32;
            pp->macros = (Macro *)realloc(pp->macros, (size_t)pp->capmacros * sizeof(Macro));
        }
        m = &pp->macros[pp->nmacros++];
        memset(m, 0, sizeof(*m));
        m->name = xstrdup(name);
    } else {
        for (int i = 0; i < m->nparams; i++) free(m->params[i]);
        free(m->params);
        free(m->body);
        memset(m, 0, sizeof(*m));
        m->name = xstrdup(name);
    }
    m->is_func = is_func;
    m->is_variadic = is_variadic;
    m->nparams = nparams;
    m->params = params;
    m->body = xstrdup(body ? body : "");
}

static void undef_macro(PP *pp, const char *name) {
    for (int i = 0; i < pp->nmacros; i++) {
        if (strcmp(pp->macros[i].name, name) == 0) {
            Macro *m = &pp->macros[i];
            free(m->name);
            for (int j = 0; j < m->nparams; j++) free(m->params[j]);
            free(m->params);
            free(m->body);
            pp->macros[i] = pp->macros[pp->nmacros - 1];
            pp->nmacros--;
            return;
        }
    }
}

/* ---------------- 宏展开 ---------------- */

static void expand_text(PP *pp, const char *s, Buf *out, int depth);

static char *trim_left(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static char *trim_right(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    return s;
}

static char *expand_macro_body(Macro *m, char **args, int nargs) {
    Buf b;
    buf_init(&b);
    const char *p = m->body;
    while (*p) {
        if (is_ident_start(*p)) {
            const char *start = p;
            while (is_ident_part(*p)) p++;
            size_t len = (size_t)(p - start);
            int replaced = 0;
            /* __VA_ARGS__：拼接所有可变参数 */
            if (m->is_variadic && len == 11 && strncmp(start, "__VA_ARGS__", 11) == 0) {
                for (int i = m->nparams; i < nargs; i++) {
                    if (i > m->nparams) buf_puts(&b, ", ");
                    buf_puts(&b, args[i]);
                }
                replaced = 1;
            }
            for (int i = 0; !replaced && i < m->nparams; i++) {
                if (strlen(m->params[i]) == len && strncmp(m->params[i], start, len) == 0) {
                    buf_puts(&b, args[i]);
                    replaced = 1;
                    break;
                }
            }
            if (!replaced) {
                char *tmp = xstrndup(start, len);
                buf_puts(&b, tmp);
                free(tmp);
            }
        } else {
            buf_putc(&b, *p);
            p++;
        }
    }
    if (!b.data) b.data = xstrdup("");
    return b.data;
}

static char **split_args(const char *s, int *out_n) {
    /* s 指向 '(' 之后的第一个字符；返回参数数组（不含括号） */
    int n = 0, cap = 0;
    char **args = NULL;
    const char *p = s;
    int depth = 0;
    const char *arg_start = p;
    while (*p) {
        char c = *p;
        if (c == '(') depth++;
        else if (c == ')') {
            if (depth == 0) break;
            depth--;
        } else if (c == ',' && depth == 0) {
            char *raw = xstrndup(arg_start, (size_t)(p - arg_start));
            char *t = trim_left(raw);
            if (t != raw) memmove(raw, t, strlen(t) + 1);
            trim_right(raw);
            if (n >= cap) {
                cap = cap ? cap * 2 : 8;
                args = (char **)realloc(args, (size_t)cap * sizeof(char *));
            }
            args[n++] = raw;
            arg_start = p + 1;
        }
        p++;
    }
    if (p > arg_start || n > 0) {
        char *raw = xstrndup(arg_start, (size_t)(p - arg_start));
        char *t = trim_left(raw);
        if (t != raw) memmove(raw, t, strlen(t) + 1);
        trim_right(raw);
        if (n >= cap) {
            cap = cap ? cap * 2 : 8;
            args = (char **)realloc(args, (size_t)cap * sizeof(char *));
        }
        args[n++] = raw;
    }
    *out_n = n;
    return args;
}

static const char *skip_spaces(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static void expand_text(PP *pp, const char *s, Buf *out, int depth) {
    const char *p = s;
    while (*p) {
        if (*p == '"' || *p == '\'') {
            char q = *p;
            buf_putc(out, *p++);
            while (*p && *p != q) {
                if (*p == '\\' && p[1]) buf_putc(out, *p++);
                buf_putc(out, *p++);
            }
            if (*p == q) buf_putc(out, *p++);
            continue;
        }
        if (p[0] == '/' && p[1] == '/') {
            buf_puts(out, p);
            return;
        }
        if (p[0] == '/' && p[1] == '*') {
            buf_puts(out, "/*");
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                buf_putc(out, *p++);
            }
            if (*p) { buf_puts(out, "*/"); p += 2; }
            continue;
        }
        if (is_ident_start(*p)) {
            const char *start = p;
            while (is_ident_part(*p)) p++;
            size_t len = (size_t)(p - start);
            char *name = xstrndup(start, len);
            Macro *m = find_macro(pp, name);
            if (!m) {
                buf_puts(out, name);
                free(name);
                continue;
            }
            if (m->is_func) {
                const char *q = skip_spaces(p);
                if (*q == '(') {
                    if (m->expanding || depth >= PP_MAX_EXPAND_DEPTH) {
                        buf_puts(out, name);
                        buf_putc(out, '(');
                        p = q + 1;
                        /* 原样复制参数 */
                        int pd = 0;
                        while (*p && !(*p == ')' && pd == 0)) {
                            if (*p == '(') pd++;
                            else if (*p == ')') pd--;
                            buf_putc(out, *p++);
                        }
                        if (*p == ')') { buf_putc(out, *p); p++; }
                        free(name);
                        continue;
                    }
                    int nargs;
                    char **args = split_args(q + 1, &nargs);
                    /* 跳过匹配的 ')' */
                    int pd = 0;
                    const char *r = q + 1;
                    while (*r && !(*r == ')' && pd == 0)) {
                        if (*r == '(') pd++;
                        else if (*r == ')') pd--;
                        r++;
                    }
                    if (*r == ')') r++;
                    if (m->is_variadic) {
                        if (nargs < m->nparams) {
                            set_err(pp, "宏 %s 参数个数不足", name);
                            free(name);
                            for (int i = 0; i < nargs; i++) free(args[i]);
                            free(args);
                            return;
                        }
                    } else if (nargs != m->nparams) {
                        set_err(pp, "宏 %s 参数个数不匹配", name);
                        free(name);
                        for (int i = 0; i < nargs; i++) free(args[i]);
                        free(args);
                        return;
                    }
                    m->expanding = 1;
                    char *subst = expand_macro_body(m, args, nargs);
                    m->expanding = 0;
                    Buf tmp;
                    buf_init(&tmp);
                    expand_text(pp, subst, &tmp, depth + 1);
                    buf_puts(out, tmp.data);
                    buf_free(&tmp);
                    free(subst);
                    for (int i = 0; i < nargs; i++) free(args[i]);
                    free(args);
                    p = r;
                    free(name);
                    continue;
                } else {
                    buf_puts(out, name);
                    free(name);
                    continue;
                }
            }
            /* 对象宏 */
            if (m->expanding || depth >= PP_MAX_EXPAND_DEPTH) {
                buf_puts(out, name);
            } else {
                m->expanding = 1;
                char *subst = xstrdup(m->body);
                Buf tmp;
                buf_init(&tmp);
                expand_text(pp, subst, &tmp, depth + 1);
                buf_puts(out, tmp.data);
                buf_free(&tmp);
                free(subst);
                m->expanding = 0;
            }
            free(name);
            continue;
        }
        buf_putc(out, *p++);
    }
}

/* ---------------- 条件编译 ---------------- */

static void expand_if_text(PP *pp, const char *s, Buf *out, int depth) {
    const char *p = s;
    while (*p) {
        if (*p == '"' || *p == '\'') {
            char q = *p;
            buf_putc(out, *p++);
            while (*p && *p != q) {
                if (*p == '\\' && p[1]) buf_putc(out, *p++);
                buf_putc(out, *p++);
            }
            if (*p == q) buf_putc(out, *p++);
            continue;
        }
        if (p[0] == '/' && p[1] == '/') {
            buf_puts(out, p);
            return;
        }
        if (p[0] == '/' && p[1] == '*') {
            buf_puts(out, "/*");
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                buf_putc(out, *p++);
            }
            if (*p) { buf_puts(out, "*/"); p += 2; }
            continue;
        }
        if (is_ident_start(*p)) {
            const char *start = p;
            while (is_ident_part(*p)) p++;
            size_t len = (size_t)(p - start);
            char *name = xstrndup(start, len);
            if (strcmp(name, "defined") == 0) {
                buf_puts(out, "defined");
                const char *q = skip_spaces(p);
                if (*q == '(') {
                    buf_putc(out, '(');
                    q++;
                    int pd = 1;
                    while (*q && pd > 0) {
                        if (*q == '(') pd++;
                        else if (*q == ')') pd--;
                        if (*q && !(*q == ')' && pd == 0)) buf_putc(out, *q);
                        q++;
                    }
                    if (*q == ')') buf_putc(out, *q++);
                    p = q;
                } else if (is_ident_start(*q)) {
                    while (is_ident_part(*q)) buf_putc(out, *q++);
                    p = q;
                }
                free(name);
                continue;
            }
            Macro *m = find_macro(pp, name);
            if (m && !m->expanding && depth < PP_MAX_EXPAND_DEPTH) {
                m->expanding = 1;
                expand_if_text(pp, m->body, out, depth + 1);
                m->expanding = 0;
            } else {
                buf_puts(out, name);
            }
            free(name);
            continue;
        }
        buf_putc(out, *p++);
    }
}

static void cond_push(PP *pp, int active, int taken) {
    if (pp->nconds >= pp->capconds) {
        pp->capconds = pp->capconds ? pp->capconds * 2 : 8;
        pp->conds = (CondFrame *)realloc(pp->conds, (size_t)pp->capconds * sizeof(CondFrame));
    }
    int parent = pp->nconds ? pp->conds[pp->nconds - 1].active : 1;
    pp->conds[pp->nconds].parent_active = parent;
    pp->conds[pp->nconds].active = parent && active;
    pp->conds[pp->nconds].taken = parent && taken;
    pp->nconds++;
}

static int cond_active(PP *pp) {
    return pp->nconds == 0 || pp->conds[pp->nconds - 1].active;
}

typedef struct {
    PP *pp;
    const char *p;
    int ok;
} ExprParser;

static void ep_skip(ExprParser *ep) {
    while (*ep->p && isspace((unsigned char)*ep->p)) ep->p++;
}

static long long ep_primary(ExprParser *ep);
static long long ep_unary(ExprParser *ep);
static long long ep_mul(ExprParser *ep);
static long long ep_add(ExprParser *ep);
static long long ep_shift(ExprParser *ep);
static long long ep_rel(ExprParser *ep);
static long long ep_eq(ExprParser *ep);
static long long ep_bitand(ExprParser *ep);
static long long ep_bitxor(ExprParser *ep);
static long long ep_bitor(ExprParser *ep);
static long long ep_logand(ExprParser *ep);
static long long ep_logor(ExprParser *ep);
static long long ep_cond(ExprParser *ep);

static long long ep_primary(ExprParser *ep) {
    ep_skip(ep);
    if (!*ep->p) { ep->ok = 0; return 0; }
    if (*ep->p == '(') {
        ep->p++;
        long long v = ep_cond(ep);
        ep_skip(ep);
        if (*ep->p != ')') ep->ok = 0;
        else ep->p++;
        return v;
    }
    if (isdigit((unsigned char)*ep->p)) {
        char *end = NULL;
        long long v = strtoll(ep->p, &end, 0);
        if (!end || end == ep->p) { ep->ok = 0; return 0; }
        ep->p = end;
        while (*ep->p == 'u' || *ep->p == 'U' || *ep->p == 'l' || *ep->p == 'L') ep->p++;
        return v;
    }
    if (is_ident_start(*ep->p)) {
        const char *start = ep->p;
        while (is_ident_part(*ep->p)) ep->p++;
        size_t len = (size_t)(ep->p - start);
        if (len == 7 && strncmp(start, "defined", 7) == 0) {
            ep_skip(ep);
            if (*ep->p == '(') {
                ep->p++;
                ep_skip(ep);
                const char *ns = ep->p;
                while (is_ident_part(*ep->p)) ep->p++;
                if (ep->p == ns) { ep->ok = 0; return 0; }
                char *nm = xstrndup(ns, (size_t)(ep->p - ns));
                int r = macro_defined(ep->pp, nm);
                free(nm);
                ep_skip(ep);
                if (*ep->p != ')') ep->ok = 0;
                else ep->p++;
                return r;
            } else if (is_ident_start(*ep->p)) {
                const char *ns = ep->p;
                while (is_ident_part(*ep->p)) ep->p++;
                char *nm = xstrndup(ns, (size_t)(ep->p - ns));
                int r = macro_defined(ep->pp, nm);
                free(nm);
                return r;
            }
            ep->ok = 0;
            return 0;
        }
        /* 未定义/未展开的标识符按 0 处理 */
        return 0;
    }
    ep->ok = 0;
    return 0;
}

static long long ep_unary(ExprParser *ep) {
    ep_skip(ep);
    if (*ep->p == '!') { ep->p++; return !ep_unary(ep); }
    if (*ep->p == '~') { ep->p++; return ~ep_unary(ep); }
    if (*ep->p == '-') { ep->p++; return -ep_unary(ep); }
    if (*ep->p == '+') { ep->p++; return +ep_unary(ep); }
    return ep_primary(ep);
}

static long long ep_mul(ExprParser *ep) {
    long long v = ep_unary(ep);
    for (;;) {
        ep_skip(ep);
        if (*ep->p == '*') { ep->p++; v = v * ep_unary(ep); }
        else if (*ep->p == '/') { ep->p++; long long r = ep_unary(ep); if (r == 0) ep->ok = 0; else v = v / r; }
        else if (*ep->p == '%') { ep->p++; long long r = ep_unary(ep); if (r == 0) ep->ok = 0; else v = v % r; }
        else return v;
    }
}

static long long ep_add(ExprParser *ep) {
    long long v = ep_mul(ep);
    for (;;) {
        ep_skip(ep);
        if (*ep->p == '+') { ep->p++; v = v + ep_mul(ep); }
        else if (*ep->p == '-') { ep->p++; v = v - ep_mul(ep); }
        else return v;
    }
}

static long long ep_shift(ExprParser *ep) {
    long long v = ep_add(ep);
    for (;;) {
        ep_skip(ep);
        if (ep->p[0] == '<' && ep->p[1] == '<') { ep->p += 2; v = v << ep_add(ep); }
        else if (ep->p[0] == '>' && ep->p[1] == '>') { ep->p += 2; v = v >> ep_add(ep); }
        else return v;
    }
}

static long long ep_rel(ExprParser *ep) {
    long long v = ep_shift(ep);
    for (;;) {
        ep_skip(ep);
        if (ep->p[0] == '<' && ep->p[1] == '=') { ep->p += 2; v = v <= ep_shift(ep); }
        else if (ep->p[0] == '>' && ep->p[1] == '=') { ep->p += 2; v = v >= ep_shift(ep); }
        else if (*ep->p == '<') { ep->p++; v = v < ep_shift(ep); }
        else if (*ep->p == '>') { ep->p++; v = v > ep_shift(ep); }
        else return v;
    }
}

static long long ep_eq(ExprParser *ep) {
    long long v = ep_rel(ep);
    for (;;) {
        ep_skip(ep);
        if (ep->p[0] == '=' && ep->p[1] == '=') { ep->p += 2; v = v == ep_rel(ep); }
        else if (ep->p[0] == '!' && ep->p[1] == '=') { ep->p += 2; v = v != ep_rel(ep); }
        else return v;
    }
}

static long long ep_bitand(ExprParser *ep) {
    long long v = ep_eq(ep);
    for (;;) {
        ep_skip(ep);
        if (*ep->p == '&' && ep->p[1] != '&') { ep->p++; v = v & ep_eq(ep); }
        else return v;
    }
}

static long long ep_bitxor(ExprParser *ep) {
    long long v = ep_bitand(ep);
    for (;;) {
        ep_skip(ep);
        if (*ep->p == '^') { ep->p++; v = v ^ ep_bitand(ep); }
        else return v;
    }
}

static long long ep_bitor(ExprParser *ep) {
    long long v = ep_bitxor(ep);
    for (;;) {
        ep_skip(ep);
        if (*ep->p == '|' && ep->p[1] != '|') { ep->p++; v = v | ep_bitxor(ep); }
        else return v;
    }
}

static long long ep_logand(ExprParser *ep) {
    long long v = ep_bitor(ep);
    for (;;) {
        ep_skip(ep);
        if (ep->p[0] == '&' && ep->p[1] == '&') { ep->p += 2; v = v && ep_bitor(ep); }
        else return v;
    }
}

static long long ep_logor(ExprParser *ep) {
    long long v = ep_logand(ep);
    for (;;) {
        ep_skip(ep);
        if (ep->p[0] == '|' && ep->p[1] == '|') { ep->p += 2; v = v || ep_logand(ep); }
        else return v;
    }
}

static long long ep_cond(ExprParser *ep) {
    long long v = ep_logor(ep);
    ep_skip(ep);
    if (*ep->p == '?') {
        ep->p++;
        long long a = ep_cond(ep);
        ep_skip(ep);
        if (*ep->p != ':') { ep->ok = 0; return v; }
        ep->p++;
        long long b = ep_cond(ep);
        return v ? a : b;
    }
    return v;
}

static int eval_if_expr(PP *pp, const char *expr) {
    Buf b;
    buf_init(&b);
    expand_if_text(pp, expr, &b, 0);
    ExprParser ep;
    memset(&ep, 0, sizeof(ep));
    ep.pp = pp;
    ep.p = b.data ? b.data : "";
    ep.ok = 1;
    long long v = ep_cond(&ep);
    ep_skip(&ep);
    if (*ep.p != 0) ep.ok = 0;
    buf_free(&b);
    if (!ep.ok) return 1;
    return v != 0;
}

/* ---------------- 指令处理 ---------------- */

static char *find_include(PP *pp, const char *fname, int angled, const char *current_dir) {
    /* 搜索顺序：
       <...>:  lib 目录 -> 当前源文件目录 -> -I 目录
       "...":  当前源文件目录 -> lib 目录 -> -I 目录 */
    const char *first = angled ? pp->opt->lib_include_dir : current_dir;
    const char *second = angled ? current_dir : pp->opt->lib_include_dir;
    const char *order[2];
    order[0] = first;
    order[1] = second;
    for (int i = 0; i < 2; i++) {
        if (!order[i]) continue;
        char *path = path_join(order[i], fname);
        if (path && path_exists(path)) return path;
        free(path);
    }
    for (int i = 0; i < pp->opt->n_include_dirs; i++) {
        char *path = path_join(pp->opt->include_dirs[i], fname);
        if (path && path_exists(path)) return path;
        free(path);
    }
    return NULL;
}

static int process_file(PP *pp, const char *path, Buf *out);

static void handle_directive(PP *pp, const char *line, const char *current_dir, Buf *out) {
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '#') return;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    /* 非条件指令在跳过分支中不执行；条件指令仍需维护条件栈 */
    if (!cond_active(pp)) {
        int is_cond =
            (strncmp(p, "ifdef", 5) == 0 && !is_ident_part(p[5])) ||
            (strncmp(p, "ifndef", 6) == 0 && !is_ident_part(p[6])) ||
            (strncmp(p, "if", 2) == 0 && !is_ident_part(p[2])) ||
            (strncmp(p, "elif", 4) == 0 && !is_ident_part(p[4])) ||
            (strncmp(p, "else", 4) == 0 && !is_ident_part(p[4])) ||
            (strncmp(p, "endif", 5) == 0 && !is_ident_part(p[5]));
        if (!is_cond) return;
    }
    if (strncmp(p, "include", 7) == 0 && !is_ident_part(p[7])) {
        p += 7;
        while (*p && isspace((unsigned char)*p)) p++;
        int angled = 0;
        const char *end = NULL;
        if (*p == '<') { angled = 1; end = strchr(p + 1, '>'); }
        else if (*p == '"') { angled = 0; end = strchr(p + 1, '"'); }
        if (!end) { set_err(pp, "include 语法错误"); return; }
        size_t n = (size_t)(end - p - 1);
        char *fname = xstrndup(p + 1, n);
        char *found = find_include(pp, fname, angled, current_dir);
        if (!found) {
            set_err(pp, "找不到头文件 %s", fname);
            free(fname);
            return;
        }
        free(fname);
        pp_add_header(pp, found);
        if (pp->include_depth >= PP_MAX_INCLUDE_DEPTH) {
            set_err(pp, "头文件包含嵌套过深");
            free(found);
            return;
        }
        pp->include_depth++;
        process_file(pp, found, out);
        pp->include_depth--;
        free(found);
        return;
    }
    if (strncmp(p, "define", 6) == 0 && !is_ident_part(p[6])) {
        p += 6;
        while (*p && isspace((unsigned char)*p)) p++;
        if (!is_ident_start(*p)) { set_err(pp, "define 缺少宏名"); return; }
        const char *name_start = p;
        while (is_ident_part(*p)) p++;
        char *name = xstrndup(name_start, (size_t)(p - name_start));
        int is_func = 0;
        int is_variadic = 0;
        char **params = NULL;
        int nparams = 0;
        if (*p == '(') {
            is_func = 1;
            p++;
            while (*p && *p != ')') {
                while (*p && isspace((unsigned char)*p)) p++;
                if (*p == ')') break;
                /* 可变参数：... */
                if (p[0] == '.' && p[1] == '.' && p[2] == '.') {
                    is_variadic = 1;
                    p += 3;
                    while (*p && isspace((unsigned char)*p)) p++;
                    if (*p != ')') {
                        set_err(pp, "可变参数 ... 必须是宏参数列表的最后一项");
                        free(name);
                        for (int i=0;i<nparams;i++) free(params[i]);
                        free(params);
                        return;
                    }
                    break;
                }
                if (!is_ident_start(*p)) { set_err(pp, "宏参数名非法"); free(name); return; }
                const char *as = p;
                while (is_ident_part(*p)) p++;
                params = (char **)realloc(params, (size_t)(nparams + 1) * sizeof(char *));
                params[nparams++] = xstrndup(as, (size_t)(p - as));
                while (*p && isspace((unsigned char)*p)) p++;
                if (*p == ',') { p++; continue; }
                if (*p != ')') { set_err(pp, "宏参数列表语法错误"); free(name); for (int i=0;i<nparams;i++) free(params[i]); free(params); return; }
            }
            if (*p == ')') p++;
            else { set_err(pp, "宏参数列表缺少 ')'"); free(name); for (int i=0;i<nparams;i++) free(params[i]); free(params); return; }
        }
        while (*p && isspace((unsigned char)*p)) p++;
        char *body = xstrdup(p);
        body = trim_right(body);
        add_macro(pp, name, is_func, is_variadic, params, nparams, body);
        free(name);
        free(body);
        return;
    }
    if (strncmp(p, "undef", 5) == 0 && !is_ident_part(p[5])) {
        p += 5;
        while (*p && isspace((unsigned char)*p)) p++;
        const char *ns = p;
        while (is_ident_part(*p)) p++;
        if (p == ns) { set_err(pp, "undef 缺少宏名"); return; }
        char *name = xstrndup(ns, (size_t)(p - ns));
        undef_macro(pp, name);
        free(name);
        return;
    }
    if (strncmp(p, "error", 5) == 0 && !is_ident_part(p[5])) {
        p += 5;
        while (*p && isspace((unsigned char)*p)) p++;
        set_err(pp, "#error %s", p);
        return;
    }
    if (strncmp(p, "ifdef", 5) == 0 && !is_ident_part(p[5])) {
        p += 5;
        while (*p && isspace((unsigned char)*p)) p++;
        const char *ns = p;
        while (is_ident_part(*p)) p++;
        char *name = xstrndup(ns, (size_t)(p - ns));
        int d = macro_defined(pp, name);
        cond_push(pp, d, d);
        free(name);
        return;
    }
    if (strncmp(p, "ifndef", 6) == 0 && !is_ident_part(p[6])) {
        p += 6;
        while (*p && isspace((unsigned char)*p)) p++;
        const char *ns = p;
        while (is_ident_part(*p)) p++;
        char *name = xstrndup(ns, (size_t)(p - ns));
        int d = !macro_defined(pp, name);
        cond_push(pp, d, d);
        free(name);
        return;
    }
    if (strncmp(p, "if", 2) == 0 && !is_ident_part(p[2])) {
        p += 2;
        while (*p && isspace((unsigned char)*p)) p++;
        int v = eval_if_expr(pp, p);
        cond_push(pp, v, v);
        return;
    }
    if (strncmp(p, "elif", 4) == 0 && !is_ident_part(p[4])) {
        if (pp->nconds == 0) { set_err(pp, "#elif 没有对应的 #if"); return; }
        CondFrame *c = &pp->conds[pp->nconds - 1];
        if (!c->taken && c->parent_active) {
            p += 4;
            while (*p && isspace((unsigned char)*p)) p++;
            int v = eval_if_expr(pp, p);
            c->active = v;
            if (v) c->taken = 1;
        } else {
            c->active = 0;
        }
        return;
    }
    if (strncmp(p, "else", 4) == 0 && !is_ident_part(p[4])) {
        if (pp->nconds == 0) { set_err(pp, "#else 没有对应的 #if"); return; }
        CondFrame *c = &pp->conds[pp->nconds - 1];
        if (c->parent_active && !c->taken) {
            c->active = 1;
            c->taken = 1;
        } else {
            c->active = 0;
        }
        return;
    }
    if (strncmp(p, "endif", 5) == 0 && !is_ident_part(p[5])) {
        if (pp->nconds == 0) { set_err(pp, "#endif 没有对应的 #if"); return; }
        pp->nconds--;
        return;
    }
    /* 未知指令（#pragma 等）忽略 */
}

static int process_file(PP *pp, const char *path, Buf *out) {
    size_t len = 0;
    char *src = read_file(path, &len);
    if (!src) {
        set_err(pp, "无法读取文件 %s", path);
        return 0;
    }
    char *cur_dir = dir_name_of(path);
    char *p = src;
    int line_no = 1;
    while (*p) {
        char *line_start = p;
        while (*p && *p != '\n') p++;
        char *line_end = p;
        /* 去掉行尾 \r */
        if (line_end > line_start && line_end[-1] == '\r') line_end--;
        size_t line_len = (size_t)(line_end - line_start);
        char *line = xstrndup(line_start, line_len);
        char *t = line;
        while (*t && isspace((unsigned char)*t)) t++;
        if (*t == '#') {
            handle_directive(pp, line, cur_dir, out);
        } else if (cond_active(pp)) {
            Buf tmp;
            buf_init(&tmp);
            expand_text(pp, line, &tmp, 0);
            buf_puts(out, tmp.data);
            buf_free(&tmp);
            buf_putc(out, '\n');
        } else {
            buf_putc(out, '\n');
        }
        free(line);
        if (*p == '\n') { p++; line_no++; }
    }
    free(cur_dir);
    free(src);
    if (pp->err_set) return 0;
    return 1;
}

/* ---------------- 对外接口 ---------------- */

void preprocess_options_init(PreprocessOptions *opt) {
    memset(opt, 0, sizeof(*opt));
    opt->hosted = 1;
}

void preprocess_options_add_include_dir(PreprocessOptions *opt, const char *dir) {
    if (opt->n_include_dirs >= opt->cap_include_dirs) {
        opt->cap_include_dirs = opt->cap_include_dirs ? opt->cap_include_dirs * 2 : 8;
        opt->include_dirs = (char **)realloc(opt->include_dirs, (size_t)opt->cap_include_dirs * sizeof(char *));
    }
    opt->include_dirs[opt->n_include_dirs++] = xstrdup(dir);
}

void preprocess_options_free(PreprocessOptions *opt) {
    for (int i = 0; i < opt->n_include_dirs; i++) free(opt->include_dirs[i]);
    free(opt->include_dirs);
    free(opt->lib_include_dir);
    memset(opt, 0, sizeof(*opt));
}

static void pp_cleanup(PP *pp) {
    for (int i = 0; i < pp->nmacros; i++) {
        free(pp->macros[i].name);
        for (int j = 0; j < pp->macros[i].nparams; j++) free(pp->macros[i].params[j]);
        free(pp->macros[i].params);
        free(pp->macros[i].body);
    }
    free(pp->macros);
    free(pp->conds);
    for (int i = 0; i < pp->nheaders; i++) free(pp->headers[i]);
    free(pp->headers);
}

PreprocessResult preprocess_file_result(const char *path, const PreprocessOptions *opt, char **err) {
    PreprocessResult r;
    memset(&r, 0, sizeof(r));
    PP pp;
    memset(&pp, 0, sizeof(pp));
    pp.opt = (PreprocessOptions *)opt;
    Buf out;
    buf_init(&out);
    if (!process_file(&pp, path, &out)) {
        if (err) *err = xstrdup(pp.err[0] ? pp.err : "预处理失败");
        pp_cleanup(&pp);
        buf_free(&out);
        return r;
    }
    if (err) *err = NULL;
    if (!out.data) out.data = xstrdup("");
    r.source = out.data;
    r.headers = pp.headers;
    r.nheaders = pp.nheaders;
    pp.headers = NULL;
    pp.nheaders = 0;
    pp_cleanup(&pp);
    return r;
}

void preprocess_result_free(PreprocessResult *r) {
    free(r->source);
    for (int i = 0; i < r->nheaders; i++) free(r->headers[i]);
    free(r->headers);
    memset(r, 0, sizeof(*r));
}

char *preprocess_file(const char *path, const PreprocessOptions *opt, char **err) {
    PreprocessResult r = preprocess_file_result(path, opt, err);
    char *src = r.source;
    r.source = NULL;
    preprocess_result_free(&r);
    return src;
}
