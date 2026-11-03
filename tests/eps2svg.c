// Prueba: convierte un EPS en SVG (eps2svg dibujo.eps salida.svg)
#include <stdio.h>
#include <stdlib.h>
#include "../src/eps.h"

int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc(n);
    if (fread(d, 1, n, f) != (size_t)n) return 1;
    fclose(f);
    int skipped = 0;
    char *svg = eps_to_svg(d, (int)n, &skipped);
    if (!svg) { printf("no se ha podido\n"); return 2; }
    FILE *o = fopen(argv[2], "wb");
    fputs(svg, o);
    fclose(o);
    printf("ok, %d cosas sin dibujar\n", skipped);
    return 0;
}
