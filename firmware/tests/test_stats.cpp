// Host tests for Core/Stats.cpp (one-variable statistics, regressions,
// probability laws) and Core/Exact.cpp (exact results), plus a regression
// test for long expressions in the shunting-yard (C3).
#include "Stats.h"
#include "Exact.h"
#include "Evaluator.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

static int fails = 0;

static void check_num(const char *what, double got, double want, double tol = 1e-9)
{
    bool ok = (std::isnan(want) && std::isnan(got)) ||
              fabs(got - want) <= tol * (fabs(want) > 1 ? fabs(want) : 1);
    printf("%s %-46s got %.12g  want %.12g\n", ok ? "[OK ]" : "[FAIL]", what, got, want);
    if (!ok)
        fails++;
}

static void check_true(const char *what, bool cond)
{
    printf("%s %s\n", cond ? "[OK ]" : "[FAIL]", what);
    if (!cond)
        fails++;
}

static void check_exact(double v, const char *want)
{
    char buf[64];
    bool found = exact_form(v, buf, sizeof(buf));
    std::string got = found ? buf : "(none)";
    bool ok = got == want;
    printf("%s exact(%.15g) = %-14s want %s\n", ok ? "[OK ]" : "[FAIL]", v, got.c_str(), want);
    if (!ok)
        fails++;
    // The exact form must evaluate back to the same value.
    if (found)
    {
        int n = 0;
        token *t = parse_string_to_token(buf, (int)strlen(buf), &n);
        token *o = shunting_yard(t, n);
        double back = evaluate_npi(o, count_yarded(t, n));
        free(t);
        free(o);
        bool same = fabs(back - v) <= 1e-9 * (fabs(v) > 1 ? fabs(v) : 1);
        printf("%s   re-evaluates to %.15g\n", same ? "[OK ]" : "[FAIL]", back);
        if (!same)
            fails++;
    }
}

int main()
{
    // ---- one variable ----
    {
        double x[] = {2, 4, 4, 4, 5, 5, 7, 9};
        stats1 s;
        check_true("stats_one_var 8 values", stats_one_var(x, NULL, 8, &s));
        check_num("n", s.n, 8);
        check_num("mean", s.mean, 5);
        check_num("sigma (population)", s.sd, 2);
        check_num("s (sample)", s.sd_s, sqrt(32.0 / 7));
        check_num("min", s.min, 2);
        check_num("max", s.max, 9);
        check_num("median (even count)", s.median, 4.5);
        check_num("Q1 (lycee)", s.q1, 4);
        check_num("Q3 (lycee)", s.q3, 5);
    }
    {
        // Same data given as values + frequencies.
        double v[] = {9, 2, 4, 5, 7};
        double w[] = {1, 1, 3, 2, 1};
        stats1 s;
        stats_one_var(v, w, 5, &s);
        check_num("weighted n", s.n, 8);
        check_num("weighted mean", s.mean, 5);
        check_num("weighted median", s.median, 4.5);
        check_num("weighted Q3", s.q3, 5);
    }
    {
        double x[] = {1, 2, 3, 4, 5, 6, 7};
        stats1 s;
        stats_one_var(x, NULL, 7, &s);
        check_num("median odd count", s.median, 4);
        check_num("Q1 odd count", s.q1, 2);
        check_num("Q3 odd count", s.q3, 6);
    }
    {
        double x[] = {NAN, 3};
        double w[] = {1, 0};
        stats1 s;
        check_true("no valid point -> false", !stats_one_var(x, w, 2, &s));
    }

    // ---- regressions ----
    {
        double x[] = {1, 2, 3, 4, 5};
        double y[] = {3, 5, 7, 9, 11};
        regression r;
        check_true("linear fit", regress(x, y, 5, REG_LINEAR, &r));
        check_num("linear a", r.a, 2);
        check_num("linear b", r.b, 1);
        check_num("linear r", r.r, 1);
        check_num("linear r2", r.r2, 1);
    }
    {
        double x[] = {1, 2, 3, 4};
        double y[] = {1, 3, 2, 4};
        regression r;
        regress(x, y, 4, REG_LINEAR, &r);
        check_num("noisy linear a", r.a, 0.8);
        check_num("noisy linear b", r.b, 0.5);
        check_num("noisy linear r", r.r, 0.8);
    }
    {
        double x[] = {-2, -1, 0, 1, 2, 3};
        double y[6];
        for (int i = 0; i < 6; i++)
            y[i] = 2 * x[i] * x[i] - 3 * x[i] + 1;
        regression r;
        check_true("quadratic fit", regress(x, y, 6, REG_QUAD, &r));
        check_num("quad a", r.a, 2);
        check_num("quad b", r.b, -3);
        check_num("quad c", r.c, 1);
        check_num("quad r2", r.r2, 1);
    }
    {
        double x[] = {0, 1, 2, 3, 4};
        double y[5];
        for (int i = 0; i < 5; i++)
            y[i] = 3 * exp(0.5 * x[i]);
        regression r;
        regress(x, y, 5, REG_EXP, &r);
        check_num("exp a", r.a, 3);
        check_num("exp b", r.b, 0.5);
    }
    {
        double x[] = {1, 2, 4, 8};
        double y[4];
        for (int i = 0; i < 4; i++)
            y[i] = 5 * pow(x[i], 1.5);
        regression r;
        regress(x, y, 4, REG_POWER, &r);
        check_num("power a", r.a, 5);
        check_num("power b", r.b, 1.5);
    }
    {
        double x[] = {1, M_E, M_E * M_E};
        double y[] = {2, 5, 8};
        regression r;
        regress(x, y, 3, REG_LOG, &r);
        check_num("log a", r.a, 2);
        check_num("log b", r.b, 3);
        check_num("reg_eval log at e^3", reg_eval(&r, exp(3.0)), 11);
    }
    {
        double x[] = {1, 1, 1};
        double y[] = {1, 2, 3};
        regression r;
        check_true("vertical points -> no linear fit", !regress(x, y, 3, REG_LINEAR, &r));
    }

    // ---- probability laws ----
    {
        double b[] = {10, 0.5};
        check_num("B(10,0.5) P(X=5)", law_pdf(LAW_BINOMIAL, b, 5), 252.0 / 1024);
        check_num("B(10,0.5) P(X<=3)", law_cdf(LAW_BINOMIAL, b, 3), 176.0 / 1024);
        check_num("B(10,0.5) P(2<=X<=4)", law_interval(LAW_BINOMIAL, b, 2, 4), (45 + 120 + 210) / 1024.0);
        check_num("B(10,0.5) inverse 0.5", law_inverse(LAW_BINOMIAL, b, 0.5), 5);
        check_num("B(10,0.5) mean", law_mean(LAW_BINOMIAL, b), 5);
        double b2[] = {20, 0.3};
        check_num("B(20,0.3) P(X<=6)", law_cdf(LAW_BINOMIAL, b2, 6), 0.608009812, 1e-8);
        double bad[] = {10.5, 0.5};
        check_true("B(10.5,.) rejected", std::isnan(law_cdf(LAW_BINOMIAL, bad, 3)));
    }
    {
        double n[] = {0, 1};
        check_num("N(0,1) P(X<=1.96)", law_cdf(LAW_NORMAL, n, 1.96), 0.9750021048517795, 1e-12);
        check_num("N(0,1) P(-1<=X<=1)", law_interval(LAW_NORMAL, n, -1, 1), 0.6826894921370859, 1e-12);
        check_num("N(0,1) inverse 0.975", law_inverse(LAW_NORMAL, n, 0.975), 1.959963984540054, 1e-12);
        check_num("N(0,1) inverse 0.001", law_inverse(LAW_NORMAL, n, 0.001), -3.090232306167813, 1e-12);
        double n2[] = {100, 15};
        check_num("N(100,15) P(X<=130)", law_cdf(LAW_NORMAL, n2, 130), 0.9772498680518208, 1e-12);
        check_num("N(100,15) inverse 0.9", law_inverse(LAW_NORMAL, n2, 0.9), 119.2232735, 1e-9);
        check_num("N(0,1) density at 0", law_pdf(LAW_NORMAL, n, 0), 1 / sqrt(2 * M_PI));
    }
    {
        double g[] = {0.2};
        check_num("G(0.2) P(X=3)", law_pdf(LAW_GEOMETRIC, g, 3), 0.8 * 0.8 * 0.2);
        check_num("G(0.2) P(X<=3)", law_cdf(LAW_GEOMETRIC, g, 3), 1 - 0.512);
        check_num("G(0.2) inverse 0.488", law_inverse(LAW_GEOMETRIC, g, 0.488), 3);
        check_num("G(0.2) inverse 0.489", law_inverse(LAW_GEOMETRIC, g, 0.489), 4);
        check_num("G(0.2) mean", law_mean(LAW_GEOMETRIC, g), 5);
    }
    {
        double l[] = {3};
        check_num("P(3) P(X=2)", law_pdf(LAW_POISSON, l, 2), 4.5 * exp(-3.0));
        check_num("P(3) P(X<=2)", law_cdf(LAW_POISSON, l, 2), 8.5 * exp(-3.0));
        check_num("P(3) inverse 0.5", law_inverse(LAW_POISSON, l, 0.5), 3);
    }
    {
        double e[] = {0.5};
        check_num("E(0.5) P(X<=2)", law_cdf(LAW_EXPONENTIAL, e, 2), 1 - exp(-1.0));
        check_num("E(0.5) inverse", law_inverse(LAW_EXPONENTIAL, e, 1 - exp(-1.0)), 2);
        double u[] = {2, 6};
        check_num("U(2,6) P(3<=X<=4)", law_interval(LAW_UNIFORM, u, 3, 4), 0.25);
        check_num("U(2,6) sd", law_sd(LAW_UNIFORM, u), 4 / sqrt(12.0));
    }

    // ---- exact results ----
    check_exact(0.5, "1/2");
    check_exact(1.0 / 3 + 1.0 / 6, "1/2");
    check_exact(-7.0 / 12, "-7/12");
    check_exact(0.1 + 0.2, "3/10");
    check_exact(4.0, "(none)");
    check_exact(M_PI / 4, "p/4");
    check_exact(3 * M_PI / 4, "3p/4");
    check_exact(-M_PI / 6, "-p/6");
    check_exact(2 * M_PI, "2p");
    check_exact(sqrt(2.0), "r(2)");
    check_exact(sqrt(3.0) / 2, "r(3)/2");
    check_exact(2 / sqrt(3.0), "2r(3)/3");
    check_exact(-sqrt(2.0) / 2, "-r(2)/2");
    check_exact(sqrt(12.0), "2r(3)");
    check_exact((1 + sqrt(5.0)) / 2, "(1+r(5))/2");
    check_exact(3 - 2 * sqrt(2.0), "3-2r(2)");
    check_exact(M_E, "(none)");
    check_exact(log(2.0), "(none)");
    check_exact(1e-15, "(none)");

    check_true("decimal input disables exact", !exact_input_allowed("0.5+1", 5));
    check_true("integer input allows exact", exact_input_allowed("1/3", 3));
    check_true("complex i disables exact", !exact_input_allowed("2i+1", 4));
    check_true("min( keeps exact", exact_input_allowed("min(1/2,1/3)", 12));

    // ---- C3: long expressions must not overflow the shunting-yard output ----
    {
        std::string e = "1";
        for (int i = 0; i < 400; i++)
            e += "+1";
        int n = 0;
        token *t = parse_string_to_token((char *)e.c_str(), (int)e.size(), &n);
        token *o = shunting_yard(t, n);
        double v = evaluate_npi(o, count_yarded(t, n));
        free(t);
        free(o);
        check_num("401-term sum (801 tokens)", v, 401);
    }
    // Implicit multiplication before a function.
    {
        const char *e = "2r(9)";
        int n = 0;
        token *t = parse_string_to_token((char *)e, 5, &n);
        token *o = shunting_yard(t, n);
        double v = evaluate_npi(o, count_yarded(t, n));
        free(t);
        free(o);
        check_num("2sqrt(9) implicit product", v, 6);
    }

    // Implicit product before a number, and a lone ')' must not crash.
    {
        struct
        {
            const char *e;
            double re, im;
        } cases[] = {{"i3", 0, 3}, {"p2", 2 * M_PI, 0}, {"(1+i)2", 2, 2}, {"12.5+0.25", 12.75, 0}};
        for (auto &c : cases)
        {
            int n = 0;
            token *t = parse_string_to_token((char *)c.e, (int)strlen(c.e), &n);
            token *o = shunting_yard(t, n);
            complex v = evaluate_npi_c(o, count_yarded(t, n));
            free(t);
            free(o);
            char what[48];
            snprintf(what, sizeof(what), "%s (re)", c.e);
            check_num(what, v.re, c.re);
            snprintf(what, sizeof(what), "%s (im)", c.e);
            check_num(what, v.im, c.im);
        }
        int n = 0;
        token *t = parse_string_to_token((char *)")", 1, &n);
        token *o = shunting_yard(t, n);
        evaluate_npi_c(o, count_yarded(t, n));
        free(t);
        free(o);
        check_true("lone ')' does not hang or crash", true);
    }

    if (fails == 0)
        printf("ALL PASS (0 failures)\n");
    else
        printf("FAILURES: %d\n", fails);
    return fails ? 1 : 0;
}
