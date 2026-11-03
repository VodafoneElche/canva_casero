// Leer PDF (como los que descarga Canva) y sacar sus piezas: imágenes, formas y textos.
// No es un lector de PDF completo: entiende lo que suelen tener los diseños (objetos
// comprimidos, imágenes JPG/PNG con transparencia, fuentes con tabla ToUnicode...).
#include "pdf.h"
#include "magic.h"

#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "external/nanosvg.h"
#include "external/nanosvgrast.h"

// ------------------------------------------------------------------ Descompresor (deflate)
// Uno propio y con cuidado de no salirse de la memoria, porque los PDF pueden venir rotos.

typedef struct {
    const unsigned char *in; int inLen, inPos;
    unsigned int bitBuf; int bitCnt;
    unsigned char *out; int outLen, outCap;
    bool err;
} Inf;

typedef struct { short count[16]; short symbol[320]; } Huff;

static int inf_bits(Inf *s, int need)
{
    unsigned int val = s->bitBuf;
    while (s->bitCnt < need) {
        if (s->inPos >= s->inLen) { s->err = true; return 0; }
        val |= (unsigned int)s->in[s->inPos++] << s->bitCnt;
        s->bitCnt += 8;
    }
    s->bitBuf = val >> need;
    s->bitCnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static bool inf_put(Inf *s, int c)
{
    if (s->outLen >= s->outCap) {
        if (s->outCap > 400 * 1024 * 1024) { s->err = true; return false; }
        s->outCap = s->outCap * 2 + 4096;
        s->out = (unsigned char *)realloc(s->out, s->outCap);
    }
    s->out[s->outLen++] = (unsigned char)c;
    return true;
}

static int inf_decode(Inf *s, const Huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= inf_bits(s, 1);
        if (s->err) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = true;
    return -1;
}

static void inf_build(Huff *h, const short *length, int n)
{
    short offs[16];
    memset(h->count, 0, sizeof(h->count));
    for (int i = 0; i < n; i++) h->count[length[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h->count[len];
    for (int i = 0; i < n; i++) if (length[i]) h->symbol[offs[length[i]]++] = (short)i;
}

static void inf_codes(Inf *s, const Huff *lc, const Huff *dc)
{
    static const short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    for (;;) {
        int sym = inf_decode(s, lc);
        if (s->err || sym < 0) return;
        if (sym < 256) { if (!inf_put(s, sym)) return; }
        else if (sym == 256) return;
        else {
            sym -= 257;
            if (sym >= 29) { s->err = true; return; }
            int len = lbase[sym] + inf_bits(s, lext[sym]);
            int ds = inf_decode(s, dc);
            if (s->err || ds < 0 || ds >= 30) { s->err = true; return; }
            int dist = dbase[ds] + inf_bits(s, dext[ds]);
            if (s->err || dist > s->outLen) { s->err = true; return; }
            for (int i = 0; i < len; i++) if (!inf_put(s, s->out[s->outLen - dist])) return;
        }
    }
}

static unsigned char *inflate_raw(const unsigned char *in, int inLen, int *outLen)
{
    Inf s = { 0 };
    s.in = in; s.inLen = inLen;
    s.outCap = inLen * 4 + 1024;
    s.out = (unsigned char *)malloc(s.outCap);
    int last;
    do {
        last = inf_bits(&s, 1);
        int type = inf_bits(&s, 2);
        if (s.err) break;
        if (type == 0) {
            s.bitBuf = 0; s.bitCnt = 0;
            if (s.inPos + 4 > s.inLen) { s.err = true; break; }
            int len = s.in[s.inPos] | (s.in[s.inPos + 1] << 8);
            s.inPos += 4;
            if (s.inPos + len > s.inLen) len = s.inLen - s.inPos;
            for (int i = 0; i < len; i++) inf_put(&s, s.in[s.inPos + i]);
            s.inPos += len;
        } else if (type == 1) {
            static Huff lf, df;
            static bool built = false;
            if (!built) {
                short l[288];
                int i = 0;
                for (; i < 144; i++) l[i] = 8;
                for (; i < 256; i++) l[i] = 9;
                for (; i < 280; i++) l[i] = 7;
                for (; i < 288; i++) l[i] = 8;
                inf_build(&lf, l, 288);
                for (i = 0; i < 30; i++) l[i] = 5;
                inf_build(&df, l, 30);
                built = true;
            }
            inf_codes(&s, &lf, &df);
        } else if (type == 2) {
            static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            int nlen = inf_bits(&s, 5) + 257, ndist = inf_bits(&s, 5) + 1, ncode = inf_bits(&s, 4) + 4;
            if (s.err || nlen > 286 || ndist > 30) { s.err = true; break; }
            short lengths[320] = { 0 };
            for (int i = 0; i < ncode; i++) lengths[order[i]] = (short)inf_bits(&s, 3);
            Huff lc, dc;
            inf_build(&lc, lengths, 19);
            int idx = 0;
            while (idx < nlen + ndist && !s.err) {
                int sym = inf_decode(&s, &lc);
                if (sym < 0) break;
                if (sym < 16) lengths[idx++] = (short)sym;
                else {
                    int len = 0, rep;
                    if (sym == 16) { if (idx == 0) { s.err = true; break; } len = lengths[idx - 1]; rep = 3 + inf_bits(&s, 2); }
                    else if (sym == 17) rep = 3 + inf_bits(&s, 3);
                    else rep = 11 + inf_bits(&s, 7);
                    if (idx + rep > nlen + ndist) { s.err = true; break; }
                    while (rep--) lengths[idx++] = (short)len;
                }
            }
            if (s.err) break;
            inf_build(&lc, lengths, nlen);
            inf_build(&dc, lengths + nlen, ndist);
            inf_codes(&s, &lc, &dc);
        } else { s.err = true; break; }
    } while (!last && !s.err);
    // Aunque haya un error al final, devolvemos lo que se ha podido leer
    if (s.outLen == 0) { free(s.out); *outLen = 0; return NULL; }
    *outLen = s.outLen;
    return s.out;
}

// ------------------------------------------------------------------ Objetos

typedef enum { O_NULL, O_BOOL, O_NUM, O_NAME, O_STR, O_ARR, O_DICT, O_REF, O_OP } OType;

typedef struct Obj {
    int type;
    double num;
    char *s;            // nombre, cadena u operador
    int len;
    struct Obj *v;      // elementos de lista / valores del diccionario
    char **k;           // claves del diccionario
    int n;
    int ref;
    // flujo (stream)
    const unsigned char *stream;
    int streamLen;
} Obj;

static Obj NULLOBJ = { O_NULL };

// Toda la memoria de una importación sale de aquí y se libera de golpe
typedef struct Block { struct Block *next; size_t used, cap; } Block;
static Block *g_arena = NULL;

static void *arena(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (!g_arena || g_arena->used + n > g_arena->cap) {
        size_t cap = n > (1 << 20) ? n : (1 << 20);
        Block *b = (Block *)malloc(sizeof(Block) + cap);
        b->next = g_arena;
        b->used = 0;
        b->cap = cap;
        g_arena = b;
    }
    void *p = (char *)(g_arena + 1) + g_arena->used;
    g_arena->used += n;
    memset(p, 0, n);
    return p;
}

static void arena_free(void)
{
    while (g_arena) { Block *n = g_arena->next; free(g_arena); g_arena = n; }
}

// ------------------------------------------------------------------ Lector de símbolos

typedef struct { const unsigned char *p, *end; } Lex;

static bool is_ws(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0; }
static bool is_delim(int c) { return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%'; }

static void skip_ws(Lex *l)
{
    while (l->p < l->end) {
        if (is_ws(*l->p)) l->p++;
        else if (*l->p == '%') { while (l->p < l->end && *l->p != '\n' && *l->p != '\r') l->p++; }
        else break;
    }
}

static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static Obj parse_obj(Lex *l, int depth);

static Obj read_token(Lex *l)
{
    Obj o = { O_NULL };
    skip_ws(l);
    if (l->p >= l->end) return o;
    int c = *l->p;
    if (c == '/') {
        l->p++;
        const unsigned char *s = l->p;
        while (l->p < l->end && !is_ws(*l->p) && !is_delim(*l->p)) l->p++;
        int n = (int)(l->p - s);
        o.type = O_NAME;
        o.s = (char *)arena(n + 1);
        // #xx dentro de los nombres
        int k = 0;
        for (int i = 0; i < n; i++) {
            if (s[i] == '#' && i + 2 < n && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) { o.s[k++] = (char)(hexv(s[i + 1]) * 16 + hexv(s[i + 2])); i += 2; }
            else o.s[k++] = (char)s[i];
        }
        o.len = k;
        return o;
    }
    if (c == '(') {
        l->p++;
        int depth = 1, cap = 64, n = 0;
        char *buf = (char *)malloc(cap);
        while (l->p < l->end) {
            int ch = *l->p++;
            if (ch == '\\' && l->p < l->end) {
                int e = *l->p++;
                switch (e) {
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case '\r': if (l->p < l->end && *l->p == '\n') l->p++; continue;
                case '\n': continue;
                default:
                    if (e >= '0' && e <= '7') {
                        int v = e - '0';
                        for (int i = 0; i < 2 && l->p < l->end && *l->p >= '0' && *l->p <= '7'; i++) v = v * 8 + (*l->p++ - '0');
                        ch = v & 0xFF;
                    } else ch = e;
                }
            } else if (ch == '(') depth++;
            else if (ch == ')') { if (--depth == 0) break; }
            if (n + 1 >= cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
            buf[n++] = (char)ch;
        }
        o.type = O_STR;
        o.s = (char *)arena(n + 1);
        memcpy(o.s, buf, n);
        o.len = n;
        free(buf);
        return o;
    }
    if (c == '<' && l->p + 1 < l->end && l->p[1] == '<') { l->p += 2; o.type = O_OP; o.s = "<<"; return o; }
    if (c == '>' && l->p + 1 < l->end && l->p[1] == '>') { l->p += 2; o.type = O_OP; o.s = ">>"; return o; }
    if (c == '<') {
        l->p++;
        const unsigned char *s = l->p;
        while (l->p < l->end && *l->p != '>') l->p++;
        int n = (int)(l->p - s);
        if (l->p < l->end) l->p++;
        o.type = O_STR;
        o.s = (char *)arena(n / 2 + 2);
        int k = 0, hi = -1;
        for (int i = 0; i < n; i++) {
            int v = hexv(s[i]);
            if (v < 0) continue;
            if (hi < 0) hi = v;
            else { o.s[k++] = (char)(hi * 16 + v); hi = -1; }
        }
        if (hi >= 0) o.s[k++] = (char)(hi * 16);
        o.len = k;
        return o;
    }
    if (c == '[' || c == ']' || c == '{' || c == '}') {
        l->p++;
        o.type = O_OP;
        o.s = c == '[' ? "[" : c == ']' ? "]" : c == '{' ? "{" : "}";
        return o;
    }
    if (c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9')) {
        char tmp[64];
        int n = 0;
        while (l->p < l->end && n < 63 && (*l->p == '+' || *l->p == '-' || *l->p == '.' || isdigit(*l->p))) tmp[n++] = (char)*l->p++;
        tmp[n] = 0;
        o.type = O_NUM;
        o.num = atof(tmp);
        return o;
    }
    // Palabra (operador, true, false, null, R, obj...)
    const unsigned char *s = l->p;
    while (l->p < l->end && !is_ws(*l->p) && !is_delim(*l->p)) l->p++;
    if (l->p == s) { l->p++; return o; }   // carácter raro: lo saltamos
    int n = (int)(l->p - s);
    o.type = O_OP;
    o.s = (char *)arena(n + 1);
    memcpy(o.s, s, n);
    o.len = n;
    if (strcmp(o.s, "true") == 0) { o.type = O_BOOL; o.num = 1; }
    else if (strcmp(o.s, "false") == 0) { o.type = O_BOOL; o.num = 0; }
    else if (strcmp(o.s, "null") == 0) o.type = O_NULL;
    return o;
}

// Lee un valor completo. En listas y diccionarios reconoce "N G R".
static Obj parse_from(Lex *l, Obj first, int depth)
{
    if (depth > 64) return NULLOBJ;
    if (first.type == O_OP && strcmp(first.s, "[") == 0) {
        int cap = 8, n = 0;
        Obj *v = (Obj *)malloc(sizeof(Obj) * cap);
        for (;;) {
            Obj t = read_token(l);
            if (l->p >= l->end && t.type == O_NULL) break;
            if (t.type == O_OP && strcmp(t.s, "]") == 0) break;
            if (t.type == O_OP && strcmp(t.s, "R") == 0 && n >= 2 && v[n - 1].type == O_NUM && v[n - 2].type == O_NUM) {
                Obj r = { O_REF };
                r.ref = (int)v[n - 2].num;
                n -= 2;
                v[n++] = r;
                continue;
            }
            Obj x = parse_from(l, t, depth + 1);
            if (n == cap) { cap *= 2; v = (Obj *)realloc(v, sizeof(Obj) * cap); }
            v[n++] = x;
        }
        Obj o = { O_ARR };
        o.n = n;
        o.v = (Obj *)arena(sizeof(Obj) * (n ? n : 1));
        memcpy(o.v, v, sizeof(Obj) * n);
        free(v);
        return o;
    }
    if (first.type == O_OP && strcmp(first.s, "<<") == 0) {
        int cap = 8, n = 0;
        Obj *v = (Obj *)malloc(sizeof(Obj) * cap);
        char **k = (char **)malloc(sizeof(char *) * cap);
        for (;;) {
            Obj key = read_token(l);
            if (key.type == O_OP && strcmp(key.s, ">>") == 0) break;
            if (l->p >= l->end) break;
            if (key.type != O_NAME) continue;
            Obj val = parse_obj(l, depth + 1);
            // ¿Referencia?
            if (val.type == O_NUM) {
                Lex save = *l;
                Obj g = read_token(l);
                if (g.type == O_NUM) {
                    Obj r = read_token(l);
                    if (r.type == O_OP && strcmp(r.s, "R") == 0) { Obj ref = { O_REF }; ref.ref = (int)val.num; val = ref; }
                    else *l = save;
                } else *l = save;
            }
            if (n == cap) { cap *= 2; v = (Obj *)realloc(v, sizeof(Obj) * cap); k = (char **)realloc(k, sizeof(char *) * cap); }
            k[n] = key.s;
            v[n++] = val;
        }
        Obj o = { O_DICT };
        o.n = n;
        o.v = (Obj *)arena(sizeof(Obj) * (n ? n : 1));
        o.k = (char **)arena(sizeof(char *) * (n ? n : 1));
        memcpy(o.v, v, sizeof(Obj) * n);
        memcpy(o.k, k, sizeof(char *) * n);
        free(v);
        free(k);
        return o;
    }
    return first;
}

static Obj parse_obj(Lex *l, int depth) { return parse_from(l, read_token(l), depth); }

// ------------------------------------------------------------------ Archivo

typedef struct { int offset; int stm; int idx; } XRef;   // offset en el archivo, o dentro de un ObjStm

typedef struct {
    const unsigned char *data;
    int size;
    XRef *xref;
    int maxObj;
    Obj *cache;
    bool *cached;
    bool *busy;
} Pdf;

static Pdf P;

static Obj get(int num);

static Obj resolve(Obj o)
{
    for (int i = 0; i < 8 && o.type == O_REF; i++) o = get(o.ref);
    return o;
}

static Obj dget(Obj d, const char *key)
{
    d = resolve(d);
    if (d.type != O_DICT) return NULLOBJ;
    for (int i = 0; i < d.n; i++) if (strcmp(d.k[i], key) == 0) return resolve(d.v[i]);
    return NULLOBJ;
}

static Obj dget_raw(Obj d, const char *key)
{
    d = resolve(d);
    if (d.type != O_DICT) return NULLOBJ;
    for (int i = 0; i < d.n; i++) if (strcmp(d.k[i], key) == 0) return d.v[i];
    return NULLOBJ;
}

static double dnum(Obj d, const char *key, double def)
{
    Obj o = dget(d, key);
    return o.type == O_NUM ? o.num : def;
}

static bool is_name(Obj o, const char *name) { return o.type == O_NAME && strcmp(o.s, name) == 0; }

static Obj parse_at(const unsigned char *start, const unsigned char *end, bool header)
{
    Lex l = { start, end };
    if (header) {
        read_token(&l); read_token(&l);
        Obj kw = read_token(&l);
        if (kw.type != O_OP || strcmp(kw.s, "obj") != 0) return NULLOBJ;
    }
    Obj o = parse_obj(&l, 0);
    if (o.type == O_NUM) {
        // Puede ser una referencia suelta
        Lex save = l;
        Obj g = read_token(&l), r = read_token(&l);
        if (g.type == O_NUM && r.type == O_OP && strcmp(r.s, "R") == 0) { Obj ref = { O_REF }; ref.ref = (int)o.num; return ref; }
        l = save;
    }
    if (o.type == O_DICT && header) {
        Lex save = l;
        Obj kw = read_token(&l);
        if (kw.type == O_OP && strcmp(kw.s, "stream") == 0) {
            const unsigned char *p = l.p;
            if (p < end && *p == '\r') p++;
            if (p < end && *p == '\n') p++;
            int len = -1;
            Obj lo = dget_raw(o, "Length");
            if (lo.type == O_NUM) len = (int)lo.num;
            else if (lo.type == O_REF) { Obj r = resolve(lo); if (r.type == O_NUM) len = (int)r.num; }
            if (len < 0 || p + len > P.data + P.size || (p + len + 9 <= P.data + P.size && memcmp(p + len, "endstream", 9) != 0 &&
                                                         memcmp(p + len + 1, "endstream", 9) != 0 && memcmp(p + len + 2, "endstream", 9) != 0)) {
                // Longitud mal: buscamos "endstream"
                const unsigned char *q = p;
                len = -1;
                while (q + 9 <= P.data + P.size) { if (memcmp(q, "endstream", 9) == 0) { len = (int)(q - p); break; } q++; }
                if (len < 0) len = 0;
                while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r')) len--;
            }
            o.stream = p;
            o.streamLen = len;
        } else l = save;
    }
    return o;
}

// Descomprimir un flujo (FlateDecode y DCTDecode se reconocen; DCT se devuelve tal cual)
static unsigned char *inflate_data(const unsigned char *in, int inLen, int *outLen)
{
    if (inLen < 2) { *outLen = 0; return NULL; }
    const unsigned char *src = in;
    int srcLen = inLen;
    if ((in[0] & 0x0F) == 8 && ((in[0] << 8) | in[1]) % 31 == 0) { src += 2; srcLen -= 2; }
    return inflate_raw(src, srcLen, outLen);
}

static void png_unpredict(unsigned char *data, int len, int colors, int bpc, int columns, unsigned char **out, int *outLen)
{
    int bpp = (colors * bpc + 7) / 8;
    int row = (colors * bpc * columns + 7) / 8;
    int rows = len / (row + 1);
    unsigned char *res = (unsigned char *)calloc((size_t)rows * row + 1, 1);
    unsigned char *prev = (unsigned char *)calloc(row, 1);
    for (int r = 0; r < rows; r++) {
        unsigned char *src = data + (size_t)r * (row + 1);
        int f = src[0];
        unsigned char *dst = res + (size_t)r * row;
        for (int i = 0; i < row; i++) {
            int a = i >= bpp ? dst[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0, x = src[1 + i];
            switch (f) {
            case 1: x += a; break;
            case 2: x += b; break;
            case 3: x += (a + b) / 2; break;
            case 4: { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c); x += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
            default: break;
            }
            dst[i] = (unsigned char)x;
        }
        memcpy(prev, dst, row);
    }
    free(prev);
    *out = res;
    *outLen = rows * row;
}

// Devuelve los datos del flujo ya descomprimidos (malloc). isJpeg indica DCTDecode.
static unsigned char *stream_data(Obj o, int *len, bool *isJpeg)
{
    if (isJpeg) *isJpeg = false;
    *len = 0;
    if (!o.stream) return NULL;
    Obj f = dget(o, "Filter");
    Obj parms = dget(o, "DecodeParms");
    const char *filters[4];
    Obj parmList[4];
    int nf = 0;
    if (f.type == O_NAME) { filters[nf] = f.s; parmList[nf++] = parms; }
    else if (f.type == O_ARR) {
        for (int i = 0; i < f.n && nf < 4; i++) {
            Obj fi = resolve(f.v[i]);
            if (fi.type != O_NAME) continue;
            parmList[nf] = parms.type == O_ARR && i < parms.n ? resolve(parms.v[i]) : parms;
            filters[nf++] = fi.s;
        }
    }
    unsigned char *buf = (unsigned char *)malloc(o.streamLen + 1);
    memcpy(buf, o.stream, o.streamLen);
    int n = o.streamLen;
    for (int i = 0; i < nf; i++) {
        if (strcmp(filters[i], "FlateDecode") == 0 || strcmp(filters[i], "Fl") == 0) {
            int on;
            unsigned char *d = inflate_data(buf, n, &on);
            free(buf);
            if (!d) return NULL;
            buf = d;
            n = on;
            int pred = (int)dnum(parmList[i], "Predictor", 1);
            if (pred >= 10) {
                unsigned char *u;
                int un;
                png_unpredict(buf, n, (int)dnum(parmList[i], "Colors", 1), (int)dnum(parmList[i], "BitsPerComponent", 8), (int)dnum(parmList[i], "Columns", 1), &u, &un);
                free(buf);
                buf = u;
                n = un;
            }
        } else if (strcmp(filters[i], "ASCII85Decode") == 0 || strcmp(filters[i], "A85") == 0) {
            unsigned char *o = (unsigned char *)malloc(n + 8);
            int on = 0, cnt = 0;
            unsigned int acc = 0;
            for (int k = 0; k < n; k++) {
                int ch = buf[k];
                if (ch == '~') break;
                if (is_ws(ch)) continue;
                if (ch == 'z' && cnt == 0) { memset(o + on, 0, 4); on += 4; continue; }
                if (ch < '!' || ch > 'u') continue;
                acc = acc * 85 + (ch - '!');
                if (++cnt == 5) {
                    o[on++] = (unsigned char)(acc >> 24); o[on++] = (unsigned char)(acc >> 16);
                    o[on++] = (unsigned char)(acc >> 8); o[on++] = (unsigned char)acc;
                    cnt = 0; acc = 0;
                }
            }
            if (cnt > 1) {
                for (int k = cnt; k < 5; k++) acc = acc * 85 + 84;
                for (int k = 0; k < cnt - 1; k++) o[on++] = (unsigned char)(acc >> (24 - 8 * k));
            }
            free(buf);
            buf = o;
            n = on;
        } else if (strcmp(filters[i], "ASCIIHexDecode") == 0 || strcmp(filters[i], "AHx") == 0) {
            unsigned char *o = (unsigned char *)malloc(n / 2 + 2);
            int on = 0, hi = -1;
            for (int k = 0; k < n && buf[k] != '>'; k++) {
                int v = hexv(buf[k]);
                if (v < 0) continue;
                if (hi < 0) hi = v; else { o[on++] = (unsigned char)(hi * 16 + v); hi = -1; }
            }
            if (hi >= 0) o[on++] = (unsigned char)(hi * 16);
            free(buf);
            buf = o;
            n = on;
        } else if (strcmp(filters[i], "DCTDecode") == 0 || strcmp(filters[i], "DCT") == 0) {
            if (isJpeg) *isJpeg = true;
            break;
        } else {
            // Filtro que no sabemos leer (LZW, JPX, ASCII85...)
            free(buf);
            return NULL;
        }
    }
    *len = n;
    return buf;
}

static Obj get(int num)
{
    if (num <= 0 || num > P.maxObj) return NULLOBJ;
    if (P.cached[num]) return P.cache[num];
    if (P.busy[num]) return NULLOBJ;
    P.busy[num] = true;
    Obj o = NULLOBJ;
    XRef x = P.xref[num];
    if (x.offset > 0) {
        o = parse_at(P.data + x.offset, P.data + P.size, true);
    } else if (x.stm > 0) {
        Obj stm = get(x.stm);
        int len;
        unsigned char *d = stream_data(stm, &len, NULL);
        if (d) {
            int first = (int)dnum(stm, "First", 0), count = (int)dnum(stm, "N", 0);
            Lex l = { d, d + len };
            for (int i = 0; i < count; i++) {
                Obj on = read_token(&l), off = read_token(&l);
                if (on.type != O_NUM || off.type != O_NUM) break;
                if ((int)on.num == num) {
                    int p = first + (int)off.num;
                    if (p < len) {
                        // La copia vive en la arena para que las cadenas sigan siendo válidas
                        unsigned char *copy = (unsigned char *)arena(len - p + 1);
                        memcpy(copy, d + p, len - p);
                        o = parse_at(copy, copy + (len - p), false);
                    }
                    break;
                }
            }
            free(d);
        }
    }
    P.busy[num] = false;
    P.cache[num] = o;
    P.cached[num] = true;
    return o;
}

// Encuentra todos los "N G obj" del archivo y los objetos dentro de ObjStm
static void build_xref(void)
{
    const unsigned char *d = P.data;
    int n = P.size;
    int maxObj = 0;
    for (int i = 4; i + 3 <= n; i++) {
        if (d[i] != 'o' || d[i + 1] != 'b' || d[i + 2] != 'j' || !is_ws(d[i - 1])) continue;
        int j = i - 1;
        while (j > 0 && is_ws(d[j])) j--;
        while (j > 0 && isdigit(d[j])) j--;
        if (!is_ws(d[j])) continue;
        while (j > 0 && is_ws(d[j])) j--;
        int e = j;
        while (j > 0 && isdigit(d[j])) j--;
        if (e == j) continue;
        int num = atoi((const char *)d + j + 1);
        if (num > maxObj && num < 5000000) maxObj = num;
    }
    // Los ObjStm pueden tener números más altos que el último objeto suelto
    maxObj = maxObj * 2 + 1000;
    P.maxObj = maxObj;
    P.xref = (XRef *)calloc(maxObj + 1, sizeof(XRef));
    P.cache = (Obj *)calloc(maxObj + 1, sizeof(Obj));
    P.cached = (bool *)calloc(maxObj + 1, sizeof(bool));
    P.busy = (bool *)calloc(maxObj + 1, sizeof(bool));
    for (int i = 4; i + 3 <= n; i++) {
        if (d[i] != 'o' || d[i + 1] != 'b' || d[i + 2] != 'j' || !is_ws(d[i - 1])) continue;
        if (i + 3 < n && !is_ws(d[i + 3]) && !is_delim(d[i + 3])) continue;
        int j = i - 1;
        while (j > 0 && is_ws(d[j])) j--;
        while (j > 0 && isdigit(d[j])) j--;
        if (!is_ws(d[j])) continue;
        while (j > 0 && is_ws(d[j])) j--;
        int e = j;
        while (j > 0 && isdigit(d[j])) j--;
        if (e == j) continue;
        int num = atoi((const char *)d + j + 1);
        if (num <= 0 || num > maxObj) continue;
        P.xref[num].offset = j + 1;
        P.xref[num].stm = 0;
    }
    // Objetos comprimidos
    for (int num = 1; num <= maxObj; num++) {
        if (P.xref[num].offset <= 0) continue;
        Obj o = get(num);
        if (o.type != O_DICT || !o.stream || !is_name(dget(o, "Type"), "ObjStm")) continue;
        int len;
        unsigned char *s = stream_data(o, &len, NULL);
        if (!s) continue;
        int count = (int)dnum(o, "N", 0);
        Lex l = { s, s + len };
        for (int i = 0; i < count; i++) {
            Obj on = read_token(&l), off = read_token(&l);
            if (on.type != O_NUM || off.type != O_NUM) break;
            int k = (int)on.num;
            if (k > 0 && k <= maxObj && P.xref[k].offset <= 0) { P.xref[k].stm = num; P.xref[k].idx = i; }
        }
        free(s);
    }
}

// ------------------------------------------------------------------ Fuentes

typedef struct {
    Obj dict;
    bool twoByte;
    int *uni;          // código -> Unicode (0 = no se sabe)
    int uniSize;
    float *widths;     // código -> ancho (milésimas)
    int widthsSize;
    float defWidth;
    char name[64];
    bool bold, italic, serif, script;
} PFont;

#define MAX_PFONTS 96
static PFont g_pf[MAX_PFONTS];
static int g_pfCount;

static int utf16_to_cp(const unsigned char *s, int len)
{
    if (len >= 2) {
        int a = (s[0] << 8) | s[1];
        if (a >= 0xD800 && a < 0xDC00 && len >= 4) {
            int b = (s[2] << 8) | s[3];
            return 0x10000 + ((a - 0xD800) << 10) + (b - 0xDC00);
        }
        return a;
    }
    if (len == 1) return s[0];
    return 0;
}

static void parse_tounicode(PFont *f, Obj stm)
{
    int len;
    unsigned char *d = stream_data(stm, &len, NULL);
    if (!d) return;
    f->uniSize = f->twoByte ? 65536 : 256;
    f->uni = (int *)calloc(f->uniSize, sizeof(int));
    Lex l = { d, d + len };
    Obj prev[3];
    int np = 0;
    for (;;) {
        Obj t = read_token(&l);
        if (l.p >= l.end && t.type == O_NULL) break;
        if (t.type == O_OP && strcmp(t.s, "beginbfchar") == 0) {
            for (;;) {
                Obj a = read_token(&l);
                if (a.type == O_OP) break;
                Obj b = read_token(&l);
                if (a.type != O_STR || b.type != O_STR) continue;
                int code = a.len >= 2 ? (((unsigned char)a.s[0] << 8) | (unsigned char)a.s[1]) : (unsigned char)a.s[0];
                if (code < f->uniSize) f->uni[code] = utf16_to_cp((const unsigned char *)b.s, b.len);
            }
        } else if (t.type == O_OP && strcmp(t.s, "beginbfrange") == 0) {
            for (;;) {
                Obj a = read_token(&l);
                if (a.type == O_OP && strcmp(a.s, "[") != 0) break;
                Obj b = read_token(&l);
                Obj c = parse_obj(&l, 0);
                if (a.type != O_STR || b.type != O_STR) continue;
                int lo = a.len >= 2 ? (((unsigned char)a.s[0] << 8) | (unsigned char)a.s[1]) : (unsigned char)a.s[0];
                int hi = b.len >= 2 ? (((unsigned char)b.s[0] << 8) | (unsigned char)b.s[1]) : (unsigned char)b.s[0];
                if (hi - lo > 65535) continue;
                if (c.type == O_STR) {
                    int base = utf16_to_cp((const unsigned char *)c.s, c.len);
                    for (int k = lo; k <= hi && k < f->uniSize; k++) f->uni[k] = base + (k - lo);
                } else if (c.type == O_ARR) {
                    for (int k = lo; k <= hi && k < f->uniSize && k - lo < c.n; k++)
                        if (c.v[k - lo].type == O_STR) f->uni[k] = utf16_to_cp((const unsigned char *)c.v[k - lo].s, c.v[k - lo].len);
                }
            }
        }
        (void)prev; (void)np;
    }
    free(d);
}

// Nombres de glifos habituales (para fuentes sin ToUnicode con /Differences)
static int glyph_name_cp(const char *n)
{
    static const struct { const char *n; int cp; } T[] = {
        { "space", 32 }, { "exclam", 33 }, { "quotedbl", 34 }, { "numbersign", 35 }, { "dollar", 36 }, { "percent", 37 },
        { "ampersand", 38 }, { "quotesingle", 39 }, { "parenleft", 40 }, { "parenright", 41 }, { "asterisk", 42 }, { "plus", 43 },
        { "comma", 44 }, { "hyphen", 45 }, { "period", 46 }, { "slash", 47 }, { "zero", 48 }, { "one", 49 }, { "two", 50 },
        { "three", 51 }, { "four", 52 }, { "five", 53 }, { "six", 54 }, { "seven", 55 }, { "eight", 56 }, { "nine", 57 },
        { "colon", 58 }, { "semicolon", 59 }, { "less", 60 }, { "equal", 61 }, { "greater", 62 }, { "question", 63 }, { "at", 64 },
        { "bracketleft", 91 }, { "backslash", 92 }, { "bracketright", 93 }, { "underscore", 95 }, { "quoteleft", 0x2018 },
        { "quoteright", 0x2019 }, { "quotedblleft", 0x201C }, { "quotedblright", 0x201D }, { "endash", 0x2013 }, { "emdash", 0x2014 },
        { "bullet", 0x2022 }, { "ellipsis", 0x2026 }, { "Euro", 0x20AC }, { "exclamdown", 0xA1 }, { "questiondown", 0xBF },
        { "ordfeminine", 0xAA }, { "ordmasculine", 0xBA }, { "degree", 0xB0 }, { "copyright", 0xA9 }, { "registered", 0xAE },
        { "aacute", 0xE1 }, { "eacute", 0xE9 }, { "iacute", 0xED }, { "oacute", 0xF3 }, { "uacute", 0xFA }, { "ntilde", 0xF1 },
        { "Aacute", 0xC1 }, { "Eacute", 0xC9 }, { "Iacute", 0xCD }, { "Oacute", 0xD3 }, { "Uacute", 0xDA }, { "Ntilde", 0xD1 },
        { "udieresis", 0xFC }, { "Udieresis", 0xDC }, { "ccedilla", 0xE7 }, { "Ccedilla", 0xC7 }, { "agrave", 0xE0 }, { "egrave", 0xE8 },
        { "ograve", 0xF2 }, { "fi", 0xFB01 }, { "fl", 0xFB02 },
    };
    if (n[0] && !n[1]) return (unsigned char)n[0];
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++) if (strcmp(T[i].n, n) == 0) return T[i].cp;
    if (n[0] == 'u' && n[1] == 'n' && n[2] == 'i' && strlen(n) >= 7) return (int)strtol(n + 3, NULL, 16);
    return 0;
}

static void lower_copy(char *dst, const char *src, int max)
{
    int i = 0;
    for (; src[i] && i < max - 1; i++) dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = 0;
}

static PFont *load_font(Obj fontRef)
{
    Obj d = resolve(fontRef);
    if (d.type != O_DICT) return NULL;
    for (int i = 0; i < g_pfCount; i++) if (g_pf[i].dict.v == d.v) return &g_pf[i];
    if (g_pfCount >= MAX_PFONTS) return &g_pf[MAX_PFONTS - 1];
    PFont *f = &g_pf[g_pfCount++];
    memset(f, 0, sizeof(*f));
    f->dict = d;
    f->defWidth = 500;
    Obj sub = dget(d, "Subtype");
    Obj base = dget(d, "BaseFont");
    if (base.type == O_NAME) {
        const char *b = base.s;
        const char *plus = strchr(b, '+');
        if (plus && plus - b == 6) b = plus + 1;
        snprintf(f->name, sizeof(f->name), "%s", b);
    }
    Obj desc = dget(d, "FontDescriptor");
    if (is_name(sub, "Type0")) {
        f->twoByte = true;
        Obj df = dget(d, "DescendantFonts");
        Obj cid = df.type == O_ARR && df.n > 0 ? resolve(df.v[0]) : NULLOBJ;
        if (desc.type == O_NULL) desc = dget(cid, "FontDescriptor");
        f->defWidth = (float)dnum(cid, "DW", 1000);
        Obj w = dget(cid, "W");
        if (w.type == O_ARR) {
            f->widthsSize = 65536;
            f->widths = (float *)malloc(sizeof(float) * 65536);
            for (int i = 0; i < 65536; i++) f->widths[i] = f->defWidth;
            for (int i = 0; i < w.n;) {
                Obj a = resolve(w.v[i]);
                if (a.type != O_NUM) { i++; continue; }
                Obj b = i + 1 < w.n ? resolve(w.v[i + 1]) : NULLOBJ;
                if (b.type == O_ARR) {
                    for (int k = 0; k < b.n; k++) { int c = (int)a.num + k; Obj x = resolve(b.v[k]); if (c >= 0 && c < 65536 && x.type == O_NUM) f->widths[c] = (float)x.num; }
                    i += 2;
                } else if (b.type == O_NUM && i + 2 < w.n) {
                    Obj x = resolve(w.v[i + 2]);
                    for (int c = (int)a.num; c <= (int)b.num && c < 65536; c++) if (c >= 0 && x.type == O_NUM) f->widths[c] = (float)x.num;
                    i += 3;
                } else i++;
            }
        }
    } else {
        int first = (int)dnum(d, "FirstChar", 0);
        Obj w = dget(d, "Widths");
        f->widthsSize = 256;
        f->widths = (float *)malloc(sizeof(float) * 256);
        f->defWidth = (float)dnum(desc, "MissingWidth", 500);
        for (int i = 0; i < 256; i++) f->widths[i] = f->defWidth;
        if (w.type == O_ARR)
            for (int k = 0; k < w.n; k++) { Obj x = resolve(w.v[k]); if (first + k < 256 && x.type == O_NUM) f->widths[first + k] = (float)x.num; }
    }
    Obj tu = dget(d, "ToUnicode");
    if (tu.stream) parse_tounicode(f, tu);
    if (!f->uni && !f->twoByte) {
        // Sin ToUnicode: WinAnsi (casi Latin-1) más /Differences
        f->uniSize = 256;
        f->uni = (int *)calloc(256, sizeof(int));
        static const int win[32] = { 0x20AC, 0, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0, 0x17D, 0,
                                     0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0, 0x17E, 0x178 };
        for (int c = 32; c < 256; c++) f->uni[c] = (c >= 128 && c < 160) ? win[c - 128] : c;
        Obj enc = dget(d, "Encoding");
        Obj diff = dget(enc, "Differences");
        if (diff.type == O_ARR) {
            int code = 0;
            for (int i = 0; i < diff.n; i++) {
                Obj x = resolve(diff.v[i]);
                if (x.type == O_NUM) code = (int)x.num;
                else if (x.type == O_NAME) { int cp = glyph_name_cp(x.s); if (code >= 0 && code < 256 && cp) f->uni[code] = cp; code++; }
            }
        }
    }
    // Estilo
    char low[64];
    lower_copy(low, f->name, sizeof(low));
    int flags = (int)dnum(desc, "Flags", 0);
    double weight = dnum(desc, "FontWeight", 400);
    f->bold = strstr(low, "bold") || strstr(low, "black") || strstr(low, "heavy") || strstr(low, "semibold") || weight >= 600;
    f->italic = strstr(low, "italic") || strstr(low, "oblique") || (flags & 64) || fabs(dnum(desc, "ItalicAngle", 0)) > 2;
    f->serif = (flags & 2) != 0;
    f->script = (flags & 8) != 0;
    return f;
}

// ------------------------------------------------------------------ Dibujo

typedef struct { double a, b, c, d, e, f; } Mat;

static Mat mat_mul(Mat m, Mat n)   // m después n (como en PDF: m × n)
{
    Mat r;
    r.a = m.a * n.a + m.b * n.c;
    r.b = m.a * n.b + m.b * n.d;
    r.c = m.c * n.a + m.d * n.c;
    r.d = m.c * n.b + m.d * n.d;
    r.e = m.e * n.a + m.f * n.c + n.e;
    r.f = m.e * n.b + m.f * n.d + n.f;
    return r;
}

typedef struct {
    Mat ctm;
    float fill[3], stroke[3];
    float fillA, strokeA;
    float lineW;
    int fillN, strokeN;          // componentes del espacio de color
    bool fillPattern;
    // recorte (en píxeles del lienzo)
    float cx0, cy0, cx1, cy1;
    // texto
    PFont *font;
    float fontSize, tc, tw, th, tl, rise;
    int render;
} GState;

typedef struct {
    PdfResult *out;
    float scale;       // px por punto
    float ox, oy;      // origen de la página (MediaBox)
    float pageHpt;
    // Formas pendientes de juntar en un dibujo
    char *svg;
    int svgLen, svgCap;
    float sx0, sy0, sx1, sy1;    // caja de lo acumulado
    float scx0, scy0, scx1, scy1; // recorte de lo acumulado
    unsigned int shapeColor;
    bool shapeSingle, shapeAny;
    int shapeCount;
    float shapeAlpha;
    // Camino actual
    char *path;
    int pathLen, pathCap;
    float px0, py0, px1, py1;
    bool pathAny, pathRect;
    // Texto
    Mat tm, tlm;
    int depth;
    int qdepth, shapeQ;
} Ctx;

static void str_append(char **buf, int *len, int *cap, const char *s, int n)
{
    if (*len + n + 1 > *cap) { *cap = (*len + n + 1) * 2 + 256; *buf = (char *)realloc(*buf, *cap); }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = 0;
}

static void str_printf(char **buf, int *len, int *cap, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) str_append(buf, len, cap, tmp, n < (int)sizeof(tmp) ? n : (int)sizeof(tmp) - 1);
}

static Vector2 dev(Ctx *c, const GState *g, double x, double y)
{
    double ux = x * g->ctm.a + y * g->ctm.c + g->ctm.e;
    double uy = x * g->ctm.b + y * g->ctm.d + g->ctm.f;
    return (Vector2){ (float)((ux - c->ox) * c->scale), (float)((c->pageHpt - (uy - c->oy)) * c->scale) };
}

static PdfItem *add_item(PdfResult *r)
{
    if (r->count == r->cap) { r->cap = r->cap ? r->cap * 2 : 64; r->items = (PdfItem *)realloc(r->items, sizeof(PdfItem) * r->cap); }
    PdfItem *it = &r->items[r->count++];
    memset(it, 0, sizeof(*it));
    it->opacity = 1;
    return it;
}

static void color_to_rgb(const float *v, int n, float *rgb)
{
    if (n == 1) { rgb[0] = rgb[1] = rgb[2] = v[0]; }
    else if (n == 4) {
        rgb[0] = (1 - v[0]) * (1 - v[3]);
        rgb[1] = (1 - v[1]) * (1 - v[3]);
        rgb[2] = (1 - v[2]) * (1 - v[3]);
    } else { rgb[0] = v[0]; rgb[1] = v[1]; rgb[2] = v[2]; }
}

static unsigned int pack(const float *rgb)
{
    int r = (int)roundf(fminf(1, fmaxf(0, rgb[0])) * 255), g = (int)roundf(fminf(1, fmaxf(0, rgb[1])) * 255), b = (int)roundf(fminf(1, fmaxf(0, rgb[2])) * 255);
    return (unsigned int)(r << 16 | g << 8 | b);
}

// Convierte lo acumulado (formas vectoriales) en una pieza
static void flush_shapes(Ctx *c)
{
    if (!c->shapeAny) return;
    c->shapeAny = false;
    float x0 = fmaxf(c->sx0, c->scx0), y0 = fmaxf(c->sy0, c->scy0);
    float x1 = fminf(c->sx1, c->scx1), y1 = fminf(c->sy1, c->scy1);
    x0 = fmaxf(x0, 0); y0 = fmaxf(y0, 0);
    x1 = fminf(x1, c->out->pageW); y1 = fminf(y1, c->out->pageH);
    int ix0 = (int)floorf(x0), iy0 = (int)floorf(y0), ix1 = (int)ceilf(x1), iy1 = (int)ceilf(y1);
    int W = ix1 - ix0, H = iy1 - iy0;
    if (W < 1 || H < 1 || W > 8000 || H > 8000) { c->svgLen = 0; return; }
    // Dibujamos al doble para que quede suave al ampliar
    float ss = ((long)W * H <= 1500000L) ? 2.0f : 1.0f;
    if (c->shapeSingle) ss = 1.0f;   // un solo color: con el tamaño normal basta
    char *doc = NULL;
    int len = 0, cap = 0;
    str_printf(&doc, &len, &cap, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\">", (int)c->out->pageW + 1, (int)c->out->pageH + 1);
    str_append(&doc, &len, &cap, c->svg, c->svgLen);
    str_append(&doc, &len, &cap, "</svg>", 6);
    NSVGimage *img = nsvgParse(doc, "px", 96);
    free(doc);
    c->svgLen = 0;
    if (!img) return;
    int PW = (int)(W * ss), PH = (int)(H * ss);
    unsigned char *px = (unsigned char *)calloc((size_t)PW * PH, 4);
    NSVGrasterizer *rast = nsvgCreateRasterizer();
    nsvgRasterize(rast, img, -ix0 * ss, -iy0 * ss, ss, px, PW, PH, PW * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);
    // ¿Hay algo?
    bool any = false;
    for (int i = 0; i < PW * PH && !any; i++) if (px[i * 4 + 3]) any = true;
    if (!any) { free(px); return; }
    PdfItem *it = add_item(c->out);
    it->kind = PDF_SHAPE;
    it->x = (float)ix0;
    it->y = (float)iy0;
    it->w = (float)W;
    it->h = (float)H;
    it->img = (Image){ px, PW, PH, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    if (c->shapeSingle) {
        it->tint = true;
        it->color = (Color){ (unsigned char)(c->shapeColor >> 16), (unsigned char)(c->shapeColor >> 8), (unsigned char)c->shapeColor, 255 };
        for (int i = 0; i < PW * PH; i++) { px[i * 4] = 255; px[i * 4 + 1] = 255; px[i * 4 + 2] = 255; }
    } else {
        image_bleed_edges(&it->img);
    }
}

static void path_reset(Ctx *c)
{
    c->pathLen = 0;
    c->pathAny = false;
    c->pathRect = true;
    c->px0 = c->py0 = 1e9f;
    c->px1 = c->py1 = -1e9f;
}

static void path_point(Ctx *c, Vector2 p)
{
    if (p.x < c->px0) c->px0 = p.x;
    if (p.y < c->py0) c->py0 = p.y;
    if (p.x > c->px1) c->px1 = p.x;
    if (p.y > c->py1) c->py1 = p.y;
    c->pathAny = true;
}

static void paint_path(Ctx *c, GState *g, bool fill, bool stroke, bool evenodd)
{
    if (!c->pathAny || (!fill && !stroke)) { path_reset(c); return; }
    if (fill && g->fillPattern) { fill = false; c->out->skipped++; }
    if (!fill && !stroke) { path_reset(c); return; }
    float area = (c->px1 - c->px0) * (c->py1 - c->py0);
    bool big = area > c->out->pageW * c->out->pageH * 0.85f;
    // Un fondo que cubre la página va por separado
    if (big) flush_shapes(c);
    float rgb[3], srgb[3];
    color_to_rgb(g->fill, g->fillN, rgb);
    color_to_rgb(g->stroke, g->strokeN, srgb);
    unsigned int fc = pack(rgb), sc = pack(srgb);
    float lw = g->lineW * (float)sqrt(fabs(g->ctm.a * g->ctm.d - g->ctm.b * g->ctm.c)) * c->scale;
    if (lw < 0.5f) lw = 0.5f;
    unsigned int col = fill ? fc : sc;
    // Otro color y lejos de lo que llevamos: es otra pieza
    if (c->shapeAny && col != c->shapeColor) {
        float pad0 = stroke ? lw : 0;
        bool apart = c->px1 + pad0 < c->sx0 || c->px0 - pad0 > c->sx1 || c->py1 + pad0 < c->sy0 || c->py0 - pad0 > c->sy1;
        if (apart) flush_shapes(c);
    }
    // Color único del grupo
    bool single = true;
    if (fill && stroke && fc != sc) single = false;
    if (c->shapeAny && c->shapeColor != col) single = false;
    if (!c->shapeAny) {
        c->shapeSingle = true;
        c->shapeColor = col;
        c->sx0 = c->sy0 = 1e9f;
        c->sx1 = c->sy1 = -1e9f;
        c->scx0 = g->cx0; c->scy0 = g->cy0; c->scx1 = g->cx1; c->scy1 = g->cy1;
        c->shapeCount = 0;
        c->shapeQ = c->qdepth;
    } else {
        // El recorte del grupo es la unión de los recortes
        c->scx0 = fminf(c->scx0, g->cx0); c->scy0 = fminf(c->scy0, g->cy0);
        c->scx1 = fmaxf(c->scx1, g->cx1); c->scy1 = fmaxf(c->scy1, g->cy1);
    }
    if (!single) c->shapeSingle = false;
    if ((fill && g->fillA < 0.999f) || (stroke && g->strokeA < 0.999f)) c->shapeSingle = false;
    float pad = stroke ? lw : 0;
    c->sx0 = fminf(c->sx0, c->px0 - pad); c->sy0 = fminf(c->sy0, c->py0 - pad);
    c->sx1 = fmaxf(c->sx1, c->px1 + pad); c->sy1 = fmaxf(c->sy1, c->py1 + pad);
    // Recorte rectangular de este camino: lo aplicamos con un <clipPath> no; nanosvg no
    // lo entiende. Lo aproximamos limitando la caja del grupo (arriba).
    str_printf(&c->svg, &c->svgLen, &c->svgCap, "<path d=\"");
    str_append(&c->svg, &c->svgLen, &c->svgCap, c->path, c->pathLen);
    if (fill) str_printf(&c->svg, &c->svgLen, &c->svgCap, "\" fill=\"#%06X\" fill-opacity=\"%.3f\" fill-rule=\"%s\"", fc, g->fillA, evenodd ? "evenodd" : "nonzero");
    else str_printf(&c->svg, &c->svgLen, &c->svgCap, "\" fill=\"none\"");
    if (stroke) str_printf(&c->svg, &c->svgLen, &c->svgCap, " stroke=\"#%06X\" stroke-opacity=\"%.3f\" stroke-width=\"%.2f\" stroke-linejoin=\"round\"", sc, g->strokeA, lw);
    str_printf(&c->svg, &c->svgLen, &c->svgCap, "/>");
    c->shapeAny = true;
    c->shapeCount++;
    path_reset(c);
    if (big) flush_shapes(c);
    // Grupos enormes: los partimos para que no sea una sola pieza gigantesca
    if (c->shapeCount > 400) flush_shapes(c);
}

// ------------------------------------------------------------------ Imágenes

static bool decode_image(Obj im, Obj resources, Image *out);

static int cs_components(Obj cs, Obj resources, Obj *palette, int *hival, int *baseN)
{
    cs = resolve(cs);
    if (cs.type == O_NAME) {
        if (strcmp(cs.s, "DeviceGray") == 0 || strcmp(cs.s, "G") == 0 || strcmp(cs.s, "CalGray") == 0) return 1;
        if (strcmp(cs.s, "DeviceRGB") == 0 || strcmp(cs.s, "RGB") == 0 || strcmp(cs.s, "CalRGB") == 0) return 3;
        if (strcmp(cs.s, "DeviceCMYK") == 0 || strcmp(cs.s, "CMYK") == 0) return 4;
        Obj named = dget(dget(resources, "ColorSpace"), cs.s);
        if (named.type != O_NULL) return cs_components(named, resources, palette, hival, baseN);
        return 3;
    }
    if (cs.type == O_ARR && cs.n > 0) {
        Obj kind = resolve(cs.v[0]);
        if (is_name(kind, "ICCBased") && cs.n > 1) return (int)dnum(cs.v[1], "N", 3);
        if (is_name(kind, "Indexed") || is_name(kind, "I")) {
            if (cs.n >= 4) {
                *baseN = cs_components(cs.v[1], resources, NULL, NULL, NULL);
                *hival = (int)resolve(cs.v[2]).num;
                *palette = resolve(cs.v[3]);
            }
            return -1;
        }
        if (is_name(kind, "CalRGB") || is_name(kind, "Lab")) return 3;
        if (is_name(kind, "CalGray")) return 1;
        if (is_name(kind, "Separation")) return 1;
        if (is_name(kind, "DeviceN") && cs.n > 1) { Obj names = resolve(cs.v[1]); return names.type == O_ARR ? names.n : 1; }
    }
    return 3;
}

static bool decode_image(Obj im, Obj resources, Image *out)
{
    int len;
    bool jpeg;
    unsigned char *d = stream_data(im, &len, &jpeg);
    if (!d) return false;
    int W = (int)dnum(im, "Width", 0), H = (int)dnum(im, "Height", 0);
    if (jpeg) {
        Image img = LoadImageFromMemory(".jpg", d, len);
        free(d);
        if (!img.data) return false;
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        *out = img;
    } else {
        if (W <= 0 || H <= 0 || (long)W * H > 40000000L) { free(d); return false; }
        int bpc = (int)dnum(im, "BitsPerComponent", 8);
        bool mask = dget(im, "ImageMask").type == O_BOOL && dget(im, "ImageMask").num != 0;
        Obj palette = NULLOBJ;
        int hival = 0, baseN = 3;
        int ncomp = mask ? 1 : cs_components(dget(im, "ColorSpace"), resources, &palette, &hival, &baseN);
        int comps = ncomp < 0 ? 1 : ncomp;
        int rowBytes = (W * comps * bpc + 7) / 8;
        if ((long)rowBytes * H > len) { free(d); return false; }
        unsigned char *pal = NULL;
        int palLen = 0;
        if (ncomp < 0) {
            if (palette.type == O_STR) { pal = (unsigned char *)palette.s; palLen = palette.len; }
            else if (palette.stream) pal = stream_data(palette, &palLen, NULL);
        }
        unsigned char *px = (unsigned char *)malloc((size_t)W * H * 4);
        for (int y = 0; y < H; y++) {
            const unsigned char *row = d + (size_t)y * rowBytes;
            for (int x = 0; x < W; x++) {
                float v[4] = { 0 };
                for (int k = 0; k < comps && k < 4; k++) {
                    int bit = (x * comps + k) * bpc;
                    int val;
                    if (bpc == 8) val = row[bit / 8];
                    else if (bpc == 16) val = row[bit / 8];
                    else val = (row[bit / 8] >> (8 - bpc - (bit % 8))) & ((1 << bpc) - 1);
                    v[k] = bpc == 16 ? val / 255.0f : val / (float)((1 << (bpc == 16 ? 8 : bpc)) - 1);
                    if (ncomp < 0) v[k] = (float)val;
                }
                float rgb[3];
                unsigned char *o = px + ((size_t)y * W + x) * 4;
                if (ncomp < 0) {
                    int idx = (int)v[0];
                    float pv[4] = { 0 };
                    for (int k = 0; k < baseN && k < 4; k++) { int p = idx * baseN + k; pv[k] = (pal && p < palLen) ? pal[p] / 255.0f : 0; }
                    color_to_rgb(pv, baseN, rgb);
                } else if (mask) {
                    rgb[0] = rgb[1] = rgb[2] = 0;
                } else color_to_rgb(v, comps, rgb);
                o[0] = (unsigned char)(rgb[0] * 255);
                o[1] = (unsigned char)(rgb[1] * 255);
                o[2] = (unsigned char)(rgb[2] * 255);
                o[3] = mask ? (v[0] < 0.5f ? 255 : 0) : 255;
            }
        }
        if (pal && palette.type != O_STR) free(pal);
        free(d);
        *out = (Image){ px, W, H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    }
    // Transparencia (SMask)
    Obj sm = dget(im, "SMask");
    if (sm.stream) {
        Image a;
        Obj smGray = sm;
        if (decode_image(smGray, resources, &a)) {
            if (a.width != out->width || a.height != out->height) ImageResize(&a, out->width, out->height);
            unsigned char *dp = (unsigned char *)out->data, *ap = (unsigned char *)a.data;
            for (int i = 0; i < out->width * out->height; i++) dp[i * 4 + 3] = ap[i * 4];
            UnloadImage(a);
        }
    }
    return true;
}

static void place_image(Ctx *c, GState *g, Obj im, Obj resources)
{
    Image img;
    if (!decode_image(im, resources, &img)) { c->out->skipped++; return; }
    flush_shapes(c);
    // La imagen ocupa el cuadrado unidad: arriba-izquierda es (0,1)
    Vector2 p01 = dev(c, g, 0, 1), p11 = dev(c, g, 1, 1), p00 = dev(c, g, 0, 0);
    Vector2 xd = { p11.x - p01.x, p11.y - p01.y }, yd = { p00.x - p01.x, p00.y - p01.y };
    float w = sqrtf(xd.x * xd.x + xd.y * xd.y), h = sqrtf(yd.x * yd.x + yd.y * yd.y);
    if (w < 0.5f || h < 0.5f) { UnloadImage(img); return; }
    float cross = xd.x * yd.y - xd.y * yd.x;
    if (cross < 0) {
        // Espejo: damos la vuelta a la imagen y cambiamos el sentido de x
        ImageFlipHorizontal(&img);
        xd.x = -xd.x; xd.y = -xd.y;
        p01 = p11;
    }
    float rot = atan2f(xd.y, xd.x) * RAD2DEG;
    float cx = p01.x + (xd.x + yd.x) / 2, cy = p01.y + (xd.y + yd.y) / 2;
    // Recorte rectangular (solo si la imagen no está girada)
    if (fabsf(rot) < 0.5f) {
        float x0 = cx - w / 2, y0 = cy - h / 2, x1 = cx + w / 2, y1 = cy + h / 2;
        float nx0 = fmaxf(x0, g->cx0), ny0 = fmaxf(y0, g->cy0), nx1 = fminf(x1, g->cx1), ny1 = fminf(y1, g->cy1);
        if (nx1 <= nx0 || ny1 <= ny0) { UnloadImage(img); return; }
        if (nx0 > x0 + 0.5f || ny0 > y0 + 0.5f || nx1 < x1 - 0.5f || ny1 < y1 - 0.5f) {
            float kx = img.width / w, ky = img.height / h;
            Rectangle r = { (nx0 - x0) * kx, (ny0 - y0) * ky, (nx1 - nx0) * kx, (ny1 - ny0) * ky };
            if (r.width >= 1 && r.height >= 1) ImageCrop(&img, r);
            x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
            w = x1 - x0; h = y1 - y0;
            cx = (x0 + x1) / 2; cy = (y0 + y1) / 2;
        }
        rot = 0;
    }
    PdfItem *it = add_item(c->out);
    it->kind = PDF_IMAGE;
    it->img = img;
    it->w = w;
    it->h = h;
    it->x = cx - w / 2;
    it->y = cy - h / 2;
    it->rotation = rot;
    it->opacity = g->fillA;
}

// ------------------------------------------------------------------ Texto

typedef struct {
    char *text;
    int len, cap;
    PFont *font;
    float size;            // px
    unsigned int color;
    float x0, x1, base;    // en px
    float rot;
    bool active;
} TextRun;

typedef struct {
    TextRun *lines;
    int count, cap;
} Lines;

static Lines g_lines;
static TextRun g_run;

static void end_run(void)
{
    if (!g_run.active) return;
    g_run.active = false;
    // Quitamos espacios de los extremos
    while (g_run.len > 0 && g_run.text[g_run.len - 1] == ' ') g_run.text[--g_run.len] = 0;
    int lead = 0;
    while (lead < g_run.len && g_run.text[lead] == ' ') lead++;
    if (g_run.len - lead <= 0) { free(g_run.text); g_run.text = NULL; return; }
    if (lead) { memmove(g_run.text, g_run.text + lead, g_run.len - lead + 1); g_run.len -= lead; }
    if (g_lines.count == g_lines.cap) { g_lines.cap = g_lines.cap ? g_lines.cap * 2 : 64; g_lines.lines = (TextRun *)realloc(g_lines.lines, sizeof(TextRun) * g_lines.cap); }
    g_lines.lines[g_lines.count++] = g_run;
    g_run.text = NULL;
}

static void run_add_cp(int cp)
{
    char u[5];
    int n = 0;
    if (cp < 0x80) u[n++] = (char)cp;
    else if (cp < 0x800) { u[n++] = (char)(0xC0 | (cp >> 6)); u[n++] = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { u[n++] = (char)(0xE0 | (cp >> 12)); u[n++] = (char)(0x80 | ((cp >> 6) & 63)); u[n++] = (char)(0x80 | (cp & 63)); }
    else { u[n++] = (char)(0xF0 | (cp >> 18)); u[n++] = (char)(0x80 | ((cp >> 12) & 63)); u[n++] = (char)(0x80 | ((cp >> 6) & 63)); u[n++] = (char)(0x80 | (cp & 63)); }
    str_append(&g_run.text, &g_run.len, &g_run.cap, u, n);
}

static void show_text(Ctx *c, GState *g, const unsigned char *s, int len)
{
    PFont *f = g->font;
    if (!f) return;
    int step = f->twoByte ? 2 : 1;
    // Matriz de texto completa
    for (int i = 0; i + step <= len; i += step) {
        int code = step == 2 ? (s[i] << 8 | s[i + 1]) : s[i];
        Mat trm0 = { g->fontSize * g->th, 0, 0, g->fontSize, 0, g->rise };
        Mat trm = mat_mul(mat_mul(trm0, c->tm), g->ctm);
        float wid = (f->widths && code < f->widthsSize) ? f->widths[code] : f->defWidth;
        float adv = (wid / 1000.0f * g->fontSize + g->tc + ((step == 1 && code == 32) ? g->tw : 0)) * g->th;
        // Posición en el lienzo
        double ux = trm.e, uy = trm.f;
        float px = (float)((ux - c->ox) * c->scale), py = (float)((c->pageHpt - (uy - c->oy)) * c->scale);
        float sizePx = (float)sqrt(trm.c * trm.c + trm.d * trm.d) * c->scale;
        float rot = (float)(atan2(-trm.b, trm.a) * RAD2DEG);
        int cp = (f->uni && code < f->uniSize) ? f->uni[code] : 0;
        if (!cp && !f->uni) cp = code;   // sin tabla: probamos con el código tal cual
        float advPx = (float)(adv * sqrt(c->tm.a * c->tm.a + c->tm.b * c->tm.b) * sqrt(g->ctm.a * g->ctm.a + g->ctm.b * g->ctm.b)) * c->scale;
        float rgb[3];
        color_to_rgb(g->fill, g->fillN, rgb);
        unsigned int col = pack(rgb);
        if (g->render != 3 && cp) {
            bool same = g_run.active && g_run.font == f && fabsf(g_run.size - sizePx) < sizePx * 0.05f && g_run.color == col &&
                        fabsf(g_run.base - py) < sizePx * 0.25f && fabsf(g_run.rot - rot) < 1 &&
                        px > g_run.x1 - sizePx * 0.6f && px < g_run.x1 + sizePx * 3.0f;
            if (fabsf(rot) > 1) same = same && fabsf(px - g_run.x1) < sizePx * 0.3f;
            if (!same) {
                end_run();
                g_run.active = true;
                g_run.font = f;
                g_run.size = sizePx;
                g_run.color = col;
                g_run.x0 = px;
                g_run.x1 = px;
                g_run.base = py;
                g_run.rot = rot;
                g_run.len = 0;
                g_run.cap = 0;
                g_run.text = NULL;
            } else if (px - g_run.x1 > sizePx * 0.18f && g_run.len > 0 && g_run.text[g_run.len - 1] != ' ' && cp != ' ') {
                run_add_cp(' ');
            }
            if (cp >= 32 || cp == 9) {
                if (cp == 0xFB01) { run_add_cp('f'); run_add_cp('i'); }
                else if (cp == 0xFB02) { run_add_cp('f'); run_add_cp('l'); }
                else run_add_cp(cp == 9 ? ' ' : cp);
            }
            g_run.x1 = px + advPx;
        }
        // Avanzar
        Mat t = { 1, 0, 0, 1, adv, 0 };
        c->tm = mat_mul(t, c->tm);
    }
}

// Junta líneas en párrafos y crea las piezas de texto
static void build_paragraphs(PdfResult *out)
{
    bool *used = (bool *)calloc(g_lines.count + 1, sizeof(bool));
    for (int i = 0; i < g_lines.count; i++) {
        if (used[i]) continue;
        used[i] = true;
        TextRun *first = &g_lines.lines[i];
        int members[200];
        int nm = 0;
        members[nm++] = i;
        TextRun *last = first;
        float spacing = 0;
        for (int j = i + 1; j < g_lines.count && nm < 200; j++) {
            if (used[j]) continue;
            TextRun *l = &g_lines.lines[j];
            if (l->font != first->font || fabsf(l->size - first->size) > first->size * 0.06f || l->color != first->color) continue;
            if (fabsf(l->rot) > 1 || fabsf(first->rot) > 1) continue;
            float dy = l->base - last->base;
            if (dy < first->size * 0.75f || dy > first->size * 2.2f) continue;
            if (spacing > 0 && fabsf(dy - spacing) > first->size * 0.25f) continue;
            // Que se solapen en horizontal
            float ox = fminf(l->x1, last->x1) - fmaxf(l->x0, last->x0);
            if (ox < -first->size * 0.5f) continue;
            spacing = dy;
            members[nm++] = j;
            used[j] = true;
            last = l;
        }
        // Alineación
        float minL = 1e9f, maxR = -1e9f, maxW = 0;
        for (int k = 0; k < nm; k++) {
            TextRun *l = &g_lines.lines[members[k]];
            minL = fminf(minL, l->x0);
            maxR = fmaxf(maxR, l->x1);
            maxW = fmaxf(maxW, l->x1 - l->x0);
        }
        int align = 0;
        if (nm > 1) {
            float dl = 0, dc = 0, dr = 0;
            float mc = (minL + maxR) / 2;
            for (int k = 0; k < nm; k++) {
                TextRun *l = &g_lines.lines[members[k]];
                dl = fmaxf(dl, fabsf(l->x0 - minL));
                dr = fmaxf(dr, fabsf(l->x1 - maxR));
                dc = fmaxf(dc, fabsf((l->x0 + l->x1) / 2 - mc));
            }
            if (dc < dl && dc <= dr) align = 1;
            else if (dr < dl) align = 2;
        } else {
            // Una sola línea: si está centrada en la página, la centramos
            float mc = (first->x0 + first->x1) / 2;
            if (fabsf(mc - out->pageW / 2) < out->pageW * 0.03f) align = 1;
        }
        PdfItem *it = add_item(out);
        it->kind = PDF_TEXT;
        int total = 0;
        for (int k = 0; k < nm; k++) total += g_lines.lines[members[k]].len + 1;
        it->text = (char *)malloc(total + 1);
        int pos = 0;
        for (int k = 0; k < nm; k++) {
            TextRun *l = &g_lines.lines[members[k]];
            memcpy(it->text + pos, l->text, l->len);
            pos += l->len;
            if (k + 1 < nm) it->text[pos++] = '\n';
        }
        it->text[pos] = 0;
        PFont *f = first->font;
        snprintf(it->font, sizeof(it->font), "%s", f->name);
        it->bold = f->bold;
        it->italic = f->italic;
        it->serif = f->serif;
        it->script = f->script;
        it->fontSize = first->size;
        it->lineSpacing = nm > 1 ? spacing / first->size : 0;
        it->align = align;
        it->color = (Color){ (unsigned char)(first->color >> 16), (unsigned char)(first->color >> 8), (unsigned char)first->color, 255 };
        it->baseline = first->base;
        it->x = minL;
        it->w = maxW;
        it->rotation = first->rot != 0 ? -first->rot : 0;
        it->y = first->base;
        it->h = (nm - 1) * spacing + first->size;
    }
    free(used);
    for (int i = 0; i < g_lines.count; i++) free(g_lines.lines[i].text);
    free(g_lines.lines);
    g_lines.lines = NULL;
    g_lines.count = g_lines.cap = 0;
}

// ------------------------------------------------------------------ Intérprete

#define MAX_STACK 64
#define MAX_GS 64

static void run_content(Ctx *c, const unsigned char *data, int len, Obj resources, GState *gs0);

static void set_clip(Ctx *c, GState *g)
{
    if (!c->pathAny) return;
    g->cx0 = fmaxf(g->cx0, c->px0);
    g->cy0 = fmaxf(g->cy0, c->py0);
    g->cx1 = fminf(g->cx1, c->px1);
    g->cy1 = fminf(g->cy1, c->py1);
}

static void do_xobject(Ctx *c, GState *g, Obj resources, const char *name)
{
    Obj xo = dget(dget(resources, "XObject"), name);
    if (!xo.stream) return;
    Obj sub = dget(xo, "Subtype");
    if (is_name(sub, "Image")) { place_image(c, g, xo, resources); return; }
    if (!is_name(sub, "Form") || c->depth > 12) return;
    GState ng = *g;
    Obj m = dget(xo, "Matrix");
    if (m.type == O_ARR && m.n == 6) {
        Mat fm = { resolve(m.v[0]).num, resolve(m.v[1]).num, resolve(m.v[2]).num, resolve(m.v[3]).num, resolve(m.v[4]).num, resolve(m.v[5]).num };
        ng.ctm = mat_mul(fm, g->ctm);
    }
    Obj res = dget(xo, "Resources");
    if (res.type != O_DICT) res = resources;
    int len;
    unsigned char *d = stream_data(xo, &len, NULL);
    if (!d) return;
    c->depth++;
    c->qdepth++;
    run_content(c, d, len, res, &ng);
    c->qdepth--;
    if (c->shapeAny && c->qdepth < c->shapeQ) flush_shapes(c);
    c->depth--;
    free(d);
}

static void run_content(Ctx *c, const unsigned char *data, int len, Obj resources, GState *gs0)
{
    GState stack[MAX_GS];
    int sp = 0;
    GState g = *gs0;
    Obj ops[MAX_STACK];
    int n = 0;
    Lex l = { data, data + len };
    int startDepth = 0;
    (void)startDepth;
    for (;;) {
        Obj t = read_token(&l);
        if (l.p >= l.end && t.type == O_NULL) break;
        if (t.type == O_OP && (strcmp(t.s, "[") == 0 || strcmp(t.s, "<<") == 0)) t = parse_from(&l, t, 0);
        if (t.type != O_OP) {
            if (n < MAX_STACK) ops[n++] = t;
            continue;
        }
        const char *op = t.s;
#define NUM(i) (n > (i) ? (float)ops[i].num : 0.0f)
        if (strcmp(op, "q") == 0) {
            if (sp < MAX_GS) stack[sp++] = g;
            c->qdepth++;
        } else if (strcmp(op, "Q") == 0) {
            if (sp > 0) { g = stack[--sp]; c->qdepth--; }
            // Termina el bloque donde empezó el dibujo: es una pieza
            if (c->shapeAny && c->qdepth < c->shapeQ) flush_shapes(c);
        } else if (strcmp(op, "cm") == 0 && n >= 6) {
            Mat m = { NUM(0), NUM(1), NUM(2), NUM(3), NUM(4), NUM(5) };
            g.ctm = mat_mul(m, g.ctm);
        } else if (strcmp(op, "w") == 0 && n >= 1) g.lineW = NUM(0);
        else if (strcmp(op, "g") == 0 && n >= 1) { g.fill[0] = NUM(0); g.fillN = 1; g.fillPattern = false; }
        else if (strcmp(op, "G") == 0 && n >= 1) { g.stroke[0] = NUM(0); g.strokeN = 1; }
        else if (strcmp(op, "rg") == 0 && n >= 3) { g.fill[0] = NUM(0); g.fill[1] = NUM(1); g.fill[2] = NUM(2); g.fillN = 3; g.fillPattern = false; }
        else if (strcmp(op, "RG") == 0 && n >= 3) { g.stroke[0] = NUM(0); g.stroke[1] = NUM(1); g.stroke[2] = NUM(2); g.strokeN = 3; }
        else if (strcmp(op, "k") == 0 && n >= 4) { for (int i = 0; i < 3; i++) g.fill[i] = 0; float v[4] = { NUM(0), NUM(1), NUM(2), NUM(3) }; float rgb[3]; color_to_rgb(v, 4, rgb); memcpy(g.fill, rgb, sizeof(rgb)); g.fillN = 3; g.fillPattern = false; }
        else if (strcmp(op, "K") == 0 && n >= 4) { float v[4] = { NUM(0), NUM(1), NUM(2), NUM(3) }; float rgb[3]; color_to_rgb(v, 4, rgb); memcpy(g.stroke, rgb, sizeof(rgb)); g.strokeN = 3; }
        else if (strcmp(op, "cs") == 0 && n >= 1) {
            g.fillPattern = is_name(ops[0], "Pattern");
            if (!g.fillPattern && ops[0].type == O_NAME) {
                Obj cs = dget(dget(resources, "ColorSpace"), ops[0].s);
                if (cs.type == O_ARR && cs.n > 0 && is_name(resolve(cs.v[0]), "Pattern")) g.fillPattern = true;
            }
        } else if ((strcmp(op, "sc") == 0 || strcmp(op, "scn") == 0)) {
            int k = 0;
            float v[4];
            for (int i = 0; i < n && k < 4; i++) if (ops[i].type == O_NUM) v[k++] = (float)ops[i].num;
            if (n > 0 && ops[n - 1].type == O_NAME) g.fillPattern = true;
            else if (k > 0) { float rgb[3]; color_to_rgb(v, k == 2 ? 1 : k, rgb); memcpy(g.fill, rgb, sizeof(rgb)); g.fillN = 3; g.fillPattern = false; }
        } else if ((strcmp(op, "SC") == 0 || strcmp(op, "SCN") == 0)) {
            int k = 0;
            float v[4];
            for (int i = 0; i < n && k < 4; i++) if (ops[i].type == O_NUM) v[k++] = (float)ops[i].num;
            if (k > 0) { float rgb[3]; color_to_rgb(v, k == 2 ? 1 : k, rgb); memcpy(g.stroke, rgb, sizeof(rgb)); g.strokeN = 3; }
        } else if (strcmp(op, "gs") == 0 && n >= 1 && ops[0].type == O_NAME) {
            Obj e = dget(dget(resources, "ExtGState"), ops[0].s);
            Obj ca = dget(e, "ca"), CA = dget(e, "CA");
            if (ca.type == O_NUM) g.fillA = (float)ca.num;
            if (CA.type == O_NUM) g.strokeA = (float)CA.num;
            Obj lw = dget(e, "LW");
            if (lw.type == O_NUM) g.lineW = (float)lw.num;
        }
        // Caminos
        else if (strcmp(op, "m") == 0 && n >= 2) {
            Vector2 p = dev(c, &g, NUM(0), NUM(1));
            str_printf(&c->path, &c->pathLen, &c->pathCap, "M%.2f %.2f", p.x, p.y);
            path_point(c, p);
        } else if (strcmp(op, "l") == 0 && n >= 2) {
            Vector2 p = dev(c, &g, NUM(0), NUM(1));
            str_printf(&c->path, &c->pathLen, &c->pathCap, "L%.2f %.2f", p.x, p.y);
            path_point(c, p);
            c->pathRect = false;
        } else if (strcmp(op, "c") == 0 && n >= 6) {
            Vector2 a = dev(c, &g, NUM(0), NUM(1)), b = dev(c, &g, NUM(2), NUM(3)), d = dev(c, &g, NUM(4), NUM(5));
            str_printf(&c->path, &c->pathLen, &c->pathCap, "C%.2f %.2f %.2f %.2f %.2f %.2f", a.x, a.y, b.x, b.y, d.x, d.y);
            path_point(c, a); path_point(c, b); path_point(c, d);
            c->pathRect = false;
        } else if (strcmp(op, "v") == 0 && n >= 4) {
            // Primer punto de control = punto actual: lo aproximamos con S (suave) no; usamos Q
            Vector2 b = dev(c, &g, NUM(0), NUM(1)), d = dev(c, &g, NUM(2), NUM(3));
            str_printf(&c->path, &c->pathLen, &c->pathCap, "Q%.2f %.2f %.2f %.2f", b.x, b.y, d.x, d.y);
            path_point(c, b); path_point(c, d);
            c->pathRect = false;
        } else if (strcmp(op, "y") == 0 && n >= 4) {
            Vector2 a = dev(c, &g, NUM(0), NUM(1)), d = dev(c, &g, NUM(2), NUM(3));
            str_printf(&c->path, &c->pathLen, &c->pathCap, "C%.2f %.2f %.2f %.2f %.2f %.2f", a.x, a.y, d.x, d.y, d.x, d.y);
            path_point(c, a); path_point(c, d);
            c->pathRect = false;
        } else if (strcmp(op, "h") == 0) {
            str_append(&c->path, &c->pathLen, &c->pathCap, "Z", 1);
        } else if (strcmp(op, "re") == 0 && n >= 4) {
            float x = NUM(0), y = NUM(1), w = NUM(2), h = NUM(3);
            Vector2 a = dev(c, &g, x, y), b = dev(c, &g, x + w, y), cc = dev(c, &g, x + w, y + h), d = dev(c, &g, x, y + h);
            str_printf(&c->path, &c->pathLen, &c->pathCap, "M%.2f %.2fL%.2f %.2fL%.2f %.2fL%.2f %.2fZ", a.x, a.y, b.x, b.y, cc.x, cc.y, d.x, d.y);
            path_point(c, a); path_point(c, b); path_point(c, cc); path_point(c, d);
        } else if (strcmp(op, "f") == 0 || strcmp(op, "F") == 0) paint_path(c, &g, true, false, false);
        else if (strcmp(op, "f*") == 0) paint_path(c, &g, true, false, true);
        else if (strcmp(op, "S") == 0) paint_path(c, &g, false, true, false);
        else if (strcmp(op, "s") == 0) { str_append(&c->path, &c->pathLen, &c->pathCap, "Z", 1); paint_path(c, &g, false, true, false); }
        else if (strcmp(op, "B") == 0) paint_path(c, &g, true, true, false);
        else if (strcmp(op, "B*") == 0) paint_path(c, &g, true, true, true);
        else if (strcmp(op, "b") == 0) { str_append(&c->path, &c->pathLen, &c->pathCap, "Z", 1); paint_path(c, &g, true, true, false); }
        else if (strcmp(op, "b*") == 0) { str_append(&c->path, &c->pathLen, &c->pathCap, "Z", 1); paint_path(c, &g, true, true, true); }
        else if (strcmp(op, "W") == 0 || strcmp(op, "W*") == 0) set_clip(c, &g);
        else if (strcmp(op, "n") == 0) path_reset(c);
        // Imágenes y formularios
        else if (strcmp(op, "Do") == 0 && n >= 1 && ops[0].type == O_NAME) {
            Obj xo = dget(dget(resources, "XObject"), ops[0].s);
            if (!is_name(dget(xo, "Subtype"), "Image")) {
                // Cada formulario de primer nivel es una pieza aparte
                if (c->depth == 0) flush_shapes(c);
                do_xobject(c, &g, resources, ops[0].s);
                if (c->depth == 0) flush_shapes(c);
            } else do_xobject(c, &g, resources, ops[0].s);
        } else if (strcmp(op, "BI") == 0) {
            // Imagen dentro del contenido: la saltamos
            const unsigned char *q = l.p;
            while (q + 2 < l.end && !(is_ws(q[0]) && q[1] == 'E' && q[2] == 'I' && (q + 3 >= l.end || is_ws(q[3])))) q++;
            l.p = q + 3;
            c->out->skipped++;
        } else if (strcmp(op, "sh") == 0) {
            c->out->skipped++;
        }
        // Texto
        else if (strcmp(op, "BT") == 0) {
            flush_shapes(c);
            Mat id = { 1, 0, 0, 1, 0, 0 };
            c->tm = c->tlm = id;
        } else if (strcmp(op, "ET") == 0) {
        } else if (strcmp(op, "Tf") == 0 && n >= 2) {
            if (ops[0].type == O_NAME) g.font = load_font(dget_raw(dget(resources, "Font"), ops[0].s));
            g.fontSize = NUM(1);
        } else if (strcmp(op, "Tc") == 0 && n >= 1) g.tc = NUM(0);
        else if (strcmp(op, "Tw") == 0 && n >= 1) g.tw = NUM(0);
        else if (strcmp(op, "Tz") == 0 && n >= 1) g.th = NUM(0) / 100.0f;
        else if (strcmp(op, "TL") == 0 && n >= 1) g.tl = NUM(0);
        else if (strcmp(op, "Ts") == 0 && n >= 1) g.rise = NUM(0);
        else if (strcmp(op, "Tr") == 0 && n >= 1) g.render = (int)NUM(0);
        else if (strcmp(op, "Td") == 0 && n >= 2) {
            Mat t = { 1, 0, 0, 1, NUM(0), NUM(1) };
            c->tlm = mat_mul(t, c->tlm);
            c->tm = c->tlm;
        } else if (strcmp(op, "TD") == 0 && n >= 2) {
            g.tl = -NUM(1);
            Mat t = { 1, 0, 0, 1, NUM(0), NUM(1) };
            c->tlm = mat_mul(t, c->tlm);
            c->tm = c->tlm;
        } else if (strcmp(op, "Tm") == 0 && n >= 6) {
            Mat m = { NUM(0), NUM(1), NUM(2), NUM(3), NUM(4), NUM(5) };
            c->tlm = c->tm = m;
        } else if (strcmp(op, "T*") == 0) {
            Mat t = { 1, 0, 0, 1, 0, -g.tl };
            c->tlm = mat_mul(t, c->tlm);
            c->tm = c->tlm;
        } else if (strcmp(op, "Tj") == 0 && n >= 1 && ops[0].type == O_STR) {
            show_text(c, &g, (const unsigned char *)ops[0].s, ops[0].len);
        } else if (strcmp(op, "'") == 0 && n >= 1) {
            Mat t = { 1, 0, 0, 1, 0, -g.tl };
            c->tlm = mat_mul(t, c->tlm);
            c->tm = c->tlm;
            if (ops[0].type == O_STR) show_text(c, &g, (const unsigned char *)ops[0].s, ops[0].len);
        } else if (strcmp(op, "\"") == 0 && n >= 3) {
            g.tw = NUM(0);
            g.tc = NUM(1);
            Mat t = { 1, 0, 0, 1, 0, -g.tl };
            c->tlm = mat_mul(t, c->tlm);
            c->tm = c->tlm;
            if (ops[2].type == O_STR) show_text(c, &g, (const unsigned char *)ops[2].s, ops[2].len);
        } else if (strcmp(op, "TJ") == 0 && n >= 1 && ops[0].type == O_ARR) {
            Obj a = ops[0];
            for (int i = 0; i < a.n; i++) {
                if (a.v[i].type == O_STR) show_text(c, &g, (const unsigned char *)a.v[i].s, a.v[i].len);
                else if (a.v[i].type == O_NUM) {
                    float adv = -(float)a.v[i].num / 1000.0f * g.fontSize * g.th;
                    Mat t = { 1, 0, 0, 1, adv, 0 };
                    c->tm = mat_mul(t, c->tm);
                }
            }
        }
#undef NUM
        n = 0;
    }
}

// ------------------------------------------------------------------ Páginas

static int collect_pages(Obj node, Obj *pages, int max, int count, int depth)
{
    node = resolve(node);
    if (depth > 32 || count >= max) return count;
    Obj kids = dget(node, "Kids");
    if (kids.type == O_ARR) {
        for (int i = 0; i < kids.n && count < max; i++) count = collect_pages(kids.v[i], pages, max, count, depth + 1);
    } else if (node.type == O_DICT) {
        pages[count++] = node;
    }
    return count;
}

// Busca un valor heredado de las páginas padre (Resources, MediaBox)
static Obj inherited(Obj page, const char *key)
{
    for (int i = 0; i < 32 && page.type == O_DICT; i++) {
        Obj v = dget(page, key);
        if (v.type != O_NULL) return v;
        page = dget(page, "Parent");
    }
    return NULLOBJ;
}

bool pdf_import(const unsigned char *data, int size, int pageIndex, float pxPerPoint, PdfResult *out)
{
    memset(out, 0, sizeof(*out));
    if (size < 8 || memcmp(data, "%PDF", 4) != 0) return false;
    memset(&P, 0, sizeof(P));
    P.data = data;
    P.size = size;
    g_pfCount = 0;
    build_xref();

    // Catálogo
    Obj root = NULLOBJ;
    for (int i = size - 5; i >= 0; i--) {
        if (memcmp(data + i, "/Root", 5) == 0) {
            Lex l = { data + i + 5, data + size };
            Obj a = read_token(&l), b = read_token(&l), r = read_token(&l);
            if (a.type == O_NUM && b.type == O_NUM && r.type == O_OP && strcmp(r.s, "R") == 0) { root = get((int)a.num); break; }
        }
    }
    if (root.type != O_DICT) {
        for (int i = 1; i <= P.maxObj; i++) {
            if (!P.xref[i].offset && !P.xref[i].stm) continue;
            Obj o = get(i);
            if (is_name(dget(o, "Type"), "Catalog")) { root = o; break; }
        }
    }
    static Obj pages[2000];
    int np = collect_pages(dget(root, "Pages"), pages, 2000, 0, 0);
    out->pages = np;
    bool ok = false;
    if (np > 0) {
        if (pageIndex < 0 || pageIndex >= np) pageIndex = 0;
        Obj page = pages[pageIndex];
        Obj box = inherited(page, "CropBox");
        if (box.type != O_ARR || box.n < 4) box = inherited(page, "MediaBox");
        float bx0 = 0, by0 = 0, bx1 = 595, by1 = 842;
        if (box.type == O_ARR && box.n >= 4) {
            bx0 = (float)resolve(box.v[0]).num; by0 = (float)resolve(box.v[1]).num;
            bx1 = (float)resolve(box.v[2]).num; by1 = (float)resolve(box.v[3]).num;
        }
        float pw = fabsf(bx1 - bx0), ph = fabsf(by1 - by0);
        // Que no salga un lienzo gigantesco
        float scale = pxPerPoint;
        if (fmaxf(pw, ph) * scale > 4000) scale = 4000 / fmaxf(pw, ph);
        Ctx c;
        memset(&c, 0, sizeof(c));
        c.out = out;
        c.scale = scale;
        c.ox = fminf(bx0, bx1);
        c.oy = fminf(by0, by1);
        c.pageHpt = ph;
        out->pageW = roundf(pw * scale);
        out->pageH = roundf(ph * scale);
        path_reset(&c);
        GState g;
        memset(&g, 0, sizeof(g));
        g.ctm = (Mat){ 1, 0, 0, 1, 0, 0 };
        g.fillN = g.strokeN = 1;
        g.fillA = g.strokeA = 1;
        g.lineW = 1;
        g.th = 1;
        g.cx0 = -1e9f; g.cy0 = -1e9f; g.cx1 = 1e9f; g.cy1 = 1e9f;
        Obj res = inherited(page, "Resources");
        // Contenido (uno o varios flujos)
        Obj contents = dget(page, "Contents");
        unsigned char *all = NULL;
        int allLen = 0, allCap = 0;
        if (contents.type == O_ARR) {
            for (int i = 0; i < contents.n; i++) {
                int len;
                unsigned char *d = stream_data(resolve(contents.v[i]), &len, NULL);
                if (d) { str_append((char **)&all, &allLen, &allCap, (const char *)d, len); str_append((char **)&all, &allLen, &allCap, "\n", 1); free(d); }
            }
        } else if (contents.stream) {
            int len;
            unsigned char *d = stream_data(contents, &len, NULL);
            if (d) { all = d; allLen = len; }
        }
        if (all) {
            run_content(&c, all, allLen, res, &g);
            flush_shapes(&c);
            end_run();
            build_paragraphs(out);
            free(all);
            ok = true;
        }
        free(c.svg);
        free(c.path);
    }
    for (int i = 0; i < g_pfCount; i++) { free(g_pf[i].uni); free(g_pf[i].widths); }
    g_pfCount = 0;
    free(P.xref);
    free(P.cache);
    free(P.cached);
    free(P.busy);
    memset(&P, 0, sizeof(P));
    arena_free();
    return ok;
}

void pdf_free(PdfResult *r)
{
    for (int i = 0; i < r->count; i++) {
        if (r->items[i].img.data) UnloadImage(r->items[i].img);
        free(r->items[i].text);
    }
    free(r->items);
    memset(r, 0, sizeof(*r));
}
