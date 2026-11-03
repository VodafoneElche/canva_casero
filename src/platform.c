// Este archivo NO incluye raylib.h: en Windows choca con windows.h.
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
// ---------------------------------------------------------------- Windows
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

static wchar_t *to_wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc(sizeof(wchar_t) * (n > 0 ? n : 1));
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n); else w[0] = 0;
    return w;
}

static void from_wide(const wchar_t *w, char *out, int outSize)
{
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, outSize, NULL, NULL) == 0) out[0] = 0;
}

static const wchar_t *filter_for(FileKind kind, const wchar_t **defExt)
{
    switch (kind) {
    case FILE_KIND_IMAGE:
        *defExt = L"png";
        return L"Imágenes (png, jpg, bmp, gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tga\0Todos los archivos\0*.*\0";
    case FILE_KIND_DESIGN:
        *defExt = L"lienzo";
        return L"Diseños de Lienzo Casero (*.lienzo)\0*.lienzo\0";
    case FILE_KIND_PNG:
        *defExt = L"png";
        return L"Imagen PNG (*.png)\0*.png\0";
    case FILE_KIND_PDF:
        *defExt = L"pdf";
        return L"Documento PDF (*.pdf)\0*.pdf\0";
    }
    *defExt = NULL;
    return L"Todos los archivos\0*.*\0";
}

static char g_path[4096];

const char *plat_open_dialog(const char *title, FileKind kind)
{
    wchar_t file[2048] = {0};
    wchar_t *wtitle = to_wide(title);
    const wchar_t *defExt;
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter_for(kind, &defExt);
    ofn.lpstrFile = file;
    ofn.nMaxFile = 2048;
    ofn.lpstrTitle = wtitle;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    BOOL ok = GetOpenFileNameW(&ofn);
    free(wtitle);
    if (!ok) return NULL;
    from_wide(file, g_path, sizeof(g_path));
    return g_path;
}

const char *plat_save_dialog(const char *title, const char *defaultName, FileKind kind)
{
    wchar_t file[2048] = {0};
    wchar_t *wtitle = to_wide(title);
    if (defaultName) {
        wchar_t *wn = to_wide(defaultName);
        wcsncpy(file, wn, 2047);
        free(wn);
    }
    const wchar_t *defExt;
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter_for(kind, &defExt);
    ofn.lpstrDefExt = defExt;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 2048;
    ofn.lpstrTitle = wtitle;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    BOOL ok = GetSaveFileNameW(&ofn);
    free(wtitle);
    if (!ok) return NULL;
    from_wide(file, g_path, sizeof(g_path));
    return g_path;
}

unsigned char *plat_read_file(const char *path, int *size)
{
    wchar_t *w = to_wide(path);
    FILE *f = _wfopen(w, L"rb");
    free(w);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = (unsigned char *)malloc(n + 1);
    if (fread(data, 1, n, f) != (size_t)n) { free(data); fclose(f); return NULL; }
    data[n] = 0;
    fclose(f);
    *size = (int)n;
    return data;
}

bool plat_write_file(const char *path, const void *data, int size)
{
    wchar_t *w = to_wide(path);
    FILE *f = _wfopen(w, L"wb");
    free(w);
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == (size_t)size;
    fclose(f);
    return ok;
}

bool plat_delete_file(const char *path)
{
    wchar_t *w = to_wide(path);
    BOOL ok = DeleteFileW(w);
    free(w);
    return ok != 0;
}

bool plat_file_exists(const char *path)
{
    wchar_t *w = to_wide(path);
    DWORD a = GetFileAttributesW(w);
    free(w);
    return a != INVALID_FILE_ATTRIBUTES;
}

bool plat_make_dir(const char *path)
{
    wchar_t *w = to_wide(path);
    BOOL ok = CreateDirectoryW(w, NULL);
    free(w);
    return ok || GetLastError() == ERROR_ALREADY_EXISTS;
}

const char *plat_data_dir(void)
{
    static char dir[1024] = {0};
    if (dir[0]) return dir;
    wchar_t w[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, w) == S_OK) {
        from_wide(w, dir, sizeof(dir));
        strncat(dir, "\\LienzoCasero", sizeof(dir) - strlen(dir) - 1);
    } else {
        strcpy(dir, ".\\LienzoCasero");
    }
    plat_make_dir(dir);
    return dir;
}

int plat_list_dir(const char *dir, const char *ext, char (*names)[260], int max)
{
    char pattern[1100];
    snprintf(pattern, sizeof(pattern), "%s\\*%s", dir, ext);
    wchar_t *w = to_wide(pattern);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(w, &fd);
    free(w);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n < max) {
            from_wide(fd.cFileName, names[n], 260);
            n++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

void plat_message(const char *title, const char *msg)
{
    wchar_t *t = to_wide(title), *m = to_wide(msg);
    MessageBoxW(GetActiveWindow(), m, t, MB_OK | MB_ICONINFORMATION);
    free(t); free(m);
}

int plat_ask_yes_no_cancel(const char *title, const char *msg)
{
    wchar_t *t = to_wide(title), *m = to_wide(msg);
    int r = MessageBoxW(GetActiveWindow(), m, t, MB_YESNOCANCEL | MB_ICONQUESTION);
    free(t); free(m);
    if (r == IDYES) return 1;
    if (r == IDNO) return 0;
    return -1;
}

#else
// ---------------------------------------------------------------- Linux / macOS
// Solo para probar el programa fuera de Windows. Usa "zenity" si está instalado.
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

static char g_path[4096];

static const char *run_zenity(const char *args)
{
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "zenity %s 2>/dev/null", args);
    FILE *p = popen(cmd, "r");
    if (!p) return NULL;
    if (!fgets(g_path, sizeof(g_path), p)) g_path[0] = 0;
    int status = pclose(p);
    size_t n = strlen(g_path);
    while (n > 0 && (g_path[n - 1] == '\n' || g_path[n - 1] == '\r')) g_path[--n] = 0;
    if (status != 0 || n == 0) return NULL;
    return g_path;
}

static const char *zenity_filter(FileKind kind)
{
    switch (kind) {
    case FILE_KIND_IMAGE: return "--file-filter='Imágenes | *.png *.jpg *.jpeg *.bmp *.gif *.tga'";
    case FILE_KIND_DESIGN: return "--file-filter='Diseños | *.lienzo'";
    case FILE_KIND_PNG: return "--file-filter='PNG | *.png'";
    case FILE_KIND_PDF: return "--file-filter='PDF | *.pdf'";
    }
    return "";
}

const char *plat_open_dialog(const char *title, FileKind kind)
{
    char args[1024];
    snprintf(args, sizeof(args), "--file-selection --title='%s' %s", title, zenity_filter(kind));
    return run_zenity(args);
}

const char *plat_save_dialog(const char *title, const char *defaultName, FileKind kind)
{
    char args[1024];
    snprintf(args, sizeof(args), "--file-selection --save --confirm-overwrite --title='%s' --filename='%s' %s",
             title, defaultName ? defaultName : "", zenity_filter(kind));
    return run_zenity(args);
}

unsigned char *plat_read_file(const char *path, int *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = (unsigned char *)malloc(n + 1);
    if (fread(data, 1, n, f) != (size_t)n) { free(data); fclose(f); return NULL; }
    data[n] = 0;
    fclose(f);
    *size = (int)n;
    return data;
}

bool plat_write_file(const char *path, const void *data, int size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == (size_t)size;
    fclose(f);
    return ok;
}

bool plat_delete_file(const char *path) { return unlink(path) == 0; }
bool plat_file_exists(const char *path) { struct stat st; return stat(path, &st) == 0; }
bool plat_make_dir(const char *path) { return mkdir(path, 0755) == 0 || plat_file_exists(path); }

const char *plat_data_dir(void)
{
    static char dir[1024] = {0};
    if (dir[0]) return dir;
    const char *home = getenv("HOME");
    snprintf(dir, sizeof(dir), "%s/.local", home ? home : ".");
    plat_make_dir(dir);
    strncat(dir, "/share", sizeof(dir) - strlen(dir) - 1);
    plat_make_dir(dir);
    strncat(dir, "/LienzoCasero", sizeof(dir) - strlen(dir) - 1);
    plat_make_dir(dir);
    return dir;
}

int plat_list_dir(const char *dir, const char *ext, char (*names)[260], int max)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int n = 0;
    size_t el = strlen(ext);
    while ((e = readdir(d)) && n < max) {
        size_t l = strlen(e->d_name);
        if (l > el && strcmp(e->d_name + l - el, ext) == 0) {
            strncpy(names[n], e->d_name, 259);
            names[n][259] = 0;
            n++;
        }
    }
    closedir(d);
    return n;
}

void plat_message(const char *title, const char *msg)
{
    fprintf(stderr, "[%s] %s\n", title, msg);
    char args[2048];
    snprintf(args, sizeof(args), "--info --title='%s' --text='%s'", title, msg);
    run_zenity(args);
}

int plat_ask_yes_no_cancel(const char *title, const char *msg)
{
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "zenity --question --title='%s' --text='%s' 2>/dev/null", title, msg);
    int r = system(cmd);
    if (r == -1 || r == 127 * 256) return 0; // sin zenity: responder "no"
    return r == 0 ? 1 : 0;
}
#endif

// En ambos sistemas guardamos nombres de carpeta con "/" o "\" indistintamente.
