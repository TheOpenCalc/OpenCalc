#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "headers/grapher.h"
#include "headers/elements.h"
#include "headers/menu.h"
#include "headers/sequences.h"
#include "headers/settings.h"
#include "Solver.h"
#include "headers/Calc.h"
#include "headers/ui.h"
#include <stdio.h>
#include <string> 

int x_cursor = 0;
int y_cursor = 0;

int main()
{
    stdio_init_all();

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);

    gpio_init(PIN_DC);
    gpio_set_dir(PIN_DC, GPIO_OUT);

    gpio_init(PIN_RST);
    gpio_set_dir(PIN_RST, GPIO_OUT);

    spi_init(spi0, 62500000);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    ili_init();
    init_keypad();

    text_box **menu_button = (text_box**) malloc(sizeof(text_box**) * 6);
    int select_item_menu = 0;

    for (int i = 0; i < 6; i++) {
        int x = 18 + 101 * (i % 3);
        int y;
        if (i < 3) {
            y = 119;
        } else {
            y = 16;
        }
        menu_button[i] = create_text_box(x, y, 83, 83, 2, false);
    }
    menu_button[0]->text = "Calc";
    menu_button[0]->t_size = 4;

    menu_button[1]->text = "Graph";
    menu_button[1]->t_size = 5;

    menu_button[2]->text = "Equat";
    menu_button[2]->t_size = 5;

    menu_button[3]->text = "Param";
    menu_button[3]->t_size = 5;

    menu_button[4]->text = "Seque";
    menu_button[4]->t_size = 5;

    menu_button[5]->text = "Periodic";
    menu_button[5]->t_size = 8;

    fill_screen(BACKGROUND_COLOR);  

    while (true) {
        for (int i = 0; i < 6; i++) {
            display_text_box(menu_button[i], 0, -77, i == select_item_menu);
        }
        display_battery(222, 286, 2);
        draw_char(230, 160 - 9 * 5, "Main menu", 0x0000, BACKGROUND_COLOR, 2);

        int last_pressed = scan_keypad();
        while (last_pressed == -1) {
            last_pressed = scan_keypad();
        }
        switch (last_pressed) {
        case DOWN : //down
            select_item_menu = (select_item_menu + 3) % 6;
            break;
        case UP : //up
            select_item_menu = (select_item_menu - 3) % 6;
            break;
        case RIGHT : //right
            select_item_menu = (select_item_menu + 1) % 6;
            break;
        case LEFT : //left
            select_item_menu = (select_item_menu - 1) % 6;
            break;
        case OK : //ok
            switch (select_item_menu) {
            case 0 :
                Calc();
                break;
            case 1 :
                Grapher();
                break;
            case 2 :
                Solver();
                break;
            case 3 :
                settings();
                break;
            case 4 :
                Sequencer();
                break;
            case 5 :
                display_table();
                break;
            default :
                break;
            }
            fill_screen(BACKGROUND_COLOR);
            break;
        default :
            break;
        }

        if (select_item_menu < 0) {
            select_item_menu += 6;
        }
    }
}
