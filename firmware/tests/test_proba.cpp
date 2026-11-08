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
    check_tok("binom(5,2)", 0, 'q');
    check_tok("perm(5,2)", 0, 'y');
    check_tok("2binom(5,2)", 1, '*');
    check_tok("2binom(5,2)", 2, 'q');
    check_tok("2perm(5,2)", 2, 'y');

    check("binom(5,2)", eval_c("binom(5,2)"), 10, 0);
    check("binom(5,0)", eval_c("binom(5,0)"), 1, 0);
    check("binom(5,5)", eval_c("binom(5,5)"), 1, 0);
    check("perm(5,2)", eval_c("perm(5,2)"), 20, 0);
    check("perm(5,5)", eval_c("perm(5,5)"), 120, 0);
    check("perm(5,0)", eval_c("perm(5,0)"), 1, 0);
    check("binom(52,5)", eval_c("binom(52,5)"), 2598960, 0);
    check("2binom(5,2)", eval_c("2binom(5,2)"), 20, 0);
    check("binom(5,2)+perm(4,2)", eval_c("binom(5,2)+perm(4,2)"), 22, 0);

    complex nan = eval_c("binom(2,5)");
    if (!std::isnan(nan.re))
        fails++;
    printf("[%s] binom(2,5) invalid => %f\n", std::isnan(nan.re) ? "OK " : "FAIL", nan.re);
    nan = eval_c("perm(-1,2)");
    if (!std::isnan(nan.re))
        fails++;
    printf("[%s] perm(-1,2) invalid => %f\n", std::isnan(nan.re) ? "OK " : "FAIL", nan.re);

    check("calc binom(5,2)", eval_calc("binom(5,2)"), 10, 0);
    check("calc perm(5,2)", eval_calc("perm(5,2)"), 20, 0);

    check_rpn("binom(5,2)", "nnq", 3);
    check_rpn("perm(5,2)", "nny", 3);
    check_rpn("1.5", "n", 1);

    if (fails)
    {
        printf("FAILURES: %d\n", fails);
        return 1;
    }
    printf("ALL PASS (0 failures)\n");
    return 0;
}
