#include "headers/ui.h"
#include "headers/menu.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"

void settings()
{
    fill_screen(BACKGROUND_COLOR);
    pontentiometer *luminosity = create_potentiometer(0, 10, 40, 320, 2, "Brightness", 20, false);
    display_potentiometer(luminosity, false);
    int cursor_pos = 0;
    int last_pressed = scan_keypad();
    int max_size = 0;
    while (true) {
        switch (last_pressed) {
        case DOWN :
            cursor_pos = min(max_size, cursor_pos + 1);
            break;
        case UP :
            cursor_pos = max(0, cursor_pos - 1);
            break;
        case RIGHT :
            switch (cursor_pos) {
            case 0 :
                increment_potentiometer(luminosity);
                break;
            default :
                break;
            }
        case LEFT :
            switch (cursor_pos) {
            case 0 :
                decrement_potentiometer(luminosity);
                break;
            default :
                break;
            }
            break;
        case BACK :
            return;
            break;
        default :
            break;
        }
    }
    last_pressed = scan_keypad();
    while (last_pressed == -1) {
        last_pressed = scan_keypad();
    }
    sleep_ms(150);
}
