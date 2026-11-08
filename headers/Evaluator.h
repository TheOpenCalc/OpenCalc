#ifndef EVALUATOR_H
#define EVALUATOR_H

#include "stack.h"
#include "Matrix.h"

struct token_s
{
    char type;
    int src_pos;
    double value;
    matrix *mat;
};

struct operation_s
{
    char operator_type;
    void *el1;
    void *el2;
};

typedef struct operation_s operation;

typedef struct token_s token;

/* Ans : dernier resultat du calcul. Scalaire dans g_ans, ou matrice dans
 * g_ans_mat (NULL quand Ans est un scalaire). Le jeton interne est 'z'. */
extern complex g_ans;
extern matrix *g_ans_mat;

/* Blocs de suites recurrentes (voir sequences.cpp). Jetons internes :
 *   '@' arith(r)      = U + r
 *   '#' geom(q)       = q * U
 *   '&' arigeo(a,b)   = a * U + b
 *   '?' arithn(a,b)   = U + a * N + b
 * U (terme precedent) et N (indice courant) sont lus dans ces globales,
 * positionnees par la suite avant chaque evaluation. */
extern double g_seq_u;
extern double g_seq_n;
bool is_block_type(char t);

/* Fonctions nommees ajoutees (jetons internes) :
 *   '$' exp(x)        ':' ent(x) partie entiere   ';' signe(x)
 *   '<' min(a,b)      '>' max(a,b)                '%' racine(x,n)
 *   '`' alea()        '|' randint(a,b)
 * Reconnues seulement suivies de '(' ; alea() n'a pas d'argument. */
bool is_named_call_type(char t);

/* Analyse : '{' somme(f,a,b)  '}' produit(f,a,b)  '_' derivee(f,a)
 * '"' integrale(f,a,b). Evaluees par Core/Calculus.cpp. */
bool is_calculus_type(char t);

void set_ans(complex v);
/* Ans devient une copie de m. */
void set_ans_matrix(const matrix *m);
/* Vrai si les jetons contiennent Ans et que Ans est une matrice. */
bool tokens_use_matrix_ans(const token *toks, int n);

double fact(double in);

double comb(double n, double k);

double perm(double n, double k);

int double_to_string_scientific(double in, char *out);

bool is_in(char test, char *arr);

int count_yarded(char *in);

int count_yarded(token *in, int n);

bool higher_priority(char a, char b);

token *shunting_yard(token *input, int n);

double evaluate_npi(token *in, int n, double x, int letter);

double evaluate_npi(token *in, int n);

complex evaluate_npi_c(token *in, int n);

complex evaluate_npi_c_var(token *in, int n, complex x, int letter);

typedef struct
{
    bool is_matrix;
    complex scalar;
    matrix *m;  /* set when is_matrix */
    bool owns;  /* true when m is owned by the caller (must be freed) */
} calc_result;

calc_result evaluate_calc(token *in, int n);
void calc_result_free(calc_result *r);
void token_free_matrices(token *toks, int n);

complex cadd(complex a, complex b);
complex csub(complex a, complex b);
complex cmul(complex a, complex b);
complex cdiv(complex a, complex b);
complex cexp(complex z);
complex clog(complex z);
complex cpow(complex a, complex b);
complex csqrt(complex z);
complex csin(complex z);
complex ccos(complex z);
complex ctan(complex z);
complex casin(complex z);
complex cacos(complex z);
complex catan(complex z);
complex csinh(complex z);
complex ccosh(complex z);
complex ctanh(complex z);
complex casinh(complex z);
complex cacosh(complex z);
complex catanh(complex z);
complex cfact(complex z);
complex cconj(complex z);
complex carg(complex z);
complex cmod(complex z);
complex clog10(complex z);

double angle_in(double v);
double angle_out(double v);
complex angle_in_c(complex z);
complex angle_out_c(complex z);

int complex_to_string(complex in, char *out);

char *prompt_input(int max_size);

token *parse_string_to_token(char *in, int n, int *tokenized_size);

#endif