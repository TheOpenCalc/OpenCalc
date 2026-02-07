#include <vector>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <cmath>
#include <cstdlib>
#include <string.h>
#include <string>

#include "headers/ui.h"
#include "headers/menu.h"
#include "headers/Evaluator.h"

#ifdef OPENCALC_WASM

#include <emscripten/emscripten.h>
static uint16_t g_framebuffer[SCREEN_HEIGHT * SCREEN_WIDTH];

extern "C" {
    EMSCRIPTEN_KEEPALIVE uint16_t *opencalc_framebuffer() { return g_framebuffer; }
    EMSCRIPTEN_KEEPALIVE int opencalc_fb_width() { return SCREEN_WIDTH; }
    EMSCRIPTEN_KEEPALIVE int opencalc_fb_height() { return SCREEN_HEIGHT; }
}

#endif

struct coord_s {
    int x;
    int y;
};

typedef struct coord_s coord;

extern int x_cursor;
extern int y_cursor;

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
            return; // Caractères imprimables uniquement
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

int min(int a, int b)
{
    if (a < b) {
        return a;
    }
    return b;
}

int max(int a, int b)
{
    if (a > b) {
        return a;
    }
    return b;
}

void axis()
{
    fill_rect(0, 159, 220, 2, 0x0000);
    fill_rect(120, 0, 2, 340, 0x0000);
}

fill_box *create_fill_box(int x, int y, int h, int w, int border)
{
    fill_box *out = (fill_box*) malloc(sizeof(fill_box));
    out->curso_pos = 0;
    out->x = x;
    out->y = y;
    out->h = h;
    out->w = w;
    out->border = border;
    out->color = 0xffff;
    out->t_size = 0;
    char *in = (char*) malloc(sizeof(char) * 100);
    for (int i = 0; i < 100; i++) {
        in[i] = '\0';
    }
    out->text = in;
    return out;
}

text_box *create_text_box(int x, int y, int h, int w, int border, bool transparent)
{
    text_box *out = (text_box*) malloc(sizeof(text_box));
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
    char *in = (char*) malloc(sizeof(char) * 100);
    for (int i = 0; i < 100; i++) {
        in[i] = '\0';
    }
    out->text = in;
    return out;
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
    printf("%i %i\n", p->val, p->grad);
    p->val = min(p->val + 1, p->grad);
    printf("%i %i\n", p->val, p->grad);
}

void decrement_potentiometer(pontentiometer *p)
{
    p->val = max(p->val - 1, 0);
}
void display_text_box(text_box *in, int shift_y, int shift_text, bool is_selected)
{
    printf("L: %c\n", in->allign);
    if (in == nullptr)
        return;

    if (!is_selected)
        fill_rect(in->y + 1 + shift_y, in->x + 1, in->h - 2, in->w - 2, in->col);
    else
        fill_rect(in->y + shift_y, in->x, in->h, in->w, 0xfff0);
    if (in->allign == 'c')
    {
        draw_char(in->y + shift_text + shift_y + in->h - 10, (in->w - in->t_size * 5) / 2 + in->x, in->text, 0x0000, 0xFF, in->display_text_size);
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
    /*   sf::Vector2f s(in->w,in->h);
        sf::RectangleShape border(s);
        border.setPosition(in->x,in->y+shift_y);
        border.setFillColor(sf::Color::Black);

        sf::Vector2f sb(in->w-2*in->border,in->h-2*in->border);
        sf::RectangleShape inside(sb);
        inside.setPosition(in->x+in->border,in->y+in->border+shift_y);
        if(!in->transparent){
            if(!is_selected){
                inside.setFillColor(sf::Color::White);
            }else
                inside.setFillColor(sf::Color{ 190, 190, 190});
            window->draw(border);
            window->draw(inside);
        }

        */
}


void draw_buffer(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *buffer)
{
    if ((x >= SCREEN_HEIGHT) || (y >= SCREEN_WIDTH))
        return;
    if ((x + w - 1) >= SCREEN_HEIGHT)
        w = SCREEN_HEIGHT - x;
    if ((y + h - 1) >= SCREEN_WIDTH)
        h = SCREEN_WIDTH - y;

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

    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

// Envoyer le buffer en batch pour ne pas saturer la mémoire
#define BUF_PIXELS 1024
    static uint8_t spi_buf[BUF_PIXELS * 2];

    int total_pixels = w * h;
    int idx = 0;

    while (total_pixels > 0)
    {
        int batch = (total_pixels > BUF_PIXELS) ? BUF_PIXELS : total_pixels;

        // Convertir les pixels du buffer 16 bits en tableau d'octets pour SPI
        for (int i = 0; i < batch; ++i)
        {
            uint16_t color = buffer[idx++];
            spi_buf[2 * i] = color >> 8;
            spi_buf[2 * i + 1] = color & 0xFF;
        }

        spi_write_blocking(spi0, spi_buf, batch * 2);
        total_pixels -= batch;
    }

    gpio_put(PIN_CS, 1);
}

void display_fill_box(fill_box *in, int shift_y, bool is_selected, int pos, char prefix)
{
    if (in == nullptr) {
        return;
    }
    if (!is_selected) {
        fill_rect(in->y + shift_y, in->x, in->h, in->w, in->color);
    } else {
        fill_rect(in->y + shift_y, in->x, in->h, in->w, 0xfff0);
    }

    display_equation(in->text, 100, in->y + shift_y, in->x + (prefix == ' ' ? 0 : 55), 2, is_selected ? in->curso_pos : -10);

    if (prefix == 'f') {
        char *t = (char*) malloc(sizeof(char*) * 5);
        t[0] = 'f' + pos;
        t[1] = '(';
        t[2] = 'x';
        t[3] = ')';
        t[4] = '=';
        display_equation(t, 5 /*+log(pos)*/, in->y + shift_y, in->x, 2, is_selected ? in->curso_pos : -10);
    }
    if (prefix == 'u') {
        char *t = (char*) malloc(sizeof(char*) * 2);
        t[0] = 'u' + pos;
        t[1] = '=';
        display_equation(t, 2 /*+log(pos)*/, in->y + shift_y, in->x, 2, is_selected ? in->curso_pos : -10);
        char *n = (char*) malloc(sizeof(char));
        n[0] = 'n';
        draw_char(in->x - 3, in->y, n, 0x0000, 0x0000, 1);
    }
}

void update_fill_box(fill_box *in, int event, bool snd)
{
    char temp1 = in->text[in->curso_pos];
    char temp2 = in->text[in->curso_pos + 1];
    char temp3 = in->text[in->curso_pos + 2];
    int in_tsize = in->t_size;
    if (snd) {
        switch (event) {
        case ZERO :
            in->text[in->curso_pos] = 'U';
            in->curso_pos++;
            in->t_size++;
            break;
        case ONE :
            in->text[in->curso_pos] = 'P';
            in->curso_pos++;
            in->t_size++;
            break;
        case TWO :
            in->text[in->curso_pos] = 'Q';
            in->curso_pos++;
            in->t_size++;
            break;
        case THREE :
            in->text[in->curso_pos] = 'R';
            in->curso_pos++;
            in->t_size++;
            break;
        case EQUAL :
            in->text[in->curso_pos] = '=';
            in->curso_pos++;
            in->t_size++;
            break;
        case FOUR :
            in->text[in->curso_pos] = 'K';
            in->curso_pos++;
            in->t_size++;
            break;
        case FIVE :
            in->text[in->curso_pos] = 'L';
            in->curso_pos++;
            in->t_size++;
            break;
        case SIX :
            in->text[in->curso_pos] = 'M';
            in->curso_pos++;
            in->t_size++;
            break;
        case SEVEN :
            in->text[in->curso_pos] = 'F';
            in->curso_pos++;
            in->t_size++;
            break;
        case EIGHT : 
            in->text[in->curso_pos] = 'G';
            in->curso_pos++;
            in->t_size++;
            break;
        case NINE :
            in->text[in->curso_pos] = 'H';
            in->curso_pos++;
            in->t_size++;
            break;
        case PLUS :
            in->text[in->curso_pos] = 'S';
            in->curso_pos++;
            in->t_size++;
            break;
        case MINUS :
            in->text[in->curso_pos] = 'N';
            in->curso_pos++;
            in->t_size++;
            break;
        case TIMES :
            in->text[in->curso_pos] = 'T';
            in->curso_pos++;
            in->t_size++;
            break;
        case DIVIDE :
            in->text[in->curso_pos] = 'O';
            in->curso_pos++;
            in->t_size++;
            break;
        case OPENING_PARENTHESIS : 
            in->text[in->curso_pos] = 'I';
            in->curso_pos++;
            in->t_size++;
            break;
        case CLOSING_PARENTHESIS :
            in->text[in->curso_pos] = 'J';
            in->curso_pos++;
            in->t_size++;
            break;
        case LN :
            in->text[in->curso_pos] = 'l';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->t_size += 3;
            in->curso_pos += 2;
            break;
        case E :
            in->text[in->curso_pos] = 'e';
            in->curso_pos += 2;
            in->t_size++;
            break;
        case X :
            in->text[in->curso_pos] = 'X';
            in->curso_pos++;
            in->t_size++;
            break;
        case COMA :
            in->text[in->curso_pos] = 'V';
            in->curso_pos++;
            in->t_size++;
            break;
        case PI :
            in->text[in->curso_pos] = 'W';
            in->curso_pos++;
            in->t_size++;
            break;
        case COS :
            in->text[in->curso_pos] = 'A';
            in->curso_pos++;
            in->t_size++;
            break;
        case SIN :
            in->text[in->curso_pos] = 'B';
            in->curso_pos++;
            in->t_size++;
            break;
        case TAN :
            in->text[in->curso_pos] = 'C';
            in->curso_pos++;
            in->t_size++;
            break;
        case SQRT :
            in->text[in->curso_pos] = 'D';
            in->curso_pos++;
            in->t_size++;
            break;
        case POW :
            in->text[in->curso_pos] = 'E';
            in->curso_pos++;
            in->t_size++;
            break;
        case BACK :
            in->curso_pos--;
            if (in->t_size > 0) {
                in->t_size--;
                in->text[in->t_size] = '\0';
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
                in->curso_pos += 2;

                in->t_size += 3;
                break;

            case SINH:

                in->text[in->curso_pos] = 'g';
                in->text[in->curso_pos + 1] = '(';
                in->text[in->curso_pos + 2] = ')';
                in->curso_pos += 2;

                in->t_size += 3;
                break;

            case TANH:

                in->text[in->curso_pos] = 'h';
                in->text[in->curso_pos + 1] = '(';
                in->text[in->curso_pos + 2] = ')';
                in->curso_pos += 2;

                in->t_size += 3;
                break;

            case ACOSH:

                in->text[in->curso_pos] = 'i';
                in->text[in->curso_pos + 1] = '(';
                in->text[in->curso_pos + 2] = ')';
                in->curso_pos += 2;

                in->t_size += 3;
                break;

            case ASINH:

                in->text[in->curso_pos] = 'j';
                in->text[in->curso_pos + 1] = '(';
                in->text[in->curso_pos + 2] = ')';
                in->curso_pos += 2;

                in->t_size += 3;
                break;

            case ATANH:

                in->text[in->curso_pos] = 'k';
                in->text[in->curso_pos + 1] = '(';
                in->text[in->curso_pos + 2] = ')';
                in->curso_pos += 2;

                in->t_size += 3;
                break;
        default:
            break;
        }
    } else {
        switch (event) {
        case ZERO :
            in->text[in->curso_pos] = '0';
            in->curso_pos++;
            in->t_size++;
            break;
        case ONE :
            in->text[in->curso_pos] = '1';
            in->curso_pos++;
            in->t_size++;
            break;
        case TWO :
            in->text[in->curso_pos] = '2';
            in->curso_pos++;
            in->t_size++;
            break;
        case THREE :
            in->text[in->curso_pos] = '3';
            in->curso_pos++;
            in->t_size++;
            break;
        case EQUAL :
            in->text[in->curso_pos] = '=';
            in->curso_pos++;
            in->t_size++;
            break;
        case FOUR :
            in->text[in->curso_pos] = '4';
            in->curso_pos++;
            in->t_size++;
            break;
        case FIVE :
            in->text[in->curso_pos] = '5';
            in->curso_pos++;
            in->t_size++;
            break;
        case SIX :
            in->text[in->curso_pos] = '6';
            in->curso_pos++;
            in->t_size++;
            break;
        case SEVEN :
            in->text[in->curso_pos] = '7';
            in->curso_pos++;
            in->t_size++;
            break;
        case EIGHT :
            in->text[in->curso_pos] = '8';
            in->curso_pos++;
            in->t_size++;
            break;
        case NINE :
            in->text[in->curso_pos] = '9';
            in->curso_pos++;
            in->t_size++;
            break;
        case PLUS :
            in->text[in->curso_pos] = '+';
            in->curso_pos++;
            in->t_size++;
            break;
        case MINUS :
            in->text[in->curso_pos] = '-';
            in->curso_pos++;
            in->t_size++;
            break;
        case TIMES :
            in->text[in->curso_pos] = '*';
            in->curso_pos++;
            in->t_size++;
            break;
        case DIVIDE :
            in->text[in->curso_pos] = '/';
            in->curso_pos++;
            in->t_size++;
            break;
        case OPENING_PARENTHESIS :
            in->text[in->curso_pos] = '(';
            in->curso_pos++;
            in->t_size++;
            break;
        case CLOSING_PARENTHESIS :
            in->text[in->curso_pos] = ')';
            in->curso_pos++;
            in->t_size++;
            break;
        case LN :
            in->text[in->curso_pos] = 'l';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->t_size += 3;
            in->curso_pos += 2;
            break;
        case E :
            in->text[in->curso_pos] = 'e';
            in->curso_pos += 2;
            in->t_size++;
            break;
        case X :
            in->text[in->curso_pos] = 'X';
            in->curso_pos++;
            in->t_size++;
            break;
        case COMA :
            in->text[in->curso_pos] = '.';
            in->curso_pos++;
            in->t_size++;
            break;
        case PI :
            in->text[in->curso_pos] = 'p';
            in->curso_pos++;
            in->t_size++;
            break;
        case COS :
            in->text[in->curso_pos] = 'c';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 2;
            in->t_size += 3;
            break;
        case SIN :
            in->text[in->curso_pos] = 's';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 2;
            in->t_size += 3;
            break;
        case TAN :
            in->text[in->curso_pos] = 't';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 2;
            in->t_size += 3;
            break;
        case SQRT :
            in->text[in->curso_pos] = 'r';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 2;
            in->t_size += 3;
            break;
        case POW :
            in->text[in->curso_pos] = '^';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 2;
            in->t_size += 3;
            break;
        case BACK :
            in->curso_pos--;
            if (in->t_size > 0) {
                in->t_size--;
                in->text[in->t_size] = '\0';
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
                in->curso_pos += 2;

                in->t_size += 3;
                break;

            case SINH:

                in->text[in->curso_pos] = 'g';
                in->text[in->curso_pos + 1] = '(';
                in->text[in->curso_pos + 2] = ')';
                in->curso_pos += 2;

                in->t_size += 3;
                break;

            case TANH:

                in->text[in->curso_pos] = 'h';
                in->text[in->curso_pos + 1] = '(';
                in->text[in->curso_pos + 2] = ')';
                in->curso_pos += 2;

                in->t_size += 3;
                break;

            case ACOSH:

                in->text[in->curso_pos] = 'i';
                in->text[in->curso_pos + 1] = '(';
                in->text[in->curso_pos + 2] = ')';
                in->curso_pos += 2;

                in->t_size += 3;
                break;

            case ASINH:

                in->text[in->curso_pos] = 'j';
                in->text[in->curso_pos + 1] = '(';
                in->text[in->curso_pos + 2] = ')';
                in->curso_pos += 2;

                in->t_size += 3;
                break;

            case ATANH:

                in->text[in->curso_pos] = 'k';
                in->text[in->curso_pos + 1] = '(';
                in->text[in->curso_pos + 2] = ')';
                in->curso_pos += 2;

                in->t_size += 3;
                break;
        default :
            break;
        }
    }
    /*for (int i = 0; i <= in->t_size - in_tsize; i++) {
        char temp = in->text[i + in->curso_pos];
        in->text[i + in->curso_pos] = temp1;
        temp1 = temp2;
        temp2 = temp3;
        temp3 = temp;
    }*/
    // FIXME : NE FONCTIONNE PAS EN L'ETAT A REPARER
}

void display_equation(char *in, int input_size, int x, int y, int SIZE, int cursor_pos)
{
    x += 25;

    int pos = 0;
    char *cos = (char*) malloc(sizeof(char) * 4);
    char *sin = (char*) malloc(sizeof(char) * 4);
    char *tan = (char*) malloc(sizeof(char) * 4);
    cos[0] = 'c';
    cos[1] = 'o';
    cos[2] = 's';

    sin[0] = 's';
    sin[1] = 'i';
    sin[2] = 'n';

    tan[0] = 't';
    tan[1] = 'a';
    tan[2] = 'n';

    cos[3] = '\0';
    sin[3] = '\0';
    tan[3] = '\0';

    char *temp = (char*) malloc(sizeof(char) * 2);
    temp[1] = '\0';

    for (int i = 0; i < input_size; i++) {
        if (i == cursor_pos - 1) {
            x_cursor = x;
            y_cursor = y + 5 + pos + 7;
        }
        if (is_in(in[i], "0123456789,.+-*()/Xx^=ABCDEFGHIJKLMNOPQRSTUVWYZ!")) {
            temp[0] = in[i];
            draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, SIZE);
            pos += 5.5 * SIZE;
        } else if (is_in(in[i], "uvwijkfghcst")) {
        if (is_in(in[i],"uvwijk")){
            temp[0] = 'a';
            draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
            pos += 5.5 * SIZE;
            }
            if (is_in(in[i],"cfui")) {
                draw_char(x, y + 5 + pos, cos, 0X0000, 0X0000, 2);
            }
            if (is_in(in[i], "sgvj")) {
                draw_char(x, y + 5 + pos, sin, 0X0000, 0X0000, 2);
            }
            if (is_in(in[i] ,"twhk")) {
                draw_char(x, y + 5 + pos, tan, 0X0000, 0X0000, 2);
            }
            if(is_in(in[i],"uvwcst")){
              pos+=15.5*SIZE;
            }
            else if(is_in(in[i],"ijkfgh")){
              pos+=15.5*SIZE;
                      temp[0] = 'h';
            draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
            pos += 5.5 * SIZE;
            }
        } else if (in[i] == 'c') {
            draw_char(x, y + 5 + pos, cos, 0X0000, 0X0000, 2);
            pos += 12 * SIZE;
        } else if (in[i] == 's') {
            draw_char(x, y + 5 + pos, sin, 0X0000, 0X0000, 2);
            pos += 13 * SIZE;
        } else if (in[i] == 't') {
            draw_char(x, y + 5 + pos, tan, 0X0000, 0X0000, 2);
            pos += 13 * SIZE;
        } /*else if (in[i] == '(') {
             int j = 1;
             int ind = 1;
             while (i + j < input_size && ind != 0) {
                 if (in[i + j] == '(') {
                     ind++;
                 } else if (in[i + j] == ')') {
                     ind--;
                 }
                 j++;
             }
             if (i + j >= input_size || in[i + j] != '/') {
                 temp[0] = in[i];
                 draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
                pos += 9;
             } else {
                 display_equation(&in[i + 1], j - 2, x + pos, y - 9, 2, cursor_pos);
                 i += j + 1;
                 j = 1;
                 ind = 1;
                 while (i + j < input_size && ind != 0) {
                     if (in[i + j] == '(') {
                         ind++;
                     } else if (in[i + j] == ')') {
                         ind--;
                     }
                     j++;
                 }
                 if (ind == 0) {
                     display_equation(&in[i + 1], j - 2, x + pos, y + 7, 2, cursor_pos);
                     i += j - 1;
                     pos += SIZE * 4 * j - 14;
                 }
             }
         }*/
        else if (in[i] == 'p') {
#ifdef OPENCALC_WASM      /// Pourquoi ????
            temp[0] = 'p';
#else
            temp[0] = 'π';
#endif
            draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
            pos += SIZE * 4;
        } else if (in[i] == 'e') {
            temp[0] = 'e';
            draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
            pos += SIZE * 4;
        } else if (in[i] == 'r') {
            int ind = 1;
            int j = 2;
            while (i + j < input_size && ind != 0) {
                if (in[i + j] == '(') {
                    ind++;
                } else if (in[i + j] == ')') {
                    ind--;
                }
                j++;
            }
            if (ind == 0) {
                // temp[0] = (char) 251;
                temp[0] = 'R';
                draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
                pos += SIZE * 4;
                display_equation(&in[i + 2], j - 3, x + pos, y - 3, 2, cursor_pos);
                i += j - 1;
                pos += SIZE * 4 * (j - 2);
            }
        } else if (in[i] == 'l') {
            pos += 2;
            temp[0] = 'l';
            draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
            pos += 1.5 * SIZE;
            temp[0] = 'n';
            draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
            pos += 4.5 * SIZE;
        } else if (in[i] == '^') {
            int ind = 1;
            int j = 2;
            while (i + j < input_size && ind != 0) {
                if (in[i + j] == '(') {
                    ind++;
                } else if (in[i + j] == ')') {
                    ind--;
                }
                j++;
            }
            if (i + j < input_size) {
                display_equation(&in[i + 2], j - 3, x - 17, y + pos, 1, cursor_pos);
                i += j - 1;
                pos += 2.5 * (j - 1);
            }
            if (i + j == input_size) {
                display_equation(&in[i + 2], j - 3, x - 17, y + pos, 1, cursor_pos);
                i += j;
                pos += 2.5 * (j - 1);
            }
        }
    }
}

void blink_cursor()
{
    char *temp = (char*) malloc(sizeof(char) * 2);

    temp[0] = '|';
    temp[1] = '\0';

    if (to_ms_since_boot(get_absolute_time()) % 1500 < 750) {
        draw_char(x_cursor, y_cursor, temp, 0X0000, 0X0000, 2);
    } else {
        draw_char(x_cursor, y_cursor, temp, BACKGROUND_COLOR, BACKGROUND_COLOR, 2);
    };
}

void draw_image(int x, int y, int h, int w, uint16_t *img, uint16_t bck)
{
    for (int i = 0; i < w; i++)
    {
        for (int j = 0; j < h; j++)
        {
            if (img[i * h + j] != bck)
                fill_rect(x + h - i, y + j, 1, 1, img[i * h + j]);
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

int menu_tools()
{
    text_box **items = (text_box **)malloc(sizeof(text_box *) * 6);
    printf("aaeeaII");

    for (int i = 0; i < 6; i++)
    {
        items[i] = create_text_box(32, i * 30, 30, 256, 0, false);
        printf("aaaII %i\n", i);
    }
    items[0]->text = "Probabilites";
    items[1]->text = "Matrices";
    items[2]->text = "Arithmétique";
    items[3]->text = "Trigonométrie";
    items[4]->text = "Nombres decimaux";
    items[5]->text = "Constantes";

    items[0]->t_size = 12;
    items[1]->t_size = 8;

    items[2]->t_size = 12;
    items[3]->t_size = 13;
    items[4]->t_size = 16;
    items[5]->t_size = 10;

    items[0]->allign = 'r';
    items[1]->allign = 'r';
    items[2]->allign = 'r';
    items[3]->allign = 'r';
    items[4]->allign = 'r';
    items[5]->allign = 'r';

    int pos = 0;
    int selected = 0;
    int last_pressed = scan_keypad();

    while (true)
    {
        for (int i = pos; i < pos + 4; i++)
        {
            display_text_box(items[i], pos * -30, 0, i == selected);
        }
        last_pressed = scan_keypad();
        while (last_pressed == -1)
        {
            last_pressed = scan_keypad();
        }

        switch (last_pressed)
        {
        case UP:
            selected = min(4, selected + 1);

            if (selected - pos >= 4)
            {
                pos = min(pos + 1, 1);
            }
            break;
        case DOWN:
            selected = max(0, selected - 1);

            pos = min(selected, pos);

            break;
        case BACK:
        {
            return END_KEYS;
        }
        case OK:
            switch (selected)
            {
            case 0:
                return (menu_proba());
                break;
            case 3:
                return (menu_trigo());
            default:
                break;
            }
            break;
        default:

            break;
        }
    }

    return END_KEYS;
}

int menu_proba()
{
    text_box **items = (text_box **)malloc(sizeof(text_box *) * 3);
    fill_rect(0, 32, 120, 256, 0x311f);
    printf("aaeeaII");

    for (int i = 0; i < 3; i++)
    {
        items[i] = create_text_box(60, i * 30, 30, 200, 2, false);
        printf("aaaII %i\n", i);
    }
    items[0]->text = "Factorielle n!";
    items[1]->text = "k parmis n ";
    items[2]->text = "permutation(n,k)";

    items[0]->t_size = 12;
    items[1]->t_size = 8;

    items[2]->t_size = 12;

    items[0]->allign = 'r';
    items[1]->allign = 'r';
    items[2]->allign = 'r';

    int pos = 0;
    int selected = 0;
    int last_pressed = scan_keypad();
    printf("aaaII\n");
    while (true)
    {
        for (int i = pos; i < pos + 4; i++)
        {
            display_text_box(items[i], pos * -30, 0, i == selected);
            printf("aaaIzzzzI %i\n", i);
        }
        last_pressed = scan_keypad();
        while (last_pressed == -1)
        {
            last_pressed = scan_keypad();
        }
        printf("aaaII\n");

        switch (last_pressed)
        {
        case UP:
            selected = min(4, selected + 1);

            if (selected - pos >= 4)
            {
                pos = min(pos + 1, 1);
            }
            break;
        case DOWN:
            selected = max(0, selected - 1);

            pos = min(selected, pos);

            break;
        case BACK:
        {
            return END_KEYS;
        }
        case OK:
            switch (selected)
            {
            case 0:

                return FACT;
                break;

            default:
                break;
            }
            break;
        default:

            break;
        }
    }
    return END_KEYS;
}


int menu_trigo()
{
    text_box **items = (text_box **)malloc(sizeof(text_box *) * 6);
    fill_rect(0, 32, 120, 256, 0x311f);
    printf("aaeeaII");

    for (int i = 0; i < 6; i++)
    {
        items[i] = create_text_box(60, i * 30, 30, 200, 2, false);
        printf("aaaII %i\n", i);
    }
    items[0]->text = "cosinus hyperbolique";
    items[1]->text = "sinus hyperbolique";
    items[2]->text = "tangente hyperbolique";

    items[3]->text = "arccosinus hyperbolique";
    items[4]->text = "arcsinus hyperbolique";
    items[5]->text = "arctangente hyperbolique";

    items[0]->t_size = 20;
    items[1]->t_size = 18;
    items[2]->t_size = 21;
    items[3]->t_size = 23;
    items[4]->t_size = 21;
    items[5]->t_size = 24;

    items[0]->allign = 'r';
    items[1]->allign = 'r';
    items[2]->allign = 'r';
    items[3]->allign = 'r';
    items[4]->allign = 'r';
    items[5]->allign = 'r';

    int pos = 0;
    int selected = 0;
    int last_pressed = scan_keypad();
    printf("aaaII\n");
    while (true)
    {
        for (int i = pos; i < pos + 4; i++)
        {
            display_text_box(items[i], pos * -30, 0, i == selected);
            printf("aaaIzzzzI %i\n", i);
        }
        last_pressed = scan_keypad();
        while (last_pressed == -1)
        {
            last_pressed = scan_keypad();
        }
        printf("aaaII\n");

        switch (last_pressed)
        {
        case UP:
            selected = min(5, selected + 1);

            if (selected - pos >= 4)
            {
                pos = min(pos + 1, 2);
            }
            break;
        case DOWN:
            selected = max(0, selected - 1);

            pos = min(selected, pos);

            break;
        case BACK:
        {
            return END_KEYS;
        }
        case OK:
            switch (selected)
            {
            case 0:

                return COSH;
                break;
            case 1:
                return SINH;
                break;

            case 2:
                return TANH;
                break;
            case 3:
                return ACOSH;
                break;
            case 4:
                return ASINH;
                break;
            case 5:
                return ATANH;
                break;
            default:
                break;
            }
            break;
        default:

            break;
        }
    }
    return END_KEYS;
}
