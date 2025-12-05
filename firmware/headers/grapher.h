#ifndef GRAPHER_H
#define GRAPHER_H
#include "Evaluator.h"
void graph(double cursor_pos ,token * function,double x_min,double x_max,double y_min, double y_max,int n, uint16_t color,bool is_selected,bool fill_between_points);
int Grapher();
#endif