// Host tests for Core/Calculus.cpp: somme, produit, derivee, integrale in the
// Calculatrice. The input text uses the editor's internal codes: s( sin,
// c( cos, l( ln, r( sqrt, p pi, e Euler's number, X the variable.
#include "Calculus.h"
#include "Evaluator.h"
#include <cmath>
#include <cstdio>
#include <cstring>

static int fails = 0;

static void check(const char *text, double want_re, double want_im = 0, double tol = 1e-9)
{
    complex v = calculus_eval(text, (int)strlen(text));
    bool ok;
    if (std::isnan(want_re))
        ok = std::isnan(v.re) || std::isnan(v.im);
    else
    {
        double s = fabs(want_re) > 1 ? fabs(want_re) : 1;
        ok = fabs(v.re - want_re) <= tol * s && fabs(v.im - want_im) <= tol * s;
    }
    printf("%s %-40s got %.15g%+.3gi  want %.15g%+.3gi\n", ok ? "[OK ]" : "[FAIL]", text, v.re, v.im,
           want_re, want_im);
    if (!ok)
        fails++;
}

static void check_true(const char *what, bool cond)
{
    printf("%s %s\n", cond ? "[OK ]" : "[FAIL]", what);
    if (!cond)
        fails++;
}

int main()
{
    // Somme
    check("somme(X,1,100)", 5050);
    check("somme(X^2,1,10)", 385);
    check("somme(1/X^2,1,10000)", 1.6448340718480652, 0, 1e-12);
    check("somme(2^X,0,10)", 2047);
    check("somme(X,5,1)", 0);          // somme vide
    check("somme(X,1,2.5)", NAN);       // borne non entiere
    check("somme(X,1,1000000)", NAN);   // trop de termes
    check("somme(,1,5)", NAN);          // expression vide
    check("somme(X,,5)", NAN);          // borne vide
    check("somme(X,1)", NAN);           // argument manquant
    check("somme(i*X,1,3)", 0, 6);      // complexe
    check("2somme(X,1,3)", 12);         // produit implicite
    check("somme(X,1,3)^2", 36);
    check("1+somme(X,1,3)*2", 13);
    check("somme(X,1,3)-somme(X,1,2)", 3);
    check("somme(X,1,4)/somme(X,1,2)", 10.0 / 3.0);
    check("-somme(X,1,3)", -6);

    // Produit
    check("produit(X,1,10)", 3628800);
    check("produit(1+1/X,1,99)", 100, 0, 1e-12);
    check("produit(X,3,2)", 1);         // produit vide

    // Derivee
    check("derivee(X^2,3)", 6);
    check("derivee(X^3-2X,2)", 10);
    check("derivee(s(X),0)", 1);
    check("derivee(e^X,1)", M_E);
    check("derivee(l(X),2)", 0.5);
    check("derivee(r(X),0.04)", 2.5, 0, 1e-7);
    check("derivee(1/X,-2)", -0.25);
    check("derivee(X^2,)", NAN);

    // Integrale
    check("integrale(X^2,0,1)", 1.0 / 3.0, 0, 1e-12);
    check("integrale(s(X),0,p)", 2, 0, 1e-12);
    check("integrale(e^X,0,1)", M_E - 1, 0, 1e-12);
    check("integrale(1/X,1,e)", 1, 0, 1e-12);
    check("integrale(X^2,1,0)", -1.0 / 3.0, 0, 1e-12);   // bornes inversees
    check("integrale(X,2,2)", 0);
    check("integrale(4/(1+X^2),0,1)", M_PI, 0, 1e-12);
    check("integrale(e^(-X^2),-10,10)", sqrt(M_PI), 0, 1e-10);
    check("integrale(1/r(X),0,1)", 2, 0, 1e-6);           // singularite integrable
    check("integrale(r(1-X^2),-1,1)", M_PI / 2, 0, 1e-9);

    // Imbrications
    check("integrale(derivee(X^3,X),0,2)", 8, 0, 1e-8);
    check("somme(somme(X,1,X),1,4)", 20);                // 1+3+6+10
    check("derivee(integrale(X,0,X),3)", 3, 0, 1e-8);
    check("somme(X,1,somme(X,1,3))", 21);
    check("integrale(somme(X^2,1,3),0,1)", 14, 0, 1e-12); // X interne ignore le X externe

    // Detection
    check_true("has_call somme", calculus_has_call("1+somme(X,1,2)", 14));
    check_true("no call in 2s(3)", !calculus_has_call("2s(3)", 5));
    check_true("somme is exact", !calculus_is_approx("somme(X,1,2)", 12));
    check_true("integrale is approx", calculus_is_approx("integrale(X,0,1)", 16));

    // Tokenizer : the calls become structured tokens with ',' separators so
    // that the history can be drawn.
    {
        const char *txt = "integrale(X^2,0,1)";
        int n = 0;
        token *t = parse_string_to_token((char *)txt, (int)strlen(txt), &n);
        int commas = 0;
        for (int i = 0; i < n; i++)
            commas += t[i].type == ',';
        check_true("tokenizer: integrale is one token", n > 0 && t[0].type == '"');
        check_true("tokenizer: 2 separators", commas == 2);
        free(t);
    }

    if (fails == 0)
        printf("ALL PASS (0 failures)\n");
    else
        printf("FAILURES: %d\n", fails);
    return fails ? 1 : 0;
}
