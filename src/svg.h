#ifndef SVG_H
#define SVG_H

#include "raylib.h"

// Convierte un SVG en imagen. El lado más largo mide maxSide píxeles.
// Devuelve una imagen vacía (data == NULL) si el archivo no se puede leer.
Image svg_to_image(const unsigned char *data, int size, int maxSide);

#endif
