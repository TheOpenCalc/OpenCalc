#ifndef MATRIX_H
#define MATRIX_H

#include "stack.h"

#define MATRIX_MAX_DIM 99

typedef struct matrix_s
{
    int rows;
    int cols;
    complex *data; /* row-major, rows*cols */
} matrix;

matrix *mat_create(int rows, int cols);
void mat_free(matrix *m);
matrix *mat_clone(const matrix *m);
complex mat_get(const matrix *m, int r, int c);
void mat_set(matrix *m, int r, int c, complex v);
matrix *mat_add(const matrix *a, const matrix *b, bool *ok);
matrix *mat_sub(const matrix *a, const matrix *b, bool *ok);
matrix *mat_scale(const matrix *a, complex s);
matrix *mat_mul(const matrix *a, const matrix *b, bool *ok);
matrix *mat_transpose(const matrix *a);
matrix *mat_identity(int n);
complex mat_det(const matrix *a, bool *ok);
matrix *mat_invert(const matrix *a, bool *ok);
matrix *mat_power(const matrix *a, int n, bool *ok);
int mat_to_string(const matrix *m, char *buf, int maxlen);

#endif
