#ifndef UI_H
#define UI_H
#include <stdint.h>
#include "display.h"
#include "Tools.h"
#include "Evaluator.h"

const int HISTORY_SIZE = 50;

typedef struct
{
    token *toks;
    int n;
    int pos;
} Parser;

int min(int a, int b);

int max(int a, int b);

enum touches
{
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
typedef enum
{
    N_NUMBER,
    N_VARIABLE,
    N_CONST,
    N_BINOP,
    N_FUNC,
    N_PLACEHOLDER,
    N_PARENTHESIS,
} NodeType;

typedef struct ASTNode
{
    NodeType type;
    char op;
    double number;
    char variable;
    int src_pos;
    struct ASTNode *left;
    struct ASTNode *right;
} ASTNode;
typedef struct
{
    int w;
    int h;
    int baseline;
} Dims;

void axis(double x_min, double x_max, double y_min, double y_max);
ASTNode *parse_equation(Parser *p);

Dims measure(ASTNode *nd, int SIZE);

int fmt_number(double v, char *buf);

void display_text(int x, int y, char *t, int SIZE, int t_size);

void display_equation(char *in, int input_size, int x, int y, int SIZE, int cursor_pos,bool left);

void draw_char(uint16_t x, uint16_t y, char *c, uint16_t color, uint16_t bg, uint8_t size);

void draw_buffer(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *buffer);

#endif
