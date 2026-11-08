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

#define TBL_TOP 22
#define TBL_LABEL_W 38
#define TBL_COL_W 40
#define TBL_COLS 7
#define TBL_ROW_N_H 24
#define TBL_ROW_VAL_H 30
#define TBL_MAX_SUITES 4

/* Suites : chaque suite occupe DEUX lignes de l'onglet Formule.
 *   ligne 2k   : formule
 *                - sans U : suite explicite  u(n) = f(N)
 *                - avec U : suite recurrente u(n+1) = f(N, U)
 *                  U = u(n), N = n (indice courant)
 *   ligne 2k+1 : premier terme u(0) (utilise seulement si la formule a un U)
 * N s'obtient avec la touche X, U avec ALPHA (ou 2nd puis 0).
 * Outils : blocs prets a l'emploi (arith, geom, arigeo, arithn) inseres d'un
 * seul bloc ; leur nom et leurs parentheses ne peuvent pas etre effaces un a un. */
#define SEQ_BOXES 100
#define SEQ_COUNT (SEQ_BOXES / 2)
#define SEQ_MAX_SEQ TBL_MAX_SUITES
#define SEQ_MAX_N 5000 /* indice max calcule pour une suite recurrente */
#define SEQ_MAX_U 16
#define SEQ_FIT_N 20

typedef struct
{
    bool recurrent;
    token *tok; /* formule en notation postfixee */
    int ntok;
    int uidx[SEQ_MAX_U]; /* positions des U dans tok */
    int nu;
    double u0;
} seq_def;

typedef struct
{
    int n;
    double u; /* u(n) */
} seq_iter;

static bool box_has_text(fill_box *b)
{
    return b != nullptr && b->t_size > 0;
}

static bool seq_active(fill_box **arr, int k)
{
    return k >= 0 && k < SEQ_COUNT && box_has_text(arr[2 * k]);
}

/* Construit la definition d'une suite a partir de ses deux lignes. */
static bool seq_build(fill_box *f, fill_box *init, seq_def *s)
{
    memset(s, 0, sizeof(*s));
    if (!box_has_text(f))
        return false;

    int tn = 0;
    token *raw = parse_string_to_token(f->text, f->t_size, &tn);
    s->tok = shunting_yard(raw, tn);
    s->ntok = count_yarded(raw, tn);
    token_free_matrices(raw, tn);
    free(raw);

    for (int i = 0; i < s->ntok; i++)
        if (s->tok[i].type == 'X' && (int)s->tok[i].value + (int)'a' == 'U' && s->nu < SEQ_MAX_U)
            s->uidx[s->nu++] = i;
    s->recurrent = s->nu > 0;
    /* Les blocs arith/geom/arigeo/arithn contiennent un U implicite. */
    for (int i = 0; i < s->ntok; i++)
        if (is_block_type(s->tok[i].type))
            s->recurrent = true;

    s->u0 = 0;
    if (s->recurrent && box_has_text(init))
    {
        int in_n = 0;
        token *ir = parse_string_to_token(init->text, init->t_size, &in_n);
        token *io = shunting_yard(ir, in_n);
        int iy = count_yarded(ir, in_n);
        s->u0 = evaluate_npi(io, iy, 0, 'N');
        token_free_matrices(ir, in_n);
        free(ir);
        free(io);
    }
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

/* Suite recurrente : on part de u(0) et on avance terme par terme.
 * u(n+1) = f(N = n, U = u(n)). Les U du postfixe sont remplaces par la valeur
 * courante (jetons 'n' reecrits a chaque pas). */
static void seq_iter_init(seq_def *s, seq_iter *it)
{
    it->n = 0;
    it->u = s->u0;
}

static void seq_iter_next(seq_def *s, seq_iter *it)
{
    for (int k = 0; k < s->nu; k++)
    {
        s->tok[s->uidx[k]].type = 'n';
        s->tok[s->uidx[k]].value = it->u;
    }
    g_seq_u = it->u; /* lus par les blocs */
    g_seq_n = it->n;
    it->u = evaluate_npi(s->tok, s->ntok, (double)it->n, 'N');
    it->n++;
}

/* Valeur de la suite en n (ok = false si non definie / trop loin). */
static double seq_value(seq_def *s, int n, bool *ok)
{
    *ok = true;
    if (!s->recurrent)
        return seq_explicit(s, n);
    if (n < 0 || n > SEQ_MAX_N)
    {
        *ok = false;
        return NAN;
    }
    seq_iter it;
    seq_iter_init(s, &it);
    while (it.n < n)
        seq_iter_next(s, &it);
    return it.u;
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
        for (int c = 0; c < TBL_COLS; c++)
        {
            have[c] = true;
            vals[c] = NAN;
        }
        if (defs[s].recurrent)
        {
            /* Une seule passe de 0 a la derniere colonne affichee. */
            int last = start_n + TBL_COLS - 1;
            if (last > SEQ_MAX_N)
            {
                for (int c = 0; c < TBL_COLS; c++)
                    have[c] = false;
            }
            else
            {
                seq_iter it;
                seq_iter_init(&defs[s], &it);
                for (int n = 0; n <= last; n++)
                {
                    if (n >= start_n)
                        vals[n - start_n] = it.u;
                    if (n < last)
                        seq_iter_next(&defs[s], &it);
                }
            }
        }
        else
        {
            for (int c = 0; c < TBL_COLS; c++)
                vals[c] = seq_explicit(&defs[s], start_n + c);
        }

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
    int n_lo = (int)lo, n_hi = (int)hi;
    int step = (n_hi - n_lo + 1) / SCREEN_WIDTH;
    if (step < 1)
        step = 1;

    if (s->recurrent)
    {
        if (n_hi > SEQ_MAX_N)
            n_hi = SEQ_MAX_N;
        seq_iter it;
        seq_iter_init(s, &it);
        for (int n = 0; n <= n_hi; n++)
        {
            if (n >= n_lo && (n % step) == 0)
                seq_point(n, it.u, x_min, x_max, y_min, y_max, color);
            if (n < n_hi)
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
    for (int s = 0; s < nd; s++)
    {
        seq_iter it;
        if (defs[s].recurrent)
            seq_iter_init(&defs[s], &it);
        for (int n = 0; n <= SEQ_FIT_N; n++)
        {
            double v;
            if (defs[s].recurrent)
            {
                v = it.u;
                if (n < SEQ_FIT_N)
                    seq_iter_next(&defs[s], &it);
            }
            else
                v = seq_explicit(&defs[s], n);
            if (isnan(v) || isinf(v))
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
    *x_min = -1;
    *x_max = SEQ_FIT_N + 1;
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

static void seq_cleanup(fill_box **arr, text_box *t1, text_box *t2, text_box *t3)
{
    for (int i = 0; i < SEQ_BOXES; i++)
    {
        if (arr[i] != nullptr)
        {
            free(arr[i]->text);
            free(arr[i]);
        }
    }
    free(arr);
    free_text_box(t1);
    free_text_box(t2);
    free_text_box(t3);
}

int Sequencer()
{
    bool snd = false;

    text_box *TabFormule = create_text_box(0, 220, 20, 107, 1, false);
    text_box *TabGraphe = create_text_box(107, 220, 20, 107, 1, false);
    text_box *TabValeur = create_text_box(214, 220, 20, 106, 1, false);
    TabFormule->text = "Formule";
    TabFormule->t_size = 7;
    TabGraphe->text = "Graphe";
    TabGraphe->t_size = 6;
    TabValeur->text = "Valeur";
    TabValeur->t_size = 6;

    const uint16_t palet[] = {
        COLOR_RED, COLOR_LIME, COLOR_BLUE, COLOR_YELLOW,
        COLOR_CYAN, COLOR_MAGENTA, COLOR_SILVER, COLOR_GRAY,
        COLOR_MAROON, COLOR_OLIVE, COLOR_GREEN, COLOR_PURPLE,
        COLOR_TEAL, COLOR_NAVY};

    fill_box **arr_fill_box = (fill_box **)malloc(sizeof(fill_box *) * SEQ_BOXES);
    for (int i = 0; i < SEQ_BOXES; i++)
        arr_fill_box[i] = nullptr;
    arr_fill_box[0] = create_fill_box(0, 20, 40, 320, 3);
    arr_fill_box[1] = create_fill_box(0, 20, 40, 320, 3);

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
            {
                seq_cleanup(arr_fill_box, TabFormule, TabGraphe, TabValeur);
                return 0;
            }
            break;

        case UP:
            if (tab == 0)
            {
                selected_fill_box = std::max(-1, selected_fill_box - 1);
                if (selected_fill_box < first_display)
                    first_display = selected_fill_box;
            }
            else if (tab == 1)
            {
                /* En mode graphe on selectionne une suite (pas une ligne). */
                int k = selected_fill_box < 0 ? -1 : selected_fill_box / 2;
                int j = k - 1;
                while (j >= 0 && !seq_active(arr_fill_box, j))
                    j--;
                selected_fill_box = j >= 0 ? 2 * j : -1;
                cursor_n = 0;
            }
            break;

        case DOWN:
            if (tab == 0)
            {
                selected_fill_box = std::min(SEQ_BOXES - 1, selected_fill_box + 1);
                if (arr_fill_box[selected_fill_box] == nullptr)
                    arr_fill_box[selected_fill_box] = create_fill_box(0, 20, 40, 320, 3);
                if (first_display + 4 < selected_fill_box)
                    first_display++;
            }
            else if (tab == 1)
            {
                int k = selected_fill_box < 0 ? -1 : selected_fill_box / 2;
                int j = k + 1;
                while (j < SEQ_COUNT && !seq_active(arr_fill_box, j))
                    j++;
                if (j < SEQ_COUNT)
                {
                    selected_fill_box = 2 * j;
                    cursor_n = 0;
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
                    if (cursor_n > 0)
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
                /* Ligne de formule : blocs de recurrence (puis outils
                 * classiques). Ligne du premier terme : outils seuls. */
                int ev = (selected_fill_box % 2 == 0) ? menu_seq_blocks() : menu_tools();
                update_fill_box(arr_fill_box[selected_fill_box], ev, snd);
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

        /* Chaque suite a sa ligne "premier terme" juste apres sa formule. */
        if (tab == 0 && selected_fill_box >= 0)
            for (int i = 0; i < 2; i++)
            {
                int base = selected_fill_box & ~1;
                if (arr_fill_box[base + i] == nullptr)
                    arr_fill_box[base + i] = create_fill_box(0, 20, 40, 320, 3);
            }

        int mv = 0;
        if (tab == 0)
        {
            for (int i = std::max(first_display, 0); i < first_display + 6 && i < SEQ_BOXES; i++)
            {
                if (arr_fill_box[i] == nullptr)
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

                display_fill_box(arr_fill_box[i], 160 - (i - std::max(first_display, 0)) * 40 - mv, i == selected_fill_box, i, 'u', false);
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
                if (seq_build(arr_fill_box[2 * k], arr_fill_box[2 * k + 1], &defs[nd]))
                    ids[nd++] = k;
            }

            if (tab == 1)
            {
                if (need_fit)
                {
                    seq_fit(defs, nd, &x_min, &x_max, &y_min, &y_max);
                    cursor_n = 0;
                    need_fit = false;
                }
                axis(x_min, x_max, y_min, y_max);
                for (int s = 0; s < nd; s++)
                    seq_plot(&defs[s], x_min, x_max, y_min, y_max, palet[ids[s] % 14]);

                if (selected_fill_box >= 0)
                    for (int s = 0; s < nd; s++)
                        if (ids[s] == selected_fill_box / 2)
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