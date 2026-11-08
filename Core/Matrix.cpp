#include <cmath>
#include <stdio.h>
#include <stdlib.h>

#include "../headers/Matrix.h"
#include "../headers/Evaluator.h"

matrix *mat_create(int rows, int cols)
{
    if (rows <= 0 || cols <= 0 || rows > MATRIX_MAX_DIM || cols > MATRIX_MAX_DIM)
        return NULL;
    matrix *m = (matrix *)malloc(sizeof(matrix));
    if (!m)
        return NULL;
    m->rows = rows;
    m->cols = cols;
    m->data = (complex *)malloc(sizeof(complex) * rows * cols);
    if (!m->data)
    {
        free(m);
        return NULL;
    }
    for (int i = 0; i < rows * cols; i++)
    {
        m->data[i].re = 0.0;
        m->data[i].im = 0.0;
    }
    return m;
}

void mat_free(matrix *m)
{
    if (!m)
        return;
    if (m->data)
        free(m->data);
    free(m);
}

matrix *mat_clone(const matrix *m)
{
    if (!m)
        return NULL;
    matrix *r = mat_create(m->rows, m->cols);
    if (!r)
        return NULL;
    for (int i = 0; i < m->rows * m->cols; i++)
        r->data[i] = m->data[i];
    return r;
}

complex mat_get(const matrix *m, int r, int c)
{
    return m->data[r * m->cols + c];
}

void mat_set(matrix *m, int r, int c, complex v)
{
    m->data[r * m->cols + c] = v;
}

matrix *mat_add(const matrix *a, const matrix *b, bool *ok)
{
    *ok = true;
    if (a->rows != b->rows || a->cols != b->cols)
    {
        *ok = false;
        return NULL;
    }
    matrix *r = mat_create(a->rows, a->cols);
    if (!r)
    {
        *ok = false;
        return NULL;
    }
    for (int i = 0; i < a->rows * a->cols; i++)
        r->data[i] = cadd(a->data[i], b->data[i]);
    return r;
}

matrix *mat_sub(const matrix *a, const matrix *b, bool *ok)
{
    *ok = true;
    if (a->rows != b->rows || a->cols != b->cols)
    {
        *ok = false;
        return NULL;
    }
    matrix *r = mat_create(a->rows, a->cols);
    if (!r)
    {
        *ok = false;
        return NULL;
    }
    for (int i = 0; i < a->rows * a->cols; i++)
        r->data[i] = csub(a->data[i], b->data[i]);
    return r;
}

matrix *mat_scale(const matrix *a, complex s)
{
    matrix *r = mat_create(a->rows, a->cols);
    if (!r)
        return NULL;
    for (int i = 0; i < a->rows * a->cols; i++)
        r->data[i] = cmul(a->data[i], s);
    return r;
}

matrix *mat_mul(const matrix *a, const matrix *b, bool *ok)
{
    *ok = true;
    if (a->cols != b->rows)
    {
        *ok = false;
        return NULL;
    }
    matrix *r = mat_create(a->rows, b->cols);
    if (!r)
    {
        *ok = false;
        return NULL;
    }
    for (int i = 0; i < a->rows; i++)
    {
        for (int j = 0; j < b->cols; j++)
        {
            complex s = {0, 0};
            for (int k = 0; k < a->cols; k++)
                s = cadd(s, cmul(a->data[i * a->cols + k], b->data[k * b->cols + j]));
            r->data[i * r->cols + j] = s;
        }
    }
    return r;
}

matrix *mat_transpose(const matrix *a)
{
    matrix *r = mat_create(a->cols, a->rows);
    if (!r)
        return NULL;
    for (int i = 0; i < a->rows; i++)
        for (int j = 0; j < a->cols; j++)
            r->data[j * r->cols + i] = a->data[i * a->cols + j];
    return r;
}

matrix *mat_identity(int n)
{
    matrix *r = mat_create(n, n);
    if (!r)
        return NULL;
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            complex z = {(i == j) ? 1.0 : 0.0, 0.0};
            r->data[i * n + j] = z;
        }
    }
    return r;
}

/* Modulus used for partial pivoting: |z|^2 is enough for comparisons and
   avoids a hypot call per candidate. */
static double zmod2(complex z)
{
    return z.re * z.re + z.im * z.im;
}

complex mat_det(const matrix *a, bool *ok)
{
    *ok = true;
    complex nanz = {NAN, NAN};
    if (a->rows != a->cols)
    {
        *ok = false;
        return nanz;
    }
    int n = a->rows;
    if (n == 0)
        return nanz;
    complex *m = (complex *)malloc(sizeof(complex) * n * n);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            m[i * n + j] = a->data[i * a->cols + j];

    complex det = {1.0, 0.0};
    for (int col = 0; col < n; col++)
    {
        int piv = col;
        double mx = zmod2(m[col * n + col]);
        for (int r = col + 1; r < n; r++)
        {
            double mz = zmod2(m[r * n + col]);
            if (mz > mx)
            {
                mx = mz;
                piv = r;
            }
        }
        if (mx == 0.0)
        {
            free(m);
            *ok = false;
            return nanz;
        }
        if (piv != col)
        {
            for (int j = col; j < n; j++)
            {
                complex tmp = m[col * n + j];
                m[col * n + j] = m[piv * n + j];
                m[piv * n + j] = tmp;
            }
            det.re = -det.re;
            det.im = -det.im;
        }
        complex pv = m[col * n + col];
        det = cmul(det, pv);
        for (int r = col + 1; r < n; r++)
        {
            complex f = cdiv(m[r * n + col], pv);
            if (f.re == 0.0 && f.im == 0.0)
                continue;
            for (int j = col; j < n; j++)
                m[r * n + j] = csub(m[r * n + j], cmul(f, m[col * n + j]));
        }
    }
    free(m);
    return det;
}

matrix *mat_invert(const matrix *a, bool *ok)
{
    *ok = true;
    if (a->rows != a->cols)
    {
        *ok = false;
        return NULL;
    }
    int n = a->rows;
    int w = 2 * n;
    complex *aug = (complex *)malloc(sizeof(complex) * n * w);
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
            aug[i * w + j] = a->data[i * a->cols + j];
        for (int j = n; j < w; j++)
        {
            complex z = {(j - n == i) ? 1.0 : 0.0, 0.0};
            aug[i * w + j] = z;
        }
    }
    for (int col = 0; col < n; col++)
    {
        int piv = col;
        double mx = zmod2(aug[col * w + col]);
        for (int r = col + 1; r < n; r++)
        {
            double mz = zmod2(aug[r * w + col]);
            if (mz > mx)
            {
                mx = mz;
                piv = r;
            }
        }
        if (mx == 0.0)
        {
            free(aug);
            *ok = false;
            return NULL;
        }
        if (piv != col)
        {
            for (int j = col; j < w; j++)
            {
                complex tmp = aug[col * w + j];
                aug[col * w + j] = aug[piv * w + j];
                aug[piv * w + j] = tmp;
            }
        }
        complex pv = aug[col * w + col];
        for (int j = col; j < w; j++)
            aug[col * w + j] = cdiv(aug[col * w + j], pv);
        for (int r = 0; r < n; r++)
        {
            if (r == col)
                continue;
            complex f = aug[r * w + col];
            if (f.re == 0.0 && f.im == 0.0)
                continue;
            for (int j = col; j < w; j++)
                aug[r * w + j] = csub(aug[r * w + j], cmul(f, aug[col * w + j]));
        }
    }
    matrix *res = mat_create(n, n);
    if (!res)
    {
        free(aug);
        *ok = false;
        return NULL;
    }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            res->data[i * n + j] = aug[i * w + n + j];
    free(aug);
    return res;
}

matrix *mat_power(const matrix *a, int n, bool *ok)
{
    *ok = true;
    if (a->rows != a->cols)
    {
        *ok = false;
        return NULL;
    }
    if (n < 0)
    {
        matrix *inv = mat_invert(a, ok);
        if (!*ok || !inv)
            return NULL;
        matrix *r = mat_power(inv, -n, ok);
        mat_free(inv);
        return r;
    }
    matrix *result = mat_identity(a->rows);
    matrix *base = mat_clone(a);
    while (n > 0)
    {
        if (n & 1)
        {
            matrix *t = mat_mul(result, base, ok);
            if (!*ok || !t)
            {
                mat_free(result);
                mat_free(base);
                return NULL;
            }
            mat_free(result);
            result = t;
        }
        n >>= 1;
        if (n > 0)
        {
            matrix *t = mat_mul(base, base, ok);
            if (!*ok || !t)
            {
                mat_free(result);
                mat_free(base);
                return NULL;
            }
            mat_free(base);
            base = t;
        }
    }
    mat_free(base);
    return result;
}

int mat_to_string(const matrix *m, char *buf, int maxlen)
{
    if (!m || maxlen <= 0)
        return 0;
    int p = 0;
    buf[p++] = '[';
    for (int r = 0; r < m->rows && p < maxlen - 1; r++)
    {
        if (r > 0 && p < maxlen - 1)
            buf[p++] = ',';
        if (p < maxlen - 1)
            buf[p++] = '[';
        for (int c = 0; c < m->cols && p < maxlen - 1; c++)
        {
            if (c > 0 && p < maxlen - 1)
                buf[p++] = ',';
            char tmp[64];
            complex_to_string(mat_get(m, r, c), tmp);
            for (int k = 0; tmp[k] && p < maxlen - 1; k++)
                buf[p++] = tmp[k];
        }
        if (p < maxlen - 1)
            buf[p++] = ']';
    }
    if (p < maxlen - 1)
    {
        buf[p++] = ']';
        buf[p] = '\0';
    }
    else
    {
        buf[maxlen - 4] = '.';
        buf[maxlen - 3] = '.';
        buf[maxlen - 2] = '.';
        buf[maxlen - 1] = '\0';
    }
    return p;
}
