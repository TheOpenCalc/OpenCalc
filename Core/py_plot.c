/* Module Python "matplotlib.pyplot" : sous-ensemble utilise au lycee et
 * propose par la NumWorks (plot, scatter, bar, hist, text, arrow, axis,
 * grid, show...). Importable par "import matplotlib.pyplot as plt".
 *
 * Les traces sont memorises jusqu'a show(), qui calcule les bornes, dessine
 * la figure et attend une touche. Une figure non affichee a la fin du
 * script est affichee automatiquement (py_plot_autoshow). */
#include "py_gfx.h"
#include "pika_wrapper.h"
#include "TinyObj.h"
#include "builtins.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ELEMS 1024 /* tableau agrandi a la demande */
#define MAX_POINTS 4096 /* tous traces confondus */
#define MAX_CATS 16

enum
{
    K_LINE,  /* plot : segments et/ou marqueurs */
    K_BAR,   /* bar, hist : x = centre, y = hauteur */
    K_TEXT,  /* text */
    K_ARROW, /* arrow : x[0], y[0], x[1] = dx, y[1] = dy */
    K_HLINE, /* axhline */
    K_VLINE, /* axvline */
};

typedef struct
{
    int kind;
    int color;
    int n;
    float *x, *y;
    float width;  /* barres : largeur ; lignes : epaisseur */
    float bottom; /* barres */
    char marker;  /* 0 : aucun */
    int line;     /* 0 aucun trait, 1 plein, 2 tirets, 3 points */
    int msize;    /* rayon des marqueurs */
    char *text;
} elem;

typedef struct
{
    elem *e;
    int ne, cap;
    int npoints;
    int cycle;
    int axis_on;
    int equal;
    int grid;
    int has_xlim, has_ylim;
    double xlim[2], ylim[2];
    char title[48], xlabel[48], ylabel[48];
    int ncats;
    char cats[MAX_CATS][12];
} figure;

static figure F;

static const unsigned k_cycle[10] = {0x1F77B4, 0xFF7F0E, 0x2CA02C, 0xD62728, 0x9467BD,
                                     0x8C564B, 0xE377C2, 0x7F7F7F, 0xBCBD22, 0x17BECF};

static int next_color(void)
{
    unsigned v = k_cycle[F.cycle++ % 10];
    return pyg_rgb((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, 1);
}

static void fig_clear(void)
{
    for (int i = 0; i < F.ne; i++)
    {
        free(F.e[i].x);
        free(F.e[i].y);
        free(F.e[i].text);
    }
    free(F.e);
    memset(&F, 0, sizeof(F));
    F.axis_on = 1;
}

void py_plot_reset(void)
{
    fig_clear();
}

/* ---------- Ajout d'elements ---------- */

static elem *add_elem(PikaObj *self, int kind, int n)
{
    if (F.ne >= MAX_ELEMS)
    {
        pyg_error(self, "MemoryError: trop de traces (show() vide la figure)");
        return NULL;
    }
    if (F.ne >= F.cap)
    {
        int cap = F.cap ? F.cap * 2 : 16;
        elem *ne = (elem *)realloc(F.e, sizeof(elem) * cap);
        if (ne == NULL)
        {
            pyg_error(self, "MemoryError: figure");
            return NULL;
        }
        F.e = ne;
        F.cap = cap;
    }
    if (F.npoints + n > MAX_POINTS)
    {
        pyg_error(self, "MemoryError: trop de points dans la figure (4096 au plus)");
        return NULL;
    }
    elem *e = &F.e[F.ne];
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->n = n;
    e->width = 1;
    if (n > 0)
    {
        e->x = (float *)malloc(sizeof(float) * n);
        e->y = (float *)malloc(sizeof(float) * n);
        if (e->x == NULL || e->y == NULL)
        {
            free(e->x);
            free(e->y);
            e->x = e->y = NULL;
            pyg_error(self, "MemoryError: figure");
            return NULL;
        }
    }
    F.ne++;
    F.npoints += n;
    return e;
}

static int is_str(Arg *a) { return a && arg_getType(a) == ARG_TYPE_STRING; }
static int is_none(Arg *a) { return a == NULL || arg_getType(a) == ARG_TYPE_NONE; }

static char *dup_str(const char *s)
{
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (d)
        memcpy(d, s, n + 1);
    return d;
}

static const char *as_text(PikaObj *self, Arg *a)
{
    if (a == NULL)
        return "";
    if (is_str(a))
        return arg_getStr(a);
    const char *s = builtins_str(self, a);
    return s ? s : "";
}

/* Format "r-", "bo", "g--", "k.", ":"... */
static void parse_fmt(const char *s, int *color, int *has_color, char *marker, int *line)
{
    int ls = -1;
    *marker = 0;
    for (const char *p = s; *p; p++)
    {
        if (p[0] == '-' && p[1] == '-')
        {
            ls = 2;
            p++;
        }
        else if (p[0] == '-' && p[1] == '.')
        {
            ls = 2;
            p++;
        }
        else if (*p == '-')
            ls = 1;
        else if (*p == ':')
            ls = 3;
        else if (strchr("o.,+x*s^vd<>Dph|_", *p))
            *marker = *p;
        else
        {
            char c[2] = {*p, 0};
            if (pyg_color_name(c, color))
                *has_color = 1;
        }
    }
    *line = ls >= 0 ? ls : (*marker ? 0 : 1);
}

static int parse_linestyle(const char *s)
{
    if (!strcmp(s, "--") || !strcmp(s, "dashed") || !strcmp(s, "-.") || !strcmp(s, "dashdot"))
        return 2;
    if (!strcmp(s, ":") || !strcmp(s, "dotted"))
        return 3;
    if (!strcmp(s, "") || !strcmp(s, " ") || !strcmp(s, "None") || !strcmp(s, "none"))
        return 0;
    return 1;
}

/* Options communes : color/c, linestyle/ls, marker, linewidth/lw. */
static int apply_kw(PikaObj *self, Args *a, elem *e, int *has_color)
{
    Arg *c = pyg_kw(a, "color");
    if (c == NULL)
        c = pyg_kw(a, "c");
    if (c && !is_none(c))
    {
        if (!pyg_color(c, 0, &e->color))
        {
            pyg_error(self, "ValueError: couleur inconnue (ex. 'red', 'b', '#ff0000')");
            return 0;
        }
        *has_color = 1;
    }
    Arg *ls = pyg_kw(a, "linestyle");
    if (ls == NULL)
        ls = pyg_kw(a, "ls");
    if (is_str(ls))
        e->line = parse_linestyle(arg_getStr(ls));
    Arg *mk = pyg_kw(a, "marker");
    if (is_str(mk))
    {
        const char *m = arg_getStr(mk);
        e->marker = (m[0] && strcmp(m, "None") && strcmp(m, "none")) ? m[0] : 0;
    }
    Arg *lw = pyg_kw(a, "linewidth");
    if (lw == NULL)
        lw = pyg_kw(a, "lw");
    double w;
    if (lw && pyg_num(lw, &w))
        e->width = (float)(w < 1 ? 1 : (w > 8 ? 8 : w));
    return 1;
}

/* Deux suites de meme longueur -> element. */
static elem *add_xy(PikaObj *self, int kind, const double *x, const double *y, int n)
{
    elem *e = add_elem(self, kind, n);
    if (e == NULL)
        return NULL;
    for (int i = 0; i < n; i++)
    {
        e->x[i] = x ? (float)x[i] : (float)i;
        e->y[i] = (float)y[i];
    }
    return e;
}

static void err_seq(PikaObj *self, const char *fn)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "TypeError: %s() attend des listes de nombres", fn);
    pyg_error(self, msg);
}

static void err_len(PikaObj *self, const char *fn)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "ValueError: %s() : x et y n'ont pas la meme longueur", fn);
    pyg_error(self, msg);
}

/* plot([x], y, [fmt], ..., color=, linestyle=, marker=, linewidth=) */
static void m_plot(PikaObj *self, Args *a)
{
    int na = pyg_nargs(a), i = 0;
    if (na == 0)
        return;
    while (i < na)
    {
        double *x = NULL, *y = NULL;
        int nx = pyg_seq(pyg_arg(a, i), &x);
        if (nx < 0)
        {
            err_seq(self, "plot");
            return;
        }
        i++;
        int ny = -1;
        if (i < na && !is_str(pyg_arg(a, i)))
        {
            ny = pyg_seq(pyg_arg(a, i), &y);
            if (ny < 0)
            {
                free(x);
                err_seq(self, "plot");
                return;
            }
            i++;
        }
        if (ny < 0)
        {
            y = x; /* plot(y) : x = 0, 1, 2... */
            ny = nx;
            x = NULL;
        }
        else if (nx != ny)
        {
            free(x);
            free(y);
            err_len(self, "plot");
            return;
        }
        int color = 0, has_color = 0;
        char marker = 0;
        int line = 1;
        if (i < na && is_str(pyg_arg(a, i)))
        {
            parse_fmt(arg_getStr(pyg_arg(a, i)), &color, &has_color, &marker, &line);
            i++;
        }
        elem *e = add_xy(self, K_LINE, x, y, ny);
        free(x);
        free(y);
        if (e == NULL)
            return;
        e->color = color;
        e->marker = marker;
        e->line = line;
        e->msize = 2;
        if (!apply_kw(self, a, e, &has_color))
            return;
        /* Un point seul sans marqueur ne se verrait pas. */
        if (e->n == 1 && !e->marker)
            e->marker = 'o';
        if (!has_color)
            e->color = next_color();
    }
}

/* scatter(x, y, s=None, c=None, color=, marker=) */
static void m_scatter(PikaObj *self, Args *a)
{
    double *x = NULL, *y = NULL;
    int nx = pyg_seq(pyg_arg(a, 0), &x), ny = pyg_seq(pyg_arg(a, 1), &y);
    if (nx < 0 || ny < 0)
    {
        free(x);
        free(y);
        err_seq(self, "scatter");
        return;
    }
    if (nx != ny)
    {
        free(x);
        free(y);
        err_len(self, "scatter");
        return;
    }
    elem *e = add_xy(self, K_LINE, x, y, nx);
    free(x);
    free(y);
    if (e == NULL)
        return;
    e->marker = 'o';
    e->line = 0;
    e->msize = 2;
    double s;
    Arg *sa = pyg_arg_or_kw(a, 2, "s");
    if (sa && pyg_num(sa, &s) && s > 0)
    {
        int r = (int)lround(sqrt(s) / 2.2);
        e->msize = r < 1 ? 1 : (r > 10 ? 10 : r);
    }
    int has_color = 0;
    Arg *ca = pyg_arg(a, 3);
    if (ca && !is_none(ca) && pyg_color(ca, 0, &e->color))
        has_color = 1;
    if (!apply_kw(self, a, e, &has_color))
        return;
    e->line = 0;
    if (!e->marker)
        e->marker = 'o';
    if (!has_color)
        e->color = next_color();
}

/* Abscisses de bar() : nombres, ou noms de categories (0, 1, 2...). */
static int bar_positions(Arg *a, double **out)
{
    int n = pyg_seq(a, out);
    if (n >= 0 || a == NULL || !arg_isList(a))
        return n;
    PikaObj *o = arg_getObj(a);
    n = (int)pikaList_getSize(o);
    *out = (double *)malloc(sizeof(double) * (n > 0 ? n : 1));
    if (*out == NULL)
        return -1;
    F.ncats = 0;
    for (int i = 0; i < n; i++)
    {
        Arg *e = pikaList_get(o, i);
        if (!is_str(e))
        {
            free(*out);
            *out = NULL;
            return -1;
        }
        (*out)[i] = i;
        if (i < MAX_CATS)
        {
            strncpy(F.cats[i], arg_getStr(e), sizeof(F.cats[i]) - 1);
            F.cats[i][sizeof(F.cats[i]) - 1] = '\0';
            F.ncats = i + 1;
        }
    }
    return n;
}

/* bar(x, height, width=0.8, bottom=0, color=) */
static void m_bar(PikaObj *self, Args *a)
{
    double *x = NULL, *h = NULL;
    int nx = bar_positions(pyg_arg(a, 0), &x), nh = pyg_seq(pyg_arg_or_kw(a, 1, "height"), &h);
    if (nx < 0 || nh < 0)
    {
        free(x);
        free(h);
        err_seq(self, "bar");
        return;
    }
    if (nh == 1 && nx > 1)
    {
        /* hauteur commune */
        double v = h[0];
        free(h);
        h = (double *)malloc(sizeof(double) * nx);
        if (h == NULL)
        {
            free(x);
            return;
        }
        for (int i = 0; i < nx; i++)
            h[i] = v;
        nh = nx;
    }
    if (nx != nh)
    {
        free(x);
        free(h);
        err_len(self, "bar");
        return;
    }
    elem *e = add_xy(self, K_BAR, x, h, nx);
    free(x);
    free(h);
    if (e == NULL)
        return;
    double w = 0.8, b = 0.0;
    Arg *wa = pyg_arg_or_kw(a, 2, "width");
    if (wa)
        pyg_num(wa, &w);
    Arg *ba = pyg_arg_or_kw(a, 3, "bottom");
    if (ba)
        pyg_num(ba, &b);
    e->width = (float)w;
    e->bottom = (float)b;
    int has_color = 0;
    if (!apply_kw(self, a, e, &has_color))
        return;
    if (!has_color)
        e->color = next_color();
}

static void ret_hist(Args *a, const int *counts, const double *edges, int nb)
{
    PikaList *c = New_PikaList();
    PikaList *ed = New_PikaList();
    for (int i = 0; i < nb; i++)
        pikaList_append(c, arg_newInt(counts[i]));
    for (int i = 0; i <= nb; i++)
        pikaList_append(ed, arg_newFloat(edges[i]));
    method_returnArg(a, arg_newObj(New_PikaTupleFromVarArgs(arg_newObj(c), arg_newObj(ed))));
}

/* hist(x, bins=10, color=) -> (effectifs, bornes des classes) */
static void m_hist(PikaObj *self, Args *a)
{
    double *x = NULL, *edges = NULL;
    int n = pyg_seq(pyg_arg(a, 0), &x);
    if (n <= 0)
    {
        free(x);
        err_seq(self, "hist");
        return;
    }
    int nb = 10;
    Arg *ba = pyg_arg_or_kw(a, 1, "bins");
    double v;
    int ne = -1;
    if (ba && pyg_num(ba, &v))
        nb = (int)v;
    else if (ba && !is_none(ba))
    {
        ne = pyg_seq(ba, &edges);
        if (ne < 2)
        {
            free(x);
            free(edges);
            pyg_error(self, "ValueError: hist() : bins doit etre un entier ou une liste de bornes");
            return;
        }
        nb = ne - 1;
    }
    if (nb < 1 || nb > 200)
    {
        free(x);
        free(edges);
        pyg_error(self, "ValueError: hist() : bins doit etre entre 1 et 200");
        return;
    }
    if (edges == NULL)
    {
        double lo = x[0], hi = x[0];
        for (int i = 1; i < n; i++)
        {
            lo = x[i] < lo ? x[i] : lo;
            hi = x[i] > hi ? x[i] : hi;
        }
        if (hi == lo)
        {
            lo -= 0.5;
            hi += 0.5;
        }
        edges = (double *)malloc(sizeof(double) * (nb + 1));
        if (edges == NULL)
        {
            free(x);
            return;
        }
        for (int i = 0; i <= nb; i++)
            edges[i] = lo + (hi - lo) * i / nb;
    }
    int *counts = (int *)calloc(nb, sizeof(int));
    if (counts == NULL)
    {
        free(x);
        free(edges);
        return;
    }
    /* Classes [a, b[, la derniere fermee [a, b] (comme numpy). */
    for (int i = 0; i < n; i++)
    {
        for (int k = 0; k < nb; k++)
        {
            if (x[i] >= edges[k] && (x[i] < edges[k + 1] || (k == nb - 1 && x[i] <= edges[k + 1])))
            {
                counts[k]++;
                break;
            }
        }
    }
    elem *e = add_elem(self, K_BAR, nb);
    if (e != NULL)
    {
        for (int k = 0; k < nb; k++)
        {
            e->x[k] = (float)((edges[k] + edges[k + 1]) / 2);
            e->y[k] = (float)counts[k];
        }
        /* Les classes peuvent etre de largeurs differentes : width = -1
         * indique que les nb + 1 bornes (floats) sont rangees dans text
         * (voir bar_left / bar_right). */
        e->width = -1;
        e->text = (char *)malloc(sizeof(float) * (nb + 1));
        if (e->text)
            for (int k = 0; k <= nb; k++)
                ((float *)e->text)[k] = (float)edges[k];
        int has_color = 0;
        if (apply_kw(self, a, e, &has_color) && !has_color)
            e->color = next_color();
        ret_hist(a, counts, edges, nb);
    }
    free(counts);
    free(x);
    free(edges);
}

/* text(x, y, s, color=) */
static void m_text(PikaObj *self, Args *a)
{
    double x, y;
    if (!pyg_num(pyg_arg(a, 0), &x) || !pyg_num(pyg_arg(a, 1), &y) || pyg_arg(a, 2) == NULL)
    {
        pyg_error(self, "TypeError: text() attend x, y, texte");
        return;
    }
    const char *s = as_text(self, pyg_arg(a, 2));
    elem *e = add_elem(self, K_TEXT, 1);
    if (e == NULL)
        return;
    e->x[0] = (float)x;
    e->y[0] = (float)y;
    e->text = dup_str(s);
    e->color = PYG_BLACK;
    int has_color = 0;
    apply_kw(self, a, e, &has_color);
}

/* arrow(x, y, dx, dy, color=) */
static void m_arrow(PikaObj *self, Args *a)
{
    double v[4];
    for (int i = 0; i < 4; i++)
        if (!pyg_num(pyg_arg(a, i), &v[i]))
        {
            pyg_error(self, "TypeError: arrow() attend x, y, dx, dy");
            return;
        }
    elem *e = add_elem(self, K_ARROW, 2);
    if (e == NULL)
        return;
    e->x[0] = (float)v[0];
    e->y[0] = (float)v[1];
    e->x[1] = (float)v[2];
    e->y[1] = (float)v[3];
    e->line = 1;
    int has_color = 0;
    if (apply_kw(self, a, e, &has_color) && !has_color)
        e->color = next_color();
}

static void m_axline(PikaObj *self, Args *a, int kind)
{
    double v = 0;
    Arg *p = pyg_arg_or_kw(a, 0, kind == K_HLINE ? "y" : "x");
    if (p && !pyg_num(p, &v))
    {
        pyg_error(self, "TypeError: axhline()/axvline() attend un nombre");
        return;
    }
    elem *e = add_elem(self, kind, 1);
    if (e == NULL)
        return;
    e->x[0] = e->y[0] = (float)v;
    e->line = 1;
    int has_color = 0;
    if (apply_kw(self, a, e, &has_color) && !has_color)
        e->color = next_color();
}

static void m_axhline(PikaObj *self, Args *a) { m_axline(self, a, K_HLINE); }
static void m_axvline(PikaObj *self, Args *a) { m_axline(self, a, K_VLINE); }

/* ---------- Reglages ---------- */

static int read_pair(Args *a, double *lo, double *hi)
{
    if (pyg_num(pyg_arg(a, 0), lo) && pyg_num(pyg_arg(a, 1), hi))
        return 1;
    double *v = NULL;
    int n = pyg_seq(pyg_arg(a, 0), &v);
    if (n == 2)
    {
        *lo = v[0];
        *hi = v[1];
    }
    free(v);
    return n == 2;
}

static void bounds(double *x0, double *x1, double *y0, double *y1);

static void ret_limits(Args *a)
{
    double x0, x1, y0, y1;
    bounds(&x0, &x1, &y0, &y1);
    method_returnArg(a, arg_newObj(New_PikaTupleFromVarArgs(arg_newFloat(x0), arg_newFloat(x1),
                                                            arg_newFloat(y0), arg_newFloat(y1))));
}

/* axis([xmin, xmax, ymin, ymax]), axis('off'), axis('on'), axis('equal'),
 * axis('auto'). Sans argument : renvoie les bornes. */
static void m_axis(PikaObj *self, Args *a)
{
    Arg *p = pyg_arg(a, 0);
    if (p == NULL)
    {
        ret_limits(a);
        return;
    }
    if (is_str(p))
    {
        const char *s = arg_getStr(p);
        if (!strcmp(s, "off"))
            F.axis_on = 0;
        else if (!strcmp(s, "on"))
            F.axis_on = 1;
        else if (!strcmp(s, "equal") || !strcmp(s, "scaled") || !strcmp(s, "square"))
            F.equal = 1;
        else if (!strcmp(s, "auto") || !strcmp(s, "tight"))
        {
            F.equal = 0;
            F.has_xlim = F.has_ylim = 0;
        }
        return;
    }
    if (arg_getType(p) == ARG_TYPE_BOOL)
    {
        F.axis_on = arg_getBool(p) ? 1 : 0;
        return;
    }
    double *v = NULL;
    int n = pyg_seq(p, &v);
    if (n != 4 && pyg_nargs(a) == 4)
    {
        free(v);
        v = (double *)malloc(sizeof(double) * 4);
        n = 4;
        for (int i = 0; v && i < 4; i++)
            if (!pyg_num(pyg_arg(a, i), &v[i]))
                n = -1;
    }
    if (n != 4 || v == NULL)
    {
        free(v);
        pyg_error(self, "TypeError: axis() attend [xmin, xmax, ymin, ymax]");
        return;
    }
    F.xlim[0] = v[0];
    F.xlim[1] = v[1];
    F.ylim[0] = v[2];
    F.ylim[1] = v[3];
    F.has_xlim = F.has_ylim = 1;
    free(v);
}

static void m_xlim(PikaObj *self, Args *a)
{
    if (pyg_nargs(a) == 0)
    {
        double x0, x1, y0, y1;
        bounds(&x0, &x1, &y0, &y1);
        method_returnArg(a, arg_newObj(New_PikaTupleFromVarArgs(arg_newFloat(x0), arg_newFloat(x1))));
        return;
    }
    if (!read_pair(a, &F.xlim[0], &F.xlim[1]))
    {
        pyg_error(self, "TypeError: xlim() attend xmin, xmax");
        return;
    }
    F.has_xlim = 1;
}

static void m_ylim(PikaObj *self, Args *a)
{
    if (pyg_nargs(a) == 0)
    {
        double x0, x1, y0, y1;
        bounds(&x0, &x1, &y0, &y1);
        method_returnArg(a, arg_newObj(New_PikaTupleFromVarArgs(arg_newFloat(y0), arg_newFloat(y1))));
        return;
    }
    if (!read_pair(a, &F.ylim[0], &F.ylim[1]))
    {
        pyg_error(self, "TypeError: ylim() attend ymin, ymax");
        return;
    }
    F.has_ylim = 1;
}

static void m_grid(PikaObj *self, Args *a)
{
    Arg *p = pyg_arg_or_kw(a, 0, "visible");
    double v;
    if (p == NULL || is_none(p))
        F.grid = !F.grid;
    else if (pyg_num(p, &v))
        F.grid = v != 0;
}

static void set_label(PikaObj *self, Args *a, char *dst, size_t cap)
{
    const char *s = as_text(self, pyg_arg(a, 0));
    strncpy(dst, s, cap - 1);
    dst[cap - 1] = '\0';
}

static void m_title(PikaObj *self, Args *a) { set_label(self, a, F.title, sizeof(F.title)); }
static void m_xlabel(PikaObj *self, Args *a) { set_label(self, a, F.xlabel, sizeof(F.xlabel)); }
static void m_ylabel(PikaObj *self, Args *a) { set_label(self, a, F.ylabel, sizeof(F.ylabel)); }
static void m_clf(PikaObj *self, Args *a) { fig_clear(); }
static void m_nop(PikaObj *self, Args *a) {}

/* ---------- Rendu ---------- */

static void grow(double v, double *lo, double *hi, int *any)
{
    if (!isfinite(v))
        return;
    if (!*any)
    {
        *lo = *hi = v;
        *any = 1;
        return;
    }
    if (v < *lo)
        *lo = v;
    if (v > *hi)
        *hi = v;
}

static float bar_left(const elem *e, int i)
{
    return e->width < 0 ? ((const float *)e->text)[i] : e->x[i] - e->width / 2;
}

static float bar_right(const elem *e, int i)
{
    return e->width < 0 ? ((const float *)e->text)[i + 1] : e->x[i] + e->width / 2;
}

/* Bornes des donnees, avec 5 % de marge comme matplotlib. */
static void bounds(double *x0, double *x1, double *y0, double *y1)
{
    double xl = 0, xh = 1, yl = 0, yh = 1;
    int ax = 0, ay = 0, bars = 0;
    for (int k = 0; k < F.ne; k++)
    {
        const elem *e = &F.e[k];
        for (int i = 0; i < e->n; i++)
        {
            switch (e->kind)
            {
            case K_BAR:
                if (e->width < 0 && e->text == NULL)
                    break;
                grow(bar_left(e, i), &xl, &xh, &ax);
                grow(bar_right(e, i), &xl, &xh, &ax);
                grow(e->bottom, &yl, &yh, &ay);
                grow(e->bottom + e->y[i], &yl, &yh, &ay);
                bars = 1;
                break;
            case K_ARROW:
                if (i == 0)
                {
                    grow(e->x[0], &xl, &xh, &ax);
                    grow(e->y[0], &yl, &yh, &ay);
                    grow(e->x[0] + e->x[1], &xl, &xh, &ax);
                    grow(e->y[0] + e->y[1], &yl, &yh, &ay);
                }
                break;
            case K_HLINE:
                grow(e->y[0], &yl, &yh, &ay);
                break;
            case K_VLINE:
                grow(e->x[0], &xl, &xh, &ax);
                break;
            default:
                grow(e->x[i], &xl, &xh, &ax);
                grow(e->y[i], &yl, &yh, &ay);
            }
        }
    }
    if (!ax)
    {
        xl = 0;
        xh = 1;
    }
    if (!ay)
    {
        yl = 0;
        yh = 1;
    }
    if (xh - xl < 1e-12)
    {
        double d = fabs(xl) > 1e-12 ? fabs(xl) * 0.05 : 0.5;
        xl -= d;
        xh += d;
    }
    if (yh - yl < 1e-12)
    {
        double d = fabs(yl) > 1e-12 ? fabs(yl) * 0.05 : 0.5;
        yl -= d;
        yh += d;
    }
    double mx = (xh - xl) * 0.05, my = (yh - yl) * 0.05;
    /* Les barres partent de 0 sans marge (comme matplotlib). */
    int y_from_zero = bars && yl == 0.0;
    xl -= mx;
    xh += mx;
    if (!y_from_zero)
        yl -= my;
    yh += my;
    if (F.has_xlim)
    {
        xl = F.xlim[0];
        xh = F.xlim[1];
    }
    if (F.has_ylim)
    {
        yl = F.ylim[0];
        yh = F.ylim[1];
    }
    *x0 = xl;
    *x1 = xh;
    *y0 = yl;
    *y1 = yh;
}

/* Pas "rond" (1, 2 ou 5 x 10^k) pour environ target graduations. */
static double nice_step(double range, int target)
{
    if (!(range > 0) || target < 1)
        return 1.0;
    double raw = range / target;
    double mag = pow(10.0, floor(log10(raw)));
    double n = raw / mag;
    double s = n < 1.5 ? 1 : n < 3.5 ? 2
                         : n < 7.5   ? 5
                                     : 10;
    return s * mag;
}

typedef struct
{
    double x0, x1, y0, y1;  /* bornes des donnees */
    int px0, px1, py0, py1; /* zone de trace a l'ecran */
} view;

static double vx(const view *v, double x) { return v->px0 + (x - v->x0) * (v->px1 - v->px0) / (v->x1 - v->x0); }
static double vy(const view *v, double y) { return v->py1 - (y - v->y0) * (v->py1 - v->py0) / (v->y1 - v->y0); }

static void draw_marker(double x, double y, char m, int r, int color)
{
    switch (m)
    {
    case '.':
    case ',':
        pyg_disc(x, y, r > 1 ? r / 2 : 1, color);
        break;
    case '+':
        pyg_line(x - r - 1, y, x + r + 1, y, color, 1, 0);
        pyg_line(x, y - r - 1, x, y + r + 1, color, 1, 0);
        break;
    case 'x':
        pyg_line(x - r, y - r, x + r, y + r, color, 1, 0);
        pyg_line(x - r, y + r, x + r, y - r, color, 1, 0);
        break;
    case 's':
    case 'D':
    case 'd':
        pyg_rect(x - r, y - r, x + r + 1, y + r + 1, color);
        break;
    case '|':
        pyg_line(x, y - r - 1, x, y + r + 1, color, 1, 0);
        break;
    case '_':
        pyg_line(x - r - 1, y, x + r + 1, y, color, 1, 0);
        break;
    case '^':
    case 'v':
    {
        float s = m == '^' ? -1.0f : 1.0f;
        float xs[3] = {(float)x, (float)(x - r - 1), (float)(x + r + 1)};
        float ys[3] = {(float)(y + s * (r + 1)), (float)(y - s * r), (float)(y - s * r)};
        pyg_fill_poly(xs, ys, 3, color);
        break;
    }
    default:
        pyg_disc(x, y, r, color);
    }
}

static void draw_elem(const view *v, const elem *e)
{
    int dash = e->line == 2 ? 1 : e->line == 3 ? 2
                                                : 0;
    int w = (int)lround(e->width);
    pyg_dash_reset();
    switch (e->kind)
    {
    case K_LINE:
        if (e->line)
            for (int i = 0; i + 1 < e->n; i++)
                pyg_line(vx(v, e->x[i]), vy(v, e->y[i]), vx(v, e->x[i + 1]), vy(v, e->y[i + 1]), e->color, w, dash);
        if (e->marker)
            for (int i = 0; i < e->n; i++)
            {
                double X = vx(v, e->x[i]), Y = vy(v, e->y[i]);
                if (X >= v->px0 - 1 && X <= v->px1 + 1 && Y >= v->py0 - 1 && Y <= v->py1 + 1)
                    draw_marker(X, Y, e->marker, e->msize, e->color);
            }
        break;
    case K_BAR:
        if (e->width < 0 && e->text == NULL)
            break;
        for (int i = 0; i < e->n; i++)
        {
            double xa = vx(v, bar_left(e, i)), xb = vx(v, bar_right(e, i));
            double ya = vy(v, e->bottom), yb = vy(v, e->bottom + e->y[i]);
            pyg_rect(xa, ya, xb, yb, e->color);
            if (e->width < 0 && xb - xa >= 4) /* histogramme : separation des classes */
            {
                double X = floor(xb);
                pyg_rect(X, ya, X + 1, yb, PYG_WHITE);
            }
        }
        break;
    case K_TEXT:
        if (e->text)
            pyg_text(vx(v, e->x[0]), vy(v, e->y[0]) - 8, e->text, e->color, 1);
        break;
    case K_ARROW:
    {
        double xa = vx(v, e->x[0]), ya = vy(v, e->y[0]);
        double xb = vx(v, e->x[0] + e->x[1]), yb = vy(v, e->y[0] + e->y[1]);
        pyg_line(xa, ya, xb, yb, e->color, w, 0);
        double ang = atan2(yb - ya, xb - xa), len = hypot(xb - xa, yb - ya);
        double hl = len < 14 ? len / 2 : 7;
        for (int s = -1; s <= 1; s += 2)
        {
            double t = ang + M_PI + s * 0.45;
            pyg_line(xb, yb, xb + hl * cos(t), yb + hl * sin(t), e->color, w, 0);
        }
        break;
    }
    case K_HLINE:
        pyg_line(v->px0, vy(v, e->y[0]), v->px1, vy(v, e->y[0]), e->color, w, dash);
        break;
    case K_VLINE:
        pyg_line(vx(v, e->x[0]), v->py0, vx(v, e->x[0]), v->py1, e->color, w, dash);
        break;
    }
}

#define AXIS_COLOR 0x0000
#define GRID_COLOR 0xD69A /* gris clair */

static void render(void)
{
    view v;
    bounds(&v.x0, &v.x1, &v.y0, &v.y1);
    if (v.x1 == v.x0)
        v.x1 = v.x0 + 1;
    if (v.y1 == v.y0)
        v.y1 = v.y0 + 1;

    /* Zone de trace : place pour les graduations et les titres. */
    int top = 4, bottom = PYG_H - 2, left = 2, right = PYG_W - 3;
    double ystep = 1, xstep = 1;
    char buf[16];
    if (F.axis_on)
    {
        if (F.title[0] || F.ylabel[0])
            top = 14;
        bottom = PYG_H - 13 - (F.xlabel[0] ? 10 : 0);
        ystep = nice_step(fabs(v.y1 - v.y0), (bottom - top) / 36);
        int lw = 0;
        for (double t = ceil(fmin(v.y0, v.y1) / ystep) * ystep; t <= fmax(v.y0, v.y1) + ystep * 1e-9; t += ystep)
        {
            pyg_fmt(t, buf, sizeof(buf));
            int w = pyg_text_w(buf, 1);
            lw = w > lw ? w : lw;
        }
        left = lw + 6;
        right = PYG_W - 8;
    }

    /* Repere orthonorme : on elargit l'intervalle le plus serre. */
    if (F.equal)
    {
        double pw = right - left, ph = bottom - top;
        double ux = fabs(v.x1 - v.x0) / pw, uy = fabs(v.y1 - v.y0) / ph;
        if (ux > uy)
        {
            double c = (v.y0 + v.y1) / 2, h = ux * ph / 2 * (v.y1 > v.y0 ? 1 : -1);
            v.y0 = c - h;
            v.y1 = c + h;
        }
        else
        {
            double c = (v.x0 + v.x1) / 2, h = uy * pw / 2 * (v.x1 > v.x0 ? 1 : -1);
            v.x0 = c - h;
            v.x1 = c + h;
        }
        if (F.axis_on)
            ystep = nice_step(fabs(v.y1 - v.y0), (bottom - top) / 36);
    }
    v.px0 = left;
    v.px1 = right;
    v.py0 = top;
    v.py1 = bottom;
    xstep = nice_step(fabs(v.x1 - v.x0), (right - left) / 50);

    pyg_begin();
    oc_draw_clear(PYG_WHITE);
    pyg_reset_clip();

    double ylo = fmin(v.y0, v.y1), yhi = fmax(v.y0, v.y1);
    double xlo = fmin(v.x0, v.x1), xhi = fmax(v.x0, v.x1);
    if (F.axis_on && F.grid)
    {
        for (double t = ceil(ylo / ystep) * ystep; t <= yhi + ystep * 1e-9; t += ystep)
            pyg_line(left, vy(&v, t), right, vy(&v, t), GRID_COLOR, 1, 0);
        if (F.ncats == 0)
            for (double t = ceil(xlo / xstep) * xstep; t <= xhi + xstep * 1e-9; t += xstep)
                pyg_line(vx(&v, t), top, vx(&v, t), bottom, GRID_COLOR, 1, 0);
    }

    pyg_set_clip(left, top, right, bottom);
    for (int k = 0; k < F.ne; k++)
        if (F.e[k].kind != K_TEXT)
            draw_elem(&v, &F.e[k]);
    pyg_reset_clip();
    for (int k = 0; k < F.ne; k++)
        if (F.e[k].kind == K_TEXT)
            draw_elem(&v, &F.e[k]);

    if (F.axis_on)
    {
        pyg_line(left, top, right, top, AXIS_COLOR, 1, 0);
        pyg_line(left, bottom, right, bottom, AXIS_COLOR, 1, 0);
        pyg_line(left, top, left, bottom, AXIS_COLOR, 1, 0);
        pyg_line(right, top, right, bottom, AXIS_COLOR, 1, 0);
        for (double t = ceil(ylo / ystep) * ystep; t <= yhi + ystep * 1e-9; t += ystep)
        {
            double Y = vy(&v, t);
            pyg_line(left - 3, Y, left, Y, AXIS_COLOR, 1, 0);
            pyg_fmt(t, buf, sizeof(buf));
            pyg_text(left - 4 - pyg_text_w(buf, 1), Y - 4, buf, AXIS_COLOR, 1);
        }
        if (F.ncats > 0)
        {
            for (int i = 0; i < F.ncats; i++)
            {
                double X = vx(&v, i);
                if (X < left || X > right)
                    continue;
                pyg_line(X, bottom, X, bottom + 3, AXIS_COLOR, 1, 0);
                pyg_text(X - pyg_text_w(F.cats[i], 1) / 2.0, bottom + 4, F.cats[i], AXIS_COLOR, 1);
            }
        }
        else
        {
            for (double t = ceil(xlo / xstep) * xstep; t <= xhi + xstep * 1e-9; t += xstep)
            {
                double X = vx(&v, t);
                pyg_line(X, bottom, X, bottom + 3, AXIS_COLOR, 1, 0);
                pyg_fmt(t, buf, sizeof(buf));
                double tx = X - pyg_text_w(buf, 1) / 2.0;
                if (tx + pyg_text_w(buf, 1) > PYG_W - 1)
                    tx = PYG_W - 1 - pyg_text_w(buf, 1);
                pyg_text(tx, bottom + 4, buf, AXIS_COLOR, 1);
            }
        }
        if (F.xlabel[0])
            pyg_text((left + right - pyg_text_w(F.xlabel, 1)) / 2.0, PYG_H - 9, F.xlabel, AXIS_COLOR, 1);
        if (F.ylabel[0])
            pyg_text(2, 3, F.ylabel, AXIS_COLOR, 1);
        if (F.title[0])
            pyg_text((left + right - pyg_text_w(F.title, 1)) / 2.0, 3, F.title, AXIS_COLOR, 1);
    }
    else if (F.title[0])
        pyg_text((PYG_W - pyg_text_w(F.title, 1)) / 2.0, 3, F.title, AXIS_COLOR, 1);
}

static int fig_has_content(void)
{
    return F.ne > 0;
}

/* show() : dessine la figure, attend une touche, puis vide la figure. */
static void m_show(PikaObj *self, Args *a)
{
    render();
    pyg_show_and_wait();
    fig_clear();
}

void py_plot_autoshow(void)
{
    if (!fig_has_content())
        return;
    render(); /* l'app Python montre l'ecran et attend une touche */
    fig_clear();
}

typedef struct
{
    const char *name;
    void (*fn)(PikaObj *, Args *);
} plot_method;

static const plot_method k_methods[] = {
    {"plot", m_plot},
    {"scatter", m_scatter},
    {"bar", m_bar},
    {"hist", m_hist},
    {"text", m_text},
    {"arrow", m_arrow},
    {"axhline", m_axhline},
    {"axvline", m_axvline},
    {"axis", m_axis},
    {"xlim", m_xlim},
    {"ylim", m_ylim},
    {"grid", m_grid},
    {"title", m_title},
    {"xlabel", m_xlabel},
    {"ylabel", m_ylabel},
    {"show", m_show},
    {"clf", m_clf},
    {"cla", m_clf},
    {"close", m_clf},
    {"figure", m_nop},
    {"legend", m_nop},
    {"tight_layout", m_nop},
    {"savefig", m_nop},
    {"xticks", m_nop},
    {"yticks", m_nop},
};

#define N_METHODS (sizeof(k_methods) / sizeof(k_methods[0]))

const char *py_plot_name(int i)
{
    return i >= 0 && i < (int)N_METHODS ? k_methods[i].name : NULL;
}

void py_plot_install(PikaObj *root)
{
    fig_clear();
    PikaObj *p = newNormalObj(New_TinyObj);
    for (size_t i = 0; i < N_METHODS; i++)
        class_defineMethod(p, (char *)k_methods[i].name, "*a,**kw", k_methods[i].fn);
    PikaObj *mpl = newNormalObj(New_TinyObj);
    obj_setObj(mpl, "pyplot", p);
    obj_setObj(root, "matplotlib", mpl);
}
