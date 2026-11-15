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
#include "Cas.h"
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

/* Un texte de s_approx qui commence par ce caractere est un message
 * ("Aucune solution", erreur du calcul formel), affiche sans "~~". */
#define APPROX_MSG '\x01'
/* Valeur decimale exacte (1/2 = 0.5) : affichee apres "=" au lieu de "~~". */
#define APPROX_EQ '\x02'

/* Vrai si dec (ecriture decimale affichee) montre la valeur exacte : pas de
 * notation scientifique et au moins `needed` chiffres apres la virgule. */
static bool decimal_is_exact(const char *dec, int needed)
{
    if (needed < 0)
        return false;
    const char *dot = NULL;
    for (const char *c = dec; *c; c++)
    {
        if (*c == '.')
            dot = c;
        else if (!((*c >= '0' && *c <= '9') || *c == '-'))
            return false; /* 1.5*10^(3), e, inf... */
    }
    int shown = dot ? (int)strlen(dot + 1) : 0;
    return shown >= needed;
}

/* "~~ 0.5" en petit, a gauche de la ligne de resultat. */
static void draw_approx(int band_bottom, int band_h, const char *txt)
{
    int x = band_bottom + band_h / 2 + 4; /* haut du texte (x compte depuis le bas) */
    if (txt[0] == APPROX_MSG)
    {
        draw_char(x, 6, (char *)txt + 1, 0x4208, 0, 2);
        return;
    }
    if (txt[0] == APPROX_EQ)
    {
        draw_char(x, 6, (char *)"=", 0x4208, 0, 2);
        draw_char(x, 20, (char *)txt + 1, 0x4208, 0, 2);
        return;
    }
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

/* Resultat non defini (1/0, ln(0), racine d'une matrice...) : un message
 * au lieu de "nan". */
static void show_undefined(fill_box *res, char *approx, int cap)
{
    res->text[0] = '\0';
    res->t_size = 0;
    snprintf(approx, cap, "%cNon défini", APPROX_MSG);
}

/* Ecrit un resultat (texte de l'editeur) sur la ligne de resultat. */
static void set_result_text(fill_box *res, const char *txt)
{
    int len = (int)strlen(txt);
    if (len > 999)
        len = 999;
    memcpy(res->text, txt, len);
    res->text[len] = '\0';
    res->t_size = len;
}

/* Calcul formel. force : la saisie contient X ou une commande (developper...),
 * le resultat ou le message du moteur est toujours affiche. Sinon, le moteur
 * ne sert qu'a donner une forme exacte algebrique (fractions, racines, pi) ;
 * il rend la main (false) pour le reste.
 * approx : valeur approchee a afficher a gauche (vide si inutile). */
static bool calc_cas(const char *text, int len, bool force, fill_box *res, char *approx, int approx_cap,
                     complex *value, bool *value_ok)
{
    static char out[1000]; /* comme une case de l'historique (999 caracteres) */
    *value_ok = false;
    approx[0] = '\0';
    cas_result r = cas_eval(text, len, out, sizeof(out));
    if (!force)
    {
        if (r.status != CAS_OK || !r.numeric || r.transcendental || !isfinite(r.approx) ||
            strlen(out) > 24 || r.solutions)
            return false;
    }
    if (r.status == CAS_OK)
    {
        set_result_text(res, out);
        if (r.numeric && isfinite(r.approx))
        {
            complex z = {r.approx, 0};
            if (!r.solutions)
            {
                *value = z;
                *value_ok = true;
            }
            char dec[32];
            complex_to_string(z, dec);
            /* Pas de "~~ 12" a cote de "12" ni de "X=0.739" approche. */
            if (strcmp(dec, out) != 0 && !(r.solutions && r.approximate))
            {
                const char *eqp = strchr(out, '=');
                if (!(eqp && strcmp(eqp + 1, dec) == 0))
                {
                    if (decimal_is_exact(dec, r.dec_digits))
                        snprintf(approx, approx_cap, "%c%s", APPROX_EQ, dec);
                    else
                        snprintf(approx, approx_cap, "%s", dec);
                }
            }
        }
        if (r.msg[0])
            snprintf(approx, approx_cap, "%c%s", APPROX_MSG, r.msg);
        return true;
    }
    set_result_text(res, "");
    snprintf(approx, approx_cap, "%c%s", APPROX_MSG, r.status == CAS_ERROR ? r.msg : "Non supporte");
    return true;
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
        /* x grandit vers le haut : titre en haut, ligne 1 de la matrice
         * juste dessous (avant : titre en bas et matrice a l'envers). */
        fill_rect(SCREEN_HEIGHT - header_h, 0, header_h, SCREEN_WIDTH, BACKGROUND_COLOR);
        draw_char(SCREEN_HEIGHT - 6, 40, title, 0x0000, BACKGROUND_COLOR, 2);
        for (int r = 0; r < view_rows; r++)
        {
            for (int c = 0; c < view_cols; c++)
            {
                int ar = scroll_r + r;
                int ac = scroll_c + c;
                int x0 = SCREEN_HEIGHT - header_h - (r + 1) * cell_h;
                int y0 = c * cell_w;
                uint16_t bg = ((ar % 2 == 0) == (ac % 2 == 0)) ? FRONTGROUND_COLOR : FRONTGROUND_COLOR_BIS;
                fill_rect(x0, y0, cell_h, cell_w, bg);
                if (ar < m->rows && ac < m->cols)
                {
                    /* Un element complexe fait jusqu'a ~50 caracteres
                     * ("-3.44003917527736*10^19+1.65291460889264*10^21i") :
                     * l'ancien tampon de 24 debordait sur la pile. */
                    char nb[64];
                    complex el = mat_get(m, ar, ac);
                    int len = complex_to_string(el, nb);
                    /* Trop long pour la case : forme courte d'un reel. */
                    if (len * 7 > cell_w && el.im == 0 && isfinite(el.re))
                        len = snprintf(nb, sizeof(nb), "%.3g", el.re);
                    int ty = y0 + (cell_w - len * 7) / 2;
                    if (ty < y0)
                        ty = y0;
                    draw_char(x0 + (cell_h - 8) / 2, ty, nb, 0x0000, bg, 1);
                }
                fill_rect(x0, y0, 1, cell_w, 0x0000);
                fill_rect(x0 + cell_h - 1, y0, 1, cell_w, 0x0000);
                fill_rect(x0, y0, cell_h, 1, 0x0000);
                fill_rect(x0, y0 + cell_w - 1, cell_h, 1, 0x0000);
            }
        }
        /* Pas de blink_cursor() ici : la vue n'a pas de curseur de texte, il
         * clignoterait par-dessus la grille a sa derniere position. */
        flush_display();
        int last = scan_keypad();
        while (last == -1)
            last = scan_keypad();
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

/* Historique alloue a la demande.
 *
 * Une case n'a besoin de ses 1000 octets que pendant qu'on y ecrit : la
 * saisie en cours et le resultat en train d'etre produit. Les autres sont
 * creees quand on y arrive (hist_open), puis reduites a la longueur de leur
 * texte une fois le calcul termine (hist_close). 50 cases pleines occupent
 * ainsi quelques Ko au lieu de 51 Ko, ce qui laisse la place au calcul formel. */
#define HIST_TEXT_CAP 1000

/* Rend la case a vide et prete a recevoir HIST_TEXT_CAP octets. Faux si la
 * memoire manque (la case garde alors son ancien contenu). */
static bool hist_open(fill_box **history, int a)
{
    if (history[a] == nullptr)
    {
        fill_box *b = create_fill_box(0, 0, 40, 320, 2);
        if (b == nullptr)
            return false;
        b->color = a % 2 == 0 ? FRONTGROUND_COLOR : FRONTGROUND_COLOR_BIS;
        history[a] = b;
        return true; /* texte deja a zero et de pleine taille */
    }
    char *t = (char *)realloc(history[a]->text, HIST_TEXT_CAP);
    if (t == nullptr)
        return false;
    memset(t, 0, HIST_TEXT_CAP);
    history[a]->text = t;
    history[a]->t_size = 0;
    history[a]->curso_pos = 0;
    return true;
}

/* Case terminee : ne garde que le texte et son '\0'. Plus personne n'y ecrit
 * avant le prochain hist_open. */
static void hist_close(fill_box *b)
{
    if (b == nullptr)
        return;
    if (b->t_size < 0)
        b->t_size = 0;
    if (b->t_size > HIST_TEXT_CAP - 1)
        b->t_size = HIST_TEXT_CAP - 1;
    b->text[b->t_size] = '\0';
    char *t = (char *)realloc(b->text, (size_t)b->t_size + 1);
    if (t != nullptr)
        b->text = t;
}

/* Recopie une ligne de l'historique (saisie ou resultat) a la position du
 * curseur de la saisie en cours. Rien pour une ligne vide ("Non defini") ni
 * pour plusieurs solutions ("X=1 ; X=2"), qui ne forment pas une saisie. */
static void hist_recall(const fill_box *src, fill_box *dst)
{
    if (src == nullptr || dst == nullptr || src->t_size <= 0)
        return;
    if (memchr(src->text, CAS_SEPARATOR, (size_t)src->t_size) != nullptr)
        return;
    int n = src->t_size;
    if (dst->curso_pos < 0 || dst->curso_pos > dst->t_size)
        dst->curso_pos = dst->t_size;
    if (dst->t_size + n > HIST_TEXT_CAP - 1)
        return;
    memmove(dst->text + dst->curso_pos + n, dst->text + dst->curso_pos,
            (size_t)(dst->t_size - dst->curso_pos));
    memcpy(dst->text + dst->curso_pos, src->text, (size_t)n);
    dst->t_size += n;
    dst->curso_pos += n;
    dst->text[dst->t_size] = '\0';
}

/* L'historique survit a la sortie de l'appli (BACK ou HOME) : il est cree
 * au premier lancement et garde ensuite, comme sur les autres calculatrices.
 * Chaque case terminee est deja reduite a la longueur de son texte. */
static fill_box **s_history = nullptr;
static fill_box *s_blank = nullptr;
/* Compteurs croissants : la case utilisee est compteur % HISTORY_SIZE.
 * Chaque calcul prend deux cases (saisie paire, resultat impair) ;
 * HISTORY_SIZE est pair, donc la parite est conservee au retour a 0. */
static int s_last_history = 0;
static bool s_have_ans = false; /* un calcul a deja abouti */

int Calc()
{
    bool snd = false;
    if (s_history == nullptr)
    {
        fill_box **h = (fill_box **)calloc(HISTORY_SIZE + 1, sizeof(fill_box *));
        if (h == nullptr)
            return 0;
        if (!hist_open(h, 0)) /* seule la premiere saisie existe au depart */
        {
            free(h);
            return 0;
        }
        s_history = h;
        s_last_history = 0;
        s_have_ans = false;
        memset(s_approx, 0, sizeof(s_approx));
    }
    if (s_blank == nullptr)
        s_blank = create_fill_box(0, 0, 40, 320, 2);
    fill_box **history = s_history;
    fill_box *blank = s_blank;
    /* References : le reste de la fonction lit et ecrit l'etat garde. */
    int &cur_last_history = s_last_history;
    bool &have_ans = s_have_ans;
    int cur_selected = cur_last_history;
    history[cur_last_history % HISTORY_SIZE]->curso_pos = history[cur_last_history % HISTORY_SIZE]->t_size;
    fill_screen(BACKGROUND_COLOR); // Bleu

    while (true)
    {
        int mv = 0;
        matrix_blink_state saved_blink = {0};

        for (int i = cur_selected; i >= cur_selected - 5; i--)
        {
            if (i < 0 || i <= cur_last_history - HISTORY_SIZE)
            {
                /* Avant le plus ancien calcul garde : case vide. */
                int par = ((i % 2) + 2) % 2;
                blank->color = par == 0 ? FRONTGROUND_COLOR : FRONTGROUND_COLOR_BIS;
                blank->h = 40;
                display_fill_box(blank, 160 - (i - cur_selected + 4) * 40 + mv, false, -1, ' ', par == 1);
                continue;
            }
            int a = i % HISTORY_SIZE;
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
                display_fill_box(history[a], 160 - (i - cur_selected + 4) * 40 + old_mv, i == cur_selected, -1, ' ',a%2==1);
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
                 * comme avec la touche X. */
                if (ch == 'x' || ch == 'X')
                {
                    update_fill_box(history[cur_last_history % HISTORY_SIZE], X, snd);
                    break;
                }
                /* '<' et '>' du clavier : signes d'inequation (le texte
                 * interne ne les ecrit pas en ASCII, voir Cas.h). */
                if (ch == '<' || ch == '>')
                {
                    update_fill_box(history[cur_last_history % HISTORY_SIZE], ch == '<' ? KEY_LT : KEY_GT, snd);
                    break;
                }
                insert_ans_if_empty(history[cur_last_history % HISTORY_SIZE], have_ans, -1, ch);
                if (!matrix_cell_edit(history[cur_last_history % HISTORY_SIZE], ch, true))
                    fill_box_insert_char(history[cur_last_history % HISTORY_SIZE], (char)ch);
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
            if (!matrix_cell_edit(history[cur_last_history % HISTORY_SIZE], UP, false) &&
                cur_selected > 0 && cur_selected > cur_last_history - (HISTORY_SIZE - 1))
                cur_selected--;
            break;
        case DOWN:
            if (!matrix_cell_edit(history[cur_last_history % HISTORY_SIZE], DOWN, false))
                cur_selected = min(cur_last_history, cur_selected + 1);
            break;
        case OK:
        case ENTER:
        {
            /* Ligne de l'historique selectionnee : OK la recopie dans la
             * saisie (au curseur), sans rien calculer. */
            if (cur_selected != cur_last_history)
            {
                hist_recall(history[cur_selected % HISTORY_SIZE], history[cur_last_history % HISTORY_SIZE]);
                cur_selected = cur_last_history;
                break;
            }
            /* Saisie vide : rien a calculer (avant : une ligne "0"). */
            if (history[cur_last_history % HISTORY_SIZE]->t_size <= 0)
                break;
            int l = cur_last_history % HISTORY_SIZE;
            /* Cases du resultat et de la prochaine saisie : creees si c'est
             * leur premier usage, videes si elles contiennent un ancien
             * calcul (apres un tour complet). Sans memoire pour elles, la
             * saisie reste en place et rien n'est calcule. */
            if (!hist_open(history, (cur_last_history + 1) % HISTORY_SIZE) ||
                !hist_open(history, (cur_last_history + 2) % HISTORY_SIZE))
                break;
            cur_last_history++;
            s_approx[cur_last_history % HISTORY_SIZE][0] = '\0';
            s_approx[(cur_last_history + 1) % HISTORY_SIZE][0] = '\0';
            int r = cur_last_history % HISTORY_SIZE; /* case du resultat */
            history[r]->hscroll = 0;
            history[(cur_last_history + 1) % HISTORY_SIZE]->hscroll = 0;
            int tokenized_size;

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
            const char *src = history[l]->text;
            int src_len = history[l]->t_size;
            /* X hors de somme/derivee... ou commande : calcul formel. */
            bool force_cas = has_calculus ? cas_has_command(src, src_len) : cas_required(src, src_len);
            complex cas_value;
            bool cas_value_ok = false;
            bool done_cas = false;
            if (force_cas || (!has_matrix && !has_calculus && exact_input_allowed(src, src_len)))
                done_cas = calc_cas(src, src_len, force_cas, history[r], s_approx[r],
                                    sizeof(s_approx[r]), &cas_value, &cas_value_ok);
            if (done_cas)
            {
                if (cas_value_ok && result_ok(cas_value))
                {
                    set_ans(cas_value);
                    have_ans = true;
                }
            }
            else if (has_matrix && !has_calculus)
            {
                calc_result cr = evaluate_calc(out, yarded);
                if (cr.is_matrix)
                {
                    char buf[96];
                    int len = mat_to_string(cr.m, buf, 96);
                    for (int i = 0; i < len; i++)
                        history[r]->text[i] = buf[i];
                    history[r]->t_size = len;
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
                    if (result_ok(cr.scalar))
                        history[r]->t_size = complex_to_string(cr.scalar, history[r]->text);
                    else
                        show_undefined(history[r], s_approx[r], sizeof(s_approx[r]));
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
                fill_box *res = history[r];
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
                    complex_to_string(temp, s_approx[r]);
                }
                else if (!result_ok(temp))
                    show_undefined(res, s_approx[r], sizeof(s_approx[r]));
                else
                    res->t_size = complex_to_string(temp, res->text);
            }
            cur_last_history++;
            cur_selected = cur_last_history;
            token_free_matrices(t, tokenized_size);
            free(t);
            free(out);
            hist_close(history[l]);
            hist_close(history[r]);
        }
            history[cur_last_history % HISTORY_SIZE]->curso_pos = history[cur_last_history % HISTORY_SIZE]->t_size;
            break;
        case LEFT:
            /* Resultat trop large selectionne dans l'historique : defilement. */
            if (cur_selected != cur_last_history && history[cur_selected % HISTORY_SIZE] &&
                history[cur_selected % HISTORY_SIZE]->hscroll > 0)
            {
                history[cur_selected % HISTORY_SIZE]->hscroll -= 48;
                break;
            }
            cur_selected = cur_last_history;
            if (!matrix_cell_edit(history[cur_selected % HISTORY_SIZE], LEFT, false))
                fill_box_left(history[cur_selected % HISTORY_SIZE]);
            break;
        case RIGHT:
            if (cur_selected != cur_last_history && history[cur_selected % HISTORY_SIZE] &&
                history[cur_selected % HISTORY_SIZE]->hscroll < history[cur_selected % HISTORY_SIZE]->hscroll_max)
            {
                history[cur_selected % HISTORY_SIZE]->hscroll += 48;
                break;
            }
            cur_selected = cur_last_history;
            if (!matrix_cell_edit(history[cur_selected % HISTORY_SIZE], RIGHT, false))
                fill_box_right(history[cur_selected % HISTORY_SIZE]);
            break;
        case X:
            /* X : variable de somme/derivee..., ou variable du calcul formel. */
            update_fill_box(history[cur_last_history % HISTORY_SIZE], X, snd);
            break;
        case BACK:
            if (history[cur_last_history % HISTORY_SIZE]->t_size > 0)
            {
                if (!matrix_cell_edit(history[cur_last_history % HISTORY_SIZE], BACK, false))
                    update_fill_box(history[cur_last_history % HISTORY_SIZE], last_pressed, snd);
            }
            else
                return 0; /* l'historique est garde pour la prochaine fois */
            break;
        case TOOLS:
        {
            
            update_fill_box(history[cur_last_history % HISTORY_SIZE], menu_tools(true, true), snd);
        }
        break;
        default:
            if (last_pressed == -1)
                break;
            insert_ans_if_empty(history[cur_last_history % HISTORY_SIZE], have_ans, last_pressed, 0);
            if (!matrix_cell_edit(history[cur_last_history % HISTORY_SIZE], last_pressed, false))
                update_fill_box(history[cur_last_history % HISTORY_SIZE], last_pressed, snd);
            break;
        }

    }
    return 0;
}
