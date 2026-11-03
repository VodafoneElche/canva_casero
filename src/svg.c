// Leer dibujos SVG y convertirlos en imagen (con nanosvg, licencia zlib)
#include "svg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define NANOSVG_IMPLEMENTATION
#include "external/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "external/nanosvgrast.h"

Image svg_to_image(const unsigned char *data, int size, int maxSide)
{
    Image out = { 0 };
    // nanosvg modifica el texto mientras lo lee: trabajamos con una copia
    char *copy = (char *)malloc(size + 1);
    memcpy(copy, data, size);
    copy[size] = 0;
    NSVGimage *svg = nsvgParse(copy, "px", 96.0f);
    free(copy);
    if (!svg) return out;
    if (svg->width < 1 || svg->height < 1 || !svg->shapes) { nsvgDelete(svg); return out; }

    float scale = maxSide / fmaxf(svg->width, svg->height);
    int w = (int)ceilf(svg->width * scale), h = (int)ceilf(svg->height * scale);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    unsigned char *pixels = (unsigned char *)malloc((size_t)w * h * 4);
    NSVGrasterizer *rast = nsvgCreateRasterizer();
    if (!rast || !pixels) { free(pixels); nsvgDelete(svg); return out; }
    nsvgRasterize(rast, svg, 0, 0, scale, pixels, w, h, w * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(svg);

    out.data = pixels;
    out.width = w;
    out.height = h;
    out.mipmaps = 1;
    out.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    return out;
}

// ------------------------------------------------------------------ Separar en partes
// Leemos el texto del SVG para encontrar sus grupos de primer nivel (<g>, <path>...).
// Cada grupo se dibuja por separado y se convierte en una capa. Si todo está en un
// único grupo, entramos en él. Si solo hay una parte, separamos por colores.

typedef struct { int start, end, tagEnd; } Span;

static int skip_to(const char *s, int i, int n, const char *what)
{
    const char *p = strstr(s + i, what);
    if (!p || p - s >= n) return n;
    return (int)(p - s) + (int)strlen(what);
}

// Fin de la etiqueta que empieza en i (respetando comillas). Devuelve la posición tras '>'
static int tag_end(const char *s, int i, int n)
{
    char q = 0;
    for (; i < n; i++) {
        if (q) { if (s[i] == q) q = 0; }
        else if (s[i] == '"' || s[i] == '\'') q = s[i];
        else if (s[i] == '>') return i + 1;
    }
    return n;
}

// Hijos directos entre from y to
static int find_children(const char *s, int from, int to, Span *out, int max)
{
    int depth = 0, count = 0, i = from;
    Span cur = { 0 };
    while (i < to) {
        if (s[i] != '<') { i++; continue; }
        if (strncmp(s + i, "<!--", 4) == 0) { i = skip_to(s, i, to, "-->"); continue; }
        if (strncmp(s + i, "<![CDATA[", 9) == 0) { i = skip_to(s, i, to, "]]>"); continue; }
        if (s[i + 1] == '?' || s[i + 1] == '!') { i = tag_end(s, i, to); continue; }
        int e = tag_end(s, i, to);
        if (s[i + 1] == '/') {
            depth--;
            if (depth == 0 && count < max) { cur.end = e; out[count++] = cur; }
            i = e;
            continue;
        }
        bool selfClose = e >= 2 && s[e - 2] == '/';
        if (depth == 0) { cur.start = i; cur.tagEnd = e; }
        if (selfClose) {
            if (depth == 0 && count < max) { cur.end = e; out[count++] = cur; }
        } else {
            depth++;
        }
        i = e;
    }
    return count;
}

static bool tag_is(const char *s, Span sp, const char *name)
{
    size_t l = strlen(name);
    return strncmp(s + sp.start + 1, name, l) == 0 && strchr(" \t\r\n/>", s[sp.start + 1 + l]) != NULL;
}

// Valor de un atributo dentro de [from, to). Devuelve longitud o -1
static int attr_value(const char *s, int from, int to, const char *name, char *out, int max)
{
    char pat[64];
    snprintf(pat, sizeof(pat), " %s=", name);
    for (int i = from; i < to; i++) {
        if ((s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') && strncmp(s + i + 1, name, strlen(name)) == 0 && s[i + 1 + strlen(name)] == '=') {
            int j = i + 2 + (int)strlen(name);
            char q = s[j];
            if (q != '"' && q != '\'') return -1;
            int k = j + 1, n = 0;
            while (k < to && s[k] != q && n < max - 1) out[n++] = s[k++];
            out[n] = 0;
            return n;
        }
    }
    (void)pat;
    return -1;
}

// Busca el elemento con id="..." en todo el texto y devuelve su etiqueta
static bool find_by_id(const char *s, int n, const char *id, Span *out)
{
    char pat[200];
    snprintf(pat, sizeof(pat), "id=\"%s\"", id);
    const char *p = strstr(s, pat);
    if (!p) return false;
    int i = (int)(p - s);
    while (i > 0 && s[i] != '<') i--;
    out->start = i;
    out->tagEnd = tag_end(s, i, n);
    out->end = out->tagEnd;
    return true;
}

// Si la parte usa clip-path con un rectángulo (lo típico de Illustrator), lo devuelve
static bool part_clip_rect(const char *s, int n, Span part, float *cx, float *cy, float *cw, float *ch)
{
    const char *p = strstr(s + part.start, "clip-path=\"url(#");
    if (!p || p - s >= part.end) return false;
    char id[128];
    int k = 0;
    p += strlen("clip-path=\"url(#");
    while (*p && *p != ')' && k < 127) id[k++] = *p++;
    id[k] = 0;
    Span clip;
    if (!find_by_id(s, n, id, &clip)) return false;
    // Primer hijo del clipPath: <rect> o <use xlink:href="#rect">
    int close = skip_to(s, clip.tagEnd, n, "</clipPath>");
    Span kids[4];
    int nk = find_children(s, clip.tagEnd, close, kids, 4);
    if (nk < 1) return false;
    Span r = kids[0];
    if (tag_is(s, r, "use")) {
        char href[140];
        if (attr_value(s, r.start, r.tagEnd, "xlink:href", href, sizeof(href)) < 2 &&
            attr_value(s, r.start, r.tagEnd, "href", href, sizeof(href)) < 2) return false;
        if (!find_by_id(s, n, href + 1, &r)) return false;
    }
    if (!tag_is(s, r, "rect")) return false;
    char v[64];
    float x = 0, y = 0, w = 0, h = 0;
    if (attr_value(s, r.start, r.tagEnd, "x", v, 64) > 0) x = (float)atof(v);
    if (attr_value(s, r.start, r.tagEnd, "y", v, 64) > 0) y = (float)atof(v);
    if (attr_value(s, r.start, r.tagEnd, "width", v, 64) > 0) w = (float)atof(v);
    if (attr_value(s, r.start, r.tagEnd, "height", v, 64) > 0) h = (float)atof(v);
    if (w <= 0 || h <= 0) return false;
    *cx = x; *cy = y; *cw = w; *ch = h;
    return true;
}

typedef struct { char *buf; int len, cap; } Buf;
static void buf_add(Buf *b, const char *t, int n)
{
    if (b->len + n + 1 > b->cap) { b->cap = (b->len + n + 1) * 2; b->buf = (char *)realloc(b->buf, b->cap); }
    memcpy(b->buf + b->len, t, n);
    b->len += n;
    b->buf[b->len] = 0;
}

// Recorta la imagen a lo que tiene color. Devuelve false si está vacía
static bool crop_to_content(unsigned char *px, int W, int H, int *ox, int *oy, int *ow, int *oh)
{
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (px[(y * W + x) * 4 + 3] > 4) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 < 0) return false;
    *ox = x0; *oy = y0; *ow = x1 - x0 + 1; *oh = y1 - y0 + 1;
    return true;
}

// Dibuja un SVG (ya leído) y lo convierte en parte. clip en unidades del viewBox.
static bool make_part(NSVGimage *svg, NSVGrasterizer *rast, float scale, int W, int H,
                      bool hasClip, float clx, float cly, float clw, float clh, float vbScale, float vbX, float vbY,
                      SvgPart *out)
{
    unsigned char *px = (unsigned char *)calloc((size_t)W * H, 4);
    nsvgRasterize(rast, svg, 0, 0, scale, px, W, H, W * 4);
    if (hasClip) {
        int x0 = (int)floorf((clx - vbX) * vbScale * scale), y0 = (int)floorf((cly - vbY) * vbScale * scale);
        int x1 = (int)ceilf((clx + clw - vbX) * vbScale * scale), y1 = (int)ceilf((cly + clh - vbY) * vbScale * scale);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (x < x0 || x >= x1 || y < y0 || y >= y1) px[(y * W + x) * 4 + 3] = 0;
    }
    int ox, oy, ow, oh;
    if (!crop_to_content(px, W, H, &ox, &oy, &ow, &oh)) { free(px); return false; }
    unsigned char *crop = (unsigned char *)malloc((size_t)ow * oh * 4);
    for (int y = 0; y < oh; y++) memcpy(crop + (size_t)y * ow * 4, px + ((size_t)(y + oy) * W + ox) * 4, (size_t)ow * 4);
    free(px);

    // ¿Es de un solo color? Entonces se podrá cambiar de color
    out->single = true;
    unsigned int col = 0;
    bool first = true;
    for (NSVGshape *sh = svg->shapes; sh; sh = sh->next) {
        if (!(sh->flags & NSVG_FLAGS_VISIBLE)) continue;
        if (sh->fill.type == NSVG_PAINT_COLOR) {
            if (first) { col = sh->fill.color; first = false; }
            else if (sh->fill.color != col) out->single = false;
        } else if (sh->fill.type != NSVG_PAINT_NONE) out->single = false;
        if (sh->stroke.type == NSVG_PAINT_COLOR && sh->strokeWidth > 0) {
            if (first) { col = sh->stroke.color; first = false; }
            else if (sh->stroke.color != col) out->single = false;
        } else if (sh->stroke.type != NSVG_PAINT_NONE && sh->stroke.type != 0) out->single = false;
    }
    if (first) out->single = false;
    out->color = (Color){ (unsigned char)(col & 0xFF), (unsigned char)((col >> 8) & 0xFF), (unsigned char)((col >> 16) & 0xFF), 255 };
    if (out->single) {
        // Guardamos la forma en blanco; el color lo pone el programa
        for (int i = 0; i < ow * oh; i++) { crop[i * 4] = 255; crop[i * 4 + 1] = 255; crop[i * 4 + 2] = 255; }
    }
    out->img = (Image){ crop, ow, oh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    out->x = ox / scale;
    out->y = oy / scale;
    out->w = ow / scale;
    out->h = oh / scale;
    return true;
}


static bool shape_color(const NSVGshape *sh, unsigned int *c)
{
    if (sh->fill.type == NSVG_PAINT_COLOR) { *c = sh->fill.color; return true; }
    if (sh->fill.type == NSVG_PAINT_NONE && sh->stroke.type == NSVG_PAINT_COLOR) { *c = sh->stroke.color; return true; }
    return false;
}

// Una parte por cada color del dibujo (en el orden en que aparecen)
static int split_colors(NSVGimage *img, NSVGrasterizer *rast, float scale, int W, int H,
                        bool clip, float clx, float cly, float clw, float clh, float vbScale, float vbX, float vbY,
                        const char *prefix, SvgPart *parts, int maxParts)
{
    unsigned int colors[64];
    int nc = 0, count = 0;
    bool other = false;
    for (NSVGshape *sh = img->shapes; sh; sh = sh->next) {
        unsigned int c;
        if (!shape_color(sh, &c)) { other = true; continue; }
        int k;
        for (k = 0; k < nc; k++) if (colors[k] == c) break;
        if (k == nc && nc < 64) colors[nc++] = c;
    }
    for (int k = 0; k <= nc && count < maxParts; k++) {
        if (k == nc && !other) break;
        for (NSVGshape *sh = img->shapes; sh; sh = sh->next) {
            unsigned int c;
            bool known = shape_color(sh, &c);
            bool vis = (k < nc) ? (known && c == colors[k]) : !known;
            if (vis) sh->flags |= NSVG_FLAGS_VISIBLE; else sh->flags &= ~NSVG_FLAGS_VISIBLE;
        }
        memset(&parts[count], 0, sizeof(SvgPart));
        if (make_part(img, rast, scale, W, H, clip, clx, cly, clw, clh, vbScale, vbX, vbY, &parts[count])) {
            if (k < nc) {
                unsigned int c = colors[k];
                snprintf(parts[count].name, sizeof(parts[count].name), "%s%s#%02X%02X%02X", prefix, prefix[0] ? " · " : "Color ", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
            } else {
                snprintf(parts[count].name, sizeof(parts[count].name), "%s%sDegradados", prefix, prefix[0] ? " · " : "");
            }
            count++;
        }
    }
    for (NSVGshape *sh = img->shapes; sh; sh = sh->next) sh->flags |= NSVG_FLAGS_VISIBLE;
    return count;
}

// Etiquetas que no se dibujan y que todas las partes necesitan (degradados, estilos...)
static bool is_global(const char *s, Span sp)
{
    return tag_is(s, sp, "defs") || tag_is(s, sp, "style") || tag_is(s, sp, "linearGradient") ||
           tag_is(s, sp, "radialGradient") || tag_is(s, sp, "metadata") || tag_is(s, sp, "title") ||
           tag_is(s, sp, "desc") || tag_is(s, sp, "clipPath") || tag_is(s, sp, "mask");
}

int svg_split(const unsigned char *data, int size, int maxSide, int mode, SvgPart *parts, int maxParts, float *outW, float *outH, int *how)
{
    *how = 0;
    char *s = (char *)malloc(size + 1);
    memcpy(s, data, size);
    s[size] = 0;
    int n = size;

    const char *sv = strstr(s, "<svg");
    if (!sv) { free(s); return 0; }
    int svgStart = (int)(sv - s);
    int svgTagEnd = tag_end(s, svgStart, n);
    const char *svClose = NULL;
    for (const char *q = strstr(s, "</svg>"); q; q = strstr(q + 1, "</svg>")) svClose = q;
    int svgClose = svClose ? (int)(svClose - s) : n;

    NSVGimage *whole;
    {
        char *c = (char *)malloc(size + 1);
        memcpy(c, data, size);
        c[size] = 0;
        whole = nsvgParse(c, "px", 96.0f);
        free(c);
    }
    if (!whole || whole->width < 1 || whole->height < 1) { if (whole) nsvgDelete(whole); free(s); return 0; }
    float svgW = whole->width, svgH = whole->height;
    *outW = svgW;
    *outH = svgH;
    float vbX = 0, vbY = 0, vbScale = 1;
    char vb[128];
    if (attr_value(s, svgStart, svgTagEnd, "viewBox", vb, sizeof(vb)) > 0) {
        float a, b, c2, d;
        if (sscanf(vb, "%f %f %f %f", &a, &b, &c2, &d) == 4 || sscanf(vb, "%f,%f,%f,%f", &a, &b, &c2, &d) == 4) {
            if (c2 > 0) { vbX = a; vbY = b; vbScale = svgW / c2; }
        }
    }
    float scale = maxSide / fmaxf(svgW, svgH);
    int W = (int)ceilf(svgW * scale), H = (int)ceilf(svgH * scale);
    NSVGrasterizer *rast = nsvgCreateRasterizer();

    // Las partes son los hijos de <svg>; si solo hay un <g> sin recorte, entramos en él
    static Span kids[1024], partSp[1024], sub[1024], subParts[1024];
    int nk = find_children(s, svgTagEnd, svgClose, kids, 1024);
    Buf globals = { 0 };
    int np = 0;
    for (int i = 0; i < nk; i++) {
        if (is_global(s, kids[i])) buf_add(&globals, s + kids[i].start, kids[i].end - kids[i].start);
        else partSp[np++] = kids[i];
    }
    Buf wrapOpen = { 0 }, wrapClose = { 0 };
    for (int guard = 0; guard < 8 && np == 1 && tag_is(s, partSp[0], "g") && partSp[0].tagEnd < partSp[0].end; guard++) {
        Span g = partSp[0];
        char tmp[16];
        if (attr_value(s, g.start, g.tagEnd, "clip-path", tmp, sizeof(tmp)) > 0) break;
        int ns = find_children(s, g.tagEnd, g.end, sub, 1024);
        int real = 0;
        for (int i = 0; i < ns; i++) {
            if (is_global(s, sub[i])) buf_add(&globals, s + sub[i].start, sub[i].end - sub[i].start);
            else subParts[real++] = sub[i];
        }
        if (real < 1) break;
        buf_add(&wrapOpen, s + g.start, g.tagEnd - g.start);
        buf_add(&wrapClose, "</g>", 4);
        memcpy(partSp, subParts, sizeof(Span) * real);
        np = real;
    }

    int count = 0;
    if (np >= 2) {
        *how = 1;
        int group = 0;
        for (int i = 0; i < np && count < maxParts; i++) {
            Buf mini = { 0 };
            buf_add(&mini, s + svgStart, svgTagEnd - svgStart);
            if (globals.len) buf_add(&mini, globals.buf, globals.len);
            if (wrapOpen.len) buf_add(&mini, wrapOpen.buf, wrapOpen.len);
            buf_add(&mini, s + partSp[i].start, partSp[i].end - partSp[i].start);
            if (wrapClose.len) buf_add(&mini, wrapClose.buf, wrapClose.len);
            buf_add(&mini, "</svg>", 6);
            NSVGimage *img = nsvgParse(mini.buf, "px", 96.0f);
            float clx = 0, cly = 0, clw = 0, clh = 0;
            bool clip = part_clip_rect(s, n, partSp[i], &clx, &cly, &clw, &clh);
            if (img && img->shapes) {
                img->width = svgW;
                img->height = svgH;
                char prefix[24];
                snprintf(prefix, sizeof(prefix), "Parte %d", group + 1);
                int got = 0;
                if (mode == 1) got = split_colors(img, rast, scale, W, H, clip, clx, cly, clw, clh, vbScale, vbX, vbY, prefix, parts + count, maxParts - count);
                else if (make_part(img, rast, scale, W, H, clip, clx, cly, clw, clh, vbScale, vbX, vbY, &parts[count])) {
                    snprintf(parts[count].name, sizeof(parts[count].name), "%s", prefix);
                    got = 1;
                }
                count += got;
                if (got) group++;
            }
            if (img) nsvgDelete(img);
            free(mini.buf);
        }
    }
    if (count < 2) {
        for (int i = 0; i < count; i++) UnloadImage(parts[i].img);
        *how = 2;
        count = split_colors(whole, rast, scale, W, H, false, 0, 0, 0, 0, vbScale, vbX, vbY, "", parts, maxParts);
        if (count < 1) *how = 0;
    }
    nsvgDeleteRasterizer(rast);
    nsvgDelete(whole);
    free(globals.buf);
    free(wrapOpen.buf);
    free(wrapClose.buf);
    free(s);
    return count;
}

void svg_free_parts(SvgPart *parts, int count)
{
    for (int i = 0; i < count; i++) UnloadImage(parts[i].img);
}

// Dibujo entero respetando los recortes: juntamos las partes en una sola imagen
Image svg_to_image_clipped(const unsigned char *data, int size, int maxSide)
{
    static SvgPart parts[SVG_MAX_PARTS];
    float w, h;
    int how;
    int n = svg_split(data, size, maxSide, 0, parts, SVG_MAX_PARTS, &w, &h, &how);
    if (how != 1 || n < 1) {
        svg_free_parts(parts, n);
        return svg_to_image(data, size, maxSide);
    }
    float scale = maxSide / fmaxf(w, h);
    int W = (int)ceilf(w * scale), H = (int)ceilf(h * scale);
    unsigned char *out = (unsigned char *)calloc((size_t)W * H, 4);
    for (int i = 0; i < n; i++) {
        SvgPart *p = &parts[i];
        unsigned char *src = (unsigned char *)p->img.data;
        int ox = (int)roundf(p->x * scale), oy = (int)roundf(p->y * scale);
        for (int y = 0; y < p->img.height; y++) {
            int ty = oy + y;
            if (ty < 0 || ty >= H) continue;
            for (int x = 0; x < p->img.width; x++) {
                int tx = ox + x;
                if (tx < 0 || tx >= W) continue;
                unsigned char *sp = src + ((size_t)y * p->img.width + x) * 4;
                unsigned char *dp = out + ((size_t)ty * W + tx) * 4;
                float sr = sp[0], sg = sp[1], sb = sp[2];
                if (p->single) { sr = p->color.r; sg = p->color.g; sb = p->color.b; }
                float sa = sp[3] / 255.0f, da = dp[3] / 255.0f;
                float oa = sa + da * (1 - sa);
                if (oa <= 0) continue;
                dp[0] = (unsigned char)((sr * sa + dp[0] * da * (1 - sa)) / oa);
                dp[1] = (unsigned char)((sg * sa + dp[1] * da * (1 - sa)) / oa);
                dp[2] = (unsigned char)((sb * sa + dp[2] * da * (1 - sa)) / oa);
                dp[3] = (unsigned char)(oa * 255);
            }
        }
    }
    svg_free_parts(parts, n);
    return (Image){ out, W, H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
}
