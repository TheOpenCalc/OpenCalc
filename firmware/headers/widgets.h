#ifndef WIDGETS_H
#define WIDGETS_H
#include <stdint.h>

extern uint16_t g_cursor_bg;

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
    // Buffer owned by the widget. `text` may be replaced by the caller with
    // a literal (or any other pointer), so only this one may be freed.
    char *text_buf;
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
static int binom_name_start(const char *text, int lparen);
typedef struct fill_box_s fill_box;

typedef struct text_box_s text_box;

typedef struct pontentiometer_s pontentiometer;

void fill_box_left(fill_box *in);
void fill_box_right(fill_box *in);
fill_box *create_fill_box(int x, int y, int h, int w, int border);

void display_text_box(text_box *in, int shift_y, int shift_text, bool is_selected);
text_box *create_text_box(int x, int y, int h, int w, int border, bool transparent);
void free_text_box(text_box *in);
void update_fill_box(fill_box *in, int event, bool snd);

void display_fill_box(fill_box *in, int shift_y, bool is_selected, int pos, char prefix,bool left);

int matrix_cell_edit(fill_box *in, int key, bool text_char);

void display_potentiometer(pontentiometer *in, bool is_selected);

pontentiometer *create_potentiometer(int x, int y, int h, int w, int border, char *name, int grad, bool transparent_back);

void increment_potentiometer(pontentiometer *p);

void decrement_potentiometer(pontentiometer *p);

void blink_cursor();

/* Menu Outils. with_analysis ajoute la rubrique Analyse (somme, produit,
 * derivee, integrale), evaluee seulement par la Calculatrice. */
int menu_tools(bool with_analysis = false);
int menu_analysis();
/* Vrai si le curseur est dans les arguments d'un appel somme / produit /
 * derivee / integrale (la touche X y insere la variable). */
bool cursor_in_calculus_call(const char *text, int cursor);

int menu_proba();

int menu_trigo();

int menu_arithmetic();

int menu_complex();

int menu_matrices();

int menu_const_phy();

// Sous-menus des Outils : nombres decimaux (ent, signe, alea, randint) et
// fonctions (exp, min, max, racine n-ieme).
int menu_decimals();
int menu_functions();

// Blocs prets a l'emploi pour les suites recurrentes (onglet Formule).
int menu_seq_blocks();
#endif