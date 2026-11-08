#include "ui.h"
#include "Evaluator.h"
#include "grapher.h"
#include <math.h>
#include "menu.h"
#include <algorithm>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <cstdio>
#include <string.h>
#include "colors.h"
#include "ui_kit.h"

extern int g_decimals; // reglage "Decimales" (settings.cpp)

// Nombre pour l'affichage : arrondi au nombre de decimales des reglages, donc
// pas de 0.99999999999998 apres un aimant ni de 1e-16 pour un zero.
static void fmt_val(double v, char *buf)
{
    if (isnan(v))
    {
        strcpy(buf, "NaN");
        return;
    }
    if (isinf(v))
    {
        strcpy(buf, v > 0 ? "inf" : "-inf");
        return;
    }
    int d = g_decimals < 0 ? 0 : (g_decimals > 9 ? 9 : g_decimals);
    double f = pow(10.0, d);
    double r = round(v * f) / f;
    if (fabs(r) < 1e-12)
        r = 0;
    snprintf(buf, 40, "%.10g", r);
}

bool X_valid = false; // vrai si graph() a place le curseur pendant cette image
int X_cursor = 0;
int Y_cursor = 0;

double X_pos = 0;
double Y_pos = 0;

void wu_line(int y0, int x0, int y1, int x1, uint16_t color)
{
    int steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep)
    {
        int tmp = x0;
        x0 = y0;
        y0 = tmp;

        tmp = x1;
        x1 = y1;
        y1 = tmp;
    }
    if (x0 > x1)
    {
        int tmp = x0;
        x0 = x1;
        x1 = tmp;

        tmp = y0;
        y0 = y1;
        y1 = tmp;
    }

    float dx = x1 - x0;
    float dy = y1 - y0;
    float gradient = dx == 0 ? 1 : dy / dx;
    float y = y0 + gradient * (roundf(x0) - x0);

    for (int x = x0; x <= x1; x++)
    {
        int y_int = (int)y;
        float frac = y - y_int;

        if (steep)
        {
            fill_rect(y_int, x, 1, 1, color);                                    // pixel principal
            fill_rect(y_int + 1, x, 1, 1, (color * 10 + BACKGROUND_COLOR) / 11); // pixel voisin (antialias)
        }
        else
        {
            fill_rect(x, y_int, 1, 1, color);
            fill_rect(x, y_int + 1, 1, 1, color);
        }

        y += gradient;
    }
}

void graph(double cursor_pos, token *function, double x_min, double x_max, double y_min, double y_max, int n, uint16_t color, int id, bool fill_between_points, char var)
{
    double pas = (x_max - x_min) / SCREEN_WIDTH;

    token *tokenized_expression = shunting_yard(function, n);
    double pos = x_min;
    int yarded = count_yarded(function, n);

    double last = (evaluate_npi(tokenized_expression, yarded, pos, var) - y_min) / (y_max - y_min) * SCREEN_HEIGHT;
    double y = 0;
    int k = 0;
    if (fill_between_points)
    {
        k = 1;
    }
    else
    {
        // Pas en pixels entre deux points (un par unite de x). Au-dela de 320
        // unites de large, la division entiere donnait 0 -> boucle infinie.
        k = (int)(SCREEN_WIDTH / (x_max - x_min));
        if (k < 1)
            k = 1;
    }

    for (int i = 0; i < SCREEN_WIDTH; i += k)
    {
        y = evaluate_npi(tokenized_expression, yarded, pos, var);

        if (isnan(y))
        {
            if (fill_between_points)
            {
                pos += pas;
            }
            else
            {
                pos++;
            }
            last = NAN;
        }
        else
        {
            if (isnan(last))
            {
                last = y;
            }
            double h = (y_max - y_min);
            double display_y = (y - y_min) / h * SCREEN_HEIGHT;
            if (fill_between_points)
            {
                if (last < 320 && last > 0 && display_y > 0 && display_y < 320)
                {
                    wu_line((i - 1), last, i, display_y, color);
                }
            }
            else
            {
                fill_rect(display_y - 2, i - 2, 3, 3, color);
            }
            if ((id != -1) && pos < cursor_pos && pos + pas > cursor_pos)
            {
                X_valid = true;
                X_cursor = i - 1;
                Y_cursor = last;
                X_pos = pos;
                Y_pos = y;
            }
            if (fill_between_points)
            {
                pos += pas;
            }
            else
            {
                pos++;
            }
            last = display_y;
        }
    }
    if (id != -1)
    {
        // id - 1 = indice de la fonction (f, g, h...). Les textes sont ecrits
        // avec snprintf : toujours termines, aucune lecture hors tampon.
        char fname = (char)((id - 1 + 5) % 26 + 'a');
        char num[64];

        text_box *t = create_text_box(0, 0, 20, 107, 2, false);
        fmt_val(cursor_pos, num);
        snprintf(t->text, 100, "%c: %s", var, num);
        t->t_size = (int)strlen(t->text);
        display_text_box(t, 0, 0, false);
        free_text_box(t);

        double fx = evaluate_npi(tokenized_expression, yarded, cursor_pos, var);
        text_box *tb = create_text_box(107, 0, 20, 107, 2, false);
        fmt_val(fx, num);
        snprintf(tb->text, 100, "%c(%c): %s", fname, var, num);
        tb->t_size = (int)strlen(tb->text);
        display_text_box(tb, 0, 0, false);
        free_text_box(tb);

        double hstep = (x_max - x_min) / 100000000;
        double deriv = (evaluate_npi(tokenized_expression, yarded, cursor_pos + hstep, var) - fx) / hstep;
        fmt_val(deriv, num);
        text_box *tc = create_text_box(214, 0, 20, 106, 2, false);
        snprintf(tc->text, 100, "%c'(%c): %s", fname, var, num);
        tc->t_size = (int)strlen(tc->text);
        display_text_box(tc, 0, 0, false);
        free_text_box(tc);
    }
    free(tokenized_expression);
    return;
}

/* ===================== Calculs sur la courbe =====================
 * Touche OK dans le graphe : zero, minimum, maximum, intersection, integrale
 * de la fonction selectionnee, recherches dans la fenetre affichee et le plus
 * pres possible du curseur. */

typedef struct
{
    token *rpn;
    int n;
} fn_t;

static bool fn_build(fill_box *b, fn_t *f)
{
    f->rpn = nullptr;
    f->n = 0;
    if (b == nullptr || b->t_size <= 0)
        return false;
    int tn = 0;
    token *raw = parse_string_to_token(b->text, b->t_size, &tn);
    f->rpn = shunting_yard(raw, tn);
    f->n = count_yarded(raw, tn);
    token_free_matrices(raw, tn);
    free(raw);
    return true;
}

static void fn_free(fn_t *f)
{
    free(f->rpn);
    f->rpn = nullptr;
}

static double fn_eval(const fn_t *f, double x)
{
    return evaluate_npi(f->rpn, f->n, x, 'X');
}

typedef double (*scalar_fn)(void *ctx, double x);

static double ctx_f(void *c, double x)
{
    return fn_eval((const fn_t *)c, x);
}

typedef struct
{
    const fn_t *f;
    const fn_t *g;
} fn_pair;

static double ctx_diff(void *c, double x)
{
    const fn_pair *p = (const fn_pair *)c;
    return fn_eval(p->f, x) - fn_eval(p->g, x);
}

typedef struct
{
    scalar_fn F;
    void *c;
} abs_ctx;

static double ctx_abs(void *c, double x)
{
    const abs_ctx *a = (const abs_ctx *)c;
    return fabs(a->F(a->c, x));
}

static bool finite_d(double v)
{
    return !isnan(v) && !isinf(v);
}

#define GC_SAMPLES 800

// Zero de F dans [a, b] (F(a) et F(b) de signes opposes), par dichotomie.
static double bisect(scalar_fn F, void *c, double a, double b, double fa)
{
    for (int i = 0; i < 80; i++)
    {
        double m = 0.5 * (a + b);
        double fm = F(c, m);
        if (!finite_d(fm))
            return NAN;
        if (fm == 0)
            return m;
        if ((fa < 0) == (fm < 0))
        {
            a = m;
            fa = fm;
        }
        else
            b = m;
    }
    return 0.5 * (a + b);
}

// Extremum de F dans [a, b] par recherche du nombre d'or.
static double golden(scalar_fn F, void *c, double a, double b, bool maximize)
{
    const double gr = 0.6180339887498949;
    double s = maximize ? -1.0 : 1.0;
    double x1 = b - gr * (b - a), x2 = a + gr * (b - a);
    double g1 = s * F(c, x1), g2 = s * F(c, x2);
    for (int i = 0; i < 100; i++)
    {
        if (g1 < g2)
        {
            b = x2;
            x2 = x1;
            g2 = g1;
            x1 = b - gr * (b - a);
            g1 = s * F(c, x1);
        }
        else
        {
            a = x1;
            x1 = x2;
            g1 = g2;
            x2 = a + gr * (b - a);
            g2 = s * F(c, x2);
        }
        if (fabs(b - a) < 1e-13 * (1 + fabs(a) + fabs(b)))
            break;
    }
    return 0.5 * (a + b);
}

// Racine candidate au voisinage de l'echantillon i (NAN si aucune). Detecte les
// changements de signe (en rejetant les poles et les sauts) et les zeros
// doubles (x^2). fv = valeurs de F aux N+1 echantillons lo + k*step.
static double root_candidate(scalar_fn F, void *c, const double *fv, int i, int N,
                             double lo, double step)
{
    double x = lo + i * step;
    double fi = fv[i];
    if (!finite_d(fi))
        return NAN;
    if (fi == 0)
        return x;
    if (i < N && finite_d(fv[i + 1]) && fi * fv[i + 1] < 0)
    {
        double r = bisect(F, c, x, x + step, fi);
        if (finite_d(r))
        {
            double fr = F(c, r);
            double tol = 1e-6 * (fabs(fi) + fabs(fv[i + 1])) + 1e-12;
            if (finite_d(fr) && fabs(fr) <= tol)
                return r;
        }
        return NAN;
    }
    if (i > 0 && i < N && finite_d(fv[i - 1]) && finite_d(fv[i + 1]) &&
        fabs(fi) <= fabs(fv[i - 1]) && fabs(fi) < fabs(fv[i + 1]))
    {
        // minimum de |F| proche de 0 : zero double
        abs_ctx ac = {F, c};
        double r = golden(ctx_abs, &ac, x - step, x + step, false);
        double fr = fabs(F(c, r));
        if (finite_d(fr) && fr <= 1e-9 * (1 + fabs(fv[i - 1]) + fabs(fv[i + 1])))
            return r;
    }
    return NAN;
}

// Zero de F dans [lo, hi] le plus proche de `near`.
static bool find_root_near(scalar_fn F, void *c, double lo, double hi, double near, double *out)
{
    const int N = GC_SAMPLES;
    double step = (hi - lo) / N;
    double *fv = (double *)malloc(sizeof(double) * (N + 1));
    if (!fv)
        return false;
    for (int i = 0; i <= N; i++)
        fv[i] = F(c, lo + i * step);

    bool found = false;
    double best = INFINITY;
    for (int i = 0; i <= N; i++)
    {
        double cand = root_candidate(F, c, fv, i, N, lo, step);
        if (finite_d(cand) && fabs(cand - near) < best)
        {
            best = fabs(cand - near);
            *out = cand;
            found = true;
        }
    }
    free(fv);
    return found;
}

// Tous les zeros de F dans [lo, hi] (tries, au plus `max`).
static int collect_roots(scalar_fn F, void *c, double lo, double hi, double *out, int max)
{
    const int N = GC_SAMPLES;
    double step = (hi - lo) / N;
    double *fv = (double *)malloc(sizeof(double) * (N + 1));
    if (!fv)
        return 0;
    for (int i = 0; i <= N; i++)
        fv[i] = F(c, lo + i * step);

    int n = 0;
    for (int i = 0; i <= N && n < max; i++)
    {
        double cand = root_candidate(F, c, fv, i, N, lo, step);
        if (!finite_d(cand))
            continue;
        // meme racine vue depuis deux echantillons voisins : on la garde une fois
        if (n > 0 && fabs(cand - out[n - 1]) < 0.5 * step)
            continue;
        out[n++] = cand;
    }
    free(fv);
    return n;
}

// Minimum ou maximum local de F dans [lo, hi] le plus proche de `near`.
static bool find_extremum_near(scalar_fn F, void *c, double lo, double hi, double near,
                               bool maximize, double *xo, double *yo)
{
    const int N = GC_SAMPLES;
    double step = (hi - lo) / N;
    double *fv = (double *)malloc(sizeof(double) * (N + 1));
    if (!fv)
        return false;
    for (int i = 0; i <= N; i++)
        fv[i] = F(c, lo + i * step);

    bool found = false;
    double best = INFINITY;
    for (int i = 1; i < N; i++)
    {
        double a = fv[i - 1], b = fv[i], d = fv[i + 1];
        if (!finite_d(a) || !finite_d(b) || !finite_d(d))
            continue;
        bool ext = maximize ? (b > a && b >= d) : (b < a && b <= d);
        if (!ext)
            continue;
        double x = lo + i * step;
        double r = golden(F, c, x - step, x + step, maximize);
        double fr = F(c, r);
        if (!finite_d(fr) || (maximize ? fr < b : fr > b))
        {
            r = x; // repli sur l'echantillon (fonction non lisse)
            fr = b;
        }
        if (fabs(r - near) < best)
        {
            best = fabs(r - near);
            *xo = r;
            *yo = fr;
            found = true;
        }
    }
    free(fv);
    return found;
}

// Integrale de F entre a et b (Simpson, 2000 intervalles).
static bool integrate(scalar_fn F, void *c, double a, double b, double *out)
{
    if (a == b)
    {
        *out = 0;
        return true;
    }
    const int n = 2000;
    double h = (b - a) / n;
    double s = F(c, a) + F(c, b);
    if (!finite_d(s))
        return false;
    for (int i = 1; i < n; i++)
    {
        double x = a + i * h;
        double v = F(c, x);
        if (!finite_d(v))
        {
            // Trou isole (ex. sin(x)/x en 0) : accepte seulement si la valeur
            // juste a cote colle aux voisines (sinon c'est un pole, ex. 1/x).
            double n1 = F(c, x - h), n2 = F(c, x + h);
            v = F(c, x + h * 1e-6);
            if (!finite_d(n1) || !finite_d(n2) || !finite_d(v))
                return false;
            double avg = 0.5 * (n1 + n2), spread = fabs(n1 - n2);
            if (fabs(v - avg) > 0.5 * (fabs(avg) + spread) + 1e-9)
                return false;
        }
        s += (i % 2 ? 4.0 : 2.0) * v;
    }
    *out = s * h / 3.0;
    return true;
}

// Petit menu vertical (meme comportement que les menus des Outils).
static int small_menu(const char **labels, int n)
{
    text_box **items = (text_box **)malloc(sizeof(text_box *) * n);
   // fill_rect(0, 32, 120, 256, 0x311f);
    for (int i = 0; i < n; i++)
    {
        items[i] = create_text_box(60, i * 30, 30, 200, 2, false);
        items[i]->text = (char *)labels[i];
        items[i]->t_size = (int)strlen(labels[i]);
        items[i]->allign = 'r';
    }
    int pos = 0, sel = 0, result = -1;
    while (true)
    {
        for (int i = pos; i < pos + 4 && i < n; i++)
            display_text_box(items[i], pos * -30, 0, i == sel);
        flush_display();
        int k = scan_keypad();
        while (k == -1)
            k = scan_keypad();
        if (k == UP)
        {
            sel = min(n - 1, sel + 1);
            if (sel - pos >= 4)
                pos = sel - 3;
        }
        else if (k == DOWN)
        {
            sel = max(0, sel - 1);
            if (sel < pos)
                pos = sel;
        }
        else if (k == BACK)
        {
            result = -1;
            break;
        }
        else if (k == OK || k == ENTER)
        {
            result = sel;
            break;
        }
    }
    for (int i = 0; i < n; i++)
        free_text_box(items[i]);
    free(items);
    return result;
}

// Les recherches numeriques ont une precision limitee (~1e-8 sur un extremum) :
// les valeurs minuscules devant la fenetre sont affichees comme 0.
static double clean_val(double v, double scale)
{
    return fabs(v) < 1e-7 * scale ? 0.0 : v;
}

typedef struct
{
    char msg[80];
    bool is_result;  // message de resultat : efface a la touche suivante
    int integ_stage; // 1 = borne a choisie, en attente de la borne b
    double integ_a;
    bool shade;
    double shade_a, shade_b;
    int shade_fn;
} calc_state;

static void calc_menu(calc_state *st, fill_box **arr, int sel, double x_min, double x_max, double *cursor)
{
    fn_t f;
    if (!fn_build(arr[sel], &f))
        return;
    const char *labels[5] = {"Zero", "Minimum", "Maximum", "Intersection", "Integrale"};
    fill_screen(BACKGROUND_COLOR);
    
    int c = small_menu(labels, 5);
    fill_screen(BACKGROUND_COLOR);
    
    double x = 0, y = 0;
    double wx = x_max - x_min;
    st->is_result = true;
    switch (c)
    {
    case 0:
        if (find_root_near(ctx_f, &f, x_min, x_max, *cursor, &x))
        {
            x = clean_val(x, wx);
            *cursor = x;
            char sx[40];
            fmt_val(x, sx);
            snprintf(st->msg, sizeof(st->msg), "Zero: X=%s", sx);
        }
        else
            snprintf(st->msg, sizeof(st->msg), "Aucun zero dans la fenetre");
        break;
    case 1:
    case 2:
        if (find_extremum_near(ctx_f, &f, x_min, x_max, *cursor, c == 2, &x, &y))
        {
            x = clean_val(x, wx);
            y = clean_val(y, 1.0);
            *cursor = x;
            char sx[40], sy[40];
            fmt_val(x, sx);
            fmt_val(y, sy);
            snprintf(st->msg, sizeof(st->msg), "%s: X=%s Y=%s", c == 2 ? "Max" : "Min", sx, sy);
        }
        else
            snprintf(st->msg, sizeof(st->msg), "Aucun %s dans la fenetre", c == 2 ? "maximum" : "minimum");
        break;
    case 3:
    {
        int ids[26];
        static char names[26][8];
        const char *labs[26];
        int no = 0;
        for (int i = 0; i < 26; i++)
        {
            if (i == sel || arr[i] == nullptr || arr[i]->t_size <= 0)
                continue;
            snprintf(names[no], sizeof(names[no]), "%c(x)", (char)((i + 5) % 26 + 'a'));
            labs[no] = names[no];
            ids[no++] = i;
        }
        if (no == 0)
        {
            snprintf(st->msg, sizeof(st->msg), "Il faut une 2e fonction");
            break;
        }
        int pick = 0;
        if (no > 1)
        {
            pick = small_menu(labs, no);
            fill_screen(BACKGROUND_COLOR);
            if (pick < 0)
            {
                st->is_result = false;
                st->msg[0] = '\0';
                break;
            }
        }
        fn_t g;
        if (fn_build(arr[ids[pick]], &g))
        {
            fn_pair p = {&f, &g};
            if (find_root_near(ctx_diff, &p, x_min, x_max, *cursor, &x))
            {
                x = clean_val(x, wx);
                *cursor = x;
                char sx[40], sy[40];
                fmt_val(x, sx);
                fmt_val(fn_eval(&f, x), sy);
                snprintf(st->msg, sizeof(st->msg), "Inters.: X=%s Y=%s", sx, sy);
            }
            else
                snprintf(st->msg, sizeof(st->msg), "Aucune intersection");
            fn_free(&g);
        }
        break;
    }
    case 4:
        st->integ_stage = 1;
        st->integ_a = *cursor;
        st->is_result = false;
        {
            char sa[40];
            fmt_val(*cursor, sa);
            snprintf(st->msg, sizeof(st->msg), "a=%s : deplacer le curseur, OK=b, BACK=annuler", sa);
        }
        break;
    default:
        st->is_result = false;
        st->msg[0] = '\0';
        break;
    }
    fn_free(&f);
}

static void calc_finish_integral(calc_state *st, fill_box **arr, int sel, double cursor)
{
    fn_t f;
    st->integ_stage = 0;
    st->is_result = true;
    if (!fn_build(arr[sel], &f))
    {
        st->msg[0] = '\0';
        return;
    }
    double a = st->integ_a, b = cursor, r = 0;
    if (integrate(ctx_f, &f, a, b, &r))
    {
        st->shade = true;
        st->shade_a = a < b ? a : b;
        st->shade_b = a < b ? b : a;
        st->shade_fn = sel;
        char sa[40], sb[40], sr[40];
        fmt_val(a, sa);
        fmt_val(b, sb);
        fmt_val(r, sr);
        snprintf(st->msg, sizeof(st->msg), "Int [%s;%s] = %s", sa, sb, sr);
    }
    else
        snprintf(st->msg, sizeof(st->msg), "Integrale non definie");
    fn_free(&f);
}

// Hachure l'aire entre la courbe et l'axe des abscisses.
static void draw_shade(const fn_t *f, double sa, double sb,
                       double x_min, double x_max, double y_min, double y_max)
{
    double wx = x_max - x_min, wy = y_max - y_min;
    if (!(wx > 0) || !(wy > 0))
        return;
    int pa = (int)((sa - x_min) / wx * SCREEN_WIDTH);
    int pb = (int)((sb - x_min) / wx * SCREEN_WIDTH);
    if (pa < 0)
        pa = 0;
    if (pb > SCREEN_WIDTH - 1)
        pb = SCREEN_WIDTH - 1;
    int y0 = (int)((0 - y_min) / wy * SCREEN_HEIGHT);
    if (y0 < 0)
        y0 = 0;
    if (y0 > SCREEN_HEIGHT - 1)
        y0 = SCREEN_HEIGHT - 1;
    for (int px = pa; px <= pb; px++)
    {
        double x = x_min + (px + 0.5) * wx / SCREEN_WIDTH;
        double v = fn_eval(f, x);
        if (!finite_d(v))
            continue;
        double fy = (v - y_min) / wy * SCREEN_HEIGHT;
        int py = fy < 0 ? 0 : (fy > SCREEN_HEIGHT - 1 ? SCREEN_HEIGHT - 1 : (int)fy);
        int lo = py < y0 ? py : y0;
        int hi = py < y0 ? y0 : py;
        fill_rect(lo, px, hi - lo + 1, 1, 0xB5FF);
    }
}

/* ===================== Aimant sur les intersections =====================
 * En deplacant le curseur le long de la fonction selectionnee, il "colle" aux
 * intersections avec les autres fonctions : un repere rouge et un message
 * l'indiquent tant que le curseur y est. */

#define SNAP_MAX 32
#define SNAP_MAX_OTHERS 8
#define SNAP_RADIUS_PX 6

typedef struct
{
    double x, y;
    int other; // indice de l'autre fonction
} snap_pt;

typedef struct
{
    bool valid;
    double x_min, x_max;
    int sel;
    unsigned hash;
    int n;
    snap_pt pts[SNAP_MAX];
} snap_cache;

static unsigned arr_hash(fill_box **arr)
{
    unsigned h = 2166136261u;
    for (int i = 0; i < 100; i++)
    {
        if (arr[i] == nullptr || arr[i]->t_size <= 0)
            continue;
        h = (h ^ (unsigned)i) * 16777619u;
        for (int k = 0; k < arr[i]->t_size; k++)
            h = (h ^ (unsigned char)arr[i]->text[k]) * 16777619u;
    }
    return h;
}

// Recalcule les intersections seulement si la fenetre, la fonction selectionnee
// ou une formule a change.
static void snap_update(snap_cache *sc, fill_box **arr, int sel, double x_min, double x_max)
{
    unsigned h = arr_hash(arr);
    if (sc->valid && sc->sel == sel && sc->x_min == x_min && sc->x_max == x_max && sc->hash == h)
        return;
    sc->valid = true;
    sc->sel = sel;
    sc->x_min = x_min;
    sc->x_max = x_max;
    sc->hash = h;
    sc->n = 0;
    if (sel < 0 || sel >= 100 || arr[sel] == nullptr || arr[sel]->t_size <= 0)
        return;
    fn_t f;
    if (!fn_build(arr[sel], &f))
        return;
    int others = 0;
    for (int i = 0; i < 100 && others < SNAP_MAX_OTHERS && sc->n < SNAP_MAX; i++)
    {
        if (i == sel || arr[i] == nullptr || arr[i]->t_size <= 0)
            continue;
        others++;
        fn_t g;
        if (!fn_build(arr[i], &g))
            continue;
        fn_pair p = {&f, &g};
        double xs[SNAP_MAX];
        int nr = collect_roots(ctx_diff, &p, x_min, x_max, xs, SNAP_MAX - sc->n);
        for (int k = 0; k < nr && sc->n < SNAP_MAX; k++)
        {
            snap_pt *pt = &sc->pts[sc->n++];
            pt->x = xs[k];
            pt->y = fn_eval(&f, xs[k]);
            pt->other = i;
        }
        fn_free(&g);
    }
    fn_free(&f);
}

// Deplacement du curseur de `old` vers *cur dans le sens dir (+1/-1) : colle a
// la premiere intersection situee devant, a moins de SNAP_RADIUS_PX pixels.
// Une intersection ou l'on se trouve deja n'est pas reprise (on peut repartir).
static void snap_apply(const snap_cache *sc, double old, double *cur, int dir, double wx)
{
    double radius = wx * SNAP_RADIUS_PX / SCREEN_WIDTH;
    double eps = 1e-9 * wx;
    int best = -1;
    for (int i = 0; i < sc->n; i++)
    {
        double xp = sc->pts[i].x;
        bool ahead = dir > 0 ? (xp > old + eps && xp <= *cur + radius)
                             : (xp < old - eps && xp >= *cur - radius);
        if (!ahead)
            continue;
        if (best < 0 || (dir > 0 ? xp < sc->pts[best].x : xp > sc->pts[best].x))
            best = i;
    }
    if (best >= 0)
        *cur = sc->pts[best].x;
}

// Indice de l'intersection sur laquelle se trouve le curseur, ou -1.
static int snap_find(const snap_cache *sc, double cursor, double wx)
{
    for (int i = 0; i < sc->n; i++)
        if (fabs(sc->pts[i].x - cursor) <= 1e-9 * wx)
            return i;
    return -1;
}

static void draw_snap_marker(const snap_pt *p, double x_min, double x_max, double y_min, double y_max)
{
    double wx = x_max - x_min, wy = y_max - y_min;
    if (!(wx > 0) || !(wy > 0) || !finite_d(p->y))
        return;
    int px = (int)((p->x - x_min) / wx * SCREEN_WIDTH);
    int py = (int)((p->y - y_min) / wy * SCREEN_HEIGHT);
    if (px < 7 || px > SCREEN_WIDTH - 8 || py < 7 || py > SCREEN_HEIGHT - 8)
        return;
    const uint16_t col = COLOR_RED;
    // fill_rect(x = ligne verticale, y = colonne, w = hauteur, h = largeur) :
    // carre de 13x13 pixels centre sur (py, px), traits de 2 pixels.
    fill_rect(py - 6, px - 6, 2, 13, col); // cote bas
    fill_rect(py + 5, px - 6, 2, 13, col); // cote haut
    fill_rect(py - 6, px - 6, 13, 2, col); // cote gauche

    
    fill_rect(py - 6, px + 5, 13, 2, col); // cote droit
}

extern int g_angle_mode; // reglage Angles (settings.cpp)

/* ===================== Fenetre & zooms automatiques ===================== */

typedef struct
{
    double x_min, x_max, y_min, y_max;
    double step; // pas du curseur, 0 = auto (fenetre / 100)
} win_t;

static void zoom_standard(win_t *w)
{
    w->x_min = -10;
    w->x_max = 10;
    w->y_min = -10;
    w->y_max = 10;
    w->step = 0;
}

// x = +-47/24 pi (ou +-47/24*180 en degres), y = +-4, pas = pi/24.
static void zoom_trig(win_t *w)
{
    double k = g_angle_mode ? 180.0 : 3.14159265358979323846;
    w->x_max = 47.0 / 24.0 * k;
    w->x_min = -w->x_max;
    w->y_min = -4;
    w->y_max = 4;
    w->step = k / 24.0;
}

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

// Ajuste Y sur toutes les fonctions, X inchange. Ignore 1 % de valeurs
// extremes de chaque cote (poles, asymptotes) puis ajoute 10 % de marge.
static bool zoom_fit(win_t *w, fill_box **arr)
{
    const int N = 320;
    double *v = (double *)malloc(sizeof(double) * N);
    if (!v)
        return false;
    double lo = INFINITY, hi = -INFINITY;
    for (int i = 0; i < 100; i++)
    {
        fn_t f;
        if (arr[i] == nullptr || !fn_build(arr[i], &f))
            continue;
        int n = 0;
        for (int k = 0; k < N; k++)
        {
            double x = w->x_min + (w->x_max - w->x_min) * k / (N - 1);
            double y = fn_eval(&f, x);
            if (finite_d(y))
                v[n++] = y;
        }
        fn_free(&f);
        if (n == 0)
            continue;
        qsort(v, n, sizeof(double), cmp_d);
        int cut = n / 100;
        if (v[cut] < lo)
            lo = v[cut];
        if (v[n - 1 - cut] > hi)
            hi = v[n - 1 - cut];
    }
    free(v);
    if (!(lo <= hi) || !finite_d(lo) || !finite_d(hi))
        return false;
    if (hi - lo < 1e-9)
    {
        lo -= 1;
        hi += 1;
    }
    double pad = (hi - lo) * 0.1;
    w->y_min = lo - pad;
    w->y_max = hi + pad;
    return finite_d(w->y_min) && finite_d(w->y_max);
}

// Evalue le contenu d'un champ (expression acceptee : -2p, 3/4, ...).
static bool field_value(fill_box *b, double *out)
{
    fn_t f;
    if (!fn_build(b, &f))
        return false;
    double v = fn_eval(&f, 0);
    fn_free(&f);
    if (!finite_d(v))
        return false;
    *out = v;
    return true;
}

static void field_set(fill_box *b, double v)
{
    char t[40];
    snprintf(t, sizeof(t), "%.10f", v);
    int n = (int)strlen(t);
    if (strchr(t, '.'))
    {
        while (n > 0 && t[n - 1] == '0')
            n--;
        if (n > 0 && t[n - 1] == '.')
            n--;
        t[n] = '\0';
    }
    if (strcmp(t, "-0") == 0)
        strcpy(t, "0");
    n = (int)strlen(t);
    memcpy(b->text, t, n + 1);
    b->t_size = n;
    b->curso_pos = n;
}

// Formulaire Xmin / Xmax / Ymin / Ymax / Pas. true si valide par OK.
static bool window_menu(win_t *w)
{
    static const char *labels[5] = {"Xmin", "Xmax", "Ymin", "Ymax", "Pas"};
    double init[5] = {w->x_min, w->x_max, w->y_min, w->y_max, w->step};
    fill_box *b[5];
    for (int i = 0; i < 5; i++)
    {
        b[i] = create_fill_box(70, 0, 34, 250, 2);
        b[i]->color = FRONTGROUND_COLOR;
        field_set(b[i], init[i]);
    }

    int sel = 0;
    bool snd = false, done = false;
    char err[40] = "";

    while (true)
    {
        fill_screen(BACKGROUND_COLOR);
        draw_char(236, 100, (char *)"Fenetre", 0x0000, BACKGROUND_COLOR, 2);
        for (int i = 0; i < 5; i++)
        {
            int v = 185 - i * 38;
            display_fill_box(b[i], v, i == sel, -1, ' ', false);
            draw_char(v + 25, 6, (char *)labels[i], 0x0000, BACKGROUND_COLOR, 2);
        }
        draw_char(28, 10, err, 0xF800, BACKGROUND_COLOR, 1);
        draw_char(14, 10, (char *)"OK=valider  BACK=effacer/quitter  Pas 0=auto",
                  0x0000, BACKGROUND_COLOR, 1);
        flush_display();

        int key = scan_keypad();
        while (key == -1)
        {
            blink_cursor();
            key = scan_keypad();
        }
        sleep_ms(50);

        if (key == UP)
            sel = max(0, sel - 1);
        else if (key == DOWN)
            sel = min(4, sel + 1);
        else if (key == LEFT)
            fill_box_left(b[sel]);
        else if (key == RIGHT)
            fill_box_right(b[sel]);
        else if (key == SECOND)
            toggle(&snd);
        else if (key == BACK)
        {
            if (b[sel]->t_size > 0)
                update_fill_box(b[sel], BACK, snd);
            else
                break;
        }
        else if (key == OK || key == ENTER)
        {
            double v[5];
            bool ok = true;
            for (int i = 0; i < 5 && ok; i++)
                ok = field_value(b[i], &v[i]);
            if (!ok)
                snprintf(err, sizeof(err), "Valeur invalide");
            else if (!(v[0] < v[1]))
                snprintf(err, sizeof(err), "Il faut Xmin < Xmax");
            else if (!(v[2] < v[3]))
                snprintf(err, sizeof(err), "Il faut Ymin < Ymax");
            else if (v[4] < 0)
                snprintf(err, sizeof(err), "Pas >= 0");
            else
            {
                w->x_min = v[0];
                w->x_max = v[1];
                w->y_min = v[2];
                w->y_max = v[3];
                w->step = v[4];
                done = true;
                break;
            }
        }
        else
        {
            err[0] = '\0';
            update_fill_box(b[sel], key, snd);
        }
    }

    for (int i = 0; i < 5; i++)
    {
        free(b[i]->text);
        free(b[i]);
    }
    return done;
}

// Menu TOOLS du graphe. true si la fenetre a change.
static bool window_tools(win_t *w, fill_box **arr)
{
    const char *labels[4] = {"Fenetre", "ZStandard", "ZTrig", "ZoomFit"};
    fill_screen(BACKGROUND_COLOR);
    int c = small_menu(labels, 4);
    fill_screen(BACKGROUND_COLOR);
    switch (c)
    {
    case 0:
        return window_menu(w);
    case 1:
        zoom_standard(w);
        return true;
    case 2:
        zoom_trig(w);
        return true;
    case 3:
        return zoom_fit(w, arr);
    default:
        return false;
    }
}

/* ============ Types de courbes (polaire, parametrique) =================
 * Chaque ligne de fonction a un type. La variable se tape avec la touche X :
 *   cartesienne   y = f(X)
 *   polaire       r = f(X), X etant l'angle theta
 *   parametrique  z = x(X) + i*y(X) : partie reelle = abscisse, partie
 *                 imaginaire = ordonnee (on reutilise le moteur complexe).
 * Le parametre parcourt [0 ; 2pi] (ou [0 ; 360] en degres). */
enum
{
    FT_CART = 0,
    FT_POLAR,
    FT_PARAM
};
static int s_fn_type[100];

static void param_range(double *t0, double *t1)
{
    *t0 = 0;
    *t1 = g_angle_mode == 1 ? 360.0 : 2 * M_PI;
}

static void graph_curve(fill_box *b, int type, double x_min, double x_max, double y_min, double y_max,
                        uint16_t color)
{
    int n = 0;
    token *t = parse_string_to_token(b->text, b->t_size, &n);
    token *o = shunting_yard(t, n);
    int y = count_yarded(t, n);
    double t0, t1;
    param_range(&t0, &t1);
    const int N = 720;
    bool has_prev = false;
    int ppx = 0, ppy = 0;
    for (int i = 0; i <= N; i++)
    {
        double tt = t0 + (t1 - t0) * i / N;
        double X, Y;
        if (type == FT_POLAR)
        {
            double r = evaluate_npi(o, y, tt, 'X');
            double th = g_angle_mode == 1 ? tt * M_PI / 180.0 : tt;
            X = r * cos(th);
            Y = r * sin(th);
        }
        else
        {
            complex z = evaluate_npi_c_var(o, y, complex{tt, 0}, 'X');
            X = z.re;
            Y = z.im;
        }
        if (!isfinite(X) || !isfinite(Y))
        {
            has_prev = false;
            continue;
        }
        double fx = (X - x_min) / (x_max - x_min) * SCREEN_WIDTH;
        double fy = SCREEN_HEIGHT - (Y - y_min) / (y_max - y_min) * SCREEN_HEIGHT;
        if (fabs(fx) > 4000 || fabs(fy) > 4000)
        {
            has_prev = false;
            continue;
        }
        int px = (int)lround(fx), py = (int)lround(fy);
        if (has_prev && abs(px - ppx) < 400 && abs(py - ppy) < 400)
        {
            uk_line(ppx, ppy, px, py, color);
            uk_line(ppx, ppy + 1, px, py + 1, color);
        }
        has_prev = true;
        ppx = px;
        ppy = py;
    }
    token_free_matrices(t, n);
    free(t);
    free(o);
}

/* Copie de arr ou seules les fonctions cartesiennes restent : les outils
 * d'analyse (zero, extremum, intersections, ZoomFit) ne valent que pour elles. */
static void cart_only(fill_box **arr, fill_box **out)
{
    for (int i = 0; i < 100; i++)
        out[i] = (arr[i] && s_fn_type[i] == FT_CART) ? arr[i] : nullptr;
}

/* ========================= Tableau de valeurs ========================== */

static double s_tbl_start = 0, s_tbl_step = 1;

/* Retourne 0 = onglet Graphe, 1 = onglet Fonctions, 2 = quitter l'app. */
static int values_table(fill_box **arr, const uint16_t *palet, text_box *Formula, text_box *Graph, text_box *Table)
{
    int idx[100], m = 0;
    for (int i = 0; i < 100; i++)
        if (arr[i] && arr[i]->t_size > 0 && s_fn_type[i] == FT_CART)
            idx[m++] = i;

    const int TB_NCOL = 3, ROW_H = 20, TOP = 24, XW = 80;
    const int VIS = (SCREEN_HEIGHT - TOP - 20 - UK_FOOTER_H) / ROW_H;
    int first_col = 0, first = 0, sel = 0;
    bool tabs = false;
    while (true)
    {
        fill_screen(BACKGROUND_COLOR);
        display_text_box(Formula, 0, 0, false);
        display_text_box(Graph, 0, 0, false);
        display_text_box(Table, 0, 0, tabs);

        int ncol = m - first_col < TB_NCOL ? m - first_col : TB_NCOL;
        int cw = ncol > 0 ? (SCREEN_WIDTH - XW) / ncol : SCREEN_WIDTH - XW;
        uk_rect(0, TOP, SCREEN_WIDTH, ROW_H, FRONTGROUND_COLOR_BIS);
        uk_text_center(0, TOP + 6, XW, "x", COLOR_BLACK, 1);
        token *rpn[3] = {NULL, NULL, NULL};
        token *raw[3] = {NULL, NULL, NULL};
        int ny[3] = {0, 0, 0}, nt[3] = {0, 0, 0};
        for (int c = 0; c < ncol; c++)
        {
            int f = idx[first_col + c];
            char name[8];
            snprintf(name, sizeof(name), "%c(x)", (char)((f + 5) % 26 + 'a'));
            uk_rect(XW + c * cw + 4, TOP + 7, 6, 6, palet[f % 14]);
            uk_text_center(XW + c * cw + 8, TOP + 6, cw - 8, name, COLOR_BLACK, 1);
            raw[c] = parse_string_to_token(arr[f]->text, arr[f]->t_size, &nt[c]);
            rpn[c] = shunting_yard(raw[c], nt[c]);
            ny[c] = count_yarded(raw[c], nt[c]);
        }
        for (int r = 0; r < VIS; r++)
        {
            int k = first + r;
            double x = s_tbl_start + k * s_tbl_step;
            if (fabs(x) < 1e-12 * fabs(s_tbl_step))
                x = 0;
            int y = TOP + ROW_H * (r + 1);
            uint16_t bg = (!tabs && k == sel) ? UK_SEL_BG : (k % 2 ? FRONTGROUND_COLOR_BIS : FRONTGROUND_COLOR);
            uk_rect(0, y, SCREEN_WIDTH, ROW_H - 1, bg);
            char buf[24];
            uk_fmt(x, buf, 12);
            uk_text_center(0, y + 6, XW, buf, COLOR_BLACK, 1);
            for (int c = 0; c < ncol; c++)
            {
                double v = evaluate_npi(rpn[c], ny[c], x, 'X');
                if (isfinite(v))
                    uk_fmt(v, buf, cw / 5 - 1);
                else
                    strcpy(buf, "--");
                uk_text_right(XW + (c + 1) * cw - 6, y + 6, buf, COLOR_BLACK, 1);
                uk_rect(XW + c * cw, y, 1, ROW_H - 1, COLOR_GRAY);
            }
        }
        for (int c = 0; c < ncol; c++)
        {
            token_free_matrices(raw[c], nt[c]);
            free(raw[c]);
            free(rpn[c]);
        }
        if (m == 0)
            uk_text_center(0, 110, SCREEN_WIDTH, "Aucune fonction cartesienne a tabuler", COLOR_GRAY, 1);
        char hint[64];
        if (tabs)
            snprintf(hint, sizeof(hint), "LEFT: Graphe   DOWN: tableau   BACK: quitter");
        else
            snprintf(hint, sizeof(hint), "OK: debut/pas  LEFT/RIGHT: colonnes  BACK: onglets");
        uk_footer(hint);
        flush_display();

        int key = uk_wait_key();
        if (tabs)
        {
            if (key == LEFT)
                return 0;
            if (key == DOWN || key == OK || key == ENTER)
                tabs = false;
            else if (key == BACK)
                return 2;
            continue;
        }
        switch (key)
        {
        case UP:
            sel--;
            break;
        case DOWN:
            sel++;
            break;
        case LEFT:
            if (first_col > 0)
                first_col--;
            break;
        case RIGHT:
            if (first_col + TB_NCOL < m)
                first_col++;
            break;
        case BACK:
            tabs = true;
            break;
        case OK:
        case ENTER:
        {
            const char *opts[2] = {"Valeur de depart", "Pas"};
            int c = uk_menu("Parametres du tableau", opts, 2, 0);
            double v;
            if (c == 0 && uk_input("Tableau de valeurs", "x de depart =", &v, -1))
            {
                s_tbl_start = v;
                sel = first = 0;
            }
            else if (c == 1 && uk_input("Tableau de valeurs", "Pas =", &v, -1) && v != 0)
            {
                s_tbl_start += sel * s_tbl_step;
                s_tbl_step = v;
                sel = first = 0;
            }
            break;
        }
        default:
            break;
        }
        if (sel < first)
            first = sel;
        if (sel >= first + VIS)
            first = sel - VIS + 1;
    }
}

int Grapher()
{
    bool snd = false;

    text_box *Graph = create_text_box(107, 220, 20, 106, 1, false);
    text_box *Formula = create_text_box(0, 220, 20, 107, 1, false);
    text_box *Table = create_text_box(213, 220, 20, 107, 1, false);
    Graph->text = (char *)"Graphe";
    Graph->t_size = 6;
    Formula->text = (char *)"Fonctions";
    Formula->t_size = 9;
    Table->text = (char *)"Tableau";
    Table->t_size = 7;
    memset(s_fn_type, 0, sizeof(s_fn_type));
    fill_box *cart_arr[100];

    fill_box *n = create_fill_box(0, 20, 40, 320, 3);
    const uint16_t palet[] = {
        COLOR_RED,
        COLOR_LIME,
        COLOR_BLUE,
        COLOR_YELLOW,
        COLOR_CYAN,
        COLOR_MAGENTA,
        COLOR_SILVER,
        COLOR_GRAY,
        COLOR_MAROON,
        COLOR_OLIVE,
        COLOR_GREEN,
        COLOR_PURPLE,
        COLOR_TEAL,
        COLOR_NAVY};

    fill_box **arr_fill_box = (fill_box **)malloc(sizeof(fill_box *) * 100);
    for (int i = 0; i < 100; i++)
    {
        arr_fill_box[i] = nullptr;
    }

    int selected_fill_box = 0;
    arr_fill_box[0] = n;
    int first_display = 0;
    bool show_graph = false;
    calc_state cs;
    memset(&cs, 0, sizeof(cs));
    snap_cache sc;
    memset(&sc, 0, sizeof(sc));
    int prev_snap = -1;
    bool cross_valid = false; // croix du curseur actuellement dessinee
    int cross_x = 0, cross_y = 0;
    double x_min = -5;
    double x_max = 5;
    double y_min = -5;
    double y_max = 5;
    double cursor_pos = 0;
    double cursor_step = 0;
    bool follow =true;
    int last_pressed = scan_keypad_nowait(); // pas bloquant : l'ecran s'affiche tout de suite
    fill_screen(BACKGROUND_COLOR);

    while (1)
    {
        int mv = 0;
        double border = 0.25;
        if(follow){
        if (y_max - Y_pos < (y_max - y_min) * border)
        {
            fill_screen(BACKGROUND_COLOR);

            double d = abs(y_max - Y_pos - (y_max - y_min) * border * 2);
            y_max += d;
            y_min += d;
        }
        else if (Y_pos - y_min < (y_max - y_min) * border)
        {
            fill_screen(BACKGROUND_COLOR);

            double d = abs(Y_pos - y_min - (y_max - y_min) * border * 2);
            y_max -= d;
            y_min -= d;
        }
        if (x_max - X_pos < (x_max - x_min) * border)
        {
            fill_screen(BACKGROUND_COLOR);

            double d = abs(x_max - X_pos - (x_max - x_min) * border * 2);
            x_max += d;
            x_min += d;
        }
        else if (X_pos - x_min < (x_max - x_min) * border)
        {
            fill_screen(BACKGROUND_COLOR);

            double d = abs(X_pos - x_min - (x_max - x_min) * border * 2);
            x_max -= d;
            x_min -= d;
        }
    }
        if (!show_graph)
        {
            for (int i = std::max(first_display, 0); i < first_display + 6; i++)
            {
                if (arr_fill_box[i] == nullptr)
                    continue;
                int old_mv = mv;
                int tok_n = 0;
                int input_size;
                display_set_source(arr_fill_box[i]->text, arr_fill_box[i]->t_size);
                token *toks = parse_string_to_token(arr_fill_box[i]->text, arr_fill_box[i]->t_size, &tok_n);
                Parser p = {toks, tok_n, 0};

                while (p.pos < p.n)
                {
                    int before = p.pos;
                    ASTNode *node = parse_equation(&p);
                    if (p.pos == before)
                        p.pos++;
                    if (node)
                        mv += max(0, measure(node, 2).h - 16);
                }
                token_free_matrices(toks, tok_n);
                free(toks);
                arr_fill_box[i]->h = mv - old_mv + 40;
                //                display_fill_box(arr_fill_box[i], 160 - (i - selected_fill_box + 4) * 40 + old_mv, (i - selected_fill_box) % HISTORY_SIZE == 0, -1, ' ');

                char pfx = s_fn_type[i] == FT_POLAR ? 'r' : (s_fn_type[i] == FT_PARAM ? 'z' : 'f');
                display_fill_box(arr_fill_box[i], 160 - (i - std::max(first_display, 0)) * 40 - mv, i == selected_fill_box, i, pfx, false);
            }
        }
        else
        {
            // Efface la croix de l'image precedente (changement de fonction,
            // deplacement, zoom...) : les courbes sont redessinees juste apres.
            if (cross_valid)
            {
                fill_rect(cross_y, cross_x - 5, 1, 10, BACKGROUND_COLOR);
                fill_rect(cross_y - 5, cross_x, 10, 1, BACKGROUND_COLOR);
                cross_valid = false;
            }
            X_valid = false;

            // Intersection sous le curseur ? (efface l'ancien repere/message
            // quand on la quitte ou qu'on en change)
            cart_only(arr_fill_box, cart_arr);
            snap_update(&sc, cart_arr, selected_fill_box, x_min, x_max);
            int snap_i = (selected_fill_box >= 0 && s_fn_type[selected_fill_box] == FT_CART)
                             ? snap_find(&sc, cursor_pos, x_max - x_min)
                             : -1;
            if (prev_snap >= 0 && snap_i != prev_snap)
                fill_screen(BACKGROUND_COLOR);
            prev_snap = snap_i;

            axis(x_min, x_max, y_min, y_max);

            if (cs.shade && cs.shade_fn >= 0 && cs.shade_fn < 100)
            {
                fn_t sf;
                if (fn_build(arr_fill_box[cs.shade_fn], &sf))
                {
                    draw_shade(&sf, cs.shade_a, cs.shade_b, x_min, x_max, y_min, y_max);
                    fn_free(&sf);
                }
            }

            for (int i = 0; i < 100; i++)
            {
                // Les lignes vides ne sont pas tracees (elles donnaient y = 0).
                if (arr_fill_box[i] != nullptr && arr_fill_box[i]->t_size > 0 && s_fn_type[i] != FT_CART)
                    graph_curve(arr_fill_box[i], s_fn_type[i], x_min, x_max, y_min, y_max, palet[i % 14]);
                else if (arr_fill_box[i] != nullptr && arr_fill_box[i]->t_size > 0)
                {
                    int tokenized_size = 0;
                    token *tokenized = parse_string_to_token(arr_fill_box[i]->text, arr_fill_box[i]->t_size, &tokenized_size);
                    // id = i + 1 pour la fonction selectionnee (curseur et infos), -1 sinon.
                    graph(cursor_pos, tokenized, x_min, x_max, y_min, y_max, tokenized_size, palet[i % 14], i == selected_fill_box ? i + 1 : -1, true, 'X');
                    token_free_matrices(tokenized, tokenized_size);
                    free(tokenized);
                    // Croix seulement si le point du curseur est sur la courbe
                    // (hors domaine, elle restait a son ancienne place).
                    if (i == selected_fill_box && X_valid)
                    {
                        fill_rect(Y_cursor, X_cursor - 5, 1, 10, 0x000000);
                        fill_rect(Y_cursor - 5, X_cursor, 10, 1, 0x000000);
                        cross_x = X_cursor;
                        cross_y = Y_cursor;
                        cross_valid = true;
                    }
                }
            }

            if (snap_i >= 0)
                draw_snap_marker(&sc.pts[snap_i], x_min, x_max, y_min, y_max);

            char snap_msg[80];
            snap_msg[0] = '\0';
            if (snap_i >= 0)
            {
                char sx[40], sy[40];
                fmt_val(sc.pts[snap_i].x, sx);
                fmt_val(sc.pts[snap_i].y, sy);
                snprintf(snap_msg, sizeof(snap_msg), "Intersection %c/%c : X=%s Y=%s",
                         (char)((selected_fill_box + 5) % 26 + 'a'),
                         (char)((sc.pts[snap_i].other + 5) % 26 + 'a'), sx, sy);
            }

            if (cs.msg[0] || snap_msg[0])
            {
                text_box *mb = create_text_box(0, 22, 20, 320, 2, false);
                snprintf(mb->text, 100, "%s", cs.msg[0] ? cs.msg : snap_msg);
                mb->t_size = (int)strlen(mb->text);
                display_text_box(mb, 0, 0, false);
                free_text_box(mb);
            }
        }
        display_text_box(Formula, 0, 0, !show_graph && selected_fill_box == -1);
        display_text_box(Graph, 0, 0, show_graph && selected_fill_box == -1);
        display_text_box(Table, 0, 0, false);
        flush_display();
        sleep_ms(150);

        last_pressed = scan_keypad();
        while (last_pressed == -1)
        {
            if (!show_graph)
            {
                blink_cursor();
            }
            last_pressed = scan_keypad();
        }
        // Un message de resultat disparait a la touche suivante (BACK ne fait
        // alors que le fermer).
        if (cs.is_result)
        {
            cs.msg[0] = '\0';
            cs.is_result = false;
            cs.shade = false;
            fill_screen(BACKGROUND_COLOR);
            if (last_pressed == BACK)
                last_pressed = -2;
        }
        switch (last_pressed)
        {
        case SECOND:
            toggle(&snd);
            break;
        case BACK:
            if (cs.integ_stage)
            {
                cs.integ_stage = 0;
                cs.msg[0] = '\0';
                fill_screen(BACKGROUND_COLOR);
                break;
            }
            if (!show_graph && selected_fill_box >= 0 && arr_fill_box[selected_fill_box]->t_size > 0)
            {
                update_fill_box(arr_fill_box[selected_fill_box], last_pressed, snd);
            }
            else
            {
                return 0;
            }
            break;
        case UP:
            if (show_graph)
            {
                selected_fill_box = std::max(-1, selected_fill_box - 1);
            }
            else
            {
                selected_fill_box = std::max(-1, selected_fill_box - 1);
                if (selected_fill_box < first_display)
                {
                    first_display = selected_fill_box;
                }
            }
            break;
        case DOWN:
            if (!show_graph)
            {
                selected_fill_box = std::min(25, selected_fill_box + 1);
                if (arr_fill_box[selected_fill_box] == nullptr)
                {
                    arr_fill_box[selected_fill_box] = create_fill_box(0, 20, 40, 320, 3);
                }
                if (first_display + 4 < selected_fill_box)
                {
                    first_display++;
                }
            }
            else
            {
                selected_fill_box = min(25, selected_fill_box + 1);
            }
            break;
        case RIGHT:
            if (show_graph && selected_fill_box != -1)
            {

                fill_rect(Y_cursor, X_cursor - 5, 1, 10, BACKGROUND_COLOR);
                fill_rect(Y_cursor - 5, X_cursor, 10, 1, BACKGROUND_COLOR);

                // Pas = 1/100 de la fenetre ; le curseur est recale sur cette grille
                // (apres un aimant il n'est plus sur un multiple du pas).
                double old_cursor = cursor_pos;
                double step = cursor_step > 0 ? cursor_step : (x_max - x_min) / 100;
                follow = true;
                cursor_pos = round((old_cursor + step) / step) * step;
                cart_only(arr_fill_box, cart_arr);
                snap_update(&sc, cart_arr, selected_fill_box, x_min, x_max);
                snap_apply(&sc, old_cursor, &cursor_pos, +1, x_max - x_min);
            }
            else if (!show_graph && selected_fill_box != -1)
            {
                fill_box_right(arr_fill_box[selected_fill_box]);
            }
            else if (show_graph)
            {
                /* Onglet Tableau (a droite de Graphe). */
                int r = values_table(arr_fill_box, palet, Formula, Graph, Table);
                fill_screen(BACKGROUND_COLOR);
                if (r == 2)
                    return 0;
                if (r == 1)
                    show_graph = false;
                selected_fill_box = -1;
            }
            else
            {
                fill_screen(BACKGROUND_COLOR);
                selected_fill_box = -1;
                show_graph = true;
            }
            break;
        case LEFT:
            if (show_graph && selected_fill_box != -1)
            {
                fill_rect(20, 0, 200, 340, BACKGROUND_COLOR);
                double old_cursor = cursor_pos;
                double step = cursor_step > 0 ? cursor_step : (x_max - x_min) / 100;
                follow = true;
                cursor_pos = round((old_cursor - step) / step) * step;
                cart_only(arr_fill_box, cart_arr);
                snap_update(&sc, cart_arr, selected_fill_box, x_min, x_max);
                snap_apply(&sc, old_cursor, &cursor_pos, -1, x_max - x_min);
            }
            else if (!show_graph && selected_fill_box != -1)
            {
                fill_box_left(arr_fill_box[selected_fill_box]);
            }
            else
            {
                fill_screen(BACKGROUND_COLOR);
                show_graph = false;
                selected_fill_box = -1;
            }
            break;
        case OK:
        case ENTER:
            if (show_graph && selected_fill_box >= 0 && selected_fill_box < 100 &&
                arr_fill_box[selected_fill_box] != nullptr && arr_fill_box[selected_fill_box]->t_size > 0)
            {
                cart_only(arr_fill_box, cart_arr);
                if (s_fn_type[selected_fill_box] != FT_CART)
                {
                    snprintf(cs.msg, sizeof(cs.msg), "Calculs : courbes y=f(x) seulement");
                    cs.is_result = true;
                }
                else if (cs.integ_stage == 1)
                    calc_finish_integral(&cs, cart_arr, selected_fill_box, cursor_pos);
                else
                    calc_menu(&cs, cart_arr, selected_fill_box, x_min, x_max, &cursor_pos);
            }
            break;
        case TOOLS:
            if (show_graph)
            {
                win_t w = {x_min, x_max, y_min, y_max, cursor_step};
                cart_only(arr_fill_box, cart_arr);
                if (window_tools(&w, cart_arr))
                {
                    x_min = w.x_min;
                    x_max = w.x_max;
                    y_min = w.y_min;
                    y_max = w.y_max;
                    cursor_step = w.step;
                    cs.shade = false;
                    cs.integ_stage = 0;
                    cs.msg[0] = '\0';
                    cs.is_result = false;
                    if (cursor_pos < x_min || cursor_pos > x_max)
                        cursor_pos = 0.5 * (x_min + x_max);
                    X_pos = cursor_pos;
                    Y_pos = 0.5 * (y_min + y_max);
                    follow = false; // pas de recentrage avant que le curseur bouge
                }
                fill_screen(BACKGROUND_COLOR);
            }
            else if (selected_fill_box >= 0)
            {
                /* Ecran Fonctions : type de la courbe ou insertion d'une fonction. */
                const char *opts[4] = {"Cartesienne  y=f(x)", "Polaire  r=f(t)",
                                       "Parametrique  x(t)+i*y(t)", "Inserer une fonction..."};
                int c = uk_menu("Type de courbe", opts, 4, s_fn_type[selected_fill_box]);
                fill_screen(BACKGROUND_COLOR);
                if (c >= 0 && c <= 2)
                    s_fn_type[selected_fill_box] = c;
                else if (c == 3)
                {
                    update_fill_box(arr_fill_box[selected_fill_box], menu_tools(), snd);
                    fill_screen(BACKGROUND_COLOR);
                }
            }
            break;
        default:
            if (show_graph)
            {
                if (last_pressed == PLUS)
                {
                    fill_rect(20, 0, 200, 340, BACKGROUND_COLOR);
                    x_min /= 2;
                    x_max /= 2;
                    y_min /= 2;
                    y_max /= 2;
                }
                else if (last_pressed == MINUS)
                {
                    fill_rect(20, 0, 200, 340, BACKGROUND_COLOR);
                    x_min *= 2;
                    x_max *= 2;
                    y_min *= 2;
                    y_max *= 2;
                }
            }
            else if (selected_fill_box >= 0 && !show_graph)
            {
                update_fill_box(arr_fill_box[selected_fill_box], last_pressed, snd);
            }
            break;
        }
    }
}