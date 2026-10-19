#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Annulation d'écho (ESP-SR) : retire du micro la voix que joue le haut-parleur, grâce à la
 * voie de référence de l'ES7210. Avec elle, le micro peut rester ouvert pendant que Cornelius
 * parle (écoute attentive) : on peut lui couper la parole.
 */
bool echo_start(void);

/** Annulation active : annoncée à Cornelius dans « hello » (aec). */
bool echo_active(void);

/**
 * Une trame : `interleaved` alterne micro et référence (2 × `samples`), `out` reçoit le micro
 * sans l'écho (`samples` échantillons). `samples` est un multiple de la taille de bloc.
 */
void echo_process(const int16_t *interleaved, int16_t *out, size_t samples);
