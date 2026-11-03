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
        return L"Imágenes y dibujos (png, jpg, svg...)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tga;*.svg\0Todos los archivos\0*.*\0";
    case FILE_KIND_DESIGN:
        *defExt = L"lienzo";
        return L"Diseños de Lienzo Casero (*.lienzo)\0*.lienzo\0";
    case FILE_KIND_PNG:
        *defExt = L"png";
        return L"Imagen PNG (*.png)\0*.png\0";
    case FILE_KIND_PDF:
        *defExt = L"pdf";
        return L"Documento PDF (*.pdf)\0*.pdf\0";
    case FILE_KIND_FONT:
        *defExt = L"ttf";
        return L"Fuentes (*.ttf, *.otf)\0*.ttf;*.otf\0";
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


// ---- Leer texto de imágenes con el OCR de Windows 10/11 (Windows.Media.Ocr) ----
static const char OCR_PS1[] =
"param([string]$img, [string]$out)\r\n"
"$ErrorActionPreference = 'Stop'\r\n"
"try {\r\n"
"Add-Type -AssemblyName System.Runtime.WindowsRuntime\r\n"
"$null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]\r\n"
"$null = [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime]\r\n"
"$null = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Foundation, ContentType = WindowsRuntime]\r\n"
"$null = [Windows.Globalization.Language, Windows.Foundation, ContentType = WindowsRuntime]\r\n"
"$asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]\r\n"
"function Await($op, [Type]$t) { $task = $asTask.MakeGenericMethod($t).Invoke($null, @($op)); $null = $task.Wait(-1); $task.Result }\r\n"
"$file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($img)) ([Windows.Storage.StorageFile])\r\n"
"$stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])\r\n"
"$dec = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])\r\n"
"$bmp = Await ($dec.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])\r\n"
"$engine = $null\r\n"
"foreach ($code in @('es-ES', 'es')) { if ($engine -eq $null) { $lang = New-Object Windows.Globalization.Language $code; if ([Windows.Media.Ocr.OcrEngine]::IsLanguageSupported($lang)) { $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage($lang) } } }\r\n"
"if ($engine -eq $null) { $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages() }\r\n"
"$res = Await ($engine.RecognizeAsync($bmp)) ([Windows.Media.Ocr.OcrResult])\r\n"
"$sb = New-Object System.Text.StringBuilder\r\n"
"foreach ($line in $res.Lines) {\r\n"
"  $x0 = 1e9; $y0 = 1e9; $x1 = -1e9; $y1 = -1e9\r\n"
"  foreach ($w in $line.Words) { $r = $w.BoundingRect; if ($r.X -lt $x0) { $x0 = $r.X }; if ($r.Y -lt $y0) { $y0 = $r.Y }; if ($r.X + $r.Width -gt $x1) { $x1 = $r.X + $r.Width }; if ($r.Y + $r.Height -gt $y1) { $y1 = $r.Y + $r.Height } }\r\n"
"  $null = $sb.Append(('{0} {1} {2} {3}' -f [int]$x0, [int]$y0, [int]($x1 - $x0), [int]($y1 - $y0)) + \"`t\" + $line.Text + \"`n\")\r\n"
"}\r\n"
"[System.IO.File]::WriteAllText($out, $sb.ToString(), (New-Object System.Text.UTF8Encoding $false))\r\n"
"} catch { [System.IO.File]::WriteAllText($out, 'ERROR ' + $_.Exception.Message) }\r\n";

bool plat_ocr(const char *pngPath, char *out, int max)
{
    out[0] = 0;
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wchar_t ps1[MAX_PATH + 40], res[MAX_PATH + 40];
    swprintf(ps1, MAX_PATH + 40, L"%lslienzo_ocr.ps1", tmp);
    swprintf(res, MAX_PATH + 40, L"%lslienzo_ocr.txt", tmp);
    FILE *f = _wfopen(ps1, L"wb");
    if (!f) return false;
    fwrite(OCR_PS1, 1, sizeof(OCR_PS1) - 1, f);
    fclose(f);
    DeleteFileW(res);
    wchar_t *img = to_wide(pngPath);
    wchar_t cmd[4096];
    swprintf(cmd, 4096, L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%ls\" \"%ls\" \"%ls\"", ps1, img, res);
    free(img);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, 30000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    FILE *r = _wfopen(res, L"rb");
    if (!r) return false;
    int n = (int)fread(out, 1, max - 1, r);
    fclose(r);
    out[n < 0 ? 0 : n] = 0;
    if (strncmp(out, "ERROR", 5) == 0) return false;
    return true;
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
    case FILE_KIND_IMAGE: return "--file-filter='Imágenes | *.png *.jpg *.jpeg *.bmp *.gif *.tga *.svg'";
    case FILE_KIND_DESIGN: return "--file-filter='Diseños | *.lienzo'";
    case FILE_KIND_PNG: return "--file-filter='PNG | *.png'";
    case FILE_KIND_PDF: return "--file-filter='PDF | *.pdf'";
    case FILE_KIND_FONT: return "--file-filter='Fuentes | *.ttf *.otf'";
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


// En Linux (para pruebas) usamos tesseract si está instalado
bool plat_ocr(const char *pngPath, char *out, int max)
{
    out[0] = 0;
    char cmd[2400];
    snprintf(cmd, sizeof(cmd), "tesseract '%s' stdout -l spa --psm 6 tsv 2>/dev/null", pngPath);
    FILE *p = popen(cmd, "r");
    if (!p) return false;
    char line[2048];
    int curKey = -1, x0 = 0, y0 = 0, x1 = 0, y1 = 0, len = 0;
    char text[1024] = { 0 };
    bool any = false;
    while (fgets(line, sizeof(line), p)) {
        int level, page, block, par, ln, word, l, t, w, h;
        float conf;
        char wtext[512] = { 0 };
        if (sscanf(line, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%f\t%511[^\n]", &level, &page, &block, &par, &ln, &word, &l, &t, &w, &h, &conf, wtext) < 11) continue;
        if (level != 5 || !wtext[0]) continue;
        int key = block * 10000 + par * 100 + ln;
        if (key != curKey) {
            if (any) len += snprintf(out + len, max - len, "%d %d %d %d\t%s\n", x0, y0, x1 - x0, y1 - y0, text);
            curKey = key; x0 = l; y0 = t; x1 = l + w; y1 = t + h; text[0] = 0; any = true;
        } else {
            if (l < x0) x0 = l;
            if (t < y0) y0 = t;
            if (l + w > x1) x1 = l + w;
            if (t + h > y1) y1 = t + h;
            strncat(text, " ", sizeof(text) - strlen(text) - 1);
        }
        strncat(text, wtext, sizeof(text) - strlen(text) - 1);
        if (len >= max - 1) break;
    }
    if (any && len < max - 1) len += snprintf(out + len, max - len, "%d %d %d %d\t%s\n", x0, y0, x1 - x0, y1 - y0, text);
    pclose(p);
    return any;
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
