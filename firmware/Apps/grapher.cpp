#include "headers/ui.h"
#include "headers/Evaluator.h"
#include "headers/grapher.h"
#include <math.h>
#include "headers/menu.h"
#include <algorithm>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <cstdio>
#include "headers/colors.h"

int X_cursor =0;
int Y_cursor =0;

double X_pos=0;
double Y_pos=0;


void wu_line(int y0, int x0, int y1, int x1, uint16_t color)
{
    int steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep) {
        int tmp = x0;
        x0 = y0;
        y0 = tmp;

        tmp = x1;
        x1 = y1;
        y1 = tmp;
    }
    if (x0 > x1) {
        int tmp = x0;
        x0 = x1;
        x1 = tmp;
        
        tmp = y0;
        y0 = y1; 
        y1 = tmp;
    }

    float dx = x1 - x0;
    float dy = y1 - y0;
    float gradient = dx == 0 ? 1 : dy / dx;
    float y = y0 + gradient * (roundf(x0) - x0);

    for (int x = x0; x <= x1; x++) {
        int y_int = (int)y;
        float frac = y - y_int;

        if (steep) {
            fill_rect(y_int, x, 1, 1, color);       // pixel principal
            fill_rect(y_int + 1, x, 1, 1, (color*10+BACKGROUND_COLOR)/11);   // pixel voisin (antialias)
        } else {
            fill_rect(x, y_int, 1, 1, color);
            fill_rect(x, y_int + 1, 1, 1, color);
        }

        y += gradient;
    }
}

void graph(double cursor_pos, token *function, double x_min, double x_max, double y_min, double y_max, int n, uint16_t color, int id, bool fill_between_points,char var)
{
    double pas = (x_max - x_min) / SCREEN_WIDTH;
    
    token *tokenized_expression = shunting_yard(function, n);
    double pos = x_min;
    int yarded = count_yarded(function, n);

    double last = (evaluate_npi(tokenized_expression, yarded, pos, var) - y_min) / (y_max - y_min) * SCREEN_HEIGHT;
    double y = 0;
    int k = 0;
    if (fill_between_points) {
        k = 1;
    } else {
        k = SCREEN_WIDTH / (x_max - x_min);
    }

    for (int i = 0; i < SCREEN_WIDTH; i += k) {
        y = evaluate_npi(tokenized_expression, yarded, pos, var);

        if (isnan(y)) {
            if (fill_between_points) {
                pos += pas;
            } else {
                pos++;
            }
            last = NAN;
        } else {
            if (last == NAN) {
                last = y;
            }
            double h = (y_max - y_min);
            double display_y = (y - y_min) / h * SCREEN_HEIGHT;
            if (fill_between_points) {
                if (last < 320 && last > 0 && display_y > 0 && display_y < 320) {
                    wu_line((i - 1), last, i, display_y, color);
                }
            } else {
                fill_rect(display_y - 2, i - 2, 3, 3, color);
            }
            if((id!=-1) && pos<cursor_pos && pos+pas>cursor_pos){
                X_cursor=i-1;
                Y_cursor=last;
                X_pos=pos;
                Y_pos=y;

            }
            if (fill_between_points) {
                pos += pas;
            } else {
                pos++;
            }            last = display_y;
        }
    }
    if (id!=-1) {

        text_box *t = create_text_box(0, 0, 20, 107, 2, false);
        char *temp = (char *) malloc(sizeof(char) * 100);
        int s_temp = double_to_string_scientific(cursor_pos, temp);
        
        for (int i = 3; s_temp + 3 > i; i++) {
            t->text[i] = temp[i - 3];
        }
        t->t_size = 3 + s_temp;
        t->text[0] = var;
        t->text[1] = ':';
        t->text[2] = ' ';
        free(temp);
        display_text_box(t, 0, 0, false);

        text_box *tb = create_text_box(107, 0, 20, 107, 2, false);
        char *tempb = (char*) malloc(sizeof(char) * 100);
        int s_tempb = double_to_string_scientific(evaluate_npi(tokenized_expression, yarded, cursor_pos, var), tempb);

        for (int i = 6; s_tempb + 6 > i; i++) {
            tb->text[i] = tempb[i - 6];
        }
        tb->t_size = 6 + s_tempb;
        tb->text[0] = (('f'+id-1 ));

        tb->text[1] = '(';
        tb->text[2] = var;
        tb->text[3] = ')';
        
        tb->text[4] = ':';
        tb->text[5] = ' ';
        free(tempb);
        display_text_box(tb, 0, 0, false);


        

        text_box *tc = create_text_box(214, 0, 20, 106, 2, false);
        char *tempc = (char*) malloc(sizeof(char) * 100);
        char * buff = (char*)malloc(sizeof(char)*20);
        fmt_number((-evaluate_npi(tokenized_expression, yarded, cursor_pos, var)+evaluate_npi(tokenized_expression, yarded, cursor_pos+(x_max-x_min)/100000000, var))/((x_max-x_min)/100000000),buff);

        for (int i = 6; 15 > i; i++) {
            tc->text[i] = buff[i - 6];
        }
        tc->t_size = 7;
        tc->text[0] = (('f'+id-1 ));
        tc->text[1] = '\'';

        tc->text[2] = '(';
        tc->text[3] = var;
        tc->text[4] = ')';
        
        tc->text[5] = ':';
        free(tempc);
        display_text_box(tc, 0, 0, false);
    }
    return;
}

int Grapher()
{
    bool snd = false;
    
    text_box *Graph = create_text_box(160, 220, 20, 160, 1, false);
    text_box *Formula = create_text_box(0, 220, 20, 160, 1, false);
    Graph->text = "Graphe";
    Graph->t_size = 6;
    Formula->text = "Fonctions";
    Formula->t_size = 9;

    fill_box *n = create_fill_box(0, 20, 40, 320, 3);
    const uint16_t palet[] = {
        COLOR_RED,
        COLOR_LIME,
        COLOR_BLUE,
        COLOR_YELLOW,
        COLOR_CYAN,
        COLOR_MAGENTA,
        COLOR_SILVER,
        COLOR_GRAY,
        COLOR_MAROON,
        COLOR_OLIVE,
        COLOR_GREEN,
        COLOR_PURPLE,
        COLOR_TEAL,
        COLOR_NAVY
    };

    fill_box **arr_fill_box = (fill_box**) malloc(sizeof(fill_box*) * 100);
    for (int i = 0; i < 100; i++) {
        arr_fill_box[i] = nullptr;
    }

    int selected_fill_box = 0;
    arr_fill_box[0] = n; 
    int first_display = 0;
    bool show_graph = false;
    double x_min = -5;
    double x_max = 5;
    double y_min = -5;
    double y_max = 5;
    double cursor_pos = 0;
    int last_pressed = scan_keypad();
    fill_screen(BACKGROUND_COLOR);  

    while (1) {
  int mv=0;
        double border =0.25;
       
        if(y_max-Y_pos<(y_max-y_min)*border ){
                            fill_screen(BACKGROUND_COLOR);

            double d = abs(y_max-Y_pos  -(y_max-y_min)*border*2);
            y_max+=d;
            y_min+=d;
        }
        else if(Y_pos-y_min<(y_max-y_min)*border ){
                            fill_screen(BACKGROUND_COLOR);

            double d = abs(Y_pos-y_min  -(y_max-y_min)*border*2);
            y_max-=d;
            y_min-=d;
        }if(x_max-X_pos<(x_max-x_min)*border ){
                            fill_screen(BACKGROUND_COLOR);

            double d = abs(x_max-X_pos  -(x_max-x_min)*border*2);
            x_max+=d;
            x_min+=d;
        }
        else if(X_pos-x_min<(x_max-x_min)*border ){
                            fill_screen(BACKGROUND_COLOR);

            double d = abs(X_pos-x_min  -(x_max-x_min)*border*2);
            x_max-=d;
            x_min-=d;
        }
       

                  if (!show_graph)
        {
            for (int i = std::max(first_display, 0); i < first_display + 6; i++){
            int old_mv = mv;
                int tok_n = 0;
                int input_size;
                token *toks = parse_string_to_token(arr_fill_box[i]->text, arr_fill_box[i]->t_size, &tok_n);
                Parser p = {toks, tok_n, 0};

                while (p.pos < p.n)
                {
                    int before = p.pos;
                    ASTNode *node = parse_equation(&p);
                    if (p.pos == before)
                        p.pos++;
                    if (node)
                        mv += max(0, measure(node, 2).h - 16);
                }
                arr_fill_box[i]->h = mv - old_mv + 40;
//                display_fill_box(arr_fill_box[i], 160 - (i - selected_fill_box + 4) * 40 + old_mv, (i - selected_fill_box) % HISTORY_SIZE == 0, -1, ' ');
            
                display_fill_box(arr_fill_box[i], 160 - (i - std::max(first_display, 0)) * 40-mv, i == selected_fill_box, i, 'f');

            }
        }else {
            axis(x_min,x_max, y_min,y_max);

            for (int i = 0; i < 100; i++) {
                if (arr_fill_box[i] != nullptr) {
                    int yarded = count_yarded(arr_fill_box[i]->text);
                    int tokenized_size = 0;
                    token *tokenized = parse_string_to_token(arr_fill_box[i]->text, arr_fill_box[i]->t_size, &tokenized_size);
                    token *out = shunting_yard(tokenized, tokenized_size);
                    graph(cursor_pos, tokenized, x_min, x_max, y_min, y_max, tokenized_size, palet[i % 14], i == selected_fill_box, true,'X');
                    fill_rect(Y_cursor,X_cursor-5,1,10,0x000000);
                    fill_rect(Y_cursor-5,X_cursor,10,1,0x000000);
       
                }
            }
            
        }
        display_text_box(Formula,0,0,!show_graph && selected_fill_box==-1);
        display_text_box(Graph,0,0,show_graph&& selected_fill_box==-1);
             sleep_ms(150);

        last_pressed=scan_keypad();
        while (last_pressed == -1) {
            if(!show_graph){
                            blink_cursor();
                }
            last_pressed = scan_keypad();
        }
        switch (last_pressed) {
        case SECOND :
            toggle(&snd);
            break;
        case BACK :
            if (!show_graph && arr_fill_box[selected_fill_box]->t_size > 0) {
                                          update_fill_box(arr_fill_box[selected_fill_box], last_pressed,snd);
            } else {
                return 0;
            }
            break;
        case UP :
            if (show_graph) {
                selected_fill_box -= 1;
            } else {
                selected_fill_box = std::max(-1, selected_fill_box - 1);
                if (selected_fill_box < first_display) {
                    first_display = selected_fill_box;
                }
            }
            break;
        case DOWN :
            if(!show_graph) {
                selected_fill_box = std::min(100, selected_fill_box + 1);
                if (arr_fill_box[selected_fill_box] == nullptr) {
                    arr_fill_box[selected_fill_box] = create_fill_box(0, 20, 40, 320, 3);
                }
                if (first_display + 4 < selected_fill_box) {
                    first_display++;
                }
            } else {
                selected_fill_box++;
            }
            break;
        case RIGHT :
            if (show_graph && selected_fill_box != -1) {

                fill_rect(Y_cursor,X_cursor-5,1,10,BACKGROUND_COLOR);
                    fill_rect(Y_cursor-5,X_cursor,10,1,BACKGROUND_COLOR);
                    
                cursor_pos += (x_max - x_min) / 100;
            } else {
                fill_screen(BACKGROUND_COLOR);
                selected_fill_box =- 1;
                show_graph = true;
            }
            break;
        case LEFT :
            if (show_graph && selected_fill_box != -1) {
                                    fill_rect(20,0,200,340,BACKGROUND_COLOR);

                    
                cursor_pos -= (x_max - x_min) / 100;
            } else {
                fill_screen(BACKGROUND_COLOR);
                show_graph = false;
                selected_fill_box =- 1;
            }
            break;
        default :
            if (show_graph) {
                if (last_pressed == PLUS) {
                                        fill_rect(20,0,200,340,BACKGROUND_COLOR);

                    x_min /= 2;
                    x_max /= 2;
                    y_min /= 2;
                    y_max /= 2;
                } else if (last_pressed == MINUS) {
                                                            fill_rect(20,0,200,340,BACKGROUND_COLOR);

                    x_min *= 2;
                    x_max *= 2;
                    y_min *= 2;
                    y_max *= 2;
                }
            } else if (selected_fill_box >= 0 && !show_graph) {
                update_fill_box(arr_fill_box[selected_fill_box], last_pressed, snd);
            }
            break;
        }
    }
}
