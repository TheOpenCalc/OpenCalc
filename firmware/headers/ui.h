#ifndef UI_H
#define UI_H
#include <stdint.h>
#include "headers/display.h"
#include "Tools.h"



#define BACKGROUND_COLOR 0xf7de
#define FRONTGROUND_COLOR 0xdefb
#define FRONTGROUND_COLOR_BIS 0xce79


const int HISTORY_SIZE = 50;


int min(int a, int b);

int max(int a, int b);



enum touches  {
        ZERO,
    COMA,
    PI,
    NOT2,
    ENTER,
    ONE,
    TWO,
    THREE,
    PLUS,
    TIMES,
    FOUR,
    FIVE,
    SIX,
  MINUS,
    DIVIDE,
    SEVEN,
    EIGHT,
    NINE,
    OPENING_PARENTHESIS,
    CLOSING_PARENTHESIS,
    COS,
    SIN,
    TAN,
    SQRT,
    POW,
     LN,
    BACK,
    E,
    DOWN,
    RIGHT,
     X,
    TOOLS,
        EQUAL,
 LEFT,
    OK,
    PASS3,
    SECOND,
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
    
   
};

void axis();

void display_text(int x, int y, char * t,int SIZE, int t_size);

void display_equation(char *in, int input_size, int x, int y, int SIZE, int cursor_pos);


void draw_char(uint16_t x, uint16_t y, char *c, uint16_t color, uint16_t bg, uint8_t size);

void draw_buffer(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *buffer);


#endif
