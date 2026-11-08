#ifndef CAS_H
#define CAS_H
/* Calcul formel (prototype) : Core/Cas.cpp.
 *
 * Le moteur lit le texte interne de l'editeur (le meme que celui de la
 * Calculatrice : 'r(' racine, 'p' pi, 's(' sinus, '^(' puissance...) et
 * renvoie un resultat exact dans ce meme format, pour que l'afficheur le
 * dessine (fractions empilees, racines, puissances).
 *
 * Ce qu'il sait faire :
 *   - nombres exacts : entiers de taille arbitraire, fractions, racines
 *     simplifiees (r(12) = 2r(3)), multiples de pi, valeurs remarquables de
 *     sin/cos/tan et de leurs reciproques ;
 *   - expressions en X : regroupement des termes, puissances ;
 *   - developper(f), factoriser(f) (polynomes a coefficients rationnels,
 *     entiers en facteurs premiers), deriver(f) (derivee en X),
 *     primitive(f) (regles usuelles), resoudre(f) ou resoudre(f=g)
 *     (polynomes : exact jusqu'au degre 2 et racines rationnelles, sinon
 *     valeurs approchees).
 *
 * Toute la memoire vient d'une zone allouee pour un calcul puis liberee :
 * un calcul trop gros echoue proprement ("Memoire insuffisante"). */

/* Fonctions du calcul formel dans le texte de l'editeur, et leurs jetons
 * internes (Evaluator.cpp). Une seule entree chacune. */
#define CAS_TOK_EXPAND '\x01'    /* developper( */
#define CAS_TOK_FACTOR '\x02'    /* factoriser( */
#define CAS_TOK_DERIVE '\x03'    /* deriver(    */
#define CAS_TOK_PRIMITIVE '\x04' /* primitive(  */
#define CAS_TOK_SOLVE '\x05'     /* resoudre(   */

/* Separateur de solutions dans un resultat ("X=1 ; X=2"). Caractere du texte
 * et jeton interne a la fois ; affiche ';'. */
#define CAS_SEPARATOR '\x06'

static inline bool is_cas_type(char t)
{
    return t >= CAS_TOK_EXPAND && t <= CAS_TOK_SOLVE;
}

enum cas_status
{
    CAS_OK = 0,      /* resultat ecrit dans out */
    CAS_UNSUPPORTED, /* saisie hors du calcul formel : utiliser le calcul decimal */
    CAS_ERROR,       /* erreur a montrer (message dans msg) */
};

typedef struct
{
    int status;
    bool symbolic;   /* le resultat contient X */
    bool numeric;    /* le resultat est une constante : approx est valable */
    bool transcendental; /* constante qui contient e, ln, sin... */
    bool solutions;  /* resultat de resoudre() */
    bool approximate; /* solutions approchees (methode numerique) */
    double approx;
    /* Nombre de chiffres apres la virgule pour ecrire le resultat exactement
     * en decimal (1/2 -> 1, 3/8 -> 3, 7 -> 0) ; -1 quand c'est impossible
     * (1/3, r(2), pi...). Sert a choisir "=" ou "~~" devant la valeur. */
    int dec_digits;
    char msg[40];    /* message (erreur, "Aucune solution"...) */
} cas_result;

/* Vrai si la saisie doit passer par le calcul formel : elle contient X ou
 * une fonction du calcul formel. */
bool cas_required(const char *text, int len);

/* Vrai si la saisie contient developper(, factoriser(, deriver(, primitive(
 * ou resoudre( (X seul ne compte pas : somme(X,1,3) reste numerique). */
bool cas_has_command(const char *text, int len);

/* Calcule la saisie. out recoit le resultat (texte de l'editeur, termine par
 * '\0'), vide quand il n'y a qu'un message. */
cas_result cas_eval(const char *text, int len, char *out, int cap);

/* Taille de la zone de calcul (octets). */
#define CAS_ARENA_SIZE (48 * 1024)

#endif
