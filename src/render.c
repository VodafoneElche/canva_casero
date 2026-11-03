#include "render.h"
#include "platform.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Font g_uiFont;
Font g_uiFontBold;

// ------------------------------------------------------------------ Fuentes
// Tres grupos: las que ya trae Windows, las que vienen con el programa
// (carpeta "fuentes" junto al .exe) y las que añade cada persona
// (carpeta "fuentes" dentro de %APPDATA%\LienzoCasero).
// Se cargan la primera vez que se usan.

FontEntry g_fonts[MAX_FONTS];
int g_fontCount = 0;

typedef struct { const char *name; const char *files[4]; } FontSpec;

static const FontSpec SYSTEM_FONTS[] = {
    { "Moderna",          { "C:/Windows/Fonts/segoeui.ttf",  "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf" } },
    { "Moderna negrita",  { "C:/Windows/Fonts/segoeuib.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" } },
    { "Clásica",          { "C:/Windows/Fonts/georgia.ttf",  "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf" } },
    { "Clásica negrita",  { "C:/Windows/Fonts/georgiab.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf" } },
    { "Divertida",        { "C:/Windows/Fonts/comic.ttf",    "C:/Windows/Fonts/comicbd.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf" } },
    { "Titular",          { "C:/Windows/Fonts/impact.ttf",   "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" } },
    { "Manuscrita",       { "C:/Windows/Fonts/segoesc.ttf",  "C:/Windows/Fonts/segoepr.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf" } },
    { "Máquina",          { "C:/Windows/Fonts/consola.ttf",  "C:/Windows/Fonts/cour.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf" } },
};

// Nombres bonitos para las fuentes incluidas (archivo -> nombre)
static const struct { const char *file; const char *name; } BUNDLED_NAMES[] = {
    { "Poppins-Regular.ttf", "Poppins" }, { "Poppins-Bold.ttf", "Poppins negrita" }, { "Poppins-Black.ttf", "Poppins gruesa" },
    { "Montserrat.ttf", "Montserrat" }, { "PlayfairDisplay.ttf", "Playfair" }, { "AbrilFatface-Regular.ttf", "Abril Fatface" },
    { "Cinzel.ttf", "Cinzel" }, { "Lobster-Regular.ttf", "Lobster" }, { "Pacifico-Regular.ttf", "Pacifico" },
    { "DancingScript.ttf", "Dancing Script" }, { "GreatVibes-Regular.ttf", "Great Vibes" }, { "Satisfy-Regular.ttf", "Satisfy" },
    { "Caveat.ttf", "Caveat" }, { "IndieFlower-Regular.ttf", "Indie Flower" }, { "PermanentMarker-Regular.ttf", "Rotulador" },
    { "AmaticSC-Bold.ttf", "Amatic" }, { "BebasNeue-Regular.ttf", "Bebas Neue" }, { "Anton-Regular.ttf", "Anton" },
    { "Bungee-Regular.ttf", "Bungee" }, { "Bangers-Regular.ttf", "Bangers (cómic)" }, { "LuckiestGuy-Regular.ttf", "Luckiest Guy" },
    { "Righteous-Regular.ttf", "Righteous" }, { "Fredoka.ttf", "Fredoka" }, { "Comfortaa.ttf", "Comfortaa" },
    { "SpecialElite-Regular.ttf", "Máquina antigua" },
};

static int g_codepoints[400];
static int g_codepointCount = 0;

static void build_codepoints(void)
{
    g_codepointCount = 0;
    for (int c = 32; c < 127; c++) g_codepoints[g_codepointCount++] = c;
    for (int c = 160; c < 384; c++) g_codepoints[g_codepointCount++] = c;   // Latin-1 y Latin extendido
    const int extra[] = { 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x20AC, 0x2122, 0x2190, 0x2192, 0x2605, 0x2665, 0x2713 };
    for (size_t i = 0; i < sizeof(extra) / sizeof(extra[0]); i++) g_codepoints[g_codepointCount++] = extra[i];
}

static Font load_font_file(const char *path, int size)
{
    int dataSize = 0;
    unsigned char *data = plat_read_file(path, &dataSize);
    if (!data) return (Font){0};
    const char *ext = IsFileExtension(path, ".otf;.OTF") ? ".otf" : ".ttf";
    Font f = LoadFontFromMemory(ext, data, dataSize, size, g_codepoints, g_codepointCount);
    free(data);
    return f;
}

static Font load_first(const char *const *files, int size)
{
    for (int i = 0; i < 4 && files[i]; i++) {
        Font f = load_font_file(files[i], size);
        if (f.texture.id != 0) return f;
    }
    return (Font){0};
}

int font_find(const char *name)
{
    for (int i = 0; i < g_fontCount; i++)
        if (strcmp(g_fonts[i].name, name) == 0) return i;
    return -1;
}

static int font_register(const char *name, const char *path, int group)
{
    if (g_fontCount >= MAX_FONTS || font_find(name) >= 0) return font_find(name);
    FontEntry *f = &g_fonts[g_fontCount];
    memset(f, 0, sizeof(*f));
    strncpy(f->name, name, sizeof(f->name) - 1);
    strncpy(f->path, path, sizeof(f->path) - 1);
    f->group = group;
    return g_fontCount++;
}

// "mi_fuente-Regular.ttf" -> "mi fuente"
static void name_from_file(const char *file, char *out, int max)
{
    const char *base = GetFileNameWithoutExt(file);
    int n = 0;
    for (const char *p = base; *p && n < max - 1; p++) out[n++] = (*p == '_' || *p == '-') ? ' ' : *p;
    out[n] = 0;
    char *reg = strstr(out, " Regular");
    if (reg && reg[8] == 0) *reg = 0;
    if (!out[0]) strncpy(out, "Fuente", max - 1);
}

static int cmp_fonts(const void *a, const void *b)
{
    return strcmp(((const FontEntry *)a)->name, ((const FontEntry *)b)->name);
}

static void register_dir(const char *dir, int group)
{
    static char names[MAX_FONTS][260];
    int start = g_fontCount;
    const char *exts[2] = { ".ttf", ".otf" };
    for (int e = 0; e < 2; e++) {
        int n = plat_list_dir(dir, exts[e], names, MAX_FONTS);
        for (int i = 0; i < n; i++) {
            char path[1400], name[64];
            snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
            name_from_file(names[i], name, sizeof(name));
            for (size_t k = 0; k < sizeof(BUNDLED_NAMES) / sizeof(BUNDLED_NAMES[0]); k++)
                if (strcmp(BUNDLED_NAMES[k].file, names[i]) == 0) strncpy(name, BUNDLED_NAMES[k].name, sizeof(name) - 1);
            font_register(name, path, group);
        }
    }
    qsort(&g_fonts[start], g_fontCount - start, sizeof(FontEntry), cmp_fonts);
}

// Medidas de la fuente desde su archivo (tablas head y hhea).
// raylib usa "tamaño = de arriba a abajo de la letra", no el "em" de los PDF y de Canva.
static float g_emRatio[MAX_FONTS], g_ascRatio[MAX_FONTS];
static bool g_metricsRead[MAX_FONTS];

static unsigned rd16(const unsigned char *p) { return (unsigned)(p[0] << 8 | p[1]); }
static unsigned rd32(const unsigned char *p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }

static void read_metrics(int index)
{
    g_metricsRead[index] = true;
    g_emRatio[index] = 1.2f;
    g_ascRatio[index] = 0.8f;
    int size = 0;
    unsigned char *d = plat_read_file(g_fonts[index].path, &size);
    if (!d) return;
    unsigned base = 0;
    if (size > 16 && memcmp(d, "ttcf", 4) == 0) base = rd32(d + 12);
    if (base + 12 > (unsigned)size) { free(d); return; }
    unsigned n = rd16(d + base + 4);
    int upm = 0, asc = 0, desc = 0;
    for (unsigned i = 0; i < n && base + 12 + i * 16 + 16 <= (unsigned)size; i++) {
        const unsigned char *t = d + base + 12 + i * 16;
        unsigned off = rd32(t + 8);
        if (off + 8 > (unsigned)size) continue;
        if (memcmp(t, "head", 4) == 0 && off + 20 <= (unsigned)size) upm = (int)rd16(d + off + 18);
        if (memcmp(t, "hhea", 4) == 0) { asc = (short)rd16(d + off + 4); desc = (short)rd16(d + off + 6); }
    }
    free(d);
    if (upm > 0 && asc - desc > 0) {
        g_emRatio[index] = (float)(asc - desc) / (float)upm;
        g_ascRatio[index] = (float)asc / (float)(asc - desc);
    }
}

float font_em_ratio(int index)
{
    if (index < 0 || index >= g_fontCount) return 1.2f;
    if (!g_metricsRead[index]) read_metrics(index);
    return g_emRatio[index];
}

float font_ascent_ratio(int index)
{
    if (index < 0 || index >= g_fontCount) return 0.8f;
    if (!g_metricsRead[index]) read_metrics(index);
    return g_ascRatio[index];
}

const char *fonts_user_dir(void)
{
    static char dir[1100] = {0};
    if (!dir[0]) {
        snprintf(dir, sizeof(dir), "%s/fuentes", plat_data_dir());
        plat_make_dir(dir);
    }
    return dir;
}

void fonts_load(void)
{
    build_codepoints();
    g_fontCount = 0;
    // 1. Las de Windows (las primeras 8 siempre en este orden: los diseños antiguos las usan por número)
    for (size_t i = 0; i < sizeof(SYSTEM_FONTS) / sizeof(SYSTEM_FONTS[0]); i++) {
        const char *path = SYSTEM_FONTS[i].files[0];
        for (int k = 0; k < 4 && SYSTEM_FONTS[i].files[k]; k++)
            if (plat_file_exists(SYSTEM_FONTS[i].files[k])) { path = SYSTEM_FONTS[i].files[k]; break; }
        font_register(SYSTEM_FONTS[i].name, path, FONT_GROUP_SYSTEM);
    }
    // 2. Las que vienen con el programa
    char dir[1200];
    snprintf(dir, sizeof(dir), "%sfuentes", GetApplicationDirectory());
    register_dir(dir, FONT_GROUP_BUNDLED);
#if defined(LIENZO_FONTS_DIR)
    if (g_fontCount == 8) register_dir(LIENZO_FONTS_DIR, FONT_GROUP_BUNDLED); // al compilar en desarrollo
#endif
    // 3. Las de cada persona
    register_dir(fonts_user_dir(), FONT_GROUP_USER);

    g_uiFont = load_first(SYSTEM_FONTS[0].files, 20);
    g_uiFontBold = load_first(SYSTEM_FONTS[1].files, 20);
    if (g_uiFont.texture.id == 0) g_uiFont = GetFontDefault();
    if (g_uiFontBold.texture.id == 0) g_uiFontBold = g_uiFont;
    SetTextureFilter(g_uiFont.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(g_uiFontBold.texture, TEXTURE_FILTER_BILINEAR);
}

void fonts_unload(void)
{
    for (int i = 0; i < g_fontCount; i++)
        if (g_fonts[i].loaded) UnloadFont(g_fonts[i].font);
}

static bool ensure_loaded(int i)
{
    FontEntry *f = &g_fonts[i];
    if (f->loaded) return true;
    if (f->failed) return false;
    f->font = load_font_file(f->path, FONT_BASE_SIZE);
    if (f->font.texture.id == 0) { f->failed = true; return false; }
    GenTextureMipmaps(&f->font.texture);
    SetTextureFilter(f->font.texture, TEXTURE_FILTER_TRILINEAR);
    f->loaded = true;
    return true;
}

Font font_get(int index)
{
    if (index >= 0 && index < g_fontCount && ensure_loaded(index)) return g_fonts[index].font;
    if (g_fontCount > 0 && ensure_loaded(0)) return g_fonts[0].font;
    return GetFontDefault();
}

static int g_previewBudget = 2;
void fonts_new_frame(void) { g_previewBudget = 2; }

Font font_get_preview(int index)
{
    if (index < 0 || index >= g_fontCount) return g_uiFont;
    if (g_fonts[index].loaded) return g_fonts[index].font;
    if (g_fonts[index].failed || g_previewBudget <= 0) return g_uiFont;
    g_previewBudget--;
    return ensure_loaded(index) ? g_fonts[index].font : g_uiFont;
}

const char *font_name(int index)
{
    if (index >= 0 && index < g_fontCount) return g_fonts[index].name;
    return g_fontCount > 0 ? g_fonts[0].name : "";
}

// Copia una fuente a la carpeta del usuario y la añade a la lista
int font_install_file(const char *path)
{
    int size = 0;
    unsigned char *data = plat_read_file(path, &size);
    if (!data) return -1;
    char name[64];
    name_from_file(GetFileName(path), name, sizeof(name));
    int idx = font_install_data(name, IsFileExtension(path, ".otf;.OTF") ? ".otf" : ".ttf", data, size);
    free(data);
    return idx;
}

int font_install_data(const char *name, const char *ext, const unsigned char *data, int size)
{
    int existing = font_find(name);
    if (existing >= 0) return existing;
    // Comprobar que es una fuente válida antes de guardarla
    Font test = LoadFontFromMemory(ext, data, size, 16, NULL, 95);
    if (test.texture.id == 0) return -1;
    UnloadFont(test);
    char safe[64];
    int n = 0;
    for (const char *p = name; *p && n < 63; p++) safe[n++] = strchr("\\/:*?\"<>|", *p) ? '_' : *p;
    safe[n] = 0;
    char dst[1400];
    snprintf(dst, sizeof(dst), "%s/%s%s", fonts_user_dir(), safe, ext);
    if (!plat_write_file(dst, data, size)) return -1;
    return font_register(name, dst, FONT_GROUP_USER);
}

// ------------------------------------------------------------------ Ayudas de dibujo

static Color with_opacity(Color c, float o)
{
    c.a = (unsigned char)(c.a * (o < 0 ? 0 : o > 1 ? 1 : o));
    return c;
}

// Triángulo en cualquier orden (raylib necesita sentido antihorario)
static void tri(Vector2 a, Vector2 b, Vector2 c, Color col)
{
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross < 0) DrawTriangle(a, b, c, col);
    else DrawTriangle(a, c, b, col);
}

static void poly_fill(Vector2 center, const Vector2 *p, int n, Color col)
{
    for (int i = 0; i < n; i++) tri(center, p[i], p[(i + 1) % n], col);
}

static void poly_outline(const Vector2 *p, int n, float thick, Color col)
{
    for (int i = 0; i < n; i++) {
        DrawLineEx(p[i], p[(i + 1) % n], thick, col);
        DrawCircleV(p[i], thick / 2, col);
    }
}

#define MAXPTS 128

// Puntos de cada forma dentro de la caja local (-w/2..w/2, -h/2..h/2)
static int shape_points(const Element *e, Vector2 *p, Vector2 *center)
{
    float hw = e->w / 2, hh = e->h / 2;
    int n = 0;
    *center = (Vector2){ 0, 0 };
    switch (e->type) {
    case EL_ELLIPSE:
        n = 96;
        for (int i = 0; i < n; i++) {
            float a = (float)i / n * 2 * PI;
            p[i] = (Vector2){ cosf(a) * hw, sinf(a) * hh };
        }
        break;
    case EL_TRIANGLE:
        p[0] = (Vector2){ 0, -hh };
        p[1] = (Vector2){ hw, hh };
        p[2] = (Vector2){ -hw, hh };
        n = 3;
        center->y = hh / 3;
        break;
    case EL_STAR:
        n = 10;
        for (int i = 0; i < n; i++) {
            float a = -PI / 2 + i * PI / 5;
            float r = (i % 2 == 0) ? 1.0f : 0.42f;
            p[i] = (Vector2){ cosf(a) * hw * r, sinf(a) * hh * r + hh * 0.08f };
        }
        break;
    case EL_HEART:
        n = 100;
        for (int i = 0; i < n; i++) {
            float t = (float)i / n * 2 * PI;
            float x = 16 * powf(sinf(t), 3);
            float y = -(13 * cosf(t) - 5 * cosf(2 * t) - 2 * cosf(3 * t) - cosf(4 * t));
            // x va de -16 a 16, y de -12 a 17 aprox.
            p[i] = (Vector2){ x / 16.0f * hw, (y - 2.5f) / 14.5f * hh };
        }
        center->y = 0;
        break;
    default:
        break;
    }
    return n;
}

// ------------------------------------------------------------------ Texto

typedef struct { int start, end; float width; } TextLine;

// Parte el texto en líneas que caben en el ancho de la caja
static int layout_text(const Element *e, TextLine *lines, int maxLines)
{
    Font f = font_get(e->font);
    float size = e->fontSize;
    float spacing = size * 0.02f;
    const char *t = e->text;
    int len = (int)strlen(t);
    int n = 0;
    int lineStart = 0;
    int lastSpace = -1;
    int i = 0;
    while (i <= len && n < maxLines) {
        if (i == len || t[i] == '\n') {
            char tmp[TEXT_MAX];
            memcpy(tmp, t + lineStart, i - lineStart);
            tmp[i - lineStart] = 0;
            lines[n].start = lineStart;
            lines[n].end = i;
            lines[n].width = MeasureTextEx(f, tmp, size, spacing).x;
            n++;
            lineStart = i + 1;
            lastSpace = -1;
            i++;
            continue;
        }
        if (t[i] == ' ') lastSpace = i;
        // ¿Se sale del ancho?
        char tmp[TEXT_MAX];
        int cl = 1;
        while (i + cl < len && (t[i + cl] & 0xC0) == 0x80) cl++;
        memcpy(tmp, t + lineStart, i + cl - lineStart);
        tmp[i + cl - lineStart] = 0;
        float w = MeasureTextEx(f, tmp, size, spacing).x;
        if (w > e->w && i > lineStart) {
            int breakAt = (lastSpace > lineStart) ? lastSpace : i;
            memcpy(tmp, t + lineStart, breakAt - lineStart);
            tmp[breakAt - lineStart] = 0;
            lines[n].start = lineStart;
            lines[n].end = breakAt;
            lines[n].width = MeasureTextEx(f, tmp, size, spacing).x;
            n++;
            lineStart = (lastSpace > lineStart) ? breakAt + 1 : breakAt;
            i = lineStart;
            lastSpace = -1;
            continue;
        }
        i += cl;
    }
    return n;
}

#define LINE_SPACING_DEFAULT 1.18f
#define LINE_SPACING (e->lineSpacing > 0.3f ? e->lineSpacing : LINE_SPACING_DEFAULT)

float text_height(const Element *e)
{
    TextLine lines[200];
    int n = layout_text(e, lines, 200);
    if (n < 1) n = 1;
    return n * e->fontSize * LINE_SPACING;
}

static float line_x(const Element *e, float width)
{
    if (e->align == ALIGN_CENTER) return -e->w / 2 + (e->w - width) / 2;
    if (e->align == ALIGN_RIGHT) return e->w / 2 - width;
    return -e->w / 2;
}

Vector2 text_caret_pos(const Element *e, int byteIndex, float *lineH)
{
    TextLine lines[200];
    int n = layout_text(e, lines, 200);
    Font f = font_get(e->font);
    float size = e->fontSize;
    *lineH = size * LINE_SPACING;
    for (int i = 0; i < n; i++) {
        bool last = (i == n - 1);
        if (byteIndex >= lines[i].start && (byteIndex <= lines[i].end || last)) {
            if (!last && byteIndex == lines[i].end && lines[i + 1].start == byteIndex) continue;
            char tmp[TEXT_MAX];
            int k = byteIndex - lines[i].start;
            if (k > lines[i].end - lines[i].start) k = lines[i].end - lines[i].start;
            memcpy(tmp, e->text + lines[i].start, k);
            tmp[k] = 0;
            float x = line_x(e, lines[i].width) + MeasureTextEx(f, tmp, size, size * 0.02f).x;
            return (Vector2){ x, -e->h / 2 + i * size * LINE_SPACING };
        }
    }
    return (Vector2){ line_x(e, 0), -e->h / 2 };
}

// Dibuja todas las líneas del texto desplazadas (dx, dy)
static void draw_text_lines(const Element *e, const TextLine *lines, int n, Font f, float dx, float dy, Color col)
{
    float size = e->fontSize;
    for (int i = 0; i < n; i++) {
        char tmp[TEXT_MAX];
        int l = lines[i].end - lines[i].start;
        memcpy(tmp, e->text + lines[i].start, l);
        tmp[l] = 0;
        Vector2 pos = { line_x(e, lines[i].width) + dx, -e->h / 2 + i * size * LINE_SPACING + size * (LINE_SPACING - 1) / 2 + dy };
        DrawTextEx(f, tmp, pos, size, size * 0.02f, col);
    }
}

static void draw_text_element(const Element *e, Color col)
{
    TextLine lines[200];
    int n = layout_text(e, lines, 200);
    Font f = font_get(e->font);
    float size = e->fontSize;
    float amt = e->effectAmount < 0 ? 0 : e->effectAmount > 1 ? 1 : e->effectAmount;
    Color fx = with_opacity(e->effectColor, e->opacity);

    switch (e->effect) {
    case FX_SHADOW: {
        // Sombra suave: varias copias desplazadas y transparentes
        float d = 2 + size * 0.10f * amt;
        float blur = 1 + size * 0.035f * amt;
        Color c = fx;
        c.a = (unsigned char)(fx.a / 5);
        for (int k = 0; k < 8; k++) {
            float a = k * PI / 4;
            draw_text_lines(e, lines, n, f, d + cosf(a) * blur, d + sinf(a) * blur, c);
        }
        Color core = fx;
        core.a = (unsigned char)(fx.a * 0.6f);
        draw_text_lines(e, lines, n, f, d, d, core);
        break;
    }
    case FX_OUTLINE: {
        float r = 1 + size * 0.09f * amt;
        int steps = 24;
        for (int k = 0; k < steps; k++) {
            float a = k * 2 * PI / steps;
            draw_text_lines(e, lines, n, f, cosf(a) * r, sinf(a) * r, fx);
            draw_text_lines(e, lines, n, f, cosf(a) * r * 0.5f, sinf(a) * r * 0.5f, fx);
        }
        break;
    }
    case FX_RELIEF: {
        // Copias apiladas hacia abajo a la derecha: parece que las letras salen del papel
        float depth = 2 + size * 0.14f * amt;
        int steps = (int)ceilf(depth);
        if (steps > 80) steps = 80;
        for (int k = steps; k >= 1; k--) {
            float t = (float)k / steps;
            Color c = fx;
            float shade = 1.0f - 0.35f * t; // más oscuro cuanto más al fondo
            c.r = (unsigned char)(c.r * shade); c.g = (unsigned char)(c.g * shade); c.b = (unsigned char)(c.b * shade);
            draw_text_lines(e, lines, n, f, depth * t * 0.7f, depth * t, c);
        }
        // Sombra en el suelo
        Color sh = { 0, 0, 0, (unsigned char)(50 * e->opacity) };
        draw_text_lines(e, lines, n, f, depth * 0.9f, depth * 1.3f, sh);
        break;
    }
    case FX_EMBOSS: {
        float d = 1 + size * 0.035f * amt;
        Color light = with_opacity((Color){ 255, 255, 255, 200 }, e->opacity);
        Color dark = with_opacity((Color){ 0, 0, 0, 140 }, e->opacity);
        draw_text_lines(e, lines, n, f, -d, -d, light);
        draw_text_lines(e, lines, n, f, d, d, dark);
        break;
    }
    case FX_NEON: {
        float r = 2 + size * 0.22f * amt;
        for (int ring = 3; ring >= 1; ring--) {
            float rr = r * ring / 3.0f;
            Color c = fx;
            c.a = (unsigned char)(fx.a * 0.07f * (4 - ring));
            for (int k = 0; k < 16; k++) {
                float a = k * 2 * PI / 16;
                draw_text_lines(e, lines, n, f, cosf(a) * rr, sinf(a) * rr, c);
            }
        }
        break;
    }
    default:
        break;
    }
    draw_text_lines(e, lines, n, f, 0, 0, col);
}

// ------------------------------------------------------------------ Elementos

void render_element(const Element *e)
{
    if (!e->visible) return;
    Color fill = with_opacity(e->fill, e->opacity);
    Color stroke = with_opacity(e->stroke, e->opacity);
    float hw = e->w / 2, hh = e->h / 2;

    rlPushMatrix();
    rlTranslatef(e->x + hw, e->y + hh, 0);
    rlRotatef(e->rotation, 0, 0, 1);

    switch (e->type) {
    case EL_RECT: {
        Rectangle r = { -hw, -hh, e->w, e->h };
        float shortSide = e->w < e->h ? e->w : e->h;
        if (e->roundness > 0.001f) {
            DrawRectangleRounded(r, e->roundness, 24, fill);
            if (e->strokeW > 0) {
                // raylib dibuja el borde hacia fuera; lo metemos hacia dentro
                Rectangle ri = { r.x + e->strokeW, r.y + e->strokeW, r.width - 2 * e->strokeW, r.height - 2 * e->strokeW };
                float ro = e->roundness * shortSide / (shortSide - 2 * e->strokeW > 1 ? shortSide - 2 * e->strokeW : 1);
                DrawRectangleRoundedLinesEx(ri, ro > 1 ? 1 : ro, 24, e->strokeW, stroke);
            }
        } else {
            DrawRectangleRec(r, fill);
            if (e->strokeW > 0) DrawRectangleLinesEx(r, e->strokeW, stroke);
        }
        break;
    }
    case EL_ELLIPSE:
    case EL_TRIANGLE:
    case EL_STAR:
    case EL_HEART: {
        Vector2 p[MAXPTS], c;
        int n = shape_points(e, p, &c);
        poly_fill(c, p, n, fill);
        if (e->strokeW > 0) poly_outline(p, n, e->strokeW, stroke);
        break;
    }
    case EL_LINE:
    case EL_ARROW: {
        float t = e->strokeW > 1 ? e->strokeW : 1;
        float endX = hw;
        if (e->type == EL_ARROW) {
            float head = t * 3.2f;
            if (head > e->w * 0.5f) head = e->w * 0.5f;
            endX = hw - head * 0.8f;
            tri((Vector2){ hw, 0 }, (Vector2){ hw - head, -head * 0.75f }, (Vector2){ hw - head, head * 0.75f }, fill);
        }
        DrawLineEx((Vector2){ -hw + t / 2, 0 }, (Vector2){ endX, 0 }, t, fill);
        DrawCircleV((Vector2){ -hw + t / 2, 0 }, t / 2, fill);
        if (e->type == EL_LINE) DrawCircleV((Vector2){ hw - t / 2, 0 }, t / 2, fill);
        break;
    }
    case EL_TEXT:
        draw_text_element(e, fill);
        break;
    case EL_IMAGE: {
        Texture2D tex = asset_texture(e->asset);
        if (tex.id != 0) {
            Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
            Rectangle dst = { -hw, -hh, e->w, e->h };
            DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0, with_opacity(e->tint ? e->fill : WHITE, e->opacity));
            if (e->strokeW > 0) DrawRectangleLinesEx(dst, e->strokeW, stroke);
        } else {
            DrawRectangle((int)-hw, (int)-hh, (int)e->w, (int)e->h, LIGHTGRAY);
        }
        break;
    }
    }
    rlPopMatrix();
}

void render_document(const Document *d, bool drawBackground)
{
    if (drawBackground) DrawRectangle(0, 0, d->w, d->h, d->bg);
    for (int i = 0; i < d->count; i++) render_element(&d->el[i]);
}

// ------------------------------------------------------------------ Exportar

Image render_to_image(const Document *d, float scale)
{
    int w = (int)(d->w * scale), h = (int)(d->h * scale);
    // Dibujamos al doble y luego reducimos: así los bordes quedan suaves
    int ss = 2;
    if (w * ss > 8192 || h * ss > 8192) ss = 1;
    RenderTexture2D rt = LoadRenderTexture(w * ss, h * ss);
    BeginTextureMode(rt);
    ClearBackground(BLANK);
    rlPushMatrix();
    rlScalef(scale * ss, scale * ss, 1);
    // Fondo y elementos usan mezcla normal; para que la transparencia quede bien
    // en PNG con fondo transparente usamos la mezcla con alfa separado.
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    render_document(d, true);
    EndBlendMode();
    rlPopMatrix();
    EndTextureMode();
    Image img = LoadImageFromTexture(rt.texture);
    UnloadRenderTexture(rt);
    ImageFlipVertical(&img);
    if (ss > 1) ImageResize(&img, w, h);
    return img;
}

Image render_thumbnail(const Document *d, int maxSide)
{
    float s = (float)maxSide / (d->w > d->h ? d->w : d->h);
    return render_to_image(d, s);
}

bool export_png(const Document *d, const char *path)
{
    Image img = render_to_image(d, 1.0f);
    int size = 0;
    unsigned char *png = ExportImageToMemory(img, ".png", &size);
    UnloadImage(img);
    if (!png) return false;
    bool ok = plat_write_file(path, png, size);
    MemFree(png);
    return ok;
}

// PDF muy sencillo: una página con la imagen del diseño.
// Tamaño de página: 150 píxeles = 1 pulgada (así un A4 de 1240x1754 px mide 21x29,7 cm).
static unsigned adler32(const unsigned char *d, int n)
{
    unsigned a = 1, b = 0;
    for (int i = 0; i < n; i++) { a = (a + d[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

bool export_pdf(const Document *d, const char *path)
{
    Image img = render_to_image(d, 1.0f);
    // El PDF no tiene transparencia: ponemos el diseño sobre blanco
    Image rgb = GenImageColor(img.width, img.height, WHITE);
    ImageDraw(&rgb, img, (Rectangle){ 0, 0, (float)img.width, (float)img.height },
              (Rectangle){ 0, 0, (float)img.width, (float)img.height }, WHITE);
    UnloadImage(img);
    ImageFormat(&rgb, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    int rawSize = rgb.width * rgb.height * 3;

    // Comprimimos (deflate) y añadimos la cabecera zlib que pide el PDF
    int defSize = 0;
    unsigned char *def = CompressData((unsigned char *)rgb.data, rawSize, &defSize);
    unsigned char *z = NULL;
    int zSize = 0;
    if (def) {
        zSize = defSize + 6;
        z = (unsigned char *)malloc(zSize);
        z[0] = 0x78; z[1] = 0x9C;
        memcpy(z + 2, def, defSize);
        unsigned ad = adler32((unsigned char *)rgb.data, rawSize);
        z[zSize - 4] = (ad >> 24) & 0xFF; z[zSize - 3] = (ad >> 16) & 0xFF;
        z[zSize - 2] = (ad >> 8) & 0xFF;  z[zSize - 1] = ad & 0xFF;
        MemFree(def);
    }

    float pw = d->w * 72.0f / 150.0f, ph = d->h * 72.0f / 150.0f;
    size_t cap = (size_t)(z ? zSize : rawSize) + 4096;
    char *out = (char *)malloc(cap);
    size_t len = 0;
    long offs[6];
    char content[256];
    int contentLen = snprintf(content, sizeof(content), "q %.2f 0 0 %.2f 0 0 cm /Im0 Do Q\n", pw, ph);

#define PUT(...) len += (size_t)snprintf(out + len, cap - len, __VA_ARGS__)
    PUT("%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n");
    offs[1] = (long)len; PUT("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    offs[2] = (long)len; PUT("2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
    offs[3] = (long)len; PUT("3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f] "
                             "/Resources << /XObject << /Im0 4 0 R >> >> /Contents 5 0 R >>\nendobj\n", pw, ph);
    offs[4] = (long)len;
    PUT("4 0 obj\n<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB "
        "/BitsPerComponent 8 %s/Length %d >>\nstream\n", rgb.width, rgb.height, z ? "/Filter /FlateDecode " : "", z ? zSize : rawSize);
    memcpy(out + len, z ? z : (unsigned char *)rgb.data, z ? zSize : rawSize);
    len += z ? zSize : rawSize;
    PUT("\nendstream\nendobj\n");
    offs[5] = (long)len; PUT("5 0 obj\n<< /Length %d >>\nstream\n%sendstream\nendobj\n", contentLen, content);
    long xref = (long)len;
    PUT("xref\n0 6\n0000000000 65535 f \n");
    for (int i = 1; i <= 5; i++) PUT("%010ld 00000 n \n", offs[i]);
    PUT("trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", xref);
#undef PUT

    bool ok = plat_write_file(path, out, (int)len);
    free(out);
    free(z);
    UnloadImage(rgb);
    return ok;
}
