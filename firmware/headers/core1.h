#ifndef CORE1_H
#define CORE1_H

#include <pico/util/queue.h>

/* --- Files d'échange core0 <-> core1 ---
 * Tous les échanges passent par des queues (pico/util/queue.h), thread-safe
 * entre les deux cœurs : pas de variables partagées mutables en dehors des
 * files et de deux flags de pause protégés par la handshake.
 */

#define CORE1_KEY_QUEUE_CAP 16
#define CORE1_FLUSH_QUEUE_CAP 2
#define CORE1_FREE_QUEUE_CAP 2

/* Tampon d'affichage libéré (indice « prêt à être rendu »). Voir display.cpp. */
#define CORE1_NUM_FBS 2

typedef struct
{
    int buffer; /* indice du framebuffer à transférer */
    int x0;
    int y0;
    int x1;
    int y1; /* bornes de la région dirty (incluse) */
} core1_flush_job_t;

extern queue_t opencalc_key_q;
extern queue_t opencalc_flush_q;
extern queue_t opencalc_free_q;

/* Lance core1 (scanner clavier + moteur de flush SPI). */
void core1_init(void);

/* Parke core1 (spin en RAM) autour des opérations flash de core0 :
 * l'effacement flash fige l'XIP ; core1 ne doit pas exécuter de code flash
 * pendant ce temps. */
void core1_flash_begin(void);
void core1_flash_end(void);

#endif /* CORE1_H */