#ifndef PDF_H
#define PDF_H

#include "raylib.h"
#include <stdbool.h>

// Importar una página de un PDF (por ejemplo, un diseño descargado de Canva) como piezas
// sueltas: imágenes, dibujos (formas vectoriales) y textos que se pueden reescribir.

typedef enum { PDF_IMAGE, PDF_SHAPE, PDF_TEXT } PdfItemKind;

typedef struct {
    int kind;
    // Posición en píxeles del lienzo: caja sin girar y giro en grados alrededor del centro
    float x, y, w, h, rotation;
    float opacity;
    // PDF_IMAGE y PDF_SHAPE
    Image img;
    bool tint;           // forma de un solo color: la imagen va en blanco y se pinta con color
    Color color;         // color del texto o de la forma
    // PDF_TEXT
    char *text;          // UTF-8, líneas separadas por \n
    char font[64];       // nombre de la fuente en el PDF (sin el prefijo ABCDEF+)
    bool bold, italic, serif, script;
    float fontSize;      // en píxeles del lienzo
    float lineSpacing;   // distancia entre líneas / tamaño
    int align;           // 0 izquierda, 1 centro, 2 derecha
    float baseline;      // y de la primera línea de base
} PdfItem;

typedef struct {
    PdfItem *items;
    int count, cap;
    float pageW, pageH;  // tamaño de la página en píxeles del lienzo
    int pages;           // páginas que tiene el PDF
    int skipped;         // cosas que no se han podido leer (degradados, etc.)
} PdfResult;

// pxPerPoint: píxeles por punto de PDF (150 ppp -> 150/72). page empieza en 0.
bool pdf_import(const unsigned char *data, int size, int page, float pxPerPoint, PdfResult *out);
void pdf_free(PdfResult *r);

#endif
