#include <vector>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <bits/stdc++.h>
#include <string.h>
#include <string>

#include "headers/ui.h"
#include "headers/menu.h"
#include "headers/Evaluator.h"
struct coord_s
{
    int x;
    int y;
};

typedef struct coord_s coord;
extern int x_cursor;
extern int y_cursor;

void ili_cmd(uint8_t cmd)
{
    gpio_put(PIN_DC, 0);
    gpio_put(PIN_CS, 0);
    spi_write_blocking(spi0, &cmd, 1);
    gpio_put(PIN_CS, 1);
}

void ili_data(uint8_t data)
{
    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);
    spi_write_blocking(spi0, &data, 1);
    gpio_put(PIN_CS, 1);
}

void ili_reset()
{
    gpio_put(PIN_RST, 0);
    sleep_ms(50);
    gpio_put(PIN_RST, 1);
    sleep_ms(50);
}

void ili_init()
{
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
}

void fill_screen(uint16_t color)
{
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
    for (int i = 0; i < 320 * 240; ++i)
    {
        uint8_t hi = color >> 8;
        uint8_t lo = color & 0xFF;
        uint8_t data[2] = {hi, lo};
        spi_write_blocking(spi0, data, 2);
    }
    gpio_put(PIN_CS, 1);
}

void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
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

// Préparation du buffer (statique pour éviter la pile)
#define BUF_PIXELS 1024
    static uint8_t buf[BUF_PIXELS * 2];
    const uint8_t hi = color >> 8;
    const uint8_t lo = color & 0xFF;

    // Remplir le buffer avec la couleur
    for (int i = 0; i < BUF_PIXELS; ++i)
    {
        buf[2 * i] = hi;
        buf[2 * i + 1] = lo;
    }

    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

    int total_pixels = w * h;
    while (total_pixels > 0)
    {
        int batch = (total_pixels > BUF_PIXELS) ? BUF_PIXELS : total_pixels;
        spi_write_blocking(spi0, buf, batch * 2);
        total_pixels -= batch;
    }

    gpio_put(PIN_CS, 1);
}

void draw_char(uint16_t x, uint16_t y, char *c, uint16_t color, uint16_t bg, uint8_t size)
{

    int i = 0;
    while (c[i] != '\0')
    {
        if (c[i] < 32 || c[i] > 126)
            return; // Caractères imprimables uniquement
        const uint8_t *glyph = &font5x7[(c[i] - 32) * 5];

        for (int col = 0; col < 5; col++)
        {
            uint8_t line = glyph[col];
            for (int row = 0; row < 8; row++)
            {
                // uint16_t pixel_color = (line & 0x01) ? color : bg;
                if (line & 0x01)
                    fill_rect(x - row * size, y + col * size, size, size, color);

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

    for (int i = 0; i < 5; i++)
        fill_rect(x, y + i * 6, 13, 1, 0x0000);
    fill_rect(x, y, 1, 25, 0x0000);
    fill_rect(x + 13, y, 1, 25, 0x0000);

    fill_rect(x + 5, y + 25, 4, 2, level == 5 ? 0x1dc0 : 0xe503);

    fill_rect(x + 4, y + 25, 1, 3, 0x0000);
    fill_rect(x + 9, y + 25, 1, 3, 0x0000);
    fill_rect(x + 5, y + 27, 4, 1, 0x0000);
}

int min(int a, int b)
{
    if (a < b)
        return a;
    return b;
}

int max(int a, int b)
{
    if (a > b)
        return a;
    return b;
}

void axis()
{
    fill_rect(0, 159, 220, 2, 0x0000);
    fill_rect(120, 0, 2, 340, 0x0000);
}

fill_box *create_fill_box(int x, int y, int h, int w, int border)
{
    fill_box *out = (fill_box *)malloc(sizeof(fill_box));
    out->curso_pos = 0;
    out->x = x;
    out->y = y;
    out->h = h;
    out->w = w;
    out->border = border;
    out->color = 0xffff;
    out->t_size = 0;
    char *in = (char *)malloc(sizeof(char) * 100);
    for (int i = 0; i < 100; i++)
        in[i] = '\0';
    out->text = in;
    return out;
}

text_box *create_text_box(int x, int y, int h, int w, int border, bool transparent)
{
    text_box *out = (text_box *)malloc(sizeof(text_box));
    out->transparent = transparent;
    out->x = x;
    out->y = y;
    out->h = h;
    out->w = w;
    out->border = border;
    out->t_size = 0;
    out->col = 0xffff;
    char *in = (char *)malloc(sizeof(char) * 100);
    for (int i = 0; i < 100; i++)
        in[i] = '\0';
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

void display_text_box(text_box *in, int shift_y, bool is_selected)
{
    if (in == nullptr)
        return;

    if (!is_selected)
        fill_rect(in->y, in->x, in->h, in->w, in->col);
    else
        fill_rect(in->y, in->x, in->h, in->w, 0xfff0);

    draw_char(in->y + shift_y + in->h - 10, (in->w - in->t_size * 7) / 2 + in->x, in->text, 0x0000, 0xFF, 1);

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

void display_fill_box(fill_box *in, int shift_y, bool is_selected, int pos, char prefix)
{
    if (in == nullptr)
        return;
    if (!is_selected)
        fill_rect(in->y + shift_y, in->x, in->h, in->w, in->color);
    else
        fill_rect(in->y + shift_y, in->x, in->h, in->w, 0xfff0);

    display_equation(in->text, 100, in->y + shift_y, in->x + (prefix == ' ' ? 0 : 55), 2, is_selected ? in->curso_pos : -10);

    if (prefix == 'f')
    {

        char *t = (char *)malloc(sizeof(char *) * 5);
        t[0] = 'f' + pos;
        t[1] = '(';
        t[2] = 'x';
        t[3] = ')';
        t[4] = '=';
        display_equation(t, 5 /*+log(pos)*/, in->y + shift_y, in->x, 2, is_selected ? in->curso_pos : -10);
    }
    if (prefix == 'u')
    {

        char *t = (char *)malloc(sizeof(char *) * 2);
        t[0] = 'u' + pos;
        t[1] = '=';
        display_equation(t, 2 /*+log(pos)*/, in->y + shift_y, in->x, 2, is_selected ? in->curso_pos : -10);
        char *n = (char *)malloc(sizeof(char));
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
    if (snd)
    {
        switch (event)
        {
            {
            case ZERO:
                 in->text[in->curso_pos]='U';
                        in->curso_pos++;

            in->t_size++;
                break;
            case ONE:
               in->text[in->curso_pos]='P';
                        in->curso_pos++;

            in->t_size++;
                break;
            case TWO:
                in->text[in->curso_pos]='Q';
                        in->curso_pos++;

            in->t_size++;
                break;
            case THREE:
                in->text[in->curso_pos]='R';
                        in->curso_pos++;

            in->t_size++;
                break;
            case EQUAL:
                in->text[in->curso_pos] = '=';

                in->curso_pos++;
                in->t_size++;

                break;
            case FOUR:
                 in->text[in->curso_pos]='K';
                        in->curso_pos++;

            in->t_size++;
                break;
            case FIVE:
               in->text[in->curso_pos]='L';
                        in->curso_pos++;

            in->t_size++;
                break;
            case SIX:
                 in->text[in->curso_pos]='M';
                        in->curso_pos++;

            in->t_size++;
                break;
            case SEVEN:
               in->text[in->curso_pos]='F';
                        in->curso_pos++;

            in->t_size++;
                break;
            case EIGHT: 
            in->text[in->curso_pos]='G';
                        in->curso_pos++;

            in->t_size++;
                break;
            case NINE:
             in->text[in->curso_pos]='H';
                        in->curso_pos++;

            in->t_size++;
                break;
            case PLUS:
               in->text[in->curso_pos]='S';
                        in->curso_pos++;

            in->t_size++;
                break;
            case MINUS:
                in->text[in->curso_pos]='N';
                        in->curso_pos++;

            in->t_size++;
                break;
            case TIMES:
             in->text[in->curso_pos]='T';
                        in->curso_pos++;

            in->t_size++;
                break;
            case DIVIDE:
                in->text[in->curso_pos]='O';
                        in->curso_pos++;

            in->t_size++;
                break;

            case OPENING_PARENTHESIS: 
            in->text[in->curso_pos]='I';
                        in->curso_pos++;

            in->t_size++;
                break;

            case CLOSING_PARENTHESIS:
             in->text[in->curso_pos]='J';
                        in->curso_pos++;

            in->t_size++;

                break;

            case LN:
                in->text[in->curso_pos] = 'l';
                in->text[in->curso_pos + 1] = '(';
                in->text[in->curso_pos + 2] = ')';
                in->t_size += 3;
                in->curso_pos += 2;

                break;
            case E:
                in->text[in->curso_pos] = 'e';

                in->curso_pos += 2;
                in->t_size++;

                break;
            case X:

                in->text[in->curso_pos] = 'X';
                in->curso_pos++;

                in->t_size++;

                break;
            case COMA:
               in->text[in->curso_pos]='V';
                        in->curso_pos++;

            in->t_size++;
                break;
            case PI:
                in->text[in->curso_pos]='W';
                        in->curso_pos++;

            in->t_size++;
                break;

            case COS:
                in->text[in->curso_pos] = 'A';
                in->curso_pos++;

                in->t_size++;
                break;

            case SIN:
                in->text[in->curso_pos] = 'B';
                in->curso_pos++;

                in->t_size++;
                break;

            case TAN:
 in->text[in->curso_pos]='C';
                        in->curso_pos++;

            in->t_size++;
                break;
            case SQRT:

                in->text[in->curso_pos]='D';
                        in->curso_pos++;

            in->t_size++;
                break;
            case POW:

             in->text[in->curso_pos]='E';
                        in->curso_pos++;

            in->t_size++;
                break;

            case BACK:
                in->curso_pos--;

                if (in->t_size > 0)
                {
                    in->t_size--;
                    in->text[in->t_size] = '\0';
                }
                break;
            default:
                break;
            }
        }
    }
    else
    {
        switch (event)
        {
        case ZERO:
            in->text[in->curso_pos] = '0';
            in->curso_pos++;

            in->t_size++;
            break;
        case ONE:
            in->text[in->curso_pos] = '1';
            in->curso_pos++;

            in->t_size++;
            break;
        case TWO:
            in->text[in->curso_pos] = '2';
            in->curso_pos++;

            in->t_size++;
            break;
        case THREE:
            in->text[in->curso_pos] = '3';

            in->curso_pos++;
            in->t_size++;
            break;
        case EQUAL:
            in->text[in->curso_pos] = '=';

            in->curso_pos++;
            in->t_size++;

            break;
        case FOUR:
            in->text[in->curso_pos] = '4';
            in->curso_pos++;
            in->t_size++;
            break;
        case FIVE:
            in->text[in->curso_pos] = '5';
            in->curso_pos++;
            in->t_size++;
            break;
        case SIX:
            in->text[in->curso_pos] = '6';
            in->curso_pos++;
            in->t_size++;
            break;
        case SEVEN:
            in->text[in->curso_pos] = '7';
            in->curso_pos++;
            in->t_size++;
            break;
        case EIGHT:
            in->text[in->curso_pos] = '8';
            in->curso_pos++;
            in->t_size++;
            break;
        case NINE:
            in->text[in->curso_pos] = '9';
            in->curso_pos++;
            in->t_size++;
            break;
        case PLUS:
            in->text[in->curso_pos] = '+';
            in->curso_pos++;
            in->t_size++;
            break;
        case MINUS:
            in->text[in->curso_pos] = '-';
            in->curso_pos++;
            in->t_size++;
            break;
        case TIMES:
            in->text[in->curso_pos] = '*';
            in->curso_pos++;
            in->t_size++;
            break;
        case DIVIDE:
            in->text[in->curso_pos] = '/';
            in->curso_pos++;
            in->t_size++;
            break;

        case OPENING_PARENTHESIS:
            in->text[in->curso_pos] = '(';
            in->curso_pos++;
            in->t_size++;
            break;

        case CLOSING_PARENTHESIS:
            in->text[in->curso_pos] = ')';
            in->curso_pos++;
            in->t_size++;

            break;

        case LN:
            in->text[in->curso_pos] = 'l';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->t_size += 3;
            in->curso_pos += 2;

            break;
        case E:
            in->text[in->curso_pos] = 'e';

            in->curso_pos += 2;
            in->t_size++;

            break;
        case X:

            in->text[in->curso_pos] = 'X';
            in->curso_pos++;

            in->t_size++;

            break;
        case COMA:
            in->text[in->curso_pos] = '.';
            in->curso_pos++;

            in->t_size++;

            break;
        case PI:
            in->text[in->curso_pos] = 'p';
            in->curso_pos++;

            in->t_size++;

            break;

        case COS:
            in->text[in->curso_pos] = 'c';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 2;

            in->t_size += 3;
            break;

        case SIN:

            in->text[in->curso_pos] = 's';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 2;

            in->t_size += 3;
            break;

        case TAN:

            in->text[in->curso_pos] = 't';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 2;

            in->t_size += 3;
            break;
        case SQRT:

            in->text[in->curso_pos] = 'r';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 2;

            in->t_size += 3;
            break;
        case POW:

            in->text[in->curso_pos] = '^';
            in->text[in->curso_pos + 1] = '(';
            in->text[in->curso_pos + 2] = ')';
            in->curso_pos += 2;

            in->t_size += 3;
            break;

        case BACK:
            in->curso_pos--;

            if (in->t_size > 0)
            {
                in->t_size--;
                in->text[in->t_size] = '\0';
            }
            break;
        default:
            break;
        }
    }
    /*for(int i = 0 ; i <= in->t_size-in_tsize;i++)
    {
        char temp = in->text[i+in->curso_pos];
        in->text[i+in->curso_pos]=temp1;
        temp1=temp2;
        temp2=temp3;
        temp3=temp;
    }*/
    // NE FONCTIONNE PAS EN L'ETAT A REPARER
}

void display_equation(char *in, int input_size, int x, int y, int SIZE, int cursor_pos)
{
    x += 25;

    int pos = 0;
    char *cos = (char *)malloc(sizeof(char) * 4);
    char *sin = (char *)malloc(sizeof(char) * 4);
    char *tan = (char *)malloc(sizeof(char) * 4);
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

    char *temp = (char *)malloc(sizeof(char) * 2);
    temp[1] = '\0';

    for (int i = 0; i < input_size; i++)
    {
        if (i == cursor_pos - 1)
        {
            x_cursor = x;
            y_cursor = y + 5 + pos + 7;
        }
        if (is_in(in[i], "0123456789,.+-*()/Xx^=fghijkABCDEFGHIJKLMNOPQRSTUVWYZ"))
        {
            temp[0] = in[i];
            draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, SIZE);
            pos += 5.5 * SIZE;
        }
        else if (is_in(in[i], "uvw"))
        {
            temp[0] = 'a';
            pos += 9.5 * SIZE;
            draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
            pos += 6 * SIZE;
            if (in[i] == 'u')
                draw_char(x, y + 5 + pos, cos, 0X0000, 0X0000, 2);

            if (in[i] == 'v')
                draw_char(x, y + 5 + pos, sin, 0X0000, 0X0000, 2);
            if (in[i] == 'w')
                draw_char(x, y + 5 + pos, tan, 0X0000, 0X0000, 2);
        }
        else if (in[i] == 'c')
        {
            draw_char(x, y + 5 + pos, cos, 0X0000, 0X0000, 2);
            pos += 12 * SIZE;
        }
        else if (in[i] == 's')
        {
            draw_char(x, y + 5 + pos, sin, 0X0000, 0X0000, 2);
            pos += 13 * SIZE;
        }
        else if (in[i] == 't')
        {
            draw_char(x, y + 5 + pos, tan, 0X0000, 0X0000, 2);
            pos += 13 * SIZE;
        } /*else if (in[i]=='('){
             int j = 1;
             int ind = 1;
             while(i+j<input_size && ind!=0){
                 if(in[i+j]=='('){
                     ind++;
                 }else if (in[i+j]==')'){
                     ind--;
                 }
                 j++;
             }
             if(i+j >= input_size||in[i+j]!='/'){
                 temp[0]= in[i];

                 draw_char(x,y+5+pos,temp,0X0000,0X0000,2);

             pos+=9;
             }
             else{
                 display_equation( &in[i+1] ,j-2, x+pos, y-9,2,cursor_pos);
                 i+=j+1;
                 j=1;
                 ind=1;
                 while(i+j<input_size && ind!=0){
                     if(in[i+j]=='('){
                         ind++;
                     }else if (in[i+j]==')'){
                         ind--;
                     }
                     j++;
                 }
                 if(ind==0){
                 display_equation( &in[i+1] ,j-2, x+pos, y+7,2,cursor_pos);
                 i+=j-1;
                 pos+=SIZE*4*j-14;
                 }
             }

         }*/
        else if (in[i] == 'p')
        {
            temp[0] = 'π';
            draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
            pos += SIZE * 4;
        }
        else if (in[i] == 'e')
        {
            temp[0] = 'e';
            draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
            pos += SIZE * 4;
        }
        else if (in[i] == 'r')
        {
            int ind = 1;
            int j = 2;
            while (i + j < input_size && ind != 0)
            {
                if (in[i + j] == '(')
                {
                    ind++;
                }
                else if (in[i + j] == ')')
                {
                    ind--;
                }
                j++;
            }
            if (ind == 0)
            {
                // temp[0]=(char)251;
                temp[0] = 'R';
                draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
                pos += SIZE * 4;
                display_equation(&in[i + 2], j - 3, x + pos, y - 3, 2, cursor_pos);
                i += j - 1;
                pos += SIZE * 4 * (j - 2);
                //  sf::RectangleShape rectangle(sf::Vector2f(120, 50));
                // rectangle.setSize(sf::Vector2f(8*(j-2), 2));
                // rectangle.setPosition({x+5,y+4});
                // rectangle.setFillColor(sf::Color(20, 20, 20));
                // window->draw(rectangle);
            }
        }
        else if (in[i] == 'l')
        {
            pos += 2;
            temp[0] = 'l';
            draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
            pos += 1.5 * SIZE;
            temp[0] = 'n';
            draw_char(x, y + 5 + pos, temp, 0X0000, 0X0000, 2);
            pos += 4.5 * SIZE;
        }
        else if (in[i] == '^')
        {
            int ind = 1;
            int j = 2;
            while (i + j < input_size && ind != 0)
            {
                if (in[i + j] == '(')
                {
                    ind++;
                }
                else if (in[i + j] == ')')
                {
                    ind--;
                }
                j++;
            }
            if (i + j < input_size)
            {
                display_equation(&in[i + 2], j - 3, x - 17, y + pos, 1, cursor_pos);
                i += j - 1;
                pos += 2.5 * (j - 1);
            }
            if (i + j == input_size)
            {
                display_equation(&in[i + 2], j - 3, x - 17, y + pos, 1, cursor_pos);
                i += j;
                pos += 2.5 * (j - 1);
            }
        }
    }
}

void blink_cursor()
{
    char *temp = (char *)malloc(sizeof(char) * 2);

    temp[0] = '|';
    temp[1] = '\0';

    if (to_ms_since_boot(get_absolute_time()) % 1500 < 750)
    {
        draw_char(x_cursor, y_cursor, temp, 0X0000, 0X0000, 2);
    }
    else
    {
        draw_char(x_cursor, y_cursor, temp, BACKGROUND_COLOR, BACKGROUND_COLOR, 2);
    };
}

/*

coord display_tree_expr(operation *in){
    operation * el1=(operation *)in->el1;
    operation * el2=(operation *)in->el1;
    int pos = 0;
    switch (in->operator_type)
    {
        case '+':
        display_tree_expr()
        break;
    default:
        break;
    }
}
*/

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

        // fill_rect(in->x,in->y,in->w,in->h,FRONTGROUND_COLOR);

        //   fill_rect(in->x+in->border,in->y+in->border,in->w-2*in->border,in->h-2*in->border,col);
    }

    fill_rect(in->x + in->w * 0.05, in->y + in->h * 0.6, in->h * 0.2, in->w * 0.9 * (((float)in->val / in->grad)), 0x00FF);

    /* sf::Vector2f sd(in->w*0.9*(1.0-((float)in->val/in->grad)),in->h*0.2);
     sf::RectangleShape barreb(sd);
     barreb.setPosition();
     barreb.setFillColor(sf::Color::Black);
     window->draw(barreb);
 */
    fill_rect(in->x + in->w * 0.05 + in->w * 0.9 * (((float)in->val / in->grad)), in->h * 0.2, in->y + in->h * 0.6, in->w * 0.9 * (1.0 - ((float)in->val / in->grad)), 0xF0F0);
}