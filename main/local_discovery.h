#pragma once

#include <esp_err.h>
#include <esp_netif_types.h>
#include <stdbool.h>
#include <stdint.h>

esp_err_t app_local_discovery_start(uint16_t http_port, const esp_ip4_addr_t *ipv4);
const char *app_local_discovery_hostname();
const char *app_local_discovery_fqdn();
bool app_local_discovery_ready();
