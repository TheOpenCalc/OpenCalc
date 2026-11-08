/* App Statistiques.
 *
 * Onglets :
 *   Donnees     tableau a deux colonnes : X et Y (Y = effectifs pour les stats
 *               a une variable, ou ordonnees pour les regressions)
 *   Stats       moyenne, ecart-type, quartiles... de X
 *   Regression  ajustement de Y en fonction de X (5 modeles) + prevision
 *   Graphe      nuage de points + courbe, histogramme, boite a moustaches
 *
 * Navigation : la barre d'onglets a le focus quand on remonte tout en haut
 * (UP) ; LEFT/RIGHT y changent d'onglet, DOWN/OK rentrent dans l'onglet.
 * Les donnees restent en memoire tant que la calculatrice est allumee. */
#include "stats_app.h"
#include "Stats.h"
#include "ui_kit.h"
#include "ui.h"
#include "colors.h"
#include "Tools.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define ST_MAX 100
#define ROW_H 17
#define TBL_TOP (UK_HEADER_H + UK_TABS_H + 16)
#define TBL_VIS ((SCREEN_HEIGHT - UK_FOOTER_H - TBL_TOP) / ROW_H)
#define COL_IDX_W 34
#define COL_W 143

static double s_x[ST_MAX], s_y[ST_MAX];
static bool s_ready = false;
static bool s_use_freq = true; /* colonne Y = effectifs dans l'onglet Stats */
static int s_model = REG_LINEAR;
static int s_graph_mode = 0; /* 0 nuage, 1 histogramme, 2 boite */
static double s_pred_x = NAN;

enum
{
    TAB_DATA = 0,
    TAB_STATS,
    TAB_REG,
    TAB_GRAPH,
    TAB_COUNT
};
static const char *k_tabs[TAB_COUNT] = {"Donnees", "Stats", "Regression", "Graphe"};

static void data_init(void)
{
    if (s_ready)
        return;
    for (int i = 0; i < ST_MAX; i++)
        s_x[i] = s_y[i] = NAN;
    s_ready = true;
}

/* Nombre de lignes utilisees (jusqu'a la derniere cellule remplie). */
static int data_rows(void)
{
    int n = 0;
    for (int i = 0; i < ST_MAX; i++)
        if (!isnan(s_x[i]) || !isnan(s_y[i]))
            n = i + 1;
    return n;
}

/* Valeurs de X et effectifs associes, pour les stats a une variable. */
static int collect_one_var(double *x, double *w)
{
    int m = 0, rows = data_rows();
    for (int i = 0; i < rows; i++)
    {
        if (isnan(s_x[i]))
            continue;
        x[m] = s_x[i];
        w[m] = (s_use_freq && !isnan(s_y[i])) ? s_y[i] : 1.0;
        m++;
    }
    return m;
}

/* Couples (X, Y) complets, pour les regressions et le nuage. */
static int collect_pairs(double *x, double *y)
{
    int m = 0, rows = data_rows();
    for (int i = 0; i < rows; i++)
    {
        if (isnan(s_x[i]) || isnan(s_y[i]))
            continue;
        x[m] = s_x[i];
        y[m] = s_y[i];
        m++;
    }
    return m;
}

static void sort_by_x(void)
{
    int rows = data_rows();
    for (int i = 1; i < rows; i++)
    {
        double kx = s_x[i], ky = s_y[i];
        int j = i - 1;
        /* Les lignes sans X partent a la fin. */
        while (j >= 0 && (isnan(s_x[j]) ? !isnan(kx) : (!isnan(kx) && s_x[j] > kx)))
        {
            s_x[j + 1] = s_x[j];
            s_y[j + 1] = s_y[j];
            j--;
        }
        s_x[j + 1] = kx;
        s_y[j + 1] = ky;
    }
}

static void load_example(void)
{
    /* Notes d'une classe (X) et effectifs (Y). */
    static const double ex[] = {6, 8, 9, 10, 11, 12, 13, 14, 15, 17};
    static const double ey[] = {1, 2, 3, 5, 4, 6, 3, 2, 2, 1};
    for (int i = 0; i < ST_MAX; i++)
        s_x[i] = s_y[i] = NAN;
    for (int i = 0; i < 10; i++)
    {
        s_x[i] = ex[i];
        s_y[i] = ey[i];
    }
}

/* ------------------------------ Affichage -------------------------------- */

static void draw_frame(int tab, bool tab_focus, const char *hint)
{
    fill_screen(BACKGROUND_COLOR);
    uk_header("Statistiques");
    uk_tabs(k_tabs, TAB_COUNT, tab, tab_focus);
    uk_footer(hint);
}

static void draw_data(int row, int col, int first, bool focus)
{
    int y0 = UK_HEADER_H + UK_TABS_H;
    uk_rect(0, y0, SCREEN_WIDTH, 16, FRONTGROUND_COLOR_BIS);
    uk_text_center(0, y0 + 4, COL_IDX_W, "#", COLOR_BLACK, 1);
    uk_text_center(COL_IDX_W, y0 + 4, COL_W, "X", COLOR_BLACK, 1);
    uk_text_center(COL_IDX_W + COL_W, y0 + 4, SCREEN_WIDTH - COL_IDX_W - COL_W,
                   s_use_freq ? "Y  (effectifs)" : "Y", COLOR_BLACK, 1);

    int rows = data_rows();
    for (int r = 0; r < TBL_VIS; r++)
    {
        int i = first + r;
        if (i >= ST_MAX)
            break;
        int y = TBL_TOP + r * ROW_H;
        uint16_t bg = i % 2 ? FRONTGROUND_COLOR_BIS : FRONTGROUND_COLOR;
        uk_rect(0, y, SCREEN_WIDTH, ROW_H - 1, bg);
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", i + 1);
        uk_text_center(0, y + 4, COL_IDX_W, buf, i < rows ? COLOR_BLACK : COLOR_GRAY, 1);
        for (int c = 0; c < 2; c++)
        {
            int cx = COL_IDX_W + c * COL_W;
            int cw = c == 0 ? COL_W : SCREEN_WIDTH - cx;
            if (focus && i == row && c == col)
            {
                uk_rect(cx, y, cw, ROW_H - 1, UK_SEL_BG);
                uk_frame(cx, y, cw, ROW_H - 1, COLOR_BLACK);
            }
            double v = c == 0 ? s_x[i] : s_y[i];
            if (!isnan(v))
            {
                uk_fmt(v, buf, 20);
                uk_text_right(cx + cw - 6, y + 4, buf, COLOR_BLACK, 1);
            }
        }
        uk_rect(COL_IDX_W, y, 1, ROW_H - 1, COLOR_GRAY);
        uk_rect(COL_IDX_W + COL_W, y, 1, ROW_H - 1, COLOR_GRAY);
    }
    if (rows == 0)
        uk_text_center(0, TBL_TOP + 3 * ROW_H + 4, SCREEN_WIDTH,
                       "Tapez une valeur pour commencer", COLOR_GRAY, 1);
}

typedef struct
{
    const char *label;
    double value;
    bool is_text;
    const char *text;
} line_item;

/* Liste etiquette / valeur avec une ligne selectionnee. */
static void draw_lines(const line_item *items, int n, int sel, int first, bool focus)
{
    const int lh = 19;
    int top = UK_HEADER_H + UK_TABS_H + 2;
    int vis = (SCREEN_HEIGHT - UK_FOOTER_H - top) / lh;
    for (int r = 0; r < vis && first + r < n; r++)
    {
        int i = first + r;
        int y = top + r * lh;
        uint16_t bg = (focus && i == sel) ? UK_SEL_BG : (i % 2 ? FRONTGROUND_COLOR_BIS : FRONTGROUND_COLOR);
        uk_rect(0, y, SCREEN_WIDTH, lh - 1, bg);
        uk_text(8, y + 5, items[i].label, COLOR_BLACK, 1);
        char buf[40];
        const char *val = items[i].text;
        if (!items[i].is_text)
        {
            uk_fmt(items[i].value, buf, 22);
            val = buf;
        }
        if (val)
            uk_text_right(SCREEN_WIDTH - 8, y + 5, val, COLOR_BLACK, 1);
    }
}

static int lines_visible(void)
{
    return (SCREEN_HEIGHT - UK_FOOTER_H - (UK_HEADER_H + UK_TABS_H + 2)) / 19;
}

static int build_stats_items(line_item *it, stats1 *s, bool *ok)
{
    static double x[ST_MAX], w[ST_MAX];
    int m = collect_one_var(x, w);
    *ok = stats_one_var(x, w, m, s);
    int n = 0;
    it[n++] = {"Effectifs (colonne Y)", 0, true, s_use_freq ? "utilises" : "ignores"};
    if (!*ok)
    {
        it[n++] = {"Aucune donnee valide", 0, true, ""};
        return n;
    }
    it[n++] = {"Effectif total  N", s->n, false, NULL};
    it[n++] = {"Moyenne", s->mean, false, NULL};
    it[n++] = {"Ecart-type  sigma", s->sd, false, NULL};
    it[n++] = {"Ecart-type corrige  s", s->sd_s, false, NULL};
    it[n++] = {"Variance", s->var, false, NULL};
    it[n++] = {"Minimum", s->min, false, NULL};
    it[n++] = {"Premier quartile  Q1", s->q1, false, NULL};
    it[n++] = {"Mediane", s->median, false, NULL};
    it[n++] = {"Troisieme quartile  Q3", s->q3, false, NULL};
    it[n++] = {"Maximum", s->max, false, NULL};
    it[n++] = {"Etendue", s->range, false, NULL};
    it[n++] = {"Ecart interquartile", s->iqr, false, NULL};
    it[n++] = {"Somme des x", s->sum, false, NULL};
    it[n++] = {"Somme des x^2", s->sum_sq, false, NULL};
    return n;
}

static int build_reg_items(line_item *it, regression *r, bool *ok, char *pred_buf)
{
    static double x[ST_MAX], y[ST_MAX];
    int m = collect_pairs(x, y);
    *ok = regress(x, y, m, s_model, r);
    int n = 0;
    it[n++] = {"Modele", 0, true, reg_model_name(s_model)};
    it[n++] = {"Equation", 0, true, reg_model_formula(s_model)};
    if (!*ok)
    {
        it[n++] = {m < 2 ? "Il faut au moins 2 couples (X,Y)" : "Ajustement impossible", 0, true, ""};
        return n;
    }
    it[n++] = {"a", r->a, false, NULL};
    it[n++] = {"b", r->b, false, NULL};
    if (s_model == REG_QUAD)
        it[n++] = {"c", r->c, false, NULL};
    else
        it[n++] = {"Coefficient de correlation r", r->r, false, NULL};
    it[n++] = {"Coef. de determination r^2", r->r2, false, NULL};
    it[n++] = {"Nombre de points", (double)r->n, false, NULL};
    if (isnan(s_pred_x))
        it[n++] = {"Prevoir y pour x = ...", 0, true, "OK"};
    else
    {
        char a[24], b[24];
        uk_fmt(s_pred_x, a, 10);
        uk_fmt(reg_eval(r, s_pred_x), b, 12);
        snprintf(pred_buf, 48, "x=%s -> y=%s", a, b);
        it[n++] = {"Prevision", 0, true, pred_buf};
    }
    return n;
}

/* ------------------------------- Graphes --------------------------------- */

#define PX0 34
#define PY0 (UK_HEADER_H + UK_TABS_H + 18)
#define PW (SCREEN_WIDTH - PX0 - 8)
#define PH (SCREEN_HEIGHT - UK_FOOTER_H - PY0 - 14)

typedef struct
{
    double xmin, xmax, ymin, ymax;
} window;

static int map_x(const window *w, double x)
{
    return PX0 + (int)lround((x - w->xmin) / (w->xmax - w->xmin) * PW);
}

static int map_y(const window *w, double y)
{
    return PY0 + PH - (int)lround((y - w->ymin) / (w->ymax - w->ymin) * PH);
}

static void widen(double *lo, double *hi, double margin)
{
    if (!(*hi > *lo))
    {
        *lo -= 1;
        *hi += 1;
        return;
    }
    double d = (*hi - *lo) * margin;
    *lo -= d;
    *hi += d;
}

static void draw_axes(const window *w, bool show_y)
{
    uk_frame(PX0, PY0, PW + 1, PH + 1, COLOR_GRAY);
    if (w->ymin < 0 && w->ymax > 0)
        uk_rect(PX0, map_y(w, 0), PW, 1, COLOR_BLACK);
    if (w->xmin < 0 && w->xmax > 0)
        uk_rect(map_x(w, 0), PY0, 1, PH, COLOR_BLACK);
    char buf[24];
    uk_fmt(w->xmin, buf, 9);
    uk_text(PX0, PY0 + PH + 4, buf, COLOR_BLACK, 1);
    uk_fmt(w->xmax, buf, 9);
    uk_text_right(PX0 + PW, PY0 + PH + 4, buf, COLOR_BLACK, 1);
    if (show_y)
    {
        uk_fmt(w->ymax, buf, 6);
        uk_text_right(PX0 - 2, PY0, buf, COLOR_BLACK, 1);
        uk_fmt(w->ymin, buf, 6);
        uk_text_right(PX0 - 2, PY0 + PH - 7, buf, COLOR_BLACK, 1);
    }
}

static void draw_scatter(void)
{
    static double x[ST_MAX], y[ST_MAX];
    int m = collect_pairs(x, y);
    if (m == 0)
    {
        uk_text_center(0, 110, SCREEN_WIDTH, "Saisissez des couples (X, Y)", COLOR_GRAY, 1);
        return;
    }
    window w = {x[0], x[0], y[0], y[0]};
    for (int i = 1; i < m; i++)
    {
        w.xmin = fmin(w.xmin, x[i]);
        w.xmax = fmax(w.xmax, x[i]);
        w.ymin = fmin(w.ymin, y[i]);
        w.ymax = fmax(w.ymax, y[i]);
    }
    widen(&w.xmin, &w.xmax, 0.08);
    widen(&w.ymin, &w.ymax, 0.12);
    draw_axes(&w, true);

    regression r;
    if (regress(x, y, m, s_model, &r))
    {
        int px_prev = -1, py_prev = 0;
        for (int px = PX0; px <= PX0 + PW; px += 2)
        {
            double xv = w.xmin + (px - PX0) * (w.xmax - w.xmin) / PW;
            double yv = reg_eval(&r, xv);
            if (!isfinite(yv))
            {
                px_prev = -1;
                continue;
            }
            int py = map_y(&w, yv);
            bool inside = py >= PY0 && py <= PY0 + PH;
            if (px_prev >= 0 && inside)
                uk_line(px_prev, py_prev, px, py, COLOR_RED);
            px_prev = inside ? px : -1;
            py_prev = py;
        }
        char eq[48], a[16], b[16], c[16];
        uk_fmt(r.a, a, 8);
        uk_fmt(r.b, b, 8);
        uk_fmt(r.c, c, 8);
        switch (s_model)
        {
        case REG_LINEAR:
            snprintf(eq, sizeof(eq), "y=%sx+%s", a, b);
            break;
        case REG_QUAD:
            snprintf(eq, sizeof(eq), "y=%sx^2+%sx+%s", a, b, c);
            break;
        case REG_EXP:
            snprintf(eq, sizeof(eq), "y=%s*e^(%sx)", a, b);
            break;
        case REG_POWER:
            snprintf(eq, sizeof(eq), "y=%s*x^%s", a, b);
            break;
        default:
            snprintf(eq, sizeof(eq), "y=%s+%s*ln(x)", a, b);
            break;
        }
        /* "+-3" -> "-3" */
        for (char *p = eq; *p; p++)
            if (p[0] == '+' && p[1] == '-')
                memmove(p, p + 1, strlen(p));
        uk_text(PX0 + 4, PY0 + 3, eq, COLOR_RED, 1);
    }
    for (int i = 0; i < m; i++)
    {
        int px = map_x(&w, x[i]), py = map_y(&w, y[i]);
        uk_rect(px - 2, py - 2, 5, 5, 0x001f);
    }
}

static void draw_histogram(void)
{
    static double x[ST_MAX], wt[ST_MAX];
    int m = collect_one_var(x, wt);
    stats1 s;
    if (!stats_one_var(x, wt, m, &s))
    {
        uk_text_center(0, 110, SCREEN_WIDTH, "Saisissez des valeurs dans X", COLOR_GRAY, 1);
        return;
    }
    /* Nombre de classes : regle de Sturges, bornee a 12. */
    int k = (int)ceil(log2(s.n > 1 ? s.n : 1)) + 1;
    if (k < 1)
        k = 1;
    if (k > 12)
        k = 12;
    double lo = s.min, hi = s.max;
    if (!(hi > lo))
    {
        lo -= 0.5;
        hi += 0.5;
        k = 1;
    }
    double bw = (hi - lo) / k;
    double counts[12] = {0};
    double cmax = 0;
    for (int i = 0; i < m; i++)
    {
        if (wt[i] <= 0 || !isfinite(wt[i]))
            continue;
        int b = (int)floor((x[i] - lo) / bw);
        if (b >= k)
            b = k - 1;
        if (b < 0)
            b = 0;
        counts[b] += wt[i];
        cmax = fmax(cmax, counts[b]);
    }
    window w = {lo, hi, 0, cmax * 1.1};
    draw_axes(&w, true);
    for (int b = 0; b < k; b++)
    {
        int x0 = map_x(&w, lo + b * bw), x1 = map_x(&w, lo + (b + 1) * bw);
        int y0 = map_y(&w, counts[b]);
        uk_rect(x0 + 1, y0, x1 - x0 - 1, PY0 + PH - y0, 0x3d7f);
        uk_frame(x0, y0, x1 - x0 + 1, PY0 + PH - y0 + 1, COLOR_NAVY);
    }
    char buf[40], a[12];
    uk_fmt(bw, a, 8);
    snprintf(buf, sizeof(buf), "%d classes de largeur %s", k, a);
    uk_text(PX0 + 4, PY0 + 3, buf, COLOR_BLACK, 1);
}

static void draw_box(void)
{
    static double x[ST_MAX], wt[ST_MAX];
    int m = collect_one_var(x, wt);
    stats1 s;
    if (!stats_one_var(x, wt, m, &s))
    {
        uk_text_center(0, 110, SCREEN_WIDTH, "Saisissez des valeurs dans X", COLOR_GRAY, 1);
        return;
    }
    window w = {s.min, s.max, 0, 1};
    widen(&w.xmin, &w.xmax, 0.08);
    draw_axes(&w, false);
    int yc = PY0 + PH / 2, hh = PH / 4;
    int xmin = map_x(&w, s.min), xq1 = map_x(&w, s.q1), xme = map_x(&w, s.median);
    int xq3 = map_x(&w, s.q3), xmax = map_x(&w, s.max);
    uk_rect(xmin, yc, xq1 - xmin, 1, COLOR_BLACK);
    uk_rect(xq3, yc, xmax - xq3 + 1, 1, COLOR_BLACK);
    uk_rect(xmin, yc - hh / 2, 1, hh, COLOR_BLACK);
    uk_rect(xmax, yc - hh / 2, 1, hh, COLOR_BLACK);
    uk_rect(xq1, yc - hh, xq3 - xq1 + 1, 2 * hh, 0xaedc);
    uk_frame(xq1, yc - hh, xq3 - xq1 + 1, 2 * hh, COLOR_NAVY);
    uk_rect(xme, yc - hh, 2, 2 * hh, COLOR_RED);
    /* Valeurs sous la boite, en evitant les chevauchements. */
    struct
    {
        const char *name;
        double v;
        int px;
    } lab[5] = {{"min", s.min, xmin}, {"Q1", s.q1, xq1}, {"Me", s.median, xme}, {"Q3", s.q3, xq3}, {"max", s.max, xmax}};
    int last_end = -100, row = 0;
    for (int i = 0; i < 5; i++)
    {
        char buf[24], v[12];
        uk_fmt(lab[i].v, v, 7);
        snprintf(buf, sizeof(buf), "%s=%s", lab[i].name, v);
        int tw = uk_text_w(buf, 1);
        int tx = lab[i].px - tw / 2;
        if (tx < 2)
            tx = 2;
        if (tx + tw > SCREEN_WIDTH - 2)
            tx = SCREEN_WIDTH - 2 - tw;
        row = (tx < last_end + 4) ? 1 - row : 0;
        uk_text(tx, yc + hh + 8 + row * 11, buf, lab[i].name[0] == 'M' ? COLOR_RED : COLOR_BLACK, 1);
        last_end = tx + tw;
    }
}

/* -------------------------------- Boucle --------------------------------- */

static void edit_cell(int row, int col, int first_key)
{
    char title[32], prompt[32];
    snprintf(title, sizeof(title), "Ligne %d", row + 1);
    snprintf(prompt, sizeof(prompt), col == 0 ? "X =" : (s_use_freq ? "Y (effectif) =" : "Y ="));
    double v;
    if (uk_input(title, prompt, &v, first_key))
    {
        if (col == 0)
            s_x[row] = v;
        else
            s_y[row] = v;
    }
}

int Statistics(void)
{
    data_init();
    int tab = TAB_DATA;
    bool tab_focus = false;
    int row = 0, col = 0, first = 0;
    int sel_line[TAB_COUNT] = {0, 0, 0, 0};
    int first_line[TAB_COUNT] = {0, 0, 0, 0};
    static line_item items[24];
    char pred_buf[48];

    while (true)
    {
        int n_items = 0;
        const char *hint = "";
        stats1 st;
        regression rg;
        bool ok = false;
        switch (tab)
        {
        case TAB_DATA:
            hint = "OK: saisir  BACK: effacer  TOOLS: options";
            break;
        case TAB_STATS:
            n_items = build_stats_items(items, &st, &ok);
            hint = "OK sur Effectifs: utiliser/ignorer Y";
            break;
        case TAB_REG:
            n_items = build_reg_items(items, &rg, &ok, pred_buf);
            hint = "OK: changer de modele / prevoir y";
            break;
        case TAB_GRAPH:
            hint = "OK: nuage / histogramme / boite";
            break;
        }
        if (n_items > 0 && sel_line[tab] >= n_items)
            sel_line[tab] = n_items - 1;

        draw_frame(tab, tab_focus, tab_focus ? "LEFT/RIGHT: onglet  DOWN: entrer  BACK: quitter" : hint);
        switch (tab)
        {
        case TAB_DATA:
            draw_data(row, col, first, !tab_focus);
            break;
        case TAB_STATS:
        case TAB_REG:
            draw_lines(items, n_items, sel_line[tab], first_line[tab], !tab_focus);
            break;
        case TAB_GRAPH:
        {
            static const char *modes[3] = {"Nuage de points (X,Y)", "Histogramme de X", "Boite a moustaches de X"};
            uk_text_center(0, UK_HEADER_H + UK_TABS_H + 4, SCREEN_WIDTH, modes[s_graph_mode], COLOR_BLACK, 1);
            if (s_graph_mode == 0)
                draw_scatter();
            else if (s_graph_mode == 1)
                draw_histogram();
            else
                draw_box();
            break;
        }
        }
        flush_display();

        int key = uk_wait_key();

        if (tab_focus)
        {
            if (key == LEFT)
                tab = (tab + TAB_COUNT - 1) % TAB_COUNT;
            else if (key == RIGHT)
                tab = (tab + 1) % TAB_COUNT;
            else if (key == DOWN || key == OK || key == ENTER)
                tab_focus = false;
            else if (key == BACK)
                return 0;
            continue;
        }

        if (tab == TAB_DATA)
        {
            int rows = data_rows();
            if (uk_is_entry_key(key))
            {
                edit_cell(row, col, key);
                if (row < ST_MAX - 1)
                    row++;
            }
            else
                switch (key)
                {
                case UP:
                    if (row == 0)
                        tab_focus = true;
                    else
                        row--;
                    break;
                case DOWN:
                    if (row < ST_MAX - 1 && row < rows)
                        row++;
                    break;
                case LEFT:
                    col = 0;
                    break;
                case RIGHT:
                    col = 1;
                    break;
                case OK:
                case ENTER:
                    edit_cell(row, col, -1);
                    if (row < ST_MAX - 1)
                        row++;
                    break;
                case BACK:
                {
                    double *cell = col == 0 ? &s_x[row] : &s_y[row];
                    if (!isnan(*cell))
                        *cell = NAN;
                    else
                        tab_focus = true;
                    break;
                }
                case TOOLS:
                {
                    static const char *opts[] = {"Effacer la colonne X", "Effacer la colonne Y",
                                                 "Tout effacer", "Trier selon X",
                                                 "Exemple : notes / effectifs"};
                    int c = uk_menu("Options des donnees", opts, 5, 0);
                    for (int i = 0; i < ST_MAX; i++)
                    {
                        if (c == 0 || c == 2)
                            s_x[i] = NAN;
                        if (c == 1 || c == 2)
                            s_y[i] = NAN;
                    }
                    if (c == 3)
                        sort_by_x();
                    if (c == 4)
                        load_example();
                    if (c == 2 || c == 4)
                        row = 0;
                    break;
                }
                default:
                    break;
                }
            if (row < first)
                first = row;
            if (row >= first + TBL_VIS)
                first = row - TBL_VIS + 1;
            continue;
        }

        /* Onglets en liste (Stats, Regression) et Graphe. */
        int &sl = sel_line[tab];
        int &fl = first_line[tab];
        switch (key)
        {
        case LEFT:
            tab = (tab + TAB_COUNT - 1) % TAB_COUNT;
            break;
        case RIGHT:
            tab = (tab + 1) % TAB_COUNT;
            break;
        case UP:
            if (tab == TAB_GRAPH || sl == 0)
                tab_focus = true;
            else
                sl--;
            break;
        case DOWN:
            if (tab != TAB_GRAPH && sl < n_items - 1)
                sl++;
            break;
        case BACK:
            tab_focus = true;
            break;
        case OK:
        case ENTER:
            if (tab == TAB_GRAPH)
                s_graph_mode = (s_graph_mode + 1) % 3;
            else if (tab == TAB_STATS && sl == 0)
                s_use_freq = !s_use_freq;
            else if (tab == TAB_REG && sl <= 1)
            {
                const char *names[REG_COUNT];
                for (int i = 0; i < REG_COUNT; i++)
                    names[i] = reg_model_name(i);
                int c = uk_menu("Modele de regression", names, REG_COUNT, s_model);
                if (c >= 0)
                    s_model = c;
            }
            else if (tab == TAB_REG && sl == n_items - 1 && ok)
            {
                double v;
                if (uk_input("Prevision", "x =", &v, -1))
                    s_pred_x = v;
            }
            break;
        default:
            break;
        }
        if (tab == TAB_STATS || tab == TAB_REG)
        {
            int vis = lines_visible();
            if (sl < fl)
                fl = sl;
            if (sl >= fl + vis)
                fl = sl - vis + 1;
        }
    }
}
