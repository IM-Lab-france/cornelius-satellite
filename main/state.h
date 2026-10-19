#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Conversation de ce satellite, telle que Cornelius l'annonce (message « session »). */
typedef enum {
    CONVERSATION_IDLE,
    CONVERSATION_CONNECTING,
    CONVERSATION_READY,
} conversation_t;

/**
 * État partagé entre les tâches (Wi-Fi, liaison, audio, LED, bouton). Chaque champ a un seul
 * écrivain ; les lectures d'un octet ou d'un entier aligné sont atomiques sur l'ESP32-S3.
 */
typedef struct {
    volatile bool wifi;          /* Wi-Fi connecté (wifi.c) */
    volatile bool online;        /* « welcome » reçu (link.c) */
    volatile bool listen;        /* le serveur demande le micro (link.c) */
    volatile bool muted;         /* bouton de vie privée (button.c) */
    volatile conversation_t conversation; /* (link.c) */
    volatile int64_t wake_at_us; /* dernier « wake », pour le signal lumineux (link.c) */
    volatile int64_t busy_at_us; /* dernier « busy » (link.c) */
    volatile bool speaking;      /* le haut-parleur joue un son audible (audio.c) */
} satellite_state_t;

extern satellite_state_t satellite;
