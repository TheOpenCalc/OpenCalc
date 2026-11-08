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

bool is_cas_type(char t)
{
    return t >= CAS_TOK_EXPAND && t <= CAS_TOK_SOLVE;
}

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
    E_EQ,  /* a[0] = a[1] (seulement dans resoudre) */
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

/* Decomposition en facteurs premiers d'un entier <= 10^12. */
struct Factors
{
    unsigned long long p[16];
    int e[16];
    int n;
};

static bool factorize(unsigned long long a, Factors *f)
{
    f->n = 0;
    if (a > 1000000000000ULL)
        return false;
    for (unsigned long long d = 2; d * d <= a; d += (d == 2 ? 1 : 2))
    {
        if (a % d)
            continue;
        int k = 0;
        while (a % d == 0)
        {
            a /= d;
            k++;
        }
        f->p[f->n] = d;
        f->e[f->n] = k;
        f->n++;
    }
    if (a > 1)
    {
        f->p[f->n] = a;
        f->e[f->n] = 1;
        f->n++;
    }
    return true;
}

/* A^(r/q) pour un entier A > 0 et 0 < r < q : k * m^(1/q') avec m sans
 * puissance q'-ieme. */
static Expr *radical(Big A, long long r, long long q)
{
    if (big_is_one(A))
        return inum(1);
    unsigned long long a;
    Factors f;
    if (!big_fits_u64(A, &a) || !factorize(a, &f))
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

static Expr *s_pow(Expr *b, Expr *x)
{
    tick();
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
    Q m = q_mul(t, q_int(12));
    if (!q_isint(m))
        return NULL;
    long long v;
    big_fits_ll(m.n, &v);
    Expr *r;
    switch (v)
    {
    case 0:
        return inum(0);
    case 2:
        r = qnum(1, 2);
        break;
    case 3:
        r = half_root(2);
        break;
    case 4:
        r = half_root(3);
        break;
    case 6:
        r = inum(1);
        break;
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

static Expr *EX(Expr *e)
{
    tick();
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
            if (!big_fits_ll(x->q.n, &k) || k > 40)
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
static Expr *best(Expr *e)
{
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
    static unsigned long long dp[512], dq[512];
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
                Q r = q_frac(s * (long long)dp[i], (long long)dq[j]);
                bool first = true;
                while (*deg >= 1 && q_sign(poly_at(P, *deg, r)) == 0)
                {
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
    if (!has_x(ex) || !poly_of(ex, c, &deg) || deg < 2)
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
    if (nr == 0)
        return best(e);
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
    if (d >= 1)
    {
        Expr **cc = (Expr **)amalloc(sizeof(Expr *) * (d + 1));
        for (int i = 0; i <= d; i++)
            cc[i] = num(P[i]);
        l_push(&fl, poly_expr(cc, d));
    }
    Q content = q_div(orig_lead, lead_prod);
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

static Expr *P(Expr *e, int depth)
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

/* ---------------------------- Resoudre ----------------------------------- */

#define MAX_SOL 12

struct Sol
{
    Expr *v[MAX_SOL];
    int n;
    bool approx;  /* au moins une solution approchee */
    bool all;     /* tous les reels */
};

static void sol_add(Sol *s, Expr *v)
{
    for (int i = 0; i < s->n; i++)
        if (eq(s->v[i], v))
            return;
    if (s->n < MAX_SOL)
        s->v[s->n++] = v;
}

static Expr *dec_num(double v)
{
    /* Valeur approchee gardee comme fraction decimale a 10 chiffres. */
    if (fabs(v) < 1e-12)
        return inum(0);
    double scale = pow(10.0, 9 - (int)floor(log10(fabs(v))));
    if (scale > 1e15)
        scale = 1e15;
    double r = round(v * scale);
    if (fabs(r) > 9e15)
        return inum((long long)v);
    return num(q_make(big_ll((long long)r), big_ll((long long)scale)));
}

/* Racines reelles approchees de f (variable X) sur [lo, hi]. */
static void numeric_roots(Expr *f, double lo, double hi, Sol *s)
{
    const int N = 4000;
    double h = (hi - lo) / N;
    double x0 = lo, y0 = nv(f, x0);
    for (int i = 1; i <= N && s->n < MAX_SOL; i++)
    {
        tick();
        double x1 = lo + i * h, y1 = nv(f, x1);
        if (y1 == 0)
        {
            sol_add(s, dec_num(x1));
            s->approx = true;
        }
        else if (isfinite(y0) && isfinite(y1) && y0 != 0 && ((y0 < 0) != (y1 < 0)))
        {
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
        x0 = x1;
        y0 = y1;
    }
}

static int cmp_sol(const void *a, const void *b)
{
    double x = nv(*(Expr *const *)a, NAN), y = nv(*(Expr *const *)b, NAN);
    return x < y ? -1 : (x > y ? 1 : 0);
}

static void solve_quadratic(Expr *a, Expr *b, Expr *c, Sol *s)
{
    Expr *v[3] = {inum(-4), a, c};
    Expr *delta = add2(s_pow(b, inum(2)), s_mul(v, 3));
    double dv = nv(delta, NAN);
    Expr *twoa = mul2(inum(2), a);
    Expr *mb = neg(b);
    if (is_val(delta, 0) || fabs(dv) < 1e-13 * (1 + fabs(nv(b, NAN)) * fabs(nv(b, NAN))))
    {
        sol_add(s, div2(mb, twoa));
        return;
    }
    if (dv > 0)
    {
        Expr *r = s_pow(delta, qnum(1, 2));
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
    Q dexp[16];
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
    Expr *g = has_x(den) ? EX(mul2(ex, den)) : ex;
    Expr *c[POLY_MAX + 1];
    int deg;
    if (!poly_of(g, c, &deg))
    {
        numeric_roots(f, -100, 100, s);
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
        Q roots[POLY_MAX];
        int mult[POLY_MAX];
        int d = deg;
        int nr = rational_roots(Pq, &d, roots, mult, POLY_MAX);
        for (int i = 0; i < nr; i++)
            sol_add(&tmp, num(roots[i]));
        for (int i = 0; i <= d; i++)
            c[i] = num(Pq[i]);
        deg = d;
    }
    if (deg == 1)
        sol_add(&tmp, neg(div2(c[0], c[1])));
    else if (deg == 2)
        solve_quadratic(c[2], c[1], c[0], &tmp);
    else if (deg >= 3)
    {
        /* Borne de Cauchy des racines. */
        double lead = fabs(nv(c[deg], NAN)), bound = 0;
        for (int i = 0; i < deg; i++)
        {
            double r = fabs(nv(c[i], NAN)) / lead;
            if (r > bound)
                bound = r;
        }
        bound += 1;
        numeric_roots(poly_expr(c, deg), -bound, bound, &tmp);
        tmp.approx = true;
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

static Expr *p_equation(Parser *p)
{
    Expr *l = p_sum(p);
    if (at(p, '='))
    {
        p->i++;
        Expr *e = node(E_EQ, 2);
        e->a[0] = l;
        e->a[1] = p_sum(p);
        return e;
    }
    return l;
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
        out[n++] = allow_eq ? p_equation(p) : p_sum(p);
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
    if (ty == ')' || ty == ',' || ty == '=' || ty == '+' || ty == '*' || ty == '/' || ty == '^')
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

static Expr *p_pow(Parser *p)
{
    Expr *b = p_post(p);
    if (at(p, '^'))
    {
        p->i++;
        return rpow(b, p_unary(p));
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

bool cas_required(const char *text, int len)
{
    if (!text)
        return false;
    for (int i = 0; i < len && text[i]; i++)
    {
        if (text[i] == 'X')
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

static token *g_toks;

static cas_result g_res; /* hors pile : relu apres longjmp */

cas_result cas_eval(const char *text, int len, char *out, int cap)
{
    cas_result &res = g_res;
    memset(&res, 0, sizeof(res));
    res.approx = NAN;
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
    g_buf = (uint8_t *)malloc(CAS_ARENA_SIZE);
    if (!g_buf)
    {
        free(g_toks);
        res.status = CAS_ERROR;
        strcpy(res.msg, "Memoire insuffisante");
        return res;
    }
    g_cap = CAS_ARENA_SIZE;
    g_used = 0;
    g_steps = 0;
    g_err = NULL;

    if (setjmp(g_jmp))
    {
        out[0] = '\0';
        memset(&res, 0, sizeof(res));
        res.approx = NAN;
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
        Expr *root = p_equation(&p);
        if (p.i < p.n)
            fail("Syntaxe");
        W w = {out, cap, 0};
        out[0] = '\0';
        bool solve = root->k == E_EQ || (root->k == E_OP && root->f == CAS_TOK_SOLVE);
        if (solve)
        {
            Expr *arg = root->k == E_OP ? root->a[0] : root;
            Expr *f = arg->k == E_EQ ? sub2(S(arg->a[0]), S(arg->a[1])) : S(arg);
            Sol s;
            memset(&s, 0, sizeof(s));
            solve_expr(f, &s);
            res.solutions = true;
            res.approximate = s.approx;
            if (s.all)
                strcpy(res.msg, "Tous les reels");
            else if (s.n == 0)
                strcpy(res.msg, "Aucune solution");
            else
            {
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
                    pr(&w, s.v[i], 0);
                }
                if (s.n == 1)
                {
                    res.numeric = !has_sym_i(s.v[0]);
                    res.approx = nv(s.v[0], NAN);
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
            }
            pr(&w, r, 0);
            res.symbolic = has_x(r);
            res.numeric = !res.symbolic;
            res.transcendental = has_transc(r);
            if (res.numeric)
                res.approx = nv(r, NAN);
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
