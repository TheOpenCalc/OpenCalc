#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "headers/display.h"
#include "headers/colors.h"
#ifdef OPENCALC_WASM

#include <emscripten/emscripten.h>
static uint16_t g_framebuffer[SCREEN_HEIGHT * SCREEN_WIDTH];

extern "C" {
    EMSCRIPTEN_KEEPALIVE uint16_t *opencalc_framebuffer() { return g_framebuffer; }
    EMSCRIPTEN_KEEPALIVE int opencalc_fb_width() { return SCREEN_WIDTH; }
    EMSCRIPTEN_KEEPALIVE int opencalc_fb_height() { return SCREEN_HEIGHT; }
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
    ili_cmd(0x11);
    sleep_ms(120);
    ili_cmd(0x29);
#endif
}


void fill_screen(uint16_t color)
{
#ifdef OPENCALC_WASM
    for (int i = 0; i < SCREEN_HEIGHT * SCREEN_WIDTH; ++i) {
        g_framebuffer[i] = color;
    }
#else
    ili_cmd(0x2A);
    ili_data(0);
    ili_data(0);
    ili_data(0);
    ili_data(239);
    ili_cmd(0x2B);
    ili_data(0);
    ili_data(0);
    ili_data(1);
    ili_data(63);
    ili_cmd(0x2C);

    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);
    for (int i = 0; i < 320 * 240; ++i) {
        uint8_t hi = color >> 8;
        uint8_t lo = color & 0xFF;
        uint8_t data[2] = {hi, lo};
        spi_write_blocking(spi0, data, 2);
    }
    gpio_put(PIN_CS, 1);
#endif
}


void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
#ifdef OPENCALC_WASM
    if ((x >= SCREEN_HEIGHT) || (y >= SCREEN_WIDTH)) {
        return;
    }
    if ((x + w - 1) >= SCREEN_HEIGHT) {
        w = SCREEN_HEIGHT - x;
    }
    if ((y + h - 1) >= SCREEN_WIDTH) {
        h = SCREEN_WIDTH - y;
    }

    for (uint16_t row = x; row < x + w; ++row) {
        uint16_t mapped_row = (uint16_t)(SCREEN_HEIGHT - 1 - row);
        uint32_t base = (uint32_t)mapped_row * SCREEN_WIDTH;
        for (uint16_t col = y; col < y + h; ++col) {
            g_framebuffer[base + col] = color;
        }
    }
#else
    if ((x >= SCREEN_HEIGHT) || (y >= SCREEN_WIDTH)) {
        return;
    }
    if ((x + w - 1) >= SCREEN_HEIGHT) {
        w = SCREEN_HEIGHT - x;
    }
    if ((y + h - 1) >= SCREEN_WIDTH) {
        h = SCREEN_WIDTH - y;
    }

    // Définir la zone de dessin
    ili_cmd(0x2A); // Set column address
    ili_data(x >> 8);
    ili_data(x & 0xFF);
    ili_data((x + w - 1) >> 8);
    ili_data((x + w - 1) & 0xFF);

    ili_cmd(0x2B); // Set row address
    ili_data(y >> 8);
    ili_data(y & 0xFF);
    ili_data((y + h - 1) >> 8);
    ili_data((y + h - 1) & 0xFF);

    ili_cmd(0x2C); // Memory write

// Préparation du buffer (statique pour éviter la pile)
#define BUF_PIXELS 1024
    static uint8_t buf[BUF_PIXELS * 2];
    const uint8_t hi = color >> 8;
    const uint8_t lo = color & 0xFF;

    // Remplir le buffer avec la couleur
    for (int i = 0; i < BUF_PIXELS; ++i) {
        buf[2 * i] = hi;
        buf[2 * i + 1] = lo;
    }

    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

    int total_pixels = w * h;
    while (total_pixels > 0) {
        int batch = (total_pixels > BUF_PIXELS) ? BUF_PIXELS : total_pixels;
        spi_write_blocking(spi0, buf, batch * 2);
        total_pixels -= batch;
    }

    gpio_put(PIN_CS, 1);
#endif
}


void draw_char(uint16_t x, uint16_t y, char *c, uint16_t color, uint16_t bg, uint8_t size)
{
    int i = 0;
    while (c[i] != '\0') {
        if (c[i] < 32 || c[i] > 126) {
            return; 
        }
        const uint8_t *glyph = &font5x7[(c[i] - 32) * 5];

        for (int col = 0; col < 5; col++) {
            uint8_t line = glyph[col];
            for (int row = 0; row < 8; row++) {
                // uint16_t pixel_color = (line & 0x01) ? color : bg;
                if (line & 0x01) {
                    fill_rect(x - row * size, y + col * size, size, size, color);
                }
                line >>= 1;
            }
        }
        y += size * 5;
        i++;
    }
}


void display_battery(uint16_t x, uint16_t y, int level)
{
    fill_rect(x + 1, y + 1, 6 * level, 13, 0x1dc0);
    fill_rect(x + 1, y + 6 * level, 6 * (4 - level), 13, 0xe503);

    for (int i = 0; i < 5; i++) {
        fill_rect(x, y + i * 6, 13, 1, 0x0000);
    }
    fill_rect(x, y, 1, 25, 0x0000);
    fill_rect(x + 13, y, 1, 25, 0x0000);

    fill_rect(x + 5, y + 25, 4, 2, level == 5 ? 0x1dc0 : 0xe503);

    fill_rect(x + 4, y + 25, 1, 3, 0x0000);
    fill_rect(x + 9, y + 25, 1, 3, 0x0000);
    fill_rect(x + 5, y + 27, 4, 1, 0x0000);
}