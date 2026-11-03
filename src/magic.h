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

// Igual, pero dentro del recuadro (x0,y0)-(x1,y1): sirve para fondos con degradado o textura.
// El hueco se rellena mezclando los colores del borde del recuadro. mainColor: color principal
// de lo separado (por ejemplo, el de las letras). Devuelve 0 si en el recuadro no hay nada.
int magic_extract_box(Image src, int x0, int y0, int x1, int y1, float tolerance,
                      Image *part, int *bx, int *by, Image *rest, Color *mainColor);

// Para letras: solo separa lo que es del color de las letras (no los dibujos de alrededor)
int magic_extract_text(Image src, int x0, int y0, int x1, int y1, float tolerance,
                       Image *part, int *bx, int *by, Image *rest, Color *textColor);

// Da a los píxeles transparentes del borde el color de sus vecinos (evita bordes oscuros al escalar)
void image_bleed_edges(Image *img);

#endif
