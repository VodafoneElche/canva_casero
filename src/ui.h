// Botones, deslizadores y casillas de texto muy sencillos (modo inmediato).
#ifndef UI_H
#define UI_H

#include "raylib.h"
#include <stdbool.h>

// Colores de la aplicación
#define COL_BG        (Color){ 236, 239, 244, 255 }   // fondo detrás del lienzo
#define COL_PANEL     (Color){ 255, 255, 255, 255 }
#define COL_RAIL      (Color){ 24, 25, 38, 255 }
#define COL_RAIL_TXT  (Color){ 190, 194, 214, 255 }
#define COL_TEXT      (Color){ 33, 35, 48, 255 }
#define COL_MUTED     (Color){ 110, 115, 135, 255 }
#define COL_BORDER    (Color){ 222, 225, 234, 255 }
#define COL_HOVER     (Color){ 241, 242, 248, 255 }
#define COL_ACCENT    (Color){ 109, 74, 255, 255 }    // violeta
#define COL_ACCENT2   (Color){ 31, 181, 166, 255 }    // turquesa
#define COL_ACCENT_LT (Color){ 236, 232, 255, 255 }
#define COL_DANGER    (Color){ 220, 60, 80, 255 }

typedef enum { BTN_NORMAL, BTN_PRIMARY, BTN_GHOST, BTN_TOP, BTN_DANGER } ButtonStyle;

void ui_begin_frame(void);
// Mientras hay una ventana emergente, lo que queda debajo no recibe clics
void ui_set_blocker(Rectangle r, bool fullScreenModal);
void ui_clear_blocker(void);
void ui_set_layer(int layer); // 0 = normal, 1 = ventana emergente
// Solo cuentan los clics dentro de esta zona (para paneles con desplazamiento)
void ui_set_clip(Rectangle r);
void ui_clear_clip(void);

bool ui_mouse_free(void);           // el ratón no está sobre nada que lo bloquee
bool ui_hover(Rectangle r);
bool ui_clicked(Rectangle r);       // se soltó el clic encima
bool ui_pressed(Rectangle r);       // se pulsó encima

void ui_text(const char *t, float x, float y, float size, Color c);
void ui_text_bold(const char *t, float x, float y, float size, Color c);
float ui_text_width(const char *t, float size);
void ui_text_center(const char *t, Rectangle r, float size, Color c);
void ui_text_fit(const char *t, float x, float y, float maxW, float size, Color c);

bool ui_button(Rectangle r, const char *label, ButtonStyle style);
bool ui_button_enabled(Rectangle r, const char *label, ButtonStyle style, bool enabled);
bool ui_swatch(Rectangle r, Color c, bool selected);

// Devuelve 0 si no pasa nada, 1 al empezar a arrastrar, 2 mientras cambia
int ui_slider(int id, Rectangle r, float *value, float min, float max);
// Devuelve true cuando el texto cambia. Enter = *submitted
bool ui_textbox(int id, Rectangle r, char *buf, int max, bool *submitted);
bool ui_textbox_focused(void);
void ui_focus_textbox(int id);

void ui_shadow(Rectangle r, float radius);

// Tamaño de todo el programa (menús, botones, letras): 1 = normal, 1.25 = grande...
// Se dibuja todo con una lupa; el ratón y el tamaño de pantalla van en esa escala.
void ui_set_scale(float s);
float ui_scale(void);
int ui_sw(void);                    // ancho de la ventana en la escala del programa
int ui_sh(void);
void ui_apply_scale(void);          // tras BeginDrawing / EndMode2D
void ui_scissor(float x, float y, float w, float h);
Camera2D ui_camera(Camera2D cam);   // la cámara del lienzo con la lupa ya puesta

#endif
