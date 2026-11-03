#ifndef EPS_H
#define EPS_H

#include <stdbool.h>

// Convierte un dibujo EPS (o un .ai antiguo, que es PostScript) en un SVG con los mismos
// trazados y colores, para abrirlo como cualquier otro SVG (entero o por partes).
// Entiende lo que guardan Illustrator, Inkscape, CorelDRAW... para dibujos vectoriales:
// trazados, rellenos, contornos, colores CMYK/RGB/gris, grupos y recortes.
// Devuelve el SVG (hay que liberarlo con free) o NULL. skipped: cosas que no sabe dibujar
// (degradados, fotos dentro del EPS...).
char *eps_to_svg(const unsigned char *data, int size, int *skipped);

#endif
