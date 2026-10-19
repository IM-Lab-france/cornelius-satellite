# Satellite audio de Cornelius

Firmware ESP-IDF d'un satellite audio pour [Cornelius](https://github.com/lfpoulain/Cornelius), le miroir intelligent familial : un micro et un haut-parleur dans une autre pièce, qui ouvrent une conversation quand on dit « Cornelius ».

Carte visée : **Waveshare ESP32-S3-AUDIO-Board** (ESP32-S3R8, 8 Mo de PSRAM, 16 Mo de flash, ES8311 pour le haut-parleur, ES7210 pour les micros, anneau de LED, extenseur TCA9555).

Le dialogue avec Cornelius suit le **protocole des satellites, version 1**, décrit dans le dépôt de Cornelius : `docs/developpement/satellites-protocole.md`.

## Ce que fait la version 0.1

- Se connecte au Wi-Fi, puis à Cornelius en `wss://` avec son jeton ; ne fait confiance qu'à la clé de Cornelius, dont l'empreinte est réglée à la compilation.
- Envoie le son du micro (PCM 16 bits, 16 kHz, trames de 80 ms) **seulement quand Cornelius le demande** (`listen`) : Cornelius y cherche « Cornelius ».
- Joue la voix de Cornelius pendant la conversation, avec 200 ms d'avance pour absorber les à-coups du Wi-Fi.
- Semi-duplex : Cornelius coupe le micro pendant qu'il parle (pas encore d'annulation d'écho sur la carte, `aec: false`).
- Bouton **BOOT** : coupe ou rouvre le micro (vie privée), état gardé après un redémarrage.
- Anneau de LED :

| Couleur | Sens |
|---|---|
| bleu qui tourne | recherche du Wi-Fi |
| orange qui respire | Cornelius injoignable (adresse, jeton ou empreinte de clé) |
| éteint | veille |
| blanc bref | « Cornelius » entendu |
| turquoise qui tourne | la conversation s'ouvre |
| turquoise, plus vif quand il parle | conversation en cours |
| ambre qui clignote | occupé : une conversation se tient ailleurs |
| rouge | micro coupé |

## Préparer la compilation

ESP-IDF 5.4 ou plus récent (testé avec 5.5.6). Sous Windows, dans PowerShell :

```powershell
. C:\Users\artus\esp\esp-idf\export.ps1
idf.py set-target esp32s3
idf.py menuconfig
```

Dans **Satellite Cornelius**, renseignez :

- le réseau Wi-Fi (2,4 GHz) et son mot de passe ;
- l'adresse de Cornelius, par exemple `wss://192.168.1.10:3443/satellite` ;
- l'empreinte de la clé de Cornelius, affichée au même endroit (32 octets séparés par des deux-points) ;
- le jeton, créé dans **Administration → Satellites audio** (montré une seule fois).

Cornelius signe lui-même son certificat et le recrée quand les adresses de l'ordinateur changent ; sa clé, elle, reste la même. Le satellite vérifie donc l'empreinte de cette clé, pas le certificat : rien à recopier quand le certificat change. L'accès depuis le réseau local doit être activé dans Cornelius.

`sdkconfig` (Wi-Fi, jeton) reste hors du dépôt.

## Compiler et flasher

```powershell
idf.py build
idf.py -p COM5 flash monitor
```

Remplacez `COM5` par le port de la carte (Gestionnaire de périphériques). Le journal série dit chaque étape : carte audio, Wi-Fi, WebSocket, « Accueilli par Cornelius ».

## Organisation

| Fichier | Rôle |
|---|---|
| `main/board.c` | bus I²C, ampli (EXIO8 du TCA9555), codecs ES8311 et ES7210, I²S en duplex (sortie standard, entrée TDM sur 4 voies) |
| `main/link.c` | WebSocket et protocole : `hello`, `welcome`, `listen`, `wake`, `busy`, `session`, `volume`, `mic` |
| `main/audio.c` | lecture continue du micro, tampon de lecture en PSRAM |
| `main/leds.c` | anneau de LED (GPIO38) |
| `main/button.c` | bouton BOOT (GPIO0) |
| `main/wifi.c` | Wi-Fi en station, reconnexion |

Le câblage vient du wiki Waveshare et de la carte `waveshare/esp32-s3-audio-board` de [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) (licence MIT), dont la configuration I²S et codecs est reprise.

## Essayé sur la carte

Le 19 octobre 2026, sur une Waveshare ESP32-S3-AUDIO-Board : Wi-Fi, connexion à Cornelius, mot d'appel entendu par Cornelius, conversation à voix nette, fin après silence, mémoire au nom du profil rattaché. Le Wi-Fi a des à-coups d'une à deux secondes : le micro passe par une file d'envoi qui les absorbe (journal « Envoi du micro lent »).

## Pas encore fait

- Le nombre exact de LED de l'anneau (6 ou 7) et le réglage fin du gain du micro restent à confirmer.
- Annulation d'écho (ESP-SR, avec la voie de référence de l'ES7210) pour pouvoir couper la parole à Cornelius.
- Mot d'appel détecté sur la carte (microWakeWord), pour que rien ne parte sur le réseau en veille.
- Configuration du Wi-Fi et du jeton sans recompiler (point d'accès de configuration), mise à jour à distance.
- Boutons K1 à K3 de la carte.
