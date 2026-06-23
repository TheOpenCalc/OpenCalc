#ifndef WIDGETS_H
#define WIDGETS_H
#include <stdint.h>

struct fill_box_s
{
    int border;
    char *text;
    int t_size;
    int x, y;
    int h, w;
    int curso_pos;
    uint16_t color;
};

struct text_box_s
{
    int border;
    char *text;
    int t_size;
    int x, y;
    int h, w;
    int display_text_size;
    bool transparent;
    uint16_t col;
    char allign;
};

struct pontentiometer_s
{
    int border;
    int x, y;
    int h, w;
    int grad;
    char *name;
    int val;
    bool transparent_back;
};

typedef struct fill_box_s fill_box;

typedef struct text_box_s text_box;

typedef struct pontentiometer_s pontentiometer;

void fill_box_left(fill_box *in);
void fill_box_right(fill_box *in);
fill_box *create_fill_box(int x, int y, int h, int w, int border);

void display_text_box(text_box *in, int shift_y, int shift_text, bool is_selected);
text_box *create_text_box(int x, int y, int h, int w, int border, bool transparent);
void update_fill_box(fill_box *in, int event, bool snd);

void display_fill_box(fill_box *in, int shift_y, bool is_selected, int pos, char prefix,bool left);

void display_potentiometer(pontentiometer *in, bool is_selected);

pontentiometer *create_potentiometer(int x, int y, int h, int w, int border, char *name, int grad, bool transparent_back);

void increment_potentiometer(pontentiometer *p);

void decrement_potentiometer(pontentiometer *p);

void blink_cursor();

int menu_tools();

int menu_proba();

int menu_trigo();
#endif WIDGETS_H