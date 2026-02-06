#ifndef TOOLS_H
#define TOOLS_H

#define ROWS 8
#define COLS 5

const int row_pins[ROWS] = { 8, 9, 10, 11 ,12,13,14,15};  // GPIO 0–5 : lignes (output)
const int col_pins[COLS] = { 0, 1, 2, 3, 4}; // GPIO 6–11 : colonnes (input)

int scan_keypad() ;
void toggle (bool * snd);
double ** init_2d_Mat(int L,int H,double default_value);
void init_keypad();

#endif
