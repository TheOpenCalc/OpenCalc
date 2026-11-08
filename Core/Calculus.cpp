/* Somme, produit, derivee en un point et integrale pour la Calculatrice
 * (voir headers/Calculus.h).
 *
 * L'evaluateur RPN ne sait pas evaluer une expression plusieurs fois avec des
 * valeurs differentes de X : ces appels sont donc traites ici, sur le texte.
 * Chaque appel est calcule (recursivement pour les appels imbriques), puis
 * remplace dans le texte par son resultat ecrit comme un nombre, et le texte
 * obtenu est evalue normalement.
 *
 * La pile du Pico est petite (2 Ko par defaut) : les tableaux sont alloues
 * sur le tas. */
#include "Calculus.h"
#include "Evaluator.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Profondeur maximale d'imbrication des appels. */
#define CALC_MAX_DEPTH 4

enum
{
    K_SUM,
    K_PROD,
    K_DERIV,
    K_INTEG
};

static const struct
{
    const char *name;
    int len;
    int kind;
    int nargs;
} k_calls[] = {
    {"somme", 5, K_SUM, 3},
    {"produit", 7, K_PROD, 3},
    {"derivee", 7, K_DERIV, 2},
    {"integrale", 9, K_INTEG, 3},
};
#define NB_CALLS ((int)(sizeof(k_calls) / sizeof(k_calls[0])))

static const complex C_NAN = {NAN, NAN};

static bool c_finite(complex z) { return isfinite(z.re) && isfinite(z.im); }
static double c_abs(complex z) { return hypot(z.re, z.im); }

/* Index de l'appel qui commence en text[i] (nom suivi de '('), ou -1. */
static int call_at(const char *text, int n, int i)
{
    if (i > 0 && text[i - 1] >= 'a' && text[i - 1] <= 'z')
        return -1;
    for (int k = 0; k < NB_CALLS; k++)
    {
        int len = k_calls[k].len;
        if (i + len < n && text[i + len] == '(' && strncmp(text + i, k_calls[k].name, len) == 0)
            return k;
    }
    return -1;
}

bool calculus_has_call(const char *text, int n)
{
    for (int i = 0; i < n; i++)
        if (call_at(text, n, i) >= 0)
            return true;
    return false;
}

bool calculus_is_approx(const char *text, int n)
{
    for (int i = 0; i < n; i++)
    {
        int k = call_at(text, n, i);
        if (k >= 0 && (k_calls[k].kind == K_DERIV || k_calls[k].kind == K_INTEG))
            return true;
    }
    return false;
}

/* ---------- Evaluation d'un texte ---------- */

static complex eval_text(const char *text, int n, complex x, int letter, int depth);

/* Expression f(X) evaluee de nombreuses fois. Sans appel imbrique, elle est
 * convertie une seule fois en RPN ; sinon le texte est reevalue a chaque fois. */
typedef struct
{
    const char *src;
    int n;
    int depth;
    bool nested;
    token *toks;
    int tok_n;
    token *rpn;
    int yarded;
} expr_fn;

static bool fn_init(expr_fn *f, const char *src, int n, int depth)
{
    memset(f, 0, sizeof(*f));
    f->src = src;
    f->n = n;
    f->depth = depth;
    f->nested = calculus_has_call(src, n);
    if (f->nested)
        return true;
    char *buf = (char *)malloc(n + 1);
    if (!buf)
        return false;
    memcpy(buf, src, n);
    buf[n] = '\0';
    f->toks = parse_string_to_token(buf, n, &f->tok_n);
    free(buf);
    if (!f->toks)
        return false;
    f->rpn = shunting_yard(f->toks, f->tok_n);
    f->yarded = count_yarded(f->toks, f->tok_n);
    return f->rpn != NULL;
}

static void fn_free(expr_fn *f)
{
    if (f->toks)
    {
        token_free_matrices(f->toks, f->tok_n);
        free(f->toks);
    }
    free(f->rpn);
    f->toks = NULL;
    f->rpn = NULL;
}

static complex fn_eval(expr_fn *f, complex t)
{
    if (f->nested)
        return eval_text(f->src, f->n, t, 'X', f->depth + 1);
    return evaluate_npi_c_var(f->rpn, f->yarded, t, 'X');
}

static complex fn_eval_r(expr_fn *f, double t)
{
    complex z = {t, 0};
    return fn_eval(f, z);
}

/* Borne reelle : un argument non vide dont la valeur est reelle et finie. */
static bool eval_bound(const char *src, int n, complex x, int letter, int depth, double *out)
{
    bool empty = true;
    for (int i = 0; i < n; i++)
        if (src[i] != ' ')
            empty = false;
    if (empty)
        return false;
    complex v = eval_text(src, n, x, letter, depth + 1);
    if (!c_finite(v) || fabs(v.im) > 1e-12 * fmax(1.0, fabs(v.re)))
        return false;
    *out = v.re;
    return true;
}

static bool to_integer(double v, long long *out)
{
    double r = floor(v + 0.5);
    if (fabs(v - r) > 1e-9 || fabs(r) > 1e15)
        return false;
    *out = (long long)r;
    return true;
}

/* ---------- Somme et produit ---------- */

static complex sum_prod(expr_fn *f, double a, double b, bool product)
{
    long long ia, ib;
    if (!to_integer(a, &ia) || !to_integer(b, &ib))
        return C_NAN;
    complex acc = {product ? 1.0 : 0.0, 0.0};
    if (ib < ia) /* somme vide = 0, produit vide = 1 */
        return acc;
    if (ib - ia + 1 > CALCULUS_MAX_TERMS)
        return C_NAN;
    for (long long k = ia; k <= ib; k++)
    {
        complex v = fn_eval_r(f, (double)k);
        acc = product ? cmul(acc, v) : cadd(acc, v);
        if (!c_finite(acc))
            return acc.re != acc.re ? C_NAN : acc;
    }
    return acc;
}

/* ---------- Derivee : methode de Ridders (Numerical Recipes, dfridr) ----------
 * Differences centrees avec un pas qui diminue, puis extrapolation de
 * Richardson. L'erreur obtenue est de l'ordre de 1e-12 pour une fonction
 * reguliere. */

#define RID_NTAB 10

static complex deriv_ridders(expr_fn *f, double a)
{
    const double CON = 1.4, CON2 = CON * CON, SAFE = 2.0;
    double h = 0.1 * fmax(1.0, fabs(a));
    /* Pres de 0, un grand pas sortirait du domaine de ln(X), sqrt(X)... */
    if (a != 0.0 && h > 0.5 * fabs(a))
        h = fmax(0.5 * fabs(a), 1e-3);
    complex *tab = (complex *)malloc(sizeof(complex) * RID_NTAB * RID_NTAB);
    if (!tab)
        return C_NAN;
#define T(i, j) tab[(i) * RID_NTAB + (j)]
    complex ans = C_NAN;
    double err = INFINITY;
    complex d = csub(fn_eval_r(f, a + h), fn_eval_r(f, a - h));
    T(0, 0) = (complex){d.re / (2 * h), d.im / (2 * h)};
    for (int i = 1; i < RID_NTAB; i++)
    {
        h /= CON;
        d = csub(fn_eval_r(f, a + h), fn_eval_r(f, a - h));
        T(0, i) = (complex){d.re / (2 * h), d.im / (2 * h)};
        double fac = CON2;
        for (int j = 1; j <= i; j++)
        {
            complex p = T(j - 1, i), q = T(j - 1, i - 1);
            T(j, i) = (complex){(p.re * fac - q.re) / (fac - 1), (p.im * fac - q.im) / (fac - 1)};
            fac *= CON2;
            double e1 = c_abs(csub(T(j, i), p));
            double e2 = c_abs(csub(T(j, i), q));
            double errt = e1 > e2 ? e1 : e2;
            if (errt <= err)
            {
                err = errt;
                ans = T(j, i);
            }
        }
        if (c_abs(csub(T(i, i), T(i - 1, i - 1))) >= SAFE * err)
            break;
    }
#undef T
    free(tab);
    if (!c_finite(ans) || !(err <= 1e-6 * fmax(1.0, c_abs(ans))))
        return C_NAN;
    return ans;
}

/* ---------- Integrale : Gauss-Kronrod 7-15 adaptative ---------- */

static const double XGK[8] = {0.991455371120812639206854697526329, 0.949107912342758524526189684047851,
                              0.864864423359769072789712788640926, 0.741531185599394439863864773280788,
                              0.586087235467691130294144845693013, 0.405845151377397166906606412076961,
                              0.207784955007898467600689403773245, 0.0};
static const double WGK[8] = {0.022935322010529224963732008058970, 0.063092092629978553290700663189204,
                              0.104790010322250183839876322541518, 0.140653259715525918745189590510238,
                              0.169004726639267902826583426598550, 0.190350578064785409913256402421014,
                              0.204432940075298892414161999234649, 0.209482141084727828012999174891714};
/* Poids de Gauss a 7 points, aux noeuds XGK[1], XGK[3], XGK[5], XGK[7]. */
static const double WG[4] = {0.129484966168869693270611432679082, 0.279705391489276667901467771423780,
                             0.381830050505118944950369775488975, 0.417959183673469387755102040816327};

typedef struct
{
    double a, b;
    complex val;
    double err;
} gk_seg;

static void gk15(expr_fn *f, gk_seg *s)
{
    double c = 0.5 * (s->a + s->b), h = 0.5 * (s->b - s->a);
    complex fc = fn_eval_r(f, c);
    complex k = {fc.re * WGK[7], fc.im * WGK[7]};
    complex g = {fc.re * WG[3], fc.im * WG[3]};
    for (int j = 0; j < 7; j++)
    {
        complex f1 = fn_eval_r(f, c - h * XGK[j]);
        complex f2 = fn_eval_r(f, c + h * XGK[j]);
        complex sum = cadd(f1, f2);
        k.re += WGK[j] * sum.re;
        k.im += WGK[j] * sum.im;
        if (j % 2 == 1)
        {
            g.re += WG[j / 2] * sum.re;
            g.im += WG[j / 2] * sum.im;
        }
    }
    s->val = (complex){k.re * h, k.im * h};
    complex diff = {(k.re - g.re) * h, (k.im - g.im) * h};
    s->err = c_abs(diff);
}

#define GK_MAX_SEG 100

static complex integrate(expr_fn *f, double a, double b)
{
    if (a == b)
        return (complex){0, 0};
    double sign = 1.0;
    if (a > b)
    {
        double t = a;
        a = b;
        b = t;
        sign = -1.0;
    }
    gk_seg *seg = (gk_seg *)malloc(sizeof(gk_seg) * GK_MAX_SEG);
    if (!seg)
        return C_NAN;
    int ns = 1;
    seg[0].a = a;
    seg[0].b = b;
    gk15(f, &seg[0]);
    complex total = C_NAN;
    double err = INFINITY;
    while (true)
    {
        total = (complex){0, 0};
        err = 0;
        int worst = 0;
        for (int i = 0; i < ns; i++)
        {
            total = cadd(total, seg[i].val);
            err += seg[i].err;
            if (seg[i].err > seg[worst].err)
                worst = i;
        }
        if (!c_finite(total))
            break;
        double tol = fmax(1e-13, 1e-11 * c_abs(total));
        if (err <= tol || ns >= GK_MAX_SEG)
            break;
        /* Coupe en deux le morceau le moins precis. */
        double m = 0.5 * (seg[worst].a + seg[worst].b);
        if (!(m > seg[worst].a && m < seg[worst].b))
            break; /* morceau trop petit pour etre coupe */
        seg[ns].a = m;
        seg[ns].b = seg[worst].b;
        seg[worst].b = m;
        gk15(f, &seg[worst]);
        gk15(f, &seg[ns]);
        ns++;
    }
    free(seg);
    if (!c_finite(total) || err > 1e-6 * fmax(1.0, c_abs(total)))
        return C_NAN; /* ne converge pas (fonction non integrable ?) */
    return (complex){sign * total.re, sign * total.im};
}

/* ---------- Un appel ---------- */

static complex eval_call(int kind, const char *const *arg, const int *alen, complex x, int letter, int depth)
{
    expr_fn f;
    bool empty = true;
    for (int i = 0; i < alen[0]; i++)
        if (arg[0][i] != ' ')
            empty = false;
    if (empty || !fn_init(&f, arg[0], alen[0], depth))
        return C_NAN;
    complex r = C_NAN;
    double a, b;
    switch (kind)
    {
    case K_SUM:
    case K_PROD:
        if (eval_bound(arg[1], alen[1], x, letter, depth, &a) &&
            eval_bound(arg[2], alen[2], x, letter, depth, &b))
            r = sum_prod(&f, a, b, kind == K_PROD);
        break;
    case K_DERIV:
        if (eval_bound(arg[1], alen[1], x, letter, depth, &a))
            r = deriv_ridders(&f, a);
        break;
    case K_INTEG:
        if (eval_bound(arg[1], alen[1], x, letter, depth, &a) &&
            eval_bound(arg[2], alen[2], x, letter, depth, &b))
            r = integrate(&f, a, b);
        break;
    }
    fn_free(&f);
    return r;
}

/* Ecrit v (>= 0) sous une forme que le tokenizer relit : 1.25 ou
 * 1.25*10^(-7). Le 'e' de printf serait lu comme la constante e. */
static int fmt_abs(char *out, double v)
{
    char m[40];
    snprintf(m, sizeof(m), "%.16e", fabs(v));
    char *e = strchr(m, 'e');
    int ex = 0;
    if (e)
    {
        ex = atoi(e + 1);
        *e = '\0';
    }
    if (ex == 0)
        return sprintf(out, "%s", m);
    return sprintf(out, "%s*10^(%d)", m, ex);
}

/* "(re+im*i)" entre parentheses pour garder les priorites ("2somme(...)",
 * "somme(...)^2"). */
static int fmt_literal(char *out, complex v)
{
    int len = 0;
    out[len++] = '(';
    if (v.re < 0)
        out[len++] = '-';
    len += fmt_abs(out + len, v.re);
    if (v.im != 0)
    {
        out[len++] = v.im < 0 ? '-' : '+';
        len += fmt_abs(out + len, v.im);
        len += sprintf(out + len, "*i");
    }
    out[len++] = ')';
    out[len] = '\0';
    return len;
}

#define LITERAL_MAX 96

static complex eval_text(const char *text, int n, complex x, int letter, int depth)
{
    if (depth > CALC_MAX_DEPTH)
        return C_NAN;
    /* Chaque appel fait au moins 9 caracteres ("derivee(,)") et devient un
     * nombre d'au plus LITERAL_MAX caracteres. */
    int cap = n + (n / 9 + 1) * LITERAL_MAX + 1;
    char *buf = (char *)malloc(cap);
    if (!buf)
        return C_NAN;
    int len = 0;
    int i = 0;
    while (i < n)
    {
        int k = call_at(text, n, i);
        if (k < 0)
        {
            buf[len++] = text[i++];
            continue;
        }
        /* Arguments : virgules au premier niveau de parentheses/crochets. */
        int open = i + k_calls[k].len;
        const char *arg[3];
        int alen[3];
        int na = 0, depth_p = 0, start = open + 1, j = open;
        bool bad = false;
        for (; j < n; j++)
        {
            char c = text[j];
            if (c == '(' || c == '[')
                depth_p++;
            else if (c == ')' || c == ']')
            {
                depth_p--;
                if (depth_p == 0)
                    break;
            }
            else if (c == ',' && depth_p == 1)
            {
                if (na >= 2)
                    bad = true;
                else
                {
                    arg[na] = text + start;
                    alen[na] = j - start;
                    na++;
                }
                start = j + 1;
            }
        }
        /* j = ')' fermante, ou n si l'appel n'est pas ferme. */
        if (!bad && na < 3)
        {
            arg[na] = text + start;
            alen[na] = j - start;
            na++;
        }
        complex v = C_NAN;
        if (!bad && na == k_calls[k].nargs)
            v = eval_call(k_calls[k].kind, arg, alen, x, letter, depth);
        if (!c_finite(v))
        {
            free(buf);
            return C_NAN;
        }
        len += fmt_literal(buf + len, v);
        i = j < n ? j + 1 : n;
    }
    buf[len] = '\0';
    int tok_n = 0;
    token *toks = parse_string_to_token(buf, len, &tok_n);
    free(buf);
    if (!toks)
        return C_NAN;
    token *rpn = shunting_yard(toks, tok_n);
    int yarded = count_yarded(toks, tok_n);
    complex r = rpn ? evaluate_npi_c_var(rpn, yarded, x, letter) : C_NAN;
    token_free_matrices(toks, tok_n);
    free(toks);
    free(rpn);
    return r;
}

complex calculus_eval(const char *text, int n)
{
    complex zero = {0, 0};
    return eval_text(text, n, zero, -1, 0);
}
