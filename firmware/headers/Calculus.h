#ifndef CALCULUS_H
#define CALCULUS_H

#include "stack.h"

/* Analyse dans la Calculatrice : somme, produit, derivee en un point et
 * integrale, ecrites avec la variable X (touche X) :
 *
 *   somme(f,a,b)       f(a) + f(a+1) + ... + f(b)   (a, b entiers)
 *   produit(f,a,b)     f(a) * f(a+1) * ... * f(b)
 *   derivee(f,a)       f'(a), calcul numerique (methode de Ridders)
 *   integrale(f,a,b)   integrale de f entre a et b (Gauss-Kronrod adaptative)
 *
 * Les appels peuvent s'imbriquer et les bornes peuvent dependre du X d'un
 * appel englobant : integrale(derivee(X^3,X),0,2) = 8.
 * Jetons internes (Evaluator.cpp) : '{' somme, '}' produit, '_' derivee,
 * '"' integrale. Ils ne passent jamais par l'evaluateur RPN. */

/* Vrai si le texte contient au moins un de ces appels. */
bool calculus_has_call(const char *text, int n);

/* Vrai si le texte contient derivee( ou integrale( : le resultat est alors
 * approche, il ne faut pas chercher a l'ecrire sous forme exacte. */
bool calculus_is_approx(const char *text, int n);

/* Evalue une saisie de la Calculatrice qui contient ces appels. Renvoie NaN
 * en cas d'erreur (argument manquant, bornes non entieres, trop de termes,
 * integrale qui ne converge pas...). */
complex calculus_eval(const char *text, int n);

/* Nombre maximal de termes d'une somme ou d'un produit. */
#define CALCULUS_MAX_TERMS 100000

#endif
