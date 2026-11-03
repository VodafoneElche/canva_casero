// El documento: un lienzo con una lista de elementos. El orden de la lista
// es el orden de las capas (el 0 es el que queda más al fondo).
#ifndef DOC_H
#define DOC_H

#include "raylib.h"
#include <stdbool.h>

#define MAX_ELEMENTS 400
#define TEXT_MAX 1024
#define MAX_ASSETS 256

typedef enum {
    EL_RECT,
    EL_ELLIPSE,
    EL_TRIANGLE,
    EL_STAR,
    EL_HEART,
    EL_LINE,
    EL_ARROW,
    EL_TEXT,
    EL_IMAGE,
    EL_TYPE_COUNT
} ElementType;

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } TextAlign;

// Efectos de texto
typedef enum { FX_NONE, FX_SHADOW, FX_OUTLINE, FX_RELIEF, FX_EMBOSS, FX_NEON, FX_COUNT } TextEffect;

typedef struct {
    int type;
    float x, y, w, h;      // caja sin girar (esquina superior izquierda y tamaño)
    float rotation;        // grados, giro alrededor del centro
    Color fill;            // color de relleno (o color del texto / de la línea)
    Color stroke;          // color del borde
    float strokeW;         // grosor del borde (0 = sin borde)
    float roundness;       // esquinas redondeadas (0..1), solo rectángulos
    float opacity;         // 0..1
    int font;              // índice de fuente (textos)
    float fontSize;
    int align;
    int effect;            // TextEffect
    Color effectColor;
    float effectAmount;    // 0..1
    float lineSpacing;     // distancia entre líneas / tamaño (0 = normal)
    float letterSpacing;   // espacio de más entre letras / tamaño (0 = normal)
    int asset;             // índice de imagen (imágenes)
    bool tint;             // imagen de un solo color: se pinta con 'fill' (partes de SVG)
    bool visible;
    bool locked;
    char text[TEXT_MAX];
} Element;

typedef struct {
    int w, h;
    Color bg;
    int count;
    Element el[MAX_ELEMENTS];
} Document;

// ---- Imágenes guardadas dentro del diseño (como PNG) ----
typedef struct {
    unsigned char *png;
    int pngSize;
    Texture2D tex;
    int w, h;
    unsigned char *mask;   // transparencia reducida (para saber si se ha hecho clic encima)
    int mw, mh;
    bool hidden;           // no sale en "Imágenes usadas" (trozos del lápiz, partes de SVG/PDF...)
} Asset;

extern Asset g_assets[MAX_ASSETS];
extern int g_assetCount;

int asset_add_image(Image img);                       // devuelve índice o -1
int asset_add_png(const unsigned char *data, int size);
int asset_load_file(const char *path);                 // png/jpg/... desde disco
Texture2D asset_texture(int id);
bool asset_opaque_at(int id, float u, float v);   // u, v de 0 a 1

// ---- Documento ----
void doc_init(Document *d, int w, int h, Color bg);
Element *doc_add(Document *d, int type);   // al final (arriba del todo)
void doc_remove(Document *d, int index);
int doc_duplicate(Document *d, int index); // devuelve el nuevo índice
int doc_move_layer(Document *d, int index, int delta); // devuelve nuevo índice
Element element_default(int type);
const char *element_type_name(int type);
const char *effect_name(int fx);
Color effect_default_color(const Element *e, int fx);
Rectangle element_bounds(const Element *e); // caja exterior ya girada

// ---- Deshacer / rehacer ----
void history_push(const Document *d);  // llamar ANTES de cambiar algo
bool history_undo(Document *d);
bool history_redo(Document *d);
bool history_can_undo(void);
bool history_can_redo(void);
void history_clear(void);

// ---- Archivos .lienzo ----
bool doc_save(const Document *d, const char *path);
bool doc_load(Document *d, const char *path);

#endif
