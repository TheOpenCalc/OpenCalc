#include "Tools.h"
#include "pico/stdlib.h"
#include <string.h>

/* Fausse XIP pour les tests hôte : région mémoire propre sur laquelle les
 * stubs flash_range_erase/program opèrent, couvrant tout le plan flash
 * (examen 0xFF000, réglages 0x100000, fichiers py 0x101000). */
static uint8_t s_fake_xip[0x110000];
uint8_t *opencalc_xip_base = s_fake_xip;

void core1_flash_begin(void) {}
void core1_flash_end(void) {}

static const int CAP = 512;
static int g_chars[CAP], g_keys[CAP];
static int ch_h = 0, ch_t = 0, ky_h = 0, ky_t = 0;

void pyhost_send_char(int c)
{
    if ((ch_t + 1) % CAP != ch_h) {
        g_chars[ch_t] = c;
        ch_t = (ch_t + 1) % CAP;
    }
}

void pyhost_send_key(int k)
{
    if ((ky_t + 1) % CAP != ky_h) {
        g_keys[ky_t] = k;
        ky_t = (ky_t + 1) % CAP;
    }
}

void pyhost_drain(void)
{
    ch_h = ch_t;
    ky_h = ky_t;
}

int scan_text_char(void)
{
    if (ch_h == ch_t)
        return -1;
    int c = g_chars[ch_h];
    ch_h = (ch_h + 1) % CAP;
    return c;
}

int scan_keypad_nowait(void)
{
    if (ky_h == ky_t)
        return -1;
    int k = g_keys[ky_h];
    ky_h = (ky_h + 1) % CAP;
    return k;
}

int scan_keypad(void)
{
    return scan_keypad_nowait();
}

/* Module Python "draw" : les primitives sont enregistrees dans un journal
 * texte que test_python peut verifier. */
#include <stdio.h>
char g_draw_log[4096];
static int g_draw_log_len = 0;
static void draw_log(const char *s)
{
    int n = (int)strlen(s);
    if (g_draw_log_len + n + 1 < (int)sizeof(g_draw_log))
    {
        memcpy(g_draw_log + g_draw_log_len, s, n);
        g_draw_log_len += n;
        g_draw_log[g_draw_log_len++] = ';';
        g_draw_log[g_draw_log_len] = '\0';
    }
}
extern "C" void oc_draw_begin(void) { draw_log("begin"); }
extern "C" void oc_draw_clear(int c)
{
    char b[32];
    snprintf(b, sizeof(b), "clear %d", c);
    draw_log(b);
}
extern "C" void oc_draw_rect(int x, int y, int w, int h, int c)
{
    char b[64];
    snprintf(b, sizeof(b), "rect %d %d %d %d %d", x, y, w, h, c);
    draw_log(b);
}
extern "C" void oc_draw_line(int x0, int y0, int x1, int y1, int c)
{
    char b[64];
    snprintf(b, sizeof(b), "line %d %d %d %d %d", x0, y0, x1, y1, c);
    draw_log(b);
}
extern "C" void oc_draw_circle(int x, int y, int r, int c, int fill)
{
    char b[64];
    snprintf(b, sizeof(b), "circle %d %d %d %d %d", x, y, r, c, fill);
    draw_log(b);
}
extern "C" void oc_draw_text(const char *s, int x, int y, int c)
{
    char b[96];
    snprintf(b, sizeof(b), "text %s %d %d %d", s, x, y, c);
    draw_log(b);
}
extern "C" void oc_draw_text_size(const char *s, int x, int y, int c, int size)
{
    char b[96];
    snprintf(b, sizeof(b), "text%d %s %d %d %d", size, s, x, y, c);
    draw_log(b);
}
extern "C" unsigned oc_draw_ticks_ms(void) { return 0; }
extern "C" void oc_draw_show(void) { draw_log("show"); }
extern "C" int oc_draw_wait_key(void) { return 0; }
