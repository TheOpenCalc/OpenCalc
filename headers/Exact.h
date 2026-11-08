#ifndef EXACT_H
#define EXACT_H

/* Ecriture exacte d'un resultat numerique (Core/Exact.cpp).
 *
 * A partir d'un double, cherche une forme exacte simple qui lui est egale a
 * ~1e-12 pres (relatif) :
 *   - fraction irreductible            p/q          (q <= 10000)
 *   - multiple rationnel de pi         p*pi/q       (q <= 360)
 *   - racine simplifiee                k*sqrt(m)/q  (v^2 rationnel)
 *   - somme avec une racine            (a + b*sqrt(m))/d
 *
 * La forme est ecrite dans la syntaxe interne de l'editeur ('r(' = racine,
 * 'p' = pi), pour etre affichee par display_equation (fractions empilees,
 * symbole racine) et reevaluable telle quelle. Retourne false quand le nombre
 * est entier (rien a ajouter) ou qu'aucune forme simple n'est trouvee. */
bool exact_form(double v, char *out, int cap);

/* True si l'expression saisie ne contient pas de nombre decimal : comme sur
 * les calculatrices du marche, une saisie decimale donne un resultat decimal. */
bool exact_input_allowed(const char *text, int len);

#endif
