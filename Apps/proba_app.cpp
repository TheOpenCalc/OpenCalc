/* App Probabilites : lois usuelles (binomiale, normale, geometrique, Poisson,
 * exponentielle, uniforme), calculs P(X=x), P(X<=x), P(X>=x), P(a<=X<=b) et
 * calcul inverse, avec le diagramme / la densite et la zone concernee. */
#include "stats_app.h"
#include "Stats.h"
#include "ui_kit.h"
#include "ui.h"
#include "colors.h"
#include "Tools.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum
{
    CT_EQ = 0, /* P(X = x)       (lois discretes) */
    CT_LE,     /* P(X <= x) */
    CT_GE,     /* P(X >= x) */
    CT_BETWEEN,/* P(a <= X <= b) */
    CT_INV,    /* x tel que P(X <= x) = q */
    CT_COUNT
};

static const char *k_ct_names[CT_COUNT] = {"P(X = x)", "P(X <= x)", "P(X >= x)",
                                           "P(a <= X <= b)", "Inverse : P(X <= x) = q"};

static int s_law = LAW_BINOMIAL;
static double s_p[2] = {10, 0.5};
static int s_ct = CT_LE;
static double s_a = 3, s_b = 6, s_q = 0.95;

#define LH 19
#define TOP (UK_HEADER_H + 2)
#define GX0 8
#define GW (SCREEN_WIDTH - 16)

typedef struct
{
    const char *label;
    char value[40];
    int kind; /* 0 loi, 1 param0, 2 param1, 3 calcul, 4 a/x, 5 b, 6 q */
} row_t;

static bool ct_allowed(int ct)
{
    return ct != CT_EQ || law_get(s_law)->discrete;
}

static int build_rows(row_t *r)
{
    const law_info *L = law_get(s_law);
    int n = 0;
    r[n].label = "Loi";
    snprintf(r[n].value, sizeof(r[n].value), "%s  %s", L->name, L->short_name);
    r[n++].kind = 0;
    for (int i = 0; i < L->nparams; i++)
    {
        r[n].label = L->pname[i];
        uk_fmt(s_p[i], r[n].value, 16);
        r[n++].kind = 1 + i;
    }
    r[n].label = "Calcul";
    snprintf(r[n].value, sizeof(r[n].value), "%s", k_ct_names[s_ct]);
    r[n++].kind = 3;
    if (s_ct == CT_INV)
    {
        r[n].label = "q";
        uk_fmt(s_q, r[n].value, 16);
        r[n++].kind = 6;
    }
    else if (s_ct == CT_BETWEEN)
    {
        r[n].label = "a";
        uk_fmt(s_a, r[n].value, 16);
        r[n++].kind = 4;
        r[n].label = "b";
        uk_fmt(s_b, r[n].value, 16);
        r[n++].kind = 5;
    }
    else
    {
        r[n].label = "x";
        uk_fmt(s_a, r[n].value, 16);
        r[n++].kind = 4;
    }
    return n;
}

/* Resultat du calcul courant ; *lo/*hi recoivent la zone a colorer. */
static double compute(double *lo, double *hi)
{
    const double inf = INFINITY;
    switch (s_ct)
    {
    case CT_EQ:
        *lo = *hi = s_a;
        return law_pdf(s_law, s_p, s_a);
    case CT_LE:
        *lo = -inf;
        *hi = s_a;
        return law_cdf(s_law, s_p, s_a);
    case CT_GE:
        *lo = s_a;
        *hi = inf;
        return law_interval(s_law, s_p, s_a, inf);
    case CT_BETWEEN:
        *lo = s_a;
        *hi = s_b;
        return law_interval(s_law, s_p, s_a, s_b);
    case CT_INV:
    {
        double x = law_inverse(s_law, s_p, s_q);
        *lo = -inf;
        *hi = x;
        return x;
    }
    }
    return NAN;
}

/* Fenetre en x du graphique. */
static void x_range(double *xmin, double *xmax)
{
    double m = law_mean(s_law, s_p), sd = law_sd(s_law, s_p);
    switch (s_law)
    {
    case LAW_BINOMIAL:
        *xmin = fmax(0, floor(m - 4 * sd));
        *xmax = fmin(round(s_p[0]), ceil(m + 4 * sd));
        if (*xmax - *xmin < 4)
        {
            *xmin = fmax(0, *xmin - 2);
            *xmax = fmin(round(s_p[0]), *xmax + 2);
        }
        break;
    case LAW_GEOMETRIC:
        *xmin = 1;
        *xmax = fmax(6, ceil(m + 4 * sd));
        break;
    case LAW_POISSON:
        *xmin = fmax(0, floor(m - 4 * sd));
        *xmax = fmax(6, ceil(m + 4 * sd));
        break;
    case LAW_EXPONENTIAL:
        *xmin = 0;
        *xmax = 5 * m;
        break;
    case LAW_UNIFORM:
    {
        double w = s_p[1] - s_p[0];
        *xmin = s_p[0] - 0.25 * w;
        *xmax = s_p[1] + 0.25 * w;
        break;
    }
    default:
        *xmin = m - 4 * sd;
        *xmax = m + 4 * sd;
        break;
    }
    if (!(*xmax > *xmin))
    {
        *xmin -= 1;
        *xmax += 1;
    }
}

static void draw_graph(int gy0, int gh, double lo, double hi)
{
    double xmin, xmax;
    x_range(&xmin, &xmax);
    const law_info *L = law_get(s_law);
    uk_rect(GX0, gy0, GW, gh, COLOR_WHITE);
    uk_frame(GX0, gy0, GW, gh, COLOR_GRAY);
    int base = gy0 + gh - 12;
    int top = gy0 + 4;
    uk_rect(GX0 + 1, base, GW - 2, 1, COLOR_BLACK);
    char buf[16];
    uk_fmt(xmin, buf, 8);
    uk_text(GX0 + 3, base + 3, buf, COLOR_BLACK, 1);
    uk_fmt(xmax, buf, 8);
    uk_text_right(GX0 + GW - 3, base + 3, buf, COLOR_BLACK, 1);

    const uint16_t c_in = 0xfd20, c_out = 0x3d7f;
    if (L->discrete)
    {
        int k0 = (int)xmin, k1 = (int)xmax;
        int nb = k1 - k0 + 1;
        if (nb > GW - 4)
        {
            k1 = k0 + GW - 5;
            nb = k1 - k0 + 1;
        }
        double pmax = 0;
        for (int k = k0; k <= k1; k++)
            pmax = fmax(pmax, law_pdf(s_law, s_p, k));
        if (pmax <= 0)
            return;
        double slot = (double)(GW - 4) / nb;
        for (int k = k0; k <= k1; k++)
        {
            double pk = law_pdf(s_law, s_p, k);
            int h = (int)lround(pk / pmax * (base - top));
            int x = GX0 + 2 + (int)((k - k0) * slot);
            int w = (int)fmax(1, slot * 0.7);
            bool in = k >= lo - 1e-9 && k <= hi + 1e-9;
            uk_rect(x, base - h, w, h, in ? c_in : c_out);
        }
    }
    else
    {
        double ymax = 0;
        for (int i = 0; i <= GW - 4; i++)
        {
            double x = xmin + (xmax - xmin) * i / (GW - 4);
            double d = law_pdf(s_law, s_p, x);
            if (isfinite(d))
                ymax = fmax(ymax, d);
        }
        if (ymax <= 0)
            return;
        int prev = -1;
        for (int i = 0; i <= GW - 4; i++)
        {
            double x = xmin + (xmax - xmin) * i / (GW - 4);
            double d = law_pdf(s_law, s_p, x);
            int h = isfinite(d) ? (int)lround(d / ymax * (base - top)) : 0;
            int px = GX0 + 2 + i;
            if (x >= lo && x <= hi && h > 0)
                uk_rect(px, base - h, 1, h, c_in);
            int py = base - h;
            if (prev >= 0)
                uk_line(px - 1, prev, px, py, COLOR_NAVY);
            prev = py;
        }
    }
}

int Probabilities(void)
{
    int sel = 0;
    static row_t rows[8];
    while (true)
    {
        int n = build_rows(rows);
        if (sel >= n)
            sel = n - 1;

        fill_screen(BACKGROUND_COLOR);
        uk_header("Probabilites");
        for (int i = 0; i < n; i++)
        {
            int y = TOP + i * LH;
            uint16_t bg = i == sel ? UK_SEL_BG : (i % 2 ? FRONTGROUND_COLOR_BIS : FRONTGROUND_COLOR);
            uk_rect(0, y, SCREEN_WIDTH, LH - 1, bg);
            uk_text(8, y + 5, rows[i].label, COLOR_BLACK, 1);
            uk_text_right(SCREEN_WIDTH - 8, y + 5, rows[i].value, COLOR_BLACK, 1);
        }

        double lo = NAN, hi = NAN;
        double res = law_params_ok(s_law, s_p) ? compute(&lo, &hi) : NAN;
        int ry = TOP + n * LH + 3;
        char line[64], v[24];
        if (!law_params_ok(s_law, s_p))
            snprintf(line, sizeof(line), "Parametres invalides");
        else if (isnan(res))
            snprintf(line, sizeof(line), s_ct == CT_INV ? "q doit etre dans ]0;1[" : "Indefini");
        else
        {
            uk_fmt(res, v, 14);
            char a[14], b[14];
            uk_fmt(s_a, a, 8);
            uk_fmt(s_b, b, 8);
            switch (s_ct)
            {
            case CT_EQ:
                snprintf(line, sizeof(line), "P(X=%s) = %s", a, v);
                break;
            case CT_LE:
                snprintf(line, sizeof(line), "P(X<=%s) = %s", a, v);
                break;
            case CT_GE:
                snprintf(line, sizeof(line), "P(X>=%s) = %s", a, v);
                break;
            case CT_BETWEEN:
                snprintf(line, sizeof(line), "P(%s<=X<=%s) = %s", a, b, v);
                break;
            default:
                snprintf(line, sizeof(line), "x = %s", v);
                break;
            }
        }
        int size = uk_text_w(line, 2) <= SCREEN_WIDTH - 16 ? 2 : 1;
        uk_text_center(0, ry, SCREEN_WIDTH, line, isnan(res) ? COLOR_RED : COLOR_BLACK, size);
        ry += size * 8 + 4;
        if (law_params_ok(s_law, s_p))
        {
            char m[16], sd[16];
            uk_fmt(law_mean(s_law, s_p), m, 10);
            uk_fmt(law_sd(s_law, s_p), sd, 10);
            snprintf(line, sizeof(line), "E(X) = %s    sigma(X) = %s", m, sd);
            uk_text_center(0, ry, SCREEN_WIDTH, line, 0x4208, 1);
            ry += 12;
            int gh = SCREEN_HEIGHT - UK_FOOTER_H - ry - 2;
            if (gh > 30)
                draw_graph(ry, gh, lo, hi);
        }
        uk_footer("OK: modifier   UP/DOWN: choisir   BACK: quitter");
        flush_display();

        int key = uk_wait_key();
        int kind = rows[sel].kind;
        if (key == UP)
            sel = (sel - 1 + n) % n;
        else if (key == DOWN)
            sel = (sel + 1) % n;
        else if (key == BACK)
            return 0;
        else if (key == OK || key == ENTER || (uk_is_entry_key(key) && kind != 0 && kind != 3))
        {
            int first = (key == OK || key == ENTER) ? -1 : key;
            if (kind == 0)
            {
                const char *names[LAW_COUNT];
                for (int i = 0; i < LAW_COUNT; i++)
                    names[i] = law_get(i)->name;
                int c = uk_menu("Loi de probabilite", names, LAW_COUNT, s_law);
                if (c >= 0 && c != s_law)
                {
                    s_law = c;
                    s_p[0] = law_get(c)->pdef[0];
                    s_p[1] = law_get(c)->pdef[1];
                    if (!ct_allowed(s_ct))
                        s_ct = CT_LE;
                    /* Valeurs de x raisonnables pour la nouvelle loi. */
                    double m = law_mean(c, s_p), sd = law_sd(c, s_p);
                    s_a = law_get(c)->discrete ? round(m) : round((m + sd) * 100) / 100;
                    s_b = law_get(c)->discrete ? round(m + sd) : round((m + 2 * sd) * 100) / 100;
                    if (s_law == LAW_NORMAL)
                    {
                        s_a = -1.96;
                        s_b = 1.96;
                    }
                }
            }
            else if (kind == 3)
            {
                const char *names[CT_COUNT];
                int map[CT_COUNT], m = 0, cur = 0;
                for (int i = 0; i < CT_COUNT; i++)
                    if (ct_allowed(i))
                    {
                        if (i == s_ct)
                            cur = m;
                        map[m] = i;
                        names[m++] = k_ct_names[i];
                    }
                int c = uk_menu("Type de calcul", names, m, cur);
                if (c >= 0)
                    s_ct = map[c];
            }
            else
            {
                const law_info *L = law_get(s_law);
                double v;
                char prompt[24];
                const char *nm = kind == 1 ? L->pname[0] : kind == 2 ? L->pname[1]
                                                     : kind == 4 ? (s_ct == CT_BETWEEN ? "a" : "x")
                                                     : kind == 5 ? "b"
                                                                 : "q";
                snprintf(prompt, sizeof(prompt), "%s =", nm);
                if (uk_input(L->name, prompt, &v, first))
                {
                    if (kind == 1)
                        s_p[0] = v;
                    else if (kind == 2)
                        s_p[1] = v;
                    else if (kind == 4)
                        s_a = v;
                    else if (kind == 5)
                        s_b = v;
                    else
                        s_q = v;
                }
            }
        }
    }
}
