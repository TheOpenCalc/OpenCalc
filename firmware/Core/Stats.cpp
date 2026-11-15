/* Statistiques, regressions et lois de probabilite. Voir headers/Stats.h. */
#include "Stats.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------- Une variable ------------------------------- */

typedef struct
{
    double x, w;
} wpoint;

static int cmp_wpoint(const void *a, const void *b)
{
    double xa = ((const wpoint *)a)->x, xb = ((const wpoint *)b)->x;
    return (xa > xb) - (xa < xb);
}

/* Plus petite valeur dont l'effectif cumule atteint frac * n (definition des
 * quartiles du programme de lycee). */
static double quantile_lycee(const wpoint *pts, int m, double n, double frac)
{
    double target = frac * n;
    double cum = 0;
    for (int i = 0; i < m; i++)
    {
        cum += pts[i].w;
        if (cum >= target - 1e-9)
            return pts[i].x;
    }
    return pts[m - 1].x;
}

/* Mediane : valeur centrale, ou moyenne des deux valeurs centrales quand
 * l'effectif total est pair (et entier). */
static double median_of(const wpoint *pts, int m, double n)
{
    double half = n / 2.0;
    double cum = 0;
    for (int i = 0; i < m; i++)
    {
        cum += pts[i].w;
        if (cum > half + 1e-9)
            return pts[i].x;
        if (fabs(cum - half) <= 1e-9)
        {
            /* La moitie tombe pile entre cette valeur et la suivante. */
            if (i + 1 < m)
                return (pts[i].x + pts[i + 1].x) / 2.0;
            return pts[i].x;
        }
    }
    return pts[m - 1].x;
}

bool stats_one_var(const double *x, const double *w, int count, stats1 *out)
{
    if (!x || !out || count <= 0)
        return false;
    wpoint *pts = (wpoint *)malloc(sizeof(wpoint) * count);
    if (!pts)
        return false;
    int m = 0;
    double n = 0, s = 0, s2 = 0;
    for (int i = 0; i < count; i++)
    {
        double wi = w ? w[i] : 1.0;
        if (!isfinite(x[i]) || !isfinite(wi) || wi <= 0)
            continue;
        pts[m].x = x[i];
        pts[m].w = wi;
        m++;
        n += wi;
        s += wi * x[i];
        s2 += wi * x[i] * x[i];
    }
    if (m == 0 || n <= 0)
    {
        free(pts);
        return false;
    }
    qsort(pts, m, sizeof(wpoint), cmp_wpoint);

    memset(out, 0, sizeof(*out));
    out->n = n;
    out->sum = s;
    out->sum_sq = s2;
    out->mean = s / n;
    /* Variance par l'ecart a la moyenne (plus stable que s2/n - mean^2). */
    double v = 0;
    for (int i = 0; i < m; i++)
    {
        double d = pts[i].x - out->mean;
        v += pts[i].w * d * d;
    }
    out->var = v / n;
    out->sd = sqrt(out->var);
    out->sd_s = n > 1 ? sqrt(v / (n - 1)) : NAN;
    out->min = pts[0].x;
    out->max = pts[m - 1].x;
    out->range = out->max - out->min;
    out->median = median_of(pts, m, n);
    out->q1 = quantile_lycee(pts, m, n, 0.25);
    out->q3 = quantile_lycee(pts, m, n, 0.75);
    out->iqr = out->q3 - out->q1;
    free(pts);
    return true;
}

/* ----------------------------- Regressions ------------------------------- */

static const char *k_reg_names[REG_COUNT] = {
    "Lineaire", "Quadratique", "Exponentielle", "Puissance", "Logarithmique"};
static const char *k_reg_formulas[REG_COUNT] = {
    "y=ax+b", "y=ax^2+bx+c", "y=a*e^(bx)", "y=a*x^b", "y=a+b*ln(x)"};

const char *reg_model_name(int model)
{
    return (model >= 0 && model < REG_COUNT) ? k_reg_names[model] : "?";
}

const char *reg_model_formula(int model)
{
    return (model >= 0 && model < REG_COUNT) ? k_reg_formulas[model] : "?";
}

/* Droite des moindres carres sur (u, v) : v = a u + b, et correlation r. */
static bool fit_line(const double *u, const double *v, int m,
                     double *a, double *b, double *r)
{
    if (m < 2)
        return false;
    double mu = 0, mv = 0;
    for (int i = 0; i < m; i++)
    {
        mu += u[i];
        mv += v[i];
    }
    mu /= m;
    mv /= m;
    double suu = 0, svv = 0, suv = 0;
    for (int i = 0; i < m; i++)
    {
        double du = u[i] - mu, dv = v[i] - mv;
        suu += du * du;
        svv += dv * dv;
        suv += du * dv;
    }
    if (suu <= 0)
        return false;
    *a = suv / suu;
    *b = mv - *a * mu;
    *r = (svv > 0) ? suv / sqrt(suu * svv) : 1.0;
    return true;
}

/* Systeme 3x3 par elimination de Gauss avec pivot partiel. */
static bool solve3(double A[3][4], double sol[3])
{
    for (int c = 0; c < 3; c++)
    {
        int piv = c;
        for (int r = c + 1; r < 3; r++)
            if (fabs(A[r][c]) > fabs(A[piv][c]))
                piv = r;
        if (fabs(A[piv][c]) < 1e-300)
            return false;
        if (piv != c)
            for (int k = 0; k < 4; k++)
            {
                double t = A[c][k];
                A[c][k] = A[piv][k];
                A[piv][k] = t;
            }
        for (int r = 0; r < 3; r++)
        {
            if (r == c)
                continue;
            double f = A[r][c] / A[c][c];
            for (int k = c; k < 4; k++)
                A[r][k] -= f * A[c][k];
        }
    }
    for (int i = 0; i < 3; i++)
        sol[i] = A[i][3] / A[i][i];
    return true;
}

double reg_eval(const regression *r, double x)
{
    switch (r->model)
    {
    case REG_LINEAR:
        return r->a * x + r->b;
    case REG_QUAD:
        return (r->a * x + r->b) * x + r->c;
    case REG_EXP:
        return r->a * exp(r->b * x);
    case REG_POWER:
        return x > 0 ? r->a * pow(x, r->b) : NAN;
    case REG_LOG:
        return x > 0 ? r->a + r->b * log(x) : NAN;
    default:
        return NAN;
    }
}

bool regress(const double *x, const double *y, int count, int model, regression *out)
{
    if (!x || !y || !out || count < 2 || model < 0 || model >= REG_COUNT)
        return false;
    double *u = (double *)malloc(sizeof(double) * count);
    double *v = (double *)malloc(sizeof(double) * count);
    double *px = (double *)malloc(sizeof(double) * count);
    double *py = (double *)malloc(sizeof(double) * count);
    if (!u || !v || !px || !py)
    {
        free(u);
        free(v);
        free(px);
        free(py);
        return false;
    }
    /* Points utilisables pour le modele (ln y demande y > 0, ln x demande x > 0). */
    int m = 0;
    for (int i = 0; i < count; i++)
    {
        if (!isfinite(x[i]) || !isfinite(y[i]))
            continue;
        if ((model == REG_EXP || model == REG_POWER) && y[i] <= 0)
            continue;
        if ((model == REG_POWER || model == REG_LOG) && x[i] <= 0)
            continue;
        px[m] = x[i];
        py[m] = y[i];
        m++;
    }

    memset(out, 0, sizeof(*out));
    out->model = model;
    out->n = m;
    bool ok = false;
    double a = 0, b = 0, r = 0;
    switch (model)
    {
    case REG_LINEAR:
        ok = fit_line(px, py, m, &a, &b, &r);
        out->a = a;
        out->b = b;
        out->r = r;
        break;
    case REG_EXP:
        for (int i = 0; i < m; i++)
        {
            u[i] = px[i];
            v[i] = log(py[i]);
        }
        ok = fit_line(u, v, m, &a, &b, &r);
        out->a = exp(b);
        out->b = a;
        out->r = r;
        break;
    case REG_POWER:
        for (int i = 0; i < m; i++)
        {
            u[i] = log(px[i]);
            v[i] = log(py[i]);
        }
        ok = fit_line(u, v, m, &a, &b, &r);
        out->a = exp(b);
        out->b = a;
        out->r = r;
        break;
    case REG_LOG:
        for (int i = 0; i < m; i++)
            u[i] = log(px[i]);
        ok = fit_line(u, py, m, &a, &b, &r);
        out->a = b;
        out->b = a;
        out->r = r;
        break;
    case REG_QUAD:
    {
        if (m < 3)
            break;
        /* Equations normales sur les puissances de x centrees (stabilite). */
        double mx = 0;
        for (int i = 0; i < m; i++)
            mx += px[i];
        mx /= m;
        double S[5] = {0, 0, 0, 0, 0}, T[3] = {0, 0, 0};
        for (int i = 0; i < m; i++)
        {
            double t = px[i] - mx, p = 1;
            for (int k = 0; k < 5; k++)
            {
                S[k] += p;
                if (k < 3)
                    T[k] += p * py[i];
                p *= t;
            }
        }
        double A[3][4] = {{S[4], S[3], S[2], T[2]},
                          {S[3], S[2], S[1], T[1]},
                          {S[2], S[1], S[0], T[0]}};
        double sol[3];
        if (!solve3(A, sol))
            break;
        /* y = A t^2 + B t + C avec t = x - mx, a developper en x. */
        out->a = sol[0];
        out->b = sol[1] - 2 * sol[0] * mx;
        out->c = sol[0] * mx * mx - sol[1] * mx + sol[2];
        out->r = NAN;
        ok = true;
        break;
    }
    }

    if (ok)
    {
        double my = 0;
        for (int i = 0; i < m; i++)
            my += py[i];
        my /= m;
        double sres = 0, stot = 0;
        for (int i = 0; i < m; i++)
        {
            double e = py[i] - reg_eval(out, px[i]);
            double d = py[i] - my;
            sres += e * e;
            stot += d * d;
        }
        out->r2 = stot > 0 ? 1.0 - sres / stot : 1.0;
        if (model == REG_QUAD)
            out->r = NAN;
        ok = isfinite(out->a) && isfinite(out->b) && isfinite(out->c);
    }
    free(u);
    free(v);
    free(px);
    free(py);
    return ok;
}

/* ------------------------- Lois de probabilite --------------------------- */

static const law_info k_laws[LAW_COUNT] = {
    {"Binomiale", "B(n,p)", 2, {"n", "p"}, {10, 0.5}, true},
    {"Normale", "N(mu,sigma)", 2, {"mu", "sigma"}, {0, 1}, false},
    {"Geometrique", "G(p)", 1, {"p", ""}, {0.5, 0}, true},
    {"Poisson", "P(lambda)", 1, {"lambda", ""}, {3, 0}, true},
    {"Exponentielle", "E(lambda)", 1, {"lambda", ""}, {1, 0}, false},
    {"Uniforme", "U([a,b])", 2, {"a", "b"}, {0, 1}, false},
};

/* Bornes de securite pour les sommes des lois discretes. */
#define BINOM_MAX_N 100000
#define POISSON_MAX_K 200000

const law_info *law_get(int law)
{
    return (law >= 0 && law < LAW_COUNT) ? &k_laws[law] : NULL;
}

static bool is_int(double v)
{
    return isfinite(v) && fabs(v - round(v)) < 1e-9;
}

bool law_params_ok(int law, const double *p)
{
    switch (law)
    {
    case LAW_BINOMIAL:
        return is_int(p[0]) && p[0] >= 0 && p[0] <= BINOM_MAX_N && p[1] >= 0 && p[1] <= 1;
    case LAW_NORMAL:
        return isfinite(p[0]) && isfinite(p[1]) && p[1] > 0;
    case LAW_GEOMETRIC:
        return p[0] > 0 && p[0] <= 1;
    case LAW_POISSON:
        return isfinite(p[0]) && p[0] > 0 && p[0] <= 10000;
    case LAW_EXPONENTIAL:
        return isfinite(p[0]) && p[0] > 0;
    case LAW_UNIFORM:
        return isfinite(p[0]) && isfinite(p[1]) && p[0] < p[1];
    default:
        return false;
    }
}

double normal_cdf_std(double z)
{
    return 0.5 * erfc(-z / sqrt(2.0));
}

/* Inverse de la fonction de repartition de N(0,1) : approximation rationnelle
 * de P. J. Acklam (erreur relative ~1e-9) puis un pas de Halley (~1e-15). */
double normal_inv_std(double q)
{
    if (!(q > 0 && q < 1))
        return q == 0 ? -INFINITY : (q == 1 ? INFINITY : NAN);
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02,
                               -2.759285104469687e+02, 1.383577518672690e+02,
                               -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02,
                               -1.556989798598866e+02, 6.680131188771972e+01,
                               -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
                               -2.400758277161838e+00, -2.549732539343734e+00,
                               4.374664141464968e+00, 2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01,
                               2.445134137142996e+00, 3.754408661907416e+00};
    const double plow = 0.02425, phigh = 1 - plow;
    double x;
    if (q < plow)
    {
        double t = sqrt(-2 * log(q));
        x = (((((c[0] * t + c[1]) * t + c[2]) * t + c[3]) * t + c[4]) * t + c[5]) /
            ((((d[0] * t + d[1]) * t + d[2]) * t + d[3]) * t + 1);
    }
    else if (q <= phigh)
    {
        double t = q - 0.5, r = t * t;
        x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * t /
            (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
    }
    else
    {
        double t = sqrt(-2 * log(1 - q));
        x = -(((((c[0] * t + c[1]) * t + c[2]) * t + c[3]) * t + c[4]) * t + c[5]) /
            ((((d[0] * t + d[1]) * t + d[2]) * t + d[3]) * t + 1);
    }
    double e = normal_cdf_std(x) - q;
    double u = e * sqrt(2 * M_PI) * exp(x * x / 2);
    x = x - u / (1 + x * u / 2);
    return x;
}

static double binom_pmf(double n, double p, double k)
{
    if (!is_int(k) || k < 0 || k > n)
        return 0;
    k = round(k);
    if (p == 0)
        return k == 0 ? 1 : 0;
    if (p == 1)
        return k == n ? 1 : 0;
    double lc = lgamma(n + 1) - lgamma(k + 1) - lgamma(n - k + 1);
    return exp(lc + k * log(p) + (n - k) * log1p(-p));
}

static double poisson_pmf(double l, double k)
{
    if (!is_int(k) || k < 0)
        return 0;
    k = round(k);
    return exp(-l + k * log(l) - lgamma(k + 1));
}

double law_pdf(int law, const double *p, double x)
{
    if (!law_params_ok(law, p) || isnan(x))
        return NAN;
    switch (law)
    {
    case LAW_BINOMIAL:
        return binom_pmf(round(p[0]), p[1], x);
    case LAW_NORMAL:
    {
        double z = (x - p[0]) / p[1];
        return exp(-z * z / 2) / (p[1] * sqrt(2 * M_PI));
    }
    case LAW_GEOMETRIC:
        if (!is_int(x) || x < 1)
            return 0;
        return pow(1 - p[0], round(x) - 1) * p[0];
    case LAW_POISSON:
        return poisson_pmf(p[0], x);
    case LAW_EXPONENTIAL:
        return x < 0 ? 0 : p[0] * exp(-p[0] * x);
    case LAW_UNIFORM:
        return (x < p[0] || x > p[1]) ? 0 : 1.0 / (p[1] - p[0]);
    }
    return NAN;
}

double law_cdf(int law, const double *p, double x)
{
    if (!law_params_ok(law, p) || isnan(x))
        return NAN;
    switch (law)
    {
    case LAW_BINOMIAL:
    {
        double n = round(p[0]);
        if (x < 0)
            return 0;
        if (x >= n)
            return 1;
        double k = floor(x + 1e-9), s = 0;
        for (double i = 0; i <= k; i++)
            s += binom_pmf(n, p[1], i);
        return s > 1 ? 1 : s;
    }
    case LAW_NORMAL:
        return normal_cdf_std((x - p[0]) / p[1]);
    case LAW_GEOMETRIC:
        if (x < 1)
            return 0;
        return 1 - pow(1 - p[0], floor(x + 1e-9));
    case LAW_POISSON:
    {
        if (x < 0)
            return 0;
        double k = floor(x + 1e-9);
        if (k > POISSON_MAX_K)
            return 1;
        double s = 0;
        for (double i = 0; i <= k; i++)
        {
            double t = poisson_pmf(p[0], i);
            s += t;
            if (i > p[0] && t < 1e-17)
                break;
        }
        return s > 1 ? 1 : s;
    }
    case LAW_EXPONENTIAL:
        return x <= 0 ? 0 : -expm1(-p[0] * x);
    case LAW_UNIFORM:
        if (x <= p[0])
            return 0;
        if (x >= p[1])
            return 1;
        return (x - p[0]) / (p[1] - p[0]);
    }
    return NAN;
}

double law_interval(int law, const double *p, double a, double b)
{
    if (!law_params_ok(law, p) || isnan(a) || isnan(b))
        return NAN;
    if (a > b)
        return 0;
    double r;
    /* Queue droite : 1 - P(X < a) perd tous ses chiffres (P(Z >= 30) donnait
     * 0 au lieu de 4.9e-198). On somme ou on utilise la fonction de survie. */
    switch (law)
    {
    case LAW_NORMAL:
    {
        double za = (a - p[0]) / p[1], zb = (b - p[0]) / p[1];
        r = za > 0 ? normal_cdf_std(-za) - normal_cdf_std(-zb) : normal_cdf_std(zb) - normal_cdf_std(za);
        break;
    }
    case LAW_EXPONENTIAL:
    {
        double lo = a < 0 ? 0 : a;
        if (b <= lo)
            return 0;
        /* e^(-l lo) - e^(-l b) */
        r = exp(-p[0] * lo) * -expm1(-p[0] * (b - lo));
        break;
    }
    case LAW_GEOMETRIC:
    {
        double lo = ceil(a - 1e-9), hi = floor(b + 1e-9);
        if (lo < 1)
            lo = 1;
        if (hi < lo)
            return 0;
        /* P(lo <= X <= hi) = (1-p)^(lo-1) - (1-p)^hi */
        double l1p = log1p(-p[0]);
        r = exp((lo - 1) * l1p) * (isinf(hi) ? 1 : -expm1((hi - lo + 1) * l1p));
        break;
    }
    case LAW_BINOMIAL:
    case LAW_POISSON:
    {
        double lo = ceil(a - 1e-9), hi = floor(b + 1e-9);
        if (lo < 0)
            lo = 0;
        double mean = law_mean(law, p);
        if (lo <= mean)
        {
            r = law_cdf(law, p, hi) - (lo > 0 ? law_cdf(law, p, lo - 1) : 0);
            break;
        }
        /* au-dessus de la moyenne : somme directe des termes, qui decroissent */
        double top = law == LAW_BINOMIAL ? fmin(hi, round(p[0])) : fmin(hi, (double)POISSON_MAX_K);
        r = 0;
        for (double k = lo; k <= top; k++)
        {
            double t = law_pdf(law, p, k);
            r += t;
            if (t < 1e-17 * r || t == 0)
                break;
        }
        break;
    }
    default:
        r = law_cdf(law, p, b) - law_cdf(law, p, a);
    }
    return r < 0 ? 0 : (r > 1 ? 1 : r);
}

double law_inverse(int law, const double *p, double q)
{
    if (!law_params_ok(law, p) || !(q > 0 && q < 1))
        return NAN;
    const double eps = 1e-12;
    switch (law)
    {
    case LAW_NORMAL:
        return p[0] + p[1] * normal_inv_std(q);
    case LAW_EXPONENTIAL:
        return -log1p(-q) / p[0];
    case LAW_UNIFORM:
        return p[0] + q * (p[1] - p[0]);
    case LAW_GEOMETRIC:
    {
        if (p[0] == 1)
            return 1;
        double k = ceil(log1p(-q) / log1p(-p[0]) - 1e-9);
        if (k < 1)
            k = 1;
        /* Correction des erreurs d'arrondi au voisinage d'un entier. */
        while (k > 1 && law_cdf(law, p, k - 1) >= q - eps)
            k--;
        while (law_cdf(law, p, k) < q - eps)
            k++;
        return k;
    }
    case LAW_BINOMIAL:
    {
        double n = round(p[0]), s = 0;
        for (double k = 0; k <= n; k++)
        {
            s += binom_pmf(n, p[1], k);
            if (s >= q - eps)
                return k;
        }
        return n;
    }
    case LAW_POISSON:
    {
        double s = 0;
        for (double k = 0; k <= POISSON_MAX_K; k++)
        {
            s += poisson_pmf(p[0], k);
            if (s >= q - eps)
                return k;
        }
        return NAN;
    }
    }
    return NAN;
}

double law_mean(int law, const double *p)
{
    if (!law_params_ok(law, p))
        return NAN;
    switch (law)
    {
    case LAW_BINOMIAL:
        return round(p[0]) * p[1];
    case LAW_NORMAL:
        return p[0];
    case LAW_GEOMETRIC:
        return 1 / p[0];
    case LAW_POISSON:
        return p[0];
    case LAW_EXPONENTIAL:
        return 1 / p[0];
    case LAW_UNIFORM:
        return (p[0] + p[1]) / 2;
    }
    return NAN;
}

double law_sd(int law, const double *p)
{
    if (!law_params_ok(law, p))
        return NAN;
    switch (law)
    {
    case LAW_BINOMIAL:
        return sqrt(round(p[0]) * p[1] * (1 - p[1]));
    case LAW_NORMAL:
        return p[1];
    case LAW_GEOMETRIC:
        return sqrt(1 - p[0]) / p[0];
    case LAW_POISSON:
        return sqrt(p[0]);
    case LAW_EXPONENTIAL:
        return 1 / p[0];
    case LAW_UNIFORM:
        return (p[1] - p[0]) / sqrt(12.0);
    }
    return NAN;
}

/* ------------------------------ Student ---------------------------------- */

/* Fraction continue de la fonction beta incomplete (methode de Lentz). */
static double beta_cf(double a, double b, double x)
{
    const double tiny = 1e-300;
    double c = 1, d = 1 - (a + b) * x / (a + 1);
    if (fabs(d) < tiny)
        d = tiny;
    d = 1 / d;
    double f = d;
    for (int m = 1; m <= 300; m++)
    {
        double m2 = 2.0 * m;
        double num = m * (b - m) * x / ((a + m2 - 1) * (a + m2));
        d = 1 + num * d;
        c = 1 + num / c;
        if (fabs(d) < tiny)
            d = tiny;
        if (fabs(c) < tiny)
            c = tiny;
        d = 1 / d;
        f *= d * c;
        num = -(a + m) * (a + b + m) * x / ((a + m2) * (a + m2 + 1));
        d = 1 + num * d;
        c = 1 + num / c;
        if (fabs(d) < tiny)
            d = tiny;
        if (fabs(c) < tiny)
            c = tiny;
        d = 1 / d;
        double delta = d * c;
        f *= delta;
        if (fabs(delta - 1) < 1e-15)
            break;
    }
    return f;
}

/* Fonction beta incomplete regularisee I_x(a, b). */
static double inc_beta(double a, double b, double x)
{
    if (x <= 0)
        return 0;
    if (x >= 1)
        return 1;
    double lbeta = lgamma(a + b) - lgamma(a) - lgamma(b) + a * log(x) + b * log(1 - x);
    if (x < (a + 1) / (a + b + 2))
        return exp(lbeta) * beta_cf(a, b, x) / a;
    return 1 - exp(lbeta) * beta_cf(b, a, 1 - x) / b;
}

double student_pdf(double t, double df)
{
    if (!(df > 0) || !isfinite(t))
        return isfinite(t) ? NAN : 0;
    double l = lgamma((df + 1) / 2) - lgamma(df / 2) - 0.5 * log(df * M_PI);
    return exp(l - (df + 1) / 2 * log(1 + t * t / df));
}

double student_cdf(double t, double df)
{
    if (!(df > 0) || isnan(t))
        return NAN;
    if (t == INFINITY)
        return 1;
    if (t == -INFINITY)
        return 0;
    double tail = 0.5 * inc_beta(df / 2, 0.5, df / (df + t * t)); /* P(T > |t|) */
    return t > 0 ? 1 - tail : tail;
}

double student_inv(double q, double df)
{
    if (!(df > 0) || !(q > 0 && q < 1))
        return NAN;
    if (q == 0.5)
        return 0;
    /* Encadrement puis dichotomie : la fonction est strictement croissante. */
    double lo = -1, hi = 1;
    while (student_cdf(lo, df) > q && lo > -1e12)
        lo *= 2;
    while (student_cdf(hi, df) < q && hi < 1e12)
        hi *= 2;
    for (int i = 0; i < 200; i++)
    {
        double mid = 0.5 * (lo + hi);
        if (student_cdf(mid, df) < q)
            lo = mid;
        else
            hi = mid;
        if (hi - lo <= 1e-13 * fmax(1.0, fabs(mid)))
            break;
    }
    return 0.5 * (lo + hi);
}

/* --------------- Tests statistiques et intervalles de confiance ---------- */

static const inf_info k_inf[INF_COUNT] = {
    {"1 proportion", 3, {"p0", "x (succes)", "n"}, {0.5, 52, 100}, true, false},
    {"2 proportions", 4, {"x1", "n1", "x2", "n2"}, {45, 100, 30, 100}, false, false},
    {"1 moyenne (sigma connu)", 4, {"mu0", "moyenne", "sigma", "n"}, {10, 10.4, 1.2, 50}, true, false},
    {"1 moyenne (Student)", 4, {"mu0", "moyenne", "s", "n"}, {10, 10.4, 1.2, 20}, true, true},
    {"2 moyennes (Student)", 6, {"moyenne 1", "s1", "n1", "moyenne 2", "s2", "n2"},
     {12.1, 2.3, 30, 10.9, 1.9, 25}, false, true},
};

const inf_info *inf_get(int method)
{
    return (method >= 0 && method < INF_COUNT) ? &k_inf[method] : &k_inf[0];
}

static bool is_count(double n, double min)
{
    return isfinite(n) && n >= min && n == floor(n);
}

bool inf_params_ok(int method, const double *p)
{
    for (int i = 0; i < inf_get(method)->nparams; i++)
        if (!isfinite(p[i]))
            return false;
    switch (method)
    {
    case INF_PROP1:
        return p[0] > 0 && p[0] < 1 && is_count(p[2], 1) && is_count(p[1], 0) && p[1] <= p[2];
    case INF_PROP2:
        return is_count(p[1], 1) && is_count(p[3], 1) && is_count(p[0], 0) && is_count(p[2], 0) && p[0] <= p[1] &&
               p[2] <= p[3];
    case INF_MEAN_Z:
        return p[2] > 0 && is_count(p[3], 1);
    case INF_MEAN_T:
        return p[2] > 0 && is_count(p[3], 2);
    case INF_MEAN2_T:
        return p[1] > 0 && p[4] > 0 && is_count(p[2], 2) && is_count(p[5], 2);
    }
    return false;
}

/* Degres de liberte de Welch-Satterthwaite. */
static double welch_df(double v1, double n1, double v2, double n2)
{
    double a = v1 / n1, b = v2 / n2;
    return (a + b) * (a + b) / (a * a / (n1 - 1) + b * b / (n2 - 1));
}

static double tail_pvalue(double stat, double df, int alt)
{
    double cdf = isnan(df) ? normal_cdf_std(stat) : student_cdf(stat, df);
    double upper = isnan(df) ? normal_cdf_std(-stat) : student_cdf(-stat, df); /* P(Z >= stat) */
    switch (alt)
    {
    case ALT_LT:
        return cdf;
    case ALT_GT:
        return upper;
    default:
        return fmin(1.0, 2 * fmin(cdf, upper));
    }
}

bool inf_test(int method, const double *p, int alt, inf_test_result *out)
{
    if (!inf_params_ok(method, p) || alt < 0 || alt >= ALT_COUNT)
        return false;
    double stat = NAN, df = NAN, est = NAN;
    switch (method)
    {
    case INF_PROP1:
        est = p[1] / p[2];
        stat = (est - p[0]) / sqrt(p[0] * (1 - p[0]) / p[2]);
        break;
    case INF_PROP2:
    {
        double p1 = p[0] / p[1], p2 = p[2] / p[3];
        double pool = (p[0] + p[2]) / (p[1] + p[3]);
        double se = sqrt(pool * (1 - pool) * (1 / p[1] + 1 / p[3]));
        est = p1 - p2;
        if (se == 0)
            return false; /* 0 ou 100 % de succes dans les deux echantillons */
        stat = est / se;
        break;
    }
    case INF_MEAN_Z:
        est = p[1];
        stat = (p[1] - p[0]) / (p[2] / sqrt(p[3]));
        break;
    case INF_MEAN_T:
        est = p[1];
        stat = (p[1] - p[0]) / (p[2] / sqrt(p[3]));
        df = p[3] - 1;
        break;
    case INF_MEAN2_T:
    {
        double v1 = p[1] * p[1], v2 = p[4] * p[4];
        est = p[0] - p[3];
        stat = est / sqrt(v1 / p[2] + v2 / p[5]);
        df = welch_df(v1, p[2], v2, p[5]);
        break;
    }
    }
    if (!isfinite(stat))
        return false;
    out->stat = stat;
    out->df = df;
    out->estimate = est;
    out->pvalue = tail_pvalue(stat, df, alt);
    return true;
}

bool inf_interval(int method, const double *p, double level, inf_ci_result *out)
{
    if (!(level > 0 && level < 1))
        return false;
    /* p0 / mu0 n'intervient pas : on le remplace par une valeur valide. */
    double q[INF_MAX_PARAMS];
    memcpy(q, p, sizeof(q));
    if (inf_get(method)->first_is_h0)
        q[0] = (method == INF_PROP1) ? 0.5 : 0;
    if (!inf_params_ok(method, q))
        return false;
    double est = NAN, se = NAN, df = NAN;
    switch (method)
    {
    case INF_PROP1:
        est = q[1] / q[2];
        se = sqrt(est * (1 - est) / q[2]);
        break;
    case INF_PROP2:
    {
        double p1 = q[0] / q[1], p2 = q[2] / q[3];
        est = p1 - p2;
        se = sqrt(p1 * (1 - p1) / q[1] + p2 * (1 - p2) / q[3]);
        break;
    }
    case INF_MEAN_Z:
        est = q[1];
        se = q[2] / sqrt(q[3]);
        break;
    case INF_MEAN_T:
        est = q[1];
        se = q[2] / sqrt(q[3]);
        df = q[3] - 1;
        break;
    case INF_MEAN2_T:
    {
        double v1 = q[1] * q[1], v2 = q[4] * q[4];
        est = q[0] - q[3];
        se = sqrt(v1 / q[2] + v2 / q[5]);
        df = welch_df(v1, q[2], v2, q[5]);
        break;
    }
    }
    double a = (1 + level) / 2;
    double crit = isnan(df) ? normal_inv_std(a) : student_inv(a, df);
    if (!isfinite(crit) || !isfinite(se))
        return false;
    out->estimate = est;
    out->se = se;
    out->df = df;
    out->crit = crit;
    out->margin = crit * se;
    out->lo = est - out->margin;
    out->hi = est + out->margin;
    return true;
}
