#pragma once

/**
 * Portail de configuration : point d'accès Wi-Fi « Cornelius-Satellite-xxxx » (ouvert), page
 * http://192.168.4.1 pour choisir le Wi-Fi de la maison (et, au besoin, une adresse fixe de
 * Cornelius), et redirection de toutes les adresses vers cette page (portail captif). Le
 * satellite redémarre une fois la configuration enregistrée.
 */
void portal_start(void);
