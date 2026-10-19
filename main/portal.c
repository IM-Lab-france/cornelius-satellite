#include "portal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "state.h"

static const char *TAG = "portal";
static httpd_handle_t server;
static bool started;

/* --- Page --------------------------------------------------------------------------------- */

static const char PAGE_HEAD[] =
    "<!doctype html><html lang=fr><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Satellite Cornelius</title><style>"
    "body{font-family:system-ui,sans-serif;background:#0b1312;color:#e8f2ef;margin:0;padding:24px}"
    "main{max-width:420px;margin:auto}h1{font-size:1.4em}label{display:block;margin:16px 0 6px}"
    "input,select{width:100%;box-sizing:border-box;padding:12px;border-radius:8px;"
    "border:1px solid #2c4440;background:#13201e;color:inherit;font-size:1em}"
    "button{margin-top:22px;width:100%;padding:14px;border:0;border-radius:8px;"
    "background:#3fb7a3;color:#06110f;font-size:1em;font-weight:600}"
    "small,details{color:#9fb8b2}details{margin-top:18px}</style></head><body><main>"
    "<h1>Satellite Cornelius</h1>"
    "<p>Choisissez le Wi-Fi de la maison (2,4 GHz). Le satellite y trouvera Cornelius tout "
    "seul, puis il apparaîtra dans l'administration, rubrique Satellites audio, pour y être "
    "associé.</p><form method=post action=/save><label for=ssid>Réseau Wi-Fi</label>";

static const char PAGE_TAIL[] =
    "<label for=password>Mot de passe du Wi-Fi</label>"
    "<input id=password name=password type=password autocomplete=current-password>"
    "<details><summary>Adresse fixe de Cornelius (facultatif)</summary>"
    "<label for=server>Adresse</label>"
    "<input id=server name=server placeholder='wss://192.168.1.10:3443/satellite'>"
    "<small>Laisser vide : le satellite trouve Cornelius sur le réseau.</small></details>"
    "<button type=submit>Enregistrer</button></form></main></body></html>";

/* Échappe le nom d'un réseau dans la page (guillemets et chevrons). */
static void append_escaped(char *out, size_t size, const char *text)
{
    size_t used = strlen(out);
    for (const char *p = text; *p && used + 7 < size; p++) {
        const char *entity = *p == '<' ? "&lt;" : *p == '>' ? "&gt;" : *p == '&' ? "&amp;"
                            : *p == '\'' ? "&#39;" : *p == '"' ? "&quot;" : NULL;
        if (entity) {
            strcpy(out + used, entity);
            used += strlen(entity);
        } else {
            out[used++] = *p;
            out[used] = 0;
        }
    }
}

static esp_err_t page_get(httpd_req_t *req)
{
    /* Réseaux à portée, du plus fort au plus faible (l'ESP32 ne voit que le 2,4 GHz). */
    uint16_t count = 16;
    wifi_ap_record_t records[16];
    if (esp_wifi_scan_start(NULL, true) != ESP_OK ||
        esp_wifi_scan_get_ap_records(&count, records) != ESP_OK)
        count = 0;
    static char options[2600];
    options[0] = 0;
    for (uint16_t i = 0; i < count; i++) {
        const char *ssid = (const char *)records[i].ssid;
        bool seen = false;
        for (uint16_t j = 0; j < i && !seen; j++)
            seen = strcmp((const char *)records[j].ssid, ssid) == 0;
        if (!ssid[0] || seen) continue;
        strlcat(options, "<option value='", sizeof(options));
        append_escaped(options, sizeof(options), ssid);
        strlcat(options, "'>", sizeof(options));
        append_escaped(options, sizeof(options), ssid);
        strlcat(options, "</option>", sizeof(options));
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    if (options[0]) {
        httpd_resp_sendstr_chunk(req, "<select id=ssid name=ssid>");
        httpd_resp_sendstr_chunk(req, options);
        httpd_resp_sendstr_chunk(req, "</select><small>Réseau absent de la liste ? "
                                      "Écrivez son nom :</small>"
                                      "<input name=ssid_other aria-label='Autre réseau'>");
    } else {
        httpd_resp_sendstr_chunk(req, "<input id=ssid name=ssid_other required>");
    }
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    return httpd_resp_sendstr_chunk(req, NULL);
}

/* --- Enregistrement ----------------------------------------------------------------------- */

static void url_decode(char *text)
{
    char *out = text;
    for (char *p = text; *p; p++) {
        if (*p == '+') {
            *out++ = ' ';
        } else if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            const char hex[3] = {p[1], p[2], 0};
            *out++ = (char)strtol(hex, NULL, 16);
            p += 2;
        } else {
            *out++ = *p;
        }
    }
    *out = 0;
}

static void form_value(const char *body, const char *key, char *out, size_t size)
{
    out[0] = 0;
    if (httpd_query_key_value(body, key, out, size) == ESP_OK) url_decode(out);
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

static esp_err_t save_post(httpd_req_t *req)
{
    char body[512] = {0};
    const int length = httpd_req_recv(req, body, sizeof(body) - 1);
    if (length <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Formulaire vide.");
    char ssid[33], other[33], password[65], uri[128];
    form_value(body, "ssid", ssid, sizeof(ssid));
    form_value(body, "ssid_other", other, sizeof(other));
    form_value(body, "password", password, sizeof(password));
    form_value(body, "server", uri, sizeof(uri));
    const char *chosen = other[0] ? other : ssid;
    if (!chosen[0]) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Réseau manquant.");
    config_set("ssid", chosen);
    config_set("password", password);
    config_set("server", uri);
    ESP_LOGI(TAG, "Wi-Fi « %s » enregistré, redémarrage", chosen);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
                       "<!doctype html><meta charset=utf-8><meta name=viewport "
                       "content='width=device-width,initial-scale=1'><body style='font-family:"
                       "system-ui;background:#0b1312;color:#e8f2ef;padding:24px'><h1>C'est "
                       "enregistré</h1><p>Le satellite redémarre et rejoint votre Wi-Fi. "
                       "Reconnectez ce téléphone à votre Wi-Fi habituel, puis ouvrez "
                       "l'administration de Cornelius : le satellite y attend d'être associé.</p>");
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/* Toute autre adresse (détection de portail des téléphones) mène à la page. */
static esp_err_t redirect_get(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

/* --- DNS : toute question reçoit l'adresse du satellite (portail captif) ------------------- */

static void dns_task(void *arg)
{
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(53)};
    if (sock < 0 || bind(sock, (struct sockaddr *)&address, sizeof(address)) < 0) {
        ESP_LOGE(TAG, "Serveur DNS du portail indisponible");
        vTaskDelete(NULL);
    }
    uint8_t packet[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t from_length = sizeof(from);
        const int length = recvfrom(sock, packet, sizeof(packet) - 16, 0,
                                    (struct sockaddr *)&from, &from_length);
        if (length < 12 || (packet[2] & 0x80)) continue; /* réponses ignorées */
        /* Fin de la première question : nom (étiquettes), puis type et classe. */
        int end = 12;
        while (end < length && packet[end]) end += packet[end] + 1;
        end += 5;
        if (end > length) continue;
        packet[2] = 0x81; /* réponse, récursion souhaitée */
        packet[3] = 0x80; /* récursion disponible, pas d'erreur */
        packet[6] = 0, packet[7] = 1; /* une réponse */
        packet[8] = packet[9] = packet[10] = packet[11] = 0;
        const uint8_t answer[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1};
        memcpy(packet + end, answer, sizeof(answer));
        sendto(sock, packet, end + sizeof(answer), 0, (struct sockaddr *)&from, from_length);
    }
}

/* --- Démarrage ------------------------------------------------------------------------------ */

void portal_start(void)
{
    if (started) return;
    started = true;
    satellite.setup = true;
    esp_netif_create_default_wifi_ap();
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    wifi_config_t ap = {
        .ap = {.max_connection = 4, .authmode = WIFI_AUTH_OPEN, .channel = 1},
    };
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "Cornelius-Satellite-%02X%02X", mac[4],
             mac[5]);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    /* Station gardée : elle sert à lister les réseaux, et continue d'essayer le Wi-Fi connu. */
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    ESP_LOGI(TAG, "Portail de configuration : Wi-Fi « %s », http://192.168.4.1", ap.ap.ssid);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Serveur web du portail indisponible");
        return;
    }
    const httpd_uri_t page = {.uri = "/", .method = HTTP_GET, .handler = page_get};
    const httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_post};
    const httpd_uri_t other = {.uri = "/*", .method = HTTP_GET, .handler = redirect_get};
    httpd_register_uri_handler(server, &page);
    httpd_register_uri_handler(server, &save);
    httpd_register_uri_handler(server, &other);
    xTaskCreate(dns_task, "dns", 3072, NULL, 4, NULL);
}
