#ifndef GRAPHER_H
#define GRAPHER_H
#include "Evaluator.h"
#include "widgets.h"

void graph(double cursor_pos, token *function, double x_min, double x_max, double y_min, double y_max, int n, uint16_t color, int id, bool fill_between_points, char var);

int Grapher();

#endif
