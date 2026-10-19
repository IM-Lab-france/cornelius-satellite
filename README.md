# Satellite audio de Cornelius

Firmware ESP-IDF d'un satellite audio pour [Cornelius](https://github.com/lfpoulain/Cornelius), le miroir intelligent familial : un micro et un haut-parleur dans une autre pièce, qui ouvrent une conversation quand on dit « Cornelius ».

Carte visée : **Waveshare ESP32-S3-AUDIO-Board** (ESP32-S3R8, 8 Mo de PSRAM, 16 Mo de flash, ES8311 pour le haut-parleur, ES7210 pour les micros, anneau de LED, extenseur TCA9555).

Le dialogue avec Cornelius suit le **protocole des satellites, version 1**, décrit dans le dépôt de Cornelius : `docs/developpement/satellites-protocole.md`.

## Mettre en service un satellite

Aucune compilation à refaire, aucun jeton à recopier :

1. Branchez le satellite (neuf, ou remis à zéro). Son anneau **respire en bleu** : il ouvre un réseau Wi-Fi « **Cornelius-Satellite-XXXX** ».
2. Connectez-y un téléphone. La page de configuration s'ouvre seule (sinon : `http://192.168.4.1`). Choisissez le Wi-Fi de la maison (2,4 GHz) et son mot de passe, puis **Enregistrer**.
3. Le satellite redémarre, rejoint votre Wi-Fi et trouve Cornelius sur le réseau (mDNS). Son anneau **respire en blanc** : il attend d'être associé.
4. Dans Cornelius, **Administration → Satellites audio → Nouveaux satellites** : « Faire clignoter » pour le reconnaître, une pièce, puis **Associer**. Le satellite reçoit son jeton, retient la clé de Cornelius et redémarre : il est prêt.

Si le Wi-Fi enregistré ne répond pas (mot de passe changé…), le portail rouvre de lui-même après quelques essais.

**Remise à zéro** : bouton **BOOT** tenu 5 secondes (pas RST), l'anneau clignote en rouge, puis le satellite repart de l'étape 1.

## Ce qu'il fait

- Envoie le son du micro (PCM 16 bits, 16 kHz, trames de 80 ms) **seulement quand Cornelius le demande** (`listen`) : Cornelius y cherche « Cornelius ».
- Joue la voix de Cornelius pendant la conversation, avec 200 ms d'avance pour absorber les à-coups du Wi-Fi.
- Ne fait confiance qu'à la clé de Cornelius retenue à l'association (empreinte SHA-256), même quand Cornelius renouvelle son certificat.
- Applique les réglages de l'administration (message `settings`) : volume, couleur et luminosité de l'anneau, veilleuse ; gardés dans la carte pour le démarrage suivant.
- Annulation d'écho (ESP-SR, voie de référence du haut-parleur sur l'ES7210) : avec l'**écoute attentive** activée dans l'administration, le micro reste ouvert pendant que Cornelius parle et on peut lui couper la parole. Sinon, semi-duplex : Cornelius coupe le micro pendant qu'il parle. Mesurée sur la carte : sa voix baisse de 35 à 55 dB ; les niveaux partent dans le journal de Cornelius (« Journal du satellite ») pendant qu'il parle.
- Bouton **BOOT** : appui bref, micro coupé ou rouvert (vie privée, gardé après un redémarrage) ; tenu 5 s, remise à zéro.

| Anneau | Sens |
|---|---|
| bleu qui respire | portail de configuration ouvert |
| bleu qui tourne | recherche du Wi-Fi |
| blanc qui respire | en attente d'association dans l'administration |
| blanc qui clignote | « Faire clignoter » |
| orange qui respire | Cornelius injoignable |
| éteint, ou veilleuse | veille |
| couleur choisie, bref | « Cornelius » entendu |
| couleur choisie qui tourne | la conversation s'ouvre |
| couleur choisie, plus vive quand il parle | conversation en cours |
| ambre qui clignote | occupé : une conversation se tient ailleurs |
| rouge | micro coupé |
| rouge qui clignote | remise à zéro |

La couleur principale et la luminosité se règlent dans l'administration ; les alertes gardent la leur.

## Compiler et flasher

ESP-IDF 5.4 ou plus récent (testé avec 5.5.6). Sous Windows, dans PowerShell :

```powershell
. C:\Users\artus\esp\esp-idf\export.ps1
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

Remplacez `COM5` par le port de la carte (Gestionnaire de périphériques). Le journal série dit chaque étape : carte audio, Wi-Fi, Cornelius trouvé, « Accueilli par Cornelius ».

Pour le développement, `idf.py menuconfig` (**Satellite Cornelius**) permet aussi d'inscrire le Wi-Fi, une adresse fixe de Cornelius, son empreinte de clé et un jeton créé dans l'administration : ces valeurs servent tant que la carte n'a pas été configurée par le portail ou remise à zéro. `sdkconfig` reste hors du dépôt.

## Organisation

| Fichier | Rôle |
|---|---|
| `main/board.c` | bus I²C, ampli (EXIO8 du TCA9555), codecs ES8311 et ES7210, I²S en duplex (sortie standard, entrée TDM sur 4 voies) |
| `main/config.c` | configuration en NVS (Wi-Fi, adresse, jeton, clé), réglages reçus, remise à zéro |
| `main/portal.c` | portail de configuration : point d'accès, page, DNS captif |
| `main/discovery.c` | mDNS : nom du satellite, recherche de `_cornelius._tcp` |
| `main/link.c` | WebSocket et protocole : association, `hello`, `settings`, `listen`, `wake`, `busy`, `session`, `mic` ; clé épinglée |
| `main/audio.c` | lecture continue du micro, tampon de lecture en PSRAM |
| `main/echo.c` | annulation d'écho ESP-SR (micro 1 et référence, blocs de 16 ms) |
| `main/leds.c` | anneau de LED (GPIO38, ordre RGB) |
| `main/button.c` | bouton BOOT (GPIO0) |
| `main/wifi.c` | Wi-Fi en station, reconnexion, portail si le Wi-Fi ne répond pas |

Le câblage vient du wiki Waveshare et de la carte `waveshare/esp32-s3-audio-board` de [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) (licence MIT), dont la configuration I²S et codecs est reprise.

## Essayé sur la carte

Le 19 octobre 2026, sur une Waveshare ESP32-S3-AUDIO-Board : portail de configuration, association depuis l'administration, découverte mDNS, réglages appliqués, mot d'appel entendu par Cornelius, conversation à voix nette, fin après silence, mémoire au nom du profil rattaché, annulation d'écho et parole coupée.

TCP rattrape un paquet perdu en 1, 2 voire 4 s : le micro passe par une file d'envoi qui tolère 10 s et abandonne les trames trop anciennes (journal « Envoi du micro lent »). Si ces à-coups gênent en conversation, compresser le son (Opus) réduirait les pertes.

## Pas encore fait

- Mot d'appel détecté sur la carte (microWakeWord), pour que rien ne parte sur le réseau en veille.
- Mise à jour du firmware à distance.
- Le nombre exact de LED de l'anneau (6 ou 7), le réglage fin du gain du micro, les boutons K1 à K3.
