#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "display.h"
#include "font10x16.h"
#include "colors.h"
#include "exam_mode.h"
#include "settings.h"
#include <cstdio>
#ifndef OPENCALC_WASM
#include "core1.h"
#endif
#ifdef OPENCALC_WASM

#include <emscripten/emscripten.h>
static uint16_t g_framebuffer[SCREEN_HEIGHT * SCREEN_WIDTH];

extern "C"
{
    EMSCRIPTEN_KEEPALIVE uint16_t *opencalc_framebuffer() { return g_framebuffer; }
    EMSCRIPTEN_KEEPALIVE int opencalc_fb_width() { return SCREEN_WIDTH; }
    EMSCRIPTEN_KEEPALIVE int opencalc_fb_height() { return SCREEN_HEIGHT; }
    /* Luminosite 0..20 lue par le simulateur web pour assombrir le canvas. */
    EMSCRIPTEN_KEEPALIVE int opencalc_brightness() { extern int g_brightness; return g_brightness; }
}

#else

/* Double tampon : core0 rend dans le tampon actif pendant que core1 transfère
 * le tampon précédent par SPI. opencalc_framebuffers est lu par core1. */
uint16_t opencalc_framebuffers[CORE1_NUM_FBS][SCREEN_HEIGHT * SCREEN_WIDTH];

static int active_fb = 0;

static int dirty_x0[CORE1_NUM_FBS];
static int dirty_y0[CORE1_NUM_FBS];
static int dirty_x1[CORE1_NUM_FBS];
static int dirty_y1[CORE1_NUM_FBS];

static inline void reset_dirty(int b)
{
    dirty_x0[b] = SCREEN_HEIGHT;
    dirty_y0[b] = SCREEN_WIDTH;
    dirty_x1[b] = 0;
    dirty_y1[b] = 0;
}

static inline void mark_dirty(int x, int y, int w, int h)
{
    if (x < dirty_x0[active_fb]) dirty_x0[active_fb] = x;
    if (y < dirty_y0[active_fb]) dirty_y0[active_fb] = y;
    if (x + w > dirty_x1[active_fb]) dirty_x1[active_fb] = x + w;
    if (y + h > dirty_y1[active_fb]) dirty_y1[active_fb] = y + h;
}

#endif

void ili_cmd(uint8_t cmd)
{
#ifdef OPENCALC_WASM
    (void)cmd;
    return;
#else
    gpio_put(PIN_DC, 0);
    gpio_put(PIN_CS, 0);
    spi_write_blocking(spi0, &cmd, 1);
    gpio_put(PIN_CS, 1);
#endif
}

void ili_data(uint8_t data)
{
#ifdef OPENCALC_WASM
    (void)data;
    return;
#else
    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);
    spi_write_blocking(spi0, &data, 1);
    gpio_put(PIN_CS, 1);
#endif
}

void ili_reset()
{
#ifdef OPENCALC_WASM
    return;
#else
    gpio_put(PIN_RST, 0);
    sleep_ms(50);
    gpio_put(PIN_RST, 1);
    sleep_ms(50);
#endif
}

void ili_init()
{
#ifdef OPENCALC_WASM
    fill_screen(BACKGROUND_COLOR);
    return;
#else
    ili_reset();
    ili_cmd(0x01);
    sleep_ms(5);
    ili_cmd(0x28);
    ili_cmd(0x3A);
    ili_data(0x55);
    ili_cmd(0x36);
    ili_data(0x48);
    ili_cmd(0x21);
    ili_cmd(0x11);
    sleep_ms(120);
    ili_cmd(0x29);
#endif
}

void fill_screen(uint16_t color)
{
#ifdef OPENCALC_WASM
    for (int i = 0; i < SCREEN_HEIGHT * SCREEN_WIDTH; ++i)
        g_framebuffer[i] = color;
#else
    for (int b = 0; b < CORE1_NUM_FBS; b++)
    {
        for (int i = 0; i < SCREEN_HEIGHT * SCREEN_WIDTH; ++i)
            opencalc_framebuffers[b][i] = color;
        dirty_x0[b] = 0;
        dirty_y0[b] = 0;
        dirty_x1[b] = SCREEN_HEIGHT;
        dirty_y1[b] = SCREEN_WIDTH;
    }
#endif
}

void flush_display()
{
    /* Indicateur "unité d'angle" : petit badge (DEG ou RAD) dans le coin haut
     * gauche, dessiné à chaque frame par-dessus l'app dans le framebuffer (les
     * deux modes matériel et WASM), comme l'icône du mode examen. Convention :
     * x = ligne verticale (grand = haut), y = colonne horizontale (petit = gauche). */
    {
        static const char *angle_labels[2] = {"RAD", "DEG"};
        const char *label = angle_labels[angle_mode_get() % 2];
        fill_rect(ANGLE_LBL_X0, ANGLE_LBL_Y0, ANGLE_LBL_H, ANGLE_LBL_W, 0x0000);
        draw_char(ANGLE_LBL_X0 + ANGLE_LBL_H - 2, ANGLE_LBL_Y0 + 2, (char *)label, 0xFFFF, 0x0000, 1);
    }

#ifdef OPENCALC_WASM
    return;
#else
    /* Indicateur "mode examen" : petite icône carrée clignotante dans le coin
     * haut droit. Peinte RÉELLEMENT dans le framebuffer via fill_rect (qui marque
     * la zone comme dirty) à chaque frame, donc l'app ne peut pas la faire
     * disparaître et elle est systématiquement envoyée à l'écran. Dessinée
     * AVANT le dirty-check pour toujours forcer un rafraîchissement du coin. */
    if (exam_mode_banner_active())
    {
        uint32_t tone = to_ms_since_boot(get_absolute_time());
        uint16_t icon_color = ((tone / EXAM_LED_ON_MS) % 2) ? COLOR_RED : COLOR_MAROON;
        fill_rect(EXAM_ICON_X0, EXAM_ICON_Y0, EXAM_ICON_W, EXAM_ICON_W, icon_color);
    }

    if (dirty_x0[active_fb] >= dirty_x1[active_fb] || dirty_y0[active_fb] >= dirty_y1[active_fb])
        return;

    static uint32_t frame_count = 0;
    static uint32_t last_fps_time = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    frame_count++;
    if (now - last_fps_time >= 1000)
    {
#ifdef OPENCALC_FPS_DEBUG
        int area = (dirty_x1[active_fb] - dirty_x0[active_fb]) * (dirty_y1[active_fb] - dirty_y0[active_fb]);
        printf("FPS: %lu (dirty: %dx%d = %d px)\n",
               (unsigned long)frame_count,
               dirty_x1[active_fb] - dirty_x0[active_fb], dirty_y1[active_fb] - dirty_y0[active_fb], area);
#endif
        frame_count = 0;
        last_fps_time = now;
    }

    int to_send = active_fb;
    core1_flush_job_t job;
    job.buffer = to_send;
    job.x0 = dirty_x0[to_send];
    job.y0 = dirty_y0[to_send];
    job.x1 = dirty_x1[to_send] - 1;
    job.y1 = dirty_y1[to_send] - 1;
    reset_dirty(to_send);

    /* Confie le tampon courant à core1 (transféré par SPI en arrière-plan) puis
     * attend qu'un tampon libre revienne : il devient le tampon actif de rendu.
     * En pratique core0 ne bloque que si core1 n'a pas fini le précédent flush. */
    queue_add_blocking(&opencalc_flush_q, &job);
    int free_buf;
    queue_remove_blocking(&opencalc_free_q, &free_buf);
    if (free_buf >= 0 && free_buf < CORE1_NUM_FBS)
        active_fb = free_buf;
#endif
}

void blit_buffer(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *buffer)
{
    if ((x >= SCREEN_HEIGHT) || (y >= SCREEN_WIDTH))
        return;
    if ((x + w - 1) >= SCREEN_HEIGHT)
        w = SCREEN_HEIGHT - x;
    if ((y + h - 1) >= SCREEN_WIDTH)
        h = SCREEN_WIDTH - y;

#ifdef OPENCALC_WASM
    for (uint16_t dy = 0; dy < h; ++dy)
    {
        uint16_t mapped_row = (uint16_t)(SCREEN_HEIGHT - 1 - (x + dy));
        uint32_t base = (uint32_t)mapped_row * SCREEN_WIDTH;
        for (uint16_t dx = 0; dx < w; ++dx)
        {
            g_framebuffer[base + y + dx] = buffer[dy * w + dx];
        }
    }
#else
    mark_dirty(x, y, w, h);
    for (uint16_t dy = 0; dy < h; ++dy)
    {
        uint32_t base = (uint32_t)(y + dy) * SCREEN_HEIGHT;
        for (uint16_t dx = 0; dx < w; ++dx)
        {
            opencalc_framebuffers[active_fb][base + x + dx] = buffer[dy * w + dx];
        }
    }
#endif
}

void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if ((x >= SCREEN_HEIGHT) || (y >= SCREEN_WIDTH))
    {
        return;
    }
    if ((x + w - 1) >= SCREEN_HEIGHT)
    {
        w = SCREEN_HEIGHT - x;
    }
    if ((y + h - 1) >= SCREEN_WIDTH)
    {
        h = SCREEN_WIDTH - y;
    }

#ifdef OPENCALC_WASM
    for (uint16_t row = x; row < x + w; ++row)
    {
        uint16_t mapped_row = (uint16_t)(SCREEN_HEIGHT - 1 - row);
        uint32_t base = (uint32_t)mapped_row * SCREEN_WIDTH;
        for (uint16_t col = y; col < y + h; ++col)
        {
            g_framebuffer[base + col] = color;
        }
    }
#else
    mark_dirty(x, y, w, h);
    for (uint16_t dy = 0; dy < h; ++dy)
    {
        uint32_t base = (uint32_t)(y + dy) * SCREEN_HEIGHT;
        for (uint16_t dx = 0; dx < w; ++dx)
        {
            opencalc_framebuffers[active_fb][base + x + dx] = color;
        }
    }
#endif
}

/* Lit un caractere UTF-8 en s[*i] et renvoie l'indice de son glyphe dans
 * font5x7 ('?' s'il n'y en a pas). Les octets isoles 127 (racine) et 128
 * (integrale) restent acceptes, comme avant. */
static int next_glyph(const unsigned char *s, int *i)
{
    unsigned char b = s[*i];
    if (b < 0x80)
    {
        (*i)++;
        if (b == 127)
            return 95;
        return b >= 32 ? b - 32 : '?' - 32;
    }
    uint32_t cp;
    int extra;
    if (b >= 0xC2 && b <= 0xDF)
    {
        cp = b & 0x1F;
        extra = 1;
    }
    else if (b >= 0xE0 && b <= 0xEF)
    {
        cp = b & 0x0F;
        extra = 2;
    }
    else
    {
        (*i)++;
        return b == 0x80 ? 96 : '?' - 32;
    }
    (*i)++;
    for (int k = 0; k < extra; k++)
    {
        if ((s[*i] & 0xC0) != 0x80)
            return '?' - 32; /* sequence tronquee : on ne consomme pas la suite */
        cp = (cp << 6) | (s[*i] & 0x3F);
        (*i)++;
    }
    for (int k = 0; k < FONT_EXTRA_COUNT; k++)
        if (font_extra[k].cp == cp)
            return font_extra[k].index;
    return '?' - 32;
}

int utf8_glyph_count(const char *s, int nbytes)
{
    if (!s)
        return 0;
    int n = 0;
    for (int i = 0; i < nbytes && s[i]; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80 || (unsigned char)s[i] == 0x80)
            n++;
    return n;
}

void draw_char(uint16_t x, uint16_t y, char *c, uint16_t color, uint16_t bg, uint8_t size)
{
    const unsigned char *s = (const unsigned char *)c;
    int i = 0;
    while (s[i] != '\0')
    {
        int gi = next_glyph(s, &i);

        /* Taille 2 : police 10x16 dediee (plus detaillee qu'une 5x7 doublee),
         * meme encombrement. Ligne r dessinee en x + 1 - r pour couvrir
         * exactement les lignes de l'ancien rendu 5x7 x2. */
        if (size == 2)
        {
            const uint16_t *big = &font10x16[gi * 10];
            for (int col = 0; col < 10; col++)
            {
                uint16_t line = big[col];
                for (int row = 0; line; row++, line >>= 1)
                    if (line & 0x01)
                        fill_rect(x + 1 - row, y + col, 1, 1, color);
            }
            y += 10;
            continue;
        }

        const uint8_t *glyph = &font5x7[gi * 5];

        for (int col = 0; col < 5; col++)
        {
            uint8_t line = glyph[col];
            for (int row = 0; row < 8; row++)
            {
                if (line & 0x01)
                {
                    fill_rect(x - row * size, y + col * size, size, size, color);
                }
                line >>= 1;
            }
        }
        y += size * 5;
    }
}

void display_battery(uint16_t x, uint16_t y, int level)
{
    fill_rect(x + 1, y + 1, 6 * level, 13, 0x1dc0);
    fill_rect(x + 1, y + 6 * level, 6 * (4 - level), 13, 0xe503);

    for (int i = 0; i < 5; i++)
    {
        fill_rect(x, y + i * 6, 13, 1, 0x0000);
    }
    fill_rect(x, y, 1, 25, 0x0000);
    fill_rect(x + 13, y, 1, 25, 0x0000);

    fill_rect(x + 5, y + 25, 4, 2, level == 5 ? 0x1dc0 : 0xe503);

    fill_rect(x + 4, y + 25, 1, 3, 0x0000);
    fill_rect(x + 9, y + 25, 1, 3, 0x0000);
    fill_rect(x + 5, y + 27, 4, 1, 0x0000);
}
