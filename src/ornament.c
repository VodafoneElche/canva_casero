// Marcos, adornos (espirales, volutas, lazos) y letreros con el texto en curva.
// Todo se dibuja con líneas y triángulos de raylib, así se ve nítido a cualquier tamaño
// y se puede cambiar de color. Diseños propios.
#include "ornament.h"
#include "render.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <string.h>

// ------------------------------------------------------------------ Nombres

static const char *FRAME_NAMES[FRAME_VARIANTS] = {
    "Clásico", "Espirales", "Entrelazado", "Ondas", "Cuerda", "Solo esquinas", "Escalonado"
};
static const char *ORN_NAMES[ORNAMENT_VARIANTS] = {
    "Espiral", "Doble espiral", "Volutas", "Separador", "Esquina", "Lazo", "Rizos", "Trenza"
};
static const char *BANNER_NAMES[BANNER_VARIANTS] = {
    "Cinta con pliegues", "Cinta con picos", "Pergamino", "Cinta ondulada", "Etiqueta"
};

const char *frame_variant_name(int v) { return FRAME_NAMES[((v % FRAME_VARIANTS) + FRAME_VARIANTS) % FRAME_VARIANTS]; }
const char *ornament_variant_name(int v) { return ORN_NAMES[((v % ORNAMENT_VARIANTS) + ORNAMENT_VARIANTS) % ORNAMENT_VARIANTS]; }
const char *banner_variant_name(int v) { return BANNER_NAMES[((v % BANNER_VARIANTS) + BANNER_VARIANTS) % BANNER_VARIANTS]; }

// ------------------------------------------------------------------ Trazos

#define OP 700
typedef struct { Vector2 p[OP]; float w[OP]; int n; } Path;   // puntos y grosor relativo
typedef struct { Vector2 o, ax, ay; } Xf;                       // punto = o + ax*u + ay*v

static Vector2 X(Xf T, float u, float v)
{
    return (Vector2){ T.o.x + T.ax.x * u + T.ay.x * v, T.o.y + T.ax.y * u + T.ay.y * v };
}

static Xf xf_scaled(Xf T, Vector2 at, float k)   // otro sistema centrado en 'at' (u,v de T) y k veces más pequeño
{
    Xf R = { X(T, at.x, at.y), { T.ax.x * k, T.ax.y * k }, { T.ay.x * k, T.ay.y * k } };
    return R;
}

static void add(Path *P, float u, float v, float w)
{
    if (P->n < OP) { P->p[P->n] = (Vector2){ u, v }; P->w[P->n] = w; P->n++; }
}

static void reverse_from(Path *P, int s)
{
    for (int i = s, j = P->n - 1; i < j; i++, j--) {
        Vector2 t = P->p[i]; P->p[i] = P->p[j]; P->p[j] = t;
        float q = P->w[i]; P->w[i] = P->w[j]; P->w[j] = q;
    }
}

static void swap_uv(Path *P)
{
    for (int i = 0; i < P->n; i++) { float t = P->p[i].x; P->p[i].x = P->p[i].y; P->p[i].y = t; }
}

static void mirror_u(Path *P)
{
    for (int i = 0; i < P->n; i++) P->p[i].x = -P->p[i].x;
}

// Espiral de fuera hacia dentro. a0 en grados; dir +1 = sentido de las agujas del reloj
static void spiral(Path *P, float cx, float cy, float R, float a0, float turns, float dir, float inner, float w0, float w1)
{
    int n = (int)(turns * 44) + 2;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (n - 1);
        float a = a0 * DEG2RAD + dir * t * turns * 2 * PI;
        float r = R * (1 - (1 - inner) * t);
        add(P, cx + r * cosf(a), cy + r * sinf(a), w0 + (w1 - w0) * t);
    }
}

// Curva de Bézier; 'bulge' engorda el centro del trazo
static void cubic(Path *P, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                  float w0, float w1, float bulge, int steps, bool skipFirst)
{
    for (int i = skipFirst ? 1 : 0; i <= steps; i++) {
        float t = (float)i / steps, m = 1 - t;
        float a = m * m * m, b = 3 * m * m * t, c = 3 * m * t * t, d = t * t * t;
        add(P, a * x0 + b * x1 + c * x2 + d * x3, a * y0 + b * y1 + c * y2 + d * y3,
            w0 + (w1 - w0) * t + bulge * sinf(PI * t));
    }
}

static void tri(Vector2 a, Vector2 b, Vector2 c, Color col)
{
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross < 0) DrawTriangle(a, b, c, col);
    else DrawTriangle(a, c, b, col);
}

static void quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col) { tri(a, b, c, col); tri(a, c, d, col); }

// Trazo de grosor variable por puntos ya colocados
static void raw_stroke(const Vector2 *q, const float *w, int n, float ws, float extra, Color c, bool capStart)
{
    if (n < 2) return;
    if (capStart) DrawCircleV(q[0], (w[0] * ws + extra) / 2, c);
    for (int i = 0; i < n - 1; i++) {
        float wa = w[i] * ws + extra, wb = w[i + 1] * ws + extra;
        DrawLineEx(q[i], q[i + 1], (wa + wb) / 2, c);
        if (wb > 2.5f) DrawCircleV(q[i + 1], wb / 2, c);
    }
}

static void stroke(const Path *P, Xf T, float ws, Color c)
{
    static Vector2 q[OP];
    for (int i = 0; i < P->n; i++) q[i] = X(T, P->p[i].x, P->p[i].y);
    raw_stroke(q, P->w, P->n, ws, 0, c, true);
}

static Color outline_of(Color c)
{
    float lum = (0.3f * c.r + 0.59f * c.g + 0.11f * c.b) / 255.0f;
    if (lum < 0.25f) return (Color){ (unsigned char)(c.r + (255 - c.r) * 0.55f), (unsigned char)(c.g + (255 - c.g) * 0.55f), (unsigned char)(c.b + (255 - c.b) * 0.55f), c.a };
    return (Color){ (unsigned char)(c.r * 0.5f), (unsigned char)(c.g * 0.5f), (unsigned char)(c.b * 0.5f), c.a };
}

static Color darker(Color c, float k) { return (Color){ (unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), c.a }; }

// Cinta con borde oscuro, por trozos: lo que se dibuja después pasa por encima (entrelazado)
static void ribbon(const Path *P, Xf T, float ws, Color c, int chunk)
{
    static Vector2 q[OP];
    for (int i = 0; i < P->n; i++) q[i] = X(T, P->p[i].x, P->p[i].y);
    Color o = outline_of(c);
    if (chunk < 2) chunk = P->n;
    for (int s = 0; s < P->n - 1; s += chunk) {
        int e = s + chunk < P->n - 1 ? s + chunk : P->n - 1;
        raw_stroke(q + s, P->w + s, e - s + 1, ws, ws * 0.9f, o, s == 0);
        raw_stroke(q + s, P->w + s, e - s + 1, ws, 0, c, s == 0);
    }
}

// ------------------------------------------------------------------ Piezas comunes

// Esquina con una espiral, dos brazos con rizos y tres puntos (esquina en (0,0), hacia dentro +u y +v, tamaño 1)
static void draw_corner(Xf T, float ws, Color c)
{
    // Espiral central: de dentro hacia fuera, acaba arriba yendo hacia la derecha
    Path S = { 0 };
    spiral(&S, 0.2f, 0.2f, 0.16f, -90, 1.6f, -1, 0.12f, 1.2f, 0.3f);
    reverse_from(&S, 0);
    stroke(&S, T, ws, c);
    for (int m = 0; m < 2; m++) {
        Path P = { 0 };
        cubic(&P, 0.2f, 0.04f, 0.4f, 0.02f, 0.55f, 0.07f, 0.75f, 0.04f, 1.2f, 0.9f, 0, 22, false);
        spiral(&P, 0.75f, 0.12f, 0.08f, -90, 1.3f, 1, 0.22f, 0.9f, 0.3f);
        Path B = { 0 };   // rizo pequeño que cuelga del brazo
        cubic(&B, 0.42f, 0.045f, 0.44f, 0.07f, 0.47f, 0.09f, 0.47f, 0.12f, 0.8f, 0.7f, 0, 6, false);
        spiral(&B, 0.42f, 0.12f, 0.05f, 0, 1.2f, 1, 0.25f, 0.7f, 0.3f);
        if (m) { swap_uv(&P); swap_uv(&B); }
        stroke(&P, T, ws, c);
        stroke(&B, T, ws, c);
    }
    DrawCircleV(X(T, 0.37f, 0.37f), ws * 1.3f, c);
    DrawCircleV(X(T, 0.45f, 0.45f), ws * 0.95f, c);
    DrawCircleV(X(T, 0.51f, 0.51f), ws * 0.65f, c);
}

// Adorno central de separador (centro en (0,0), a lo largo de u, hacia dentro +v)
static void draw_divider_core(Xf T, float ws, Color c, bool both)
{
    for (int sx = -1; sx <= 1; sx += 2) {
        Path P = { 0 };
        spiral(&P, 0.55f, 0.2f, 0.2f, -90, 1.3f, 1, 0.2f, 1.0f, 0.3f);
        add(&P, 0, 0, 0);   // marcador, se quita abajo
        P.n--;
        Path Q = { 0 };
        add(&Q, 0.2f, 0, 1.0f);
        for (int i = 0; i < P.n; i++) add(&Q, P.p[i].x, P.p[i].y, P.w[i]);
        if (sx < 0) mirror_u(&Q);
        stroke(&Q, T, ws, c);
        if (both) {
            Path U = { 0 };
            spiral(&U, 0.38f, -0.13f, 0.13f, 90, 1.2f, -1, 0.22f, 0.8f, 0.3f);
            if (sx < 0) mirror_u(&U);
            stroke(&U, T, ws, c);
        }
    }
    Vector2 d[4] = { X(T, 0, -0.32f), X(T, 0.17f, 0), X(T, 0, 0.32f), X(T, -0.17f, 0) };
    quad(d[0], d[1], d[2], d[3], c);
}

// Dos cintas que se cruzan a lo largo de u (entre u0 y u1), centradas en v = base
static void braid(Xf E, float u0, float u1, float base, float A, float halfWave, float ws, Color c)
{
    float len = u1 - u0;
    if (len <= 0) return;
    int nh = (int)roundf(len / halfWave);
    if (nh < 1) nh = 1;
    float hwv = len / nh;   // media onda
    for (int k = 0; k <= nh; k++) {
        float a = u0 + k * hwv - hwv / 2, b = u0 + k * hwv + hwv / 2;
        if (a < u0) a = u0;
        if (b > u1) b = u1;
        for (int pass = 0; pass < 2; pass++) {
            // Primero la que va por debajo; se alternan en cada cruce
            int strand = (k % 2 == 0) ? pass : 1 - pass;
            float sg = strand ? -1.0f : 1.0f;
            Path P = { 0 };
            int steps = 12;
            for (int i = 0; i <= steps; i++) {
                float u = a + (b - a) * i / steps;
                add(&P, u, base + sg * A * sinf(PI * (u - u0) / hwv), 1);
            }
            ribbon(&P, E, ws, c, 0);
        }
    }
}

// Sistemas de cada lado del marco: u a lo largo, v hacia dentro (unidades del documento)
static Xf edge_xf(const Element *e, int k, float *len)
{
    float hw = e->w / 2, hh = e->h / 2;
    switch (k) {
    case 0: *len = e->w; return (Xf){ { -hw, -hh }, { 1, 0 }, { 0, 1 } };
    case 1: *len = e->h; return (Xf){ { hw, -hh }, { 0, 1 }, { -1, 0 } };
    case 2: *len = e->w; return (Xf){ { hw, hh }, { -1, 0 }, { 0, -1 } };
    default: *len = e->h; return (Xf){ { -hw, hh }, { 0, -1 }, { 1, 0 } };
    }
}

// Esquina k con tamaño s, metida 'in' hacia dentro
static Xf corner_xf(const Element *e, int k, float s, float in)
{
    float hw = e->w / 2, hh = e->h / 2;
    float sx = (k == 0 || k == 3) ? 1 : -1, sy = (k < 2) ? 1 : -1;
    return (Xf){ { -sx * hw + sx * in, -sy * hh + sy * in }, { sx * s, 0 }, { 0, sy * s } };
}

static void edge_line(Xf E, float u0, float u1, float v, float w, Color c)
{
    if (u1 <= u0) return;
    DrawLineEx(X(E, u0, v), X(E, u1, v), w, c);
}

static void rect_lines(const Element *e, float in, float w, Color c)
{
    float hw = e->w / 2 - in, hh = e->h / 2 - in;
    if (hw <= 0 || hh <= 0) return;
    Vector2 p[4] = { { -hw, -hh }, { hw, -hh }, { hw, hh }, { -hw, hh } };
    for (int i = 0; i < 4; i++) {
        DrawLineEx(p[i], p[(i + 1) % 4], w, c);
        DrawRectangleV((Vector2){ p[i].x - w / 2, p[i].y - w / 2 }, (Vector2){ w, w }, c);
    }
}

// ------------------------------------------------------------------ Marcos

float frame_corner_size(const Element *e)
{
    return fminf(e->w, e->h) * (0.08f + 0.17f * fminf(1, fmaxf(0, e->roundness)));
}

float frame_band(const Element *e)
{
    float s = frame_corner_size(e);
    int v = e->variant % FRAME_VARIANTS;
    float b = v == 5 ? s * 1.5f : v == 1 ? s * 1.25f : s * 0.5f;
    return b + e->strokeW * 2;
}

void frame_draw(const Element *e, Color c)
{
    float s = frame_corner_size(e);
    float t = e->strokeW > 0.5f ? e->strokeW : 0.5f;
    int v = ((e->variant % FRAME_VARIANTS) + FRAME_VARIANTS) % FRAME_VARIANTS;
    switch (v) {
    case 0: {   // Clásico: línea fina por fuera, línea con volutas y adorno en el centro de cada lado
        float g = s * 0.08f, lv = g + s * 0.04f;
        rect_lines(e, 0, t * 0.55f, c);
        for (int k = 0; k < 4; k++) draw_corner(corner_xf(e, k, s, g), t, c);
        for (int k = 0; k < 4; k++) {
            float len;
            Xf E = edge_xf(e, k, &len);
            float a = g + 0.75f * s, b = len - a, mid = len / 2, cu = s * 0.45f;
            if (b - a > cu * 3) {
                edge_line(E, a, mid - cu * 0.2f, lv, t, c);
                edge_line(E, mid + cu * 0.2f, b, lv, t, c);
                draw_divider_core(xf_scaled(E, (Vector2){ mid, lv }, cu), t, c, false);
            } else edge_line(E, a, b, lv, t, c);
        }
        break;
    }
    case 1: {   // Espirales grandes en las esquinas unidas por una línea
        float S = s * 1.25f, lv = S * 0.04f;
        for (int k = 0; k < 4; k++) draw_corner(corner_xf(e, k, S, 0), t, c);
        for (int k = 0; k < 4; k++) {
            float len;
            Xf E = edge_xf(e, k, &len);
            edge_line(E, 0.75f * S, len - 0.75f * S, lv, t, c);
        }
        break;
    }
    case 2: {   // Dos cintas entrelazadas entre líneas finas, con un aro en cada esquina
        float b = s * 0.2f, A = s * 0.085f;
        rect_lines(e, 0, t * 0.5f, c);
        rect_lines(e, 2 * b, t * 0.5f, c);
        for (int k = 0; k < 4; k++) {
            float len;
            Xf E = edge_xf(e, k, &len);
            edge_line(E, 2 * b, 2 * b, 0, t * 0.5f, c);
            DrawLineEx(X(E, 2 * b, 0), X(E, 2 * b, 2 * b), t * 0.5f, c);
            DrawLineEx(X(E, len - 2 * b, 0), X(E, len - 2 * b, 2 * b), t * 0.5f, c);
            braid(E, 2 * b, len - 2 * b, b, A, s * 0.55f, t, c);
            // Aro de la esquina
            Path R = { 0 };
            for (int i = 0; i <= 40; i++) {
                float a = i / 40.0f * 2 * PI;
                add(&R, b + b * 0.55f * cosf(a), b + b * 0.55f * sinf(a), 1);
            }
            ribbon(&R, E, t, c, 0);
            DrawCircleV(X(E, b, b), t * 1.2f, c);
        }
        break;
    }
    case 3: {   // Ondas: una línea con rizos que dan la vuelta al marco
        float g = s * 0.1f;
        rect_lines(e, 0, t * 0.55f, c);
        rect_lines(e, g, t, c);
        for (int k = 0; k < 4; k++) {
            float len;
            Xf E = edge_xf(e, k, &len);
            Path C = { 0 };
            float R = 0.13f * s;
            spiral(&C, g + R, g + R, R * 1.41f, 225, 1.5f, 1, 0.15f, 1.0f, 0.3f);
            stroke(&C, E, t, c);
            float L = len - 2 * g - 0.7f * s;
            int N = (int)(L / (s * 0.42f));
            if (N < 1) continue;
            float U = L / N, u0 = g + 0.35f * s;
            for (int i = 0; i < N; i++) {
                Path P = { 0 };
                float cx = u0 + i * U + 0.5f * U;
                spiral(&P, cx, g + 0.3f * U, 0.3f * U, -90, 1.3f, 1, 0.2f, 1.0f, 0.35f);
                stroke(&P, E, t, c);
            }
        }
        break;
    }
    case 4: {   // Cuerda trenzada
        float r = s * 0.11f, bw = 2 * r;
        rect_lines(e, 0, t * 0.5f, c);
        rect_lines(e, bw, t * 0.5f, c);
        for (int k = 0; k < 4; k++) {
            float len;
            Xf E = edge_xf(e, k, &len);
            DrawLineEx(X(E, bw, 0), X(E, bw, bw), t * 0.5f, c);
            DrawLineEx(X(E, len - bw, 0), X(E, len - bw, bw), t * 0.5f, c);
            float p = r * 0.9f;
            for (float u = bw + p * 0.2f; u + p * 1.1f <= len - bw; u += p) {
                Path P = { 0 };
                cubic(&P, u, bw * 0.88f, u + p * 0.05f, bw * 0.45f, u + p * 0.6f, bw * 0.1f, u + p * 1.1f, bw * 0.12f, 0.5f, 0.5f, 0.9f, 10, false);
                ribbon(&P, E, t, c, 0);
            }
            DrawCircleV(X(E, r, r), r * 0.55f, outline_of(c));
            DrawCircleV(X(E, r, r), r * 0.42f, c);
        }
        break;
    }
    case 5: {   // Solo esquinas grandes
        float S = s * 1.5f;
        for (int k = 0; k < 4; k++) draw_corner(corner_xf(e, k, S, s * 0.04f), t, c);
        break;
    }
    default: {  // Escalonado (estilo años 20): tres líneas con esquinas en escalón
        float gap = s * 0.09f, cst = s * 0.3f;
        rect_lines(e, 0, t * 0.55f, c);
        for (int i = 1; i <= 2; i++) {
            float d = i * gap, hw = e->w / 2 - d, hh = e->h / 2 - d, cc = cst - (i - 1) * gap * 0.6f;
            if (hw <= cc || hh <= cc) continue;
            Vector2 p[12] = {
                { -hw + cc, -hh }, { hw - cc, -hh }, { hw - cc, -hh + cc }, { hw, -hh + cc },
                { hw, hh - cc }, { hw - cc, hh - cc }, { hw - cc, hh }, { -hw + cc, hh },
                { -hw + cc, hh - cc }, { -hw, hh - cc }, { -hw, -hh + cc }, { -hw + cc, -hh + cc },
            };
            float w = i == 1 ? t : t * 0.6f;
            for (int j = 0; j < 12; j++) {
                DrawLineEx(p[j], p[(j + 1) % 12], w, c);
                DrawRectangleV((Vector2){ p[j].x - w / 2, p[j].y - w / 2 }, (Vector2){ w, w }, c);
            }
        }
        // Rombos en las esquinas
        for (int k = 0; k < 4; k++) {
            Xf T = corner_xf(e, k, s, 0);
            float m = 0.05f + 0.3f * 0.5f + 0.05f;
            float dsz = 0.09f;
            quad(X(T, m, m - dsz), X(T, m + dsz, m), X(T, m, m + dsz), X(T, m - dsz, m), c);
        }
        break;
    }
    }
}

// ------------------------------------------------------------------ Adornos sueltos

// Escala un diseño que mide (2*ex) x (2*ey) para que quepa en la caja
static Xf fit_xf(const Element *e, float ex, float ey, float cy)
{
    float k = fminf(e->w / 2 / ex, e->h / 2 / ey);
    return (Xf){ { 0, -cy * k }, { k, 0 }, { 0, k } };
}

void ornament_draw(const Element *e, Color c)
{
    float t = e->strokeW > 0.5f ? e->strokeW : 0.5f;
    float hw = e->w / 2, hh = e->h / 2;
    int v = ((e->variant % ORNAMENT_VARIANTS) + ORNAMENT_VARIANTS) % ORNAMENT_VARIANTS;
    switch (v) {
    case 0: {   // Espiral
        Path P = { 0 };
        spiral(&P, 0, 0, 0.97f, 0, 2.6f, 1, 0.06f, 1.4f, 0.3f);
        stroke(&P, fit_xf(e, 1, 1, 0), t, c);
        break;
    }
    case 1: {   // Doble espiral en forma de S
        Path P = { 0 };
        spiral(&P, -0.5f, 0, 0.47f, 90, 1.5f, 1, 0.15f, 1.3f, 0.3f);
        reverse_from(&P, 0);
        cubic(&P, -0.5f, 0.47f, 0, 0.47f, 0, -0.47f, 0.5f, -0.47f, 1.3f, 1.3f, 0.4f, 24, true);
        spiral(&P, 0.5f, 0, 0.47f, -90, 1.5f, 1, 0.15f, 1.3f, 0.3f);
        stroke(&P, fit_xf(e, 1, 0.5f, 0), t, c);
        break;
    }
    case 2: {   // Volutas en forma de corazón
        Xf T = fit_xf(e, 0.96f, 0.85f, 0.12f);
        for (int m = 0; m < 2; m++) {
            Path P = { 0 };
            cubic(&P, 0, 0.95f, -0.3f, 0.7f, -0.95f, 0.25f, -0.9f, -0.3f, 0.4f, 1.3f, 0, 24, false);
            spiral(&P, -0.5f, -0.3f, 0.4f, 180, 1.4f, 1, 0.2f, 1.3f, 0.3f);
            if (m) mirror_u(&P);
            stroke(&P, T, t, c);
        }
        break;
    }
    case 3: {   // Separador: rombo, volutas y dos colas
        float k = hh / 0.45f, half = hw / k;
        Xf T = { { 0, 0 }, { k, 0 }, { 0, k } };
        for (int sx = -1; sx <= 1; sx += 2) {
            Path P = { 0 };
            cubic(&P, 0.18f, 0, 0.4f, 0, half * 0.7f, 0, half - 0.02f, 0, 1.0f, 0.2f, 0, 16, false);
            if (sx < 0) mirror_u(&P);
            stroke(&P, T, t, c);
        }
        draw_divider_core(T, t, c, true);
        break;
    }
    case 4: {   // Esquina
        float m = fminf(e->w, e->h);
        draw_corner((Xf){ { -hw, -hh }, { m, 0 }, { 0, m } }, t, c);
        break;
    }
    case 5: {   // Lazo de tres hojas entrelazado
        Path P = { 0 };
        int n = 240;
        for (int i = 0; i <= n; i++) {
            float a = (float)i / n * 2 * PI + 0.3f;
            add(&P, (sinf(a) + 2 * sinf(2 * a)) / 3.0f, (cosf(a) - 2 * cosf(2 * a)) / 3.0f + 0.05f, 1);
        }
        ribbon(&P, fit_xf(e, 1.02f, 1.02f, 0), t, c, 5);
        break;
    }
    case 6: {   // Rizos: una línea que da vueltas sobre sí misma
        float b = 1.2f, aa = 0.35f;
        int N = (int)roundf(((hw / hh) * (b + 0.1f) - b) / (PI * aa));
        if (N < 2) N = 2;
        if (N > 40) N = 40;
        float ex = N * PI * aa + b;
        Xf T = fit_xf(e, ex, b + 0.1f, 0);
        Path P = { 0 };
        int steps = N * 40;
        if (steps > OP - 1) steps = OP - 1;
        for (int i = 0; i <= steps; i++) {
            float tt = (float)i / steps * N * 2 * PI;
            add(&P, aa * tt - b * sinf(tt) - N * PI * aa, -b * cosf(tt), 1);
        }
        ribbon(&P, T, t, c, 4);
        break;
    }
    default: {  // Trenza
        Xf E = { { -hw, 0 }, { 1, 0 }, { 0, 1 } };
        braid(E, t, e->w - t, 0, hh * 0.6f, hh * 1.2f, t, c);
        break;
    }
    }
}

// ------------------------------------------------------------------ Letreros

// Línea central de la cinta: y según x (de -X0 a X0)
static float banner_y(const Element *e, float x, float X0)
{
    float sag = e->bend * e->h * 0.32f;
    float u = x / X0;
    if (e->variant % BANNER_VARIANTS == 3) return sag * 0.5f * sinf(u * PI * 1.5f);
    return -sag * (1 - u * u) + sag * 0.5f;
}

static void band(const Element *e, float xa, float xb, float X0, float yoff, float bh, Color fill, Color stroke, float sw)
{
    const int N = 48;
    Vector2 top[N + 1], bot[N + 1];
    for (int i = 0; i <= N; i++) {
        float x = xa + (xb - xa) * i / N;
        float y = banner_y(e, x, X0) + yoff;
        top[i] = (Vector2){ x, y - bh / 2 };
        bot[i] = (Vector2){ x, y + bh / 2 };
    }
    for (int i = 0; i < N; i++) quad(top[i], top[i + 1], bot[i + 1], bot[i], fill);
    if (sw > 0) {
        for (int i = 0; i < N; i++) { DrawLineEx(top[i], top[i + 1], sw, stroke); DrawLineEx(bot[i], bot[i + 1], sw, stroke); }
        DrawLineEx(top[0], bot[0], sw, stroke);
        DrawLineEx(top[N], bot[N], sw, stroke);
        // Línea fina por dentro, como en las cintas antiguas
        if (bh > sw * 8) {
            float in = bh * 0.13f;
            for (int i = 0; i < N; i++) {
                DrawLineEx((Vector2){ top[i].x, top[i].y + in }, (Vector2){ top[i + 1].x, top[i + 1].y + in }, sw * 0.5f, stroke);
                DrawLineEx((Vector2){ bot[i].x, bot[i].y - in }, (Vector2){ bot[i + 1].x, bot[i + 1].y - in }, sw * 0.5f, stroke);
            }
        }
    }
}

static void poly(const Vector2 *p, int n, Color fill, Color stroke, float sw)
{
    for (int i = 1; i < n - 1; i++) tri(p[0], p[i], p[i + 1], fill);
    if (sw > 0) for (int i = 0; i < n; i++) DrawLineEx(p[i], p[(i + 1) % n], sw, stroke);
}

// Texto que sigue la línea central
static void banner_text(const Element *e, float X0, float yoff, float bh)
{
    if (!e->text[0]) return;
    Font f = font_get(e->font);
    if (f.texture.id == 0 || f.glyphCount == 0) return;
    // Largo de la curva
    const int N = 200;
    float acc[N + 1];
    Vector2 pt[N + 1];
    acc[0] = 0;
    for (int i = 0; i <= N; i++) {
        float x = -X0 + 2 * X0 * i / N;
        pt[i] = (Vector2){ x, banner_y(e, x, X0) + yoff };
        if (i > 0) acc[i] = acc[i - 1] + Vector2Distance(pt[i - 1], pt[i]);
    }
    float total = acc[N];
    float size = e->fontSize > 4 ? e->fontSize : bh * 0.55f;
    if (size > bh * 0.8f) size = bh * 0.8f;
    float spacing = e->letterSpacing * size;
    // Ancho del texto (una sola línea)
    float scale = size / f.baseSize, width = 0;
    int cps[TEXT_MAX];
    float adv[TEXT_MAX];
    int n = 0;
    for (const char *s = e->text; *s && n < TEXT_MAX;) {
        int bytes = 0;
        int cp = GetCodepointNext(s, &bytes);
        s += bytes > 0 ? bytes : 1;
        if (cp == '\n' || cp == '\r') cp = ' ';
        int gi = GetGlyphIndex(f, cp);
        float a = (f.glyphs[gi].advanceX ? f.glyphs[gi].advanceX : f.recs[gi].width) * scale;
        cps[n] = cp; adv[n] = a; n++;
        width += a + spacing;
    }
    width -= spacing;
    float maxW = total * (e->variant % BANNER_VARIANTS == 2 ? 0.8f : 0.9f);
    if (width > maxW && width > 0) {
        float k = maxW / width;
        size *= k; spacing *= k; width *= k;
        for (int i = 0; i < n; i++) adv[i] *= k;
    }
    float asc = font_ascent_ratio(e->font);
    float s0 = (total - width) / 2;
    Color tc = e->textColor;
    tc.a = (unsigned char)(tc.a * e->opacity);
    int j = 0;
    for (int i = 0; i < n; i++) {
        float sc = s0 + adv[i] / 2;
        s0 += adv[i] + spacing;
        if (cps[i] == ' ') continue;
        while (j < N - 1 && acc[j + 1] < sc) j++;
        while (j > 0 && acc[j] > sc) j--;
        float seg = acc[j + 1] - acc[j];
        float tt = seg > 0 ? (sc - acc[j]) / seg : 0;
        Vector2 p = Vector2Lerp(pt[j], pt[j + 1], tt);
        float ang = atan2f(pt[j + 1].y - pt[j].y, pt[j + 1].x - pt[j].x) * RAD2DEG;
        rlPushMatrix();
        rlTranslatef(p.x, p.y, 0);
        rlRotatef(ang, 0, 0, 1);
        DrawTextCodepoint(f, cps[i], (Vector2){ -adv[i] / 2, -size * asc + size * 0.36f }, size, tc);
        rlPopMatrix();
    }
}

void banner_draw(const Element *e, Color fill, Color edge, bool withText)
{
    float hw = e->w / 2, hh = e->h / 2;
    int v = ((e->variant % BANNER_VARIANTS) + BANNER_VARIANTS) % BANNER_VARIANTS;
    float sw = e->strokeW;
    float bh = e->h * 0.5f;
    float sag = fabsf(e->bend) * e->h * 0.32f;
    if (bh + sag > e->h * 0.98f) bh = e->h * 0.98f - sag;
    if (bh < 4) bh = 4;
    Color back = darker(fill, 0.78f), fold = darker(fill, 0.55f);
    float X0 = hw;
    float yoff = 0;
    switch (v) {
    case 0:
    case 3: {   // Cinta con colas dobladas por detrás
        X0 = hw * 0.78f;
        float drop = bh * 0.38f;
        yoff = -drop / 2;
        for (int sx = -1; sx <= 1; sx += 2) {
            float xi = sx * X0 * 0.84f, xo = sx * hw;
            float yi = banner_y(e, sx * X0, X0) + yoff + drop;
            float yo = yi + sag * 0.15f * (e->bend >= 0 ? 1 : -1);
            float notch = bh * 0.38f;
            Vector2 tail[5] = { { xi, yi - bh / 2 }, { xo, yo - bh / 2 }, { xo - sx * notch, yo }, { xo, yo + bh / 2 }, { xi, yi + bh / 2 } };
            poly(tail, 5, back, edge, sw);
            // El pliegue
            float yb = banner_y(e, sx * X0, X0) + yoff + bh / 2;
            Vector2 f3[3] = { { sx * X0, yb }, { xi, yi + bh / 2 }, { xi, yb } };
            poly(f3, 3, fold, edge, sw);
        }
        band(e, -X0, X0, X0, yoff, bh, fill, edge, sw);
        break;
    }
    case 1: {   // Cinta con picos en los extremos
        X0 = hw;
        float n = bh * 0.4f, xi = X0 - n;
        band(e, -xi, xi, X0, 0, bh, fill, edge, 0);
        for (int sx = -1; sx <= 1; sx += 2) {
            float yi = banner_y(e, sx * xi, X0), yo = banner_y(e, sx * X0, X0);
            Vector2 A = { sx * xi, yi - bh / 2 }, B = { sx * X0, yo - bh / 2 }, M = { sx * xi, (yi + yo) / 2 };
            Vector2 C = { sx * X0, yo + bh / 2 }, D = { sx * xi, yi + bh / 2 };
            tri(A, B, M, fill);
            tri(M, C, D, fill);
            if (sw > 0) { DrawLineEx(A, B, sw, edge); DrawLineEx(B, M, sw, edge); DrawLineEx(M, C, sw, edge); DrawLineEx(C, D, sw, edge); }
        }
        if (sw > 0) {
            const int N = 48;
            for (int i = 0; i < N; i++) {
                float x0 = -xi + 2 * xi * i / N, x1 = -xi + 2 * xi * (i + 1) / N;
                float y0 = banner_y(e, x0, X0), y1 = banner_y(e, x1, X0);
                DrawLineEx((Vector2){ x0, y0 - bh / 2 }, (Vector2){ x1, y1 - bh / 2 }, sw, edge);
                DrawLineEx((Vector2){ x0, y0 + bh / 2 }, (Vector2){ x1, y1 + bh / 2 }, sw, edge);
            }
        }
        break;
    }
    case 2: {   // Pergamino con los extremos enrollados
        X0 = hw * 0.88f;
        band(e, -X0, X0, X0, 0, bh, fill, edge, sw);
        for (int sx = -1; sx <= 1; sx += 2) {
            float x = sx * X0, y = banner_y(e, x, X0);
            float rw = bh * 0.22f, rh = bh * 0.62f;
            Rectangle r = { x - rw, y - rh, rw * 2, rh * 2 };
            DrawRectangleRounded(r, 1, 12, back);
            if (sw > 0) DrawRectangleRoundedLinesEx(r, 1, 12, sw, edge);
            DrawEllipse((int)x, (int)(y + rh - rw * 0.8f), rw * 0.9f, rw * 0.6f, fold);
            Path P = { 0 };
            spiral(&P, 0, 0, 1, 0, 1.6f, sx > 0 ? 1 : -1, 0.15f, 1, 0.5f);
            Xf T = { { x, y + rh - rw * 0.8f }, { rw * 0.6f, 0 }, { 0, rw * 0.6f } };
            stroke(&P, T, fmaxf(sw, 1.5f), edge);
        }
        break;
    }
    default: {  // Etiqueta redondeada
        X0 = hw - bh / 2;
        band(e, -X0, X0, X0, 0, bh, fill, BLANK, 0);
        for (int sx = -1; sx <= 1; sx += 2) {
            Vector2 c = { sx * X0, banner_y(e, sx * X0, X0) };
            DrawCircleSector(c, bh / 2, sx > 0 ? -90 : 90, sx > 0 ? 90 : 270, 24, fill);
            if (sw > 0) DrawRing(c, bh / 2 - sw / 2, bh / 2 + sw / 2, sx > 0 ? -90 : 90, sx > 0 ? 90 : 270, 24, edge);
        }
        if (sw > 0) {
            const int N = 48;
            for (int i = 0; i < N; i++) {
                float x0 = -X0 + 2 * X0 * i / N, x1 = -X0 + 2 * X0 * (i + 1) / N;
                float y0 = banner_y(e, x0, X0), y1 = banner_y(e, x1, X0);
                DrawLineEx((Vector2){ x0, y0 - bh / 2 }, (Vector2){ x1, y1 - bh / 2 }, sw, edge);
                DrawLineEx((Vector2){ x0, y0 + bh / 2 }, (Vector2){ x1, y1 + bh / 2 }, sw, edge);
            }
        }
        break;
    }
    }
    (void)hh;
    if (withText) banner_text(e, X0, yoff, bh);
}
