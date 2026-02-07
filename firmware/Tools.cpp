#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "headers/Tools.h"
#include <stdlib.h>

#ifndef OPENCALC_WASM

void init_keypad()
{
    for (int r = 0; r < ROWS; r++) {
        gpio_init(row_pins[r]);
        gpio_set_dir(row_pins[r], GPIO_OUT);
        gpio_put(row_pins[r], 1); // inactif (HIGH)
    }

    for (int c = 0; c < COLS; c++) {
        gpio_init(col_pins[c]);
        gpio_set_dir(col_pins[c], GPIO_IN);
        gpio_pull_up(col_pins[c]); // pull-up activé
    }
}

int scan_keypad()
{
    for (int row = 0; row < ROWS; row++) {
        // Activer la ligne courante (LOW)
        gpio_put(row_pins[row], 0);
        sleep_ms(20);
        // Lire chaque colonne
        for (int col = 0; col < COLS; col++) {
            if (gpio_get(col_pins[col]) == 0) {  // touche détectée (LOW)
                // Rétablir la ligne avant de retourner
                gpio_put(row_pins[row], 1);
                return row * COLS + col;  // bouton 0 à 35
            }
        }    

        // Désactiver la ligne (remettre HIGH)
        gpio_put(row_pins[row], 1);
    }
    return -1; // aucune touche
}

#endif


double **init_2d_Mat(int L, int H, double default_value)
{
    double **a = (double**) malloc(sizeof(double*) * H);
    for (int i = 0; i < H; i++) {
        a[i] = (double*) malloc(sizeof(double) * L);
        for(int j = 0; j < L; j++) {
            a[i][j] = default_value;
        }
    }
    return a;
}


void toggle (bool *snd)
{
    *snd = !(*snd);
    return;
}
