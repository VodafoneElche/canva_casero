#ifndef SVG_H
#define SVG_H

#include "raylib.h"
#include <stdbool.h>

// Convierte un SVG en imagen. El lado más largo mide maxSide píxeles.
// Devuelve una imagen vacía (data == NULL) si el archivo no se puede leer.
Image svg_to_image(const unsigned char *data, int size, int maxSide);

// Una parte de un SVG separado en capas. Posición y tamaño en unidades del SVG.
typedef struct {
    Image img;          // recortada a su contenido
    float x, y, w, h;
    bool single;        // de un solo color: la imagen va en blanco y se pinta con "color"
    Color color;
    char name[40];
} SvgPart;

#define SVG_MAX_PARTS 80

// Separa el SVG en partes. mode 0: por bloques (grupos); mode 1: bloques y, dentro, colores.
// Si no hay bloques, separa por colores. how: 1 = por bloques, 2 = por colores, 0 = nada.
int svg_split(const unsigned char *data, int size, int maxSide, int mode, SvgPart *parts, int maxParts, float *svgW, float *svgH, int *how);
void svg_free_parts(SvgPart *parts, int count);

// Como svg_to_image, pero respetando los recortes rectangulares (clip-path) de Illustrator
Image svg_to_image_clipped(const unsigned char *data, int size, int maxSide);

#endif
