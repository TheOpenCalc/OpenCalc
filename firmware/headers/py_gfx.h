#ifndef PY_GFX_H
#define PY_GFX_H

/* Outils communs aux modules Python graphiques (draw, turtle,
 * matplotlib.pyplot) : lecture des arguments, couleurs, trace de segments
 * rognes a l'ecran. Core/py_gfx.c. Usage interne (C uniquement). */

#include "PikaObj.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PYG_W 320
#define PYG_H 240

/* Couleurs RGB565 de base. */
#define PYG_BLACK 0x0000
#define PYG_WHITE 0xFFFF

/* A appeler avant tout dessin : efface l'ecran au premier dessin du script
 * et marque l'ecran comme "a montrer" (voir pika_draw_used). */
void pyg_begin(void);

/* L'ecran a ete montre a l'utilisateur (attente d'une touche faite) : l'app
 * n'a plus besoin de l'afficher a la fin du script. */
void pyg_shown(void);

/* Envoie l'image a l'ecran au plus toutes les ~40 ms (animation). */
void pyg_flush_throttled(void);

/* Envoie l'image a l'ecran et attend une touche. */
void pyg_show_and_wait(void);

/* Nombre (int, float ou bool). Renvoie 0 si l'argument n'est pas un nombre. */
int pyg_num(Arg *a, double *out);

/* Nombre de rang i d'un tuple d'arguments (0 si absent ou invalide). */
int pyg_tuple_num(PikaTuple *t, int i, double *out);

/* Liste, tuple ou nombre seul -> tableau de doubles alloue (a liberer avec
 * free). Renvoie le nombre d'elements, ou -1 si l'argument n'est pas une
 * suite de nombres. */
int pyg_seq(Arg *a, double **out);

/* Couleur RGB565 a partir d'un nom ('red', 'r', '#ff0000'), d'un tuple
 * (r, g, b) ou d'un entier RGB565. scale255 : les composantes d'un tuple
 * sont sur 0..255 (sinon 0..1, et 0..255 si l'une depasse 1). Renvoie 0 si
 * la couleur n'est pas reconnue. */
int pyg_color(Arg *a, int scale255, int *out);
int pyg_color_name(const char *s, int *out);
int pyg_rgb(double r, double g, double b, int scale255);

/* Arguments d'une methode declaree "*a,**kw" : nombre d'arguments
 * positionnels, argument de rang i (NULL si absent), argument nomme (NULL si
 * absent). */
int pyg_nargs(Args *a);
Arg *pyg_arg(Args *a, int i);
Arg *pyg_kw(Args *a, const char *name);
/* Argument de rang i, sinon argument nomme name. */
Arg *pyg_arg_or_kw(Args *a, int i, const char *name);

/* Leve une erreur Python (le script s'arrete) avec ce message. */
void pyg_error(PikaObj *self, const char *msg);

/* Segment en coordonnees ecran (x vers la droite, y vers le bas), rogne a
 * l'ecran. width : epaisseur en pixels. dash : 0 plein, 1 tirets, 2 points. */
void pyg_line(double x0, double y0, double x1, double y1, int color, int width, int dash);
/* Le motif des pointilles continue d'un appel de pyg_line au suivant ; a
 * remettre a zero au debut de chaque courbe. */
void pyg_dash_reset(void);

/* Zone de rognage des traces (pyg_line, pyg_fill_poly, pyg_rect).
 * Par defaut : tout l'ecran. */
void pyg_set_clip(int x0, int y0, int x1, int y1);
void pyg_reset_clip(void);
/* Rectangle plein rogne, coordonnees ecran (coins quelconques). */
void pyg_rect(double x0, double y0, double x1, double y1, int color);

/* Polygone plein (regle pair-impair), coordonnees ecran. */
void pyg_fill_poly(const float *xs, const float *ys, int n, int color);

/* Disque plein. */
void pyg_disc(double x, double y, double r, int color);

/* Texte : coin haut gauche, taille 1 = 5x8 px par lettre. */
void pyg_text(double x, double y, const char *s, int color, int size);
int pyg_text_w(const char *s, int size);

/* Nombre court pour les graduations (au plus 7 caracteres). */
void pyg_fmt(double v, char *buf, int cap);

/* Modules (Core/py_turtle.c, Core/py_plot.c). */
void py_turtle_install(PikaObj *root);
void py_turtle_reset(void);
void py_plot_install(PikaObj *root);
void py_plot_reset(void);
/* Le script se termine avec une figure non affichee : on l'affiche. */
void py_plot_autoshow(void);
/* Noms des fonctions de chaque module (NULL apres le dernier), pour
 * traduire "from module import *". */
const char *py_turtle_name(int i);
const char *py_plot_name(int i);

#endif
