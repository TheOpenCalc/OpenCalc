/* App Inference : tests statistiques (Z, Student, Welch) et intervalles de
 * confiance pour une ou deux proportions, une ou deux moyennes. Les calculs
 * sont dans Core/Stats.cpp (inf_test, inf_interval). */
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
    MODE_TEST = 0,
    MODE_CI,
};

static int s_mode = MODE_TEST;
static int s_method = INF_PROP1;
static int s_alt = ALT_NE;
static double s_alpha = 0.05; /* seuil du test */
static double s_level = 0.95; /* niveau de confiance */
static double s_p[INF_COUNT][INF_MAX_PARAMS];
static bool s_init = false;

#define LH 18
#define TOP (UK_HEADER_H + 2)
#define RESULT_H 46 /* trois lignes de resultat */
#define GX0 8
#define GW (SCREEN_WIDTH - 16)

enum
{
    K_MODE = 0,
    K_METHOD,
    K_PARAM, /* + indice du parametre */
    K_ALT = 20,
    K_RISK,
};

typedef struct
{
    const char *label;
    char value[40];
    int kind;
} row_t;

/* Texte de H1 pour la methode courante. */
static void alt_label(int alt, char *out, int cap)
{
    static const char *const ops[ALT_COUNT] = {"!=", "<", ">"};
    const char *l = "p", *r = "p0";
    switch (s_method)
    {
    case INF_PROP2:
        l = "p1", r = "p2";
        break;
    case INF_MEAN_Z:
    case INF_MEAN_T:
        l = "mu", r = "mu0";
        break;
    case INF_MEAN2_T:
        l = "mu1", r = "mu2";
        break;
    }
    snprintf(out, cap, "%s %s %s", l, ops[alt], r);
}

static void fmt_percent(double v, char *out, int cap)
{
    char b[16];
    uk_fmt(v * 100, b, 8);
    snprintf(out, cap, "%s %%", b);
}

static int build_rows(row_t *r)
{
    const inf_info *I = inf_get(s_method);
    int n = 0;
    r[n].label = "Calcul";
    snprintf(r[n].value, sizeof(r[n].value), "%s", s_mode == MODE_TEST ? "Test" : "Intervalle de confiance");
    r[n++].kind = K_MODE;
    r[n].label = "Methode";
    snprintf(r[n].value, sizeof(r[n].value), "%s", I->name);
    r[n++].kind = K_METHOD;
    for (int i = 0; i < I->nparams; i++)
    {
        if (i == 0 && I->first_is_h0 && s_mode == MODE_CI)
            continue;
        r[n].label = I->pname[i];
        uk_fmt(s_p[s_method][i], r[n].value, 16);
        r[n++].kind = K_PARAM + i;
    }
    if (s_mode == MODE_TEST)
    {
        r[n].label = "Hypothese H1";
        alt_label(s_alt, r[n].value, sizeof(r[n].value));
        r[n++].kind = K_ALT;
        r[n].label = "Seuil alpha";
        fmt_percent(s_alpha, r[n].value, sizeof(r[n].value));
        r[n++].kind = K_RISK;
    }
    else
    {
        r[n].label = "Niveau de confiance";
        fmt_percent(s_level, r[n].value, sizeof(r[n].value));
        r[n++].kind = K_RISK;
    }
    return n;
}

/* Densite de la statistique (N(0,1) ou Student) avec la zone de la
 * p-valeur coloree. */
static void draw_test_graph(int gy0, int gh, const inf_test_result *t)
{
    bool student = !isnan(t->df);
    double lim = fmax(4.0, fmin(fabs(t->stat) + 1, 12.0));
    uk_rect(GX0, gy0, GW, gh, COLOR_WHITE);
    uk_frame(GX0, gy0, GW, gh, COLOR_GRAY);
    bool labels = gh >= 44; /* petit graphique : pas de graduations */
    int base = gy0 + gh - (labels ? 12 : 3), top = gy0 + 3;
    uk_rect(GX0 + 1, base, GW - 2, 1, COLOR_BLACK);
    double peak = student ? student_pdf(0, t->df) : 1 / sqrt(2 * M_PI);
    double s = fabs(t->stat);
    int prev = -1;
    for (int i = 0; i <= GW - 4; i++)
    {
        double x = -lim + 2 * lim * i / (GW - 4);
        double d = student ? student_pdf(x, t->df) : exp(-x * x / 2) / sqrt(2 * M_PI);
        int h = (int)lround(d / peak * (base - top));
        int px = GX0 + 2 + i;
        bool in = (s_alt == ALT_NE && fabs(x) >= s) || (s_alt == ALT_LT && x <= t->stat) ||
                  (s_alt == ALT_GT && x >= t->stat);
        if (in && h > 0)
            uk_rect(px, base - h, 1, h, 0xfd20);
        if (prev >= 0)
            uk_line(px - 1, prev, px, base - h, COLOR_NAVY);
        prev = base - h;
    }
    /* Trait vertical a la valeur de la statistique. */
    if (fabs(t->stat) <= lim)
    {
        int px = GX0 + 2 + (int)lround((t->stat + lim) / (2 * lim) * (GW - 4));
        uk_rect(px, top, 1, base - top, COLOR_RED);
    }
    if (!labels)
        return;
    char b[16];
    uk_fmt(-lim, b, 6);
    uk_text(GX0 + 3, base + 3, b, COLOR_BLACK, 1);
    uk_fmt(lim, b, 6);
    uk_text_right(GX0 + GW - 3, base + 3, b, COLOR_BLACK, 1);
}

/* Intervalle dessine sur un axe, avec l'estimation au milieu. */
static void draw_ci_graph(int gy0, int gh, const inf_ci_result *c)
{
    uk_rect(GX0, gy0, GW, gh, COLOR_WHITE);
    uk_frame(GX0, gy0, GW, gh, COLOR_GRAY);
    int y = gy0 + gh / 2;
    uk_rect(GX0 + 4, y, GW - 8, 1, COLOR_GRAY);
    int x0 = GX0 + 30, x1 = GX0 + GW - 30;
    uk_rect(x0, y - 2, x1 - x0, 5, 0x3d7f);
    uk_rect(x0, y - 8, 2, 17, COLOR_NAVY);
    uk_rect(x1 - 1, y - 8, 2, 17, COLOR_NAVY);
    int xm = (x0 + x1) / 2;
    uk_rect(xm - 2, y - 5, 5, 11, 0xfd20);
    char b[16];
    uk_fmt(c->lo, b, 9);
    uk_text_center(x0 - 30, y + 12, 60, b, COLOR_BLACK, 1);
    uk_fmt(c->hi, b, 9);
    uk_text_center(x1 - 30, y + 12, 60, b, COLOR_BLACK, 1);
    uk_fmt(c->estimate, b, 9);
    uk_text_center(xm - 30, y - 20, 60, b, COLOR_BLACK, 1);
}

/* Ecrit les trois lignes de resultat et dessine le graphique s'il reste de la
 * place sous ry. */
static void draw_results(int ry)
{
    char l1[64], l2[64], l3[64], a[20], b[20], c[20];
    uint16_t col3 = 0x4208, col1 = COLOR_BLACK;
    double *p = s_p[s_method];
    bool graph = false;
    inf_test_result t;
    inf_ci_result ci;
    l2[0] = l3[0] = '\0';
    if (s_mode == MODE_TEST)
    {
        if (!(s_alpha > 0 && s_alpha < 1))
            snprintf(l1, sizeof l1, "alpha doit etre dans ]0;1[");
        else if (!inf_test(s_method, p, s_alt, &t))
            snprintf(l1, sizeof l1, "Parametres invalides");
        else
        {
            uk_fmt(t.stat, a, 9);
            uk_fmt(t.pvalue, b, 9);
            snprintf(l1, sizeof l1, "%s = %s   p = %s", isnan(t.df) ? "z" : "t", a, b);
            uk_fmt(t.estimate, a, 9);
            if (isnan(t.df))
                snprintf(l2, sizeof l2, "Estimation : %s", a);
            else
            {
                uk_fmt(t.df, c, 8);
                snprintf(l2, sizeof l2, "Estimation : %s    ddl = %s", a, c);
            }
            fmt_percent(s_alpha, c, sizeof c);
            bool reject = t.pvalue < s_alpha;
            snprintf(l3, sizeof l3, reject ? "p < alpha : H0 rejetee au seuil %s" : "p >= alpha : H0 non rejetee (%s)",
                     c);
            col3 = reject ? 0xc000 : 0x0420;
            graph = true;
        }
    }
    else
    {
        if (!(s_level > 0 && s_level < 1))
            snprintf(l1, sizeof l1, "Le niveau doit etre dans ]0;1[");
        else if (!inf_interval(s_method, p, s_level, &ci))
            snprintf(l1, sizeof l1, "Parametres invalides");
        else
        {
            uk_fmt(ci.lo, a, 10);
            uk_fmt(ci.hi, b, 10);
            snprintf(l1, sizeof l1, "[%s ; %s]", a, b);
            uk_fmt(ci.estimate, a, 9);
            uk_fmt(ci.margin, b, 9);
            snprintf(l2, sizeof l2, "Estimation %s   marge %s", a, b);
            uk_fmt(ci.crit, a, 9);
            if (isnan(ci.df))
                snprintf(l3, sizeof l3, "z* = %s", a);
            else
            {
                uk_fmt(ci.df, c, 8);
                snprintf(l3, sizeof l3, "t* = %s    ddl = %s", a, c);
            }
            graph = true;
        }
    }
    if (!graph)
        col1 = COLOR_RED;
    int size = uk_text_w(l1, 2) <= SCREEN_WIDTH - 16 ? 2 : 1;
    uk_text_center(0, ry, SCREEN_WIDTH, l1, col1, size);
    ry += size * 8 + 4;
    uk_text_center(0, ry, SCREEN_WIDTH, l2, 0x4208, 1);
    ry += 11;
    uk_text_center(0, ry, SCREEN_WIDTH, l3, col3, 1);
    ry += 12;
    int gh = SCREEN_HEIGHT - UK_FOOTER_H - ry - 2;
    if (graph && gh >= 26)
    {
        if (s_mode == MODE_TEST)
            draw_test_graph(ry, gh, &t);
        else
            draw_ci_graph(ry, gh, &ci);
    }
}

/* Graphique plein ecran (touche RIGHT). */
static void graph_screen(void)
{
    inf_test_result t;
    inf_ci_result ci;
    bool ok = s_mode == MODE_TEST ? inf_test(s_method, s_p[s_method], s_alt, &t)
                                  : inf_interval(s_method, s_p[s_method], s_level, &ci);
    if (!ok)
        return;
    fill_screen(BACKGROUND_COLOR);
    uk_header(inf_get(s_method)->name);
    char line[64], a[20], b[20];
    if (s_mode == MODE_TEST)
    {
        uk_fmt(t.stat, a, 9);
        uk_fmt(t.pvalue, b, 9);
        snprintf(line, sizeof line, "%s = %s    p-valeur = %s (orange)", isnan(t.df) ? "z" : "t", a, b);
    }
    else
    {
        fmt_percent(s_level, a, sizeof a);
        snprintf(line, sizeof line, "Intervalle de confiance a %s", a);
    }
    uk_text_center(0, TOP + 4, SCREEN_WIDTH, line, COLOR_BLACK, 1);
    int gy = TOP + 18, gh = SCREEN_HEIGHT - UK_FOOTER_H - gy - 4;
    if (s_mode == MODE_TEST)
        draw_test_graph(gy, gh, &t);
    else
        draw_ci_graph(gy, gh, &ci);
    uk_footer("Une touche pour revenir");
    flush_display();
    uk_wait_key();
}

int Inference(void)
{
    if (!s_init)
    {
        for (int m = 0; m < INF_COUNT; m++)
            for (int i = 0; i < INF_MAX_PARAMS; i++)
                s_p[m][i] = inf_get(m)->pdef[i];
        s_init = true;
    }
    int sel = 0, first_row = 0;
    static row_t rows[12];
    while (true)
    {
        int n = build_rows(rows);
        if (sel >= n)
            sel = n - 1;
        /* Lignes visibles : on garde la place des resultats en bas. */
        int max_rows = (SCREEN_HEIGHT - TOP - UK_FOOTER_H - RESULT_H - 2) / LH;
        int vis = n < max_rows ? n : max_rows;
        if (sel < first_row)
            first_row = sel;
        if (sel >= first_row + vis)
            first_row = sel - vis + 1;
        if (first_row > n - vis)
            first_row = n - vis;

        fill_screen(BACKGROUND_COLOR);
        uk_header(s_mode == MODE_TEST ? "Inference : test" : "Inference : intervalle");
        for (int k = 0; k < vis; k++)
        {
            int i = first_row + k;
            int y = TOP + k * LH;
            uint16_t bg = i == sel ? UK_SEL_BG : (i % 2 ? FRONTGROUND_COLOR_BIS : FRONTGROUND_COLOR);
            uk_rect(0, y, SCREEN_WIDTH, LH - 1, bg);
            uk_text(8, y + 5, rows[i].label, COLOR_BLACK, 1);
            uk_text_right(SCREEN_WIDTH - 8, y + 5, rows[i].value, COLOR_BLACK, 1);
        }
        /* Fleches quand la liste depasse. */
        if (first_row > 0)
            uk_text_right(SCREEN_WIDTH - 2, TOP + 1, "^", COLOR_GRAY, 1);
        if (first_row + vis < n)
            uk_text_right(SCREEN_WIDTH - 2, TOP + vis * LH - 9, "v", COLOR_GRAY, 1);

        draw_results(TOP + vis * LH + 4);
        uk_footer("OK: modifier  UP/DOWN: choisir  RIGHT: graphe");
        flush_display();

        int key = uk_wait_key();
        int kind = rows[sel].kind;
        bool is_value = kind >= K_PARAM && kind != K_ALT;
        if (key == UP)
            sel = (sel - 1 + n) % n;
        else if (key == DOWN)
            sel = (sel + 1) % n;
        else if (key == BACK)
            return 0;
        else if (key == RIGHT)
            graph_screen();
        else if (key == OK || key == ENTER || (uk_is_entry_key(key) && is_value))
        {
            int first = (key == OK || key == ENTER) ? -1 : key;
            if (kind == K_MODE)
            {
                const char *names[2] = {"Test", "Intervalle de confiance"};
                int c = uk_menu("Calcul", names, 2, s_mode);
                if (c >= 0)
                    s_mode = c;
            }
            else if (kind == K_METHOD)
            {
                const char *names[INF_COUNT];
                for (int i = 0; i < INF_COUNT; i++)
                    names[i] = inf_get(i)->name;
                int c = uk_menu("Methode", names, INF_COUNT, s_method);
                if (c >= 0)
                    s_method = c;
            }
            else if (kind == K_ALT)
            {
                char lbl[ALT_COUNT][24];
                const char *names[ALT_COUNT];
                for (int i = 0; i < ALT_COUNT; i++)
                {
                    alt_label(i, lbl[i], sizeof lbl[i]);
                    names[i] = lbl[i];
                }
                int c = uk_menu("Hypothese alternative H1", names, ALT_COUNT, s_alt);
                if (c >= 0)
                    s_alt = c;
            }
            else if (kind == K_RISK)
            {
                /* Saisie en pourcentage (5 pour 5 %) ou en proportion (0.05). */
                double v;
                const char *prompt = s_mode == MODE_TEST ? "alpha (%) =" : "niveau (%) =";
                if (uk_input(inf_get(s_method)->name, prompt, &v, first))
                {
                    if (v >= 1)
                        v /= 100;
                    if (s_mode == MODE_TEST)
                        s_alpha = v;
                    else
                        s_level = v;
                }
            }
            else
            {
                int i = kind - K_PARAM;
                char prompt[24];
                double v;
                snprintf(prompt, sizeof(prompt), "%s =", inf_get(s_method)->pname[i]);
                if (uk_input(inf_get(s_method)->name, prompt, &v, first))
                    s_p[s_method][i] = v;
            }
        }
    }
}
