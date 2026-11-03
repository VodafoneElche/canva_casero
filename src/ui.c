#include "ui.h"
#include "render.h"

#include <math.h>
#include <string.h>

static Rectangle g_blocker;
static bool g_hasBlocker = false;
static bool g_modal = false;
static int g_layer = 0;
static int g_activeSlider = -1;
static int g_focusBox = -1;
static int g_focusRequest = -1;
static Rectangle g_clip;
static bool g_hasClip = false;

void ui_set_clip(Rectangle r) { g_clip = r; g_hasClip = true; }
void ui_clear_clip(void) { g_hasClip = false; }

void ui_begin_frame(void)
{
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) g_activeSlider = -1;
    if (g_focusRequest >= 0) { g_focusBox = g_focusRequest; g_focusRequest = -1; }
}

void ui_set_blocker(Rectangle r, bool fullScreenModal)
{
    g_blocker = r;
    g_hasBlocker = true;
    g_modal = fullScreenModal;
}

void ui_clear_blocker(void) { g_hasBlocker = false; g_modal = false; }
void ui_set_layer(int layer) { g_layer = layer; }

bool ui_mouse_free(void)
{
    if (g_layer == 1) return true;
    if (g_modal) return false;
    if (g_hasBlocker && CheckCollisionPointRec(GetMousePosition(), g_blocker)) return false;
    return true;
}

bool ui_hover(Rectangle r)
{
    if (g_hasClip && !CheckCollisionPointRec(GetMousePosition(), g_clip)) return false;
    return ui_mouse_free() && CheckCollisionPointRec(GetMousePosition(), r);
}

bool ui_clicked(Rectangle r) { return ui_hover(r) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT); }
bool ui_pressed(Rectangle r) { return ui_hover(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

void ui_text(const char *t, float x, float y, float size, Color c)
{
    DrawTextEx(g_uiFont, t, (Vector2){ roundf(x), roundf(y) }, size, 0, c);
}

void ui_text_bold(const char *t, float x, float y, float size, Color c)
{
    DrawTextEx(g_uiFontBold, t, (Vector2){ roundf(x), roundf(y) }, size, 0, c);
}

float ui_text_width(const char *t, float size)
{
    return MeasureTextEx(g_uiFont, t, size, 0).x;
}

void ui_text_center(const char *t, Rectangle r, float size, Color c)
{
    Vector2 m = MeasureTextEx(g_uiFont, t, size, 0);
    ui_text(t, r.x + (r.width - m.x) / 2, r.y + (r.height - m.y) / 2, size, c);
}

// Escribe el texto y lo corta con "..." si no cabe
void ui_text_fit(const char *t, float x, float y, float maxW, float size, Color c)
{
    if (ui_text_width(t, size) <= maxW) { ui_text(t, x, y, size, c); return; }
    char buf[256];
    int n = (int)strlen(t);
    if (n > 250) n = 250;
    while (n > 0) {
        n--;
        while (n > 0 && ((unsigned char)t[n] & 0xC0) == 0x80) n--;
        memcpy(buf, t, n);
        strcpy(buf + n, "...");
        if (ui_text_width(buf, size) <= maxW) break;
    }
    ui_text(buf, x, y, size, c);
}

void ui_shadow(Rectangle r, float radius)
{
    for (int i = 1; i <= 6; i++) {
        Rectangle s = { r.x - i, r.y - i + 3, r.width + 2 * i, r.height + 2 * i };
        float rr = (radius + i) / (s.height < s.width ? s.height : s.width) * 2;
        DrawRectangleRounded(s, rr > 1 ? 1 : rr, 8, (Color){ 20, 20, 40, (unsigned char)(14 - i * 2) });
    }
}

bool ui_button_enabled(Rectangle r, const char *label, ButtonStyle style, bool enabled)
{
    bool hover = enabled && ui_hover(r);
    bool down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    Color bg, fg, border = BLANK;
    switch (style) {
    case BTN_PRIMARY:
        bg = hover ? (Color){ 92, 58, 240, 255 } : COL_ACCENT;
        fg = WHITE;
        break;
    case BTN_GHOST:
        bg = hover ? COL_HOVER : BLANK;
        fg = COL_TEXT;
        break;
    case BTN_TOP:
        bg = hover ? (Color){ 255, 255, 255, 50 } : BLANK;
        fg = WHITE;
        break;
    case BTN_DANGER:
        bg = hover ? (Color){ 255, 235, 238, 255 } : COL_PANEL;
        fg = COL_DANGER;
        border = COL_BORDER;
        break;
    default:
        bg = hover ? COL_HOVER : COL_PANEL;
        fg = COL_TEXT;
        border = COL_BORDER;
        break;
    }
    if (down) { bg.r = (unsigned char)(bg.r * 0.92f); bg.g = (unsigned char)(bg.g * 0.92f); bg.b = (unsigned char)(bg.b * 0.95f); }
    if (!enabled) fg = (Color){ fg.r, fg.g, fg.b, 90 };
    float round = 8.0f / (r.height < r.width ? r.height : r.width) * 2;
    if (round > 1) round = 1;
    DrawRectangleRounded(r, round, 8, bg);
    if (border.a) DrawRectangleRoundedLinesEx(r, round, 8, 1, border);
    ui_text_center(label, r, 17, fg);
    return enabled && hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

bool ui_button(Rectangle r, const char *label, ButtonStyle style)
{
    return ui_button_enabled(r, label, style, true);
}

bool ui_swatch(Rectangle r, Color c, bool selected)
{
    bool hover = ui_hover(r);
    if (c.a < 255) {
        // Cuadros para mostrar transparencia
        DrawRectangleRec(r, WHITE);
        float s = r.width / 4;
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                if ((x + y) % 2) DrawRectangle((int)(r.x + x * s), (int)(r.y + y * s), (int)ceilf(s), (int)ceilf(s), (Color){ 210, 210, 215, 255 });
        if (c.a == 0) DrawLineEx((Vector2){ r.x + 3, r.y + r.height - 3 }, (Vector2){ r.x + r.width - 3, r.y + 3 }, 2, COL_DANGER);
    }
    DrawRectangleRounded(r, 0.25f, 6, c);
    DrawRectangleRoundedLinesEx(r, 0.25f, 6, 1, (Color){ 0, 0, 0, 40 });
    if (selected || hover) {
        Rectangle o = { r.x - 3, r.y - 3, r.width + 6, r.height + 6 };
        DrawRectangleRoundedLinesEx(o, 0.3f, 6, 2, selected ? COL_ACCENT : COL_BORDER);
    }
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

int ui_slider(int id, Rectangle r, float *value, float min, float max)
{
    int result = 0;
    Rectangle hit = { r.x - 6, r.y - 8, r.width + 12, r.height + 16 };
    if (g_activeSlider < 0 && ui_pressed(hit)) {
        g_activeSlider = id;
        result = 1;
    }
    if (g_activeSlider == id) {
        float t = (GetMousePosition().x - r.x) / r.width;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float nv = min + t * (max - min);
        if (nv != *value) { *value = nv; if (result == 0) result = 2; }
    }
    float t = (*value - min) / (max - min);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float cy = r.y + r.height / 2;
    DrawRectangleRounded((Rectangle){ r.x, cy - 2, r.width, 4 }, 1, 4, COL_BORDER);
    DrawRectangleRounded((Rectangle){ r.x, cy - 2, r.width * t, 4 }, 1, 4, COL_ACCENT);
    bool hover = ui_hover(hit) || g_activeSlider == id;
    DrawCircleV((Vector2){ r.x + r.width * t, cy }, hover ? 8 : 7, WHITE);
    DrawCircleLinesV((Vector2){ r.x + r.width * t, cy }, hover ? 8 : 7, COL_ACCENT);
    return result;
}

bool ui_textbox_focused(void) { return g_focusBox >= 0; }
void ui_focus_textbox(int id) { g_focusRequest = id; }

bool ui_textbox(int id, Rectangle r, char *buf, int max, bool *submitted)
{
    bool changed = false;
    if (submitted) *submitted = false;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (ui_hover(r)) g_focusBox = id;
        else if (g_focusBox == id) g_focusBox = -1;
    }
    bool focused = g_focusBox == id;
    if (focused) {
        int ch;
        while ((ch = GetCharPressed()) != 0) {
            int len = (int)strlen(buf);
            int bytes = 0;
            const char *utf8 = CodepointToUTF8(ch, &bytes);
            if (len + bytes < max) {
                memcpy(buf + len, utf8, bytes);
                buf[len + bytes] = 0;
                changed = true;
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            int len = (int)strlen(buf);
            if (len > 0) {
                len--;
                while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80) len--;
                buf[len] = 0;
                changed = true;
            }
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            if (submitted) *submitted = true;
        }
    }
    DrawRectangleRounded(r, 0.25f, 6, WHITE);
    DrawRectangleRoundedLinesEx(r, 0.25f, 6, focused ? 2 : 1, focused ? COL_ACCENT : COL_BORDER);
    float ty = r.y + (r.height - 18) / 2;
    BeginScissorMode((int)r.x + 4, (int)r.y, (int)r.width - 8, (int)r.height);
    float tw = ui_text_width(buf, 18);
    float off = tw > r.width - 24 ? tw - (r.width - 24) : 0;
    ui_text(buf, r.x + 10 - off, ty, 18, COL_TEXT);
    if (focused && fmod(GetTime(), 1.0) < 0.6) DrawRectangle((int)(r.x + 11 + tw - off), (int)ty, 2, 19, COL_ACCENT);
    EndScissorMode();
    return changed;
}
