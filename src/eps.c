// EPS -> SVG: un intérprete muy pequeño de PostScript que solo entiende lo necesario para
// dibujos vectoriales (trazados, colores, grupos y recortes). No ejecuta el programa entero
// (eso necesitaría Ghostscript): lee las órdenes de dibujo de la página, que en los EPS de
// Illustrator, Inkscape o CorelDRAW son siempre las mismas pocas palabras.
#include "eps.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *s; size_t len, cap; } Buf;

static void buf_add(Buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->len + n + 1) nc *= 2;
        char *ns = (char *)realloc(b->s, nc);
        if (!ns) return;
        b->s = ns;
        b->cap = nc;
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}

static void buf_printf(Buf *b, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n < (int)sizeof(tmp)) { buf_add(b, tmp, (size_t)n); return; }
    char *big = (char *)malloc((size_t)n + 1);
    if (!big) return;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    buf_add(b, big, (size_t)n);
    free(big);
}

typedef struct { double a, b, c, d, e, f; } Mat;

static Mat mat_mul(Mat m, Mat n) // primero m, luego n
{
    return (Mat){ m.a * n.a + m.b * n.c, m.a * n.b + m.b * n.d,
                  m.c * n.a + m.d * n.c, m.c * n.b + m.d * n.d,
                  m.e * n.a + m.f * n.c + n.e, m.e * n.b + m.f * n.d + n.f };
}

typedef struct {
    Mat ctm;
    double r, g, b;     // color actual (PostScript solo tiene uno para rellenar y para el contorno)
    double lw;
    int opened;         // <g> abiertos en este nivel (recortes)
} GState;

#define MAX_GS 64
#define MAX_STACK 256

typedef struct {
    const char *p, *end;
    double st[MAX_STACK];
    int sp;
    GState gs[MAX_GS];
    int gsp;
    Buf path;           // trazado actual (ya en coordenadas del SVG)
    double cx, cy;      // punto actual (sin transformar)
    double sx, sy;      // inicio del subtrazado (sin transformar)
    bool hasCur;
    Buf out;
    int clipId;
    int skipped;
    int shapes;
    // Alias del prólogo: /m {moveto} bind def
    struct { char name[16]; char op[16]; } alias[64];
    int nalias;
} Eps;

static void pt(Eps *E, double x, double y, double *ox, double *oy)
{
    Mat m = E->gs[E->gsp].ctm;
    *ox = m.a * x + m.c * y + m.e;
    *oy = m.b * x + m.d * y + m.f;
}

static void path_cmd(Eps *E, char cmd, int n, const double *xy)
{
    char tmp[200];
    int len = snprintf(tmp, sizeof(tmp), "%c", cmd);
    for (int i = 0; i < n; i++) {
        double X, Y;
        pt(E, xy[i * 2], xy[i * 2 + 1], &X, &Y);
        len += snprintf(tmp + len, sizeof(tmp) - len, "%s%.2f %.2f", i ? " " : "", X, Y);
    }
    buf_add(&E->path, tmp, (size_t)len);
}

static void color_hex(const Eps *E, char *out)
{
    const GState *g = &E->gs[E->gsp];
    int r = (int)lround(fmax(0, fmin(1, g->r)) * 255), gg = (int)lround(fmax(0, fmin(1, g->g)) * 255), b = (int)lround(fmax(0, fmin(1, g->b)) * 255);
    snprintf(out, 8, "#%02x%02x%02x", r, gg, b);
}

// CMYK de imprenta -> RGB de pantalla. La cuenta sencilla (1-c)(1-k) da colores demasiado
// vivos; esta fórmula imita el perfil de imprenta habitual (la misma aproximación que usa
// el visor de PDF de Firefox, pdf.js).
static void set_cmyk(Eps *E, double c, double m, double y, double k)
{
    GState *g = &E->gs[E->gsp];
    c = fmax(0, fmin(1, c)); m = fmax(0, fmin(1, m)); y = fmax(0, fmin(1, y)); k = fmax(0, fmin(1, k));
    double r = 255 + c * (-4.387332384609988 * c + 54.48615194189176 * m + 18.82290502165302 * y + 212.25662451639585 * k - 285.2331026137004) +
               m * (1.7149763477362134 * m - 5.6096736904047315 * y - 17.873870861415444 * k - 5.497006427196366) +
               y * (-2.5217340131683033 * y - 21.248923337353073 * k + 17.5119270841813) + k * (-21.86122147463605 * k - 189.48180835922747);
    double gg = 255 + c * (8.841041422036149 * c + 60.118027045597366 * m + 6.871425592049007 * y + 31.159100130055922 * k - 79.2970844816548) +
                m * (-15.310361306967817 * m + 17.575251261109482 * y + 131.35250912493976 * k - 190.9453302588951) +
                y * (4.444339102852739 * y + 9.8632861493405 * k - 24.86741582555878) + k * (-20.737325471181034 * k - 187.80453709719578);
    double b = 255 + c * (0.8842522430003296 * c + 8.078677503112928 * m + 30.89978309703729 * y - 0.23883238689178934 * k - 14.183576799673286) +
               m * (10.49593273432072 * m + 63.02378494754052 * y + 50.606957656360734 * k - 112.23884253719248) +
               y * (0.03296041114873217 * y + 115.60384449646641 * k - 193.58209356861505) + k * (-22.33816807309886 * k - 180.12613974708367);
    g->r = r / 255; g->g = gg / 255; g->b = b / 255;
}

static void paint(Eps *E, bool fill, bool stroke, bool evenodd)
{
    if (E->path.len == 0) return;
    char col[8];
    color_hex(E, col);
    if (fill) {
        buf_printf(&E->out, "<path d=\"%s\" fill=\"%s\"%s/>\n", E->path.s, col, evenodd ? " fill-rule=\"evenodd\"" : "");
        E->shapes++;
    }
    if (stroke) {
        Mat m = E->gs[E->gsp].ctm;
        double scale = sqrt(fabs(m.a * m.d - m.b * m.c));
        double w = E->gs[E->gsp].lw * scale;
        if (w < 0.3) w = 0.3;
        buf_printf(&E->out, "<path d=\"%s\" fill=\"none\" stroke=\"%s\" stroke-width=\"%.2f\" stroke-linejoin=\"round\" stroke-linecap=\"round\"/>\n", E->path.s, col, w);
        E->shapes++;
    }
}

static void new_path(Eps *E)
{
    E->path.len = 0;
    if (E->path.s) E->path.s[0] = 0;
    E->hasCur = false;
}

static void clip(Eps *E, bool evenodd)
{
    if (E->path.len == 0) return;
    E->clipId++;
    buf_printf(&E->out, "<clipPath id=\"c%d\"><path d=\"%s\"%s/></clipPath>\n<g clip-path=\"url(#c%d)\">\n",
               E->clipId, E->path.s, evenodd ? " clip-rule=\"evenodd\"" : "", E->clipId);
    E->gs[E->gsp].opened++;
}

static void gsave(Eps *E)
{
    if (E->gsp >= MAX_GS - 1) return;
    E->gs[E->gsp + 1] = E->gs[E->gsp];
    E->gsp++;
    E->gs[E->gsp].opened = 1;
    buf_add(&E->out, "<g>\n", 4);
}

static void grestore(Eps *E)
{
    if (E->gsp <= 0) return;
    for (int i = 0; i < E->gs[E->gsp].opened; i++) buf_add(&E->out, "</g>\n", 5);
    E->gsp--;
}

static bool pop(Eps *E, int n, double *v)
{
    if (E->sp < n) { E->sp = 0; return false; }
    for (int i = 0; i < n; i++) v[i] = E->st[E->sp - n + i];
    E->sp -= n;
    return true;
}

static void concat(Eps *E, Mat m)
{
    GState *g = &E->gs[E->gsp];
    g->ctm = mat_mul(m, g->ctm);
}

static void rect_path(Eps *E, double x, double y, double w, double h)
{
    double p[2] = { x, y };
    path_cmd(E, 'M', 1, p);
    p[0] = x + w; path_cmd(E, 'L', 1, p);
    p[1] = y + h; path_cmd(E, 'L', 1, p);
    p[0] = x; path_cmd(E, 'L', 1, p);
    buf_add(&E->path, "Z", 1);
    E->cx = E->sx = x; E->cy = E->sy = y;
    E->hasCur = true;
}

// Ejecuta una palabra. Devuelve false si no la conoce.
static bool exec_op(Eps *E, const char *op)
{
    double v[8];
    // Órdenes de Illustrator antiguo (AI8) en mayúscula/minúscula tienen el mismo sentido
#define IS(x) (strcmp(op, x) == 0)
    if (IS("moveto") || IS("mo") || IS("m")) {
        if (!pop(E, 2, v)) return true;
        path_cmd(E, 'M', 1, v);
        E->cx = E->sx = v[0]; E->cy = E->sy = v[1];
        E->hasCur = true;
    } else if (IS("lineto") || IS("li") || IS("l") || IS("L")) {
        if (!pop(E, 2, v)) return true;
        path_cmd(E, E->hasCur ? 'L' : 'M', 1, v);
        E->cx = v[0]; E->cy = v[1];
        E->hasCur = true;
    } else if (IS("rmoveto") || IS("rlineto")) {
        if (!pop(E, 2, v)) return true;
        v[0] += E->cx; v[1] += E->cy;
        path_cmd(E, IS("rmoveto") || !E->hasCur ? 'M' : 'L', 1, v);
        if (IS("rmoveto")) { E->sx = v[0]; E->sy = v[1]; }
        E->cx = v[0]; E->cy = v[1];
        E->hasCur = true;
    } else if (IS("curveto") || IS("cv") || IS("c") || IS("C")) {
        if (!pop(E, 6, v)) return true;
        path_cmd(E, 'C', 3, v);
        E->cx = v[4]; E->cy = v[5];
    } else if (IS("rcurveto")) {
        if (!pop(E, 6, v)) return true;
        for (int i = 0; i < 6; i += 2) { v[i] += E->cx; v[i + 1] += E->cy; }
        path_cmd(E, 'C', 3, v);
        E->cx = v[4]; E->cy = v[5];
    } else if (IS("v") || IS("V")) {
        // Primer punto de control = punto actual
        double w[6];
        if (!pop(E, 4, v)) return true;
        w[0] = E->cx; w[1] = E->cy; w[2] = v[0]; w[3] = v[1]; w[4] = v[2]; w[5] = v[3];
        path_cmd(E, 'C', 3, w);
        E->cx = v[2]; E->cy = v[3];
    } else if (IS("y") || IS("Y")) {
        // Segundo punto de control = punto final
        double w[6];
        if (!pop(E, 4, v)) return true;
        w[0] = v[0]; w[1] = v[1]; w[2] = v[2]; w[3] = v[3]; w[4] = v[2]; w[5] = v[3];
        path_cmd(E, 'C', 3, w);
        E->cx = v[2]; E->cy = v[3];
    } else if (IS("closepath") || IS("cp") || IS("h") || IS("H")) {
        if (E->path.len) buf_add(&E->path, "Z", 1);
        E->cx = E->sx; E->cy = E->sy;
    } else if (IS("fill") || IS("f") || IS("F")) {
        if (IS("f") && E->path.len) buf_add(&E->path, "Z", 1);
        paint(E, true, false, false);
        new_path(E);
    } else if (IS("eofill") || IS("ef")) {
        paint(E, true, false, true);
        new_path(E);
    } else if (IS("stroke") || IS("@") || IS("S")) {
        paint(E, false, true, false);
        new_path(E);
    } else if (IS("s")) {
        if (E->path.len) buf_add(&E->path, "Z", 1);
        paint(E, false, true, false);
        new_path(E);
    } else if (IS("b") || IS("B")) {
        if (IS("b") && E->path.len) buf_add(&E->path, "Z", 1);
        paint(E, true, true, false);
        new_path(E);
    } else if (IS("newpath") || IS("np") || IS("n") || IS("N")) {
        new_path(E);
    } else if (IS("clip") || IS("W")) {
        clip(E, false);
    } else if (IS("eoclip")) {
        clip(E, true);
    } else if (IS("clp") || IS("eoclp")) {
        // Los de Illustrator recortan y empiezan un trazado nuevo
        clip(E, IS("eoclp"));
        new_path(E);
    } else if (IS("rectfill") || IS("rectstroke") || IS("rectclip") || IS("re")) {
        if (!pop(E, 4, v)) return true;
        if (!IS("re")) new_path(E);
        rect_path(E, v[0], v[1], v[2], v[3]);
        if (IS("rectfill")) { paint(E, true, false, false); new_path(E); }
        else if (IS("rectstroke")) { paint(E, false, true, false); new_path(E); }
        else if (IS("rectclip")) { clip(E, false); new_path(E); }
    } else if (IS("gsave") || IS("q") || IS("pgsv") || IS("u")) {
        gsave(E);
    } else if (IS("grestore") || IS("Q") || IS("pgrs") || IS("U")) {
        grestore(E);
    } else if (IS("concat") || IS("ct")) {
        if (!pop(E, 6, v)) return true;
        concat(E, (Mat){ v[0], v[1], v[2], v[3], v[4], v[5] });
    } else if (IS("cm")) {
        if (!pop(E, 6, v)) return true;
        concat(E, (Mat){ v[0], v[1], v[2], v[3], v[4], v[5] });
    } else if (IS("translate")) {
        if (!pop(E, 2, v)) return true;
        concat(E, (Mat){ 1, 0, 0, 1, v[0], v[1] });
    } else if (IS("scale")) {
        if (!pop(E, 2, v)) return true;
        concat(E, (Mat){ v[0], 0, 0, v[1], 0, 0 });
    } else if (IS("rotate")) {
        if (!pop(E, 1, v)) return true;
        double a = v[0] * 3.14159265358979 / 180;
        concat(E, (Mat){ cos(a), sin(a), -sin(a), cos(a), 0, 0 });
    } else if (IS("setrgbcolor") || IS("rgb") || IS("rg") || IS("RG") || IS("Xa") || IS("XA")) {
        if (!pop(E, 3, v)) return true;
        E->gs[E->gsp].r = v[0]; E->gs[E->gsp].g = v[1]; E->gs[E->gsp].b = v[2];
    } else if (IS("setcmykcolor") || IS("cmyk") || IS("k") || IS("K")) {
        if (!pop(E, 4, v)) return true;
        set_cmyk(E, v[0], v[1], v[2], v[3]);
    } else if (IS("x") || IS("X")) {
        // Color de muestra de Illustrator antiguo: c m y k (nombre) tinte tipo
        if (E->sp >= 6) {
            pop(E, 6, v);
            double t = v[4];
            set_cmyk(E, v[0] * t, v[1] * t, v[2] * t, v[3] * t);
        } else E->sp = 0;
    } else if (IS("setgray") || IS("gray") || IS("g") || IS("G")) {
        if (!pop(E, 1, v)) return true;
        // En Illustrator antiguo "g" es gris como en PostScript (1 = blanco)
        E->gs[E->gsp].r = E->gs[E->gsp].g = E->gs[E->gsp].b = v[0];
    } else if (IS("setlinewidth") || IS("lw") || IS("w")) {
        if (!pop(E, 1, v)) return true;
        E->gs[E->gsp].lw = v[0];
    } else if (IS("shfill") || IS("sh") || IS("image") || IS("colorimage") || IS("imagemask")) {
        E->skipped++;
        E->sp = 0;
    } else {
#undef IS
        return false;
    }
    return true;
}

static bool is_delim(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0 || c == '(' || c == ')' || c == '<' || c == '>' ||
           c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
}

// Salta un bloque { ... } (procedimientos: no los ejecutamos)
static void skip_proc(Eps *E)
{
    int depth = 1;
    while (E->p < E->end && depth > 0) {
        char c = *E->p++;
        if (c == '{') depth++;
        else if (c == '}') depth--;
        else if (c == '%') { while (E->p < E->end && *E->p != '\n' && *E->p != '\r') E->p++; }
        else if (c == '(') {
            int d = 1;
            while (E->p < E->end && d > 0) {
                char k = *E->p++;
                if (k == '\\') E->p++;
                else if (k == '(') d++;
                else if (k == ')') d--;
            }
        }
    }
}

static void run(Eps *E)
{
    while (E->p < E->end) {
        char c = *E->p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0) { E->p++; continue; }
        if (c == '%') {
            // Comentario; dentro puede venir un bloque de datos que hay que saltar entero
            const char *ls = E->p;
            while (E->p < E->end && *E->p != '\n' && *E->p != '\r') E->p++;
            size_t n = (size_t)(E->p - ls);
            if ((n >= 11 && strncmp(ls, "%%BeginData", 11) == 0) || (n >= 13 && strncmp(ls, "%%BeginBinary", 13) == 0)) {
                const char *endTag = ls[2] == 'B' && ls[7] == 'D' ? "%%EndData" : "%%EndBinary";
                const char *q = E->p;
                size_t tl = strlen(endTag);
                while (q + tl <= E->end && strncmp(q, endTag, tl) != 0) q++;
                E->p = q;
            }
            continue;
        }
        if (c == '(') {
            E->p++;
            int d = 1;
            while (E->p < E->end && d > 0) {
                char k = *E->p++;
                if (k == '\\') E->p++;
                else if (k == '(') d++;
                else if (k == ')') d--;
            }
            continue;
        }
        if (c == '<') {
            E->p++;
            if (E->p < E->end && *E->p == '<') { E->p++; continue; } // diccionario: lo ignoramos
            while (E->p < E->end && *E->p != '>') E->p++;           // texto en hexadecimal
            if (E->p < E->end) E->p++;
            continue;
        }
        if (c == '>') { E->p++; if (E->p < E->end && *E->p == '>') E->p++; continue; }
        if (c == '{') { E->p++; skip_proc(E); E->sp = 0; continue; }
        if (c == '}' || c == '[' || c == ']') { E->p++; continue; }
        if (c == '/') {
            E->p++;
            while (E->p < E->end && !is_delim(*E->p)) E->p++;
            continue;
        }
        // Número o palabra
        const char *s = E->p;
        while (E->p < E->end && !is_delim(*E->p)) E->p++;
        size_t n = (size_t)(E->p - s);
        if (n == 0) { E->p++; continue; }
        char word[64];
        if (n >= sizeof(word)) { E->sp = 0; continue; }
        memcpy(word, s, n);
        word[n] = 0;
        char *endp;
        double num = strtod(word, &endp);
        if (*endp == 0 && (isdigit((unsigned char)word[0]) || word[0] == '-' || word[0] == '+' || word[0] == '.')) {
            if (E->sp < MAX_STACK) E->st[E->sp++] = num;
            else { memmove(E->st, E->st + 1, sizeof(double) * (MAX_STACK - 1)); E->st[MAX_STACK - 1] = num; }
            continue;
        }
        const char *op = word;
        for (int i = 0; i < E->nalias; i++)
            if (strcmp(E->alias[i].name, word) == 0) { op = E->alias[i].op; break; }
        if (!exec_op(E, op)) E->sp = 0; // palabra desconocida: no sabemos qué coge, empezamos de cero
    }
}

static const char *find(const char *p, const char *end, const char *s)
{
    size_t n = strlen(s);
    for (; p + n <= end; p++)
        if (*p == *s && memcmp(p, s, n) == 0) return p;
    return NULL;
}

// Busca en el prólogo definiciones sencillas como "/m {moveto} bind def" o "/l /lineto load def"
static void read_aliases(Eps *E, const char *p, const char *end)
{
    static const char *KNOWN[] = { "moveto", "lineto", "curveto", "closepath", "fill", "eofill", "stroke", "newpath",
                                   "clip", "eoclip", "gsave", "grestore", "concat", "translate", "scale", "rotate",
                                   "setrgbcolor", "setcmykcolor", "setgray", "setlinewidth", "rmoveto", "rlineto",
                                   "rcurveto", "rectfill", "rectstroke", "rectclip" };
    while (p < end && E->nalias < 64) {
        const char *q = find(p, end, "/");
        if (!q) break;
        p = q + 1;
        const char *ns = p;
        while (p < end && !is_delim(*p)) p++;
        size_t nl = (size_t)(p - ns);
        if (nl == 0 || nl >= 16) continue;
        const char *r = p;
        while (r < end && (*r == ' ' || *r == '\t')) r++;
        bool brace = r < end && *r == '{';
        bool slash = r < end && *r == '/';
        if (!brace && !slash) continue;
        r++;
        while (r < end && (*r == ' ' || *r == '\t')) r++;
        const char *os = r;
        while (r < end && !is_delim(*r)) r++;
        size_t ol = (size_t)(r - os);
        if (ol == 0 || ol >= 16) continue;
        while (r < end && (*r == ' ' || *r == '\t')) r++;
        if (brace && (r >= end || *r != '}')) continue;
        if (slash && (end - r < 4 || strncmp(r, "load", 4) != 0)) continue;
        for (size_t k = 0; k < sizeof(KNOWN) / sizeof(KNOWN[0]); k++)
            if (strlen(KNOWN[k]) == ol && strncmp(KNOWN[k], os, ol) == 0) {
                memcpy(E->alias[E->nalias].name, ns, nl);
                E->alias[E->nalias].name[nl] = 0;
                memcpy(E->alias[E->nalias].op, os, ol);
                E->alias[E->nalias].op[ol] = 0;
                E->nalias++;
                break;
            }
    }
}

char *eps_to_svg(const unsigned char *data, int size, int *skipped)
{
    if (skipped) *skipped = 0;
    if (!data || size < 16) return NULL;
    const char *start = (const char *)data, *end = start + size;
    // EPS "de DOS": cabecera binaria con la parte PostScript y una vista previa
    if (data[0] == 0xC5 && data[1] == 0xD0 && data[2] == 0xD3 && data[3] == 0xC6) {
        unsigned off = data[4] | data[5] << 8 | data[6] << 16 | (unsigned)data[7] << 24;
        unsigned len = data[8] | data[9] << 8 | data[10] << 16 | (unsigned)data[11] << 24;
        if (off >= (unsigned)size) return NULL;
        if (len > (unsigned)size - off) len = (unsigned)size - off;
        start = (const char *)data + off;
        end = start + len;
    }
    if (end - start < 4 || strncmp(start, "%!", 2) != 0) return NULL;
    // Tamaño del dibujo
    double llx = 0, lly = 0, urx = 0, ury = 0;
    const char *bb = find(start, end, "%%HiResBoundingBox:");
    if (!bb || sscanf(bb + 19, "%lf %lf %lf %lf", &llx, &lly, &urx, &ury) != 4) {
        bb = find(start, end, "%%BoundingBox:");
        if (!bb || sscanf(bb + 14, "%lf %lf %lf %lf", &llx, &lly, &urx, &ury) != 4) return NULL;
    }
    if (urx - llx < 1 || ury - lly < 1) return NULL;

    Eps *E = (Eps *)calloc(1, sizeof(Eps));
    if (!E) return NULL;
    // La parte que dibuja: después de la preparación de la página y antes del final
    const char *body = NULL;
    const char *k;
    if ((k = find(start, end, "%%EndPageSetup"))) body = k;
    else if ((k = find(start, end, "%%EndSetup"))) body = k;
    else if ((k = find(start, end, "%%EndProlog"))) body = k;
    else body = start;
    read_aliases(E, start, body);
    const char *stop = end;
    if ((k = find(body, end, "%%PageTrailer"))) stop = k;
    else if ((k = find(body, end, "%%Trailer"))) stop = k;
    if ((k = find(body, stop, "%AI9_PrintingDataEnd"))) stop = k;

    double W = urx - llx, H = ury - lly;
    E->gs[0] = (GState){ { 1, 0, 0, -1, -llx, ury }, 0, 0, 0, 1, 0 };
    E->p = body;
    E->end = stop;
    buf_printf(&E->out, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.2f\" height=\"%.2f\" viewBox=\"0 0 %.2f %.2f\">\n", W, H, W, H);
    run(E);
    while (E->gsp > 0) grestore(E);
    for (int i = 0; i < E->gs[0].opened; i++) buf_add(&E->out, "</g>\n", 5);
    buf_add(&E->out, "</svg>\n", 7);
    char *res = E->out.s;
    if (skipped) *skipped = E->skipped;
    if (E->shapes == 0) { free(res); res = NULL; }
    free(E->path.s);
    free(E);
    return res;
}
