#include "headers/ui.h"
#include "headers/Evaluator.h"
#include "headers/sequences.h"
#include <math.h>
#include "headers/menu.h"
#include <algorithm>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <cstdio>
#include "headers/grapher.h"

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

#define TBL_TOP        22      
#define TBL_LABEL_W    38      
#define TBL_COL_W      40      
#define TBL_COLS        7      
#define TBL_ROW_N_H    24      
#define TBL_ROW_VAL_H  30      
#define TBL_MAX_SUITES  4      

static void tbl_cell(int col, int row, int h, int w,
                     char *txt, int txt_len,
                     uint16_t bg, bool selected)
{
    text_box *tb          = create_text_box(col, row, h, w, 1, false);
    free(tb->text);         
    tb->col               = bg;
    tb->text              = txt;
    tb->t_size            = txt_len;
    tb->display_text_size = 1;
    display_text_box(tb, 0, 0, selected);
    tb->text = nullptr;      
    free(tb);
}

static void display_values_table(fill_box **arr,
                                 int start_n,
                                 int sel_col)
{

    int active[TBL_MAX_SUITES];
    int nsuites = 0;
    for (int i = 0; i < 100 && nsuites < TBL_MAX_SUITES; i++) {
        if (arr[i] != nullptr && arr[i]->t_size > 0)
            active[nsuites++] = i;
    }

    char lbl[4];    
    char buf[32];   

    lbl[0] = 'n'; lbl[1] = '\0';
    tbl_cell(0, TBL_TOP,
             TBL_ROW_N_H, TBL_LABEL_W,
             lbl, 1, FRONTGROUND_COLOR, false);

    for (int c = 0; c < TBL_COLS; c++) {
        int n_val = start_n + c;
        int col_x = TBL_LABEL_W + c * TBL_COL_W;

        int len = 0;
        int v = n_val;
        if (v == 0) { buf[len++] = '0'; }
        else {
            if (v < 0) { buf[len++] = '-'; v = -v; }
            char tmp[10]; int tl = 0;
            while (v > 0) { tmp[tl++] = '0' + (v % 10); v /= 10; }
            for (int j = 0; j < tl; j++) buf[len++] = tmp[tl - 1 - j];
        }
        buf[len] = '\0';

        tbl_cell(col_x, TBL_TOP,
                 TBL_ROW_N_H, TBL_COL_W,
                 buf, len, FRONTGROUND_COLOR, c == sel_col);
    }

    
    for (int s = 0; s < nsuites; s++) {
        int row_y = TBL_TOP + TBL_ROW_N_H + s * TBL_ROW_VAL_H;
        int si    = active[s];

        lbl[0] = 'u'; lbl[1] = '1' + active[s]; lbl[2] = '\0';
        tbl_cell(0, row_y,
                 TBL_ROW_VAL_H, TBL_LABEL_W,
                 lbl, 2, FRONTGROUND_COLOR, false);

        for (int c = 0; c < TBL_COLS; c++) {
            int n_val = start_n + c;
            int col_x = TBL_LABEL_W + c * TBL_COL_W;

            int tok_size = 0;
            token *tokenized = parse_string_to_token(
                arr[si]->text, arr[si]->t_size, &tok_size);
            token *npi      = shunting_yard(tokenized, tok_size);
            int    npi_size = count_yarded(tokenized, tok_size);
            double val      = evaluate_npi(npi, npi_size, (double)n_val, 'N');
            free(tokenized);
            free(npi);

            int vlen;
            if (isnan(val))      { buf[0]='N';buf[1]='a';buf[2]='N';buf[3]='\0'; vlen=3; }
            else if (isinf(val)) { buf[0]='i';buf[1]='n';buf[2]='f';buf[3]='\0'; vlen=3; }
            else {
                vlen = fmt_number(val, buf);
                int max_ch = (TBL_COL_W - 4) / 6;
                if (vlen > max_ch)
                    vlen = double_to_string_scientific(val, buf);
                if (vlen > max_ch) { buf[max_ch] = '\0'; vlen = max_ch; }
            }

            tbl_cell(col_x, row_y,
                     TBL_ROW_VAL_H, TBL_COL_W,
                     buf, vlen, BACKGROUND_COLOR, c == sel_col);
        }
    }
}


int Sequencer()
{
    bool snd = false;

    text_box *TabFormule = create_text_box(  0, 220, 20, 107, 1, false);
    text_box *TabGraphe  = create_text_box(107, 220, 20, 107, 1, false);
    text_box *TabValeur  = create_text_box(214, 220, 20, 106, 1, false);
    TabFormule->text = "Formule";  TabFormule->t_size = 7;
    TabGraphe->text  = "Graphe";   TabGraphe->t_size  = 6;
    TabValeur->text  = "Valeur";   TabValeur->t_size  = 6;

    fill_box *n = create_fill_box(0, 20, 40, 320, 3);
    const uint16_t palet[] = {
        COLOR_RED,    COLOR_LIME,    COLOR_BLUE,   COLOR_YELLOW,
        COLOR_CYAN,   COLOR_MAGENTA, COLOR_SILVER, COLOR_GRAY,
        COLOR_MAROON, COLOR_OLIVE,   COLOR_GREEN,  COLOR_PURPLE,
        COLOR_TEAL,   COLOR_NAVY
    };

    fill_box **arr_fill_box = (fill_box**) malloc(sizeof(fill_box*) * 100);
    for (int i = 0; i < 100; i++) arr_fill_box[i] = nullptr;
    arr_fill_box[0] = n;

    int selected_fill_box = 0;
    int first_display     = 0;

    int tab           = 0;
    int table_start_n = 0;   
    int table_sel_col = 0;   

    double x_min = -5, x_max = 5;
    double y_min = -5, y_max = 5;
    double cursor_pos = 0;

    int last_pressed = scan_keypad();

    while (1) {
        fill_screen(BACKGROUND_COLOR);

        switch (last_pressed) {

        case SECOND:
            toggle(&snd);
            break;

        case BACK:
            if (tab == 0 && selected_fill_box >= 0 &&
                arr_fill_box[selected_fill_box]->t_size > 0) {
                                    update_fill_box(arr_fill_box[selected_fill_box], last_pressed, snd);
                } else {
                return 0;
            }
            break;

        case UP:
            if (tab == 0) {
                selected_fill_box = std::max(-1, selected_fill_box - 1);
                if (selected_fill_box < first_display) first_display = selected_fill_box;
            } else if (tab == 1) {
                selected_fill_box--;
            }
            break;

        case DOWN:
            if (tab == 0) {
                selected_fill_box = std::min(100, selected_fill_box + 1);
                if (arr_fill_box[selected_fill_box] == nullptr)
                    arr_fill_box[selected_fill_box] = create_fill_box(0, 20, 40, 320, 3);
                if (first_display + 4 < selected_fill_box) first_display++;
            } else if (tab == 1) {
                selected_fill_box++;
            }
            break;

        case RIGHT:
            if (tab == 0) {
                tab = 1; selected_fill_box = -1;
            } else if (tab == 1) {
                if (selected_fill_box != -1) cursor_pos += (x_max - x_min) / 100;
                else { tab = 2; }
            } else {
                if (table_sel_col < TBL_COLS - 1) table_sel_col++;
                else { table_start_n++; }              }
            break;

        case LEFT:
            if (tab == 1) {
                if (selected_fill_box != -1) cursor_pos -= (x_max - x_min) / 100;
                else { tab = 0; selected_fill_box = -1; }
            } else if (tab == 2) {
                if (table_sel_col > 0) table_sel_col--;
                else if (table_start_n > 0) table_start_n--;  
                else { tab = 1; selected_fill_box = -1; }     
            }
            break;

        case X:
            if (tab == 0 && selected_fill_box >= 0)
                            update_fill_box(arr_fill_box[selected_fill_box], MINUS, true);

            break;

        default:
            if (tab == 1) {
                if      (last_pressed == PLUS)  { x_min/=2; x_max/=2; y_min/=2; y_max/=2; }
                else if (last_pressed == MINUS) { x_min*=2; x_max*=2; y_min*=2; y_max*=2; }
            } else if (tab == 0 && selected_fill_box >= 0) {
                update_fill_box(arr_fill_box[selected_fill_box], last_pressed, snd);
            }
            break;
        }

        if (tab == 0) {
            for (int i = std::max(first_display, 0); i < first_display + 6; i++) {
                display_fill_box(arr_fill_box[i],
                                 160 - (i - std::max(first_display, 0)) * 41,
                                 i == selected_fill_box, i, 'f');
            }
        } else if (tab == 1) {
            axis();
            for (int i = 0; i < 100; i++) {
                if (arr_fill_box[i] != nullptr) {
                    int tok_size = 0;
                    token *tok = parse_string_to_token(
                        arr_fill_box[i]->text, arr_fill_box[i]->t_size, &tok_size);
                    graph(cursor_pos, tok, x_min, x_max, y_min, y_max,
                          tok_size, palet[i % 14], i == selected_fill_box, false, 'N');
                }
            }
        } else {
            display_values_table(arr_fill_box, table_start_n, table_sel_col);
        }

        display_text_box(TabFormule, 0, 0, tab == 0 && selected_fill_box == -1);
        display_text_box(TabGraphe,  0, 0, tab == 1 && selected_fill_box == -1);
        display_text_box(TabValeur,  0, 0, tab == 2);

        last_pressed = scan_keypad();
        while (last_pressed == -1) last_pressed = scan_keypad();
        sleep_ms(150);
    }
}