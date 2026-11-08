#ifndef STATS_H
#define STATS_H

/* Statistiques, regressions et lois de probabilite (Core/Stats.cpp).
 * Fonctions pures, sans affichage : utilisees par les apps Statistiques et
 * Probabilites, et testees sur PC (tests/test_stats.cpp). */

/* ---------------------------- Une variable ------------------------------- */

typedef struct
{
    double n;      /* effectif total (somme des effectifs) */
    double sum;    /* somme des x*effectif */
    double sum_sq; /* somme des x^2*effectif */
    double mean;
    double var;    /* variance (population, divisee par n) */
    double sd;     /* ecart-type sigma (population) */
    double sd_s;   /* ecart-type corrige s (echantillon, divise par n-1) */
    double min, max;
    double q1, median, q3; /* quartiles au sens du programme francais */
    double range;
    double iqr;    /* ecart interquartile q3 - q1 */
} stats1;

/* x[i] : valeurs, w[i] : effectifs (NULL = tous a 1). Les effectifs <= 0 ou
 * non finis et les valeurs non finies sont ignores. Retourne false si aucun
 * point valide. */
bool stats_one_var(const double *x, const double *w, int count, stats1 *out);

/* ----------------------------- Regressions ------------------------------- */

enum reg_model
{
    REG_LINEAR = 0, /* y = a x + b */
    REG_QUAD,       /* y = a x^2 + b x + c */
    REG_EXP,        /* y = a e^(b x)   (ajustement de ln y) */
    REG_POWER,      /* y = a x^b       (ajustement de ln y / ln x) */
    REG_LOG,        /* y = a + b ln x */
    REG_COUNT
};

typedef struct
{
    int model;
    double a, b, c;
    double r;  /* coefficient de correlation (modele lineaire ou linearise) */
    double r2; /* coefficient de determination calcule sur y */
    int n;
} regression;

const char *reg_model_name(int model);
const char *reg_model_formula(int model);

bool regress(const double *x, const double *y, int count, int model, regression *out);
double reg_eval(const regression *r, double x);

/* ------------------------- Lois de probabilite --------------------------- */

enum law_id
{
    LAW_BINOMIAL = 0, /* B(n, p)          entier 0..n */
    LAW_NORMAL,       /* N(mu, sigma)     reelle */
    LAW_GEOMETRIC,    /* G(p)             entier >= 1 (rang du 1er succes) */
    LAW_POISSON,      /* P(lambda)        entier >= 0 */
    LAW_EXPONENTIAL,  /* E(lambda)        reelle >= 0 */
    LAW_UNIFORM,      /* U([a, b])        reelle */
    LAW_COUNT
};

typedef struct
{
    const char *name;
    const char *short_name;
    int nparams;
    const char *pname[2];
    double pdef[2];
    bool discrete;
} law_info;

const law_info *law_get(int law);

/* Parametres valides pour la loi ? */
bool law_params_ok(int law, const double *p);

/* P(X = k) pour une loi discrete, densite f(x) pour une loi continue. */
double law_pdf(int law, const double *p, double x);

/* P(X <= x). */
double law_cdf(int law, const double *p, double x);

/* P(a <= X <= b) (bornes incluses pour les lois discretes). */
double law_interval(int law, const double *p, double a, double b);

/* Plus petit x tel que P(X <= x) >= q (q dans ]0,1[). Pour une loi continue,
 * c'est l'inverse exact de la fonction de repartition. NAN si impossible. */
double law_inverse(int law, const double *p, double q);

double law_mean(int law, const double *p);
double law_sd(int law, const double *p);

/* Briques de base exposees pour les tests. */
double normal_cdf_std(double z);
double normal_inv_std(double q);

#endif
