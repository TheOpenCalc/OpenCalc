#ifndef TEST_STUB_UI_H
#define TEST_STUB_UI_H
#include <stdint.h>

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

typedef enum {
    COMA,
    ZERO,
    OPENING_PARENTHESIS,
    CLOSING_PARENTHESIS,
    ENTER,
    NOT2,
    NOT3,
    ONE,
    TWO,
    THREE,
    PLUS,
    ANS,
    NOT4,
    NOT5,
    FOUR,
    FIVE,
    SIX,
    MINUS,
    PI,
    NOT6,
    NOT7,
    SEVEN,
    EIGHT,
    NINE,
    TIMES,
    POW,
    NNNN,
    NN,
    NNN,
    COS,
    SIN,
    TAN,
    DIVIDE,
    SQRT,
    X,
    TOOLS,
    HOME,
    CONST,
    BACK,
    E,
    LOG,
    LN,
    DOWN,
    LEFT,
    OK,
    RIGHT,
    PHY,
    NOTN,
    EQUAL,
    SECOND,
    ALPHA,
    PASS4,
    UP,
    END_KEYS,
    FACT,
    COSH,
    SINH,
    TANH,
    ACOSH,
    ASINH,
    ATANH,
    IMAGINARY,
    CONJ,
    ARG,
    MOD,
    ACOS,
    ASIN,
    ATAN,
    MATRIX_2X2,
    MATRIX_3X3,
    MATRIX_4X4,
    MATRIX_2X3,
    MATRIX_3X2,
    MATRIX_CUSTOM,
    FUNC_DET,
    FUNC_INV,
    FUNC_TRANS,
    FUNC_BINOM,
    FUNC_PERM,
} touches;

static uint16_t g_fb[240][320];
static inline void fill_screen(uint16_t c)
{
    for (int x = 0; x < 240; x++)
        for (int y = 0; y < 320; y++)
            g_fb[x][y] = c;
}
static inline void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    for (int xx = x; xx < x + w && xx < 240; xx++)
        for (int yy = y; yy < y + h && yy < 320; yy++)
            g_fb[xx][yy] = color;
}
static inline void draw_char(uint16_t x, uint16_t y, char *c, uint16_t color, uint16_t bg, uint8_t size)
{
    fill_rect(x, y, 8 * size, 5 * size, bg);
    (void)c;
    (void)color;
}
static inline void draw_num(uint16_t x, uint16_t y, char *c, uint16_t color, uint16_t bg, uint8_t size)
{
    (void)x;
    (void)y;
    (void)c;
    (void)color;
    (void)bg;
    (void)size;
}
static inline void display_battery(uint16_t x, uint16_t y, uint8_t n)
{
    (void)x;
    (void)y;
    (void)n;
}
static inline void flush_display(void)
{
}
#endif
