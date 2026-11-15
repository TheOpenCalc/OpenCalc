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

/* Loi de Student a df degres de liberte (df > 0, pas forcement entier). */
double student_pdf(double t, double df);
double student_cdf(double t, double df);
double student_inv(double q, double df);

/* --------------- Tests statistiques et intervalles de confiance ---------- */

enum inf_method
{
    INF_PROP1 = 0, /* 1 proportion (Z)           p0 ; x ; n */
    INF_PROP2,     /* 2 proportions (Z)          x1 ; n1 ; x2 ; n2 */
    INF_MEAN_Z,    /* 1 moyenne, sigma connu (Z) mu0 ; xbar ; sigma ; n */
    INF_MEAN_T,    /* 1 moyenne (T de Student)   mu0 ; xbar ; s ; n */
    INF_MEAN2_T,   /* 2 moyennes (T de Welch)    xbar1 ; s1 ; n1 ; xbar2 ; s2 ; n2 */
    INF_COUNT
};

/* Hypothese alternative H1 : p != p0, p < p0, p > p0 (idem pour mu, ou pour
 * p1 - p2 et mu1 - mu2 comparees a 0). */
enum inf_alt
{
    ALT_NE = 0,
    ALT_LT,
    ALT_GT,
    ALT_COUNT
};

#define INF_MAX_PARAMS 6

typedef struct
{
    const char *name;
    int nparams;
    const char *pname[INF_MAX_PARAMS];
    double pdef[INF_MAX_PARAMS];
    /* Le premier parametre (p0, mu0) ne sert qu'au test, pas a l'intervalle. */
    bool first_is_h0;
    bool student; /* statistique T (sinon Z) */
} inf_info;

const inf_info *inf_get(int method);

typedef struct
{
    double stat;   /* z ou t */
    double df;     /* degres de liberte (T), NAN pour Z */
    double pvalue;
    double estimate; /* p chapeau, xbar, p1 - p2 ou xbar1 - xbar2 */
} inf_test_result;

typedef struct
{
    double lo, hi;
    double estimate;
    double margin;   /* demi-largeur */
    double crit;     /* z* ou t* */
    double df;
    double se;       /* ecart-type de l'estimateur */
} inf_ci_result;

/* Parametres valides ? (effectifs entiers > 0, 0 <= x <= n, ecarts-types > 0,
 * 0 < p0 < 1...) */
bool inf_params_ok(int method, const double *p);

/* p : tableau de INF_MAX_PARAMS valeurs, dans l'ordre de inf_info.pname. */
bool inf_test(int method, const double *p, int alt, inf_test_result *out);

/* level : niveau de confiance dans ]0 ; 1[ (0,95 pour 95 %). */
bool inf_interval(int method, const double *p, double level, inf_ci_result *out);

#endif
