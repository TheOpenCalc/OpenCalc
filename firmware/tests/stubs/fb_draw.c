/* Primitives oc_draw_* pour les tests hote : dessin dans une image
 * 320x240 RGB565 en memoire (fb_pixels), que les tests inspectent et
 * peuvent enregistrer (fb_save_ppm). */
#include "pika_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FB_W 320
#define FB_H 240

unsigned short fb_pixels[FB_H][FB_W];
int fb_begin_count = 0, fb_show_count = 0, fb_wait_count = 0, fb_text_count = 0;
char fb_last_text[128];

static void px(int x, int y, int c)
{
    if (x >= 0 && y >= 0 && x < FB_W && y < FB_H)
        fb_pixels[y][x] = (unsigned short)c;
}

void fb_reset(void)
{
    memset(fb_pixels, 0, sizeof(fb_pixels));
    fb_begin_count = fb_show_count = fb_wait_count = fb_text_count = 0;
    fb_last_text[0] = '\0';
}

void oc_draw_clear(int c)
{
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++)
            fb_pixels[y][x] = (unsigned short)c;
}

void oc_draw_begin(void)
{
    fb_begin_count++;
    oc_draw_clear(0xFFFF);
}

void oc_draw_rect(int x, int y, int w, int h, int c)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            px(x + i, y + j, c);
}

void oc_draw_line(int x0, int y0, int x1, int y1, int c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;)
    {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void oc_draw_circle(int cx, int cy, int r, int c, int fill)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
        {
            int d = x * x + y * y;
            if (fill ? d <= r * r : (d <= r * r && d > (r - 1) * (r - 1)))
                px(cx + x, cy + y, c);
        }
}

/* Texte : un rectangle plein de la taille du texte (5x8 px par lettre). */
void oc_draw_text_size(const char *s, int x, int y, int c, int size)
{
    fb_text_count++;
    snprintf(fb_last_text, sizeof(fb_last_text), "%s", s);
    int n = (int)strlen(s);
    for (int i = 0; i < n; i++)
        if (s[i] != ' ')
            oc_draw_rect(x + i * 5 * size + 1, y + 1, 3 * size, 6 * size, c);
}

void oc_draw_text(const char *s, int x, int y, int c) { oc_draw_text_size(s, x, y, c, 1); }
int fb_save_ppm(const char *path);
void oc_draw_show(void) { fb_show_count++; }

/* Avec FB_DUMP=prefixe dans l'environnement, chaque ecran montre a
 * l'utilisateur est enregistre (prefixe1.ppm, prefixe2.ppm...). */
int oc_draw_wait_key(void)
{
    fb_wait_count++;
    const char *dump = getenv("FB_DUMP");
    if (dump)
    {
        char path[256];
        snprintf(path, sizeof(path), "%s%d.ppm", dump, fb_wait_count);
        fb_save_ppm(path);
    }
    return 0;
}

static unsigned g_ticks = 0;
unsigned oc_draw_ticks_ms(void) { return g_ticks += 7; }

int fb_save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++)
        {
            unsigned v = fb_pixels[y][x];
            unsigned char rgb[3] = {(unsigned char)(((v >> 11) & 31) * 255 / 31),
                                    (unsigned char)(((v >> 5) & 63) * 255 / 63),
                                    (unsigned char)((v & 31) * 255 / 31)};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    return 1;
}
