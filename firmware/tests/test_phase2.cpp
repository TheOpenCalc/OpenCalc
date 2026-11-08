#include <stdio.h>
#include <string.h>
#include <cmath>
#include "Evaluator.h"

extern int g_angle_mode;
extern int g_decimals;

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

static void check_str(const char *name, const char *got, const char *expected)
{
    bool ok = strcmp(got, expected) == 0;
    if (!ok)
        fails++;
    printf("[%s] %-24s => \"%s\" (expected \"%s\")\n",
           ok ? "OK " : "FAIL", name, got, expected);
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

int main()
{
    check_tok("log(100)", 0, 'o');
    check_tok("ln(2)", 0, 'l');
    check_tok("det(", 0, 'F');
    check_tok("ans", 0, 'z');
    check_tok("2ans", 1, '*');
    check_tok("2ans", 2, 'z');
    check_tok("2log(10)", 1, '*');
    check_tok("2log(10)", 2, 'o');
    check_tok("2det(", 1, '*');
    check_tok("2det(", 2, 'F');

    check("log(100)", eval_c("log(100)"), 2, 0);
    check("log(1000)", eval_c("log(1000)"), 3, 0);
    check("2log(100)", eval_c("2log(100)"), 4, 0);
    check("log(8)/log(2)", eval_c("log(8)/log(2)"), 3, 0);

    complex ans = {42, 0};
    set_ans(ans);
    check("ans", eval_c("ans"), 42, 0);
    check("ans*2", eval_c("ans*2"), 84, 0);
    check("2ans", eval_c("2ans"), 84, 0);
    check("ans+1", eval_c("ans+1"), 43, 0);
    complex ansc = {0, 3};
    set_ans(ansc);
    check("ans (complex)", eval_c("ans"), 0, 3);
    ans.re = 42;
    ans.im = 0;
    set_ans(ans);

    g_angle_mode = 1;
    check("s(90) DEG", eval_c("s(90)"), 1, 0);
    check("c(60) DEG", eval_c("c(60)"), 0.5, 0);
    check("t(45) DEG", eval_c("t(45)"), 1, 0);
    check("c(90) DEG", eval_c("c(90)"), 0, 0);
    check("v(1) DEG", eval_c("v(1)"), 90, 0);
    check("u(0) DEG", eval_c("u(0)"), 90, 0);
    check("w(1) DEG", eval_c("w(1)"), 45, 0);
    g_angle_mode = 0;
    check("s(p/2) RAD", eval_c("s(p/2)"), 1, 0);
    check("v(1) RAD", eval_c("v(1)"), 1.5707963267948966, 0);
    check("u(0) RAD", eval_c("u(0)"), 1.5707963267948966, 0);

    check("calc log(100)", eval_calc("log(100)"), 2, 0);
    check("calc ans", eval_calc("ans"), 42, 0);
    g_angle_mode = 1;
    check("calc s(90) DEG", eval_calc("s(90)"), 1, 0);
    check("calc v(1) DEG", eval_calc("v(1)"), 90, 0);
    g_angle_mode = 0;

    char buf[64];
    complex third = {1.0 / 3.0, 0};
    complex two_thirds = {2.0 / 3.0, 0};
    complex two_int = {2, 0};
    complex one_four = {1.4, 0};
    complex one_six = {1.6, 0};
    complex ithird = {0, 1.0 / 3.0};
    g_decimals = 2;
    complex_to_string(third, buf);
    check_str("dec2 1/3", buf, "0.33");
    complex_to_string(two_thirds, buf);
    check_str("dec2 2/3", buf, "0.67");
    complex_to_string(two_int, buf);
    check_str("dec2 2", buf, "2");
    g_decimals = 0;
    complex_to_string(one_four, buf);
    check_str("dec0 1.4", buf, "1");
    complex_to_string(one_six, buf);
    check_str("dec0 1.6", buf, "2");
    g_decimals = 6;
    complex_to_string(third, buf);
    check_str("dec6 1/3", buf, "0.333333");
    g_decimals = 2;
    complex_to_string(ithird, buf);
    check_str("dec2 i/3", buf, "0.33i");
    g_decimals = 6;

    if (fails)
    {
        printf("FAILURES: %d\n", fails);
        return 1;
    }
    printf("ALL PASS (0 failures)\n");
    return 0;
}
