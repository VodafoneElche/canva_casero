// Lápiz mágico: separar del fondo la parte de una imagen que se toca
#include "magic.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned char r, g, b, a; } Px;

static int color_dist(Px a, Px b)
{
    int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return (int)sqrtf((float)(dr * dr * 2 + dg * dg * 4 + db * db * 3) / 9.0f);
}

// Color más repetido en el borde de la imagen (o transparente si el borde lo es)
static Px border_color(const Px *p, int W, int H, bool *transparent)
{
    static int hist[4096];
    memset(hist, 0, sizeof(hist));
    int clear = 0, total = 0;
    for (int i = 0; i < W * 2 + H * 2; i++) {
        int x, y;
        if (i < W) { x = i; y = 0; }
        else if (i < W * 2) { x = i - W; y = H - 1; }
        else if (i < W * 2 + H) { x = 0; y = i - W * 2; }
        else { x = W - 1; y = i - W * 2 - H; }
        Px c = p[y * W + x];
        total++;
        if (c.a < 128) { clear++; continue; }
        hist[((c.r >> 4) << 8) | ((c.g >> 4) << 4) | (c.b >> 4)]++;
    }
    *transparent = clear * 2 > total;
    int best = 0;
    for (int i = 1; i < 4096; i++) if (hist[i] > hist[best]) best = i;
    // Media de los píxeles del borde que caen en ese grupo
    long sr = 0, sg = 0, sb = 0, n = 0;
    for (int i = 0; i < W * 2 + H * 2; i++) {
        int x, y;
        if (i < W) { x = i; y = 0; }
        else if (i < W * 2) { x = i - W; y = H - 1; }
        else if (i < W * 2 + H) { x = 0; y = i - W * 2; }
        else { x = W - 1; y = i - W * 2 - H; }
        Px c = p[y * W + x];
        if (c.a < 128) continue;
        if ((((c.r >> 4) << 8) | ((c.g >> 4) << 4) | (c.b >> 4)) == best) { sr += c.r; sg += c.g; sb += c.b; n++; }
    }
    Px out = { 255, 255, 255, 255 };
    if (n) { out.r = (unsigned char)(sr / n); out.g = (unsigned char)(sg / n); out.b = (unsigned char)(sb / n); }
    return out;
}

// Relleno desde (x, y) por los píxeles marcados con 1 en m; los deja marcados con 2
static void flood(unsigned char *m, int W, int H, int x, int y)
{
    int cap = 1 << 16, n = 0;
    int *st = (int *)malloc(sizeof(int) * cap);
    st[n++] = y * W + x;
    m[y * W + x] = 2;
    while (n) {
        int i = st[--n];
        int cx = i % W, cy = i / W;
        int nb[4] = { cx > 0 ? i - 1 : -1, cx < W - 1 ? i + 1 : -1, cy > 0 ? i - W : -1, cy < H - 1 ? i + W : -1 };
        for (int k = 0; k < 4; k++) {
            int j = nb[k];
            if (j < 0 || m[j] != 1) continue;
            m[j] = 2;
            if (n == cap) { cap *= 2; st = (int *)realloc(st, sizeof(int) * cap); }
            st[n++] = j;
        }
    }
    free(st);
}

// Ensancha la máscara (valores 0/1) un radio r, con sumas acumuladas
static unsigned char *dilate(const unsigned char *m, int W, int H, int r)
{
    int *sum = (int *)calloc((size_t)(W + 1) * (H + 1), sizeof(int));
    for (int y = 0; y < H; y++) {
        int row = 0;
        for (int x = 0; x < W; x++) {
            row += m[y * W + x] ? 1 : 0;
            sum[(y + 1) * (W + 1) + x + 1] = sum[y * (W + 1) + x + 1] + row;
        }
    }
    unsigned char *out = (unsigned char *)malloc((size_t)W * H);
    for (int y = 0; y < H; y++) {
        int y0 = y - r < 0 ? 0 : y - r, y1 = y + r + 1 > H ? H : y + r + 1;
        for (int x = 0; x < W; x++) {
            int x0 = x - r < 0 ? 0 : x - r, x1 = x + r + 1 > W ? W : x + r + 1;
            int s = sum[y1 * (W + 1) + x1] - sum[y0 * (W + 1) + x1] - sum[y1 * (W + 1) + x0] + sum[y0 * (W + 1) + x0];
            out[y * W + x] = s > 0 ? 1 : 0;
        }
    }
    free(sum);
    return out;
}

int magic_extract(Image src, int px, int py, float tolerance, Image *part, int *bx, int *by, Image *rest)
{
    Image img = ImageCopy(src);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    int W = img.width, H = img.height;
    if (px < 0 || py < 0 || px >= W || py >= H) { UnloadImage(img); return 0; }
    Px *p = (Px *)img.data;

    bool clearBg;
    Px bg = border_color(p, W, H, &clearBg);
    int tol = (int)(12 + tolerance * 90);

    // 1 = dibujo (distinto del fondo), 0 = fondo
    unsigned char *fg = (unsigned char *)malloc((size_t)W * H);
    for (int i = 0; i < W * H; i++) {
        if (p[i].a < 40) fg[i] = 0;
        else if (clearBg) fg[i] = 1;
        else fg[i] = color_dist(p[i], bg) > tol ? 1 : 0;
    }
    // Si se ha tocado fondo, buscamos dibujo muy cerca
    int sx = px, sy = py;
    if (!fg[sy * W + sx]) {
        int rr = (W > H ? W : H) / 100 + 3;
        bool found = false;
        for (int d = 1; d <= rr && !found; d++)
            for (int dy = -d; dy <= d && !found; dy++)
                for (int dx = -d; dx <= d && !found; dx++) {
                    int x = px + dx, y = py + dy;
                    if (x >= 0 && y >= 0 && x < W && y < H && fg[y * W + x]) { sx = x; sy = y; found = true; }
                }
        if (!found) { free(fg); UnloadImage(img); return 0; }
    }

    // Juntamos lo que está muy cerca (las letras de una palabra, las partes de un dibujo)
    int r = (W < H ? W : H) / 120;
    if (r < 2) r = 2;
    unsigned char *grown = dilate(fg, W, H, r);
    flood(grown, W, H, sx, sy);

    // Lo que queda dentro de la pieza (huecos) también es de la pieza:
    // rellenamos desde los bordes todo lo que no es pieza; lo que no se alcanza es interior
    unsigned char *outside = (unsigned char *)malloc((size_t)W * H);
    for (int i = 0; i < W * H; i++) outside[i] = grown[i] == 2 ? 0 : 1;
    for (int x = 0; x < W; x++) {
        if (outside[x] == 1) flood(outside, W, H, x, 0);
        if (outside[(H - 1) * W + x] == 1) flood(outside, W, H, x, H - 1);
    }
    for (int y = 0; y < H; y++) {
        if (outside[y * W] == 1) flood(outside, W, H, 0, y);
        if (outside[y * W + W - 1] == 1) flood(outside, W, H, W - 1, y);
    }
    // Pieza = pixel de dibujo dentro de la zona, o hueco interior
    unsigned char *sel = (unsigned char *)calloc((size_t)W * H, 1);
    int x0 = W, y0 = H, x1 = -1, y1 = -1, count = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            // Huecos: lo que es de otro color se queda con la pieza; lo del color del fondo, transparente
            bool in = (grown[i] == 2 || outside[i] == 1) && fg[i];
            if (outside[i] == 1 && !fg[i] && !clearBg) sel[i] = 4;   // hueco: se tapa en el original
            if (in) {
                sel[i] = 1;
                count++;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    free(grown);
    free(outside);
    free(fg);
    // Si la pieza es casi toda la imagen, no tiene sentido separarla
    if (count == 0 || (long)count > (long)W * H * 92 / 100) { free(sel); UnloadImage(img); return 0; }

    // Borde suave: los píxeles de dibujo pegados a la pieza también se llevan
    // (antialias), pero con transparencia según lo parecidos que sean al fondo
    int pw = x1 - x0 + 1, ph = y1 - y0 + 1;
    int ex = 1;
    x0 = x0 - ex < 0 ? 0 : x0 - ex; y0 = y0 - ex < 0 ? 0 : y0 - ex;
    x1 = x1 + ex >= W ? W - 1 : x1 + ex; y1 = y1 + ex >= H ? H - 1 : y1 + ex;
    pw = x1 - x0 + 1; ph = y1 - y0 + 1;
    Px *out = (Px *)calloc((size_t)pw * ph, sizeof(Px));
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            int i = (y + y0) * W + x + x0;
            if (sel[i] == 1) { out[y * pw + x] = p[i]; continue; }
            if (sel[i] == 4) continue;
            // vecino de la pieza: antialias
            bool near = false;
            int cx = x + x0, cy = y + y0;
            for (int dy = -1; dy <= 1 && !near; dy++)
                for (int dx = -1; dx <= 1 && !near; dx++) {
                    int nx = cx + dx, ny = cy + dy;
                    if (nx >= 0 && ny >= 0 && nx < W && ny < H && sel[ny * W + nx]) near = true;
                }
            if (near && !clearBg) {
                int d = color_dist(p[i], bg);
                if (d > tol / 3) {
                    Px c = p[i];
                    float a = fminf(1.0f, (float)d / (float)tol);
                    c.a = (unsigned char)(c.a * a);
                    out[y * pw + x] = c;
                    sel[i] = 3;   // también se tapa en el original
                }
            }
        }

    // El resto: tapamos la pieza (y un par de píxeles alrededor) con el color del fondo
    for (int i = 0; i < W * H; i++) sel[i] = sel[i] ? 1 : 0;
    unsigned char *cover = dilate(sel, W, H, 2);
    for (int i = 0; i < W * H; i++) {
        if (!cover[i]) continue;
        if (sel[i] || clearBg || color_dist(p[i], bg) < tol) p[i] = clearBg ? (Px){ bg.r, bg.g, bg.b, 0 } : bg;
    }
    free(cover);
    free(sel);
    // Píxeles transparentes de la pieza con el color de al lado: así no salen bordes oscuros
    for (int i = 0; i < pw * ph; i++) if (out[i].a == 0) { out[i].r = bg.r; out[i].g = bg.g; out[i].b = bg.b; }
    Image tmp = { out, pw, ph, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    image_bleed_edges(&tmp);

    *part = (Image){ out, pw, ph, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    *bx = x0;
    *by = y0;
    *rest = img;
    return 1;
}

void image_bleed_edges(Image *img)
{
    if (img->format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) return;
    int W = img->width, H = img->height;
    Px *p = (Px *)img->data;
    unsigned char *done = (unsigned char *)malloc((size_t)W * H);
    for (int i = 0; i < W * H; i++) done[i] = p[i].a > 0;
    for (int pass = 0; pass < 2; pass++) {
        unsigned char *next = (unsigned char *)malloc((size_t)W * H);
        memcpy(next, done, (size_t)W * H);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                if (done[i]) continue;
                int r = 0, g = 0, b = 0, n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= W || ny >= H || !done[ny * W + nx]) continue;
                        Px c = p[ny * W + nx];
                        r += c.r; g += c.g; b += c.b; n++;
                    }
                if (n) { p[i].r = (unsigned char)(r / n); p[i].g = (unsigned char)(g / n); p[i].b = (unsigned char)(b / n); next[i] = 1; }
            }
        free(done);
        done = next;
    }
    free(done);
}

// ------------------------------------------------------------------ Por recuadro
// Para carteles con fondos de degradado o con textura: el fondo se calcula a partir
// del borde del recuadro que dibujas, y el hueco se rellena mezclando ese borde.

static int cmp_uc(const void *a, const void *b) { return *(const unsigned char *)a - *(const unsigned char *)b; }

// Mediana de 7 en una fila de píxeles (quita trocitos de dibujo que toca el borde)
static void median_line(Px *line, int n)
{
    Px *tmp = (Px *)malloc(sizeof(Px) * n);
    memcpy(tmp, line, sizeof(Px) * n);
    for (int i = 0; i < n; i++) {
        unsigned char r[7], g[7], b[7];
        int k = 0;
        for (int d = -3; d <= 3; d++) {
            int j = i + d < 0 ? 0 : i + d >= n ? n - 1 : i + d;
            r[k] = tmp[j].r; g[k] = tmp[j].g; b[k] = tmp[j].b; k++;
        }
        qsort(r, 7, 1, cmp_uc); qsort(g, 7, 1, cmp_uc); qsort(b, 7, 1, cmp_uc);
        line[i] = (Px){ r[3], g[3], b[3], 255 };
    }
    free(tmp);
}

static int extract_box(Image src, int x0, int y0, int x1, int y1, float tolerance,
                       Image *part, int *bx, int *by, Image *rest, Color *mainColor, bool textMode)
{
    Image img = ImageCopy(src);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    int W = img.width, H = img.height;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    if (bw < 4 || bh < 4) { UnloadImage(img); return 0; }
    Px *p = (Px *)img.data;

    // Bordes del recuadro, suavizados
    Px *top = (Px *)malloc(sizeof(Px) * bw), *bot = (Px *)malloc(sizeof(Px) * bw);
    Px *lef = (Px *)malloc(sizeof(Px) * bh), *rig = (Px *)malloc(sizeof(Px) * bh);
    for (int x = 0; x < bw; x++) { top[x] = p[y0 * W + x0 + x]; bot[x] = p[y1 * W + x0 + x]; }
    for (int y = 0; y < bh; y++) { lef[y] = p[(y0 + y) * W + x0]; rig[y] = p[(y0 + y) * W + x1]; }
    median_line(top, bw); median_line(bot, bw); median_line(lef, bh); median_line(rig, bh);

    // Fondo calculado: mezcla de los cuatro bordes según la distancia
    Px *fill = (Px *)malloc(sizeof(Px) * bw * bh);
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            float wl = 1.0f / (x + 1), wr = 1.0f / (bw - x), wt = 1.0f / (y + 1), wb = 1.0f / (bh - y);
            wl *= wl; wr *= wr; wt *= wt; wb *= wb;
            float s = wl + wr + wt + wb;
            Px a = lef[y], b = rig[y], c = top[x], d = bot[x];
            fill[y * bw + x] = (Px){
                (unsigned char)((a.r * wl + b.r * wr + c.r * wt + d.r * wb) / s),
                (unsigned char)((a.g * wl + b.g * wr + c.g * wt + d.g * wb) / s),
                (unsigned char)((a.b * wl + b.b * wr + c.b * wt + d.b * wb) / s), 255 };
        }
    free(top); free(bot); free(lef); free(rig);

    int tol = (int)(14 + tolerance * 80);
    // Cuánto es "dibujo" cada píxel (0..1)
    float *alpha = (float *)calloc((size_t)bw * bh, sizeof(float));
    int ax0 = bw, ay0 = bh, ax1 = -1, ay1 = -1;
    double sr = 0, sg = 0, sb = 0, sw = 0;
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            Px c = p[(y0 + y) * W + x0 + x];
            if (c.a < 40) continue;
            int dd = color_dist(c, fill[y * bw + x]);
            float a = (dd - tol * 0.5f) / (tol * 0.5f);
            if (a <= 0) continue;
            if (a > 1) a = 1;
            alpha[y * bw + x] = a;
            if (a >= 1) {
                if (x < ax0) ax0 = x;
                if (x > ax1) ax1 = x;
                if (y < ay0) ay0 = y;
                if (y > ay1) ay1 = y;
            }
        }
    // Color principal: la media de los píxeles más distintos del fondo (el centro de las letras,
    // no sus bordes suaves)
    {
        int maxd = 0;
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++) {
                if (alpha[y * bw + x] < 1) continue;
                int dd = color_dist(p[(y0 + y) * W + x0 + x], fill[y * bw + x]);
                if (dd > maxd) maxd = dd;
            }
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++) {
                if (alpha[y * bw + x] < 1) continue;
                Px c = p[(y0 + y) * W + x0 + x];
                int dd = color_dist(c, fill[y * bw + x]);
                if (dd < maxd * 0.75f) continue;
                sr += c.r; sg += c.g; sb += c.b; sw += 1;
            }
    }
    if (ax1 < 0) { free(alpha); free(fill); UnloadImage(img); return 0; }
    if (textMode && sw > 0) {
        // Letras: solo cuenta lo que va del color del fondo al color de las letras.
        // Así no se borran bordes de manchas o dibujos que haya en el recuadro.
        float mr = (float)(sr / sw), mg = (float)(sg / sw), mb = (float)(sb / sw);
        ax0 = bw; ay0 = bh; ax1 = -1; ay1 = -1;
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++) {
                int i = y * bw + x;
                Px c = p[(y0 + y) * W + x0 + x], f = fill[i];
                float dr = mr - f.r, dg = mg - f.g, db = mb - f.b;
                float len2 = dr * dr + dg * dg + db * db;
                if (len2 < 1) { alpha[i] = 0; continue; }
                float t = ((c.r - f.r) * dr + (c.g - f.g) * dg + (c.b - f.b) * db) / len2;
                float er = f.r + dr * t - c.r, eg = f.g + dg * t - c.g, eb = f.b + db * t - c.b;
                float resid = sqrtf(er * er + eg * eg + eb * eb);
                float a = (t - 0.12f) / 0.6f;
                if (resid > tol * 1.2f) a = 0;
                a = a < 0 ? 0 : a > 1 ? 1 : a;
                alpha[i] = a;
                if (a > 0.5f) {
                    if (x < ax0) ax0 = x;
                    if (x > ax1) ax1 = x;
                    if (y < ay0) ay0 = y;
                    if (y > ay1) ay1 = y;
                }
            }
        if (ax1 < 0) { free(alpha); free(fill); UnloadImage(img); return 0; }
    }
    ax0 = ax0 > 1 ? ax0 - 2 : 0; ay0 = ay0 > 1 ? ay0 - 2 : 0;
    ax1 = ax1 < bw - 2 ? ax1 + 2 : bw - 1; ay1 = ay1 < bh - 2 ? ay1 + 2 : bh - 1;
    int pw = ax1 - ax0 + 1, ph = ay1 - ay0 + 1;
    Px *out = (Px *)calloc((size_t)pw * ph, sizeof(Px));
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            int i = (ay0 + y) * bw + ax0 + x;
            Px c = p[(y0 + ay0 + y) * W + x0 + ax0 + x];
            float a = alpha[i];
            if (a <= 0) { Px f = fill[i]; out[y * pw + x] = (Px){ f.r, f.g, f.b, 0 }; continue; }
            // Quitamos la parte de fondo que lleva mezclada el borde suave
            Px f = fill[i];
            Px o;
            if (a < 1) {
                o.r = (unsigned char)fminf(255, fmaxf(0, (c.r - f.r * (1 - a)) / a));
                o.g = (unsigned char)fminf(255, fmaxf(0, (c.g - f.g * (1 - a)) / a));
                o.b = (unsigned char)fminf(255, fmaxf(0, (c.b - f.b * (1 - a)) / a));
            } else o = c;
            o.a = (unsigned char)(a * c.a);
            out[y * pw + x] = o;
        }
    // En la imagen original, lo separado se sustituye por el fondo calculado
    unsigned char *m = (unsigned char *)calloc((size_t)bw * bh, 1);
    for (int i = 0; i < bw * bh; i++) m[i] = alpha[i] > 0 ? 1 : 0;
    unsigned char *grown = dilate(m, bw, bh, textMode ? 3 : 2);
    // Primero el fondo calculado y luego lo suavizamos con lo que hay alrededor de cada letra,
    // así se mantienen los degradados y las manchas de color del fondo
    float *acc = (float *)malloc(sizeof(float) * 3 * bw * bh);
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            int i = y * bw + x;
            Px c = grown[i] ? fill[i] : p[(y0 + y) * W + x0 + x];
            acc[i * 3] = c.r; acc[i * 3 + 1] = c.g; acc[i * 3 + 2] = c.b;
        }
    for (int it = 0; it < 80; it++)
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++) {
                int i = y * bw + x;
                if (!grown[i]) continue;
                float r = 0, g = 0, b = 0;
                int n = 0;
                int nb[4][2] = { { x - 1, y }, { x + 1, y }, { x, y - 1 }, { x, y + 1 } };
                for (int k = 0; k < 4; k++) {
                    int nx = nb[k][0], ny = nb[k][1];
                    if (nx < 0 || ny < 0 || nx >= bw || ny >= bh) continue;
                    int j = ny * bw + nx;
                    r += acc[j * 3]; g += acc[j * 3 + 1]; b += acc[j * 3 + 2];
                    n++;
                }
                if (n) { acc[i * 3] = r / n; acc[i * 3 + 1] = g / n; acc[i * 3 + 2] = b / n; }
            }
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            int i = y * bw + x;
            if (grown[i]) p[(y0 + y) * W + x0 + x] = (Px){ (unsigned char)acc[i * 3], (unsigned char)acc[i * 3 + 1], (unsigned char)acc[i * 3 + 2], 255 };
        }
    free(acc);
    free(grown); free(m); free(alpha); free(fill);

    *part = (Image){ out, pw, ph, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    *bx = x0 + ax0;
    *by = y0 + ay0;
    *rest = img;
    if (mainColor) {
        *mainColor = sw > 0 ? (Color){ (unsigned char)(sr / sw), (unsigned char)(sg / sw), (unsigned char)(sb / sw), 255 } : BLACK;
    }
    return 1;
}

int magic_extract_box(Image src, int x0, int y0, int x1, int y1, float tolerance,
                      Image *part, int *bx, int *by, Image *rest, Color *mainColor)
{
    return extract_box(src, x0, y0, x1, y1, tolerance, part, bx, by, rest, mainColor, false);
}

int magic_extract_text(Image src, int x0, int y0, int x1, int y1, float tolerance,
                       Image *part, int *bx, int *by, Image *rest, Color *textColor)
{
    return extract_box(src, x0, y0, x1, y1, tolerance, part, bx, by, rest, textColor, true);
}
