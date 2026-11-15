#include "pika_wrapper.h"
#include "PikaPlatform.h"
#include "PikaObj.h"
#include "pikaScript.h"
#include "py_gfx.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PIKA_OUTPUT_SIZE 4096

/* ---------- Garde-fous d'execution ----------
 * Sans eux, un script pouvait figer la calculatrice : boucle infinie,
 * input() (pika_platform_getchar non fournie = while(1)), assertion de
 * PikaPython (abort) ou recursion sans fin (debordement de pile). */
#include <setjmp.h>
#include "PikaVM.h"

/* Fournies par l'application (Apps/python.cpp). Versions par defaut ci-dessous
 * pour les tests hote qui ne lient pas l'application. */
__attribute__((weak)) int oc_py_poll_stop(void) { return 0; }
__attribute__((weak)) int oc_py_input_line(const char *prompt, char *buf, int cap)
{
    (void)prompt;
    (void)buf;
    (void)cap;
    return -1; /* pas d'interface : comportement d'origine */
}

/* Pile C autorisee pendant un script. Chaque appel Python imbrique consomme
 * de l'ordre de 1 Ko : au-dela, RecursionError au lieu d'un plantage. */
#if defined(__linux__) || defined(__EMSCRIPTEN__) || defined(_WIN32) || defined(__APPLE__)
#define OC_PY_STACK_LIMIT (192 * 1024)
#else
#define OC_PY_STACK_LIMIT (12 * 1024)
#endif

static jmp_buf g_run_jmp;
static volatile int g_run_active = 0;
static volatile int g_run_stopped = 0; /* 1 = touche, 2 = recursion */
static char *g_stack_base = NULL;
static unsigned g_hook_count = 0;

void pika_hook_instruct(void)
{
    if (!g_run_active || g_run_stopped)
        return;
    char probe;
    long used = (long)(g_stack_base - &probe);
    if (used < 0)
        used = -used;
    if (used > OC_PY_STACK_LIMIT)
    {
        g_run_stopped = 2;
        pika_vm_exit();
        return;
    }
    if ((++g_hook_count & 63u) == 0 && oc_py_poll_stop())
    {
        g_run_stopped = 1;
        pika_vm_exit();
    }
}

/* Assertion ou erreur fatale de PikaPython : retour a pika_run au lieu de
 * abort() / while(1). */
static void oc_py_bail(void)
{
    if (g_run_active)
        longjmp(g_run_jmp, 1);
}
void pika_platform_abort_handler(void) { oc_py_bail(); }
void pika_platform_panic_handle(void)
{
    oc_py_bail();
    while (1)
    {
    }
}

static PikaObj *g_root = NULL;
static char g_output[PIKA_OUTPUT_SIZE];
static size_t g_out_len = 0;

/* Sortie au fil de l'eau : l'application recoit chaque ligne terminee
 * pendant l'execution (au lieu de tout voir a la fin). g_hook_pos : debut
 * du texte pas encore transmis. */
static void (*g_line_hook)(const char *s, int n, int newline) = NULL;
static size_t g_hook_pos = 0;
/* Le debut de la sortie a ete efface faute de place (sans hook). */
static int g_out_trimmed = 0;

static int is_vm_dump_line(const char *l, size_t n);

void pika_set_output_hook(void (*hook)(const char *s, int n, int newline))
{
    g_line_hook = hook;
}

/* Copie un morceau de ligne sans les codes de couleur ANSI ni les '\r'. */
static int strip_line(const char *src, size_t n, char *dst)
{
    int o = 0;
    for (size_t k = 0; k < n; k++)
    {
        char c = src[k];
        if (c == 27 && k + 1 < n && src[k + 1] == '[')
        {
            k += 2;
            while (k < n && !((src[k] >= 'A' && src[k] <= 'Z') || (src[k] >= 'a' && src[k] <= 'z')))
                k++;
            continue;
        }
        if (c != '\r')
            dst[o++] = c;
    }
    return o;
}

/* Transmet les lignes terminees (et, si force, le reste) au hook. */
static void hook_flush(int force)
{
    static char line[PIKA_OUTPUT_SIZE];
    if (g_line_hook == NULL)
        return;
    while (g_hook_pos < g_out_len)
    {
        size_t e = g_hook_pos;
        while (e < g_out_len && g_output[e] != '\n')
            e++;
        int complete = e < g_out_len;
        if (!complete && !force)
            break;
        if (!(complete && is_vm_dump_line(g_output + g_hook_pos, e - g_hook_pos)))
        {
            int n = strip_line(g_output + g_hook_pos, e - g_hook_pos, line);
            g_line_hook(line, n, complete);
        }
        g_hook_pos = complete ? e + 1 : e;
    }
}

/* Plus de place dans g_output : avec un hook, le texte deja transmis est
 * oublie (et une ligne geante est transmise en morceaux) ; sans hook, on
 * garde la fin de la sortie (avant : tout ce qui depassait 4 Ko etait
 * perdu sans le dire). */
static void make_room(void)
{
    if (g_line_hook != NULL)
    {
        memmove(g_output, g_output + g_hook_pos, g_out_len - g_hook_pos);
        g_out_len -= g_hook_pos;
        g_hook_pos = 0;
        if (g_out_len >= PIKA_OUTPUT_SIZE - 1)
            hook_flush(1);
        if (g_hook_pos > 0)
        {
            memmove(g_output, g_output + g_hook_pos, g_out_len - g_hook_pos);
            g_out_len -= g_hook_pos;
            g_hook_pos = 0;
        }
        return;
    }
    size_t cut = PIKA_OUTPUT_SIZE / 4;
    while (cut < g_out_len && g_output[cut - 1] != '\n')
        cut++;
    if (cut > g_out_len)
        cut = g_out_len;
    memmove(g_output, g_output + cut, g_out_len - cut);
    g_out_len -= cut;
    g_out_trimmed = 1;
}

static void out_write(const char *s, size_t n)
{
    while (n > 0)
    {
        if (g_out_len >= PIKA_OUTPUT_SIZE - 1)
            make_room();
        size_t room = PIKA_OUTPUT_SIZE - 1 - g_out_len;
        size_t k = n < room ? n : room;
        memcpy(g_output + g_out_len, s, k);
        g_out_len += k;
        s += k;
        n -= k;
        hook_flush(0);
    }
}

void pika_platform_printf(char *fmt, ...)
{
    char small[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(small, sizeof(small), fmt, args);
    va_end(args);
    if (n <= 0)
        return;
    if (n < (int)sizeof(small))
    {
        out_write(small, (size_t)n);
        return;
    }
    char *big = (char *)malloc((size_t)n + 1);
    if (big == NULL)
    {
        out_write(small, sizeof(small) - 1);
        return;
    }
    va_start(args, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, args);
    va_end(args);
    out_write(big, (size_t)n);
    free(big);
}

void pika_init(void)
{
    if (g_root != NULL)
        return;
    /* La banniere de PikaPython et le prelude ne vont pas a l'ecran : le
     * hook (pose par l'appli avant le premier pika_run) est coupe ici. */
    void (*hook)(const char *, int, int) = g_line_hook;
    g_line_hook = NULL;
    g_out_len = 0;
    g_root = pikaPythonInit();
    {
        extern void pika_extras_install(PikaObj * root);
        pika_extras_install(g_root);
    }
    g_out_len = 0;
    g_hook_pos = 0;
    g_line_hook = hook;
}

/* ---------- Reecriture du script ----------
 * Formes de Python que PikaPython ne connait pas (comprehensions avec if,
 * lambda, f-strings, "from m import *"...) : traduites avant l'execution
 * par Core/py_rewrite.c. */
extern char *py_rewrite(const char *src);

/* Les variables d'un script restent definies pour le suivant : si un script
 * a remplace "turtle" ou "random" (par exemple via "from random import *"),
 * on remet les modules avant chaque execution. Les alias _oc_m_* sont crees
 * par le prelude (Core/py_draw.c) et gardent les modules en vie. */
static const char *k_restore =
    "math = _oc_m_math; random = _oc_m_random; turtle = _oc_m_turtle; "
    "draw = _oc_m_draw; matplotlib = _oc_m_matplotlib; time = _oc_m_time\n";

/* Sortie lisible par un eleve : sans les codes de couleur ANSI (affiches
 * "?[31m" sur l'ecran) ni le listing interne de la machine virtuelle
 * ("   REF b    (#92)", " -> OPT +  (#94)") que PikaPython ajoute aux
 * erreurs. */
static int is_vm_dump_line(const char *l, size_t n)
{
    size_t i = 0;
    if (n >= 4 && l[0] == ' ' && l[1] == '-' && l[2] == '>' && l[3] == ' ')
        i = 4;
    else if (n >= 3 && l[0] == ' ' && l[1] == ' ' && l[2] == ' ')
        i = 3;
    else
        return 0;
    if (i + 3 > n)
        return 0;
    for (size_t k = i; k < i + 3; k++)
        if (l[k] < 'A' || l[k] > 'Z')
            return 0;
    for (size_t k = i + 3; k + 1 < n; k++)
        if (l[k] == '(' && l[k + 1] == '#')
            return 1;
    return 0;
}

static void clean_output(void)
{
    size_t o = 0;
    size_t i = 0;
    while (i < g_out_len)
    {
        size_t e = i;
        while (e < g_out_len && g_output[e] != '\n')
            e++;
        size_t next = e < g_out_len ? e + 1 : e;
        if (!is_vm_dump_line(g_output + i, e - i))
        {
            for (size_t k = i; k < next; k++)
            {
                char c = g_output[k];
                if (c == 27 && k + 1 < next && g_output[k + 1] == '[')
                {
                    k += 2;
                    while (k < next && !((g_output[k] >= 'A' && g_output[k] <= 'Z') ||
                                         (g_output[k] >= 'a' && g_output[k] <= 'z')))
                        k++;
                    continue;
                }
                g_output[o++] = c;
            }
        }
        i = next;
    }
    g_out_len = o;
    g_output[g_out_len] = '\0';
}

int pika_run(const char *script)
{
    if (g_root == NULL)
        pika_init();
    if (g_root == NULL)
        return -1;
    char stack_mark;
    char *volatile rewritten = NULL;
    g_stack_base = &stack_mark;
    g_run_stopped = 0;
    g_hook_count = 0;
    pika_vmSignal_setCtrlClear();
    if (setjmp(g_run_jmp) == 0)
    {
        g_run_active = 1;
        obj_run(g_root, (char *)k_restore);
        g_out_len = 0;
        g_hook_pos = 0;
        g_out_trimmed = 0;
        rewritten = py_rewrite(script);
        obj_run(g_root, rewritten ? rewritten : (char *)script);
        g_run_active = 0;
        py_plot_autoshow();
    }
    else
    {
        /* Assertion de PikaPython pendant le script : on revient ici au lieu
         * de figer la machine. L'interpreteur est garde tel quel (le recreer
         * bloque : son etat global n'est pas reinitialisable). */
        g_run_active = 0;
        pika_platform_printf("\nErreur interne : script arrete.\n");
    }
    free(rewritten);
    pika_vmSignal_setCtrlClear();
    if (g_run_stopped == 1)
        pika_platform_printf("\nKeyboardInterrupt (BACK)\n");
    else if (g_run_stopped == 2)
        pika_platform_printf("\nRecursionError: trop d'appels imbriques\n");
    hook_flush(1);
    clean_output();
    while (g_out_len > 0 && (g_output[g_out_len - 1] == '\n' || g_output[g_out_len - 1] == '\r'))
        g_out_len--;
    /* Sortie trop longue : on garde la fin et on le signale en tete. */
    static const char k_cut[] = "[...]\n";
    if (g_out_trimmed && g_out_len + sizeof(k_cut) - 1 < PIKA_OUTPUT_SIZE)
    {
        memmove(g_output + sizeof(k_cut) - 1, g_output, g_out_len);
        memcpy(g_output, k_cut, sizeof(k_cut) - 1);
        g_out_len += sizeof(k_cut) - 1;
    }
    g_output[g_out_len] = '\0';
    return 0;
}

const char *pika_get_output(void)
{
    g_output[g_out_len] = '\0';
    return g_output;
}

void pika_clear_output(void)
{
    g_out_len = 0;
    g_hook_pos = 0;
    g_out_trimmed = 0;
    g_output[0] = '\0';
}

void pika_deinit(void)
{
    if (g_root != NULL)
    {
        obj_deinit(g_root);
        g_root = NULL;
    }
    g_out_len = 0;
}
