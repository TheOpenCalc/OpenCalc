#ifndef ELEMENT_H
#define ELEMENT_H
#include "widgets.h"

void display_table();

/* Rend la memoire du tableau periodique. Appelee a la sortie par BACK, et par
 * le menu apres une sortie par HOME (longjmp, qui saute la fin de
 * display_table). Sans effet si rien n'est alloue. */
void elements_release();

#endif
