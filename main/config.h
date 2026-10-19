#pragma once

#include <stdbool.h>

/**
 * Configuration du satellite : gardée dans la mémoire de la carte (NVS), avec les valeurs de
 * menuconfig par défaut. Le portail de configuration et l'association l'écrivent ici.
 */
typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    /** Adresse fixe de Cornelius (wss://…/satellite) ; vide : découverte mDNS. */
    char server_uri[128];
    char token[96];
    /** Empreinte SHA-256 de la clé de Cornelius, en hexadécimal séparé par « : ». */
    char server_key[100];
} satellite_config_t;

/** Lit la configuration (NVS, sinon menuconfig). */
void config_load(void);

/** Configuration courante. */
const satellite_config_t *config_get(void);

/** Enregistre un champ (clé NVS : ssid, password, server, token, key). */
bool config_set(const char *key, const char *value);

/** Réglages reçus de Cornelius (couleur « #rrggbb », luminosité, veilleuse, volume). */
void config_apply_settings(const char *color, int brightness, bool night_light, int volume,
                           bool attentive);

/** Derniers réglages reçus, gardés en NVS : l'anneau et le volume sont justes dès le démarrage. */
void config_load_settings(void);

/** Pas de Wi-Fi configuré : le portail de configuration s'ouvre. */
bool config_needs_setup(void);

/** Pas de jeton : le satellite demande à être associé. */
bool config_needs_pairing(void);

/**
 * Réinitialisation (bouton tenu 5 s) : Wi-Fi, jeton, clé et réglages oubliés. Les valeurs de
 * menuconfig ne s'appliquent plus ensuite : le satellite repart du portail de configuration.
 */
void config_forget(void);
