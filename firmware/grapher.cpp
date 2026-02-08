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

#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_LIME      0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_SILVER    0xC618
#define COLOR_GRAY      0x8410
#define COLOR_MAROON    0x8000
#define COLOR_OLIVE     0x8400
#define COLOR_GREEN     0x0400
#define COLOR_PURPLE    0x8010
#define COLOR_TEAL      0x0410
#define COLOR_NAVY      0x0010

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
            fill_rect(y_int + 1, x, 1, 1, color);   // pixel voisin (antialias)
        } else {
            fill_rect(x, y_int, 1, 1, color);
            fill_rect(x, y_int + 1, 1, 1, color);
        }

        y += gradient;
    }
}

void graph(double cursor_pos, token *function, double x_min, double x_max, double y_min, double y_max, int n, uint16_t color, bool is_selected, bool fill_between_points)
{
    double pas = (x_max - x_min) / SCREEN_WIDTH;
    
    token *tokenized_expression = shunting_yard(function, n);
    double pos = x_min;
    int yarded = count_yarded(function, n);

    double last = (evaluate_npi(tokenized_expression, yarded, pos, 'X') - y_min) / (y_max - y_min) * SCREEN_HEIGHT;
    double y = 0;
    int k = 0;
    if (fill_between_points) {
        k = 1;
    } else {
        k = SCREEN_WIDTH / (x_max - x_min);
    }

    for (int i = 0; i < SCREEN_WIDTH; i += k) {
        y = evaluate_npi(tokenized_expression, yarded, pos, 'X');

        if (y == NAN) {
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
            //printf("%f / %f / %f\n", pos, y, display_y);
            if (fill_between_points) {
                if (last < 320 && last > 0 && display_y > 0 && display_y < 320) {
                    wu_line((i - 1), last, i, display_y, color);
                }
            } else {
                fill_rect(display_y - 2, i - 2, 3, 3, color);
            }

            pos += pas;
            last = display_y;
        }
    }
    if (is_selected) {
        text_box *t = create_text_box(0, 0, 20, 160, 2, false);
        char *temp = (char *) malloc(sizeof(char) * 100);
        int s_temp = double_to_string_scientific(cursor_pos, temp);
     
        for (int i = 3; s_temp + 3 > i; i++) {
            t->text[i] = temp[i - 3];
        }
        t->t_size = 3 + s_temp;
        t->text[0] = 'X';
        t->text[1] = ':';
        t->text[2] = ' ';
        free(temp);
        display_text_box(t, 0, 0, false);

        text_box *tb = create_text_box(160, 0, 20, 160, 2, false);
        char *tempb = (char*) malloc(sizeof(char) * 100);
        int s_tempb = double_to_string_scientific(evaluate_npi(tokenized_expression, yarded, cursor_pos, 'X'), tempb);

        for (int i = 3; s_tempb + 3 > i; i++) {
            tb->text[i] = tempb[i - 3];
        }
        tb->t_size = 3 + s_temp;
        tb->text[0] = 'Y';
        tb->text[1] = ':';
        tb->text[2] = ' ';
        free(tempb);
        display_text_box(tb, 0, 0, false);


        // sf::Vector2f s(6.0f, 6.0f);
        // sf::RectangleShape rect(s);
        // rect.setPosition((1 - (cursor_pos - x_max) / (x_min - x_max)) * 320 - 3,
        //                   SCREEN_HEIGHT - (evaluate_npi(tokenized_expression, yarded, cursor_pos, 'X') - y_min) / (y_max - y_min) * SCREEN_HEIGHT - 3);
        // rect.setFillColor(sf::Color(0xffffff));

        // window->draw(rect);
    }
    return;
}

int Grapher()
{
    bool snd = false;
    
    text_box *Graph = create_text_box(160, 220, 20, 160, 1, false);
    text_box *Formula = create_text_box(0, 220, 20, 160, 1, false);
    Graph->text = "Graph";
    Graph->t_size = 5;
    Formula->text = "Formula";
    Formula->t_size = 7;

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
        if (!show_graph) {
            for (int i = std::max(first_display, 0); i < first_display + 6; i++) {
                display_fill_box(arr_fill_box[i], 160 - (i - std::max(first_display, 0)) * 41, i == selected_fill_box, i, 'f');
            }
        } else {
            axis();
            for (int i = 0; i < 100; i++) {
                printf("%i %i\n", i, (int) (arr_fill_box[i] != nullptr));   
                if (arr_fill_box[i] != nullptr) {
                    int yarded = count_yarded(arr_fill_box[i]->text);
                    int tokenized_size = 0;
                    token *tokenized = parse_string_to_token(arr_fill_box[i]->text, arr_fill_box[i]->t_size, &tokenized_size);
                    token *out = shunting_yard(tokenized, tokenized_size);
                    graph(cursor_pos, tokenized, x_min, x_max, y_min, y_max, tokenized_size, palet[i % 14], i == selected_fill_box, true);
                }
            }
        }

        display_text_box(Formula,0,0,!show_graph && selected_fill_box==-1);
        display_text_box(Graph,0,0,show_graph&& selected_fill_box==-1);
     
        last_pressed=scan_keypad();
        while (last_pressed == -1) {
            last_pressed = scan_keypad();
        }
        std::printf("%i\n", (int) show_graph);
        switch (last_pressed) {
        case SECOND :
            toggle(&snd);
            break;
        case BACK :
            if (!show_graph && arr_fill_box[selected_fill_box]->t_size > 0) {
                arr_fill_box[selected_fill_box]->t_size--;
                arr_fill_box[selected_fill_box]->text[arr_fill_box[selected_fill_box]->t_size] = '\0';
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
                cursor_pos += (x_max - x_min) / 100;
            } else {
                fill_screen(BACKGROUND_COLOR);
                selected_fill_box =- 1;
                show_graph = true;
            }
            break;
        case LEFT :
            if (show_graph && selected_fill_box != -1) {
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
                    fill_screen(BACKGROUND_COLOR);  
                    x_min /= 2;
                    x_max /= 2;
                    y_min /= 2;
                    y_max /= 2;
                } else if (last_pressed == MINUS) {
                    fill_screen(BACKGROUND_COLOR);  
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
        sleep_ms(150);
    }
}
