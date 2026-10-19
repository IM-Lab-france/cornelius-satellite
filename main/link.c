#include "link.h"

#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "board.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "sdkconfig.h"
#include "state.h"

static const char *TAG = "link";

/* Certificat auto-signé de Cornelius (data/tls/cert.pem), embarqué à la compilation : le
 * satellite ne fait confiance qu'à lui. Vide pour une adresse ws:// de développement. */
extern const char server_cert_start[] asm("_binary_server_cert_pem_start");
extern const char server_cert_end[] asm("_binary_server_cert_pem_end");

static esp_websocket_client_handle_t client;
static char headers[320];
/* Message texte reçu en plusieurs morceaux. */
static char text[1024];
static uint8_t message_opcode;

static void send_json(cJSON *json)
{
    char *body = cJSON_PrintUnformatted(json);
    if (body && esp_websocket_client_is_connected(client))
        esp_websocket_client_send_text(client, body, strlen(body), pdMS_TO_TICKS(1000));
    cJSON_free(body);
    cJSON_Delete(json);
}

static void send_hello(void)
{
    cJSON *hello = cJSON_CreateObject();
    cJSON_AddStringToObject(hello, "type", "hello");
    cJSON_AddNumberToObject(hello, "protocol", SATELLITE_PROTOCOL);
    cJSON_AddStringToObject(hello, "firmware", esp_app_get_description()->version);
#ifdef CONFIG_SATELLITE_AEC
    cJSON_AddBoolToObject(hello, "aec", true);
#else
    cJSON_AddBoolToObject(hello, "aec", false);
#endif
    send_json(hello);
    if (satellite.muted) link_send_mic(true);
}

static void set_conversation(conversation_t next)
{
    if (satellite.conversation == next) return;
    satellite.conversation = next;
    /* Fin de conversation : plus rien à jouer de la précédente. */
    if (next != CONVERSATION_READY) audio_play_flush();
}

static void handle_text(const char *body, size_t length)
{
    cJSON *message = cJSON_ParseWithLength(body, length);
    if (!message) return;
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(message, "type"));
    if (!type) {
        cJSON_Delete(message);
        return;
    }
    if (strcmp(type, "welcome") == 0) {
        const cJSON *volume = cJSON_GetObjectItem(message, "volume");
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(message, "name"));
        if (cJSON_IsNumber(volume)) board_set_volume(volume->valueint);
        ESP_LOGI(TAG, "Accueilli par Cornelius : « %s »", name ? name : "?");
        satellite.online = true;
    } else if (strcmp(type, "listen") == 0) {
        satellite.listen = cJSON_IsTrue(cJSON_GetObjectItem(message, "enabled"));
        ESP_LOGI(TAG, "Micro %s", satellite.listen ? "demandé" : "au repos");
    } else if (strcmp(type, "wake") == 0) {
        satellite.wake_at_us = esp_timer_get_time();
        ESP_LOGI(TAG, "« Cornelius » entendu");
    } else if (strcmp(type, "busy") == 0) {
        satellite.busy_at_us = esp_timer_get_time();
        ESP_LOGI(TAG, "Occupé : la conversation ne s'ouvre pas ici");
    } else if (strcmp(type, "session") == 0) {
        const char *state = cJSON_GetStringValue(cJSON_GetObjectItem(message, "state"));
        if (state && strcmp(state, "connecting") == 0) set_conversation(CONVERSATION_CONNECTING);
        else if (state && strcmp(state, "ready") == 0) set_conversation(CONVERSATION_READY);
        else if (state && strcmp(state, "closed") == 0) set_conversation(CONVERSATION_IDLE);
        ESP_LOGI(TAG, "Conversation : %s", state ? state : "?");
    } else if (strcmp(type, "volume") == 0) {
        const cJSON *volume = cJSON_GetObjectItem(message, "volume");
        if (cJSON_IsNumber(volume)) board_set_volume(volume->valueint);
    }
    cJSON_Delete(message);
}

static void offline(void)
{
    satellite.online = false;
    satellite.listen = false;
    set_conversation(CONVERSATION_IDLE);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    const esp_websocket_event_data_t *data = event_data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket ouvert");
        send_hello();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGW(TAG, "WebSocket fermé");
        offline();
        break;
    case WEBSOCKET_EVENT_DATA: {
        /* Un morceau peut continuer le message précédent (opcode 0) ou un grand message
         * découpé par le tampon de réception (payload_offset > 0). */
        if (data->op_code == 0x1 || data->op_code == 0x2) message_opcode = data->op_code;
        else if (data->op_code != 0x0) break;
        if (message_opcode == 0x2) {
            audio_play_push((const uint8_t *)data->data_ptr, data->data_len);
        } else if (message_opcode == 0x1) {
            const size_t offset = data->payload_offset;
            if (offset + data->data_len >= sizeof(text)) break;
            memcpy(text + offset, data->data_ptr, data->data_len);
            if (offset + data->data_len >= (size_t)data->payload_len)
                handle_text(text, offset + data->data_len);
        }
        break;
    }
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "Erreur WebSocket");
        break;
    default:
        break;
    }
}

void link_start(void)
{
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", CONFIG_SATELLITE_TOKEN);
    const bool pinned = server_cert_end - server_cert_start > 1;
    esp_websocket_client_config_t config = {
        .uri = CONFIG_SATELLITE_SERVER_URI,
        .headers = headers,
        .cert_pem = pinned ? server_cert_start : NULL,
        /* Le certificat de Cornelius nomme l'ordinateur, pas son adresse IP : seul le
         * certificat exact est accepté, c'est lui qui protège la connexion. */
        .skip_cert_common_name_check = true,
        .reconnect_timeout_ms = 3000,
        .network_timeout_ms = 10000,
        .buffer_size = 8192,
        .task_stack = 6144,
        .task_prio = 6,
        .ping_interval_sec = 10,
    };
    if (!pinned && strncmp(config.uri, "wss:", 4) == 0)
        ESP_LOGE(TAG, "main/server_cert.pem est vide : la connexion wss:// sera refusée");
    client = esp_websocket_client_init(&config);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, on_event, NULL);
    esp_websocket_client_start(client);
}

void link_send_frame(const int16_t *frame)
{
    if (!satellite.online || !satellite.listen || satellite.muted) return;
    if (!esp_websocket_client_is_connected(client)) return;
    /* Une trame en retard ne sert plus à rien : on n'attend pas. */
    esp_websocket_client_send_bin(client, (const char *)frame,
                                  LINK_FRAME_SAMPLES * sizeof(int16_t), pdMS_TO_TICKS(40));
}

void link_send_mic(bool muted)
{
    if (!client || !esp_websocket_client_is_connected(client)) return;
    cJSON *mic = cJSON_CreateObject();
    cJSON_AddStringToObject(mic, "type", "mic");
    cJSON_AddBoolToObject(mic, "muted", muted);
    send_json(mic);
}
