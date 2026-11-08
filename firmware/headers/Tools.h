#ifndef TOOLS_H
#define TOOLS_H

#include <setjmp.h>

#define ROWS 8
#define COLS 7
#define EXAM_LED_GPIO 26

const int row_pins[ROWS] = {15,14,13,12,11,10,9,8}; // GPIO 8–15 : lignes (output)
const int col_pins[COLS] = {0, 1, 2, 3, 4,5,6};                // GPIO 0–6 : colonnes (input)

/* Bouton HOME : retour au menu principal depuis n'importe quelle appli.
 * menu.cpp arme g_home_armed pendant qu'une appli tourne et fait le setjmp ;
 * scan_keypad()/scan_keypad_nowait() appellent home_check() sur chaque touche
 * (a appeler aussi depuis la version WASM de scan_keypad). */
extern jmp_buf g_home_jmp;
extern volatile bool g_home_armed;
void home_check(int key);

int scan_keypad();
int scan_keypad_nowait();
int scan_text_char();
void toggle(bool *snd);
double **init_2d_Mat(int L, int H, double default_value);
void init_keypad();

#endif