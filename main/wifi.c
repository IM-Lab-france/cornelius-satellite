#include "wifi.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "config.h"
#include "portal.h"
#include "state.h"

static const char *TAG = "wifi";
static int retry_delay_ms = 1000;
/* Échecs depuis le démarrage, sans jamais réussir : mauvais mot de passe, réseau changé… */
static int failures;
static bool ever_connected;

static void retry_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
    if (retry_delay_ms < 60000) retry_delay_ms *= 2;
    esp_wifi_connect();
    vTaskDelete(NULL);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (!config_needs_setup()) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (config_needs_setup()) return;
        satellite.wifi = false;
        /* Le Wi-Fi enregistré ne marche pas : le portail rouvre pour le corriger, et les
         * essais continuent en même temps (une box simplement éteinte reviendra). */
        if (!ever_connected && ++failures == 6) portal_start();
        ESP_LOGW(TAG, "Wi-Fi perdu, nouvel essai dans %d ms", retry_delay_ms);
        xTaskCreate(retry_task, "wifi_retry", 2048, NULL, 3, NULL);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "Connecté, adresse " IPSTR, IP2STR(&event->ip_info.ip));
        retry_delay_ms = 1000;
        ever_connected = true;
        satellite.wifi = true;
    }
}

void wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    if (config_needs_setup()) {
        /* Satellite neuf ou réinitialisé : seul le portail de configuration s'ouvre. */
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        portal_start();
        return;
    }
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, config_get()->wifi_ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, config_get()->wifi_password, sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Son en continu : pas d'économie d'énergie, qui ajouterait des à-coups. */
    esp_wifi_set_ps(WIFI_PS_NONE);
}
