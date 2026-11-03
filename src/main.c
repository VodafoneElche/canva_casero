// Lienzo Casero: un programa de diseño sencillo para usar en casa.
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "doc.h"
#include "platform.h"
#include "render.h"
#include "svg.h"
#include "templates.h"
#include "ui.h"

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
typedef enum { MODAL_NONE, MODAL_NEW, MODAL_SAVE_TEMPLATE } ModalId;
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
    A.toastUntil = GetTime() + 2.8;
}

static void begin_change(void)
{
    history_push(&A.doc);
    A.dirty = true;
}

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

static Rectangle canvas_rect(void)
{
    float left = RAIL_W + (A.panelOpen ? PANEL_W : 0);
    float top = TOPBAR_H + TOOLBAR_H;
    return (Rectangle){ left, top, GetScreenWidth() - left, GetScreenHeight() - top - BOTTOM_H };
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
    A.editing = false;
    A.editPushed = false;
}

static void select_index(int i)
{
    if (i != A.sel) stop_editing();
    A.sel = i;
}

static void after_history_change(void)
{
    stop_editing();
    if (A.sel >= A.doc.count) A.sel = -1;
    A.dirty = true;
}

static void new_document(int w, int h)
{
    history_clear();
    doc_init(&A.doc, w, h, WHITE);
    A.sel = -1;
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

static void cmd_upload_image(void)
{
    const char *p = plat_open_dialog("Elegir una imagen", FILE_KIND_IMAGE);
    if (!p) return;
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
    Element *e = selected();
    if (!e) return;
    begin_change();
    doc_remove(&A.doc, A.sel);
    A.sel = -1;
    stop_editing();
}

static void cmd_duplicate(void)
{
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

static int pick(Vector2 w)
{
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

static void canvas_input(Rectangle cr)
{
    Vector2 m = GetMousePosition();
    bool over = CheckCollisionPointRec(m, cr) && ui_mouse_free() && A.modal == MODAL_NONE && A.popup == POP_NONE && !A.closingPopup;
    Vector2 w = GetScreenToWorld2D(m, A.cam);
    bool space = IsKeyDown(KEY_SPACE) && !A.editing && !ui_textbox_focused();

    if (over) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) zoom_at(m, wheel > 0 ? 1.12f : 1 / 1.12f);
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

    Element *e = selected();
    if (IsKeyPressed(KEY_ESCAPE)) { A.popup = POP_NONE; select_index(-1); }
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
    BeginScissorMode((int)cr.x, (int)cr.y, (int)cr.width, (int)cr.height);

    // Sombra de la hoja
    Vector2 p0 = GetWorldToScreen2D((Vector2){ 0, 0 }, A.cam);
    Vector2 p1 = GetWorldToScreen2D((Vector2){ (float)A.doc.w, (float)A.doc.h }, A.cam);
    Rectangle page = { p0.x, p0.y, p1.x - p0.x, p1.y - p0.y };
    for (int i = 1; i <= 8; i++)
        DrawRectangleRec((Rectangle){ page.x - i + 2, page.y - i + 4, page.width + 2 * i - 4, page.height + 2 * i - 4 }, (Color){ 30, 30, 60, 6 });
    if (A.doc.bg.a < 255) draw_checker(page, 16);

    BeginMode2D(A.cam);
    render_document(&A.doc, true);
    EndMode2D();

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
    if (x + w > GetScreenWidth() - 8) x = GetScreenWidth() - 8 - w;
    if (y + h > GetScreenHeight() - 8) y = anchor.y - h - 6;
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

static void draw_popup(void)
{
    if (A.popup == POP_NONE) return;
    ui_set_layer(1);
    Rectangle r = { 0 };
    if (A.popup == POP_FILE) {
        r = popup_place(A.popupAnchor, 300, 6 * 42 + 16);
        popup_card(r);
        float y = r.y + 8;
        PopupId keep = POP_NONE;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Nuevo diseño...", "Ctrl+N")) A.modal = MODAL_NEW;
        y += 42;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Abrir...", "Ctrl+O")) { A.popup = POP_NONE; cmd_open(); }
        y += 42;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Guardar", "Ctrl+S")) { A.popup = POP_NONE; cmd_save(false); }
        y += 42;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Guardar como...", "Ctrl+Mayús+S")) { A.popup = POP_NONE; cmd_save(true); }
        y += 42;
        if (menu_item((Rectangle){ r.x + 8, y, r.width - 16, 40 }, "Guardar como plantilla...", NULL)) { A.modal = MODAL_SAVE_TEMPLATE; A.templateName[0] = 0; ui_focus_textbox(100); }
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
        float maxH = GetScreenHeight() - (A.popupAnchor.y + A.popupAnchor.height) - 24;
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
        BeginScissorMode((int)r.x, (int)r.y + 6, (int)r.width, (int)r.height - 12);
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
    int sw = GetScreenWidth(), sh = GetScreenHeight();
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
    }
    ui_set_layer(0);
}

// ------------------------------------------------------------------ Barra superior

static void draw_topbar(void)
{
    int sw = GetScreenWidth();
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
    int sh = GetScreenHeight();
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
    if (ui_button((Rectangle){ x, y, w, 42 }, "Abrir un dibujo SVG del ordenador", BTN_NORMAL)) cmd_upload_image();
    y += 48;
    ui_text("Por ejemplo, los que descargues de", x, y, 14, COL_MUTED);
    ui_text("vector4free.com para uso en casa.", x, y + 18, 14, COL_MUTED);
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
            if (cell.y > GetScreenHeight() || cell.y + cell.height < TOPBAR_H) continue;
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
        if (b.y > GetScreenHeight() || b.y + b.height < TOPBAR_H) continue; // fuera de la vista
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
    for (int i = 0; i < g_assetCount; i++) {
        Rectangle cell = { x + (i % 2) * (cw + 10), y + (i / 2) * (cw * 0.8f + 10), cw, cw * 0.8f };
        bool hover = ui_hover(cell);
        DrawRectangleRounded(cell, 0.08f, 6, COL_HOVER);
        Texture2D t = g_assets[i].tex;
        float s = fminf((cell.width - 8) / t.width, (cell.height - 8) / t.height);
        Rectangle dst = { cell.x + (cell.width - t.width * s) / 2, cell.y + (cell.height - t.height * s) / 2, t.width * s, t.height * s };
        DrawTexturePro(t, (Rectangle){ 0, 0, (float)t.width, (float)t.height }, dst, (Vector2){ 0 }, 0, WHITE);
        if (hover) DrawRectangleRoundedLinesEx(cell, 0.08f, 6, 2, COL_ACCENT);
        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) add_image_asset(i, doc_center());
    }
    return y + ((g_assetCount + 1) / 2) * (cw * 0.8f + 10);
}

static void element_label(const Element *e, char *out, int max)
{
    if (e->type == EL_TEXT) {
        char first[64];
        int n = 0;
        for (const char *p = e->text; *p && *p != '\n' && n < 40; p++) first[n++] = *p;
        first[n] = 0;
        snprintf(out, max, "\"%s\"", n ? first : "(vacío)");
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
        Color sc = e->type == EL_IMAGE ? (Color){ 200, 200, 210, 255 } : e->fill;
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
    int sh = GetScreenHeight();
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

    BeginScissorMode((int)content.x, (int)content.y, (int)content.width, (int)content.height);
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
    DrawLine((int)x, (int)y + 10, (int)x, (int)y + TOOLBAR_H - 10, COL_BORDER);
    return x + 14;
}

static void draw_toolbar(void)
{
    Rectangle cr = canvas_rect();
    Rectangle tb = { cr.x, TOPBAR_H, cr.width, TOOLBAR_H };
    DrawRectangleRec(tb, COL_PANEL);
    DrawLine((int)tb.x, (int)(tb.y + tb.height - 1), (int)(tb.x + tb.width), (int)(tb.y + tb.height - 1), COL_BORDER);
    float x = tb.x + 16, y = tb.y;
    Element *e = selected();

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
        if (ui_button((Rectangle){ x, y + 10, 150, 36 }, "Cambiar tamaño", BTN_NORMAL)) { A.panel = PANEL_DESIGN; A.panelOpen = true; A.panelScroll = 0; }
        x += 164;
        ui_text("Elige algo en el panel de la izquierda para añadirlo.", x, y + 19, 15, COL_MUTED);
        return;
    }

    bool shape = e->type == EL_RECT || e->type == EL_ELLIPSE || e->type == EL_TRIANGLE || e->type == EL_STAR || e->type == EL_HEART;
    if (e->type != EL_IMAGE) {
        tb_label(e->type == EL_TEXT ? "Color" : e->type == EL_LINE || e->type == EL_ARROW ? "Color" : "Relleno", x, y);
        Rectangle sw = { x, y + 20, 30, 28 };
        if (ui_swatch(sw, e->fill, A.popup == POP_COLOR && A.colorTarget == COLOR_FILL)) open_color(COLOR_FILL, sw);
        x += 56;
    }
    if (shape || e->type == EL_IMAGE) {
        tb_label("Borde", x, y);
        Rectangle sw = { x, y + 20, 30, 28 };
        Color bc = e->strokeW > 0 ? e->stroke : BLANK;
        if (ui_swatch(sw, bc, A.popup == POP_COLOR && A.colorTarget == COLOR_STROKE)) open_color(COLOR_STROKE, sw);
        x += 52;
        x = tb_slider(1, "Grosor del borde", x, y, 110, &e->strokeW, 0, fmaxf(4, fminf(e->w, e->h) * 0.25f));
    }
    if (e->type == EL_LINE || e->type == EL_ARROW) {
        x = tb_slider(2, "Grosor", x, y, 110, &e->strokeW, 1, fmaxf(10, e->w * 0.15f));
        if (e->h < e->strokeW * 3) e->h = e->strokeW * 3;
    }
    if (e->type == EL_RECT) x = tb_slider(3, "Esquinas", x, y, 100, &e->roundness, 0, 1);

    if (e->type == EL_TEXT) {
        x = tb_divider(x, y);
        Rectangle fb = { x, y + 10, 170, 36 };
        bool h = ui_hover(fb);
        DrawRectangleRounded(fb, 0.25f, 6, h ? COL_HOVER : WHITE);
        DrawRectangleRoundedLinesEx(fb, 0.25f, 6, 1, COL_BORDER);
        BeginScissorMode((int)fb.x, (int)fb.y, (int)fb.width - 26, (int)fb.height);
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
    }
    x = tb_divider(x, y);
    x = tb_slider(4, "Transparencia", x, y, 110, &e->opacity, 0.05f, 1);

    // Botones de la derecha
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
    ui_text("Rueda: zoom  ·  Botón derecho o Espacio: mover la vista  ·  Doble clic: escribir", bb.x + 16, bb.y + 11, 14, COL_MUTED);
    float x = bb.x + bb.width - 16 - 300;
    if (x < bb.x + 560) return;
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
    Rectangle r = { (GetScreenWidth() - tw) / 2 - 22, GetScreenHeight() - BOTTOM_H - 64.0f, tw + 44, 42 };
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
        if (IsFileExtension(p, ".ttf;.otf;.TTF;.OTF")) {
            int fi = font_install_file(p);
            if (fi >= 0) { char m[200]; snprintf(m, sizeof(m), "Fuente añadida: %s", font_name(fi)); toast(m); }
            continue;
        }
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
    case 201: A.quit = true; break;
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

    fonts_load();
    templates_install_defaults();
    templates_refresh();

    A.sel = -1;
    A.lastClickSel = -1;
    A.panel = PANEL_TEMPLATES;
    A.panelOpen = true;
    strcpy(A.customW, "1080");
    strcpy(A.customH, "1080");
    new_document(1080, 1080);
    if (argc > 1 && plat_file_exists(argv[1])) open_path(argv[1]);
    else toast("¡Hola! Elige una plantilla o empieza con el lienzo en blanco");

#if defined(LIENZO_TESTS)
    // Modo de prueba: LIENZO_TEST=carpeta hace capturas y exporta
    const char *testDir = getenv("LIENZO_TEST");
    int frame = 0;
#endif

    bool firstFrame = true;
    while (!A.quit) {
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
        shortcuts();
        canvas_input(cr);

        BeginDrawing();
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
