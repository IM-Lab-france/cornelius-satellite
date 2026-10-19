#pragma once

#include <stdbool.h>
#include <stddef.h>

/** Répondeur mDNS du satellite (nom cornelius-satellite-xxxxxx.local). */
void discovery_start(void);

/** Nom du satellite sur le réseau (cornelius-satellite-xxxxxx), qui l'identifie à l'association. */
const char *discovery_device_name(void);

/**
 * Cherche Cornelius sur le réseau local (service mDNS `_cornelius._tcp`) et écrit son adresse
 * WebSocket dans `uri` (wss://<adresse>:<port>/satellite). Faux si aucun Cornelius ne répond.
 */
bool discovery_find(char *uri, size_t size);
