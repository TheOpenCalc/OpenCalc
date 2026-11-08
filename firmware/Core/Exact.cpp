/* Ecriture exacte des resultats. Voir headers/Exact.h. */
#include "Exact.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define EXACT_TOL 1e-12
#define FRAC_MAX_DEN 10000
#define PI_MAX_DEN 360
#define SQ_MAX_DEN 1000
#define MAX_NUM 1e9

static long long gcdll(long long a, long long b)
{
    if (a < 0)
        a = -a;
    if (b < 0)
        b = -b;
    while (b)
    {
        long long t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static bool close_to(double a, double b)
{
    double scale = fabs(b) > 1 ? fabs(b) : 1;
    return fabs(a - b) <= EXACT_TOL * scale;
}

/* Meilleure fraction p/q (q <= max_den) par fractions continues ; true si elle
 * vaut v a la tolerance pres. */
static bool to_fraction(double v, long long max_den, long long *p, long long *q)
{
    if (!isfinite(v) || fabs(v) > MAX_NUM)
        return false;
    double x = fabs(v);
    long long h0 = 0, h1 = 1, k0 = 1, k1 = 0;
    for (int it = 0; it < 64; it++)
    {
        double a = floor(x);
        if (a > MAX_NUM)
            break;
        long long ai = (long long)a;
        long long h2 = ai * h1 + h0, k2 = ai * k1 + k0;
        if (k2 > max_den)
            break;
        h0 = h1;
        h1 = h2;
        k0 = k1;
        k1 = k2;
        if (close_to((double)h1 / (double)k1, fabs(v)))
        {
            *p = v < 0 ? -h1 : h1;
            *q = k1;
            return true;
        }
        double f = x - a;
        if (f < 1e-15)
            break;
        x = 1.0 / f;
    }
    return false;
}

/* n = k^2 * m avec m sans facteur carre. */
static void split_square(long long n, long long *k, long long *m)
{
    long long kk = 1, mm = n;
    for (long long f = 2; f * f <= mm; f++)
    {
        while (mm % (f * f) == 0)
        {
            mm /= f * f;
            kk *= f;
        }
    }
    *k = kk;
    *m = mm;
}

static bool squarefree(long long m)
{
    for (long long f = 2; f * f <= m; f++)
        if (m % (f * f) == 0)
            return false;
    return true;
}

static int put(char *out, int cap, int pos, const char *s)
{
    int n = (int)strlen(s);
    if (pos + n >= cap)
        return -1;
    memcpy(out + pos, s, n);
    out[pos + n] = '\0';
    return pos + n;
}

static int put_ll(char *out, int cap, int pos, long long v)
{
    char b[24];
    snprintf(b, sizeof(b), "%lld", v);
    return put(out, cap, pos, b);
}

/* "k r(m)" : k omis quand il vaut 1. */
static int put_root(char *out, int cap, int pos, long long k, long long m)
{
    if (k != 1)
        pos = put_ll(out, cap, pos, k);
    if (pos < 0)
        return -1;
    pos = put(out, cap, pos, "r(");
    if (pos < 0)
        return -1;
    pos = put_ll(out, cap, pos, m);
    if (pos < 0)
        return -1;
    return put(out, cap, pos, ")");
}

bool exact_form(double v, char *out, int cap)
{
    if (!out || cap < 8 || !isfinite(v))
        return false;
    out[0] = '\0';
    if (fabs(v) > MAX_NUM || fabs(v) < 1e-9)
        return false;
    if (v == round(v))
        return false;

    long long p, q;
    int pos = 0;

    /* 1. Fraction. */
    if (to_fraction(v, FRAC_MAX_DEN, &p, &q) && q > 1)
    {
        pos = put_ll(out, cap, 0, p);
        if (pos >= 0)
            pos = put(out, cap, pos, "/");
        if (pos >= 0)
            pos = put_ll(out, cap, pos, q);
        return pos > 0;
    }

    /* 2. Multiple rationnel de pi. */
    if (to_fraction(v / M_PI, PI_MAX_DEN, &p, &q) && p != 0 && llabs(p) <= 10000)
    {
        if (p == -1)
            pos = put(out, cap, 0, "-");
        else if (p != 1)
            pos = put_ll(out, cap, 0, p);
        if (pos >= 0)
            pos = put(out, cap, pos, "p");
        if (pos >= 0 && q > 1)
        {
            pos = put(out, cap, pos, "/");
            if (pos >= 0)
                pos = put_ll(out, cap, pos, q);
        }
        return pos > 0;
    }

    /* 3. Racine simplifiee : v^2 = a/b  =>  |v| = sqrt(a*b)/b = k sqrt(m)/b. */
    long long a, b;
    if (to_fraction(v * v, SQ_MAX_DEN, &a, &b) && a > 0 && a <= 1000000)
    {
        long long k, m;
        split_square(a * b, &k, &m);
        if (m > 1)
        {
            long long g = gcdll(k, b);
            k /= g;
            b /= g;
            if (v < 0)
                pos = put(out, cap, 0, "-");
            pos = put_root(out, cap, pos, k, m);
            if (pos >= 0 && b > 1)
            {
                pos = put(out, cap, pos, "/");
                if (pos >= 0)
                    pos = put_ll(out, cap, pos, b);
            }
            return pos > 0;
        }
    }

    /* 4. (a + b sqrt(m)) / d avec de petits entiers. */
    for (long long d = 1; d <= 12; d++)
    {
        for (long long m = 2; m <= 30; m++)
        {
            if (!squarefree(m))
                continue;
            double sm = sqrt((double)m);
            for (long long bb = -12; bb <= 12; bb++)
            {
                if (bb == 0)
                    continue;
                double ad = v * d - bb * sm;
                double ar = round(ad);
                if (fabs(ar) > 1000 || ar == 0)
                    continue;
                if (!close_to((ar + bb * sm) / d, v))
                    continue;
                long long aa = (long long)ar;
                long long g = gcdll(gcdll(aa, bb), d);
                if (g != 1)
                    continue; /* la meme forme reduite sera trouvee pour un d plus petit */
                pos = 0;
                if (d > 1)
                    pos = put(out, cap, pos, "(");
                if (pos >= 0)
                    pos = put_ll(out, cap, pos, aa);
                if (pos >= 0)
                    pos = put(out, cap, pos, bb < 0 ? "-" : "+");
                if (pos >= 0)
                    pos = put_root(out, cap, pos, llabs(bb), m);
                if (pos >= 0 && d > 1)
                {
                    pos = put(out, cap, pos, ")/");
                    if (pos >= 0)
                        pos = put_ll(out, cap, pos, d);
                }
                return pos > 0;
            }
        }
    }
    out[0] = '\0';
    return false;
}

bool exact_input_allowed(const char *text, int len)
{
    if (!text)
        return false;
    for (int i = 0; i < len && text[i]; i++)
    {
        /* Un '.' entre deux chiffres (ou devant un chiffre) est un decimal. */
        if (text[i] == '.')
        {
            bool digit_before = i > 0 && text[i - 1] >= '0' && text[i - 1] <= '9';
            bool digit_after = i + 1 < len && text[i + 1] >= '0' && text[i + 1] <= '9';
            if (digit_before || digit_after)
                return false;
        }
        /* Matrices et nombres complexes restent en ecriture decimale. */
        if (text[i] == '[')
            return false;
        /* 'i' seul = unite imaginaire (et non une lettre de min(, inv(, ...). */
        if (text[i] == 'i')
        {
            bool letter_before = i > 0 && text[i - 1] >= 'a' && text[i - 1] <= 'z';
            bool letter_after = i + 1 < len && text[i + 1] >= 'a' && text[i + 1] <= 'z';
            if (!letter_before && !letter_after)
                return false;
        }
    }
    return true;
}
