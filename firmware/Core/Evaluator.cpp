#include <cfloat>
#include <cstdbool>
#include <cstring>
#include <stdlib.h>
#include <cmath>
#include <math.h>
#include <stdio.h>
#include <string>
#include "stack.h"
#include "Evaluator.h"
#include "Cas.h"

__attribute__((weak)) int g_angle_mode = 0;
__attribute__((weak)) int g_decimals = 6;
__attribute__((weak)) complex g_ans = {0, 0};
matrix *g_ans_mat = NULL;

double g_seq_u = 0;
double g_seq_n = 0;

bool is_block_type(char t)
{
    return t == '@' || t == '#' || t == '&' || t == '?';
}

bool is_calculus_type(char t)
{
    return t == '{' || t == '}' || t == '_' || t == '"';
}

bool is_named_call_type(char t)
{
    return is_block_type(t) || t == '$' || t == ':' || t == ';' || t == '<' || t == '>' ||
           t == '%' || t == '`' || t == '|' || is_calculus_type(t) || is_cas_type(t);
}

/* Generateur pseudo-aleatoire (xorshift32), initialise au premier appel avec
 * une graine variable (opencalc_seed est redefinie dans Tools.cpp avec l'horloge). */
__attribute__((weak)) unsigned opencalc_seed(void)
{
    return 2463534242u;
}

static unsigned g_rng_state = 0;

static double rand01(void)
{
    if (g_rng_state == 0)
    {
        g_rng_state = opencalc_seed() ^ 0x9E3779B9u;
        if (g_rng_state == 0)
            g_rng_state = 2463534242u;
    }
    g_rng_state ^= g_rng_state << 13;
    g_rng_state ^= g_rng_state >> 17;
    g_rng_state ^= g_rng_state << 5;
    return (double)g_rng_state / 4294967296.0; // [0, 1)
}

double angle_in(double v)
{
    return g_angle_mode ? v * (3.14159265358979323846 / 180.0) : v;
}

double angle_out(double v)
{
    return g_angle_mode ? v * (180.0 / 3.14159265358979323846) : v;
}

complex angle_in_c(complex z)
{
    complex r = {angle_in(z.re), angle_in(z.im)};
    return r;
}

complex angle_out_c(complex z)
{
    complex r = {angle_out(z.re), angle_out(z.im)};
    return r;
}

void set_ans(complex v)
{
    g_ans = v;
    if (g_ans_mat)
    {
        mat_free(g_ans_mat);
        g_ans_mat = NULL;
    }
}

void set_ans_matrix(const matrix *m)
{
    if (!m)
        return;
    /* Copier d'abord : m peut etre g_ans_mat lui-meme (saisie "ans"). */
    matrix *copy = mat_clone(m);
    if (!copy)
        return;
    if (g_ans_mat)
        mat_free(g_ans_mat);
    g_ans_mat = copy;
    g_ans.re = NAN;
    g_ans.im = NAN;
}

bool tokens_use_matrix_ans(const token *toks, int n)
{
    if (!g_ans_mat)
        return false;
    for (int i = 0; i < n; i++)
        if (toks[i].type == 'z')
            return true;
    return false;
}

double fact_r(double in)
{
    double out = 0;
    if (in > 170)
        return NAN;
    else
    {
        out = 1;
        for (int i = 1; i <= in + 0.5; i++)
        {
            out *= i;
        }
    }
    return out;
}

double fact(double in)
{
    // !(in >= 0) refuse aussi NaN : fact(NaN) passait les tests ci-dessous
    // et rendait 1 (171!! = 1, pgcd(12.5,5)! = 1).
    if (!(in >= 0))
        return NAN;
    double k = std::round(in);
    if (std::fabs(in - k) > 1e-9 * std::fmax(1.0, k))
        return NAN;
    return fact_r(k);
}

double comb(double n, double k)
{
    if (!(n >= 0) || n != std::floor(n) || k != std::floor(k))
        return NAN;
    // Hors de 0..n : 0, comme le calcul formel (somme(binom(3,X),0,5) = 8).
    if (k < 0 || k > n)
        return 0;
    if (k > n - k)
        k = n - k;
    double out = 1;
    // Arret des que le produit deborde : binom(10^9, 5*10^8) tournait
    // 500 millions de fois apres avoir atteint inf.
    for (double i = 1; i <= k && std::isfinite(out); i++)
        out = out * (n - k + i) / i;
    if (!std::isfinite(out))
        return NAN;
    return std::round(out);
}

double perm(double n, double k)
{
    if (!(n >= 0) || n != std::floor(n) || k != std::floor(k))
        return NAN;
    if (k < 0 || k > n)
        return 0;
    double out = 1;
    for (double i = 0; i < k && std::isfinite(out); i++)
        out *= (n - i);
    if (!std::isfinite(out))
        return NAN;
    return std::round(out);
}

static bool integer_operands(double a, double b)
{
    return std::isfinite(a) && std::isfinite(b) &&
           a == std::floor(a) && b == std::floor(b);
}

// Greatest common divisor of two integers (result is non-negative).
static double arith_gcd(double a, double b)
{
    if (!integer_operands(a, b))
        return NAN;
    a = std::fabs(a);
    b = std::fabs(b);
    while (b > 0)
    {
        double r = std::fmod(a, b);
        a = b;
        b = r;
    }
    return a;
}

// Least common multiple of two integers (0 when one of them is 0).
static double arith_lcm(double a, double b)
{
    if (!integer_operands(a, b))
        return NAN;
    double g = arith_gcd(a, b);
    if (g == 0)
        return 0;
    return std::fabs(a / g * b);
}

// Division euclidienne : a = b*q + r avec 0 <= r < |b|, quel que soit le
// signe de b (17 = (-5)(-3) + 2, donc reste(17,-5) = 2, quot(17,-5) = -3).
static double arith_rem(double a, double b)
{
    if (!std::isfinite(a) || !std::isfinite(b) || b == 0)
        return NAN;
    double m = std::fabs(b);
    // fmod est exact ; a - m*floor(a/m) arrondissait au-dela de 2^53
    // (reste(10^17+1, 10) = 0).
    double r = std::fmod(a, m);
    if (r < 0)
        r += m;
    // Decimaux : 0.3 = 3*0.1 a l'arrondi pres, le reste est 0 et non ~0.1.
    if (!integer_operands(a, b) && m - r <= 4.0 * DBL_EPSILON * std::fabs(a))
        r = 0;
    return r;
}

static double arith_quot(double a, double b)
{
    if (!std::isfinite(a) || !std::isfinite(b) || b == 0)
        return NAN;
    return std::round((a - arith_rem(a, b)) / b);
}

// Racine n-ieme de x : x^(1/n). x < 0 seulement si n est un entier impair.
static double arith_root(double x, double n)
{
    if (!std::isfinite(x) || !std::isfinite(n) || n == 0)
        return NAN;
    if (x >= 0)
        return std::pow(x, 1.0 / n);
    double r = std::round(n);
    if (std::fabs(n - r) > 1e-9 || std::fmod(r, 2.0) == 0)
        return NAN;
    return -std::pow(-x, 1.0 / n);
}

// Entier aleatoire uniforme dans [a, b] (bornes entieres, ordre indifferent).
static double arith_randint(double a, double b)
{
    if (!std::isfinite(a) || !std::isfinite(b))
        return NAN;
    a = std::round(a);
    b = std::round(b);
    if (a > b)
    {
        double t = a;
        a = b;
        b = t;
    }
    return a + std::floor(rand01() * (b - a + 1));
}

// Dispatches the two-argument arithmetic functions.
static double arith_two(char op, double first, double second)
{
    switch (op)
    {
    case 'P':
        return arith_gcd(first, second);
    case 'L':
        return arith_lcm(first, second);
    case 'D':
        return arith_quot(first, second);
    case 'R':
        return arith_rem(first, second);
    case '<':
        return (std::isnan(first) || std::isnan(second)) ? NAN : (first < second ? first : second);
    case '>':
        return (std::isnan(first) || std::isnan(second)) ? NAN : (first > second ? first : second);
    case '%':
        return arith_root(first, second);
    case '|':
        return arith_randint(first, second);
    case '&': /* arigeo(a,b) = a*U + b */
        return first * g_seq_u + second;
    case '?': /* arithn(a,b) = U + a*N + b */
        return g_seq_u + first * g_seq_n + second;
    default:
        return NAN;
    }
}

static bool is_two_arg_op(char op)
{
    return op == 'q' || op == 'y' || op == 'P' || op == 'L' || op == 'D' || op == 'R' ||
           op == '&' || op == '?' || op == '<' || op == '>' || op == '%' || op == '|' ||
           is_calculus_type(op);
}


int double_to_string_scientific(double in, char *out)
{
    if (std::isnan(in))
    {
        strcpy(out, "nan");
        return 3;
    }
    if (std::isinf(in))
    {
        if (in < 0)
        {
            strcpy(out, "-inf");
            return 4;
        }
        strcpy(out, "inf");
        return 3;
    }

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.15g", in);

    int o = 0;
    for (int i = 0; buffer[i] != '\0'; i++)
    {
        if (buffer[i] == 'e')
        {
            out[o++] = '*';
            out[o++] = '1';
            out[o++] = '0';
            out[o++] = '^';
            i++;
            if (buffer[i] == '-')
            {
                out[o++] = '-';
                i++;
            }
            else if (buffer[i] == '+')
                i++;
            // "05" -> "5" (on garde au moins un chiffre)
            while (buffer[i] == '0' && buffer[i + 1] != '\0')
                i++;
            for (; buffer[i] != '\0'; i++)
                out[o++] = buffer[i];
            break;
        }
        out[o++] = buffer[i];
    }
    out[o] = '\0';
    return o;
}
bool is_in(char test, char *arr)
{
    int i = 0;
    while (arr[i] != '\0')
    {
        if (test == arr[i])
        {
            return true;
        }
        i++;
    }
    return false;
}

int count_yarded(char *in)
{
    int out = 0;
    for (int i = 0; in[i] != '\0'; i++)
    {
        if (!is_in(in[i], (char *)"()"))
        {
            out++;
        }
    }
    return out - 1;
}

int count_yarded(token *in, int n)
{
    int out = 0;
    for (int i = 0; i < n; i++)
    {
        if (!is_in(in[i].type, (char *)"(),"))
        {
            out++;
        }
    }
    return out;
}

/* Bruit d'une soustraction de nombres presque egaux : 0.1+0.2-0.3 donne
 * 5.5e-17, sqrt(2)^2-2 donne 4.4e-16. Un resultat de quelques ulp des
 * operandes vaut 0. Les vrais petits nombres (1.6e-19) ne sont pas touches. */
static double snap_sum(double r, double a, double b)
{
    if (!std::isfinite(r))
        return r;
    double m = std::fmax(std::fabs(a), std::fabs(b));
    if (std::fabs(r) > 4.0 * DBL_EPSILON * m || r == 0.0)
        return r;
    /* Un resultat court en decimal (0.5, 0.25, 3) n'est pas du bruit :
     * 10^15+0.5-10^15 vaut bien 0.5 (avant : 0). Le bruit d'arrondi, lui,
     * est une puissance de 2 sans ecriture courte (5.551115123125783e-17). */
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6g", r);
    if (strtod(buf, nullptr) == r)
        return r;
    return 0.0;
}

static complex snap_csum(complex r, complex a, complex b)
{
    return complex{snap_sum(r.re, a.re, b.re), snap_sum(r.im, a.im, b.im)};
}

bool higher_priority(char a, char b)
{
    if (a == '~')
        return b != '^';
    if (a == '!')
        return true;
    // ^ est associatif a droite : 2^3^2 = 2^(3^2), comme le calcul formel.
    if (a == '^')
        return b != '^';
    if (b == '^')
    {
        return false;
    }
    if (is_in(a, (char *)"*/") && is_in(b, (char *)"*/"))
        return true;
    if (is_in(a, (char *)"+-") && is_in(b, (char *)"+-"))
        return true;
    if (is_in(a, (char *)"+-") && is_in(b, (char *)"*/"))
    {
        return false;
    }
    if (is_in(a, (char *)"*/") && is_in(b, (char *)"-+"))
    {
        return true;
    }
    return false;
}

// True when a token of this type ends an operand, so that a following '-' is
// a binary subtraction. Anywhere else (start, after an operator, '(' or ',')
// the '-' is a unary minus.
static bool operand_end_type(char t)
{
    return t == 'n' || t == 'X' || t == 'e' || t == 'p' || t == 'i' || t == 'z' ||
           t == 'M' || t == ')' || t == '!' || is_in(t, (char *)"ABCEIJKMNQTUVWYZ");
}

token *shunting_yard(token *input, int n)
{
    char *function = (char *)"lrcstuvwfghjak!bdmFGHoqyPLDRSO@#&?$:;<>%|`";
    char *operato = (char *)"+-*/^";
    char *letters = (char *)"ABCEIJKMNQTUVWYZ";
    // La sortie RPN ne contient jamais plus de jetons que l'entree (les
    // parentheses et virgules disparaissent, '-' unaire devient '~').
    int out_cap = n > 0 ? n + 1 : 1;
    token *output = (token *)malloc(sizeof(token) * out_cap);
    memset(output, 0, sizeof(token) * out_cap);

    int output_size = 0;
    stack Operator_stack;
    init(&Operator_stack, n);
    for (int i = 0; i < n; i++)
    {
        if (input[i].type == '-' && (i == 0 || !operand_end_type(input[i - 1].type)))
        {
            // Unary minus: becomes the prefix operator '~', applied to the
            // next operand once every binary operator weaker than it is done.
            push(&Operator_stack, '~');
            continue;
        }
        if (is_in(input[i].type, letters) || input[i].type == 'n' || input[i].type == 'X' || input[i].type == 'e' || input[i].type == 'p' || input[i].type == 'i' || input[i].type == 'z' || input[i].type == 'M')
        {
            output[output_size].value = input[i].value;
            output[output_size].type = input[i].type;
            output[output_size].mat = input[i].mat;
            output_size++;
        }
        else if (input[i].type == '!')
        {
            // Factorielle postfixe : s'applique a l'operande qui precede.
            // Apres une puissance "2^(3)!", la boite d'exposant est fermee
            // et l'ecran montre (2^3)! : on sort d'abord les ^ en attente.
            while (!isEmpty(&Operator_stack) && peek(&Operator_stack) == '^')
            {
                output[output_size].type = '^';
                output_size++;
                pop(&Operator_stack);
            }
            output[output_size].type = '!';
            output_size++;
        }
        else if (is_in(input[i].type, function))
        {
            push(&Operator_stack, input[i].type);
        }
        else if (is_in(input[i].type, operato))
        {
            while (is_in(input[i].type, operato) && higher_priority(peek(&Operator_stack), input[i].type))
            {
                output[output_size].type = peek(&Operator_stack);
                output_size++;
                pop(&Operator_stack);
            }
            push(&Operator_stack, input[i].type);
        }
        else if (input[i].type == '(')
        {
            push(&Operator_stack, input[i].type);
        }
        else if (input[i].type == ',')
        {
            while (!isEmpty(&Operator_stack) && peek(&Operator_stack) != '(')
            {
                output[output_size].type = peek(&Operator_stack);
                pop(&Operator_stack);
                output_size++;
            }
        }
        else if (input[i].type == ')')
        {
            // Une ')' sans '(' correspondante ne doit pas vider la pile a
            // l'infini (boucle "Stack Underflow" puis plantage).
            while (!isEmpty(&Operator_stack) && peek(&Operator_stack) != '(')
            {
                output[output_size].type = peek(&Operator_stack);
                pop(&Operator_stack);
                output_size++;
            }
            if (!isEmpty(&Operator_stack))
                pop(&Operator_stack);
            if (is_in(peek(&Operator_stack), function))
            {
                output[output_size].type = peek(&Operator_stack);
                pop(&Operator_stack);
                output_size++;
            }
        }
    }
    while (!isEmpty(&Operator_stack))
    {
        output[output_size].type = peek(&Operator_stack);
        pop(&Operator_stack);
        output_size++;
    }
    free(&Operator_stack);
    return output;
}


/* sin et cos d'un argument reel. Pres d'un multiple de pi/2, le resultat
 * n'est que l'erreur d'arrondi de x (sin(pi) = 1.2e-16) : on rend 0, pour
 * que 5/sin(pi) et tan(pi/2) soient des erreurs. Le seuil suit |x|, donc
 * sin(1e-11) reste 1e-11 (l'ancien seuil fixe de 1e-10 le mettait a 0). */
static double trig_zero(double v, double x)
{
    double tol = 4 * 2.220446049250313e-16 * std::fabs(x);
    // Au-dela de |x| ~ 1e9 le seuil effacerait des valeurs justes
    // (sin(10^15) = 0,858) : on rend alors le resultat tel quel.
    if (tol > 1e-6)
        return v;
    return std::fabs(v) <= tol ? 0.0 : v;
}
static double rsin(double x) { return trig_zero(std::sin(x), x); }
static double rcos(double x) { return trig_zero(std::cos(x), x); }
static double rtan(double x)
{
    double c = rcos(x);
    return c == 0 ? NAN : rsin(x) / c;
}

bool almost_equal(double a, double b) {
    double eps = 1e-10;
    return std::fabs(a - b) < eps;
}

static double real_pow(double b, double a);

double evaluate_npi(token *in, int n, double x, int letter)
{
    stack_d nb_stack;
    double a;
    double b;
    init(&nb_stack, n);
    g_stack_underflow = false;

    for (int i = 0; i < n; i++)
    {
        if (in[i].type == 'X')
        {
            if ((int)(in[i].value) + (int)'a' == letter)
            {
                push(&nb_stack, x);
            }
            else
            {
                push(&nb_stack, (double)0);
            }
        }
        else if (in[i].type == 'n')
        {
            push(&nb_stack, in[i].value);
        }
        else if (is_in(in[i].type, (char *)"+-*/^"))
        { // operator requiring 2 input
            a = peek(&nb_stack);
            pop(&nb_stack);
            b = peek(&nb_stack);
            pop(&nb_stack);
            switch (in[i].type)
            {
            case '+':
                push(&nb_stack, snap_sum(a + b, a, b));
                break;
            case '*':
                push(&nb_stack, a * b);
                break;
            case '-':
                push(&nb_stack, snap_sum(b - a, a, b));
                break;
            case '/':
                if (a == 0)
                {
                    free(&nb_stack);
                    return NAN;
                }
                push(&nb_stack, b / a);
                break;
            case '^':
                push(&nb_stack, real_pow(b, a));
                break;
            default:
                break;
            }
        }
        else if (in[i].type == '`')
        { // alea() : pas d'argument
            push(&nb_stack, rand01());
        }
        else if (in[i].type == 'p' || in[i].type == 'e' || in[i].type == 'i' || in[i].type == 'z')
        { // zero-argument constants
            if (in[i].type == 'p')
                push(&nb_stack, 3.14159265358979323846264338327);
            else if (in[i].type == 'e')
                push(&nb_stack, 2.71828182845904523536);
            else if (in[i].type == 'z')
                push(&nb_stack, g_ans.re);
            else
                push(&nb_stack, NAN);
        }
        else if (is_two_arg_op(in[i].type))
        { // functions requiring 2 arguments
            a = peek(&nb_stack);
            pop(&nb_stack);
            b = peek(&nb_stack);
            pop(&nb_stack);
            if (in[i].type == 'q')
                push(&nb_stack, comb(b, a));
            else if (in[i].type == 'y')
                push(&nb_stack, perm(b, a));
            else
                push(&nb_stack, arith_two(in[i].type, b, a));
        }
        else
        { // in case of function requiring single argument
            a = peek(&nb_stack);
            pop(&nb_stack);
            switch (in[i].type)
            {
            case 'r':
                a = sqrt(a);
                break;
            case 'l':
                a = log(a);
                break;
            case 'o':
                a = std::log10(a);
                break;
            case 'c':
                a = rcos(angle_in(a));
                break;
            case 's':
                a = rsin(angle_in(a));
                break;
            case 't':
                a = rtan(angle_in(a));
                break;
            case 'u':
                a = angle_out(acos(a));
                break;
            case 'v':
                a = angle_out(asin(a));
                break;
            case 'w':
                a = angle_out(atan(a));
                break;
            case 'f':
                a = cosh(a);
                break;
            case 'g':
                a = sinh(a);
                break;
            case 'h':
                a = tanh(a);
                break;
            case 'j':
                a = asinh(a);
                break;
            case 'k':
                a = atanh(a);
                break;
            case 'a':
                a = acosh(a);
                break;
            case 'd':
                a = angle_out(std::atan2(0.0, a));
                break;
            case 'm':
                a = std::fabs(a);
                break;
            case 'S':
                a = std::fabs(a);
                break;
            case '~':
                a = -a;
                break;
            case '@':
                a = a + g_seq_u;
                break;
            case '#':
                a = a * g_seq_u;
                break;
            case '$':
                a = std::exp(a);
                break;
            case ':':
                a = std::floor(a);
                break;
            case ';':
                a = std::isnan(a) ? a : (a > 0 ? 1.0 : (a < 0 ? -1.0 : 0.0));
                break;
            case 'O':
                a = std::round(a);
                break;
            case '!':
                a = fact(a);
                break;
            default:
                break;
            }
            push(&nb_stack, a);
        }
    }
    /* Saisie incomplete ("2+", "sin()", ")") : il doit rester exactement
     * un nombre sur la pile, sans operande manquant en chemin. */
    bool incomplete = g_stack_underflow || nb_stack.top != 0;
    a = peek(&nb_stack);
    free(&nb_stack);
    return incomplete ? NAN : a;
}
double evaluate_npi(token *in, int n)
{
    stack_d nb_stack;
    double a;
    double b;
    init(&nb_stack, n);
    g_stack_underflow = false;
    int nb_frac = 0;
    for (int i = 0; i < n; i++)
    {
        if (in[i].type == 'n')
        {
            push(&nb_stack, in[i].value);
        }
        else if (is_in(in[i].type, (char *)"+-*/^"))
        { // operator requiring 2 input
            a = peek(&nb_stack);
            pop(&nb_stack);
            b = peek(&nb_stack);
            pop(&nb_stack);
            switch (in[i].type)
            {
            case '+':
                push(&nb_stack, snap_sum(a + b, a, b));
                break;
            case '*':
                push(&nb_stack, a * b);
                break;
            case '-':
                push(&nb_stack, snap_sum(b - a, a, b));
                break;
            case '/':
                push(&nb_stack, b / a);
                break;
            case '^':
                push(&nb_stack, real_pow(b, a));
                break;
            default:
                break;
            }
        }
        else if (in[i].type == '`')
        { // alea() : pas d'argument
            push(&nb_stack, rand01());
        }
        else if (in[i].type == 'p' || in[i].type == 'e' || in[i].type == 'i' || in[i].type == 'z')
        { // zero-argument constants
            if (in[i].type == 'p')
                push(&nb_stack, 3.14159265358979323846264338327);
            else if (in[i].type == 'e')
                push(&nb_stack, 2.71828182845904523536);
            else if (in[i].type == 'z')
                push(&nb_stack, g_ans.re);
            else
                push(&nb_stack, NAN);
        }
        else if (is_two_arg_op(in[i].type))
        { // functions requiring 2 arguments
            a = peek(&nb_stack);
            pop(&nb_stack);
            b = peek(&nb_stack);
            pop(&nb_stack);
            if (in[i].type == 'q')
                push(&nb_stack, comb(b, a));
            else if (in[i].type == 'y')
                push(&nb_stack, perm(b, a));
            else
                push(&nb_stack, arith_two(in[i].type, b, a));
        }
        else
        { // in case of function requiring single argument
            a = peek(&nb_stack);
            pop(&nb_stack);
            switch (in[i].type)
            {
            case 'r':
                a = sqrt(a);
                break;
            case 'l':
                a = log(a);
                break;
            case 'o':
                a = std::log10(a);
                break;
            case 'c':
                a = rcos(angle_in(a));
                break;
            case 's':
                a = rsin(angle_in(a));
                break;
            case 't':
                a = rtan(angle_in(a));
                break;
            case 'u':
                a = angle_out(acos(a));
                break;
            case 'v':
                a = angle_out(asin(a));
                break;
            case 'w':
                a = angle_out(atan(a));
                break;
            case 'f':
                a = cosh(a);
                break;
            case 'g':
                a = sinh(a);
                break;
            case 'h':
                a = tanh(a);
                break;
            case 'j':
                a = asinh(a);
                break;
            case 'k':
                a = atanh(a);
                break;
            case 'a':
                a = acosh(a);
                break;
            case 'd':
                a = angle_out(std::atan2(0.0, a));
                break;
            case 'm':
                a = std::fabs(a);
                break;
            case 'S':
                a = std::fabs(a);
                break;
            case '~':
                a = -a;
                break;
            case '@':
                a = a + g_seq_u;
                break;
            case '#':
                a = a * g_seq_u;
                break;
            case '$':
                a = std::exp(a);
                break;
            case ':':
                a = std::floor(a);
                break;
            case ';':
                a = std::isnan(a) ? a : (a > 0 ? 1.0 : (a < 0 ? -1.0 : 0.0));
                break;
            case 'O':
                a = std::round(a);
                break;
            case '!':
                a = fact(a);
                break;
            default:
                break;
            }
            push(&nb_stack, a);
        }
    }
    /* Saisie incomplete ("2+", "sin()", ")") : il doit rester exactement
     * un nombre sur la pile, sans operande manquant en chemin. */
    bool incomplete = g_stack_underflow || nb_stack.top != 0;
    a = peek(&nb_stack);
    free(&nb_stack);
    return incomplete ? NAN : a;
}

////////////// Complex numbers //////////////

static const complex C_I = {0, 1};
static const complex C_ONE = {1, 0};
static const complex C_PI = {3.14159265358979323846264338327, 0};
static const complex C_E = {2.71828182845904523536, 0};
static const complex C_LN10 = {2.302585092994045684017991454684, 0};

complex cadd(complex a, complex b)
{
    complex r = {a.re + b.re, a.im + b.im};
    return r;
}

complex csub(complex a, complex b)
{
    complex r = {a.re - b.re, a.im - b.im};
    return r;
}

complex cmul(complex a, complex b)
{
    complex r = {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
    return r;
}

complex cdiv(complex a, complex b)
{
    if (b.re == 0 && b.im == 0)
    {
        complex nan = {NAN, NAN};
        return nan;
    }
    if (b.im == 0)
    {
        complex r = {a.re / b.re, a.im / b.re};
        return r;
    }
    // Smith : evite |b|^2, qui deborde des 1e154 ou s'annule des 1e-162.
    if (std::fabs(b.re) >= std::fabs(b.im))
    {
        double t = b.im / b.re, d = b.re + b.im * t;
        complex r = {(a.re + a.im * t) / d, (a.im - a.re * t) / d};
        return r;
    }
    double t = b.re / b.im, d = b.re * t + b.im;
    complex r = {(a.re * t + a.im) / d, (a.im * t - a.re) / d};
    return r;
}

complex cexp(complex z)
{
    if (z.im == 0) // exp(710) = inf, et non inf*0 = nan en partie imaginaire
        return complex{std::exp(z.re), 0.0};
    double e = std::exp(z.re);
    complex r = {e * std::cos(z.im), e * std::sin(z.im)};
    return r;
}

complex clog(complex z)
{
    // -0 imaginary parts (from a negation) must not flip the branch: ln(-1)=+i*pi.
    if (z.im == 0)
        z.im = 0.0;
    double r = std::hypot(z.re, z.im);
    complex r2 = {std::log(r), std::atan2(z.im, z.re)};
    return r2;
}

/* Base negative, exposant p/q avec q impair : racine reelle, comme le
 * calcul exact et racine(x,n). (-32)^(0.2) = -2 comme (-32)^(1/5), et non
 * la racine principale complexe 1,618+1,176i. Rend false sinon. */
static bool odd_root_pow(double base, double e, double *out)
{
    if (!(base < 0) || !std::isfinite(e) || e == std::floor(e))
        return false;
    for (int q = 3; q <= 999; q += 2)
    {
        double pq = e * q;
        double p = std::round(pq);
        if (std::fabs(pq - p) <= 1e-10 * std::fmax(1.0, std::fabs(pq)))
        {
            double m = std::pow(-base, e);
            bool odd = std::fmod(std::fabs(p), 2.0) == 1.0;
            *out = odd ? -m : m;
            return true;
        }
    }
    return false;
}

static double real_pow(double b, double a)
{
    double r;
    if (odd_root_pow(b, a, &r))
        return r;
    return std::pow(b, a);
}

complex cpow(complex a, complex b)
{
    if (a.re == 0 && a.im == 0)
    {
        if (b.re == 0 && b.im == 0)
            return C_ONE;
        if (b.re > 0)
        {
            complex zero = {0, 0};
            return zero;
        }
        complex nan = {NAN, NAN}; // 0^(-1) : division par zero
        return nan;
    }
    double rr;
    if (a.im == 0 && b.im == 0 && odd_root_pow(a.re, b.re, &rr))
        return complex{rr, 0.0};
    // Base reelle, exposant entier : pow, quel que soit l'exposant
    // ((-1.0)^(1000001) donnait -1+3.4e-10i par exp(b ln a)).
    if (a.im == 0 && b.im == 0 && b.re == std::floor(b.re))
    {
        complex r = {std::pow(a.re, b.re), 0};
        return r;
    }
    if (b.im == 0 && b.re == std::floor(b.re) && std::fabs(b.re) <= 1024)
    {
        // Exposant entier : produits successifs (2^(3) = 8 exactement,
        // au lieu de 7.9999999999999982 par exp(3 ln 2)).
        long n = (long)std::fabs(b.re);
        complex r = C_ONE, p = a;
        while (n)
        {
            if (n & 1)
                r = cmul(r, p);
            p = cmul(p, p);
            n >>= 1;
        }
        return b.re < 0 ? cdiv(C_ONE, r) : r;
    }
    if (a.im == 0 && a.re > 0 && b.im == 0)
    {
        complex r = {std::pow(a.re, b.re), 0};
        return r;
    }
    return cexp(cmul(b, clog(a)));
}

complex csqrt(complex z)
{
    if (z.im == 0 && z.re >= 0)
    {
        complex r = {std::sqrt(z.re), 0};
        return r;
    }
    if (z.im == 0)
        z.im = 0.0; // sqrt(-1) = +i, not -i
    // Forme sans cos/sin : partie reelle exactement nulle pour un negatif.
    // Mise a l'echelle pour que hypot et la somme ne debordent pas.
    double s = std::fmax(std::fabs(z.re), std::fabs(z.im));
    double x = z.re / s, y = z.im / s;
    double h = std::hypot(x, y);
    double rs = std::sqrt(s);
    complex r2;
    if (x >= 0)
    {
        double t = std::sqrt((h + x) / 2);
        r2.re = t * rs;
        r2.im = y / (2 * t) * rs;
    }
    else
    {
        double t = std::sqrt((h - x) / 2);
        r2.re = std::fabs(y) / (2 * t) * rs;
        r2.im = std::copysign(t, y) * rs;
    }
    return r2;
}

complex csin(complex z)
{
    if (z.im == 0)
    {
        complex r = {rsin(z.re), 0};
        return r;
    }
    complex r = {std::sin(z.re) * std::cosh(z.im), std::cos(z.re) * std::sinh(z.im)};
    return r;
}

complex ccos(complex z)
{
    if (z.im == 0)
    {
        complex r = {rcos(z.re), 0};
        return r;
    }
    complex r = {std::cos(z.re) * std::cosh(z.im), -std::sin(z.re) * std::sinh(z.im)};
    return r;
}

complex ctan(complex z)
{
    return cdiv(csin(z), ccos(z));
}

complex casin(complex z)
{
    // asin(-z) = -asin(z) : i*z + sqrt(1-z^2) s'annule pour Re(z) << 0.
    if (z.re < 0)
    {
        complex r = casin(complex{-z.re, -z.im});
        return complex{-r.re, -r.im};
    }
    // -i * ln(i*z + sqrt(1 - z^2))
    complex iz = cmul(C_I, z);
    complex z2 = cmul(z, z);
    complex s = csqrt(csub(C_ONE, z2));
    return cmul(complex{-0.0, -1.0}, clog(cadd(iz, s)));
}

complex cacos(complex z)
{
    // acos(z) = pi/2 - asin(z) : la formule directe z + i*sqrt(1-z^2)
    // s'annule pour z reel grand (acos(10^8) donnait nan).
    complex a = casin(z);
    return complex{1.5707963267948966 - a.re, -a.im};
}

complex catan(complex z)
{
    // (i/2) * (ln(1 - i*z) - ln(1 + i*z))
    complex a = clog(csub(C_ONE, cmul(C_I, z)));
    complex b = clog(cadd(C_ONE, cmul(C_I, z)));
    complex half_i = {0.0, 0.5};
    return cmul(half_i, csub(a, b));
}

complex csinh(complex z)
{
    if (z.im == 0)
        return complex{std::sinh(z.re), 0.0};
    complex r = {std::sinh(z.re) * std::cos(z.im), std::cosh(z.re) * std::sin(z.im)};
    return r;
}

complex ccosh(complex z)
{
    if (z.im == 0)
        return complex{std::cosh(z.re), 0.0};
    complex r = {std::cosh(z.re) * std::cos(z.im), std::sinh(z.re) * std::sin(z.im)};
    return r;
}

complex ctanh(complex z)
{
    if (z.im == 0)
        return complex{std::tanh(z.re), 0.0};
    // Au-dela, sinh et cosh debordent (inf/inf = nan) ; tanh vaut +-1.
    if (std::fabs(z.re) > 20)
        return complex{std::copysign(1.0, z.re), 0.0};
    return cdiv(csinh(z), ccosh(z));
}

complex casinh(complex z)
{
    if (z.im == 0)
    {
        complex r = {std::asinh(z.re), 0};
        return r;
    }
    // asinh(-z) = -asinh(z) : z + sqrt(z^2+1) s'annule pour Re(z) << 0.
    if (z.re < 0)
    {
        complex m = {-z.re, -z.im};
        complex r = casinh(m);
        r.re = -r.re;
        r.im = -r.im;
        return r;
    }
    // ln(z + sqrt(z^2 + 1))
    complex z2 = cmul(z, z);
    return clog(cadd(z, csqrt(cadd(z2, C_ONE))));
}

complex cacosh(complex z)
{
    // ln(z + sqrt(z - 1) * sqrt(z + 1))
    complex a = csqrt(csub(z, C_ONE));
    complex b = csqrt(cadd(z, C_ONE));
    return clog(cadd(z, cmul(a, b)));
}

complex catanh(complex z)
{
    // (1/2) * (ln(1 + z) - ln(1 - z))
    complex half = {0.5, 0};
    return cmul(half, csub(clog(cadd(C_ONE, z)), clog(csub(C_ONE, z))));
}

complex cfact(complex z)
{
    if (z.im != 0)
    {
        complex nan = {NAN, NAN};
        return nan;
    }
    complex r = {fact(z.re), 0};
    return r;
}

complex cconj(complex z)
{
    complex r = {z.re, -z.im};
    return r;
}

complex carg(complex z)
{
    if (z.im == 0)
        z.im = 0.0; // arg(-1) = pi, pas -pi
    complex r = {std::atan2(z.im, z.re), 0};
    return r;
}

complex cmod(complex z)
{
    complex r = {std::hypot(z.re, z.im), 0};
    return r;
}

complex clog10(complex z)
{
    return cdiv(clog(z), C_LN10);
}

static complex eval_c_core(token *in, int n, const complex *vals, const int *letters, int nvar, bool strict);

complex evaluate_npi_c_var(token *in, int n, complex x, int letter)
{
    return eval_c_core(in, n, &x, &letter, letter >= 0 ? 1 : 0, false);
}

/* Variables nommees letters[v] (code de la lettre, 'X'...) valant vals[v] ;
 * une lettre absente vaut 0 (le Solveur s'en sert pour extraire les
 * coefficients d'un systeme lineaire). */
complex evaluate_npi_c_vars(token *in, int n, const complex *vals, const int *letters, int nvar)
{
    return eval_c_core(in, n, vals, letters, nvar, false);
}

/* strict : une lettre sans valeur est une erreur (Calculatrice : "1.5E3" ou
 * "A+1" ne doivent pas valoir 1.5 ou 1 en silence). */
static complex eval_c_core(token *in, int n, const complex *vals, const int *letters, int nvar, bool strict)
{
    stack_c nb_stack;
    complex a;
    complex b;
    init(&nb_stack, n);
    g_stack_underflow = false;
    for (int i = 0; i < n; i++)
    {
        if (in[i].type == 'X')
        { // variable
            complex t = {0, 0};
            if (strict)
                t.re = t.im = NAN;
            int ch = (int)(in[i].value) + (int)'a';
            for (int v = 0; v < nvar; v++)
                if (letters[v] == ch)
                {
                    t = vals[v];
                    break;
                }
            push(&nb_stack, t);
        }
        else if (in[i].type == 'n')
        {
            complex t = {in[i].value, 0};
            push(&nb_stack, t);
        }
        else if (is_in(in[i].type, (char *)"+-*/^"))
        { // operator requiring 2 input
            a = peek(&nb_stack);
            pop(&nb_stack);
            b = peek(&nb_stack);
            pop(&nb_stack);
            switch (in[i].type)
            {
            case '+':
                push(&nb_stack, snap_csum(cadd(a, b), a, b));
                break;
            case '*':
                push(&nb_stack, cmul(a, b));
                break;
            case '-':
                push(&nb_stack, snap_csum(csub(b, a), b, a));
                break;
            case '/':
                push(&nb_stack, cdiv(b, a));
                break;
            case '^':
                push(&nb_stack, cpow(b, a));
                break;
            default:
                break;
            }
        }
        else if (in[i].type == '`')
        { // alea()
            complex t = {rand01(), 0};
            push(&nb_stack, t);
        }
        else if (in[i].type == 'p' || in[i].type == 'e' || in[i].type == 'i' || in[i].type == 'z')
        { // zero-argument constants
            complex t;
            if (in[i].type == 'p')
                t = C_PI;
            else if (in[i].type == 'e')
                t = C_E;
            else if (in[i].type == 'z')
                t = g_ans;
            else
                t = C_I;
            push(&nb_stack, t);
        }
        else if (is_two_arg_op(in[i].type))
        { // functions requiring 2 arguments
            a = peek(&nb_stack);
            pop(&nb_stack);
            b = peek(&nb_stack);
            pop(&nb_stack);
            complex r = {NAN, NAN};
            // Arguments reels seulement (binom(5+i,2) valait 10).
            if (a.im != 0 || b.im != 0)
                ;
            else if (in[i].type == 'q')
                r = (complex){comb(b.re, a.re), 0};
            else if (in[i].type == 'y')
                r = (complex){perm(b.re, a.re), 0};
            else
                r = (complex){arith_two(in[i].type, b.re, a.re), 0};
            push(&nb_stack, r);
        }
        else
        { // single-argument function
            a = peek(&nb_stack);
            pop(&nb_stack);
            switch (in[i].type)
            {
            case 'r':
                a = csqrt(a);
                break;
            case 'l':
                a = clog(a);
                break;
            case 'o':
                a = clog10(a);
                break;
            case 'c':
                a = ccos(angle_in_c(a));
                break;
            case 's':
                a = csin(angle_in_c(a));
                break;
            case 't':
                a = ctan(angle_in_c(a));
                break;
            case 'u':
                a = angle_out_c(cacos(a));
                break;
            case 'v':
                a = angle_out_c(casin(a));
                break;
            case 'w':
                a = angle_out_c(catan(a));
                break;
            case 'f':
                a = ccosh(a);
                break;
            case 'g':
                a = csinh(a);
                break;
            case 'h':
                a = ctanh(a);
                break;
            case 'a':
                a = cacosh(a);
                break;
            case 'j':
                a = casinh(a);
                break;
            case 'k':
                a = catanh(a);
                break;
            case 'b':
                a = cconj(a);
                break;
            case 'd':
                a = angle_out_c(carg(a));
                break;
            case 'm':
                a = cmod(a);
                break;
            case 'S':
                a = cmod(a);
                break;
            case '~':
                a.re = -a.re;
                a.im = -a.im;
                break;
            case '@':
                a = cadd(a, (complex){g_seq_u, 0});
                break;
            case '#':
                a = cmul(a, (complex){g_seq_u, 0});
                break;
            case '$':
                a = cexp(a);
                break;
            case ':':
                a.re = std::floor(a.re);
                a.im = std::floor(a.im);
                break;
            case ';':
                if (a.im == 0)
                    a.re = std::isnan(a.re) ? a.re : (a.re > 0 ? 1.0 : (a.re < 0 ? -1.0 : 0.0));
                else
                {
                    double m = std::hypot(a.re, a.im);
                    a.re /= m;
                    a.im /= m;
                }
                break;
            case 'O':
                a.re = std::round(a.re);
                a.im = std::round(a.im);
                break;
            case '!':
                a = cfact(a);
                break;
            default:
                break;
            }
            push(&nb_stack, a);
        }
    }
    /* Saisie incomplete ("2+", "sin()", ")") : il doit rester exactement
     * un nombre sur la pile, sans operande manquant en chemin. */
    bool incomplete = g_stack_underflow || nb_stack.top != 0;
    a = peek(&nb_stack);
    free(&nb_stack);
    if (incomplete)
        a.re = a.im = NAN;
    return a;
}

complex evaluate_npi_c(token *in, int n)
{
    return eval_c_core(in, n, nullptr, nullptr, 0, true);
}

typedef struct
{
    bool is_matrix;
    complex scalar;
    matrix *m;
    bool owns;
} MatValue;

static void mv_free(MatValue *v)
{
    if (v->is_matrix && v->m && v->owns)
    {
        mat_free(v->m);
        v->m = NULL;
        v->owns = false;
    }
}

static MatValue mv_scalar(complex s)
{
    MatValue v;
    v.is_matrix = false;
    v.scalar = s;
    v.m = NULL;
    v.owns = false;
    return v;
}

static MatValue mv_mat(matrix *m)
{
    MatValue v;
    v.is_matrix = true;
    v.scalar = (complex){0, 0};
    v.m = m;
    v.owns = true;
    return v;
}

static MatValue mv_error(void)
{
    MatValue v;
    v.is_matrix = false;
    v.scalar = (complex){NAN, NAN};
    v.m = NULL;
    v.owns = false;
    return v;
}

static MatValue mv_add(MatValue a, MatValue b)
{
    if (!a.is_matrix && !b.is_matrix)
        return mv_scalar(cadd(a.scalar, b.scalar));
    if (a.is_matrix && b.is_matrix)
    {
        bool ok;
        matrix *r = mat_add(a.m, b.m, &ok);
        if (!ok)
            return mv_error();
        return mv_mat(r);
    }
    matrix *r = a.is_matrix ? mat_clone(a.m) : mat_clone(b.m);
    if (!r)
        return mv_error();
    complex s = a.is_matrix ? b.scalar : a.scalar;
    for (int k = 0; k < r->rows * r->cols; k++)
        r->data[k] = cadd(r->data[k], s);
    return mv_mat(r);
}

static MatValue mv_sub(MatValue a, MatValue b)
{
    // a is the right operand, b the left one: results are b - a.
    if (!a.is_matrix && !b.is_matrix)
        return mv_scalar(csub(b.scalar, a.scalar));
    if (a.is_matrix && b.is_matrix)
    {
        bool ok;
        matrix *r = mat_sub(b.m, a.m, &ok);
        if (!ok)
            return mv_error();
        return mv_mat(r);
    }
    matrix *r = mat_clone(a.is_matrix ? a.m : b.m);
    if (!r)
        return mv_error();
    if (a.is_matrix)
    {
        // right operand is the matrix, left is the scalar: s - M
        for (int k = 0; k < r->rows * r->cols; k++)
            r->data[k] = csub(b.scalar, r->data[k]);
    }
    else
    {
        // left operand is the matrix, right is the scalar: M - s
        for (int k = 0; k < r->rows * r->cols; k++)
            r->data[k] = csub(r->data[k], a.scalar);
    }
    return mv_mat(r);
}

static MatValue mv_mul(MatValue a, MatValue b)
{
    if (!a.is_matrix && !b.is_matrix)
        return mv_scalar(cmul(a.scalar, b.scalar));
    if (a.is_matrix && b.is_matrix)
    {
        bool ok;
        matrix *r = mat_mul(b.m, a.m, &ok);
        if (!ok)
            return mv_error();
        return mv_mat(r);
    }
    matrix *r = mat_scale(a.is_matrix ? a.m : b.m, a.is_matrix ? b.scalar : a.scalar);
    if (!r)
        return mv_error();
    return mv_mat(r);
}

static MatValue mv_div(MatValue a, MatValue b)
{
    // a is the right operand, b the left one: results are b / a.
    if (!a.is_matrix && !b.is_matrix)
        return mv_scalar(cdiv(b.scalar, a.scalar));
    if (!a.is_matrix && b.is_matrix)
    {
        // matrix / scalar
        if (a.scalar.re == 0 && a.scalar.im == 0)
            return mv_error();
        matrix *r = mat_scale(b.m, cdiv((complex){1, 0}, a.scalar));
        if (!r)
            return mv_error();
        return mv_mat(r);
    }
    if (a.is_matrix && b.is_matrix)
    {
        bool ok;
        matrix *inv = mat_invert(a.m, &ok);
        if (!ok || !inv)
            return mv_error();
        matrix *r = mat_mul(b.m, inv, &ok);
        mat_free(inv);
        if (!ok || !r)
            return mv_error();
        return mv_mat(r);
    }
    return mv_error();
}

static MatValue mv_pow(MatValue a, MatValue b)
{
    if (!a.is_matrix && !b.is_matrix)
        return mv_scalar(cpow(b.scalar, a.scalar));
    if (b.is_matrix && !a.is_matrix)
    {
        if (a.scalar.im != 0)
            return mv_error();
        int n = (int)std::round(a.scalar.re);
        if (std::fabs(a.scalar.re - n) > 1e-10)
            return mv_error();
        bool ok;
        matrix *r = mat_power(b.m, n, &ok);
        if (!ok || !r)
            return mv_error();
        return mv_mat(r);
    }
    return mv_error();
}

static MatValue mv_det(MatValue a)
{
    if (!a.is_matrix)
        return mv_error();
    bool ok;
    complex d = mat_det(a.m, &ok);
    if (!ok)
        return mv_error();
    return mv_scalar(d);
}

static MatValue mv_inv(MatValue a)
{
    if (!a.is_matrix)
    {
        complex one = {1, 0};
        return mv_scalar(cdiv(one, a.scalar));
    }
    bool ok;
    matrix *r = mat_invert(a.m, &ok);
    if (!ok || !r)
        return mv_error();
    return mv_mat(r);
}

static MatValue mv_trans(MatValue a)
{
    if (!a.is_matrix)
        return mv_scalar(a.scalar);
    matrix *r = mat_transpose(a.m);
    if (!r)
        return mv_error();
    return mv_mat(r);
}

calc_result evaluate_calc(token *in, int n)
{
    MatValue *st = (MatValue *)malloc(sizeof(MatValue) * (n > 0 ? n : 1));
    int top = 0;
    for (int i = 0; i < n; i++)
    {
        char t = in[i].type;
        if (t == 'X')
        {
            complex z = {0, 0};
            st[top++] = mv_scalar(z);
        }
        else if (t == 'n')
        {
            complex z = {in[i].value, 0};
            st[top++] = mv_scalar(z);
        }
        else if (t == 'M')
        {
            MatValue v = mv_mat(in[i].mat);
            v.owns = false; // borrowed from token stream
            st[top++] = v;
        }
        else if (t == '`')
        {
            complex z = {rand01(), 0};
            st[top++] = mv_scalar(z);
        }
        else if (t == 'z' && g_ans_mat)
        {
            MatValue v = mv_mat(g_ans_mat);
            v.owns = false; // emprunte a g_ans_mat
            st[top++] = v;
        }
        else if (t == 'p' || t == 'e' || t == 'i' || t == 'z')
        {
            complex z;
            if (t == 'p')
                z = C_PI;
            else if (t == 'e')
                z = C_E;
            else if (t == 'z')
                z = g_ans;
            else
                z = C_I;
            st[top++] = mv_scalar(z);
        }
        else if (is_in(t, (char *)"+-*/^"))
        {
            if (top < 2)
            {
                // Malformed RPN (e.g. a unary minus the parser does not
                // support): fail cleanly instead of reading below the stack.
                while (top > 0)
                {
                    top--;
                    mv_free(&st[top]);
                }
                st[top++] = mv_error();
                continue;
            }
            MatValue a = st[top - 1];
            top--;
            MatValue b = st[top - 1];
            top--;
            MatValue r;
            switch (t)
            {
            case '+':
                r = mv_add(a, b);
                break;
            case '-':
                r = mv_sub(a, b);
                break;
            case '*':
                r = mv_mul(a, b);
                break;
            case '/':
                r = mv_div(a, b);
                break;
            default:
                r = mv_pow(a, b);
                break;
            }
            mv_free(&a);
            mv_free(&b);
            st[top++] = r;
        }
        else if (is_two_arg_op(t))
        { // functions requiring 2 arguments
            if (top < 2)
            {
                while (top > 0)
                {
                    top--;
                    mv_free(&st[top]);
                }
                st[top++] = mv_error();
                continue;
            }
            MatValue a = st[top - 1];
            top--;
            MatValue b = st[top - 1];
            top--;
            complex r = {NAN, NAN};
            if (!a.is_matrix && !b.is_matrix)
            {
                if (t == 'q')
                    r = (complex){comb(b.scalar.re, a.scalar.re), 0};
                else if (t == 'y')
                    r = (complex){perm(b.scalar.re, a.scalar.re), 0};
                else if (a.scalar.im == 0 && b.scalar.im == 0)
                    r = (complex){arith_two(t, b.scalar.re, a.scalar.re), 0};
            }
            else
            {
                mv_free(&a);
                mv_free(&b);
                st[top++] = mv_error();
                continue;
            }
            mv_free(&a);
            mv_free(&b);
            st[top++] = mv_scalar(r);
        }
        else
        {
            if (top < 1)
            {
                st[top++] = mv_error();
                continue;
            }
            MatValue a = st[top - 1];
            top--;
            MatValue r = mv_error();
            switch (t)
            {
            case 'F':
                r = mv_det(a);
                break;
            case 'G':
                r = mv_inv(a);
                break;
            case 'H':
                r = mv_trans(a);
                break;
            case 'r':
                if (!a.is_matrix)
                    r = mv_scalar(csqrt(a.scalar));
                break;
            case 'l':
                if (!a.is_matrix)
                    r = mv_scalar(clog(a.scalar));
                break;
            case 'o':
                if (!a.is_matrix)
                    r = mv_scalar(clog10(a.scalar));
                break;
            case 'c':
                if (!a.is_matrix)
                    r = mv_scalar(ccos(angle_in_c(a.scalar)));
                break;
            case 's':
                if (!a.is_matrix)
                    r = mv_scalar(csin(angle_in_c(a.scalar)));
                break;
            case 't':
                if (!a.is_matrix)
                    r = mv_scalar(ctan(angle_in_c(a.scalar)));
                break;
            case 'u':
                if (!a.is_matrix)
                    r = mv_scalar(angle_out_c(cacos(a.scalar)));
                break;
            case 'v':
                if (!a.is_matrix)
                    r = mv_scalar(angle_out_c(casin(a.scalar)));
                break;
            case 'w':
                if (!a.is_matrix)
                    r = mv_scalar(angle_out_c(catan(a.scalar)));
                break;
            case 'f':
                if (!a.is_matrix)
                    r = mv_scalar(ccosh(a.scalar));
                break;
            case 'g':
                if (!a.is_matrix)
                    r = mv_scalar(csinh(a.scalar));
                break;
            case 'h':
                if (!a.is_matrix)
                    r = mv_scalar(ctanh(a.scalar));
                break;
            case 'a':
                if (!a.is_matrix)
                    r = mv_scalar(cacosh(a.scalar));
                break;
            case 'j':
                if (!a.is_matrix)
                    r = mv_scalar(casinh(a.scalar));
                break;
            case 'k':
                if (!a.is_matrix)
                    r = mv_scalar(catanh(a.scalar));
                break;
            case 'b':
                if (!a.is_matrix)
                    r = mv_scalar(cconj(a.scalar));
                break;
            case 'd':
                if (!a.is_matrix)
                    r = mv_scalar(angle_out_c(carg(a.scalar)));
                break;
            case 'm':
                if (!a.is_matrix)
                    r = mv_scalar(cmod(a.scalar));
                break;
            case 'S':
                if (!a.is_matrix)
                    r = mv_scalar(cmod(a.scalar));
                break;
            case '$':
                if (!a.is_matrix)
                    r = mv_scalar(cexp(a.scalar));
                break;
            case ':':
                if (!a.is_matrix)
                {
                    complex z = a.scalar;
                    z.re = std::floor(z.re);
                    z.im = std::floor(z.im);
                    r = mv_scalar(z);
                }
                break;
            case ';':
                if (!a.is_matrix)
                {
                    complex z = a.scalar;
                    if (z.im == 0)
                        z.re = std::isnan(z.re) ? z.re : (z.re > 0 ? 1.0 : (z.re < 0 ? -1.0 : 0.0));
                    else
                    {
                        double m = std::hypot(z.re, z.im);
                        z.re /= m;
                        z.im /= m;
                    }
                    r = mv_scalar(z);
                }
                break;
            case '~':
                if (a.is_matrix)
                {
                    matrix *neg = mat_scale(a.m, (complex){-1, 0});
                    r = neg ? mv_mat(neg) : mv_error();
                }
                else
                    r = mv_scalar((complex){-a.scalar.re, -a.scalar.im});
                break;
            case 'O':
                if (!a.is_matrix)
                {
                    complex z = a.scalar;
                    z.re = std::round(z.re);
                    z.im = std::round(z.im);
                    r = mv_scalar(z);
                }
                break;
            case '!':
                if (!a.is_matrix)
                    r = mv_scalar(cfact(a.scalar));
                break;
            default:
                break;
            }
            mv_free(&a);
            st[top++] = r;
        }
    }
    MatValue res = top > 0 ? st[top - 1] : mv_error();
    free(st);
    calc_result cr;
    cr.is_matrix = res.is_matrix;
    cr.scalar = res.scalar;
    cr.m = res.m;
    cr.owns = res.is_matrix && res.owns;
    return cr;
}

void calc_result_free(calc_result *r)
{
    if (r->is_matrix && r->m && r->owns)
    {
        mat_free(r->m);
        r->m = NULL;
        r->owns = false;
    }
}

void token_free_matrices(token *toks, int n)
{
    if (!toks)
        return;
    for (int i = 0; i < n; i++)
    {
        if (toks[i].type == 'M' && toks[i].mat)
        {
            mat_free(toks[i].mat);
            toks[i].mat = NULL;
        }
    }
}

// Arrondi d'affichage : g_decimals decimales quand |v| >= 1, mais
// g_decimals chiffres significatifs quand |v| < 1. Avant, 1/100000000
// arrondi a 6 decimales donnait 0.
static double round_for_display(double v)
{
    if (v == 0 || !std::isfinite(v) || g_decimals < 0 || g_decimals > 15)
        return v;
    int digits = g_decimals;
    int mag = (int)std::floor(std::log10(std::fabs(v)));
    if (mag < 0)
    {
        int sig = g_decimals > 0 ? g_decimals : 1;
        digits = sig - 1 - mag;
    }
    if (digits > 300)
        return v;
    double f = std::pow(10.0, digits);
    double r = std::round(v * f) / f;
    return std::isfinite(r) ? r : v;
}

int complex_to_string(complex in, char *out)
{
    if (std::isnan(in.re) || std::isnan(in.im)) 
{
    strcpy(out, "nan");
    return 3;
}
if (std::isinf(in.re) || std::isinf(in.im))
{
    if (std::isinf(in.re) && in.re < 0) { strcpy(out, "-inf"); return 4; }
    strcpy(out, "inf");
    return 3;
}
    // Supprime une partie negligeable devant l'autre (e^(ip) = -1+1.2e-16i).
    // Pas de seuil absolu : 1.6*10^(-19) s'affiche. Le bruit des
    // soustractions est retire au calcul (snap_sum), sin(pi) par trig_zero.
    double ar = std::fabs(in.re), ai = std::fabs(in.im);
    if (ar < 1e-10 * ai)
        in.re = 0;
    if (ai < 1e-10 * ar)
        in.im = 0;
    in.re = round_for_display(in.re);
    in.im = round_for_display(in.im);

    if (in.im == 0)
    {
        return double_to_string_scientific(in.re, out);
    }

    int len = 0;
    if (in.re != 0)
    {
        len = double_to_string_scientific(in.re, out);
        out[len] = in.im > 0 ? '+' : '-';
        out[len + 1] = '\0';
        len++;
    }
    else if (in.im < 0)
    {
        out[0] = '-';
        out[1] = '\0';
        len = 1;
    }

    char imbuf[256];
    double abs_im = std::fabs(in.im);
    int iml;
    if (abs_im == 1.0)
    {
        iml = 0;
    }
    else
    {
        iml = double_to_string_scientific(abs_im, imbuf);
        for (int i = 0; i < iml; i++)
        {
            out[len++] = imbuf[i];
        }
    }
    out[len++] = 'i';
    out[len] = '\0';
    return len;
}

char *prompt_input(int max_size)
{
    char *in = (char *)malloc(sizeof(char) * max_size);
    for (int i = 0; i < max_size; i++)
    {
        in[i] = EOF;
    }
    scanf("%[^\n]s", in);
    return in;
}

bool need_implicit_star(char next_type, char last_type)
{
    // Insert implicit multiplication when an operand-start token (a bare constant
    // like i/e/p, an opening parenthesis, or a variable) follows a complete
    // operand (number, closing paren, or a bare constant/variable): "2i", "3+2i",
    // "2(3+4i)", "iX", "2X", "Xi", "X(2)", "i(2)".
    // 'z' = Ans : "ans(2)", "ans p", "ans r(4)" sont des produits.
    char *operand_end = (char *)"n)iXep]!z";
    bool last_is_var = last_type >= 'A' && last_type <= 'Z' &&
                       !is_in(last_type, (char *)"FGHPLDRSO");
    // Prefix functions (sqrt 'r', ln 'l', cos 'c', ...) also start an operand:
    // "2r(3)" is 2*sqrt(3), not sqrt(3).
    if ((is_in(next_type, (char *)"iep([zoqy") || is_in(next_type, (char *)"lrcstuvwfghjakbdm") ||
         (next_type >= 'A' && next_type <= 'Z') ||
         is_named_call_type(next_type)) &&
        (is_in(last_type, operand_end) || last_is_var))
        return true;
    return false;
}

/* Valeur exacte (correctement arrondie) des chiffres in[start..end[, ou la
 * virgule vaut un point. Accumuler chiffre par chiffre donnait 0.3 =
 * 0.30000000000000004. */
static double digits_to_double(const char *in, int start, int end)
{
    char small[64];
    int len = end - start;
    char *buf = len < (int)sizeof(small) ? small : (char *)malloc(len + 1);
    if (!buf)
        return NAN;
    for (int k = 0; k < len; k++)
        buf[k] = in[start + k] == ',' ? '.' : in[start + k];
    buf[len] = '\0';
    double v = strtod(buf, NULL);
    if (buf != small)
        free(buf);
    return v;
}

static bool parse_plain_number(char *in, int n, int *pos, double *out)
{
    int start = *pos;
    double v = 0;
    bool any = false;
    while (*pos < n && in[*pos] >= '0' && in[*pos] <= '9')
    {
        v = v * 10 + (in[*pos] - '0');
        any = true;
        (*pos)++;
    }
    if (*pos < n && in[*pos] == '.')
    {
        (*pos)++;
        double mult = 0.1;
        while (*pos < n && in[*pos] >= '0' && in[*pos] <= '9')
        {
            v += (in[*pos] - '0') * mult;
            mult *= 0.1;
            any = true;
            (*pos)++;
        }
    }
    *out = any ? digits_to_double(in, start, *pos) : v;
    return any;
}

// Parses one matrix-literal element: a real ("3", "-2.5"), an imaginary
// ("4i", "-i") or a full complex value ("3+4i", "1-i"). At most two signed
// terms, at most one of them carrying an 'i'.
static complex parse_element_literal(char *in, int n, int *pos, bool *ok)
{
    complex v = {0, 0};
    *ok = false;
    bool any = false;
    for (int term = 0; term < 2; term++)
    {
        if (*pos >= n)
            break;
        char c = in[*pos];
        bool is_digit = c >= '0' && c <= '9';
        if (c != '+' && c != '-' && c != 'i' && c != '.' && !is_digit)
            break;
        double sign = 1.0;
        if (c == '+' || c == '-')
        {
            if (c == '-')
                sign = -1.0;
            (*pos)++;
        }
        double num = 0;
        bool has_num = parse_plain_number(in, n, pos, &num);
        bool has_i = *pos < n && in[*pos] == 'i';
        if (has_i)
            (*pos)++;
        if (!has_num && !has_i)
        {
            // dangling sign like "[1+,2]"
            return v;
        }
        any = true;
        if (has_i)
        {
            v.im += sign * (has_num ? num : 1.0);
            break; // only one imaginary unit per element
        }
        v.re += sign * num;
    }
    if (!any)
        return v;
    *ok = true;
    return v;
}

static complex *parse_row_literal(char *in, int n, int *pos, int *out_len, bool *ok)
{
    // expects in[*pos] == '['
    *ok = true;
    (*pos)++;
    complex *row = NULL;
    int rlen = 0, cap = 0;
    while (*pos < n && in[*pos] != ']')
    {
        bool eok;
        complex e;
        if (in[*pos] == ',')
        {
            // empty cell (template like "[[,]]" or a gap being filled)
            e.re = NAN;
            e.im = NAN;
            eok = true;
        }
        else
        {
            e = parse_element_literal(in, n, pos, &eok);
        }
        if (!eok)
        {
            *ok = false;
            break;
        }
        if (rlen == cap)
        {
            cap = cap ? cap * 2 : 8;
            row = (complex *)realloc(row, sizeof(complex) * cap);
        }
        row[rlen++] = e;
        if (*pos < n && in[*pos] == ',')
        {
            (*pos)++;
            if (*pos < n && in[*pos] == ']')
            {
                // empty last cell before the row close
                if (rlen == cap)
                {
                    cap = cap ? cap * 2 : 8;
                    row = (complex *)realloc(row, sizeof(complex) * cap);
                }
                row[rlen].re = NAN;
                row[rlen].im = NAN;
                rlen++;
            }
        }
        else if (*pos < n && in[*pos] != ']')
        {
            *ok = false;
            break;
        }
    }
    if (!*ok || *pos >= n || in[*pos] != ']')
    {
        *ok = false;
        free(row);
        return NULL;
    }
    (*pos)++;
    *out_len = rlen;
    return row;
}

// Arithmetic functions recognised by name in the input text.
static const struct
{
    const char *name;
    char type;
} g_arith_names[] = {
    {"pgcd", 'P'},
    {"ppcm", 'L'},
    {"quot", 'D'},
    {"reste", 'R'},
    {"arrondi", 'O'},
    {"abs", 'S'},
    {"arithn", '?'}, /* avant "arith" : meme prefixe */
    {"arigeo", '&'},
    {"arith", '@'},
    {"geom", '#'},
    {"exp", '$'},
    {"ent", ':'},
    {"signe", ';'},
    {"min", '<'},
    {"max", '>'},
    {"racine", '%'},
    {"alea", '`'},
    {"randint", '|'},
    /* Analyse (Core/Calculus.cpp) : evaluees sur le texte, hors RPN. */
    {"somme", '{'},
    {"produit", '}'},
    {"derivee", '_'},
    {"integrale", '"'},
    /* Calcul formel (Core/Cas.cpp) : evalues hors RPN. */
    {"developper", CAS_TOK_EXPAND},
    {"factoriser", CAS_TOK_FACTOR},
    {"deriver", CAS_TOK_DERIVE},
    {"primitive", CAS_TOK_PRIMITIVE},
    {"resoudre", CAS_TOK_SOLVE},
};

static matrix *parse_matrix_literal(char *in, int n, int *pos)
{
    // parses "[e1,e2,...]" or "[[..],[..],...]" starting at in[*pos]=='['
    if (*pos >= n || in[*pos] != '[')
        return NULL;
    int start = *pos;
    bool is_matrix = (*pos + 1 < n && in[*pos + 1] == '[');
    complex **rows = (complex **)malloc(sizeof(complex *) * (MATRIX_MAX_DIM + 1));
    int *rowlens = (int *)malloc(sizeof(int) * (MATRIX_MAX_DIM + 1));
    int rcount = 0;
    bool ok = true;

    if (is_matrix)
    {
        (*pos)++; // skip the matrix-level '['
        while (*pos < n && in[*pos] == '[')
        {
            if (rcount >= MATRIX_MAX_DIM)
            {
                ok = false;
                break;
            }
            int rlen;
            complex *row = parse_row_literal(in, n, pos, &rlen, &ok);
            if (!ok)
                break;
            rows[rcount] = row;
            rowlens[rcount] = rlen;
            rcount++;
            if (*pos < n && in[*pos] == ',')
            {
                (*pos)++;
                continue;
            }
            break;
        }
        if (ok && (*pos >= n || in[*pos] != ']'))
            ok = false;
        if (ok)
            (*pos)++;
    }
    else
    {
        int rlen;
        complex *row = parse_row_literal(in, n, pos, &rlen, &ok);
        if (ok)
        {
            rows[0] = row;
            rowlens[0] = rlen;
            rcount = 1;
        }
    }

    if (ok)
    {
        int ncols = rowlens[0];
        for (int r = 1; r < rcount; r++)
            if (rowlens[r] != ncols)
            {
                ok = false;
                break;
            }
        if (ok && (rcount == 0 || ncols == 0))
            ok = false;
        if (ok && (rcount > MATRIX_MAX_DIM || ncols > MATRIX_MAX_DIM))
            ok = false;
    }

    if (!ok)
    {
        for (int r = 0; r < rcount; r++)
            free(rows[r]);
        free(rows);
        free(rowlens);
        // Consume through the bracket that opened the literal so a malformed
        // literal tokenizes as a single NaN placeholder instead of leaking
        // its raw contents into the token stream.
        int depth = 0;
        int j = start;
        while (j < n)
        {
            if (in[j] == '[')
                depth++;
            else if (in[j] == ']')
            {
                depth--;
                if (depth == 0)
                {
                    j++;
                    break;
                }
            }
            j++;
        }
        *pos = j;
        return NULL;
    }

    matrix *m = mat_create(rcount, rowlens[0]);
    if (!m)
    {
        for (int r = 0; r < rcount; r++)
            free(rows[r]);
        free(rows);
        free(rowlens);
        return NULL;
    }
    for (int r = 0; r < rcount; r++)
        for (int c = 0; c < rowlens[0]; c++)
            m->data[r * rowlens[0] + c] = rows[r][c];
    for (int r = 0; r < rcount; r++)
        free(rows[r]);
    free(rows);
    free(rowlens);
    return m;
}

token *parse_string_to_token(char *in, int n, int *tokenized_size)
{ // suppose input is correct, i'm too lazy to handle this, random behavior if not
    if (n == 0)
    {
        token *out = (token *)malloc(sizeof(token));
        out[0].type = 'n';
        out[0].src_pos = -1;
        out[0].value = 0;
        out[0].mat = NULL;
        *tokenized_size = 1;
        return out;
    }
    if (n >= 3 && in[0] == 'i' && in[1] == 'n' && in[2] == 'f')
    {
        token *out = (token *)malloc(sizeof(token));
        out[0].type = 'n';
        out[0].src_pos = -1;
        out[0].value = INFINITY;
        out[0].mat = NULL;
        *tokenized_size = 1;
        return out;
    }
    if (n >= 3 && in[0] == 'n' && in[1] == 'a' && in[2] == 'n')
{
    token *out = (token *)malloc(sizeof(token));
    out[0].type = 'n';
    out[0].src_pos = -1;
    out[0].value = NAN;
    out[0].mat = NULL;
    *tokenized_size = 1;
    return out;
}
    if (n >= 4 && in[0] == '-' && in[1] == 'i' && in[2] == 'n' && in[3] == 'f')
    {
        token *out = (token *)malloc(sizeof(token));
        out[0].type = 'n';
        out[0].src_pos = -1;
        out[0].value = -INFINITY;
        out[0].mat = NULL;
        *tokenized_size = 1;
        return out;
    }
    int OUT_S = n * 3;
    for (int i = 0; i < n; i++)
    {
        if (in[i] == '!')
        {
            OUT_S += 2;
        }
    }
    token *out = (token *)malloc(sizeof(token) * (OUT_S));
    memset(out, 0, sizeof(token) * OUT_S);
    int out_size = 0;
    char last_type = 0;
    char *nb = (char *)"0123456789";
    char *funop = (char *)"+-*/arcstuvwfghijkel)pX=^bdm";
    token cur;
    cur.value = 0;
    cur.type = 'n';
    cur.mat = NULL;
    char paren_kind[64];
    int depth = 0;
    bool pending = false;
    int num_start = 0;

    for (int i = 0; i < n; i++)
    {
    retry:
        if (in[i] == 'd' && i + 2 < n && in[i + 1] == 'e' && in[i + 2] == 't')
        {
            if (need_implicit_star('F', last_type))
            {
                out[out_size].type = '*';
                out[out_size].src_pos = -1;
                last_type = '*';
                out_size++;
            }
            out[out_size].type = 'F';
            out[out_size].value = 0;
            out[out_size].src_pos = i;
            last_type = 'F';
            out_size++;
            i += 2;
            continue;
        }
        if (in[i] == 'i' && i + 2 < n && in[i + 1] == 'n' && in[i + 2] == 'v')
        {
            if (need_implicit_star('G', last_type))
            {
                out[out_size].type = '*';
                out[out_size].src_pos = -1;
                last_type = '*';
                out_size++;
            }
            out[out_size].type = 'G';
            out[out_size].value = 0;
            out[out_size].src_pos = i;
            last_type = 'G';
            out_size++;
            i += 2;
            continue;
        }
        if (in[i] == 't' && i + 4 < n && in[i + 1] == 'r' && in[i + 2] == 'a' && in[i + 3] == 'n' && in[i + 4] == 's')
        {
            if (need_implicit_star('H', last_type))
            {
                out[out_size].type = '*';
                out[out_size].src_pos = -1;
                last_type = '*';
                out_size++;
            }
            out[out_size].type = 'H';
            out[out_size].value = 0;
            out[out_size].src_pos = i;
            last_type = 'H';
            out_size++;
            i += 4;
            continue;
        }
        if (in[i] == 'l' && i + 2 < n && in[i + 1] == 'o' && in[i + 2] == 'g')
        {
            if (need_implicit_star('o', last_type))
            {
                out[out_size].type = '*';
                out[out_size].src_pos = -1;
                last_type = '*';
                out_size++;
            }
            out[out_size].type = 'o';
            out[out_size].value = 'o' - 'a';
            out[out_size].src_pos = i;
            last_type = 'o';
            out_size++;
            i += 2;
            continue;
        }
        if (in[i] == 'a' && i + 2 < n && in[i + 1] == 'n' && in[i + 2] == 's')
        {
            if (need_implicit_star('z', last_type))
            {
                out[out_size].type = '*';
                out[out_size].src_pos = -1;
                last_type = '*';
                out_size++;
            }
            out[out_size].type = 'z';
            out[out_size].value = 0;
            out[out_size].src_pos = i;
            last_type = 'z';
            out_size++;
            i += 2;
            continue;
        }
        if (in[i] == 'b' && i + 4 < n && in[i + 1] == 'i' && in[i + 2] == 'n' && in[i + 3] == 'o' && in[i + 4] == 'm')
        {
            if (need_implicit_star('q', last_type))
            {
                out[out_size].type = '*';
                out[out_size].src_pos = -1;
                last_type = '*';
                out_size++;
            }
            out[out_size].type = 'q';
            out[out_size].value = 'q' - 'a';
            out[out_size].src_pos = i;
            last_type = 'q';
            out_size++;
            i += 4;
            continue;
        }
        if (in[i] == 'p' && i + 3 < n && in[i + 1] == 'e' && in[i + 2] == 'r' && in[i + 3] == 'm')
        {
            if (need_implicit_star('y', last_type))
            {
                out[out_size].type = '*';
                out[out_size].src_pos = -1;
                last_type = '*';
                out_size++;
            }
            out[out_size].type = 'y';
            out[out_size].value = 'y' - 'a';
            out[out_size].src_pos = i;
            last_type = 'y';
            out_size++;
            i += 3;
            continue;
        }
        {
            bool matched_arith = false;
            for (int f = 0; f < (int)(sizeof(g_arith_names) / sizeof(g_arith_names[0])); f++)
            {
                const char *nm = g_arith_names[f].name;
                int len = (int)strlen(nm);
                if (i + len > n || strncmp(in + i, nm, len) != 0)
                    continue;
                char type = g_arith_names[f].type;
                // Un bloc de suite n'est reconnu que suivi de '('.
                if (is_named_call_type(type) && (i + len >= n || in[i + len] != '('))
                    continue;
                if (need_implicit_star(type, last_type))
                {
                    out[out_size].type = '*';
                    out[out_size].src_pos = -1;
                    last_type = '*';
                    out_size++;
                }
                out[out_size].type = type;
                out[out_size].value = type - 'a';
                out[out_size].src_pos = i;
                last_type = type;
                out_size++;
                i += len - 1;
                matched_arith = true;
                break;
            }
            if (matched_arith)
                continue;
        }
        if (in[i] == '[')
        {
            int mpos = i;
            matrix *m = parse_matrix_literal(in, n, &mpos);
            if (mpos <= i)
                mpos = i + 1;
            if (need_implicit_star('[', last_type))
            {
                out[out_size].type = '*';
                out[out_size].src_pos = -1;
                last_type = '*';
                out_size++;
            }
            if (m)
            {
                out[out_size].type = 'M';
                out[out_size].mat = m;
                out[out_size].value = 0;
                out[out_size].src_pos = i;
                last_type = ']';
                out_size++;
            }
            else
            {
                out[out_size].type = 'n';
                out[out_size].value = NAN;
                out[out_size].src_pos = i;
                last_type = 'n';
                out_size++;
            }
            i = mpos - 1;
            continue;
        }
        if (is_in(in[i], nb))
        {
            // Nombre juste apres une constante, une variable ou ')' : produit
            // implicite ("i3" = 3i, "p2" = 2pi, "(1+i)2"). Sans ca le nombre
            // ecrasait silencieusement l'operande precedent.
            bool last_is_var = last_type >= 'A' && last_type <= 'Z' &&
                               !is_in(last_type, (char *)"FGHPLDRSO");
            if (!pending && (is_in(last_type, (char *)"iepz)]X!") || last_is_var))
            {
                out[out_size].type = '*';
                out[out_size].src_pos = -1;
                last_type = '*';
                out_size++;
            }
            num_start = i;
            while (i < n && is_in(in[i], nb))
            {
                cur.type = 'n';
                cur.value *= 10;
                cur.value += (int)(in[i] - '0');
                i++;
            }
            pending = true;
            if (i == n || (in[i] != ',' && in[i] != '.'))
            {
                out[out_size].value = cur.value;
                out[out_size].type = 'n';
                cur.value = 0;
                out[out_size].src_pos = i - 1;
                last_type = 'n';
                out_size++;
                pending = false;
                if (i < n)
                    goto retry;
            }
        }

        if (is_rel_type(in[i]))
        {
            /* Signe d'inequation : se comporte comme '='. */
            out[out_size].type = in[i];
            out[out_size].value = 0;
            out[out_size].src_pos = i;
            out[out_size].mat = NULL;
            last_type = '=';
            out_size++;
            continue;
        }
        if (in[i] == CAS_SEPARATOR)
        {
            /* Separateur de solutions ("X=1 ; X=2") : jamais de produit implicite. */
            out[out_size].type = CAS_SEPARATOR;
            out[out_size].value = 0;
            out[out_size].src_pos = i;
            out[out_size].mat = NULL;
            last_type = CAS_SEPARATOR;
            out_size++;
            continue;
        }
        if (in[i] == '!')
        {
            if (out_size > 0)
            {
                out[out_size].type = '!';
                out[out_size].value = (int)'!' - 'a';
                out[out_size].src_pos = i;
                last_type = '!';
                out_size++;
            }
        }
        if (i < n && (in[i] == ',' || in[i] == '.'))
        {
            if (in[i] == ',' && depth > 0 && paren_kind[depth - 1] == 'q')
            {
                if (pending)
                {
                    out[out_size].value = cur.value;
                    out[out_size].type = 'n';
                    out[out_size].src_pos = i - 1;
                    cur.value = 0;
                    last_type = 'n';
                    out_size++;
                    pending = false;
                }
                out[out_size].type = ',';
                out[out_size].value = 0;
                out[out_size].src_pos = i;
                out[out_size].mat = NULL;
                last_type = ',';
                out_size++;
            }
            else
            {
                int start = pending ? num_start : i;
                i++;
                while (i < n && is_in(in[i], nb))
                    i++;
                cur.value = digits_to_double(in, start, i);
                out[out_size].value = cur.value;
                out[out_size].type = 'n';
                out[out_size].src_pos = i - 1;
                cur.value = 0;
                last_type = 'n';
                out_size++;
                pending = false;
                // Le caractere qui suit ("2.5!", "0.5exp(0)") doit etre lu.
                if (i < n)
                    goto retry;
            }
        }
        if (i < n && is_in(in[i], (char *)"("))
        {
            char paren_prev = last_type;
            if (need_implicit_star('(', last_type))
            {
                out[out_size].type = '*';
                out[out_size].src_pos = -1;
                last_type = '*';
                out_size++;
            }
            if (depth < 64)
            {
                paren_kind[depth++] = is_two_arg_op(paren_prev) ? 'q' : '.';
            }
            out[out_size].value = (int)in[i] - 'a';
            out[out_size].type = in[i];
            out[out_size].src_pos = i;
            last_type = '(';
            out_size++;
        }
        else if (i < n && is_in(in[i], funop))
        {

            if (need_implicit_star(in[i], last_type))
            {
                out[out_size].type = '*';
                out[out_size].src_pos = -1;
                last_type = '*';
                out_size++;
            }
            out[out_size].value = (int)in[i] - 'a';
            out[out_size].type = in[i];
            out[out_size].src_pos = i;

            last_type = in[i];
            out_size++;
            if (in[i] == ')' && depth > 0)
            {
                depth--;
            }
        }
        else if (i < n && in[i] <= 'Z' && 'A' <= in[i])
        {
            if (need_implicit_star(in[i], last_type))
            {
                out[out_size].type = '*';
                out[out_size].src_pos = -1;
                last_type = '*';
                out_size++;
            }
            out[out_size].value = in[i] - 'a';
            out[out_size].type = 'X';
            out[out_size].src_pos = i; // ← position du caractère
            last_type = 'X';
            out_size++;
        }
    }

    *tokenized_size = out_size;
    return (out);
}