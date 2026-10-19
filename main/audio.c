#include "audio.h"

#include <stdbool.h>
#include <string.h>
#include "board.h"
#include "echo.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "link.h"
#include "state.h"

static const char *TAG = "audio";

/* 10 s de voix en PSRAM : le serveur l'envoie au rythme de la lecture, la marge est large. */
#define PLAY_BUFFER_BYTES (BOARD_SAMPLE_RATE * 2 * 10)
/* Avant de commencer à jouer : 200 ms, pour absorber les à-coups du Wi-Fi. */
#define PLAY_PREBUFFER_BYTES (BOARD_SAMPLE_RATE * 2 / 5)
/* Morceau écrit au codec : 60 ms. */
#define PLAY_CHUNK_BYTES 1920
/* Même seuil que Cornelius pour un son audible (RMS de 0,7 % de la pleine échelle). */
#define AUDIBLE_RMS (0.007f * 32768.0f)

static RingbufHandle_t play_buffer;
static volatile bool flush_requested;

static void capture_task(void *arg)
{
    static int16_t frame[LINK_FRAME_SAMPLES];
    static int16_t raw[LINK_FRAME_SAMPLES * BOARD_MIC_CHANNELS];
    int64_t last = esp_timer_get_time();
    for (;;) {
        /* Le micro est toujours lu, pour que le tampon I2S ne déborde jamais ; la trame ne
         * part que si Cornelius la demande et que le micro n'est pas coupé. */
        if (board_mic_read(raw, LINK_FRAME_SAMPLES * BOARD_MIC_CHANNELS) != ESP_OK) {
            ESP_LOGW(TAG, "Lecture du micro échouée");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        const int64_t now = esp_timer_get_time();
        if (now - last > 200000)
            ESP_LOGW(TAG, "Trou dans la lecture du micro : %lld ms", (now - last) / 1000);
        last = now;
        if (BOARD_MIC_CHANNELS == 1) {
            memcpy(frame, raw, sizeof(frame));
        } else if (echo_active()) {
            echo_process(raw, frame, LINK_FRAME_SAMPLES);
        } else {
            for (size_t i = 0; i < LINK_FRAME_SAMPLES; i++) frame[i] = raw[2 * i];
        }
        link_send_frame(frame);
    }
}

static size_t buffered(void)
{
    return PLAY_BUFFER_BYTES - xRingbufferGetCurFreeSize(play_buffer);
}

static bool audible(const int16_t *samples, size_t count)
{
    float energy = 0;
    for (size_t i = 0; i < count; i += 8) energy += (float)samples[i] * samples[i];
    const size_t used = (count + 7) / 8;
    return used && energy / used >= AUDIBLE_RMS * AUDIBLE_RMS;
}

static void playback_task(void *arg)
{
    bool playing = false;
    int64_t quiet_since = 0;
    for (;;) {
        if (flush_requested) {
            size_t size;
            void *item;
            while ((item = xRingbufferReceiveUpTo(play_buffer, &size, 0, PLAY_BUFFER_BYTES)))
                vRingbufferReturnItem(play_buffer, item);
            flush_requested = false;
            playing = false;
            satellite.speaking = false;
        }
        if (!playing) {
            if (buffered() < PLAY_PREBUFFER_BYTES) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            playing = true;
        }
        size_t size = 0;
        int16_t *chunk = xRingbufferReceiveUpTo(play_buffer, &size, pdMS_TO_TICKS(100),
                                                PLAY_CHUNK_BYTES);
        if (!chunk) {
            /* Plus rien : on attend de nouveau un peu d'avance avant de reprendre. */
            playing = false;
            satellite.speaking = false;
            continue;
        }
        const size_t samples = size / sizeof(int16_t);
        const int64_t now = esp_timer_get_time();
        if (audible(chunk, samples)) {
            satellite.speaking = true;
            quiet_since = now;
        } else if (now - quiet_since > 300000) {
            satellite.speaking = false;
        }
        board_speaker_write(chunk, samples);
        vRingbufferReturnItem(play_buffer, chunk);
    }
}

void audio_play_push(const uint8_t *data, size_t length)
{
    if (!play_buffer || length == 0 || satellite.conversation != CONVERSATION_READY) return;
    /* Tampon plein : la voix a pris trop de retard, ce morceau est perdu plutôt que tout
     * bloquer (la réception du WebSocket ne doit jamais attendre le haut-parleur). */
    if (xRingbufferSend(play_buffer, data, length & ~(size_t)1, 0) != pdTRUE)
        ESP_LOGW(TAG, "Tampon de lecture plein : %u octets perdus", (unsigned)length);
}

void audio_play_flush(void)
{
    flush_requested = true;
}

void audio_start(void)
{
    echo_start();
    play_buffer = xRingbufferCreateWithCaps(PLAY_BUFFER_BYTES, RINGBUF_TYPE_BYTEBUF,
                                            MALLOC_CAP_SPIRAM);
    if (!play_buffer) ESP_LOGE(TAG, "Pas de mémoire pour le tampon de lecture");
    /* 16 Ko : l'annulation d'écho d'ESP-SR travaille sur la pile de cette tâche. */
    xTaskCreatePinnedToCore(capture_task, "capture", 16384, NULL, 7, NULL, 1);
    xTaskCreatePinnedToCore(playback_task, "playback", 4096, NULL, 7, NULL, 1);
}
