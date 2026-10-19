/*
 * Satellite audio de Cornelius : micro et haut-parleur d'une autre pièce, reliés au miroir
 * par le protocole des satellites v1 (dépôt cornelius, docs/developpement/satellites-protocole.md).
 *
 * Version 1 : le mot d'appel est cherché par Cornelius (le son part quand il le demande),
 * semi-duplex (pas d'annulation d'écho sur la carte), configuration par menuconfig.
 */
#include "audio.h"
#include "board.h"
#include "button.h"
#include "config.h"
#include "discovery.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "leds.h"
#include "link.h"
#include "nvs_flash.h"
#include "state.h"
#include "wifi.h"

static const char *TAG = "satellite";

satellite_state_t satellite;

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    config_load();
    leds_start();
    button_start();
    if (board_init() != ESP_OK) {
        /* Sans codec, rien à faire : le journal série dit lequel manque. */
        ESP_LOGE(TAG, "Carte audio indisponible");
        return;
    }
    config_load_settings();
    audio_start();
    wifi_start();
    discovery_start();
    link_start();
    ESP_LOGI(TAG, "Satellite démarré");
}
