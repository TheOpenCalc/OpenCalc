#include "headers/ui.h"
#include "headers/menu.h"
#include "headers/settings.h"
#include "headers/widgets.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <malloc.h>
#include "headers/colors.h"


int g_angle_mode = 0;   
int g_decimals   = 6;
int g_brightness = 10; 

#define LABEL_X      2      
#define LABEL_W      110    
#define WIDGET_X     118    
#define WIDGET_W     196    
#define ROW_H        42     

#define ROW0_Y       152    
#define ROW1_Y       105    
#define ROW2_Y       58
#define ROW3_Y       11     

#define TITLE_Y      200   
#define TITLE_H      40

#define HELP_Y       0      
#define HELP_H       20

#define NUM_SETTINGS 4

static char dec_buf[8];
static char deg_buf[4];
static char rad_buf[4];

static void refresh_dec_text(text_box *tb)
{
    dec_buf[0] = '<'; dec_buf[1] = ' ';
    dec_buf[2] = '0' + g_decimals;
    dec_buf[3] = ' '; dec_buf[4] = '>'; dec_buf[5] = '\0';
    tb->text   = dec_buf;
    tb->t_size = 5;
}

void settings()
{
    fill_screen(BACKGROUND_COLOR);

    text_box *title = create_text_box(0, TITLE_Y, TITLE_H, 320, 0, false);
    title->text     = "Parametres";
    title->display_text_size=2;
    title->t_size   = 20;
    title->col      = 0x311f;
    title->allign   = 'c';

    text_box *help = create_text_box(0, HELP_Y, HELP_H, 320, 0, false);
    help->text     = "^v Naviguer  </> Modifier  BACK Quitter";
    help->t_size   = 38;
    help->col      = 0x311f;
    help->allign   = 'r';

    text_box *lbl0 = create_text_box(LABEL_X, ROW0_Y, ROW_H, LABEL_W, 2, false);
    lbl0->text     = "Luminosite";
    lbl0->t_size   = 10;
    lbl0->allign   = 'r';

    text_box *lbl1 = create_text_box(LABEL_X, ROW1_Y, ROW_H, LABEL_W, 2, false);
    lbl1->text     = "Angles";
    lbl1->t_size   = 6;
    lbl1->allign   = 'r';

    text_box *lbl2 = create_text_box(LABEL_X, ROW2_Y, ROW_H, LABEL_W, 2, false);
    lbl2->text     = "Decimales";
    lbl2->t_size   = 9;
    lbl2->allign   = 'r';


    text_box *lbl3 = create_text_box(LABEL_X, ROW3_Y+ROW_H/2, ROW_H/2, 316, 2, false);
    lbl3->text     = "Credits : OpenCalc opencacl.fr version 0.9.5";
    lbl3->t_size   = 43;
    lbl3->allign   = 'c';

    pontentiometer *pot_bright = create_potentiometer(
        ROW0_Y + 11,  
        WIDGET_X,       
        20,             
        WIDGET_W,       
        2,
        (char*)"",
        20,
        true            
    );
    pot_bright->val = g_brightness;

    int half_w = WIDGET_W / 2 - 2;

    rad_buf[0]='R'; rad_buf[1]='A'; rad_buf[2]='D'; rad_buf[3]='\0';
    text_box *tb_rad = create_text_box(WIDGET_X, ROW1_Y, ROW_H, half_w, 2, false);
    tb_rad->text     = rad_buf;
    tb_rad->t_size   = 3;
    tb_rad->allign   = 'c';

    deg_buf[0]='D'; deg_buf[1]='E'; deg_buf[2]='G'; deg_buf[3]='\0';
    text_box *tb_deg = create_text_box(WIDGET_X + half_w + 4, ROW1_Y, ROW_H, half_w, 2, false);
    tb_deg->text     = deg_buf;
    tb_deg->t_size   = 3;
    tb_deg->allign   = 'c';

    text_box *tb_dec = create_text_box(WIDGET_X, ROW2_Y, ROW_H, WIDGET_W, 2, false);
    tb_dec->allign   = 'c';
    refresh_dec_text(tb_dec);

    int  cursor_pos = 0;
    bool need_draw  = true;

    while (true)
    {
        if (need_draw)
        {
            fill_screen(BACKGROUND_COLOR);

            display_text_box(title, 0, 0, false);
            display_text_box(help,  0, 0, false);

            pot_bright->val = g_brightness;
            display_text_box(lbl0, 0, 0, cursor_pos == 0);
            display_potentiometer(pot_bright, cursor_pos == 0);

           
            display_text_box(lbl1,   0, 0, cursor_pos == 1);
            display_text_box(tb_rad, 0, 0, cursor_pos == 1 && g_angle_mode == 0);
            display_text_box(tb_deg, 0, 0, cursor_pos == 1 && g_angle_mode == 1);


            refresh_dec_text(tb_dec);
            display_text_box(lbl2,   0, 0, cursor_pos == 2);
            display_text_box(tb_dec, 0, 0, cursor_pos == 2);

            display_text_box(lbl3,   0, 0, cursor_pos == 3);


            need_draw = false;
        }


        int key = scan_keypad();
        while (key == -1)
            key = scan_keypad();

        
        switch (key)
        {
        case UP:
            if (cursor_pos > 0) { cursor_pos--; need_draw = true; }
            break;

        case DOWN:
            if (cursor_pos < NUM_SETTINGS - 1) { cursor_pos++; need_draw = true; }
            break;

        case LEFT:
            switch (cursor_pos)
            {
            case 0:
                decrement_potentiometer(pot_bright);
                g_brightness = pot_bright->val;
                need_draw = true;
                break;
            case 1:
                g_angle_mode = (g_angle_mode + 1) % 2;
                need_draw = true;
                break;
            case 2:
                g_decimals = max(0, g_decimals - 1);
                need_draw = true;
                break;
            }
            break;

        case RIGHT:
            switch (cursor_pos)
            {
            case 0:
                increment_potentiometer(pot_bright);
                g_brightness = pot_bright->val;
                need_draw = true;
                break;
            case 1:
                g_angle_mode = (g_angle_mode + 1) % 2;
                need_draw = true;
                break;
            case 2:
                g_decimals = min(9, g_decimals + 1);
                need_draw = true;
                break;
            }
            break;

        case OK:
            if (cursor_pos == 1)
            {
                g_angle_mode = (g_angle_mode + 1) % 2;
                need_draw = true;
            }
            break;

        case BACK:
            return;

        default:
            break;
        }

        sleep_ms(80);
    }
}