#include "config.h"

#include <string.h>
#include <stdlib.h>
#include "board.h"
#include "esp_log.h"
#include "state.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "config";
static satellite_config_t current;
/* Configuré par le portail (ou réinitialisé) : la mémoire de la carte seule fait foi. */
static bool manual;

static void load_field(nvs_handle_t nvs, const char *key, char *out, size_t size,
                       const char *fallback)
{
    size_t length = size;
    if (nvs && nvs_get_str(nvs, key, out, &length) == ESP_OK) return;
    strlcpy(out, manual ? "" : fallback, size);
}

void config_load(void)
{
    nvs_handle_t nvs = 0;
    if (nvs_open("config", NVS_READONLY, &nvs) != ESP_OK) nvs = 0;
    uint8_t flag = 0;
    if (nvs) nvs_get_u8(nvs, "manual", &flag);
    manual = flag;
    load_field(nvs, "ssid", current.wifi_ssid, sizeof(current.wifi_ssid),
               CONFIG_SATELLITE_WIFI_SSID);
    load_field(nvs, "password", current.wifi_password, sizeof(current.wifi_password),
               CONFIG_SATELLITE_WIFI_PASSWORD);
    load_field(nvs, "server", current.server_uri, sizeof(current.server_uri),
               CONFIG_SATELLITE_SERVER_URI);
    load_field(nvs, "token", current.token, sizeof(current.token), CONFIG_SATELLITE_TOKEN);
    load_field(nvs, "key", current.server_key, sizeof(current.server_key),
               CONFIG_SATELLITE_SERVER_KEY_SHA256);
    if (nvs) nvs_close(nvs);
    ESP_LOGI(TAG, "Wi-Fi « %s », Cornelius %s", current.wifi_ssid,
             current.server_uri[0] ? current.server_uri : "trouvé par mDNS");
}

const satellite_config_t *config_get(void)
{
    return &current;
}

static bool parse_color(const char *color)
{
    if (!color || strlen(color) != 7 || color[0] != '#') return false;
    const unsigned long value = strtoul(color + 1, NULL, 16);
    satellite.led_r = (value >> 16) & 0xff;
    satellite.led_g = (value >> 8) & 0xff;
    satellite.led_b = value & 0xff;
    return true;
}

void config_apply_settings(const char *color, int brightness, bool night_light, int volume,
                           bool attentive)
{
    parse_color(color);
    satellite.led_brightness = brightness < 0 ? 0 : brightness > 10 ? 10 : brightness;
    satellite.night_light = night_light;
    satellite.attentive = attentive;
    board_set_volume(volume);
    nvs_handle_t nvs;
    if (nvs_open("settings", NVS_READWRITE, &nvs) != ESP_OK) return;
    if (color) nvs_set_str(nvs, "color", color);
    nvs_set_u8(nvs, "bright", satellite.led_brightness);
    nvs_set_u8(nvs, "night", night_light);
    nvs_set_u8(nvs, "volume", volume < 0 ? 0 : volume > 100 ? 100 : volume);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void config_load_settings(void)
{
    /* Valeurs par défaut de Cornelius, tant qu'il n'a rien envoyé. */
    parse_color("#00ffb4");
    satellite.led_brightness = 6;
    uint8_t volume = 70;
    nvs_handle_t nvs;
    if (nvs_open("settings", NVS_READONLY, &nvs) == ESP_OK) {
        char color[8];
        size_t length = sizeof(color);
        if (nvs_get_str(nvs, "color", color, &length) == ESP_OK) parse_color(color);
        uint8_t value;
        if (nvs_get_u8(nvs, "bright", &value) == ESP_OK) satellite.led_brightness = value;
        if (nvs_get_u8(nvs, "night", &value) == ESP_OK) satellite.night_light = value;
        nvs_get_u8(nvs, "volume", &volume);
        nvs_close(nvs);
    }
    board_set_volume(volume);
}

bool config_set(const char *key, const char *value)
{
    nvs_handle_t nvs;
    if (nvs_open("config", NVS_READWRITE, &nvs) != ESP_OK) return false;
    const bool ok = nvs_set_str(nvs, key, value) == ESP_OK &&
                    nvs_set_u8(nvs, "manual", 1) == ESP_OK && nvs_commit(nvs) == ESP_OK;
    nvs_close(nvs);
    if (ok) config_load();
    return ok;
}

bool config_needs_setup(void)
{
    return current.wifi_ssid[0] == 0;
}

bool config_needs_pairing(void)
{
    return current.token[0] == 0;
}

void config_forget(void)
{
    nvs_handle_t nvs;
    /* Micro coupé compris : un satellite remis à zéro repart micro ouvert. */
    if (nvs_open("satellite", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (nvs_open("settings", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (nvs_open("config", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_set_u8(nvs, "manual", 1);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGW(TAG, "Configuration oubliée");
}
