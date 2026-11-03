// Lienzo Casero: un programa de diseño sencillo para usar en casa.
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "doc.h"
#include "platform.h"
#include "render.h"
#include "magic.h"
#include "pdf.h"
#include "eps.h"
#include "update.h"
#include "svg.h"
#include "templates.h"
#include "ui.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// raylib trae GLFW dentro; lo usamos para poder cancelar el cierre de la ventana
extern void glfwSetWindowShouldClose(void *window, int value);

#define APP_NAME "Lienzo Casero"
#define TOPBAR_H 56
#define TOOLBAR_H 56
#define RAIL_W 78
#define PANEL_W 310
#define BOTTOM_H 40

typedef enum { PANEL_DESIGN, PANEL_TEMPLATES, PANEL_ELEMENTS, PANEL_CLIPART, PANEL_TEXT, PANEL_IMAGES, PANEL_LAYERS, PANEL_COUNT } PanelId;
typedef enum { POP_NONE, POP_FILE, POP_EXPORT, POP_COLOR, POP_FONT, POP_EFFECT } PopupId;
typedef enum { MODAL_NONE, MODAL_NEW, MODAL_SAVE_TEMPLATE, MODAL_SVG } ModalId;
typedef enum { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_ROTATE, DRAG_PAN } DragMode;
typedef enum { COLOR_FILL, COLOR_STROKE, COLOR_BG, COLOR_EFFECT } ColorTarget;

typedef struct { const char *name; int w, h; } SizePreset;
static const SizePreset PRESETS[] = {
    { "Publicación cuadrada (Instagram)", 1080, 1080 },
    { "Historia (Instagram / WhatsApp)", 1080, 1920 },
    { "Folio A4 vertical", 1240, 1754 },
    { "Folio A4 horizontal", 1754, 1240 },
    { "Tarjeta o invitación", 1500, 1050 },
    { "Presentación 16:9", 1920, 1080 },
    { "Póster", 1800, 2400 },
    { "Portada de Facebook", 1640, 624 },
    { "Marcapáginas", 600, 1800 },
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

static const Color PALETTE[] = {
    { 0, 0, 0, 255 },       { 64, 66, 80, 255 },    { 128, 132, 148, 255 }, { 200, 204, 214, 255 }, { 240, 241, 245, 255 }, { 255, 255, 255, 255 },
    { 220, 50, 60, 255 },   { 240, 70, 110, 255 },  { 255, 140, 170, 255 }, { 255, 120, 40, 255 },  { 255, 196, 40, 255 },  { 255, 236, 140, 255 },
    { 30, 150, 80, 255 },   { 110, 200, 90, 255 },  { 31, 181, 166, 255 },  { 150, 225, 215, 255 }, { 40, 110, 220, 255 },  { 120, 180, 255, 255 },
    { 30, 32, 58, 255 },    { 80, 55, 200, 255 },   { 125, 92, 255, 255 },  { 190, 160, 255, 255 }, { 120, 80, 50, 255 },   { 232, 196, 120, 255 },
};
#define PALETTE_COUNT ((int)(sizeof(PALETTE) / sizeof(PALETTE[0])))

static struct {
    Document doc;
    int sel;
    char path[2048];
    bool dirty;
    Camera2D cam;
    Rectangle lastCanvas;

    PanelId panel;
    bool panelOpen;
    float panelScroll;
    float panelContentH;

    PopupId popup;
    Rectangle popupAnchor;
    Rectangle popupRect;
    bool closingPopup;
    ModalId modal;

    ColorTarget colorTarget;
    bool colorPushed;
    float hue, sat, val;
    int colorDrag; // 0 nada, 1 cuadro, 2 tono
    float fontScroll;

    DragMode drag;
    int handle;
    Vector2 dragStartWorld;
    Vector2 dragStartMouse;
    Element dragOrig;
    bool dragPushed;
    Vector2 panStartTarget;

    bool editing;
    bool editPushed;
    int caret;

    Element clipboard;
    bool hasClipboard;

    char toast[300];
    double toastUntil;

    bool snapX, snapY;
    float snapXv, snapYv;

    char templateName[128];
    bool wand;              // lápiz mágico activo
    bool fixText;           // corregir un texto de una imagen
    bool ocrMode;           // textos de la imagen ya leídos y marcados para tocarlos
    int ocrElem, ocrAsset;  // imagen a la que pertenecen
    int ocrPending;         // frames que faltan para leer (así antes se ve el aviso)
    bool replaceOnType;     // al escribir se cambia todo el texto (recién sacado de la imagen)
    float wandTol;
    bool boxDrag;           // dibujando un recuadro con el lápiz
    Vector2 boxStart, boxEnd;
    char svgPath[2048];     // SVG esperando a que elijas "entero" o "por partes"
    Vector2 svgAt;
    char customW[16], customH[16];

    double lastClickTime;
    int lastClickSel;
    bool quit;
} A;

// ------------------------------------------------------------------ Utilidades

static bool ctrl_down(void) { return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL); }
static bool shift_down(void) { return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT); }

static void toast(const char *msg)
{
    strncpy(A.toast, msg, sizeof(A.toast) - 1);
    A.toastUntil = GetTime() + 2.8 + strlen(msg) / 25.0;
}

static void begin_change(void)
{
    history_push(&A.doc);
    A.dirty = true;
}

// Varios objetos seleccionados a la vez (arrastrando un recuadro, Mayús + clic o Ctrl + A).
// Mientras hay varios, A.sel es -1 y la lista está aquí.
static bool g_multi[MAX_ELEMENTS];
static int g_multiN;
static Element g_clipMany[MAX_ELEMENTS];   // varios copiados con Ctrl + C
static int g_clipManyN;
static void multi_clear(void) { memset(g_multi, 0, sizeof(g_multi)); g_multiN = 0; }

static Element *selected(void)
{
    if (A.sel < 0 || A.sel >= A.doc.count) return NULL;
    return &A.doc.el[A.sel];
}

static void fix_text_height(Element *e)
{
    if (e->type == EL_TEXT) e->h = text_height(e);
}

static const char *doc_display_name(void)
{
    if (!A.path[0]) return "Diseño sin título";
    return GetFileNameWithoutExt(A.path);
}

static void update_title(void)
{
    char t[600];
    snprintf(t, sizeof(t), "%s%s - %s", doc_display_name(), A.dirty ? " *" : "", APP_NAME);
    SetWindowTitle(t);
}

static void ensure_ext(char *path, size_t cap, const char *ext)
{
    const char *cur = GetFileExtension(path);
    if (cur && TextIsEqual(TextToLower(cur), ext)) return;
    strncat(path, ext, cap - strlen(path) - 1);
}

// Filas de la barra de herramientas: si no cabe todo en una, pasa a dos (o tres)
static int g_tbRows = 1;
static float g_tbLeft, g_tbRight;
static int g_tbRowsNow;

static Rectangle canvas_rect(void)
{
    float left = RAIL_W + (A.panelOpen ? PANEL_W : 0);
    float top = TOPBAR_H + TOOLBAR_H * g_tbRows;
    return (Rectangle){ left, top, ui_sw() - left, ui_sh() - top - BOTTOM_H };
}

static void fit_view(void)
{
    Rectangle cr = canvas_rect();
    float z = fminf((cr.width - 80) / A.doc.w, (cr.height - 80) / A.doc.h);
    if (z <= 0.01f) z = 0.01f;
    A.cam.zoom = z;
    A.cam.offset = (Vector2){ cr.x + cr.width / 2, cr.y + cr.height / 2 };
    A.cam.target = (Vector2){ A.doc.w / 2.0f, A.doc.h / 2.0f };
    A.cam.rotation = 0;
    A.lastCanvas = cr;
}

static void zoom_at(Vector2 screen, float factor)
{
    Vector2 before = GetScreenToWorld2D(screen, A.cam);
    A.cam.zoom = Clamp(A.cam.zoom * factor, 0.05f, 8.0f);
    Vector2 after = GetScreenToWorld2D(screen, A.cam);
    A.cam.target = Vector2Add(A.cam.target, Vector2Subtract(before, after));
}

static void stop_editing(void)
{
    A.replaceOnType = false;
    A.editing = false;
    A.editPushed = false;
}

static void select_index(int i)
{
    if (i != A.sel) { stop_editing(); A.wand = false; A.fixText = false; A.boxDrag = false; }
    A.sel = i;
    multi_clear();
}

// Deja seleccionados los objetos marcados en "on" (uno solo = selección normal)
static void select_many(const bool *on)
{
    int n = 0, last = -1;
    for (int i = 0; i < A.doc.count; i++) if (on[i]) { n++; last = i; }
    if (n <= 1) { select_index(last); return; }
    select_index(-1);
    for (int i = 0; i < A.doc.count; i++) g_multi[i] = on[i];
    g_multiN = n;
}

static void after_history_change(void)
{
    stop_editing();
    multi_clear();
    if (A.sel >= A.doc.count) A.sel = -1;
    A.dirty = true;
}

static void new_document(int w, int h)
{
    history_clear();
    doc_init(&A.doc, w, h, WHITE);
    A.sel = -1;
    multi_clear();
    A.path[0] = 0;
    A.dirty = false;
    stop_editing();
    fit_view();
}

// ------------------------------------------------------------------ Archivos

static void after_load(void)
{
    for (int i = 0; i < A.doc.count; i++) fix_text_height(&A.doc.el[i]);
    history_clear();
    A.sel = -1;
    stop_editing();
    fit_view();
}

static bool save_to(const char *path)
{
    char p[2048];
    strncpy(p, path, sizeof(p) - 1);
    p[sizeof(p) - 1] = 0;
    ensure_ext(p, sizeof(p), ".lienzo");
    if (!doc_save(&A.doc, p)) {
        plat_message(APP_NAME, "No se ha podido guardar el diseño. Comprueba que la carpeta existe y que tienes permiso.");
        return false;
    }
    strncpy(A.path, p, sizeof(A.path) - 1);
    A.dirty = false;
    toast("Diseño guardado");
    return true;
}

static void cmd_save(bool saveAs)
{
    if (!saveAs && A.path[0]) { save_to(A.path); return; }
    char def[300];
    snprintf(def, sizeof(def), "%s.lienzo", A.path[0] ? doc_display_name() : "Mi diseño");
    const char *p = plat_save_dialog("Guardar diseño", def, FILE_KIND_DESIGN);
    if (p) save_to(p);
}

// true si se puede seguir (no había cambios, se guardaron o el usuario no los quiere)
static bool confirm_discard(void)
{
    if (!A.dirty) return true;
    int r = plat_ask_yes_no_cancel(APP_NAME, "El diseño tiene cambios sin guardar.\n¿Quieres guardarlos antes de continuar?");
    if (r == 1) { cmd_save(false); return !A.dirty; }
    return r == 0;
}

static void open_path(const char *path)
{
    if (!doc_load(&A.doc, path)) {
        plat_message(APP_NAME, "No se ha podido abrir el archivo. ¿Es un diseño de Lienzo Casero (.lienzo)?");
        return;
    }
    strncpy(A.path, path, sizeof(A.path) - 1);
    A.dirty = false;
    after_load();
    toast("Diseño abierto");
}

static void cmd_open(void)
{
    if (!confirm_discard()) return;
    const char *p = plat_open_dialog("Abrir diseño", FILE_KIND_DESIGN);
    if (p) {
        char copy[2048];
        strncpy(copy, p, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = 0;
        open_path(copy);
    }
}

static void cmd_export(bool pdf)
{
    char def[300];
    snprintf(def, sizeof(def), "%s%s", A.path[0] ? doc_display_name() : "Mi diseño", pdf ? ".pdf" : ".png");
    const char *p = plat_save_dialog(pdf ? "Exportar como PDF" : "Exportar como imagen PNG", def, pdf ? FILE_KIND_PDF : FILE_KIND_PNG);
    if (!p) return;
    char path[2048];
    strncpy(path, p, sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    ensure_ext(path, sizeof(path), pdf ? ".pdf" : ".png");
    stop_editing();
    bool ok = pdf ? export_pdf(&A.doc, path) : export_png(&A.doc, path);
    if (ok) {
        char msg[400];
        snprintf(msg, sizeof(msg), "Exportado: %s", GetFileName(path));
        toast(msg);
    } else {
        plat_message(APP_NAME, "No se ha podido exportar el archivo.");
    }
}

static void open_template(int i)
{
    if (!confirm_discard()) return;
    if (doc_load(&A.doc, g_templates[i].path)) {
        A.path[0] = 0;   // es un diseño nuevo basado en la plantilla
        A.dirty = false;
        after_load();
        toast("Plantilla abierta. Guárdala con otro nombre cuando termines.");
    }
}

// ------------------------------------------------------------------ Añadir elementos

static Vector2 doc_center(void) { return (Vector2){ A.doc.w / 2.0f, A.doc.h / 2.0f }; }

static float base_size(void) { return fminf(A.doc.w, A.doc.h) * 0.32f; }

static void add_shape(int type)
{
    begin_change();
    Element *e = doc_add(&A.doc, type);
    if (!e) return;
    float s = base_size();
    e->w = s;
    e->h = (type == EL_LINE || type == EL_ARROW) ? fmaxf(20, s * 0.12f) : s;
    if (type == EL_LINE || type == EL_ARROW) { e->w = s * 1.4f; e->strokeW = fmaxf(4, s * 0.03f); }
    Vector2 c = doc_center();
    e->x = c.x - e->w / 2;
    e->y = c.y - e->h / 2;
    select_index(A.doc.count - 1);
}

static void add_text(const char *text, int font, float sizeFactor)
{
    begin_change();
    Element *e = doc_add(&A.doc, EL_TEXT);
    if (!e) return;
    strncpy(e->text, text, TEXT_MAX - 1);
    e->font = font;
    e->fontSize = roundf(fmaxf(16, fminf(A.doc.w, A.doc.h) * sizeFactor));
    e->w = A.doc.w * 0.7f;
    e->align = ALIGN_CENTER;
    fix_text_height(e);
    Vector2 c = doc_center();
    e->x = c.x - e->w / 2;
    e->y = c.y - e->h / 2;
    select_index(A.doc.count - 1);
}

static void add_image_asset(int asset, Vector2 center)
{
    if (asset < 0) return;
    begin_change();
    Element *e = doc_add(&A.doc, EL_IMAGE);
    if (!e) return;
    e->asset = asset;
    float aw = (float)g_assets[asset].w, ah = (float)g_assets[asset].h;
    float s = fminf(A.doc.w * 0.6f / aw, A.doc.h * 0.6f / ah);
    e->w = aw * s;
    e->h = ah * s;
    e->x = center.x - e->w / 2;
    e->y = center.y - e->h / 2;
    e->fill = WHITE;
    e->stroke = WHITE;
    select_index(A.doc.count - 1);
}

// Mete cada parte del SVG como una imagen suelta, en su sitio
static int import_svg_parts(const char *path, int mode, Vector2 center)
{
    int size = 0;
    unsigned char *data = plat_read_file(path, &size);
    if (!data) return 0;
    static SvgPart parts[SVG_MAX_PARTS];
    float sw, sh;
    int how;
    int n = svg_split(data, size, 1800, mode, parts, SVG_MAX_PARTS, &sw, &sh, &how);
    free(data);
    if (n < 1) return 0;
    float k = fminf(A.doc.w * 0.8f / sw, A.doc.h * 0.8f / sh);
    float ox = center.x - sw * k / 2, oy = center.y - sh * k / 2;
    begin_change();
    int added = 0;
    for (int i = 0; i < n; i++) {
        int id = asset_add_image(parts[i].img);
        if (id < 0) continue;
        g_assets[id].hidden = true;
        Element *e = doc_add(&A.doc, EL_IMAGE);
        if (!e) break;
        e->asset = id;
        e->x = ox + parts[i].x * k;
        e->y = oy + parts[i].y * k;
        e->w = parts[i].w * k;
        e->h = parts[i].h * k;
        e->tint = parts[i].single;
        e->fill = parts[i].single ? parts[i].color : WHITE;
        e->stroke = WHITE;
        snprintf(e->text, TEXT_MAX, "%s", parts[i].name);
        added++;
    }
    svg_free_parts(parts, n);
    if (added) select_index(A.doc.count - 1);
    return added;
}

static void ask_svg(const char *path, Vector2 at)
{
    snprintf(A.svgPath, sizeof(A.svgPath), "%s", path);
    A.svgAt = at;
    A.modal = MODAL_SVG;
}

// ---- Importar PDF ----

static void norm_name(const char *in, char *out, int max)
{
    int n = 0;
    for (const char *p = in; *p && n < max - 1; p++)
        if (isalnum((unsigned char)*p)) out[n++] = (char)tolower((unsigned char)*p);
    out[n] = 0;
}

// Elige la fuente nuestra que más se parece a la del PDF
static int match_font(const PdfItem *it)
{
    char full[80], fam[80];
    norm_name(it->font, full, sizeof(full));
    char famRaw[80];
    snprintf(famRaw, sizeof(famRaw), "%s", it->font);
    char *cut = strpbrk(famRaw, "-,_");
    if (cut) *cut = 0;
    norm_name(famRaw, fam, sizeof(fam));
    // Quitamos "MT", "PS", "Std" del final de la familia
    int best = -1, bestScore = 0;
    for (int i = 0; i < g_fontCount; i++) {
        char nm[80], file[80];
        norm_name(g_fonts[i].name, nm, sizeof(nm));
        norm_name(GetFileNameWithoutExt(g_fonts[i].path), file, sizeof(file));
        int score = 0;
        if (full[0] && (strcmp(file, full) == 0)) score = 100;
        else if (fam[0] && strlen(fam) >= 4 && (strncmp(file, fam, strlen(fam)) == 0 || strncmp(nm, fam, strlen(fam)) == 0)) {
            score = 50;
            bool fb = strstr(file, "bold") || strstr(file, "black") || strstr(nm, "negrita");
            if (fb == it->bold) score += 10;
        }
        if (score > bestScore) { bestScore = score; best = i; }
    }
    if (best >= 0) return best;
    const char *fallback = it->script ? "Manuscrita" : it->serif ? (it->bold ? "Clásica negrita" : "Clásica") : (it->bold ? "Moderna negrita" : "Moderna");
    int f = font_find(fallback);
    return f >= 0 ? f : 0;
}

static bool import_pdf(const char *path)
{
    int size = 0;
    unsigned char *data = plat_read_file(path, &size);
    if (!data) return false;
    PdfResult r;
    bool ok = pdf_import(data, size, 0, 150.0f / 72.0f, &r);
    free(data);
    if (!ok || r.pageW < 16 || r.pageH < 16) { pdf_free(&r); return false; }
    new_document((int)r.pageW, (int)r.pageH);
    int start = 0;
    // Un rectángulo de un color que cubre toda la página es el fondo
    if (r.count > 0 && r.items[0].kind == PDF_SHAPE && r.items[0].tint && r.items[0].w >= r.pageW * 0.98f && r.items[0].h >= r.pageH * 0.98f) {
        A.doc.bg = r.items[0].color;
        start = 1;
    }
    int added = 0, texts = 0;
    for (int i = start; i < r.count; i++) {
        PdfItem *it = &r.items[i];
        if (it->kind == PDF_TEXT) {
            Element *e = doc_add(&A.doc, EL_TEXT);
            if (!e) break;
            snprintf(e->text, TEXT_MAX, "%s", it->text);
            e->font = match_font(it);
            float em = font_em_ratio(e->font);
            e->fontSize = fmaxf(4, roundf(it->fontSize * em * 10) / 10);
            e->fill = it->color;
            e->align = it->align == 1 ? ALIGN_CENTER : it->align == 2 ? ALIGN_RIGHT : ALIGN_LEFT;
            // El interlineado del PDF va en "em"; el nuestro, en alto de letra
            e->lineSpacing = it->lineSpacing > 0.3f ? it->lineSpacing / em : 0;
            // Un poco más ancho: nuestras fuentes pueden ocupar algo más que las del PDF
            e->w = it->w * 1.12f + e->fontSize * 0.5f;
            if (e->align == ALIGN_CENTER) e->x = it->x + it->w / 2 - e->w / 2;
            else if (e->align == ALIGN_RIGHT) e->x = it->x + it->w - e->w;
            else e->x = it->x;
            float ls = e->lineSpacing > 0.3f ? e->lineSpacing : 1.18f;
            e->y = it->baseline - (e->fontSize * (ls - 1) / 2 + font_ascent_ratio(e->font) * e->fontSize);
            e->rotation = it->rotation;
            fix_text_height(e);
            texts++;
        } else {
            int id = asset_add_image(it->img);
            if (id < 0) continue;
            if (it->kind == PDF_SHAPE) g_assets[id].hidden = true;
            Element *e = doc_add(&A.doc, EL_IMAGE);
            if (!e) break;
            e->asset = id;
            e->x = it->x; e->y = it->y; e->w = it->w; e->h = it->h;
            e->rotation = it->rotation;
            e->opacity = it->opacity > 0 ? it->opacity : 1;
            e->tint = it->tint;
            e->fill = it->tint ? it->color : WHITE;
            e->stroke = WHITE;
            snprintf(e->text, TEXT_MAX, "%s", it->kind == PDF_SHAPE ? "Dibujo" : "Imagen");
        }
        added++;
    }
    char msg[200];
    if (r.pages > 1) snprintf(msg, sizeof(msg), "PDF abierto (página 1 de %d): %d piezas, %d textos para editar", r.pages, added, texts);
    else snprintf(msg, sizeof(msg), "PDF abierto: %d piezas, %d textos para editar", added, texts);
    toast(msg);
    pdf_free(&r);
    A.panel = PANEL_LAYERS;
    A.panelOpen = true;
    A.panelScroll = 0;
    fit_view();
    return true;
}

// Un .ai moderno es un PDF por dentro: metemos sus piezas en el diseño que ya está abierto,
// en pequeño y centradas en "at", sin cambiar de documento.
static bool add_pdf_drawing(const unsigned char *data, int size, Vector2 at)
{
    PdfResult r;
    if (!pdf_import(data, size, 0, 150.0f / 72.0f, &r) || r.count == 0 || r.pageW < 4 || r.pageH < 4) { pdf_free(&r); return false; }
    float s = fminf(1.0f, fminf(A.doc.w * 0.8f / r.pageW, A.doc.h * 0.8f / r.pageH));
    float ox = at.x - r.pageW * s / 2, oy = at.y - r.pageH * s / 2;
    begin_change();
    int added = 0;
    for (int i = 0; i < r.count; i++) {
        PdfItem *it = &r.items[i];
        if (it->kind == PDF_TEXT) {
            Element *e = doc_add(&A.doc, EL_TEXT);
            if (!e) break;
            snprintf(e->text, TEXT_MAX, "%s", it->text);
            e->font = match_font(it);
            float em = font_em_ratio(e->font);
            e->fontSize = fmaxf(4, roundf(it->fontSize * s * em * 10) / 10);
            e->fill = it->color;
            e->align = it->align == 1 ? ALIGN_CENTER : it->align == 2 ? ALIGN_RIGHT : ALIGN_LEFT;
            e->lineSpacing = it->lineSpacing > 0.3f ? it->lineSpacing / em : 0;
            e->w = it->w * s * 1.12f + e->fontSize * 0.5f;
            float x = ox + it->x * s;
            if (e->align == ALIGN_CENTER) e->x = x + it->w * s / 2 - e->w / 2;
            else if (e->align == ALIGN_RIGHT) e->x = x + it->w * s - e->w;
            else e->x = x;
            float ls = e->lineSpacing > 0.3f ? e->lineSpacing : 1.18f;
            e->y = oy + it->baseline * s - (e->fontSize * (ls - 1) / 2 + font_ascent_ratio(e->font) * e->fontSize);
            e->rotation = it->rotation;
            fix_text_height(e);
        } else {
            int id = asset_add_image(it->img);
            if (id < 0) continue;
            if (it->kind == PDF_SHAPE) g_assets[id].hidden = true;
            Element *e = doc_add(&A.doc, EL_IMAGE);
            if (!e) break;
            e->asset = id;
            e->x = ox + it->x * s; e->y = oy + it->y * s; e->w = it->w * s; e->h = it->h * s;
            e->rotation = it->rotation;
            e->opacity = it->opacity > 0 ? it->opacity : 1;
            e->tint = it->tint;
            e->fill = it->tint ? it->color : WHITE;
            e->stroke = WHITE;
            snprintf(e->text, TEXT_MAX, "%s", it->kind == PDF_SHAPE ? "Dibujo" : "Imagen");
        }
        added++;
    }
    pdf_free(&r);
    if (added == 0) return false;
    char m[120];
    snprintf(m, sizeof(m), "Dibujo añadido en %d partes. Míralas en \"Capas\".", added);
    toast(m);
    return true;
}

// Dibujos EPS y AI (los que bajas de Vecteezy, Freepik...). Un EPS o un .ai antiguo es
// PostScript: lo pasamos a SVG y se abre como cualquier SVG (entero o por partes).
static void import_vector(const char *path, Vector2 at)
{
    int size = 0;
    unsigned char *data = plat_read_file(path, &size);
    if (!data) { plat_message(APP_NAME, "No se ha podido abrir ese archivo."); return; }
    // Un .ai moderno empieza por %PDF (a veces tras unos bytes de cabecera)
    bool isPdf = false;
    for (int i = 0; i + 4 <= size && i < 1024; i++) if (memcmp(data + i, "%PDF", 4) == 0) { isPdf = true; break; }
    if (isPdf) {
        bool ok = add_pdf_drawing(data, size, at);
        free(data);
        if (!ok) plat_message(APP_NAME, "No se ha podido leer ese dibujo AI. Si puedes, descarga el mismo dibujo en EPS o SVG.");
        return;
    }
    int skipped = 0;
    char *svg = eps_to_svg(data, size, &skipped);
    free(data);
    if (!svg) { plat_message(APP_NAME, "No se ha podido leer ese dibujo. Si puedes, descarga el mismo dibujo en SVG."); return; }
    char out[1200];
#ifdef _WIN32
    snprintf(out, sizeof(out), "%s\\%s.svg", plat_data_dir(), GetFileNameWithoutExt(path));
#else
    snprintf(out, sizeof(out), "%s/%s.svg", plat_data_dir(), GetFileNameWithoutExt(path));
#endif
    bool ok = plat_write_file(out, svg, (int)strlen(svg));
    free(svg);
    if (!ok) { plat_message(APP_NAME, "No se ha podido preparar ese dibujo."); return; }
    if (skipped > 0) toast("Algunos detalles del dibujo (degradados o fotos) no se han podido copiar.");
    ask_svg(out, at);
}

static bool is_vector_file(const char *p) { return IsFileExtension(p, ".eps;.EPS;.ai;.AI;.epsf;.ps"); }

static void cmd_import_pdf(void)
{
    if (!confirm_discard()) return;
    const char *p = plat_open_dialog("Importar un PDF", FILE_KIND_PDF);
    if (!p) return;
    if (!import_pdf(p)) plat_message(APP_NAME, "No se ha podido leer ese PDF. Si lo descargaste de Canva, prueba a descargarlo otra vez como \"PDF estándar\".");
}

static void cmd_upload_image(void)
{
    const char *p = plat_open_dialog("Elegir una imagen", FILE_KIND_IMAGE);
    if (!p) return;
    if (IsFileExtension(p, ".svg;.SVG")) { ask_svg(p, doc_center()); return; }
    if (is_vector_file(p)) { import_vector(p, doc_center()); return; }
    int id = asset_load_file(p);
    if (id < 0) { plat_message(APP_NAME, "No se ha podido leer esa imagen. Prueba con una foto PNG o JPG."); return; }
    add_image_asset(id, doc_center());
}

static void cmd_add_font(void)
{
    const char *p = plat_open_dialog("Añadir una fuente", FILE_KIND_FONT);
    if (!p) return;
    int idx = font_install_file(p);
    if (idx < 0) { plat_message(APP_NAME, "No se ha podido usar ese archivo. Tiene que ser una fuente .ttf o .otf."); return; }
    char msg[200];
    snprintf(msg, sizeof(msg), "Fuente añadida: %s", font_name(idx));
    toast(msg);
    Element *e = selected();
    if (e && e->type == EL_TEXT) { begin_change(); e->font = idx; fix_text_height(e); }
}

static void cmd_delete(void)
{
    if (g_multiN >= 2) {
        begin_change();
        for (int i = A.doc.count - 1; i >= 0; i--) if (g_multi[i] && !A.doc.el[i].locked) doc_remove(&A.doc, i);
        select_index(-1);
        return;
    }
    Element *e = selected();
    if (!e) return;
    begin_change();
    doc_remove(&A.doc, A.sel);
    A.sel = -1;
    stop_editing();
}

static void cmd_duplicate(void)
{
    if (g_multiN >= 2) {
        static bool on[MAX_ELEMENTS];
        memset(on, 0, sizeof(on));
        begin_change();
        int n = A.doc.count;
        for (int i = 0; i < n && A.doc.count < MAX_ELEMENTS; i++) {
            if (!g_multi[i]) continue;
            Element c = A.doc.el[i];
            c.x += 30; c.y += 30; c.locked = false;
            on[A.doc.count] = true;
            A.doc.el[A.doc.count++] = c;
        }
        select_many(on);
        return;
    }
    if (!selected()) return;
    begin_change();
    select_index(doc_duplicate(&A.doc, A.sel));
}

static void cmd_layer(int delta)
{
    if (!selected()) return;
    begin_change();
    A.sel = doc_move_layer(&A.doc, A.sel, delta);
}

static void cmd_undo(void) { if (history_undo(&A.doc)) after_history_change(); }
static void cmd_redo(void) { if (history_redo(&A.doc)) after_history_change(); }

static void cmd_paste(void)
{
#if defined(_WIN32)
    Image img = GetClipboardImage();
    if (img.data) {
        int id = asset_add_image(img);
        UnloadImage(img);
        if (id >= 0) { add_image_asset(id, doc_center()); return; }
    }
#endif
    if (g_clipManyN > 0) {
        static bool on[MAX_ELEMENTS];
        memset(on, 0, sizeof(on));
        begin_change();
        for (int k = 0; k < g_clipManyN && A.doc.count < MAX_ELEMENTS; k++) {
            g_clipMany[k].x += 30;
            g_clipMany[k].y += 30;
            on[A.doc.count] = true;
            A.doc.el[A.doc.count++] = g_clipMany[k];
        }
        select_many(on);
        return;
    }
    if (A.hasClipboard && A.doc.count < MAX_ELEMENTS) {
        begin_change();
        A.clipboard.x += 30;
        A.clipboard.y += 30;
        A.doc.el[A.doc.count++] = A.clipboard;
        select_index(A.doc.count - 1);
    }
}

static void cmd_copy(void)
{
    g_clipManyN = 0;
    if (g_multiN >= 2) {
        for (int i = 0; i < A.doc.count; i++)
            if (g_multi[i]) { g_clipMany[g_clipManyN] = A.doc.el[i]; g_clipMany[g_clipManyN++].locked = false; }
        A.hasClipboard = false;
        SetClipboardText(" ");
        return;
    }
    Element *e = selected();
    if (!e) return;
    A.clipboard = *e;
    A.clipboard.locked = false;
    A.hasClipboard = true;
    SetClipboardText(e->type == EL_TEXT ? e->text : " "); // vacía una imagen copiada antes
}

// ------------------------------------------------------------------ Geometría de selección

static Vector2 el_center(const Element *e) { return (Vector2){ e->x + e->w / 2, e->y + e->h / 2 }; }

static Vector2 to_local(const Element *e, Vector2 w)
{
    return Vector2Rotate(Vector2Subtract(w, el_center(e)), -e->rotation * DEG2RAD);
}

static Vector2 to_world(const Element *e, Vector2 l)
{
    return Vector2Add(el_center(e), Vector2Rotate(l, e->rotation * DEG2RAD));
}

static bool hit_element(const Element *e, Vector2 w, float pad)
{
    Vector2 l = to_local(e, w);
    float hh = e->h / 2;
    if (e->type == EL_LINE || e->type == EL_ARROW) hh = fmaxf(hh, pad * 2);
    return fabsf(l.x) <= e->w / 2 + pad && fabsf(l.y) <= hh + pad;
}

// En las imágenes con transparencia (partes de un SVG, dibujos) solo cuenta lo que tiene color
static bool hit_opaque(const Element *e, Vector2 w)
{
    if (e->type != EL_IMAGE) return true;
    Vector2 l = to_local(e, w);
    float u = (l.x + e->w / 2) / e->w, v = (l.y + e->h / 2) / e->h;
    return asset_opaque_at(e->asset, u, v);
}

static int pick(Vector2 w)
{
    for (int i = A.doc.count - 1; i >= 0; i--)
        if (A.doc.el[i].visible && hit_element(&A.doc.el[i], w, 2 / A.cam.zoom) && hit_opaque(&A.doc.el[i], w)) return i;
    for (int i = A.doc.count - 1; i >= 0; i--)
        if (A.doc.el[i].visible && hit_element(&A.doc.el[i], w, 2 / A.cam.zoom)) return i;
    return -1;
}

static const int HX[8] = { -1, 0, 1, 1, 1, 0, -1, -1 };
static const int HY[8] = { -1, -1, -1, 0, 1, 1, 1, 0 };

static bool handle_enabled(const Element *e, int h)
{
    if (h == 8) return true;
    if (e->type == EL_LINE || e->type == EL_ARROW) return h == 3 || h == 7;
    if (e->type == EL_TEXT) return HY[h] != 0 ? HX[h] != 0 : true;
    return true;
}

static Vector2 handle_world(const Element *e, int h)
{
    if (h == 8) return to_world(e, (Vector2){ 0, -e->h / 2 - 34 / A.cam.zoom });
    return to_world(e, (Vector2){ HX[h] * e->w / 2, HY[h] * e->h / 2 });
}

static int handle_at(const Element *e, Vector2 screen)
{
    for (int h = 8; h >= 0; h--) {
        if (!handle_enabled(e, h)) continue;
        Vector2 s = GetWorldToScreen2D(handle_world(e, h), A.cam);
        if (Vector2Distance(s, screen) <= 10) return h;
    }
    return -1;
}

static int caret_from_point(const Element *e, Vector2 world)
{
    Vector2 l = to_local(e, world);
    int len = (int)strlen(e->text);
    int best = len;
    float bestD = 1e9f;
    for (int i = 0; i <= len; i++) {
        if (i < len && ((unsigned char)e->text[i] & 0xC0) == 0x80) continue;
        float lh;
        Vector2 p = text_caret_pos(e, i, &lh);
        float dy = 0;
        if (l.y < p.y) dy = p.y - l.y;
        else if (l.y > p.y + lh) dy = l.y - (p.y + lh);
        float d = dy * 1000 + fabsf(l.x - p.x);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// ------------------------------------------------------------------ Ratón sobre el lienzo

static void apply_resize(Element *e, Vector2 mouseWorld)
{
    const Element *o = &A.dragOrig;
    int hx = HX[A.handle], hy = HY[A.handle];
    Vector2 lp = to_local(o, mouseWorld);
    float l = -o->w / 2, r = o->w / 2, t = -o->h / 2, b = o->h / 2;
    float minS = 8;
    if (hx == 1) r = fmaxf(lp.x, l + minS);
    if (hx == -1) l = fminf(lp.x, r - minS);
    if (hy == 1) b = fmaxf(lp.y, t + minS);
    if (hy == -1) t = fminf(lp.y, b - minS);

    bool corner = hx != 0 && hy != 0;
    bool keepAspect = corner && (o->type == EL_IMAGE || o->type == EL_TEXT) ? !shift_down() : (corner && shift_down());
    if (keepAspect) {
        float s = fmaxf((r - l) / o->w, (b - t) / o->h);
        float nw = o->w * s, nh = o->h * s;
        if (hx == 1) r = l + nw; else l = r - nw;
        if (hy == 1) b = t + nh; else t = b - nh;
    }
    float nw = r - l, nh = b - t;
    if (o->type == EL_TEXT) {
        if (corner) {
            float s = nw / o->w;
            e->fontSize = fmaxf(6, o->fontSize * s);
        }
        e->w = nw;
        fix_text_height(e);
        nh = e->h;
        if (hy == -1) t = b - nh; else b = t + nh;
        if (hy == 0) { t = -o->h / 2; b = t + nh; }
    }
    if (o->type == EL_LINE || o->type == EL_ARROW) { t = -o->h / 2; b = o->h / 2; nh = o->h; }
    Vector2 lc = { (l + r) / 2, (t + b) / 2 };
    Vector2 wc = Vector2Add(el_center(o), Vector2Rotate(lc, o->rotation * DEG2RAD));
    e->w = nw;
    e->h = nh;
    e->x = wc.x - nw / 2;
    e->y = wc.y - nh / 2;
}

static void apply_move(Element *e, Vector2 mouseWorld)
{
    Vector2 d = Vector2Subtract(mouseWorld, A.dragStartWorld);
    e->x = A.dragOrig.x + d.x;
    e->y = A.dragOrig.y + d.y;
    // Imán: centrar y alinear con los bordes del lienzo
    A.snapX = A.snapY = false;
    if (shift_down()) return;
    float th = 7 / A.cam.zoom;
    Rectangle b = element_bounds(e);
    float xs[3] = { b.x, b.x + b.width / 2, b.x + b.width };
    float tx[3] = { 0, A.doc.w / 2.0f, (float)A.doc.w };
    float bestX = th, dx = 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (fabsf(xs[i] - tx[j]) < bestX) { bestX = fabsf(xs[i] - tx[j]); dx = tx[j] - xs[i]; A.snapX = true; A.snapXv = tx[j]; }
    float ys[3] = { b.y, b.y + b.height / 2, b.y + b.height };
    float ty[3] = { 0, A.doc.h / 2.0f, (float)A.doc.h };
    float bestY = th, dy = 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (fabsf(ys[i] - ty[j]) < bestY) { bestY = fabsf(ys[i] - ty[j]); dy = ty[j] - ys[i]; A.snapY = true; A.snapYv = ty[j]; }
    e->x += dx;
    e->y += dy;
}

static void apply_rotate(Element *e, Vector2 mouseWorld)
{
    Vector2 c = el_center(e);
    float a = atan2f(mouseWorld.y - c.y, mouseWorld.x - c.x) * RAD2DEG + 90;
    if (shift_down()) a = roundf(a / 15) * 15;
    else {
        float near45 = roundf(a / 45) * 45;
        if (fabsf(a - near45) < 4) a = near45;
    }
    while (a > 180) a -= 360;
    while (a <= -180) a += 360;
    e->rotation = a;
}

// ------------------------------------------------------------------ Varios objetos a la vez

static Rectangle multi_bounds(void)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < A.doc.count; i++) {
        if (!g_multi[i]) continue;
        Rectangle b = element_bounds(&A.doc.el[i]);
        x0 = fminf(x0, b.x); y0 = fminf(y0, b.y);
        x1 = fmaxf(x1, b.x + b.width); y1 = fmaxf(y1, b.y + b.height);
    }
    if (x1 < x0) return (Rectangle){ 0 };
    return (Rectangle){ x0, y0, x1 - x0, y1 - y0 };
}

static void select_all(void)
{
    static bool on[MAX_ELEMENTS];
    for (int i = 0; i < A.doc.count; i++) on[i] = A.doc.el[i].visible && !A.doc.el[i].locked;
    select_many(on);
    if (g_multiN >= 2) {
        char m[80];
        snprintf(m, sizeof(m), "%d objetos seleccionados", g_multiN);
        toast(m);
    }
}

// Mayús + clic: añade o quita un objeto de la selección
static void toggle_in_selection(int hit)
{
    static bool on[MAX_ELEMENTS];
    for (int i = 0; i < A.doc.count; i++) on[i] = g_multiN >= 2 ? g_multi[i] : i == A.sel;
    on[hit] = !on[hit];
    select_many(on);
}

static int g_groupDrag;          // 0 = nada, 1 = mover, 2 = cambiar el tamaño
static int g_groupCorner;
static Element g_groupOrig[MAX_ELEMENTS];
static Rectangle g_groupBox0;
static Vector2 g_groupStart, g_groupStartMouse;
static bool g_groupPushed;
static bool g_marquee;           // arrastrando un recuadro para elegir objetos
static Vector2 g_mqStart, g_mqEnd;
static int g_mqHit;

static Vector2 box_corner(Rectangle b, int k)
{
    return (Vector2){ (k == 1 || k == 2) ? b.x + b.width : b.x, k >= 2 ? b.y + b.height : b.y };
}

static int group_corner_at(Vector2 screen)
{
    Rectangle b = multi_bounds();
    for (int k = 0; k < 4; k++)
        if (Vector2Distance(GetWorldToScreen2D(box_corner(b, k), A.cam), screen) <= 11) return k;
    return -1;
}

static void group_begin(int mode, int corner, Vector2 w, Vector2 m)
{
    g_groupDrag = mode;
    g_groupCorner = corner;
    g_groupBox0 = multi_bounds();
    g_groupStart = w;
    g_groupStartMouse = m;
    g_groupPushed = false;
    for (int i = 0; i < A.doc.count; i++) g_groupOrig[i] = A.doc.el[i];
}

static void group_update(Vector2 w, Vector2 m)
{
    if (!g_groupPushed && Vector2Distance(m, g_groupStartMouse) > 2) { begin_change(); g_groupPushed = true; }
    if (!g_groupPushed) return;
    if (g_groupDrag == 1) {
        Vector2 d = Vector2Subtract(w, g_groupStart);
        for (int i = 0; i < A.doc.count; i++) {
            if (!g_multi[i] || A.doc.el[i].locked) continue;
            A.doc.el[i].x = g_groupOrig[i].x + d.x;
            A.doc.el[i].y = g_groupOrig[i].y + d.y;
        }
        return;
    }
    // Esquina: todo crece o encoge a la vez, desde la esquina contraria
    Vector2 anchor = box_corner(g_groupBox0, (g_groupCorner + 2) % 4);
    Vector2 c0 = Vector2Subtract(box_corner(g_groupBox0, g_groupCorner), anchor);
    Vector2 v = Vector2Subtract(w, anchor);
    float dd = c0.x * c0.x + c0.y * c0.y;
    float sc = dd > 0 ? (v.x * c0.x + v.y * c0.y) / dd : 1;
    if (sc < 0.05f) sc = 0.05f;
    for (int i = 0; i < A.doc.count; i++) {
        if (!g_multi[i] || A.doc.el[i].locked) continue;
        const Element *o = &g_groupOrig[i];
        Element *e = &A.doc.el[i];
        Vector2 c = Vector2Add(anchor, Vector2Scale(Vector2Subtract(el_center(o), anchor), sc));
        e->w = o->w * sc;
        e->h = o->h * sc;
        e->strokeW = o->strokeW * sc;
        if (e->type == EL_TEXT) { e->fontSize = fmaxf(4, o->fontSize * sc); fix_text_height(e); }
        e->x = c.x - e->w / 2;
        e->y = c.y - e->h / 2;
    }
}

// Lápiz mágico: separa la parte tocada de la imagen seleccionada en una capa nueva
static void wand_at(int index, Vector2 w)
{
    Element *e = &A.doc.el[index];
    if (e->asset < 0 || e->asset >= g_assetCount) return;
    Asset *as = &g_assets[e->asset];
    Image src = LoadImageFromMemory(".png", as->png, as->pngSize);
    if (!src.data) return;
    Vector2 l = to_local(e, w);
    int px = (int)((l.x + e->w / 2) / e->w * src.width), py = (int)((l.y + e->h / 2) / e->h * src.height);
    Image part, rest;
    int bx, by;
    int ok = magic_extract(src, px, py, A.wandTol, &part, &bx, &by, &rest);
    UnloadImage(src);
    if (!ok) { toast("Ahí solo hay fondo. Toca encima de un dibujo o de un texto."); return; }
    int partId = asset_add_image(part), restId = asset_add_image(rest);
    // La parte separada sale en "Imágenes usadas" (para volver a ponerla); el resto no
    if (restId >= 0) g_assets[restId].hidden = true;
    float sx = e->w / (float)rest.width, sy = e->h / (float)rest.height;
    UnloadImage(part);
    UnloadImage(rest);
    if (partId < 0 || restId < 0) { toast("No caben más imágenes en este diseño"); return; }
    begin_change();
    e = &A.doc.el[index];
    Element orig = *e;
    Element *n = doc_add(&A.doc, EL_IMAGE);
    if (!n) { toast("No caben más elementos en este diseño"); return; }
    e = &A.doc.el[index];
    e->asset = restId;
    *n = orig;
    n->asset = partId;
    n->w = g_assets[partId].w * sx;
    n->h = g_assets[partId].h * sy;
    Vector2 lc = { (bx + g_assets[partId].w / 2.0f) * sx - orig.w / 2, (by + g_assets[partId].h / 2.0f) * sy - orig.h / 2 };
    Vector2 c = to_world(&orig, lc);
    n->x = c.x - n->w / 2;
    n->y = c.y - n->h / 2;
    n->locked = false;
    snprintf(n->text, TEXT_MAX, "Recorte");
    // Justo encima de la imagen de la que sale
    doc_move_layer(&A.doc, A.doc.count - 1, index + 1 - (A.doc.count - 1));
    A.sel = index;
    toast("Parte separada: ya la puedes mover. Toca otra o pulsa Esc para terminar.");
}

// Recuadro en coordenadas del lienzo -> píxeles de la imagen del elemento
static void box_to_pixels(const Element *e, int imgW, int imgH, int *x0, int *y0, int *x1, int *y1)
{
    Vector2 a = to_local(e, A.boxStart), b = to_local(e, A.boxEnd);
    float ax = (fminf(a.x, b.x) + e->w / 2) / e->w * imgW, bx = (fmaxf(a.x, b.x) + e->w / 2) / e->w * imgW;
    float ay = (fminf(a.y, b.y) + e->h / 2) / e->h * imgH, by = (fmaxf(a.y, b.y) + e->h / 2) / e->h * imgH;
    *x0 = (int)ax; *y0 = (int)ay; *x1 = (int)bx; *y1 = (int)by;
}

// Separa lo que hay dentro del recuadro. asText: en vez de una imagen, deja un texto para escribir
static void wand_box(int index, bool asText)
{
    Element *e = &A.doc.el[index];
    if (e->asset < 0 || e->asset >= g_assetCount) return;
    Asset *as = &g_assets[e->asset];
    Image src = LoadImageFromMemory(".png", as->png, as->pngSize);
    if (!src.data) return;
    int x0, y0, x1, y1;
    box_to_pixels(e, src.width, src.height, &x0, &y0, &x1, &y1);
    Image part, rest;
    int bx, by;
    Color main;
    int ok = magic_extract_box(src, x0, y0, x1, y1, A.wandTol, &part, &bx, &by, &rest, &main);
    UnloadImage(src);
    if (!ok) { toast("En ese recuadro no hay nada que separar del fondo."); return; }
    float sx = e->w / (float)rest.width, sy = e->h / (float)rest.height;
    int pw = part.width, ph = part.height;
    int restId = asset_add_image(rest);
    int partId = asText ? -1 : asset_add_image(part);
    if (restId >= 0) g_assets[restId].hidden = true;
    UnloadImage(part);
    UnloadImage(rest);
    if (restId < 0 || (!asText && partId < 0)) { toast("No caben más imágenes en este diseño"); return; }
    begin_change();
    e = &A.doc.el[index];
    Element orig = *e;
    e->asset = restId;
    Vector2 lc = { (bx + pw / 2.0f) * sx - orig.w / 2, (by + ph / 2.0f) * sy - orig.h / 2 };
    Vector2 c = to_world(&orig, lc);
    if (asText) {
        Element *n = doc_add(&A.doc, EL_TEXT);
        if (!n) return;
        n->text[0] = 0;
        n->fill = main;
        n->align = ALIGN_CENTER;
        int fi = font_find("Moderna negrita");
        n->font = fi >= 0 ? fi : 0;
        // Las mayúsculas ocupan unas tres cuartas partes del tamaño de letra
        n->fontSize = fmaxf(10, roundf(ph * sy / 0.75f));
        n->w = fmaxf(pw * sx * 1.25f, n->fontSize * 4);
        n->rotation = orig.rotation;
        fix_text_height(n);
        n->x = c.x - n->w / 2;
        n->y = c.y - n->h / 2;
        int ni = doc_move_layer(&A.doc, A.doc.count - 1, index + 1 - (A.doc.count - 1));
        A.fixText = false;
        select_index(ni);
        A.editing = true;
        A.editPushed = true;
        A.caret = 0;
        toast("Escribe el texto corregido. Arriba puedes cambiar la letra, el tamaño y el color.");
    } else {
        Element *n = doc_add(&A.doc, EL_IMAGE);
        if (!n) return;
        *n = orig;
        n->asset = partId;
        n->w = pw * sx;
        n->h = ph * sy;
        n->x = c.x - n->w / 2;
        n->y = c.y - n->h / 2;
        n->locked = false;
        snprintf(n->text, TEXT_MAX, "Recorte");
        doc_move_layer(&A.doc, A.doc.count - 1, index + 1 - (A.doc.count - 1));
        A.sel = index;
        toast("Parte separada: ya la puedes mover. Rodea otra o pulsa Esc para terminar.");
    }
}

// ---- Editar texto de la imagen: leer las letras (OCR), borrarlas y poner un texto igual ----

typedef struct { float x, y, w, h; char text[400]; } OcrLine;

// Una "palabra" de un solo signo que no es número ni una palabra de una letra (a, e, o, u, y)
static bool ocr_junk(const char *t)
{
    int cp = 0, bytes = 0;
    // Solo rayas, puntos u otros signos (por ejemplo "————" de una línea dibujada)
    bool letters = false;
    for (const char *q = t; *q;) {
        int b = 0, c = GetCodepoint(q, &b);
        q += b > 0 ? b : 1;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7)) { letters = true; break; }
    }
    if (!letters) return true;
    cp = GetCodepoint(t, &bytes);
    if (t[bytes] != 0) return false;
    if (cp >= '0' && cp <= '9') return false;
    if (cp < 128 && strchr("aeouyAEOUY", cp)) return false;
    return true;
}

static char g_ocrError[300];

// Lee las letras de una zona de la imagen. Una zona grande se parte en trozos que se
// solapan (el lector de Windows no admite imágenes de más de unos 2600 px y lee mejor las
// letras grandes); luego se juntan las palabras en renglones.
typedef struct { int x, y, w, h, tile, line; char t[120]; } OcrWord;

static int run_ocr(Image src, int x0, int y0, int x1, int y1, OcrLine *lines, int max)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > src.width) x1 = src.width;
    if (y1 > src.height) y1 = src.height;
    if (x1 - x0 < 4 || y1 - y0 < 4) return 0;
    int rw = x1 - x0, rh = y1 - y0;
    // Trozos de como mucho TILE px que se solapan OVER px
    const int TILE = 1100, OVER = 300;
    int nx = rw <= TILE ? 1 : 1 + (rw - TILE + (TILE - OVER) - 1) / (TILE - OVER);
    int ny = rh <= TILE ? 1 : 1 + (rh - TILE + (TILE - OVER) - 1) / (TILE - OVER);
    if (nx * ny > 24) { nx = nx > 4 ? 4 : nx; ny = ny > 6 ? 6 : ny; }
    int tw = nx == 1 ? rw : (rw + (nx - 1) * OVER + nx - 1) / nx;
    int th = ny == 1 ? rh : (rh + (ny - 1) * OVER + ny - 1) / ny;
    int ntiles = nx * ny;
    static char paths[24][1200];
    const char *pp[24];
    int tx0[24], ty0[24], tx1[24], ty1[24];
    float ups[24];
    int made = 0;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            int k = j * nx + i;
            int ax = x0 + (nx == 1 ? 0 : i * (rw - tw) / (nx - 1));
            int ay = y0 + (ny == 1 ? 0 : j * (rh - th) / (ny - 1));
            int bw = tw < rw ? tw : rw, bh = th < rh ? th : rh;
            tx0[k] = ax; ty0[k] = ay; tx1[k] = ax + bw; ty1[k] = ay + bh;
            Image crop = ImageFromImage(src, (Rectangle){ (float)ax, (float)ay, (float)bw, (float)bh });
            // El lector va mejor con letras grandes
            float up = 1;
            if (crop.height < 400) up = fminf(3.0f, 400.0f / crop.height);
            else up = 2;
            float big = (float)(crop.width > crop.height ? crop.width : crop.height);
            if (big * up > 2500) up = 2500 / big;
            if (up < 1) up = 1;
            ups[k] = up;
            if (fabsf(up - 1) > 0.01f) ImageResize(&crop, (int)(crop.width * up), (int)(crop.height * up));
            ImageFormat(&crop, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            // Sin transparencia (el lector se lía con ella)
            unsigned char *px = (unsigned char *)crop.data;
            for (int q = 0; q < crop.width * crop.height; q++) {
                float a = px[q * 4 + 3] / 255.0f;
                for (int c = 0; c < 3; c++) px[q * 4 + c] = (unsigned char)(px[q * 4 + c] * a + 255 * (1 - a));
                px[q * 4 + 3] = 255;
            }
#if defined(_WIN32)
            snprintf(paths[k], sizeof(paths[k]), "%s\\lector%d.png", plat_data_dir(), k);
#else
            snprintf(paths[k], sizeof(paths[k]), "%s/lector%d.png", plat_data_dir(), k);
#endif
            int size = 0;
            unsigned char *png = ExportImageToMemory(crop, ".png", &size);
            UnloadImage(crop);
            if (!png) continue;
            plat_write_file(paths[k], png, size);
            MemFree(png);
            pp[k] = paths[k];
            made++;
        }
    if (made < ntiles) { for (int k = 0; k < ntiles; k++) plat_delete_file(paths[k]); return 0; }
    static char out[262144];
    bool ok = plat_ocr(pp, ntiles, out, sizeof(out));
    for (int k = 0; k < ntiles; k++) plat_delete_file(paths[k]);
    g_ocrError[0] = 0;
    if (!ok) {
        snprintf(g_ocrError, sizeof(g_ocrError), "%s", strncmp(out, "ERROR ", 6) == 0 ? out + 6 : out[0] ? out : "sin respuesta");
        for (char *q = g_ocrError; *q; q++) if (*q == '\r' || *q == '\n') *q = ' ';
        return 0;
    }
    // Palabras, ya en píxeles de la imagen. De cada trozo solo nos quedamos con las palabras
    // cuyo centro cae en su parte "propia" (sin la mitad del solape), para no repetirlas.
    static OcrWord words[3000];
    int nw = 0, tile = -1;
    for (char *l = out; *l && nw < 3000;) {
        char *nl = strchr(l, '\n');
        if (nl) *nl = 0;
        if (l[0] == '#') tile = atoi(l + 1);
        else if (tile >= 0 && tile < ntiles) {
            OcrWord wd;
            char *tab = strchr(l, '\t');
            if (tab && sscanf(l, "%d %d %d %d %d", &wd.line, &wd.x, &wd.y, &wd.w, &wd.h) == 5 && wd.w > 0 && wd.h > 0) {
                char *t = tab + 1;
                while (*t == ' ') t++;
                int len = (int)strlen(t);
                while (len > 0 && (t[len - 1] == ' ' || t[len - 1] == '\r')) t[--len] = 0;
                float up = ups[tile];
                wd.x = (int)(tx0[tile] + wd.x / up); wd.y = (int)(ty0[tile] + wd.y / up);
                wd.w = (int)(wd.w / up + 0.5f); wd.h = (int)(wd.h / up + 0.5f);
                wd.tile = tile;
                float cx = wd.x + wd.w / 2.0f, cy = wd.y + wd.h / 2.0f;
                int ti = tile % nx, tj = tile / nx;
                float cx0 = ti > 0 ? tx0[tile] + OVER / 2.0f : -1e9f, cx1 = ti < nx - 1 ? tx1[tile] - OVER / 2.0f : 1e9f;
                float cy0 = tj > 0 ? ty0[tile] + OVER / 2.0f : -1e9f, cy1 = tj < ny - 1 ? ty1[tile] - OVER / 2.0f : 1e9f;
                if (len > 0 && cx >= cx0 && cx < cx1 && cy >= cy0 && cy < cy1) {
                    snprintf(wd.t, sizeof(wd.t), "%s", t);
                    words[nw++] = wd;
                }
            }
        }
        if (!nl) break;
        l = nl + 1;
    }
    // Renglones: las palabras de un mismo renglón del lector (y del mismo trozo)
    int n = 0;
    for (int i = 0; i < nw && n < max;) {
        int j = i;
        while (j < nw && words[j].line == words[i].line && words[j].tile == words[i].tile) j++;
        // Fuera las marcas sueltas al principio o al final del renglón (trocitos de un dibujo
        // o de un círculo que el lector confunde con una letra)
        int a0 = i, a1 = j;
        while (a0 < a1 && ocr_junk(words[a0].t)) a0++;
        while (a1 > a0 && ocr_junk(words[a1 - 1].t)) a1--;
        // Palabras de un tamaño muy distinto al resto del renglón, en las puntas: son de otro
        // texto que está al lado (por ejemplo, una frase pequeña junto a un título)
        if (a1 - a0 >= 3) {
            int hs[64], nh = 0;
            for (int k = a0; k < a1 && nh < 64; k++) hs[nh++] = words[k].h;
            for (int x = 1; x < nh; x++) { int v = hs[x], y = x - 1; while (y >= 0 && hs[y] > v) { hs[y + 1] = hs[y]; y--; } hs[y + 1] = v; }
            float med = (float)hs[nh / 2];
            while (a1 - a0 > 1 && (words[a1 - 1].h < med * 0.55f || words[a1 - 1].h > med * 1.8f)) a1--;
            while (a1 - a0 > 1 && (words[a0].h < med * 0.55f || words[a0].h > med * 1.8f)) a0++;
            // Y lo que está muy separado del resto (otro texto en la misma fila)
            for (int k = a0 + 1; k < a1; k++)
                if (words[k].x - (words[k - 1].x + words[k - 1].w) > med * 2.5f) {
                    // Nos quedamos con el lado que tiene más letras
                    int left = 0, right = 0;
                    for (int q = a0; q < k; q++) left += (int)strlen(words[q].t);
                    for (int q = k; q < a1; q++) right += (int)strlen(words[q].t);
                    if (left >= right) a1 = k; else a0 = k;
                    break;
                }
        }
        if (a1 > a0) {
            int bx0 = 1 << 30, by0 = 1 << 30, bx1 = -(1 << 30), by1 = -(1 << 30);
            lines[n].text[0] = 0;
            for (int k = a0; k < a1; k++) {
                if (words[k].x < bx0) bx0 = words[k].x;
                if (words[k].y < by0) by0 = words[k].y;
                if (words[k].x + words[k].w > bx1) bx1 = words[k].x + words[k].w;
                if (words[k].y + words[k].h > by1) by1 = words[k].y + words[k].h;
                if (k > a0) strncat(lines[n].text, " ", sizeof(lines[n].text) - strlen(lines[n].text) - 1);
                strncat(lines[n].text, words[k].t, sizeof(lines[n].text) - strlen(lines[n].text) - 1);
            }
            lines[n].x = (float)bx0; lines[n].y = (float)by0;
            lines[n].w = (float)(bx1 - bx0); lines[n].h = (float)(by1 - by0);
            n++;
        }
        i = j;
    }
    // Renglones partidos entre dos trozos: si están a la misma altura y casi pegados, uno solo
    for (bool merged = true; merged;) {
        merged = false;
        for (int i = 0; i < n && !merged; i++)
            for (int j = 0; j < n && !merged; j++) {
                if (i == j) continue;
                OcrLine *A1 = &lines[i], *B1 = &lines[j];
                if (B1->x < A1->x) continue;
                float h = fmaxf(A1->h, B1->h);
                float gap = B1->x - (A1->x + A1->w);
                bool sameRow = fabsf((A1->y + A1->h / 2) - (B1->y + B1->h / 2)) < h * 0.3f && fminf(A1->h, B1->h) > h * 0.7f;
                if (!sameRow || gap > h * 0.9f) continue;
                if (gap < -h * 0.3f) {
                    // Se pisan: es la misma palabra leída en los dos trozos; nos quedamos con la más larga
                    if (strlen(B1->text) > strlen(A1->text)) *A1 = *B1;
                } else {
                    float nx1 = fmaxf(A1->x + A1->w, B1->x + B1->w);
                    float ny0 = fminf(A1->y, B1->y), ny1 = fmaxf(A1->y + A1->h, B1->y + B1->h);
                    strncat(A1->text, " ", sizeof(A1->text) - strlen(A1->text) - 1);
                    strncat(A1->text, B1->text, sizeof(A1->text) - strlen(A1->text) - 1);
                    A1->w = nx1 - A1->x; A1->y = ny0; A1->h = ny1 - ny0;
                }
                lines[j] = lines[n - 1];
                n--;
                merged = true;
            }
    }
    // De arriba abajo
    for (int i = 1; i < n; i++) {
        OcrLine t = lines[i];
        int k = i - 1;
        while (k >= 0 && lines[k].y > t.y) { lines[k + 1] = lines[k]; k--; }
        lines[k + 1] = t;
    }
    return n;
}

// Busca entre nuestras fuentes la que dibuja ese texto con una forma más parecida a la de la
// imagen. mask: transparencia de las letras recortadas (mw x mh). Devuelve la fuente, y en
// size / topOff el tamaño y lo que queda por encima de las letras.
// Rasgos de la forma de un texto, que no dependen de que cada letra caiga en el mismo sitio:
// cuánta tinta hay en cada altura (dónde quedan las minúsculas, mayúsculas y remates) y lo
// largos que son los trazos en horizontal y en vertical (grosor de palos, contraste, serifas)
#define FEAT_H 48
#define FEAT_ROWS 24
#define FEAT_RUNS 16
#define FEAT_CH 8
#define FEAT_CW 160
typedef struct { float aspect, ink; float rows[FEAT_ROWS]; float hr[FEAT_RUNS], vr[FEAT_RUNS]; float coarse[FEAT_CH * FEAT_CW]; } ShapeFeat;

static void shape_features(const float *orig, int ow, int sh, int cw, ShapeFeat *f)
{
    memset(f, 0, sizeof(*f));
    // Quitamos las columnas vacías (huecos entre letras y palabras): así un texto con las
    // letras muy separadas se compara igual que uno con las letras juntas
    float *src = (float *)malloc(sizeof(float) * ow * sh);
    int sw = 0;
    for (int x = 0; x < ow; x++) {
        float mx = 0;
        for (int y = 0; y < sh; y++) mx = fmaxf(mx, orig[y * ow + x]);
        if (mx < 0.25f) continue;
        for (int y = 0; y < sh; y++) src[y * ow + sw] = orig[y * ow + x];
        sw++;
    }
    if (sw < 2) { free(src); return; }
    // (src sigue teniendo filas de ow de ancho; copiamos a filas de sw)
    for (int y = 1; y < sh; y++) memmove(src + y * sw, src + y * ow, sizeof(float) * sw);
    f->aspect = (float)sw / sh;
    int H = FEAT_H, W = (int)(FEAT_H * f->aspect + 0.5f);
    if (W < 4) W = 4;
    if (W > 2400) W = 2400;
    float *g = (float *)calloc((size_t)W * H, sizeof(float));
    float *cnt = (float *)calloc((size_t)W * H, sizeof(float));
    // Reducir por medias (o ampliar repitiendo) a FEAT_H de alto
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) {
            int gi = (y * H / sh) * W + x * W / sw;
            g[gi] += src[y * sw + x];
            cnt[gi] += 1;
        }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            if (cnt[i] > 0) g[i] /= cnt[i];
            else {
                int sx = x * sw / W, sy = y * sh / H;
                g[i] = src[sy * sw + sx];
            }
        }
    free(cnt);
    float total = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            total += g[y * W + x];
            f->rows[y * FEAT_ROWS / H] += g[y * W + x];
        }
    f->ink = total / ((float)W * H);
    for (int i = 0; i < FEAT_ROWS; i++) f->rows[i] /= total > 0 ? total : 1;
    // Trazos: tramos seguidos de tinta (más de la mitad) por filas y por columnas
    float hs = 0, vs = 0;
    for (int y = 0; y < H; y++) {
        int run = 0;
        for (int x = 0; x <= W; x++) {
            if (x < W && g[y * W + x] > 0.5f) run++;
            else if (run) { int b = run < FEAT_RUNS ? run : FEAT_RUNS - 1; f->hr[b] += run; hs += run; run = 0; }
        }
    }
    for (int x = 0; x < W; x++) {
        int run = 0;
        for (int y = 0; y <= H; y++) {
            if (y < H && g[y * W + x] > 0.5f) run++;
            else if (run) { int b = run < FEAT_RUNS ? run : FEAT_RUNS - 1; f->vr[b] += run; vs += run; run = 0; }
        }
    }
    for (int i = 0; i < FEAT_RUNS; i++) { f->hr[i] /= hs > 0 ? hs : 1; f->vr[i] /= vs > 0 ? vs : 1; }
    // Rejilla gruesa (unas pocas celdas por letra) estirada a la caja del texto: dónde hay
    // letras altas, bajas, huecos entre palabras... sin exigir que cada trazo coincida
    {
        float cc[FEAT_CH * FEAT_CW] = { 0 }, cn[FEAT_CH * FEAT_CW] = { 0 };
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int ci = (y * FEAT_CH / H) * FEAT_CW + x * cw / W;
                cc[ci] += g[y * W + x];
                cn[ci] += 1;
            }
        for (int i = 0; i < FEAT_CH * FEAT_CW; i++) f->coarse[i] = cn[i] > 0 ? cc[i] / cn[i] : 0;
    }
    free(g);
    free(src);
}

// Parecido de dos histogramas (1 = iguales); se suaviza un poco para tolerar un píxel de diferencia
static float hist_match(const float *a, const float *b, int n)
{
    float s = 0;
    for (int i = 0; i < n; i++) {
        float aa = a[i] * 0.5f + (i > 0 ? a[i - 1] : 0) * 0.25f + (i < n - 1 ? a[i + 1] : 0) * 0.25f;
        float bb = b[i] * 0.5f + (i > 0 ? b[i - 1] : 0) * 0.25f + (i < n - 1 ? b[i + 1] : 0) * 0.25f;
        s += fminf(aa, bb);
    }
    return s;
}

static float shape_similarity(const ShapeFeat *a, const ShapeFeat *b, float *parts)
{
    float ar = fminf(a->aspect, b->aspect) / fmaxf(a->aspect, b->aspect);
    float ink = (a->ink > 0 && b->ink > 0) ? fminf(a->ink, b->ink) / fmaxf(a->ink, b->ink) : 0;
    float rows = hist_match(a->rows, b->rows, FEAT_ROWS);
    float hr = hist_match(a->hr, b->hr, FEAT_RUNS), vr = hist_match(a->vr, b->vr, FEAT_RUNS);
    float inter = 0, uni = 0;
    for (int i = 0; i < FEAT_CH * FEAT_CW; i++) { inter += fminf(a->coarse[i], b->coarse[i]); uni += fmaxf(a->coarse[i], b->coarse[i]); }
    float layout = uni > 0 ? inter / uni : 0;
    if (parts) { parts[0] = ar; parts[1] = ink; parts[2] = rows; parts[3] = hr; parts[4] = vr; parts[5] = layout; }
    return ar * 0.2f + ink * 0.1f + rows * 0.15f + hr * 0.15f + vr * 0.15f + layout * 0.25f;
}

// Elige la fuente que, escribiendo el mismo texto, deja una forma más parecida a la de la imagen
static int match_font_shape(const char *text, const unsigned char *mask, int mw, int mh, float *size, float *topOff, float *inkW)
{
    int best = 0;
    float bestScore = -1;
    *size = mh / 0.75f;
    *topOff = 0;
    // ¿Está casi todo en mayúsculas?
    int upper = 0, lower = 0;
    for (const char *q = text; *q;) {
        int bytes = 0, cp = GetCodepoint(q, &bytes);
        q += bytes > 0 ? bytes : 1;
        if ((cp >= 'A' && cp <= 'Z') || (cp >= 0xC0 && cp <= 0xDE)) upper++;
        else if ((cp >= 'a' && cp <= 'z') || (cp >= 0xDF && cp <= 0xFF)) lower++;
    }
    bool caps = upper > 0 && upper >= 4 * lower;
    float *src = (float *)malloc(sizeof(float) * mw * mh);
    for (int i = 0; i < mw * mh; i++) src[i] = mask[i] / 255.0f;
    ShapeFeat target, cf;
    // Rejilla gruesa: unas dos celdas por cada letra de alto
    shape_features(src, mw, mh, FEAT_CW, &target);
    int cw = (int)(target.aspect * FEAT_CH * 0.35f + 0.5f);
    if (cw < 2) cw = 2;
    if (cw > FEAT_CW) cw = FEAT_CW;
    shape_features(src, mw, mh, cw, &target);
    free(src);
#if defined(LIENZO_TESTS)
    if (getenv("LIENZO_DEBUG_FONTS")) {
        Image dm = GenImageColor(mw, mh, BLACK);
        for (int y = 0; y < mh; y++) for (int x = 0; x < mw; x++) { unsigned char v = mask[y * mw + x]; ImageDrawPixel(&dm, x, y, (Color){ v, v, v, 255 }); }
        ExportImage(dm, "debug_mascara.png");
        UnloadImage(dm);
    }
#endif
    for (int fi = 0; fi < g_fontCount; fi++) {
        Font f = font_get(fi);
        if (f.texture.id == 0 || g_fonts[fi].failed) continue;
        // Del mismo alto que en la imagen, para que el grosor de los trazos se compare bien
        const float S = 100;
        Image im = ImageTextEx(f, text, S, S * 0.02f, WHITE);
        if (!im.data) continue;
        ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        unsigned char *p = (unsigned char *)im.data;
        int x0 = im.width, y0 = im.height, x1 = -1, y1 = -1;
        for (int y = 0; y < im.height; y++)
            for (int x = 0; x < im.width; x++)
                if (p[(y * im.width + x) * 4 + 3] > 100) {
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
        if (x1 < 0) { UnloadImage(im); continue; }
        int rw = x1 - x0 + 1, rh = y1 - y0 + 1;
        float *cand = (float *)malloc(sizeof(float) * rw * rh);
        for (int y = 0; y < rh; y++)
            for (int x = 0; x < rw; x++) cand[y * rw + x] = p[((y0 + y) * im.width + x0 + x) * 4 + 3] / 255.0f;
        shape_features(cand, rw, rh, cw, &cf);
        free(cand);
        float parts[6];
        float score = shape_similarity(&target, &cf, parts);
        // Las letras de caligrafía (unidas) casi nunca van en carteles con textos normales, y
        // menos en mayúsculas: solo ganan si se parecen claramente más
        static const char *SCRIPTS[] = { "Satisfy", "Great Vibes", "Dancing Script", "Pacifico", "Lobster", "Manuscrita" };
        for (size_t k = 0; k < sizeof(SCRIPTS) / sizeof(SCRIPTS[0]); k++)
            if (strcmp(font_name(fi), SCRIPTS[k]) == 0) score -= caps ? 0.08f : 0.04f;
#if defined(LIENZO_TESTS)
        if (getenv("LIENZO_DEBUG_FONTS")) printf("  %-22s ar %.3f ink %.3f filas %.3f horiz %.3f vert %.3f forma %.3f = %.3f\n", font_name(fi), parts[0], parts[1], parts[2], parts[3], parts[4], parts[5], score);
#endif
        if (score > bestScore) {
            bestScore = score;
            best = fi;
            *size = S * mh / rh;
            *topOff = y0 * (*size / S);
            *inkW = rw * (*size / S);
        }
        UnloadImage(im);
    }
    return best;
}

// Caja para borrar un renglón: un poco más grande que las letras, pero sin meterse en los
// renglones de arriba y de abajo (si no, el borde de la caja cogería sus letras como fondo)
static void line_box(const OcrLine *lines, int n, int i, int *x0, int *y0, int *x1, int *y1)
{
    float pad = lines[i].h * 0.3f + 2, padY = lines[i].h * 0.06f + 1;
    float top = lines[i].y - padY, bottom = lines[i].y + lines[i].h + padY;
    for (int j = 0; j < n; j++) {
        if (j == i) continue;
        // Solo si se tocan en horizontal
        if (lines[j].x > lines[i].x + lines[i].w || lines[j].x + lines[j].w < lines[i].x) continue;
        float jb = lines[j].y + lines[j].h, jt = lines[j].y;
        if (jb <= lines[i].y + lines[i].h * 0.5f && jb + 1 > top) top = fminf(lines[i].y, jb + 1);
        if (jt >= lines[i].y + lines[i].h * 0.5f && jt - 1 < bottom) bottom = fmaxf(lines[i].y + lines[i].h, jt - 1);
    }
    *x0 = (int)(lines[i].x - pad); *y0 = (int)top;
    *x1 = (int)(lines[i].x + lines[i].w + pad); *y1 = (int)bottom;
}

// Borra de la imagen los renglones leídos y pone en su sitio un texto igual que se puede
// cambiar. src es la imagen del elemento (se libera aquí). Devuelve el texto nuevo o -1.
static int convert_text_lines(int index, Image src, const OcrLine *lines, int n)
{
    Element *e = &A.doc.el[index];
    // Borrar cada renglón por separado (así no se tocan los dibujos de alrededor)
    Image work = ImageCopy(src);
    Color main = BLACK;
    bool gotColor = false;
    int longest = 0;
    unsigned char *mask = NULL;
    int mw = 0, mh = 0, maskTop = 0;
    for (int i = 0; i < n; i++) if (strlen(lines[i].text) > strlen(lines[longest].text)) longest = i;
    // Dónde hemos borrado letras (para la pasada de limpieza del final)
    unsigned char *erased = (unsigned char *)calloc((size_t)src.width * src.height, 1);
    for (int i = 0; i < n; i++) {
        int lx0, ly0, lx1, ly1;
        line_box(lines, n, i, &lx0, &ly0, &lx1, &ly1);
        Image part, rest;
        int bx, by;
        Color c;
        if (!magic_extract_text(work, lx0, ly0, lx1, ly1, A.wandTol, &part, &bx, &by, &rest, &c)) continue;
        {
            unsigned char *pa = (unsigned char *)part.data;
            for (int y = 0; y < part.height; y++)
                for (int x = 0; x < part.width; x++) {
                    int gx = bx + x, gy = by + y;
                    if (gx >= 0 && gy >= 0 && gx < src.width && gy < src.height && pa[(y * part.width + x) * 4 + 3] >= 64) erased[gy * src.width + gx] = 1;
                }
        }
#if defined(LIENZO_TESTS)
        if (getenv("LIENZO_DEBUG_FONTS")) { char dp[64]; snprintf(dp, sizeof(dp), "debug_parte%d.png", i); ExportImage(part, dp); snprintf(dp, sizeof(dp), "debug_resto%d.png", i); ExportImage(rest, dp); }
        printf("renglon %d: %.0f %.0f %.0f %.0f color %02X%02X%02X '%s'\n", i, lines[i].x, lines[i].y, lines[i].w, lines[i].h, c.r, c.g, c.b, lines[i].text);
#endif
        // Nos quedamos con el color que más contrasta con el fondo (el de las letras de verdad)
        {
            Color bg = GetImageColor(work, lx0 < 0 ? 0 : lx0, ly0 < 0 ? 0 : ly0);
            float d = fabsf((c.r + c.g + c.b) - (float)(bg.r + bg.g + bg.b));
            static float bestD;
            if (!gotColor || d > bestD) { main = c; bestD = d; gotColor = true; }
        }
        if (i == longest) {
            // Máscara de las letras: recortada justo a la tinta, igual que se recortan las
            // letras de prueba de cada fuente, para que coincidan al compararlas
            unsigned char *pp = (unsigned char *)part.data;
            // Solo dentro de la caja del renglón leído (con 2 píxeles de margen), para no coger
            // trocitos del renglón de arriba o de abajo
            int ry0 = (int)lines[i].y - 2 - by, ry1 = (int)(lines[i].y + lines[i].h) + 2 - by;
            int rx0 = (int)lines[i].x - 2 - bx, rx1 = (int)(lines[i].x + lines[i].w) + 2 - bx;
            if (ry0 < 0) ry0 = 0;
            if (rx0 < 0) rx0 = 0;
            if (ry1 > part.height) ry1 = part.height;
            if (rx1 > part.width) rx1 = part.width;
            int qx0 = part.width, qy0 = part.height, qx1 = -1, qy1 = -1;
            for (int y = ry0; y < ry1; y++)
                for (int x = rx0; x < rx1; x++)
                    if (pp[(y * part.width + x) * 4 + 3] > 100) {
                        if (x < qx0) qx0 = x;
                        if (x > qx1) qx1 = x;
                        if (y < qy0) qy0 = y;
                        if (y > qy1) qy1 = y;
                    }
            mw = qx1 - qx0 + 1; mh = qy1 - qy0 + 1;
            if (qx1 >= 0 && mw > 2 && mh > 2) {
                mask = (unsigned char *)calloc((size_t)mw * mh, 1);
                for (int y = 0; y < mh; y++)
                    for (int x = 0; x < mw; x++) mask[y * mw + x] = pp[((qy0 + y) * part.width + qx0 + x) * 4 + 3];
                maskTop = by + qy0;
            }
        }
        // Sombra o contorno de las letras (otro color pegado a ellas): una segunda pasada que
        // solo se acepta si lo que coge está casi todo junto a las letras ya borradas
        {
            Image part2, rest2;
            int bx2, by2;
            Color c2;
            if (magic_extract_text(rest, lx0, ly0, lx1, ly1, A.wandTol, &part2, &bx2, &by2, &rest2, &c2)) {
                int rad = (int)fmaxf(3, lines[i].h * 0.12f);
                unsigned char *p1 = (unsigned char *)part.data, *p2 = (unsigned char *)part2.data;
                int ink = 0, near = 0;
                for (int y = 0; y < part2.height; y++)
                    for (int x = 0; x < part2.width; x++) {
                        if (p2[(y * part2.width + x) * 4 + 3] < 128) continue;
                        ink++;
                        // ¿Hay letra borrada cerca?
                        int gx = bx2 + x - bx, gy = by2 + y - by;
                        bool found = false;
                        for (int dy = -rad; dy <= rad && !found; dy += 1)
                            for (int dx = -rad; dx <= rad && !found; dx += 1) {
                                int xx = gx + dx, yy = gy + dy;
                                if (xx < 0 || yy < 0 || xx >= part.width || yy >= part.height) continue;
                                if (p1[(yy * part.width + xx) * 4 + 3] >= 128) found = true;
                            }
                        if (found) near++;
                    }
#if defined(LIENZO_TESTS)
                printf("  sombra: %d de %d junto a las letras (letras %d), color %02X%02X%02X\n", near, ink, part.width * part.height, c2.r, c2.g, c2.b);
#endif
                int ink1 = 0;
                for (int k = 0; k < part.width * part.height; k++) if (p1[k * 4 + 3] >= 128) ink1++;
                // Una sombra es menos que las propias letras; si coge más, es el fondo
                if (ink > 0 && near >= ink * 0.85f && ink <= ink1) { UnloadImage(rest); rest = rest2; }
                else UnloadImage(rest2);
                UnloadImage(part2);
            }
        }
        UnloadImage(part);
        UnloadImage(work);
        work = rest;
    }
    // Limpieza: al borrar un renglón, el relleno puede coger color de las letras del renglón
    // de al lado, que aún no estaban borradas. Ahora que ya no están, se repasa cada renglón y
    // se quita lo que quede pegado a donde había letras.
    for (int i = 0; i < n; i++) {
        int lx0, ly0, lx1, ly1;
        line_box(lines, n, i, &lx0, &ly0, &lx1, &ly1);
        Image part, rest;
        int bx, by;
        Color c;
        if (!magic_extract_text(work, lx0, ly0, lx1, ly1, A.wandTol * 0.7f, &part, &bx, &by, &rest, &c)) continue;
        int rad = (int)fmaxf(2, lines[i].h * 0.08f);
        unsigned char *pa = (unsigned char *)part.data;
        int ink = 0, near = 0;
        for (int y = 0; y < part.height; y++)
            for (int x = 0; x < part.width; x++) {
                if (pa[(y * part.width + x) * 4 + 3] < 128) continue;
                ink++;
                int gx = bx + x, gy = by + y;
                bool found = false;
                for (int dy = -rad; dy <= rad && !found; dy++)
                    for (int dx = -rad; dx <= rad && !found; dx++) {
                        int xx = gx + dx, yy = gy + dy;
                        if (xx >= 0 && yy >= 0 && xx < src.width && yy < src.height && erased[yy * src.width + xx]) found = true;
                    }
                if (found) near++;
            }
#if defined(LIENZO_TESTS)
        printf("  limpieza %d: %d de %d junto a lo borrado\n", i, near, ink);
#endif
        UnloadImage(part);
        if (ink > 0 && near >= ink * 0.9f) { UnloadImage(work); work = rest; }
        else UnloadImage(rest);
    }
    free(erased);
    UnloadImage(src);
    // Elegir la fuente y el tamaño
    float size = lines[longest].h / 0.75f, topOff = 0, letterSp = 0;
    int font = font_find("Moderna");
    if (font < 0) font = 0;
    if (mask) {
        float inkW = 0;
        font = match_font_shape(lines[longest].text, mask, mw, mh, &size, &topOff, &inkW);
        // Espacio entre letras: lo que falta (o sobra) de ancho, repartido entre las letras
        int chars = 0;
        for (const char *q = lines[longest].text; *q;) { int b = 0; GetCodepoint(q, &b); q += b > 0 ? b : 1; chars++; }
        if (inkW > 0 && chars > 1) {
            float extra = (mw - inkW) / (chars - 1) / size;
            if (extra < -0.04f) {
                // Más estrecho que nuestra letra: un poco más pequeña y algo más juntas
                float k2 = fmaxf(0.88f, (float)mw / inkW);
                size *= k2; topOff *= k2; inkW *= k2;
                extra = (mw - inkW) / (chars - 1) / size;
            }
            if (fabsf(extra) > 0.012f) letterSp = fmaxf(-0.05f, fminf(0.6f, extra));
        }
        free(mask);
    }
    bool hadMask = mask != NULL;
    float sx = e->w / (float)work.width, sy = e->h / (float)work.height;
    int restId = asset_add_image(work);
    UnloadImage(work);
    if (restId < 0) { toast("No caben más imágenes en este diseño"); return -1; }
    begin_change();
    e = &A.doc.el[index];
    Element orig = *e;
    e->asset = restId;
    g_assets[restId].hidden = true;
    Element *t = doc_add(&A.doc, EL_TEXT);
    if (!t) return -1;
    // Texto: renglones unidos; alineación según cómo empiezan y acaban los renglones
    t->text[0] = 0;
    float minL = 1e9f, maxR = -1e9f, maxW = 0;
    for (int i = 0; i < n; i++) {
        if (i) strncat(t->text, "\n", TEXT_MAX - strlen(t->text) - 1);
        strncat(t->text, lines[i].text, TEXT_MAX - strlen(t->text) - 1);
        minL = fminf(minL, lines[i].x);
        maxR = fmaxf(maxR, lines[i].x + lines[i].w);
        maxW = fmaxf(maxW, lines[i].w);
    }
    int align = ALIGN_LEFT;
    if (n > 1) {
        float dl = 0, dr = 0, dc = 0, mc = (minL + maxR) / 2;
        for (int i = 0; i < n; i++) {
            dl = fmaxf(dl, fabsf(lines[i].x - minL));
            dr = fmaxf(dr, fabsf(lines[i].x + lines[i].w - maxR));
            dc = fmaxf(dc, fabsf(lines[i].x + lines[i].w / 2 - mc));
        }
        if (dc < dl && dc <= dr) align = ALIGN_CENTER;
        else if (dr < dl) align = ALIGN_RIGHT;
    } else align = ALIGN_CENTER;
    // Todo en píxeles de la imagen; luego pasamos al lienzo
    float k = sy;
    t->font = font;
    t->fontSize = fmaxf(6, roundf(size * k));
    t->letterSpacing = letterSp;
    t->fill = main;
    t->align = align;
    if (n > 1) {
        float step = (lines[n - 1].y - lines[0].y) / (n - 1);
        t->lineSpacing = step / size;
        if (t->lineSpacing < 0.6f || t->lineSpacing > 3) t->lineSpacing = 0;
    }
    float ls = t->lineSpacing > 0.3f ? t->lineSpacing : 1.18f;
    float boxW = (maxR - minL) * sx;
    // Caja tan ancha como el renglón más largo escrito con la letra elegida (así no se parte)
    float maxMeasured = 0;
    for (int i = 0; i < n; i++) {
        Vector2 m = MeasureTextEx(font_get(font), lines[i].text, t->fontSize, t->fontSize * (0.02f + t->letterSpacing));
        maxMeasured = fmaxf(maxMeasured, m.x);
    }
    t->w = fmaxf(boxW, maxMeasured) * 1.04f + t->fontSize * 0.3f;
    float left = minL * sx, right = maxR * sx;
    float lx = align == ALIGN_CENTER ? (left + right) / 2 - t->w / 2 : align == ALIGN_RIGHT ? right - t->w : left - t->fontSize * 0.05f;
    // Arriba de la tinta del primer renglón (medida en el renglón más largo)
    float inkTop = hadMask ? maskTop - (lines[longest].y - lines[0].y) : lines[0].y;
    float ty = inkTop * sy - (t->fontSize * (ls - 1) / 2 + topOff * k);
    fix_text_height(t);
    t->rotation = orig.rotation;
    // Posición dentro de la imagen (por si la imagen no empieza en 0,0 o está girada)
    Vector2 lc = { lx + t->w / 2 - orig.w / 2, ty + t->h / 2 - orig.h / 2 };
    Vector2 c = to_world(&orig, lc);
    t->x = c.x - t->w / 2;
    t->y = c.y - t->h / 2;
    int ni = doc_move_layer(&A.doc, A.doc.count - 1, index + 1 - (A.doc.count - 1));
    return ni;
}

static void edit_text_box(int index)
{
    Element *e = &A.doc.el[index];
    if (e->asset < 0 || e->asset >= g_assetCount) return;
    Asset *as = &g_assets[e->asset];
    Image src = LoadImageFromMemory(".png", as->png, as->pngSize);
    if (!src.data) return;
    int x0, y0, x1, y1;
    box_to_pixels(e, src.width, src.height, &x0, &y0, &x1, &y1);
    static OcrLine lines[64];
    int n = run_ocr(src, x0, y0, x1, y1, lines, 64);
    if (n == 0) {
        UnloadImage(src);
        // Sin lector: borramos y dejamos un texto vacío como antes
        wand_box(index, true);
        char msg[500];
        if (g_ocrError[0]) snprintf(msg, sizeof(msg), "No he podido leer las letras (%s). Escribe tú el texto.", g_ocrError);
        else snprintf(msg, sizeof(msg), "No he encontrado letras en el recuadro. Escribe tú el texto o rodéalo un poco más grande.");
        toast(msg);
        return;
    }
    int ni = convert_text_lines(index, src, lines, n);
    if (ni < 0) return;
    A.fixText = false;
    bool fromOcr = A.ocrMode && A.ocrElem == index;
    if (fromOcr) A.ocrAsset = A.doc.el[index].asset;
    select_index(ni);
    if (fromOcr) {
        A.editing = true;
        A.editPushed = false;
        A.caret = (int)strlen(A.doc.el[ni].text);
        A.replaceOnType = true;
    } else toast("Texto leído: haz doble clic para cambiarlo. Arriba puedes cambiar la letra, el tamaño y el color.");
}

// ---- Todos los textos de la imagen: se leen de una vez y se marcan para tocarlos ----

#define OCR_MAX_LINES 400
#define OCR_MAX_BLOCKS 200
static OcrLine g_ocrLines[OCR_MAX_LINES];
static int g_ocrN;
typedef struct { float x, y, w, h; int idx[24]; int n; bool used; } OcrBlock;
static OcrBlock g_ocrBlocks[OCR_MAX_BLOCKS];
static int g_ocrNB;

// Junta los renglones en párrafos: uno debajo de otro, de altura parecida y bien pegados
static void ocr_build_blocks(void)
{
    g_ocrNB = 0;
    for (int i = 0; i < g_ocrN; i++) {
        const OcrLine *L = &g_ocrLines[i];
        int best = -1;
        for (int b = 0; b < g_ocrNB; b++) {
            OcrBlock *B = &g_ocrBlocks[b];
            if (B->n >= 24) continue;
            const OcrLine *P = &g_ocrLines[B->idx[B->n - 1]];
            float h = fmaxf(P->h, L->h);
            float gap = L->y - (P->y + P->h);
            if (gap < -h * 0.3f || gap > h * 0.65f) continue;
            // Mismo tamaño de letra (un título y su explicación pequeña van aparte)
            const OcrLine *F = &g_ocrLines[B->idx[0]];
            if (fmaxf(P->h, L->h) > fminf(P->h, L->h) * 1.25f || fmaxf(F->h, L->h) > fminf(F->h, L->h) * 1.3f) continue;
            // Que se toquen en horizontal (o tengan el centro casi igual)
            float ov = fminf(P->x + P->w, L->x + L->w) - fmaxf(P->x, L->x);
            float dc = fabsf((P->x + P->w / 2) - (L->x + L->w / 2));
            if (ov < fminf(P->w, L->w) * 0.3f && dc > h) continue;
            best = b;
            break;
        }
        if (best < 0) {
            if (g_ocrNB >= OCR_MAX_BLOCKS) continue;
            best = g_ocrNB++;
            g_ocrBlocks[best] = (OcrBlock){ L->x, L->y, L->w, L->h, { 0 }, 0, false };
        }
        OcrBlock *B = &g_ocrBlocks[best];
        B->idx[B->n++] = i;
        float x0 = fminf(B->x, L->x), y0 = fminf(B->y, L->y);
        float x1 = fmaxf(B->x + B->w, L->x + L->w), y1 = fmaxf(B->y + B->h, L->y + L->h);
        B->x = x0; B->y = y0; B->w = x1 - x0; B->h = y1 - y0;
    }
}

static bool ocr_valid(void)
{
    if (!A.ocrMode) return false;
    if (A.ocrElem < 0 || A.ocrElem >= A.doc.count) return false;
    const Element *e = &A.doc.el[A.ocrElem];
    return e->type == EL_IMAGE && e->asset == A.ocrAsset && e->visible;
}

static void ocr_scan(int index)
{
    A.ocrMode = false;
    if (index < 0 || index >= A.doc.count) return;
    Element *e = &A.doc.el[index];
    if (e->type != EL_IMAGE || e->asset < 0 || e->asset >= g_assetCount) return;
    Asset *as = &g_assets[e->asset];
    Image src = LoadImageFromMemory(".png", as->png, as->pngSize);
    if (!src.data) return;
    g_ocrN = run_ocr(src, 0, 0, src.width, src.height, g_ocrLines, OCR_MAX_LINES);
    UnloadImage(src);
    if (g_ocrN == 0) {
        char msg[500];
        if (g_ocrError[0]) snprintf(msg, sizeof(msg), "No he podido leer las letras (%s).", g_ocrError);
        else snprintf(msg, sizeof(msg), "No he encontrado textos en esta imagen.");
        toast(msg);
        return;
    }
    // Renglones de una sola letra suelta: casi siempre son trozos de dibujo
    {
        int k = 0;
        for (int i = 0; i < g_ocrN; i++) {
            int alnum = 0;
            for (const char *c = g_ocrLines[i].text; *c;) {
                int by = 0, cp = GetCodepoint(c, &by);
                c += by > 0 ? by : 1;
                if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= 0xC0 && cp <= 0x24F)) alnum++;
            }
            if (alnum >= 2) g_ocrLines[k++] = g_ocrLines[i];
        }
        g_ocrN = k;
    }
    ocr_build_blocks();
    // Fuera los "textos" que son trocitos de dibujo: muy pocas letras o casi todo signos
    {
        int k = 0;
        for (int b = 0; b < g_ocrNB; b++) {
            int alnum = 0, other = 0;
            for (int q = 0; q < g_ocrBlocks[b].n; q++)
                for (const char *c = g_ocrLines[g_ocrBlocks[b].idx[q]].text; *c;) {
                    int by = 0, cp = GetCodepoint(c, &by);
                    c += by > 0 ? by : 1;
                    if (cp == ' ') continue;
                    if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= 0xC0 && cp <= 0x24F)) alnum++;
                    else other++;
                }
            if (alnum >= 3 && alnum >= (alnum + other) * 0.6f && g_ocrBlocks[b].h >= 6) g_ocrBlocks[k++] = g_ocrBlocks[b];
        }
        g_ocrNB = k;
    }
    if (g_ocrNB == 0) { toast("No he encontrado textos en esta imagen."); return; }
    A.ocrMode = true;
    A.ocrElem = index;
    A.ocrAsset = e->asset;
    A.wand = false;
    A.fixText = false;
    char msg[200];
    snprintf(msg, sizeof(msg), "He encontrado %d textos. Pulsa encima del que quieras cambiar y escribe. Esc para terminar.", g_ocrNB);
    toast(msg);
}

// Esquinas de un recuadro de la imagen (en píxeles de la imagen) en el lienzo
static void ocr_block_corners(const Element *e, const OcrBlock *b, Vector2 *c)
{
    const Asset *as = &g_assets[e->asset];
    float sx = e->w / (float)as->w, sy = e->h / (float)as->h;
    float pad = fminf(8, b->h * 0.15f);
    float xs[2] = { (b->x - pad) * sx - e->w / 2, (b->x + b->w + pad) * sx - e->w / 2 };
    float ys[2] = { (b->y - pad) * sy - e->h / 2, (b->y + b->h + pad) * sy - e->h / 2 };
    c[0] = to_world(e, (Vector2){ xs[0], ys[0] });
    c[1] = to_world(e, (Vector2){ xs[1], ys[0] });
    c[2] = to_world(e, (Vector2){ xs[1], ys[1] });
    c[3] = to_world(e, (Vector2){ xs[0], ys[1] });
}

static int ocr_block_at(Vector2 w)
{
    if (!ocr_valid()) return -1;
    const Element *e = &A.doc.el[A.ocrElem];
    const Asset *as = &g_assets[e->asset];
    Vector2 l = to_local(e, w);
    float px = (l.x + e->w / 2) / e->w * as->w, py = (l.y + e->h / 2) / e->h * as->h;
    int best = -1;
    float bestArea = 1e30f;
    for (int i = 0; i < g_ocrNB; i++) {
        const OcrBlock *b = &g_ocrBlocks[i];
        if (b->used) continue;
        float pad = fminf(8, b->h * 0.15f);
        if (px >= b->x - pad && px <= b->x + b->w + pad && py >= b->y - pad && py <= b->y + b->h + pad && b->w * b->h < bestArea) {
            best = i;
            bestArea = b->w * b->h;
        }
    }
    return best;
}

static void ocr_use_block(int bi)
{
    if (!ocr_valid() || bi < 0 || bi >= g_ocrNB) return;
    OcrBlock *b = &g_ocrBlocks[bi];
    Element *e = &A.doc.el[A.ocrElem];
    Asset *as = &g_assets[e->asset];
    Image src = LoadImageFromMemory(".png", as->png, as->pngSize);
    if (!src.data) return;
    OcrLine tmp[24];
    for (int i = 0; i < b->n; i++) tmp[i] = g_ocrLines[b->idx[i]];
    stop_editing();
    int ni = convert_text_lines(A.ocrElem, src, tmp, b->n);
    if (ni < 0) return;
    b->used = true;
    // La imagen sigue en el mismo sitio (el texto va justo encima), con el hueco ya borrado
    A.ocrAsset = A.doc.el[A.ocrElem].asset;
    select_index(ni);
    Element *t = &A.doc.el[ni];
    A.editing = true;
    A.editPushed = false;
    A.caret = (int)strlen(t->text);
    A.replaceOnType = true;
}

static void ocr_draw(void)
{
    if (!ocr_valid()) { A.ocrMode = false; return; }
    const Element *e = &A.doc.el[A.ocrElem];
    Vector2 mw = GetScreenToWorld2D(GetMousePosition(), A.cam);
    int hover = ui_mouse_free() && A.popup == POP_NONE ? ocr_block_at(mw) : -1;
    for (int i = 0; i < g_ocrNB; i++) {
        if (g_ocrBlocks[i].used) continue;
        Vector2 c[4];
        ocr_block_corners(e, &g_ocrBlocks[i], c);
        for (int k = 0; k < 4; k++) c[k] = GetWorldToScreen2D(c[k], A.cam);
        Color fill = i == hover ? (Color){ 109, 74, 255, 60 } : (Color){ 109, 74, 255, 18 };
        DrawTriangle(c[0], c[2], c[1], fill);
        DrawTriangle(c[0], c[3], c[2], fill);
        DrawTriangle(c[0], c[1], c[2], fill);
        DrawTriangle(c[0], c[2], c[3], fill);
        Color line = i == hover ? COL_ACCENT : (Color){ 109, 74, 255, 150 };
        for (int k = 0; k < 4; k++) DrawLineEx(c[k], c[(k + 1) % 4], i == hover ? 2.5f : 1.5f, line);
    }
}

static void canvas_input(Rectangle cr)
{
    Vector2 m = GetMousePosition();
    bool over = CheckCollisionPointRec(m, cr) && ui_mouse_free() && A.modal == MODAL_NONE && A.popup == POP_NONE && !A.closingPopup;
    Vector2 w = GetScreenToWorld2D(m, A.cam);
    bool space = IsKeyDown(KEY_SPACE) && !A.editing && !ui_textbox_focused();

    if (over) {
        // Como en Canva: rueda o dos dedos en el pad = mover el documento; Ctrl + rueda (o
        // pellizcar en el pad) = zoom; Mayúsculas + rueda = a izquierda y derecha
        Vector2 wv = GetMouseWheelMoveV();
        if (wv.x != 0 || wv.y != 0) {
            if (ctrl_down()) {
                float z = wv.y != 0 ? wv.y : wv.x;
                zoom_at(m, powf(1.12f, fmaxf(-3, fminf(3, z))));
            } else {
                float dx = -wv.x, dy = -wv.y;
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) { dx = -wv.y; dy = 0; }
                A.cam.target.x += dx * 60 / A.cam.zoom;
                A.cam.target.y += dy * 60 / A.cam.zoom;
                // Que el documento no se pierda de vista
                A.cam.target.x = fmaxf(0, fminf((float)A.doc.w, A.cam.target.x));
                A.cam.target.y = fmaxf(0, fminf((float)A.doc.h, A.cam.target.y));
            }
        }
    }

    // Mover la vista: botón derecho, rueda pulsada o espacio + clic
    if (A.drag == DRAG_NONE && over &&
        (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || (space && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))) {
        A.drag = DRAG_PAN;
        A.dragStartMouse = m;
        A.panStartTarget = A.cam.target;
    }
    if (A.drag == DRAG_PAN) {
        Vector2 d = Vector2Scale(Vector2Subtract(m, A.dragStartMouse), 1 / A.cam.zoom);
        A.cam.target = Vector2Subtract(A.panStartTarget, d);
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) A.drag = DRAG_NONE;
        return;
    }

    Element *sel = selected();
    if ((A.wand || A.fixText) && (!sel || sel->type != EL_IMAGE)) { A.wand = false; A.fixText = false; A.boxDrag = false; }
    if (A.boxDrag) {
        A.boxEnd = w;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            A.boxDrag = false;
            Vector2 a = GetWorldToScreen2D(A.boxStart, A.cam), b = GetWorldToScreen2D(A.boxEnd, A.cam);
            bool click = fabsf(a.x - b.x) < 6 && fabsf(a.y - b.y) < 6;
            if (click && A.wand) wand_at(A.sel, A.boxStart);
            else if (click) { A.fixText = false; toast("Ahí no he encontrado texto. Si hay uno, arrastra para rodearlo."); }
            else if (A.fixText) edit_text_box(A.sel);
            else wand_box(A.sel, false);
        }
        return;
    }
    // Textos de la imagen ya leídos: pulsar encima de uno lo convierte en texto para escribir
    if (ocr_valid() && over && A.drag == DRAG_NONE && !space) {
        int b = ocr_block_at(w);
        bool inEdit = A.editing && sel && hit_element(sel, w, 0);
        if (b >= 0 && !inEdit) {
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { ocr_use_block(b); return; }
        } else if (!inEdit && hit_element(&A.doc.el[A.ocrElem], w, 0) && pick(w) == A.ocrElem) {
            // Fuera de los textos encontrados: arrastrando se rodea uno que no haya visto
            SetMouseCursor(MOUSE_CURSOR_CROSSHAIR);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                select_index(A.ocrElem);
                A.fixText = true;
                A.boxDrag = true;
                A.boxStart = A.boxEnd = w;
                return;
            }
        }
    }
    if ((A.wand || A.fixText) && over && A.drag == DRAG_NONE) {
        bool onIt = hit_element(sel, w, 0);
        if (onIt) SetMouseCursor(MOUSE_CURSOR_CROSSHAIR);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !space) {
            if (onIt) { A.boxDrag = true; A.boxStart = A.boxEnd = w; return; }
            A.wand = false;
            A.fixText = false;
        }
    }
    // Recuadro para elegir varios objetos
    if (g_marquee) {
        g_mqEnd = w;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            g_marquee = false;
            Vector2 a = GetWorldToScreen2D(g_mqStart, A.cam), b = GetWorldToScreen2D(g_mqEnd, A.cam);
            if (fabsf(a.x - b.x) < 5 && fabsf(a.y - b.y) < 5) { select_index(g_mqHit); return; }
            Rectangle r = { fminf(g_mqStart.x, g_mqEnd.x), fminf(g_mqStart.y, g_mqEnd.y), fabsf(g_mqStart.x - g_mqEnd.x), fabsf(g_mqStart.y - g_mqEnd.y) };
            static bool on[MAX_ELEMENTS];
            for (int i = 0; i < A.doc.count; i++) {
                const Element *e = &A.doc.el[i];
                on[i] = e->visible && !e->locked && CheckCollisionRecs(r, element_bounds(e));
            }
            select_many(on);
        }
        return;
    }
    // Mover o cambiar el tamaño de varios a la vez
    if (g_groupDrag) {
        group_update(w, m);
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) g_groupDrag = 0;
        SetMouseCursor(g_groupCorner >= 0 ? MOUSE_CURSOR_RESIZE_NWSE : MOUSE_CURSOR_RESIZE_ALL);
        return;
    }
    if (over && A.drag == DRAG_NONE && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !space && !A.editing) {
        int hit = pick(w);
        if (g_multiN >= 2) {
            int k = group_corner_at(m);
            if (k >= 0) { group_begin(2, k, w, m); return; }
            if (shift_down() && hit >= 0) { toggle_in_selection(hit); return; }
            bool inBox = CheckCollisionPointRec(w, multi_bounds());
            if ((hit >= 0 && g_multi[hit]) || (inBox && (hit < 0 || A.doc.el[hit].locked))) { group_begin(1, -1, w, m); return; }
        } else if (shift_down() && hit >= 0) {
            toggle_in_selection(hit);
            return;
        }
        // En un hueco (o encima de algo bloqueado, como un fondo): recuadro para elegir varios
        bool onHandle = sel && !sel->locked && handle_at(sel, m) >= 0;
        if (!onHandle && (hit < 0 || A.doc.el[hit].locked) && !(sel && hit == A.sel)) {
            g_marquee = true;
            g_mqStart = g_mqEnd = w;
            g_mqHit = hit;
            select_index(-1);
            return;
        }
    }
    if (g_multiN >= 2 && over) {
        Rectangle gb = multi_bounds();
        if (group_corner_at(m) >= 0) SetMouseCursor(MOUSE_CURSOR_RESIZE_NWSE);
        else if (CheckCollisionPointRec(w, gb)) SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
        else SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    }
    if (over && A.drag == DRAG_NONE && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !space) {
        if (sel && !sel->locked) {
            int h = handle_at(sel, m);
            if (h >= 0) {
                A.drag = h == 8 ? DRAG_ROTATE : DRAG_RESIZE;
                A.handle = h;
                A.dragOrig = *sel;
                A.dragStartWorld = w;
                A.dragStartMouse = m;
                A.dragPushed = false;
                return;
            }
        }
        int hit = pick(w);
        // Si estamos escribiendo y se pulsa dentro del mismo texto: mover el cursor
        if (A.editing && hit == A.sel && sel) {
            A.caret = caret_from_point(sel, w);
            A.replaceOnType = false;
            return;
        }
        double now = GetTime();
        bool dbl = hit >= 0 && hit == A.lastClickSel && now - A.lastClickTime < 0.45;
        A.lastClickTime = now;
        A.lastClickSel = hit;
        select_index(hit);
        sel = selected();
        if (sel && dbl && sel->type == EL_TEXT && !sel->locked) {
            A.editing = true;
            A.editPushed = false;
            A.caret = caret_from_point(sel, w);
            return;
        }
        if (sel && !sel->locked) {
            A.drag = DRAG_MOVE;
            A.dragOrig = *sel;
            A.dragStartWorld = w;
            A.dragStartMouse = m;
            A.dragPushed = false;
        }
    }

    if (A.drag == DRAG_MOVE || A.drag == DRAG_RESIZE || A.drag == DRAG_ROTATE) {
        if (!sel) { A.drag = DRAG_NONE; return; }
        if (!A.dragPushed && Vector2Distance(m, A.dragStartMouse) > 2) {
            begin_change();
            A.dragPushed = true;
        }
        if (A.dragPushed) {
            if (A.drag == DRAG_MOVE) apply_move(sel, w);
            else if (A.drag == DRAG_RESIZE) apply_resize(sel, w);
            else apply_rotate(sel, w);
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            A.drag = DRAG_NONE;
            A.snapX = A.snapY = false;
        }
    }

    // Forma del puntero
    int cursor = MOUSE_CURSOR_DEFAULT;
    if (A.drag == DRAG_PAN || space) cursor = MOUSE_CURSOR_RESIZE_ALL;
    else if (over && sel && !sel->locked) {
        int h = A.drag == DRAG_RESIZE ? A.handle : handle_at(sel, m);
        if (A.drag == DRAG_ROTATE || h == 8) cursor = MOUSE_CURSOR_POINTING_HAND;
        else if (h >= 0) {
            float a = atan2f((float)HY[h], (float)HX[h]) * RAD2DEG + sel->rotation;
            while (a < 0) a += 180;
            while (a >= 180) a -= 180;
            if (a < 22.5f || a >= 157.5f) cursor = MOUSE_CURSOR_RESIZE_EW;
            else if (a < 67.5f) cursor = MOUSE_CURSOR_RESIZE_NWSE;
            else if (a < 112.5f) cursor = MOUSE_CURSOR_RESIZE_NS;
            else cursor = MOUSE_CURSOR_RESIZE_NESW;
        } else if (A.editing && hit_element(sel, w, 0)) cursor = MOUSE_CURSOR_IBEAM;
        else if (A.drag == DRAG_MOVE || hit_element(sel, w, 0)) cursor = MOUSE_CURSOR_RESIZE_ALL;
    }
    SetMouseCursor(cursor);
}

// ------------------------------------------------------------------ Teclado

static int prev_char(const char *t, int i)
{
    if (i <= 0) return 0;
    i--;
    while (i > 0 && ((unsigned char)t[i] & 0xC0) == 0x80) i--;
    return i;
}

static int next_char(const char *t, int i)
{
    int len = (int)strlen(t);
    if (i >= len) return len;
    i++;
    while (i < len && ((unsigned char)t[i] & 0xC0) == 0x80) i++;
    return i;
}

static void text_edit_change(void)
{
    if (!A.editPushed) { begin_change(); A.editPushed = true; }
    A.dirty = true;
}

static void insert_text(Element *e, const char *s, int n)
{
    int len = (int)strlen(e->text);
    if (len + n >= TEXT_MAX) return;
    text_edit_change();
    memmove(e->text + A.caret + n, e->text + A.caret, len - A.caret + 1);
    memcpy(e->text + A.caret, s, n);
    A.caret += n;
    fix_text_height(e);
}

static bool key_hit(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }

static void text_edit_input(void)
{
    Element *e = selected();
    if (!e || e->type != EL_TEXT) { stop_editing(); return; }
    int len = (int)strlen(e->text);
    if (A.caret > len) A.caret = len;

    int ch;
    // Texto recién sacado de la imagen: lo primero que escribes lo sustituye entero
    // (con las flechas o pulsando dentro del texto se corrige solo una parte)
    if (A.replaceOnType) {
        bool typed = false;
        int pend[32], np = 0;
        while ((ch = GetCharPressed()) != 0 && np < 32) pend[np++] = ch;
        if (np > 0 || key_hit(KEY_BACKSPACE) || key_hit(KEY_DELETE)) {
            text_edit_change();
            e->text[0] = 0;
            A.caret = 0;
            typed = true;
        }
        for (int i = 0; i < np; i++) {
            int bytes = 0;
            const char *u = CodepointToUTF8(pend[i], &bytes);
            insert_text(e, u, bytes);
        }
        if (typed) { A.replaceOnType = false; fix_text_height(e); return; }
        if (key_hit(KEY_LEFT) || key_hit(KEY_RIGHT) || key_hit(KEY_HOME) || key_hit(KEY_END) || key_hit(KEY_UP) || key_hit(KEY_DOWN)) A.replaceOnType = false;
    }
    while ((ch = GetCharPressed()) != 0) {
        int bytes = 0;
        const char *u = CodepointToUTF8(ch, &bytes);
        insert_text(e, u, bytes);
    }
    if (key_hit(KEY_ENTER) || key_hit(KEY_KP_ENTER)) insert_text(e, "\n", 1);
    if (key_hit(KEY_BACKSPACE) && A.caret > 0) {
        text_edit_change();
        int p = prev_char(e->text, A.caret);
        memmove(e->text + p, e->text + A.caret, strlen(e->text) - A.caret + 1);
        A.caret = p;
        fix_text_height(e);
    }
    if (key_hit(KEY_DELETE) && A.caret < (int)strlen(e->text)) {
        text_edit_change();
        int n = next_char(e->text, A.caret);
        memmove(e->text + A.caret, e->text + n, strlen(e->text) - n + 1);
        fix_text_height(e);
    }
    if (key_hit(KEY_LEFT)) A.caret = prev_char(e->text, A.caret);
    if (key_hit(KEY_RIGHT)) A.caret = next_char(e->text, A.caret);
    if (key_hit(KEY_HOME)) { while (A.caret > 0 && e->text[A.caret - 1] != '\n') A.caret--; }
    if (key_hit(KEY_END)) { while (e->text[A.caret] && e->text[A.caret] != '\n') A.caret++; }
    if (key_hit(KEY_UP) || key_hit(KEY_DOWN)) {
        float lh;
        Vector2 p = text_caret_pos(e, A.caret, &lh);
        p.y += key_hit(KEY_UP) ? -lh * 0.5f : lh * 1.5f;
        A.caret = caret_from_point(e, to_world(e, p));
    }
    if (ctrl_down() && IsKeyPressed(KEY_V)) {
        const char *clip = GetClipboardText();
        if (clip) {
            char buf[TEXT_MAX];
            int n = 0;
            for (const char *c = clip; *c && n < TEXT_MAX - 1; c++)
                if (*c != '\r') buf[n++] = *c;
            insert_text(e, buf, n);
        }
    }
    if (ctrl_down() && IsKeyPressed(KEY_A)) A.caret = (int)strlen(e->text);
    if (IsKeyPressed(KEY_ESCAPE)) stop_editing();
}

static void shortcuts(void)
{
    if (A.modal != MODAL_NONE) {
        if (IsKeyPressed(KEY_ESCAPE)) A.modal = MODAL_NONE;
        return;
    }
    if (ui_textbox_focused()) return;
    if (A.editing) { text_edit_input(); return; }
    // Vaciar letras pulsadas para que no se acumulen
    while (GetCharPressed() != 0) {}

    bool c = ctrl_down();
    if (c && IsKeyPressed(KEY_Z)) { if (shift_down()) cmd_redo(); else cmd_undo(); }
    if (c && IsKeyPressed(KEY_Y)) cmd_redo();
    if (c && IsKeyPressed(KEY_S)) cmd_save(shift_down());
    if (c && IsKeyPressed(KEY_O)) cmd_open();
    if (c && IsKeyPressed(KEY_N)) A.modal = MODAL_NEW;
    if (c && IsKeyPressed(KEY_A)) select_all();
    if (c && IsKeyPressed(KEY_D)) cmd_duplicate();
    if (c && IsKeyPressed(KEY_C)) cmd_copy();
    if (c && IsKeyPressed(KEY_X)) { cmd_copy(); cmd_delete(); }
    if (c && IsKeyPressed(KEY_V)) cmd_paste();
    if (c && IsKeyPressed(KEY_E)) cmd_export(false);
    if (c && IsKeyPressed(KEY_P)) cmd_export(true);
    if (c && (IsKeyPressed(KEY_ZERO) || IsKeyPressed(KEY_KP_0))) fit_view();
    if (c && (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))) zoom_at((Vector2){ A.cam.offset.x, A.cam.offset.y }, 1.25f);
    if (c && (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))) zoom_at((Vector2){ A.cam.offset.x, A.cam.offset.y }, 0.8f);
    if (c) return;

    if (g_multiN >= 2) {
        if (IsKeyPressed(KEY_ESCAPE)) { A.popup = POP_NONE; select_index(-1); return; }
        if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) { cmd_delete(); return; }
        float step = shift_down() ? 10 : 1;
        int keys[4] = { KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN };
        float dx[4] = { -step, step, 0, 0 }, dy[4] = { 0, 0, -step, step };
        for (int k = 0; k < 4; k++) {
            if (IsKeyPressed(keys[k])) begin_change();
            if (key_hit(keys[k]))
                for (int i = 0; i < A.doc.count; i++)
                    if (g_multi[i] && !A.doc.el[i].locked) { A.doc.el[i].x += dx[k]; A.doc.el[i].y += dy[k]; }
        }
        return;
    }
    Element *e = selected();
    if (IsKeyPressed(KEY_ESCAPE) && A.ocrMode && !A.editing) { A.ocrMode = false; return; }
    if (IsKeyPressed(KEY_ESCAPE)) { A.popup = POP_NONE; if (A.wand || A.fixText) { A.wand = false; A.fixText = false; A.boxDrag = false; } else select_index(-1); }
    if (!e) return;
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) { if (!e->locked) cmd_delete(); return; }
    if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_F2)) && e->type == EL_TEXT && !e->locked) {
        A.editing = true;
        A.editPushed = false;
        A.caret = (int)strlen(e->text);
        return;
    }
    if (e->locked) return;
    float step = shift_down() ? 10 : 1;
    int keys[4] = { KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN };
    float dx[4] = { -step, step, 0, 0 }, dy[4] = { 0, 0, -step, step };
    for (int i = 0; i < 4; i++) {
        if (IsKeyPressed(keys[i])) begin_change();
        if (key_hit(keys[i])) { e->x += dx[i]; e->y += dy[i]; }
    }
}

// ------------------------------------------------------------------ Dibujo del lienzo

static void draw_checker(Rectangle r, float cell)
{
    DrawRectangleRec(r, WHITE);
    Color g = { 225, 226, 232, 255 };
    for (float y = 0; y < r.height; y += cell)
        for (float x = 0; x < r.width; x += cell)
            if (((int)(x / cell) + (int)(y / cell)) % 2)
                DrawRectangleRec((Rectangle){ r.x + x, r.y + y, fminf(cell, r.width - x), fminf(cell, r.height - y) }, g);
}

static void draw_element_outline(const Element *e, Color c, float thick)
{
    Vector2 p[4];
    float hw = e->w / 2, hh = e->h / 2;
    Vector2 l[4] = { { -hw, -hh }, { hw, -hh }, { hw, hh }, { -hw, hh } };
    for (int i = 0; i < 4; i++) p[i] = GetWorldToScreen2D(to_world(e, l[i]), A.cam);
    for (int i = 0; i < 4; i++) DrawLineEx(p[i], p[(i + 1) % 4], thick, c);
}

static void draw_canvas(Rectangle cr)
{
    DrawRectangleRec(cr, COL_BG);
    ui_scissor(cr.x, cr.y, cr.width, cr.height);

    // Sombra de la hoja
    Vector2 p0 = GetWorldToScreen2D((Vector2){ 0, 0 }, A.cam);
    Vector2 p1 = GetWorldToScreen2D((Vector2){ (float)A.doc.w, (float)A.doc.h }, A.cam);
    Rectangle page = { p0.x, p0.y, p1.x - p0.x, p1.y - p0.y };
    for (int i = 1; i <= 8; i++)
        DrawRectangleRec((Rectangle){ page.x - i + 2, page.y - i + 4, page.width + 2 * i - 4, page.height + 2 * i - 4 }, (Color){ 30, 30, 60, 6 });
    if (A.doc.bg.a < 255) draw_checker(page, 16);

    BeginMode2D(ui_camera(A.cam));
    render_document(&A.doc, true);
    EndMode2D();
    ui_apply_scale();

    // Lo que se sale de la hoja se ve más claro (no saldrá al exportar)
    Color veil = { COL_BG.r, COL_BG.g, COL_BG.b, 215 };
    DrawRectangleRec((Rectangle){ cr.x, cr.y, cr.width, page.y - cr.y }, veil);
    DrawRectangleRec((Rectangle){ cr.x, page.y + page.height, cr.width, cr.y + cr.height - page.y - page.height }, veil);
    DrawRectangleRec((Rectangle){ cr.x, page.y, page.x - cr.x, page.height }, veil);
    DrawRectangleRec((Rectangle){ page.x + page.width, page.y, cr.x + cr.width - page.x - page.width, page.height }, veil);

    // Guías del imán
    Color guide = { 255, 60, 170, 255 };
    if (A.snapX) {
        Vector2 a = GetWorldToScreen2D((Vector2){ A.snapXv, 0 }, A.cam);
        DrawLineEx((Vector2){ a.x, page.y }, (Vector2){ a.x, page.y + page.height }, 1.5f, guide);
    }
    if (A.snapY) {
        Vector2 a = GetWorldToScreen2D((Vector2){ 0, A.snapYv }, A.cam);
        DrawLineEx((Vector2){ page.x, a.y }, (Vector2){ page.x + page.width, a.y }, 1.5f, guide);
    }

    // Borde al pasar el ratón
    Vector2 mw = GetScreenToWorld2D(GetMousePosition(), A.cam);
    if (A.drag == DRAG_NONE && ui_mouse_free() && A.popup == POP_NONE && CheckCollisionPointRec(GetMousePosition(), cr)) {
        int h = pick(mw);
        if (h >= 0 && h != A.sel) draw_element_outline(&A.doc.el[h], (Color){ 109, 74, 255, 140 }, 1.5f);
    }

    if (A.ocrMode) ocr_draw();
    if (A.boxDrag) {
        Vector2 a = GetWorldToScreen2D(A.boxStart, A.cam), b = GetWorldToScreen2D(A.boxEnd, A.cam);
        Rectangle br = { fminf(a.x, b.x), fminf(a.y, b.y), fabsf(a.x - b.x), fabsf(a.y - b.y) };
        DrawRectangleRec(br, (Color){ 109, 74, 255, 40 });
        DrawRectangleLinesEx(br, 2, COL_ACCENT);
    }

    if (g_marquee) {
        Vector2 a = GetWorldToScreen2D(g_mqStart, A.cam), b = GetWorldToScreen2D(g_mqEnd, A.cam);
        Rectangle br = { fminf(a.x, b.x), fminf(a.y, b.y), fabsf(a.x - b.x), fabsf(a.y - b.y) };
        DrawRectangleRec(br, (Color){ 109, 74, 255, 30 });
        DrawRectangleLinesEx(br, 1.5f, COL_ACCENT);
        // Lo que va a quedar elegido
        Rectangle r = { fminf(g_mqStart.x, g_mqEnd.x), fminf(g_mqStart.y, g_mqEnd.y), fabsf(g_mqStart.x - g_mqEnd.x), fabsf(g_mqStart.y - g_mqEnd.y) };
        for (int i = 0; i < A.doc.count; i++) {
            const Element *el = &A.doc.el[i];
            if (el->visible && !el->locked && CheckCollisionRecs(r, element_bounds(el))) draw_element_outline(el, (Color){ 109, 74, 255, 170 }, 1.5f);
        }
    }
    if (g_multiN >= 2) {
        for (int i = 0; i < A.doc.count; i++) if (g_multi[i]) draw_element_outline(&A.doc.el[i], (Color){ 109, 74, 255, 150 }, 1.5f);
        Rectangle gb = multi_bounds();
        Vector2 a = GetWorldToScreen2D((Vector2){ gb.x, gb.y }, A.cam), b = GetWorldToScreen2D((Vector2){ gb.x + gb.width, gb.y + gb.height }, A.cam);
        Rectangle sr = { a.x - 4, a.y - 4, b.x - a.x + 8, b.y - a.y + 8 };
        DrawRectangleLinesEx(sr, 2, COL_ACCENT);
        Vector2 cs[4] = { { sr.x, sr.y }, { sr.x + sr.width, sr.y }, { sr.x + sr.width, sr.y + sr.height }, { sr.x, sr.y + sr.height } };
        for (int k = 0; k < 4; k++) { DrawCircleV(cs[k], 7, WHITE); DrawCircleLinesV(cs[k], 7, COL_ACCENT); }
    }

    Element *e = selected();
    if (e) {
        draw_element_outline(e, e->locked ? COL_MUTED : COL_ACCENT, 2);
        if (!e->locked && !A.editing) {
            Vector2 top = GetWorldToScreen2D(handle_world(e, 1), A.cam);
            Vector2 rot = GetWorldToScreen2D(handle_world(e, 8), A.cam);
            DrawLineEx(top, rot, 1.5f, COL_ACCENT);
            for (int h = 0; h < 9; h++) {
                if (!handle_enabled(e, h)) continue;
                Vector2 s = GetWorldToScreen2D(handle_world(e, h), A.cam);
                if (h == 8) {
                    DrawCircleV(s, 11, WHITE);
                    DrawCircleLinesV(s, 11, COL_ACCENT);
                    DrawRing(s, 4.5f, 6.5f, 40, 320, 16, COL_ACCENT);
                    DrawTriangle((Vector2){ s.x + 6, s.y - 7 }, (Vector2){ s.x + 2, s.y - 3 }, (Vector2){ s.x + 8.5f, s.y - 1.5f }, COL_ACCENT);
                } else if (HX[h] != 0 && HY[h] != 0) {
                    DrawCircleV(s, 7, WHITE);
                    DrawCircleLinesV(s, 7, COL_ACCENT);
                } else {
                    float a = e->rotation;
                    Rectangle pill = { s.x, s.y, HX[h] ? 7 : 18, HX[h] ? 18 : 7 };
                    DrawRectanglePro(pill, (Vector2){ pill.width / 2, pill.height / 2 }, a, WHITE);
                    Rectangle in = { s.x, s.y, pill.width - 2, pill.height - 2 };
                    DrawRectanglePro((Rectangle){ s.x, s.y, pill.width + 2, pill.height + 2 }, (Vector2){ pill.width / 2 + 1, pill.height / 2 + 1 }, a, COL_ACCENT);
                    DrawRectanglePro(in, (Vector2){ in.width / 2, in.height / 2 }, a, WHITE);
                }
            }
        }
        if (A.editing && A.replaceOnType && e->type == EL_TEXT) {
            // Todo el texto "seleccionado": lo que escribas lo sustituye
            Vector2 c[4] = { to_world(e, (Vector2){ -e->w / 2, -e->h / 2 }), to_world(e, (Vector2){ e->w / 2, -e->h / 2 }),
                             to_world(e, (Vector2){ e->w / 2, e->h / 2 }), to_world(e, (Vector2){ -e->w / 2, e->h / 2 }) };
            for (int k = 0; k < 4; k++) c[k] = GetWorldToScreen2D(c[k], A.cam);
            Color sc = { 109, 74, 255, 45 };
            DrawTriangle(c[0], c[2], c[1], sc); DrawTriangle(c[0], c[3], c[2], sc);
            DrawTriangle(c[0], c[1], c[2], sc); DrawTriangle(c[0], c[2], c[3], sc);
        }
        if (A.editing && e->type == EL_TEXT && fmod(GetTime(), 1.0) < 0.6) {
            float lh;
            Vector2 cp = text_caret_pos(e, A.caret, &lh);
            Vector2 a = GetWorldToScreen2D(to_world(e, cp), A.cam);
            Vector2 b = GetWorldToScreen2D(to_world(e, (Vector2){ cp.x, cp.y + lh }), A.cam);
            DrawLineEx(a, b, 2, COL_ACCENT);
        }
        if (A.drag == DRAG_ROTATE) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0f°", e->rotation);
            Vector2 mp = GetMousePosition();
            DrawRectangleRounded((Rectangle){ mp.x + 16, mp.y + 10, 56, 26 }, 0.5f, 6, COL_RAIL);
            ui_text(buf, mp.x + 26, mp.y + 13, 17, WHITE);
        }
    }
    EndScissorMode();
}

// ------------------------------------------------------------------ Ventanas emergentes

static void open_popup(PopupId p, Rectangle anchor)
{
    A.popup = p;
    A.popupAnchor = anchor;
}

static Color *color_target_ptr(void)
{
    Element *e = selected();
    if (A.colorTarget == COLOR_BG) return &A.doc.bg;
    if (!e) return NULL;
    if (A.colorTarget == COLOR_EFFECT) return &e->effectColor;
    return A.colorTarget == COLOR_FILL ? &e->fill : &e->stroke;
}

static void open_color(ColorTarget t, Rectangle anchor)
{
    A.colorTarget = t;
    A.colorPushed = false;
    Color *c = color_target_ptr();
    if (c) {
        Vector3 hsv = ColorToHSV(*c);
        A.hue = hsv.x; A.sat = hsv.y; A.val = hsv.z;
    }
    open_popup(POP_COLOR, anchor);
}

static void set_target_color(Color col)
{
    Color *c = color_target_ptr();
    if (!c) return;
    if (!A.colorPushed) { begin_change(); A.colorPushed = true; c = color_target_ptr(); }
    *c = col;
    Element *e = selected();
    if (A.colorTarget == COLOR_STROKE && e && e->strokeW <= 0 && col.a > 0) e->strokeW = fmaxf(3, fminf(e->w, e->h) * 0.02f);
}

static Rectangle popup_place(Rectangle anchor, float w, float h)
{
    float x = anchor.x, y = anchor.y + anchor.height + 6;
    if (x + w > ui_sw() - 8) x = ui_sw() - 8 - w;
    if (y + h > ui_sh() - 8) y = anchor.y - h - 6;
    if (x < 8) x = 8;
    return (Rectangle){ x, y, w, h };
}

static void popup_card(Rectangle r)
{
    ui_shadow(r, 10);
    DrawRectangleRounded(r, 12 / fminf(r.width, r.height) * 2, 10, WHITE);
}

static bool menu_item(Rectangle r, const char *label, const char *hint)
{
    bool hover = ui_hover(r);
    if (hover) DrawRectangleRounded(r, 0.25f, 6, COL_HOVER);
    ui_text(label, r.x + 12, r.y + (r.height - 18) / 2, 18, COL_TEXT);
    if (hint) ui_text(hint, r.x + r.width - 12 - ui_text_width(hint, 15), r.y + (r.height - 15) / 2, 15, COL_MUTED);
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

// ------------------------------------------------------------------ Tamaño del programa

static const float UI_SIZES[3] = { 1.0f, 1.25f, 1.5f };
static const char *UI_SIZE_NAMES[3] = { "Normal", "Grande", "Muy grande" };
static int g_uiSize = 1;   // por defecto, grande: se lee mejor

static void settings_path(char *out, int max)
{
#ifdef _WIN32
    snprintf(out, max, "%s\\ajustes.txt", plat_data_dir());
#else
    snprintf(out, max, "%s/ajustes.txt", plat_data_dir());
#endif
}

// Si la ventana es pequeña, la lupa baja un poco para que quepa todo
static void apply_ui_size(void)
{
    float s = UI_SIZES[g_uiSize];
    float fit = fminf(GetScreenWidth() / 1100.0f, GetScreenHeight() / 620.0f);
    if (s > fit) s = fmaxf(1.0f, fit);
    if (fabsf(s - ui_scale()) < 0.01f) return;
    ui_set_scale(s);
    fonts_reload_ui((int)ceilf(20 * s));
}

static void load_settings(void)
{
    char p[1200];
    settings_path(p, sizeof(p));
    int size = 0;
    unsigned char *d = plat_read_file(p, &size);
    if (!d) return;
    char buf[256] = { 0 };
    memcpy(buf, d, size < 255 ? size : 255);
    free(d);
    int v;
    const char *k = strstr(buf, "tamano ");
    if (k && sscanf(k + 7, "%d", &v) == 1 && v >= 0 && v < 3) g_uiSize = v;
}

static void save_settings(void)
{
    char p[1200], buf[64];
    settings_path(p, sizeof(p));
    snprintf(buf, sizeof(buf), "tamano %d\n", g_uiSize);
    plat_write_file(p, buf, (int)strlen(buf));
}

static void cmd_ui_size(void)
{
    g_uiSize = (g_uiSize + 1) % 3;
    save_settings();
    apply_ui_size();
    fit_view();
    char m[120];
    snprintf(m, sizeof(m), "Tamaño del programa: %s", UI_SIZE_NAMES[g_uiSize]);
    toast(m);
}

// ------------------------------------------------------------------ Actualizaciones

static bool g_updManual;   // la búsqueda la pidió el usuario: avisar también si no hay nada

static void run_installer(void)
{
    if (!confirm_discard()) return;
    if (plat_run_file(update_installer())) A.quit = true;
    else plat_message(APP_NAME, "No se ha podido abrir el instalador de la versión nueva.");
}

static void cmd_check_updates(void)
{
    UpdateState st = update_state();
    if (st == UPD_READY) { run_installer(); return; }
    if (st == UPD_AVAILABLE || st == UPD_FAILED) { update_download(); return; }
    if (st == UPD_CHECKING || st == UPD_DOWNLOADING) { toast("Un momento, estoy en ello..."); return; }
    g_updManual = true;
    toast("Buscando una versión nueva...");
    update_check(APP_VERSION);
}

// Cada fotograma: avisa de lo que va pasando y, con el instalador bajado, lo abre
static void update_tick(void)
{
    static UpdateState last = UPD_IDLE;
    UpdateState st = update_state();
    if (st == last) return;
    last = st;
    char m[200];
    if (st == UPD_AVAILABLE) {
        snprintf(m, sizeof(m), "Hay una versión nueva (%s). Pulsa el botón de abajo para actualizar.", update_version());
        toast(m);
    } else if (st == UPD_LATEST && g_updManual) {
        toast("Ya tienes la última versión (" APP_VERSION ").");
    } else if (st == UPD_FAILED) {
        toast("No se ha podido bajar la versión nueva. Mira si hay internet y vuelve a probar.");
    } else if (st == UPD_READY) {
        run_installer();
    }
    if (st != UPD_CHECKING) g_updManual = false;
}

static void draw_popup(void)
{
    if (A.popup == POP_NONE) return;
    ui_set_layer(1);
    Rectangle r = { 0 };
    if (A.popup == POP_FILE) {
        r = popup_place(A.popupAnchor, 330, 9 * 42 + 16);
        popup_card(r);
        float y = r.y + 8;
        PopupId keep = POP_NONE;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Nuevo diseño...", "Ctrl+N")) A.modal = MODAL_NEW;
        y += 42;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Abrir...", "Ctrl+O")) { A.popup = POP_NONE; cmd_open(); }
        y += 42;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Importar PDF (de Canva...)", NULL)) { A.popup = POP_NONE; cmd_import_pdf(); }
        y += 42;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Guardar", "Ctrl+S")) { A.popup = POP_NONE; cmd_save(false); }
        y += 42;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Guardar como...", "Ctrl+Mayús+S")) { A.popup = POP_NONE; cmd_save(true); }
        y += 42;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Guardar como plantilla...", NULL)) { A.modal = MODAL_SAVE_TEMPLATE; A.templateName[0] = 0; ui_focus_textbox(100); }
        y += 42;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Tamaño de las letras", UI_SIZE_NAMES[g_uiSize])) { keep = POP_FILE; cmd_ui_size(); }
        y += 42;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Buscar actualizaciones", "v" APP_VERSION)) { A.popup = POP_NONE; cmd_check_updates(); }
        y += 42;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Salir", NULL)) A.quit = true;
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) A.popup = keep;
    } else if (A.popup == POP_EXPORT) {
        r = popup_place(A.popupAnchor, 330, 200);
        popup_card(r);
        ui_text_bold("Descargar el diseño", r.x + 18, r.y + 14, 18, COL_TEXT);
        Rectangle b1 = { r.x + 12, r.y + 46, r.width - 24, 64 };
        Rectangle b2 = { r.x + 12, r.y + 118, r.width - 24, 64 };
        bool h1 = ui_hover(b1), h2 = ui_hover(b2);
        DrawRectangleRounded(b1, 0.2f, 6, h1 ? COL_ACCENT_LT : COL_HOVER);
        DrawRectangleRounded(b2, 0.2f, 6, h2 ? COL_ACCENT_LT : COL_HOVER);
        ui_text_bold("Imagen PNG", b1.x + 14, b1.y + 10, 18, COL_TEXT);
        ui_text("Para WhatsApp, redes o fondo de pantalla", b1.x + 14, b1.y + 35, 15, COL_MUTED);
        ui_text_bold("Documento PDF", b2.x + 14, b2.y + 10, 18, COL_TEXT);
        ui_text("Para imprimir o mandar por correo", b2.x + 14, b2.y + 35, 15, COL_MUTED);
        if (h1 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { A.popup = POP_NONE; cmd_export(false); }
        if (h2 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { A.popup = POP_NONE; cmd_export(true); }
    } else if (A.popup == POP_FONT) {
        Element *e = selected();
        float rowH = 42;
        float maxH = ui_sh() - (A.popupAnchor.y + A.popupAnchor.height) - 24;
        float contentH = 0;
        int lastGroup = -1;
        for (int i = 0; i < g_fontCount; i++) {
            if (g_fonts[i].group != lastGroup) { contentH += 30; lastGroup = g_fonts[i].group; }
            contentH += rowH;
        }
        float h = fminf(contentH + 16, maxH);
        r = popup_place(A.popupAnchor, 280, h);
        popup_card(r);
        if (CheckCollisionPointRec(GetMousePosition(), r)) A.fontScroll -= GetMouseWheelMove() * 60;
        A.fontScroll = Clamp(A.fontScroll, 0, fmaxf(0, contentH + 16 - h));
        ui_set_clip((Rectangle){ r.x, r.y + 6, r.width, r.height - 12 });
        ui_scissor(r.x, r.y + 6, r.width, r.height - 12);
        float y = r.y + 8 - A.fontScroll;
        lastGroup = -1;
        const char *groupNames[3] = { "De Windows", "Incluidas en el programa", "Mis fuentes" };
        for (int i = 0; i < g_fontCount; i++) {
            if (g_fonts[i].group != lastGroup) {
                lastGroup = g_fonts[i].group;
                ui_text_bold(groupNames[lastGroup], r.x + 18, y + 8, 14, COL_MUTED);
                y += 30;
            }
            Rectangle it = { r.x + 8, y, r.width - 16, rowH - 2 };
            y += rowH;
            if (it.y + it.height < r.y || it.y > r.y + r.height) continue;
            bool hover = ui_hover(it);
            bool cur = e && e->font == i;
            if (hover || cur) DrawRectangleRounded(it, 0.25f, 6, cur ? COL_ACCENT_LT : COL_HOVER);
            DrawTextEx(font_get_preview(i), g_fonts[i].name, (Vector2){ it.x + 12, it.y + 7 }, 25, 0, COL_TEXT);
            if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && e) {
                begin_change();
                e->font = i;
                fix_text_height(e);
                A.popup = POP_NONE;
            }
        }
        EndScissorMode();
        ui_clear_clip();
    } else if (A.popup == POP_EFFECT) {
        Element *e = selected();
        if (!e || e->type != EL_TEXT) { A.popup = POP_NONE; ui_set_layer(0); return; }
        r = popup_place(A.popupAnchor, 384, 330);
        popup_card(r);
        ui_text_bold("Efectos del texto", r.x + 18, r.y + 14, 18, COL_TEXT);
        float cw = 110, chh = 82;
        for (int fx = 0; fx < FX_COUNT; fx++) {
            Rectangle c = { r.x + 18 + (fx % 3) * (cw + 9), r.y + 46 + (fx / 3) * (chh + 30), cw, chh };
            bool hover = ui_hover(c);
            bool cur = e->effect == fx;
            Color cardBg = cur ? COL_ACCENT_LT : hover ? COL_HOVER : (Color){ 247, 248, 251, 255 };
            if (fx == FX_NEON) cardBg = (Color){ 30, 28, 52, 255 }; // el neón luce sobre oscuro
            DrawRectangleRounded(c, 0.15f, 6, cardBg);
            if (cur) DrawRectangleRoundedLinesEx(c, 0.15f, 6, 2, COL_ACCENT);
            // Muestra con la fuente y el color del texto
            Element p = *e;
            p.effect = fx;
            p.effectColor = fx == e->effect ? e->effectColor : effect_default_color(e, fx);
            if (fx == FX_NONE) p.effectColor = BLANK;
            strcpy(p.text, "Aa");
            p.fontSize = 44;
            p.align = ALIGN_CENTER;
            p.rotation = 0;
            p.opacity = 1;
            p.w = cw;
            p.h = 52;
            p.x = c.x;
            p.y = c.y + (chh - 52) / 2 - 2;
            if (p.fill.r > 235 && p.fill.g > 235 && p.fill.b > 235 && fx != FX_OUTLINE && fx != FX_NEON) p.fill = (Color){ 200, 200, 210, 255 };
            render_element(&p);
            float tw = ui_text_width(effect_name(fx), 15);
            ui_text(effect_name(fx), c.x + (cw - tw) / 2, c.y + chh + 5, 15, COL_TEXT);
            if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !cur) {
                begin_change();
                e->effect = fx;
                e->effectColor = effect_default_color(e, fx);
            }
        }
        float y = r.y + 46 + 2 * (chh + 30) + 2;
        bool has = e->effect != FX_NONE && e->effect != FX_EMBOSS;
        if (has) {
            ui_text("Color", r.x + 18, y + 6, 15, COL_MUTED);
            Rectangle sw = { r.x + 66, y, 30, 28 };
            if (ui_swatch(sw, e->effectColor, false)) { open_color(COLOR_EFFECT, A.popupAnchor); ui_set_layer(0); return; }
        }
        if (e->effect != FX_NONE) {
            ui_text("Intensidad", r.x + 120, y + 6, 15, COL_MUTED);
            float v = e->effectAmount;
            int sr = ui_slider(20, (Rectangle){ r.x + 206, y + 8, r.width - 230, 12 }, &v, 0, 1);
            if (sr == 1) begin_change();
            if (sr) e->effectAmount = v;
        }
    } else if (A.popup == POP_COLOR) {
        r = popup_place(A.popupAnchor, 284, 372);
        popup_card(r);
        Color *cur = color_target_ptr();
        if (!cur) { A.popup = POP_NONE; ui_set_layer(0); return; }
        const char *title = A.colorTarget == COLOR_BG ? "Color de fondo" : A.colorTarget == COLOR_STROKE ? "Color del borde" : A.colorTarget == COLOR_EFFECT ? "Color del efecto" : "Color";
        ui_text_bold(title, r.x + 16, r.y + 12, 18, COL_TEXT);
        float sx = r.x + 16, sy = r.y + 44, cs = 32, gap = 10;
        for (int i = 0; i < PALETTE_COUNT; i++) {
            Rectangle sw = { sx + (i % 6) * (cs + gap), sy + (i / 6) * (cs + gap), cs, cs };
            Color pc = PALETTE[i];
            bool isSel = cur->r == pc.r && cur->g == pc.g && cur->b == pc.b && cur->a == 255;
            if (ui_swatch(sw, pc, isSel)) {
                set_target_color(pc);
                Vector3 hsv = ColorToHSV(pc);
                A.hue = hsv.x; A.sat = hsv.y; A.val = hsv.z;
            }
        }
        // Selector libre: cuadro de saturación/brillo y barra de tono
        Rectangle sv = { r.x + 16, sy + 4 * (cs + gap) + 4, r.width - 32, 92 };
        Color hueCol = ColorFromHSV(A.hue, 1, 1);
        DrawRectangleGradientH((int)sv.x, (int)sv.y, (int)sv.width, (int)sv.height, WHITE, hueCol);
        DrawRectangleGradientV((int)sv.x, (int)sv.y, (int)sv.width, (int)sv.height, BLANK, BLACK);
        DrawRectangleLinesEx(sv, 1, COL_BORDER);
        Rectangle hb = { sv.x, sv.y + sv.height + 12, sv.width, 14 };
        for (int i = 0; i < (int)hb.width; i++) DrawRectangle((int)hb.x + i, (int)hb.y, 1, (int)hb.height, ColorFromHSV(i / hb.width * 360, 1, 1));
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (ui_hover(sv)) A.colorDrag = 1;
            else if (ui_hover((Rectangle){ hb.x, hb.y - 4, hb.width, hb.height + 8 })) A.colorDrag = 2;
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) A.colorDrag = 0;
        Vector2 m = GetMousePosition();
        if (A.colorDrag == 1) {
            A.sat = Clamp((m.x - sv.x) / sv.width, 0, 1);
            A.val = Clamp(1 - (m.y - sv.y) / sv.height, 0, 1);
            set_target_color(ColorFromHSV(A.hue, A.sat, A.val));
        } else if (A.colorDrag == 2) {
            A.hue = Clamp((m.x - hb.x) / hb.width, 0, 0.999f) * 360;
            set_target_color(ColorFromHSV(A.hue, A.sat, A.val));
        }
        DrawCircleLinesV((Vector2){ sv.x + A.sat * sv.width, sv.y + (1 - A.val) * sv.height }, 6, WHITE);
        DrawCircleLinesV((Vector2){ sv.x + A.sat * sv.width, sv.y + (1 - A.val) * sv.height }, 7, BLACK);
        DrawRectangleLinesEx((Rectangle){ hb.x + A.hue / 360 * hb.width - 3, hb.y - 3, 6, hb.height + 6 }, 2, WHITE);
        // Transparente
        Rectangle tr = { r.x + 16, hb.y + hb.height + 12, 26, 26 };
        if (ui_swatch(tr, BLANK, cur->a == 0)) set_target_color(BLANK);
        ui_text("Sin color", tr.x + 36, tr.y + 4, 16, COL_MUTED);
        char hex[16];
        snprintf(hex, sizeof(hex), "#%02X%02X%02X", cur->r, cur->g, cur->b);
        ui_text(hex, r.x + r.width - 16 - ui_text_width(hex, 16), tr.y + 4, 16, COL_MUTED);
    }
    A.popupRect = r;
    ui_set_layer(0);
}

// ------------------------------------------------------------------ Ventanas centrales (modales)

static void draw_modal(void)
{
    if (A.modal == MODAL_NONE) return;
    ui_set_layer(1);
    int sw = ui_sw(), sh = ui_sh();
    DrawRectangle(0, 0, sw, sh, (Color){ 15, 15, 30, 120 });
    if (A.modal == MODAL_NEW) {
        Rectangle r = { sw / 2.0f - 330, sh / 2.0f - 300, 660, 600 };
        popup_card(r);
        ui_text_bold("Crear un diseño nuevo", r.x + 28, r.y + 24, 24, COL_TEXT);
        ui_text("Elige el tamaño. Podrás cambiarlo después en \"Diseño\".", r.x + 28, r.y + 58, 16, COL_MUTED);
        for (int i = 0; i < PRESET_COUNT; i++) {
            Rectangle b = { r.x + 28 + (i % 2) * 306, r.y + 96 + (i / 2) * 70, 296, 60 };
            bool hover = ui_hover(b);
            DrawRectangleRounded(b, 0.2f, 6, hover ? COL_ACCENT_LT : COL_HOVER);
            ui_text_fit(PRESETS[i].name, b.x + 14, b.y + 9, b.width - 28, 17, COL_TEXT);
            char s[64];
            snprintf(s, sizeof(s), "%d × %d px", PRESETS[i].w, PRESETS[i].h);
            ui_text(s, b.x + 14, b.y + 33, 15, COL_MUTED);
            if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && confirm_discard()) {
                new_document(PRESETS[i].w, PRESETS[i].h);
                A.modal = MODAL_NONE;
            }
        }
        float y = r.y + 96 + ((PRESET_COUNT + 1) / 2) * 70 + 4;
        ui_text("Tamaño a medida:", r.x + 28, y + 10, 17, COL_TEXT);
        ui_textbox(101, (Rectangle){ r.x + 190, y, 100, 40 }, A.customW, 6, NULL);
        ui_text("×", r.x + 300, y + 9, 20, COL_MUTED);
        ui_textbox(102, (Rectangle){ r.x + 322, y, 100, 40 }, A.customH, 6, NULL);
        ui_text("px", r.x + 430, y + 10, 17, COL_MUTED);
        if (ui_button((Rectangle){ r.x + r.width - 140, y, 112, 40 }, "Crear", BTN_PRIMARY)) {
            int w = atoi(A.customW), h = atoi(A.customH);
            if (w >= 16 && h >= 16 && w <= 8000 && h <= 8000) {
                if (confirm_discard()) { new_document(w, h); A.modal = MODAL_NONE; }
            } else {
                plat_message(APP_NAME, "Escribe un ancho y un alto entre 16 y 8000 píxeles.");
            }
        }
        if (ui_button((Rectangle){ r.x + r.width - 140, r.y + r.height - 56, 112, 40 }, "Cancelar", BTN_NORMAL)) A.modal = MODAL_NONE;
    } else if (A.modal == MODAL_SAVE_TEMPLATE) {
        Rectangle r = { sw / 2.0f - 240, sh / 2.0f - 110, 480, 220 };
        popup_card(r);
        ui_text_bold("Guardar como plantilla", r.x + 28, r.y + 24, 22, COL_TEXT);
        ui_text("Aparecerá en \"Plantillas\" para usarla otras veces.", r.x + 28, r.y + 56, 16, COL_MUTED);
        bool submitted = false;
        ui_textbox(100, (Rectangle){ r.x + 28, r.y + 90, r.width - 56, 42 }, A.templateName, 60, &submitted);
        if (!A.templateName[0]) ui_text("Nombre de la plantilla", r.x + 40, r.y + 101, 18, (Color){ 160, 163, 180, 255 });
        bool save = ui_button((Rectangle){ r.x + r.width - 148, r.y + r.height - 62, 120, 42 }, "Guardar", BTN_PRIMARY) || submitted;
        if (ui_button((Rectangle){ r.x + r.width - 280, r.y + r.height - 62, 120, 42 }, "Cancelar", BTN_NORMAL)) A.modal = MODAL_NONE;
        if (save && A.templateName[0]) {
            stop_editing();
            if (templates_save(&A.doc, A.templateName)) {
                templates_refresh();
                toast("Plantilla guardada");
                A.panel = PANEL_TEMPLATES;
                A.panelOpen = true;
            } else {
                plat_message(APP_NAME, "No se ha podido guardar la plantilla.");
            }
            A.modal = MODAL_NONE;
        }
    } else if (A.modal == MODAL_SVG) {
        Rectangle r = { sw / 2.0f - 280, sh / 2.0f - 190, 560, 380 };
        popup_card(r);
        ui_text_bold("¿Cómo quieres abrir el dibujo?", r.x + 28, r.y + 24, 22, COL_TEXT);
        const char *base = GetFileName(A.svgPath);
        ui_text_fit(base, r.x + 28, r.y + 56, r.width - 56, 16, COL_MUTED);
        struct { const char *t, *d; } opt[3] = {
            { "Entero", "Una sola imagen, como está." },
            { "Por partes", "Cada bloque del dibujo se puede mover y borrar." },
            { "Por partes y colores", "Además, cada color por separado para poder cambiarlo." },
        };
        for (int i = 0; i < 3; i++) {
            Rectangle b = { r.x + 28, r.y + 92 + i * 76, r.width - 56, 66 };
            bool hover = ui_hover(b);
            DrawRectangleRounded(b, 0.2f, 6, hover ? COL_ACCENT_LT : COL_HOVER);
            ui_text_bold(opt[i].t, b.x + 16, b.y + 10, 18, COL_TEXT);
            ui_text(opt[i].d, b.x + 16, b.y + 37, 15, COL_MUTED);
            if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                A.modal = MODAL_NONE;
                if (i == 0) {
                    int id = asset_load_file(A.svgPath);
                    if (id >= 0) add_image_asset(id, A.svgAt);
                    else plat_message(APP_NAME, "No se ha podido leer ese dibujo SVG.");
                } else {
                    int n = import_svg_parts(A.svgPath, i - 1, A.svgAt);
                    if (n <= 0) plat_message(APP_NAME, "No se ha podido leer ese dibujo SVG.");
                    else {
                        char m[120];
                        snprintf(m, sizeof(m), "Dibujo separado en %d partes. Míralas en \"Capas\".", n);
                        toast(m);
                    }
                }
            }
        }
        if (ui_button((Rectangle){ r.x + r.width - 140, r.y + r.height - 56, 112, 40 }, "Cancelar", BTN_NORMAL)) A.modal = MODAL_NONE;
    }
    ui_set_layer(0);
}

// ------------------------------------------------------------------ Barra superior

static void draw_topbar(void)
{
    int sw = ui_sw();
    DrawRectangleGradientH(0, 0, sw, TOPBAR_H, COL_ACCENT2, COL_ACCENT);
    // Logotipo
    DrawRectangleRounded((Rectangle){ 16, 12, 32, 32 }, 0.35f, 8, WHITE);
    DrawCircle(27, 24, 6, COL_ACCENT2);
    DrawTriangle((Vector2){ 36, 21 }, (Vector2){ 29, 38 }, (Vector2){ 43, 38 }, COL_ACCENT);
    ui_text_bold(APP_NAME, 58, 17, 21, WHITE);

    float x = 210;
    Rectangle bFile = { x, 10, 92, 36 };
    if (ui_button(bFile, "Archivo", BTN_TOP)) { if (A.popup == POP_FILE) A.popup = POP_NONE; else open_popup(POP_FILE, bFile); }
    x += 100;
    if (ui_button_enabled((Rectangle){ x, 10, 96, 36 }, "Deshacer", BTN_TOP, history_can_undo())) cmd_undo();
    x += 100;
    if (ui_button_enabled((Rectangle){ x, 10, 90, 36 }, "Rehacer", BTN_TOP, history_can_redo())) cmd_redo();
    x += 98;

    char title[400];
    snprintf(title, sizeof(title), "%s%s", doc_display_name(), A.dirty ? " (sin guardar)" : "");
    float right = sw - 16;
    Rectangle bExport = { right - 130, 10, 130, 36 };
    Rectangle bTpl = { bExport.x - 180, 10, 170, 36 };
    float maxW = bTpl.x - x - 20;
    float tw = ui_text_width(title, 18);
    float tx = fmaxf(x + 10, (sw - tw) / 2);
    if (tx + tw > bTpl.x - 20) tx = x + 10;
    ui_text_fit(title, tx, 19, maxW, 18, (Color){ 255, 255, 255, 230 });

    if (ui_button(bTpl, "Guardar plantilla", BTN_TOP)) { A.modal = MODAL_SAVE_TEMPLATE; A.templateName[0] = 0; ui_focus_textbox(100); }
    DrawRectangleRoundedLinesEx(bTpl, 0.45f, 8, 1, (Color){ 255, 255, 255, 150 });
    bool h = ui_hover(bExport);
    DrawRectangleRounded(bExport, 0.45f, 8, h ? (Color){ 245, 243, 255, 255 } : WHITE);
    ui_text_center("Exportar", bExport, 18, COL_ACCENT);
    if (h && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { if (A.popup == POP_EXPORT) A.popup = POP_NONE; else open_popup(POP_EXPORT, bExport); }
}

// ------------------------------------------------------------------ Barra lateral (iconos)

static void icon_panel(PanelId p, float cx, float cy, Color c)
{
    switch (p) {
    case PANEL_DESIGN:
        DrawRectangleRoundedLinesEx((Rectangle){ cx - 11, cy - 9, 22, 18 }, 0.2f, 4, 2, c);
        DrawLineEx((Vector2){ cx - 11, cy - 3 }, (Vector2){ cx + 11, cy - 3 }, 2, c);
        break;
    case PANEL_TEMPLATES:
        for (int i = 0; i < 4; i++) DrawRectangleRounded((Rectangle){ cx - 10 + (i % 2) * 11, cy - 10 + (i / 2) * 11, 9, 9 }, 0.3f, 4, c);
        break;
    case PANEL_ELEMENTS:
        DrawCircleV((Vector2){ cx - 5, cy - 4 }, 6, c);
        DrawRectangle((int)cx + 1, (int)cy - 1, 10, 10, c);
        DrawTriangle((Vector2){ cx - 6, cy + 2 }, (Vector2){ cx - 12, cy + 11 }, (Vector2){ cx, cy + 11 }, c);
        break;
    case PANEL_CLIPART:
        DrawCircleLinesV((Vector2){ cx, cy }, 11, c);
        DrawCircleLinesV((Vector2){ cx, cy }, 10.4f, c);
        DrawCircleV((Vector2){ cx - 4, cy - 3 }, 1.8f, c);
        DrawCircleV((Vector2){ cx + 4, cy - 3 }, 1.8f, c);
        DrawRing((Vector2){ cx, cy + 1 }, 4.5f, 6, 20, 160, 10, c);
        break;
    case PANEL_TEXT:
        DrawTextEx(g_uiFontBold, "T", (Vector2){ cx - 7, cy - 13 }, 26, 0, c);
        break;
    case PANEL_IMAGES:
        DrawRectangleRoundedLinesEx((Rectangle){ cx - 11, cy - 9, 22, 18 }, 0.2f, 4, 2, c);
        DrawTriangle((Vector2){ cx - 3, cy - 1 }, (Vector2){ cx - 9, cy + 7 }, (Vector2){ cx + 3, cy + 7 }, c);
        DrawTriangle((Vector2){ cx + 4, cy + 1 }, (Vector2){ cx, cy + 7 }, (Vector2){ cx + 9, cy + 7 }, c);
        DrawCircleV((Vector2){ cx + 5, cy - 4 }, 2.5f, c);
        break;
    case PANEL_LAYERS:
        for (int i = 0; i < 3; i++) DrawRectangleRoundedLinesEx((Rectangle){ cx - 10 + i * 2, cy - 10 + i * 6, 18, 8 }, 0.3f, 4, 2, c);
        break;
    default:
        break;
    }
}

static const char *PANEL_NAMES[PANEL_COUNT] = { "Diseño", "Plantillas", "Elementos", "Dibujos", "Texto", "Imágenes", "Capas" };

static void draw_rail(void)
{
    int sh = ui_sh();
    DrawRectangle(0, TOPBAR_H, RAIL_W, sh - TOPBAR_H, COL_RAIL);
    for (int i = 0; i < PANEL_COUNT; i++) {
        Rectangle r = { 4, TOPBAR_H + 8 + i * 72.0f, RAIL_W - 8, 66 };
        bool active = A.panelOpen && A.panel == (PanelId)i;
        bool hover = ui_hover(r);
        if (active || hover) DrawRectangleRounded(r, 0.2f, 6, active ? (Color){ 255, 255, 255, 30 } : (Color){ 255, 255, 255, 14 });
        Color c = active ? WHITE : COL_RAIL_TXT;
        icon_panel((PanelId)i, r.x + r.width / 2, r.y + 24, c);
        float tw = ui_text_width(PANEL_NAMES[i], 13);
        ui_text(PANEL_NAMES[i], r.x + (r.width - tw) / 2, r.y + 44, 13, c);
        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (active) A.panelOpen = false;
            else {
                A.panel = (PanelId)i;
                A.panelOpen = true;
                A.panelScroll = 0;
                if (i == PANEL_TEMPLATES) templates_refresh();
            }
        }
    }
}

// ------------------------------------------------------------------ Panel lateral

static float section_title(const char *t, float x, float y)
{
    ui_text_bold(t, x, y, 17, COL_TEXT);
    return y + 30;
}

static void draw_shape_preview(int type, Rectangle cell)
{
    Element e = element_default(type);
    e.w = cell.width * 0.62f;
    e.h = (type == EL_LINE || type == EL_ARROW) ? 16 : cell.height * 0.62f;
    if (type == EL_LINE || type == EL_ARROW) e.strokeW = 5;
    if (type == EL_RECT) e.fill = (Color){ 125, 92, 255, 255 };
    e.x = cell.x + (cell.width - e.w) / 2;
    e.y = cell.y + (cell.height - e.h) / 2;
    render_element(&e);
}

static float panel_design(float x, float y, float w)
{
    y = section_title("Color de fondo", x, y);
    Rectangle sw = { x, y, 40, 40 };
    if (ui_swatch(sw, A.doc.bg, false)) open_color(COLOR_BG, sw);
    for (int i = 0; i < 5; i++) {
        Color c = PALETTE[(int[]){ 5, 11, 15, 21, 18 }[i]];
        Rectangle s = { x + 58 + i * 42, y + 6, 28, 28 };
        if (ui_swatch(s, c, false)) { begin_change(); A.doc.bg = c; }
    }
    y += 62;
    y = section_title("Tamaño del lienzo", x, y);
    char cur[80];
    snprintf(cur, sizeof(cur), "Ahora: %d × %d píxeles", A.doc.w, A.doc.h);
    ui_text(cur, x, y - 4, 16, COL_MUTED);
    y += 24;
    for (int i = 0; i < PRESET_COUNT; i++) {
        Rectangle b = { x, y, w, 50 };
        bool hover = ui_hover(b);
        bool isCur = A.doc.w == PRESETS[i].w && A.doc.h == PRESETS[i].h;
        if (hover || isCur) DrawRectangleRounded(b, 0.2f, 6, isCur ? COL_ACCENT_LT : COL_HOVER);
        // Miniatura de la proporción
        float s = 30.0f / fmaxf(PRESETS[i].w, PRESETS[i].h);
        float pw = PRESETS[i].w * s, ph = PRESETS[i].h * s;
        DrawRectangleLinesEx((Rectangle){ b.x + 12 + (30 - pw) / 2, b.y + 10 + (30 - ph) / 2, pw, ph }, 2, COL_MUTED);
        ui_text_fit(PRESETS[i].name, b.x + 54, b.y + 6, w - 64, 16, COL_TEXT);
        char d[40];
        snprintf(d, sizeof(d), "%d × %d", PRESETS[i].w, PRESETS[i].h);
        ui_text(d, b.x + 54, b.y + 27, 14, COL_MUTED);
        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !isCur) {
            begin_change();
            // Escalamos el contenido para que siga encajando
            float sc = fminf((float)PRESETS[i].w / A.doc.w, (float)PRESETS[i].h / A.doc.h);
            float ox = (PRESETS[i].w - A.doc.w * sc) / 2, oy = (PRESETS[i].h - A.doc.h * sc) / 2;
            for (int k = 0; k < A.doc.count; k++) {
                Element *e = &A.doc.el[k];
                e->x = e->x * sc + ox; e->y = e->y * sc + oy;
                e->w *= sc; e->h *= sc;
                if (e->type == EL_TEXT) { e->fontSize *= sc; fix_text_height(e); }
                if (e->type != EL_TEXT && e->type != EL_IMAGE) e->strokeW *= sc;
            }
            A.doc.w = PRESETS[i].w;
            A.doc.h = PRESETS[i].h;
            fit_view();
        }
        y += 54;
    }
    y += 10;
    if (ui_button((Rectangle){ x, y, w, 44 }, "Crear un diseño nuevo...", BTN_PRIMARY)) A.modal = MODAL_NEW;
    return y + 54;
}

static float panel_templates(float x, float y, float w)
{
    if (ui_button((Rectangle){ x, y, w, 44 }, "Guardar este diseño como plantilla", BTN_PRIMARY)) {
        A.modal = MODAL_SAVE_TEMPLATE;
        A.templateName[0] = 0;
        ui_focus_textbox(100);
    }
    y += 60;
    y = section_title("Mis plantillas", x, y);
    if (g_templateCount == 0) {
        ui_text("Todavía no hay plantillas.", x, y, 16, COL_MUTED);
        return y + 30;
    }
    float cw = (w - 12) / 2, chh = cw + 30;
    int deleteIndex = -1;
    for (int i = 0; i < g_templateCount; i++) {
        Rectangle cell = { x + (i % 2) * (cw + 12), y + (i / 2) * (chh + 10), cw, cw };
        bool hover = ui_hover(cell);
        DrawRectangleRounded(cell, 0.06f, 6, COL_HOVER);
        TemplateInfo *t = &g_templates[i];
        if (t->thumb.id) {
            float s = fminf((cw - 12) / t->thumb.width, (cw - 12) / t->thumb.height);
            float tw = t->thumb.width * s, th = t->thumb.height * s;
            Rectangle dst = { cell.x + (cw - tw) / 2, cell.y + (cw - th) / 2, tw, th };
            DrawRectangleRec((Rectangle){ dst.x + 1, dst.y + 2, dst.width, dst.height }, (Color){ 0, 0, 0, 25 });
            DrawTexturePro(t->thumb, (Rectangle){ 0, 0, (float)t->thumb.width, (float)t->thumb.height }, dst, (Vector2){ 0 }, 0, WHITE);
        }
        if (hover) DrawRectangleRoundedLinesEx(cell, 0.06f, 6, 2, COL_ACCENT);
        ui_text_fit(t->name, cell.x + 2, cell.y + cw + 6, cw - 4, 15, COL_TEXT);
        Rectangle del = { cell.x + cw - 30, cell.y + 6, 24, 24 };
        bool overDel = false;
        if (hover) {
            overDel = ui_hover(del);
            DrawCircleV((Vector2){ del.x + 12, del.y + 12 }, 12, overDel ? COL_DANGER : (Color){ 0, 0, 0, 120 });
            DrawLineEx((Vector2){ del.x + 7, del.y + 7 }, (Vector2){ del.x + 17, del.y + 17 }, 2, WHITE);
            DrawLineEx((Vector2){ del.x + 17, del.y + 7 }, (Vector2){ del.x + 7, del.y + 17 }, 2, WHITE);
        }
        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (overDel) deleteIndex = i;
            else open_template(i);
        }
    }
    if (deleteIndex >= 0) {
        char msg[400];
        snprintf(msg, sizeof(msg), "¿Borrar la plantilla \"%s\"?", g_templates[deleteIndex].name);
        if (plat_ask_yes_no_cancel(APP_NAME, msg) == 1) {
            templates_delete(deleteIndex);
            templates_refresh();
        }
    }
    int rows = (g_templateCount + 1) / 2;
    return y + rows * (chh + 10);
}

static float panel_elements(float x, float y, float w)
{
    y = section_title("Formas", x, y);
    int shapes[] = { EL_RECT, EL_ELLIPSE, EL_TRIANGLE, EL_STAR, EL_HEART, -1 };
    float cs = (w - 20) / 3;
    for (int i = 0; i < 6; i++) {
        Rectangle cell = { x + (i % 3) * (cs + 10), y + (i / 3) * (cs + 10), cs, cs };
        bool hover = ui_hover(cell);
        DrawRectangleRounded(cell, 0.12f, 6, hover ? COL_ACCENT_LT : COL_HOVER);
        if (shapes[i] < 0) {
            // Rectángulo con esquinas redondeadas
            Element e = element_default(EL_RECT);
            e.w = cs * 0.62f; e.h = cs * 0.62f; e.roundness = 0.5f;
            e.fill = (Color){ 31, 181, 166, 255 };
            e.x = cell.x + (cs - e.w) / 2; e.y = cell.y + (cs - e.h) / 2;
            render_element(&e);
            if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                add_shape(EL_RECT);
                Element *s = selected();
                if (s) { s->roundness = 0.4f; s->fill = e.fill; }
            }
        } else {
            draw_shape_preview(shapes[i], cell);
            if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) add_shape(shapes[i]);
        }
    }
    y += 2 * (cs + 10) + 10;
    y = section_title("Líneas", x, y);
    int lines[] = { EL_LINE, EL_ARROW };
    for (int i = 0; i < 2; i++) {
        Rectangle cell = { x + i * (cs + 10), y, cs, cs * 0.6f };
        bool hover = ui_hover(cell);
        DrawRectangleRounded(cell, 0.12f, 6, hover ? COL_ACCENT_LT : COL_HOVER);
        draw_shape_preview(lines[i], cell);
        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) add_shape(lines[i]);
    }
    y += cs * 0.6f + 20;
    ui_text("Consejo: con Mayús. pulsada las formas", x, y, 15, COL_MUTED);
    ui_text("mantienen la proporción y giran de 15° en 15°.", x, y + 20, 15, COL_MUTED);
    return y + 50;
}

// ------------------------------------------------------------------ Dibujos incluidos
// Están en la carpeta "dibujos" junto al programa, una subcarpeta por tema.

typedef struct { char path[1400]; Texture2D thumb; bool tried; } ClipItem;
static const struct { const char *folder; const char *title; } CLIP_CATS[] = {
    { "01-fiestas", "Fiestas y cumpleaños" }, { "02-amor", "Amor" }, { "03-naturaleza", "Naturaleza" },
    { "04-animales", "Animales" }, { "05-comida", "Comida" }, { "06-casa-y-cole", "Casa y cole" },
    { "07-viajes", "Viajes" }, { "08-navidad-y-fiestas", "Navidad, Halloween y Pascua" },
    { "09-caras-y-manos", "Caras y manos" }, { "10-simbolos", "Símbolos" },
};
#define CLIP_CAT_COUNT ((int)(sizeof(CLIP_CATS) / sizeof(CLIP_CATS[0])))
#define MAX_CLIPS 1000
static ClipItem g_clips[MAX_CLIPS];
static int g_clipCount = 0;
static int g_clipCatStart[CLIP_CAT_COUNT + 1];
static bool g_clipsLoaded = false;

static int cmp_str260(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static void clipart_init(void)
{
    if (g_clipsLoaded) return;
    g_clipsLoaded = true;
    static char names[300][260];
    for (int c = 0; c < CLIP_CAT_COUNT; c++) {
        g_clipCatStart[c] = g_clipCount;
        char dir[1300];
        snprintf(dir, sizeof(dir), "%sdibujos/%s", GetApplicationDirectory(), CLIP_CATS[c].folder);
        int n = plat_list_dir(dir, ".svg", names, 300);
        qsort(names, n, 260, cmp_str260);
        for (int i = 0; i < n && g_clipCount < MAX_CLIPS; i++) {
            ClipItem *it = &g_clips[g_clipCount++];
            memset(it, 0, sizeof(*it));
            snprintf(it->path, sizeof(it->path), "%s/%s", dir, names[i]);
        }
    }
    g_clipCatStart[CLIP_CAT_COUNT] = g_clipCount;
}

static Texture2D clip_thumb(int i, int *budget)
{
    ClipItem *it = &g_clips[i];
    if (!it->tried && *budget > 0) {
        (*budget)--;
        it->tried = true;
        int size = 0;
        unsigned char *data = plat_read_file(it->path, &size);
        if (data) {
            Image img = svg_to_image(data, size, 96);
            free(data);
            if (img.data) {
                it->thumb = LoadTextureFromImage(img);
                SetTextureFilter(it->thumb, TEXTURE_FILTER_BILINEAR);
                UnloadImage(img);
            }
        }
    }
    return it->thumb;
}

static void add_clip(int i)
{
    int id = asset_load_file(g_clips[i].path);
    if (id < 0) { toast("No se ha podido abrir ese dibujo"); return; }
    add_image_asset(id, doc_center());
    Element *e = selected();
    if (e) {
        // Los dibujos entran más pequeños que las fotos
        float s = base_size() / fmaxf(e->w, e->h);
        Vector2 c = { e->x + e->w / 2, e->y + e->h / 2 };
        e->w *= s; e->h *= s;
        e->x = c.x - e->w / 2; e->y = c.y - e->h / 2;
    }
}

static float panel_clipart(float x, float y, float w)
{
    clipart_init();
    if (ui_button((Rectangle){ x, y, w, 42 }, "Abrir un dibujo SVG, EPS o AI", BTN_NORMAL)) cmd_upload_image();
    y += 48;
    ui_text("Por ejemplo, los que descargues de", x, y, 14, COL_MUTED);
    ui_text("vector4free o vecteezy, para uso en casa.", x, y + 18, 14, COL_MUTED);
    y += 48;
    if (g_clipCount == 0) {
        ui_text("No se encuentra la carpeta \"dibujos\".", x, y, 15, COL_MUTED);
        return y + 30;
    }
    int budget = 6;
    int cols = 5;
    float gap = 7, cs = (w - gap * (cols - 1)) / cols;
    for (int c = 0; c < CLIP_CAT_COUNT; c++) {
        int first = g_clipCatStart[c], last = g_clipCatStart[c + 1];
        if (first == last) continue;
        y = section_title(CLIP_CATS[c].title, x, y);
        for (int i = first; i < last; i++) {
            int k = i - first;
            Rectangle cell = { x + (k % cols) * (cs + gap), y + (k / cols) * (cs + gap), cs, cs };
            if (cell.y > ui_sh() || cell.y + cell.height < TOPBAR_H) continue;
            bool hover = ui_hover(cell);
            DrawRectangleRounded(cell, 0.18f, 6, hover ? COL_ACCENT_LT : COL_HOVER);
            Texture2D t = clip_thumb(i, &budget);
            if (t.id) {
                float s = (cs - 12) / fmaxf(t.width, t.height);
                Rectangle dst = { cell.x + (cs - t.width * s) / 2, cell.y + (cs - t.height * s) / 2, t.width * s, t.height * s };
                DrawTexturePro(t, (Rectangle){ 0, 0, (float)t.width, (float)t.height }, dst, (Vector2){ 0 }, 0, WHITE);
            }
            if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) add_clip(i);
        }
        y += ((last - first + cols - 1) / cols) * (cs + gap) + 12;
    }
    ui_text("Dibujos: Twemoji (CC BY 4.0).", x, y, 13, COL_MUTED);
    return y + 30;
}

static float panel_text(float x, float y, float w)
{
    struct { const char *label; int font; float size; float factor; } adds[] = {
        { "Añadir un título", 1, 30, 0.11f },
        { "Añadir un subtítulo", 0, 22, 0.065f },
        { "Añadir un poco de texto", 0, 17, 0.04f },
    };
    for (int i = 0; i < 3; i++) {
        Rectangle b = { x, y, w, i == 0 ? 60.0f : 50.0f };
        bool hover = ui_hover(b);
        DrawRectangleRounded(b, 0.2f, 6, hover ? COL_ACCENT_LT : COL_HOVER);
        Font f = font_get(adds[i].font);
        Vector2 m = MeasureTextEx(f, adds[i].label, adds[i].size, 0);
        DrawTextEx(f, adds[i].label, (Vector2){ b.x + 14, b.y + (b.height - m.y) / 2 }, adds[i].size, 0, COL_TEXT);
        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            const char *txt = i == 0 ? "Un título" : i == 1 ? "Un subtítulo" : "Escribe aquí tu texto";
            add_text(txt, adds[i].font, adds[i].factor);
        }
        y += b.height + 10;
    }
    y += 10;
    if (ui_button((Rectangle){ x, y, w, 42 }, "Añadir una fuente (.ttf u .otf)", BTN_NORMAL)) cmd_add_font();
    y += 48;
    ui_text("Hay miles gratis en fonts.google.com", x, y, 14, COL_MUTED);
    ui_text("o dafont.com. Descárgala y añádela aquí.", x, y + 18, 14, COL_MUTED);
    y += 48;
    const char *groupNames[3] = { "Fuentes de Windows", "Incluidas en el programa", "Mis fuentes" };
    int lastGroup = -1;
    for (int i = 0; i < g_fontCount; i++) {
        if (g_fonts[i].group != lastGroup) {
            lastGroup = g_fonts[i].group;
            y += 6;
            y = section_title(groupNames[lastGroup], x, y);
        }
        Rectangle b = { x, y, w, 46 };
        y += 48;
        if (b.y > ui_sh() || b.y + b.height < TOPBAR_H) continue; // fuera de la vista
        bool hover = ui_hover(b);
        if (hover) DrawRectangleRounded(b, 0.2f, 6, COL_HOVER);
        DrawTextEx(font_get_preview(i), g_fonts[i].name, (Vector2){ b.x + 14, b.y + 9 }, 27, 0, COL_TEXT);
        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) add_text("Tu texto aquí", i, 0.08f);
    }
    if (lastGroup != FONT_GROUP_USER) {
        y += 6;
        y = section_title(groupNames[FONT_GROUP_USER], x, y);
        ui_text("Aún no has añadido ninguna.", x, y, 15, COL_MUTED);
        y += 26;
    }
    y += 10;
    ui_text("Doble clic en un texto para escribir.", x, y, 15, COL_MUTED);
    return y + 30;
}

static float panel_images(float x, float y, float w)
{
    if (ui_button((Rectangle){ x, y, w, 44 }, "Subir una imagen del ordenador", BTN_PRIMARY)) cmd_upload_image();
    y += 56;
    ui_text("También puedes arrastrar fotos a la", x, y, 15, COL_MUTED);
    ui_text("ventana o pegarlas con Ctrl+V.", x, y + 20, 15, COL_MUTED);
    y += 54;
    y = section_title("Imágenes usadas", x, y);
    if (g_assetCount == 0) {
        ui_text("Aún no has subido ninguna.", x, y, 16, COL_MUTED);
        return y + 30;
    }
    float cw = (w - 10) / 2;
    int shown = 0;
    for (int i = 0; i < g_assetCount; i++) {
        if (g_assets[i].hidden) continue;
        int k = shown++;
        Rectangle cell = { x + (k % 2) * (cw + 10), y + (k / 2) * (cw * 0.8f + 10), cw, cw * 0.8f };
        bool hover = ui_hover(cell);
        DrawRectangleRounded(cell, 0.08f, 6, COL_HOVER);
        Texture2D t = g_assets[i].tex;
        float s = fminf((cell.width - 8) / t.width, (cell.height - 8) / t.height);
        Rectangle dst = { cell.x + (cell.width - t.width * s) / 2, cell.y + (cell.height - t.height * s) / 2, t.width * s, t.height * s };
        DrawTexturePro(t, (Rectangle){ 0, 0, (float)t.width, (float)t.height }, dst, (Vector2){ 0 }, 0, WHITE);
        if (hover) DrawRectangleRoundedLinesEx(cell, 0.08f, 6, 2, COL_ACCENT);
        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) add_image_asset(i, doc_center());
    }
    return y + ((shown + 1) / 2) * (cw * 0.8f + 10);
}

static void element_label(const Element *e, char *out, int max)
{
    if (e->type == EL_TEXT) {
        char first[64];
        int n = 0;
        for (const char *p = e->text; *p && *p != '\n' && n < 40; p++) first[n++] = *p;
        first[n] = 0;
        snprintf(out, max, "\"%s\"", n ? first : "(vacío)");
    } else if (e->type == EL_IMAGE && e->text[0]) {
        snprintf(out, max, "%s", e->text);
    } else {
        snprintf(out, max, "%s", element_type_name(e->type));
    }
}

static void icon_eye(Vector2 c, bool open, Color col)
{
    Vector2 p[24];
    for (int i = 0; i < 24; i++) {
        float a = i / 24.0f * 2 * PI;
        p[i] = (Vector2){ c.x + cosf(a) * 9, c.y + sinf(a) * 5 };
    }
    for (int i = 0; i < 24; i++) DrawLineEx(p[i], p[(i + 1) % 24], 1.6f, col);
    if (open) DrawCircleV(c, 2.8f, col);
    else DrawLineEx((Vector2){ c.x - 9, c.y + 6 }, (Vector2){ c.x + 9, c.y - 6 }, 1.8f, col);
}

static void icon_lock(Vector2 c, bool locked, Color col)
{
    DrawRectangleRounded((Rectangle){ c.x - 6, c.y - 1, 12, 9 }, 0.3f, 4, col);
    if (locked) DrawRing((Vector2){ c.x, c.y - 2 }, 3, 4.6f, 180, 360, 12, col);
    else DrawRing((Vector2){ c.x + 4, c.y - 3 }, 3, 4.6f, 160, 360, 12, col);
}

static float panel_layers(float x, float y, float w)
{
    if (A.doc.count == 0) {
        ui_text("El diseño está vacío.", x, y, 16, COL_MUTED);
        ui_text("Añade textos, formas o imágenes.", x, y + 22, 16, COL_MUTED);
        return y + 50;
    }
    ui_text("Arriba lo que está delante.", x, y, 15, COL_MUTED);
    y += 28;
    for (int i = A.doc.count - 1; i >= 0; i--) {
        Element *e = &A.doc.el[i];
        Rectangle row = { x, y, w, 44 };
        bool hover = ui_hover(row);
        bool isSel = i == A.sel;
        if (hover || isSel) DrawRectangleRounded(row, 0.2f, 6, isSel ? COL_ACCENT_LT : COL_HOVER);
        Rectangle eye = { row.x + 4, row.y + 7, 30, 30 };
        Rectangle lock = { row.x + 36, row.y + 7, 30, 30 };
        Color ic = e->visible ? COL_TEXT : COL_MUTED;
        icon_eye((Vector2){ eye.x + 15, eye.y + 15 }, e->visible, ui_hover(eye) ? COL_ACCENT : ic);
        icon_lock((Vector2){ lock.x + 15, lock.y + 15 }, e->locked, ui_hover(lock) ? COL_ACCENT : (e->locked ? COL_TEXT : (Color){ 180, 183, 198, 255 }));
        // Muestra de color
        Color sc = (e->type == EL_IMAGE && !e->tint) ? (Color){ 200, 200, 210, 255 } : e->fill;
        if (sc.a == 0) sc = e->stroke;
        DrawRectangleRounded((Rectangle){ row.x + 74, row.y + 13, 18, 18 }, 0.3f, 4, sc);
        char label[128];
        element_label(e, label, sizeof(label));
        ui_text_fit(label, row.x + 102, row.y + 12, w - 110, 17, e->visible ? COL_TEXT : COL_MUTED);
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (ui_hover(eye)) { begin_change(); e->visible = !e->visible; }
            else if (ui_hover(lock)) { begin_change(); e->locked = !e->locked; }
            else if (hover) select_index(i);
        }
        y += 48;
    }
    return y;
}

static void draw_panel(void)
{
    if (!A.panelOpen) return;
    int sh = ui_sh();
    Rectangle pr = { RAIL_W, TOPBAR_H, PANEL_W, sh - TOPBAR_H };
    DrawRectangleRec(pr, COL_PANEL);
    DrawLine((int)(pr.x + pr.width), (int)pr.y, (int)(pr.x + pr.width), sh, COL_BORDER);
    ui_text_bold(PANEL_NAMES[A.panel], pr.x + 20, pr.y + 18, 22, COL_TEXT);

    float bottomBar = A.panel == PANEL_LAYERS ? 64 : 0;
    Rectangle content = { pr.x, pr.y + 56, pr.width, pr.height - 56 - bottomBar };
    if (ui_hover(content)) {
        A.panelScroll -= GetMouseWheelMove() * 60;
    }
    float maxScroll = fmaxf(0, A.panelContentH - content.height + 20);
    A.panelScroll = Clamp(A.panelScroll, 0, maxScroll);

    ui_scissor(content.x, content.y, content.width, content.height);
    ui_set_clip(content); // los clics fuera de la zona visible no cuentan
    float x = pr.x + 20, w = pr.width - 40;
    float y0 = content.y + 4 - A.panelScroll;
    float yEnd = y0;
    switch (A.panel) {
    case PANEL_DESIGN: yEnd = panel_design(x, y0, w); break;
    case PANEL_TEMPLATES: yEnd = panel_templates(x, y0, w); break;
    case PANEL_ELEMENTS: yEnd = panel_elements(x, y0, w); break;
    case PANEL_CLIPART: yEnd = panel_clipart(x, y0, w); break;
    case PANEL_TEXT: yEnd = panel_text(x, y0, w); break;
    case PANEL_IMAGES: yEnd = panel_images(x, y0, w); break;
    case PANEL_LAYERS: yEnd = panel_layers(x, y0, w); break;
    default: break;
    }
    ui_clear_clip();
    A.panelContentH = yEnd - y0;
    EndScissorMode();

    if (A.panel == PANEL_LAYERS) {
        float by = pr.y + pr.height - bottomBar + 10;
        DrawLine((int)pr.x, (int)by - 10, (int)(pr.x + pr.width), (int)by - 10, COL_BORDER);
        bool has = selected() != NULL;
        float bw = (w - 18) / 4;
        if (ui_button_enabled((Rectangle){ x, by, bw, 40 }, "Subir", BTN_NORMAL, has)) cmd_layer(1);
        if (ui_button_enabled((Rectangle){ x + bw + 6, by, bw, 40 }, "Bajar", BTN_NORMAL, has)) cmd_layer(-1);
        if (ui_button_enabled((Rectangle){ x + 2 * (bw + 6), by, bw, 40 }, "Copiar", BTN_NORMAL, has)) cmd_duplicate();
        if (ui_button_enabled((Rectangle){ x + 3 * (bw + 6), by, bw, 40 }, "Borrar", BTN_DANGER, has)) cmd_delete();
    }
}

// ------------------------------------------------------------------ Barra de herramientas (según lo seleccionado)

static float tb_label(const char *t, float x, float y)
{
    ui_text(t, x, y + 4, 13, COL_MUTED);
    return x;
}

// Si lo siguiente (de ancho w) no cabe en esta fila, empieza otra debajo
static void tb_fit(float *x, float *y, float w)
{
    if (*x + w <= g_tbRight || *x <= g_tbLeft + 1) return;
    *x = g_tbLeft;
    *y += TOOLBAR_H;
    g_tbRowsNow++;
}

static float tb_slider(int id, const char *label, float x, float y, float w, float *field, float min, float max)
{
    tb_label(label, x, y);
    float v = *field;
    int r = ui_slider(id, (Rectangle){ x + 4, y + 30, w - 8, 12 }, &v, min, max);
    if (r == 1) begin_change();
    if (r) *field = v;
    return x + w + 14;
}

static float tb_divider(float x, float y)
{
    if (x <= g_tbLeft + 1) return x;
    DrawLine((int)x, (int)y + 10, (int)x, (int)y + TOOLBAR_H - 10, COL_BORDER);
    return x + 14;
}

static void draw_toolbar_items(Element *e, Rectangle tb, float x, float y);

static void draw_toolbar(void)
{
    Rectangle cr = canvas_rect();
    Rectangle tb = { cr.x, TOPBAR_H, cr.width, (float)TOOLBAR_H * g_tbRows };
    DrawRectangleRec(tb, COL_PANEL);
    DrawLine((int)tb.x, (int)(tb.y + tb.height - 1), (int)(tb.x + tb.width), (int)(tb.y + tb.height - 1), COL_BORDER);
    float x = tb.x + 16, y = tb.y;
    g_tbLeft = x;
    g_tbRight = tb.x + tb.width - 16;
    g_tbRowsNow = 1;
    Element *e = selected();
    draw_toolbar_items(e, tb, x, y);
    if (g_tbRowsNow != g_tbRows) { g_tbRows = g_tbRowsNow; }
}

static void draw_toolbar_items(Element *e, Rectangle tb, float x, float y)
{

    if (!e && g_multiN >= 2) {
        char t[80];
        snprintf(t, sizeof(t), "%d objetos seleccionados", g_multiN);
        ui_text_bold(t, x, y + 18, 17, COL_TEXT);
        x += ui_text_width(t, 17) + 20;
        float need = 76 + 92 + 88 + 3 * 6;
        const char *hint = "Arrástralos para moverlos; las esquinas cambian el tamaño.";
        if (x + ui_text_width(hint, 15) + 20 + need <= g_tbRight) ui_text(hint, x, y + 19, 15, COL_MUTED);
        else if (x + need > g_tbRight) { y += TOOLBAR_H; g_tbRowsNow++; }
        float bx = tb.x + tb.width - 16;
        bx -= 76;
        if (ui_button((Rectangle){ bx, y + 10, 76, 36 }, "Borrar", BTN_DANGER)) cmd_delete();
        bx -= 6 + 92;
        if (ui_button((Rectangle){ bx, y + 10, 92, 36 }, "Bloquear", BTN_GHOST)) {
            begin_change();
            for (int i = 0; i < A.doc.count; i++) if (g_multi[i]) A.doc.el[i].locked = true;
            select_index(-1);
            toast("Bloqueados. Para soltar uno, púlsalo y elige \"Desbloquear\".");
        }
        bx -= 6 + 88;
        if (ui_button((Rectangle){ bx, y + 10, 88, 36 }, "Duplicar", BTN_GHOST)) cmd_duplicate();
        return;
    }
    if (!e) {
        tb_label("Fondo", x, y);
        Rectangle sw = { x, y + 20, 30, 28 };
        if (ui_swatch(sw, A.doc.bg, false)) open_color(COLOR_BG, sw);
        x += 60;
        x = tb_divider(x, y);
        char s[80];
        snprintf(s, sizeof(s), "Lienzo de %d × %d px", A.doc.w, A.doc.h);
        ui_text(s, x, y + 18, 17, COL_TEXT);
        x += ui_text_width(s, 17) + 14;
        tb_fit(&x, &y, 150);
        if (ui_button((Rectangle){ x, y + 10, 150, 36 }, "Cambiar tamaño", BTN_NORMAL)) { A.panel = PANEL_DESIGN; A.panelOpen = true; A.panelScroll = 0; }
        x += 164;
        const char *hint = "Elige algo en el panel de la izquierda para añadirlo.";
        if (x + ui_text_width(hint, 15) <= g_tbRight) ui_text(hint, x, y + 19, 15, COL_MUTED);
        return;
    }

    bool shape = e->type == EL_RECT || e->type == EL_ELLIPSE || e->type == EL_TRIANGLE || e->type == EL_STAR || e->type == EL_HEART;
    if (e->type != EL_IMAGE) {
        tb_label(e->type == EL_TEXT ? "Color" : e->type == EL_LINE || e->type == EL_ARROW ? "Color" : "Relleno", x, y);
        Rectangle sw = { x, y + 20, 30, 28 };
        if (ui_swatch(sw, e->fill, A.popup == POP_COLOR && A.colorTarget == COLOR_FILL)) open_color(COLOR_FILL, sw);
        x += 56;
    }
    if (e->type == EL_IMAGE && e->tint) {
        tb_label("Color", x, y);
        Rectangle sw = { x, y + 20, 30, 28 };
        if (ui_swatch(sw, e->fill, A.popup == POP_COLOR && A.colorTarget == COLOR_FILL)) open_color(COLOR_FILL, sw);
        x += 56;
    }
    if (shape || e->type == EL_IMAGE) {
        tb_fit(&x, &y, 52 + 110);
        tb_label("Borde", x, y);
        Rectangle sw = { x, y + 20, 30, 28 };
        Color bc = e->strokeW > 0 ? e->stroke : BLANK;
        if (ui_swatch(sw, bc, A.popup == POP_COLOR && A.colorTarget == COLOR_STROKE)) open_color(COLOR_STROKE, sw);
        x += 52;
        x = tb_slider(1, "Grosor del borde", x, y, 110, &e->strokeW, 0, fmaxf(4, fminf(e->w, e->h) * 0.25f));
    }
    if (e->type == EL_LINE || e->type == EL_ARROW) {
        tb_fit(&x, &y, 110);
        x = tb_slider(2, "Grosor", x, y, 110, &e->strokeW, 1, fmaxf(10, e->w * 0.15f));
        if (e->h < e->strokeW * 3) e->h = e->strokeW * 3;
    }
    if (e->type == EL_RECT) tb_fit(&x, &y, 100);
    if (e->type == EL_RECT) x = tb_slider(3, "Esquinas", x, y, 100, &e->roundness, 0, 1);

    if (e->type == EL_TEXT) {
        tb_fit(&x, &y, 14 + 180 + 124);
        x = tb_divider(x, y);
        Rectangle fb = { x, y + 10, 170, 36 };
        bool h = ui_hover(fb);
        DrawRectangleRounded(fb, 0.25f, 6, h ? COL_HOVER : WHITE);
        DrawRectangleRoundedLinesEx(fb, 0.25f, 6, 1, COL_BORDER);
        ui_scissor(fb.x, fb.y, fb.width - 26, fb.height);
        DrawTextEx(font_get(e->font), font_name(e->font), (Vector2){ fb.x + 10, fb.y + 8 }, 19, 0, COL_TEXT);
        EndScissorMode();
        DrawTriangle((Vector2){ fb.x + fb.width - 20, fb.y + 15 }, (Vector2){ fb.x + fb.width - 15, fb.y + 21 }, (Vector2){ fb.x + fb.width - 10, fb.y + 15 }, COL_MUTED);
        if (h && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { A.fontScroll = 0; open_popup(POP_FONT, fb); }
        x += 180;
        // Tamaño de letra
        if (ui_button((Rectangle){ x, y + 10, 32, 36 }, "-", BTN_NORMAL)) { begin_change(); e->fontSize = fmaxf(6, roundf(e->fontSize * 0.9f)); fix_text_height(e); }
        char sz[16];
        snprintf(sz, sizeof(sz), "%.0f", e->fontSize);
        ui_text_center(sz, (Rectangle){ x + 32, y + 10, 48, 36 }, 18, COL_TEXT);
        if (ui_button((Rectangle){ x + 80, y + 10, 32, 36 }, "+", BTN_NORMAL)) { begin_change(); e->fontSize = fminf(1000, roundf(e->fontSize * 1.1f + 1)); fix_text_height(e); }
        x += 124;
        tb_fit(&x, &y, 96 + 170);
        Rectangle fxb = { x, y + 10, 86, 36 };
        bool fxOn = e->effect != FX_NONE;
        if (fxOn) DrawRectangleRounded(fxb, 0.25f, 6, COL_ACCENT_LT);
        if (ui_button(fxb, "Efectos", fxOn ? BTN_GHOST : BTN_NORMAL)) open_popup(POP_EFFECT, fxb);
        if (fxOn) ui_text_center("Efectos", fxb, 17, COL_ACCENT);
        x += 96;
        const char *al[3] = { "Izq.", "Centro", "Der." };
        float aw[3] = { 48, 64, 48 };
        for (int i = 0; i < 3; i++) {
            Rectangle b = { x, y + 10, aw[i], 36 };
            bool act = e->align == i;
            if (act) DrawRectangleRounded(b, 0.25f, 6, COL_ACCENT_LT);
            if (ui_button(b, al[i], act ? BTN_GHOST : BTN_GHOST)) { begin_change(); e->align = i; }
            if (act) ui_text_center(al[i], b, 17, COL_ACCENT);
            x += aw[i] + 2;
        }
        x += 10;
        tb_fit(&x, &y, 96);
        float ls0 = e->letterSpacing;
        x = tb_slider(6, "Espacio letras", x, y, 96, &e->letterSpacing, -0.05f, 0.6f);
        if (e->letterSpacing != ls0) fix_text_height(e);
    }
    tb_fit(&x, &y, 14 + 110);
    x = tb_divider(x, y);
    x = tb_slider(4, "Transparencia", x, y, 110, &e->opacity, 0.05f, 1);
    if (e->type == EL_IMAGE && !e->locked) {
        tb_fit(&x, &y, 14 + 140);
        x = tb_divider(x, y);
        Rectangle wb = { x, y + 10, 140, 36 };
        if (ui_button(wb, A.wand ? "Terminar" : "Lápiz mágico", A.wand ? BTN_PRIMARY : BTN_NORMAL)) {
            A.wand = !A.wand;
            A.fixText = false;
            if (A.wand) toast("Toca un dibujo, o rodéalo arrastrando, para separarlo del fondo");
        }
        x += 150;
        tb_fit(&x, &y, 210);
        Rectangle fb2 = { x, y + 10, 210, 36 };
        bool ocrOn = A.ocrMode && A.ocrElem == A.sel;
        if (ui_button(fb2, ocrOn ? "Terminar" : "Editar textos de la imagen", ocrOn ? BTN_PRIMARY : BTN_NORMAL)) {
            A.wand = false;
            A.fixText = false;
            if (ocrOn) A.ocrMode = false;
            else if (A.ocrMode == false && A.ocrAsset == e->asset && A.ocrElem == A.sel && g_ocrNB > 0) A.ocrMode = true;
            else {
                toast("Leyendo los textos de la imagen... (tarda unos segundos)");
                A.ocrPending = 2;
            }
        }
        x += 224;
        if (A.wand || A.fixText) {
            tb_fit(&x, &y, 124);
            tb_label("Parecido al fondo", x, y);
            ui_slider(5, (Rectangle){ x + 4, y + 30, 120 - 8, 12 }, &A.wandTol, 0, 1);
            x += 134;
        }
    }

    // Botones de la derecha: si no caben al lado, en una fila nueva
    float need = 76 + (e->locked ? 118 : 92) + 88 + 72 + 80 + 5 * 6;
    if (x + need > g_tbRight) { x = g_tbLeft; y += TOOLBAR_H; g_tbRowsNow++; }
    float bx = tb.x + tb.width - 16;
    struct { const char *label; int id; float w; } btns[] = {
        { "Borrar", 0, 76 }, { e->locked ? "Desbloquear" : "Bloquear", 1, e->locked ? 118 : 92 },
        { "Duplicar", 2, 88 }, { "Detrás", 3, 72 }, { "Delante", 4, 80 },
    };
    for (int i = 0; i < 5; i++) {
        bx -= btns[i].w;
        if (bx < x) break;
        Rectangle b = { bx, y + 10, btns[i].w, 36 };
        if (ui_button(b, btns[i].label, btns[i].id == 0 ? BTN_DANGER : BTN_GHOST)) {
            switch (btns[i].id) {
            case 0: cmd_delete(); break;
            case 1: begin_change(); e->locked = !e->locked; break;
            case 2: cmd_duplicate(); break;
            case 3: cmd_layer(-1); break;
            case 4: cmd_layer(1); break;
            }
        }
        bx -= 6;
    }
}

// ------------------------------------------------------------------ Barra inferior

static void draw_bottombar(void)
{
    Rectangle cr = canvas_rect();
    Rectangle bb = { cr.x, cr.y + cr.height, cr.width, BOTTOM_H };
    DrawRectangleRec(bb, COL_PANEL);
    DrawLine((int)bb.x, (int)bb.y, (int)(bb.x + bb.width), (int)bb.y, COL_BORDER);
    UpdateState us = update_state();
    if (us == UPD_AVAILABLE || us == UPD_FAILED || us == UPD_READY) {
        char t[120];
        snprintf(t, sizeof(t), us == UPD_FAILED ? "Volver a probar a bajar la versión %s" : "Actualizar a la versión %s", update_version());
        if (ui_button((Rectangle){ bb.x + 10, bb.y + 4, 330, 32 }, t, BTN_PRIMARY)) cmd_check_updates();
        ui_text("Se cierra el programa y se abre el instalador.", bb.x + 354, bb.y + 11, 14, COL_MUTED);
    } else if (us == UPD_DOWNLOADING) {
        char t[120];
        snprintf(t, sizeof(t), "Bajando la versión %s...", update_version());
        ui_text_bold(t, bb.x + 16, bb.y + 11, 14, COL_ACCENT);
    }
    float x = bb.x + bb.width - 16 - 300;
    if (us != UPD_AVAILABLE && us != UPD_FAILED && us != UPD_READY && us != UPD_DOWNLOADING) {
        const char *hint = "Rueda: mover  ·  Ctrl + rueda: zoom  ·  Botón derecho o Espacio: arrastrar la vista  ·  Doble clic: escribir";
        if (ui_text_width(hint, 14) > x - bb.x - 32) hint = "Rueda: mover  ·  Ctrl + rueda: zoom  ·  Doble clic: escribir";
        ui_text_fit(hint, bb.x + 16, bb.y + 11, x - bb.x - 32, 14, COL_MUTED);
    }
    if (x < bb.x + 200) return;
    if (ui_button((Rectangle){ x, bb.y + 5, 30, 30 }, "-", BTN_GHOST)) zoom_at(A.cam.offset, 0.8f);
    float z = logf(A.cam.zoom);
    int r = ui_slider(10, (Rectangle){ x + 40, bb.y + 14, 120, 12 }, &z, logf(0.05f), logf(8));
    if (r) {
        float factor = expf(z) / A.cam.zoom;
        zoom_at(A.cam.offset, factor);
    }
    if (ui_button((Rectangle){ x + 170, bb.y + 5, 30, 30 }, "+", BTN_GHOST)) zoom_at(A.cam.offset, 1.25f);
    char zs[16];
    snprintf(zs, sizeof(zs), "%.0f%%", A.cam.zoom * 100);
    ui_text(zs, x + 206, bb.y + 11, 15, COL_TEXT);
    if (ui_button((Rectangle){ x + 252, bb.y + 5, 64, 30 }, "Ajustar", BTN_GHOST)) fit_view();
}

static void draw_toast(void)
{
    if (GetTime() > A.toastUntil) return;
    float tw = ui_text_width(A.toast, 17);
    Rectangle r = { (ui_sw() - tw) / 2 - 22, ui_sh() - BOTTOM_H - 64.0f, tw + 44, 42 };
    DrawRectangleRounded(r, 0.5f, 8, (Color){ 24, 25, 38, 235 });
    ui_text(A.toast, r.x + 22, r.y + 11, 17, WHITE);
}

// ------------------------------------------------------------------ Archivos arrastrados

static void handle_drops(void)
{
    if (!IsFileDropped()) return;
    FilePathList files = LoadDroppedFiles();
    Vector2 at = GetScreenToWorld2D(GetMousePosition(), A.cam);
    if (at.x < 0 || at.y < 0 || at.x > A.doc.w || at.y > A.doc.h) at = doc_center();
    for (unsigned i = 0; i < files.count; i++) {
        const char *p = files.paths[i];
        if (IsFileExtension(p, ".lienzo")) {
            if (confirm_discard()) open_path(p);
            break;
        }
        if (IsFileExtension(p, ".pdf;.PDF")) {
            if (confirm_discard() && !import_pdf(p)) plat_message(APP_NAME, "No se ha podido leer ese PDF.");
            break;
        }
        if (IsFileExtension(p, ".ttf;.otf;.TTF;.OTF")) {
            int fi = font_install_file(p);
            if (fi >= 0) { char m[200]; snprintf(m, sizeof(m), "Fuente añadida: %s", font_name(fi)); toast(m); }
            continue;
        }
        if (IsFileExtension(p, ".svg;.SVG") && A.modal == MODAL_NONE) { ask_svg(p, at); continue; }
        if (is_vector_file(p) && A.modal == MODAL_NONE) { import_vector(p, at); continue; }
        int id = asset_load_file(p);
        if (id >= 0) {
            add_image_asset(id, at);
            at.x += 40; at.y += 40;
        } else {
            toast("Ese archivo no es una imagen que se pueda usar");
        }
    }
    UnloadDroppedFiles(files);
}

// ------------------------------------------------------------------ Pruebas automáticas
// Solo se compilan con -DLIENZO_TESTS=ON (sirven para hacer capturas al desarrollar)
#if defined(LIENZO_TESTS)

static void test_shot(const char *dir, const char *name)
{
    Image img = LoadImageFromScreen();
    char p[1200];
    snprintf(p, sizeof(p), "%s/%s.png", dir, name);
    ExportImage(img, p);
    UnloadImage(img);
}

static void test_step(const char *dir, int frame)
{
    char p[1200];
    switch (frame) {
    case 20: test_shot(dir, "01_inicio"); break;
    case 22: if (g_templateCount > 0) open_template(0); break;
    case 30:
        for (int i = 0; i < A.doc.count; i++) if (A.doc.el[i].type == EL_TEXT) { A.sel = i; break; }
        break;
    case 40: test_shot(dir, "02_plantilla_texto"); break;
    case 41: A.panel = PANEL_LAYERS; break;
    case 42: A.sel = 3; break;
    case 50: test_shot(dir, "03_capas_estrella"); break;
    case 51: {
        Rectangle a = { 500, 120, 30, 28 };
        open_color(COLOR_FILL, a);
        break;
    }
    case 60: test_shot(dir, "04_color"); A.popup = POP_NONE; break;
    case 61:
        snprintf(p, sizeof(p), "%s/export.png", dir); export_png(&A.doc, p);
        snprintf(p, sizeof(p), "%s/export.pdf", dir); export_pdf(&A.doc, p);
        snprintf(p, sizeof(p), "%s/guardado.lienzo", dir); save_to(p);
        break;
    case 62: A.panel = PANEL_TEXT; A.sel = -1; add_text("Hola ñandú: ¡qué guay! €", 6, 0.08f); A.editing = true; A.caret = 5; break;
    case 70: test_shot(dir, "05_texto_nuevo"); break;
    case 71: cmd_undo(); cmd_undo(); break;
    case 72: {
        A.panel = PANEL_IMAGES;
        const char *jpg = getenv("LIENZO_TEST_JPG");
        if (jpg) add_image_asset(asset_load_file(jpg), doc_center());
        add_shape(EL_HEART);
        A.doc.el[A.sel].rotation = 20;
        break;
    }
    case 80: test_shot(dir, "06_elementos"); break;
    case 81: A.modal = MODAL_NEW; break;
    case 90: test_shot(dir, "07_nuevo"); A.modal = MODAL_NONE; break;
    case 91: snprintf(p, sizeof(p), "%s/guardado.lienzo", dir); open_path(p); A.panel = PANEL_DESIGN; break;
    case 100: test_shot(dir, "08_reabierto_diseno"); break;
    case 101: new_document(1080, 1080); A.panel = PANEL_TEXT; A.panelScroll = 380; break;
    case 102: {
        const char *names[] = { "Lobster", "Bungee", "Pacifico", "Bangers (cómic)", "Playfair" };
        int fx[] = { FX_RELIEF, FX_OUTLINE, FX_SHADOW, FX_NEON, FX_EMBOSS };
        Color cols[] = { { 255, 196, 40, 255 }, { 240, 70, 110, 255 }, { 31, 181, 166, 255 }, { 255, 255, 255, 255 }, { 200, 190, 230, 255 } };
        A.doc.bg = (Color){ 40, 36, 70, 255 };
        for (int i = 0; i < 5; i++) {
            add_text(i == 3 ? "¡Neón!" : i == 0 ? "Relieve 3D" : "Lienzo Casero", font_find(names[i]) >= 0 ? font_find(names[i]) : 0, 0.11f);
            Element *e = selected();
            e->fill = cols[i];
            e->effect = fx[i];
            e->effectColor = effect_default_color(e, fx[i]);
            e->effectAmount = 0.7f;
            e->w = 1000; e->x = 40;
            fix_text_height(e);
            e->y = 40 + i * 205;
        }
        A.sel = 0;
        break;
    }
    case 130: test_shot(dir, "09_fuentes_efectos"); break;
    case 131: { Rectangle a = { 900, 66, 86, 36 }; open_popup(POP_EFFECT, a); break; }
    case 140: test_shot(dir, "10_menu_efectos"); A.popup = POP_NONE; break;
    case 141: { Rectangle a = { 560, 66, 170, 36 }; A.fontScroll = 300; open_popup(POP_FONT, a); break; }
    case 170: test_shot(dir, "11_menu_fuentes"); A.popup = POP_NONE; break;
    case 171: {
        snprintf(p, sizeof(p), "%s/efectos.png", dir); export_png(&A.doc, p);
        const char *tf = getenv("LIENZO_TEST_FONT");
        if (tf) { int fi = font_install_file(tf); if (fi >= 0) { A.doc.el[0].font = fi; } }
        snprintf(p, sizeof(p), "%s/con_fuente.lienzo", dir); save_to(p);
        break;
    }
    case 172: new_document(1080, 1080); A.panel = PANEL_CLIPART; A.panelOpen = true; A.panelScroll = 0; break;
    case 190: clipart_init(); add_clip(0); add_clip(4); A.doc.el[1].x += 250; A.doc.el[1].rotation = 15; break;
    case 192: {
        const char *svg = getenv("LIENZO_TEST_SVG");
        if (svg) { int id = asset_load_file(svg); if (id >= 0) add_image_asset(id, (Vector2){ 300, 800 }); }
        break;
    }
    case 200: test_shot(dir, "12_dibujos"); break;
    case 202: {
        const char *svg = getenv("LIENZO_TEST_SVG2");
        if (!svg) { A.quit = true; break; }
        new_document(1754, 1240);
        if (is_vector_file(svg)) import_vector(svg, doc_center());
        else ask_svg(svg, doc_center());
        break;
    }
    case 205: test_shot(dir, "13_svg_pregunta"); A.modal = MODAL_NONE; break;
    case 206: { int id = asset_load_file(getenv("LIENZO_TEST_SVG2")); add_image_asset(id, doc_center()); break; }
    case 210: test_shot(dir, "14_svg_entero"); break;
    case 211: new_document(1754, 1240); import_svg_parts(getenv("LIENZO_TEST_SVG2"), 1, doc_center()); A.panel = PANEL_LAYERS; A.panelOpen = true; A.panelScroll = 0; break;
    case 212: {
        // Mover una parte y cambiar el color de otra
        for (int i = 0; i < A.doc.count; i++) if (A.doc.el[i].tint) { A.doc.el[i].fill = (Color){ 40, 170, 90, 255 }; break; }
        A.doc.el[A.doc.count - 1].x += 200;
        A.doc.el[A.doc.count - 1].y += 80;
        select_index(A.doc.count - 1);
        break;
    }
    case 216: test_shot(dir, "15_svg_partes"); { char p[1200]; snprintf(p, sizeof(p), "%s/partes.lienzo", dir); save_to(p); } break;
    case 217: { char p[1200]; snprintf(p, sizeof(p), "%s/partes.lienzo", dir); open_path(p); select_index(-1); break; }
    case 220: test_shot(dir, "16_svg_reabierto"); break;
    case 222: {
        const char *png = getenv("LIENZO_TEST_POSTER");
        if (!png) { A.quit = true; break; }
        Image im = LoadImage(png);
        new_document(im.width, im.height);
        UnloadImage(im);
        add_image_asset(asset_load_file(png), doc_center());
        Element *e = selected();
        e->x = 0; e->y = 0; e->w = (float)A.doc.w; e->h = (float)A.doc.h;
        break;
    }
    case 225: test_shot(dir, "17_lapiz"); break;
    case 226: {
        // Leer y rehacer el texto gris (3 renglones) y el título marrón
        if (getenv("LIENZO_TEST_OCRALL")) {
            select_index(0);
            ocr_scan(0);
            int big = -1;
            for (int b = 0; b < g_ocrNB; b++) {
                printf("BLOQUE %d (%.0f,%.0f %.0fx%.0f) %d renglones: ", b, g_ocrBlocks[b].x, g_ocrBlocks[b].y, g_ocrBlocks[b].w, g_ocrBlocks[b].h, g_ocrBlocks[b].n);
                for (int k = 0; k < g_ocrBlocks[b].n; k++) printf("[%s] ", g_ocrLines[g_ocrBlocks[b].idx[k]].text);
                printf("\n");
                if (big < 0 || g_ocrBlocks[b].h / g_ocrBlocks[b].n > g_ocrBlocks[big].h / g_ocrBlocks[big].n) big = b;
            }
            const char *use = getenv("LIENZO_TEST_OCRUSE");
            if (use) {
                for (const char *q = use; *q;) { int bi = atoi(q); ocr_use_block(bi); printf("TEXTO esp=%.3f fuente=%s tam=%.0f \"%s\"\n", A.doc.el[A.sel].letterSpacing, font_name(A.doc.el[A.sel].font), A.doc.el[A.sel].fontSize, A.doc.el[A.sel].text); while (*q && *q != ',') q++; if (*q == ',') q++; }
            } else if (big >= 0) ocr_use_block(big);
            break;
        }
        const char *clicks = getenv("LIENZO_TEST_CLICKS");
        if (clicks) {
            const char *q = clicks;
            float a, b;
            int used;
            while (sscanf(q, "%f,%f%n", &a, &b, &used) == 2) {
                select_index(0);
                wand_at(0, (Vector2){ a, b });
                q += used;
                if (*q == ';') q++;
            }
            for (int i = 0; i < A.doc.count; i++) printf("ELEMENTO %d tipo %d %.0f,%.0f %.0fx%.0f\n", i, A.doc.el[i].type, A.doc.el[i].x, A.doc.el[i].y, A.doc.el[i].w, A.doc.el[i].h);
            break;
        }
        const char *boxes = getenv("LIENZO_TEST_BOXES");
        if (boxes) {
            // "x0,y0,x1,y1;x0,y0,x1,y1..." para probar otras imágenes
            const char *q = boxes;
            float a, b, c, d;
            int used;
            while (sscanf(q, "%f,%f,%f,%f%n", &a, &b, &c, &d, &used) == 4) {
                A.boxStart = (Vector2){ a, b }; A.boxEnd = (Vector2){ c, d };
                select_index(0);
                edit_text_box(0);
                q += used;
                if (*q == ';') q++;
            }
        } else {
            A.boxStart = (Vector2){ 225, 362 }; A.boxEnd = (Vector2){ 1065, 535 };
            edit_text_box(0);
            A.boxStart = (Vector2){ 195, 190 }; A.boxEnd = (Vector2){ 1095, 310 };
            select_index(0);
            edit_text_box(0);
        }
        for (int i = 0; i < A.doc.count; i++) if (A.doc.el[i].type == EL_TEXT) printf("TEXTO esp=%.3f fuente=%s tam=%.0f interl=%.2f al=%d color=%02X%02X%02X \"%s\"\n", A.doc.el[i].letterSpacing, font_name(A.doc.el[i].font), A.doc.el[i].fontSize, A.doc.el[i].lineSpacing, A.doc.el[i].align, A.doc.el[i].fill.r, A.doc.el[i].fill.g, A.doc.el[i].fill.b, A.doc.el[i].text);
        stop_editing();
        select_index(0);
        // Separar la espiral de la izquierda y el teléfono
        select_index(0);
        A.boxStart = (Vector2){ 30, 555 }; A.boxEnd = (Vector2){ 108, 635 };
        wand_box(0, false);
        A.boxStart = (Vector2){ 680, 1505 }; A.boxEnd = (Vector2){ 990, 1585 };
        wand_box(0, false);
        break;
    }
    case 227: {
        // Mover lo separado
        for (int i = 0; i < A.doc.count; i++) if (A.doc.el[i].type == EL_IMAGE && A.doc.el[i].w < 120) { A.doc.el[i].x += 520; A.doc.el[i].y += 900; }
        for (int i = 0; i < A.doc.count; i++) if (A.doc.el[i].type == EL_IMAGE && A.doc.el[i].w > 200 && A.doc.el[i].w < 400) { A.doc.el[i].x -= 560; A.doc.el[i].y -= 60; }
        select_index(-1);
        break;
    }
    case 230: test_shot(dir, "18_lapiz_separado"); if (getenv("LIENZO_TEST_HIDETEXT")) for (int i = 0; i < A.doc.count; i++) if (A.doc.el[i].type == EL_TEXT) A.doc.el[i].visible = false; { char p[1200]; snprintf(p, sizeof(p), "%s/texto_editado.png", dir); export_png(&A.doc, p); } break;
    case 232: {
        const char *pdf = getenv("LIENZO_TEST_PDF");
        if (!pdf) { A.quit = true; break; }
        A.dirty = false;
        if (!import_pdf(pdf)) printf("PDF: no se ha podido importar\n");
        break;
    }
    case 236: test_shot(dir, "19_pdf"); { char p[1200]; snprintf(p, sizeof(p), "%s/pdf.png", dir); export_png(&A.doc, p); } break;
    case 237: A.panelOpen = false; select_index(-1); fit_view(); break;
    case 240: test_shot(dir, "20_pdf_lienzo"); break;
    case 241: A.quit = true; break;
    }
}

#endif

// ------------------------------------------------------------------ Programa principal

int main(int argc, char **argv)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(1440, 880, APP_NAME);
    SetWindowMinSize(1100, 660);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
#if !defined(LIENZO_NO_MAXIMIZE)
    MaximizeWindow();
#endif

    if (getenv("LIENZO_VENTANA")) {   // para pruebas: tamaño de ventana "1366x768"
        int vw = 0, vh = 0;
        if (sscanf(getenv("LIENZO_VENTANA"), "%dx%d", &vw, &vh) == 2) { SetWindowMinSize(800, 500); SetWindowSize(vw, vh); }
    }
    fonts_load();
    load_settings();
    if (getenv("LIENZO_TEST")) g_uiSize = 0;   // las pruebas usan posiciones fijas
    if (getenv("LIENZO_TAMANO")) g_uiSize = atoi(getenv("LIENZO_TAMANO")) % 3;
    apply_ui_size();
    templates_install_defaults();
    templates_refresh();

    A.sel = -1;
    A.lastClickSel = -1;
    A.panel = PANEL_TEMPLATES;
    A.panelOpen = true;
    A.wandTol = 0.35f;
    strcpy(A.customW, "1080");
    strcpy(A.customH, "1080");
    new_document(1080, 1080);
    if (argc > 1 && plat_file_exists(argv[1])) {
        open_path(argv[1]);
        if (getenv("LIENZO_SEL")) select_index(atoi(getenv("LIENZO_SEL")));   // para pruebas
    }
    else toast("¡Hola! Elige una plantilla o empieza con el lienzo en blanco");

#if defined(LIENZO_TESTS)
    // Modo de prueba: LIENZO_TEST=carpeta hace capturas y exporta
    const char *testDir = getenv("LIENZO_TEST");
    int frame = 0;
#endif

    // Mirar en internet si hay una versión nueva (en segundo plano)
    if (!getenv("LIENZO_TEST") && !getenv("LIENZO_SIN_ACTUALIZAR")) update_check(APP_VERSION);

    bool firstFrame = true;
    while (!A.quit) {
        update_tick();
        if (IsWindowResized()) apply_ui_size();
#if defined(LIENZO_TESTS)
        if (testDir) test_step(testDir, frame++);
#endif
        if (WindowShouldClose()) {
            if (confirm_discard()) break;
            glfwSetWindowShouldClose(GetWindowHandle(), 0);
        }
        // Si cambia el tamaño de la zona del lienzo, mantener el dibujo centrado
        Rectangle cr = canvas_rect();
        if (firstFrame) { fit_view(); firstFrame = false; }
        if (cr.width != A.lastCanvas.width || cr.height != A.lastCanvas.height || cr.x != A.lastCanvas.x) {
            A.cam.offset.x += (cr.x + cr.width / 2) - (A.lastCanvas.x + A.lastCanvas.width / 2);
            A.cam.offset.y += (cr.y + cr.height / 2) - (A.lastCanvas.y + A.lastCanvas.height / 2);
            A.lastCanvas = cr;
        }

        ui_begin_frame();
        fonts_new_frame();
        // Ventanas emergentes: bloquean lo que hay debajo
        if (A.modal != MODAL_NONE) ui_set_blocker((Rectangle){ 0 }, true);
        else if (A.closingPopup) ui_set_blocker((Rectangle){ 0 }, true);
        else if (A.popup != POP_NONE) ui_set_blocker(A.popupRect, false);
        else ui_clear_blocker();

        if (A.popup != POP_NONE && A.modal == MODAL_NONE && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 m = GetMousePosition();
            if (!CheckCollisionPointRec(m, A.popupRect) && !CheckCollisionPointRec(m, A.popupAnchor)) {
                A.popup = POP_NONE;
                A.closingPopup = true;
                ui_set_blocker((Rectangle){ 0 }, true);
            }
        }
        if (A.closingPopup && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) A.closingPopup = false;

        handle_drops();
        // Leer los textos de la imagen cuando ya se ha dibujado el aviso de "Leyendo..."
        if (A.ocrPending > 0 && --A.ocrPending == 0) ocr_scan(A.sel);
        shortcuts();
        canvas_input(cr);

        BeginDrawing();
        ui_apply_scale();
        ClearBackground(COL_BG);
        draw_canvas(cr);
        draw_toolbar();
        draw_bottombar();
        draw_panel();
        draw_rail();
        draw_topbar();
        if (A.modal == MODAL_NONE && !A.closingPopup) {
            if (A.popup != POP_NONE) ui_clear_blocker();
            draw_popup();
        }
        draw_modal();
        draw_toast();
        EndDrawing();

        update_title();
    }

    fonts_unload();
    CloseWindow();
    return 0;
}
