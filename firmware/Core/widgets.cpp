#include "widgets.h"
#include "display.h"
#include "ui.h"
#include <malloc.h>
#include "colors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Buffer holding the matrix literal produced by the n*m grid editor
// (menu > Matrices > Matrice n*m), consumed by update_fill_box(MATRIX_CUSTOM).
static char g_custom_matrix[4096];

fill_box *create_fill_box(int x, int y, int h, int w, int border)
{
    fill_box *out = (fill_box *)malloc(sizeof(fill_box));
    out->curso_pos = 0;
    out->x = x;
    out->y = y;
    out->h = h;
    out->w = w;
    out->border = border;
    out->color = 0xffff;
    out->t_size = 0;
    char *in = (char *)malloc(sizeof(char) * 1000);
    for (int i = 0; i < 1000; i++)
    {
        in[i] = '\0';
    }
    out->text = in;
    return out;
}

text_box *create_text_box(int x, int y, int h, int w, int border, bool transparent)
{
    text_box *out = (text_box *)malloc(sizeof(text_box));
    out->allign = 'c';
    out->display_text_size = 1;
    out->transparent = transparent;
    out->x = x;
    out->y = y;
    out->h = h;
    out->w = w;
    out->border = border;
    out->t_size = 0;
    out->col = 0xffff;
    char *in = (char *)malloc(sizeof(char) * 100);
    for (int i = 0; i < 100; i++)
    {
        in[i] = '\0';
    }
    out->text_buf = in;
    out->text = in;
    return out;
}

void free_text_box(text_box *in)
{
    if (in == nullptr)
        return;
    // `text` can point to a literal set by the caller; only the buffer owned
    // by the widget may be released.
    free(in->text_buf);
    in->text_buf = nullptr;
    in->text = nullptr;
    free(in);
}

// Releases an array of text boxes created by create_text_box().
static void free_text_boxes(text_box **items, int n)
{
    if (items == nullptr)
        return;
    for (int i = 0; i < n; i++)
        free_text_box(items[i]);
    free(items);
}

static int call_name_start(const char *text, int lparen);

// When the cursor sits inside the name of a structured call (between the
// first letter and the '(' of "somme(", "pgcd("...), returns the index of
// that '('; otherwise -1. Typing there would corrupt the name.
static int call_lparen_if_in_name(const fill_box *in)
{
    int k = in->curso_pos;
    while (k < in->t_size && in->text[k] >= 'a' && in->text[k] <= 'z')
        k++;
    if (k >= in->t_size || in->text[k] != '(')
        return -1;
    int s = call_name_start(in->text, k);
    return (s >= 0 && s < in->curso_pos) ? k : -1;
}

// "ans" (Ans) forme un seul bloc : le curseur ne s'arrete jamais au milieu,
// et Effacer le retire en entier (sinon le 'a' restant se lit acosh).
// Renvoie le debut du mot "ans" si s < pos < s + 3 (inside) ou
// s < pos <= s + 3 (!inside : curseur juste apres compris), -1 sinon.
// Le "ans" de "trans(" n'est pas Ans.
static int ans_word_around(const char *text, int len, int pos, bool inside)
{
    if (!text)
        return -1;
    for (int s = pos - 3; s < pos; s++)
    {
        if (s < 0 || s + 3 > len)
            continue;
        if (inside && pos == s + 3)
            continue;
        if (text[s] != 'a' || text[s + 1] != 'n' || text[s + 2] != 's')
            continue;
        if (s >= 2 && text[s - 2] == 't' && text[s - 1] == 'r')
            continue;
        return s;
    }
    return -1;
}

void fill_box_left(fill_box *in)
{
    do
    {
        in->curso_pos--;
    } while (in->curso_pos > 1 && in->text[in->curso_pos - 1] == '/');
    in->curso_pos = max(in->curso_pos, 0);
    int as = ans_word_around(in->text, in->t_size, in->curso_pos, true);
    if (as >= 0)
        in->curso_pos = as;
    // Never stop inside a call name: jump before the whole name.
    int lp = call_lparen_if_in_name(in);
    if (lp >= 0)
        in->curso_pos = call_name_start(in->text, lp);
}

void fill_box_right(fill_box *in)
{
    do
    {
        in->curso_pos++;
    } while (in->curso_pos < in->t_size - 1 && in->text[in->curso_pos + 1] == '/');
    in->curso_pos = min(in->curso_pos, in->t_size);
    int as = ans_word_around(in->text, in->t_size, in->curso_pos, true);
    if (as >= 0)
        in->curso_pos = as + 3;
    // Never stop inside a call name: jump into the first argument.
    int lp = call_lparen_if_in_name(in);
    if (lp >= 0)
        in->curso_pos = lp + 1;
}

static void box_insert_at(fill_box *in, int pos, char c)
{
    if (in->t_size >= 999)
        return;
    for (int i = in->t_size; i > pos; i--)
        in->text[i] = in->text[i - 1];
    in->text[pos] = c;
    in->t_size++;
}

static void box_delete_at(fill_box *in, int pos)
{
    for (int i = pos; i < in->t_size - 1; i++)
        in->text[i] = in->text[i + 1];
    in->t_size--;
    in->text[in->t_size] = '\0';
}

static int keypad_to_cell_char(int key)
{
    switch (key)
    {
    case ZERO:
        return '0';
    case ONE:
        return '1';
    case TWO:
        return '2';
    case THREE:
        return '3';
    case FOUR:
        return '4';
    case FIVE:
        return '5';
    case SIX:
        return '6';
    case SEVEN:
        return '7';
    case EIGHT:
        return '8';
    case NINE:
        return '9';
    case COMA:
        return '.';
    case MINUS:
        return '-';
    default:
        return 0;
    }
}

// In-place editing of a matrix literal rendered as a grid: the cursor sits
// inside a cell, arrow keys move between cells, digits replace / extend the
// current cell, BACK removes a character or jumps to the previous cell.
// Returns 1 when the key was consumed inside the matrix (the caller must
// skip its normal handling). Returns 0 when the cursor is not inside a
// matrix literal, or when the key should fall through after the cursor has
// been moved out of it.
int matrix_cell_edit(fill_box *in, int key, bool text_char)
{
    if (!in || in->t_size <= 0)
        return 0;
    matrix_lit L;
    int found = 0;
    int i = 0;
    while (i < in->t_size && !found)
    {
        if (in->text[i] == '[')
        {
            if (matrix_parse_literal(in->text, in->t_size, i, &L) > 0)
            {
                if (in->curso_pos >= L.start && in->curso_pos <= L.start + L.span)
                {
                    found = 1;
                    break;
                }
                i = L.start + L.span;
                continue;
            }
        }
        i++;
    }
    if (!found)
        return 0;

    if (key == BACK)
    {
        if (in->curso_pos == L.start)
        {
            return 1;
        }
        if (in->curso_pos == L.start + L.span || in->curso_pos == L.start + L.span - 1)
        {
            for (int k = 0; k < L.span; k++)
                box_delete_at(in, L.start);
            in->curso_pos = L.start;
            return 1;
        }
    }

    if (in->curso_pos == L.start || in->curso_pos == L.start + L.span
        || in->curso_pos == L.start + L.span - 1)
        return 0;

    int r = 0, c = 0, intra = 0;
    for (r = 0; r < L.rows; r++)
    {
        for (c = 0; c < L.cols; c++)
        {
            int cs = L.cell_start[r * L.cols + c];
            int cl = L.cell_len[r * L.cols + c];
            if (in->curso_pos >= cs && in->curso_pos <= cs + cl)
            {
                intra = in->curso_pos - cs;
                break;
            }
        }
        if (c < L.cols)
            break;
    }
    if (r >= L.rows)
    {
        int best = -1, bestdist = 1 << 30;
        for (int rr = 0; rr < L.rows; rr++)
            for (int cc = 0; cc < L.cols; cc++)
            {
                int idx = rr * L.cols + cc;
                int cs = L.cell_start[idx];
                int cl = L.cell_len[idx];
                if (cs >= in->curso_pos && cs - in->curso_pos < bestdist)
                {
                    bestdist = cs - in->curso_pos;
                    best = idx;
                    intra = 0;
                }
                else if (cs + cl <= in->curso_pos && in->curso_pos - (cs + cl) < bestdist)
                {
                    bestdist = in->curso_pos - (cs + cl);
                    best = idx;
                    intra = cl;
                }
            }
        if (best < 0)
            return 0;
        r = best / L.cols;
        c = best % L.cols;
    }
    if (intra < 0)
        intra = 0;
    int idx = r * L.cols + c;
    int cs = L.cell_start[idx];
    int cl = L.cell_len[idx];
    if (intra > cl)
        intra = cl;

    if (key == LEFT || key == UP || key == RIGHT || key == DOWN)
    {
        if (key == LEFT)
        {
            if (intra > 0)
                intra--;
            else if (c > 0)
            {
                c--;
                intra = L.cell_len[r * L.cols + c];
            }
            else if (r > 0)
            {
                r--;
                c = L.cols - 1;
                intra = L.cell_len[r * L.cols + c];
            }
            else
            {
                in->curso_pos = L.start;
                return 1;
            }
        }
        else if (key == RIGHT)
        {
            if (intra < cl)
                intra++;
            else if (c < L.cols - 1)
            {
                c++;
                intra = 0;
            }
            else if (r < L.rows - 1)
            {
                r++;
                c = 0;
                intra = 0;
            }
            else
            {
                in->curso_pos = L.start + L.span;
                return 1;
            }
        }
        else if (key == UP)
        {
            if (r > 0)
            {
                r--;
                intra = 0;
            }
            else
            {
                in->curso_pos = L.start;
                return 1;
            }
        }
        else
        {
            if (r < L.rows - 1)
            {
                r++;
                intra = 0;
            }
            else
            {
                in->curso_pos = L.start + L.span;
                return 1;
            }
        }
        in->curso_pos = L.cell_start[r * L.cols + c] + intra;
        return 1;
    }

    if (key == BACK)
    {
        if (intra > 0)
        {
            box_delete_at(in, cs + intra - 1);
            in->curso_pos = cs + intra - 1;
        }
        else if (c > 0)
        {
            c--;
            in->curso_pos = L.cell_start[r * L.cols + c] + L.cell_len[r * L.cols + c];
        }
        else if (r > 0)
        {
            r--;
            in->curso_pos = L.cell_start[r * L.cols + c] + L.cell_len[r * L.cols + c];
        }
        else
        {
            return 1;
        }
        return 1;
    }

    char ch = text_char ? (char)key : (char)keypad_to_cell_char(key);
    if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-')
    {
        if (intra == 0 && cl > 0)
        {
            for (int k = 0; k < cl; k++)
                box_delete_at(in, cs);
            box_insert_at(in, cs, ch);
            in->curso_pos = cs + 1;
        }
        else
        {
            box_insert_at(in, cs + intra, ch);
            in->curso_pos = cs + intra + 1;
        }
        return 1;
    }

    if (text_char)
    {
        in->curso_pos = L.start + L.span;
        return 0;
    }
    in->curso_pos = L.start + L.span;
    return 0;
}

pontentiometer *create_potentiometer(int x, int y, int h, int w, int border, char *name, int grad, bool transparent_back)
{
    pontentiometer *out = (pontentiometer *)malloc(sizeof(pontentiometer));
    out->transparent_back = transparent_back;
    out->x = x;
    out->y = y;
    out->h = h;
    out->w = w;
    out->grad = grad;
    out->val = 1;
    out->name = name;
    out->border = border;
    return out;
}

void increment_potentiometer(pontentiometer *p)
{
    p->val = min(p->val + 1, p->grad);
}

void decrement_potentiometer(pontentiometer *p)
{
    p->val = max(p->val - 1, 0);
}

void display_text_box(text_box *in, int shift_y, int shift_text, bool is_selected)

{
    if (in == nullptr)
        return;

    if (!is_selected)
        fill_rect(in->y + 1 + shift_y, in->x + 1, in->h - 2, in->w - 2, in->col);
    else
        fill_rect(in->y + shift_y, in->x, in->h, in->w, 0xfff0);
    if (in->allign == 'c')
    {
        draw_char(in->y + shift_text + shift_y + in->h - 10, (in->w - utf8_glyph_count(in->text, in->t_size) * in->display_text_size * 5) / 2 + in->x, in->text, 0x0000, 0xFF, in->display_text_size);
    }
    else if (in->allign == 'r')
    {
        draw_char(in->y + shift_text + shift_y + in->h - 10, in->x + 4, in->text, 0x0000, 0xFF, in->display_text_size);
    }

    fill_rect(in->y + shift_y, in->x + 2, 1, in->w - 4, 0x0000);
    fill_rect(in->y + shift_y + 2, in->x, in->h - 4, 1, 0x0000);

    fill_rect(in->y + shift_y + in->h - 1, in->x + 2, 1, in->w - 4, 0x0000);
    fill_rect(in->y + shift_y + 2, in->x + in->w - 1, in->h - 4, 1, 0x0000);

    fill_rect(in->y + shift_y, in->x, 2, 2, BACKGROUND_COLOR);
    fill_rect(in->y + shift_y, in->x + in->w - 2, 2, 2, BACKGROUND_COLOR);
    fill_rect(in->y + shift_y + in->h - 2, in->x, 2, 2, BACKGROUND_COLOR);
    fill_rect(in->y + shift_y + in->h - 2, in->x + in->w - 2, 2, 2, BACKGROUND_COLOR);

    fill_rect(in->y + shift_y + 1, in->x + 1, 1, 1, 0x0000); // bottom left

    fill_rect(in->y + shift_y + in->h - 2, in->x + 1, 1, 1, 0x0000);

    fill_rect(in->y + shift_y + 1, in->x + in->w - 2, 1, 1, 0x0000);

    fill_rect(in->y + shift_y + in->h - 2, in->x + in->w - 2, 1, 1, 0x0000);
}

void display_fill_box(fill_box *in, int shift_y, bool is_selected, int pos, char prefix,bool left)
{
    if (in == nullptr)
    {
        return;
    }
    uint16_t fill = is_selected ? 0xfff0 : in->color;
    if (is_selected)
        g_cursor_bg = fill;

    int clip_lo = in->x + 1;
    int clip_hi = in->x + in->w - 1;
    // 'u' (suites) a des libelles plus longs : u(n+1)=
    int text_y = in->x + (prefix == ' ' ? 0 : (prefix == 'u' ? 80 : 55));
    int scroll = 0;

    // Only the selected line can scroll; a rough character-width estimate is
    // enough to skip the extra pass for the common, short inputs.
    if (is_selected && in->t_size > 0 && in->t_size * 12 > in->w)
    {
        fill_rect(in->y + shift_y, in->x, in->h, in->w, fill);
        display_equation(in->text, in->t_size, in->y + shift_y, text_y, 2, in->curso_pos, left, 0, clip_lo, clip_hi);
        int cursor_y;
        if (display_get_cursor(&cursor_y))
        {
            const int margin = 6;
            if (cursor_y > clip_hi - margin)
                scroll = cursor_y - (clip_hi - margin);
            else if (cursor_y < clip_lo + margin)
                scroll = cursor_y - (clip_lo + margin);
            if (!left && scroll < 0)
                scroll = 0;
        }
    }

    fill_rect(in->y + shift_y, in->x, in->h, in->w, fill);

    display_equation(in->text, in->t_size, in->y + shift_y, text_y, 2, is_selected ? in->curso_pos : -10, left, scroll, clip_lo, clip_hi);

    if (prefix == 'f')
    {
        char t[6];
        t[0] = (pos + 5) % 26 + 'a';
        t[1] = '(';
        t[2] = 'x';
        t[3] = ')';
        t[4] = '=';
        t[5] = '\0';
        draw_char(in->y + shift_y + 25, in->x, t, 0X0000, 0X0000, 2);
    }
    if (prefix == 'r' || prefix == 'z')
    {
        // Grapheur : courbe polaire r(t)= ou parametrique z(t)=x(t)+i*y(t).
        char t[6] = {prefix, '(', 't', ')', '=', '\0'};
        draw_char(in->y + shift_y + 25, in->x, t, 0X0000, 0X0000, 2);
    }
    if (prefix == 'u')
    {
        // Suites : deux lignes par suite. Ligne paire = formule, ligne impaire
        // = premier terme. u(n+1)= si la formule utilise U (terme precedent).
        char lab[12];
        char letter = 'u' + (pos / 2) % 6;
        if (pos % 2 == 0)
        {
            bool rec = false;
            for (int i = 0; i < in->t_size; i++)
                if (in->text[i] == 'U')
                    rec = true;
            if (!rec && in->text &&
                (strstr(in->text, "arith") || strstr(in->text, "geom") || strstr(in->text, "arigeo")))
                rec = true;
            snprintf(lab, sizeof(lab), rec ? "%c(n+1)=" : "%c(n)=", letter);
        }
        else
            snprintf(lab, sizeof(lab), "%c(0)=", letter);
        draw_char(in->y + shift_y + 25, in->x, lab, 0X0000, 0X0000, 2);
    }
}


// Start index of the identifier when text[lparen] opens a structured call
// (binom/perm/pgcd/ppcm/quot/reste, the recurrence blocks arith/geom/arigeo/
// arithn and exp/min/max/ent/signe/racine/alea/randint), or -1 for anything
// else. These calls are inserted as a whole by the menus: their name,
// parentheses and separator cannot be edited alone. *arity = 0 to 3
// arguments (alea() takes none: its parentheses stay empty; somme, produit
// and integrale take 3).
static int call_name_lookup(const char *text, int lparen, int *arity)
{
    int start = binom_name_start(text, lparen);
    if (start >= 0)
    {
        if (arity)
            *arity = 2;
        return start;
    }
    static const struct
    {
        const char *name;
        int len;
        int arity;
    } names[] = {{"pgcd", 4, 2}, {"ppcm", 4, 2}, {"quot", 4, 2}, {"reste", 5, 2},
                 {"arith", 5, 1}, {"geom", 4, 1}, {"arigeo", 6, 2}, {"arithn", 6, 2},
                 {"exp", 3, 1}, {"min", 3, 2}, {"max", 3, 2}, {"ent", 3, 1},
                 {"signe", 5, 1}, {"racine", 6, 2}, {"alea", 4, 0}, {"randint", 7, 2},
                 {"somme", 5, 3}, {"produit", 7, 3}, {"derivee", 7, 2}, {"integrale", 9, 3},
                 {"developper", 10, 1}, {"factoriser", 10, 1}, {"deriver", 7, 1},
                 {"primitive", 9, 1}, {"resoudre", 8, 1}};
    for (int k = 0; k < (int)(sizeof(names) / sizeof(names[0])); k++)
    {
        int len = names[k].len;
        if (lparen < len || strncmp(text + lparen - len, names[k].name, len) != 0)
            continue;
        if (lparen - len == 0 || !(text[lparen - len - 1] >= 'a' && text[lparen - len - 1] <= 'z'))
        {
            if (arity)
                *arity = names[k].arity;
            return lparen - len;
        }
    }
    return -1;
}

static int call_name_start(const char *text, int lparen)
{
    return call_name_lookup(text, lparen, nullptr);
}

// Same, but only for calls with several arguments (',' separators): the comma
// key types the separator there and a plain decimal point elsewhere.
static int two_arg_name_start(const char *text, int lparen)
{
    int arity = 0;
    int start = call_name_lookup(text, lparen, &arity);
    return (start >= 0 && arity >= 2) ? start : -1;
}

bool cursor_in_calculus_call(const char *text, int cursor)
{
    if (!text)
        return false;
    static const char *names[4] = {"somme", "produit", "derivee", "integrale"};
    int depth = 0;
    for (int i = cursor - 1; i >= 0; i--)
    {
        char c = text[i];
        if (c == ')')
            depth++;
        else if (c == '(')
        {
            if (depth > 0)
            {
                depth--;
                continue;
            }
            for (int k = 0; k < 4; k++)
            {
                int len = (int)strlen(names[k]);
                if (i >= len && strncmp(text + i - len, names[k], len) == 0)
                    return true;
            }
            // Parenthese ordinaire : on continue vers un appel englobant.
        }
    }
    return false;
}

// Index of the '(' matching the ')' at `rparen`, or -1.
static int matching_open(const char *text, int rparen)
{
    int depth = 0;
    for (int i = rparen; i >= 0; i--)
    {
        if (text[i] == ')')
            depth++;
        else if (text[i] == '(')
        {
            depth--;
            if (depth == 0)
                return i;
        }
    }
    return -1;
}

// Index of the ')' matching the '(' at `lparen`, or -1.
static int matching_close(const char *text, int lparen)
{
    int depth = 0;
    for (int i = lparen; text[i]; i++)
    {
        if (text[i] == '(')
            depth++;
        else if (text[i] == ')')
        {
            depth--;
            if (depth == 0)
                return i;
        }
    }
    return -1;
}

// Start index of the identifier when text[lparen] opens a binom()/perm()
// call, or -1 when it opens something else.
static int binom_name_start(const char *text, int lparen)
{
    if (!text || lparen < 1 || text[lparen] != '(')
        return -1;
    if (lparen >= 5 && strncmp(text + lparen - 5, "binom", 5) == 0 &&
        ((lparen - 5 == 0) || !(text[lparen - 6] >= 'a' && text[lparen - 6] <= 'z')))
        return lparen - 5;
    if (lparen >= 4 && strncmp(text + lparen - 4, "perm", 4) == 0 &&
        ((lparen - 4 == 0) || !(text[lparen - 5] >= 'a' && text[lparen - 5] <= 'z')))
        return lparen - 4;
    return -1;
}

// True when text[i] is the '(' of a binom(...) or perm(...) call.
static bool is_binom_open(const char *text, int i)
{
    return binom_name_start(text, i) >= 0;
}

// '(' of the innermost binom()/perm() call containing `cursor`, or -1 when
// the cursor is not inside such a call.
static int enclosing_binom_open(const char *text, int cursor)
{
    int depth = 0;
    for (int i = cursor - 1; i >= 0; i--)
    {
        char c = text[i];
        if (c == ')')
            depth++;
        else if (c == '(')
        {
            if (depth > 0)
                depth--;
            else
                return is_binom_open(text, i) ? i : -1;
        }
    }
    return -1;
}

static bool is_digit_key(int event)
{
    switch (event)
    {
    case ZERO:
    case ONE:
    case TWO:
    case THREE:
    case FOUR:
    case FIVE:
    case SIX:
    case SEVEN:
    case EIGHT:
    case NINE:
        return true;
    default:
        return false;
    }
}

// Opening '(' of the two-argument call (binom/perm/pgcd/ppcm/quot/reste) one
// of whose structural characters (name letter, '(', ',' or ')') sits just
// before the cursor, or -1 when the cursor is not right after such a character.
static int call_struct_char_before(const char *text, int cursor)
{
    if (!text || cursor <= 0)
        return -1;
    char prev = text[cursor - 1];
    if (prev == '(')
        return call_name_start(text, cursor - 1) >= 0 ? cursor - 1 : -1;
    if (prev == ')')
    {
        int op = matching_open(text, cursor - 1);
        return (op >= 0 && call_name_start(text, op) >= 0) ? op : -1;
    }
    if (prev == ',')
    {
        int depth = 0;
        int j = cursor - 2;
        for (; j >= 0; j--)
        {
            char c = text[j];
            if (c == ')')
                depth++;
            else if (c == '(')
            {
                if (depth == 0)
                    break;
                depth--;
            }
        }
        if (j >= 0 && text[j] == '(' && call_name_start(text, j) >= 0)
            return j;
        return -1;
    }
    if (prev >= 'a' && prev <= 'z')
    {
        int k = cursor;
        while (text[k] >= 'a' && text[k] <= 'z')
            k++;
        if (text[k] == '(')
        {
            int s = call_name_start(text, k);
            if (s >= 0 && s <= cursor - 1)
                return k;
        }
    }
    return -1;
}

// BACK pressed right after a structural character of a two-argument call
// (name, parentheses, separator). While the call still holds an argument,
// nothing is deleted: removing one of these characters would leave a broken
// call. Once both arguments are empty, BACK removes the whole call.
// Returns 0 when the character before the cursor is not part of a call (normal
// deletion), 1 when the whole call was removed (the caller must drop its tail
// snapshot), 2 when the deletion was refused.
static int back_on_call(fill_box *in)
{
    if (in == NULL || in->text == NULL || in->curso_pos <= 0 || in->t_size <= 0)
        return 0;
    int lparen = call_struct_char_before(in->text, in->curso_pos);
    if (lparen < 0)
        return 0;
    int close = matching_close(in->text, lparen);
    int end = (close >= 0) ? close : in->t_size - 1;
    for (int k = lparen + 1; k < end; k++)
        if (in->text[k] != ',' && in->text[k] != ' ')
            return 2;
    int start = call_name_start(in->text, lparen);
    int after = in->t_size - end - 1;
    memmove(in->text + start, in->text + end + 1, after);
    in->t_size = start + after;
    in->curso_pos = start;
    in->text[in->t_size] = '\0';
    return 1;
}

// True when the cursor sits inside the name of a structured call (after its
// first letter, up to the '('): typing there would corrupt the name.
static bool cursor_in_call_name(const char *text, int cursor)
{
    if (!text || cursor <= 0)
        return false;
    int k = cursor;
    while (text[k] >= 'a' && text[k] <= 'z')
        k++;
    if (text[k] != '(')
        return false;
    int s = call_name_start(text, k);
    return s >= 0 && s < cursor;
}

// True when the cursor is inside the parentheses of alea(): that call takes no
// argument, so nothing may be typed there.
static bool cursor_in_noarg_call(const char *text, int cursor)
{
    if (!text)
        return false;
    int depth = 0;
    for (int i = cursor - 1; i >= 0; i--)
    {
        char c = text[i];
        if (c == ')')
            depth++;
        else if (c == '(')
        {
            if (depth > 0)
            {
                depth--;
                continue;
            }
            int arity = -1;
            int s = call_name_lookup(text, i, &arity);
            return s >= 0 && arity == 0;
        }
    }
    return false;
}

void update_fill_box(fill_box *in, int event, bool snd)
{
    if (in == NULL || in->text == NULL || in->t_size < 0)
        return;
    if (in->curso_pos < 0)
        in->curso_pos = 0;
    if (in->curso_pos > in->t_size)
        in->curso_pos = in->t_size;
    // Inside the argument list of a binom()/perm() call only digits may be
    // typed (the separator is already part of the template). Deletion and
    // stepping over the existing separator stay available; a nested function,
    // operator or second-function token is refused.
    if (enclosing_binom_open(in->text, in->curso_pos) >= 0)
    {
        bool allowed = (event == BACK) || (!snd && is_digit_key(event));
        if (event == COMA)
            allowed = !snd && in->curso_pos < in->t_size &&
                      in->text[in->curso_pos] == ',';
        if (!allowed)
            return;
    }
    // Never type inside the name of a block/call (arith, pgcd, ...).
    if (event != BACK && (cursor_in_call_name(in->text, in->curso_pos) ||
                          cursor_in_noarg_call(in->text, in->curso_pos) ||
                          ans_word_around(in->text, in->t_size, in->curso_pos, true) >= 0))
        return;
    // Effacer juste apres (ou dans) "ans" retire le mot entier.
    if (event == BACK)
    {
        int as = ans_word_around(in->text, in->t_size, in->curso_pos, false);
        if (as >= 0)
        {
            memmove(in->text + as, in->text + as + 3, in->t_size - as - 3);
            in->t_size -= 3;
            in->curso_pos = as;
            in->text[in->t_size] = '\0';
            return;
        }
    }
    // Snapshot of the text after the cursor. It is restored at the end of the
    // function, which shifts it by the number of characters inserted/deleted.
    char tail[1000];
    int tail_len = in->t_size - in->curso_pos;
    // Templates like "c()" leave the caret inside the parentheses: they move
    // it past the ')' (so the tail is restored after it) and step back here.
    int post_back = 0;
    if (tail_len > 0)
        memcpy(tail, in->text + in->curso_pos, tail_len);
if (event >= MATRIX_2X2 && event <= FUNC_SOLVE)    {
        const char *tmpl = NULL;
        int cursor_back = 0;
        bool is_matrix_tmpl = false;
        switch (event)
        {
        case MATRIX_2X2:
            tmpl = "[[0,0],[0,0]]";
            is_matrix_tmpl = true;
            break;
        case MATRIX_3X3:
            tmpl = "[[0,0,0],[0,0,0],[0,0,0]]";
            is_matrix_tmpl = true;
            break;
        case MATRIX_4X4:
            tmpl = "[[0,0,0,0],[0,0,0,0],[0,0,0,0],[0,0,0,0]]";
            is_matrix_tmpl = true;
            break;
        case MATRIX_2X3:
            tmpl = "[[0,0,0],[0,0,0]]";
            is_matrix_tmpl = true;
            break;
        case MATRIX_3X2:
            tmpl = "[[0,0],[0,0],[0,0]]";
            is_matrix_tmpl = true;
            break;
        case MATRIX_CUSTOM:
            tmpl = g_custom_matrix;
            is_matrix_tmpl = true;
            break;
        case FUNC_DET:
            tmpl = "det()";
            cursor_back = 1;
            break;
        case FUNC_INV:
            tmpl = "inv()";
            cursor_back = 1;
            break;
        case FUNC_TRANS:
            tmpl = "trans()";
            cursor_back = 1;
            break;
        case FUNC_BINOM:
            tmpl = "binom(,)";
            cursor_back = 2;
            break;
        case FUNC_PERM:
            tmpl = "perm(,)";
            cursor_back = 2;
            break;
        case FUNC_PGCD:
            tmpl = "pgcd(,)";
            cursor_back = 2;
            break;
        case FUNC_PPCM:
            tmpl = "ppcm(,)";
            cursor_back = 2;
            break;
        case FUNC_QUOT:
            tmpl = "quot(,)";
            cursor_back = 2;
            break;
        case FUNC_RESTE:
            tmpl = "reste(,)";
            cursor_back = 2;
            break;
        case FUNC_ABS:
            tmpl = "abs()";
            cursor_back = 1;
            break;
        case FUNC_ARRONDI:
            tmpl = "arrondi()";
            cursor_back = 1;
            break;
        case SEQ_ARITH:
            tmpl = "arith()";
            cursor_back = 1;
            break;
        case SEQ_GEOM:
            tmpl = "geom()";
            cursor_back = 1;
            break;
        case SEQ_ARIGEO:
            tmpl = "arigeo(,)";
            cursor_back = 2;
            break;
        case SEQ_ARITHN:
            tmpl = "arithn(,)";
            cursor_back = 2;
            break;
        case FUNC_EXP:
            tmpl = "exp()";
            cursor_back = 1;
            break;
        case FUNC_MIN:
            tmpl = "min(,)";
            cursor_back = 2;
            break;
        case FUNC_MAX:
            tmpl = "max(,)";
            cursor_back = 2;
            break;
        case FUNC_FLOOR:
            tmpl = "ent()";
            cursor_back = 1;
            break;
        case FUNC_SIGN:
            tmpl = "signe()";
            cursor_back = 1;
            break;
        case FUNC_ROOT:
            tmpl = "racine(,)";
            cursor_back = 2;
            break;
        case FUNC_RAND:
            tmpl = "alea()";
            cursor_back = 1;
            break;
        case FUNC_RANDINT:
            tmpl = "randint(,)";
            cursor_back = 2;
            break;
        case CST_G:      tmpl = "9.80665"; break;
        case CST_C:      tmpl = "299792458"; break;
        case CST_GRAV:   tmpl = "6.6743*10^(-11)"; break;
        case CST_PLANCK: tmpl = "6.62607015*10^(-34)"; break;
        case CST_QE:     tmpl = "1.602176634*10^(-19)"; break;
        case CST_ME:     tmpl = "9.1093837*10^(-31)"; break;
        case CST_NA:     tmpl = "6.02214076*10^(23)"; break;
        case CST_R:      tmpl = "8.314462618"; break;
        case FUNC_SUM:
            tmpl = "somme(,,)";
            cursor_back = 3;
            break;
        case FUNC_PROD:
            tmpl = "produit(,,)";
            cursor_back = 3;
            break;
        case FUNC_DERIV:
            tmpl = "derivee(,)";
            cursor_back = 2;
            break;
        case FUNC_INTEG:
            tmpl = "integrale(,,)";
            cursor_back = 3;
            break;
        case FUNC_EXPAND:
            tmpl = "developper()";
            cursor_back = 1;
            break;
        case FUNC_FACTOR:
            tmpl = "factoriser()";
            cursor_back = 1;
            break;
        case FUNC_DERIVE:
            tmpl = "deriver()";
            cursor_back = 1;
            break;
        case FUNC_PRIMITIVE:
            tmpl = "primitive()";
            cursor_back = 1;
            break;
        case FUNC_SOLVE:
            tmpl = "resoudre()";
            cursor_back = 1;
            break;
        default:
            break;
        }
        if (tmpl)
        {
            int slen = 0;
            while (tmpl[slen])
                slen++;
            if (is_matrix_tmpl)
                cursor_back = slen - 2;
            if (in->curso_pos + slen + tail_len < 1000)
            {
                memmove(in->text + in->curso_pos + slen, in->text + in->curso_pos, tail_len);
                memcpy(in->text + in->curso_pos, tmpl, slen);
                in->t_size += slen;
                in->curso_pos += slen - cursor_back;
                tail_len = 0; // already shifted in place
                in->text[in->t_size] = '\0';
            }
        }
    }
    // No room left for this key's insertion; BACK must still be processed.
    int room = (event == LOG) ? 5 : 3;
    if (event != BACK && in->t_size + room > 999)
        return;
    if (snd)
    {
        switch (event)
        {
        case ZERO:
            in->text[in->curso_pos] = 'U';
            in->curso_pos++;
            in->t_size++;
            break;
        case ONE:
            in->text[in->curso_pos] = 'P';
            in->curso_pos++;
            in->t_size++;
            break;
        case TWO:
            in->text[in->curso_pos] = 'Q';
            in->curso_pos++;
            in->t_size++;
            break;
        case THREE:
            in->text[in->curso_pos] = 'R';
            in->curso_pos++;
            in->t_size++;
            break;
        case EQUAL:
            in->text[in->curso_pos] = '=';
            in->curso_pos++;
            in->t_size++;
            break;
        case FOUR:
            in->text[in->curso_pos] = 'K';
            in->curso_pos++;
            in->t_size++;
            break;
        case FIVE:
            in->text[in->curso_pos] = 'L';
            in->curso_pos++;
            in->t_size++;
            break;
        case SIX:
            in->text[in->curso_pos] = 'M';
            in->curso_pos++;
            in->t_size++;
            break;
        case SEVEN:
            in->text[in->curso_pos] = 'F';
            in->curso_pos++;
            in->t_size++;
            break;
        case EIGHT:
            in->text[in->curso_pos] = 'G';
            in->curso_pos++;
            in->t_size++;
            break;
        case NINE:
            in->text[in->curso_pos] = 'H';
            in->curso_pos++;
            in->t_size++;
            break;
        case PLUS:
            in->text[in->curso_pos] = 'S';
            in->curso_pos++;
            in->t_size++;
            break;
        case MINUS:
            in->text[in->curso_pos] = 'N';
            in->curso_pos++;
            in->t_size++;
            break;
        case TIMES:
            in->text[in->curso_pos] = 'T';
            in->curso_pos++;
            in->t_size++;
            break;
        case DIVIDE:
            in->text[in->curso_pos] = 'O';
            in->curso_pos++;
            in->t_size++;
            break;
        case OPENING_PARENTHESIS:
            in->text[in->curso_pos] = 'I';
            in->curso_pos++;
            in->t_size++;
            break;
        case CLOSING_PARENTHESIS:
            in->text[in->curso_pos] = 'J';
            in->curso_pos++;
            in->t_size++;
            break;
        case LN:
            in->text[in->curso_pos] = 'l';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->t_size += 3;
            in->curso_pos += 3;
            post_back = 1;
            break;
        case E:
            in->text[in->curso_pos] = 'e';
            in->curso_pos += 1;
            in->t_size++;
            break;
        case X:
            in->text[in->curso_pos] = 'X';
            in->curso_pos++;
            in->t_size++;
            break;
        case COMA:
            in->text[in->curso_pos] = 'V';
            in->curso_pos++;
            in->t_size++;
            break;
        case PI:
            in->text[in->curso_pos] = 'W';
            in->curso_pos++;
            in->t_size++;
            break;
        case COS:
            in->text[in->curso_pos] = 'A';
            in->curso_pos++;
            in->t_size++;
            break;
        case SIN:
            in->text[in->curso_pos] = 'B';
            in->curso_pos++;
            in->t_size++;
            break;
        case TAN:
            in->text[in->curso_pos] = 'C';
            in->curso_pos++;
            in->t_size++;
            break;
        case SQRT:
            in->text[in->curso_pos] = 'D';
            in->curso_pos++;
            in->t_size++;
            break;
        case POW:
            in->text[in->curso_pos] = 'E';
            in->curso_pos++;
            in->t_size++;
            break;
        case BACK:

            if (in->t_size > 0 && in->curso_pos > 0)
            {
                int call_r = back_on_call(in);
                if (call_r == 1)
                    tail_len = 0;
                else if (call_r == 2)
                {
                    // The call still holds arguments: BACK after its ')' or
                    // ',' steps into it, so the next BACK deletes an argument.
                    char prev = in->text[in->curso_pos - 1];
                    if (prev == ')' || prev == ',')
                        in->curso_pos--;
                    tail_len = 0;
                }
                else if (call_r == 0)
                {
                    in->t_size--;
                    in->curso_pos--;
                }
            }

            break;
        case FACT:

            in->text[in->curso_pos] = '!';
            in->curso_pos++;

            in->t_size++;
            break;
        case COSH:

            in->text[in->curso_pos] = 'f';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;

            in->t_size += 3;
            break;

        case SINH:

            in->text[in->curso_pos] = 'g';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;

            in->t_size += 3;
            break;

        case TANH:

            in->text[in->curso_pos] = 'h';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;

            in->t_size += 3;
            break;

        case ACOSH:

            in->text[in->curso_pos] = 'a';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;

            in->t_size += 3;
            break;

        case ASINH:

            in->text[in->curso_pos] = 'j';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;

            in->t_size += 3;
            break;

        case ATANH:

            in->text[in->curso_pos] = 'k';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;

            in->t_size += 3;
            break;

        case IMAGINARY:
            in->text[in->curso_pos] = 'i';
            in->curso_pos++;
            in->t_size++;
            break;
        case CONJ:
            in->text[in->curso_pos] = 'b';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case ARG:
            in->text[in->curso_pos] = 'd';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case MOD:
            in->text[in->curso_pos] = 'm';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case ACOS:
            in->text[in->curso_pos] = 'u';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case ASIN:
            in->text[in->curso_pos] = 'v';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case ATAN:
            in->text[in->curso_pos] = 'w';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        default:
            break;
        }
    }
    else
    {
        switch (event)
        {
        case ZERO:
            in->text[in->curso_pos] = '0';
            in->curso_pos++;
            in->t_size++;
            break;
        case ONE:
            in->text[in->curso_pos] = '1';
            in->curso_pos++;
            in->t_size++;
            break;
        case TWO:
            in->text[in->curso_pos] = '2';
            in->curso_pos++;
            in->t_size++;
            break;
        case THREE:
            in->text[in->curso_pos] = '3';
            in->curso_pos++;
            in->t_size++;
            break;
        case EQUAL:
            in->text[in->curso_pos] = '=';
            in->curso_pos++;
            in->t_size++;
            break;
        case FOUR:
            in->text[in->curso_pos] = '4';
            in->curso_pos++;
            in->t_size++;
            break;
        case FIVE:
            in->text[in->curso_pos] = '5';
            in->curso_pos++;
            in->t_size++;
            break;
        case SIX:
            in->text[in->curso_pos] = '6';
            in->curso_pos++;
            in->t_size++;
            break;
        case SEVEN:
            in->text[in->curso_pos] = '7';
            in->curso_pos++;
            in->t_size++;
            break;
        case EIGHT:
            in->text[in->curso_pos] = '8';
            in->curso_pos++;
            in->t_size++;
            break;
        case NINE:
            in->text[in->curso_pos] = '9';
            in->curso_pos++;
            in->t_size++;
            break;
        case PLUS:
            in->text[in->curso_pos] = '+';
            in->curso_pos++;
            in->t_size++;
            break;
        case MINUS:
            in->text[in->curso_pos] = '-';
            in->curso_pos++;
            in->t_size++;
            break;
        case TIMES:
            in->text[in->curso_pos] = '*';
            in->curso_pos++;
            in->t_size++;
            break;
        case DIVIDE:
            in->text[in->curso_pos] = '/';
            in->curso_pos++;
            in->t_size++;
            break;
        case OPENING_PARENTHESIS:
            in->text[in->curso_pos] = '(';
            in->curso_pos++;
            in->t_size++;
            break;
        case CLOSING_PARENTHESIS:
            in->text[in->curso_pos] = ')';
            in->curso_pos++;
            in->t_size++;
            break;
        case LN:
            in->text[in->curso_pos] = 'l';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->t_size += 3;
            in->curso_pos += 3;
            post_back = 1;
            break;
        case LOG:
            in->text[in->curso_pos] = 'l';
            in->text[in->curso_pos + 1] = 'o';
            in->text[in->curso_pos + 2] = 'g';
            in->text[in->curso_pos + 3] = '(';
            in->text[in->curso_pos + 4] = ')';
            in->t_size += 5;
            in->curso_pos += 5;
            post_back = 1;
            break;
        case ANS:
            in->text[in->curso_pos] = 'a';
            in->text[in->curso_pos + 1] = 'n';
            in->text[in->curso_pos + 2] = 's';
            in->t_size += 3;
            in->curso_pos += 3;
            break;
        case E:
            in->text[in->curso_pos] = 'e';
            in->curso_pos += 1;
            in->t_size++;
            break;
        case X:
            in->text[in->curso_pos] = 'X';
            in->curso_pos++;
            in->t_size++;
            break;
        case COMA:
        {
            // Inside a two-argument call the comma separates the arguments;
            // everywhere else it is the decimal separator.
            bool arg_comma = false;
            int depth = 0;
            int j = in->curso_pos - 1;
            for (; j >= 0; j--)
            {
                char c = in->text[j];
                if (c == ')')
                    depth++;
                else if (c == '(')
                {
                    if (depth == 0)
                        break;
                    depth--;
                }
            }
            if (j >= 1 && in->text[j] == '(' && two_arg_name_start(in->text, j) >= 0)
                arg_comma = true;
            if (arg_comma && in->curso_pos < in->t_size && in->text[in->curso_pos] == ',')
            {
                // The separator is already there (the call template inserts
                // it): just step over it. The text is left untouched, so the
                // tail must not be restored.
                in->curso_pos++;
                tail_len = 0;
                break;
            }
            in->text[in->curso_pos] = arg_comma ? ',' : '.';
            in->curso_pos++;
            in->t_size++;
            break;
        }
        case PI:
            in->text[in->curso_pos] = 'p';
            in->curso_pos++;
            in->t_size++;
            break;
        case COS:
            in->text[in->curso_pos] = 'c';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case SIN:
            in->text[in->curso_pos] = 's';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case TAN:
            in->text[in->curso_pos] = 't';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case SQRT:
            in->text[in->curso_pos] = 'r';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case POW:
            in->text[in->curso_pos] = '^';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case BACK:
            if (in->t_size > 0 && in->curso_pos > 0)
            {
                int call_r = back_on_call(in);
                if (call_r == 1)
                    tail_len = 0;
                else if (call_r == 2)
                {
                    // The call still holds arguments: BACK after its ')' or
                    // ',' steps into it, so the next BACK deletes an argument.
                    char prev = in->text[in->curso_pos - 1];
                    if (prev == ')' || prev == ',')
                        in->curso_pos--;
                    tail_len = 0;
                }
                else if (call_r == 0)
                {
                    // The argument separator of a binom()/perm() call is
                    // protected as long as the rest of the call is there.
                    in->t_size--;
                    in->curso_pos--;
                }
            }
            break;
        case FACT:

            in->text[in->curso_pos] = '!';
            in->curso_pos++;

            in->t_size++;
            break;
        case COSH:

            in->text[in->curso_pos] = 'f';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;

            in->t_size += 3;
            break;

        case SINH:

            in->text[in->curso_pos] = 'g';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;

            in->t_size += 3;
            break;

        case TANH:

            in->text[in->curso_pos] = 'h';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;

            in->t_size += 3;
            break;

        case ACOSH:

            in->text[in->curso_pos] = 'a';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;

            in->t_size += 3;
            break;

        case ASINH:

            in->text[in->curso_pos] = 'j';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;

            in->t_size += 3;
            break;

        case ATANH:

            in->text[in->curso_pos] = 'k';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;

            in->t_size += 3;
            break;

        case IMAGINARY:
            in->text[in->curso_pos] = 'i';
            in->curso_pos++;
            in->t_size++;
            break;
        case CONJ:
            in->text[in->curso_pos] = 'b';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case ARG:
            in->text[in->curso_pos] = 'd';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case MOD:
            in->text[in->curso_pos] = 'm';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case ACOS:
            in->text[in->curso_pos] = 'u';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case ASIN:
            in->text[in->curso_pos] = 'v';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        case ATAN:
            in->text[in->curso_pos] = 'w';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 3;
            post_back = 1;
            in->t_size += 3;
            break;
        default:
            break;
        }
    }
    if (tail_len > 0)
        memcpy(in->text + in->curso_pos, tail, tail_len);
    in->curso_pos -= post_back;
    in->text[in->t_size] = '\0';
    if (in->curso_pos > in->t_size)
        in->curso_pos = in->t_size;
}

void draw_image(int x, int y, int h, int w, uint16_t *img, uint16_t bck)
{
    if (img == NULL)
        return;

    for (int i = 0; i < w; i++)
    {
        for (int j = 0; j < h; j++)
        {
            if (img[i * h + j] != bck)
                fill_rect(x + i, y + j, 1, 1, img[j * h + i]);
        }
    }
}

void display_potentiometer(pontentiometer *in, bool is_selected)

{
    if (in == nullptr)
        return;

    if (!in->transparent_back)
    {
        int col = 0;
        if (!is_selected)
        {
            col = FRONTGROUND_COLOR;
        }
        else
            col = FRONTGROUND_COLOR_BIS;
    }

    draw_char(in->x + in->h + 15, in->y, in->name, 0x0000, BACKGROUND_COLOR, 1);
    fill_rect(in->x, in->y, 1, in->w, 0X0000);
    fill_rect(in->x + in->h - 1, in->y, 1, in->w, 0X0000);
    fill_rect(in->x, in->y, in->h, 1, 0X0000);
    fill_rect(in->x, in->y + in->w - 1, in->h, 1, 0X0000);

    fill_rect(in->x + 1, max(in->y, in->y + in->w * (((float)in->val / in->grad)) - 1), in->h - 2, in->w - in->w * (((float)in->val / in->grad)) + 1, 0x632c);
    fill_rect(in->x + 1, in->y + 1, in->h - 2, in->w * (((float)in->val / in->grad)) - 2, 0xffc0);
}



#define CELL_TEXT_MAX 9
#define MATRIX_EDITOR_MAX 300
#define MATRIX_LITERAL_MAX 990

static char keypad_to_char(int key)
{
    switch (key)
    {
    case ZERO:
        return '0';
    case ONE:
        return '1';
    case TWO:
        return '2';
    case THREE:
        return '3';
    case FOUR:
        return '4';
    case FIVE:
        return '5';
    case SIX:
        return '6';
    case SEVEN:
        return '7';
    case EIGHT:
        return '8';
    case NINE:
        return '9';
    case COMA:
        return '.';
    case MINUS:
        return '-';
    default:
        return 0;
    }
}

// Full-screen form to pick the n*m size of a new matrix. Returns 1 on OK
// (with *out_rows/*out_cols set) or 0 when cancelled.
static int pick_matrix_size(int *out_rows, int *out_cols)
{
    int rows = 2, cols = 2;
    bool touched[2] = {false, false};
    int field = 0; // 0 = rows, 1 = cols
    while (true)
    {
        fill_screen(BACKGROUND_COLOR);
        draw_char(224, 46, (char *)"Taille de la matrice", 0x0000, BACKGROUND_COLOR, 2);
        draw_char(176, 20, (char *)"Lignes", 0x0000, BACKGROUND_COLOR, 2);
        draw_char(120, 20, (char *)"Colonnes", 0x0000, BACKGROUND_COLOR, 2);
        uint16_t c0 = field == 0 ? 0xffff : FRONTGROUND_COLOR;
        uint16_t c1 = field == 1 ? 0xffff : FRONTGROUND_COLOR;
        fill_rect(168, 120, 40, 56, c0);
        fill_rect(112, 120, 40, 56, c1);
        char buf[8];
        sprintf(buf, "%d", rows);
        draw_char(176, 133, buf, 0x0000, c0, 2);
        sprintf(buf, "%d", cols);
        draw_char(120, 133, buf, 0x0000, c1, 2);

        // Live preview of the resulting shape, same cell size for any n*m.
        int cell = 10, gap = 3;
        const int avail_h = 76, avail_w = 260;
        int th = rows * cell + (rows > 1 ? (rows - 1) * gap : 0);
        int tw = cols * cell + (cols > 1 ? (cols - 1) * gap : 0);
        while (cell > 2 && (th > avail_h || tw > avail_w))
        {
            cell--;
            th = rows * cell + (rows > 1 ? (rows - 1) * gap : 0);
            tw = cols * cell + (cols > 1 ? (cols - 1) * gap : 0);
        }
        while (gap > 1 && (th > avail_h || tw > avail_w))
        {
            gap--;
            th = rows * cell + (rows > 1 ? (rows - 1) * gap : 0);
            tw = cols * cell + (cols > 1 ? (cols - 1) * gap : 0);
        }
        int x0 = 8 + (avail_h - th) / 2;
        int y0 = 30 + (avail_w - tw) / 2;
        for (int r = 0; r < rows; r++)
            for (int c = 0; c < cols; c++)
            {
                int x = x0 + th - cell - r * (cell + gap);
                int y = y0 + c * (cell + gap);
                fill_rect(x, y, cell, cell, FRONTGROUND_COLOR);
                fill_rect(x, y, 1, cell, 0x0000);
                fill_rect(x + cell - 1, y, 1, cell, 0x0000);
                fill_rect(x, y, cell, 1, 0x0000);
                fill_rect(x, y + cell - 1, cell, 1, 0x0000);
            }

        draw_char(8, 10, (char *)"UP/DOWN: champ   OK: valider", 0x0000, BACKGROUND_COLOR, 1);

        flush_display();
        // The first digit typed replaces the default value; further digits
        // append (so "12" gives 12).
        int ch = scan_text_char();
        if (ch >= '0' && ch <= '9')
        {
            int d = ch - '0';
            int v = field == 0 ? rows : cols;
            v = touched[field] ? min(v * 10 + d, 99) : d;
            touched[field] = true;
            if (field == 0)
                rows = v;
            else
                cols = v;
            continue;
        }
        int key = scan_keypad();
        if (key == -1)
            continue;
        char c = keypad_to_char(key);
        if (c >= '0' && c <= '9')
        {
            int d = c - '0';
            int v = field == 0 ? rows : cols;
            v = touched[field] ? min(v * 10 + d, 99) : d;
            touched[field] = true;
            if (field == 0)
                rows = v;
            else
                cols = v;
        }
        else if (key == UP || key == DOWN || key == LEFT || key == RIGHT)
        {
            field = 1 - field;
        }
        else if (key == BACK)
        {
            return 0;
        }
        else if (key == OK || key == ENTER)
        {
            if (rows >= 1 && rows <= 99 && cols >= 1 && cols <= 99 && rows * cols <= MATRIX_EDITOR_MAX)
            {
                *out_rows = rows;
                *out_cols = cols;
                return 1;
            }
        }
    }
}

static void editor_cell_append(char *cells, int *clen, int idx, char c)
{
    if (clen[idx] >= CELL_TEXT_MAX - 1)
        return;
    char *cell = &cells[idx * CELL_TEXT_MAX];
    if (c == '.')
    {
        if (memchr(cell, '.', clen[idx]))
            return;
    }
    else if (c == '-')
    {
        if (clen[idx] > 0)
            return;
    }
    cell[clen[idx]] = c;
    cell[clen[idx] + 1] = '\0';
    clen[idx]++;
}

static bool editor_cell_has_digit(const char *t, int len)
{
    for (int i = 0; i < len; i++)
        if (t[i] >= '0' && t[i] <= '9')
            return true;
    return false;
}

// Append a parser-compatible number for one cell: invalid or empty cells
// become "0", ".5" becomes "0.5" and "-.5" becomes "-0.5".
static int editor_emit_cell(char *out, int pos, const char *t, int len)
{
    if (len == 0 || !editor_cell_has_digit(t, len))
    {
        out[pos++] = '0';
        return pos;
    }
    if (t[0] == '.')
    {
        out[pos++] = '0';
    }
    else if (t[0] == '-' && len >= 2 && t[1] == '.')
    {
        out[pos++] = '-';
        out[pos++] = '0';
        t += 1;
        len -= 1;
    }
    for (int k = 0; k < len; k++)
        out[pos++] = t[k];
    return pos;
}

// Full-screen grid editor: type each cell value, arrows navigate, OK moves to
// the next cell (and finishes on the last one), BACK deletes / goes back.
// Returns the [[...]] literal (malloc'd) or NULL when cancelled.
static char *grid_matrix_editor(int rows, int cols)
{
    const int header_h = 24, cell_h = 40, cell_w = 64;
    const int view_rows = (SCREEN_HEIGHT - header_h) / cell_h;
    const int view_cols = SCREEN_WIDTH / cell_w;
    int total = rows * cols;
    char *cells = (char *)malloc(sizeof(char) * total * CELL_TEXT_MAX);
    int *clen = (int *)malloc(sizeof(int) * total);
    for (int i = 0; i < total; i++)
    {
        cells[i * CELL_TEXT_MAX] = '\0';
        clen[i] = 0;
    }

    int sel_r = 0, sel_c = 0;
    int scroll_r = 0, scroll_c = 0;

    char title[24];
    sprintf(title, "Matrice %dx%d", rows, cols);

    while (true)
    {
        fill_screen(BACKGROUND_COLOR);
        draw_char(4, 4, title, 0x0000, BACKGROUND_COLOR, 2);
        for (int r = 0; r < view_rows; r++)
        {
            for (int c = 0; c < view_cols; c++)
            {
                int ar = scroll_r + r;
                int ac = scroll_c + c;
                int x0 = header_h + r * cell_h;
                int y0 = c * cell_w;
                bool sel = (ar == sel_r && ac == sel_c);
                uint16_t bg = sel ? 0xffff
                                  : (((ar % 2 == 0) == (ac % 2 == 0)) ? FRONTGROUND_COLOR : FRONTGROUND_COLOR_BIS);
                fill_rect(x0, y0, cell_h, cell_w, bg);
                if (ar < rows && ac < cols)
                {
                    int idx = ar * cols + ac;
                    char *txt = &cells[idx * CELL_TEXT_MAX];
                    int len = clen[idx];
                    int ty = y0 + (cell_w - len * 7) / 2;
                    draw_char(x0 + (cell_h - 8) / 2, ty, txt, 0x0000, bg, 1);
                }
                fill_rect(x0, y0, 1, cell_w, 0x0000);
                fill_rect(x0 + cell_h - 1, y0, 1, cell_w, 0x0000);
                fill_rect(x0, y0, cell_h, 1, 0x0000);
                fill_rect(x0, y0 + cell_w - 1, cell_h, 1, 0x0000);
                if (sel)
                {
                    fill_rect(x0, y0, 2, cell_w, 0x0000);
                    fill_rect(x0 + cell_h - 2, y0, 2, cell_w, 0x0000);
                    fill_rect(x0, y0, cell_h, 2, 0x0000);
                    fill_rect(x0, y0 + cell_w - 2, cell_h, 2, 0x0000);
                }
            }
        }
        draw_char(232, 10, (char *)"OK: cell. suiv.   BACK: effacer", 0x0000, BACKGROUND_COLOR, 1);

        flush_display();
        int ch = scan_text_char();
        if (ch >= 0)
        {
            if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-')
                editor_cell_append(cells, clen, sel_r * cols + sel_c, (char)ch);
            continue;
        }
        int key = scan_keypad();
        if (key == -1)
            continue;
        char c = keypad_to_char(key);
        if (c)
        {
            editor_cell_append(cells, clen, sel_r * cols + sel_c, c);
            continue;
        }
        switch (key)
        {
        case UP:
            if (sel_r > 0)
                sel_r--;
            break;
        case DOWN:
            if (sel_r < rows - 1)
                sel_r++;
            break;
        case LEFT:
            if (sel_c > 0)
                sel_c--;
            break;
        case RIGHT:
            if (sel_c < cols - 1)
                sel_c++;
            break;
        case BACK:
            if (clen[sel_r * cols + sel_c] > 0)
            {
                clen[sel_r * cols + sel_c]--;
                cells[(sel_r * cols + sel_c) * CELL_TEXT_MAX + clen[sel_r * cols + sel_c]] = '\0';
            }
            else if (sel_r == 0 && sel_c == 0)
            {
                free(cells);
                free(clen);
                return NULL;
            }
            else
            {
                sel_c--;
                if (sel_c < 0)
                {
                    sel_c = cols - 1;
                    sel_r--;
                }
            }
            break;
        case OK:
        case ENTER:
            if (sel_r == rows - 1 && sel_c == cols - 1)
            {
                int pos = 0;
                char *out = (char *)malloc(MATRIX_LITERAL_MAX);
                out[pos++] = '[';
                out[pos++] = '[';
                for (int r = 0; r < rows; r++)
                {
                    if (r > 0)
                    {
                        out[pos++] = ']';
                        out[pos++] = ',';
                        out[pos++] = '[';
                    }
                    for (int cc = 0; cc < cols; cc++)
                    {
                        if (cc > 0)
                            out[pos++] = ',';
                        int idx = r * cols + cc;
                        pos = editor_emit_cell(out, pos, &cells[idx * CELL_TEXT_MAX], clen[idx]);
                        if (pos >= MATRIX_LITERAL_MAX - 2)
                        {
                            free(out);
                            free(cells);
                            free(clen);
                            return NULL;
                        }
                    }
                }
                out[pos++] = ']';
                out[pos++] = ']';
                out[pos] = '\0';
                free(cells);
                free(clen);
                return out;
            }
            sel_c++;
            if (sel_c >= cols)
            {
                sel_c = 0;
                sel_r++;
            }
            break;
        default:
            break;
        }
        if (sel_r < scroll_r)
            scroll_r = sel_r;
        if (sel_r >= scroll_r + view_rows)
            scroll_r = sel_r - view_rows + 1;
        if (sel_c < scroll_c)
            scroll_c = sel_c;
        if (sel_c >= scroll_c + view_cols)
            scroll_c = sel_c - view_cols + 1;
    }
}


/* ===================== Menus des Outils ===================== */
#define MENU_VIS 4
#define MENU_ROW_H 30
#define MENU_BACK (-1)

static int key_digit(int k)
{
    switch (k)
    {
    case ONE: return 1;
    case TWO: return 2;
    case THREE: return 3;
    case FOUR: return 4;
    case FIVE: return 5;
    case SIX: return 6;
    case SEVEN: return 7;
    case EIGHT: return 8;
    case NINE: return 9;
    default: return 0;
    }
}

// Menu vertical generique : premier element en haut, defilement avec retour
// en boucle, titre + position, chiffres 1-9 = acces direct.
// Retourne l'indice choisi ou MENU_BACK. *sel memorise la selection.
static int menu_list(const char *title, const char **labels, int n, int *sel)
{
    text_box **items = (text_box **)malloc(sizeof(text_box *) * n);
    for (int i = 0; i < n; i++)
    {
        items[i] = create_text_box(40, 0, MENU_ROW_H, 240, 2, false);
        items[i]->allign = 'r';
        if (i < 9)
            snprintf(items[i]->text, 100, "%d. %s", i + 1, labels[i]);
        else
            snprintf(items[i]->text, 100, "%s", labels[i]);
        items[i]->t_size = (int)strlen(items[i]->text);
    }
    text_box *head = create_text_box(32, MENU_VIS * MENU_ROW_H, 20, 256, 0, false);
    head->col = 0x311f;
    head->allign = 'c';

    if (*sel < 0 || *sel >= n)
        *sel = 0;
    int top = (*sel >= MENU_VIS) ? *sel - MENU_VIS + 1 : 0;
    int result = MENU_BACK;
    bool done = false;

    while (!done)
    {
        snprintf(head->text, 100, "%s  %d/%d", title, *sel + 1, n);
        head->t_size = (int)strlen(head->text);
        display_text_box(head, 0, 0, false);

        fill_rect(0, 32, MENU_VIS * MENU_ROW_H, 256, 0x311f);
        for (int r = 0; r < MENU_VIS && top + r < n; r++)
        {
            text_box *it = items[top + r];
            it->y = (MENU_VIS - 1 - r) * MENU_ROW_H; // y croit vers le haut
            display_text_box(it, 0, 0, top + r == *sel);
        }
        flush_display();

        int k = scan_keypad();
        while (k == -1)
            k = scan_keypad();

        if (k == UP)
            *sel = (*sel - 1 + n) % n;
        else if (k == DOWN)
            *sel = (*sel + 1) % n;
        else if (k == BACK)
            done = true;
        else if (k == OK || k == ENTER)
        {
            result = *sel;
            done = true;
        }
        else
        {
            int d = key_digit(k);
            if (d >= 1 && d <= n)
            {
                *sel = d - 1;
                result = *sel;
                done = true;
            }
        }
        if (*sel < top)
            top = *sel;
        else if (*sel >= top + MENU_VIS)
            top = *sel - MENU_VIS + 1;
    }
    free_text_boxes(items, n);
    free_text_box(head);
    return result;
}

// Sous-menu simple : labels[i] -> codes[i], END_KEYS si BACK.
static int code_menu(const char *title, const char **labels, const int *codes, int n)
{
    int sel = 0;
    int r = menu_list(title, labels, n, &sel);
    return r == MENU_BACK ? END_KEYS : codes[r];
}

int menu_analysis()
{
    const char *labels[4] = {"Somme Σ", "Produit Π", "Dérivée en a", "Intégrale ∫"};
    const int codes[4] = {FUNC_SUM, FUNC_PROD, FUNC_DERIV, FUNC_INTEG};
    return code_menu("Analyse", labels, codes, 4);
}

/* Calcul formel : commandes en X (touche X), voir Core/Cas.cpp. */
static int menu_cas()
{
    const char *labels[5] = {"Developper", "Factoriser", "Deriver (en X)", "Primitive (en X)",
                             "Resoudre f(X)=0"};
    const int codes[5] = {FUNC_EXPAND, FUNC_FACTOR, FUNC_DERIVE, FUNC_PRIMITIVE, FUNC_SOLVE};
    return code_menu("Calcul formel", labels, codes, 5);
}

int menu_tools(bool with_analysis)
{
    static const char *labels[10] = {"Probabilites", "Matrices", "Arithmetique", "Hyperboliques",
                                     "Nombres decimaux", "Nombres complexes", "Fonctions",
                                     "Constantes physiques", "Analyse", "Calcul formel"};
    static int sel = 0; // reste sur la derniere rubrique utilisee
    int count = with_analysis ? 10 : 8;
    if (sel >= count)
        sel = 0;
    while (true)
    {
        int r = menu_list("Outils", labels, count, &sel);
        if (r == MENU_BACK)
            return END_KEYS;
        int code = END_KEYS;
        switch (r)
        {
        case 0: code = menu_proba(); break;
        case 1: code = menu_matrices(); break;
        case 2: code = menu_arithmetic(); break;
        case 3: code = menu_trigo(); break;
        case 4: code = menu_decimals(); break;
        case 5: code = menu_complex(); break;
        case 6: code = menu_functions(); break;
        case 7: code = menu_const_phy(); break;
        case 8: code = menu_analysis(); break;
        case 9: code = menu_cas(); break;
        }
        if (code != END_KEYS) // BACK dans le sous-menu : on reste dans Outils
            return code;
    }
}

int menu_proba()
{
    const char *labels[3] = {"Factorielle n!", "binom(n,k)", "perm(n,k)"};
    const int codes[3] = {FACT, FUNC_BINOM, FUNC_PERM};
    return code_menu("Probabilites", labels, codes, 3);
}

int menu_trigo()
{
    const char *labels[6] = {"cosinus hyperbolique", "sinus hyperbolique", "tangente hyperbolique",
                             "arccosinus hyperbolique", "arcsinus hyperbolique", "arctangente hyperbolique"};
    const int codes[6] = {COSH, SINH, TANH, ACOSH, ASINH, ATANH};
    return code_menu("Hyperboliques", labels, codes, 6);
}

int menu_arithmetic()
{
    const char *labels[6] = {"PGCD", "PPCM", "Quotient", "Reste", "Valeur absolue", "Arrondi"};
    const int codes[6] = {FUNC_PGCD, FUNC_PPCM, FUNC_QUOT, FUNC_RESTE, FUNC_ABS, FUNC_ARRONDI};
    return code_menu("Arithmetique", labels, codes, 6);
}

int menu_decimals()
{
    const char *labels[4] = {"Partie entiere ent(x)", "Signe signe(x)", "Aleatoire alea()",
                             "Entier alea randint(a,b)"};
    const int codes[4] = {FUNC_FLOOR, FUNC_SIGN, FUNC_RAND, FUNC_RANDINT};
    return code_menu("Decimaux", labels, codes, 4);
}

int menu_functions()
{
    const char *labels[4] = {"Exponentielle exp(x)", "Minimum min(a,b)", "Maximum max(a,b)",
                             "Racine n-ieme racine(x,n)"};
    const int codes[4] = {FUNC_EXP, FUNC_MIN, FUNC_MAX, FUNC_ROOT};
    return code_menu("Fonctions", labels, codes, 4);
}

int menu_complex()
{
    const char *labels[7] = {"Unite imaginaire i", "Conjugue", "Argument", "Module",
                             "Arccos", "Arcsin", "Arctan"};
    const int codes[7] = {IMAGINARY, CONJ, ARG, MOD, ACOS, ASIN, ATAN};
    return code_menu("Complexes", labels, codes, 7);
}

int menu_const_phy()
{
    const char *labels[8] = {"Pesanteur g", "Celerite c", "Gravitation G", "Planck h",
                             "Charge elementaire e", "Masse electron me", "Avogadro NA",
                             "Gaz parfaits R"};
    const int codes[8] = {CST_G, CST_C, CST_GRAV, CST_PLANCK, CST_QE, CST_ME, CST_NA, CST_R};
    return code_menu("Constantes", labels, codes, 8);
}

// Ecrit "[[0,0],[0,0]]" (rows x cols) dans g_custom_matrix.
static void zero_matrix_literal(int rows, int cols)
{
    int p = 0;
    g_custom_matrix[p++] = '[';
    for (int r = 0; r < rows; r++)
    {
        if (r > 0)
            g_custom_matrix[p++] = ',';
        g_custom_matrix[p++] = '[';
        for (int c = 0; c < cols; c++)
        {
            if (c > 0)
                g_custom_matrix[p++] = ',';
            g_custom_matrix[p++] = '0';
        }
        g_custom_matrix[p++] = ']';
    }
    g_custom_matrix[p++] = ']';
    g_custom_matrix[p] = '\0';
}

int menu_matrices()
{
    const char *labels[6] = {"Matrice 2x2", "Matrice 3x3", "Matrice n*m...",
                             "determinant", "inverse", "transposee"};
    int sel = 0;
    while (true)
    {
        int r = menu_list("Matrices", labels, 6, &sel);
        if (r == MENU_BACK)
            return END_KEYS;
        switch (r)
        {
        case 0:
            zero_matrix_literal(2, 2);
            return MATRIX_CUSTOM;
        case 1:
            zero_matrix_literal(3, 3);
            return MATRIX_CUSTOM;
        case 2:
        {
            int rows = 2, cols = 2;
            int ok = pick_matrix_size(&rows, &cols);
            fill_screen(BACKGROUND_COLOR);
            if (ok)
            {
                zero_matrix_literal(rows, cols);
                return MATRIX_CUSTOM;
            }
            break; // annule : retour au menu Matrices
        }
        case 3: return FUNC_DET;
        case 4: return FUNC_INV;
        case 5: return FUNC_TRANS;
        }
    }
}

int menu_seq_blocks()
{
    const char *labels[5] = {"Arithmetique U+r", "Geometrique q*U", "Arith-geom a*U+b",
                             "U + a*n + b", "Autres outils..."};
    const int codes[4] = {SEQ_ARITH, SEQ_GEOM, SEQ_ARIGEO, SEQ_ARITHN};
    int sel = 0;
    int r = menu_list("Suites", labels, 5, &sel);
    if (r == MENU_BACK)
        return END_KEYS;
    if (r == 4)
        return menu_tools();
    return codes[r];
}