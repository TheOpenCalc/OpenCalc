#ifndef MENU_H
#define MENU_H
void init_keypad();
int scan_keypad() ;
void toggle (bool * snd);
double ** init_2d_Mat(int L,int H,double default_value);
#endif
