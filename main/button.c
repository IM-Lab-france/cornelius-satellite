#include "button.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link.h"
#include "nvs.h"
#include "state.h"

static const char *TAG = "button";

#define BUTTON_GPIO GPIO_NUM_0

/* Le micro coupé le reste après un redémarrage : la vie privée ne dépend pas du courant. */
static void save_muted(bool muted)
{
    nvs_handle_t nvs;
    if (nvs_open("satellite", NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_u8(nvs, "muted", muted);
    nvs_commit(nvs);
    nvs_close(nvs);
}

static bool load_muted(void)
{
    nvs_handle_t nvs;
    uint8_t muted = 0;
    if (nvs_open("satellite", NVS_READONLY, &nvs) != ESP_OK) return false;
    nvs_get_u8(nvs, "muted", &muted);
    nvs_close(nvs);
    return muted;
}

static void button_task(void *arg)
{
    int pressed_for = 0;
    for (;;) {
        const bool down = gpio_get_level(BUTTON_GPIO) == 0;
        if (down) {
            pressed_for++;
        } else if (pressed_for >= 2) { /* relâché après 40 ms au moins : anti-rebond */
            satellite.muted = !satellite.muted;
            save_muted(satellite.muted);
            link_send_mic(satellite.muted);
            ESP_LOGI(TAG, "Micro %s", satellite.muted ? "coupé" : "rouvert");
            pressed_for = 0;
        } else {
            pressed_for = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void button_start(void)
{
    satellite.muted = load_muted();
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&config);
    xTaskCreate(button_task, "button", 3072, NULL, 4, NULL);
}
