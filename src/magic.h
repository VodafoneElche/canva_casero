#ifndef MAGIC_H
#define MAGIC_H

#include "raylib.h"
#include <stdbool.h>

// Lápiz mágico: a partir del punto (px, py) de una imagen, separa el dibujo que hay
// ahí de su fondo. Devuelve la parte recortada (part, con su posición bx, by dentro
// de la imagen) y la imagen original con ese hueco tapado con el color del fondo (rest).
// tolerance: 0..1 (cuánto se puede parecer un color al fondo y seguir siendo fondo).
// Devuelve 0 si ahí solo hay fondo, 1 si ha ido bien.
int magic_extract(Image src, int px, int py, float tolerance, Image *part, int *bx, int *by, Image *rest);

// Da a los píxeles transparentes del borde el color de sus vecinos (evita bordes oscuros al escalar)
void image_bleed_edges(Image *img);

#endif
