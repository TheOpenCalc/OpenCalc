#include "headers/widgets.h"
#include "headers/display.h"
#include "headers/ui.h"
#include <malloc.h>
#include "headers/colors.h"

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
