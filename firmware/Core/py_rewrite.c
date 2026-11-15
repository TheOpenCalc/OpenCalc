/* Reecriture d'un script Python avant son execution par PikaPython.
 *
 * PikaPython ne connait pas plusieurs formes courantes au lycee. Plutot que
 * de les ajouter a son compilateur, on les traduit en Python qu'il comprend :
 *
 *   [e for x in it if c]       boucle explicite avant la ligne (aussi avec
 *                              plusieurs for, for a, b in ..., et pour les
 *                              generateurs passes a sum(), max()...,
 *                              {k: v for ...} et {e for ...})
 *   a if c else b              ((c) and [a] or [b])[0]   (paresseux)
 *   lambda x: e                def _oc_lN(x): return e    (avant la ligne)
 *   f"x = {x:.2f}"             "x = {:.2f}".format(x)
 *   s[a:b:c]                   _oc_slice(s, a, b, c)
 *   a = b = v                  _oc_aN = v ; a = _oc_aN ; b = _oc_aN
 *   import math ; from random import randint, *
 *                              affectations depuis les modules deja charges
 *                              (PikaPython plantait sur "import x; ...")
 *   if c: instruction          corps remis sur sa propre ligne (PikaPython
 *                              ignorait le corps d'un if/def sur une ligne)
 *
 * Une ligne qui n'a besoin d'aucune traduction est recopiee telle quelle.
 * Les lignes ajoutees decalent les numeros de ligne des erreurs suivantes ;
 * le message d'erreur cite le texte de la ligne fautive. */
/* Noms des modules turtle et matplotlib.pyplot (Core/py_turtle.c, py_plot.c). */
const char *py_turtle_name(int i);
const char *py_plot_name(int i);
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ tampons */

typedef struct
{
    char *p;
    int n, cap;
} Sb;

static void sb_put(Sb *b, const char *s, int n)
{
    if (n <= 0)
        return;
    if (b->n + n + 1 > b->cap)
    {
        int cap = (b->cap + n) * 2 + 64;
        char *np = (char *)realloc(b->p, (size_t)cap);
        if (np == NULL)
            return;
        b->p = np;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, (size_t)n);
    b->n += n;
    b->p[b->n] = 0;
}

static void sb_cat(Sb *b, const char *s) { sb_put(b, s, (int)strlen(s)); }

static void sb_free(Sb *b)
{
    free(b->p);
    b->p = NULL;
    b->n = b->cap = 0;
}

static const char *sb_str(Sb *b) { return b->p ? b->p : ""; }

/* ------------------------------------------------------------ lexique */

static int is_id(char c) { return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }

/* s[i] commence-t-il une chaine (avec prefixe r, b, f, u eventuel) ?
 * Renvoie l'indice du guillemet, ou -1. *is_f : prefixe f. */
static int str_quote_at(const char *s, int i, int n, int *is_f)
{
    if (is_f)
        *is_f = 0;
    if (s[i] == '\'' || s[i] == '"')
        return i;
    if (i > 0 && is_id(s[i - 1]))
        return -1;
    int j = i, f = 0;
    while (j < n && j - i < 2 && strchr("rRbBuUfF", s[j]))
    {
        if (s[j] == 'f' || s[j] == 'F')
            f = 1;
        j++;
    }
    if (j > i && j < n && (s[j] == '\'' || s[j] == '"'))
    {
        if (is_f)
            *is_f = f;
        return j;
    }
    return -1;
}

/* Fin (exclue) de la chaine dont le guillemet est en q. */
static int str_end(const char *s, int q, int n)
{
    char c = s[q];
    int triple = q + 2 < n && s[q + 1] == c && s[q + 2] == c;
    int i = q + (triple ? 3 : 1);
    while (i < n)
    {
        if (s[i] == '\\')
        {
            i += 2;
            continue;
        }
        if (s[i] == c)
        {
            if (!triple)
                return i + 1;
            if (i + 2 < n && s[i + 1] == c && s[i + 2] == c)
                return i + 3;
        }
        i++;
    }
    return n;
}

/* Saute un element lexical : une chaine entiere, sinon un caractere. */
static int skip1(const char *s, int i, int n)
{
    int q = str_quote_at(s, i, n, NULL);
    if (q >= 0)
        return str_end(s, q, n);
    return i + 1;
}

static int word_at(const char *s, int i, int n, const char *w)
{
    int l = (int)strlen(w);
    if (i + l > n || strncmp(s + i, w, (size_t)l) != 0)
        return 0;
    if (i > 0 && is_id(s[i - 1]))
        return 0;
    if (i + l < n && is_id(s[i + l]))
        return 0;
    return 1;
}

/* Premier mot-cle w au niveau 0 de s[from, n), ou -1. */
static int find_kw(const char *s, int n, const char *w, int from)
{
    int d = 0;
    for (int i = from; i < n;)
    {
        char c = s[i];
        if (str_quote_at(s, i, n, NULL) >= 0)
        {
            i = skip1(s, i, n);
            continue;
        }
        if (c == '(' || c == '[' || c == '{')
            d++;
        else if (c == ')' || c == ']' || c == '}')
            d--;
        else if (d == 0 && word_at(s, i, n, w))
            return i;
        i++;
    }
    return -1;
}

/* Caractere ch au niveau 0 ; pour '=', seulement l'affectation simple. */
static int find_ch(const char *s, int n, char ch, int from)
{
    int d = 0;
    for (int i = from; i < n;)
    {
        char c = s[i];
        if (str_quote_at(s, i, n, NULL) >= 0)
        {
            i = skip1(s, i, n);
            continue;
        }
        if (c == '(' || c == '[' || c == '{')
            d++;
        else if (c == ')' || c == ']' || c == '}')
            d--;
        else if (d == 0 && c == ch)
        {
            if (ch != '=')
                return i;
            char pc = i > 0 ? s[i - 1] : 0;
            char nc = i + 1 < n ? s[i + 1] : 0;
            if (nc != '=' && !(pc && strchr("=!<>+-*/%&|^@:", pc)))
                return i;
        }
        i++;
    }
    return -1;
}

/* Indice de la parenthese fermante qui correspond a s[i]. */
static int match_close(const char *s, int i, int n)
{
    int d = 0;
    while (i < n)
    {
        char c = s[i];
        if (str_quote_at(s, i, n, NULL) >= 0)
        {
            i = skip1(s, i, n);
            continue;
        }
        if (c == '(' || c == '[' || c == '{')
            d++;
        else if (c == ')' || c == ']' || c == '}')
        {
            d--;
            if (d == 0)
                return i;
        }
        i++;
    }
    return -1;
}

static void trim(const char *s, int *a, int *b)
{
    while (*a < *b && (s[*a] == ' ' || s[*a] == '\t' || s[*a] == '\n' || s[*a] == '\r'))
        (*a)++;
    while (*b > *a && (s[*b - 1] == ' ' || s[*b - 1] == '\t' || s[*b - 1] == '\n' || s[*b - 1] == '\r'))
        (*b)--;
}

/* ------------------------------------------------------------ contexte */

typedef struct
{
    Sb *pre;        /* lignes a placer avant la ligne en cours */
    char ind[64];   /* indentation de ces lignes */
    int can_hoist;  /* 0 : pas de lignes avant (condition d'un while...) */
    int changed;
} Ctx;

static int g_uid = 0;

static void ctx_child(const Ctx *c, Ctx *k, int more_indent)
{
    *k = *c;
    if (more_indent)
    {
        size_t l = strlen(k->ind);
        if (l + 4 < sizeof(k->ind))
            memcpy(k->ind + l, "    ", 5);
    }
}

static void pre_line(Ctx *c, const char *ind, const char *text)
{
    sb_cat(c->pre, ind);
    sb_cat(c->pre, text);
    sb_cat(c->pre, "\n");
}

static void rw_expr(const char *s, int n, Ctx *c, Sb *out);

static char *rw_dup(const char *s, int n, Ctx *c)
{
    Sb o = {0};
    rw_expr(s, n, c, &o);
    char *r = strdup(sb_str(&o));
    sb_free(&o);
    return r;
}

/* Remplace les identifiants old[k] par new[k] (hors chaines, attributs et
 * noms d'arguments f(x=...)). */
static char *rename_ids(const char *s, int n, char old[][32], char nw[][32], int cnt)
{
    Sb o = {0};
    sb_cat(&o, "");
    for (int i = 0; i < n;)
    {
        if (str_quote_at(s, i, n, NULL) >= 0)
        {
            int e = skip1(s, i, n);
            sb_put(&o, s + i, e - i);
            i = e;
            continue;
        }
        if (is_id(s[i]) && !(i > 0 && is_id(s[i - 1])) && !isdigit((unsigned char)s[i]))
        {
            int e = i;
            while (e < n && is_id(s[e]))
                e++;
            int k, done = 0;
            int prev = i - 1;
            while (prev >= 0 && s[prev] == ' ')
                prev--;
            int nxt = e;
            while (nxt < n && s[nxt] == ' ')
                nxt++;
            int is_attr = prev >= 0 && s[prev] == '.';
            int is_kw = nxt < n && s[nxt] == '=' && !(nxt + 1 < n && s[nxt + 1] == '=');
            for (k = 0; k < cnt && !is_attr && !is_kw; k++)
            {
                if ((int)strlen(old[k]) == e - i && strncmp(s + i, old[k], (size_t)(e - i)) == 0)
                {
                    sb_cat(&o, nw[k]);
                    done = 1;
                    break;
                }
            }
            if (!done)
                sb_put(&o, s + i, e - i);
            i = e;
            continue;
        }
        sb_put(&o, s + i, 1);
        i++;
    }
    char *r = strdup(sb_str(&o));
    sb_free(&o);
    return r;
}

/* Identifiants d'une cible "a", "a, b", "(a, b)". */
static int target_names(const char *s, int n, char names[][32], int max)
{
    int cnt = 0;
    for (int i = 0; i < n && cnt < max;)
    {
        if (is_id(s[i]) && !isdigit((unsigned char)s[i]))
        {
            int e = i;
            while (e < n && is_id(s[e]))
                e++;
            int l = e - i < 31 ? e - i : 31;
            memcpy(names[cnt], s + i, (size_t)l);
            names[cnt][l] = 0;
            cnt++;
            i = e;
        }
        else
            i++;
    }
    return cnt;
}

/* ------------------------------------------------------- comprehensions */

/* content = "e for t in it [if c] [for ...]". kind : '[' liste, '(' generateur
 * (liste), '{' ensemble ou dictionnaire. Ecrit le nom du resultat dans out.
 * Renvoie 0 si ce n'est pas une comprehension reconnue. */
static int rw_comprehension(const char *s, int n, char kind, Ctx *c, Sb *out)
{
    int f0 = find_kw(s, n, "for", 0);
    if (f0 <= 0 || !c->can_hoist)
        return 0;
    /* decoupe en clauses */
    int cl_kind[16], cl_a[16], cl_b[16], cl_in[16];
    int ncl = 0;
    int pos = f0;
    while (pos < n && ncl < 16)
    {
        int isfor = word_at(s, pos, n, "for");
        int start = pos + (isfor ? 3 : 2);
        int nf = find_kw(s, n, "for", start);
        int ni = find_kw(s, n, "if", start);
        int end = n;
        if (nf >= 0 && nf < end)
            end = nf;
        if (ni >= 0 && ni < end)
            end = ni;
        cl_kind[ncl] = isfor;
        cl_a[ncl] = start;
        cl_b[ncl] = end;
        cl_in[ncl] = -1;
        if (isfor)
        {
            int in = find_kw(s, end, "in", start);
            if (in < 0)
                return 0;
            cl_in[ncl] = in;
        }
        ncl++;
        pos = end;
    }
    if (pos < n)
        return 0;

    char name[24];
    snprintf(name, sizeof(name), "_oc_c%d", ++g_uid);

    /* element : "k: v" pour un dictionnaire */
    int ea = 0, eb = f0;
    trim(s, &ea, &eb);
    int colon = kind == '{' ? find_ch(s, eb, ':', ea) : -1;
    int is_dict = colon >= 0;

    char line[64];
    snprintf(line, sizeof(line), "%s = %s", name,
             kind == '{' ? (is_dict ? "{}" : "set()") : "[]");
    pre_line(c, c->ind, line);

    /* variables de boucle renommees : pas de fuite hors de la comprehension */
    char old[16][32], nw[16][32];
    int nren = 0;

    Ctx k;
    ctx_child(c, &k, 0);
    for (int i = 0; i < ncl; i++)
    {
        char *part;
        Sb l = {0};
        if (cl_kind[i])
        {
            /* iterable : evalue avec les noms deja renommes */
            char *it_ren = rename_ids(s + cl_in[i] + 2, cl_b[i] - cl_in[i] - 2, old, nw, nren);
            part = rw_dup(it_ren, (int)strlen(it_ren), &k);
            free(it_ren);
            char tn[16][32];
            int nt = target_names(s + cl_a[i], cl_in[i] - cl_a[i], tn, 16);
            for (int t = 0; t < nt && nren < 16; t++)
            {
                strcpy(old[nren], tn[t]);
                snprintf(nw[nren], 32, "_oc_v%d", ++g_uid);
                nren++;
            }
            char *tg = rename_ids(s + cl_a[i], cl_in[i] - cl_a[i], old, nw, nren);
            int ta = 0, tb = (int)strlen(tg);
            trim(tg, &ta, &tb);
            sb_cat(&l, "for ");
            sb_put(&l, tg + ta, tb - ta);
            sb_cat(&l, " in ");
            free(tg);
        }
        else
        {
            char *cr = rename_ids(s + cl_a[i], cl_b[i] - cl_a[i], old, nw, nren);
            part = rw_dup(cr, (int)strlen(cr), &k);
            free(cr);
            sb_cat(&l, "if ");
        }
        int pa = 0, pb = (int)strlen(part);
        trim(part, &pa, &pb);
        sb_put(&l, part + pa, pb - pa);
        sb_cat(&l, ":");
        pre_line(c, k.ind, sb_str(&l));
        sb_free(&l);
        free(part);
        ctx_child(&k, &k, 1);
    }
    Sb l = {0};
    if (is_dict)
    {
        char *kr = rename_ids(s + ea, colon - ea, old, nw, nren);
        char *vr = rename_ids(s + colon + 1, eb - colon - 1, old, nw, nren);
        char *kk = rw_dup(kr, (int)strlen(kr), &k);
        char *vv = rw_dup(vr, (int)strlen(vr), &k);
        int a1 = 0, b1 = (int)strlen(kk), a2 = 0, b2 = (int)strlen(vv);
        trim(kk, &a1, &b1);
        trim(vv, &a2, &b2);
        sb_cat(&l, name);
        sb_cat(&l, "[");
        sb_put(&l, kk + a1, b1 - a1);
        sb_cat(&l, "] = ");
        sb_put(&l, vv + a2, b2 - a2);
        free(kr);
        free(vr);
        free(kk);
        free(vv);
    }
    else
    {
        char *er = rename_ids(s + ea, eb - ea, old, nw, nren);
        char *ee = rw_dup(er, (int)strlen(er), &k);
        int a1 = 0, b1 = (int)strlen(ee);
        trim(ee, &a1, &b1);
        sb_cat(&l, name);
        sb_cat(&l, kind == '{' ? ".add(" : ".append(");
        sb_put(&l, ee + a1, b1 - a1);
        sb_cat(&l, ")");
        free(er);
        free(ee);
    }
    pre_line(c, k.ind, sb_str(&l));
    sb_free(&l);
    sb_cat(out, name);
    c->changed = 1;
    return 1;
}

/* ------------------------------------------------------------ f-strings */

static void rw_fstring(const char *s, int a, int q, int e, Ctx *c, Sb *out)
{
    /* prefixe sans f */
    for (int i = a; i < q; i++)
        if (s[i] != 'f' && s[i] != 'F')
            sb_put(out, s + i, 1);
    char qc = s[q];
    int triple = q + 2 < e && s[q + 1] == qc && s[q + 2] == qc;
    int qlen = triple ? 3 : 1;
    int bs = q + qlen, be = e - qlen;
    Sb args = {0};
    int nargs = 0;
    sb_put(out, s + q, qlen);
    for (int i = bs; i < be;)
    {
        if (s[i] == '{' && i + 1 < be && s[i + 1] == '{')
        {
            sb_cat(out, "{{");
            i += 2;
            continue;
        }
        if (s[i] == '}' && i + 1 < be && s[i + 1] == '}')
        {
            sb_cat(out, "}}");
            i += 2;
            continue;
        }
        if (s[i] == '{')
        {
            /* fin du champ : } de meme niveau */
            int d = 0, j = i + 1;
            for (; j < be; j++)
            {
                char ch = s[j];
                if ((ch == '\'' || ch == '"') && ch != qc)
                {
                    j = str_end(s, j, be) - 1;
                    continue;
                }
                if (ch == '(' || ch == '[' || ch == '{')
                    d++;
                else if ((ch == ')' || ch == ']' || ch == '}') && d > 0)
                    d--;
                else if (d == 0 && ch == '}')
                    break;
            }
            int close = j;
            /* expression[!conversion][:format] */
            int ex_end = close, conv = -1, sp = -1;
            d = 0;
            for (int t = i + 1; t < close; t++)
            {
                char ch = s[t];
                if ((ch == '\'' || ch == '"') && ch != qc)
                {
                    t = str_end(s, t, close) - 1;
                    continue;
                }
                if (ch == '(' || ch == '[' || ch == '{')
                    d++;
                else if (ch == ')' || ch == ']' || ch == '}')
                    d--;
                else if (d == 0 && ch == '!' && t + 1 < close && s[t + 1] != '=')
                {
                    conv = t;
                    ex_end = t;
                    for (int u = t; u < close; u++)
                        if (s[u] == ':')
                        {
                            sp = u;
                            break;
                        }
                    break;
                }
                else if (d == 0 && ch == ':')
                {
                    sp = t;
                    ex_end = t;
                    break;
                }
            }
            if (nargs++)
                sb_cat(&args, ", ");
            char *ex = rw_dup(s + i + 1, ex_end - i - 1, c);
            sb_cat(&args, ex);
            free(ex);
            sb_cat(out, "{");
            if (conv >= 0)
                sb_put(out, s + conv, (sp >= 0 ? sp : close) - conv);
            if (sp >= 0)
                sb_put(out, s + sp, close - sp);
            sb_cat(out, "}");
            i = close + 1;
            continue;
        }
        sb_put(out, s + i, 1);
        i++;
    }
    sb_put(out, s + be, qlen);
    /* .format() meme sans champ : il traduit {{ et }} */
    sb_cat(out, ".format(");
    sb_cat(out, sb_str(&args));
    sb_cat(out, ")");
    sb_free(&args);
    c->changed = 1;
}

/* ------------------------------------------------------- expressions */

static void rw_elem(const char *s, int n, Ctx *c, Sb *out)
{
    int a = 0;
    while (a < n && (s[a] == ' ' || s[a] == '\n' || s[a] == '\t'))
        a++;
    sb_put(out, s, a);
    if (a < n && s[a] == '*')
    {
        int b = a;
        while (b < n && s[b] == '*')
            b++;
        sb_put(out, s + a, b - a);
        rw_expr(s + b, n - b, c, out);
        return;
    }
    /* argument nomme : nom = valeur */
    int e = a;
    while (e < n && is_id(s[e]))
        e++;
    int f = e;
    while (f < n && s[f] == ' ')
        f++;
    if (e > a && f < n && s[f] == '=' && !(f + 1 < n && s[f + 1] == '='))
    {
        sb_put(out, s + a, f + 1 - a);
        rw_expr(s + f + 1, n - f - 1, c, out);
        return;
    }
    rw_expr(s + a, n - a, c, out);
}

/* Contenu d'un groupe () [] {} : comprehension, sinon elements. */
static void rw_group(const char *s, int n, char open, Ctx *c, Sb *out)
{
    (void)open;
    int a = 0;
    while (a <= n)
    {
        int b = find_ch(s, n, ',', a);
        if (b < 0)
            b = n;
        rw_elem(s + a, b - a, c, out);
        if (b < n)
            sb_put(out, ",", 1);
        a = b + 1;
    }
}

/* Lit les morceaux d'une tranche a:b:c ; renvoie 1 si elle a un pas. */
static int slice_parts(const char *s, int n, int *p1, int *p2)
{
    *p1 = find_ch(s, n, ':', 0);
    if (*p1 < 0)
        return 0;
    *p2 = find_ch(s, n, ':', *p1 + 1);
    return *p2 >= 0;
}

static void put_slice_arg(const char *s, int n, Ctx *c, Sb *out)
{
    int a = 0, b = n;
    trim(s, &a, &b);
    if (a == b)
        sb_cat(out, "None");
    else
        rw_expr(s + a, b - a, c, out);
}

static void rw_primary(const char *s, int n, Ctx *c, Sb *out)
{
    int atom = out->n; /* debut de l'operande en cours dans out */
    char last = 0;     /* dernier caractere significatif lu */
    for (int i = 0; i < n;)
    {
        char ch = s[i];
        int isf = 0;
        int q = str_quote_at(s, i, n, &isf);
        if (q >= 0)
        {
            int e = str_end(s, q, n);
            if (!(last && (is_id(last) || last == ')' || last == ']' || last == '.')))
                atom = out->n;
            if (isf)
                rw_fstring(s, i, q, e, c, out);
            else
                sb_put(out, s + i, e - i);
            last = '"';
            i = e;
            continue;
        }
        if (ch == '(' || ch == '[' || ch == '{')
        {
            int close = match_close(s, i, n);
            if (close < 0)
            {
                sb_put(out, s + i, n - i);
                return;
            }
            int postfix = last && (is_id(last) || last == ')' || last == ']' || last == '"');
            if (!postfix)
                atom = out->n;
            int p1, p2;
            if (ch == '[' && postfix && slice_parts(s + i + 1, close - i - 1, &p1, &p2))
            {
                /* x[a:b:c] -> _oc_slice(x, a, b, c) */
                const char *in = s + i + 1;
                char *obj = strdup(out->p ? out->p + atom : "");
                out->n = atom;
                if (out->p)
                    out->p[atom] = 0;
                sb_cat(out, "_oc_slice(");
                sb_cat(out, obj);
                free(obj);
                sb_cat(out, ", ");
                put_slice_arg(in, p1, c, out);
                sb_cat(out, ", ");
                put_slice_arg(in + p1 + 1, p2 - p1 - 1, c, out);
                sb_cat(out, ", ");
                put_slice_arg(in + p2 + 1, close - i - 1 - p2 - 1, c, out);
                sb_cat(out, ")");
                c->changed = 1;
            }
            else
            {
                Sb comp = {0};
                if (rw_comprehension(s + i + 1, close - i - 1, ch, c, &comp))
                {
                    /* f(x for x in l) -> f(_oc_cN) ; [x for ...] -> _oc_cN */
                    if (ch == '(' && postfix)
                        sb_cat(out, "(");
                    sb_cat(out, sb_str(&comp));
                    if (ch == '(' && postfix)
                        sb_cat(out, ")");
                }
                else
                {
                    sb_put(out, &ch, 1);
                    rw_group(s + i + 1, close - i - 1, ch, c, out);
                    sb_put(out, s + close, 1);
                }
                sb_free(&comp);
            }
            last = s[close];
            i = close + 1;
            continue;
        }
        if (is_id(ch))
        {
            int e = i;
            while (e < n && is_id(s[e]))
                e++;
            if (!(last == '.'))
                atom = out->n;
            sb_put(out, s + i, e - i);
            last = s[e - 1];
            i = e;
            continue;
        }
        sb_put(out, s + i, 1);
        if (ch != ' ' && ch != '\t' && ch != '\n')
            last = ch;
        i++;
    }
}

static void rw_lambda(const char *s, int n, Ctx *c, Sb *out)
{
    /* s commence par "lambda" */
    int colon = find_ch(s, n, ':', 6);
    if (colon < 0 || !c->can_hoist)
    {
        sb_put(out, s, n);
        return;
    }
    char name[24];
    snprintf(name, sizeof(name), "_oc_l%d", ++g_uid);
    Sb l = {0};
    sb_cat(&l, "def ");
    sb_cat(&l, name);
    sb_cat(&l, "(");
    int a = 6, b = colon;
    trim(s, &a, &b);
    sb_put(&l, s + a, b - a);
    sb_cat(&l, "):");
    pre_line(c, c->ind, sb_str(&l));
    sb_free(&l);
    Ctx k;
    ctx_child(c, &k, 1);
    char *body = rw_dup(s + colon + 1, n - colon - 1, &k);
    int ba = 0, bb = (int)strlen(body);
    trim(body, &ba, &bb);
    Sb r = {0};
    sb_cat(&r, "return ");
    sb_put(&r, body + ba, bb - ba);
    pre_line(c, k.ind, sb_str(&r));
    sb_free(&r);
    free(body);
    sb_cat(out, name);
    c->changed = 1;
}

static void rw_expr(const char *s, int n, Ctx *c, Sb *out)
{
    int a = 0, b = n;
    trim(s, &a, &b);
    sb_put(out, s, a); /* espaces de tete */
    if (word_at(s, a, b, "lambda"))
    {
        rw_lambda(s + a, b - a, c, out);
        sb_put(out, s + b, n - b);
        return;
    }
    /* "k: v" (dictionnaire, tranche) */
    int colon = find_ch(s, b, ':', a);
    if (colon >= 0)
    {
        rw_expr(s + a, colon - a, c, out);
        sb_put(out, ":", 1);
        rw_expr(s + colon + 1, b - colon - 1, c, out);
        sb_put(out, s + b, n - b);
        return;
    }
    /* a if c else b */
    int pi = find_kw(s, b, "if", a);
    int pe = pi > a ? find_kw(s, b, "else", pi + 2) : -1;
    if (pi > a && pe > pi)
    {
        sb_cat(out, "((");
        rw_expr(s + pi + 2, pe - pi - 2, c, out);
        sb_cat(out, ") and [");
        rw_expr(s + a, pi - a, c, out);
        sb_cat(out, "] or [");
        rw_expr(s + pe + 4, b - pe - 4, c, out);
        sb_cat(out, "])[0]");
        sb_put(out, s + b, n - b);
        c->changed = 1;
        return;
    }
    rw_primary(s + a, b - a, c, out);
    sb_put(out, s + b, n - b);
}

/* ------------------------------------------------------------ imports */

static const char *const k_math[] = {
    "pi", "e", "ceil", "fabs", "floor", "fmod", "remainder", "trunc", "exp", "log", "log2",
    "log10", "pow", "sqrt", "acos", "asin", "atan", "atan2", "cos", "sin", "tan", "degrees",
    "radians", "cosh", "sinh", "tanh", "gcd", "factorial", "comb", "isqrt", "hypot", "isclose",
    "inf", "nan", NULL};
static const char *const k_random[] = {
    "random", "randint", "randrange", "seed", "uniform", "choice", "shuffle", "sample", NULL};
static const char *const k_time[] = {"time", "sleep", "monotonic", NULL};
static const char *const k_draw[] = {
    "clear", "set_pixel", "fill_rect", "line", "circle", "fill_circle", "draw_string", "color",
    "show", "wait_key", "WIDTH", "HEIGHT", "BLACK", "WHITE", "RED", "GREEN", "BLUE", "YELLOW",
    "CYAN", "MAGENTA", "ORANGE", "GRAY", NULL};
static const char *const k_turtle_extra[] = {"Turtle", "Screen", "Pen", NULL};

/* Module connu -> expression qui le designe (alias caches du prelude). */
static const char *module_expr(const char *m)
{
    if (!strcmp(m, "math"))
        return "_oc_m_math";
    if (!strcmp(m, "random"))
        return "_oc_m_random";
    if (!strcmp(m, "time"))
        return "_oc_m_time";
    if (!strcmp(m, "turtle"))
        return "_oc_m_turtle";
    if (!strcmp(m, "draw"))
        return "_oc_m_draw";
    if (!strcmp(m, "matplotlib"))
        return "_oc_m_matplotlib";
    if (!strcmp(m, "matplotlib.pyplot"))
        return "_oc_m_matplotlib.pyplot";
    return NULL;
}

static void add_binding(Sb *o, int *first, const char *name, const char *mod)
{
    if (!*first)
        sb_cat(o, "; ");
    *first = 0;
    sb_cat(o, name);
    sb_cat(o, " = ");
    sb_cat(o, mod);
    sb_cat(o, ".");
    sb_cat(o, name);
}

/* "from m import *" : toutes les affectations sur une ligne. */
static int star_bindings(const char *m, Sb *o)
{
    const char *mod = module_expr(m);
    if (!mod)
        return 0;
    int first = 1;
    const char *const *names = NULL;
    const char *(*fn)(int) = NULL;
    if (!strcmp(m, "math"))
        names = k_math;
    else if (!strcmp(m, "random"))
        names = k_random;
    else if (!strcmp(m, "time"))
        names = k_time;
    else if (!strcmp(m, "draw"))
        names = k_draw;
    else if (!strcmp(m, "turtle"))
    {
        names = k_turtle_extra;
        fn = py_turtle_name;
    }
    else if (!strcmp(m, "matplotlib.pyplot"))
        fn = py_plot_name;
    else
        return 0;
    if (fn)
        for (int i = 0; fn(i); i++)
            add_binding(o, &first, fn(i), mod);
    if (names)
        for (int i = 0; names[i]; i++)
            add_binding(o, &first, names[i], mod);
    return 1;
}

/* Lit un nom pointe (a.b.c) ; renvoie la fin. */
static int dotted(const char *s, int i, int n, char *buf, int cap)
{
    int k = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t'))
        i++;
    while (i < n && (is_id(s[i]) || s[i] == '.') && k < cap - 1)
        buf[k++] = s[i++];
    buf[k] = 0;
    return i;
}

/* Traduit une instruction import ; 1 si c'est fait (lignes dans lines). */
static int rw_import(const char *s, int n, const char *ind, Sb *lines)
{
    char m[48], nm[48], as[48];
    if (word_at(s, 0, n, "import"))
    {
        Sb o = {0};
        int i = 6;
        while (i < n)
        {
            i = dotted(s, i, n, m, sizeof(m));
            const char *mod = module_expr(m);
            if (!mod)
            {
                sb_free(&o);
                return 0;
            }
            as[0] = 0;
            while (i < n && s[i] == ' ')
                i++;
            if (word_at(s, i, n, "as"))
                i = dotted(s, i + 2, n, as, sizeof(as));
            while (i < n && s[i] == ' ')
                i++;
            sb_cat(&o, ind);
            if (as[0])
            {
                sb_cat(&o, as);
                sb_cat(&o, " = ");
                sb_cat(&o, mod);
            }
            else
            {
                /* "import a.b" lie le nom "a" */
                char top[48];
                strcpy(top, m);
                char *dot = strchr(top, '.');
                if (dot)
                    *dot = 0;
                sb_cat(&o, top);
                sb_cat(&o, " = ");
                sb_cat(&o, module_expr(top) ? module_expr(top) : mod);
            }
            sb_cat(&o, "\n");
            if (i < n && s[i] == ',')
                i++;
            else
                break;
        }
        sb_cat(lines, sb_str(&o));
        sb_free(&o);
        return 1;
    }
    if (word_at(s, 0, n, "from"))
    {
        int i = dotted(s, 4, n, m, sizeof(m));
        const char *mod = module_expr(m);
        while (i < n && s[i] == ' ')
            i++;
        if (!mod || !word_at(s, i, n, "import"))
            return 0;
        i += 6;
        while (i < n && (s[i] == ' ' || s[i] == '('))
            i++;
        Sb o = {0};
        if (i < n && s[i] == '*')
        {
            sb_cat(&o, ind);
            if (!star_bindings(m, &o))
            {
                sb_free(&o);
                return 0;
            }
            sb_cat(&o, "\n");
        }
        else
        {
            int first = 1;
            sb_cat(&o, ind);
            while (i < n)
            {
                i = dotted(s, i, n, nm, sizeof(nm));
                if (!nm[0])
                    break;
                as[0] = 0;
                while (i < n && s[i] == ' ')
                    i++;
                if (word_at(s, i, n, "as"))
                    i = dotted(s, i + 2, n, as, sizeof(as));
                if (!first)
                    sb_cat(&o, "; ");
                first = 0;
                sb_cat(&o, as[0] ? as : nm);
                sb_cat(&o, " = ");
                sb_cat(&o, mod);
                sb_cat(&o, ".");
                sb_cat(&o, nm);
                while (i < n && (s[i] == ' ' || s[i] == ')'))
                    i++;
                if (i < n && s[i] == ',')
                    i++;
                else
                    break;
            }
            sb_cat(&o, "\n");
        }
        sb_cat(lines, sb_str(&o));
        sb_free(&o);
        return 1;
    }
    return 0;
}

/* Cible "x[i][j]" (ou "f(a)[i]", "d[k][i]") : PikaPython ne sait pas y
 * affecter. On ecrit "_oc_tN = x[i]" avant, puis la cible devient
 * "_oc_tN[j]". Renvoie 1 si la cible est traduite (dans tg). */
static int fix_target(const char *t, int n, Ctx *c, Sb *tg)
{
    int a = 0, b = n;
    trim(t, &a, &b);
    if (b - a < 4 || t[b - 1] != ']')
        return 0;
    /* '[' qui ferme en b-1 */
    int open = -1;
    for (int i = a; i < b;)
    {
        if (str_quote_at(t, i, b, NULL) >= 0)
        {
            i = skip1(t, i, b);
            continue;
        }
        if (t[i] == '[' || t[i] == '(' || t[i] == '{')
        {
            int cl = match_close(t, i, b);
            if (cl < 0)
                return 0;
            if (cl == b - 1 && t[i] == '[')
                open = i;
            i = cl + 1;
            continue;
        }
        i++;
    }
    if (open <= a)
        return 0;
    int pe = open;
    while (pe > a && t[pe - 1] == ' ')
        pe--;
    if (!(t[pe - 1] == ']' || t[pe - 1] == ')'))
        return 0;
    char tmp[24];
    snprintf(tmp, sizeof(tmp), "_oc_t%d", ++g_uid);
    Sb l = {0};
    sb_cat(&l, tmp);
    sb_cat(&l, " = ");
    rw_expr(t + a, pe - a, c, &l);
    pre_line(c, c->ind, sb_str(&l));
    sb_free(&l);
    sb_cat(tg, tmp);
    sb_cat(tg, "[");
    rw_expr(t + open + 1, b - 1 - open - 1, c, tg);
    sb_cat(tg, "]");
    c->changed = 1;
    return 1;
}

/* Cible "obj[...]" : position du '[' qui ferme en fin de cible, ou -1. */
static int subscript_open(const char *t, int a, int b)
{
    if (b - a < 4 || t[b - 1] != ']')
        return -1;
    int open = -1;
    for (int i = a; i < b;)
    {
        if (str_quote_at(t, i, b, NULL) >= 0)
        {
            i = skip1(t, i, b);
            continue;
        }
        if (t[i] == '[' || t[i] == '(' || t[i] == '{')
        {
            int cl = match_close(t, i, b);
            if (cl < 0)
                return -1;
            if (cl == b - 1 && t[i] == '[')
                open = i;
            i = cl + 1;
            continue;
        }
        i++;
    }
    return open > a ? open : -1;
}

/* Cible "obj[a:b]" ou "obj[a:b:c]" : PikaPython affectait la valeur a
 * obj[a] (l[1:3] = [9] donnait [1, [9], 3, 4]). Ecrit obj (traduit) dans
 * obj_out et "a, b, c" (None pour une borne absente) dans args. Renvoie 1
 * si la cible est une tranche. */
static int slice_target(const char *t, int n, Ctx *c, Sb *obj_out, Sb *args)
{
    int a = 0, b = n;
    trim(t, &a, &b);
    int open = subscript_open(t, a, b);
    if (open < 0)
        return 0;
    const char *in = t + open + 1;
    int m = b - 1 - open - 1;
    int p1, p2 = -1;
    int step = slice_parts(in, m, &p1, &p2);
    if (p1 < 0)
        return 0;
    rw_expr(t + a, open - a, c, obj_out);
    put_slice_arg(in, p1, c, args);
    sb_cat(args, ", ");
    if (step)
    {
        put_slice_arg(in + p1 + 1, p2 - p1 - 1, c, args);
        sb_cat(args, ", ");
        put_slice_arg(in + p2 + 1, m - p2 - 1, c, args);
    }
    else
    {
        put_slice_arg(in + p1 + 1, m - p1 - 1, c, args);
        sb_cat(args, ", None");
    }
    return 1;
}

/* "del obj[k]" et "del obj[a:b]" (PikaPython ne savait supprimer qu'un nom :
 * NameError). Ecrit l'appel equivalent dans body ; renvoie 1 si traduit. */
static int rw_del_item(const char *t, int n, Ctx *c, Sb *body)
{
    int a = 0, b = n;
    trim(t, &a, &b);
    int open = subscript_open(t, a, b);
    if (open < 0)
        return 0;
    Sb obj = {0}, args = {0};
    if (slice_target(t + a, b - a, c, &obj, &args))
    {
        sb_cat(body, "_oc_setslice(");
        sb_cat(body, sb_str(&obj));
        sb_cat(body, ", ");
        sb_cat(body, sb_str(&args));
        sb_cat(body, ", [])");
    }
    else
    {
        sb_cat(body, "_oc_delitem(");
        rw_expr(t + a, open - a, c, body);
        sb_cat(body, ", ");
        rw_expr(t + open + 1, b - 1 - open - 1, c, body);
        sb_cat(body, ")");
    }
    sb_free(&obj);
    sb_free(&args);
    c->changed = 1;
    return 1;
}

/* Ecrit la cible t (traduite si besoin). */
static void put_target(const char *t, int n, Ctx *c, Sb *out)
{
    Sb tg = {0};
    if (fix_target(t, n, c, &tg))
    {
        sb_cat(out, sb_str(&tg));
        sb_cat(out, " ");
    }
    else
        sb_put(out, t, n);
    sb_free(&tg);
}

/* ---------------------------------------------------------- instructions */

/* Une instruction simple (sans ';'). Ecrit ses lignes (et celles placees
 * avant) dans lines ; renvoie 1 si elle a change. */
static int rw_simple(const char *s, int n, const char *ind, int can_hoist, Sb *lines)
{
    int a = 0, b = n;
    trim(s, &a, &b);
    s += a;
    n = b - a;
    if (rw_import(s, n, ind, lines))
        return 1;
    Sb pre = {0}, body = {0};
    Ctx c = {&pre, {0}, can_hoist, 0};
    snprintf(c.ind, sizeof(c.ind), "%s", ind);
    static const char *const kws[] = {"return", "raise", "assert", "yield", "del", NULL};
    for (int k = 0; kws[k]; k++)
    {
        if (word_at(s, 0, n, kws[k]))
        {
            int l = (int)strlen(kws[k]);
            /* del x[i], del x[a:b] (une seule cible) */
            if (!strcmp(kws[k], "del") && find_ch(s, n, ',', l) < 0 &&
                rw_del_item(s + l, n - l, &c, &body))
                goto done;
            sb_put(&body, s, l);
            rw_expr(s + l, n - l, &c, &body);
            goto done;
        }
    }
    {
        /* affectations : cibles = ... = valeur */
        int eq[16], neq = 0, from = 0;
        while (neq < 16)
        {
            int p = find_ch(s, n, '=', from);
            if (p < 0)
                break;
            eq[neq++] = p;
            from = p + 1;
        }
        if (neq == 0)
        {
            /* expression, ou affectation augmentee (x += ...) */
            int i = 0, d = 0, aug = -1;
            for (; i + 1 < n; i = skip1(s, i, n))
            {
                char ch = s[i];
                if (ch == '(' || ch == '[' || ch == '{')
                    d++;
                else if (ch == ')' || ch == ']' || ch == '}')
                    d--;
                else if (d == 0 && s[i + 1] == '=' && strchr("+-*/%&|^@", ch) &&
                         !(i > 0 && s[i - 1] == '=' ))
                {
                    aug = i + 2;
                    if (i > 0 && (s[i - 1] == '*' || s[i - 1] == '/') && s[i - 1] == ch)
                        aug = i + 2;
                    break;
                }
            }
            int ti = aug > 0 ? -1 : find_kw(s, n, "if", 0);
            int te = ti > 0 ? find_kw(s, n, "else", ti + 2) : -1;
            if (aug > 0)
            {
                int op = aug - 2;
                while (op > 0 && strchr("+-*/%&|^@", s[op - 1]))
                    op--;
                put_target(s, op, &c, &body);
                sb_put(&body, s + op, aug - op);
                rw_expr(s + aug, n - aug, &c, &body);
            }
            else if (ti > 0 && te > ti && !word_at(s, 0, n, "lambda"))
            {
                /* "a if c else b" seul sur sa ligne (print(...) if ... :
                 * print ne rend rien, la forme [a] serait vide) : if/else */
                char sub[72];
                snprintf(sub, sizeof(sub), "%s    ", ind);
                Ctx k;
                ctx_child(&c, &k, 0);
                int ca = ti + 2, cb = te, aa = 0, ab = ti, ba = te + 4, bb = n;
                trim(s, &ca, &cb);
                trim(s, &aa, &ab);
                trim(s, &ba, &bb);
                sb_cat(&body, "if ");
                rw_expr(s + ca, cb - ca, &k, &body);
                sb_cat(&body, ":\n");
                sb_cat(&body, sub);
                rw_expr(s + aa, ab - aa, &k, &body);
                sb_cat(&body, "\n");
                sb_cat(&body, ind);
                sb_cat(&body, "else:\n");
                sb_cat(&body, sub);
                rw_expr(s + ba, bb - ba, &k, &body);
                c.changed = 1;
            }
            else
                rw_expr(s, n, &c, &body);
        }
        else
        {
            int v = eq[neq - 1] + 1;
            Sb so = {0}, sa = {0};
            if (neq == 1 && slice_target(s, v - 1, &c, &so, &sa))
            {
                /* x[a:b] = v -> _oc_setslice(x, a, b, None, v) */
                sb_cat(&body, "_oc_setslice(");
                sb_cat(&body, sb_str(&so));
                sb_cat(&body, ", ");
                sb_cat(&body, sb_str(&sa));
                sb_cat(&body, ",");
                rw_expr(s + v, n - v, &c, &body);
                sb_cat(&body, ")");
                c.changed = 1;
            }
            else if (neq == 1)
            {
                put_target(s, v - 1, &c, &body);
                sb_cat(&body, "=");
                rw_expr(s + v, n - v, &c, &body);
            }
            else
            {
                /* a = b = v */
                char tmp[24];
                snprintf(tmp, sizeof(tmp), "_oc_a%d", ++g_uid);
                Sb val = {0};
                rw_expr(s + v, n - v, &c, &val);
                sb_cat(&body, tmp);
                sb_cat(&body, " =");
                sb_cat(&body, sb_str(&val));
                sb_free(&val);
                int t0 = 0;
                for (int k = 0; k < neq; k++)
                {
                    int ta = t0, tb = eq[k];
                    trim(s, &ta, &tb);
                    Sb tg = {0};
                    put_target(s + ta, tb - ta, &c, &tg);
                    sb_cat(&body, "\n");
                    sb_cat(&body, ind);
                    sb_cat(&body, sb_str(&tg));
                    sb_cat(&body, " = ");
                    sb_cat(&body, tmp);
                    sb_free(&tg);
                    t0 = eq[k] + 1;
                }
                c.changed = 1;
            }
            sb_free(&so);
            sb_free(&sa);
        }
    }
done:
    if (c.changed)
    {
        sb_cat(lines, sb_str(&pre));
        sb_cat(lines, ind);
        sb_cat(lines, sb_str(&body));
        sb_cat(lines, "\n");
    }
    sb_free(&pre);
    sb_free(&body);
    return c.changed;
}

/* Instructions separees par ';' (au niveau 0). */
static int split_semis(const char *s, int n, int *pos, int max)
{
    int cnt = 0, a = 0;
    while (a <= n && cnt < max)
    {
        int b = find_ch(s, n, ';', a);
        if (b < 0)
            b = n;
        pos[2 * cnt] = a;
        pos[2 * cnt + 1] = b;
        cnt++;
        a = b + 1;
    }
    return cnt;
}

static const char *const k_headers[] = {"if", "elif", "else", "while", "for", "def", "class",
                                        "try", "except", "finally", "with", NULL};

static int header_kw(const char *s, int n)
{
    for (int k = 0; k_headers[k]; k++)
        if (word_at(s, 0, n, k_headers[k]))
            return k;
    return -1;
}

/* Ligne logique (sans commentaire). Ecrit sa traduction dans lines et
 * renvoie 1, ou 0 si elle reste telle quelle. body_ind : indentation de la
 * ligne suivante (corps d'un bloc). */
static int rw_line(const char *code, int n, const char *ind, const char *body_ind, Sb *lines)
{
    int a = 0, b = n;
    trim(code, &a, &b);
    const char *s = code + a;
    n = b - a;
    if (n == 0)
        return 0;
    int hk = header_kw(s, n);
    if (hk >= 0)
    {
        const char *kw = k_headers[hk];
        int kl = (int)strlen(kw);
        int colon = -1;
        /* ':' de fin d'en-tete (hors lambda, dict, tranche) */
        {
            int d = 0;
            for (int i = kl; i < n; i = skip1(s, i, n))
            {
                char ch = s[i];
                if (ch == '(' || ch == '[' || ch == '{')
                    d++;
                else if (ch == ')' || ch == ']' || ch == '}')
                    d--;
                else if (d == 0 && word_at(s, i, n, "lambda"))
                {
                    int lc = find_ch(s, n, ':', i);
                    if (lc < 0)
                        break;
                    i = lc;
                }
                else if (d == 0 && ch == ':')
                {
                    colon = i;
                    break;
                }
            }
        }
        if (colon < 0)
            return 0;
        int ra = colon + 1, rb = n;
        trim(s, &ra, &rb);
        int has_body = ra < rb;
        char sub[64];
        snprintf(sub, sizeof(sub), "%s    ", ind);
        int changed = has_body;
        Sb hdr = {0}, pre = {0};
        Ctx c = {&pre, {0}, 0, 0};
        snprintf(c.ind, sizeof(c.ind), "%s", ind);
        int is_while = !strcmp(kw, "while");
        if (!strcmp(kw, "if") || !strcmp(kw, "for") || is_while)
        {
            c.can_hoist = 1;
            if (is_while)
                snprintf(c.ind, sizeof(c.ind), "%s", has_body ? sub : body_ind);
            sb_put(&hdr, s, kl);
            rw_expr(s + kl, colon - kl, &c, &hdr);
            sb_cat(&hdr, ":");
        }
        else if (!strcmp(kw, "elif"))
        {
            sb_put(&hdr, s, kl);
            rw_expr(s + kl, colon - kl, &c, &hdr);
            sb_cat(&hdr, ":");
        }
        else
            sb_put(&hdr, s, colon + 1);
        if (c.changed)
            changed = 1;
        if (!changed)
        {
            sb_free(&hdr);
            sb_free(&pre);
            return 0;
        }
        const char *bind = has_body ? sub : body_ind;
        if (is_while && pre.n > 0)
        {
            /* while c: -> while True: / calcul / if not (c): break */
            sb_cat(lines, ind);
            sb_cat(lines, "while True:\n");
            sb_cat(lines, sb_str(&pre));
            sb_cat(lines, bind);
            sb_cat(lines, "if not (");
            const char *h = sb_str(&hdr) + 5;
            int hl = (int)strlen(h) - 1;
            sb_put(lines, h, hl);
            sb_cat(lines, "):\n");
            sb_cat(lines, bind);
            sb_cat(lines, "    break\n");
        }
        else
        {
            sb_cat(lines, sb_str(&pre));
            sb_cat(lines, ind);
            sb_cat(lines, sb_str(&hdr));
            sb_cat(lines, "\n");
        }
        sb_free(&hdr);
        sb_free(&pre);
        if (has_body)
        {
            /* corps sur la meme ligne : sur ses propres lignes */
            int pos[64];
            int cnt = split_semis(s + ra, rb - ra, pos, 32);
            for (int k = 0; k < cnt; k++)
            {
                const char *st = s + ra + pos[2 * k];
                int sl = pos[2 * k + 1] - pos[2 * k];
                if (!rw_line(st, sl, sub, sub, lines))
                {
                    int x = 0, y = sl;
                    trim(st, &x, &y);
                    if (y > x)
                    {
                        sb_cat(lines, sub);
                        sb_put(lines, st + x, y - x);
                        sb_cat(lines, "\n");
                    }
                }
            }
        }
        return 1;
    }
    /* instructions simples */
    int pos[64];
    int cnt = split_semis(s, n, pos, 32);
    Sb tmp = {0};
    int changed = cnt > 1 && (find_kw(s, n, "import", 0) >= 0 || find_kw(s, n, "return", 0) >= 0);
    for (int k = 0; k < cnt; k++)
    {
        const char *st = s + pos[2 * k];
        int sl = pos[2 * k + 1] - pos[2 * k];
        if (rw_simple(st, sl, ind, 1, &tmp))
            changed = 1;
        else
        {
            int x = 0, y = sl;
            trim(st, &x, &y);
            if (y > x)
            {
                sb_cat(&tmp, ind);
                sb_put(&tmp, st + x, y - x);
                sb_cat(&tmp, "\n");
            }
        }
    }
    if (changed)
        sb_cat(lines, sb_str(&tmp));
    sb_free(&tmp);
    return changed;
}

/* ---------------------------------------------------------- programme */

/* Profondeur des parentheses et chaine triple ouverte a la fin de s[0, n). */
static void scan_state(const char *s, int n, int *depth, int *open_triple, int *comment)
{
    *comment = -1;
    for (int i = 0; i < n;)
    {
        int q = str_quote_at(s, i, n, NULL);
        if (q >= 0)
        {
            int e = str_end(s, q, n);
            char c = s[q];
            int triple = q + 2 < n && s[q + 1] == c && s[q + 2] == c;
            if (triple && (e > n || e < q + 6 || !(s[e - 1] == c && s[e - 2] == c && s[e - 3] == c)))
            {
                *open_triple = 1;
                return;
            }
            i = e;
            continue;
        }
        if (s[i] == '#')
        {
            if (*comment < 0)
                *comment = i;
            while (i < n && s[i] != '\n')
                i++;
            continue;
        }
        if (s[i] == '(' || s[i] == '[' || s[i] == '{')
            (*depth)++;
        else if (s[i] == ')' || s[i] == ']' || s[i] == '}')
            (*depth)--;
        i++;
    }
    *open_triple = 0;
}

char *py_rewrite(const char *src)
{
    if (src == NULL)
        return NULL;
    int n = (int)strlen(src);
    Sb out = {0};
    int changed = 0;
    g_uid = 0;
    int i = 0;
    while (i < n)
    {
        /* ligne logique : jusqu'a une fin de ligne au niveau 0 */
        int start = i, end = i;
        for (;;)
        {
            while (end < n && src[end] != '\n')
                end++;
            int d = 0, tri = 0, com;
            scan_state(src + start, end - start, &d, &tri, &com);
            int cont = end > start && src[end - 1] == '\\';
            if ((d > 0 || tri || cont) && end < n)
            {
                end++;
                continue;
            }
            break;
        }
        int next = end < n ? end + 1 : n;
        /* indentation, code, commentaire */
        int a = start;
        while (a < end && (src[a] == ' ' || src[a] == '\t'))
            a++;
        char ind[64];
        int il = a - start < 63 ? a - start : 63;
        memcpy(ind, src + start, (size_t)il);
        ind[il] = 0;
        int d = 0, tri = 0, com = -1;
        scan_state(src + a, end - a, &d, &tri, &com);
        int code_end = com >= 0 ? a + com : end;
        /* indentation du corps : premiere ligne non vide suivante */
        char bind[72];
        snprintf(bind, sizeof(bind), "%s    ", ind);
        for (int j = next; j < n;)
        {
            int k = j;
            while (k < n && (src[k] == ' ' || src[k] == '\t'))
                k++;
            if (k < n && src[k] != '\n' && src[k] != '#' && src[k] != '\r')
            {
                if (k - j > il && k - j < 63)
                {
                    memcpy(bind, src + j, (size_t)(k - j));
                    bind[k - j] = 0;
                }
                break;
            }
            while (k < n && src[k] != '\n')
                k++;
            j = k + 1;
        }
        Sb lines = {0};
        int r = 0;
        if (!tri && d == 0 && code_end > a && !(code_end > a && src[code_end - 1] == '\\'))
            r = rw_line(src + a, code_end - a, ind, bind, &lines);
        if (r)
        {
            changed = 1;
            /* commentaire garde au bout de la derniere ligne */
            if (com >= 0 && lines.n > 0)
            {
                lines.n--; /* '\n' final */
                sb_cat(&lines, "  ");
                sb_put(&lines, src + code_end, end - code_end);
                sb_cat(&lines, "\n");
            }
            sb_cat(&out, sb_str(&lines));
        }
        else
        {
            sb_put(&out, src + start, next - start);
            if (next == n && end == n && n > 0 && src[n - 1] != '\n')
                ;
        }
        sb_free(&lines);
        i = next;
    }
    if (!changed)
    {
        sb_free(&out);
        return NULL;
    }
    return out.p;
}
