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


#define MAX_NODES 128

typedef enum {
    N_NUMBER,
    N_VARIABLE,
    N_CONST,
    N_BINOP,
    N_FUNC,
    N_PLACEHOLDER,
    N_PARENTHESIS,
} NodeType;

typedef struct ASTNode {
    NodeType       type;
    char           op;
    double         number;
    char           variable;
    int            src_pos;  
    struct ASTNode *left;
    struct ASTNode *right;
} ASTNode;

static ASTNode node_pool[MAX_NODES];
static int     pool_top = 0;

static ASTNode *alloc_node() {
    if (pool_top >= MAX_NODES) return &node_pool[MAX_NODES - 1];
    ASTNode *n  = &node_pool[pool_top++];
    n->left     = nullptr;
    n->right    = nullptr;
    n->number   = 0;
    n->op       = 0;
    n->src_pos  = -1;
    return n;
}
static void reset_pool() { pool_top = 0; }

static ASTNode *make_placeholder() {
    ASTNode *nd = alloc_node();
    nd->type    = N_PLACEHOLDER;
    return nd;
}

struct coord_s {
    int x;
    int y;
};


typedef struct {
    token *toks;
    int    n;
    int    pos;
} Parser;

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

}

void display_text(int x, int y, char * t,int SIZE, int t_size){
    int pos =0;
    x+=25;
    for(int i =0;i<t_size;i++){
    draw_char(x, y + 5 + pos, &t[i], 0X0000, 0X0000, SIZE);
            pos += 5.5 * SIZE;
    }
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
        char *t = (char*) malloc(sizeof(char*) * 6);
        t[0] = 'f' + pos;
        t[1] = '(';
        t[2] = 'x';
        t[3] = ')';
        t[4] = '=';
        t[5] = '\0';
        draw_char( in->y+shift_y+25,in->x, t, 0X0000, 0X0000, 2);
    }
    if (prefix == 'u') {
        char *t = (char*) malloc(sizeof(char*) * 3);
        t[0] = 'u' + pos;
        t[1] = '=';
        t[1] = '\0';
        draw_char( in->y+25,in->x, t, 0X0000, 0X0000, 2);

        // display_equation(t, 2 /*+log(pos)*/, in->y + shift_y, in->x, 2, is_selected ? in->curso_pos : -10);
        char *n = (char*) malloc(sizeof(char));
        n[0] = 'n';
        draw_char(in->x - 3, in->y, n, 0x0000, 0x0000, 1);
    }
}

void update_fill_box(fill_box *in, int event, bool snd)
{
    int size_after_cursor =0;
    while(size_after_cursor<100 && in->text[in->curso_pos+size_after_cursor]!='\0')
        size_after_cursor++;
    char * temp =(char*) malloc(sizeof(char)*size_after_cursor);
    for(int i = 0 ; i < size_after_cursor;i++){
        temp[i]=in->text[in->curso_pos+i];
    }
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
        
            if (in->t_size > 0) {
                
                int size_text_after=0;
                while(size_text_after<100 && in->text[in->curso_pos+size_after_cursor]!='\0'){
                    size_after_cursor++;
                }
                char * temp = (char *)malloc(sizeof(char)*size_text_after);
                for(int i =0 ; i < size_after_cursor;i++){
                    temp[i]=in->text[in->curso_pos+i];
                }

                in->t_size--;
                for(int i =0 ; i < size_after_cursor;i++){
                    in->text[in->curso_pos+i]=temp[i];
                }
                free(temp);
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
    for(int i = 0 ; i < size_after_cursor;i++){
        in->text[in->curso_pos+i]=temp[i];
    }
    free(temp);
}

int * get_depth(char * in, int input_size){
    int * depth = (int*)malloc(sizeof(int)*input_size);
    for(int i = 0 ; i < input_size;i++){
        depth[i]=0;
    }
    int m=0;
    int cur_depth =0;   
    for(int searched_depth = 10 ; searched_depth>=0;searched_depth--){      //A optimiser, inneficient
    for(int i = 0 ; i < input_size;i++){
        if(in[i]=='(')
            cur_depth++;
        if(in[i]==')')
            cur_depth--;
        if(cur_depth==searched_depth && in[i]=='/'){
            int a = 0;
            int loc_max = 0;
            int j=i+1;
            for(j = i+1 ; (j==i+1 || a>0    ) &&  j<input_size;j++){
                if(in[j]=='(')
                    a++;
                if(in[j]==')')
                     a--;
                if(depth[j]>loc_max)
                    loc_max=depth[j];


               
            }
            a = 0;
            int loc_min=0;
            for(j = i-1 ; (j==i-1 ||a<0 )&&  j>=0;j--){
                if(in[j]=='(')
                    a++;
                if(in[j]==')')
                     a--;
                if(depth[j]<loc_min)
                    loc_min=depth[j];

            }
            

      for(j = i-1 ; (j==i-1 ||a<0 )&&  j>=0;j--){
                if(in[j]=='(')
                    a++;
                if(in[j]==')')
                     a--;
                    depth[j]+=-loc_min+1;
            }
            for(j = i+1 ; (j==i+1 || a>0    ) &&  j<input_size;j++){
                if(in[j]=='(')
                    a++;
                if(in[j]==')')
                     a--;
                depth[j]+=-loc_max-1;
            }
         
        }
    }
}
    for(int i = 0 ; i < input_size;i++){
        
        
                        if(depth[i]<m)
                    m=depth[i];
    }
    for(int i = 0 ; i < input_size;i++){
        

        depth[i]-=m;
       //depth[i]=0;
    }
    return depth;
}

float* get_length(char* in, int input_size, int* depth) {
    float* length = (float*)malloc(sizeof(float) * input_size);
    length[0]=0;
     if(is_in(in[0],"uvwijk"))
            length[0]+=3;
        if(is_in(in[0],"fghcst"))
            length[0]+=2;
    for(int i = 1; i < input_size; i++) {
        length[i] = length[i-1]+1;
        if(is_in(in[i],"uvwijk"))
            length[i]+=3;
        if(is_in(in[i],"fghcst"))
            length[i]+=2;
    }
    for(int searched_depth = 10; searched_depth >= 0; searched_depth--) {
        int cur_depth=0;
        for(int i = 0; i < input_size;i++){
            if(in[i]=='('){
                cur_depth++;
            }
            if(in[i]==')'){
                cur_depth--;
            }
            if(in[i]=='/' && cur_depth==searched_depth){
                int a =0;
                int l1=0;
                int j;
                for(j = i-1 ; (j==i-1 ||a<0 )&&  j>0;j--){
                 if(in[j]=='(')
                    a++;
                if(in[j]==')')
                     a--;
                }

                int d = j;
                l1=length[i]-length[max(j,0)];

                int l2=0;
                a=0;
                for(j = i+1 ; (j==i+1 || a>0    ) &&  j<input_size;j++){
                    if(in[j]=='(')
                        a++;
                    if(in[j]==')')
                        a--;
                }
                l2=length[j]-length[i];
                if(l2>l1){
                    for(int j = d+1 ; j<i;j++){
                        length[j]+=(l2-l1)/2.0;
                    }
                    for(int j = i ; j<input_size;j++){
                        length[j]-=l1;
                    }
                }else{

                }
            
            }

            }

        }
            return length;

    }


static token *p_peek(Parser *p) {
    return p->pos < p->n ? &p->toks[p->pos] : nullptr;
}
static token *p_consume(Parser *p) {
    return p->pos < p->n ? &p->toks[p->pos++] : nullptr;
}

static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_term(Parser *p);
static ASTNode *parse_factor(Parser *p);
static ASTNode *parse_base(Parser *p);
static ASTNode *parse_base(Parser *p) {
    token *t = p_peek(p);
    if (!t) return make_placeholder();

    if (t->type == '(') {
     //   bool virt = t->is_virtual;
        p_consume(p);
        ASTNode *inner  = parse_expr(p);
        ASTNode *paren  = alloc_node();
        paren->type     = N_PARENTHESIS;  
        paren->op       = '(';     
        paren->left     = inner;
        paren->src_pos  = t->src_pos;
        if (p_peek(p) && p_peek(p)->type == ')') {
            paren->right = alloc_node();   
            paren->right->type = N_PLACEHOLDER;
            p_consume(p);
        }
        return paren;
    }

    if (t->type == ')') {
        p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type    = N_PARENTHESIS;
        nd->op      = ')';
        nd->src_pos = t->src_pos;
        return nd;
    }
    if (t->type == 'n' ) {
        
        p_consume(p);
        ASTNode *nd   = alloc_node();
        nd->type      = N_NUMBER;
        nd->number    = t->value;
        nd->src_pos   = t->src_pos;
        return nd;
    }
    if (t->type == 'X') {
        p_consume(p);
        ASTNode *nd   = alloc_node();
        nd->type      = N_VARIABLE;
        nd->variable  = (char)('a' + (int)t->value);
        nd->src_pos   = t->src_pos;
        return nd;
    }
    if (t->type == 'p' || t->type == 'e') {
        p_consume(p);
        ASTNode *nd  = alloc_node();
        nd->type     = N_CONST;
        nd->op       = t->type;
        nd->src_pos  = t->src_pos;
        return nd;
    }
    if (t->type == '(') {
        p_consume(p);
        ASTNode *inner = parse_expr(p);
        if (p_peek(p) && p_peek(p)->type == ')') p_consume(p);
        return inner;
    }

    char *funcs = (char *)"lrcstuvwfghijk";
    if (is_in(t->type, funcs)) {
        token *ft = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type    = N_FUNC;
        nd->op      = ft->type;
        nd->src_pos = ft->src_pos;
        if (p_peek(p) && p_peek(p)->type == '(') p_consume(p);
        token *next = p_peek(p);
        if (!next || next->type == ')')
            nd->left = make_placeholder();
        else
            nd->left = parse_expr(p);
        if (p_peek(p) && p_peek(p)->type == ')') p_consume(p);
        return nd;
    }

    return make_placeholder();
}

static ASTNode *parse_factor(Parser *p) {
    ASTNode *base = parse_base(p);
    token   *t    = p_peek(p);
    if (t && t->type == '^') {
        token *ot = p_consume(p);
        ASTNode *nd  = alloc_node();
        nd->type     = N_BINOP;
        nd->op       = '^';
        nd->src_pos  = ot->src_pos;
        nd->left     = base;
        nd->right    = parse_factor(p);
        return nd;
    }
    return base;
}

static ASTNode *parse_term(Parser *p) {
    ASTNode *left = parse_factor(p);
    while (true) {
        token *t = p_peek(p);
        if (!t || (t->type != '*' && t->type != '/')) break;
        token *ot   = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type    = N_BINOP;
        nd->op      = ot->type;
        nd->src_pos = ot->src_pos;
        nd->left    = left;
        nd->right   = parse_factor(p);
        left        = nd;
    }
    return left;
}

static ASTNode *parse_expr(Parser *p) {
    ASTNode *left = parse_term(p);
    while (true) {
        token *t = p_peek(p);
        if (!t || (t->type != '+' && t->type != '-')) break;
        token *ot   = p_consume(p);
        ASTNode *nd = alloc_node();
        nd->type    = N_BINOP;
        nd->op      = ot->type;
        nd->src_pos = ot->src_pos;
        nd->left    = left;
        nd->right   = parse_term(p);
        left        = nd;
    }
    return left;
}


typedef struct { int w; int h; int baseline; } Dims;
static inline int CW(int S) { return 6 * S; }
static inline int CH(int S) { return 8 * S; }

static int fmt_number(double v, char *buf) {
    if (v == (int)v && v >= 0 && v < 10000000) {
        return sprintf(buf, "%d", (int)v);
    }
    int    int_part = (int)v;
    double dec_part = v - int_part;
    if (dec_part < 0) dec_part = -dec_part;

    int decimals = (int)(dec_part * 1000000 + 0.5);
    int len = sprintf(buf, "%d.", int_part);

    char dec_buf[8];
    int dec_len = sprintf(dec_buf, "%04d", decimals);

    while (dec_len > 1 && dec_buf[dec_len-1] == '0') dec_len--;
    dec_buf[dec_len] = '\0';

    for (int i = 0; i < dec_len; i++) buf[len++] = dec_buf[i];
    buf[len] = '\0';
    return len;
}

static int func_name_width(char op, int SIZE) {
    switch (op) {
        case 'c': case 's': case 't':               return 3*CW(SIZE);
        case 'u': case 'v': case 'w':
        case 'f': case 'g': case 'h':               return 4*CW(SIZE);
        case 'i': case 'j': case 'k':               return 5*CW(SIZE);
        case 'l':                                   return 2*CW(SIZE);
        default:                                    return   CW(SIZE);
    }
}

static Dims measure(ASTNode *nd, int SIZE) {
    if (!nd) return {CW(SIZE), CH(SIZE), CH(SIZE)/2};
    switch (nd->type) {
      
        
        case N_PLACEHOLDER:
        case N_VARIABLE:
        case N_CONST:
            return {CW(SIZE), CH(SIZE), CH(SIZE)/2};
        case N_NUMBER: {
            char buf[32];
            int nc = fmt_number(nd->number, buf);
            return {nc*CW(SIZE), CH(SIZE), CH(SIZE)/2};
        }
         case N_PARENTHESIS: {
            if (nd->op == ')') {
                return {CW(SIZE), CH(SIZE), CH(SIZE)/2};
            }
            
            Dims c   = measure(nd->left, SIZE);
            int  pw  = CW(SIZE) + (nd->right ? CW(SIZE) : 0);
            return {c.w + pw, c.h, c.baseline};
        }
        case N_BINOP: {
            if (nd->op == '/') {
                Dims num = measure(nd->left,  SIZE);
                Dims den = measure(nd->right, SIZE);
                int  w   = (num.w > den.w ? num.w : den.w) + 4;
                return {w, num.h + den.h + 6, den.h + 3};
            } else if (nd->op == '^') {
                int eS = SIZE > 1 ? SIZE-1 : 1;
                Dims b = measure(nd->left,  SIZE);
                Dims e = measure(nd->right, eS);
                return {b.w + e.w, b.h + e.h/2, b.baseline + e.h/2};
            } else {
                Dims l       = measure(nd->left,  SIZE);
                Dims r       = measure(nd->right, SIZE);
                int ow       = nd->op == '*' ? 0 : CW(SIZE);
                int baseline = l.baseline > r.baseline ? l.baseline : r.baseline;
                int below_l  = l.h - l.baseline;
                int below_r  = r.h - r.baseline;
                int h        = baseline + (below_l > below_r ? below_l : below_r);
                return {l.w + ow + r.w, h, baseline};
            }
        }
        case N_FUNC: {
            if (nd->op == 'r') {
                Dims c = measure(nd->left, SIZE);
                return {CW(SIZE)+2+c.w, c.h+3, c.baseline+3};
            }
            Dims c = measure(nd->left, SIZE);
            int fw = func_name_width(nd->op, SIZE);
            int h  = c.h > CH(SIZE) ? c.h : CH(SIZE);
            return {fw + CW(SIZE) + c.w + CW(SIZE), h, h/2};
        }
    }
    return {CW(SIZE), CH(SIZE), CH(SIZE)/2};
}

extern int x_cursor;
extern int y_cursor;

static int draw_str(int x, int y, const char *s, int SIZE) {
    char buf[2] = {0,0};
    while (*s) { buf[0] = *s++; draw_char(x, y, buf, 0x0000, 0x0000, SIZE); y += CW(SIZE); }
    return y;
}
static int draw_func_name(char op, int x, int y, int SIZE) {
    switch (op) {
        case 'c': return draw_str(x,y,"cos",SIZE);
        case 's': return draw_str(x,y,"sin",SIZE);
        case 't': return draw_str(x,y,"tan",SIZE);
        case 'u': return draw_str(x,y,"acos",SIZE);
        case 'v': return draw_str(x,y,"asin",SIZE);
        case 'w': return draw_str(x,y,"atan",SIZE);
        case 'f': return draw_str(x,y,"cosh",SIZE);
        case 'g': return draw_str(x,y,"sinh",SIZE);
        case 'h': return draw_str(x,y,"tanh",SIZE);
        case 'i': return draw_str(x,y,"acosh",SIZE);
        case 'j': return draw_str(x,y,"asinh",SIZE);
        case 'k': return draw_str(x,y,"atanh",SIZE);
        case 'l': return draw_str(x,y,"ln",SIZE);
        default:  return y;
    }
}
static void update_cursor(ASTNode *nd, int x, int y, int cursor_pos) {
    if (nd->src_pos < 0) return;
    if (cursor_pos - 1 == nd->src_pos) {
        x_cursor = x;
        y_cursor = y + 7;
    }
}

static int render_node(ASTNode *nd, int x, int y, int SIZE, int cursor_pos) {
    if (!nd) return y;
    Dims d = measure(nd, SIZE);
    char buf[2] = {0,0}; char numbuf[32];

    switch (nd->type) {

        case N_PLACEHOLDER: {
            int w = CW(SIZE), h = CH(SIZE);
            fill_rect(x-13,         y,         1, w, 0x0000);
            fill_rect(x + h - 14, y,         1, w, 0x0000);
            fill_rect(x-13,         y,         h, 1, 0x0000);
            fill_rect(x-13,         y + w - 1, h, 1, 0x0000);
            return y + w;
        }

        case N_NUMBER: {
            update_cursor(nd, x, y, cursor_pos);
            fmt_number(nd->number, numbuf);
            return draw_str(x, y, numbuf, SIZE);
        }
        case N_PARENTHESIS: {
            update_cursor(nd, x, y, cursor_pos);
    if (nd->op == ')') {
        buf[0] = ')';
        draw_char(x, y, buf, 0x0000, 0x0000, SIZE);
        return y + CW(SIZE);
    }
    Dims c  = measure(nd->left, SIZE);
    int  xc = x + (c.h - CH(SIZE)) / 2;
    buf[0] = '(';
    draw_char(xc, y, buf, 0x0000, 0x0000, SIZE);
    int cy = render_node(nd->left, x, y + CW(SIZE), SIZE, cursor_pos);
    if (nd->right) {
        buf[0] = ')';
        draw_char(xc, cy, buf, 0x0000, 0x0000, SIZE);
        cy += CW(SIZE);
    }
    return cy;
        }

        case N_VARIABLE: {
            update_cursor(nd, x, y, cursor_pos);
            buf[0] = nd->variable;
            draw_char(x, y, buf, 0x0000, 0x0000, SIZE);
            return y + CW(SIZE);
        }

        case N_CONST: {
            update_cursor(nd, x, y, cursor_pos);
            buf[0] = nd->op;
            draw_char(x, y, buf, 0x0000, 0x0000, SIZE);
            return y + CW(SIZE);
        }
        
        case N_BINOP: {

            if (nd->op == '/') {
                Dims num = measure(nd->left,  SIZE);
                Dims den = measure(nd->right, SIZE);
                int bw   = d.w;
                update_cursor(nd, x + den.h + 2, y + bw/2, cursor_pos);
                render_node(nd->right, x,              y + (bw - den.w)/2, SIZE, cursor_pos);
                fill_rect(x + den.h -SIZE*7+3, y, 2, bw, 0x0000);
                render_node(nd->left,  x + den.h + 5, y + (bw - num.w)/2, SIZE, cursor_pos);
                return y + bw;

            } else if (nd->op == '^') {
                int eS = SIZE > 1 ? SIZE-1 : 1;
                Dims b = measure(nd->left,  SIZE);
                Dims e = measure(nd->right, eS);
                update_cursor(nd, x + e.h/2, y + b.w, cursor_pos);
                render_node(nd->left,  x + e.h/2, y,        SIZE, cursor_pos);
                render_node(nd->right, x+10,          y + b.w,  eS,   cursor_pos);
                return y + d.w;

            } else {
                Dims l = measure(nd->left,  SIZE);
                Dims r = measure(nd->right, SIZE);
                int  h        = d.h;
                int  baseline = d.baseline;
                int cy = render_node(nd->left,  x + (baseline - l.baseline), y,  SIZE, cursor_pos);
                update_cursor(nd, x + (baseline - CH(SIZE)/2), cy, cursor_pos);
                buf[0] = nd->op;
                draw_char(x + (baseline - CH(SIZE)/2), cy, buf, 0x0000, 0x0000, SIZE);
                cy += CW(SIZE);
                return render_node(nd->right, x + (baseline - r.baseline), cy, SIZE, cursor_pos);
            }
        }

        case N_FUNC: {
            update_cursor(nd, x, y, cursor_pos);
            if (nd->op == 'r') {
                Dims c = measure(nd->left, SIZE);
                buf[0] = 'R'; draw_char(x+3, y, buf, 0x0000, 0x0000, SIZE);
                int cy = y + CW(SIZE) + 2;
                fill_rect(x, cy, 1, c.w, 0x0000);
                render_node(nd->left, x+3, cy, SIZE, cursor_pos);
                return cy + c.w;
            } else {
                Dims c = measure(nd->left, SIZE);
                int  h = d.h;
                int cy = draw_func_name(nd->op, x + (h-CH(SIZE))/2, y, SIZE);
                int xc = x + (h-c.h)/2;
                buf[0] = '('; draw_char(xc, cy, buf, 0x0000, 0x0000, SIZE); cy += CW(SIZE);
                cy = render_node(nd->left, xc, cy, SIZE, cursor_pos);
                buf[0] = ')'; draw_char(xc, cy, buf, 0x0000, 0x0000, SIZE);
                return cy + CW(SIZE);
            }
        }
    }
    return y + d.w;
}


void display_equation(char *in, int input_size, int x, int y, int SIZE, int cursor_pos)
{
    x += 10;
    reset_pool();
    if (!in || input_size == 0) return;

    int    tok_n = 0;
    token *toks  = parse_string_to_token(in, input_size, &tok_n);
    Parser p     = { toks, tok_n, 0 };
    int    cy    = y + 5;

    while (p.pos < p.n) {
        int before   = p.pos;
        ASTNode *node = parse_expr(&p);
        if (node) cy = render_node(node, x + 14, cy, SIZE, cursor_pos);
        if (p.pos == before) p.pos++; 
    }

    free(toks);
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

int menu_tools()
{
    text_box **items = (text_box **)malloc(sizeof(text_box *) * 6);
    for (int i = 0; i < 6; i++)
    {
        items[i] = create_text_box(32, i * 30, 30, 256, 0, false);
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

    for (int i = 0; i < 3; i++)
    {
        items[i] = create_text_box(60, i * 30, 30, 200, 2, false);
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
    while (true)
    {
        for (int i = pos; i < pos + 3; i++)
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

    for (int i = 0; i < 6; i++)
    {
        items[i] = create_text_box(60, i * 30, 30, 200, 2, false);
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

int menu_const_phy()
{
    text_box **items = (text_box **)malloc(sizeof(text_box *) * 6);
    fill_rect(0, 32, 120, 256, 0x311f);

    for (int i = 0; i < 6; i++)
    {
        items[i] = create_text_box(60, i * 30, 30, 200, 2, false);
    }
    items[0]->text = "Pesanteur g";
    items[1]->text = "Celerite c";
    items[2]->text = "Constante de gravitation universelle G";

    items[3]->text = "Constante de planque h";
    items[4]->text = "Charge elementaire e";
    items[5]->text = "Masse electron m";

    items[6]->text = "Nombre d'avogadro N";
    items[7]->text = "Constante des gaz parfait R";


    items[0]->t_size = 11;
    items[1]->t_size = 10;
    items[2]->t_size = 38;
    items[3]->t_size = 22;
    items[4]->t_size = 20;
    items[5]->t_size = 16;
    items[6]->t_size = 19;
    items[7]->t_size = 27;

    items[0]->allign = 'r';
    items[1]->allign = 'r';
    items[2]->allign = 'r';
    items[3]->allign = 'r';
    items[4]->allign = 'r';
    items[5]->allign = 'r';
    items[6]->allign = 'r';
    items[7]->allign = 'r';

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
