#include <stdio.h>
#include <string.h>
#include <cmath>
#include "Evaluator.h"

static int fails = 0;

static complex eval_c(const char *expr)
{
    int n = strlen(expr);
    int tok_n = 0;
    token *toks = parse_string_to_token((char *)expr, n, &tok_n);
    token *rpn = shunting_yard(toks, tok_n);
    int yarded = count_yarded(toks, tok_n);
    complex r = evaluate_npi_c(rpn, yarded);
    free(toks);
    free(rpn);
    return r;
}

static complex eval_calc(const char *expr)
{
    int n = strlen(expr);
    int tok_n = 0;
    token *toks = parse_string_to_token((char *)expr, n, &tok_n);
    token *rpn = shunting_yard(toks, tok_n);
    int yarded = count_yarded(toks, tok_n);
    calc_result cr = evaluate_calc(rpn, yarded);
    complex r = cr.scalar;
    calc_result_free(&cr);
    free(toks);
    free(rpn);
    return r;
}

static void check(const char *name, complex r, double exp_re, double exp_im)
{
    double d = fabs(r.re - exp_re) + fabs(r.im - exp_im);
    bool ok = d < 1e-9;
    if (!ok)
        fails++;
    printf("[%s] %-24s => %.10f%+.10fi (expected %.10f%+.10fi)\n",
           ok ? "OK " : "FAIL", name, r.re, r.im, exp_re, exp_im);
}

static void check_nan(const char *name, complex r)
{
    bool ok = std::isnan(r.re);
    if (!ok)
        fails++;
    printf("[%s] %-24s => %f (expected nan)\n", ok ? "OK " : "FAIL", name, r.re);
}

static void check_tok(const char *expr, int idx, char expected_type)
{
    int n = strlen(expr);
    int tok_n = 0;
    token *toks = parse_string_to_token((char *)expr, n, &tok_n);
    bool ok = idx < tok_n && toks[idx].type == expected_type;
    if (!ok)
        fails++;
    printf("[%s] tok[%d] of %-14s => '%c' (expected '%c')\n",
           ok ? "OK " : "FAIL", idx, expr,
           (idx < tok_n) ? toks[idx].type : '?', expected_type);
    free(toks);
}

static void check_rpn(const char *expr, const char *expected_types, int exp_n)
{
    int n = strlen(expr);
    int tok_n = 0;
    token *toks = parse_string_to_token((char *)expr, n, &tok_n);
    token *rpn = shunting_yard(toks, tok_n);
    int yarded = count_yarded(toks, tok_n);
    bool ok = yarded == exp_n;
    for (int i = 0; ok && i < exp_n; i++)
        if (rpn[i].type != expected_types[i])
            ok = false;
    if (!ok)
        fails++;
    char got[64];
    int g = 0;
    for (int i = 0; i < yarded && g < 63; i++)
        got[g++] = rpn[i].type;
    got[g] = '\0';
    printf("[%s] rpn %-16s => \"%s\" (expected \"%s\", n=%d)\n",
           ok ? "OK " : "FAIL", expr, got, expected_types, exp_n);
    free(toks);
    free(rpn);
}

int main()
{
    check_tok("pgcd(6,4)", 0, 'P');
    check_tok("ppcm(6,4)", 0, 'L');
    check_tok("quot(6,4)", 0, 'D');
    check_tok("reste(6,4)", 0, 'R');
    check_tok("abs(6)", 0, 'S');
    check_tok("arrondi(6)", 0, 'O');
    check_tok("2pgcd(6,4)", 1, '*');
    check_tok("2pgcd(6,4)", 2, 'P');

    check("pgcd(12,8)", eval_c("pgcd(12,8)"), 4, 0);
    check("pgcd(-12,8)", eval_c("pgcd(-12,8)"), 4, 0);
    check("pgcd(0,0)", eval_c("pgcd(0,0)"), 0, 0);
    check("ppcm(4,6)", eval_c("ppcm(4,6)"), 12, 0);
    check("ppcm(-4,6)", eval_c("ppcm(-4,6)"), 12, 0);
    check("ppcm(0,5)", eval_c("ppcm(0,5)"), 0, 0);
    check("quot(7,2)", eval_c("quot(7,2)"), 3, 0);
    check("quot(-7,2)", eval_c("quot(-7,2)"), -4, 0);
    check("reste(7,2)", eval_c("reste(7,2)"), 1, 0);
    check("reste(-7,2)", eval_c("reste(-7,2)"), 1, 0);
    check("reste(7,-2)", eval_c("reste(7,-2)"), -1, 0);
    check("abs(-5)", eval_c("abs(-5)"), 5, 0);
    check("abs(3)", eval_c("abs(3)"), 3, 0);
    check("arrondi(2.4)", eval_c("arrondi(2.4)"), 2, 0);
    check("arrondi(2.5)", eval_c("arrondi(2.5)"), 3, 0);
    check("arrondi(-2.5)", eval_c("arrondi(-2.5)"), -2, 0);
    check("2pgcd(6,4)", eval_c("2pgcd(6,4)"), 4, 0);
    check("pgcd(quot(10,3),2)", eval_c("pgcd(quot(10,3),2)"), 1, 0);
    check("abs(3+4i)", eval_c("abs(3+4i)"), 5, 0);
    check("arrondi(3.2+4.6i)", eval_c("arrondi(3.2+4.6i)"), 3, 5);

    check_nan("pgcd(12.5,8)", eval_c("pgcd(12.5,8)"));
    check_nan("ppcm(12.5,8)", eval_c("ppcm(12.5,8)"));
    check_nan("quot(1,0)", eval_c("quot(1,0)"));
    check_nan("reste(1,0)", eval_c("reste(1,0)"));
    check_nan("pgcd(3+4i,2)", eval_c("pgcd(3+4i,2)"));

    check("calc pgcd(12,8)", eval_calc("pgcd(12,8)"), 4, 0);
    check("calc ppcm(4,6)", eval_calc("ppcm(4,6)"), 12, 0);
    check("calc quot(7,2)", eval_calc("quot(7,2)"), 3, 0);
    check("calc reste(7,2)", eval_calc("reste(7,2)"), 1, 0);
    check("calc abs(-5)", eval_calc("abs(-5)"), 5, 0);
    check("calc arrondi(2.5)", eval_calc("arrondi(2.5)"), 3, 0);
    check_nan("calc quot(1,0)", eval_calc("quot(1,0)"));

    check_rpn("pgcd(6,4)", "nnP", 3);
    check_rpn("abs(5)", "nS", 2);
    check_rpn("1.5", "n", 1);

    if (fails)
    {
        printf("FAILURES: %d\n", fails);
        return 1;
    }
    printf("ALL PASS (0 failures)\n");
    return 0;
}
