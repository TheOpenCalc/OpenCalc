#ifndef PIKA_WRAPPER_H
#define PIKA_WRAPPER_H

#ifdef __cplusplus
extern "C"
{
#endif

    void pika_init(void);
    int pika_run(const char *script);
    const char *pika_get_output(void);
    void pika_clear_output(void);
    void pika_deinit(void);

    /* Module "draw" (Core/py_draw.c). pika_draw_used() : le script a dessine
     * depuis le dernier pika_draw_reset(), l'app doit montrer l'ecran. */
    int pika_draw_used(void);
    void pika_draw_reset(void);

    /* Primitives de dessin appelees par le module (coordonnees naturelles :
     * x vers la droite, y vers le bas, couleurs RGB565). Implementees dans
     * Core/py_draw_bridge.cpp (et par des stubs dans les tests hote). */
    void oc_draw_begin(void);
    void oc_draw_clear(int color);
    void oc_draw_rect(int x, int y, int w, int h, int color);
    void oc_draw_line(int x0, int y0, int x1, int y1, int color);
    void oc_draw_circle(int x, int y, int r, int color, int fill);
    void oc_draw_text(const char *s, int x, int y, int color);
    void oc_draw_show(void);
    int oc_draw_wait_key(void);
    /* Texte agrandi (size >= 2 : lettres de 5*size x 8*size px). */
    void oc_draw_text_size(const char *s, int x, int y, int color, int size);
    /* Horloge en millisecondes (cadence de l'animation de la tortue). */
    unsigned oc_draw_ticks_ms(void);

#ifdef __cplusplus
}
#endif

#endif
