/* Boite a outils d'interface des apps Statistiques / Probabilites.
 * Voir headers/ui_kit.h pour la convention de coordonnees. */
#include "ui_kit.h"
#include "ui.h"
#include "widgets.h"
#include "colors.h"
#include "Evaluator.h"
#include "Tools.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_decimals;

/* ------------------------------- Dessin ---------------------------------- */

void uk_rect(int X, int Y, int W, int H, uint16_t color)
{
    if (W <= 0 || H <= 0)
        return;
    /* Rognage a l'ecran (fill_rect rejette les coordonnees negatives). */
    if (X < 0)
    {
        W += X;
        X = 0;
    }
    if (Y < 0)
    {
        H += Y;
        Y = 0;
    }
    if (X + W > SCREEN_WIDTH)
        W = SCREEN_WIDTH - X;
    if (Y + H > SCREEN_HEIGHT)
        H = SCREEN_HEIGHT - Y;
    if (W <= 0 || H <= 0)
        return;
    fill_rect((uint16_t)(SCREEN_HEIGHT - Y - H), (uint16_t)X, (uint16_t)H, (uint16_t)W, color);
}

void uk_pixel(int X, int Y, uint16_t color)
{
    if (X < 0 || Y < 0 || X >= SCREEN_WIDTH || Y >= SCREEN_HEIGHT)
        return;
    fill_rect((uint16_t)(SCREEN_HEIGHT - 1 - Y), (uint16_t)X, 1, 1, color);
}

void uk_frame(int X, int Y, int W, int H, uint16_t color)
{
    uk_rect(X, Y, W, 1, color);
    uk_rect(X, Y + H - 1, W, 1, color);
    uk_rect(X, Y, 1, H, color);
    uk_rect(X + W - 1, Y, 1, H, color);
}

void uk_line(int X0, int Y0, int X1, int Y1, uint16_t color)
{
    /* Segments tres longs (asymptotes...) : on ne trace que l'utile. */
    if ((X0 < -2000 || X0 > 2000 || Y0 < -2000 || Y0 > 2000) ||
        (X1 < -2000 || X1 > 2000 || Y1 < -2000 || Y1 > 2000))
        return;
    int dx = abs(X1 - X0), sx = X0 < X1 ? 1 : -1;
    int dy = -abs(Y1 - Y0), sy = Y0 < Y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 5000; guard++)
    {
        uk_pixel(X0, Y0, color);
        if (X0 == X1 && Y0 == Y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            X0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            Y0 += sy;
        }
    }
}

void uk_text(int X, int Y, const char *s, uint16_t fg, int size)
{
    if (!s || Y < 0 || Y >= SCREEN_HEIGHT)
        return;
    /* draw_char s'arrete au premier caractere hors police : on copie en
     * remplacant les caracteres inconnus par '?'. */
    char buf[64];
    int n = 0;
    for (; s[n] && n < 63; n++)
        buf[n] = (s[n] >= 32 && s[n] <= 126) ? s[n] : '?';
    buf[n] = '\0';
    draw_char((uint16_t)(SCREEN_HEIGHT - 1 - Y), (uint16_t)(X < 0 ? 0 : X), buf, fg, 0, (uint8_t)size);
}

int uk_text_w(const char *s, int size)
{
    return s ? utf8_glyph_count(s, (int)strlen(s)) * 5 * size : 0;
}

void uk_text_center(int X, int Y, int W, const char *s, uint16_t fg, int size)
{
    int w = uk_text_w(s, size);
    uk_text(X + (W - w) / 2, Y, s, fg, size);
}

void uk_text_right(int X_right, int Y, const char *s, uint16_t fg, int size)
{
    uk_text(X_right - uk_text_w(s, size), Y, s, fg, size);
}

void uk_header(const char *title)
{
    uk_rect(0, 0, SCREEN_WIDTH, UK_HEADER_H, UK_HEADER_BG);
    uk_text_center(0, 5, SCREEN_WIDTH, title, COLOR_WHITE, 1);
}

void uk_tabs(const char **names, int n, int sel, bool focus)
{
    int y = UK_HEADER_H;
    uk_rect(0, y, SCREEN_WIDTH, UK_TABS_H, UK_TAB_BG);
    int w = SCREEN_WIDTH / n;
    for (int i = 0; i < n; i++)
    {
        int x = i * w;
        int ww = (i == n - 1) ? SCREEN_WIDTH - x : w;
        if (i == sel)
            uk_rect(x, y, ww, UK_TABS_H, focus ? UK_TAB_FOCUS : UK_TAB_SEL);
        uk_text_center(x, y + 5, ww, names[i], COLOR_BLACK, 1);
        if (i > 0)
            uk_rect(x, y + 3, 1, UK_TABS_H - 6, COLOR_GRAY);
    }
    uk_rect(0, y + UK_TABS_H - 1, SCREEN_WIDTH, 1, COLOR_GRAY);
}

void uk_footer(const char *hint)
{
    int y = SCREEN_HEIGHT - UK_FOOTER_H;
    uk_rect(0, y, SCREEN_WIDTH, UK_FOOTER_H, FRONTGROUND_COLOR_BIS);
    uk_text(4, y + 3, hint, 0x4208, 1);
}

/* ------------------------------ Nombres ---------------------------------- */

/* "1.5e+06" -> "1.5e6", "2e-05" -> "2e-5". */
static void tidy_exponent(char *s)
{
    char *e = strchr(s, 'e');
    if (!e)
        return;
    char *src = e + 1, *dst = e + 1;
    if (*src == '+')
        src++;
    else if (*src == '-')
        *dst++ = *src++;
    while (*src == '0' && src[1])
        src++;
    while (*src)
        *dst++ = *src++;
    *dst = '\0';
}

int uk_fmt(double v, char *buf, int maxch)
{
    if (maxch > 31)
        maxch = 31;
    if (isnan(v))
    {
        strcpy(buf, "--");
        return 2;
    }
    if (isinf(v))
    {
        strcpy(buf, v > 0 ? "inf" : "-inf");
        return (int)strlen(buf);
    }
    int d = g_decimals < 0 ? 0 : (g_decimals > 9 ? 9 : g_decimals);
    if (fabs(v) >= 1e-3 && fabs(v) < 1e9)
    {
        double f = pow(10.0, d);
        double r = round(v * f) / f;
        if (r != 0 || v == 0)
            v = r;
    }
    if (fabs(v) < 1e-13)
        v = 0;
    char tmp[40];
    for (int p = 10; p >= 1; p--)
    {
        snprintf(tmp, sizeof(tmp), "%.*g", p, v);
        tidy_exponent(tmp);
        if ((int)strlen(tmp) <= maxch)
            break;
    }
    strncpy(buf, tmp, maxch);
    buf[maxch] = '\0';
    return (int)strlen(buf);
}

bool uk_eval(const char *text, int len, double *out)
{
    if (!text || len <= 0)
        return false;
    int n = 0;
    token *t = parse_string_to_token((char *)text, len, &n);
    if (!t || n == 0)
    {
        free(t);
        return false;
    }
    token *o = shunting_yard(t, n);
    int y = count_yarded(t, n);
    complex c = evaluate_npi_c(o, y);
    token_free_matrices(t, n);
    free(t);
    free(o);
    if (!isfinite(c.re) || fabs(c.im) > 1e-9 * (fabs(c.re) > 1 ? fabs(c.re) : 1))
        return false;
    *out = c.re;
    return true;
}

/* ------------------------------- Clavier --------------------------------- */

/* Le curseur clignotant n'a de sens que dans une zone de saisie : ailleurs,
 * blink_cursor() redessinerait le dernier curseur connu par-dessus la page. */
static bool s_input_active = false;

int uk_wait_key(void)
{
    while (true)
    {
        if (s_input_active)
            blink_cursor();
        int ch = scan_text_char();
        if (ch >= 32 && ch <= 126)
        {
            scan_keypad_nowait(); /* la touche correspondante est deja traitee */
            return -ch;
        }
        int k = scan_keypad();
        if (k != -1)
            return k;
    }
}

bool uk_is_entry_key(int key)
{
    if (key < 0)
    {
        int c = -key;
        return (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '(';
    }
    switch (key)
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
    case COMA:
    case MINUS:
    case PI:
    case SQRT:
    case OPENING_PARENTHESIS:
    case E:
    case LN:
    case LOG:
    case ANS:
        return true;
    default:
        return false;
    }
}

/* -------------------------------- Menus ---------------------------------- */

int uk_menu(const char *title, const char **labels, int n, int sel)
{
    const int row_h = 24, top = UK_HEADER_H + 4;
    const int vis = (SCREEN_HEIGHT - top - UK_FOOTER_H) / row_h;
    if (sel < 0 || sel >= n)
        sel = 0;
    int first = sel >= vis ? sel - vis + 1 : 0;
    while (true)
    {
        fill_screen(BACKGROUND_COLOR);
        uk_header(title);
        for (int i = 0; i < vis && first + i < n; i++)
        {
            int idx = first + i;
            int y = top + i * row_h;
            uint16_t bg = idx == sel ? UK_SEL_BG : (idx % 2 ? FRONTGROUND_COLOR_BIS : FRONTGROUND_COLOR);
            uk_rect(0, y, SCREEN_WIDTH, row_h - 2, bg);
            uk_text(12, y + 4, labels[idx], COLOR_BLACK, 2);
        }
        uk_footer("OK: choisir   BACK: retour");
        flush_display();
        int k = uk_wait_key();
        if (k == UP)
        {
            sel = (sel - 1 + n) % n;
        }
        else if (k == DOWN)
        {
            sel = (sel + 1) % n;
        }
        else if (k == OK || k == ENTER)
            return sel;
        else if (k == BACK)
            return -1;
        if (sel < first)
            first = sel;
        if (sel >= first + vis)
            first = sel - vis + 1;
    }
}

void uk_message(const char *title, const char *line1, const char *line2)
{
    uk_rect(20, 70, 280, 100, COLOR_WHITE);
    uk_frame(20, 70, 280, 100, COLOR_BLACK);
    uk_rect(21, 71, 278, 18, UK_HEADER_BG);
    uk_text_center(20, 76, 280, title, COLOR_WHITE, 1);
    if (line1)
        uk_text_center(20, 104, 280, line1, COLOR_BLACK, 1);
    if (line2)
        uk_text_center(20, 120, 280, line2, COLOR_BLACK, 1);
    uk_text_center(20, 152, 280, "OK", COLOR_GRAY, 1);
    flush_display();
    while (true)
    {
        int k = uk_wait_key();
        if (k == OK || k == ENTER || k == BACK)
            return;
    }
}

static void insert_char(fill_box *fb, char c)
{
    if (fb->t_size >= 990)
        return;
    for (int i = fb->t_size; i > fb->curso_pos; i--)
        fb->text[i] = fb->text[i - 1];
    fb->text[fb->curso_pos] = c;
    fb->t_size++;
    fb->curso_pos++;
    fb->text[fb->t_size] = '\0';
}

bool uk_input(const char *title, const char *prompt, double *val, int first_key)
{
    const int BX = 10, BY = 60, BW = 300, BH = 120;
    /* Zone de saisie : coordonnees de fill_box (x = colonne, y = depuis le bas). */
    const int FY = 60 + 52, FH = 36;
    fill_box *fb = create_fill_box(BX + 6, SCREEN_HEIGHT - FY - FH, FH, BW - 12, 2);
    fb->color = FRONTGROUND_COLOR;
    bool snd = false;
    bool error = false;
    int key = first_key;
    bool result = false;
    s_input_active = true;
    while (true)
    {
        if (key != -1)
        {
            if (key < 0)
            {
                insert_char(fb, (char)(-key));
                error = false;
            }
            else if (key == SECOND)
                toggle(&snd);
            else if (key == OK || key == ENTER)
            {
                double v;
                if (uk_eval(fb->text, fb->t_size, &v))
                {
                    *val = v;
                    result = true;
                    break;
                }
                error = true;
            }
            else if (key == BACK && fb->t_size == 0)
                break;
            else if (key == LEFT)
                fill_box_left(fb);
            else if (key == RIGHT)
                fill_box_right(fb);
            else if (key != UP && key != DOWN && key != HOME)
            {
                update_fill_box(fb, key, snd);
                error = false;
            }
        }
        uk_rect(BX, BY, BW, BH, COLOR_WHITE);
        uk_frame(BX, BY, BW, BH, COLOR_BLACK);
        uk_rect(BX + 1, BY + 1, BW - 2, 18, UK_HEADER_BG);
        uk_text_center(BX, BY + 6, BW, title, COLOR_WHITE, 1);
        uk_text(BX + 8, BY + 30, prompt, COLOR_BLACK, 1);
        display_fill_box(fb, 0, true, -1, ' ', true);
        if (error)
            uk_text(BX + 8, BY + 98, "Valeur invalide", COLOR_RED, 1);
        else
            uk_text(BX + 8, BY + 98, snd ? "[2nd]  OK: valider  BACK: annuler" : "OK: valider   BACK: effacer/annuler", COLOR_GRAY, 1);
        flush_display();
        key = uk_wait_key();
    }
    s_input_active = false;
    free(fb->text);
    free(fb);
    return result;
}

/* -------------------------------- Icones --------------------------------- */

void uk_draw_icon(int id, int X, int Y)
{
    const int S = 79;
    switch (id)
    {
    case UK_ICON_STATS:
    {
        uk_rect(X, Y, S, S, 0xe73c);
        static const int hs[5] = {22, 40, 58, 34, 48};
        static const uint16_t cs[5] = {0x3d7f, 0x2c9f, 0x1b5f, 0x3d7f, 0x2c9f};
        for (int i = 0; i < 5; i++)
        {
            int bx = X + 9 + i * 13;
            uk_rect(bx, Y + 70 - hs[i], 10, hs[i], cs[i]);
        }
        uk_rect(X + 6, Y + 70, 68, 2, COLOR_BLACK);
        uk_rect(X + 6, Y + 8, 2, 64, COLOR_BLACK);
        /* droite de regression */
        uk_line(X + 10, Y + 54, X + 72, Y + 14, COLOR_RED);
        uk_line(X + 10, Y + 55, X + 72, Y + 15, COLOR_RED);
        break;
    }
    case UK_ICON_PROBA:
    {
        uk_rect(X, Y, S, S, 0xe73c);
        int prev_y = -1;
        for (int i = 0; i < 70; i++)
        {
            double t = (i - 35) / 11.0;
            int h = (int)(54 * exp(-t * t / 2));
            int px = X + 5 + i;
            if (i >= 28 && i <= 50)
                uk_rect(px, Y + 68 - h, 1, h, 0xfd20);
            int py = Y + 68 - h;
            if (prev_y >= 0)
                uk_line(px - 1, prev_y, px, py, 0x001f);
            prev_y = py;
        }
        uk_rect(X + 4, Y + 68, 72, 2, COLOR_BLACK);
        /* de */
        uk_rect(X + 55, Y + 6, 18, 18, COLOR_WHITE);
        uk_frame(X + 55, Y + 6, 18, 18, COLOR_BLACK);
        uk_rect(X + 58, Y + 9, 3, 3, COLOR_BLACK);
        uk_rect(X + 63, Y + 14, 3, 3, COLOR_BLACK);
        uk_rect(X + 67, Y + 19, 3, 3, COLOR_BLACK);
        break;
    }
    case UK_ICON_TAMAGOTCHI:
    {
        uk_rect(X, Y, S, S, 0xe73c);
        uk_rect(X + 14, Y + 12, 51, 56, 0xfe60);
        uk_frame(X + 14, Y + 12, 51, 56, COLOR_BLACK);
        uk_rect(X + 26, Y + 28, 7, 9, COLOR_BLACK);
        uk_rect(X + 46, Y + 28, 7, 9, COLOR_BLACK);
        uk_line(X + 28, Y + 50, X + 34, Y + 55, COLOR_BLACK);
        uk_line(X + 34, Y + 55, X + 45, Y + 55, COLOR_BLACK);
        uk_line(X + 45, Y + 55, X + 51, Y + 50, COLOR_BLACK);
        break;
    }
    }
}
