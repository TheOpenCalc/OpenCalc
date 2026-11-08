#ifndef SETTINGS_H
#define SETTINGS_H
#include "widgets.h"

void settings();

int angle_mode_get(void);

/* Persistance des réglages (unité d'angle, décimales, luminosité) dans la
 * flash interne, pour qu'ils survivent aux redémarrages. */
void settings_load(void);
void settings_save(void);

#endif