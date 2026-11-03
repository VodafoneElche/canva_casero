#include "templates.h"
#include "platform.h"
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TemplateInfo g_templates[MAX_TEMPLATES];
int g_templateCount = 0;

const char *templates_dir(void)
{
    static char dir[1100] = {0};
    if (!dir[0]) {
        snprintf(dir, sizeof(dir), "%s/plantillas", plat_data_dir());
        plat_make_dir(dir);
    }
    return dir;
}

static void clean_name(const char *in, char *out, int max)
{
    int n = 0;
    for (const char *p = in; *p && n < max - 1; p++) {
        char c = *p;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
        out[n++] = c;
    }
    out[n] = 0;
    if (n == 0) strcpy(out, "Plantilla");
}

bool templates_save(const Document *d, const char *name)
{
    char safe[200], path[1400];
    clean_name(name, safe, sizeof(safe));
    snprintf(path, sizeof(path), "%s/%s.lienzo", templates_dir(), safe);
    if (!doc_save(d, path)) return false;
    Image thumb = render_thumbnail(d, 320);
    int size = 0;
    unsigned char *png = ExportImageToMemory(thumb, ".png", &size);
    UnloadImage(thumb);
    if (png) {
        snprintf(path, sizeof(path), "%s/%s.png", templates_dir(), safe);
        plat_write_file(path, png, size);
        MemFree(png);
    }
    return true;
}

bool templates_delete(int index)
{
    if (index < 0 || index >= g_templateCount) return false;
    bool ok = plat_delete_file(g_templates[index].path);
    plat_delete_file(g_templates[index].thumbPath);
    return ok;
}

static int cmp_names(const void *a, const void *b)
{
    return strcmp(((const TemplateInfo *)a)->name, ((const TemplateInfo *)b)->name);
}

void templates_refresh(void)
{
    for (int i = 0; i < g_templateCount; i++)
        if (g_templates[i].thumb.id) UnloadTexture(g_templates[i].thumb);
    g_templateCount = 0;

    static char names[MAX_TEMPLATES][260];
    int n = plat_list_dir(templates_dir(), ".lienzo", names, MAX_TEMPLATES);
    for (int i = 0; i < n; i++) {
        TemplateInfo *t = &g_templates[g_templateCount];
        memset(t, 0, sizeof(*t));
        size_t l = strlen(names[i]) - strlen(".lienzo");
        if (l >= sizeof(t->name)) l = sizeof(t->name) - 1;
        memcpy(t->name, names[i], l);
        t->name[l] = 0;
        snprintf(t->path, sizeof(t->path), "%s/%s", templates_dir(), names[i]);
        snprintf(t->thumbPath, sizeof(t->thumbPath), "%s/%s.png", templates_dir(), t->name);
        g_templateCount++;
    }
    qsort(g_templates, g_templateCount, sizeof(TemplateInfo), cmp_names);
    for (int i = 0; i < g_templateCount; i++) {
        TemplateInfo *t = &g_templates[i];
        int size = 0;
        unsigned char *png = plat_read_file(t->thumbPath, &size);
        if (png) {
            Image img = LoadImageFromMemory(".png", png, size);
            free(png);
            if (img.data) {
                t->thumb = LoadTextureFromImage(img);
                SetTextureFilter(t->thumb, TEXTURE_FILTER_BILINEAR);
                UnloadImage(img);
            }
        }
    }
}

// ------------------------------------------------------------------ Plantillas de ejemplo

static Element *add_text(Document *d, const char *text, float x, float y, float w, float size, int font, Color c, int align)
{
    Element *e = doc_add(d, EL_TEXT);
    strncpy(e->text, text, TEXT_MAX - 1);
    e->x = x; e->y = y; e->w = w;
    e->fontSize = size;
    e->font = font;
    e->fill = c;
    e->align = align;
    e->h = text_height(e);
    return e;
}

static Element *add_shape(Document *d, int type, float x, float y, float w, float h, Color c)
{
    Element *e = doc_add(d, type);
    e->x = x; e->y = y; e->w = w; e->h = h;
    e->fill = c;
    return e;
}

static void tpl_birthday(Document *d)
{
    doc_init(d, 1080, 1080, (Color){ 255, 236, 242, 255 });
    add_shape(d, EL_ELLIPSE, -180, -160, 560, 560, (Color){ 255, 196, 214, 255 });
    add_shape(d, EL_ELLIPSE, 760, 720, 520, 520, (Color){ 197, 232, 255, 255 });
    add_shape(d, EL_STAR, 120, 140, 110, 110, (Color){ 255, 196, 40, 255 })->rotation = -12;
    add_shape(d, EL_STAR, 860, 170, 80, 80, (Color){ 125, 92, 255, 255 })->rotation = 15;
    add_shape(d, EL_STAR, 150, 820, 70, 70, (Color){ 31, 181, 166, 255 });
    add_shape(d, EL_HEART, 830, 800, 120, 110, (Color){ 240, 70, 110, 255 })->rotation = 10;
    add_text(d, "¡Feliz", 90, 300, 900, 170, 5, (Color){ 240, 70, 110, 255 }, ALIGN_CENTER);
    add_text(d, "cumpleaños!", 40, 490, 1000, 128, 5, (Color){ 109, 74, 255, 255 }, ALIGN_CENTER);
    add_text(d, "Te queremos mucho", 90, 720, 900, 60, 6, (Color){ 60, 50, 80, 255 }, ALIGN_CENTER);
}

static void tpl_shopping(Document *d)
{
    doc_init(d, 1240, 1754, (Color){ 255, 253, 245, 255 });
    add_shape(d, EL_RECT, 0, 0, 1240, 260, (Color){ 31, 181, 166, 255 });
    add_text(d, "Lista de la compra", 80, 70, 1080, 110, 1, WHITE, ALIGN_LEFT);
    const char *items[] = { "Fruta y verdura", "Carne y pescado", "Lácteos", "Panadería", "Limpieza", "Otros" };
    for (int i = 0; i < 6; i++) {
        float y = 340 + i * 230;
        Element *box = add_shape(d, EL_RECT, 80, y, 56, 56, BLANK);
        box->stroke = (Color){ 31, 181, 166, 255 };
        box->strokeW = 5;
        box->roundness = 0.3f;
        add_text(d, items[i], 170, y - 4, 900, 52, 1, (Color){ 40, 45, 60, 255 }, ALIGN_LEFT);
        Element *l = add_shape(d, EL_LINE, 170, y + 120, 990, 20, (Color){ 210, 214, 222, 255 });
        l->strokeW = 3;
        Element *l2 = add_shape(d, EL_LINE, 170, y + 180, 990, 20, (Color){ 210, 214, 222, 255 });
        l2->strokeW = 3;
    }
}

static void tpl_invitation(Document *d)
{
    doc_init(d, 1500, 1050, (Color){ 30, 32, 58, 255 });
    Element *frame = add_shape(d, EL_RECT, 50, 50, 1400, 950, BLANK);
    frame->stroke = (Color){ 232, 196, 120, 255 };
    frame->strokeW = 6;
    frame->roundness = 0.06f;
    add_text(d, "Estás invitado", 100, 170, 1300, 70, 6, (Color){ 232, 196, 120, 255 }, ALIGN_CENTER);
    add_text(d, "Comida familiar", 100, 290, 1300, 150, 2, WHITE, ALIGN_CENTER);
    add_shape(d, EL_LINE, 600, 500, 300, 20, (Color){ 232, 196, 120, 255 })->strokeW = 4;
    add_text(d, "Sábado 14 · 14:00 h\nCasa de los abuelos", 100, 570, 1300, 60, 0, (Color){ 220, 222, 240, 255 }, ALIGN_CENTER);
    add_text(d, "¡Trae tu postre favorito!", 100, 820, 1300, 44, 0, (Color){ 232, 196, 120, 255 }, ALIGN_CENTER);
}

static void tpl_christmas(Document *d)
{
    doc_init(d, 1500, 1050, (Color){ 176, 32, 48, 255 });
    for (int i = 0; i < 14; i++) {
        float x = (float)((i * 397) % 1400) + 30, y = (float)((i * 211) % 980) + 20;
        float s = 30 + (i * 13) % 40;
        Element *st = add_shape(d, EL_STAR, x, y, s, s, (Color){ 255, 255, 255, 120 });
        st->rotation = (float)(i * 17 % 40);
    }
    add_shape(d, EL_TRIANGLE, 600, 230, 300, 380, (Color){ 30, 120, 70, 255 });
    add_shape(d, EL_RECT, 715, 610, 70, 80, (Color){ 110, 70, 40, 255 });
    add_shape(d, EL_STAR, 690, 180, 120, 120, (Color){ 255, 205, 60, 255 });
    add_text(d, "¡Feliz Navidad!", 100, 730, 1300, 140, 6, WHITE, ALIGN_CENTER);
    add_text(d, "y próspero año nuevo", 100, 900, 1300, 52, 0, (Color){ 255, 225, 225, 255 }, ALIGN_CENTER);
}

static void tpl_fridge_note(Document *d)
{
    doc_init(d, 1080, 1080, (Color){ 255, 241, 150, 255 });
    Element *pin = add_shape(d, EL_ELLIPSE, 500, 40, 80, 80, (Color){ 220, 60, 80, 255 });
    pin->stroke = (Color){ 150, 30, 50, 255 };
    pin->strokeW = 4;
    add_text(d, "Recordatorio", 80, 170, 920, 100, 4, (Color){ 60, 50, 30, 255 }, ALIGN_CENTER);
    add_text(d, "• Sacar la basura\n• Regar las plantas\n• Llamar a la abuela\n• Recoger la habitación", 120, 360, 860, 64, 4, (Color){ 70, 60, 40, 255 }, ALIGN_LEFT);
    add_shape(d, EL_HEART, 860, 880, 120, 110, (Color){ 240, 70, 110, 255 });
}

static void tpl_menu(Document *d)
{
    doc_init(d, 1754, 1240, WHITE);
    add_shape(d, EL_RECT, 0, 0, 1754, 200, (Color){ 109, 74, 255, 255 });
    add_text(d, "Menú de la semana", 80, 50, 1600, 100, 1, WHITE, ALIGN_LEFT);
    const char *days[] = { "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado", "Domingo" };
    for (int i = 0; i < 7; i++) {
        float y = 250 + i * 135;
        Element *r = add_shape(d, EL_RECT, 80, y, 1594, 115, i % 2 ? (Color){ 246, 244, 255, 255 } : (Color){ 236, 232, 255, 255 });
        r->roundness = 0.25f;
        add_text(d, days[i], 120, y + 30, 360, 48, 1, (Color){ 80, 55, 200, 255 }, ALIGN_LEFT);
        add_text(d, "Comida: ...      Cena: ...", 520, y + 32, 1100, 44, 0, (Color){ 70, 70, 90, 255 }, ALIGN_LEFT);
    }
}

void templates_install_defaults(void)
{
    char marker[1200];
    snprintf(marker, sizeof(marker), "%s/.ejemplos_instalados", templates_dir());
    if (plat_file_exists(marker)) return;

    Document *d = (Document *)calloc(1, sizeof(Document));
    struct { const char *name; void (*fn)(Document *); } list[] = {
        { "Cumpleaños", tpl_birthday },
        { "Lista de la compra", tpl_shopping },
        { "Invitación", tpl_invitation },
        { "Navidad", tpl_christmas },
        { "Nota para la nevera", tpl_fridge_note },
        { "Menú semanal", tpl_menu },
    };
    for (size_t i = 0; i < sizeof(list) / sizeof(list[0]); i++) {
        list[i].fn(d);
        templates_save(d, list[i].name);
    }
    free(d);
    plat_write_file(marker, "1", 1);
}
