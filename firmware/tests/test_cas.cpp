// Tests du calcul formel (Core/Cas.cpp).
// Chaque cas donne la saisie et le resultat attendu, ecrits dans une syntaxe
// lisible : la saisie est convertie vers le texte interne de l'editeur
// (sqrt -> r, pi -> p, sin -> s, ...), et le resultat interne est reconverti
// avant comparaison.
#include "Cas.h"
#include "Evaluator.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>

int g_angle_mode = 0;

static int g_fail = 0, g_pass = 0;

/* Lisible -> interne. */
static std::string to_internal(const char *s)
{
    struct
    {
        const char *from, *to;
    } m[] = {{"sqrt(", "r("}, {"sin(", "s("}, {"cos(", "c("}, {"tan(", "t("}, {"asin(", "v("},
             {"acos(", "u("}, {"atan(", "w("}, {"ln(", "l("},   {"pi", "p"}};
    std::string out;
    for (size_t i = 0; s[i];)
    {
        bool done = false;
        for (auto &e : m)
        {
            size_t n = strlen(e.from);
            if (strncmp(s + i, e.from, n) == 0)
            {
                out += e.to;
                i += n;
                done = true;
                break;
            }
        }
        if (!done)
        {
            if (s[i] == ';')
                out += CAS_SEPARATOR;
            else
                out += s[i];
            i++;
        }
    }
    return out;
}

/* Interne -> lisible (inverse des remplacements ci-dessus, pour les
 * fonctions a une lettre suivies de '('). */
static std::string to_readable(const char *s)
{
    std::string out;
    for (size_t i = 0; s[i]; i++)
    {
        char c = s[i];
        bool fn = s[i + 1] == '(' && (i == 0 || !(s[i - 1] >= 'a' && s[i - 1] <= 'z'));
        if (fn && c == 'r')
            out += "sqrt";
        else if (fn && c == 's')
            out += "sin";
        else if (fn && c == 'c')
            out += "cos";
        else if (fn && c == 't')
            out += "tan";
        else if (fn && c == 'l')
            out += "ln";
        else if (fn && c == 'v')
            out += "asin";
        else if (fn && c == 'u')
            out += "acos";
        else if (fn && c == 'w')
            out += "atan";
        else if (c == 'p' && !(i > 0 && s[i - 1] >= 'a' && s[i - 1] <= 'z') &&
                 !(s[i + 1] >= 'a' && s[i + 1] <= 'z'))
            out += "pi";
        else if (c == CAS_SEPARATOR)
            out += ";";
        else
            out += c;
    }
    return out;
}

static void check(const char *in, const char *expected)
{
    std::string txt = to_internal(in);
    char out[512];
    cas_result r = cas_eval(txt.c_str(), (int)txt.size(), out, sizeof(out));
    std::string got;
    if (r.status == CAS_OK)
        got = out[0] ? to_readable(out) : std::string("<") + r.msg + ">";
    else if (r.status == CAS_ERROR)
        got = std::string("ERR:") + r.msg;
    else
        got = "UNSUPPORTED";
    if (got == expected)
        g_pass++;
    else
    {
        g_fail++;
        printf("FAIL  %-34s attendu %-28s obtenu %s\n", in, expected, got.c_str());
    }
}

/* Le resultat, reevalue par l'evaluateur numerique, vaut la saisie. */
static void check_roundtrip(const char *in)
{
    std::string txt = to_internal(in);
    char out[512];
    cas_result r = cas_eval(txt.c_str(), (int)txt.size(), out, sizeof(out));
    if (r.status != CAS_OK || !r.numeric)
    {
        g_fail++;
        printf("FAIL  roundtrip %s : status %d\n", in, r.status);
        return;
    }
    auto eval = [](const char *s) {
        int n = 0;
        token *t = parse_string_to_token((char *)s, (int)strlen(s), &n);
        token *o = shunting_yard(t, n);
        complex z = evaluate_npi_c(o, count_yarded(t, n));
        free(t);
        free(o);
        return z.re;
    };
    double a = eval(txt.c_str()), b = eval(out);
    if (fabs(a - b) <= 1e-9 * (1 + fabs(a)) && fabs(r.approx - a) <= 1e-9 * (1 + fabs(a)))
        g_pass++;
    else
    {
        g_fail++;
        printf("FAIL  roundtrip %s : %.12g vs %.12g (%s)\n", in, a, b, to_readable(out).c_str());
    }
}

static void check_status(const char *in, int status)
{
    std::string txt = to_internal(in);
    char out[512];
    cas_result r = cas_eval(txt.c_str(), (int)txt.size(), out, sizeof(out));
    if (r.status == status)
        g_pass++;
    else
    {
        g_fail++;
        printf("FAIL  statut %s : attendu %d obtenu %d (%s %s)\n", in, status, r.status, out, r.msg);
    }
}

/* Chiffres necessaires pour ecrire le resultat exactement en decimal. */
static void check_digits(const char *in, int expected)
{
    std::string txt = to_internal(in);
    char out[512];
    cas_result r = cas_eval(txt.c_str(), (int)txt.size(), out, sizeof(out));
    if (r.status == CAS_OK && r.dec_digits == expected)
        g_pass++;
    else
    {
        g_fail++;
        printf("FAIL  decimales %s : attendu %d obtenu %d\n", in, expected, r.dec_digits);
    }
}

int main()
{
    /* --- "=" ou "~~" devant la valeur decimale --- */
    check_digits("1/3+1/6", 1);  /* 1/2 = 0.5 */
    check_digits("3/8", 3);      /* 0.375 */
    check_digits("1/1024", 10);
    check_digits("7/20", 2);     /* 0.35 */
    check_digits("12", 0);
    check_digits("1/3", -1);
    check_digits("sqrt(2)", -1);
    check_digits("3pi/4", -1);
    check_digits("resoudre(2X-1)", 1);
    check_digits("resoudre(3X-1)", -1);

    /* --- Nombres exacts --- */
    check("1/3+1/6", "1/2");
    check("2/4", "1/2");
    check("sqrt(12)", "2sqrt(3)");
    check("sqrt(8)+sqrt(2)", "3sqrt(2)");
    check("sqrt(2)*sqrt(3)", "sqrt(6)");
    check("sqrt(2)*sqrt(8)", "4");
    check("1/sqrt(2)", "sqrt(2)/2");
    check("2/sqrt(3)", "2/3sqrt(3)");
    check("sqrt(3/4)", "sqrt(3)/2");
    check("(1+sqrt(5))/2", "(1+sqrt(5))/2");
    check("developper((sqrt(2)+1)^(2))", "3+2sqrt(2)");
    check("sqrt(2)+sqrt(3)", "sqrt(2)+sqrt(3)");
    check("2^(10)", "1024");
    check("2^(-3)", "1/8");
    check("8^(1/3)", "2");
    check("(-8)^(1/3)", "-2");
    check("4^(1/4)", "sqrt(2)");
    check("2^(100)", "1267650600228229401496703205376");
    check("123456789123456789*987654321987654321", "121932631356500531347203169112635269");
    check("20!", "2432902008176640000");
    check("binom(10,3)", "120");
    check("3pi/4", "3/4pi");
    check("pi+pi", "2pi");
    check("pi/pi", "1");

    /* --- Valeurs remarquables (radians) --- */
    check("sin(pi/3)", "sqrt(3)/2");
    check("cos(pi/4)", "sqrt(2)/2");
    check("cos(2pi/3)", "-1/2");
    check("sin(7pi/6)", "-1/2");
    check("tan(pi/6)", "sqrt(3)/3");
    check("tan(pi/4)", "1");
    check("sin(-pi/2)", "-1");
    check("cos(0)", "1");
    check("2/sin(pi/3)", "4/3sqrt(3)");
    check("asin(1/2)", "pi/6");
    check("acos(-1/2)", "2/3pi");
    check("atan(1)", "pi/4");
    check("atan(sqrt(3))", "pi/3");
    check("ln(1)", "0");
    check("ln(e)", "1");
    check("ln(e^(3))", "3");
    check("e^(ln(5))", "5");
    check("ln(sqrt(e))", "1/2");
    check("log(1000)", "3");
    check("sin(1)", "sin(1)");

    /* --- Degres --- */
    g_angle_mode = 1;
    check("sin(30)", "1/2");
    check("cos(60)", "1/2");
    check("tan(45)", "1");
    check("asin(1/2)", "30");
    g_angle_mode = 0;

    /* --- Expressions en X --- */
    check("X+X", "2X");
    check("2X+3X-X", "4X");
    check("X*X", "X^(2)");
    check("X^(2)*X^(3)", "X^(5)");
    check("X/X", "1");
    check("2(X+1)-2X", "2");
    check("(X+1)^(2)", "(X+1)^(2)");
    check("(X+1)^(2)-X^(2)", "2X+1");
    check("X/2+X/3", "5/6X");
    check("sqrt(X)*sqrt(X)", "X");
    check("e^(X)*e^(X)", "e^(2X)");

    /* --- Developper --- */
    check("developper((X+1)^(2))", "X^(2)+2X+1");
    check("developper((X-1)(X+1))", "X^(2)-1");
    check("developper((X+2)^(3))", "X^(3)+6X^(2)+12X+8");
    check("developper((2X-3)(X+5))", "2X^(2)+7X-15");
    check("developper((X+sqrt(2))^(2))", "X^(2)+2Xsqrt(2)+2");

    /* --- Factoriser --- */
    check("factoriser(X^(2)-1)", "(X-1)(X+1)");
    check("factoriser(X^(2)+2X+1)", "(X+1)^(2)");
    check("factoriser(X^(2)-5X+6)", "(X-2)(X-3)");
    check("factoriser(2X^(2)-2)", "2(X-1)(X+1)");
    check("factoriser(6X^(2)-X-1)", "(2X-1)(3X+1)");
    check("factoriser(X^(3)-X)", "X(X-1)(X+1)");
    check("factoriser(X^(3)-1)", "(X-1)(X^(2)+X+1)");
    check("factoriser(X^(2)/2-1/2)", "1/2(X-1)(X+1)");
    check("factoriser(X^(2)+1)", "X^(2)+1");
    check("factoriser(360)", "2^(3)*3^(2)*5");
    check("factoriser(-12)", "-2^(2)*3");

    /* --- Deriver --- */
    check("deriver(X^(3))", "3X^(2)");
    check("deriver(X^(2)+3X+1)", "2X+3");
    check("deriver(sin(X))", "cos(X)");
    check("deriver(cos(2X))", "-2sin(2X)");
    check("deriver(e^(X))", "e^(X)");
    check("deriver(e^(2X+1))", "2e^(2X+1)");
    check("deriver(ln(X))", "1/X");
    check("deriver(sqrt(X))", "1/(2sqrt(X))");
    check("deriver(1/X)", "-1/X^(2)");
    check("deriver(X*e^(X))", "Xe^(X)+e^(X)");
    check("deriver(X^(2)*ln(X))", "2Xln(X)+X");
    check("deriver(5)", "0");

    /* --- Primitive --- */
    check("primitive(X^(2))", "X^(3)/3");
    check("primitive(3X^(2)+2X+1)", "X^(3)+X^(2)+X");
    check("primitive(1/X)", "ln(abs(X))");
    check("primitive(cos(X))", "sin(X)");
    check("primitive(sin(2X))", "-cos(2X)/2");
    check("primitive(e^(3X))", "e^(3X)/3");
    check("primitive((X+1)^(2))", "(X+1)^(3)/3");
    check("primitive(5)", "5X");
    check("primitive(ln(X))", "Xln(X)-X");
    check("primitive(X*e^(X^(2)))", "ERR:Primitive non trouvee");

    /* --- Resoudre --- */
    check("resoudre(2X-4)", "X=2");
    check("resoudre(3X+1=7)", "X=2");
    check("resoudre(X^(2)-5X+6)", "X=2;X=3");
    check("resoudre(X^(2)=2)", "X=-sqrt(2);X=sqrt(2)");
    check("resoudre(X^(2)+X-1)", "X=(-1-sqrt(5))/2;X=(-1+sqrt(5))/2");
    check("resoudre(X^(2)-2X+1)", "X=1");
    check("resoudre(X^(3)-6X^(2)+11X-6)", "X=1;X=2;X=3");
    check("resoudre(X^(3)-2X)", "X=-sqrt(2);X=0;X=sqrt(2)");
    check("resoudre(X^(2)+1)", "X=-i;X=i");
    check("resoudre(X^(2)+X+1)", "X=-1/2-1/2ir(3);X=-1/2+1/2ir(3)");
    check("resoudre(1/X=2)", "X=1/2");
    check("resoudre(X+1/X=2)", "X=1");
    check("resoudre(X/(X-1)=0)", "X=0");
    check("resoudre(X-X)", "<Tous les reels>");
    check("resoudre(X-X+1)", "<Aucune solution>");
    check("X^(2)=4", "X=-2;X=2");
    check("resoudre(cos(X)=X)", "X=0.7390851332");

    /* --- Resultats numeriques : la valeur affichee est juste --- */
    check_roundtrip("1/3+1/6");
    check_roundtrip("sqrt(12)/3");
    check_roundtrip("(1+sqrt(5))/2");
    check_roundtrip("2/sin(pi/3)");
    check_roundtrip("3pi/4");
    check_roundtrip("sqrt(2)+sqrt(3)");
    check_roundtrip("1/(1+sqrt(2))");

    /* --- Hors calcul formel / erreurs --- */
    check_status("sqrt(-4)", CAS_UNSUPPORTED); /* complexe : calcul decimal */
    check_status("ln(-1)", CAS_UNSUPPORTED);
    check_status("1/0", CAS_ERROR);
    check_status("A+1", CAS_UNSUPPORTED);
    check_status("developper((X+1)^(40)*(X+2)^(40))", CAS_ERROR);

    printf("test_cas : %d reussis, %d echecs\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
