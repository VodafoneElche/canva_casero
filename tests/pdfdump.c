// Herramienta de prueba: muestra las piezas que se sacan de un PDF
#include "../src/pdf.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2) { printf("uso: pdfdump archivo.pdf [carpeta]\n"); return 1; }
    SetTraceLogLevel(LOG_WARNING);
    int size = 0;
    unsigned char *d = LoadFileData(argv[1], &size);
    if (!d) return 1;
    PdfResult r;
    bool ok = pdf_import(d, size, 0, 150.0f / 72.0f, &r);
    printf("ok=%d paginas=%d tam=%.0fx%.0f piezas=%d saltadas=%d\n", ok, r.pages, r.pageW, r.pageH, r.count, r.skipped);
    for (int i = 0; i < r.count; i++) {
        PdfItem *it = &r.items[i];
        printf("%2d %s x=%.0f y=%.0f w=%.0f h=%.0f rot=%.1f", i, it->kind == PDF_TEXT ? "TEXTO" : it->kind == PDF_IMAGE ? "IMAGEN" : "FORMA", it->x, it->y, it->w, it->h, it->rotation);
        if (it->kind == PDF_TEXT) printf(" fuente=%s%s tam=%.1f interl=%.2f al=%d color=%02X%02X%02X base=%.0f\n    \"%s\"\n", it->font, it->bold ? "(negrita)" : "", it->fontSize, it->lineSpacing, it->align, it->color.r, it->color.g, it->color.b, it->baseline, it->text);
        else {
            printf(" img=%dx%d tinte=%d color=%02X%02X%02X\n", it->img.width, it->img.height, it->tint, it->color.r, it->color.g, it->color.b);
            if (argc > 2) { char p[512]; snprintf(p, sizeof(p), "%s/pieza%02d.png", argv[2], i); ExportImage(it->img, p); }
        }
    }
    pdf_free(&r);
    UnloadFileData(d);
    return 0;
}
