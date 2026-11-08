/* Primitives de dessin du module Python "draw" (voir Core/py_draw.c),
 * implementees avec la boite a outils d'interface (Core/ui_kit.cpp). */
#include "pika_wrapper.h"
#include "ui_kit.h"
#include "ui.h"
#include "display.h"
#include "colors.h"
#include "pico/stdlib.h"
#include <stdlib.h>

extern "C" void oc_draw_begin(void)
{
    fill_screen(COLOR_WHITE);
}

extern "C" void oc_draw_clear(int color)
{
    fill_screen((uint16_t)color);
}

extern "C" void oc_draw_rect(int x, int y, int w, int h, int color)
{
    uk_rect(x, y, w, h, (uint16_t)color);
}

extern "C" void oc_draw_line(int x0, int y0, int x1, int y1, int color)
{
    uk_line(x0, y0, x1, y1, (uint16_t)color);
}

extern "C" void oc_draw_circle(int cx, int cy, int r, int color, int fill)
{
    if (r < 0 || r > 2000)
        return;
    /* Algorithme du point milieu. */
    int x = r, y = 0, err = 1 - r;
    while (x >= y)
    {
        if (fill)
        {
            uk_rect(cx - x, cy + y, 2 * x + 1, 1, (uint16_t)color);
            uk_rect(cx - x, cy - y, 2 * x + 1, 1, (uint16_t)color);
            uk_rect(cx - y, cy + x, 2 * y + 1, 1, (uint16_t)color);
            uk_rect(cx - y, cy - x, 2 * y + 1, 1, (uint16_t)color);
        }
        else
        {
            const int px[8] = {x, y, -y, -x, -x, -y, y, x};
            const int py[8] = {y, x, x, y, -y, -x, -x, -y};
            for (int i = 0; i < 8; i++)
                uk_pixel(cx + px[i], cy + py[i], (uint16_t)color);
        }
        y++;
        if (err < 0)
            err += 2 * y + 1;
        else
        {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

extern "C" void oc_draw_text(const char *s, int x, int y, int color)
{
    uk_text(x, y, s, (uint16_t)color, 1);
}

extern "C" void oc_draw_text_size(const char *s, int x, int y, int color, int size)
{
    uk_text(x, y, s, (uint16_t)color, size);
}

extern "C" unsigned oc_draw_ticks_ms(void)
{
    return (unsigned)to_ms_since_boot(get_absolute_time());
}

extern "C" void oc_draw_show(void)
{
    flush_display();
}

extern "C" int oc_draw_wait_key(void)
{
    flush_display();
    int k = uk_wait_key();
    return k < 0 ? -k : k;
}
