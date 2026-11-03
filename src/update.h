#ifndef UPDATE_H
#define UPDATE_H

#include <stdbool.h>

// Actualizaciones: al abrir el programa se mira en GitHub (Releases) si hay una versión más
// nueva. Todo va en segundo plano para no parar el programa.
typedef enum {
    UPD_IDLE,         // no se ha mirado
    UPD_CHECKING,     // mirando
    UPD_LATEST,       // ya es la última (o no hay internet)
    UPD_AVAILABLE,    // hay una nueva: update_version()
    UPD_DOWNLOADING,  // bajando el instalador
    UPD_READY,        // instalador bajado: update_installer()
    UPD_FAILED        // no se ha podido bajar
} UpdateState;

void update_check(const char *currentVersion);
void update_download(void);
UpdateState update_state(void);
const char *update_version(void);    // p. ej. "0.9.2"
const char *update_installer(void);  // ruta del instalador ya bajado

// Para pruebas: compara "1.2.3" con "1.10.0". <0, 0, >0
int update_compare(const char *a, const char *b);
// Saca la versión y el enlace del instalador del JSON de GitHub. false si no hay.
bool update_parse(const char *json, char *ver, int verMax, char *url, int urlMax);

#endif
