#include <stdio.h>
#include <string.h>
#include <cmath>
#include "Evaluator.h"

static int fails = 0;

static void run(const char *expr, double exp_re, double exp_im)
{
    int n = strlen(expr);
    int tok_n = 0;
    token *toks = parse_string_to_token((char *)expr, n, &tok_n);
    token *rpn = shunting_yard(toks, tok_n);
    int yarded = count_yarded(toks, tok_n);
    complex r = evaluate_npi_c(rpn, yarded);
    char buf[256];
    int len = complex_to_string(r, buf);
    buf[len] = '\0';
    double d = fabs(r.re - exp_re) + fabs(r.im - exp_im);
    bool ok = d < 1e-9;
    if (!ok)
        fails++;
    printf("[%s] %-24s => %-22s  re=%.10f im=%.10f (expected %.10f %.10f)\n",
           ok ? "OK " : "FAIL", expr, buf, r.re, r.im, exp_re, exp_im);
    free(toks);
    free(rpn);
}

static void run_var(const char *expr, int letter, double x, double exp_re, double exp_im)
{
    int n = strlen(expr);
    int tok_n = 0;
    token *toks = parse_string_to_token((char *)expr, n, &tok_n);
    token *rpn = shunting_yard(toks, tok_n);
    int yarded = count_yarded(toks, tok_n);
    complex var = {x, 0};
    complex r = evaluate_npi_c_var(rpn, yarded, var, letter);
    double d = fabs(r.re - exp_re) + fabs(r.im - exp_im);
    bool ok = d < 1e-9;
    if (!ok)
        fails++;
    printf("[%s] %-24s (var %c=%.0f) => %.3f%+.3fi (expected %.3f%+.3fi)\n",
           ok ? "OK " : "FAIL", expr, letter, x, r.re, r.im, exp_re, exp_im);
    free(toks);
    free(rpn);
}

int main()
{
    run("i", 0, 1);
    run("i*i", -1, 0);
    run("2i", 0, 2);
    run("(2+3i)(1-i)", 5, 1);
    run("(2+3i)+(1-i)", 3, 2);
    run("(2+3i)-(1-i)", 1, 4);
    run("(2+3i)/(1-i)", -0.5, 2.5);
    run("3+2i", 3, 2);
    run("5i+3", 3, 5);
    run("2(3+4i)", 6, 8);
    run("r(-1)", 0, 1);
    run("r(-4)", 0, 2);
    run("i^2", -1, 0);
    run("(1+i)^2", 0, 2);
    run("l(-1)", 0, 3.141592653589793);
    run("e^(i*p)", -1, 0);
    run("s(p/2)", 1, 0);
    run("s(i)", 0, 1.175201193643801);
    run("c(i)", 1.543080634815244, 0);
    run("c(0)", 1, 0);
    run("s(0)", 0, 0);
    run("t(0)", 0, 0);
    run("t(i)", 0, 0.7615941559557649);
    run("f(0)", 1, 0);
    run("g(0)", 0, 0);
    run("h(0)", 0, 0);
    run("a(1)", 0, 0);                  // acosh(1)
    run("j(0)", 0, 0);                  // asinh(0)
    run("k(0)", 0, 0);                  // atanh(0)
    run("u(0)", 1.5707963267948966, 0); // acos(0) = pi/2
    run("v(1)", 1.5707963267948966, 0); // asin(1) = pi/2
    run("w(1)", 0.7853981633974483, 0); // atan(1) = pi/4
    run("p", 3.141592653589793, 0);
    run("e", 2.718281828459045, 0);
    run("5", 5, 0);
    run("2+2", 4, 0);
    run("3!", 6, 0);
    run("(2+3)!", 120, 0);
    run("3!+2", 8, 0);
    {
        int n = strlen("(3+4i)!");
        int tok_n = 0;
        token *toks = parse_string_to_token((char *)"(3+4i)!", n, &tok_n);
        token *rpn = shunting_yard(toks, tok_n);
        int yarded = count_yarded(toks, tok_n);
        complex r = evaluate_npi_c(rpn, yarded);
        if (std::isnan(r.re) && std::isnan(r.im))
            printf("[OK ] (3+4i)!                   => nan (undefined for complex)\n");
        else
        {
            fails++;
            printf("[FAIL] (3+4i)!                  => %.10f+%.10fi (expected NaN)\n", r.re, r.im);
        }
        free(toks);
        free(rpn);
    }
    run("b(2+3i)", 2, -3);
    run("d(i)", 1.5707963267948966, 0);
    run("d(0-1)", 3.141592653589793, 0);
    run("m(3+4i)", 5, 0);
    run("m(0-5)", 5, 0);
    run("(0.5+0.5i)*2", 1, 1);
    run("(2+2i)^3", -16, 16);
    run("1/i", 0, -1);
    run("1/(1+i)", 0.5, -0.5);
    run("(1-i)(1+i)", 2, 0);
    run("i^(1/2)", 0.7071067811865476, 0.7071067811865476);
    run("e^i", 0.5403023058681398, 0.8414709848078965);
    run("l(e)", 1, 0);
    run("l(i)", 0, 1.5707963267948966);

    // Regression: double (real) evaluator still works for grapher/sequences/solver
    {
        const char *expr = "3+4*c(0)";
        int n = strlen(expr);
        int tok_n = 0;
        token *toks = parse_string_to_token((char *)expr, n, &tok_n);
        token *rpn = shunting_yard(toks, tok_n);
        int yarded = count_yarded(toks, tok_n);
        double r = evaluate_npi(rpn, yarded);
        if (fabs(r - 7.0) < 1e-9)
            printf("[OK ] double-eval 3+4*c(0)          => 7\n");
        else
        {
            fails++;
            printf("[FAIL] double-eval 3+4*c(0)          => %.10f (expected 7)\n", r);
        }
        free(toks);
        free(rpn);
    }
    {
        const char *expr = "a(1)";
        int n = strlen(expr);
        int tok_n = 0;
        token *toks = parse_string_to_token((char *)expr, n, &tok_n);
        token *rpn = shunting_yard(toks, tok_n);
        int yarded = count_yarded(toks, tok_n);
        double r = evaluate_npi(rpn, yarded);
        if (fabs(r - 0.0) < 1e-9)
            printf("[OK ] double-eval a(1) (acosh)      => 0\n");
        else
        {
            fails++;
            printf("[FAIL] double-eval a(1)              => %.10f (expected 0)\n", r);
        }
        free(toks);
        free(rpn);
    }
    // Lone constants in the double evaluator must not corrupt the stack
    {
        const char *exprs[] = {"p", "e", "i", "p+e", "3p"};
        double expected[] = {3.141592653589793, 2.718281828459045, NAN, 5.859874482048838, 9.42477796076938};
        for (int t = 0; t < 5; t++)
        {
            int n = strlen(exprs[t]);
            int tok_n = 0;
            token *toks = parse_string_to_token((char *)exprs[t], n, &tok_n);
            token *rpn = shunting_yard(toks, tok_n);
            int yarded = count_yarded(toks, tok_n);
            double r = evaluate_npi(rpn, yarded);
            bool ok = std::isnan(expected[t]) ? std::isnan(r) : fabs(r - expected[t]) < 1e-9;
            if (ok)
                printf("[OK ] double-eval %-6s          => %.10f\n", exprs[t], r);
            else
            {
                fails++;
                printf("[FAIL] double-eval %-6s          => %.10f (expected %.10f)\n", exprs[t], r, expected[t]);
            }
            free(toks);
            free(rpn);
        }
    }
    // Same for the 3-arg evaluate_npi (grapher/sequences variable variant)
    {
        const char *expr = "p";
        int n = strlen(expr);
        int tok_n = 0;
        token *toks = parse_string_to_token((char *)expr, n, &tok_n);
        token *rpn = shunting_yard(toks, tok_n);
        int yarded = count_yarded(toks, tok_n);
        double r = evaluate_npi(rpn, yarded, 0, 'x');
        if (fabs(r - 3.141592653589793) < 1e-9)
            printf("[OK ] double-eval3 p               => 3.1415926536\n");
        else
        {
            fails++;
            printf("[FAIL] double-eval3 p               => %.10f (expected 3.1415926536)\n", r);
        }
        free(toks);
        free(rpn);
    }

    // Implicit multiplication between a bare constant/variable and a variable
    // (regression: 'iX' used to lose the '*' and evaluate the dangling 'i')
    run_var("iX", 'X', 1, 0, 1);
    run_var("iX+1", 'X', 1, 1, 1);
    run_var("2X", 'X', 1, 2, 0);
    run_var("2X+1", 'X', 1, 3, 0);
    run_var("Xi", 'X', 1, 0, 1);
    run_var("X(2+3i)", 'X', 1, 2, 3);
    run_var("X*X", 'X', 3, 9, 0);
    run_var("X+i", 'X', 1, 1, 1);

    // ---- generated bulk tests (see /tmp/opencode/gen_complex_tests.py) ----
    run("(1+2i)+(3+4i)", 4, 6);
    run("(1+2i)-(3+4i)", -2, -2);
    run("(1+2i)*(3+4i)", -5, 10);
    run("(1+2i)/(3+4i)", 0.44, 0.080000000000000002);
    run("(2+3i)(4-i)", 11, 10);
    run("(1-2i)/(1+2i)", -0.59999999999999998, -0.80000000000000004);
    run("(2+3i)^2", -5, 12);
    run("(2+3i)^3", -46.000000000000007, 9.0000000000000089);
    run("(1+i)^4", -4, 4.8985871965894128e-16);
    run("(1-i)^4", -4, -4.8985871965894128e-16);
    run("1/(2-3i)", 0.15384615384615385, 0.23076923076923078);
    run("1/(3i)", 0, -0.33333333333333331);
    run("2/(2+2i)", 0.5, -0.5);
    run("(0-5+2i)", -5, 2);
    run("5-2i", 5, -2);
    run("(3+4i)-(0-1)", 4, 4);
    run("(0-1-i)", -1, -1);
    run("7i-(2+3i)", -2, 4);
    run("(1+2i)(1-2i)", 5, 0);
    run("(3+4i)/(4-3i)", 0, 1);
    run("(2+3i)^2+1", -4, 12);
    run("(1+i)^3", -1.9999999999999996, 2);
    run("(1-i)^3", -1.9999999999999996, -2);
    run("6/(1+i)", 3, -3);
    run("(2+2i)^2/(1+i)", 4.0000000000000009, 4.0000000000000009);
    run("(0.5+1.5i)*(2+0.5i)", 0.25, 3.25);
    run("(2+2i)^4", -64.000000000000028, 7.8377395145430637e-15);
    run("(1+i)^8", 15.999999999999998, -3.9188697572715295e-15);
    run("(0.3+0.7i)*(1.1-0.4i)", 0.60999999999999999, 0.65000000000000002);
    run("p/2", 1.5707963267948966, 0);
    run("2p", 6.2831853071795862, 0);
    run("p*e", 8.539734222673566, 0);
    run("e/p", 0.86525597943226507, 0);
    run("p+i", 3.1415926535897931, 1);
    run("e-i", 2.7182818284590451, -1);
    run("2p+3e", 14.438030792556722, 0);
    run("p*e+i", 8.539734222673566, 1);
    run("p^2", 9.869604401089358, 0);
    run("e^2", 7.3890560989306504, 0);
    run("i*e", 0, 2.7182818284590451);
    run("i*e*1", 0, 2.7182818284590451);
    run("i/(2p)", 0, 0.15915494309189535);
    run("p/e+i", 1.1557273497909217, 1);
    run("e^(0-1)", 0.36787944117144233, 0);
    run("(0-1)*p", -3.1415926535897931, 0);
    run("r(2)", 1.4142135623730951, 0);
    run("r(0-4)", 1.2246467991473532e-16, 2);
    run("r(3+4i)", 2, 1);
    run("r(i)", 0.70710678118654757, 0.70710678118654746);
    run("r(1+i)", 1.0986841134678098, 0.45508986056222733);
    run("r(0-0.25)", 3.061616997868383e-17, 0.5);
    run("r(7-24i)", 4, -3);
    run("r(0-2)", 8.6595605623549341e-17, 1.4142135623730951);
    run("l(2)", 0.69314718055994529, 0);
    run("l(0-3)", 1.0986122886681098, 3.1415926535897931);
    run("l(i)", 0, 1.5707963267948966);
    run("l(2+3i)", 1.2824746787307684, 0.98279372324732905);
    run("l(e)", 1, 0);
    run("l(0.5)", -0.69314718055994529, 0);
    run("l(3-4i)", 1.6094379124341003, -0.92729521800161219);
    run("l(0-1)", 0, 3.1415926535897931);
    run("e^(2+3i)", -7.3151100949011028, 1.0427436562359045);
    run("e^(1+i)", 1.4686939399158851, 2.2873552871788423);
    run("e^(0-i)", 0.54030230586813977, -0.8414709848078965);
    run("e^(i*p/2)", 6.123233995736766e-17, 1);
    run("e^(0-i*p/2)", 6.123233995736766e-17, -1);
    run("s(p/2)", 1, 0);
    run("s(0-p/2)", -1, 0);
    run("s(1+2i)", 3.1657785132161682, 1.9596010414216063);
    run("c(1+2i)", 2.0327230070196656, -3.0518977991518002);
    run("t(1+i)", 0.27175258531951185, 1.0839233273386946);
    run("s(p/3)", 0.8660254037844386, 0);
    run("c(p/3)", 0.50000000000000011, -0);
    run("t(p/4)", 0.99999999999999989, 0);
    run("s(2p)", -2.4492935982947064e-16, 0);
    run("c(p)", -1, -0);
    run("u(0)", 1.5707963267948966, -0);
    run("u(2)", 3.9581206761708415e-16, 1.3169578969248164);
    run("v(2)", 1.5707963267948966, -1.3169578969248166);
    run("w(2)", 1.1071487177940904, 0);
    run("w(0-1)", -0.78539816339744828, 0);
    run("v(1)", 1.5707963267948966, -0);
    run("u(0-1)", 3.1415926535897931, -0);
    run("v(0-2)", -1.5707963267948961, 1.3169578969248164);
    run("f(1)", 1.5430806348152437, 0);
    run("g(1)", 1.1752011936438014, 0);
    run("h(1)", 0.76159415595576485, 0);
    run("f(i)", 0.54030230586813977, 0);
    run("g(i)", 0, 0.8414709848078965);
    run("h(i)", 0, 1.5574077246549021);
    run("a(2)", 1.3169578969248166, 0);
    run("j(1)", 0.88137358701954294, 0);
    run("k(0.5)", 0.54930614433405478, 0);
    run("k(2)", 0.54930614433405489, -1.5707963267948966);
    run("a(0.5)", -5.5511151231257827e-17, 1.0471975511965976);
    run("j(i)", 0, 1.5707963267948966);
    run("k(i)", 0, 0.78539816339744828);
    run("b(3+4i)", 3, -4);
    run("b(0-5i)", -0, 5);
    run("d(1+i)", 0.78539816339744828, 0);
    run("d(0-i)", -1.5707963267948966, 0);
    run("m(3+4i)", 5, 0);
    run("m(0-12i)", 12, 0);
    run("m(2+i)", 2.2360679774997898, 0);
    run("d(0-3)", 3.1415926535897931, 0);
    run("m(0-3)", 3, 0);
    run("b(1)", 1, 0);
    run("r(s(p/2))", 1, 0);
    run("s(c(t(p/4)))", 0.51439525852354917, -0);
    run("s(c(p))", -0.8414709848078965, -0);
    run("c(s(i))", 1.7737756783403529, -0);
    run("s(l(i))", 0, 2.3012989023072947);
    run("w(t(p/4))", 0.78539816339744828, 0);
    run("r(l(0-1))", 1.2533141373155003, 1.2533141373155001);
    run("s(p/2)^2+c(p/2)^2", 1, 0);
    run("s(c(t(p/4))+w(1))", 0.97011406966552471, 0);
    run("2(3+4i)", 6, 8);
    run("3i(2+i)", -3, 6);
    run("(1+i)(1-i)", 2, 0);
    run("(3+2i)(1+4i)", -5, 14);
    run("2(3+4i)(1-2i)", 22, -4);
    run("i(2+i)", -1, 2);
    run("(1+i)(2+i)(3+i)", 0, 10);
    run("3(2-i)", 6, -3);
    run("5(1+2i)", 5, 10);
    run("2i(3-4i)", 8, 6);
    run("(2+3i)(2-3i)", 13, 0);
    run("3(2+i)(1-i)", 9, -3);
    // ---- generated variable tests ----
    run_var("X+1", 'X', 1, 2, 0);
    run_var("X-1", 'X', 2, 1, 0);
    run_var("2X", 'X', 3, 6, 0);
    run_var("X*X", 'X', 2, 4, 0);
    run_var("X*X", 'X', -3, 9, 0);
    run_var("X*i", 'X', 2, 0, 2);
    run_var("i*X", 'X', 3, 0, 3);
    run_var("X(2+3i)", 'X', 2, 4, 6);
    run_var("X/(2+2i)", 'X', 2, 0.5, -0.5);
    run_var("X^2+1", 'X', -1, 2, -2.4492935982947064e-16);
    run_var("3X-6", 'X', 2, 0, 0);
    run_var("X+2i", 'X', 1, 1, 2);
    run_var("(X+i)(X-i)", 'X', 2, 5, 0);
    run_var("X^3", 'X', 3, 27, 0);
    run_var("1/X", 'X', 2, 0.5, 0);
    run_var("X/2-1", 'X', 4, 1, 0);
    run_var("(X+i)^2", 'X', 1, 1.2246467991473532e-16, 2);
    run_var("X*X+1", 'X', -2, 5, 0);
    run_var("X*(1+i)", 'X', 3, 3, 3);
    run_var("A+1", 'A', 1, 2, 0);
    run_var("B*B", 'B', 2, 4, 0);
    run_var("A*B", 'A', 3, 0, 0);
    run_var("B(2+i)", 'B', 2, 4, 2);

    printf("\n%s (%d failures)\n", fails == 0 ? "ALL PASS" : "FAILURES", fails);
    return fails == 0 ? 0 : 1;
}
