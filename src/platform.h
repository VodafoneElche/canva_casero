// Funciones que dependen del sistema operativo: ventanas de abrir/guardar,
// lectura y escritura de archivos (con nombres con tildes) y carpetas.
// Todas las rutas van en UTF-8.
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>

typedef enum {
    FILE_KIND_IMAGE,    // png, jpg...
    FILE_KIND_DESIGN,   // .lienzo
    FILE_KIND_PNG,
    FILE_KIND_PDF,
    FILE_KIND_FONT      // .ttf / .otf
} FileKind;

// Devuelven NULL si el usuario cancela. La cadena es estática (copiarla si hace falta).
const char *plat_open_dialog(const char *title, FileKind kind);
const char *plat_save_dialog(const char *title, const char *defaultName, FileKind kind);

// Lee el archivo entero. Liberar con free(). Devuelve NULL si falla.
unsigned char *plat_read_file(const char *path, int *size);
bool plat_write_file(const char *path, const void *data, int size);
bool plat_delete_file(const char *path);
bool plat_file_exists(const char *path);

// Carpeta de datos del programa (se crea si no existe), p. ej. %APPDATA%/LienzoCasero
const char *plat_data_dir(void);
bool plat_make_dir(const char *path);

// Lista los archivos de una carpeta que terminan en "ext". Devuelve cuántos.
int plat_list_dir(const char *dir, const char *ext, char (*names)[260], int max);

void plat_message(const char *title, const char *msg);
// 1 = sí, 0 = no, -1 = cancelar
int plat_ask_yes_no_cancel(const char *title, const char *msg);

#endif
