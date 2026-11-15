#include "ui.h"
#include "Evaluator.h"
#include "sequences.h"
#include "stack.h"
#include <math.h>
#include <cmath>
#include "menu.h"
#include <algorithm>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <cstdio>
#include "Solver.h"
#include <stdio.h>
#include "colors.h"
#include <string.h>

#include <time.h>

#ifndef SOLVER_HOST_TEST
#include "Cas.h"

/* Etiquette sous les solutions : "Exact", "Approche, a 2k*pi pres",
 * "Aucune solution"... */
static void acc_set(text_box *acc, const char *a, const char *b = nullptr)
{
    if (b != nullptr && b[0])
        snprintf(acc->text_buf, 100, "%s, %s", a, b);
    else
        snprintf(acc->text_buf, 100, "%s", a);
    acc->text = acc->text_buf;
    acc->t_size = (int)strlen(acc->text);
}

/* Resout par le calcul formel (resoudre) les nb lignes du Solveur, qui n'ont
 * qu'une inconnue : une equation, une inequation, ou plusieurs relations a
 * verifier ensemble (2X+1>0 et X-3<0). Ecrit une solution ou un intervalle
 * par ligne de sol[], et l'etiquette dans acc. Rend le nombre de lignes, ou
 * -1 quand le moteur ne sait pas faire et que must est faux (la methode
 * numerique prend alors le relais). must : il n'y a pas d'autre methode
 * (inequation), le message du moteur est affiche. */
static int solve_cas_lines(fill_box **eqs, int nb, char letter, fill_box **sol, int max_sol,
                           text_box *acc, bool must)
{
    static char in[1100];
    static char out[1024];
    int n = 0;
    const char *head = "resoudre(";
    for (const char *h = head; *h; h++)
        in[n++] = *h;
    for (int e = 0; e < nb; e++)
    {
        if (n + eqs[e]->t_size > (int)sizeof(in) - 4)
        {
            if (must)
                acc_set(acc, "Saisie trop longue");
            return must ? 0 : -1;
        }
        if (e > 0)
            in[n++] = CAS_SEPARATOR;
        for (int i = 0; i < eqs[e]->t_size; i++)
        {
            char c = eqs[e]->text[i];
            in[n++] = (c == letter) ? 'X' : c; /* le moteur resout en X */
        }
    }
    in[n++] = ')';
    in[n] = '\0';
    cas_result r = cas_eval(in, n, out, sizeof(out));
    if (r.status != CAS_OK)
    {
        if (!must)
            return -1;
        acc_set(acc, r.status == CAS_ERROR && r.msg[0] ? r.msg : "Inequation non geree");
        return 0;
    }
    if (!r.solutions && !must)
        return -1;
    if (!out[0])
    {
        /* Message seul : "Aucune solution", "Tous les reels"... */
        acc_set(acc, r.msg[0] ? r.msg : "Aucune solution");
        return 0;
    }
    int count = 0;
    const char *p = out;
    while (*p && count < max_sol)
    {
        const char *e = strchr(p, CAS_SEPARATOR);
        int len = e ? (int)(e - p) : (int)strlen(p);
        if (len > 990)
            len = 990;
        for (int i = 0; i < len; i++)
            sol[count]->text[i] = (p[i] == 'X') ? letter : p[i];
        sol[count]->text[len] = '\0';
        sol[count]->t_size = len;
        sol[count]->curso_pos = 0;
        count++;
        if (!e)
            break;
        p = e + 1;
    }
    /* La note du moteur compte : sin(X)=1/2 a deux solutions "a 2k*pi
     * pres", pas deux en tout. */
    acc_set(acc, r.approximate ? "Approche" : "Exact", r.msg);
    return count;
}
#endif

void simplify_first(double *a, double *b, int n, int first_nb)
{
    double K = b[first_nb] / a[first_nb];
    for (int i = first_nb; i < n; i++)
        b[i] -= a[i] * K;
}

void normalize(double *a, int size, int n)
{
    double K = 1.0 / a[n];
    for (int i = 0; i < size; i++)
        a[i] *= K;
}

double *solve(double **input, int nb_var, int nb_eq)
{
    for (int i = 0; i < nb_eq - 1; i++)
    {
        int max_row = i;
        double max_val = fabs(input[i][i]);
        for (int k = i + 1; k < nb_eq; k++)
        {
            if (fabs(input[k][i]) > max_val)
            {
                max_val = fabs(input[k][i]);
                max_row = k;
            }
        }
        if (max_row != i)
        {
            double *tmp = input[i];
            input[i] = input[max_row];
            input[max_row] = tmp;
        }
        for (int k = i + 1; k < nb_eq; k++)
            if (fabs(input[i][i]) > 1e-12)
                simplify_first(input[i], input[k], nb_var + 1, i);
    }
    for (int i = 0; i < nb_eq; i++)
        if (fabs(input[i][i]) > 1e-12)
            normalize(input[i], nb_var + 1, i);

    double *solution = (double *)malloc(sizeof(double) * nb_var);
    for (int i = 0; i < nb_var; i++)
        solution[i] = input[i][nb_var];

    for (int i = nb_eq - 1; i >= 0; i--)
        for (int k = i + 1; k < nb_var; k++)
            solution[i] -= solution[k] * input[i][k];

    return solution;
}


static complex **init_c_Mat(int L, int H, complex default_value)
{
    complex **a = (complex **)malloc(sizeof(complex *) * H);
    for (int i = 0; i < H; i++)
    {
        a[i] = (complex *)malloc(sizeof(complex) * L);
        for (int j = 0; j < L; j++)
        {
            a[i][j] = default_value;
        }
    }
    return a;
}

static void free_c_Mat(complex **a, int H)
{
    for (int i = 0; i < H; i++)
        free(a[i]);
    free(a);
}

/* Elimination de Gauss-Jordan a pivot partiel sur a : m lignes, n inconnues,
 * second membre en colonne n. Les lignes doivent etre a la meme echelle
 * (coefficients de l'ordre de 1 au plus) : un pivot ou un second membre plus
 * petit que le bruit d'arrondi est alors un zero.
 * Rend le rang. *consistent est faux s'il reste une ligne "0 = c", c non nul.
 * Rang n et systeme compatible : sol recoit l'unique solution.
 * Avant, la resolution supposait le systeme regulier : A+B=2 ; 2A+2B=4
 * rendait "A=2, B=0, Exact" et A+B=2 ; A+B=3 des valeurs sans rapport. */
static int gauss_c(complex **a, int m, int n, complex *sol, bool *consistent)
{
    const double PIVOT_MIN = 1e-11;
    int *pivcol = (int *)malloc(sizeof(int) * (n > 0 ? n : 1));
    int rank = 0;
    for (int col = 0; col < n && rank < m; col++)
    {
        int best = rank;
        double bv = cmod(a[rank][col]).re;
        for (int r = rank + 1; r < m; r++)
        {
            double v = cmod(a[r][col]).re;
            if (v > bv)
            {
                bv = v;
                best = r;
            }
        }
        if (!(bv > PIVOT_MIN)) /* aussi : pivot NaN */
            continue;
        complex *tmp = a[rank];
        a[rank] = a[best];
        a[best] = tmp;
        for (int r = 0; r < m; r++)
        {
            if (r == rank)
                continue;
            complex K = cdiv(a[r][col], a[rank][col]);
            for (int j = col; j <= n; j++)
                a[r][j] = csub(a[r][j], cmul(K, a[rank][j]));
            a[r][col] = (complex){0, 0};
        }
        pivcol[rank++] = col;
    }
    *consistent = true;
    for (int r = rank; r < m; r++)
        if (!(cmod(a[r][n]).re <= 1e-9))
            *consistent = false;
    if (rank == n && *consistent && sol != nullptr)
        for (int k = 0; k < n; k++)
            sol[pivcol[k]] = cdiv(a[k][n], a[k][pivcol[k]]);
    free(pivcol);
    return rank;
}

/* Evaluation par le moteur principal : toutes les fonctions de la
 * Calculatrice (exp, abs, min, max, partie entiere...) sont reconnues. */
static complex eval_c_multi(token *in, int n,
                            complex *values, int nb_var, int *var_indices)
{
    int letters[32];
    int nv = nb_var < 32 ? nb_var : 32;
    for (int v = 0; v < nv; v++)
        letters[v] = 'A' + var_indices[v];
    return evaluate_npi_c_vars(in, n, values, letters, nv);
}

/* Les equations d'un systeme, telles que le Solveur les a lues. */
typedef struct
{
    token **lt, **rt; /* gauche et droite de chaque equation (RPN) */
    int *ls, *rs;
    int ne, nv;
    int *vi;   /* lettre de chaque inconnue (0 = A) */
    long evals; /* evaluations faites : borne la recherche numerique */
} sys_eqs;

/* Residu relatif en x : plus grand |gauche - droite| / (1 + |gauche| +
 * |droite|). F (si non NULL) recoit gauche - droite et mag |gauche| +
 * |droite|. Infini si une equation n'est pas definie en x. */
static double sys_residual(sys_eqs *q, complex *x, complex *F, double *mag = nullptr)
{
    double m = 0;
    q->evals++;
    for (int i = 0; i < q->ne; i++)
    {
        complex l = eval_c_multi(q->lt[i], q->ls[i], x, q->nv, q->vi);
        complex r = eval_c_multi(q->rt[i], q->rs[i], x, q->nv, q->vi);
        complex d = csub(l, r);
        if (F)
            F[i] = d;
        if (mag)
            mag[i] = cmod(l).re + cmod(r).re;
        double rel = cmod(d).re / (1.0 + cmod(l).re + cmod(r).re);
        if (!(rel == rel) || !std::isfinite(rel))
            return INFINITY;
        if (rel > m)
            m = rel;
    }
    return m;
}

#define SYS_MAX_SOL 6         /* solutions gardees d'un systeme non lineaire */
#define SYS_EVAL_BUDGET 12000 /* evaluations du systeme, tous departs confondus */
#define SYS_TOL 1e-12         /* residu relatif d'une solution */

/* Methode de Newton depuis x (n = nv = ne), avec retour en arriere : un pas
 * qui n'ameliore pas le residu est raccourci. Vrai si x est une solution :
 * chaque equation est verifiee a SYS_TOL pres, relativement a la taille de
 * ses termes (ses deux membres et sa variation |dF/dx| (1 + |x|)), ET le
 * dernier pas est petit. Sans la seconde condition, une equation qui tend vers 0
 * sans s'annuler (e^A = 0) "convergeait" vers un point quelconque ; et sans
 * verification du tout, le meilleur point d'un systeme sans solution etait
 * affiche comme solution (A^2+B^2=1 ; A^2+B^2=2 rendait A=1, B=1). */
static bool newton_c(sys_eqs *q, complex *x)
{
    int n = q->nv;
    complex *F = (complex *)malloc(n * sizeof(complex));
    complex *xn = (complex *)malloc(n * sizeof(complex));
    complex *dx = (complex *)malloc(n * sizeof(complex));
    double *mag = (double *)malloc(n * sizeof(double));
    double *var = (double *)calloc(n, sizeof(double)); /* somme |dFi/dxj| (1 + |xj|) */
    complex **J = init_c_Mat(n + 1, n, (complex){0, 0});
    double f = sys_residual(q, x, F, mag);
    double last_step = f == 0 ? 0 : INFINITY;
    int stall = 0;
    for (int it = 0; it < 60 && std::isfinite(f) && f != 0 && q->evals < SYS_EVAL_BUDGET; it++)
    {
        for (int j = 0; j < n; j++)
        {
            double h = 1e-7 * (1.0 + cmod(x[j]).re);
            complex keep = x[j];
            x[j].re += h;
            q->evals++;
            for (int i = 0; i < n; i++)
            {
                complex Fh = csub(eval_c_multi(q->lt[i], q->ls[i], x, n, q->vi),
                                  eval_c_multi(q->rt[i], q->rs[i], x, n, q->vi));
                J[i][j] = cdiv(csub(Fh, F[i]), (complex){h, 0});
            }
            x[j] = keep;
        }
        /* Lignes ramenees a la meme echelle pour le test du pivot. */
        for (int i = 0; i < n; i++)
        {
            double mx = 0;
            var[i] = 0;
            for (int j = 0; j < n; j++)
            {
                mx = std::fmax(mx, cmod(J[i][j]).re);
                var[i] += cmod(J[i][j]).re * (1.0 + cmod(x[j]).re);
            }
            complex inv = {mx > 0 && std::isfinite(mx) ? 1.0 / mx : 1.0, 0};
            for (int j = 0; j < n; j++)
                J[i][j] = cmul(J[i][j], inv);
            J[i][n] = cmul(csub((complex){0, 0}, F[i]), inv);
        }
        bool cons;
        if (gauss_c(J, n, n, dx, &cons) < n || !cons)
            break; /* jacobien singulier : un autre depart fera mieux */
        double t = 1.0, fn = INFINITY;
        bool better = false;
        for (int k = 0; k < 10; k++, t *= 0.5)
        {
            for (int j = 0; j < n; j++)
                xn[j] = cadd(x[j], cmul((complex){t, 0}, dx[j]));
            fn = sys_residual(q, xn, nullptr);
            if (fn < f)
            {
                better = true;
                break;
            }
        }
        if (!better)
            break; /* plancher d'arrondi, ou minimum local sans solution */
        double step = 0;
        for (int j = 0; j < n; j++)
            step = std::fmax(step, t * cmod(dx[j]).re / (1.0 + cmod(x[j]).re));
        stall = fn > 0.999 * f ? stall + 1 : 0;
        for (int j = 0; j < n; j++)
            x[j] = xn[j];
        f = sys_residual(q, x, F, mag);
        last_step = step;
        if (step < 1e-14 || stall >= 8)
            break;
    }
    bool ok = std::isfinite(f) && last_step <= 1e-5;
    for (int i = 0; i < n && ok; i++)
        if (!(cmod(F[i]).re <= SYS_TOL * (mag[i] + var[i])))
            ok = false;
    free(F);
    free(xn);
    free(dx);
    free(mag);
    free(var);
    free_c_Mat(J, n);
    return ok;
}

/* Nettoie une solution de Newton : partie imaginaire de bruit (7e-53 i)
 * retiree, puis valeur ronde reprise quand elle verifie le systeme au moins
 * aussi bien. Une racine double n'est approchee qu'a 10^-8 pres
 * (0.9999997616 au lieu de 1, 4e-8 au lieu de 0) : l'arrondi a 3, 6 ou 9
 * decimales qui annule le residu est la vraie valeur. Une solution
 * irrationnelle n'est pas touchee, son arrondi verifie moins bien. */
static void sys_polish(sys_eqs *q, complex *x)
{
    int n = q->nv;
    complex *y = (complex *)malloc(n * sizeof(complex));
    double f = sys_residual(q, x, nullptr);
    for (int j = 0; j < n; j++)
    {
        y[j] = x[j];
        if (std::fabs(y[j].im) <= 1e-7 * (1.0 + std::fabs(y[j].re)))
            y[j].im = 0;
    }
    double fy = sys_residual(q, y, nullptr);
    if (fy <= 2 * f + 1e-14)
    {
        for (int j = 0; j < n; j++)
            x[j] = y[j];
        f = fy;
    }
    static const double places[3] = {1e3, 1e6, 1e9};
    for (int d = 0; d < 3; d++)
    {
        bool fits = true;
        for (int j = 0; j < n; j++)
        {
            if (std::fabs(x[j].re) > 1e6 || std::fabs(x[j].im) > 1e6)
                fits = false;
            y[j] = (complex){std::round(x[j].re * places[d]) / places[d],
                             std::round(x[j].im * places[d]) / places[d]};
        }
        if (!fits)
            break;
        fy = sys_residual(q, y, nullptr);
        if (fy <= f || fy <= 1e-15)
        {
            for (int j = 0; j < n; j++) /* + 0.0 : pas de "-0" */
                x[j] = (complex){y[j].re + 0.0, y[j].im + 0.0};
            break;
        }
    }
    free(y);
}

/* Point de depart numero k de la recherche (n inconnues). Reels d'abord :
 * valeurs usuelles, decalees d'une inconnue a l'autre (un depart A = B rend
 * singulier le jacobien d'un systeme symetrique), signes alternes, puis
 * tirage pseudo-aleatoire toujours identique. Complexes ensuite, pour les
 * systemes sans solution reelle. Faux quand la liste est finie. */
#define SYS_REAL_STARTS 34
#define SYS_ALL_STARTS 44
static bool sys_start(int k, int n, complex *x, unsigned *rng)
{
    static const double base[8] = {1, -1, 0.5, 2, -2, 3, -3, 10};
    static const double alt[6] = {1, -1, 3, -3, 0.3, 7};
    static const complex cstart[10] = {{0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {2, 3}, {-3, 2}, {0.5, -2}, {5, 5}};
    if (k >= SYS_ALL_STARTS)
        return false;
    for (int v = 0; v < n; v++)
    {
        if (k < 8)
            x[v] = (complex){base[k] * (1.0 + 0.31 * v), 0};
        else if (k < 14)
            x[v] = (complex){alt[k - 8] * ((v % 2) ? -1.0 : 1.0) * (1.0 + 0.17 * v), 0};
        else if (k < SYS_REAL_STARTS)
        {
            *rng = *rng * 1664525u + 1013904223u;
            double u = (double)(*rng >> 8) / 16777216.0; /* [0, 1) */
            double span = k < 28 ? 6.0 : 40.0;
            x[v] = (complex){(2.0 * u - 1.0) * span, 0};
        }
        else
        {
            complex c = cstart[k - SYS_REAL_STARTS];
            x[v] = (complex){c.re * (1.0 + 0.23 * v), (v % 2) ? -c.im : c.im};
        }
    }
    return true;
}

/* Solutions distinctes d'un systeme carre non lineaire : methode de Newton
 * depuis plusieurs departs. Les solutions reelles d'abord ; les complexes
 * seulement s'il n'y en a aucune reelle. Rend leur nombre (au plus
 * SYS_MAX_SOL) ; *out : count * nv valeurs. A^2+B^2=25 ; A-B=1 ne donnait
 * que (4 ; 3). */
static int solve_nonlinear_all(sys_eqs *q, complex **out)
{
    int n = q->nv;
    complex *sols = (complex *)malloc(sizeof(complex) * n * SYS_MAX_SOL);
    complex *x = (complex *)malloc(sizeof(complex) * n);
    int count = 0, since_new = 0;
    unsigned rng = 12345u;
    for (int k = 0; count < SYS_MAX_SOL && sys_start(k, n, x, &rng) && q->evals < SYS_EVAL_BUDGET; k++)
    {
        if (k == SYS_REAL_STARTS && count > 0)
            break; /* des solutions reelles existent : pas de complexes */
        if (count > 0 && since_new >= 14)
            break; /* les departs retombent sur les memes solutions */
        since_new++;
        if (!newton_c(q, x))
            continue;
        sys_polish(q, x);
        bool real = true;
        for (int j = 0; j < n; j++)
            if (x[j].im != 0)
                real = false;
        if (k < SYS_REAL_STARTS && !real)
            continue; /* un depart reel parti dans le plan complexe */
        bool known = false;
        for (int s = 0; s < count && !known; s++)
        {
            known = true;
            for (int j = 0; j < n; j++)
                if (cmod(csub(sols[s * n + j], x[j])).re > 1e-6 * (1.0 + cmod(x[j]).re))
                    known = false;
        }
        if (known)
            continue;
        for (int j = 0; j < n; j++)
            sols[count * n + j] = x[j];
        count++;
        since_new = 0;
    }
    free(x);
    /* Ordre croissant de la premiere inconnue, puis des suivantes. */
    for (int a = 1; a < count; a++)
        for (int b = a; b > 0; b--)
        {
            complex *u = &sols[(b - 1) * n], *w = &sols[b * n];
            int cmp = 0;
            for (int j = 0; j < n && cmp == 0; j++)
            {
                if (u[j].re != w[j].re)
                    cmp = u[j].re < w[j].re ? -1 : 1;
                else if (u[j].im != w[j].im)
                    cmp = u[j].im < w[j].im ? -1 : 1;
            }
            if (cmp <= 0)
                break;
            for (int j = 0; j < n; j++)
            {
                complex t = u[j];
                u[j] = w[j];
                w[j] = t;
            }
        }
    *out = sols;
    return count;
}

/* Vrai si chaque equation est affine en ses inconnues. mat contient les
 * coefficients extraits (mat[i][v]) et l'oppose de la constante
 * (mat[i][nb_var]) : la valeur qu'ils prevoient doit etre celle de
 * l'equation en deux points. Un simple residu absolu acceptait la fausse
 * solution lineaire de A^2 = 0.00001 (A = 0.00001, "Exact"). */
static bool system_is_linear(token **left_tok, int *left_sz,
                             token **right_tok, int *right_sz,
                             complex **mat, int nb_eq, int nb_var, int *var_indices)
{
    complex *x = (complex *)malloc(nb_var * sizeof(complex));
    bool linear = true;
    for (int pt = 0; pt < 2 && linear; pt++)
    {
        // Un point positif et un negatif (abs(A) est affine d'un seul cote).
        for (int v = 0; v < nb_var; v++)
            x[v] = pt == 0 ? (complex){1.7 + 0.37 * v, 0} : (complex){-0.9 - 0.53 * v, 0};
        for (int i = 0; i < nb_eq && linear; i++)
        {
            complex l = eval_c_multi(left_tok[i], left_sz[i], x, nb_var, var_indices);
            complex r = eval_c_multi(right_tok[i], right_sz[i], x, nb_var, var_indices);
            complex f = csub(l, r);
            complex pred = csub((complex){0, 0}, mat[i][nb_var]);
            /* L'echelle compte les deux membres : dans 3(A+1)-A = 2A+3 ils
             * sont egaux a un arrondi pres (9e-16), qui n'est pas un ecart. */
            double scale = cmod(pred).re + cmod(l).re + cmod(r).re;
            for (int v = 0; v < nb_var; v++)
            {
                complex t = cmul(mat[i][v], x[v]);
                pred = cadd(pred, t);
                scale += cmod(t).re;
            }
            // Ecrit a l'envers : un NaN (ln(A) en 0) n'est pas lineaire.
            if (!(cmod(csub(f, pred)).re <= 1e-9 * scale))
                linear = false;
        }
    }
    free(x);
    return linear;
}

/* Reponse de solve_system. */
enum
{
    SYS_OK,        /* count solutions dans sol */
    SYS_NONE,      /* aucune solution (systeme lineaire : prouve) */
    SYS_INFINITE,  /* infinite de solutions (systeme lineaire) */
    SYS_NOT_FOUND, /* recherche numerique sans resultat */
    SYS_UNDER,     /* non lineaire, plus d'inconnues que d'equations */
    SYS_OVER,      /* non lineaire, plus d'equations que d'inconnues */
};

typedef struct
{
    int status;
    bool exact;   /* solution unique d'un systeme lineaire */
    int count;    /* nombre de solutions */
    complex *sol; /* count * NB_VAR valeurs, a liberer ; NULL sans solution */
} sys_result;

/* Resout le systeme (NB_EQ equations gauche = droite, NB_VAR inconnues).
 * Lineaire, quel que soit le nombre d'equations : solution unique exacte,
 * aucune solution ou infinite, d'apres le rang. Sinon, et s'il est carre :
 * solutions approchees (Newton). */
static sys_result solve_system(token **left_tok, int *left_sz,
                               token **right_tok, int *right_sz,
                               int NB_EQ, int NB_VAR, int *var_index)
{
    sys_result res = {SYS_NOT_FOUND, false, 0, nullptr};
    if (NB_EQ <= 0 || NB_VAR <= 0)
        return res;
    complex **mat = init_c_Mat(NB_VAR + 1, NB_EQ, (complex){0, 0});
    /* Ordre de grandeur de chaque equation avant toute soustraction : les
     * lignes sont ramenees a cette echelle, et un coefficient qui n'est que
     * du bruit (0.1A+0.2A-0.3A) reste negligeable. */
    double *mag = (double *)malloc(sizeof(double) * NB_EQ);
    for (int i = 0; i < NB_EQ; i++)
    {
        double m = 0;
        for (int v = 0; v < NB_VAR; v++)
        {
            int letter = var_index[v];
            complex lc = csub(evaluate_npi_c_var(left_tok[i], left_sz[i], (complex){1, 0}, 'A' + letter),
                              evaluate_npi_c_var(left_tok[i], left_sz[i], (complex){0, 0}, 'A' + letter));
            complex rc = csub(evaluate_npi_c_var(right_tok[i], right_sz[i], (complex){1, 0}, 'A' + letter),
                              evaluate_npi_c_var(right_tok[i], right_sz[i], (complex){0, 0}, 'A' + letter));
            mat[i][v] = csub(lc, rc);
            m += cmod(lc).re + cmod(rc).re;
        }
        complex lk = evaluate_npi_c_var(left_tok[i], left_sz[i], (complex){0, 0}, 'A' + var_index[0]);
        complex rk = evaluate_npi_c_var(right_tok[i], right_sz[i], (complex){0, 0}, 'A' + var_index[0]);
        mat[i][NB_VAR] = csub(rk, lk);
        mag[i] = m + cmod(lk).re + cmod(rk).re;
    }
    bool linear = system_is_linear(left_tok, left_sz, right_tok, right_sz,
                                   mat, NB_EQ, NB_VAR, var_index);
    if (linear)
    {
        for (int i = 0; i < NB_EQ; i++)
        {
            if (!(mag[i] > 0) || !std::isfinite(mag[i]))
                continue;
            complex inv = {1.0 / mag[i], 0};
            for (int j = 0; j <= NB_VAR; j++)
                mat[i][j] = cmul(mat[i][j], inv);
        }
        complex *sol = (complex *)malloc(NB_VAR * sizeof(complex));
        bool consistent = true;
        int rank = gauss_c(mat, NB_EQ, NB_VAR, sol, &consistent);
        if (!consistent)
            res.status = SYS_NONE;
        else if (rank < NB_VAR)
            res.status = SYS_INFINITE;
        else
        {
            /* Gardee seulement si elle verifie les equations. */
            bool ok = true;
            for (int i = 0; i < NB_EQ; i++)
            {
                complex lv = eval_c_multi(left_tok[i], left_sz[i], sol, NB_VAR, var_index);
                complex rv = eval_c_multi(right_tok[i], right_sz[i], sol, NB_VAR, var_index);
                double r = cmod(csub(lv, rv)).re;
                if (!(r <= 1e-9 * (1.0 + cmod(lv).re + cmod(rv).re)))
                    ok = false;
            }
            if (ok)
            {
                res.status = SYS_OK;
                res.exact = true;
                res.count = 1;
                res.sol = sol;
                sol = nullptr;
            }
            else
                linear = false;
        }
        free(sol);
    }
    free_c_Mat(mat, NB_EQ);
    free(mag);
    if (linear)
        return res;

    if (NB_EQ != NB_VAR)
    {
        res.status = NB_EQ < NB_VAR ? SYS_UNDER : SYS_OVER;
        return res;
    }
    sys_eqs q = {left_tok, right_tok, left_sz, right_sz, NB_EQ, NB_VAR, var_index, 0};
    complex *sols = nullptr;
    int count = solve_nonlinear_all(&q, &sols);
    if (count > 0)
    {
        res.status = SYS_OK;
        res.count = count;
        res.sol = sols;
    }
    else
        free(sols);
    return res;
}

#ifndef SOLVER_HOST_TEST /* tests/test_solver.cpp : calcul seul, sans ecran */
/* Les equations survivent a la sortie de l'appli (BACK ou HOME). L'ecran
 * des solutions (30 cases, ~30 Ko) est recree a chaque ouverture : celui
 * d'une sortie par HOME (longjmp, sans liberation) est rendu a l'ouverture
 * suivante au lieu de fuir a chaque fois. */
static fill_box **s_eqs = nullptr;
static fill_box **s_sol = nullptr;
static text_box *s_tb_sol = nullptr, *s_tb_eq = nullptr, *s_tb_acc = nullptr;

/* Hauteur qu'une ligne prend en plus de ses 40 pixels (fraction, puissance,
 * racine...) : ce qui depasse d'une ligne de texte, pour chaque morceau. */
static int box_extra_height(fill_box *b)
{
    int extra = 0;
    int tok_n = 0;
    display_set_source(b->text, b->t_size);
    token *toks = parse_string_to_token(b->text, b->t_size, &tok_n);
    Parser p = {toks, tok_n, 0};
    while (p.pos < p.n)
    {
        int before = p.pos;
        ASTNode *node = parse_equation(&p);
        if (p.pos == before)
            p.pos++;
        if (node)
            extra += max(0, measure(node, 2).h - 16);
    }
    token_free_matrices(toks, tok_n);
    free(toks);
    return extra;
}

static void solver_release_screen(void)
{
    if (s_sol != nullptr)
    {
        for (int i = 0; i < 30; i++)
            free_fill_box(s_sol[i]);
        free(s_sol);
        s_sol = nullptr;
    }
    if (s_tb_sol)
        free_text_box(s_tb_sol);
    if (s_tb_eq)
        free_text_box(s_tb_eq);
    if (s_tb_acc)
        free_text_box(s_tb_acc);
    s_tb_sol = s_tb_eq = s_tb_acc = nullptr;
}

int Solver()
{
    stdio_init_all();
    bool snd = false;

    solver_release_screen();
    if (s_eqs == nullptr)
    {
        fill_box **e = (fill_box **)calloc(30, sizeof(fill_box *));
        if (e == nullptr)
            return 0;
        e[0] = create_fill_box(0, 20, 40, 320, 3);
        if (e[0] == nullptr)
        {
            free(e);
            return 0;
        }
        s_eqs = e;
    }

    text_box *Solution = s_tb_sol = create_text_box(160, 220, 20, 160, 1, false);
    text_box *Equations = s_tb_eq = create_text_box(0, 220, 20, 160, 1, false);
    Solution->text = (char *)"Solutions";
    Solution->t_size = 9;
    Equations->text = (char *)"Équations";
    Equations->t_size = (int)strlen("Équations");

    fill_box **arr_solution = s_sol = (fill_box **)malloc(sizeof(fill_box *) * 30);
    fill_box **arr_fill_box = s_eqs;
    for (int i = 0; i < 30; i++)
        arr_solution[i] = create_fill_box(0, 20, 40, 320, 3);

    int selected_fill_box = 0;
    int first_display = 0;
    bool show_solution = false;
    int nb_solutions = 0; /* lignes remplies de l'ecran des solutions */
    int first_sol = 0;    /* premiere ligne de solution affichee */

    fill_screen(BACKGROUND_COLOR);
    /* Toute la largeur : "Approche, solutions d'une periode" doit tenir. */
    text_box *accuracy_box = s_tb_acc = create_text_box(0, 0, 20, 320, 1, false);
    acc_set(accuracy_box, "");
    while (1)
    {
        int mv = 0;
        if (!show_solution)
        {
            for (int i = std::max(first_display, 0); i < std::min(first_display + 6, 30); i++)
            {
                if (arr_fill_box[i] == nullptr)
                    continue;
                int old_mv = mv;
                mv += box_extra_height(arr_fill_box[i]);
                arr_fill_box[i]->h = mv - old_mv + 40;
                //                display_fill_box(arr_fill_box[i], 160 - (i - selected_fill_box + 4) * 40 + old_mv, (i - selected_fill_box) % HISTORY_SIZE == 0, -1, ' ');
                display_fill_box(arr_fill_box[i],
                                 160 - (i - std::max(first_display, 0)) * 40 - mv,
                                 i == selected_fill_box, -1, ' ',false);
            }
        }
        else
        {
            /* Les lignes vont des onglets (y = 220) a l'etiquette (y = 20) :
             * cinq lignes simples, moins si une solution est une fraction
             * (ligne plus haute, comme pour les equations ; avant, X=7/2
             * debordait sous les onglets). La ligne choisie reste entiere a
             * l'ecran : avant, pas de defilement, les solutions au-dela de
             * la 5e restaient invisibles (12 racines de sin(X)cos(X)=0). */
            static int sol_h[30]; /* hors pile */
            for (int i = 0; i < 30; i++)
                sol_h[i] = 40 + (i < nb_solutions ? box_extra_height(arr_solution[i]) : 0);
            if (selected_fill_box < 0)
                first_sol = 0;
            else if (selected_fill_box < first_sol)
                first_sol = selected_fill_box;
            while (first_sol < selected_fill_box)
            {
                int used = 0;
                for (int i = first_sol; i <= selected_fill_box; i++)
                    used += sol_h[i];
                if (used <= 200)
                    break;
                first_sol++;
            }
            int used = 0;
            for (int i = first_sol; i < 30 && used < 200; i++)
            {
                used += sol_h[i];
                arr_solution[i]->h = sol_h[i];
                display_fill_box(arr_solution[i], 200 - used,
                                 i == selected_fill_box, -1, ' ',false);
            }
            display_text_box(accuracy_box, 0, 0, false);
        }

        display_text_box(Equations, 0, 0, !show_solution && selected_fill_box == -1);
        display_text_box(Solution, 0, 0, show_solution && selected_fill_box == -1);

        int last_pressed = scan_keypad();
        while (last_pressed == -1)
        {
            last_pressed = scan_keypad();
            /* Le curseur ne clignote que dans une equation en cours de
             * saisie : ailleurs (onglets, solutions) il laissait une barre a
             * son ancienne place, par-dessus les solutions. */
            if (!show_solution && selected_fill_box >= 0)
                blink_cursor();
            else
                flush_display();
        }

        switch (last_pressed)
        {

        case BACK:
            if (!show_solution && selected_fill_box >= 0 &&
                arr_fill_box[selected_fill_box]->t_size > 0)
            {
                update_fill_box(arr_fill_box[selected_fill_box], last_pressed, snd);
            }
            else
            {
                /* Les equations restent ; l'ecran des solutions est rendu. */
                solver_release_screen();
                return 0;
            }
            break;

        case UP:
            if (show_solution)
            {
                selected_fill_box = std::max(-1, selected_fill_box - 1);
                /* ligne vide entre deux solutions d'un systeme : on la passe */
                if (selected_fill_box > 0 && arr_solution[selected_fill_box]->t_size == 0)
                    selected_fill_box--;
            }
            else
            {
                selected_fill_box = std::max(-1, selected_fill_box - 1);
                if (selected_fill_box < first_display)
                    first_display = selected_fill_box;
            }
            break;

        case DOWN:
            if (!show_solution)
            {
                selected_fill_box = std::min(29, selected_fill_box + 1);
                if (arr_fill_box[selected_fill_box] == nullptr)
                    arr_fill_box[selected_fill_box] = create_fill_box(0, 20, 40, 320, 3);
                if (first_display + 4 < selected_fill_box)
                    first_display++;
            }
            else
            {
                selected_fill_box = std::min(nb_solutions - 1, selected_fill_box + 1);
                if (selected_fill_box >= 0 && selected_fill_box + 1 < nb_solutions &&
                    arr_solution[selected_fill_box]->t_size == 0)
                    selected_fill_box++;
            }
            break;

        case RIGHT:
            if (selected_fill_box == -1)
            {
                show_solution = true;
                fill_screen(BACKGROUND_COLOR);

                int NB_EQ = 0;
                for (int i = 0; i < 30; i++)
                {
                    if (arr_fill_box[i] != nullptr && arr_fill_box[i]->t_size > 0)
                        NB_EQ++;
                    else
                        break;
                }

                bool var_present[26] = {false};
                for (int i = 0; i < NB_EQ; i++)
                    for (int c = 0; c < arr_fill_box[i]->t_size; c++)
                    {
                        char ch = arr_fill_box[i]->text[c];
                        if (ch >= 'A' && ch <= 'Z')
                            var_present[ch - 'A'] = true;
                    }
                int var_index[26];
                int NB_VAR = 0;
                for (int v = 0; v < 26; v++)
                    if (var_present[v])
                        var_index[NB_VAR++] = v;

                /* Les solutions du calcul precedent ne doivent pas rester. */
                nb_solutions = 0;
                first_sol = 0;
                for (int i = 0; i < 30; i++)
                {
                    arr_solution[i]->text[0] = '\0';
                    arr_solution[i]->t_size = 0;
                }
                acc_set(accuracy_box, "");

                if (NB_EQ == 0)
                    break;
                if (NB_VAR == 0)
                {
                    acc_set(accuracy_box, "Aucune inconnue");
                    break;
                }
                bool has_rel = false;
                for (int i = 0; i < NB_EQ; i++)
                    for (int c = 0; c < arr_fill_box[i]->t_size; c++)
                        if (is_rel_type(arr_fill_box[i]->text[c]))
                            has_rel = true;
                if (has_rel && NB_VAR != 1)
                {
                    acc_set(accuracy_box, "Inequation : une seule inconnue");
                    break;
                }

                /* Une seule inconnue : le calcul formel donne toutes les
                 * solutions (la methode de Newton n'en trouvait qu'une :
                 * X^2-2=0 rendait 1.414 seul), resout les inequations, et
                 * plusieurs lignes sont des conditions a verifier ensemble. */
                if (NB_VAR == 1)
                {
                    int n = solve_cas_lines(arr_fill_box, NB_EQ, 'A' + var_index[0], arr_solution, 30,
                                            accuracy_box, has_rel);
                    if (n >= 0)
                    {
                        nb_solutions = n;
                        break;
                    }
                }

                token **left_tok = (token **)malloc(NB_EQ * sizeof(token *));
                token **right_tok = (token **)malloc(NB_EQ * sizeof(token *));
                int *left_sz = (int *)malloc(NB_EQ * sizeof(int));
                int *right_sz = (int *)malloc(NB_EQ * sizeof(int));

                for (int i = 0; i < NB_EQ; i++)
                {
                    int k = 0;
                    while (k < arr_fill_box[i]->t_size && arr_fill_box[i]->text[k] != '=')
                        k++;

                    int ts = 0;
                    token *tl = parse_string_to_token(arr_fill_box[i]->text, k, &ts);
                    left_tok[i] = shunting_yard(tl, ts);
                    left_sz[i] = count_yarded(tl, ts);
                    free(tl);

                    /* Sans '=', l'equation est f = 0 (avant : longueur -1,
                     * memset sur NULL et plantage). */
                    int ts2 = 0;
                    static char zero_txt[] = "0";
                    token *tr = k < arr_fill_box[i]->t_size
                                    ? parse_string_to_token(&arr_fill_box[i]->text[k + 1],
                                                            arr_fill_box[i]->t_size - k - 1, &ts2)
                                    : parse_string_to_token(zero_txt, 1, &ts2);
                    right_tok[i] = shunting_yard(tr, ts2);
                    right_sz[i] = count_yarded(tr, ts2);
                    free(tr);
                }

                sys_result sr = solve_system(left_tok, left_sz, right_tok, right_sz,
                                             NB_EQ, NB_VAR, var_index);
                switch (sr.status)
                {
                case SYS_OK:
                {
                    /* Une inconnue par ligne ; une ligne vide entre deux
                     * solutions d'un systeme non lineaire. */
                    int per = NB_VAR + 1;
                    int shown = std::min(sr.count, 31 / per);
                    for (int k = 0; k < shown; k++)
                        for (int v = 0; v < NB_VAR; v++)
                        {
                            fill_box *line = arr_solution[k * per + v];
                            line->text[0] = 'A' + var_index[v];
                            line->text[1] = '=';
                            int written = complex_to_string(sr.sol[k * NB_VAR + v], line->text + 2);
                            line->t_size = written + 2;
                            line->curso_pos = 0;
                        }
                    nb_solutions = shown * per - 1;
                    if (sr.exact)
                        acc_set(accuracy_box, "Exact");
                    else if (shown > 1)
                    {
                        char nb[24];
                        snprintf(nb, sizeof(nb), "%d solutions", shown);
                        acc_set(accuracy_box, "Approche", nb);
                    }
                    else
                        acc_set(accuracy_box, "Approche");
                    break;
                }
                case SYS_NONE:
                    acc_set(accuracy_box, "Aucune solution");
                    break;
                case SYS_INFINITE:
                    acc_set(accuracy_box, "Infinite de solutions");
                    break;
                case SYS_UNDER:
                    acc_set(accuracy_box, "Trop d'inconnues");
                    break;
                case SYS_OVER:
                    acc_set(accuracy_box, "Trop d'equations");
                    break;
                default:
                    acc_set(accuracy_box, "Aucune solution trouvee");
                    break;
                }

                free(sr.sol);
                for (int i = 0; i < NB_EQ; i++)
                {
                    free(left_tok[i]);
                    free(right_tok[i]);
                }
                free(left_tok);
                free(right_tok);
                free(left_sz);
                free(right_sz);
            }
            else if (!show_solution)
            {
                fill_box_right(arr_fill_box[selected_fill_box]);
            }
            break;
        case LEFT:
            if (show_solution)
            {
                /* Retour aux equations : le curseur des solutions n'est pas
                 * un indice d'equation. Les lignes de solutions et leur
                 * etiquette ne doivent pas rester sous les equations. */
                show_solution = false;
                selected_fill_box = -1;
                fill_screen(BACKGROUND_COLOR);
            }
            else if (selected_fill_box != -1)
            {
                fill_box_left(arr_fill_box[selected_fill_box]);
            }
            break;

        case SECOND:
            toggle(&snd);
            break;

        case TOOLS:
            if (!show_solution && selected_fill_box >= 0)
            {
                update_fill_box(arr_fill_box[selected_fill_box], menu_tools(false, true), snd);
                fill_screen(BACKGROUND_COLOR); /* efface le menu ferme */
            }
            break;

        default:
            if (!show_solution && selected_fill_box >= 0)
                update_fill_box(arr_fill_box[selected_fill_box], last_pressed, snd);
            break;
        }
        sleep_ms(50);
    }
}
#endif
