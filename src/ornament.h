#ifndef ORNAMENT_H
#define ORNAMENT_H

#include "doc.h"

// Marcos, adornos y letreros. Se dibujan en coordenadas locales: el centro de la caja es (0, 0).
#define FRAME_VARIANTS 7
#define ORNAMENT_VARIANTS 8
#define BANNER_VARIANTS 5

const char *frame_variant_name(int v);
const char *ornament_variant_name(int v);
const char *banner_variant_name(int v);

void frame_draw(const Element *e, Color col);
void ornament_draw(const Element *e, Color col);
void banner_draw(const Element *e, Color fill, Color stroke, bool withText);

float frame_band(const Element *e);   // ancho del borde que se puede pulsar
float frame_corner_size(const Element *e);

#endif
