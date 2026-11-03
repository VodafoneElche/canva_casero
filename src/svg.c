// Leer dibujos SVG y convertirlos en imagen (con nanosvg, licencia zlib)
#include "svg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define NANOSVG_IMPLEMENTATION
#include "external/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "external/nanosvgrast.h"

Image svg_to_image(const unsigned char *data, int size, int maxSide)
{
    Image out = { 0 };
    // nanosvg modifica el texto mientras lo lee: trabajamos con una copia
    char *copy = (char *)malloc(size + 1);
    memcpy(copy, data, size);
    copy[size] = 0;
    NSVGimage *svg = nsvgParse(copy, "px", 96.0f);
    free(copy);
    if (!svg) return out;
    if (svg->width < 1 || svg->height < 1 || !svg->shapes) { nsvgDelete(svg); return out; }

    float scale = maxSide / fmaxf(svg->width, svg->height);
    int w = (int)ceilf(svg->width * scale), h = (int)ceilf(svg->height * scale);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    unsigned char *pixels = (unsigned char *)malloc((size_t)w * h * 4);
    NSVGrasterizer *rast = nsvgCreateRasterizer();
    if (!rast || !pixels) { free(pixels); nsvgDelete(svg); return out; }
    nsvgRasterize(rast, svg, 0, 0, scale, pixels, w, h, w * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(svg);

    out.data = pixels;
    out.width = w;
    out.height = h;
    out.mipmaps = 1;
    out.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    return out;
}
