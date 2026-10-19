#include "echo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "esp_aec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "link.h"
#include "sdkconfig.h"
#include "state.h"

static const char *TAG = "echo";

static aec_handle_t *aec;
static int chunk;
/* Tampons alignés exigés par ESP-SR, pour une trame du protocole (80 ms). */
static int16_t *mic;
static int16_t *reference;
static int16_t *cleaned;

bool echo_start(void)
{
#ifdef CONFIG_SATELLITE_AEC
    /* Mode « communication » : la sortie va à une conversation, pas à un moteur de mots. */
    aec = aec_create(BOARD_SAMPLE_RATE, 4, 1, AEC_MODE_VOIP_HIGH_PERF);
    if (!aec) {
        ESP_LOGE(TAG, "Annulation d'écho indisponible : semi-duplex");
        return false;
    }
    chunk = aec_get_chunksize(aec);
    const size_t bytes = LINK_FRAME_SAMPLES * sizeof(int16_t);
    mic = heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_INTERNAL);
    reference = heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_INTERNAL);
    cleaned = heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_INTERNAL);
    if (!mic || !reference || !cleaned || LINK_FRAME_SAMPLES % chunk) {
        ESP_LOGE(TAG, "Annulation d'écho : blocs de %d échantillons non pris en charge", chunk);
        aec_destroy(aec);
        aec = NULL;
        return false;
    }
    ESP_LOGI(TAG, "Annulation d'écho active (blocs de %d échantillons)", chunk);
    return true;
#else
    return false;
#endif
}

bool echo_active(void)
{
    return aec != NULL;
}

static float level_db(const int16_t *samples, size_t count)
{
    float energy = 0;
    for (size_t i = 0; i < count; i++) energy += (float)samples[i] * samples[i];
    const float rms = sqrtf(energy / count) / 32768.0f;
    return rms > 0 ? 20.0f * log10f(rms) : -120.0f;
}

void echo_process(const int16_t *interleaved, int16_t *out, size_t samples)
{
    for (size_t i = 0; i < samples; i++) {
        mic[i] = interleaved[2 * i];
        reference[i] = interleaved[2 * i + 1];
    }
    for (size_t offset = 0; offset < samples; offset += chunk)
        aec_process(aec, mic + offset, reference + offset, cleaned + offset);
    memcpy(out, cleaned, samples * sizeof(int16_t));
    /* Contrôle sur la carte : pendant que Cornelius parle, l'écho doit baisser nettement. */
    static int64_t last_report;
    const int64_t now = esp_timer_get_time();
    if (satellite.speaking && now - last_report > 2000000) {
        last_report = now;
        char report[120];
        snprintf(report, sizeof(report),
                 "Écho pendant la voix : micro %.0f dB, référence %.0f dB, après annulation %.0f dB",
                 level_db(mic, samples), level_db(reference, samples), level_db(cleaned, samples));
        ESP_LOGI(TAG, "%s", report);
        link_send_log(report);
    }
}
