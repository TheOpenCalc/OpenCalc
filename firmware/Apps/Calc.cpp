#include "ui.h"
#include "Calc.h"
#include "Evaluator.h"
#include "menu.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <stdio.h>
#include <cstdlib>
#include "Tools.h"
#include "colors.h"
#include "Exact.h"
#include "Calculus.h"
#include "widgets.h"
#include <math.h>
#include <string.h>

static void fill_box_insert_char(fill_box *in, char c)
{
    if (in->t_size >= 1000)
        return;
    for (int i = in->t_size; i > in->curso_pos; i--)
        in->text[i] = in->text[i - 1];
    in->text[in->curso_pos] = c;
    in->t_size++;
    in->curso_pos++;
}

/* Valeur approchee affichee a cote d'un resultat ecrit sous forme exacte
 * (fraction, racine, multiple de pi). Vide pour les autres lignes. */
static char s_approx[HISTORY_SIZE + 1][32];

/* "~~ 0.5" en petit, a gauche de la ligne de resultat. */
static void draw_approx(int band_bottom, int band_h, const char *txt)
{
    int x = band_bottom + band_h / 2 + 4; /* haut du texte (x compte depuis le bas) */
    draw_char(x + 1, 6, (char *)"~", 0x4208, 0, 2);
    draw_char(x - 3, 6, (char *)"~", 0x4208, 0, 2);
    draw_char(x, 20, (char *)txt, 0x4208, 0, 2);
}

/* Une ligne vide qui commence par + * / ^ continue le dernier resultat :
 * on ecrit "ans" devant l'operateur. Pas pour '-', qui sert aussi de signe. */
static bool starts_with_ans(int key, int ch)
{
    if (ch)
        return ch == '+' || ch == '*' || ch == '/' || ch == '^';
    return key == PLUS || key == TIMES || key == DIVIDE || key == POW;
}

static void insert_ans_if_empty(fill_box *in, bool have_ans, int key, int ch)
{
    if (!have_ans || in->t_size != 0 || !starts_with_ans(key, ch))
        return;
    fill_box_insert_char(in, 'a');
    fill_box_insert_char(in, 'n');
    fill_box_insert_char(in, 's');
}

/* Ans ne change pas quand le calcul echoue (erreur, NaN). */
static bool result_ok(complex z)
{
    return !isnan(z.re) && !isnan(z.im);
}

static void matrix_view(matrix *m)
{
    fill_screen(BACKGROUND_COLOR);
    const int header_h = 24;
    const int cell_h = 40;
    const int cell_w = 64;
    int view_rows = (SCREEN_HEIGHT - header_h) / cell_h;
    int view_cols = SCREEN_WIDTH / cell_w;
    int scroll_r = 0, scroll_c = 0;
    while (true)
    {
        char title[24];
        sprintf(title, "Matrice %dx%d", m->rows, m->cols);
        draw_char(18, 4, title, 0x0000, BACKGROUND_COLOR, 2);
        for (int r = 0; r < view_rows; r++)
        {
            for (int c = 0; c < view_cols; c++)
            {
                int ar = scroll_r + r;
                int ac = scroll_c + c;
                int x0 = header_h + r * cell_h;
                int y0 = c * cell_w;
                uint16_t bg = ((ar % 2 == 0) == (ac % 2 == 0)) ? FRONTGROUND_COLOR : FRONTGROUND_COLOR_BIS;
                fill_rect(x0, y0, cell_h, cell_w, bg);
                if (ar < m->rows && ac < m->cols)
                {
                    char nb[24];
                    int len = complex_to_string(mat_get(m, ar, ac), nb);
                    int ty = y0 + (cell_w - len * 7) / 2;
                    draw_char(x0 + (cell_h - 8) / 2, ty, nb, 0x0000, bg, 1);
                }
                fill_rect(x0, y0, 1, cell_w, 0x0000);
                fill_rect(x0 + cell_h - 1, y0, 1, cell_w, 0x0000);
                fill_rect(x0, y0, cell_h, 1, 0x0000);
                fill_rect(x0, y0 + cell_w - 1, cell_h, 1, 0x0000);
            }
        }
        int last = scan_keypad();
        while (last == -1)
        {
            blink_cursor();
            last = scan_keypad();
        }
        if (last == OK || last == BACK || last == ENTER)
            return;
        if (last == UP && scroll_r > 0)
            scroll_r--;
        else if (last == DOWN && scroll_r < m->rows - view_rows)
            scroll_r++;
        else if (last == LEFT && scroll_c > 0)
            scroll_c--;
        else if (last == RIGHT && scroll_c < m->cols - view_cols)
            scroll_c++;
    }
}

int Calc()
{
    bool snd = false;
    fill_box **history = (fill_box **)malloc(sizeof(fill_box *) * (HISTORY_SIZE + 1));

    for (int i = 0; i <= HISTORY_SIZE; i++)
    {
        history[i] = create_fill_box(0, 0, 40, 320, 2);
        history[i]->color = i % 2 == 0 ? FRONTGROUND_COLOR : FRONTGROUND_COLOR_BIS;
    }

    int cur_last_history = 0;
    bool have_ans = false; /* un calcul a deja abouti dans cette session */
    int cur_selected = 0;
    memset(s_approx, 0, sizeof(s_approx));
    fill_screen(BACKGROUND_COLOR); // Bleu

    while (true)
    {
        int mv = 0;
        matrix_blink_state saved_blink = {0};

        for (int i = cur_selected; i >= cur_selected - 5; i--)
        {
            int a = i % HISTORY_SIZE;
            if (a < 0)
            {
                a += HISTORY_SIZE;
            }
            if (history[a] != nullptr)
            {
                int old_mv = mv;
                int tok_n = 0;
                int input_size;
                display_set_source(history[a]->text, history[a]->t_size);
                token *toks = parse_string_to_token(history[a]->text, history[a]->t_size, &tok_n);
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
                history[a]->h = mv - old_mv + 40;
                display_fill_box(history[a], 160 - (i - cur_selected + 4) * 40 + old_mv, (a - cur_selected) % HISTORY_SIZE == 0, -1, ' ',a%2==1);
                if (s_approx[a][0])
                    draw_approx(160 - (i - cur_selected + 4) * 40 + old_mv, history[a]->h, s_approx[a]);
                if (i == cur_selected)
                    matrix_blink_save(&saved_blink);
            }
        }
        matrix_blink_restore(&saved_blink);
        flush_display();

        int last_pressed = -1;
        while (last_pressed == -1)
        {
            blink_cursor();
            int ch = scan_text_char();
            if (ch >= 32 && ch <= 126)
            {
                scan_keypad_nowait();
                /* Clavier du PC (simulateur) : la variable est X majuscule,
                 * comme avec la touche X, et seulement dans somme / produit /
                 * derivee / integrale. */
                if (ch == 'x' || ch == 'X')
                {
                    fill_box *in = history[cur_last_history];
                    if (cursor_in_calculus_call(in->text, in->curso_pos))
                        update_fill_box(in, X, snd);
                    break;
                }
                insert_ans_if_empty(history[cur_last_history], have_ans, -1, ch);
                if (!matrix_cell_edit(history[cur_last_history], ch, true))
                    fill_box_insert_char(history[cur_last_history], (char)ch);
                break;
            }
            last_pressed = scan_keypad();
        }

        sleep_ms(50);

        switch (last_pressed)
        {
        case SECOND:
            toggle(&snd);
            break;
        case UP:
            if (!matrix_cell_edit(history[cur_last_history], UP, false))
                cur_selected = (cur_selected - 1 + HISTORY_SIZE) % HISTORY_SIZE;
            break;
        case DOWN:
            if (!matrix_cell_edit(history[cur_last_history], DOWN, false))
                cur_selected = min(cur_last_history, (cur_selected + 1) % HISTORY_SIZE);
            break;
        case OK:
        case ENTER:
        {
            int l = cur_last_history;
            cur_last_history = (cur_last_history + 1) % HISTORY_SIZE;
            if (cur_last_history < 0)
            {
                cur_last_history += HISTORY_SIZE + 1;
            }
            if (history[cur_last_history] == nullptr)
            {
                history[cur_last_history] = create_fill_box(0, 0, 20, 320, 2);
            }
            int tokenized_size;
            s_approx[cur_last_history][0] = '\0';

            token *t = parse_string_to_token(history[l]->text, history[l]->t_size, &tokenized_size);

            /* somme / produit / derivee / integrale : evaluees sur le texte. */
            bool has_calculus = calculus_has_call(history[l]->text, history[l]->t_size);
            bool has_matrix = false;
            for (int i = 0; i < tokenized_size; i++)
            {
                if (t[i].type == 'M' || t[i].type == 'F' || t[i].type == 'G' || t[i].type == 'H')
                {
                    has_matrix = true;
                    break;
                }
            }
            if (tokens_use_matrix_ans(t, tokenized_size))
                has_matrix = true;

            token *out = shunting_yard(t, tokenized_size);
            int yarded = count_yarded(t, tokenized_size);
            if (has_matrix && !has_calculus)
            {
                calc_result cr = evaluate_calc(out, yarded);
                if (cr.is_matrix)
                {
                    char buf[96];
                    int len = mat_to_string(cr.m, buf, 96);
                    for (int i = 0; i < len; i++)
                        history[cur_last_history]->text[i] = buf[i];
                    history[cur_last_history]->t_size = len;
                    set_ans_matrix(cr.m);
                    have_ans = true;
                    matrix_view(cr.m);
                }
                else
                {
                    if (result_ok(cr.scalar))
                    {
                        set_ans(cr.scalar);
                        have_ans = true;
                    }
                    history[cur_last_history]->t_size = complex_to_string(cr.scalar, history[cur_last_history]->text);
                }
                calc_result_free(&cr);
            }
            else
            {
                complex temp = has_calculus ? calculus_eval(history[l]->text, history[l]->t_size)
                                            : evaluate_npi_c(out, yarded);
                if (result_ok(temp))
                {
                    set_ans(temp);
                    have_ans = true;
                }
                fill_box *res = history[cur_last_history];
                /* Ecriture exacte (1/3, r(3)/2, 3p/4...) quand la saisie ne
                 * contient pas de nombre decimal et que le resultat est reel.
                 * Pas pour derivee/integrale : leur resultat est approche. */
                char exact[64];
                bool real = isfinite(temp.re) && fabs(temp.im) <= 1e-12 * fmax(1.0, fabs(temp.re));
                if (real && exact_input_allowed(history[l]->text, history[l]->t_size) &&
                    !calculus_is_approx(history[l]->text, history[l]->t_size) &&
                    exact_form(temp.re, exact, sizeof(exact)))
                {
                    int len = (int)strlen(exact);
                    memcpy(res->text, exact, len + 1);
                    res->t_size = len;
                    complex_to_string(temp, s_approx[cur_last_history]);
                }
                else
                    res->t_size = complex_to_string(temp, res->text);
            }
            cur_last_history++;
            cur_selected = cur_last_history;
            if (cur_last_history <= HISTORY_SIZE)
                s_approx[cur_last_history][0] = '\0';
            token_free_matrices(t, tokenized_size);
            free(t);
            free(out);
        }
            history[cur_last_history]->curso_pos = history[cur_last_history]->t_size;
            break;
        case LEFT:
            cur_selected = cur_last_history;
            if (!matrix_cell_edit(history[cur_selected], LEFT, false))
                fill_box_left(history[cur_selected]);
            break;
        case RIGHT:
            cur_selected = cur_last_history;
            if (!matrix_cell_edit(history[cur_selected], RIGHT, false))
                fill_box_right(history[cur_selected]);
            break;
        case X:
            /* La variable X n'a de sens que dans somme/produit/derivee/integrale. */
            if (cursor_in_calculus_call(history[cur_last_history]->text, history[cur_last_history]->curso_pos))
                update_fill_box(history[cur_last_history], X, snd);
            break;
        case BACK:
            if (history[cur_last_history]->t_size > 0)
            {
                if (!matrix_cell_edit(history[cur_last_history], BACK, false))
                    update_fill_box(history[cur_last_history], last_pressed, snd);
            }
            else
            {
                return 0;
            }
            break;
        case TOOLS:
        {
            
            update_fill_box(history[cur_last_history], menu_tools(true), snd);
        }
        break;
        default:
            if (last_pressed == -1)
                break;
            insert_ans_if_empty(history[cur_last_history], have_ans, last_pressed, 0);
            if (!matrix_cell_edit(history[cur_last_history], last_pressed, false))
                update_fill_box(history[cur_last_history], last_pressed, snd);
            break;
        }

        if (cur_selected < 0)
        {
            cur_selected += 50;
        }
    }
    return 0;
}
