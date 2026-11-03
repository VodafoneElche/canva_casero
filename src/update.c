#include "update.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

#define RELEASES_URL "https://api.github.com/repos/VodafoneElche/canva_casero/releases/latest"

static volatile int g_state = UPD_IDLE;
static char g_current[32];
static char g_version[32];
static char g_url[1024];
static char g_installer[1200];

UpdateState update_state(void) { return (UpdateState)g_state; }
const char *update_version(void) { return g_version; }
const char *update_installer(void) { return g_installer; }

int update_compare(const char *a, const char *b)
{
    while (*a == 'v' || *a == 'V') a++;
    while (*b == 'v' || *b == 'V') b++;
    for (int i = 0; i < 4; i++) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

// Copia el valor de texto de "key": "valor" que hay a partir de p
static const char *json_string(const char *p, const char *key, char *out, int max)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *k = strstr(p, pat);
    if (!k) return NULL;
    k += strlen(pat);
    while (*k == ' ' || *k == ':' || *k == '\t' || *k == '\n' || *k == '\r') k++;
    if (*k != '"') return NULL;
    k++;
    int n = 0;
    while (*k && *k != '"' && n < max - 1) {
        if (*k == '\\' && k[1]) k++;
        out[n++] = *k++;
    }
    out[n] = 0;
    return k;
}

bool update_parse(const char *json, char *ver, int verMax, char *url, int urlMax)
{
    char tag[64];
    if (!json_string(json, "tag_name", tag, sizeof(tag))) return false;
    const char *t = tag;
    while (*t == 'v' || *t == 'V') t++;
    snprintf(ver, verMax, "%s", t);
    // El instalador es el archivo que termina en "-instalador.exe"
    const char *p = json;
    char u[1024];
    while ((p = json_string(p, "browser_download_url", u, sizeof(u))) != NULL) {
        size_t n = strlen(u);
        if (n > 15 && strcmp(u + n - 15, "-instalador.exe") == 0) {
            snprintf(url, urlMax, "%s", u);
            return true;
        }
    }
    return false;
}

static void temp_path(char *out, int max, const char *name)
{
#ifdef _WIN32
    snprintf(out, max, "%s\\%s", plat_data_dir(), name);
#else
    snprintf(out, max, "%s/%s", plat_data_dir(), name);
#endif
}

static void check_job(void)
{
    char path[1200], url[400];
    temp_path(path, sizeof(path), "actualizacion.json");
    // La hora en la dirección evita que Windows nos dé una respuesta guardada de otra vez
    const char *test = getenv("LIENZO_ACTUALIZACION_URL");   // solo para pruebas
    if (test) snprintf(url, sizeof(url), "%s", test);
    else snprintf(url, sizeof(url), "%s?t=%ld", RELEASES_URL, (long)time(NULL));
    plat_delete_file(path);
    int size = 0;
    unsigned char *data = plat_download(url, path) ? plat_read_file(path, &size) : NULL;
    plat_delete_file(path);
    if (!data) { g_state = UPD_LATEST; return; }
    char *json = realloc(data, size + 1);
    json[size] = 0;
    bool ok = update_parse(json, g_version, sizeof(g_version), g_url, sizeof(g_url));
    free(json);
    g_state = ok && update_compare(g_version, g_current) > 0 ? UPD_AVAILABLE : UPD_LATEST;
}

static void download_job(void)
{
    char name[96];
    snprintf(name, sizeof(name), "LienzoCasero-%s-instalador.exe", g_version);
    temp_path(g_installer, sizeof(g_installer), name);
    plat_delete_file(g_installer);
    int size = 0;
    unsigned char *data = plat_download(g_url, g_installer) ? plat_read_file(g_installer, &size) : NULL;
    // Un instalador de verdad empieza por "MZ" y pesa más de 1 MB
    bool ok = data && size > 1000000 && data[0] == 'M' && data[1] == 'Z';
    free(data);
    g_state = ok ? UPD_READY : UPD_FAILED;
}

#ifdef _WIN32
static DWORD WINAPI thread_check(LPVOID p) { (void)p; check_job(); return 0; }
static DWORD WINAPI thread_download(LPVOID p) { (void)p; download_job(); return 0; }
static void start(LPTHREAD_START_ROUTINE fn)
{
    HANDLE h = CreateThread(NULL, 0, fn, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else g_state = UPD_LATEST;
}
#else
static void *thread_check(void *p) { (void)p; check_job(); return NULL; }
static void *thread_download(void *p) { (void)p; download_job(); return NULL; }
static void start(void *(*fn)(void *))
{
    pthread_t t;
    if (pthread_create(&t, NULL, fn, NULL) == 0) pthread_detach(t);
    else g_state = UPD_LATEST;
}
#endif

void update_check(const char *currentVersion)
{
    if (g_state != UPD_IDLE && g_state != UPD_LATEST) return;
    snprintf(g_current, sizeof(g_current), "%s", currentVersion);
    g_state = UPD_CHECKING;
    start(thread_check);
}

void update_download(void)
{
    if (g_state != UPD_AVAILABLE && g_state != UPD_FAILED) return;
    g_state = UPD_DOWNLOADING;
    start(thread_download);
}
