#include "headers/elements.h"
#include "headers/ui.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <stdio.h>
#include "menu.h"
#include <stdlib.h>

void display_table()
{
    fill_screen(BACKGROUND_COLOR);  // Bleu
    int hauteur[33] = {7, 6, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 6, 6, 6, 7};
    text_box ***table = (text_box***) malloc(sizeof(text_box*) * 32);
    int k = 0;
    char *name[] = {"Fr", "Cs", "Rb", "K", "Na", "Li", "H", "Ra", "Ba", "Sr", "Ca", "Mg", "Be", "Ac", "La", "Th", "Ce",
                    "Pa", "Pr", "U", "Nd", "Np", "Pm", "Pu", "Sm", "Am", "Eu", "Cm", "Gd", "Bk", "Tb", "Cf", "Dy", "Es",
                    "Ho", "Fm", "Er", "Md", "Tm", "No", "Yb", "Lr", "Lu", "Y", "Sc", "Rf", "Hf", "Zr", "Ti", "Db", "Ta",
                    "Nb", "V", "Sg", "W", "Mo", "Cr", "Bh", "Re", "Tc", "Mn", "Hs", "Os", "Ru", "Fe", "Mt", "Ir", "Rh",
                    "Co", "Ds", "Pt", "Pd", "Ni", "Rg", "Au", "Ag", "Cu", "Cn", "Hg", "Cd", "Zn", "Nh", "Tl", "In", "Ga",
                    "Al", "B", "Fl", "Pb", "Sn", "Ge", "Si", "C", "Mc", "Bi", "SB", "As", "P", "N", "Lv", "Po", "Te",
                    "Se", "S", "O", "Ts", "At", "I", "Br", "Cl", "F", "Og", "Rn", "Xe", "Kr", "Ar", "Ne", "He"};
    int col = 0;
    int L, C = 0;
    int shift = 0;
    for (int i = 0; i < 32; i++) {
        table[i] = (text_box**) malloc(sizeof(text_box*) * hauteur[i]);
        for (int j = 0; j < hauteur[i]; j++) {
            table[i][j] = (text_box*) malloc(sizeof(text_box));
            table[i][j]->x = 31 * i + 10;
            table[i][j]->y = 31 * j + 10;
            table[i][j]->h = 30;
            table[i][j]->w = 30;
            table[i][j]->border = 0;
            table[i][j]->text = (char*) malloc(sizeof(char) * 3);
            table[i][j]->t_size = 2;

            if (k < 6) {
                table[i][j]->col = 0xf000;
            } else if (k == 6) {
                table[i][j]->col = 0x07e0;
            } else if (k < 13) {
                table[i][j]->col = 0xf674;
            } else if (k < 43) {
                table[i][j]->col = k % 2 == 0 ? 0xfddf : 0xfcd9;
            } else if (k == 73 || k == 69 || k == 65 || k == 81|| k == 87 || k == 93 || k == 99 || k == 105 || k == 111) {
                table[i][j]->col = FRONTGROUND_COLOR;
            } else if (k < 78) {
                table[i][j]->col = 0xfeba;
            } else if ((k >= 78 && k <= 85) || k == 88 || k == 89 || k == 94 || k == 100) {
                table[i][j]->col = BACKGROUND_COLOR;
            } else if (k == 86 || k == 91 || k == 90 || k == 96 || k == 95 || k == 106 || k == 111) {
                table[i][j]->col = 0xce53;
            } else if (k < 105) {
                table[i][j]->col = 0x9ff3;
            } else if (k <= 110) {
                table[i][j]->col = 0xfff3;
            } else {
                table[i][j]->col = 0xbf3f;
            }

            if (k < 118) {
                table[i][j]->text = name[k];
            }
            k++;
        }
    }
    int last_pressed = scan_keypad();
    text_box *data = create_text_box(5, 200, 40, 40, 2, true);
    while (true) {
        if (C - ((shift / 31)) < 5) {
            fill_rect(105, 5, 128, 160, BACKGROUND_COLOR);
        } else {
            fill_rect(105, 175, 128, 160, BACKGROUND_COLOR);
        }        
        for (int i = max(C - 10, 0); i < min(C + 10, 32); i++) {
            for (int j = 0; j < hauteur[i]; j++) {
                display_text_box(table[i][j], 0, C == i && L == j);
            }
        }
        
        if (C - ((shift / 31)) > 5) {
            fill_rect(105, 175, 128, 160, 0);
        } else {
            fill_rect(105, 5, 128, 160, 0);
        }
        last_pressed = scan_keypad();
        while (last_pressed == -1) {
            last_pressed = scan_keypad();
        }

        switch (last_pressed) {
        case BACK :
            return;
            break;
        case RIGHT :
            C = min(C + 1, 31);
            L = min(hauteur[C] - 1, L);
            break;
        case LEFT :
            C = max(0 ,C - 1);
            L = min(hauteur[C] - 1, L);
            break;
        case UP :
            L = min(L + 1, hauteur[C] - 1);
            break;
        case DOWN :
            L = max(L - 1, 0);
            break;
        default :
            break;
        }

        if (shift + 300 < 31 * C) {
            fill_screen(BACKGROUND_COLOR);
            shift += 31;
            for (int i = 0; i < 32; i++) {
                for (int j = 0; j < hauteur[i]; j++) {
                    table[i][j]->x -= 31;
                }
            }
        } else if (shift > 31 * C) {
            fill_screen(BACKGROUND_COLOR);
            shift -= 31;
            for (int i = 0; i < 32; i++) {
                for (int j = 0; j < hauteur[i]; j++) {
                    table[i][j]->x += 31;
                }
            }
        }
    }
}
