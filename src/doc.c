#include "doc.h"
#include "platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

Asset g_assets[MAX_ASSETS];
int g_assetCount = 0;

// ------------------------------------------------------------------ Imágenes

int asset_add_png(const unsigned char *data, int size)
{
    if (g_assetCount >= MAX_ASSETS) return -1;
    // Si ya tenemos exactamente la misma imagen, reutilizarla
    for (int i = 0; i < g_assetCount; i++) {
        if (g_assets[i].pngSize == size && memcmp(g_assets[i].png, data, size) == 0) return i;
    }
    Image img = LoadImageFromMemory(".png", data, size);
    if (img.data == NULL) return -1;
    Asset *a = &g_assets[g_assetCount];
    a->png = (unsigned char *)malloc(size);
    memcpy(a->png, data, size);
    a->pngSize = size;
    a->w = img.width;
    a->h = img.height;
    a->tex = LoadTextureFromImage(img);
    GenTextureMipmaps(&a->tex);
    SetTextureFilter(a->tex, TEXTURE_FILTER_TRILINEAR);
    UnloadImage(img);
    return g_assetCount++;
}

int asset_add_image(Image img)
{
    if (img.data == NULL) return -1;
    Image copy = ImageCopy(img);
    // Las fotos del móvil son enormes: las reducimos a 2400 px como máximo
    const int maxSide = 2400;
    if (copy.width > maxSide || copy.height > maxSide) {
        float s = (float)maxSide / (copy.width > copy.height ? copy.width : copy.height);
        ImageResize(&copy, (int)(copy.width * s), (int)(copy.height * s));
    }
    ImageFormat(&copy, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    int size = 0;
    unsigned char *png = ExportImageToMemory(copy, ".png", &size);
    UnloadImage(copy);
    if (!png) return -1;
    int id = asset_add_png(png, size);
    MemFree(png);
    return id;
}

int asset_load_file(const char *path)
{
    int size = 0;
    unsigned char *data = plat_read_file(path, &size);
    if (!data) return -1;
    const char *ext = GetFileExtension(path);
    char lower[16] = {0};
    for (int i = 0; ext && ext[i] && i < 15; i++) lower[i] = (char)(ext[i] >= 'A' && ext[i] <= 'Z' ? ext[i] + 32 : ext[i]);
    if (strcmp(lower, ".jpeg") == 0) strcpy(lower, ".jpg");
    Image img = LoadImageFromMemory(lower[0] ? lower : ".png", data, size);
    free(data);
    if (img.data == NULL) return -1;
    int id = asset_add_image(img);
    UnloadImage(img);
    return id;
}

Texture2D asset_texture(int id)
{
    if (id < 0 || id >= g_assetCount) return (Texture2D){0};
    return g_assets[id].tex;
}

// ------------------------------------------------------------------ Documento

void doc_init(Document *d, int w, int h, Color bg)
{
    d->w = w;
    d->h = h;
    d->bg = bg;
    d->count = 0;
}

const char *element_type_name(int type)
{
    switch (type) {
    case EL_RECT: return "Rectángulo";
    case EL_ELLIPSE: return "Círculo";
    case EL_TRIANGLE: return "Triángulo";
    case EL_STAR: return "Estrella";
    case EL_HEART: return "Corazón";
    case EL_LINE: return "Línea";
    case EL_ARROW: return "Flecha";
    case EL_TEXT: return "Texto";
    case EL_IMAGE: return "Imagen";
    }
    return "Elemento";
}

Element element_default(int type)
{
    Element e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.w = 300;
    e.h = 300;
    e.fill = (Color){ 125, 92, 255, 255 };
    e.stroke = (Color){ 40, 30, 80, 255 };
    e.strokeW = 0;
    e.opacity = 1;
    e.fontSize = 64;
    e.align = ALIGN_LEFT;
    e.asset = -1;
    e.visible = true;
    switch (type) {
    case EL_LINE:
    case EL_ARROW:
        e.h = 40;
        e.strokeW = 8;
        e.fill = (Color){ 40, 40, 50, 255 };
        break;
    case EL_TEXT:
        e.w = 500;
        e.h = 80;
        e.fill = (Color){ 30, 30, 40, 255 };
        strcpy(e.text, "Escribe aquí");
        break;
    case EL_HEART:
        e.fill = (Color){ 240, 70, 110, 255 };
        break;
    case EL_STAR:
        e.fill = (Color){ 255, 196, 40, 255 };
        break;
    default:
        break;
    }
    return e;
}

Element *doc_add(Document *d, int type)
{
    if (d->count >= MAX_ELEMENTS) return NULL;
    Element *e = &d->el[d->count++];
    *e = element_default(type);
    return e;
}

void doc_remove(Document *d, int index)
{
    if (index < 0 || index >= d->count) return;
    memmove(&d->el[index], &d->el[index + 1], sizeof(Element) * (d->count - index - 1));
    d->count--;
}

int doc_duplicate(Document *d, int index)
{
    if (index < 0 || index >= d->count || d->count >= MAX_ELEMENTS) return index;
    memmove(&d->el[index + 2], &d->el[index + 1], sizeof(Element) * (d->count - index - 1));
    d->el[index + 1] = d->el[index];
    d->el[index + 1].x += 30;
    d->el[index + 1].y += 30;
    d->el[index + 1].locked = false;
    d->count++;
    return index + 1;
}

int doc_move_layer(Document *d, int index, int delta)
{
    int to = index + delta;
    if (to < 0) to = 0;
    if (to >= d->count) to = d->count - 1;
    if (index < 0 || index >= d->count || to == index) return index;
    Element tmp = d->el[index];
    if (to > index) memmove(&d->el[index], &d->el[index + 1], sizeof(Element) * (to - index));
    else memmove(&d->el[to + 1], &d->el[to], sizeof(Element) * (index - to));
    d->el[to] = tmp;
    return to;
}

Rectangle element_bounds(const Element *e)
{
    float cx = e->x + e->w / 2, cy = e->y + e->h / 2;
    float r = e->rotation * DEG2RAD;
    float c = fabsf(cosf(r)), s = fabsf(sinf(r));
    float bw = e->w * c + e->h * s;
    float bh = e->w * s + e->h * c;
    return (Rectangle){ cx - bw / 2, cy - bh / 2, bw, bh };
}

// ------------------------------------------------------------------ Historial

#define HISTORY_MAX 60
static Document *g_undo[HISTORY_MAX];
static int g_undoCount = 0;
static Document *g_redo[HISTORY_MAX];
static int g_redoCount = 0;

static Document *clone(const Document *d)
{
    Document *c = (Document *)malloc(sizeof(Document));
    // Copiamos solo los elementos usados para ir más rápido
    c->w = d->w; c->h = d->h; c->bg = d->bg; c->count = d->count;
    memcpy(c->el, d->el, sizeof(Element) * d->count);
    return c;
}

static void restore(Document *d, const Document *src)
{
    d->w = src->w; d->h = src->h; d->bg = src->bg; d->count = src->count;
    memcpy(d->el, src->el, sizeof(Element) * src->count);
}

void history_push(const Document *d)
{
    if (g_undoCount == HISTORY_MAX) {
        free(g_undo[0]);
        memmove(&g_undo[0], &g_undo[1], sizeof(Document *) * (HISTORY_MAX - 1));
        g_undoCount--;
    }
    g_undo[g_undoCount++] = clone(d);
    for (int i = 0; i < g_redoCount; i++) free(g_redo[i]);
    g_redoCount = 0;
}

bool history_undo(Document *d)
{
    if (g_undoCount == 0) return false;
    if (g_redoCount < HISTORY_MAX) g_redo[g_redoCount++] = clone(d);
    Document *prev = g_undo[--g_undoCount];
    restore(d, prev);
    free(prev);
    return true;
}

bool history_redo(Document *d)
{
    if (g_redoCount == 0) return false;
    if (g_undoCount == HISTORY_MAX) {
        free(g_undo[0]);
        memmove(&g_undo[0], &g_undo[1], sizeof(Document *) * (HISTORY_MAX - 1));
        g_undoCount--;
    }
    g_undo[g_undoCount++] = clone(d);
    Document *next = g_redo[--g_redoCount];
    restore(d, next);
    free(next);
    return true;
}

bool history_can_undo(void) { return g_undoCount > 0; }
bool history_can_redo(void) { return g_redoCount > 0; }

void history_clear(void)
{
    for (int i = 0; i < g_undoCount; i++) free(g_undo[i]);
    for (int i = 0; i < g_redoCount; i++) free(g_redo[i]);
    g_undoCount = g_redoCount = 0;
}

// ------------------------------------------------------------------ Guardar / abrir
// Formato de texto sencillo. Las imágenes van dentro, en base64, para que un
// diseño sea un único archivo que se pueda pasar a otro ordenador.

typedef struct { char *buf; size_t len, cap; } Str;

static void str_add(Str *s, const char *t, size_t n)
{
    if (s->len + n + 1 > s->cap) {
        s->cap = (s->len + n + 1) * 2;
        s->buf = (char *)realloc(s->buf, s->cap);
    }
    memcpy(s->buf + s->len, t, n);
    s->len += n;
    s->buf[s->len] = 0;
}

static void str_printf(Str *s, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;


static void str_printf(Str *s, const char *fmt, ...)
{
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) str_add(s, tmp, (size_t)(n < (int)sizeof(tmp) ? n : (int)sizeof(tmp) - 1));
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64_encode(Str *s, const unsigned char *d, int n)
{
    char out[4];
    for (int i = 0; i < n; i += 3) {
        unsigned v = d[i] << 16;
        if (i + 1 < n) v |= d[i + 1] << 8;
        if (i + 2 < n) v |= d[i + 2];
        out[0] = B64[(v >> 18) & 63];
        out[1] = B64[(v >> 12) & 63];
        out[2] = i + 1 < n ? B64[(v >> 6) & 63] : '=';
        out[3] = i + 2 < n ? B64[v & 63] : '=';
        str_add(s, out, 4);
    }
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static unsigned char *b64_decode(const char *s, int len, int *outLen)
{
    unsigned char *out = (unsigned char *)malloc(len / 4 * 3 + 3);
    int n = 0, bits = 0;
    unsigned v = 0;
    for (int i = 0; i < len; i++) {
        int x = b64_val(s[i]);
        if (x < 0) continue;
        v = (v << 6) | x;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[n++] = (unsigned char)((v >> bits) & 0xFF);
        }
    }
    *outLen = n;
    return out;
}

static void escape_text(Str *s, const char *t)
{
    for (; *t; t++) {
        if (*t == '\n') str_add(s, "\\n", 2);
        else if (*t == '\\') str_add(s, "\\\\", 2);
        else if (*t == '\r') continue;
        else str_add(s, t, 1);
    }
}

static void unescape_text(char *dst, const char *src, int max)
{
    int n = 0;
    for (; *src && n < max - 1; src++) {
        if (*src == '\\' && src[1] == 'n') { dst[n++] = '\n'; src++; }
        else if (*src == '\\' && src[1] == '\\') { dst[n++] = '\\'; src++; }
        else dst[n++] = *src;
    }
    dst[n] = 0;
}

bool doc_save(const Document *d, const char *path)
{
    Str s = {0};
    // Qué imágenes usa este diseño (renumeradas desde 0)
    int map[MAX_ASSETS];
    int used[MAX_ASSETS];
    int usedCount = 0;
    for (int i = 0; i < MAX_ASSETS; i++) map[i] = -1;
    for (int i = 0; i < d->count; i++) {
        int a = d->el[i].asset;
        if (d->el[i].type == EL_IMAGE && a >= 0 && a < g_assetCount && map[a] < 0) {
            map[a] = usedCount;
            used[usedCount++] = a;
        }
    }

    str_printf(&s, "LIENZOCASERO 1\n");
    str_printf(&s, "lienzo %d %d %d %d %d %d\n", d->w, d->h, d->bg.r, d->bg.g, d->bg.b, d->bg.a);
    for (int i = 0; i < d->count; i++) {
        const Element *e = &d->el[i];
        int asset = (e->type == EL_IMAGE && e->asset >= 0 && e->asset < MAX_ASSETS) ? map[e->asset] : -1;
        str_printf(&s, "elemento %d %.2f %.2f %.2f %.2f %.2f %d %d %d %d %d %d %d %d %.2f %.3f %.3f %d %.2f %d %d %d %d\n",
                   e->type, e->x, e->y, e->w, e->h, e->rotation,
                   e->fill.r, e->fill.g, e->fill.b, e->fill.a,
                   e->stroke.r, e->stroke.g, e->stroke.b, e->stroke.a,
                   e->strokeW, e->roundness, e->opacity, e->font, e->fontSize, e->align,
                   asset, e->visible ? 1 : 0, e->locked ? 1 : 0);
        if (e->type == EL_TEXT) {
            str_printf(&s, "texto ");
            escape_text(&s, e->text);
            str_add(&s, "\n", 1);
        }
    }
    for (int i = 0; i < usedCount; i++) {
        Asset *a = &g_assets[used[i]];
        str_printf(&s, "imagen %d ", i);
        b64_encode(&s, a->png, a->pngSize);
        str_add(&s, "\n", 1);
    }
    str_printf(&s, "fin\n");
    bool ok = plat_write_file(path, s.buf, (int)s.len);
    free(s.buf);
    return ok;
}

bool doc_load(Document *d, const char *path)
{
    int size = 0;
    char *data = (char *)plat_read_file(path, &size);
    if (!data) return false;
    if (strncmp(data, "LIENZOCASERO", 12) != 0) { free(data); return false; }

    Document *tmp = (Document *)calloc(1, sizeof(Document));
    doc_init(tmp, 1080, 1080, WHITE);
    int fileAsset[MAX_ASSETS];
    for (int i = 0; i < MAX_ASSETS; i++) fileAsset[i] = -1;

    char *line = data;
    while (line && *line) {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        if (end && end > line && end[-1] == '\r') end[-1] = 0;

        if (strncmp(line, "lienzo ", 7) == 0) {
            int r, g, b, a;
            if (sscanf(line + 7, "%d %d %d %d %d %d", &tmp->w, &tmp->h, &r, &g, &b, &a) == 6)
                tmp->bg = (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a };
        } else if (strncmp(line, "elemento ", 9) == 0 && tmp->count < MAX_ELEMENTS) {
            Element e = element_default(EL_RECT);
            int fr, fg, fb, fa, sr, sg, sb, sa, vis, lock;
            int n = sscanf(line + 9, "%d %f %f %f %f %f %d %d %d %d %d %d %d %d %f %f %f %d %f %d %d %d %d",
                           &e.type, &e.x, &e.y, &e.w, &e.h, &e.rotation,
                           &fr, &fg, &fb, &fa, &sr, &sg, &sb, &sa,
                           &e.strokeW, &e.roundness, &e.opacity, &e.font, &e.fontSize, &e.align,
                           &e.asset, &vis, &lock);
            if (n == 23 && e.type >= 0 && e.type < EL_TYPE_COUNT) {
                e.fill = (Color){ (unsigned char)fr, (unsigned char)fg, (unsigned char)fb, (unsigned char)fa };
                e.stroke = (Color){ (unsigned char)sr, (unsigned char)sg, (unsigned char)sb, (unsigned char)sa };
                e.visible = vis != 0;
                e.locked = lock != 0;
                e.text[0] = 0;
                tmp->el[tmp->count++] = e;
            }
        } else if (strncmp(line, "texto ", 6) == 0 && tmp->count > 0) {
            unescape_text(tmp->el[tmp->count - 1].text, line + 6, TEXT_MAX);
        } else if (strncmp(line, "imagen ", 7) == 0) {
            int idx = atoi(line + 7);
            char *b = strchr(line + 7, ' ');
            if (b && idx >= 0 && idx < MAX_ASSETS) {
                int pngLen = 0;
                unsigned char *png = b64_decode(b + 1, (int)strlen(b + 1), &pngLen);
                fileAsset[idx] = asset_add_png(png, pngLen);
                free(png);
            }
        }
        line = end ? end + 1 : NULL;
    }
    // Pasar de los números del archivo a los de esta sesión
    for (int i = 0; i < tmp->count; i++) {
        Element *e = &tmp->el[i];
        if (e->type == EL_IMAGE) e->asset = (e->asset >= 0 && e->asset < MAX_ASSETS) ? fileAsset[e->asset] : -1;
    }
    if (tmp->w < 16) tmp->w = 16;
    if (tmp->h < 16) tmp->h = 16;
    restore(d, tmp);
    free(tmp);
    free(data);
    return true;
}
