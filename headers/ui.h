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
    COS,
    SIN,
    TAN,
    DIVIDE,

    SQRT,

    NNN,
    NNNNN,
    X,
        TOOLS,
HOME,CONST,
    BACK,
    NOTTTT,NOTTT,
    E,
    LOG,
    LN,


    DOWN,
    LEFT,
    OK,
    RIGHT,
    PHY,NOTN,
    EQUAL,
    SECOND,ALPHA,
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
    FUNC_PGCD,
    FUNC_PPCM,
    FUNC_QUOT,
    FUNC_RESTE,
    FUNC_ABS,
    FUNC_ARRONDI,
    SEQ_ARITH,
    SEQ_GEOM,
    SEQ_ARIGEO,
    SEQ_ARITHN,
    FUNC_EXP,
    FUNC_MIN,
    FUNC_MAX,
    FUNC_FLOOR,
    FUNC_SIGN,
    FUNC_ROOT,
    FUNC_RAND,
        FUNC_RANDINT,
    CST_G,
    CST_C,
    CST_GRAV,
    CST_PLANCK,
    CST_QE,
    CST_ME,
    CST_NA,
    CST_R,
    FUNC_SUM,
    FUNC_PROD,
    FUNC_DERIV,
    FUNC_INTEG,
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
    N_MATRIX,
    N_NEG,
} NodeType;

typedef struct ASTNode
{
    NodeType type;
    char op;
    double number;
    char variable;
    int src_pos;
    int src_close; // indice source du ')' fermant (parentheses), -1 sinon
    struct ASTNode *left;
    struct ASTNode *right;
    matrix *mat;
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

void display_equation(char *in, int input_size, int x, int y, int SIZE, int cursor_pos, bool left, int scroll, int clip_lo, int clip_hi);

// Position of the text cursor after the last display_equation() call.
// Returns false when the cursor does not fall on a rendered node.
bool display_get_cursor(int *y);

void display_set_source(char *src, int len);

#define MAT_LIT_MAX 144

typedef struct
{
    int rows;
    int cols;
    int start;
    int span;
    int cell_start[MAT_LIT_MAX];
    int cell_len[MAT_LIT_MAX];
} matrix_lit;

int matrix_parse_literal(const char *s, int len, int start, matrix_lit *L);

typedef struct
{
    int active;
    int bar_x, bar_y, bar_w, bar_h;
    int cell_x, cell_y, cell_w, cell_h;
    const char *text;
    int text_len, text_x, text_y, gs;
    uint16_t bg;
} matrix_blink_state;

void matrix_blink_save(matrix_blink_state *s);
void matrix_blink_restore(const matrix_blink_state *s);

void draw_char(uint16_t x, uint16_t y, char *c, uint16_t color, uint16_t bg, uint8_t size);

void draw_buffer(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *buffer);

#endif