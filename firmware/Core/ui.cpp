#include "Cas.h"
#include <vector>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <cmath>
#include <cstdlib>
#include <string.h>
#include <string>

#include "ui.h"
#include "menu.h"
#include "Evaluator.h"
#include "widgets.h"
#include "colors.h"

#define MAX_NODES 128

uint16_t g_cursor_bg = 0xf7de;

static bool g_cursor_found = false;
static int g_cursor_sets = 0; // nombre d'appels a set_cursor (priorite aux enfants)
static void set_cursor(int x, int y);

extern int g_decimals;

// Raw source currently being rendered by display_equation, so that matrix
// literals can be drawn character by character and the cursor tracked.
static const char *g_src = NULL;
static int g_src_len = 0;

static ASTNode node_pool[MAX_NODES];
static int pool_top = 0;

static ASTNode *alloc_node()
{
    if (pool_top >= MAX_NODES)
        return &node_pool[MAX_NODES - 1];
    ASTNode *n = &node_pool[pool_top++];
    n->left = nullptr;
    n->right = nullptr;
    n->number = 0;
    n->op = 0;
    n->src_pos = -1;
    n->src_close = -1;
    return n;
}
static void reset_pool() { pool_top = 0; }

static ASTNode *make_placeholder()
{
    ASTNode *nd = alloc_node();
    nd->type = N_PLACEHOLDER;
    return nd;
}

struct coord_s
{
    int x;
    int y;
};

typedef struct coord_s coord;

extern int x_cursor;
extern int y_cursor;

int min(int a, int b)
{
    if (a < b)
    {
        return a;
    }
    return b;
}

int max(int a, int b)
{
    if (a > b)
    {
        return a;
    }
    return b;
}

void axis(double x_min, double x_max, double y_min, double y_max)
{
    if (x_min * x_max < 0)
        fill_rect(0, (uint16_t)(fabs(x_min) / (x_max - x_min) * 320), 220, 2, 0x0000);
    if (y_min * y_max < 0)
        fill_rect((uint16_t)(fabs(y_min) / (y_max - y_min) * 240), 0, 2, 340, 0x0000);
}

void display_text(int x, int y, char *t, int SIZE, int t_size)
{
    int pos = 0;
    x += 25;
    for (int i = 0; i < t_size; i++)
    {
        char one[2] = {t[i], '\0'}; // draw_char stops on '\0': never pass &t[i]
        draw_char(x, y + 5 + pos, one, 0X0000, 0X0000, SIZE);
        pos += 5.5 * SIZE;
    }
}

void draw_buffer(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *buffer)
{
    blit_buffer(x, y, w, h, buffer);
}

int *get_depth(char *in, int input_size)
{
    int *depth = (int *)malloc(sizeof(int) * input_size);
    for (int i = 0; i < input_size; i++)
    {
        depth[i] = 0;
    }
    int m = 0;
    int cur_depth = 0;
    for (int searched_depth = 10; searched_depth >= 0; searched_depth--)
    { // A optimiser, inneficient
        for (int i = 0; i < input_size; i++)
        {
            if (in[i] == '(')
                cur_depth++;
            if (in[i] == ')')
                cur_depth--;
            if (cur_depth == searched_depth && in[i] == '/')
            {
                int a = 0;
                int loc_max = 0;
                int j = i + 1;
                for (j = i + 1; (j == i + 1 || a > 0) && j < input_size; j++)
                {
                    if (in[j] == '(')
                        a++;
                    if (in[j] == ')')
                        a--;
                    if (depth[j] > loc_max)
                        loc_max = depth[j];
                }
                a = 0;
                int loc_min = 0;
                for (j = i - 1; (j == i - 1 || a < 0) && j >= 0; j--)
                {
                    if (in[j] == '(')
                        a++;
                    if (in[j] == ')')
                        a--;
                    if (depth[j] < loc_min)
                        loc_min = depth[j];
                }

                for (j = i - 1; (j == i - 1 || a < 0) && j >= 0; j--)
                {
                    if (in[j] == '(')
                        a++;
                    if (in[j] == ')')
                        a--;
                    depth[j] += -loc_min + 1;
                }
                for (j = i + 1; (j == i + 1 || a > 0) && j < input_size; j++)
                {
                    if (in[j] == '(')
                        a++;
                    if (in[j] == ')')
                        a--;
                    depth[j] += -loc_max - 1;
                }
            }
        }
    }
    for (int i = 0; i < input_size; i++)
    {

        if (depth[i] < m)
            m = depth[i];
    }
    for (int i = 0; i < input_size; i++)
    {

        depth[i] -= m;
        // depth[i]=0;
    }
    return depth;
}

float *get_length(char *in, int input_size, int *depth)
{
    float *length = (float *)malloc(sizeof(float) * input_size);
    length[0] = 0;
    if (is_in(in[0], "uvwajkb"))
        length[0] += 3;
    if (is_in(in[0], "fghcstdm"))
        length[0] += 2;
    for (int i = 1; i < input_size; i++)
    {
        length[i] = length[i - 1] + 1;
        if (is_in(in[i], "uvwajkb"))
            length[i] += 3;
        if (is_in(in[i], "fghcstdm"))
            length[i] += 2;
    }
    for (int searched_depth = 10; searched_depth >= 0; searched_depth--)
    {
        int cur_depth = 0;
        for (int i = 0; i < input_size; i++)
        {
            if (in[i] == '(')
            {
                cur_depth++;
            }
            if (in[i] == ')')
            {
                cur_depth--;
            }
            if (in[i] == '/' && cur_depth == searched_depth)
            {
                int a = 0;
                int l1 = 0;
                int j;
                for (j = i - 1; (j == i - 1 || a < 0) && j > 0; j--)
                {
                    if (in[j] == '(')
                        a++;
                    if (in[j] == ')')
                        a--;
                }

                int d = j;
                l1 = length[i] - length[max(j, 0)];

                int l2 = 0;
                a = 0;
                for (j = i + 1; (j == i + 1 || a > 0) && j < input_size; j++)
                {
                    if (in[j] == '(')
                        a++;
                    if (in[j] == ')')
                        a--;
                }
                l2 = length[j] - length[i];
                if (l2 > l1)
                {
                    for (int j = d + 1; j < i; j++)
                    {
                        length[j] += (l2 - l1) / 2.0;
                    }
                    for (int j = i; j < input_size; j++)
                    {
                        length[j] -= l1;
                    }
                }
                else
                {
                }
            }
        }
    }
    return length;
}

static token *p_peek(Parser *p)
{
    return p->pos < p->n ? &p->toks[p->pos] : nullptr;
}
static token *p_consume(Parser *p)
{
    return p->pos < p->n ? &p->toks[p->pos++] : nullptr;
}

static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_term(Parser *p);
static ASTNode *parse_factor(Parser *p);
static ASTNode *parse_base(Parser *p);
static ASTNode *parse_base(Parser *p)
{
    token *t = p_peek(p);
    if (!t)
        return make_placeholder();

    if (t->type == '(')
    {
        //   bool virt = t->is_virtual;
        p_consume(p);
        ASTNode *inner = parse_expr(p);
        ASTNode *paren = alloc_node();
        paren->type = N_PARENTHESIS;
        paren->op = '(';
        paren->left = inner;
        paren->src_pos = t->src_pos;
        if (p_peek(p) && p_peek(p)->type == ')')
        {
            paren->right = alloc_node();
            paren->right->type = N_PLACEHOLDER;
            paren->src_close = p_peek(p)->src_pos;
            p_consume(p);
        }
        return paren;
    }

    if (t->type == '-')
    {
        // Unary minus: binds weaker than '^' and '!' (-2^2 = -(2^2)).
        token *ot = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_NEG;
        nd->op = '-';
        nd->src_pos = ot->src_pos;
        nd->left = parse_factor(p);
        return nd;
    }

    if (t->type == ')')
    {
        p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_PARENTHESIS;
        nd->op = ')';
        nd->src_pos = t->src_pos;
        return nd;
    }
    if (t->type == 'n')
    {

        p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_NUMBER;
        nd->number = t->value;
        nd->src_pos = t->src_pos;
        return nd;
    }
    if (t->type == 'X')
    {
        p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_VARIABLE;
        nd->variable = (char)('a' + (int)t->value);
        nd->src_pos = t->src_pos;
        return nd;
    }
    if (t->type == 'p' || t->type == 'e' || t->type == 'i' || t->type == 'z')
    {
        p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_CONST;
        nd->op = t->type;
        nd->src_pos = t->src_pos;
        return nd;
    }
    if (t->type == 'M')
    {
        p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_MATRIX;
        nd->mat = t->mat;
        nd->src_pos = t->src_pos;
        return nd;
    }
    if (t->type == '(')
    {
        p_consume(p);
        ASTNode *inner = parse_expr(p);
        if (p_peek(p) && p_peek(p)->type == ')')
            p_consume(p);
        return inner;
    }

    char *funcs = (char *)"lrcstuvwfghjak!bdmFGHoqyPLDRSO@#&?$:;<>%|`{}_\"\x01\x02\x03\x04\x05";
    if (is_in(t->type, funcs))
    {
        token *ft = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_FUNC;
        nd->op = ft->type;
        nd->src_pos = ft->src_pos;
        nd->number = 1;

        if (p_peek(p) && p_peek(p)->type == '(')
        {
            p_consume(p);
            nd->number *= 2;
        }
        token *next = p_peek(p);
        if (!next || next->type == ')')
            nd->left = make_placeholder();
        else
            nd->left = parse_expr(p);
        if (p_peek(p) && p_peek(p)->type == ',')
        {
            p_consume(p);
            token *n2 = p_peek(p);
            if (!n2 || n2->type == ')')
                nd->right = make_placeholder();
            else
                nd->right = parse_expr(p);
            // Arguments suivants (somme, produit, integrale : 3 arguments) :
            // chaines dans un noeud ',' affiche "a,b".
            ASTNode **tail = &nd->right;
            while (p_peek(p) && p_peek(p)->type == ',')
            {
                token *ct = p_consume(p);
                ASTNode *cm = alloc_node();
                cm->type = N_BINOP;
                cm->op = ',';
                cm->src_pos = ct->src_pos;
                cm->left = *tail;
                token *n3 = p_peek(p);
                cm->right = (!n3 || n3->type == ')' || n3->type == ',') ? make_placeholder() : parse_expr(p);
                *tail = cm;
                tail = &cm->right;
            }
        }
        if (p_peek(p) && p_peek(p)->type == ')')
        {
            p_consume(p);
            nd->number *= 3;
        };
        return nd;
    }

    return make_placeholder();
}

static ASTNode *parse_factor(Parser *p)
{
    ASTNode *base = parse_base(p);
    token *t = p_peek(p);
    if (t && t->type == '^')
    {
        token *ot = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_BINOP;
        nd->op = '^';
        nd->src_pos = ot->src_pos;
        nd->left = base;
        nd->right = parse_factor(p);
        return nd;
    }
    if (t && t->type == '!')
    {
        token *ot = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_FUNC;
        nd->op = '!';
        nd->src_pos = ot->src_pos;
        nd->number = 1;
        nd->left = base;
        return nd;
    }
    return base;
}

static ASTNode *parse_div(Parser *p);

// '/' : lie plus fort que '*' (affichage naturel : 1/2*1/2 = (1/2)*(1/2))
static ASTNode *parse_div(Parser *p)
{
    ASTNode *left = parse_factor(p);
    while (true)
    {
        token *t = p_peek(p);
        if (!t || t->type != '/')
            break;
        token *ot = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_BINOP;
        nd->op = '/';
        nd->src_pos = ot->src_pos;
        nd->left = left;
        nd->right = parse_factor(p);
        left = nd;
    }
    return left;
}

static ASTNode *parse_term(Parser *p)
{
    ASTNode *left = parse_div(p);
    while (true)
    {
        token *t = p_peek(p);
        if (!t || t->type != '*')
            break;
        token *ot = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_BINOP;
        nd->op = '*';
        nd->src_pos = ot->src_pos;
        nd->left = left;
        nd->right = parse_div(p);
        left = nd;
    }
    return left;
}
static ASTNode *parse_expr(Parser *p)
{
    ASTNode *left = parse_term(p);
    while (true)
    {
        token *t = p_peek(p);
        if (!t || (t->type != '+' && t->type != '-'))
            break;
        token *ot = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_BINOP;
        nd->op = ot->type;
        nd->src_pos = ot->src_pos;
        nd->left = left;
        nd->right = parse_term(p);
        left = nd;
    }
    return left;
}
ASTNode *parse_equation(Parser *p)
{
    ASTNode *left = parse_expr(p);
    token *t = p_peek(p);
    if (t && t->type == '=')
    {
        token *ot = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_BINOP;
        nd->op = '=';
        nd->src_pos = ot->src_pos;
        nd->left = left;
        nd->right = parse_expr(p);
        left = nd;
    }
    /* Plusieurs solutions du calcul formel : "X=1 ; X=2". */
    t = p_peek(p);
    if (t && t->type == CAS_SEPARATOR)
    {
        token *ot = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type = N_BINOP;
        nd->op = ';';
        nd->src_pos = ot->src_pos;
        nd->left = left;
        nd->right = parse_equation(p);
        return nd;
    }
    return left;
}

static inline int CW(int S) { return 6 * S; }
static inline int CH(int S) { return 8 * S; }

// Number of source characters of the matrix literal starting at `start`
// (from the opening '[' to the matching closing ']').
static int matrix_span(const char *s, int len, int start)
{
    if (!s || start < 0 || start >= len)
        return 0;
    int depth = 0;
    for (int i = start; i < len; i++)
    {
        if (s[i] == '[')
            depth++;
        else if (s[i] == ']')
        {
            depth--;
            if (depth == 0)
                return i - start + 1;
        }
    }
    return 0;
}

// --- Inline editable matrix grid -------------------------------------------------

// Parse a complete matrix literal "[[a,b],[c,d]]" starting at s[start]=='['.
// Returns the number of rows (0 if not a well-formed uniform literal, in
// which case the caller falls back to raw character rendering).
int matrix_parse_literal(const char *s, int len, int start, matrix_lit *L)
{
    if (!s || start < 0 || start + 2 >= len || s[start] != '[' || s[start + 1] != '[')
        return 0;
    int i = start + 2, n = 0, rows = 0, cols = -1;
    L->start = start;
    while (true)
    {
        if (rows > 0)
        {
            if (i >= len || s[i] != '[')
                return 0;
            i++;
        }
        int rowcols = 0;
        while (i < len && s[i] != ']')
        {
            int st = i;
            while (i < len && s[i] != ',' && s[i] != ']' && s[i] != '\0')
                i++;
            if (n < MAT_LIT_MAX)
            {
                L->cell_start[n] = st;
                L->cell_len[n] = i - st;
                n++;
            }
            else
                return 0;
            rowcols++;
            if (i < len && s[i] == ',')
                i++;
            else
                break;
        }
        if (i >= len || s[i] != ']')
            return 0;
        i++;
        if (cols == -1)
            cols = rowcols;
        else if (rowcols != cols)
            return 0;
        rows++;
        if (rows > 12 || cols > 12)
            return 0;
        if (i < len && s[i] == ',')
        {
            i++;
            continue;
        }
        break;
    }
    if (i >= len || s[i] != ']')
        return 0;
    i++;
    L->rows = rows;
    L->cols = cols;
    L->span = i - start;
    return (rows > 0 && cols > 0) ? rows : 0;
}

// Same as call_positions for one-argument blocks (no separator): '(' index and
// matching ')' index (-1 when not closed yet).
static bool call_positions1(const ASTNode *nd, int name_len, int *lp, int *cl)
{
    if (!g_src || !nd || nd->src_pos < 0)
        return false;
    int p = nd->src_pos + name_len;
    if (p >= g_src_len || g_src[p] != '(')
        return false;
    *lp = p;
    *cl = -1;
    int depth = 0;
    for (int i = p; i < g_src_len; i++)
    {
        if (g_src[i] == '(')
            depth++;
        else if (g_src[i] == ')')
        {
            depth--;
            if (depth == 0)
            {
                *cl = i;
                break;
            }
        }
    }
    return true;
}

#define M_PAD 3
static inline int MB_W(int S) { return CW(S); }
static inline int MC_H(int S) { return CH(S) - 1; }

// Width (pixels) of each column: the widest cell content in that column.
static void matrix_grid_cols(const matrix_lit *L, int SIZE, int *colw)
{
    int gs = SIZE;
    for (int c = 0; c < L->cols; c++)
    {
        int maxn = 1;
        for (int r = 0; r < L->rows; r++)
        {
            int cl = L->cell_len[r * L->cols + c];
            if (cl > maxn)
                maxn = cl;
        }
        colw[c] = maxn * CW(gs);
    }
}

static void matrix_grid_dims(const matrix_lit *L, int SIZE, int *w, int *h)
{
    int gs = SIZE;
    int colw[MAT_LIT_MAX / 12];
    matrix_grid_cols(L, SIZE, colw);
    *w = 2 * MB_W(gs) + (L->cols + 1) * M_PAD;
    for (int c = 0; c < L->cols; c++)
        *w += colw[c];
    *h = L->rows * MC_H(gs) + (L->rows + 1) * M_PAD;
}

// Geometry of the currently-edited cell, for blink_cursor.
static matrix_blink_state g_blink;

void matrix_blink_save(matrix_blink_state *s)
{
    *s = g_blink;
}

void matrix_blink_restore(const matrix_blink_state *s)
{
    g_blink = *s;
}

static int render_matrix_grid(matrix_lit *L, int x, int y, int SIZE, int cursor_pos)
{
    int gs = SIZE;
    int gw, gh;
    matrix_grid_dims(L, SIZE, &gw, &gh);
    int bw = MB_W(gs), ch = MC_H(gs);
    int colw[MAT_LIT_MAX / 12];
    matrix_grid_cols(L, SIZE, colw);
    int top = x - gh / 2;
    int left = y;
    int right = left + gw;
    int mid_v = top + gh / 2;
    int bxb = mid_v;
    int thick = gs >= 2 ? 2 : 1;
    int bar_h = gh / 7;
    if (bar_h < thick) bar_h = thick;
    int point_h = bar_h;
    int stem_h = (gh - 2 * bar_h - point_h) / 2;
    int bar_w = bw / 2;
    int point_w = bar_w / 2;
    int lx = left + bw / 2;
    int rx = right - bw / 2;
    fill_rect(top, lx, bar_h, bar_w, 0x0000);
    fill_rect(top + bar_h, lx, stem_h, thick, 0x0000);
    fill_rect(top + bar_h + stem_h, lx - point_w, point_h, point_w + thick, 0x0000);
    fill_rect(top + bar_h + stem_h + point_h, lx, stem_h, thick, 0x0000);
    fill_rect(top + gh - bar_h, lx, bar_h, bar_w, 0x0000);
    fill_rect(top, rx - bar_w + thick, bar_h, bar_w, 0x0000);
    fill_rect(top + bar_h, rx, stem_h, thick, 0x0000);
    fill_rect(top + bar_h + stem_h, rx, point_h, point_w + thick, 0x0000);
    fill_rect(top + bar_h + stem_h + point_h, rx, stem_h, thick, 0x0000);
    fill_rect(top + gh - bar_h, rx - bar_w + thick, bar_h, bar_w, 0x0000);

    int cur_cell = -1, intra = 0;
    if (cursor_pos >= 0 && cursor_pos > L->start && cursor_pos < L->start + L->span)
    {
        for (int r = 0; r < L->rows && cur_cell < 0; r++)
            for (int c = 0; c < L->cols; c++)
            {
                int idx = r * L->cols + c;
                int cs = L->cell_start[idx], cl = L->cell_len[idx];
                if (cursor_pos >= cs && cursor_pos <= cs + cl)
                {
                    cur_cell = idx;
                    intra = cursor_pos - cs;
                    break;
                }
            }
        if (cur_cell < 0)
        {
            int best = -1, bestdist = 1 << 30;
            for (int r = 0; r < L->rows; r++)
                for (int c = 0; c < L->cols; c++)
                {
                    int idx = r * L->cols + c;
                    int cs = L->cell_start[idx], cl = L->cell_len[idx];
                    if (cs >= cursor_pos && cs - cursor_pos < bestdist)
                    {
                        bestdist = cs - cursor_pos;
                        best = idx;
                        intra = 0;
                    }
                    else if (cs + cl <= cursor_pos && cursor_pos - (cs + cl) < bestdist)
                    {
                        bestdist = cursor_pos - (cs + cl);
                        best = idx;
                        intra = cl;
                    }
                }
            cur_cell = best;
        }
    }

    for (int r = 0; r < L->rows; r++)
        for (int c = 0; c < L->cols; c++)
        {
            int idx = r * L->cols + c;
            int ctop = top + gh - M_PAD - ch - r * (ch + M_PAD);
            int cleft = left + bw + M_PAD;
            for (int cc = 0; cc < c; cc++)
                cleft += colw[cc] + M_PAD;
            int cellw = colw[c];
            int cl = L->cell_len[idx];
            char buf[24];
            int nb = 0;
            for (int k = 0; k < cl && k < 22; k++)
                buf[nb++] = g_src[L->cell_start[idx] + k];
            buf[nb] = '\0';
            if (nb > 0)
            {
                int bx = ctop + (ch + 7 * gs) / 2;
                int ty = cleft + (cellw - nb * CW(gs)) / 2;
                for (int k = 0; k < nb; k++)
                {
                    char one[2] = {buf[k], '\0'};
                    draw_char(bx, ty, one, 0x0000, 0x0000, gs);
                    ty += CW(gs);
                }
            }
            if (idx == cur_cell)
            {
                fill_rect(ctop - 2, cleft - 2, 2, cellw + 4, 0x0000);
                fill_rect(ctop + ch, cleft - 2, 2, cellw + 4, 0x0000);
                fill_rect(ctop - 2, cleft - 2, ch + 4, 1, 0x0000);
                fill_rect(ctop - 2, cleft + cellw, ch + 4, 1, 0x0000);
                g_blink.active = 1;
                g_cursor_found = true;
                int bx = ctop + (ch + 7 * gs) / 2;
                int ty = cleft + (cellw - cl * CW(gs)) / 2;
                g_blink.bar_x = bx - 7 * gs;
                g_blink.bar_y = ty + intra * CW(gs) - 2;
                g_blink.bar_w = 2;
                g_blink.bar_h = 7 * gs + 2;
                g_blink.cell_x = ctop;
                g_blink.cell_y = cleft;
                g_blink.cell_w = cellw;
                g_blink.cell_h = ch;
                g_blink.text = g_src + L->cell_start[idx];
                g_blink.text_len = cl;
                g_blink.text_x = bx;
                g_blink.text_y = ty;
                g_blink.gs = gs;
                g_blink.bg = g_cursor_bg;
            }
        }
    if (cursor_pos >= L->start && cursor_pos <= L->start + L->span && cur_cell < 0)
    {
        if (cursor_pos <= L->start)
        {
            set_cursor(bxb, left - 6);
        }
        else
        {
            set_cursor(bxb, right - 3);
        }
    }
    return y + gw;
}

void display_set_source(char *src, int len)
{
    g_src = src;
    g_src_len = len;
}

int fmt_number(double v, char *buf)
{
    if (v == (int)v && v >= 0 && v < 10000000)
    {
        return sprintf(buf, "%d", (int)v);
    }
    int ndec = g_decimals;
    if (ndec < 0)
        ndec = 0;
    if (ndec > 9)
        ndec = 9;
    double scale = 1;
    for (int i = 0; i < ndec; i++)
        scale *= 10;
    double int_part = floor(v);
    double dec_part = v - int_part;
    if (dec_part < 0)
        dec_part = -dec_part;

    double decimals = dec_part * scale + 0.5;
    if (v != INFINITY && v != -INFINITY && decimals >= scale)
    {
        int_part += 1;
        decimals = 0;
    }
    long int len;
    if (v != INFINITY && v != -INFINITY && decimals >= 1)
    {
        len = sprintf(buf, "%0.lf.", int_part);
    }
    else
        len = sprintf(buf, "%0.lf", int_part);
    long int dec_len = 0;

    char dec_buf[16];
    if (v != INFINITY && v != -INFINITY && decimals >= 1)
    {
        dec_len = sprintf(dec_buf, "%0.lf", decimals);
        while (dec_len > 1 && dec_buf[dec_len - 1] == '0')
            dec_len--;
        dec_buf[dec_len] = '\0';

        for (int i = 0; i < dec_len; i++)
            buf[len++] = dec_buf[i];
        buf[len] = '\0';
    }
    return len;
}

// Fonctions affichees en notation name(first,second) : pgcd/ppcm/quot/reste
// et les blocs a deux parametres arigeo/arithn.
static bool is_call2_op(char op)
{
    return op == 'P' || op == 'L' || op == 'D' || op == 'R' || op == '&' || op == '?' ||
           op == '<' || op == '>' || op == '%' || op == '|' || op == '{' || op == '}' || op == '_' ||
           op == '"';
}

static int func_name_width(char op, int SIZE)
{
    switch (op)
    {
    case 'c':
    case 's':
    case 't':
        return 3 * CW(SIZE);
    case 'u':
    case 'v':
    case 'w':
    case 'f':
    case 'g':
    case 'h':
        return 4 * CW(SIZE);
    case 'a':
    case 'j':
    case 'k':
        return 5 * CW(SIZE);
    case 'b':
        return 4 * CW(SIZE);
    case 'd':
    case 'm':
        return 3 * CW(SIZE);
    case 'l':
        return 2 * CW(SIZE);
    case 'o':
        return 3 * CW(SIZE);
    case 'q':
        return 5 * CW(SIZE);
    case 'y':
        return 4 * CW(SIZE);
    case 'P':
        return 4 * CW(SIZE); // pgcd
    case 'L':
        return 4 * CW(SIZE); // ppcm
    case 'D':
        return 4 * CW(SIZE); // quot
    case 'R':
        return 5 * CW(SIZE); // reste
    case 'S':
        return 3 * CW(SIZE); // abs
    case 'O':
        return 7 * CW(SIZE); // arrondi
    case '@':
        return 5 * CW(SIZE); // arith
    case '#':
        return 4 * CW(SIZE); // geom
    case '&':
        return 6 * CW(SIZE); // arigeo
    case '?':
        return 6 * CW(SIZE); // arithn
    case '$':
        return 3 * CW(SIZE); // exp
    case ':':
        return 3 * CW(SIZE); // ent
    case ';':
        return 5 * CW(SIZE); // signe
    case '<':
        return 3 * CW(SIZE); // min
    case '>':
        return 3 * CW(SIZE); // max
    case '%':
        return 6 * CW(SIZE); // racine
    case '`':
        return 4 * CW(SIZE); // alea
    case '|':
        return 7 * CW(SIZE); // randint
    case '{':
        return 5 * CW(SIZE); // somme
    case '}':
        return 7 * CW(SIZE); // produit
    case '_':
        return 7 * CW(SIZE); // derivee
    case '"':
        return 9 * CW(SIZE); // integrale
    case CAS_TOK_EXPAND:
        return 10 * CW(SIZE); // developper
    case CAS_TOK_FACTOR:
        return 10 * CW(SIZE); // factoriser
    case CAS_TOK_DERIVE:
        return 7 * CW(SIZE); // deriver
    case CAS_TOK_PRIMITIVE:
        return 9 * CW(SIZE); // primitive
    case CAS_TOK_SOLVE:
        return 8 * CW(SIZE); // resoudre
    case 'F':
    case 'G':
        return 3 * CW(SIZE);
    case 'H':
        return 5 * CW(SIZE);
    default:
        return CW(SIZE);
    }
}

// True when the ',' at g_src[comma] separates the arguments of a somme /
// produit / derivee / integrale call (their arguments are expressions, so the
// digit-run test below is not enough: "somme(X^2,1,10)").
static bool comma_in_calculus_call(int comma)
{
    int depth = 0;
    for (int i = comma - 1; i >= 0; i--)
    {
        char c = g_src[i];
        if (c == ')' || c == ']')
            depth++;
        else if (c == '(' || c == '[')
        {
            if (depth > 0)
            {
                depth--;
                continue;
            }
            if (c != '(')
                return false;
            static const char *names[4] = {"somme", "produit", "derivee", "integrale"};
            for (int k = 0; k < 4; k++)
            {
                int len = (int)strlen(names[k]);
                if (i >= len && strncmp(g_src + i - len, names[k], len) == 0)
                    return true;
            }
            return false;
        }
    }
    return false;
}

// Returns the raw source span of a numeric literal, so that typing a long
// number shows the exact digits instead of the rounded double value.
static bool number_source_span(const ASTNode *nd, int *start, int *len)
{
    if (!g_src || !nd || nd->type != N_NUMBER)
        return false;
    int end = nd->src_pos;
    if (end < 0 || end >= g_src_len)
        return false;
    char ec = g_src[end];
    if (!((ec >= '0' && ec <= '9') || ec == '.' || ec == ','))
        return false;
    int begin = end;
    while (begin > 0)
    {
        char c = g_src[begin - 1];
        if (c == ',')
        {
            // A comma belongs to the number (decimal separator) only when the
            // digits before it are not function-call arguments. If the run of
            // digits preceding the comma follows a '(' the comma is instead a
            // binom/perm argument separator, so the span stops here.
            int j = begin - 1;
            while (j > 0 && ((g_src[j - 1] >= '0' && g_src[j - 1] <= '9') ||
                             g_src[j - 1] == '.' || g_src[j - 1] == ','))
                j--;
            if (j > 0 && g_src[j - 1] == '(')
                break;
            if (comma_in_calculus_call(begin - 1))
                break;
            begin--;
        }
        else if ((c >= '0' && c <= '9') || c == '.')
        {
            begin--;
        }
        else
        {
            break;
        }
    }
    bool has_digit = false;
    for (int k = begin; k <= end; k++)
    {
        if (g_src[k] >= '0' && g_src[k] <= '9')
        {
            has_digit = true;
            break;
        }
    }
    if (!has_digit)
        return false;
    *start = begin;
    *len = end - begin + 1;
    return true;
}

static bool bigop_measure(ASTNode *nd, int SIZE, Dims *out);

Dims measure(ASTNode *nd, int SIZE)
{
    if (!nd)
        return {CW(SIZE), CH(SIZE), CH(SIZE) / 2};
    switch (nd->type)
    {

    case N_PLACEHOLDER:
    case N_VARIABLE:
        return {CW(SIZE), CH(SIZE), CH(SIZE) / 2};
    case N_CONST:
        return {nd->op == 'z' ? 3 * CW(SIZE) : CW(SIZE), CH(SIZE), CH(SIZE) / 2};
    case N_NUMBER:
    {
        int lit_start, lit_len;
        if (number_source_span(nd, &lit_start, &lit_len))
            return {lit_len * CW(SIZE), CH(SIZE), CH(SIZE) / 2};
        char buf[256];
        int nc = fmt_number(nd->number, buf);
        return {nc * CW(SIZE), CH(SIZE), CH(SIZE) / 2};
    }
    case N_PARENTHESIS:
    {
        if (nd->op == ')')
        {
            return {CW(SIZE), CH(SIZE), CH(SIZE) / 2};
        }

        Dims c = measure(nd->left, SIZE);
        int pw = CW(SIZE) + (nd->right ? CW(SIZE) : 0);
        return {c.w + pw, c.h, c.baseline};
    }
    case N_BINOP:
    {
        if (nd->op == '/')
        {
            Dims num = measure(nd->left, SIZE);
            Dims den = measure(nd->right, SIZE);
            int w = (num.w > den.w ? num.w : den.w) + 4;
            return {w, num.h + den.h + 6, den.h + 3};
        }
        else if (nd->op == '^')
        {
            int eS = SIZE > 1 ? SIZE - 1 : 1;
            Dims b = measure(nd->left, SIZE);
            Dims e = measure(nd->right, eS);
            return {b.w + e.w, b.h + e.h / 2, b.baseline + e.h / 2};
        }
        else
        {
            Dims l = measure(nd->left, SIZE);
            Dims r = measure(nd->right, SIZE);
            int ow = (nd->op == '*' && nd->src_pos < 0) ? 0 : CW(SIZE);
            int baseline = l.baseline > r.baseline ? l.baseline : r.baseline;
            int below_l = l.h - l.baseline;
            int below_r = r.h - r.baseline;
            int h = baseline + (below_l > below_r ? below_l : below_r);
            return {l.w + ow + r.w, h, baseline};
        }
    }
    case N_FUNC:
    {
        {
            Dims big;
            if (bigop_measure(nd, SIZE, &big))
                return big;
        }
        if (nd->op == 'r')
        {
            Dims c = measure(nd->left, SIZE);
            return {CW(SIZE) + 2 + c.w, c.h + 3, c.baseline + 3};
        }
        Dims c = measure(nd->left, SIZE);
        if (nd->op == '!')
        {
            int h = c.h > CH(SIZE) ? c.h : CH(SIZE);
            return {c.w + CW(SIZE), h, h / 2};
        }
        if ((nd->op == 'q' || nd->op == 'y') && nd->right)
        {
            // Traditional notation: the two arguments are stacked. `binom`
            // wraps them in tall parentheses; `perm` shows A with n above k.
            Dims top = measure(nd->left, SIZE);
            Dims bot = measure(nd->right, SIZE);
            int gap = 2 * SIZE;
            if (nd->op == 'q')
            {
                int pd = 4 * SIZE; // parenthesis depth
                int w = (top.w > bot.w ? top.w : bot.w) + 2 * pd;
                int h = top.h + bot.h + gap;
                return {w, h, bot.h + gap / 2};
            }
            int aw = CW(SIZE);
            int kw = (top.w > bot.w ? top.w : bot.w);
            int h = top.h + bot.h + gap;
            int ah = CH(SIZE);
            if (h < ah)
                h = ah;
            return {aw + kw + SIZE, h, bot.h + gap / 2};
        }
        if (is_call2_op(nd->op) && nd->right)
        {
            // Ordinary call notation: name(first,second).
            Dims l = measure(nd->left, SIZE);
            Dims r = measure(nd->right, SIZE);
            int fw = func_name_width(nd->op, SIZE);
            int h = l.h > r.h ? l.h : r.h;
            if (h < CH(SIZE))
                h = CH(SIZE);
            return {fw + 3 * CW(SIZE) + l.w + r.w, h, h / 2};
        }
        int fw = func_name_width(nd->op, SIZE);
        int h = c.h > CH(SIZE) ? c.h : CH(SIZE);
        return {fw + CW(SIZE) + c.w + CW(SIZE), h, h / 2};
    }
    case N_NEG:
    {
        Dims c = measure(nd->left, SIZE);
        return {CW(SIZE) + c.w, c.h, c.baseline};
    }
    case N_MATRIX:
    {
        int span = matrix_span(g_src, g_src_len, nd->src_pos);
        if (span > 0)
        {
            matrix_lit L;
            if (matrix_parse_literal(g_src, g_src_len, nd->src_pos, &L))
            {
                int w, h;
                matrix_grid_dims(&L, SIZE, &w, &h);
                return {w, h, h / 2};
            }
            return {span * CW(SIZE), CH(SIZE), CH(SIZE) / 2};
        }
        char buf[128];
        int nc = mat_to_string(nd->mat, buf, 128);
        return {nc * CW(SIZE), CH(SIZE), CH(SIZE) / 2};
    }
    }
    return {CW(SIZE), CH(SIZE), CH(SIZE) / 2};
}

extern int x_cursor;
extern int y_cursor;

// Visible interval (along the reading axis) while rendering the equation.
// It keeps long lines inside their box and is restored after rendering.
static int g_clip_lo = -32768;
static int g_clip_hi = 32767;

static void set_cursor(int x, int y)
{
    x_cursor = x;
    y_cursor = y;
    g_cursor_found = true;
    g_cursor_sets++;
}

static bool glyph_visible(int y, int SIZE)
{
    return y >= g_clip_lo && y + 5 * SIZE <= g_clip_hi;
}

static void render_char(int x, int y, char ch, int SIZE)
{
    if (!glyph_visible(y, SIZE))
        return;
    // draw_char draws until '\0': a lone char needs its own terminator,
    // otherwise it also draws whatever bytes follow it in memory.
    char one[2] = {ch, '\0'};
    draw_char((uint16_t)x, (uint16_t)y, one, 0x0000, 0x0000, SIZE);
}

static void render_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    int y0 = y;
    int y1 = y + h;
    if (y1 <= g_clip_lo || y0 >= g_clip_hi)
        return;
    if (y0 < g_clip_lo)
        y0 = g_clip_lo;
    if (y1 > g_clip_hi)
        y1 = g_clip_hi;
    fill_rect((uint16_t)x, (uint16_t)y0, (uint16_t)w, (uint16_t)(y1 - y0), color);
}

// Draws one character (a whole UTF-8 sequence: "é", "Σ"...) like render_char.
static void render_glyph(int x, int y, const char *seq, int len, int SIZE)
{
    if (!glyph_visible(y, SIZE))
        return;
    char one[5] = {0, 0, 0, 0, 0};
    for (int k = 0; k < len && k < 4; k++)
        one[k] = seq[k];
    draw_char((uint16_t)x, (uint16_t)y, one, 0x0000, 0x0000, SIZE);
}

static int utf8_seq_len(unsigned char b)
{
    if (b >= 0xE0 && b <= 0xEF)
        return 3;
    if (b >= 0xC2 && b <= 0xDF)
        return 2;
    return 1;
}

static int draw_str(int x, int y, const char *s, int SIZE)
{
    while (*s)
    {
        int len = utf8_seq_len((unsigned char)*s);
        render_glyph(x, y, s, len, SIZE);
        for (int k = 0; k < len && *s; k++)
            s++;
        y += CW(SIZE);
    }
    return y;
}
static int draw_func_name(char op, int x, int y, int SIZE)
{
    switch (op)
    {
    case '!':
        return draw_str(x, y, "!", SIZE);
    case 'c':
        return draw_str(x, y, "cos", SIZE);
    case 's':
        return draw_str(x, y, "sin", SIZE);
    case 't':
        return draw_str(x, y, "tan", SIZE);
    case 'u':
        return draw_str(x, y, "acos", SIZE);
    case 'v':
        return draw_str(x, y, "asin", SIZE);
    case 'w':
        return draw_str(x, y, "atan", SIZE);
    case 'f':
        return draw_str(x, y, "cosh", SIZE);
    case 'g':
        return draw_str(x, y, "sinh", SIZE);
    case 'h':
        return draw_str(x, y, "tanh", SIZE);
    case 'a':
        return draw_str(x, y, "acosh", SIZE);
    case 'j':
        return draw_str(x, y, "asinh", SIZE);
    case 'k':
        return draw_str(x, y, "atanh", SIZE);
    case 'b':
        return draw_str(x, y, "conj", SIZE);
    case 'd':
        return draw_str(x, y, "arg", SIZE);
    case 'm':
        return draw_str(x, y, "mod", SIZE);
    case 'l':
        return draw_str(x, y, "ln", SIZE);
    case 'o':
        return draw_str(x, y, "log", SIZE);
    case 'q':
        return draw_str(x, y, "binom", SIZE);
    case 'y':
        return draw_str(x, y, "perm", SIZE);
    case 'P':
        return draw_str(x, y, "pgcd", SIZE);
    case 'L':
        return draw_str(x, y, "ppcm", SIZE);
    case 'D':
        return draw_str(x, y, "quot", SIZE);
    case 'R':
        return draw_str(x, y, "reste", SIZE);
    case 'S':
        return draw_str(x, y, "abs", SIZE);
    case '@':
        return draw_str(x, y, "arith", SIZE);
    case '#':
        return draw_str(x, y, "geom", SIZE);
    case '&':
        return draw_str(x, y, "arigeo", SIZE);
    case '?':
        return draw_str(x, y, "arithn", SIZE);
    case '$':
        return draw_str(x, y, "exp", SIZE);
    case ':':
        return draw_str(x, y, "ent", SIZE);
    case ';':
        return draw_str(x, y, "signe", SIZE);
    case '<':
        return draw_str(x, y, "min", SIZE);
    case '>':
        return draw_str(x, y, "max", SIZE);
    case '%':
        return draw_str(x, y, "racine", SIZE);
    case '`':
        return draw_str(x, y, "alea", SIZE);
    case '|':
        return draw_str(x, y, "randint", SIZE);
    case '{':
        return draw_str(x, y, "somme", SIZE);
    case '}':
        return draw_str(x, y, "produit", SIZE);
    case '_':
        return draw_str(x, y, "dérivée", SIZE);
    case '"':
        return draw_str(x, y, "integrale", SIZE);
    case CAS_TOK_EXPAND:
        return draw_str(x, y, "developper", SIZE);
    case CAS_TOK_FACTOR:
        return draw_str(x, y, "factoriser", SIZE);
    case CAS_TOK_DERIVE:
        return draw_str(x, y, "deriver", SIZE);
    case CAS_TOK_PRIMITIVE:
        return draw_str(x, y, "primitive", SIZE);
    case CAS_TOK_SOLVE:
        return draw_str(x, y, "resoudre", SIZE);
    case 'O':
        return draw_str(x, y, "arrondi", SIZE);
    case 'F':
        return draw_str(x, y, "det", SIZE);
    case 'G':
        return draw_str(x, y, "inv", SIZE);
    case 'H':
        return draw_str(x, y, "trans", SIZE);
    default:
        return y;
    }
}
// Curseur juste apres le caractere source de `nd` (un glyphe de largeur CW).
// Toutes les positions sont placees au bord de cellule - 6 : la barre de 2 px
// tombe dans les 2 colonnes vides du glyphe precedent, sans toucher le suivant.
static void update_cursor(ASTNode *nd, int x, int y, int cursor_pos, int SIZE)
{
    if (nd->src_pos < 0)
        return;
    if (cursor_pos - 1 == nd->src_pos)
        set_cursor(x, y + CW(SIZE) - 6);
}

// Nombre de caracteres du nom d'une fonction dans le texte source : 1 pour les
// touches (c, s, t, r, l, ...) meme si l'ecran affiche "cos", "sqrt"...
static int func_src_name_len(char op)
{
    switch (op)
    {
    case 'l':
    case 'r':
    case 'c':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'f':
    case 'g':
    case 'h':
    case 'j':
    case 'a':
    case 'k':
    case 'b':
    case 'd':
    case 'm':
    case '!':
        return 1;
    default:
        return func_name_width(op, 1) / CW(1);
    }
}

// Curseur dans le nom d'une fonction (avant la parenthese ouvrante).
static void func_cursor_name(const ASTNode *nd, int SIZE, int xc, int y, int cursor_pos)
{
    if (nd->src_pos < 0)
        return;
    int L = func_src_name_len(nd->op);
    if (L == 1)
    {
        // un seul caractere source, nom affiche en entier ("cos") : apres le nom
        if (cursor_pos == nd->src_pos + 1)
            set_cursor(xc, y + func_name_width(nd->op, SIZE) - 6);
    }
    else if (cursor_pos > nd->src_pos && cursor_pos <= nd->src_pos + L)
        set_cursor(xc, y + (cursor_pos - nd->src_pos) * CW(SIZE) - 6);
}

// Source positions of a two-argument call written as name(first,second):
// *lp = '(' index, *cp = separator index, *cl = matching ')' index (-1 when
// the call is not closed yet). Returns false when the source does not hold
// such a call at nd->src_pos. These characters own no AST node, so the
// renderer uses them to place the cursor on the empty parts of the call.
static bool call_positions(const ASTNode *nd, int name_len, int *lp, int *cp, int *cl)
{
    if (!g_src || !nd || nd->src_pos < 0)
        return false;
    int p = nd->src_pos + name_len;
    if (p >= g_src_len || g_src[p] != '(')
        return false;
    *lp = p;
    *cp = -1;
    *cl = -1;
    int depth = 0, bdepth = 0;
    for (int i = p; i < g_src_len; i++)
    {
        char c = g_src[i];
        if (c == '[')
            bdepth++;
        else if (c == ']')
            bdepth--;
        else if (c == '(')
            depth++;
        else if (c == ')')
        {
            depth--;
            if (depth == 0)
            {
                *cl = i;
                break;
            }
        }
        else if (c == ',' && depth == 1 && bdepth == 0 && *cp < 0)
            *cp = i;
    }
    return *cp >= 0;
}

// Tall parenthesis drawn as an elliptical arc: the stroke is flat in the
// middle and curves back with a vertical tangent at the tips, like a real
// bracket. `y` is the reading position, `x0` the transverse start, `len` the
// transverse length. `(` opens towards +y, `)` towards -y.
static void draw_big_paren(int y, int x0, int len, int SIZE, bool open)
{
    int t = SIZE;
    int hook = SIZE * 2;
    int dir = open ? 1 : -1;
    int half = len / 2;
    if (half < 1)
        half = 1;
    for (int xi = 0; xi < len; xi++)
    {
        float nd = (float)(xi - half) / (float)half;
        if (nd < 0.0f)
            nd = -nd;
        if (nd > 1.0f)
            nd = 1.0f;
        // Quarter ellipse: offset = hook * (1 - sqrt(1 - nd^2)).
        float off = hook * (1.0f - sqrtf(1.0f - nd * nd));
        int yoff = (int)(off + 0.5f);
        render_fill_rect(x0 + xi, y + dir * yoff, 1, t, 0x0000);
    }
    // Tips: small nubs turning towards the content.
    int t1 = hook * dir;
    render_fill_rect(x0, y + t1, t, t, 0x0000);
    render_fill_rect(x0 + len - t, y + t1, t, t, 0x0000);
}

/* ---------- Somme, produit, integrale en notation mathematique ----------
 *
 *    b             ⌠b
 *    Σ  f(X)       |   f(X) dX
 *   X=a            ⌡a
 * Disposition (x = axe vertical, vers le haut ; y = sens de lecture) :
 * colonne du symbole puis l'expression. Σ/Π : bornes au-dessus et
 * au-dessous ; integrale : bornes a droite du signe (moins haut, comme la
 * NumWorks), pour tenir dans la ligne de saisie.
 * Un noeud place en x occupe verticalement [x - CH(S), x - CH(S) + h]. */

static int render_node(ASTNode *nd, int x, int y, int SIZE, int cursor_pos);

struct bigop_layout
{
    ASTNode *f, *lo, *hi;
    Dims df, dlo, dhi;
    int eS;      // taille des bornes
    int sym_w;   // largeur du symbole
    int sym_h;   // hauteur du symbole
    int lab_w;   // largeur de "X=" sous Σ/Π (0 pour l'integrale)
    int col_w;   // largeur de la colonne du symbole
    int tail_w;  // " dX" apres une integrale
    int lo_bot;  // bas de la borne basse (depuis le bas du bloc)
    int mid;     // milieu du symbole = ligne de base du bloc
    int gap;
    Dims total;
};

static bool is_bigop(char op) { return op == '{' || op == '}' || op == '"'; }

static bool bigop_layout_of(ASTNode *nd, int SIZE, bigop_layout *L)
{
    if (!nd || nd->type != N_FUNC || !is_bigop(nd->op) || !nd->right ||
        nd->right->type != N_BINOP || nd->right->op != ',')
        return false;
    L->f = nd->left;
    L->lo = nd->right->left;
    L->hi = nd->right->right;
    L->eS = SIZE > 1 ? SIZE - 1 : 1;
    L->gap = SIZE;
    L->df = measure(L->f, SIZE);
    L->dlo = measure(L->lo, L->eS);
    L->dhi = measure(L->hi, L->eS);
    bool integ = nd->op == '"';
    if (integ)
    {
        L->sym_w = 3 * SIZE + 4;
        L->sym_h = L->df.h + 2 * SIZE;
        if (L->sym_h < 2 * CH(SIZE))
            L->sym_h = 2 * CH(SIZE);
        L->lab_w = 0;
        L->tail_w = SIZE * 2 + 2 * CW(SIZE);
    }
    else
    {
        L->sym_w = 5 * (SIZE + 1);
        L->sym_h = 7 * (SIZE + 1);
        L->lab_w = 2 * CW(L->eS);
        L->tail_w = 0;
    }
    if (integ)
    {
        int bw = L->dlo.w > L->dhi.w ? L->dlo.w : L->dhi.w;
        L->col_w = L->sym_w + SIZE + bw + SIZE * 2;
        int half = L->sym_h / 2;
        int off = L->df.baseline > half ? L->df.baseline - half : 0;
        L->lo_bot = off;
        L->mid = half + off;
        int top1 = off + L->sym_h;
        int top2 = L->mid + (L->df.h - L->df.baseline);
        L->total.w = L->col_w + L->df.w + L->tail_w;
        L->total.h = top1 > top2 ? top1 : top2;
        L->total.baseline = L->mid;
        return true;
    }
    int lo_w = L->lab_w + L->dlo.w;
    int cw = L->sym_w;
    if (lo_w > cw)
        cw = lo_w;
    if (L->dhi.w > cw)
        cw = L->dhi.w;
    L->col_w = cw + SIZE * 2;
    int lo_h = L->dlo.h > CH(L->eS) ? L->dlo.h : CH(L->eS);
    int mid = lo_h + L->gap + L->sym_h / 2;
    int off = L->df.baseline > mid ? L->df.baseline - mid : 0;
    L->lo_bot = off;
    L->mid = mid + off;
    int top1 = L->mid + L->sym_h / 2 + L->gap + L->dhi.h;
    int top2 = L->mid + (L->df.h - L->df.baseline);
    L->total.w = L->col_w + L->df.w + L->tail_w;
    L->total.h = top1 > top2 ? top1 : top2;
    L->total.baseline = L->mid;
    return true;
}

static bool bigop_measure(ASTNode *nd, int SIZE, Dims *out)
{
    bigop_layout L;
    if (!bigop_layout_of(nd, SIZE, &L))
        return false;
    *out = L.total;
    return true;
}

// Source positions of a three-argument call name(f,a,b): '(' , ',' , ',' , ')'.
static bool call_positions3(const ASTNode *nd, int name_len, int *lp, int *c1, int *c2, int *cl)
{
    if (!g_src || !nd || nd->src_pos < 0)
        return false;
    int p = nd->src_pos + name_len;
    if (p >= g_src_len || g_src[p] != '(')
        return false;
    *lp = p;
    *c1 = *c2 = *cl = -1;
    int depth = 0, bdepth = 0;
    for (int i = p; i < g_src_len; i++)
    {
        char c = g_src[i];
        if (c == '[')
            bdepth++;
        else if (c == ']')
            bdepth--;
        else if (c == '(')
            depth++;
        else if (c == ')')
        {
            if (--depth == 0)
            {
                *cl = i;
                break;
            }
        }
        else if (c == ',' && depth == 1 && bdepth == 0)
        {
            if (*c1 < 0)
                *c1 = i;
            else if (*c2 < 0)
                *c2 = i;
        }
    }
    return *c1 >= 0 && *c2 >= 0;
}

static int render_bigop(ASTNode *nd, int x, int y, int SIZE, int cursor_pos, const bigop_layout *L)
{
    int B = x - CH(SIZE);            // bas du bloc
    int eS = L->eS;
    bool integ = nd->op == '"';
    int sym_bot = B + L->mid - L->sym_h / 2;
    int sym_y = integ ? y + SIZE : y + (L->col_w - L->sym_w) / 2;

    // Symbole
    if (integ)
    {
        int t = SIZE;
        int cx = sym_y + L->sym_w / 2 - t / 2;
        // trait vertical, crochet en haut a droite, en bas a gauche
        render_fill_rect(sym_bot + 2 * t, cx, L->sym_h - 4 * t, t, 0x0000);
        render_fill_rect(sym_bot + L->sym_h - 2 * t, cx + t, t, t, 0x0000);
        render_fill_rect(sym_bot + L->sym_h - t, cx + t, t, 2 * t, 0x0000);
        render_fill_rect(sym_bot + t, cx - t, t, t, 0x0000);
        render_fill_rect(sym_bot, cx - 2 * t, t, 2 * t, 0x0000);
    }
    else
    {
        const char *g = nd->op == '{' ? "Σ" : "Π";
        // glyphe de 7 lignes : rangee 0 en x, rangee 6 en x - 6*S1
        int S1 = SIZE + 1;
        render_glyph(sym_bot + 6 * S1, sym_y, g, 2, S1);
    }

    // Bornes : au-dessus / au-dessous (Σ, Π), a droite du signe (integrale)
    int lo_w = L->lab_w + L->dlo.w;
    int hi_bot, hi_y, lo_y, lo_x;
    if (integ)
    {
        hi_bot = sym_bot + L->sym_h - L->dhi.h;
        hi_y = sym_y + L->sym_w + SIZE;
        lo_y = hi_y;
        lo_x = sym_bot + CH(eS);
    }
    else
    {
        hi_bot = B + L->mid + L->sym_h / 2 + L->gap;
        hi_y = y + (L->col_w - L->dhi.w) / 2;
        lo_y = y + (L->col_w - lo_w) / 2;
        lo_x = B + L->lo_bot + CH(eS);
    }
    render_node(L->hi, hi_bot + CH(eS), hi_y, eS, cursor_pos);
    if (L->lab_w)
    {
        draw_str(lo_x, lo_y, "X=", eS);
    }
    render_node(L->lo, lo_x, lo_y + L->lab_w, eS, cursor_pos);

    // Expression
    int fy = y + L->col_w;
    int fx = x + (L->mid - L->df.baseline);
    int fend = render_node(L->f, fx, fy, SIZE, cursor_pos);
    if (fend < fy + L->df.w)
        fend = fy + L->df.w;
    if (integ)
        draw_str(x + L->mid - CH(SIZE) / 2, fend + 2 * SIZE, "dX", SIZE);

    // Curseur sur les parties vides ou aux bords des arguments.
    int lp, c1, c2, cl;
    int name_len = func_name_width(nd->op, 1) / CW(1);
    if (nd->src_pos >= 0 && cursor_pos > nd->src_pos && cursor_pos <= nd->src_pos + name_len)
        set_cursor(x + L->mid - CH(SIZE) / 2, y - 6);
    if (call_positions3(nd, name_len, &lp, &c1, &c2, &cl))
    {
        int fxc = x + (L->mid - L->df.baseline);
        if (cursor_pos == lp + 1)
            set_cursor(fxc, fy - 6);
        else if (cursor_pos == c1 && c1 != lp + 1)
            set_cursor(fxc, fy + L->df.w - 6);
        else if (cursor_pos == c1 + 1)
            set_cursor(lo_x, lo_y + L->lab_w - 6);
        else if (cursor_pos == c2 && c2 != c1 + 1)
            set_cursor(lo_x, lo_y + lo_w - 6);
        else if (cursor_pos == c2 + 1)
            set_cursor(hi_bot + CH(eS), hi_y - 6);
        else if (cl >= 0 && cursor_pos == cl && cl != c2 + 1)
            set_cursor(hi_bot + CH(eS), hi_y + L->dhi.w - 6);
        else if (cl >= 0 && cursor_pos == cl + 1)
            set_cursor(fxc, y + L->total.w - 6);
    }
    return y + L->total.w;
}

static int render_node(ASTNode *nd, int x, int y, int SIZE, int cursor_pos)
{
    if (!nd)
        return y;
    Dims d = measure(nd, SIZE);
    char buf[2] = {0, 0};
    char numbuf[32];

    switch (nd->type)
    {

    case N_PLACEHOLDER:
    {
        int w = CW(SIZE), h = CH(SIZE);
        //fill_rect(x - 13, y, 1, w, 0x0000);
        //fill_rect(x + h - 14, y, 1, w, 0x0000);
        //fill_rect(x - 13, y, h, 1, 0x0000);
        //fill_rect(x - 13, y + w - 1, h, 1, 0x0000);
        return y + w;
    }

    case N_NUMBER:
    {
        // Typed numeric literals are drawn from the raw source so that long
        // values keep their exact digits (the double value may be rounded).
        int lit_start, lit_len;
        if (number_source_span(nd, &lit_start, &lit_len))
        {
            int yy = y;
            for (int k = 0; k < lit_len; k++)
            {
                render_char(x, yy, g_src[lit_start + k], SIZE);
                yy += CW(SIZE);
            }
            if (cursor_pos >= lit_start && cursor_pos <= lit_start + lit_len)
                set_cursor(x, y - 6 + (cursor_pos - lit_start) * CW(SIZE));
            return y + d.w;
        }
        // A NaN token comes from an incomplete matrix literal still being
        // typed; draw the raw source characters instead of a bogus number.
        if (std::isnan(nd->number) && g_src && nd->src_pos >= 0 && nd->src_pos < g_src_len)
        {
            int yy = y;
            for (int k = nd->src_pos; k < g_src_len; k++)
            {
                char ch = g_src[k];
                if (!((ch >= '0' && ch <= '9') || ch == '.' || ch == ',' || ch == '[' || ch == ']' || ch == '-'))
                    break;
                if (cursor_pos - 1 == k)
                    set_cursor(x, yy + CW(SIZE) - 6);
                render_char(x, yy, ch, SIZE);
                yy += CW(SIZE);
            }
            return y + d.w;
        }
        int nc = fmt_number(nd->number, numbuf);
        // src_pos is the last digit's index; src_start is the first digit's index.
        int src_start = nd->src_pos - nc + 1;
        if (src_start >= 0 && cursor_pos >= src_start + 1 && cursor_pos <= src_start + nc)
            set_cursor(x, y - 6 + (cursor_pos - src_start) * CW(SIZE));
        return draw_str(x, y, numbuf, SIZE);
    }
    case N_PARENTHESIS:
    {
        update_cursor(nd, x, y, cursor_pos, SIZE);
        if (nd->op == ')')
        {
            buf[0] = ')';
            render_char(x, y, buf[0], SIZE);
            return y + CW(SIZE);
        }
        Dims c = measure(nd->left, SIZE);
        int xc = x + (c.h - CH(SIZE)) / 2;
        buf[0] = '(';
        render_char(xc, y, buf[0], SIZE);
        int cy = render_node(nd->left, x, y + CW(SIZE), SIZE, cursor_pos);
        if (nd->right)
        {
            buf[0] = ')';
            render_char(xc, cy, buf[0], SIZE);
            cy += CW(SIZE);
            if (nd->src_close >= 0 && cursor_pos == nd->src_close + 1)
                set_cursor(xc, cy - 6);
        }
        return cy;
    }

    case N_VARIABLE:
    {
        update_cursor(nd, x, y, cursor_pos, SIZE);
        buf[0] = nd->variable;
        render_char(x, y, buf[0], SIZE);
        return y + CW(SIZE);
    }

    case N_CONST:
    {
        update_cursor(nd, x, y, cursor_pos, SIZE);
        if (nd->op == 'z')
        {
            if (nd->src_pos >= 0 && cursor_pos > nd->src_pos && cursor_pos <= nd->src_pos + 3)
                set_cursor(x, y + (cursor_pos - nd->src_pos) * CW(SIZE) - 6);
            return draw_str(x, y, "Ans", SIZE);
        }
        buf[0] = nd->op;
        render_char(x, y, buf[0], SIZE);
        return y + CW(SIZE);
    }

    case N_BINOP:
    {

        if (nd->op == '/')
        {
            Dims num = measure(nd->left, SIZE);
            Dims den = measure(nd->right, SIZE);
            int bw = d.w;
            if (nd->src_pos >= 0 && cursor_pos == nd->src_pos + 1)
                set_cursor(x, y + (bw - den.w) / 2 - 6); // debut du denominateur
            render_node(nd->right, x, y + (bw - den.w) / 2, SIZE, cursor_pos);
            render_fill_rect(x + den.h - SIZE * 7 + 3, y, 2, bw, 0x0000);
            render_node(nd->left, x + den.h + 5, y + (bw - num.w) / 2, SIZE, cursor_pos);
            return y + bw;
        }
        else if (nd->op == '^')
        {
            int eS = SIZE > 1 ? SIZE - 1 : 1;
            Dims b = measure(nd->left, SIZE);
            Dims e = measure(nd->right, eS);
            if (nd->src_pos >= 0 && cursor_pos == nd->src_pos + 1)
                set_cursor(x + 10, y + b.w - 6); // debut de l'exposant
            render_node(nd->left, x + e.h / 2, y, SIZE, cursor_pos);
            render_node(nd->right, x + 10, y + b.w, eS, cursor_pos);
            return y + d.w;
        }
        else
        {
            Dims l = measure(nd->left, SIZE);
            Dims r = measure(nd->right, SIZE);
            int h = d.h;
            int baseline = d.baseline;
            bool implicit = (nd->op == '*' && nd->src_pos < 0);
            int cy = render_node(nd->left, x + (baseline - l.baseline), y, SIZE, cursor_pos);
            if (!implicit)
            {
                update_cursor(nd, x + (baseline - CH(SIZE) / 2), cy, cursor_pos, SIZE);
                buf[0] = nd->op;
                render_char(x + (baseline - CH(SIZE) / 2), cy, buf[0], SIZE);
                cy += CW(SIZE);
            }
            return render_node(nd->right, x + (baseline - r.baseline), cy, SIZE, cursor_pos);
        }
    }

    case N_FUNC:
    {
        {
            bigop_layout L;
            if (bigop_layout_of(nd, SIZE, &L))
                return render_bigop(nd, x, y, SIZE, cursor_pos, &L);
        }
        if (nd->op == 'r')
        {
            Dims c = measure(nd->left, SIZE);
            buf[0] = 127;

            render_char(x, y, buf[0], SIZE);
            int cy = y + CW(SIZE) + 2;

            render_fill_rect(x + 2, cy - 5, 3 + (c.h / SIZE - 7) * SIZE, SIZE, 0x0000);
            render_fill_rect(x + 3 + (c.h / SIZE - 7) * SIZE, cy - 5, 2, c.w + 5, 0x0000);

            int lp = -1, cl = -1;
            bool have = call_positions1(nd, 1, &lp, &cl);
            if (nd->src_pos >= 0 && cursor_pos == nd->src_pos + 1)
                set_cursor(x, y + CW(SIZE) - 6); // apres le signe racine
            if (have && cursor_pos == lp + 1)
                set_cursor(x, cy - 6); // debut du contenu
            int sets0 = g_cursor_sets;
            render_node(nd->left, x , cy, SIZE, cursor_pos);
            if (have && cl >= 0 && g_cursor_sets == sets0 && ((cursor_pos == cl && cl != lp + 1) || cursor_pos == cl + 1))
                set_cursor(x, cy + c.w - 6); // fin du contenu / apres ')'
            return cy + c.w;
        }
        else if (nd->op == '!')
        {
            int cy = render_node(nd->left, x, y, SIZE, cursor_pos);
            buf[0] = '!';
            render_char(x, cy, buf[0], SIZE);
            if (nd->src_pos >= 0 && cursor_pos == nd->src_pos + 1)
                set_cursor(x, cy + CW(SIZE) - 6);
            return cy + CW(SIZE);
        }
        else if ((nd->op == 'q' || nd->op == 'y') && nd->right)
        {
            // Traditional stacked notation. Reading axis is `y` (advances
            // rightward), transverse axis is `x` (larger = up).
            Dims top = measure(nd->left, SIZE);
            Dims bot = measure(nd->right, SIZE);
            int gap = 2 * SIZE;
            if (nd->op == 'q')
            {
                int pd = 4 * SIZE;
                int inner_w = top.w > bot.w ? top.w : bot.w;
                int inner_h = top.h + bot.h + gap;
                int xc = x; // content occupies transverse x..x+inner_h
                // The arguments are drawn one character height below the
                // block's logical top; lower the parentheses by the same
                // amount so they stay centred on the stacked arguments.
                int px = xc - CH(SIZE);
                if (nd->src_pos >= 0 && cursor_pos > nd->src_pos && cursor_pos <= nd->src_pos + func_src_name_len('q'))
                    set_cursor(px + inner_h / 2, y + pd - 6);
                draw_big_paren(y, px, inner_h, SIZE, true);
                // top argument (numerically upper => larger x)
                render_node(nd->left, xc + bot.h + gap, y + pd + (inner_w - top.w) / 2, SIZE, cursor_pos);
                // bottom argument
                render_node(nd->right, xc, y + pd + (inner_w - bot.w) / 2, SIZE, cursor_pos);
                draw_big_paren(y + d.w - pd, px, inner_h, SIZE, false);
                {
                    int lp = -1, cp = -1, cl = -1;
                    if (call_positions(nd, func_name_width(nd->op, SIZE) / CW(SIZE), &lp, &cp, &cl))
                    {
                        if (cursor_pos == lp + 1)
                            set_cursor(xc + bot.h + gap, y + pd + (inner_w - top.w) / 2 - 6);
                        else if (cursor_pos == cp + 1)
                            set_cursor(xc, y + pd + (inner_w - bot.w) / 2 - 6);
                        else if (cl >= 0 && cursor_pos == cl + 1)
                            set_cursor(px + inner_h / 2, y + d.w - 6);
                    }
                }
                return y + d.w;
            }
            // perm: A followed by (top exponent) over (bottom exponent)
            int inner_h = top.h + bot.h + gap;
            int x0 = x + (d.h - inner_h) / 2;
            if (nd->src_pos >= 0 && cursor_pos > nd->src_pos && cursor_pos <= nd->src_pos + func_src_name_len('y'))
                set_cursor(x + CH(SIZE) / 2, y + CW(SIZE) - 6);
            render_char(x + (d.h - CH(SIZE)) / 2, y, 'A', SIZE);
            int ey = y + CW(SIZE) + SIZE;
            render_node(nd->left, x0 + bot.h + gap, ey, SIZE, cursor_pos);
            render_node(nd->right, x0, ey, SIZE, cursor_pos);
            {
                int lp = -1, cp = -1, cl = -1;
                if (call_positions(nd, func_name_width(nd->op, SIZE) / CW(SIZE), &lp, &cp, &cl))
                {
                    if (cursor_pos == lp + 1)
                        set_cursor(x0 + bot.h + gap, ey - 6);
                    else if (cursor_pos == cp + 1)
                        set_cursor(x0, ey - 6);
                    else if (cl >= 0 && cursor_pos == cl + 1)
                        set_cursor(x + CH(SIZE) / 2, y + d.w - 6);
                }
            }
            return y + d.w;
        }
        else if (is_call2_op(nd->op) && nd->right)
        {
            Dims l = measure(nd->left, SIZE);
            Dims r = measure(nd->right, SIZE);
            int maxh = l.h > r.h ? l.h : r.h;
            int h = d.h;
            int cy = draw_func_name(nd->op, x + (h - CH(SIZE)) / 2, y, SIZE);
            int xc = x + (h - maxh) / 2;
            int lp = -1, cp = -1, cl = -1;
            bool have = call_positions(nd, func_name_width(nd->op, SIZE) / CW(SIZE), &lp, &cp, &cl);
            // Cursor inside the name.
            if (have && cursor_pos > nd->src_pos && cursor_pos <= lp)
                set_cursor(xc, y + (cursor_pos - nd->src_pos) * CW(SIZE) - 6);
            if ((int)nd->number % 2 == 0)
            {
                buf[0] = '(';
                render_char(xc, cy, buf[0], SIZE);
                cy += CW(SIZE);
            }
            // Cursor just after '(' (start of the first argument, even if empty).
            if (have && cursor_pos == lp + 1)
                set_cursor(xc, cy - 6);
            int sets_a = g_cursor_sets;
            cy = render_node(nd->left, xc + (maxh - l.h) / 2, cy, SIZE, cursor_pos);
            // Cursor at the end of a non-empty first argument (sauf si un noeud
            // enfant l'a deja place : fraction, exposant...).
            if (have && cp != lp + 1 && cursor_pos == cp && g_cursor_sets == sets_a)
                set_cursor(xc, cy - 6);
            buf[0] = ',';
            render_char(xc, cy, buf[0], SIZE);
            cy += CW(SIZE);
            // Cursor just after ',' (start of the second argument, even if empty).
            if (have && cursor_pos == cp + 1)
                set_cursor(xc, cy - 6);
            int sets_b = g_cursor_sets;
            cy = render_node(nd->right, xc + (maxh - r.h) / 2, cy, SIZE, cursor_pos);
            // Cursor at the end of a non-empty second argument.
            if (have && cl >= 0 && cl != cp + 1 && cursor_pos == cl && g_cursor_sets == sets_b)
                set_cursor(xc, cy - 6);
            if ((int)nd->number % 3 == 0)
            {
                buf[0] = ')';
                render_char(xc, cy, buf[0], SIZE);
                // Cursor just after ')'.
                if (have && cl >= 0 && cursor_pos == cl + 1)
                    set_cursor(xc, cy + CW(SIZE) - 6);
            }
            return cy + CW(SIZE);
        }
        else
        {
            Dims c = measure(nd->left, SIZE);
            int h = d.h;
            int cy = draw_func_name(nd->op, x + (h - CH(SIZE)) / 2, y, SIZE);
            int xc = x + (h - c.h) / 2;
            // Blocs a un parametre (arith, geom) : le curseur doit pouvoir
            // se placer meme quand le parametre est vide.
            int lp = -1, cl = -1;
            bool blk = call_positions1(nd, func_src_name_len(nd->op), &lp, &cl);
            func_cursor_name(nd, SIZE, xc, y, cursor_pos);
            if ((int)nd->number % 2 == 0)
            {
                buf[0] = '(';
                render_char(xc, cy, buf[0], SIZE);
                cy += CW(SIZE);
            }
            if (blk && cursor_pos == lp + 1)
                set_cursor(xc, cy - 6);
            int sets_c = g_cursor_sets;
            cy = render_node(nd->left, xc, cy, SIZE, cursor_pos);
            if (blk && cl >= 0 && cl != lp + 1 && cursor_pos == cl && g_cursor_sets == sets_c)
                set_cursor(xc, cy - 6);
            if ((int)nd->number % 3 == 0)
            {
                buf[0] = ')';
                render_char(xc, cy, buf[0], SIZE);
                if (blk && cl >= 0 && cursor_pos == cl + 1)
                    set_cursor(xc, cy + CW(SIZE) - 6);
            }
            return cy + CW(SIZE);
        }
    }
    case N_NEG:
    {
        int xo = x + (d.baseline - CH(SIZE) / 2);
        update_cursor(nd, xo, y, cursor_pos, SIZE);
        render_char(xo, y, '-', SIZE);
        return render_node(nd->left, x, y + CW(SIZE), SIZE, cursor_pos);
    }
    case N_MATRIX:
    {
        int span = matrix_span(g_src, g_src_len, nd->src_pos);
        if (span > 0)
        {
            matrix_lit L;
            if (matrix_parse_literal(g_src, g_src_len, nd->src_pos, &L))
                return render_matrix_grid(&L, x, y, SIZE, cursor_pos);
            int yy = y;
            for (int k = 0; k < span; k++)
            {
                char ch = g_src[nd->src_pos + k];
                if (cursor_pos - 1 == nd->src_pos + k)
                    set_cursor(x, yy + CW(SIZE) - 6);
                render_char(x, yy, ch, SIZE);
                yy += CW(SIZE);
            }
            return y + d.w;
        }
        update_cursor(nd, x, y, cursor_pos, SIZE);
        char buf[128];
        mat_to_string(nd->mat, buf, 128);
        return draw_str(x, y, buf, SIZE);
    }
    }
    return y + d.w;
}

#ifdef OPENCALC_WASM
static void _dbg_ast(const ASTNode *nd, int d)
{
    (void)nd;
    (void)d;
}
#else
static void _dbg_ast(const ASTNode *, int) {}
#endif

void display_equation(char *in, int input_size, int x, int y, int SIZE, int cursor_pos, bool left, int scroll, int clip_lo, int clip_hi)
{
    int prev_lo = g_clip_lo;
    int prev_hi = g_clip_hi;
    x += 10;
    reset_pool();
    g_src = in;
    g_src_len = input_size;
    g_blink.active = 0;
    g_cursor_found = false;
    g_clip_lo = clip_lo;
    g_clip_hi = clip_hi;
    if (!in || input_size == 0)
    {
        // Ligne vide (nouvelle fonction, nouvelle ligne de calcul) : le curseur
        // est au debut de la ligne. Sans ca il gardait l'ancienne position
        // (celle de la ligne precedente) jusqu'a la premiere frappe.
        if (cursor_pos >= 0 && !left)
            set_cursor(x + 14, y + 5 - scroll - 6);
        g_clip_lo = prev_lo;
        g_clip_hi = prev_hi;
        return;
    }

    int tok_n = 0;
    token *toks = parse_string_to_token(in, input_size, &tok_n);
    Parser p = {toks, tok_n, 0};
    int cy = y + 5 - scroll;

    while (p.pos < p.n)
    {
        int before = p.pos;
        if(!left){
            ASTNode *node = parse_equation(&p);
            if (node) {
                _dbg_ast(node, 0);
                cy = render_node(node, x + 14, cy, SIZE, cursor_pos);
            }
            if (p.pos == before)
                p.pos++;
        }else{
            ASTNode *node = parse_equation(&p);
            Dims d = measure(node,SIZE);
            if (node)
                cy = render_node(node, x + 14, 320-d.w-5-scroll, SIZE, cursor_pos);
            if (p.pos == before)
                p.pos++;
        }
    }

    // Aucun noeud ne porte cette position (debut de ligne, appel vide...) : on
    // ne garde jamais l'ancienne position du curseur, qui pourrait etre sur une
    // autre ligne. Debut de ligne pour 0, sinon fin de la ligne.
    if (cursor_pos >= 0 && !g_cursor_found && !left)
    {
        if (cursor_pos == 0)
            set_cursor(x + 14, y + 5 - scroll - 6);
        else
            set_cursor(x + 14, cy - 6);
    }

    token_free_matrices(toks, tok_n);
    free(toks);
    g_clip_lo = prev_lo;
    g_clip_hi = prev_hi;
}

bool display_get_cursor(int *y)
{
    if (!g_cursor_found)
        return false;
    if (y)
        *y = y_cursor;
    return true;
}

void blink_cursor()
{
    if (g_blink.active)
    {
        if (to_ms_since_boot(get_absolute_time()) % 1500 < 750)
        {
            fill_rect(g_blink.bar_x, g_blink.bar_y, g_blink.bar_h, g_blink.bar_w, 0x0000);
        }
        else
        {
            fill_rect(g_blink.bar_x, g_blink.bar_y, g_blink.bar_h, g_blink.bar_w, g_blink.bg);
            fill_rect(g_blink.cell_x - 2, g_blink.cell_y - 2, 2, g_blink.cell_w + 4, 0x0000);
            fill_rect(g_blink.cell_x + g_blink.cell_h, g_blink.cell_y - 2, 2, g_blink.cell_w + 4, 0x0000);
            fill_rect(g_blink.cell_x - 2, g_blink.cell_y - 2, g_blink.cell_h + 4, 1, 0x0000);
            fill_rect(g_blink.cell_x - 2, g_blink.cell_y + g_blink.cell_w, g_blink.cell_h + 4, 1, 0x0000);
        }
        flush_display();
        return;
    }
    static char tmp[2] = {'|', '\0'};
    if (to_ms_since_boot(get_absolute_time()) % 1500 < 750)
    {
        draw_char(x_cursor, y_cursor, tmp, 0x0000, g_cursor_bg, 2);
    }
    else
    {
        // Efface exactement la barre dessinee par draw_char('|', taille 2) :
        // lignes x-12..x+1, colonnes y+4..y+5 (rien en dehors, donc aucun
        // caractere voisin n'est rogne quand le curseur est eteint).
        int rx = x_cursor - 12;
        if (rx < 0)
            rx = 0;
        fill_rect(rx, y_cursor + 4, x_cursor + 2 - rx, 2, g_cursor_bg);
    }
    flush_display();
}