// Prueba del lápiz mágico por recuadro: magictest imagen.jpg x0 y0 x1 y1 salida
#include "../src/magic.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    if (argc < 7) return 1;
    SetTraceLogLevel(LOG_WARNING);
    Image src = LoadImage(argv[1]);
    Image part, rest;
    int bx, by;
    Color c;
    int ok = magic_extract_box(src, atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), argc > 7 ? (float)atof(argv[7]) : 0.35f, &part, &bx, &by, &rest, &c);
    printf("ok=%d pieza en %d,%d de %dx%d color %02X%02X%02X\n", ok, bx, by, part.width, part.height, c.r, c.g, c.b);
    if (!ok) return 1;
    char p[512];
    snprintf(p, sizeof(p), "%s_pieza.png", argv[6]); ExportImage(part, p);
    snprintf(p, sizeof(p), "%s_resto.png", argv[6]); ExportImage(rest, p);
    return 0;
}
