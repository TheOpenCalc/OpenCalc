/* Calcul formel (prototype). Voir headers/Cas.h.
 *
 * Organisation :
 *   1. zone memoire du calcul (arena) et gestion d'erreur (longjmp)
 *   2. grands entiers (Big) et rationnels (Q)
 *   3. expressions (Expr), ordre canonique
 *   4. simplification automatique : sommes, produits, puissances, fonctions
 *   5. developper, polynomes, factoriser
 *   6. deriver, primitive, resoudre
 *   7. lecture des jetons de l'editeur, ecriture du resultat
 *
 * Les expressions simplifiees sont canoniques : les sommes et les produits
 * sont aplatis et tries, le coefficient numerique vient en premier, les
 * racines sont ecrites m^(1/q) avec m entier sans puissance q-ieme. Deux
 * expressions egales ont donc la meme forme, ce qui permet de regrouper les
 * termes par simple comparaison. */
#include "Cas.h"
#include "Evaluator.h"
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_angle_mode;

/* ===================== 1. Memoire et erreurs ============================= */

static uint8_t *g_buf;
static size_t g_cap, g_used;
static jmp_buf g_jmp;
static const char *g_err; /* NULL : saisie hors du calcul formel */
static long g_steps;
#define CAS_MAX_STEPS 3000000L

[[noreturn]] static void fail(const char *msg)
{
    g_err = msg;
    longjmp(g_jmp, 1);
}

[[noreturn]] static void unsupported(void)
{
    g_err = NULL;
    longjmp(g_jmp, 1);
}

static void tick(void)
{
    if (++g_steps > CAS_MAX_STEPS)
        fail("Calcul trop long");
}

static void *amalloc(size_t n)
{
    n = (n + 7) & ~(size_t)7;
    if (g_used + n > g_cap)
        fail("Memoire insuffisante");
    void *p = g_buf + g_used;
    g_used += n;
    return p;
}

/* ===================== 2. Grands entiers ================================= */

#define BIG_MAX_LIMBS 40 /* 1280 bits, environ 385 chiffres */

struct Big
{
    int n;    /* nombre de mots utiles, 0 pour zero */
    bool neg;
    uint32_t *d; /* mots de 32 bits, poids faible en premier */
};

static Big big_alloc(int n)
{
    Big b;
    b.n = n;
    b.neg = false;
    b.d = NULL;
    if (n > 0)
    {
        b.d = (uint32_t *)amalloc(sizeof(uint32_t) * n);
        memset(b.d, 0, sizeof(uint32_t) * n);
    }
    return b;
}

static Big big_norm(Big b)
{
    while (b.n > 0 && b.d[b.n - 1] == 0)
        b.n--;
    if (b.n == 0)
        b.neg = false;
    if (b.n > BIG_MAX_LIMBS)
        fail("Nombre trop grand");
    return b;
}

static Big big_ll(long long v)
{
    Big b = big_alloc(2);
    unsigned long long u = v < 0 ? (unsigned long long)(-(v + 1)) + 1u : (unsigned long long)v;
    b.d[0] = (uint32_t)u;
    b.d[1] = (uint32_t)(u >> 32);
    b.neg = v < 0;
    return big_norm(b);
}

static bool big_fits_u64(Big a, unsigned long long *out)
{
    if (a.n > 2)
        return false;
    unsigned long long v = 0;
    if (a.n > 0)
        v = a.d[0];
    if (a.n > 1)
        v |= (unsigned long long)a.d[1] << 32;
    *out = v;
    return true;
}

static bool big_fits_ll(Big a, long long *out)
{
    unsigned long long u;
    if (!big_fits_u64(a, &u) || u > 0x7fffffffffffffffULL)
        return false;
    *out = a.neg ? -(long long)u : (long long)u;
    return true;
}

static bool big_is_one(Big a)
{
    return a.n == 1 && a.d[0] == 1 && !a.neg;
}

static int big_cmp_abs(Big a, Big b)
{
    if (a.n != b.n)
        return a.n < b.n ? -1 : 1;
    for (int i = a.n - 1; i >= 0; i--)
        if (a.d[i] != b.d[i])
            return a.d[i] < b.d[i] ? -1 : 1;
    return 0;
}

static Big big_neg(Big a)
{
    a.neg = a.n ? !a.neg : false;
    return a;
}

static Big big_abs(Big a)
{
    a.neg = false;
    return a;
}

static Big add_abs(Big a, Big b)
{
    if (a.n < b.n)
    {
        Big t = a;
        a = b;
        b = t;
    }
    Big r = big_alloc(a.n + 1);
    uint64_t c = 0;
    for (int i = 0; i < a.n; i++)
    {
        c += (uint64_t)a.d[i] + (i < b.n ? b.d[i] : 0);
        r.d[i] = (uint32_t)c;
        c >>= 32;
    }
    r.d[a.n] = (uint32_t)c;
    return big_norm(r);
}

/* |a| - |b|, avec |a| >= |b|. */
static Big sub_abs(Big a, Big b)
{
    Big r = big_alloc(a.n);
    int64_t borrow = 0;
    for (int i = 0; i < a.n; i++)
    {
        int64_t t = (int64_t)a.d[i] - (i < b.n ? (int64_t)b.d[i] : 0) - borrow;
        if (t < 0)
        {
            t += (int64_t)1 << 32;
            borrow = 1;
        }
        else
            borrow = 0;
        r.d[i] = (uint32_t)t;
    }
    return big_norm(r);
}

static Big big_add(Big a, Big b)
{
    if (a.neg == b.neg)
    {
        Big r = add_abs(a, b);
        r.neg = r.n ? a.neg : false;
        return r;
    }
    int c = big_cmp_abs(a, b);
    if (c == 0)
        return big_alloc(0);
    Big r = c > 0 ? sub_abs(a, b) : sub_abs(b, a);
    r.neg = c > 0 ? a.neg : b.neg;
    return r;
}

static Big big_sub(Big a, Big b)
{
    return big_add(a, big_neg(b));
}

static Big big_mul(Big a, Big b)
{
    if (!a.n || !b.n)
        return big_alloc(0);
    if (a.n + b.n > BIG_MAX_LIMBS + 1)
        fail("Nombre trop grand");
    Big r = big_alloc(a.n + b.n);
    for (int i = 0; i < a.n; i++)
    {
        uint64_t c = 0;
        for (int j = 0; j < b.n; j++)
        {
            c += (uint64_t)a.d[i] * b.d[j] + r.d[i + j];
            r.d[i + j] = (uint32_t)c;
            c >>= 32;
        }
        r.d[i + b.n] = (uint32_t)c;
    }
    r.neg = a.neg != b.neg;
    return big_norm(r);
}

static uint32_t divmod_small(Big a, uint32_t m, Big *q)
{
    Big r = big_alloc(a.n);
    uint64_t rem = 0;
    for (int i = a.n - 1; i >= 0; i--)
    {
        uint64_t cur = (rem << 32) | a.d[i];
        r.d[i] = (uint32_t)(cur / m);
        rem = cur % m;
    }
    r.neg = a.neg;
    *q = big_norm(r);
    return (uint32_t)rem;
}

static int cmp_raw(const uint32_t *r, int rn, const uint32_t *b, int bn)
{
    int n = rn > bn ? rn : bn;
    for (int i = n - 1; i >= 0; i--)
    {
        uint32_t x = i < rn ? r[i] : 0;
        uint32_t y = i < bn ? b[i] : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

/* Division tronquee vers zero : a = q*b + r, r du signe de a. */
static void big_divmod(Big a, Big b, Big *q, Big *r)
{
    tick();
    if (!b.n)
        fail("Division par zero");
    if (big_cmp_abs(a, b) < 0)
    {
        *q = big_alloc(0);
        *r = a;
        return;
    }
    if (b.n == 1)
    {
        Big qq;
        uint32_t rm = divmod_small(a, b.d[0], &qq);
        qq.neg = qq.n ? (a.neg != b.neg) : false;
        Big rr = big_ll(rm);
        rr.neg = rr.n ? a.neg : false;
        *q = qq;
        *r = rr;
        return;
    }
    /* Division binaire sur les valeurs absolues. */
    int cap = b.n + 1;
    uint32_t *R = (uint32_t *)amalloc(sizeof(uint32_t) * cap);
    memset(R, 0, sizeof(uint32_t) * cap);
    Big qq = big_alloc(a.n);
    for (int i = a.n * 32 - 1; i >= 0; i--)
    {
        uint32_t carry = (a.d[i / 32] >> (i % 32)) & 1u;
        for (int k = 0; k < cap; k++)
        {
            uint32_t nc = R[k] >> 31;
            R[k] = (R[k] << 1) | carry;
            carry = nc;
        }
        if (cmp_raw(R, cap, b.d, b.n) >= 0)
        {
            int64_t borrow = 0;
            for (int k = 0; k < cap; k++)
            {
                int64_t t = (int64_t)R[k] - (k < b.n ? (int64_t)b.d[k] : 0) - borrow;
                if (t < 0)
                {
                    t += (int64_t)1 << 32;
                    borrow = 1;
                }
                else
                    borrow = 0;
                R[k] = (uint32_t)t;
            }
            qq.d[i / 32] |= 1u << (i % 32);
        }
    }
    qq.neg = a.neg != b.neg;
    *q = big_norm(qq);
    Big rr = big_alloc(cap);
    memcpy(rr.d, R, sizeof(uint32_t) * cap);
    rr.neg = a.neg;
    *r = big_norm(rr);
}

static unsigned long long gcd_u64(unsigned long long a, unsigned long long b)
{
    while (b)
    {
        unsigned long long t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static Big big_gcd(Big a, Big b)
{
    a = big_abs(a);
    b = big_abs(b);
    unsigned long long x, y;
    if (big_fits_u64(a, &x) && big_fits_u64(b, &y))
    {
        unsigned long long g = gcd_u64(x, y);
        Big r = big_alloc(2);
        r.d[0] = (uint32_t)g;
        r.d[1] = (uint32_t)(g >> 32);
        return big_norm(r);
    }
    while (b.n)
    {
        Big q, r;
        big_divmod(a, b, &q, &r);
        a = b;
        b = big_abs(r);
    }
    return a;
}

static Big big_pow(Big a, long long k)
{
    Big r = big_ll(1);
    while (k > 0)
    {
        if (k & 1)
            r = big_mul(r, a);
        k >>= 1;
        if (k)
            a = big_mul(a, a);
    }
    return r;
}

static double big_to_double(Big a)
{
    double v = 0;
    for (int i = a.n - 1; i >= 0; i--)
        v = v * 4294967296.0 + a.d[i];
    return a.neg ? -v : v;
}

/* Ecriture decimale ; renvoie la longueur ou -1 si cap est trop petit. */
static int big_to_str(Big a, char *out, int cap)
{
    if (a.n == 0)
    {
        if (cap < 2)
            return -1;
        strcpy(out, "0");
        return 1;
    }
    uint32_t chunks[48];
    int nc = 0;
    Big t = big_abs(a);
    while (t.n && nc < 48)
        chunks[nc++] = divmod_small(t, 1000000000u, &t);
    char tmp[480];
    int pos = 0;
    if (a.neg)
        tmp[pos++] = '-';
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%u", (unsigned)chunks[nc - 1]);
    for (int i = nc - 2; i >= 0; i--)
        pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%09u", (unsigned)chunks[i]);
    if (pos + 1 > cap)
        return -1;
    memcpy(out, tmp, pos + 1);
    return pos;
}

/* ----------------------------- Rationnels -------------------------------- */

struct Q
{
    Big n, d; /* d > 0, fraction irreductible */
};

static Q q_make(Big n, Big d)
{
    if (!d.n)
        fail("Division par zero");
    if (d.neg)
    {
        n = big_neg(n);
        d = big_neg(d);
    }
    Q r;
    if (!n.n)
    {
        r.n = n;
        r.d = big_ll(1);
        return r;
    }
    Big g = big_gcd(n, d);
    if (!big_is_one(g))
    {
        Big rem;
        big_divmod(n, g, &n, &rem);
        big_divmod(d, g, &d, &rem);
    }
    r.n = n;
    r.d = d;
    return r;
}

static Q q_int(long long v)
{
    Q r;
    r.n = big_ll(v);
    r.d = big_ll(1);
    return r;
}

static Q q_frac(long long a, long long b)
{
    return q_make(big_ll(a), big_ll(b));
}

static bool q_isint(Q a)
{
    return big_is_one(a.d);
}

static int q_sign(Q a)
{
    return a.n.n == 0 ? 0 : (a.n.neg ? -1 : 1);
}

static bool q_is(Q a, long long v)
{
    long long x;
    return q_isint(a) && big_fits_ll(a.n, &x) && x == v;
}

static Q q_add(Q a, Q b)
{
    if (q_isint(a) && q_isint(b))
    {
        Q r;
        r.n = big_add(a.n, b.n);
        r.d = a.d;
        return r;
    }
    return q_make(big_add(big_mul(a.n, b.d), big_mul(b.n, a.d)), big_mul(a.d, b.d));
}

static Q q_neg(Q a)
{
    a.n = big_neg(a.n);
    return a;
}

static Q q_sub(Q a, Q b)
{
    return q_add(a, q_neg(b));
}

static Q q_mul(Q a, Q b)
{
    if (q_isint(a) && q_isint(b))
    {
        Q r;
        r.n = big_mul(a.n, b.n);
        r.d = a.d;
        return r;
    }
    return q_make(big_mul(a.n, b.n), big_mul(a.d, b.d));
}

static Q q_div(Q a, Q b)
{
    if (!b.n.n)
        fail("Division par zero");
    return q_make(big_mul(a.n, b.d), big_mul(a.d, b.n));
}

static int q_cmp(Q a, Q b)
{
    Big x = big_sub(big_mul(a.n, b.d), big_mul(b.n, a.d));
    return x.n == 0 ? 0 : (x.neg ? -1 : 1);
}

static double q_to_double(Q a)
{
    return big_to_double(a.n) / big_to_double(a.d);
}

static Q q_pow_int(Q a, long long k)
{
    if (k < 0)
    {
        if (!a.n.n)
            fail("Division par zero");
        a = q_div(q_int(1), a);
        k = -k;
    }
    if (k > 4096)
        fail("Exposant trop grand");
    Q r;
    r.n = big_pow(a.n, k);
    r.d = big_pow(a.d, k);
    return r;
}

/* Partie entiere (vers -infini). */
static Big q_floor(Q a)
{
    Big q, r;
    big_divmod(a.n, a.d, &q, &r);
    if (r.n && a.n.neg)
        q = big_sub(q, big_ll(1));
    return q;
}

/* ===================== 3. Expressions ==================================== */

enum
{
    E_NUM, /* rationnel q */
    E_SYM, /* 'X', 'p' (pi), 'e', 'i' (seulement dans les solutions) */
    E_POW, /* a[0] ^ a[1] */
    E_MUL, /* produit de n facteurs */
    E_ADD, /* somme de n termes */
    E_FUN, /* fonction f de a[0] */
    E_OP,  /* commande (developper...) ou fonction a evaluer : f = jeton */
    E_EQ,  /* a[0] = a[1] (seulement dans resoudre) ; f : '=' ou REL_LT... */
    E_SYS, /* plusieurs relations a verifier ensemble (resoudre) */
};

enum
{
    F_SIN,
    F_COS,
    F_TAN,
    F_ASIN,
    F_ACOS,
    F_ATAN,
    F_LN,
    F_LOG,
    F_ABS,
    F_SINH,
    F_COSH,
    F_TANH,
    F_ASINH,
    F_ACOSH,
    F_ATANH,
    F_FACT,
};

struct Expr
{
    uint8_t k;
    uint8_t f;
    char sym;
    int n;
    Q q;
    Expr **a;
};

static Expr *node(int k, int n)
{
    tick();
    Expr *e = (Expr *)amalloc(sizeof(Expr));
    memset(e, 0, sizeof(*e));
    e->k = (uint8_t)k;
    e->n = n;
    if (n > 0)
        e->a = (Expr **)amalloc(sizeof(Expr *) * n);
    return e;
}

static Expr *num(Q q)
{
    Expr *e = node(E_NUM, 0);
    e->q = q;
    return e;
}

static Expr *inum(long long v)
{
    return num(q_int(v));
}

static Expr *qnum(long long a, long long b)
{
    return num(q_frac(a, b));
}

static Expr *sym(char c)
{
    Expr *e = node(E_SYM, 0);
    e->sym = c;
    return e;
}

static Expr *rfun(int f, Expr *x)
{
    Expr *e = node(E_FUN, 1);
    e->f = (uint8_t)f;
    e->a[0] = x;
    return e;
}

static Expr *rpow(Expr *b, Expr *x)
{
    Expr *e = node(E_POW, 2);
    e->a[0] = b;
    e->a[1] = x;
    return e;
}

static Expr *rlist(int k, Expr **v, int n)
{
    Expr *e = node(k, n);
    for (int i = 0; i < n; i++)
        e->a[i] = v[i];
    return e;
}

static bool is_num(const Expr *e)
{
    return e->k == E_NUM;
}

static bool is_val(const Expr *e, long long v)
{
    return e->k == E_NUM && q_is(e->q, v);
}

static bool is_sym(const Expr *e, char c)
{
    return e->k == E_SYM && e->sym == c;
}

static bool has_x(const Expr *e)
{
    if (e->k == E_SYM)
        return e->sym == 'X';
    for (int i = 0; i < e->n; i++)
        if (has_x(e->a[i]))
            return true;
    return false;
}

/* Contient e, ln, sin... : la valeur n'est pas algebrique. */
static bool has_transc(const Expr *e)
{
    if (e->k == E_FUN || is_sym(e, 'e'))
        return true;
    for (int i = 0; i < e->n; i++)
        if (has_transc(e->a[i]))
            return true;
    return false;
}

static int esize(const Expr *e)
{
    int s = 1;
    for (int i = 0; i < e->n; i++)
        s += esize(e->a[i]);
    return s;
}

/* Liste extensible dans la zone de calcul. */
struct List
{
    Expr **v;
    int n, cap;
};

static void l_init(List *l, int cap)
{
    if (cap < 4)
        cap = 4;
    l->v = (Expr **)amalloc(sizeof(Expr *) * cap);
    l->n = 0;
    l->cap = cap;
}

static void l_push(List *l, Expr *e)
{
    if (l->n == l->cap)
    {
        int nc = l->cap * 2;
        Expr **nv = (Expr **)amalloc(sizeof(Expr *) * nc);
        memcpy(nv, l->v, sizeof(Expr *) * l->n);
        l->v = nv;
        l->cap = nc;
    }
    l->v[l->n++] = e;
}

/* Ordre total, utilise pour trier les sommes et les produits. */
static int ecmp(const Expr *a, const Expr *b)
{
    if (a == b)
        return 0;
    if (a->k != b->k)
        return a->k < b->k ? -1 : 1;
    switch (a->k)
    {
    case E_NUM:
        return q_cmp(a->q, b->q);
    case E_SYM:
        return a->sym == b->sym ? 0 : (a->sym < b->sym ? -1 : 1);
    case E_FUN:
    case E_OP:
        if (a->f != b->f)
            return a->f < b->f ? -1 : 1;
        break;
    default:
        break;
    }
    if (a->n != b->n)
        return a->n < b->n ? -1 : 1;
    for (int i = 0; i < a->n; i++)
    {
        int c = ecmp(a->a[i], b->a[i]);
        if (c)
            return c;
    }
    return 0;
}

static bool eq(const Expr *a, const Expr *b)
{
    return ecmp(a, b) == 0;
}

static void esort(Expr **v, int n)
{
    for (int i = 1; i < n; i++)
    {
        Expr *x = v[i];
        int j = i - 1;
        while (j >= 0 && ecmp(v[j], x) > 0)
        {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
}

/* ===================== 4. Simplification ================================= */

static Expr *s_add(Expr **in, int n);
static Expr *s_mul(Expr **in, int n);
static Expr *s_pow(Expr *b, Expr *x);
static Expr *s_fun(int f, Expr *x);
static Expr *S(Expr *e);

static Expr *add2(Expr *a, Expr *b)
{
    Expr *v[2] = {a, b};
    return s_add(v, 2);
}

static Expr *mul2(Expr *a, Expr *b)
{
    Expr *v[2] = {a, b};
    return s_mul(v, 2);
}

static Expr *neg(Expr *a)
{
    return mul2(inum(-1), a);
}

static Expr *sub2(Expr *a, Expr *b)
{
    return add2(a, neg(b));
}

static Expr *div2(Expr *a, Expr *b)
{
    return mul2(a, s_pow(b, inum(-1)));
}

/* Valeur numerique (X vaut x). */
static double nv(const Expr *e, double x)
{
    switch (e->k)
    {
    case E_NUM:
        return q_to_double(e->q);
    case E_SYM:
        if (e->sym == 'X')
            return x;
        if (e->sym == 'p')
            return M_PI;
        if (e->sym == 'e')
            return M_E;
        return NAN;
    case E_ADD:
    {
        double s = 0;
        for (int i = 0; i < e->n; i++)
            s += nv(e->a[i], x);
        return s;
    }
    case E_MUL:
    {
        double s = 1;
        for (int i = 0; i < e->n; i++)
            s *= nv(e->a[i], x);
        return s;
    }
    case E_POW:
    {
        double b = nv(e->a[0], x);
        double p = nv(e->a[1], x);
        if (b < 0 && p != floor(p))
        {
            /* Racine impaire d'un negatif : (-8)^(1/3) = -2. */
            const Expr *ex = e->a[1];
            if (ex->k == E_NUM)
            {
                unsigned long long dd;
                long long nn;
                if (big_fits_u64(ex->q.d, &dd) && (dd & 1) && big_fits_ll(ex->q.n, &nn))
                {
                    double r = pow(-b, p);
                    return (nn & 1) ? -r : r;
                }
            }
            return NAN;
        }
        return pow(b, p);
    }
    case E_FUN:
    {
        double v = nv(e->a[0], x);
        switch (e->f)
        {
        case F_SIN:
            return sin(angle_in(v));
        case F_COS:
            return cos(angle_in(v));
        case F_TAN:
            return tan(angle_in(v));
        case F_ASIN:
            return angle_out(asin(v));
        case F_ACOS:
            return angle_out(acos(v));
        case F_ATAN:
            return angle_out(atan(v));
        case F_LN:
            return log(v);
        case F_LOG:
            return log10(v);
        case F_ABS:
            return fabs(v);
        case F_SINH:
            return sinh(v);
        case F_COSH:
            return cosh(v);
        case F_TANH:
            return tanh(v);
        case F_ASINH:
            return asinh(v);
        case F_ACOSH:
            return acosh(v);
        case F_ATANH:
            return atanh(v);
        case F_FACT:
            return tgamma(v + 1);
        }
        return NAN;
    }
    default:
        return NAN;
    }
}

/* Coefficient rationnel et reste d'un terme : 3X^2 -> (3, X^2). */
static Expr *split_coef(Expr *t, Q *c)
{
    if (t->k == E_MUL && is_num(t->a[0]))
    {
        *c = t->a[0]->q;
        if (t->n == 2)
            return t->a[1];
        return rlist(E_MUL, t->a + 1, t->n - 1);
    }
    *c = q_int(1);
    return t;
}

/* c * t, t deja simplifie et sans coefficient. */
static Expr *with_coef(Q c, Expr *t)
{
    if (q_is(c, 1))
        return t;
    if (t->k == E_MUL)
    {
        Expr *e = node(E_MUL, t->n + 1);
        e->a[0] = num(c);
        for (int i = 0; i < t->n; i++)
            e->a[i + 1] = t->a[i];
        return e;
    }
    Expr *e = node(E_MUL, 2);
    e->a[0] = num(c);
    e->a[1] = t;
    return e;
}

static Expr *s_add(Expr **in, int n)
{
    tick();
    List flat;
    l_init(&flat, n + 4);
    for (int i = 0; i < n; i++)
    {
        if (in[i]->k == E_ADD)
            for (int j = 0; j < in[i]->n; j++)
                l_push(&flat, in[i]->a[j]);
        else
            l_push(&flat, in[i]);
    }
    Q cst = q_int(0);
    Expr **parts = (Expr **)amalloc(sizeof(Expr *) * (flat.n + 1));
    Q *coefs = (Q *)amalloc(sizeof(Q) * (flat.n + 1));
    int m = 0;
    for (int i = 0; i < flat.n; i++)
    {
        Expr *t = flat.v[i];
        if (is_num(t))
        {
            cst = q_add(cst, t->q);
            continue;
        }
        Q c;
        Expr *rest = split_coef(t, &c);
        int j;
        for (j = 0; j < m; j++)
            if (eq(parts[j], rest))
                break;
        if (j < m)
            coefs[j] = q_add(coefs[j], c);
        else
        {
            parts[m] = rest;
            coefs[m] = c;
            m++;
        }
    }
    List out;
    l_init(&out, m + 1);
    for (int j = 0; j < m; j++)
        if (q_sign(coefs[j]) != 0)
            l_push(&out, with_coef(coefs[j], parts[j]));
    esort(out.v, out.n);
    bool has_c = q_sign(cst) != 0;
    if (out.n == 0)
        return num(cst);
    if (out.n == 1 && !has_c)
        return out.v[0];
    Expr *e = node(E_ADD, out.n + (has_c ? 1 : 0));
    int k = 0;
    if (has_c)
        e->a[k++] = num(cst);
    for (int j = 0; j < out.n; j++)
        e->a[k++] = out.v[j];
    return e;
}

static Expr *s_mul(Expr **in, int n)
{
    tick();
    List flat;
    l_init(&flat, n + 4);
    for (int i = 0; i < n; i++)
    {
        if (in[i]->k == E_MUL)
            for (int j = 0; j < in[i]->n; j++)
                l_push(&flat, in[i]->a[j]);
        else
            l_push(&flat, in[i]);
    }
    Q coef = q_int(1);
    Expr **bases = (Expr **)amalloc(sizeof(Expr *) * (flat.n + 1));
    Expr **exps = (Expr **)amalloc(sizeof(Expr *) * (flat.n + 1));
    int m = 0;
    for (int i = 0; i < flat.n; i++)
    {
        Expr *x = flat.v[i];
        if (is_num(x))
        {
            coef = q_mul(coef, x->q);
            if (q_sign(coef) == 0)
                return inum(0);
            continue;
        }
        Expr *b = x->k == E_POW ? x->a[0] : x;
        Expr *p = x->k == E_POW ? x->a[1] : inum(1);
        int j;
        for (j = 0; j < m; j++)
            if (eq(bases[j], b))
                break;
        if (j < m)
            exps[j] = add2(exps[j], p);
        else
        {
            bases[m] = b;
            exps[m] = p;
            m++;
        }
    }

    /* Chaque facteur base^exposant ; un resultat numerique ou un produit
     * (racine simplifiee 2r(3), puissance d'un produit) relance le calcul. */
    List out;
    l_init(&out, m + 1);
    bool again = false;
    for (int j = 0; j < m; j++)
    {
        Expr *p = (exps[j]->k == E_NUM && q_is(exps[j]->q, 1)) ? bases[j] : s_pow(bases[j], exps[j]);
        if (is_num(p))
        {
            coef = q_mul(coef, p->q);
            if (q_sign(coef) == 0)
                return inum(0);
        }
        else if (p->k == E_MUL)
        {
            again = true;
            l_push(&out, p);
        }
        else
            l_push(&out, p);
    }
    if (again)
    {
        l_push(&out, num(coef));
        return s_mul(out.v, out.n);
    }

    /* Racines de nombres de meme indice : r(2)r(3) = r(6). */
    for (int i = 0; i < out.n; i++)
    {
        Expr *a = out.v[i];
        if (a->k != E_POW || !is_num(a->a[0]) || !is_num(a->a[1]))
            continue;
        for (int j = i + 1; j < out.n; j++)
        {
            Expr *b = out.v[j];
            if (b->k != E_POW || !is_num(b->a[0]) || !eq(a->a[1], b->a[1]))
                continue;
            Expr *merged = s_pow(num(q_mul(a->a[0]->q, b->a[0]->q)), a->a[1]);
            List rest;
            l_init(&rest, out.n + 1);
            l_push(&rest, num(coef));
            l_push(&rest, merged);
            for (int k = 0; k < out.n; k++)
                if (k != i && k != j)
                    l_push(&rest, out.v[k]);
            return s_mul(rest.v, rest.n);
        }
    }

    esort(out.v, out.n);
    bool has_c = !q_is(coef, 1);
    if (out.n == 0)
        return num(coef);
    if (out.n == 1 && !has_c)
        return out.v[0];
    Expr *e = node(E_MUL, out.n + (has_c ? 1 : 0));
    int k = 0;
    if (has_c)
        e->a[k++] = num(coef);
    for (int j = 0; j < out.n; j++)
        e->a[k++] = out.v[j];
    return e;
}

/* Decomposition en facteurs premiers d'un entier de 64 bits.
 * Petits diviseurs par essais, puis Miller-Rabin (deterministe sur 64 bits)
 * et Pollard rho. Avant : essais seulement, abandon au-dela de 10^12, et le
 * nombre etait alors affiche tel quel, comme s'il etait premier. */
struct Factors
{
    unsigned long long p[16];
    int e[16];
    int n;
};

typedef unsigned long long u64;

/* a + b mod m, sans debordement (a, b < m). */
static u64 addmod(u64 a, u64 b, u64 m) { return a >= m - b ? a - (m - b) : a + b; }

/* a * b mod m sans entier de 128 bits (absent sur ARM 32 bits). */
static u64 mulmod(u64 a, u64 b, u64 m)
{
    if ((a | b) < (1ULL << 32))
        return (a * b) % m;
    u64 r = 0;
    a %= m;
    while (b)
    {
        if (b & 1)
            r = addmod(r, a, m);
        a = addmod(a, a, m);
        b >>= 1;
    }
    return r;
}

static u64 powmod(u64 b, u64 e, u64 m)
{
    u64 r = 1 % m;
    b %= m;
    while (e)
    {
        if (e & 1)
            r = mulmod(r, b, m);
        b = mulmod(b, b, m);
        e >>= 1;
    }
    return r;
}

static bool is_prime_u64(u64 n)
{
    if (n < 2)
        return false;
    static const u64 small[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    for (u64 p : small)
        if (n % p == 0)
            return n == p;
    u64 d = n - 1;
    int s = 0;
    while (!(d & 1))
        d >>= 1, s++;
    for (u64 a : small) /* bases suffisantes pour tout n < 2^64 */
    {
        u64 x = powmod(a, d, n);
        if (x == 1 || x == n - 1)
            continue;
        bool comp = true;
        for (int i = 1; i < s && comp; i++)
        {
            x = mulmod(x, x, n);
            if (x == n - 1)
                comp = false;
        }
        if (comp)
            return false;
    }
    return true;
}

/* Un diviseur non trivial de n (compose, impair) : Pollard rho, Brent. */
static u64 rho(u64 n)
{
    for (u64 c = 1; c < 64; c++)
    {
        u64 y = 2, x = 2, q = 1, g = 1, ys = 2;
        u64 r = 1;
        while (g == 1)
        {
            x = y;
            for (u64 i = 0; i < r; i++)
                y = addmod(mulmod(y, y, n), c, n);
            for (u64 k = 0; k < r && g == 1; k += 64)
            {
                ys = y;
                for (u64 i = 0; i < 64 && i < r - k; i++)
                {
                    y = addmod(mulmod(y, y, n), c, n);
                    q = mulmod(q, x > y ? x - y : y - x, n);
                }
                g = gcd_u64(q, n);
            }
            r <<= 1;
            if (r > (1ULL << 24))
                break;
        }
        if (g == n) /* retour en arriere pas a pas */
        {
            do
            {
                ys = addmod(mulmod(ys, ys, n), c, n);
                g = gcd_u64(x > ys ? x - ys : ys - x, n);
            } while (g == 1);
        }
        if (g != 1 && g != n)
            return g;
    }
    return 0;
}

static bool add_prime(Factors *f, u64 p, int k)
{
    for (int i = 0; i < f->n; i++)
        if (f->p[i] == p)
        {
            f->e[i] += k;
            return true;
        }
    if (f->n >= 16)
        return false;
    f->p[f->n] = p;
    f->e[f->n] = k;
    f->n++;
    return true;
}

static bool factorize(unsigned long long a, Factors *f)
{
    f->n = 0;
    for (u64 d = 2; d < 1000 && d * d <= a; d += (d == 2 ? 1 : 2))
    {
        if (a % d)
            continue;
        int k = 0;
        while (a % d == 0)
        {
            a /= d;
            k++;
        }
        add_prime(f, d, k);
    }
    /* Reste sans petit facteur : morceaux a decouper (au plus 64). */
    u64 todo[64];
    int nt = 0;
    if (a > 1)
        todo[nt++] = a;
    while (nt)
    {
        u64 m = todo[--nt];
        if (is_prime_u64(m))
        {
            if (!add_prime(f, m, 1))
                return false;
            continue;
        }
        u64 g = rho(m);
        if (!g || nt + 2 > 64)
            return false;
        todo[nt++] = g;
        todo[nt++] = m / g;
    }
    /* facteurs dans l'ordre croissant */
    for (int i = 1; i < f->n; i++)
        for (int j = i; j > 0 && f->p[j - 1] > f->p[j]; j--)
        {
            u64 tp = f->p[j];
            f->p[j] = f->p[j - 1];
            f->p[j - 1] = tp;
            int te = f->e[j];
            f->e[j] = f->e[j - 1];
            f->e[j - 1] = te;
        }
    return true;
}

/* Factorise un grand entier positif : petits facteurs premiers (< 1000)
 * retires d'abord, puis le reste s'il tient sur 64 bits. Ainsi
 * 25*10^18 (plus grand que 2^64) donne 2^18 5^20, et sa racine carree
 * se simplifie. */
static bool factorize_big(Big A, Factors *f)
{
    unsigned long long a;
    if (big_fits_u64(A, &a))
        return factorize(a, f);
    f->n = 0;
    for (long long d = 2; d < 1000; d += (d == 2 ? 1 : 2))
    {
        Big bd = big_ll(d);
        int k = 0;
        for (;;)
        {
            Big q, r;
            big_divmod(A, bd, &q, &r);
            if (r.n)
                break;
            A = q;
            k++;
        }
        if (k && !add_prime(f, (u64)d, k))
            return false;
        if (big_fits_u64(A, &a))
            break;
    }
    if (!big_fits_u64(A, &a))
        return false;
    Factors rest;
    if (!factorize(a, &rest))
        return false;
    for (int i = 0; i < rest.n; i++)
        if (!add_prime(f, rest.p[i], rest.e[i]))
            return false;
    return true;
}

/* A^(r/q) pour un entier A > 0 et 0 < r < q : k * m^(1/q') avec m sans
 * puissance q'-ieme. */
static Expr *radical(Big A, long long r, long long q)
{
    if (big_is_one(A))
        return inum(1);
    Factors f;
    if (!factorize_big(A, &f))
    {
        Q base;
        base.n = A;
        base.d = big_ll(1);
        return rpow(num(base), qnum(r, q));
    }
    long long g = q;
    int rem[16];
    Q coef = q_int(1);
    for (int i = 0; i < f.n; i++)
    {
        long long total = (long long)f.e[i] * r;
        coef = q_mul(coef, q_pow_int(q_int((long long)f.p[i]), total / q));
        rem[i] = (int)(total % q);
        long long x = g, y = rem[i];
        while (y)
        {
            long long t = x % y;
            x = y;
            y = t;
        }
        g = x;
    }
    Big m = big_ll(1);
    bool any = false;
    for (int i = 0; i < f.n; i++)
    {
        if (rem[i])
        {
            any = true;
            m = big_mul(m, big_pow(big_ll((long long)f.p[i]), rem[i] / g));
        }
    }
    if (!any)
        return num(coef);
    Q mq;
    mq.n = m;
    mq.d = big_ll(1);
    Expr *rad = rpow(num(mq), qnum(1, q / g));
    if (q_is(coef, 1))
        return rad;
    Expr *e = node(E_MUL, 2);
    e->a[0] = num(coef);
    e->a[1] = rad;
    return e;
}

static Expr *num_pow(Q b, Q x)
{
    if (q_isint(x))
    {
        long long k;
        if (!big_fits_ll(x.n, &k) || k > 4096 || k < -4096)
            fail("Exposant trop grand");
        if (q_sign(b) == 0 && k < 0)
            fail("Division par zero");
        return num(q_pow_int(b, k));
    }
    if (q_sign(b) == 0)
    {
        if (q_sign(x) > 0)
            return inum(0);
        fail("Division par zero");
    }
    long long p, q;
    if (!big_fits_ll(x.n, &p) || !big_fits_ll(x.d, &q) || q > 64 || p > 4096 || p < -4096)
        return rpow(num(b), num(x));
    if (q_sign(b) < 0)
    {
        if (q % 2 == 0)
            unsupported(); /* racine paire d'un negatif : nombre complexe */
        Expr *v = num_pow(q_neg(b), x);
        return (p % 2) ? neg(v) : v;
    }
    long long fl = p >= 0 ? p / q : -((-p + q - 1) / q);
    long long r = p - fl * q; /* 0 < r < q */
    Expr *ra = radical(b.n, r, q);
    if (fl == 0 && big_is_one(b.d))
        return ra;
    /* b^(r/q) = A^(r/q) * C^((q-r)/q) / C pour b = A/C : denominateur rationnel. */
    Expr *v[4];
    v[0] = num(q_pow_int(b, fl));
    v[1] = ra;
    v[2] = radical(b.d, q - r, q);
    v[3] = num(q_make(big_ll(1), b.d));
    return s_mul(v, 4);
}

/* t = s * racine(m) (s, m rationnels, m > 0) ? */
static bool root_term(Expr *t, Q *sc, Q *m)
{
    if (t->k == E_POW && t->a[0]->k == E_NUM && t->a[1]->k == E_NUM && q_is(q_mul(t->a[1]->q, q_int(2)), 1))
    {
        *sc = q_int(1);
        *m = t->a[0]->q;
        return true;
    }
    if (t->k == E_MUL && t->n == 2 && t->a[0]->k == E_NUM && root_term(t->a[1], sc, m))
    {
        *sc = t->a[0]->q;
        return true;
    }
    return false;
}

static Expr *s_pow(Expr *b, Expr *x)
{
    tick();
    /* OpenCalc : 1/(a + s racine(m)) = (a - s racine(m)) / (a^2 - s^2 m) :
     * denominateur rendu rationnel, comme on l'ecrit au lycee
     * (1/(1+racine(2)) = racine(2)-1). */
    if (x->k == E_NUM && q_is(x->q, -1) && b->k == E_ADD && b->n == 2)
    {
        Expr *ra = NULL, *rt = NULL;
        for (int i = 0; i < 2; i++)
        {
            if (b->a[i]->k == E_NUM)
                ra = b->a[i];
            else
                rt = b->a[i];
        }
        Q sc, m;
        if (ra && rt && root_term(rt, &sc, &m))
        {
            Q den = q_sub(q_mul(ra->q, ra->q), q_mul(q_mul(sc, sc), m));
            if (q_sign(den) != 0)
            {
                Expr *conj = add2(ra, neg(rt));
                return mul2(num(q_div(q_int(1), den)), conj);
            }
        }
    }
    if (x->k == E_NUM)
    {
        if (q_sign(x->q) == 0)
        {
            if (is_val(b, 0))
                fail("0^0 indefini");
            return inum(1);
        }
        if (q_is(x->q, 1))
            return b;
        if (b->k == E_NUM)
            return num_pow(b->q, x->q);
        if (b->k == E_POW && q_isint(x->q))
            return s_pow(b->a[0], mul2(b->a[1], x));
        if (b->k == E_MUL && q_isint(x->q))
        {
            Expr **v = (Expr **)amalloc(sizeof(Expr *) * b->n);
            for (int i = 0; i < b->n; i++)
                v[i] = s_pow(b->a[i], x);
            return s_mul(v, b->n);
        }
    }
    if (is_val(b, 1))
        return inum(1);
    if (is_val(b, 0) && x->k == E_NUM)
    {
        if (q_sign(x->q) > 0)
            return inum(0);
        fail("Division par zero");
    }
    if (is_sym(b, 'e'))
    {
        if (x->k == E_FUN && x->f == F_LN)
            return x->a[0];
        /* e^(k ln u) = u^k */
        if (x->k == E_MUL && x->n == 2 && is_num(x->a[0]) && x->a[1]->k == E_FUN && x->a[1]->f == F_LN)
            return s_pow(x->a[1]->a[0], x->a[0]);
    }
    return rpow(b, x);
}

/* x = k*pi (radians) ou k*180 degres : renvoie k. */
static bool pi_multiple(Expr *x, Q *k)
{
    if (g_angle_mode)
    {
        if (!is_num(x))
            return false;
        *k = q_div(x->q, q_int(180));
        return true;
    }
    if (is_val(x, 0))
    {
        *k = q_int(0);
        return true;
    }
    if (is_sym(x, 'p'))
    {
        *k = q_int(1);
        return true;
    }
    if (x->k == E_MUL && x->n == 2 && is_num(x->a[0]) && is_sym(x->a[1], 'p'))
    {
        *k = x->a[0]->q;
        return true;
    }
    return false;
}

static Expr *half_root(long long m) /* r(m)/2 */
{
    return mul2(qnum(1, 2), s_pow(inum(m), qnum(1, 2)));
}

/* sin(k pi) pour k multiple de 1/4 ou de 1/6, NULL sinon. */
static Expr *sqrtn(long long m) { return s_pow(inum(m), qnum(1, 2)); }

/* sin(k pi), NULL hors des angles remarquables. OpenCalc : multiples de
 * pi/12, pi/10 et pi/8 (15, 18, 22.5, 36, 54, 67.5, 72, 75 degres) en plus de
 * pi/6 et pi/4. */
static Expr *sin_exact(Q k)
{
    Q fl2;
    fl2.n = q_floor(q_div(k, q_int(2)));
    fl2.d = big_ll(1);
    Q t = q_sub(k, q_mul(q_int(2), fl2));
    int sign = 1;
    if (q_cmp(t, q_int(1)) >= 0)
    {
        t = q_sub(t, q_int(1));
        sign = -1;
    }
    if (q_cmp(t, q_frac(1, 2)) > 0)
        t = q_sub(q_int(1), t);
    Q m = q_mul(t, q_int(120));
    if (!q_isint(m))
        return NULL;
    long long v;
    big_fits_ll(m.n, &v);
    Expr *r;
    switch (v)
    {
    case 0:
        return inum(0);
    case 10: /* 15 degres : (racine(6) - racine(2)) / 4 */
        r = mul2(qnum(1, 4), sub2(sqrtn(6), sqrtn(2)));
        break;
    case 12: /* 18 : (racine(5) - 1) / 4 */
        r = mul2(qnum(1, 4), sub2(sqrtn(5), inum(1)));
        break;
    case 15: /* 22.5 : racine(2 - racine(2)) / 2 */
        r = mul2(qnum(1, 2), s_pow(sub2(inum(2), sqrtn(2)), qnum(1, 2)));
        break;
    case 20:
        r = qnum(1, 2);
        break;
    case 24: /* 36 : racine(10 - 2 racine(5)) / 4 */
        r = mul2(qnum(1, 4), s_pow(sub2(inum(10), mul2(inum(2), sqrtn(5))), qnum(1, 2)));
        break;
    case 30:
        r = half_root(2);
        break;
    case 36: /* 54 : (1 + racine(5)) / 4 */
        r = mul2(qnum(1, 4), add2(inum(1), sqrtn(5)));
        break;
    case 40:
        r = half_root(3);
        break;
    case 45: /* 67.5 : racine(2 + racine(2)) / 2 */
        r = mul2(qnum(1, 2), s_pow(add2(inum(2), sqrtn(2)), qnum(1, 2)));
        break;
    case 48: /* 72 : racine(10 + 2 racine(5)) / 4 */
        r = mul2(qnum(1, 4), s_pow(add2(inum(10), mul2(inum(2), sqrtn(5))), qnum(1, 2)));
        break;
    case 50: /* 75 : (racine(6) + racine(2)) / 4 */
        r = mul2(qnum(1, 4), add2(sqrtn(6), sqrtn(2)));
        break;
    case 60:
        r = inum(1);
        break;
    default:
        return NULL;
    }
    return sign < 0 ? neg(r) : r;
}

/* tan(k pi) aux angles ou elle a une forme simple, NULL sinon. */
static Expr *tan_exact(Q k)
{
    Q fl;
    fl.n = q_floor(q_add(k, q_frac(1, 2)));
    fl.d = big_ll(1);
    Q t = q_sub(k, fl); /* dans [-1/2, 1/2[ */
    int sign = q_sign(t) < 0 ? -1 : 1;
    if (sign < 0)
        t = q_neg(t);
    Q m = q_mul(t, q_int(120));
    if (!q_isint(m))
        return NULL;
    long long v;
    big_fits_ll(m.n, &v);
    Expr *r;
    switch (v)
    {
    case 0:
        return inum(0);
    case 10:
        r = sub2(inum(2), sqrtn(3));
        break;
    case 15:
        r = sub2(sqrtn(2), inum(1));
        break;
    case 20:
        r = mul2(qnum(1, 3), sqrtn(3));
        break;
    case 30:
        r = inum(1);
        break;
    case 40:
        r = sqrtn(3);
        break;
    case 45:
        r = add2(sqrtn(2), inum(1));
        break;
    case 50:
        r = add2(inum(2), sqrtn(3));
        break;
    case 60:
        fail("tan non definie");
    default:
        return NULL;
    }
    return sign < 0 ? neg(r) : r;
}

static Expr *trig_exact(int f, Q k)
{
    Expr *s = sin_exact(k);
    Expr *c = sin_exact(q_add(k, q_frac(1, 2)));
    if (f == F_SIN)
        return s;
    if (f == F_COS)
        return c;
    if (f == F_TAN)
    {
        Expr *t = tan_exact(k);
        if (t)
            return t;
    }
    if (!s || !c)
        return NULL;
    if (is_val(c, 0))
        fail("tan non definie");
    return div2(s, c);
}

/* k*pi en radians, ou 180k en degres. */
static Expr *angle_of(Q k)
{
    if (g_angle_mode)
        return num(q_mul(k, q_int(180)));
    return mul2(num(k), sym('p'));
}

/* asin, acos, atan des valeurs remarquables. */
static Expr *arc_exact(int f, double v)
{
    static const int ks[] = {-6, -4, -3, -2, 0, 2, 3, 4, 6}; /* k*12 */
    for (unsigned i = 0; i < sizeof(ks) / sizeof(ks[0]); i++)
    {
        double a = ks[i] * M_PI / 12.0;
        if (f == F_ATAN)
        {
            if (ks[i] == 6 || ks[i] == -6)
                continue;
            if (fabs(tan(a) - v) < 1e-12)
                return angle_of(q_frac(ks[i], 12));
        }
        else if (fabs(sin(a) - v) < 1e-12)
        {
            Q k = q_frac(ks[i], 12);
            if (f == F_ACOS)
                k = q_sub(q_frac(1, 2), k);
            return angle_of(k);
        }
    }
    return NULL;
}

static bool neg_form(const Expr *x)
{
    if (is_num(x))
        return q_sign(x->q) < 0;
    return x->k == E_MUL && is_num(x->a[0]) && q_sign(x->a[0]->q) < 0;
}

static Expr *s_fun(int f, Expr *x)
{
    tick();
    switch (f)
    {
    case F_SIN:
    case F_COS:
    case F_TAN:
    {
        Q k;
        if (pi_multiple(x, &k))
        {
            Expr *v = trig_exact(f, k);
            if (v)
                return v;
        }
        if (neg_form(x))
        {
            Expr *m = neg(x);
            return f == F_COS ? rfun(F_COS, m) : neg(rfun(f, m));
        }
        return rfun(f, x);
    }
    case F_ASIN:
    case F_ACOS:
    case F_ATAN:
        if (!has_x(x) && !has_transc(x))
        {
            double v = nv(x, NAN);
            if ((f != F_ATAN) && fabs(v) > 1)
                unsupported();
            Expr *r = arc_exact(f, v);
            if (r)
                return r;
        }
        return rfun(f, x);
    case F_LN:
        if (is_num(x))
        {
            if (q_sign(x->q) <= 0)
                unsupported(); /* ln d'un negatif : nombre complexe */
            if (q_is(x->q, 1))
                return inum(0);
            if (big_is_one(x->q.n))
            {
                Q d;
                d.n = x->q.d;
                d.d = big_ll(1);
                return neg(rfun(F_LN, num(d)));
            }
        }
        if (is_sym(x, 'e'))
            return inum(1);
        if (x->k == E_POW && is_sym(x->a[0], 'e'))
            return x->a[1];
        if (x->k == E_POW && is_num(x->a[0]) && q_sign(x->a[0]->q) > 0)
            return mul2(x->a[1], s_fun(F_LN, x->a[0]));
        return rfun(F_LN, x);
    case F_LOG:
        if (is_num(x))
        {
            if (q_sign(x->q) <= 0)
                unsupported();
            /* log(10^k) = k */
            for (int k = -18; k <= 18; k++)
                if (q_cmp(x->q, q_pow_int(q_int(10), k)) == 0)
                    return inum(k);
        }
        return rfun(F_LOG, x);
    case F_ABS:
        if (!has_x(x))
        {
            double v = nv(x, NAN);
            if (v > 0)
                return x;
            if (v < 0)
                return neg(x);
            if (v == 0)
                return inum(0);
        }
        if (x->k == E_POW && is_num(x->a[1]) && q_isint(x->a[1]->q))
        {
            long long k;
            if (big_fits_ll(x->a[1]->q.n, &k) && k % 2 == 0)
                return x;
        }
        return rfun(F_ABS, x);
    case F_COSH:
        if (is_val(x, 0))
            return inum(1);
        return rfun(f, x);
    case F_ACOSH:
        if (is_val(x, 1))
            return inum(0);
        return rfun(f, x);
    case F_FACT:
        if (is_num(x) && q_isint(x->q) && q_sign(x->q) >= 0)
        {
            long long n;
            if (!big_fits_ll(x->q.n, &n) || n > 200)
                unsupported();
            Big r = big_ll(1);
            for (long long i = 2; i <= n; i++)
                r = big_mul(r, big_ll(i));
            Q q;
            q.n = r;
            q.d = big_ll(1);
            return num(q);
        }
        unsupported();
    default: /* sinh, tanh, asinh, atanh : impaires, nulles en 0 */
        if (is_val(x, 0))
            return inum(0);
        return rfun(f, x);
    }
}

static Expr *apply_op(Expr *e);

static Expr *S(Expr *e)
{
    tick();
    switch (e->k)
    {
    case E_NUM:
    case E_SYM:
        return e;
    case E_ADD:
    case E_MUL:
    {
        Expr **v = (Expr **)amalloc(sizeof(Expr *) * e->n);
        for (int i = 0; i < e->n; i++)
            v[i] = S(e->a[i]);
        return e->k == E_ADD ? s_add(v, e->n) : s_mul(v, e->n);
    }
    case E_POW:
        return s_pow(S(e->a[0]), S(e->a[1]));
    case E_FUN:
        return s_fun(e->f, S(e->a[0]));
    case E_OP:
        return apply_op(e);
    default:
        fail("Syntaxe");
    }
}

/* ===================== 5. Developper, polynomes, factoriser ============== */

static Expr *mul_expand(Expr *a, Expr *b)
{
    tick();
    if (a->k == E_ADD || b->k == E_ADD)
    {
        Expr *s = a->k == E_ADD ? a : b;
        Expr *o = a->k == E_ADD ? b : a;
        Expr **v = (Expr **)amalloc(sizeof(Expr *) * s->n);
        for (int i = 0; i < s->n; i++)
            v[i] = mul_expand(s->a[i], o);
        return s_add(v, s->n);
    }
    return mul2(a, b);
}

static Expr *poly_power(Expr *b, long long k);

/* Vrai si EX changerait e : un produit contient une somme, ou une somme est
 * elevee a une puissance entiere positive. Sans allocation : EX recopiait
 * sinon tout un polynome deja developpe (best() apres developper()). */
static bool needs_expand(const Expr *e)
{
    switch (e->k)
    {
    case E_ADD:
    case E_FUN:
        break;
    case E_MUL:
        for (int i = 0; i < e->n; i++)
            if (e->a[i]->k == E_ADD)
                return true;
        break;
    case E_POW:
        if (e->a[0]->k == E_ADD && is_num(e->a[1]) && q_isint(e->a[1]->q) && q_sign(e->a[1]->q) > 0)
            return true;
        return needs_expand(e->a[0]) || needs_expand(e->a[1]);
    default:
        return false;
    }
    int n = e->k == E_FUN ? 1 : e->n;
    for (int i = 0; i < n; i++)
        if (needs_expand(e->a[i]))
            return true;
    return false;
}

static Expr *EX(Expr *e)
{
    tick();
    if (!needs_expand(e))
        return e;
    switch (e->k)
    {
    case E_ADD:
    {
        Expr **v = (Expr **)amalloc(sizeof(Expr *) * e->n);
        for (int i = 0; i < e->n; i++)
            v[i] = EX(e->a[i]);
        return s_add(v, e->n);
    }
    case E_MUL:
    {
        Expr *r = EX(e->a[0]);
        for (int i = 1; i < e->n; i++)
            r = mul_expand(r, EX(e->a[i]));
        return r;
    }
    case E_POW:
    {
        Expr *b = EX(e->a[0]);
        Expr *x = EX(e->a[1]);
        if (b->k == E_ADD && is_num(x) && q_isint(x->q) && q_sign(x->q) > 0)
        {
            long long k;
            if (!big_fits_ll(x->q.n, &k))
                return s_pow(b, x);
            /* Polynome a coefficients rationnels : calcul direct des
             * coefficients (la boucle ci-dessous saturait la memoire des
             * (X+1)^(6)). */
            Expr *pp = poly_power(b, k);
            if (pp)
                return pp;
            if (k > 40)
                return s_pow(b, x);
            Expr *r = b;
            for (long long i = 1; i < k; i++)
                r = mul_expand(r, b);
            return r;
        }
        return s_pow(b, x);
    }
    case E_FUN:
        return s_fun(e->f, EX(e->a[0]));
    default:
        return e;
    }
}

/* La forme la plus courte entre l'expression et sa forme developpee. */
/* Puissance entiere d'une somme d'exposant >= lim quelque part dans e. */
static bool has_sum_power(const Expr *e, long long lim)
{
    if (e->k == E_POW && e->a[0]->k == E_ADD && is_num(e->a[1]) && q_isint(e->a[1]->q))
    {
        long long k;
        if (!big_fits_ll(e->a[1]->q.n, &k) || k >= lim || k <= -lim)
            return true;
    }
    if (e->k != E_ADD && e->k != E_MUL && e->k != E_POW && e->k != E_FUN)
        return false;
    int n = e->k == E_FUN ? 1 : e->n;
    for (int i = 0; i < n; i++)
        if (has_sum_power(e->a[i], lim))
            return true;
    return false;
}

static Expr *best(Expr *e)
{
    /* Produit de facteurs, dont une somme a une puissance >= 4 : la forme
     * developpee est toujours plus longue, inutile de la calculer (elle
     * epuisait la memoire pour deriver((X+1)^(30))). */
    if (e->k != E_ADD && has_sum_power(e, 4))
        return e;
    Expr *x = EX(e);
    return esize(x) <= esize(e) ? x : e;
}

#define POLY_MAX 24

/* Degre en X et coefficient d'un terme (pas une somme). */
static bool term_deg(Expr *t, int *deg, Expr **coef)
{
    if (!has_x(t))
    {
        *deg = 0;
        *coef = t;
        return true;
    }
    if (is_sym(t, 'X'))
    {
        *deg = 1;
        *coef = inum(1);
        return true;
    }
    if (t->k == E_POW && is_sym(t->a[0], 'X') && is_num(t->a[1]) && q_isint(t->a[1]->q))
    {
        long long k;
        if (!big_fits_ll(t->a[1]->q.n, &k) || k < 1 || k > POLY_MAX)
            return false;
        *deg = (int)k;
        *coef = inum(1);
        return true;
    }
    if (t->k == E_MUL)
    {
        int d = 0;
        List c;
        l_init(&c, t->n);
        for (int i = 0; i < t->n; i++)
        {
            int di;
            Expr *ci;
            if (!has_x(t->a[i]))
                l_push(&c, t->a[i]);
            else if (term_deg(t->a[i], &di, &ci) && is_val(ci, 1))
                d += di;
            else
                return false;
        }
        if (d > POLY_MAX)
            return false;
        *deg = d;
        *coef = c.n ? s_mul(c.v, c.n) : inum(1);
        return true;
    }
    return false;
}

/* Coefficients c[0..POLY_MAX] d'un polynome en X (expression developpee). */
static bool poly_of(Expr *e, Expr **c, int *deg)
{
    for (int i = 0; i <= POLY_MAX; i++)
        c[i] = inum(0);
    int nt = e->k == E_ADD ? e->n : 1;
    for (int i = 0; i < nt; i++)
    {
        Expr *t = e->k == E_ADD ? e->a[i] : e;
        int d;
        Expr *co;
        if (!term_deg(t, &d, &co))
            return false;
        c[d] = add2(c[d], co);
    }
    *deg = 0;
    for (int i = POLY_MAX; i >= 0; i--)
        if (!is_val(c[i], 0))
        {
            *deg = i;
            break;
        }
    return true;
}

static Expr *poly_expr(Expr **c, int deg)
{
    List t;
    l_init(&t, deg + 1);
    for (int i = 0; i <= deg; i++)
        if (!is_val(c[i], 0))
            l_push(&t, mul2(c[i], i == 0 ? inum(1) : (i == 1 ? sym('X') : s_pow(sym('X'), inum(i)))));
    if (!t.n)
        return inum(0);
    return s_add(t.v, t.n);
}

/* b^k developpe, pour b polynome en X a coefficients rationnels, ou NULL.
 * Recurrence de J.C.P. Miller : si P = a0 + a1 X + ... + ad X^d (a0 != 0)
 * et P^k = somme c_n X^n, alors c_0 = a0^k et
 *   c_n = 1/(n a0) * somme_{i=1..min(n,d)} ((k+1) i - n) a_i c_(n-i).
 * Environ k*d^2 operations sur des fractions, au lieu de construire et
 * fusionner tous les produits intermediaires. */
static Expr *poly_power(Expr *b, long long k)
{
    Expr *c[POLY_MAX + 1];
    int d;
    if (k < 2 || !has_x(b) || !poly_of(b, c, &d) || d < 1)
        return NULL;
    for (int i = 0; i <= d; i++)
        if (!is_num(c[i]))
            return NULL;
    int s = 0; /* plus petite puissance de X presente */
    while (q_sign(c[s]->q) == 0)
        s++;
    int dd = d - s;
    if ((long long)dd * k > 200 || (long long)d * k > 400)
        return NULL;
    int D = (int)(dd * k);
    Q *a = (Q *)amalloc(sizeof(Q) * (dd + 1));
    for (int i = 0; i <= dd; i++)
        a[i] = c[s + i]->q;
    Q *C = (Q *)amalloc(sizeof(Q) * (D + 1));
    C[0] = q_pow_int(a[0], k);
    for (int n = 1; n <= D; n++)
    {
        tick();
        Q acc = q_int(0);
        for (int i = 1; i <= dd && i <= n; i++)
        {
            if (q_sign(a[i]) == 0)
                continue;
            long long f = (k + 1) * i - n;
            if (f == 0)
                continue;
            acc = q_add(acc, q_mul(q_mul(q_int(f), a[i]), C[n - i]));
        }
        C[n] = q_div(acc, q_mul(q_int(n), a[0]));
    }
    /* Termes deja distincts et simplifies : on assemble la somme comme la
     * fin de s_add (constante en tete, termes tries), sans la refaire. */
    List t;
    l_init(&t, D + 1);
    bool has_c = false;
    for (int n = 0; n <= D; n++)
    {
        if (q_sign(C[n]) == 0)
            continue;
        long long e = s * k + n;
        if (e == 0)
        {
            has_c = true;
            continue;
        }
        Expr *xp = e == 1 ? sym('X') : s_pow(sym('X'), inum(e));
        l_push(&t, with_coef(C[n], xp));
    }
    esort(t.v, t.n);
    if (t.n == 0)
        return has_c ? num(C[0]) : inum(0);
    if (t.n == 1 && !has_c)
        return t.v[0];
    Expr *r = node(E_ADD, t.n + (has_c ? 1 : 0));
    int j = 0;
    if (has_c)
        r->a[j++] = num(C[0]);
    for (int i = 0; i < t.n; i++)
        r->a[j++] = t.v[i];
    return r;
}

/* Diviseurs positifs d'un entier <= 10^12 (au plus cap). */
static int divisors(unsigned long long a, unsigned long long *out, int cap)
{
    Factors f;
    if (!factorize(a, &f))
        return -1;
    int n = 1;
    out[0] = 1;
    for (int i = 0; i < f.n; i++)
    {
        int cur = n;
        unsigned long long pk = 1;
        for (int k = 1; k <= f.e[i]; k++)
        {
            pk *= f.p[i];
            for (int j = 0; j < cur; j++)
            {
                if (n >= cap)
                    return -1;
                out[n++] = out[j] * pk;
            }
        }
    }
    return n;
}

/* P(r) pour un polynome a coefficients entiers P[0..deg]. */
static Q poly_at(Q *P, int deg, Q r)
{
    Q v = q_int(0);
    for (int i = deg; i >= 0; i--)
        v = q_add(q_mul(v, r), P[i]);
    return v;
}

/* Divise P par (X - r) : P[0..deg] devient le quotient de degre deg-1. */
static void poly_deflate(Q *P, int deg, Q r)
{
    Q carry = q_int(0);
    Q *Qd = (Q *)amalloc(sizeof(Q) * (deg + 1));
    for (int i = deg; i >= 1; i--)
    {
        carry = q_add(q_mul(carry, r), P[i]);
        Qd[i - 1] = carry;
    }
    for (int i = 0; i < deg; i++)
        P[i] = Qd[i];
}

/* Racines rationnelles de P (coefficients rationnels) avec multiplicite.
 * P est divise par chaque facteur trouve ; *deg diminue d'autant. */
static int rational_roots(Q *P, int *deg, Q *roots, int *mult, int cap)
{
    int nr = 0;
    /* Coefficients entiers : multiplier par le ppcm des denominateurs. */
    Big L = big_ll(1);
    for (int i = 0; i <= *deg; i++)
    {
        Big g = big_gcd(L, P[i].d);
        Big q, r;
        big_divmod(big_mul(L, P[i].d), g, &q, &r);
        L = q;
    }
    Q Lq;
    Lq.n = L;
    Lq.d = big_ll(1);
    for (int i = 0; i <= *deg; i++)
        P[i] = q_mul(P[i], Lq);
    /* Racine 0. */
    while (*deg > 0 && q_sign(P[0]) == 0)
    {
        if (nr == 0 || q_sign(roots[nr - 1]) != 0)
        {
            if (nr >= cap)
                return nr;
            roots[nr] = q_int(0);
            mult[nr] = 0;
            nr++;
        }
        mult[nr - 1]++;
        for (int i = 0; i < *deg; i++)
            P[i] = P[i + 1];
        (*deg)--;
    }
    if (*deg < 1)
        return nr;
    unsigned long long a0, an;
    if (!big_fits_u64(big_abs(P[0].n), &a0) || !big_fits_u64(big_abs(P[*deg].n), &an))
        return nr;
    /* tableaux dans la zone de calcul : pas de RAM permanente */
    unsigned long long *dp = (unsigned long long *)amalloc(sizeof(unsigned long long) * 512);
    unsigned long long *dq = (unsigned long long *)amalloc(sizeof(unsigned long long) * 512);
    int np = divisors(a0, dp, 512);
    int nq = divisors(an, dq, 512);
    if (np < 0 || nq < 0)
        return nr;
    for (int i = 0; i < np && *deg >= 1; i++)
    {
        for (int j = 0; j < nq && *deg >= 1; j++)
        {
            if (gcd_u64(dp[i], dq[j]) != 1)
                continue;
            for (int s = 1; s >= -1; s -= 2)
            {
                /* Un candidat ecarte ne laisse rien dans la zone : avec les
                 * 169 diviseurs de 10^12, X^3-10^12 la remplissait. */
                size_t m0 = g_used;
                Q r = q_frac(s * (long long)dp[i], (long long)dq[j]);
                bool first = true;
                while (*deg >= 1)
                {
                    size_t m1 = g_used;
                    int sg = q_sign(poly_at(P, *deg, r));
                    g_used = m1;
                    if (sg != 0)
                        break;
                    if (first)
                    {
                        if (nr >= cap)
                            return nr;
                        roots[nr] = r;
                        mult[nr] = 0;
                        nr++;
                        first = false;
                    }
                    mult[nr - 1]++;
                    poly_deflate(P, *deg, r);
                    (*deg)--;
                }
                if (first)
                    g_used = m0;
            }
        }
    }
    return nr;
}

/* Entier (ou fraction) en facteurs premiers : 360 = 2^3*3^2*5. */
static Expr *factor_int_expr(Big a)
{
    unsigned long long u;
    Factors f;
    if (!big_fits_u64(big_abs(a), &u) || u < 2 || !factorize(u, &f))
    {
        Q q;
        q.n = a;
        q.d = big_ll(1);
        return num(q);
    }
    List l;
    l_init(&l, f.n + 1);
    if (a.neg)
        l_push(&l, inum(-1));
    for (int i = 0; i < f.n; i++)
    {
        Expr *p = inum((long long)f.p[i]);
        l_push(&l, f.e[i] == 1 ? p : rpow(p, inum(f.e[i])));
    }
    if (l.n == 1)
        return l.v[0];
    return rlist(E_MUL, l.v, l.n);
}

/* Diviseurs positifs de v (au plus cap), ou -1 si v est trop grand. */
static int divisors_ll(unsigned long long v, long long *out, int cap)
{
    if (v == 0 || v > 1000000000000ULL)
        return -1;
    Factors f;
    if (v == 1)
    {
        out[0] = 1;
        return 1;
    }
    if (!factorize(v, &f))
        return -1;
    int n = 1;
    out[0] = 1;
    for (int i = 0; i < f.n; i++)
    {
        int cur = n;
        long long pk = 1;
        for (int k = 1; k <= f.e[i]; k++)
        {
            pk *= (long long)f.p[i];
            for (int j = 0; j < cur; j++)
            {
                if (n >= cap)
                    return -1;
                out[n++] = out[j] * pk;
            }
        }
    }
    return n;
}

/* R (degre d, coefficients entiers) divise exactement par aX^2+bX+c ?
 * Quotient dans out (degre d-2). */
static bool pdiv_quad(const long long *R, int d, long long a, long long b, long long c, long long *out)
{
    long long r[POLY_MAX + 1];
    for (int i = 0; i <= d; i++)
        r[i] = R[i];
    for (int i = d; i >= 2; i--)
    {
        if (r[i] % a)
            return false;
        long long q = r[i] / a;
        out[i - 2] = q;
        r[i] -= q * a;
        r[i - 1] -= q * b;
        r[i - 2] -= q * c;
        if (r[i - 1] > 4000000000000000LL || r[i - 1] < -4000000000000000LL ||
            r[i - 2] > 4000000000000000LL || r[i - 2] < -4000000000000000LL)
            return false;
    }
    return r[1] == 0 && r[0] == 0;
}

static long long ll_eval(const long long *R, int d, long long x)
{
    long long v = 0;
    for (int i = d; i >= 0; i--)
        v = v * x + R[i];
    return v;
}

/* Facteur aX^2+bX+c (a > 0) de R sans racine rationnelle. Les contraintes
 * a | dominant, c | constant, (a+b+c) | R(1) et (a-b+c) | R(-1) limitent
 * la recherche (methode de Kronecker). */
static bool find_quad(const long long *R, int d, long long *qa, long long *qb, long long *qc)
{
    static long long da[64], dc[64], d1[128];
    long long lead = R[d], c0 = R[0];
    long long r1 = ll_eval(R, d, 1), rm1 = ll_eval(R, d, -1);
    if (c0 == 0 || r1 == 0 || rm1 == 0)
        return false;
    int na = divisors_ll((unsigned long long)(lead < 0 ? -lead : lead), da, 64);
    int nc = divisors_ll((unsigned long long)(c0 < 0 ? -c0 : c0), dc, 64);
    int n1 = divisors_ll((unsigned long long)(r1 < 0 ? -r1 : r1), d1, 128);
    if (na < 0 || nc < 0 || n1 < 0)
        return false;
    long long quo[POLY_MAX + 1];
    for (int i = 0; i < na; i++)
        for (int j = 0; j < nc; j++)
            for (int sc = -1; sc <= 1; sc += 2)
                for (int k = 0; k < n1; k++)
                    for (int s1 = -1; s1 <= 1; s1 += 2)
                    {
                        tick();
                        long long a = da[i], c = sc * dc[j];
                        long long b = s1 * d1[k] - a - c;
                        long long m = a - b + c;
                        if (m == 0 || rm1 % m)
                            continue;
                        long long disc = b * b - 4 * a * c;
                        if (disc >= 0)
                            continue; /* racines reelles : deja traitees ou irrationnelles */
                        if (pdiv_quad(R, d, a, b, c, quo))
                        {
                            *qa = a;
                            *qb = b;
                            *qc = c;
                            return true;
                        }
                    }
    return false;
}

/* Meme recherche pour un facteur aX^2+c a racines irrationnelles reelles
 * (X^4-4 = (X^2-2)(X^2+2)) : polynome pair, vu comme polynome en Y = X^2. */
static bool find_quad_even(const long long *R, int d, long long *qa, long long *qb, long long *qc)
{
    if (d % 2)
        return false;
    for (int i = 1; i <= d; i += 2)
        if (R[i] != 0)
            return false;
    /* racines rationnelles de S(Y) = sum R[2i] Y^i : facteur (pY - q) */
    long long lead = R[d], c0 = R[0];
    static long long dp[64], dq[64];
    int np = divisors_ll((unsigned long long)(lead < 0 ? -lead : lead), dp, 64);
    int nq = divisors_ll((unsigned long long)(c0 < 0 ? -c0 : c0), dq, 64);
    if (np < 0 || nq < 0 || c0 == 0)
        return false;
    long long quo[POLY_MAX + 1];
    for (int i = 0; i < np; i++)
        for (int j = 0; j < nq; j++)
            for (int sq = -1; sq <= 1; sq += 2)
            {
                tick();
                long long pp = dp[i], qq = sq * dq[j];
                if (pdiv_quad(R, d, pp, 0, -qq, quo))
                {
                    *qa = pp;
                    *qb = 0;
                    *qc = -qq;
                    return true;
                }
            }
    return false;
}

static Expr *factor_any(Expr *e)
{
    if (is_num(e))
    {
        if (q_isint(e->q))
            return factor_int_expr(e->q.n);
        Expr *v[2] = {factor_int_expr(e->q.n), rpow(factor_int_expr(e->q.d), inum(-1))};
        return rlist(E_MUL, v, 2);
    }
    Expr *c[POLY_MAX + 1];
    int deg;
    Expr *ex = EX(e);
    if (!has_x(ex) || !poly_of(ex, c, &deg) || deg < 1)
        return best(e);
    Q *P = (Q *)amalloc(sizeof(Q) * (deg + 1));
    for (int i = 0; i <= deg; i++)
    {
        if (!is_num(c[i]))
            return best(e); /* coefficients irrationnels : pas de factorisation */
        P[i] = c[i]->q;
    }
    /* Contenu : P = content * (polynome entier primitif, dominant > 0). */
    Q orig_lead = P[deg];
    Q roots[POLY_MAX];
    int mult[POLY_MAX];
    int d = deg;
    int nr = rational_roots(P, &d, roots, mult, POLY_MAX);
    /* Le quotient R (coefficients rationnels) est rendu primitif : R = g R'
     * avec R' a coefficients entiers premiers entre eux, dominant > 0. */
    Big Ld = big_ll(1), G = big_alloc(0);
    for (int i = 0; i <= d; i++)
    {
        Big gg = big_gcd(Ld, P[i].d);
        Big qq, rr;
        big_divmod(big_mul(Ld, P[i].d), gg, &qq, &rr);
        Ld = qq;
    }
    for (int i = 0; i <= d; i++)
    {
        Q t = P[i];
        Big qq, rr;
        big_divmod(big_mul(t.n, Ld), t.d, &qq, &rr);
        G = big_gcd(G, qq);
    }
    Q gq = q_make(q_sign(P[d]) < 0 ? big_neg(G) : G, Ld);
    for (int i = 0; i <= d; i++)
        P[i] = q_div(P[i], gq);
    /* Chaque racine p/q donne le facteur (qX - p). Le contenu est le
     * coefficient qui redonne le coefficient dominant d'origine. */
    Q lead_prod = P[d];
    List fl;
    l_init(&fl, nr + 3);
    for (int i = 0; i < nr; i++)
    {
        Expr *lin;
        if (q_sign(roots[i]) == 0)
            lin = sym('X');
        else
        {
            Q qd;
            qd.n = roots[i].d;
            qd.d = big_ll(1);
            Q pn;
            pn.n = roots[i].n;
            pn.d = big_ll(1);
            lin = add2(mul2(num(qd), sym('X')), num(q_neg(pn)));
            lead_prod = q_mul(lead_prod, q_pow_int(qd, mult[i]));
        }
        l_push(&fl, mult[i] == 1 ? lin : rpow(lin, inum(mult[i])));
    }
    /* OpenCalc : facteurs du second degre a coefficients entiers dans le
     * reste sans racine rationnelle (X^4+4, X^4-4, X^4+X^2+1...). */
    int nquad = 0;
    if (d >= 4)
    {
        long long R[POLY_MAX + 1];
        bool fits = true;
        for (int i = 0; i <= d && fits; i++)
            fits = q_isint(P[i]) && big_fits_ll(P[i].n, &R[i]) && R[i] < 1000000000000LL &&
                   R[i] > -1000000000000LL;
        while (fits && d >= 4)
        {
            long long a, b, cq;
            if (!find_quad(R, d, &a, &b, &cq) && !find_quad_even(R, d, &a, &b, &cq))
                break;
            long long quo[POLY_MAX + 1];
            pdiv_quad(R, d, a, b, cq, quo);
            Expr *qc3[3] = {inum(cq), inum(b), inum(a)};
            l_push(&fl, poly_expr(qc3, 2));
            /* lead_prod contient deja a (dominant du reste avant division) */
            d -= 2;
            for (int i = 0; i <= d; i++)
            {
                R[i] = quo[i];
                P[i] = q_int(quo[i]);
            }
            nquad++;
        }
    }
    if (d >= 1)
    {
        Expr **cc = (Expr **)amalloc(sizeof(Expr *) * (d + 1));
        for (int i = 0; i <= d; i++)
            cc[i] = num(P[i]);
        l_push(&fl, poly_expr(cc, d));
    }
    Q content = q_div(orig_lead, lead_prod);
    /* rien a factoriser (ni racine, ni facteur, ni contenu) : tel quel */
    if (nr == 0 && nquad == 0 && q_is(content, 1))
        return best(e);
    List out;
    l_init(&out, fl.n + 1);
    if (!q_is(content, 1))
        l_push(&out, num(content));
    for (int i = 0; i < fl.n; i++)
        l_push(&out, fl.v[i]);
    if (out.n == 1)
        return out.v[0];
    return rlist(E_MUL, out.v, out.n);
}

/* ===================== 6. Deriver, primitive, resoudre =================== */

/* Facteur des fonctions trigonometriques en mode degres : pi/180. */
static Expr *deg_in(void)
{
    if (!g_angle_mode)
        return inum(1);
    return mul2(qnum(1, 180), sym('p'));
}

static Expr *deg_out(void)
{
    if (!g_angle_mode)
        return inum(1);
    return mul2(inum(180), s_pow(sym('p'), inum(-1)));
}

static Expr *D(Expr *e)
{
    tick();
    if (!has_x(e))
        return inum(0);
    switch (e->k)
    {
    case E_SYM:
        return inum(1);
    case E_ADD:
    {
        Expr **v = (Expr **)amalloc(sizeof(Expr *) * e->n);
        for (int i = 0; i < e->n; i++)
            v[i] = D(e->a[i]);
        return s_add(v, e->n);
    }
    case E_MUL:
    {
        Expr **t = (Expr **)amalloc(sizeof(Expr *) * e->n);
        for (int i = 0; i < e->n; i++)
        {
            Expr **f = (Expr **)amalloc(sizeof(Expr *) * e->n);
            for (int j = 0; j < e->n; j++)
                f[j] = j == i ? D(e->a[j]) : e->a[j];
            t[i] = s_mul(f, e->n);
        }
        return s_add(t, e->n);
    }
    case E_POW:
    {
        Expr *b = e->a[0], *x = e->a[1];
        if (!has_x(x))
        {
            Expr *v[3] = {x, s_pow(b, add2(x, inum(-1))), D(b)};
            return s_mul(v, 3);
        }
        if (!has_x(b))
        {
            Expr *v[3] = {e, s_fun(F_LN, b), D(x)};
            return s_mul(v, 3);
        }
        return mul2(e, D(mul2(x, s_fun(F_LN, b))));
    }
    case E_FUN:
    {
        Expr *u = e->a[0];
        Expr *g;
        switch (e->f)
        {
        case F_SIN:
            g = mul2(s_fun(F_COS, u), deg_in());
            break;
        case F_COS:
            g = neg(mul2(s_fun(F_SIN, u), deg_in()));
            break;
        case F_TAN:
            g = mul2(s_pow(s_fun(F_COS, u), inum(-2)), deg_in());
            break;
        case F_ASIN:
            g = mul2(s_pow(sub2(inum(1), s_pow(u, inum(2))), qnum(-1, 2)), deg_out());
            break;
        case F_ACOS:
            g = neg(mul2(s_pow(sub2(inum(1), s_pow(u, inum(2))), qnum(-1, 2)), deg_out()));
            break;
        case F_ATAN:
            g = mul2(s_pow(add2(inum(1), s_pow(u, inum(2))), inum(-1)), deg_out());
            break;
        case F_LN:
            g = s_pow(u, inum(-1));
            break;
        case F_LOG:
            g = s_pow(mul2(u, s_fun(F_LN, inum(10))), inum(-1));
            break;
        case F_ABS:
            g = div2(u, e);
            break;
        case F_SINH:
            g = s_fun(F_COSH, u);
            break;
        case F_COSH:
            g = s_fun(F_SINH, u);
            break;
        case F_TANH:
            g = s_pow(s_fun(F_COSH, u), inum(-2));
            break;
        case F_ASINH:
            g = s_pow(add2(s_pow(u, inum(2)), inum(1)), qnum(-1, 2));
            break;
        case F_ACOSH:
            g = s_pow(sub2(s_pow(u, inum(2)), inum(1)), qnum(-1, 2));
            break;
        case F_ATANH:
            g = s_pow(sub2(inum(1), s_pow(u, inum(2))), inum(-1));
            break;
        default:
            fail("Derivee non disponible");
        }
        return mul2(g, D(u));
    }
    default:
        fail("Derivee non disponible");
    }
}

/* u = aX + b ? */
static bool linear(Expr *u, Expr **a, Expr **b)
{
    Expr *c[POLY_MAX + 1];
    int deg;
    if (!poly_of(EX(u), c, &deg) || deg != 1)
        return false;
    *a = c[1];
    *b = c[0];
    return true;
}

static Expr *P(Expr *e, int depth);

static Expr *P_basic(Expr *e, int depth)
{
    tick();
    if (depth > 6)
        return NULL;
    Expr *X = sym('X');
    if (!has_x(e))
        return mul2(e, X);
    switch (e->k)
    {
    case E_SYM:
        return mul2(qnum(1, 2), s_pow(X, inum(2)));
    case E_ADD:
    {
        Expr **v = (Expr **)amalloc(sizeof(Expr *) * e->n);
        for (int i = 0; i < e->n; i++)
            if (!(v[i] = P(e->a[i], depth + 1)))
                return NULL;
        return s_add(v, e->n);
    }
    case E_MUL:
    {
        List c, g;
        l_init(&c, e->n);
        l_init(&g, e->n);
        for (int i = 0; i < e->n; i++)
            l_push(has_x(e->a[i]) ? &g : &c, e->a[i]);
        if (c.n)
        {
            Expr *r = P(g.n == 1 ? g.v[0] : s_mul(g.v, g.n), depth + 1);
            return r ? mul2(s_mul(c.v, c.n), r) : NULL;
        }
        Expr *x = EX(e);
        if (!eq(x, e))
            return P(x, depth + 1);
        return NULL;
    }
    case E_POW:
    {
        Expr *b = e->a[0], *x = e->a[1], *a, *bb;
        if (!has_x(x) && linear(b, &a, &bb))
        {
            if (is_val(x, -1))
                return div2(s_fun(F_LN, s_fun(F_ABS, b)), a);
            Expr *n1 = add2(x, inum(1));
            return div2(s_pow(b, n1), mul2(n1, a));
        }
        if (!has_x(b) && linear(x, &a, &bb))
        {
            if (is_sym(b, 'e'))
                return div2(e, a);
            return div2(e, mul2(a, s_fun(F_LN, b)));
        }
        Expr *ex = EX(e);
        if (!eq(ex, e))
            return P(ex, depth + 1);
        return NULL;
    }
    case E_FUN:
    {
        Expr *u = e->a[0], *a, *bb;
        if (!linear(u, &a, &bb))
            return NULL;
        Expr *ak = mul2(a, deg_in());
        switch (e->f)
        {
        case F_SIN:
            return div2(neg(s_fun(F_COS, u)), ak);
        case F_COS:
            return div2(s_fun(F_SIN, u), ak);
        case F_TAN:
            return div2(neg(s_fun(F_LN, s_fun(F_ABS, s_fun(F_COS, u)))), ak);
        case F_LN:
            return div2(sub2(mul2(u, s_fun(F_LN, u)), u), a);
        case F_SINH:
            return div2(s_fun(F_COSH, u), a);
        case F_COSH:
            return div2(s_fun(F_SINH, u), a);
        default:
            return NULL;
        }
    }
    default:
        return NULL;
    }
}

/* Polynome a coefficients rationnels ? (coefficients dans c, degre dans *deg) */
static bool qpoly(Expr *e, Expr **c, int *deg)
{
    if (!poly_of(EX(e), c, deg))
        return false;
    for (int i = 0; i <= *deg; i++)
        if (!is_num(c[i]))
            return false;
    return true;
}

/* Regles supplementaires de primitive (OpenCalc) :
 *   k u'/u                 -> k ln|u|
 *   1/(aX^2+bX+c), b^2<4ac -> 2/s atan((2aX+b)/s), s = racine(4ac-b^2)
 *   P(X) e^(aX+b), P(X) sin(aX+b), P(X) cos(aX+b) : par parties repetees
 *   P(X) ln(aX)            -> Q ln(aX) - primitive(Q/X), Q primitive de P */
static Expr *P_extra(Expr *e, int depth)
{
    int n = e->k == E_MUL ? e->n : 1;
    Expr **f = e->k == E_MUL ? e->a : &e;
    List cst, num_f, den;
    l_init(&cst, n);
    l_init(&num_f, n);
    l_init(&den, n);
    for (int i = 0; i < n; i++)
    {
        Expr *t = f[i];
        if (!has_x(t))
            l_push(&cst, t);
        else if (t->k == E_POW && is_val(t->a[1], -1))
            l_push(&den, t->a[0]);
        else
            l_push(&num_f, t);
    }
    Expr *C = cst.n ? s_mul(cst.v, cst.n) : inum(1);
    Expr *N = num_f.n ? (num_f.n == 1 ? num_f.v[0] : s_mul(num_f.v, num_f.n)) : inum(1);
    Expr *c1[POLY_MAX + 1], *c2[POLY_MAX + 1];
    int d1, d2;

    if (den.n == 1)
    {
        Expr *Dn = den.v[0];
        /* k u'/u */
        Expr *dD = D(Dn);
        if (qpoly(N, c1, &d1) && qpoly(dD, c2, &d2) && d1 == d2 && !is_val(c2[d2], 0))
        {
            Q k = q_div(c1[d1]->q, c2[d2]->q);
            bool ok = true;
            for (int i = 0; i <= d1 && ok; i++)
                ok = q_sign(q_sub(c1[i]->q, q_mul(k, c2[i]->q))) == 0;
            if (ok)
                return mul2(mul2(C, num(k)), s_fun(F_LN, s_fun(F_ABS, Dn)));
        }
        else
        {
            /* cas general : N / u' constant (e^X/(e^X+1), cos(X)/sin(X)) */
            Expr *ratio = best(div2(N, dD));
            if (ratio && !has_x(ratio))
                return mul2(mul2(C, ratio), s_fun(F_LN, s_fun(F_ABS, Dn)));
        }
        /* 1 / (aX^2 + bX + c) sans racine reelle */
        if (!num_f.n && qpoly(Dn, c1, &d1) && d1 == 2)
        {
            Q a = c1[2]->q, b = c1[1]->q, c = c1[0]->q;
            Q disc = q_sub(q_mul(q_int(4), q_mul(a, c)), q_mul(b, b)); /* 4ac - b^2 */
            if (q_sign(disc) > 0)
            {
                Expr *sr = s_pow(num(disc), qnum(1, 2));
                Expr *arg = div2(add2(mul2(num(q_mul(q_int(2), a)), sym('X')), num(b)), sr);
                Expr *r = mul2(div2(inum(2), sr), s_fun(F_ATAN, arg));
                return mul2(C, div2(r, deg_out()));
            }
        }
        return NULL;
    }
    if (den.n)
        return NULL;
    /* par parties : un facteur transcendant, le reste polynomial */
    int ti = -1;
    for (int i = 0; i < num_f.n; i++)
    {
        Expr *t = num_f.v[i], *a, *b;
        bool tr = (t->k == E_POW && is_sym(t->a[0], 'e') && linear(t->a[1], &a, &b)) ||
                  (t->k == E_FUN && (t->f == F_SIN || t->f == F_COS) && linear(t->a[0], &a, &b)) ||
                  (t->k == E_FUN && t->f == F_LN && linear(t->a[0], &a, &b) && is_val(b, 0));
        if (tr)
        {
            if (ti >= 0)
                return NULL;
            ti = i;
        }
    }
    if (ti < 0 || num_f.n < 2)
        return NULL;
    Expr *T = num_f.v[ti];
    List pl;
    l_init(&pl, num_f.n);
    for (int i = 0; i < num_f.n; i++)
        if (i != ti)
            l_push(&pl, num_f.v[i]);
    Expr *Pp = pl.n == 1 ? pl.v[0] : s_mul(pl.v, pl.n);
    if (!qpoly(Pp, c1, &d1) || d1 < 1 || d1 > 12)
        return NULL;
    Pp = poly_expr(c1, d1);
    if (T->k == E_FUN && T->f == F_LN)
    {
        Expr *Q = P(Pp, depth + 1);
        if (!Q || !qpoly(Q, c2, &d2) || !is_val(c2[0], 0))
            return NULL;
        for (int i = 0; i < d2; i++)
            c2[i] = c2[i + 1];
        Expr *rest = P(poly_expr(c2, d2 - 1), depth + 1);
        if (!rest)
            return NULL;
        return mul2(C, sub2(mul2(Q, T), rest));
    }
    List terms;
    l_init(&terms, d1 + 2);
    Expr *Fk = T;
    Expr *dk = Pp;
    for (int k = 0; k <= d1; k++)
    {
        Fk = P(Fk, depth + 1);
        if (!Fk)
            return NULL;
        Expr *t = mul2(dk, Fk);
        l_push(&terms, (k % 2) ? neg(t) : t);
        dk = D(dk);
    }
    return mul2(C, s_add(terms.v, terms.n));
}

static Expr *P(Expr *e, int depth)
{
    Expr *r = P_basic(e, depth);
    if (r || depth > 6)
        return r;
    return P_extra(e, depth);
}

/* ---------------------------- Resoudre ----------------------------------- */

#define MAX_SOL 12

struct Sol
{
    Expr *v[MAX_SOL];
    int n;
    bool approx;  /* au moins une solution approchee */
    bool all;     /* tous les reels */
    bool partial; /* plus de MAX_SOL solutions : liste tronquee */
    bool searched;  /* recherche numerique sur un intervalle : rien de prouve */
    bool real_only; /* polynome : seules les racines reelles sont donnees */
    const char *note; /* OpenCalc : "a 2kpi pres"... (equations periodiques) */
};

static void sol_add(Sol *s, Expr *v)
{
    for (int i = 0; i < s->n; i++)
        if (eq(s->v[i], v))
            return;
    if (s->n < MAX_SOL)
        s->v[s->n++] = v;
    else
        s->partial = true;
}

static Expr *dec_num(double v)
{
    /* Valeur approchee gardee comme fraction decimale a 10 chiffres. */
    if (fabs(v) < 1e-12)
        return inum(0);
    double scale = pow(10.0, 9 - (int)floor(log10(fabs(v))));
    /* 10^18 tient dans un long long : 7 chiffres encore pour 10^-12. */
    if (scale > 1e18)
        scale = 1e18;
    double r = round(v * scale);
    if (fabs(r) > 9e15)
        return inum((long long)v);
    return num(q_make(big_ll((long long)r), big_ll((long long)scale)));
}

/* Changement de signe de f entre x0 et x1 : racine par dichotomie. */
static void root_between(Expr *f, double x0, double y0, double x1, double y1, Sol *s)
{
    if (y1 == 0)
    {
        sol_add(s, dec_num(x1));
        s->approx = true;
        return;
    }
    if (!isfinite(y0) || !isfinite(y1) || y0 == 0 || (y0 < 0) == (y1 < 0))
        return;
    double a = x0, b = x1, ya = y0;
    for (int k = 0; k < 80; k++)
    {
        double m = 0.5 * (a + b), ym = nv(f, m);
        if ((ym < 0) == (ya < 0))
        {
            a = m;
            ya = ym;
        }
        else
            b = m;
    }
    double r = 0.5 * (a + b);
    double yr = nv(f, r);
    /* Un saut (tan, 1/x) change aussi de signe : on verifie. */
    if (fabs(yr) < 1e-6 * (1 + fabs(y0) + fabs(y1)))
    {
        sol_add(s, dec_num(r));
        s->approx = true;
    }
}

/* Minimum de |f| sur [a, b] par recherche du nombre d'or. */
static double min_abs(Expr *f, double a, double b)
{
    const double gr = 0.6180339887498949;
    double x1 = b - gr * (b - a), x2 = a + gr * (b - a);
    double g1 = fabs(nv(f, x1)), g2 = fabs(nv(f, x2));
    for (int i = 0; i < 100; i++)
    {
        if (g1 < g2)
        {
            b = x2;
            x2 = x1;
            g2 = g1;
            x1 = b - gr * (b - a);
            g1 = fabs(nv(f, x1));
        }
        else
        {
            a = x1;
            x1 = x2;
            g1 = g2;
            x2 = a + gr * (b - a);
            g2 = fabs(nv(f, x2));
        }
    }
    return 0.5 * (a + b);
}

/* Parcours pas a pas dans un sens : les deux derniers points (xp le plus
 * recent). */
struct Scan
{
    double xq, yq, xp, yp;
    bool have_q;
    int zrun; /* zeros exacts consecutifs finissant en xp */
};

/* Avance jusqu'a x1 et ajoute les racines rencontrees :
 *   - changement de signe entre xp et x1 ;
 *   - bord du domaine entre xp et x1 (ln(X)=-10, r(X-5)=0) : f n'est definie
 *     que d'un cote, on cherche le bord puis une racine entre lui et le
 *     point defini ;
 *   - minimum de |f| nul sans changement de signe (racine double, abs). */
static void scan_step(Expr *f, Scan *sc, double x1, Sol *s)
{
    double y1 = nv(f, x1);
    bool fp = isfinite(sc->yp), f1 = isfinite(y1);
    /* Un zero exact n'est une racine que s'il est isole : X^2*e^X vaut 0
     * partout sous X = -745 (depassement), ce ne sont pas des solutions. */
    if (y1 == 0)
        sc->zrun++;
    else
    {
        if (sc->zrun == 1)
        {
            sol_add(s, dec_num(sc->xp));
            s->approx = true;
        }
        sc->zrun = 0;
    }
    if (fp && f1 && y1 != 0)
    {
        root_between(f, sc->xp, sc->yp, x1, y1, s);
        if (sc->have_q && isfinite(sc->yq) && sc->yp != 0 && y1 != 0 && sc->yq != 0 &&
            (sc->yq < 0) == (sc->yp < 0) && (sc->yp < 0) == (y1 < 0) &&
            fabs(sc->yp) < fabs(sc->yq) && fabs(sc->yp) <= fabs(y1))
        {
            double r = min_abs(f, sc->xq, x1);
            double yr = nv(f, r);
            if (isfinite(yr) && fabs(yr) <= 1e-11 * (fabs(sc->yq) + fabs(y1)))
            {
                /* Un minimum n'est localise qu'a ~1e-8 pres : on ne garde
                 * que 8 chiffres (6.2831853 et non 6.283185318). */
                if (r != 0)
                {
                    double p10 = pow(10.0, 7 - (int)floor(log10(fabs(r))));
                    r = round(r * p10) / p10;
                }
                sol_add(s, dec_num(r));
                s->approx = true;
            }
        }
    }
    else if (fp != f1)
    {
        /* a : cote defini, b : cote non defini. */
        double a = fp ? sc->xp : x1, ya = fp ? sc->yp : y1, b = fp ? x1 : sc->xp;
        double xe = a, ye = ya;
        for (int k = 0; k < 60; k++)
        {
            double m = 0.5 * (xe + b), ym = nv(f, m);
            if (isfinite(ym))
            {
                xe = m;
                ye = ym;
            }
            else
                b = m;
        }
        if (fabs(ye) <= 1e-9 * (1 + fabs(ya)))
        {
            sol_add(s, dec_num(xe));
            s->approx = true;
        }
        else if (xe < a)
            root_between(f, xe, ye, a, ya, s);
        else if (xe > a)
            root_between(f, a, ya, xe, ye, s);
    }
    sc->xq = sc->xp;
    sc->yq = sc->yp;
    sc->have_q = true;
    sc->xp = x1;
    sc->yp = y1;
}

static bool has_trig(const Expr *e)
{
    if (e->k == E_FUN && (e->f == F_SIN || e->f == F_COS || e->f == F_TAN))
        return true;
    for (int i = 0; i < e->n; i++)
        if (has_trig(e->a[i]))
            return true;
    return false;
}

/* Racines reelles approchees de f (variable X). On part de 0 et on s'en
 * eloigne des deux cotes : s'il y a plus de MAX_SOL racines (sin(X)), on garde
 * les plus proches de 0. D'abord [-100, 100] a pas fixe (en degres, une
 * fonction trigonometrique y fait moins d'une periode : l'intervalle est
 * converti), puis jusqu'a +-10^6 a pas geometrique (ln(X)=5, r(X)=11). */
static void numeric_roots(Expr *f, Sol *s)
{
    const int N = 4000;
    const double far = 1e6, ratio = 1.02;
    double hi = 100;
    if (g_angle_mode && has_trig(f))
        hi *= 180.0 / M_PI;
    double h = 2 * hi / N;
    s->searched = true;
    double yc = nv(f, 0.0);
    if (yc == 0)
    {
        sol_add(s, dec_num(0.0));
        s->approx = true;
    }
    /* zrun = 2 au depart : le zero en 0 est deja compte. */
    Scan right = {0, NAN, 0, yc, false, yc == 0 ? 2 : 0}, left = right;
    while ((right.xp < hi || left.xp > -hi) && !s->partial)
    {
        tick();
        if (right.xp < hi)
            scan_step(f, &right, fmin(hi, right.xp + h), s);
        if (left.xp > -hi)
            scan_step(f, &left, fmax(-hi, left.xp - h), s);
    }
    while ((right.xp < far || left.xp > -far) && !s->partial)
    {
        tick();
        if (right.xp < far)
            scan_step(f, &right, fmin(far, right.xp * ratio), s);
        if (left.xp > -far)
            scan_step(f, &left, fmax(-far, left.xp * ratio), s);
    }
}

/* ---- Racines reelles d'un polynome a coefficients reels ----
 * Entre deux zeros consecutifs de la derivee, P est monotone : au plus une
 * racine, donnee par le signe aux extremites. On remonte donc de la derivee
 * d'ordre n-1 (degre 1) jusqu'a P. Un zero de la derivee ou P s'annule est
 * une racine multiple. Rien n'est rate, contrairement a un balayage a pas
 * fixe (racines doubles de X^4-4X^2+4, racines voisines devant la borne). */
static double poly_val(const double *c, int n, double x)
{
    double v = c[n];
    for (int i = n - 1; i >= 0; i--)
        v = v * x + c[i];
    return v;
}

static int poly_real_roots(const double *a, int n, double *out)
{
    double *c = (double *)amalloc(sizeof(double) * (n + 1));
    double *crit = (double *)amalloc(sizeof(double) * (n + 1));
    int nc = 0; /* zeros de la derivee du niveau courant */
    int m = 0;
    for (int j = n - 1; j >= 0; j--)
    {
        /* c = derivee d'ordre j de P, de degre d. */
        int d = n - j;
        for (int i = 0; i <= d; i++)
        {
            double fct = 1;
            for (int k = i + 1; k <= i + j; k++)
                fct *= k;
            c[i] = a[i + j] * fct;
        }
        double B = 0;
        for (int i = 0; i < d; i++)
            B = fmax(B, fabs(c[i] / c[d]));
        B += 1; /* borne de Cauchy */
        m = 0;
        double xl = -B, yl = poly_val(c, d, xl);
        bool rootl = false;
        for (int k = 0; k <= nc; k++)
        {
            tick();
            double xr = k < nc ? crit[k] : B;
            double yr = poly_val(c, d, xr);
            bool rootr = false;
            if (k < nc)
            {
                double scale = 0, pw = 1;
                for (int i = 0; i <= d; i++, pw *= fabs(xr))
                    scale += fabs(c[i]) * pw;
                rootr = fabs(yr) <= 1e-12 * scale;
            }
            if (!rootl && !rootr && (yl < 0) != (yr < 0))
            {
                double lo = xl, hi2 = xr;
                for (int it = 0; it < 200; it++)
                {
                    double mid = 0.5 * (lo + hi2);
                    if (mid <= lo || mid >= hi2)
                        break;
                    if ((poly_val(c, d, mid) < 0) == (yl < 0))
                        lo = mid;
                    else
                        hi2 = mid;
                }
                out[m++] = 0.5 * (lo + hi2);
            }
            if (rootr)
                out[m++] = xr;
            xl = xr;
            yl = yr;
            rootl = rootr;
        }
        for (int i = 0; i < m; i++)
            crit[i] = out[i];
        nc = m;
    }
    return m;
}

static int cmp_sol(const void *a, const void *b)
{
    double x = nv(*(Expr *const *)a, NAN), y = nv(*(Expr *const *)b, NAN);
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* Racine carree d'une constante positive, carres sortis : r(4e^2) = 2e,
 * r(8e) = 2r(2e). s_pow ne touche pas a un produit de constantes
 * symboliques, et X^2 = e^2 donnait X = r(4e^2)/2. */
static Expr *sqrt_const(Expr *v)
{
    int nf = v->k == E_MUL ? v->n : 1;
    List outer, inner;
    l_init(&outer, nf + 1);
    l_init(&inner, nf + 1);
    for (int i = 0; i < nf; i++)
    {
        Expr *t = v->k == E_MUL ? v->a[i] : v;
        if (is_num(t))
        {
            if (q_sign(t->q) <= 0)
                return s_pow(v, qnum(1, 2));
            l_push(&outer, s_pow(t, qnum(1, 2))); /* 8 -> 2r(2) */
            continue;
        }
        Expr *b = t->k == E_POW ? t->a[0] : t;
        Expr *x = t->k == E_POW ? t->a[1] : inum(1);
        long long k;
        if (!(nv(b, NAN) > 0) || !is_num(x) || !q_isint(x->q) || !big_fits_ll(x->q.n, &k))
            return s_pow(v, qnum(1, 2));
        if (k / 2 != 0)
            l_push(&outer, s_pow(b, inum(k / 2)));
        if (k % 2 != 0)
            l_push(&inner, s_pow(b, inum(k % 2)));
    }
    /* Le reste sous une seule racine : 2r(2) r(e) s'ecrit 2r(2e). */
    Q in_num = q_int(1);
    List o2;
    l_init(&o2, outer.n + 1);
    for (int i = 0; i < outer.n; i++)
    {
        Expr *t = outer.v[i];
        int tn = t->k == E_MUL ? t->n : 1;
        for (int j = 0; j < tn; j++)
        {
            Expr *u = t->k == E_MUL ? t->a[j] : t;
            if (u->k == E_POW && is_num(u->a[0]) && is_num(u->a[1]) && !q_isint(u->a[1]->q))
                in_num = q_mul(in_num, u->a[0]->q);
            else
                l_push(&o2, u);
        }
    }
    if (inner.n == 0)
        return mul2(o2.n ? s_mul(o2.v, o2.n) : inum(1), s_pow(num(in_num), qnum(1, 2)));
    l_push(&inner, num(in_num));
    Expr *rad = rpow(s_mul(inner.v, inner.n), qnum(1, 2));
    l_push(&o2, rad);
    return s_mul(o2.v, o2.n);
}

static void solve_quadratic(Expr *a, Expr *b, Expr *c, Sol *s)
{
    Expr *v[3] = {inum(-4), a, c};
    Expr *delta = add2(s_pow(b, inum(2)), s_mul(v, 3));
    double dv = nv(delta, NAN);
    Expr *twoa = mul2(inum(2), a);
    Expr *mb = neg(b);
    /* Discriminant rationnel : test exact (X^2-10^(-20) a deux racines).
     * Sinon (radicaux), nul a l'arrondi pres, relativement a b^2 et 4ac. */
    double bv = nv(b, NAN), acv = nv(a, NAN) * nv(c, NAN);
    if (is_val(delta, 0) ||
        (!is_num(delta) && fabs(dv) <= 1e-13 * (bv * bv + 4 * fabs(acv))))
    {
        sol_add(s, div2(mb, twoa));
        return;
    }
    if (dv > 0)
    {
        Expr *r = is_num(delta) ? s_pow(delta, qnum(1, 2)) : sqrt_const(delta);
        sol_add(s, best(div2(sub2(mb, r), twoa)));
        sol_add(s, best(div2(add2(mb, r), twoa)));
        return;
    }
    /* Solutions complexes conjuguees. */
    Expr *re = div2(mb, twoa);
    Expr *im = s_fun(F_ABS, div2(s_pow(neg(delta), qnum(1, 2)), twoa));
    Expr *ii = mul2(im, sym('i'));
    sol_add(s, add2(re, neg(ii)));
    sol_add(s, add2(re, ii));
}

static void solve_expr(Expr *f, Sol *s);
static bool has_sym_i(const Expr *e);

/* ln(v) pour v rationnel : n ln(b) quand v = b^n (ou 1/b^n) avec b entier
 * (ln(10^50) -> 50 ln(10)) ; sinon ln(v). */
static Expr *ln_of(Expr *v)
{
    if (v->k == E_NUM && q_sign(v->q) > 0)
    {
        bool inv = !q_isint(v->q);
        Q w = inv ? q_div(q_int(1), v->q) : v->q;
        double wv = nv(num(w), NAN);
        if (q_isint(w) && isfinite(wv) && wv > 1)
            for (int n = 200; n >= 2; n--)
            {
                double b = round(pow(wv, 1.0 / n));
                if (b < 2 || b > 1e9)
                    continue;
                if (eq(S(s_pow(inum((long long)b), inum(n))), S(num(w))))
                {
                    Expr *r = mul2(inum(n), s_fun(F_LN, inum((long long)b)));
                    return inv ? neg(r) : r;
                }
            }
    }
    return s_fun(F_LN, v);
}

/* OpenCalc : f = a T + b, T = e^u, k^u, ln(u), log(u), sin(u), cos(u) ou tan(u)
 * (u en X, a et b constants) : on inverse T. Solutions exactes ; pour les
 * fonctions trigonometriques, celles d'une periode, avec "a 2kpi pres". */
static bool solve_inverse(Expr *f, Sol *s)
{
    Expr *ex = S(f);
    int nt = ex->k == E_ADD ? ex->n : 1;
    Expr *T = NULL, *a = inum(1);
    List rest;
    l_init(&rest, nt);
    for (int i = 0; i < nt; i++)
    {
        Expr *t = ex->k == E_ADD ? ex->a[i] : ex;
        if (!has_x(t))
        {
            l_push(&rest, t);
            continue;
        }
        if (T)
            return false;
        /* t = a * T */
        if (t->k == E_MUL)
        {
            List cs;
            l_init(&cs, t->n);
            Expr *tx = NULL;
            for (int j = 0; j < t->n; j++)
            {
                if (has_x(t->a[j]))
                {
                    if (tx)
                        return false;
                    tx = t->a[j];
                }
                else
                    l_push(&cs, t->a[j]);
            }
            T = tx;
            a = cs.n ? s_mul(cs.v, cs.n) : inum(1);
        }
        else
            T = t;
    }
    if (!T)
        return false;
    Expr *b = rest.n ? s_add(rest.v, rest.n) : inum(0);
    Expr *v = S(div2(neg(b), a)); /* T = v */
    double vv = nv(v, NAN);
    if (has_sym_i(v) || !isfinite(vv))
        return false;
    Sol sub;
    memset(&sub, 0, sizeof(sub));
    if (T->k == E_POW && !has_x(T->a[0]) && has_x(T->a[1]))
    {
        /* k^u = v */
        if (vv <= 0)
            return true; /* aucune solution reelle */
        Expr *rhs = is_sym(T->a[0], 'e') ? ln_of(v) : div2(ln_of(v), ln_of(T->a[0]));
        /* 2^X = 8 : X = 3 plutot que ln(8)/ln(2) */
        if (!is_sym(T->a[0], 'e'))
        {
            double kv = nv(T->a[0], NAN);
            double l = log(vv) / log(kv);
            for (int qd = 1; qd <= 12 && isfinite(l); qd++)
            {
                double pn = round(l * qd);
                if (fabs(pn / qd - l) < 1e-9 && fabs(pn) < 1e6)
                {
                    Expr *cand = qnum((long long)pn, qd);
                    if (eq(S(s_pow(T->a[0], cand)), S(v)))
                    {
                        rhs = cand;
                        break;
                    }
                }
            }
        }
        solve_expr(sub2(T->a[1], rhs), &sub);
    }
    else if (T->k == E_FUN && T->f == F_LN)
    {
        solve_expr(sub2(T->a[0], s_pow(sym('e'), v)), &sub);
    }
    else if (T->k == E_FUN && T->f == F_LOG)
    {
        /* log(u) = v : u = 10^v (log(X) = 2 donnait 100 "approche") */
        solve_expr(sub2(T->a[0], s_pow(inum(10), v)), &sub);
    }
    else if (T->k == E_FUN && (T->f == F_SIN || T->f == F_COS || T->f == F_TAN))
    {
        Expr *u = T->a[0], *ua, *ub;
        if (!linear(u, &ua, &ub))
            return false;
        if (T->f != F_TAN && (vv > 1 + 1e-12 || vv < -1 - 1e-12))
            return true; /* aucune solution */
        Expr *base = S(s_fun(T->f == F_SIN ? F_ASIN : (T->f == F_COS ? F_ACOS : F_ATAN), v));
        Expr *half_turn = g_angle_mode ? inum(180) : sym('p');
        List angles;
        l_init(&angles, 2);
        l_push(&angles, base);
        if (T->f == F_SIN)
            l_push(&angles, sub2(half_turn, base));
        else if (T->f == F_COS)
            l_push(&angles, neg(base));
        Expr *per = S(div2(T->f == F_TAN ? half_turn : mul2(inum(2), half_turn), ua));
        double pv = fabs(nv(per, NAN));
        for (int i = 0; i < angles.n; i++)
        {
            Expr *x = S(div2(sub2(angles.v[i], ub), ua));
            /* meme solution a une periode pres (cos X = -1 : -pi et pi) */
            bool dup = false;
            double xv = nv(x, NAN);
            for (int j = 0; j < sub.n && isfinite(pv) && pv > 0; j++)
            {
                double d = fmod(fabs(xv - nv(sub.v[j], NAN)), pv);
                if (d < 1e-9 * pv || pv - d < 1e-9 * pv)
                    dup = true;
            }
            if (!dup)
                sol_add(&sub, x);
        }
        if (T->f == F_TAN)
            s->note = g_angle_mode ? "a k*180 pres" : "a k*pi pres";
        else
            s->note = g_angle_mode ? "a k*360 pres" : "a 2k*pi pres";
        /* dans le cas a != 1 la periode est divisee par a : message general */
        if (!is_val(ua, 1) && !is_val(ua, -1))
            s->note = "solutions d'une periode";
    }
    else
        return false;
    for (int i = 0; i < sub.n; i++)
        sol_add(s, sub.v[i]);
    s->approx = s->approx || sub.approx;
    return true;
}

static void solve_expr(Expr *f, Sol *s)
{
    f = S(f);
    if (!has_x(f))
    {
        if (is_val(f, 0))
            s->all = true;
        return;
    }
    Expr *ex = EX(f);
    /* Denominateurs en X : on multiplie par leur produit, puis on retire les
     * valeurs interdites. */
    List dbase;
    l_init(&dbase, 4);
    /* Tableaux dans la zone du calcul, pas sur la pile : solve_expr se
     * rappelle (inversion d'une fonction, cas d'une valeur absolue) et son
     * cadre depassait 1,5 Ko, pour une pile de 8 Ko sur le Pico. */
    Q *dexp = (Q *)amalloc(sizeof(Q) * 16);
    int nt = ex->k == E_ADD ? ex->n : 1;
    for (int i = 0; i < nt; i++)
    {
        Expr *t = ex->k == E_ADD ? ex->a[i] : ex;
        int nf = t->k == E_MUL ? t->n : 1;
        for (int j = 0; j < nf; j++)
        {
            Expr *fac = t->k == E_MUL ? t->a[j] : t;
            if (fac->k != E_POW || !has_x(fac->a[0]) || !is_num(fac->a[1]) || q_sign(fac->a[1]->q) >= 0)
                continue;
            Q ex_abs = q_neg(fac->a[1]->q);
            int k;
            for (k = 0; k < dbase.n; k++)
                if (eq(dbase.v[k], fac->a[0]))
                    break;
            if (k < dbase.n)
            {
                if (q_cmp(ex_abs, dexp[k]) > 0)
                    dexp[k] = ex_abs;
            }
            else if (dbase.n < 16)
            {
                dexp[dbase.n] = ex_abs;
                l_push(&dbase, fac->a[0]);
            }
        }
    }
    Expr *den = inum(1);
    for (int k = 0; k < dbase.n; k++)
        den = mul2(den, s_pow(dbase.v[k], num(dexp[k])));
    Expr *g = ex;
    if (has_x(den))
    {
        /* Chaque terme est multiplie par le denominateur commun avant de
         * developper : le produit reunit alors (X-2)^(-1) et (X-2). Developper
         * le produit de deux sommes ne le faisait pas, et (X+1)/(X-2) = 3
         * partait en recherche numerique (X = 3.5 "approche"). */
        List tm;
        l_init(&tm, nt);
        for (int i = 0; i < nt; i++)
            l_push(&tm, mul2(ex->k == E_ADD ? ex->a[i] : ex, den));
        g = EX(s_add(tm.v, tm.n));
    }
    Expr *c[POLY_MAX + 1];
    int deg;
    if (!poly_of(g, c, &deg))
    {
        if (solve_inverse(f, s))
            return;
        numeric_roots(f, s);
        return;
    }
    if (deg == 0)
    {
        if (is_val(c[0], 0))
            s->all = true;
        return;
    }
    Sol tmp;
    memset(&tmp, 0, sizeof(tmp));
    bool rational = true;
    for (int i = 0; i <= deg; i++)
        if (!is_num(c[i]))
            rational = false;
    if (rational && deg >= 3)
    {
        Q *Pq = (Q *)amalloc(sizeof(Q) * (deg + 1));
        for (int i = 0; i <= deg; i++)
            Pq[i] = c[i]->q;
        Q *roots = (Q *)amalloc(sizeof(Q) * POLY_MAX);
        int *mult = (int *)amalloc(sizeof(int) * POLY_MAX);
        int d = deg;
        int nr = rational_roots(Pq, &d, roots, mult, POLY_MAX);
        for (int i = 0; i < nr; i++)
            sol_add(&tmp, num(roots[i]));
        for (int i = 0; i <= d; i++)
            c[i] = num(Pq[i]);
        deg = d;
    }
    /* OpenCalc : a X^n + c (racine n-ieme exacte) et a X^4 + b X^2 + c */
    bool pure = rational && deg >= 3 && !is_val(c[0], 0);
    for (int i = 1; i < deg && pure; i++)
        pure = is_val(c[i], 0);
    bool biq = rational && deg == 4 && is_val(c[1], 0) && is_val(c[3], 0);
    if (pure)
    {
        Q v = q_div(q_neg(c[0]->q), c[deg]->q); /* X^n = v */
        if (q_sign(v) > 0)
        {
            Expr *r = s_pow(num(v), qnum(1, deg));
            sol_add(&tmp, r);
            if (deg % 2 == 0)
                sol_add(&tmp, neg(r));
        }
        else if (deg % 2 == 1)
            sol_add(&tmp, neg(s_pow(num(q_neg(v)), qnum(1, deg))));
        tmp.real_only = true;
    }
    else if (biq)
    {
        Sol y;
        memset(&y, 0, sizeof(y));
        solve_quadratic(c[4], c[2], c[0], &y);
        for (int i = 0; i < y.n; i++)
        {
            if (has_sym_i(y.v[i]))
                continue;
            double yv = nv(y.v[i], NAN);
            if (!isfinite(yv) || yv < -1e-15)
                continue;
            if (fabs(yv) < 1e-15)
            {
                sol_add(&tmp, inum(0));
                continue;
            }
            Expr *r = s_pow(y.v[i], qnum(1, 2));
            sol_add(&tmp, r);
            sol_add(&tmp, neg(r));
        }
        tmp.real_only = true;
    }
    else if (deg == 1)
        sol_add(&tmp, neg(div2(c[0], c[1])));
    else if (deg == 2)
        solve_quadratic(c[2], c[1], c[0], &tmp);
    else if (deg >= 3)
    {
        double *pc = (double *)amalloc(sizeof(double) * (deg + 1));
        double *rts = (double *)amalloc(sizeof(double) * (deg + 1));
        bool finite = true;
        for (int i = 0; i <= deg; i++)
        {
            pc[i] = nv(c[i], NAN);
            if (!isfinite(pc[i]))
                finite = false;
        }
        if (!finite || pc[deg] == 0)
            unsupported();
        int nr = poly_real_roots(pc, deg, rts);
        for (int i = 0; i < nr; i++)
            sol_add(&tmp, dec_num(rts[i]));
        if (nr > 0)
            tmp.approx = true;
        /* Moins de racines reelles que le degre : les autres sont complexes
         * (ou multiples) et ne sont pas donnees. */
        if (nr < deg)
            tmp.real_only = true;
    }
    /* Valeurs interdites (denominateurs nuls). */
    for (int i = 0; i < tmp.n; i++)
    {
        if (has_x(den))
        {
            if (is_sym(tmp.v[i], 'i') || has_transc(tmp.v[i]))
            {
                sol_add(s, tmp.v[i]);
                continue;
            }
            double dv = nv(den, nv(tmp.v[i], NAN));
            if (isfinite(dv) && fabs(dv) < 1e-12)
                continue;
        }
        sol_add(s, tmp.v[i]);
    }
    s->approx = s->approx || tmp.approx;
    s->partial = s->partial || tmp.partial;
    s->real_only = tmp.real_only;
}

static bool has_sym_i(const Expr *e)
{
    if (is_sym(e, 'i'))
        return true;
    for (int i = 0; i < e->n; i++)
        if (has_sym_i(e->a[i]))
            return true;
    return false;
}

/* ---------------------------- Inequations -------------------------------- */

/* Solutions reelles d'une inequation, ou de plusieurs relations a une
 * inconnue : reunion d'intervalles et de points isoles, ranges dans l'ordre
 * croissant et sans recouvrement. Tout est alloue dans la zone du calcul. */
struct Bnd
{
    const char *txt; /* valeur exacte, deja ecrite (texte de l'editeur) ; NULL si approx */
    double x;
    bool approx;
    signed char dec; /* chiffres apres la virgule de la valeur exacte, -1 sinon */
};

struct Piece
{
    bool lo_inf, hi_inf; /* borne infinie */
    bool lo_in, hi_in;   /* borne comprise (un point : lo = hi, comprises) */
    Bnd lo, hi;
};

struct Set
{
    Piece *p;
    int n, cap;
};

#define CAS_NOINLINE __attribute__((noinline))

static bool g_set_searched; /* une recherche numerique a servi : rien de prouve */
static bool g_set_cplx;     /* des solutions complexes ont ete ecartees */

static const char *bnd_text(Expr *v, signed char *dec); /* apres pr() */

static Set *set_new(int cap)
{
    Set *s = (Set *)amalloc(sizeof(Set));
    s->cap = cap < 4 ? 4 : cap;
    s->n = 0;
    s->p = (Piece *)amalloc(sizeof(Piece) * s->cap);
    return s;
}

static void set_add(Set *s, const Piece *pc)
{
    if (s->n == s->cap)
    {
        Piece *np = (Piece *)amalloc(sizeof(Piece) * s->cap * 2);
        memcpy(np, s->p, sizeof(Piece) * s->n);
        s->p = np;
        s->cap *= 2;
    }
    s->p[s->n++] = *pc;
}

static Set *set_all(void)
{
    Set *s = set_new(1);
    Piece pc;
    memset(&pc, 0, sizeof(pc));
    pc.lo_inf = pc.hi_inf = true;
    set_add(s, &pc);
    return s;
}

static const char *txt_dup(const char *t)
{
    if (!t)
        return NULL;
    size_t n = strlen(t) + 1;
    char *c = (char *)amalloc(n);
    memcpy(c, t, n);
    return c;
}

/* Rend s en liberant tout ce que le calcul a alloue depuis mark : s est
 * recopie en un seul bloc (ses morceaux et le texte de leurs bornes), puis
 * ce bloc est ramene en mark. Un ensemble ne contient aucune expression,
 * c'est ce qui permet de jeter celles du calcul. Sans cela chaque cas d'une
 * valeur absolue gardait toutes ses expressions : deux valeurs absolues
 * prenaient plus de la moitie de la zone, trois la saturaient. */
static CAS_NOINLINE Set *set_keep(size_t mark, Set *s)
{
    if (!s)
    {
        g_used = mark;
        return NULL;
    }
    size_t top = g_used;
    Set *c = (Set *)amalloc(sizeof(Set));
    c->n = s->n;
    c->cap = s->n > 0 ? s->n : 1;
    c->p = (Piece *)amalloc(sizeof(Piece) * c->cap);
    for (int i = 0; i < s->n; i++)
    {
        c->p[i] = s->p[i];
        c->p[i].lo.txt = txt_dup(s->p[i].lo.txt);
        c->p[i].hi.txt = txt_dup(s->p[i].hi.txt);
    }
    size_t len = g_used - top;
    size_t delta = top - mark;
    memmove(g_buf + mark, g_buf + top, len);
    g_used = mark + len;
    c = (Set *)((uint8_t *)c - delta);
    c->p = (Piece *)((uint8_t *)c->p - delta);
    for (int i = 0; i < c->n; i++)
    {
        if (c->p[i].lo.txt)
            c->p[i].lo.txt -= delta;
        if (c->p[i].hi.txt)
            c->p[i].hi.txt -= delta;
    }
    return c;
}

/* Deux bornes a 10^-9 pres (relatif) sont la meme : une racine approchee et
 * la valeur exacte qu'elle approche ne font pas deux bornes. Pas de seuil
 * absolu : e^(-30) n'est pas 0 (ln(X) < -30), et une racine approchee
 * voisine de 0 a deja ete ramenee a 0 par dec_num. */
static int bcmp(double a, double b)
{
    if (a == b || fabs(a - b) <= 1e-9 * (fabs(a) + fabs(b)))
        return 0;
    return a < b ? -1 : 1;
}

static Bnd bnd_pick(Bnd a, Bnd b)
{
    return (a.approx && !b.approx) ? b : a;
}

static bool set_has(const Set *s, double x)
{
    for (int i = 0; i < s->n; i++)
    {
        const Piece *p = &s->p[i];
        int cl = p->lo_inf ? 1 : bcmp(x, p->lo.x), ch = p->hi_inf ? -1 : bcmp(x, p->hi.x);
        if ((cl > 0 || (cl == 0 && p->lo_in)) && (ch < 0 || (ch == 0 && p->hi_in)))
            return true;
    }
    return false;
}

static CAS_NOINLINE Set *set_inter(const Set *A, const Set *B)
{
    Set *r = set_new(A->n + B->n);
    for (int i = 0; i < A->n; i++)
        for (int j = 0; j < B->n; j++)
        {
            const Piece *a = &A->p[i], *b = &B->p[j];
            Piece o;
            memset(&o, 0, sizeof(o));
            /* borne basse : la plus grande */
            if (a->lo_inf && b->lo_inf)
                o.lo_inf = true;
            else if (a->lo_inf || b->lo_inf)
            {
                const Piece *k = a->lo_inf ? b : a;
                o.lo = k->lo;
                o.lo_in = k->lo_in;
            }
            else
            {
                int c = bcmp(a->lo.x, b->lo.x);
                o.lo = c > 0 ? a->lo : (c < 0 ? b->lo : bnd_pick(a->lo, b->lo));
                o.lo_in = c > 0 ? a->lo_in : (c < 0 ? b->lo_in : (a->lo_in && b->lo_in));
            }
            /* borne haute : la plus petite */
            if (a->hi_inf && b->hi_inf)
                o.hi_inf = true;
            else if (a->hi_inf || b->hi_inf)
            {
                const Piece *k = a->hi_inf ? b : a;
                o.hi = k->hi;
                o.hi_in = k->hi_in;
            }
            else
            {
                int c = bcmp(a->hi.x, b->hi.x);
                o.hi = c < 0 ? a->hi : (c > 0 ? b->hi : bnd_pick(a->hi, b->hi));
                o.hi_in = c < 0 ? a->hi_in : (c > 0 ? b->hi_in : (a->hi_in && b->hi_in));
            }
            if (!o.lo_inf && !o.hi_inf)
            {
                int c = bcmp(o.lo.x, o.hi.x);
                if (c > 0 || (c == 0 && !(o.lo_in && o.hi_in)))
                    continue;
                if (c == 0)
                    o.hi = o.lo = bnd_pick(o.lo, o.hi);
            }
            set_add(r, &o);
        }
    return r;
}

static int lo_cmp(const Piece *a, const Piece *b)
{
    if (a->lo_inf || b->lo_inf)
        return a->lo_inf == b->lo_inf ? 0 : (a->lo_inf ? -1 : 1);
    int c = bcmp(a->lo.x, b->lo.x);
    if (c)
        return c;
    return a->lo_in == b->lo_in ? 0 : (a->lo_in ? -1 : 1);
}

/* Reunion ; B peut etre NULL (A seul, remis en ordre et fusionne). */
static CAS_NOINLINE Set *set_union(const Set *A, const Set *B)
{
    int n = A->n + (B ? B->n : 0);
    Set *r = set_new(n);
    if (n == 0)
        return r;
    Piece *v = (Piece *)amalloc(sizeof(Piece) * n);
    memcpy(v, A->p, sizeof(Piece) * A->n);
    if (B)
        memcpy(v + A->n, B->p, sizeof(Piece) * B->n);
    for (int i = 1; i < n; i++)
    {
        Piece x = v[i];
        int j = i - 1;
        while (j >= 0 && lo_cmp(&v[j], &x) > 0)
        {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
    Piece cur = v[0];
    for (int i = 1; i < n; i++)
    {
        const Piece *nx = &v[i];
        bool touch = cur.hi_inf || nx->lo_inf;
        if (!touch)
        {
            int c = bcmp(nx->lo.x, cur.hi.x);
            touch = c < 0 || (c == 0 && (nx->lo_in || cur.hi_in));
        }
        if (!touch)
        {
            set_add(r, &cur);
            cur = *nx;
            continue;
        }
        if (!cur.lo_inf && !nx->lo_inf && bcmp(cur.lo.x, nx->lo.x) == 0)
            cur.lo = bnd_pick(cur.lo, nx->lo);
        if (cur.hi_inf)
            continue;
        if (nx->hi_inf)
            cur.hi_inf = true;
        else
        {
            int c = bcmp(nx->hi.x, cur.hi.x);
            if (c > 0)
            {
                cur.hi = nx->hi;
                cur.hi_in = nx->hi_in;
            }
            else if (c == 0)
            {
                cur.hi = bnd_pick(cur.hi, nx->hi);
                cur.hi_in = cur.hi_in || nx->hi_in;
            }
        }
    }
    set_add(r, &cur);
    return r;
}

static bool set_is_all(const Set *s)
{
    return s->n == 1 && s->p[0].lo_inf && s->p[0].hi_inf;
}

/* Premier noeud f(u) avec X dans u (parcours prefixe), ou NULL. */
static Expr *find_fun_x(Expr *e, int f)
{
    if (e->k == E_FUN && e->f == f && has_x(e->a[0]))
        return e;
    for (int i = 0; i < e->n; i++)
    {
        Expr *r = find_fun_x(e->a[i], f);
        if (r)
            return r;
    }
    return NULL;
}

/* Exposant rationnel positif non entier : u^(p/q) est une racine. */
static bool is_root_pow(const Expr *e)
{
    return e->k == E_POW && has_x(e->a[0]) && is_num(e->a[1]) && !q_isint(e->a[1]->q) &&
           q_sign(e->a[1]->q) > 0;
}

static Expr *find_root_pow(Expr *e)
{
    if (is_root_pow(e))
        return e;
    for (int i = 0; i < e->n; i++)
    {
        Expr *r = find_root_pow(e->a[i]);
        if (r)
            return r;
    }
    return NULL;
}

static bool contains(const Expr *e, const Expr *t)
{
    if (eq(e, t))
        return true;
    for (int i = 0; i < e->n; i++)
        if (contains(e->a[i], t))
            return true;
    return false;
}

/* e ou t est remplace par r (a simplifier ensuite). */
static Expr *subst(Expr *e, const Expr *t, Expr *r)
{
    if (eq(e, t))
        return r;
    if (e->n == 0 || !contains(e, t))
        return e;
    Expr *c = node(e->k, e->n);
    c->f = e->f;
    c->sym = e->sym;
    c->q = e->q;
    for (int i = 0; i < e->n; i++)
        c->a[i] = subst(e->a[i], t, r);
    return c;
}

/* rel_set s'appelle elle-meme (un niveau par valeur absolue ou par racine
 * isolee). La pile du Pico est petite : la profondeur est bornee, et les
 * fonctions a gros cadre (CAS_NOINLINE) ne sont pas fondues dans la partie
 * recursive. */
#define REL_DEPTH 8

static Set *rel_set(Expr *f, char rel, int depth);
static Set *eq_set(Expr *f, int depth);

/* f = A R + B, R = u^(p/q) racine isolee une seule fois : f = 0 equivaut a
 * A^q u^p = (-B)^q, avec A B <= 0 et u >= 0 quand q est pair (on a eleve a
 * une puissance paire). Les solutions parasites sont ainsi ecartees, et
 * r(X+1) = X-1 donne X = 3 exact. NULL si f n'a pas cette forme. */
static CAS_NOINLINE Set *eq_root_form(Expr *f, int depth)
{
    Expr *R = find_root_pow(f);
    if (!R)
        return NULL;
    long long p, q;
    if (!big_fits_ll(R->a[1]->q.n, &p) || !big_fits_ll(R->a[1]->q.d, &q) || p > 6 || q > 6)
        return NULL;
    int nt = f->k == E_ADD ? f->n : 1;
    List la, lb;
    l_init(&la, nt);
    l_init(&lb, nt);
    for (int i = 0; i < nt; i++)
    {
        Expr *t = f->k == E_ADD ? f->a[i] : f;
        if (eq(t, R))
        {
            l_push(&la, inum(1));
            continue;
        }
        int at_r = -1;
        if (t->k == E_MUL)
            for (int j = 0; j < t->n; j++)
                if (eq(t->a[j], R))
                    at_r = j;
        if (at_r < 0)
        {
            l_push(&lb, t);
            continue;
        }
        List rest;
        l_init(&rest, t->n);
        for (int j = 0; j < t->n; j++)
            if (j != at_r)
                l_push(&rest, t->a[j]);
        l_push(&la, s_mul(rest.v, rest.n));
    }
    if (la.n == 0)
        return NULL;
    Expr *A = s_add(la.v, la.n);
    Expr *B = lb.n ? s_add(lb.v, lb.n) : inum(0);
    Expr *u = R->a[0];
    if (is_val(A, 0) || contains(A, R) || contains(B, R))
        return NULL;
    Expr *g = sub2(mul2(s_pow(A, inum(q)), s_pow(u, inum(p))), s_pow(neg(B), inum(q)));
    Set *r = rel_set(g, '=', depth + 1);
    if (!r)
        return NULL;
    if (q % 2 == 0)
    {
        Set *sg = rel_set(mul2(A, B), REL_LE, depth + 1);
        Set *dom = rel_set(u, REL_GE, depth + 1);
        if (!sg || !dom)
            return NULL;
        r = set_inter(set_inter(r, sg), dom);
    }
    return r;
}

/* Solutions reelles de f = 0 (f sans valeur absolue). NULL : non gere ici
 * (equation periodique, liste tronquee). */
static CAS_NOINLINE Set *eq_set(Expr *f, int depth)
{
    if (!has_x(f))
    {
        double v = nv(f, 0.0);
        return (is_val(f, 0) || v == 0) ? set_all() : set_new(1);
    }
    Set *rf = eq_root_form(f, depth);
    if (rf)
        return rf;
    Sol s;
    memset(&s, 0, sizeof(s));
    solve_expr(f, &s);
    if (s.partial || s.note)
        return NULL;
    if (s.searched)
        g_set_searched = true;
    if (s.real_only)
        g_set_cplx = true;
    if (s.all)
        return set_all();
    Set *r = set_new(s.n + 1);
    for (int i = 0; i < s.n; i++)
    {
        if (has_sym_i(s.v[i]))
        {
            g_set_cplx = true;
            continue;
        }
        double x = nv(s.v[i], NAN);
        if (!isfinite(x))
            continue;
        Piece pc;
        memset(&pc, 0, sizeof(pc));
        pc.lo_in = pc.hi_in = true;
        pc.lo.dec = -1;
        pc.lo.txt = s.approx ? NULL : bnd_text(s.v[i], &pc.lo.dec);
        pc.lo.x = x;
        pc.lo.approx = s.approx;
        pc.hi = pc.lo;
        set_add(r, &pc);
    }
    return set_union(r, NULL);
}

/* Sous-expressions dont les zeros bornent le domaine de e ou y sont des
 * poles. open : e n'est pas definie en ces points (1/u, ln(u)) ; closed :
 * elle peut l'etre (r(u) en 0). false : fonction non geree (periodique ou
 * discontinue). */
static bool crit_walk(Expr *e, List *open, List *closed)
{
    if (!has_x(e))
        return true;
    if (e->k == E_POW && has_x(e->a[0]))
    {
        Expr *x = e->a[1];
        bool poly = is_num(x) && q_isint(x->q) && q_sign(x->q) >= 0;
        if (!poly)
            l_push(is_num(x) && q_sign(x->q) < 0 ? open : closed, e->a[0]);
    }
    else if (e->k == E_FUN)
    {
        Expr *a = e->a[0];
        switch (e->f)
        {
        case F_LN:
        case F_LOG:
            l_push(open, a);
            break;
        case F_ASIN:
        case F_ACOS:
            l_push(closed, sub2(a, inum(1)));
            l_push(closed, add2(a, inum(1)));
            break;
        case F_ACOSH:
            l_push(closed, sub2(a, inum(1)));
            break;
        case F_ATANH:
            l_push(open, sub2(a, inum(1)));
            l_push(open, add2(a, inum(1)));
            break;
        case F_SIN:
        case F_COS:
        case F_TAN:
        case F_FACT:
        case F_ABS:
            return false;
        default:
            break;
        }
    }
    for (int i = 0; i < e->n; i++)
        if (!crit_walk(e->a[i], open, closed))
            return false;
    return true;
}

/* Classe de f en x : 0 non definie, 1 negative, 2 nulle, 3 positive. */
static int sign_class(Expr *f, double x)
{
    double v = nv(f, x);
    if (!isfinite(v))
        return 0;
    return v < 0 ? 1 : (v > 0 ? 3 : 2);
}

static bool rel_holds(int cls, char rel)
{
    switch (cls)
    {
    case 1:
        return rel == REL_LT || rel == REL_LE;
    case 2:
        return rel == REL_LE || rel == REL_GE;
    case 3:
        return rel == REL_GT || rel == REL_GE;
    }
    return false;
}

struct Brk
{
    Bnd b;
    bool open;
};

static void brk_add(Brk **v, int *n, int *cap, Bnd b, bool open)
{
    for (int i = 0; i < *n; i++)
        if (bcmp((*v)[i].b.x, b.x) == 0)
        {
            (*v)[i].b = bnd_pick((*v)[i].b, b);
            (*v)[i].open = (*v)[i].open || open;
            return;
        }
    if (*n == *cap)
    {
        Brk *nv2 = (Brk *)amalloc(sizeof(Brk) * *cap * 2);
        memcpy(nv2, *v, sizeof(Brk) * *n);
        *v = nv2;
        *cap *= 2;
    }
    (*v)[*n].b = b;
    (*v)[*n].open = open;
    (*n)++;
}

static void brk_add_set(Brk **v, int *n, int *cap, const Set *s, bool open)
{
    for (int i = 0; i < s->n; i++)
    {
        if (!s->p[i].lo_inf)
            brk_add(v, n, cap, s->p[i].lo, open);
        if (!s->p[i].hi_inf)
            brk_add(v, n, cap, s->p[i].hi, open);
    }
}

/* Solutions de f rel 0 (f sans valeur absolue). Entre deux points
 * consecutifs pris parmi les zeros de f et les bornes de son domaine, f est
 * continue et ne s'annule pas : son signe y est constant, un point d'essai
 * suffit (c'est le tableau de signes). NULL : non gere. */
static CAS_NOINLINE Set *ineq_set(Expr *f, char rel, int depth)
{
    if (!has_x(f))
    {
        double v = nv(f, 0.0);
        if (!isfinite(v))
            return set_new(1);
        return rel_holds(v < 0 ? 1 : (v > 0 ? 3 : 2), rel) ? set_all() : set_new(1);
    }
    List open, closed;
    l_init(&open, 4);
    l_init(&closed, 4);
    if (!crit_walk(f, &open, &closed))
        return NULL;
    size_t mz = g_used;
    Set *Z = set_keep(mz, eq_set(f, depth + 1));
    if (!Z)
        return NULL;
    int n = 0, cap = 8;
    Brk *bk = (Brk *)amalloc(sizeof(Brk) * cap);
    brk_add_set(&bk, &n, &cap, Z, false);
    for (int pass = 0; pass < 2; pass++)
    {
        List *l = pass ? &closed : &open;
        for (int i = 0; i < l->n; i++)
        {
            Set *zu = rel_set(l->v[i], '=', depth + 1);
            if (!zu)
                return NULL;
            brk_add_set(&bk, &n, &cap, zu, pass == 0);
        }
    }
    for (int i = 1; i < n; i++)
    {
        Brk x = bk[i];
        int j = i - 1;
        while (j >= 0 && bk[j].b.x > x.b.x)
        {
            bk[j + 1] = bk[j];
            j--;
        }
        bk[j + 1] = x;
    }
    /* Elements dans l'ordre : intervalle 0, point 0, intervalle 1, ...,
     * point n-1, intervalle n. in[] : l'element fait partie des solutions. */
    int ne = 2 * n + 1;
    bool *in = (bool *)amalloc(sizeof(bool) * ne);
    for (int k = 0; k <= n; k++)
    {
        tick();
        double xs[3];
        if (n == 0)
        {
            xs[0] = 0.0;
            xs[1] = 1.37;
            xs[2] = -1.37;
        }
        else if (k == 0 || k == n)
        {
            double b = bk[k == 0 ? 0 : n - 1].b.x, d = fmax(1.0, fabs(b) * 1e-3) * (k == 0 ? -1 : 1);
            xs[0] = b + d;
            xs[1] = b + 0.5 * d;
            xs[2] = b + 2.3 * d;
        }
        else
        {
            double a = bk[k - 1].b.x, b = bk[k].b.x;
            xs[0] = a + 0.5 * (b - a);
            xs[1] = a + 0.25 * (b - a);
            xs[2] = a + 0.75 * (b - a);
        }
        int cls;
        if (set_has(Z, xs[0]) && set_has(Z, xs[1]) && set_has(Z, xs[2]))
            cls = 2; /* f nulle sur tout l'intervalle */
        else
        {
            cls = sign_class(f, xs[0]);
            /* Trois points d'essai : un desaccord trahit une racine ou une
             * borne du domaine manquee. Mieux vaut ne pas repondre. */
            if (sign_class(f, xs[1]) != cls || sign_class(f, xs[2]) != cls || cls == 2)
                return NULL;
        }
        in[2 * k] = rel_holds(cls, rel);
    }
    for (int k = 0; k < n; k++)
    {
        double x = bk[k].b.x;
        int cls;
        if (set_has(Z, x))
            cls = 2;
        else if (bk[k].open)
            cls = 0;
        else
        {
            /* Bord du domaine ou f est definie (r(u) en u = 0) : sa valeur,
             * prise juste a cote si l'arrondi fait sortir du domaine. */
            double d = 1e-9 * (1 + fabs(x));
            cls = sign_class(f, x);
            if (cls == 0)
                cls = sign_class(f, x + d);
            if (cls == 0)
                cls = sign_class(f, x - d);
            if (cls == 2)
                cls = 0; /* un zero que la resolution n'a pas donne : douteux */
        }
        in[2 * k + 1] = rel_holds(cls, rel);
    }
    Set *r = set_new(n + 1);
    for (int s0 = 0; s0 < ne;)
    {
        if (!in[s0])
        {
            s0++;
            continue;
        }
        int t = s0;
        while (t + 1 < ne && in[t + 1])
            t++;
        Piece pc;
        memset(&pc, 0, sizeof(pc));
        if (s0 == 0)
            pc.lo_inf = true;
        else
        {
            pc.lo = bk[(s0 - 1) / 2].b;
            pc.lo_in = (s0 % 2) == 1;
        }
        if (t == ne - 1)
            pc.hi_inf = true;
        else
        {
            pc.hi = bk[t / 2].b;
            pc.hi_in = (t % 2) == 1;
        }
        set_add(r, &pc);
        s0 = t + 1;
    }
    return r;
}

static Set *rel_set_work(Expr *f, char rel, int depth);

/* Solutions reelles de "f rel 0", rel etant '=' ou un signe d'inequation.
 * Une valeur absolue |u| est traitee par cas : u >= 0 (|u| = u) et u < 0
 * (|u| = -u), chaque cas etant resolu puis restreint a sa condition. |X| = X
 * donne ainsi X >= 0 (la recherche numerique ne rendait que X = 0).
 * NULL : non gere. Les expressions construites en chemin sont liberees. */
static Set *rel_set(Expr *f, char rel, int depth)
{
    size_t mark = g_used;
    return set_keep(mark, rel_set_work(f, rel, depth));
}

static Set *rel_set_work(Expr *f, char rel, int depth)
{
    tick();
    if (depth > REL_DEPTH)
        return NULL;
    f = S(f);
    Expr *ab = find_fun_x(f, F_ABS);
    if (ab)
    {
        Expr *u = ab->a[0];
        Set *pos = rel_set(u, REL_GE, depth + 1);
        Set *neg_ = rel_set(u, REL_LT, depth + 1);
        if (!pos || !neg_)
            return NULL;
        Set *a = pos->n ? rel_set(subst(f, ab, u), rel, depth + 1) : set_new(1);
        Set *b = neg_->n ? rel_set(subst(f, ab, neg(u)), rel, depth + 1) : set_new(1);
        if (!a || !b)
            return NULL;
        return set_union(set_inter(a, pos), set_inter(b, neg_));
    }
    return rel == '=' ? eq_set(f, depth) : ineq_set(f, rel, depth);
}

/* Restreint *dom aux reels ou u verifie cond : 0 "u != 0", 1 "u > 0",
 * 2 "u >= 0". Faux si non gere. */
static CAS_NOINLINE bool dom_add(Set **dom, Expr *u, int cond)
{
    Set *c;
    if (cond == 0)
    {
        Set *a = rel_set(u, REL_LT, 1), *b = rel_set(u, REL_GT, 1);
        if (!a || !b)
            return false;
        c = set_union(a, b);
    }
    else
        c = rel_set(u, cond == 1 ? REL_GT : REL_GE, 1);
    if (!c)
        return false;
    *dom = set_inter(*dom, c);
    return true;
}

/* Domaine de definition de e telle qu'elle est ECRITE (arbre lu, avant
 * simplification) : denominateurs non nuls, logarithmes de positifs, racines
 * paires de positifs ou nuls. La simplification reunit X/X en 1 et retire
 * (X+5) de (X+5)(2X-5)/((X+5)(X+1)) : sans ce domaine, la valeur interdite
 * etait rendue comme solution. Faux si non gere (tangente...). */
static bool dom_walk(Expr *e, Set **dom)
{
    if (!has_x(e))
        return true;
    if (e->k == E_POW && has_x(e->a[0]))
    {
        Expr *x = S(e->a[1]);
        if (is_num(x))
        {
            bool neg = q_sign(x->q) < 0, even_den = false;
            if (!q_isint(x->q))
            {
                unsigned long long d;
                even_den = !big_fits_u64(x->q.d, &d) || (d % 2) == 0;
            }
            if (even_den)
            {
                if (!dom_add(dom, e->a[0], neg ? 1 : 2))
                    return false;
            }
            else if (neg && !dom_add(dom, e->a[0], 0))
                return false;
        }
        else if (!dom_add(dom, e->a[0], 1)) /* exposant variable : base > 0 */
            return false;
    }
    else if (e->k == E_FUN && has_x(e->a[0]))
    {
        Expr *a = e->a[0];
        bool ok = true;
        switch (e->f)
        {
        case F_LN:
        case F_LOG:
            ok = dom_add(dom, a, 1);
            break;
        case F_ASIN:
        case F_ACOS:
            ok = dom_add(dom, add2(S(a), inum(1)), 2) && dom_add(dom, sub2(inum(1), S(a)), 2);
            break;
        case F_ACOSH:
            ok = dom_add(dom, sub2(S(a), inum(1)), 2);
            break;
        case F_ATANH:
            ok = dom_add(dom, add2(S(a), inum(1)), 1) && dom_add(dom, sub2(inum(1), S(a)), 1);
            break;
        case F_TAN:
        case F_FACT:
            ok = false;
            break;
        default:
            break;
        }
        if (!ok)
            return false;
    }
    for (int i = 0; i < e->n; i++)
        if (!dom_walk(e->a[i], dom))
            return false;
    return true;
}

/* Domaine d'une relation lue (les deux membres) ou d'une expression ; NULL
 * si non gere. */
static Set *raw_domain(Expr *e)
{
    Set *dom = set_all();
    return dom_walk(e, &dom) ? dom : NULL;
}

/* x est dans le domaine, bornes comparees a 10^-12 pres (relatif) : une
 * solution exacte egale a une valeur interdite est bien ecartee, une petite
 * solution (e^(-30) > 0) est gardee. */
static bool dom_has(const Set *s, double x)
{
    for (int i = 0; i < s->n; i++)
    {
        const Piece *p = &s->p[i];
        bool lo_ok = p->lo_inf, hi_ok = p->hi_inf;
        if (!lo_ok)
        {
            bool same = x == p->lo.x || fabs(x - p->lo.x) <= 1e-12 * (fabs(x) + fabs(p->lo.x));
            lo_ok = same ? p->lo_in : x > p->lo.x;
        }
        if (!hi_ok)
        {
            bool same = x == p->hi.x || fabs(x - p->hi.x) <= 1e-12 * (fabs(x) + fabs(p->hi.x));
            hi_ok = same ? p->hi_in : x < p->hi.x;
        }
        if (lo_ok && hi_ok)
            return true;
    }
    return false;
}

/* Vrai si l'equation f = 0 gagne a passer par rel_set : valeur absolue ou
 * racine d'une expression en X. */
static bool eq_needs_set(Expr *f)
{
    return find_fun_x(f, F_ABS) != NULL || find_root_pow(f) != NULL;
}

static Expr *apply_op(Expr *e)
{
    switch (e->f)
    {
    case 'q':
    case 'y':
    {
        Expr *n = S(e->a[0]), *k = S(e->a[1]);
        long long nn, kk;
        if (!is_num(n) || !is_num(k) || !q_isint(n->q) || !q_isint(k->q) ||
            !big_fits_ll(n->q.n, &nn) || !big_fits_ll(k->q.n, &kk) || nn < 0 || nn > 500)
            unsupported();
        if (kk < 0 || kk > nn)
            return inum(0);
        Big r = big_ll(1);
        for (long long i = 0; i < kk; i++)
            r = big_mul(r, big_ll(nn - i));
        Q q;
        q.n = r;
        q.d = big_ll(1);
        if (e->f == 'q')
            for (long long i = 2; i <= kk; i++)
                q = q_div(q, q_int(i));
        return num(q);
    }
    case CAS_TOK_EXPAND:
        return EX(S(e->a[0]));
    case CAS_TOK_FACTOR:
        return factor_any(S(e->a[0]));
    case CAS_TOK_DERIVE:
        return best(D(S(e->a[0])));
    case CAS_TOK_PRIMITIVE:
    {
        Expr *r = P(S(e->a[0]), 0);
        if (!r)
            fail("Primitive non trouvee");
        return best(r);
    }
    case CAS_TOK_SOLVE:
        fail("resoudre() doit etre seul");
    default:
        unsupported();
    }
}

/* ===================== 7. Lecture et ecriture ============================ */

struct Parser
{
    token *t;
    int n, i;
    const char *src;
    int src_len;
};

static token *pk(Parser *p)
{
    return p->i < p->n ? &p->t[p->i] : NULL;
}

static bool at(Parser *p, char c)
{
    token *t = pk(p);
    return t && t->type == c;
}

static Expr *p_sum(Parser *p);
static Expr *p_unary(Parser *p);

static bool at_rel(Parser *p)
{
    token *t = pk(p);
    return t && (t->type == '=' || is_rel_type(t->type));
}

/* Une relation "f = g", "f < g"... ou une expression seule. Un encadrement
 * "a < f <= b" donne deux relations a verifier ensemble. */
static Expr *p_equation(Parser *p)
{
    Expr *l = p_sum(p);
    if (!at_rel(p))
        return l;
    List rels;
    l_init(&rels, 2);
    while (at_rel(p))
    {
        char rel = pk(p)->type;
        p->i++;
        Expr *e = node(E_EQ, 2);
        e->f = (uint8_t)rel;
        e->a[0] = l;
        e->a[1] = p_sum(p);
        l_push(&rels, e);
        l = e->a[1];
    }
    return rels.n == 1 ? rels.v[0] : rlist(E_SYS, rels.v, rels.n);
}

/* Relations separees par CAS_SEPARATOR (les lignes du Solveur) : toutes
 * doivent etre verifiees. */
static Expr *p_system(Parser *p)
{
    Expr *first = p_equation(p);
    if (!at(p, CAS_SEPARATOR))
        return first;
    List rels;
    l_init(&rels, 4);
    Expr *e = first;
    while (true)
    {
        if (e->k == E_SYS)
            for (int i = 0; i < e->n; i++)
                l_push(&rels, e->a[i]);
        else
            l_push(&rels, e);
        if (!at(p, CAS_SEPARATOR))
            break;
        p->i++;
        e = p_equation(p);
    }
    return rlist(E_SYS, rels.v, rels.n);
}

/* Nombre relu dans le texte (exact, meme au-dela de 2^53, et decimal). */
static Expr *p_number(Parser *p, token *t)
{
    int end = t->src_pos;
    if (end >= 0 && end < p->src_len && p->src[end] >= '0' && p->src[end] <= '9')
    {
        int s = end;
        while (s > 0 && ((p->src[s - 1] >= '0' && p->src[s - 1] <= '9') || p->src[s - 1] == '.' ||
                         p->src[s - 1] == ','))
            s--;
        /* une virgule sert aussi de separateur d'arguments : on ne la prend
         * que si elle est suivie et precedee de chiffres */
        while (s < end && (p->src[s] == ',' || p->src[s] == '.'))
            s++;
        Big ip = big_ll(0), frac = big_ll(0);
        int fd = 0, dots = 0;
        for (int i = s; i <= end; i++)
        {
            char ch = p->src[i];
            if (ch == '.' || ch == ',')
            {
                dots++;
                continue;
            }
            if (dots == 0)
                ip = big_add(big_mul(ip, big_ll(10)), big_ll(ch - '0'));
            else
            {
                frac = big_add(big_mul(frac, big_ll(10)), big_ll(ch - '0'));
                fd++;
            }
        }
        if (dots <= 1)
        {
            Big den = big_pow(big_ll(10), fd);
            Q q = q_make(big_add(big_mul(ip, den), frac), den);
            if (fabs(q_to_double(q) - t->value) <= 1e-9 * (1 + fabs(t->value)))
                return num(q);
        }
    }
    double v = t->value;
    if (!isfinite(v) || v != floor(v) || fabs(v) > 9e15)
        unsupported();
    return inum((long long)v);
}

static int fun_of(char t)
{
    switch (t)
    {
    case 's':
        return F_SIN;
    case 'c':
        return F_COS;
    case 't':
        return F_TAN;
    case 'v':
        return F_ASIN;
    case 'u':
        return F_ACOS;
    case 'w':
        return F_ATAN;
    case 'l':
        return F_LN;
    case 'o':
        return F_LOG;
    case 'S':
        return F_ABS;
    case 'g':
        return F_SINH;
    case 'f':
        return F_COSH;
    case 'h':
        return F_TANH;
    case 'j':
        return F_ASINH;
    case 'a':
        return F_ACOSH;
    case 'k':
        return F_ATANH;
    }
    return -1;
}

/* Arguments entre parentheses : au plus max. */
static int p_args(Parser *p, Expr **out, int max, bool allow_eq)
{
    if (!at(p, '('))
        fail("Syntaxe");
    p->i++;
    int n = 0;
    while (true)
    {
        if (at(p, ')'))
            break;
        if (n >= max)
            fail("Syntaxe");
        out[n++] = allow_eq ? p_system(p) : p_sum(p);
        if (at(p, ','))
        {
            p->i++;
            continue;
        }
        break;
    }
    if (at(p, ')'))
        p->i++;
    return n;
}

static Expr *p_prim(Parser *p)
{
    tick();
    token *t = pk(p);
    if (!t)
        fail("Syntaxe");
    char ty = t->type;
    if (ty == 'n')
    {
        p->i++;
        return p_number(p, t);
    }
    if (ty == 'X')
    {
        p->i++;
        if ((char)('a' + (int)t->value) != 'X')
            unsupported(); /* autres lettres : variables memorisees */
        return sym('X');
    }
    if (ty == 'p' || ty == 'e')
    {
        p->i++;
        return sym(ty);
    }
    if (ty == '(')
    {
        p->i++;
        Expr *e = p_sum(p);
        if (at(p, ')'))
            p->i++;
        return e;
    }
    Expr *args[3];
    int f = fun_of(ty);
    if (f >= 0)
    {
        p->i++;
        if (p_args(p, args, 1, false) != 1)
            fail("Syntaxe");
        return rfun(f, args[0]);
    }
    if (ty == 'r' || ty == '$')
    {
        p->i++;
        if (p_args(p, args, 1, false) != 1)
            fail("Syntaxe");
        return ty == 'r' ? rpow(args[0], qnum(1, 2)) : rpow(sym('e'), args[0]);
    }
    if (ty == '%' || ty == 'q' || ty == 'y')
    {
        p->i++;
        if (p_args(p, args, 2, false) != 2)
            fail("Syntaxe");
        if (ty == '%')
            return rpow(args[0], rpow(args[1], inum(-1)));
        Expr *e = node(E_OP, 2);
        e->f = (uint8_t)ty;
        e->a[0] = args[0];
        e->a[1] = args[1];
        return e;
    }
    if (is_cas_type(ty))
    {
        p->i++;
        if (p_args(p, args, 1, ty == CAS_TOK_SOLVE) != 1)
            fail("Syntaxe");
        Expr *e = node(E_OP, 1);
        e->f = (uint8_t)ty;
        e->a[0] = args[0];
        return e;
    }
    if (ty == ')' || ty == ',' || ty == '=' || ty == '+' || ty == '*' || ty == '/' || ty == '^' ||
        is_rel_type(ty) || ty == CAS_SEPARATOR)
        fail("Syntaxe");
    unsupported(); /* i, Ans, matrices, alea, pgcd... */
}

static Expr *p_post(Parser *p)
{
    Expr *b = p_prim(p);
    while (at(p, '!'))
    {
        p->i++;
        b = rfun(F_FACT, b);
    }
    return b;
}

static Expr *p_unary(Parser *p);

/* Exposant apres '^'. "^(...)" est une boite fermee : un '!' qui suit ne lui
 * appartient pas, et "^(2)^(3)" chaine a droite (2^(2^3)). */
static Expr *p_exponent(Parser *p)
{
    if (!at(p, '('))
        return p_unary(p);
    Expr *e = p_prim(p);
    if (at(p, '^'))
    {
        p->i++;
        e = rpow(e, p_exponent(p));
    }
    return e;
}

static Expr *p_pow(Parser *p)
{
    Expr *b = p_post(p);
    if (at(p, '^'))
    {
        p->i++;
        b = rpow(b, p_exponent(p));
        /* "2^(3)!" : l'ecran montre (2^3)! */
        while (at(p, '!'))
        {
            p->i++;
            b = rfun(F_FACT, b);
        }
    }
    return b;
}

static Expr *p_unary(Parser *p)
{
    if (at(p, '-'))
    {
        p->i++;
        Expr *v[2] = {inum(-1), p_unary(p)};
        return rlist(E_MUL, v, 2);
    }
    if (at(p, '+'))
        p->i++;
    return p_pow(p);
}

static Expr *p_prod(Parser *p)
{
    List f;
    l_init(&f, 4);
    l_push(&f, p_unary(p));
    while (at(p, '*') || at(p, '/'))
    {
        bool div = at(p, '/');
        p->i++;
        Expr *x = p_unary(p);
        l_push(&f, div ? rpow(x, inum(-1)) : x);
    }
    return f.n == 1 ? f.v[0] : rlist(E_MUL, f.v, f.n);
}

static Expr *p_sum(Parser *p)
{
    List t;
    l_init(&t, 4);
    l_push(&t, p_prod(p));
    while (at(p, '+') || at(p, '-'))
    {
        bool minus = at(p, '-');
        p->i++;
        Expr *x = p_prod(p);
        if (minus)
        {
            Expr *v[2] = {inum(-1), x};
            x = rlist(E_MUL, v, 2);
        }
        l_push(&t, x);
    }
    return t.n == 1 ? t.v[0] : rlist(E_ADD, t.v, t.n);
}

/* ----------------------------- Ecriture ---------------------------------- */

struct W
{
    char *buf;
    int cap, pos;
};

static void w_str(W *w, const char *s)
{
    int n = (int)strlen(s);
    if (w->pos + n >= w->cap)
        fail("Resultat trop long");
    memcpy(w->buf + w->pos, s, n);
    w->pos += n;
    w->buf[w->pos] = '\0';
}

static void w_big(W *w, Big b)
{
    char tmp[420];
    if (big_to_str(b, tmp, sizeof(tmp)) < 0)
        fail("Resultat trop long");
    w_str(w, tmp);
}

enum
{
    PR_SUM = 1,
    PR_MUL = 2,
    PR_POW = 3,
    PR_ATOM = 4
};

static int prec_of(const Expr *e)
{
    switch (e->k)
    {
    case E_NUM:
        if (q_sign(e->q) < 0)
            return PR_SUM;
        return q_isint(e->q) ? PR_ATOM : PR_MUL;
    case E_ADD:
        return PR_SUM;
    case E_MUL:
        return neg_form(e) ? PR_SUM : PR_MUL;
    case E_POW:
        if (is_num(e->a[1]) && q_sign(e->a[1]->q) < 0)
            return PR_MUL; /* ecrit 1/... */
        if (is_num(e->a[1]) && big_is_one(e->a[1]->q.n))
            return PR_ATOM; /* racine */
        return PR_POW;
    default:
        return PR_ATOM;
    }
}

static void pr(W *w, Expr *e, int ctx);

/* Le texte de e commence par un chiffre (pour ecrire '*' entre deux
 * nombres : 2^(3)*5). */
static bool starts_digit(const Expr *e)
{
    switch (e->k)
    {
    case E_NUM:
        return q_sign(e->q) >= 0;
    case E_POW:
        if (is_num(e->a[1]) && q_sign(e->a[1]->q) < 0)
            return true; /* ecrit "1/..." */
        if (is_num(e->a[1]) && big_is_one(e->a[1]->q.n) && !q_isint(e->a[1]->q))
            return false; /* r( ou racine( */
        return prec_of(e->a[0]) >= PR_ATOM && starts_digit(e->a[0]);
    case E_MUL:
        return !neg_form(e) && starts_digit(e->a[0]);
    default:
        return false;
    }
}

static void pr_factors(W *w, Expr **v, int n)
{
    for (int i = 0; i < n; i++)
    {
        if (i > 0 && starts_digit(v[i]))
            w_str(w, "*");
        pr(w, v[i], PR_POW);
    }
}

static const char *fun_name(int f)
{
    switch (f)
    {
    case F_SIN:
        return "s(";
    case F_COS:
        return "c(";
    case F_TAN:
        return "t(";
    case F_ASIN:
        return "v(";
    case F_ACOS:
        return "u(";
    case F_ATAN:
        return "w(";
    case F_LN:
        return "l(";
    case F_LOG:
        return "log(";
    case F_ABS:
        return "abs(";
    case F_SINH:
        return "g(";
    case F_COSH:
        return "f(";
    case F_TANH:
        return "h(";
    case F_ASINH:
        return "j(";
    case F_ACOSH:
        return "a(";
    case F_ATANH:
        return "k(";
    }
    return "?(";
}

/* Degre en X pour l'ordre d'affichage d'une somme (puissances decroissantes). */
static double disp_deg(const Expr *t)
{
    if (!has_x(t))
        return -1;
    if (is_sym(t, 'X'))
        return 1;
    if (t->k == E_POW && is_sym(t->a[0], 'X') && is_num(t->a[1]))
        return q_to_double(t->a[1]->q);
    if (t->k == E_MUL)
    {
        double d = 0;
        for (int i = 0; i < t->n; i++)
            if (has_x(t->a[i]))
                d += disp_deg(t->a[i]) > 0 ? disp_deg(t->a[i]) : 0.5;
        return d;
    }
    return 0.5;
}

static void pr_mul(W *w, Expr *e)
{
    Q c = q_int(1);
    int start = 0;
    if (is_num(e->a[0]))
    {
        c = e->a[0]->q;
        start = 1;
    }
    if (q_sign(c) < 0)
    {
        w_str(w, "-");
        c = q_neg(c);
    }
    List nm, dn;
    l_init(&nm, e->n);
    l_init(&dn, e->n);
    for (int i = start; i < e->n; i++)
    {
        Expr *f = e->a[i];
        if (f->k == E_POW && is_num(f->a[1]) && q_sign(f->a[1]->q) < 0)
        {
            Q ex = q_neg(f->a[1]->q);
            l_push(&dn, q_is(ex, 1) ? f->a[0] : rpow(f->a[0], num(ex)));
        }
        else
            l_push(&nm, f);
    }
    bool cn = !big_is_one(c.n), cd = !big_is_one(c.d);
    if (dn.n == 0)
    {
        if (!cd)
        {
            if (cn || nm.n == 0)
            {
                w_big(w, c.n);
                if (nm.n && starts_digit(nm.v[0]))
                    w_str(w, "*");
            }
            pr_factors(w, nm.v, nm.n);
            return;
        }
        if (!cn && nm.n == 1)
        {
            /* X/2, (X+1)/2, r(3)/2 */
            pr(w, nm.v[0], PR_POW);
            w_str(w, "/");
            w_big(w, c.d);
            return;
        }
        /* 3/2X, 1/2(X-1)(X+1) */
        if (nm.n && starts_digit(nm.v[0]))
        {
            /* 2/3 devant 5^(p) : "2/35^(p)" se lisait 2/(35^p). On ecrit
             * (2*5^(p))/3. */
            w_str(w, "(");
            w_big(w, c.n);
            w_str(w, "*");
            pr_factors(w, nm.v, nm.n);
            w_str(w, ")/");
            w_big(w, c.d);
            return;
        }
        w_big(w, c.n);
        w_str(w, "/");
        w_big(w, c.d);
        pr_factors(w, nm.v, nm.n);
        return;
    }
    int ncount = nm.n + (cn ? 1 : 0);
    if (ncount > 1)
        w_str(w, "(");
    if (ncount == 0)
        w_str(w, "1");
    if (cn)
    {
        w_big(w, c.n);
        if (nm.n && starts_digit(nm.v[0]))
            w_str(w, "*");
    }
    pr_factors(w, nm.v, nm.n);
    if (ncount > 1)
        w_str(w, ")");
    w_str(w, "/");
    int dcount = dn.n + (cd ? 1 : 0);
    if (dcount > 1)
        w_str(w, "(");
    if (cd)
    {
        w_big(w, c.d);
        if (dn.n && starts_digit(dn.v[0]))
            w_str(w, "*");
    }
    pr_factors(w, dn.v, dn.n);
    if (dcount > 1)
        w_str(w, ")");
}

static void pr_add(W *w, Expr *e)
{
    /* Ordre d'affichage : puissances de X decroissantes, constante a la fin. */
    Expr **v = (Expr **)amalloc(sizeof(Expr *) * e->n);
    double *dg = (double *)amalloc(sizeof(double) * e->n);
    for (int i = 0; i < e->n; i++)
    {
        v[i] = e->a[i];
        dg[i] = disp_deg(v[i]);
    }
    for (int i = 1; i < e->n; i++)
    {
        Expr *x = v[i];
        double d = dg[i];
        int j = i - 1;
        while (j >= 0 && dg[j] < d)
        {
            v[j + 1] = v[j];
            dg[j + 1] = dg[j];
            j--;
        }
        v[j + 1] = x;
        dg[j + 1] = d;
    }
    /* Somme de constantes a coefficients fractionnaires : (1+r(5))/2. */
    if (!has_x(e) && !has_sym_i(e))
    {
        Big L = big_ll(1);
        bool ok = true;
        for (int i = 0; i < e->n && ok; i++)
        {
            Q c;
            split_coef(v[i], &c);
            if (is_num(v[i]))
                c = v[i]->q;
            Big g = big_gcd(L, c.d);
            Big q, r;
            big_divmod(big_mul(L, c.d), g, &q, &r);
            L = q;
        }
        if (!big_is_one(L))
        {
            Q lq;
            lq.n = L;
            lq.d = big_ll(1);
            Expr **sv = (Expr **)amalloc(sizeof(Expr *) * e->n);
            for (int i = 0; i < e->n; i++)
                sv[i] = mul2(num(lq), v[i]);
            Expr *inner = rlist(E_ADD, sv, e->n);
            w_str(w, "(");
            pr_add(w, inner);
            w_str(w, ")/");
            w_big(w, L);
            return;
        }
    }
    for (int i = 0; i < e->n; i++)
    {
        Expr *t = v[i];
        if (i > 0)
        {
            if (neg_form(t))
            {
                w_str(w, "-");
                pr(w, neg(t), PR_MUL);
                continue;
            }
            w_str(w, "+");
        }
        pr(w, t, i == 0 ? PR_SUM : PR_MUL);
    }
}

static void pr_inner(W *w, Expr *e)
{
    switch (e->k)
    {
    case E_NUM:
        w_big(w, e->q.n);
        if (!q_isint(e->q))
        {
            w_str(w, "/");
            w_big(w, e->q.d);
        }
        return;
    case E_SYM:
    {
        char s[2] = {e->sym, 0};
        w_str(w, s);
        return;
    }
    case E_FUN:
        if (e->f == F_FACT)
        {
            pr(w, e->a[0], PR_ATOM);
            w_str(w, "!");
            return;
        }
        w_str(w, fun_name(e->f));
        pr(w, e->a[0], 0);
        w_str(w, ")");
        return;
    case E_POW:
    {
        Expr *b = e->a[0], *x = e->a[1];
        if (is_num(x) && q_sign(x->q) < 0)
        {
            Q ex = q_neg(x->q);
            w_str(w, "1/");
            pr(w, q_is(ex, 1) ? b : rpow(b, num(ex)), PR_POW);
            return;
        }
        if (is_num(x) && big_is_one(x->q.n) && !q_isint(x->q))
        {
            if (q_is(q_mul(x->q, q_int(2)), 1))
            {
                w_str(w, "r(");
                pr(w, b, 0);
                w_str(w, ")");
                return;
            }
            w_str(w, "racine(");
            pr(w, b, 0);
            w_str(w, ",");
            w_big(w, x->q.d);
            w_str(w, ")");
            return;
        }
        pr(w, b, PR_ATOM);
        w_str(w, "^(");
        pr(w, x, 0);
        w_str(w, ")");
        return;
    }
    case E_MUL:
        pr_mul(w, e);
        return;
    case E_ADD:
        pr_add(w, e);
        return;
    default:
        fail("Syntaxe");
    }
}

static void pr(W *w, Expr *e, int ctx)
{
    tick();
    bool par = prec_of(e) < ctx;
    if (par)
        w_str(w, "(");
    pr_inner(w, e);
    if (par)
        w_str(w, ")");
}

/* ----------------------------- Entree ------------------------------------ */

static const char *const g_cas_names[] = {"developper(", "factoriser(", "deriver(", "primitive(", "resoudre("};

static bool scan_cas(const char *text, int len, bool with_x)
{
    if (!text)
        return false;
    for (int i = 0; i < len && text[i]; i++)
    {
        /* X ou un signe d'inequation (meme "2<3") */
        if (with_x && (text[i] == 'X' || is_rel_type(text[i])))
            return true;
        for (unsigned k = 0; k < sizeof(g_cas_names) / sizeof(g_cas_names[0]); k++)
        {
            int n = (int)strlen(g_cas_names[k]);
            if (i + n <= len && strncmp(text + i, g_cas_names[k], n) == 0)
                return true;
        }
    }
    return false;
}

bool cas_required(const char *text, int len)
{
    return scan_cas(text, len, true);
}

bool cas_has_command(const char *text, int len)
{
    return scan_cas(text, len, false);
}

/* Valeur approchee ecrite en decimal : 0.7390851332 */
static void w_decimal(W *w, double v)
{
    char b[40];
    if (fabs(v) < 1e-12)
        v = 0;
    snprintf(b, sizeof(b), "%.10g", v);
    char *e = strchr(b, 'e');
    if (e)
    {
        /* m*10^(n), relu tel quel par le tokenizer. "%.6f" ecrivait
         * 0.000000 pour une racine de 10^-7. */
        int ex = atoi(e + 1);
        *e = '\0';
        w_str(w, b);
        snprintf(b, sizeof(b), "*10^(%d)", ex);
    }
    w_str(w, b);
}

/* Chiffres apres la virgule d'une fraction p/q quand q = 2^a 5^b : max(a, b).
 * -1 pour toute autre expression. */
static int dec_digits_of(const Expr *e)
{
    if (!is_num(e))
        return -1;
    Big d = e->q.d;
    int a = 0, b = 0;
    while (!big_is_one(d))
    {
        Big q;
        if (divmod_small(d, 2, &q) == 0)
        {
            d = q;
            a++;
        }
        else if (divmod_small(d, 5, &q) == 0)
        {
            d = q;
            b++;
        }
        else
            return -1;
    }
    return a > b ? a : b;
}

static void w_bnd(W *w, const Bnd *b)
{
    if (b->approx || !b->txt)
        w_decimal(w, b->x);
    else
        w_str(w, b->txt);
}

/* Texte d'une borne exacte, garde dans la zone du calcul. */
static const char *bnd_text(Expr *v, signed char *dec)
{
    static char buf[400];
    W w = {buf, (int)sizeof(buf), 0};
    buf[0] = '\0';
    pr(&w, v, 0);
    int d = dec_digits_of(v);
    *dec = (signed char)(d > 100 ? 100 : d);
    return txt_dup(buf);
}

/* Ecrit l'ensemble : "X<-2 ; -1<=X<1 ; X=3 ; X>5". Une borne approchee, ou
 * toutes si force_dec (saisie decimale), est ecrite en decimal. */
static void w_set(W *w, Set *set, bool force_dec, bool *approx)
{
    char sep[2] = {CAS_SEPARATOR, 0};
    char lt[2] = {REL_LT, 0}, le[2] = {REL_LE, 0}, gt[2] = {REL_GT, 0}, ge[2] = {REL_GE, 0};
    for (int i = 0; i < set->n; i++)
    {
        Piece *pc = &set->p[i];
        if (force_dec)
            pc->lo.approx = pc->hi.approx = true;
        if (i > 0)
            w_str(w, sep);
        bool point = !pc->lo_inf && !pc->hi_inf && bcmp(pc->lo.x, pc->hi.x) == 0;
        if (point)
        {
            w_str(w, "X=");
            w_bnd(w, &pc->lo);
        }
        else if (pc->lo_inf)
        {
            w_str(w, "X");
            w_str(w, pc->hi_in ? le : lt);
            w_bnd(w, &pc->hi);
        }
        else if (pc->hi_inf)
        {
            w_str(w, "X");
            w_str(w, pc->lo_in ? ge : gt);
            w_bnd(w, &pc->lo);
        }
        else
        {
            w_bnd(w, &pc->lo);
            w_str(w, pc->lo_in ? le : lt);
            w_str(w, "X");
            w_str(w, pc->hi_in ? le : lt);
            w_bnd(w, &pc->hi);
        }
        if ((!pc->lo_inf && pc->lo.approx) || (!pc->hi_inf && !point && pc->hi.approx))
            *approx = true;
    }
}

static token *g_toks;

static cas_result g_res; /* hors pile : relu apres longjmp */

cas_result cas_eval(const char *text, int len, char *out, int cap)
{
    cas_result &res = g_res;
    memset(&res, 0, sizeof(res));
    res.approx = NAN;
    res.dec_digits = -1;
    if (!out || cap < 2)
    {
        res.status = CAS_UNSUPPORTED;
        return res;
    }
    out[0] = '\0';
    int ntok = 0;
    g_toks = parse_string_to_token((char *)text, len, &ntok);
    for (int i = 0; i < ntok; i++)
    {
        if (g_toks[i].type == 'M')
        {
            token_free_matrices(g_toks, ntok);
            free(g_toks);
            res.status = CAS_UNSUPPORTED;
            return res;
        }
    }
    /* Zone demandee d'un seul bloc ; si le tas est trop morcele pour la
     * fournir, on se rabat sur une zone plus petite plutot que de refuser. */
    size_t arena = CAS_ARENA_SIZE;
    g_buf = (uint8_t *)malloc(arena);
    while (!g_buf && arena > CAS_ARENA_MIN)
    {
        arena = arena > CAS_ARENA_MIN + CAS_ARENA_STEP ? arena - CAS_ARENA_STEP : CAS_ARENA_MIN;
        g_buf = (uint8_t *)malloc(arena);
    }
    if (!g_buf)
    {
        free(g_toks);
        res.status = CAS_ERROR;
        strcpy(res.msg, "Memoire insuffisante");
        return res;
    }
    g_cap = arena;
    g_used = 0;
    g_steps = 0;
    g_err = NULL;

    if (setjmp(g_jmp))
    {
        out[0] = '\0';
        memset(&res, 0, sizeof(res));
        res.approx = NAN;
        res.dec_digits = -1;
        if (g_err)
        {
            res.status = CAS_ERROR;
            snprintf(res.msg, sizeof(res.msg), "%s", g_err);
        }
        else
            res.status = CAS_UNSUPPORTED;
    }
    else
    {
        Parser p = {g_toks, ntok, 0, text, len};
        Expr *root = p_system(&p);
        if (p.i < p.n)
            fail("Syntaxe");
        W w = {out, cap, 0};
        out[0] = '\0';
        bool solve = root->k == E_EQ || root->k == E_SYS || (root->k == E_OP && root->f == CAS_TOK_SOLVE);
        Expr *arg = (solve && root->k == E_OP) ? root->a[0] : root;
        /* Une seule equation : resolution habituelle (solutions complexes,
         * "a 2k*pi pres"...), sauf valeur absolue ou racine, traitees comme
         * les inequations. Une inequation ou plusieurs relations : ensemble
         * de reels. */
        bool one_eq = solve && arg->k != E_SYS && !(arg->k == E_EQ && is_rel_type((char)arg->f));
        Set *set = NULL;
        bool truth_only = false;
        Expr *f = NULL;
        Sol s;
        memset(&s, 0, sizeof(s));
        g_set_searched = g_set_cplx = false;
        if (solve && one_eq)
        {
            f = arg->k == E_EQ ? sub2(S(arg->a[0]), S(arg->a[1])) : S(arg);
            Set *dom = raw_domain(arg);
            if (eq_needs_set(f))
            {
                set = rel_set(f, '=', 0);
                if (!set)
                    g_set_searched = g_set_cplx = false;
                else if (dom)
                    set = set_inter(set, dom);
            }
            if (!set)
            {
                solve_expr(f, &s);
                if (dom && !set_is_all(dom))
                {
                    if (s.all)
                        set = dom; /* X/X = 1 : vrai partout ou c'est defini */
                    else
                    {
                        /* X(X-1)/(X-1) = 1 se simplifie en X = 1, valeur
                         * interdite : on l'ecarte. */
                        int kept = 0;
                        for (int i = 0; i < s.n; i++)
                            if (has_sym_i(s.v[i]) || dom_has(dom, nv(s.v[i], NAN)))
                                s.v[kept++] = s.v[i];
                        s.n = kept;
                    }
                }
            }
        }
        else if (solve)
        {
            int nrel = arg->k == E_SYS ? arg->n : 1;
            bool any_x = false, ineq = false;
            set = set_all();
            for (int i = 0; i < nrel; i++)
            {
                Expr *e = arg->k == E_SYS ? arg->a[i] : arg;
                char rel = (e->k == E_EQ && is_rel_type((char)e->f)) ? (char)e->f : '=';
                Expr *f1 = e->k == E_EQ ? sub2(S(e->a[0]), S(e->a[1])) : S(e);
                any_x = any_x || has_x(e);
                ineq = ineq || rel != '=';
                Set *one = rel_set(f1, rel, 0);
                Set *dom = one ? raw_domain(e) : NULL;
                if (!one || !dom)
                    fail(rel == '=' ? "Equation non geree ici" : "Inequation non geree");
                set = set_inter(set_inter(set, one), dom);
            }
            if (!any_x)
            {
                /* Rien a resoudre : "2<3" est vrai ou faux. */
                strcpy(res.msg, set->n ? "Vrai" : "Faux");
                truth_only = true;
            }
            else if (ineq)
                g_set_cplx = false; /* une inequation ne concerne que les reels */
        }
        if (truth_only)
            ; /* "Vrai" ou "Faux" : message seul */
        else if (solve && set)
        {
            res.solutions = true;
            if (set_is_all(set))
                strcpy(res.msg, "Tous les reels");
            else if (set->n == 0)
                strcpy(res.msg, g_set_cplx ? "Aucune solution reelle"
                                           : (g_set_searched ? "Aucune solution trouvee" : "Aucune solution"));
            else
            {
                bool ap = false;
                w_set(&w, set, memchr(text, '.', (size_t)len) != NULL, &ap);
                res.approximate = ap;
                const Piece *pc = &set->p[0];
                if (set->n == 1 && !pc->lo_inf && !pc->hi_inf && bcmp(pc->lo.x, pc->hi.x) == 0)
                {
                    /* une seule solution : sa valeur decimale s'affiche a cote */
                    res.numeric = true;
                    res.approx = pc->lo.x;
                    if (!ap)
                        res.dec_digits = pc->lo.dec;
                }
            }
        }
        else if (solve)
        {
            /* Saisie avec un nombre decimal : solutions en decimal, comme
             * pour un calcul (2.0000001, e^X = 1.0000001...). */
            if (memchr(text, '.', (size_t)len))
                s.approx = true;
            res.solutions = true;
            res.approximate = s.approx;
            if (s.all)
                strcpy(res.msg, "Tous les reels");
            else if (s.n == 0)
                /* "Aucune solution" seulement quand c'est prouve. */
                strcpy(res.msg, s.real_only ? "Aucune solution reelle"
                                            : (s.searched ? "Aucune solution trouvee" : "Aucune solution"));
            else
            {
                if (s.partial)
                    strcpy(res.msg, "Liste partielle");
                else if (s.note)
                    snprintf(res.msg, sizeof(res.msg), "%s", s.note);
                else if (s.real_only)
                    strcpy(res.msg, "Solutions reelles");
                /* solutions reelles dans l'ordre croissant, complexes ensuite */
                int nreal = 0;
                for (int i = 0; i < s.n; i++)
                    if (!has_sym_i(s.v[i]))
                    {
                        Expr *t = s.v[nreal];
                        s.v[nreal++] = s.v[i];
                        s.v[i] = t;
                    }
                qsort(s.v, nreal, sizeof(Expr *), cmp_sol);
                for (int i = 0; i < s.n; i++)
                {
                    if (i > 0)
                    {
                        char sep[2] = {CAS_SEPARATOR, 0};
                        w_str(&w, sep);
                    }
                    w_str(&w, "X=");
                    if (s.approx && !has_sym_i(s.v[i]))
                        w_decimal(&w, nv(s.v[i], NAN));
                    else
                        pr(&w, s.v[i], 0);
                }
                if (s.n == 1)
                {
                    res.numeric = !has_sym_i(s.v[0]);
                    res.approx = nv(s.v[0], NAN);
                    if (!s.approx)
                        res.dec_digits = dec_digits_of(s.v[0]);
                }
            }
        }
        else
        {
            Expr *r;
            if (root->k == E_OP && root->f == CAS_TOK_FACTOR)
                r = apply_op(root);
            else
            {
                r = S(root);
                if (has_x(r))
                    r = best(r);
                else if (r->k == E_MUL && !has_sum_power(r, 5))
                {
                    /* (1+r(2))(1-r(2)) = -1 ; mais 2(3+p) reste tel quel */
                    Expr *x = EX(r);
                    if (esize(x) < esize(r))
                        r = x;
                }
            }
            pr(&w, r, 0);
            res.symbolic = has_x(r);
            res.numeric = !res.symbolic;
            res.transcendental = has_transc(r);
            if (res.numeric)
            {
                res.approx = nv(r, NAN);
                res.dec_digits = dec_digits_of(r);
            }
        }
        res.status = CAS_OK;
    }
    free(g_buf);
    g_buf = NULL;
    token_free_matrices(g_toks, ntok);
    free(g_toks);
    g_toks = NULL;
    return res;
}
