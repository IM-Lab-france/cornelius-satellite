#include "discovery.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "mdns.h"

static const char *TAG = "discovery";
static char name[40];

void discovery_start(void)
{
    if (mdns_init() != ESP_OK) {
        ESP_LOGE(TAG, "Répondeur mDNS indisponible");
        return;
    }
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, sizeof(name), "cornelius-satellite-%02x%02x%02x", mac[3], mac[4], mac[5]);
    mdns_hostname_set(name);
    ESP_LOGI(TAG, "Nom sur le réseau : %s.local", name);
}

static const char *txt_value(const mdns_result_t *result, const char *key)
{
    for (size_t i = 0; i < result->txt_count; i++)
        if (strcmp(result->txt[i].key, key) == 0) return result->txt[i].value;
    return NULL;
}

const char *discovery_device_name(void)
{
    return name;
}

bool discovery_find(char *uri, size_t size)
{
    mdns_result_t *results = NULL;
    if (mdns_query_ptr("_cornelius", "_tcp", 3000, 4, &results) != ESP_OK || !results) {
        ESP_LOGW(TAG, "Aucun Cornelius trouvé sur le réseau");
        return false;
    }
    bool found = false;
    for (const mdns_result_t *result = results; result && !found; result = result->next) {
        /* L'ordinateur annonce toutes ses adresses (Ethernet, Wi-Fi, machines virtuelles) :
         * celle du même sous-réseau que le satellite est la seule sûre d'être joignable. */
        esp_netif_ip_info_t own = {0};
        esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &own);
        esp_ip4_addr_t ip = {0};
        for (const mdns_ip_addr_t *addr = result->addr; addr; addr = addr->next) {
            if (addr->addr.type != ESP_IPADDR_TYPE_V4) continue;
            const esp_ip4_addr_t candidate = addr->addr.u_addr.ip4;
            if (ip.addr == 0) ip = candidate;
            if (own.netmask.addr &&
                ((candidate.addr ^ own.ip.addr) & own.netmask.addr) == 0) {
                ip = candidate;
                break;
            }
        }
        /* Adresse absente de la réponse : on la demande par le nom de l'ordinateur. */
        if (ip.addr == 0 && result->hostname) {
            esp_ip4_addr_t resolved;
            if (mdns_query_a(result->hostname, 2000, &resolved) == ESP_OK) ip = resolved;
        }
        if (ip.addr == 0 || result->port == 0) continue;
        const char *path = txt_value(result, "path");
        snprintf(uri, size, "wss://" IPSTR ":%u%s", IP2STR(&ip), result->port,
                 path && path[0] == '/' ? path : "/satellite");
        ESP_LOGI(TAG, "Cornelius trouvé : %s (%s)", uri,
                 result->instance_name ? result->instance_name : "?");
        found = true;
    }
    mdns_query_results_free(results);
    return found;
}
