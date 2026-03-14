#include "local_discovery.h"

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <sdkconfig.h>
#include <mdns.h>

#include <stdio.h>
#include <string.h>

static const char *TAG = "local_discovery";
static bool s_started = false;
#if CONFIG_USE_MINIMAL_MDNS
static bool s_minimal_mdns_logged = false;
#endif
static char s_hostname[48] = { 0 };
static char s_fqdn[56] = { 0 };
#if !CONFIG_USE_MINIMAL_MDNS
static const char *kHttpInstanceName = "ESP Matter Hub UI";
#endif

static esp_err_t build_collision_safe_hostname()
{
    uint8_t mac[6] = { 0 };
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        return err;
    }

    int n = snprintf(s_hostname, sizeof(s_hostname), "esp-matter-hub-%02x%02x%02x", mac[3], mac[4], mac[5]);
    if (n <= 0 || n >= static_cast<int>(sizeof(s_hostname))) {
        return ESP_ERR_INVALID_SIZE;
    }

    n = snprintf(s_fqdn, sizeof(s_fqdn), "%s.local", s_hostname);
    if (n <= 0 || n >= static_cast<int>(sizeof(s_fqdn))) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

#if !CONFIG_USE_MINIMAL_MDNS
static esp_err_t build_delegate_addr(const esp_ip4_addr_t *ipv4, mdns_ip_addr_t *out_addr)
{
    if (!out_addr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_ip4_addr_t addr4 = {};
    if (ipv4) {
        addr4 = *ipv4;
    } else {
        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!sta_netif) {
            return ESP_ERR_INVALID_STATE;
        }

        esp_netif_ip_info_t ip_info = {};
        esp_err_t err = esp_netif_get_ip_info(sta_netif, &ip_info);
        if (err != ESP_OK) {
            return err;
        }
        addr4 = ip_info.ip;
    }

    if (addr4.addr == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(out_addr, 0, sizeof(*out_addr));
    out_addr->addr.type = ESP_IPADDR_TYPE_V4;
    out_addr->addr.u_addr.ip4 = addr4;
    out_addr->next = nullptr;
    return ESP_OK;
}
#endif

esp_err_t app_local_discovery_start(uint16_t http_port, const esp_ip4_addr_t *ipv4)
{
    (void)ipv4;

#if CONFIG_USE_MINIMAL_MDNS
    (void)http_port;
    if (!s_started) {
        esp_err_t err = build_collision_safe_hostname();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Minimal mDNS mode enabled; hostname build failed: %s", esp_err_to_name(err));
        }
        s_started = true;
    }
    if (!s_minimal_mdns_logged) {
        ESP_LOGI(TAG,
                 "Minimal mDNS mode active; HTTP service registration is skipped. If available, access via http://%s",
                 s_fqdn[0] ? s_fqdn : "<device-host>.local");
        s_minimal_mdns_logged = true;
    }
    return ESP_OK;
#else

    if (s_started) {
        return ESP_OK;
    }

    esp_err_t err = build_collision_safe_hostname();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build mDNS hostname: %s", esp_err_to_name(err));
        return err;
    }

    char local_hostname[64] = { 0 };
    bool mdns_host_ready = (mdns_hostname_get(local_hostname) == ESP_OK && local_hostname[0] != '\0');
    if (!mdns_host_ready) {
        ESP_LOGW(TAG, "Matter mDNS host not ready yet, deferring HTTP mDNS registration");
        return ESP_ERR_INVALID_STATE;
    }

    mdns_ip_addr_t alias_addr = {};
    err = build_delegate_addr(ipv4, &alias_addr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Alias address not ready, deferring: %s", esp_err_to_name(err));
        return ESP_ERR_INVALID_STATE;
    }

    if (mdns_hostname_exists(s_hostname)) {
        err = mdns_delegate_hostname_set_address(s_hostname, &alias_addr);
        if (err == ESP_ERR_NOT_FOUND) {
            err = mdns_delegate_hostname_add(s_hostname, &alias_addr);
        }
    } else {
        err = mdns_delegate_hostname_add(s_hostname, &alias_addr);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register alias hostname '%s': %s", s_hostname, esp_err_to_name(err));
        return err;
    }

    snprintf(s_fqdn, sizeof(s_fqdn), "%s.local", s_hostname);

    if (mdns_service_exists_with_instance(kHttpInstanceName, "_http", "_tcp", nullptr)) {
        mdns_service_remove_for_host(kHttpInstanceName, "_http", "_tcp", nullptr);
    }

    if (mdns_service_exists_with_instance(kHttpInstanceName, "_http", "_tcp", s_hostname)) {
        mdns_service_remove_for_host(kHttpInstanceName, "_http", "_tcp", s_hostname);
    }

    mdns_txt_item_t service_txt[] = {
        { "path", "/" },
        { "service", "esp-matter-hub" },
    };

    err = mdns_service_add_for_host(kHttpInstanceName, "_http", "_tcp", s_hostname, http_port, service_txt,
                                    sizeof(service_txt) / sizeof(service_txt[0]));

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_service_add_for_host failed: %s", esp_err_to_name(err));
        return err;
    }

    s_started = true;
    ESP_LOGI(TAG, "mDNS alias-only host: %s (local host hidden for HTTP)", s_hostname);
    ESP_LOGI(TAG, "mDNS ready: http://%s", s_fqdn);
    return ESP_OK;
#endif
}

const char *app_local_discovery_hostname()
{
    return s_hostname;
}

const char *app_local_discovery_fqdn()
{
    return s_fqdn;
}

bool app_local_discovery_ready()
{
    return s_started;
}
