#ifndef RENDER_H
#define RENDER_H

#include "doc.h"

#define FONT_COUNT 8
#define FONT_BASE_SIZE 128

typedef struct {
    const char *name;
    Font font;
} FontEntry;

extern FontEntry g_fonts[FONT_COUNT];
extern Font g_uiFont;
extern Font g_uiFontBold;

void fonts_load(void);
void fonts_unload(void);
Font font_get(int index);

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
