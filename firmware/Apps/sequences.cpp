#include "headers/ui.h"
#include "headers/Evaluator.h"
#include "headers/sequences.h"
#include <math.h>
#include "headers/menu.h"
#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <cstdio>
#include "headers/colors.h"
#include "headers/widgets.h"
#include "headers/ui_kit.h"

#define TBL_TOP 22
#define TBL_LABEL_W 38
#define TBL_COL_W 40
#define TBL_COLS 7
#define TBL_ROW_N_H 24
#define TBL_ROW_VAL_H 30
#define TBL_MAX_SUITES 4

/* Suites : chaque suite occupe TROIS lignes de l'onglet Formule.
 *   ligne 3k   : formule
 *                - sans U ni P : suite explicite        u(n)   = f(N)
 *                - avec U      : recurrente d'ordre 1   u(n+1) = f(N, U)
 *                                U = u(n)
 *                - avec P      : recurrente d'ordre 2   u(n+2) = f(N, U, P)
 *                                U = u(n+1), P = u(n)
 *                  U est toujours le terme precedent, P celui d'avant ;
 *                  N = n (indice courant).
 *   ligne 3k+1 : premier terme u(n0) (suites recurrentes)
 *   ligne 3k+2 : deuxieme terme u(n0+1), affichee seulement a l'ordre 2
 * n0 (premier rang, 0 par defaut) se regle dans Outils sur la ligne de la
 * formule ; une suite explicite n'est definie qu'a partir de n0.
 * N s'obtient avec la touche X, U avec ALPHA (ou 2nd puis 0), P avec 2nd
 * puis 1, ou depuis le menu Outils.
 * Outils : blocs prets a l'emploi (arith, geom, arigeo, arithn) inseres d'un
 * seul bloc ; leur nom et leurs parentheses ne peuvent pas etre effaces un a un.
 *
 * Les formules et les premiers rangs survivent a la sortie de l'appli. */
#define SEQ_LINES 3
#define SEQ_BOXES 99
#define SEQ_COUNT (SEQ_BOXES / SEQ_LINES)
#define SEQ_MAX_SEQ TBL_MAX_SUITES
#define SEQ_MAX_N 5000 /* nombre max de pas calcules pour une suite recurrente */
#define SEQ_MAX_U 16
#define SEQ_FIT_N 20
#define SEQ_N0_MAX 99

typedef struct
{
    int order; /* 0 explicite, 1 : u(n+1) = f(n, U), 2 : u(n+2) = f(n, U, P) */
    token *tok; /* formule en notation postfixee */
    int ntok;
    int uidx[SEQ_MAX_U]; /* positions des U dans tok */
    int nu;
    int pidx[SEQ_MAX_U]; /* positions des P dans tok */
    int np;
    int n0;    /* premier rang */
    double u0; /* u(n0) */
    double u1; /* u(n0+1), ordre 2 */
} seq_def;

typedef struct
{
    int n;
    double u;    /* u(n) */
    double prev; /* u(n-1) */
} seq_iter;

static fill_box **s_arr = nullptr;
static int s_n0[SEQ_COUNT];
static text_box *s_tab_formule = nullptr, *s_tab_graphe = nullptr, *s_tab_valeur = nullptr;

static bool box_has_text(fill_box *b)
{
    return b != nullptr && b->t_size > 0;
}

static bool seq_active(fill_box **arr, int k)
{
    return k >= 0 && k < SEQ_COUNT && box_has_text(arr[SEQ_LINES * k]);
}

/* Ordre d'une formule d'apres son texte : 2 avec P, 1 avec U ou un bloc
 * (arith, geom...), 0 sinon. Les noms de fonctions sont en minuscules : une
 * majuscule est toujours une lettre tapee. */
static int text_order(const fill_box *f)
{
    if (f == nullptr)
        return 0;
    bool u = false;
    for (int i = 0; i < f->t_size; i++)
    {
        if (f->text[i] == 'P')
            return 2;
        if (f->text[i] == 'U')
            u = true;
    }
    if (!u && f->t_size > 0 &&
        (strstr(f->text, "arith") || strstr(f->text, "geom") || strstr(f->text, "arigeo")))
        u = true;
    return u ? 1 : 0;
}

/* La ligne du deuxieme terme n'apparait que pour une suite d'ordre 2. */
static bool line_visible(fill_box **arr, int i)
{
    if (i < 0 || i >= SEQ_BOXES)
        return false;
    if (i % SEQ_LINES != 2)
        return true;
    return text_order(arr[i - 2]) == 2;
}

/* Valeur d'une ligne de premier terme (0 si vide). */
static double eval_line(fill_box *b)
{
    if (!box_has_text(b))
        return 0;
    int in_n = 0;
    token *ir = parse_string_to_token(b->text, b->t_size, &in_n);
    token *io = shunting_yard(ir, in_n);
    int iy = count_yarded(ir, in_n);
    double v = evaluate_npi(io, iy, 0, 'N');
    token_free_matrices(ir, in_n);
    free(ir);
    free(io);
    return v;
}

/* Construit la definition de la suite k a partir de ses lignes. */
static bool seq_build(fill_box **arr, int k, seq_def *s)
{
    memset(s, 0, sizeof(*s));
    fill_box *f = arr[SEQ_LINES * k];
    if (!box_has_text(f))
        return false;

    int tn = 0;
    token *raw = parse_string_to_token(f->text, f->t_size, &tn);
    s->tok = shunting_yard(raw, tn);
    s->ntok = count_yarded(raw, tn);
    token_free_matrices(raw, tn);
    free(raw);

    bool block = false;
    for (int i = 0; i < s->ntok; i++)
    {
        if (s->tok[i].type == 'X')
        {
            int letter = (int)s->tok[i].value + (int)'a';
            if (letter == 'U' && s->nu < SEQ_MAX_U)
                s->uidx[s->nu++] = i;
            else if (letter == 'P' && s->np < SEQ_MAX_U)
                s->pidx[s->np++] = i;
        }
        /* Les blocs arith/geom/arigeo/arithn contiennent un U implicite. */
        if (is_block_type(s->tok[i].type))
            block = true;
    }
    s->order = s->np > 0 ? 2 : ((s->nu > 0 || block) ? 1 : 0);
    s->n0 = s_n0[k];
    if (s->order >= 1)
        s->u0 = eval_line(arr[SEQ_LINES * k + 1]);
    if (s->order == 2)
        s->u1 = eval_line(arr[SEQ_LINES * k + 2]);
    return true;
}

static void seq_free(seq_def *s)
{
    free(s->tok);
    s->tok = nullptr;
}

/* Suite explicite : u(n) = f(n). */
static double seq_explicit(seq_def *s, double n)
{
    return evaluate_npi(s->tok, s->ntok, n, 'N');
}

/* Suite recurrente : on part de u(n0) (et u(n0+1) a l'ordre 2) et on avance
 * terme par terme. Les U et P du postfixe sont remplaces par les valeurs
 * courantes (jetons 'n' reecrits a chaque pas). */
static void seq_iter_init(seq_def *s, seq_iter *it)
{
    it->n = s->n0;
    it->u = s->u0;
    it->prev = NAN;
}

static void seq_iter_next(seq_def *s, seq_iter *it)
{
    double next;
    if (s->order == 2 && it->n == s->n0)
        next = s->u1; /* deuxieme terme donne */
    else
    {
        /* Ordre 1 : u(n+1) = f(n, u(n)).
         * Ordre 2 : u(n+1) = f(n-1, U = u(n), P = u(n-1)), c'est-a-dire
         * u(m+2) = f(m, u(m+1), u(m)) avec m = n-1. */
        double N = s->order == 2 ? it->n - 1 : it->n;
        for (int k = 0; k < s->nu; k++)
        {
            s->tok[s->uidx[k]].type = 'n';
            s->tok[s->uidx[k]].value = it->u;
        }
        for (int k = 0; k < s->np; k++)
        {
            s->tok[s->pidx[k]].type = 'n';
            s->tok[s->pidx[k]].value = it->prev;
        }
        g_seq_u = it->u; /* lus par les blocs */
        g_seq_n = N;
        next = evaluate_npi(s->tok, s->ntok, N, 'N');
    }
    it->prev = it->u;
    it->u = next;
    it->n++;
}

/* Valeurs de la suite pour n = start .. start+count-1. have[c] = false hors
 * du domaine (avant n0, ou trop loin pour une recurrente). */
static void seq_values(seq_def *s, int start, int count, double *vals, bool *have)
{
    for (int c = 0; c < count; c++)
    {
        vals[c] = NAN;
        have[c] = false;
    }
    int last = start + count - 1;
    if (s->order == 0)
    {
        for (int c = 0; c < count; c++)
            if (start + c >= s->n0)
            {
                vals[c] = seq_explicit(s, start + c);
                have[c] = true;
            }
        return;
    }
    if (last < s->n0 || start > s->n0 + SEQ_MAX_N)
        return;
    int lim = std::min(last, s->n0 + SEQ_MAX_N);
    seq_iter it;
    seq_iter_init(s, &it);
    while (true)
    {
        if (it.n >= start)
        {
            vals[it.n - start] = it.u;
            have[it.n - start] = true;
        }
        if (it.n >= lim)
            break;
        seq_iter_next(s, &it);
    }
}

/* Valeur de la suite en n (ok = false si non definie / trop loin). */
static double seq_value(seq_def *s, int n, bool *ok)
{
    double v;
    seq_values(s, n, 1, &v, ok);
    return *ok ? v : NAN;
}

/* Texte court d'un nombre (au plus maxch caracteres, buf >= 32 octets). */
static int fmt_cell(double v, char *buf, int maxch)
{
    if (isnan(v))
    {
        strcpy(buf, "NaN");
        return 3;
    }
    if (isinf(v))
    {
        strcpy(buf, v > 0 ? "inf" : "-inf");
        return (int)strlen(buf);
    }
    for (int prec = 6; prec >= 1; prec--)
    {
        snprintf(buf, 32, "%.*g", prec, v);
        if ((int)strlen(buf) <= maxch)
            return (int)strlen(buf);
    }
    buf[maxch] = '\0';
    return maxch;
}

/* ----------------------------- Table de valeurs --------------------------- */

static void tbl_cell(int col, int row, int h, int w,
                     char *txt, int txt_len,
                     uint16_t bg, bool selected)
{
    text_box *tb = create_text_box(col, row, h, w, 1, false);
    tb->col = bg;
    tb->text = txt;
    tb->t_size = txt_len;
    tb->display_text_size = 1;
    display_text_box(tb, 0, 0, selected);
    free_text_box(tb);
}

static void display_values_table(seq_def *defs, int *ids, int nd,
                                 int start_n,
                                 int sel_col)
{
    char lbl[4];
    char buf[32];

    lbl[0] = 'n';
    lbl[1] = '\0';
    tbl_cell(0, TBL_TOP,
             TBL_ROW_N_H, TBL_LABEL_W,
             lbl, 1, FRONTGROUND_COLOR, false);

    for (int c = 0; c < TBL_COLS; c++)
    {
        int n_val = start_n + c;
        int len = snprintf(buf, sizeof(buf), "%d", n_val);
        tbl_cell(TBL_LABEL_W + c * TBL_COL_W, TBL_TOP,
                 TBL_ROW_N_H, TBL_COL_W,
                 buf, len, FRONTGROUND_COLOR, c == sel_col);
    }

    for (int s = 0; s < nd; s++)
    {
        int row_y = TBL_TOP + TBL_ROW_N_H + s * TBL_ROW_VAL_H;

        lbl[0] = 'u' + (ids[s] % 6);
        lbl[1] = '\0';
        tbl_cell(0, row_y,
                 TBL_ROW_VAL_H, TBL_LABEL_W,
                 lbl, 1, FRONTGROUND_COLOR, false);

        double vals[TBL_COLS];
        bool have[TBL_COLS];
        seq_values(&defs[s], start_n, TBL_COLS, vals, have);

        for (int c = 0; c < TBL_COLS; c++)
        {
            int vlen;
            if (!have[c])
            {
                strcpy(buf, "--");
                vlen = 2;
            }
            else
                vlen = fmt_cell(vals[c], buf, 7);

            tbl_cell(TBL_LABEL_W + c * TBL_COL_W, row_y,
                     TBL_ROW_VAL_H, TBL_COL_W,
                     buf, vlen, BACKGROUND_COLOR, c == sel_col);
        }
    }
}

/* --------------------------------- Graphe -------------------------------- */

static void seq_point(double n, double v,
                      double x_min, double x_max, double y_min, double y_max,
                      uint16_t color)
{
    if (isnan(v) || isinf(v))
        return;
    double fx = (n - x_min) / (x_max - x_min) * SCREEN_WIDTH;
    double fy = (v - y_min) / (y_max - y_min) * SCREEN_HEIGHT;
    if (fx < 2 || fx > SCREEN_WIDTH - 2 || fy < 2 || fy > SCREEN_HEIGHT - 2)
        return;
    fill_rect((uint16_t)fy - 2, (uint16_t)fx - 2, 3, 3, color);
}

static void seq_plot(seq_def *s,
                     double x_min, double x_max, double y_min, double y_max,
                     uint16_t color)
{
    if (!(x_max > x_min) || !(y_max > y_min))
        return;
    double lo = ceil(x_min), hi = floor(x_max);
    if (lo < -1e6)
        lo = -1e6;
    if (hi > 1e6)
        hi = 1e6;
    int n_lo = std::max((int)lo, s->n0), n_hi = (int)hi;
    if (n_hi < n_lo)
        return;
    int step = (n_hi - n_lo + 1) / SCREEN_WIDTH;
    if (step < 1)
        step = 1;

    if (s->order > 0)
    {
        if (n_hi > s->n0 + SEQ_MAX_N)
            n_hi = s->n0 + SEQ_MAX_N;
        seq_iter it;
        seq_iter_init(s, &it);
        while (true)
        {
            if (it.n >= n_lo && (it.n % step) == 0)
                seq_point(it.n, it.u, x_min, x_max, y_min, y_max, color);
            if (it.n >= n_hi)
                break;
            seq_iter_next(s, &it);
        }
    }
    else
    {
        for (int n = n_lo; n <= n_hi; n += step)
            seq_point(n, seq_explicit(s, n), x_min, x_max, y_min, y_max, color);
    }
}

/* Ajuste la fenetre aux SEQ_FIT_N premiers termes de toutes les suites. */
static void seq_fit(seq_def *defs, int nd,
                    double *x_min, double *x_max, double *y_min, double *y_max)
{
    double lo = INFINITY, hi = -INFINITY;
    int n_first = SEQ_N0_MAX, n_last = 0;
    for (int s = 0; s < nd; s++)
    {
        double vals[SEQ_FIT_N + 1];
        bool have[SEQ_FIT_N + 1];
        seq_values(&defs[s], defs[s].n0, SEQ_FIT_N + 1, vals, have);
        n_first = std::min(n_first, defs[s].n0);
        n_last = std::max(n_last, defs[s].n0 + SEQ_FIT_N);
        for (int c = 0; c <= SEQ_FIT_N; c++)
        {
            double v = vals[c];
            if (!have[c] || isnan(v) || isinf(v))
                continue;
            if (v < lo)
                lo = v;
            if (v > hi)
                hi = v;
        }
    }
    if (lo > hi)
        return;
    if (hi - lo < 1e-9)
    {
        lo -= 1;
        hi += 1;
    }
    double pad = (hi - lo) * 0.15;
    *y_min = lo - pad;
    *y_max = hi + pad;
    *x_min = n_first - 1;
    *x_max = n_last + 1;
}

/* Curseur : croix sur le terme n et bandeau d'information en bas. */
static void seq_cursor(seq_def *s, char letter, int n,
                       double x_min, double x_max, double y_min, double y_max)
{
    bool ok;
    double v = seq_value(s, n, &ok);

    if (ok && !isnan(v) && !isinf(v) && x_max > x_min && y_max > y_min)
    {
        int px = (int)((n - x_min) / (x_max - x_min) * SCREEN_WIDTH);
        int py = (int)((v - y_min) / (y_max - y_min) * SCREEN_HEIGHT);
        if (px >= 6 && px < SCREEN_WIDTH - 6 && py >= 6 && py < SCREEN_HEIGHT - 6)
        {
            fill_rect(py, px - 5, 1, 10, 0x0000);
            fill_rect(py - 5, px, 10, 1, 0x0000);
        }
    }

    char vb[32];
    if (!ok)
        strcpy(vb, "--");
    else
        fmt_cell(v, vb, 14);

    text_box *a = create_text_box(0, 0, 20, 107, 2, false);
    snprintf(a->text, 100, "n: %d", n);
    a->t_size = (int)strlen(a->text);
    display_text_box(a, 0, 0, false);
    free_text_box(a);

    text_box *b = create_text_box(107, 0, 20, 213, 2, false);
    snprintf(b->text, 100, "%c(%d): %s", letter, n, vb);
    b->t_size = (int)strlen(b->text);
    display_text_box(b, 0, 0, false);
    free_text_box(b);
}

/* ------------------------------ Onglet Formule ---------------------------- */

/* Etiquette de la ligne i : "u(n+2)=", "u(1)="... */
static void seq_set_label(fill_box **arr, int i)
{
    int k = i / SEQ_LINES, r = i % SEQ_LINES;
    char letter = 'u' + k % 6;
    if (r == 0)
    {
        static const char *const fmt[3] = {"%c(n)=", "%c(n+1)=", "%c(n+2)="};
        snprintf(g_seq_label, sizeof(g_seq_label), fmt[text_order(arr[i])], letter);
    }
    else
        snprintf(g_seq_label, sizeof(g_seq_label), "%c(%d)=", letter, s_n0[k] + r - 1);
}

/* Nombre de lignes visibles de a (compris) a b (exclu). */
static int visible_rows(fill_box **arr, int a, int b)
{
    int n = 0;
    for (int i = std::max(a, 0); i < b; i++)
        if (line_visible(arr, i))
            n++;
    return n;
}

/* Outils sur la ligne d'une formule : blocs, lettres U et P, premier rang. */
static void seq_tools(fill_box **arr, int line, bool snd)
{
    int k = line / SEQ_LINES;
    char rank[32];
    snprintf(rank, sizeof(rank), "Premier rang n0 = %d...", s_n0[k]);
    const char *labels[8] = {"Arithmetique U+r", "Geometrique q*U", "Arith-geom a*U+b",
                             "U + a*n + b", "U : terme precedent",
                             "P : terme d'avant (ordre 2)", rank, "Autres outils..."};
    static const int blocks[4] = {SEQ_ARITH, SEQ_GEOM, SEQ_ARIGEO, SEQ_ARITHN};
    int c = uk_menu("Suites", labels, 8, 0);
    fill_box *f = arr[line];
    if (c >= 0 && c <= 3)
        update_fill_box(f, blocks[c], snd);
    else if (c == 4)
        update_fill_box(f, ZERO, true); /* U */
    else if (c == 5)
        update_fill_box(f, ONE, true); /* P */
    else if (c == 6)
    {
        double v = s_n0[k];
        if (uk_input("Premier rang", "n0 (entier de 0 a 99) =", &v, -1) &&
            v >= 0 && v <= SEQ_N0_MAX && v == floor(v))
            s_n0[k] = (int)v;
    }
    else if (c == 7)
        update_fill_box(f, menu_tools(), snd);
}

int Sequencer()
{
    bool snd = false;

    if (s_tab_formule == nullptr)
    {
        s_tab_formule = create_text_box(0, 220, 20, 107, 1, false);
        s_tab_graphe = create_text_box(107, 220, 20, 107, 1, false);
        s_tab_valeur = create_text_box(214, 220, 20, 106, 1, false);
        s_tab_formule->text = "Formule";
        s_tab_formule->t_size = 7;
        s_tab_graphe->text = "Graphe";
        s_tab_graphe->t_size = 6;
        s_tab_valeur->text = "Valeur";
        s_tab_valeur->t_size = 6;
    }
    text_box *TabFormule = s_tab_formule;
    text_box *TabGraphe = s_tab_graphe;
    text_box *TabValeur = s_tab_valeur;

    const uint16_t palet[] = {
        COLOR_RED, COLOR_LIME, COLOR_BLUE, COLOR_YELLOW,
        COLOR_CYAN, COLOR_MAGENTA, COLOR_SILVER, COLOR_GRAY,
        COLOR_MAROON, COLOR_OLIVE, COLOR_GREEN, COLOR_PURPLE,
        COLOR_TEAL, COLOR_NAVY};

    /* Les formules sont gardees d'une ouverture a l'autre. */
    if (s_arr == nullptr)
    {
        fill_box **a = (fill_box **)calloc(SEQ_BOXES, sizeof(fill_box *));
        if (a == nullptr)
            return 0;
        for (int i = 0; i < SEQ_LINES; i++)
            a[i] = create_fill_box(0, 20, 40, 320, 3);
        s_arr = a;
    }
    fill_box **arr_fill_box = s_arr;

    int selected_fill_box = 0;
    int first_display = 0;

    int tab = 0;
    int table_start_n = 0;
    int table_sel_col = 0;

    double x_min = -5, x_max = 5;
    double y_min = -5, y_max = 5;
    int cursor_n = 0;
    bool need_fit = false;

    int last_pressed = -1; // l'ecran s'affiche avant d'attendre la premiere touche

    while (1)
    {
        fill_screen(BACKGROUND_COLOR);

        switch (last_pressed)
        {

        case SECOND:
            toggle(&snd);
            break;

        case BACK:
            if (tab == 0 && selected_fill_box >= 0 &&
                arr_fill_box[selected_fill_box]->t_size > 0)
            {
                update_fill_box(arr_fill_box[selected_fill_box], last_pressed, snd);
            }
            else
                return 0; /* formules gardees pour la prochaine ouverture */
            break;

        case UP:
            if (tab == 0)
            {
                int pv = selected_fill_box - 1;
                while (pv >= 0 && !line_visible(arr_fill_box, pv))
                    pv--;
                selected_fill_box = std::max(-1, pv);
                if (selected_fill_box < first_display)
                    first_display = selected_fill_box;
            }
            else if (tab == 1)
            {
                /* En mode graphe on selectionne une suite (pas une ligne). */
                int k = selected_fill_box < 0 ? -1 : selected_fill_box / SEQ_LINES;
                int j = k - 1;
                while (j >= 0 && !seq_active(arr_fill_box, j))
                    j--;
                selected_fill_box = j >= 0 ? SEQ_LINES * j : -1;
                cursor_n = j >= 0 ? s_n0[j] : 0;
            }
            break;

        case DOWN:
            if (tab == 0)
            {
                int nx = selected_fill_box + 1;
                while (nx < SEQ_BOXES && !line_visible(arr_fill_box, nx))
                    nx++;
                if (nx < SEQ_BOXES)
                {
                    selected_fill_box = nx;
                    if (arr_fill_box[selected_fill_box] == nullptr)
                        arr_fill_box[selected_fill_box] = create_fill_box(0, 20, 40, 320, 3);
                }
                while (first_display < selected_fill_box &&
                       visible_rows(arr_fill_box, first_display, selected_fill_box) > 4)
                {
                    first_display++;
                    while (first_display < selected_fill_box && !line_visible(arr_fill_box, first_display))
                        first_display++;
                }
            }
            else if (tab == 1)
            {
                int k = selected_fill_box < 0 ? -1 : selected_fill_box / SEQ_LINES;
                int j = k + 1;
                while (j < SEQ_COUNT && !seq_active(arr_fill_box, j))
                    j++;
                if (j < SEQ_COUNT)
                {
                    selected_fill_box = SEQ_LINES * j;
                    cursor_n = s_n0[j];
                }
            }
            break;

        case RIGHT:
            if (tab == 0)
            {
                if (selected_fill_box >= 0)
                    fill_box_right(arr_fill_box[selected_fill_box]);
                else
                {
                    tab = 1;
                    need_fit = true;
                }
            }
            else if (tab == 1)
            {
                if (selected_fill_box != -1)
                {
                    cursor_n++;
                    if (cursor_n > x_max)
                    {
                        double w = x_max - x_min;
                        x_min += w / 2;
                        x_max += w / 2;
                    }
                }
                else
                {
                    tab = 2;
                }
            }
            else
            {
                if (table_sel_col < TBL_COLS - 1)
                    table_sel_col++;
                else
                {
                    table_start_n++;
                }
            }
            break;

        case LEFT:
            if (tab == 0)
            {
                if (selected_fill_box >= 0)
                    fill_box_left(arr_fill_box[selected_fill_box]);
            }
            else if (tab == 1)
            {
                if (selected_fill_box != -1)
                {
                    if (cursor_n > s_n0[selected_fill_box / SEQ_LINES])
                        cursor_n--;
                    if (cursor_n < x_min)
                    {
                        double w = x_max - x_min;
                        x_min -= w / 2;
                        x_max -= w / 2;
                    }
                }
                else
                {
                    tab = 0;
                    selected_fill_box = -1;
                }
            }
            else if (tab == 2)
            {
                if (table_sel_col > 0)
                    table_sel_col--;
                else if (table_start_n > 0)
                    table_start_n--;
                else
                {
                    tab = 1;
                    selected_fill_box = -1;
                }
            }
            break;

        case OK:
        case ENTER:
            /* Graphe : OK recadre la fenetre sur les premiers termes. */
            if (tab == 1)
                need_fit = true;
            break;

        case X:
            if (tab == 0 && selected_fill_box >= 0)
                update_fill_box(arr_fill_box[selected_fill_box], MINUS, true); /* N */
            break;

        case ALPHA:
            if (tab == 0 && selected_fill_box >= 0)
                update_fill_box(arr_fill_box[selected_fill_box], ZERO, true); /* U */
            break;

        case TOOLS:
            if (tab == 0 && selected_fill_box >= 0)
            {
                /* Ligne de formule : blocs de recurrence, lettres et premier
                 * rang (puis outils classiques). Lignes des premiers termes :
                 * outils seuls. */
                if (selected_fill_box % SEQ_LINES == 0)
                    seq_tools(arr_fill_box, selected_fill_box, snd);
                else
                    update_fill_box(arr_fill_box[selected_fill_box], menu_tools(), snd);
                fill_screen(BACKGROUND_COLOR); /* efface le menu ferme */
            }
            break;

        default:
            if (tab == 1)
            {
                if (last_pressed == PLUS)
                {
                    x_min /= 2;
                    x_max /= 2;
                    y_min /= 2;
                    y_max /= 2;
                }
                else if (last_pressed == MINUS)
                {
                    x_min *= 2;
                    x_max *= 2;
                    y_min *= 2;
                    y_max *= 2;
                }
            }
            else if (tab == 0 && selected_fill_box >= 0 && last_pressed >= 0)
            {
                update_fill_box(arr_fill_box[selected_fill_box], last_pressed, snd);
            }
            break;
        }

        /* Chaque suite a ses lignes de premiers termes juste apres sa formule. */
        if (tab == 0 && selected_fill_box >= 0)
        {
            int base = selected_fill_box - selected_fill_box % SEQ_LINES;
            for (int i = 0; i < SEQ_LINES; i++)
                if (arr_fill_box[base + i] == nullptr)
                    arr_fill_box[base + i] = create_fill_box(0, 20, 40, 320, 3);
        }

        int mv = 0;
        if (tab == 0)
        {
            int row = 0;
            for (int i = std::max(first_display, 0); i < SEQ_BOXES && row < 6; i++)
            {
                if (arr_fill_box[i] == nullptr || !line_visible(arr_fill_box, i))
                    continue;
                int old_mv = mv;
                int tok_n = 0;
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

                seq_set_label(arr_fill_box, i);
                display_fill_box(arr_fill_box[i], 160 - row * 40 - mv, i == selected_fill_box, i, 'u', false);
                row++;
            }
        }
        else
        {
            /* Definitions des suites actives (au plus SEQ_MAX_SEQ). */
            seq_def defs[SEQ_MAX_SEQ];
            int ids[SEQ_MAX_SEQ];
            int nd = 0;
            for (int k = 0; k < SEQ_COUNT && nd < SEQ_MAX_SEQ; k++)
            {
                if (!seq_active(arr_fill_box, k))
                    continue;
                if (seq_build(arr_fill_box, k, &defs[nd]))
                    ids[nd++] = k;
            }

            if (tab == 1)
            {
                if (need_fit)
                {
                    seq_fit(defs, nd, &x_min, &x_max, &y_min, &y_max);
                    cursor_n = selected_fill_box >= 0 ? s_n0[selected_fill_box / SEQ_LINES] : 0;
                    need_fit = false;
                }
                axis(x_min, x_max, y_min, y_max);
                for (int s = 0; s < nd; s++)
                    seq_plot(&defs[s], x_min, x_max, y_min, y_max, palet[ids[s] % 14]);

                if (selected_fill_box >= 0)
                    for (int s = 0; s < nd; s++)
                        if (ids[s] == selected_fill_box / SEQ_LINES)
                            seq_cursor(&defs[s], 'u' + (ids[s] % 6), cursor_n, x_min, x_max, y_min, y_max);
            }
            else
            {
                display_values_table(defs, ids, nd, table_start_n, table_sel_col);
            }

            for (int s = 0; s < nd; s++)
                seq_free(&defs[s]);
        }

        display_text_box(TabFormule, 0, 0, tab == 0 && selected_fill_box == -1);
        display_text_box(TabGraphe, 0, 0, tab == 1 && selected_fill_box == -1);
        display_text_box(TabValeur, 0, 0, tab == 2);

        flush_display();
        last_pressed = scan_keypad();
        while (last_pressed == -1)
            last_pressed = scan_keypad();
        sleep_ms(150);
    }
}
