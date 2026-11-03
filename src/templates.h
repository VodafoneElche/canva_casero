#ifndef TEMPLATES_H
#define TEMPLATES_H

#include "doc.h"

typedef struct {
    char name[200];      // nombre sin extensión
    char path[1400];     // ruta del .lienzo
    char thumbPath[1400];
    Texture2D thumb;
} TemplateInfo;

#define MAX_TEMPLATES 120

extern TemplateInfo g_templates[MAX_TEMPLATES];
extern int g_templateCount;

const char *templates_dir(void);
void templates_install_defaults(void);  // crea las plantillas de ejemplo la primera vez
void templates_refresh(void);           // vuelve a leer la carpeta
bool templates_save(const Document *d, const char *name);
bool templates_delete(int index);

#endif
