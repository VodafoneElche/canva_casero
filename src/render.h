#ifndef RENDER_H
#define RENDER_H

#include "doc.h"

#define MAX_FONTS 200
#define FONT_BASE_SIZE 128

typedef enum { FONT_GROUP_SYSTEM, FONT_GROUP_BUNDLED, FONT_GROUP_USER } FontGroup;

typedef struct {
    char name[64];
    char path[1400];
    int group;
    bool loaded, failed;
    Font font;
} FontEntry;

extern FontEntry g_fonts[MAX_FONTS];
extern int g_fontCount;
extern Font g_uiFont;
extern Font g_uiFontBold;

void fonts_load(void);
void fonts_unload(void);
void fonts_reload_ui(int px);
Font font_get(int index);              // se carga la primera vez
const char *font_name(int index);
int font_find(const char *name);       // -1 si no está
int font_install_file(const char *path);                                 // .ttf / .otf del ordenador
int font_install_data(const char *name, const char *ext, const unsigned char *data, int size);
const char *fonts_user_dir(void);
// Para pasar tamaños de PDF/Canva (em) al tamaño que usa el programa, y dónde queda la línea base
float font_em_ratio(int index);       // alto de la letra / em
float font_ascent_ratio(int index);   // línea base desde arriba / alto de la letra
// Para las listas: carga como mucho 2 fuentes nuevas por fotograma (el resto, en los siguientes)
Font font_get_preview(int index);
void fonts_new_frame(void);

// Dibuja un elemento / todo el documento en coordenadas del documento.
void render_element(const Element *e);
void render_document(const Document *d, bool drawBackground);

// Texto: medidas y posición del cursor de escritura
float text_height(const Element *e);
Vector2 text_caret_pos(const Element *e, int byteIndex, float *lineH); // local, sin girar

// Exportar
Image render_to_image(const Document *d, float scale);
bool export_png(const Document *d, const char *path);
bool export_pdf(const Document *d, const char *path);
Image render_thumbnail(const Document *d, int maxSide);

#endif
