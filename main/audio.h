#pragma once

#include <stddef.h>
#include <stdint.h>

/** Tâches du micro (trames de 80 ms vers Cornelius) et du haut-parleur (voix reçue). */
void audio_start(void);

/** Voix de Cornelius reçue (PCM16 mono 16 kHz), à jouer dans l'ordre. */
void audio_play_push(const uint8_t *data, size_t length);

/** Conversation finie : ce qui reste à jouer est abandonné. */
void audio_play_flush(void);
