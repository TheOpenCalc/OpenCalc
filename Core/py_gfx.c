/* Outils communs aux modules Python graphiques (voir headers/py_gfx.h).
 * Tout le dessin passe par les primitives oc_draw_* (pika_wrapper.h). */
#include "py_gfx.h"
#include "pika_wrapper.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Etat de l'ecran ---------- */

static int g_used = 0;  /* le script a dessine */
static int g_dirty = 0; /* dessin pas encore montre (attente d'une touche) */
static unsigned g_last_flush = 0;

void pyg_begin(void)
{
    if (!g_used)
    {
        g_used = 1;
        oc_draw_begin();
        g_last_flush = oc_draw_ticks_ms();
    }
    g_dirty = 1;
}

void pyg_shown(void)
{
    g_dirty = 0;
}

void pyg_flush_throttled(void)
{
    unsigned now = oc_draw_ticks_ms();
    if (now - g_last_flush >= 40)
    {
        oc_draw_show();
        g_last_flush = now;
    }
}

void pyg_show_and_wait(void)
{
    pyg_begin();
    oc_draw_wait_key(); /* envoie l'image puis attend */
    pyg_shown();
}

int pika_draw_used(void)
{
    return g_used && g_dirty;
}

void pika_draw_reset(void)
{
    g_used = 0;
    g_dirty = 0;
    py_turtle_reset();
    py_plot_reset();
}

/* ---------- Arguments ---------- */

int pyg_num(Arg *a, double *out)
{
    if (a == NULL)
        return 0;
    switch (arg_getType(a))
    {
    case ARG_TYPE_INT:
        *out = (double)arg_getInt(a);
        return 1;
    case ARG_TYPE_BOOL:
        *out = arg_getBool(a) ? 1.0 : 0.0;
        return 1;
    case ARG_TYPE_FLOAT:
        *out = (double)arg_getFloat(a);
        return 1;
    default:
        return 0;
    }
}

int pyg_tuple_num(PikaTuple *t, int i, double *out)
{
    if (t == NULL || i < 0 || i >= (int)pikaTuple_getSize(t))
        return 0;
    return pyg_num(pikaTuple_getArg(t, i), out);
}

int pyg_seq(Arg *a, double **out)
{
    *out = NULL;
    if (a == NULL)
        return -1;
    double v;
    if (pyg_num(a, &v))
    {
        *out = (double *)malloc(sizeof(double));
        if (*out == NULL)
            return -1;
        (*out)[0] = v;
        return 1;
    }
    int is_list = arg_isList(a), is_tuple = arg_isTuple(a);
    if (!is_list && !is_tuple)
        return -1;
    PikaObj *o = arg_getObj(a);
    int n = is_list ? (int)pikaList_getSize(o) : (int)pikaTuple_getSize(o);
    *out = (double *)malloc(sizeof(double) * (n > 0 ? n : 1));
    if (*out == NULL)
        return -1;
    for (int i = 0; i < n; i++)
    {
        Arg *e = is_list ? pikaList_get(o, i) : pikaTuple_getArg(o, i);
        if (!pyg_num(e, &(*out)[i]))
        {
            free(*out);
            *out = NULL;
            return -1;
        }
    }
    return n;
}

int pyg_nargs(Args *a)
{
    PikaTuple *t = args_getTuple(a, "a");
    return t ? (int)pikaTuple_getSize(t) : 0;
}

Arg *pyg_arg(Args *a, int i)
{
    PikaTuple *t = args_getTuple(a, "a");
    if (t == NULL || i < 0 || i >= (int)pikaTuple_getSize(t))
        return NULL;
    return pikaTuple_getArg(t, i);
}

Arg *pyg_kw(Args *a, const char *name)
{
    PikaDict *k = args_getDict(a, "kw");
    if (k == NULL || !pikaDict_isArgExist(k, (char *)name))
        return NULL;
    return pikaDict_get(k, (char *)name);
}

Arg *pyg_arg_or_kw(Args *a, int i, const char *name)
{
    Arg *v = pyg_arg(a, i);
    return v ? v : pyg_kw(a, name);
}

void pyg_error(PikaObj *self, const char *msg)
{
    obj_setErrorCode(self, PIKA_RES_ERR_INVALID_PARAM);
    obj_setSysOut(self, (char *)"%s", msg);
}

/* ---------- Couleurs ---------- */

int pyg_rgb(double r, double g, double b, int scale255)
{
    if (!scale255 && (r > 1.0 || g > 1.0 || b > 1.0))
        scale255 = 1;
    if (!scale255)
    {
        r *= 255.0;
        g *= 255.0;
        b *= 255.0;
    }
    int R = (int)lround(r), G = (int)lround(g), B = (int)lround(b);
    R = R < 0 ? 0 : (R > 255 ? 255 : R);
    G = G < 0 ? 0 : (G > 255 ? 255 : G);
    B = B < 0 ? 0 : (B > 255 ? 255 : B);
    return ((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3);
}

typedef struct
{
    const char *name;
    unsigned rgb;
} named_color;

/* Noms usuels (turtle, matplotlib). Les lettres seules et C0..C9 suivent
 * matplotlib. */
static const named_color k_colors[] = {
    {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000},
    {"green", 0x008000}, {"blue", 0x0000FF}, {"yellow", 0xFFFF00},
    {"cyan", 0x00FFFF}, {"magenta", 0xFF00FF}, {"orange", 0xFFA500},
    {"purple", 0x800080}, {"pink", 0xFFC0CB}, {"brown", 0xA52A2A},
    {"gray", 0x808080}, {"grey", 0x808080}, {"lightgray", 0xD3D3D3},
    {"lightgrey", 0xD3D3D3}, {"darkgray", 0xA9A9A9}, {"darkgrey", 0xA9A9A9},
    {"lightblue", 0xADD8E6}, {"darkblue", 0x00008B}, {"navy", 0x000080},
    {"lightgreen", 0x90EE90}, {"darkgreen", 0x006400}, {"lime", 0x00FF00},
    {"darkred", 0x8B0000}, {"gold", 0xFFD700}, {"violet", 0xEE82EE},
    {"turquoise", 0x40E0D0}, {"beige", 0xF5F5DC}, {"olive", 0x808000},
    {"teal", 0x008080}, {"maroon", 0x800000}, {"silver", 0xC0C0C0},
    {"b", 0x0000FF}, {"g", 0x008000}, {"r", 0xFF0000}, {"c", 0x00BFBF},
    {"m", 0xBF00BF}, {"y", 0xBFBF00}, {"k", 0x000000}, {"w", 0xFFFFFF},
    {"C0", 0x1F77B4}, {"C1", 0xFF7F0E}, {"C2", 0x2CA02C}, {"C3", 0xD62728},
    {"C4", 0x9467BD}, {"C5", 0x8C564B}, {"C6", 0xE377C2}, {"C7", 0x7F7F7F},
    {"C8", 0xBCBD22}, {"C9", 0x17BECF},
};

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int pyg_color_name(const char *s, int *out)
{
    if (s == NULL)
        return 0;
    if (s[0] == '#' && strlen(s) == 7)
    {
        unsigned v = 0;
        for (int i = 1; i < 7; i++)
        {
            int h = hexval(s[i]);
            if (h < 0)
                return 0;
            v = (v << 4) | (unsigned)h;
        }
        *out = pyg_rgb((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, 1);
        return 1;
    }
    if (strncmp(s, "tab:", 4) == 0)
        s += 4;
    for (size_t i = 0; i < sizeof(k_colors) / sizeof(k_colors[0]); i++)
    {
        /* Comparaison sans tenir compte de la casse, sauf pour C0..C9. */
        const char *a = k_colors[i].name, *b = s;
        while (*a && *b && (*a == *b || (*a >= 'a' && *a <= 'z' && *a - 32 == *b)))
        {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
        {
            unsigned v = k_colors[i].rgb;
            *out = pyg_rgb((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, 1);
            return 1;
        }
    }
    return 0;
}

int pyg_color(Arg *a, int scale255, int *out)
{
    if (a == NULL)
        return 0;
    ArgType t = arg_getType(a);
    if (t == ARG_TYPE_STRING)
        return pyg_color_name(arg_getStr(a), out);
    if (t == ARG_TYPE_INT)
    {
        *out = (int)(arg_getInt(a) & 0xFFFF);
        return 1;
    }
    double *v = NULL;
    int n = pyg_seq(a, &v);
    if (n != 3)
    {
        free(v);
        return 0;
    }
    *out = pyg_rgb(v[0], v[1], v[2], scale255);
    free(v);
    return 1;
}

/* ---------- Trace ---------- */

/* Le motif des pointilles continue d'un segment au suivant : une courbe
 * faite de petits segments reste visiblement pointillee. */
static int g_dash_step = 0;

void pyg_dash_reset(void)
{
    g_dash_step = 0;
}

static int g_cx0 = 0, g_cy0 = 0, g_cx1 = PYG_W - 1, g_cy1 = PYG_H - 1;

void pyg_set_clip(int x0, int y0, int x1, int y1)
{
    g_cx0 = x0 < 0 ? 0 : x0;
    g_cy0 = y0 < 0 ? 0 : y0;
    g_cx1 = x1 > PYG_W - 1 ? PYG_W - 1 : x1;
    g_cy1 = y1 > PYG_H - 1 ? PYG_H - 1 : y1;
}

void pyg_reset_clip(void)
{
    pyg_set_clip(0, 0, PYG_W - 1, PYG_H - 1);
}

void pyg_rect(double x0, double y0, double x1, double y1, int color)
{
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1))
        return;
    if (x0 > x1)
    {
        double t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y0 > y1)
    {
        double t = y0;
        y0 = y1;
        y1 = t;
    }
    if (x0 < g_cx0)
        x0 = g_cx0;
    if (y0 < g_cy0)
        y0 = g_cy0;
    if (x1 > g_cx1 + 1)
        x1 = g_cx1 + 1;
    if (y1 > g_cy1 + 1)
        y1 = g_cy1 + 1;
    int X0 = (int)lround(x0), Y0 = (int)lround(y0), X1 = (int)lround(x1), Y1 = (int)lround(y1);
    if (X1 <= X0 && x1 > x0)
        X1 = X0 + 1; /* barre tres fine : au moins un pixel */
    if (X1 > X0 && Y1 > Y0)
        oc_draw_rect(X0, Y0, X1 - X0, Y1 - Y0, color);
}

/* Rognage de Liang-Barsky sur [lo, hi] x [lo2, hi2]. */
static int clip_test(double p, double q, double *t0, double *t1)
{
    if (p == 0.0)
        return q >= 0.0;
    double r = q / p;
    if (p < 0.0)
    {
        if (r > *t1)
            return 0;
        if (r > *t0)
            *t0 = r;
    }
    else
    {
        if (r < *t0)
            return 0;
        if (r < *t1)
            *t1 = r;
    }
    return 1;
}

void pyg_line(double x0, double y0, double x1, double y1, int color, int width, int dash)
{
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1))
        return;
    if (width < 1)
        width = 1;
    if (width > 40)
        width = 40;
    double xmin = g_cx0, xmax = g_cx1, ymin = g_cy0, ymax = g_cy1;
    double dx = x1 - x0, dy = y1 - y0, t0 = 0.0, t1 = 1.0;
    if (!clip_test(-dx, x0 - xmin, &t0, &t1) || !clip_test(dx, xmax - x0, &t0, &t1) ||
        !clip_test(-dy, y0 - ymin, &t0, &t1) || !clip_test(dy, ymax - y0, &t0, &t1))
        return;
    int X0 = (int)lround(x0 + t0 * dx), Y0 = (int)lround(y0 + t0 * dy);
    int X1 = (int)lround(x0 + t1 * dx), Y1 = (int)lround(y0 + t1 * dy);

    int adx = abs(X1 - X0), sx = X0 < X1 ? 1 : -1;
    int ady = -abs(Y1 - Y0), sy = Y0 < Y1 ? 1 : -1;
    int err = adx + ady, off = (width - 1) / 2;
    for (;;)
    {
        int on = 1;
        if (dash == 1)
            on = (g_dash_step % 10) < 6;
        else if (dash == 2)
            on = (g_dash_step % 5) < 2;
        if (on)
            oc_draw_rect(X0 - off, Y0 - off, width, width, color);
        if (X0 == X1 && Y0 == Y1)
            break;
        int e2 = 2 * err;
        if (e2 >= ady)
        {
            err += ady;
            X0 += sx;
        }
        if (e2 <= adx)
        {
            err += adx;
            Y0 += sy;
        }
        g_dash_step++;
    }
}

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

void pyg_fill_poly(const float *xs, const float *ys, int n, int color)
{
    if (n < 3)
        return;
    float ymin = ys[0], ymax = ys[0];
    for (int i = 1; i < n; i++)
    {
        if (ys[i] < ymin)
            ymin = ys[i];
        if (ys[i] > ymax)
            ymax = ys[i];
    }
    int ya = (int)ceilf(ymin), yb = (int)floorf(ymax);
    if (ya < g_cy0)
        ya = g_cy0;
    if (yb > g_cy1)
        yb = g_cy1;
    float *cross = (float *)malloc(sizeof(float) * n);
    if (cross == NULL)
        return;
    for (int y = ya; y <= yb; y++)
    {
        float yc = (float)y;
        int k = 0;
        for (int i = 0, j = n - 1; i < n; j = i++)
        {
            if ((ys[i] <= yc && yc < ys[j]) || (ys[j] <= yc && yc < ys[i]))
                cross[k++] = xs[i] + (yc - ys[i]) * (xs[j] - xs[i]) / (ys[j] - ys[i]);
        }
        qsort(cross, k, sizeof(float), cmp_float);
        for (int i = 0; i + 1 < k; i += 2)
        {
            int xa = (int)ceilf(cross[i] - 0.5f), xb = (int)floorf(cross[i + 1] + 0.5f);
            if (xa < g_cx0)
                xa = g_cx0;
            if (xb > g_cx1)
                xb = g_cx1;
            if (xb >= xa)
                oc_draw_rect(xa, y, xb - xa + 1, 1, color);
        }
    }
    free(cross);
}

void pyg_disc(double x, double y, double r, int color)
{
    if (!isfinite(x) || !isfinite(y) || x < -500 || x > 820 || y < -500 || y > 740)
        return;
    int R = (int)lround(r);
    if (R < 1)
        oc_draw_rect((int)lround(x), (int)lround(y), 1, 1, color);
    else
        oc_draw_circle((int)lround(x), (int)lround(y), R, color, 1);
}

void pyg_text(double x, double y, const char *s, int color, int size)
{
    if (s == NULL || !isfinite(x) || !isfinite(y))
        return;
    int X = (int)lround(x), Y = (int)lround(y);
    if (Y < 0 || Y > PYG_H - 8 * size || X > PYG_W - 1)
        return;
    if (size == 1)
        oc_draw_text(s, X < 0 ? 0 : X, Y, color);
    else
        oc_draw_text_size(s, X < 0 ? 0 : X, Y, color, size);
}

int pyg_text_w(const char *s, int size)
{
    return s ? (int)strlen(s) * 5 * size : 0;
}

void pyg_fmt(double v, char *buf, int cap)
{
    if (fabs(v) < 1e-12)
        v = 0.0;
    snprintf(buf, cap, "%.4g", v);
}
