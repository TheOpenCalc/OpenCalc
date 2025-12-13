#ifndef SOLVER_H
#define SOLVER_H

int * solve (int ** input, int nb_var,int nb_eq);
void simplify_first(double * a,double * b, int n,int first_nb);
void normalize(double * a , int size, int n);
void printdouble2D(double ** input,int L, int H);
bool comp(double * a,double * b,int n);
void insertion_sort(double ** input,int nb_element, int size_element);
double * solve (double ** input, int nb_var,int nb_eq);
int Solver();

#endif