#pragma once

#include <stddef.h>
#include <stdint.h>

#include <esp_err.h>
#include <esp_matter.h>

static constexpr size_t BRIDGE_MAX_REGISTERED_DEVICES = 16;

typedef struct bridge_device {
    uint32_t device_id;
    char name[40];
    char device_type[16];
    uint32_t on_signal_id;
    uint32_t off_signal_id;
    uint32_t level_up_signal_id;
    uint32_t level_down_signal_id;
    bool enabled;
} bridge_device_t;

typedef struct bridge_slot_state {
    uint8_t slot_id;
    uint16_t endpoint_id;
    uint32_t assigned_device_id;
    char display_name[40];
    uint32_t on_signal_id;
    uint32_t off_signal_id;
    uint32_t level_up_signal_id;
    uint32_t level_down_signal_id;
} bridge_slot_state_t;

esp_err_t bridge_action_init(const uint16_t *endpoint_ids, size_t count);
esp_err_t bridge_action_execute(uint8_t slot_id, uint32_t cluster_id, uint32_t attribute_id, const esp_matter_attr_val_t *val);
esp_err_t bridge_action_bind_slot(uint8_t slot_id, uint32_t on_signal_id, uint32_t off_signal_id, uint32_t level_up_signal_id,
                                  uint32_t level_down_signal_id);
esp_err_t bridge_action_register_device(const char *name, const char *device_type, uint32_t *out_device_id);
esp_err_t bridge_action_bind_device(uint32_t device_id, uint32_t on_signal_id, uint32_t off_signal_id, uint32_t level_up_signal_id,
                                    uint32_t level_down_signal_id);
esp_err_t bridge_action_unbind_signal_references(uint32_t signal_id);
esp_err_t bridge_action_rename_device(uint32_t device_id, const char *name);
esp_err_t bridge_action_assign_slot(uint8_t slot_id, uint32_t device_id);
esp_err_t bridge_action_sync_all_node_labels();
void bridge_action_log_slot_identity_dump();
const bridge_device_t *bridge_action_get_devices(size_t *count);
const bridge_slot_state_t *bridge_action_get_slots(size_t *count);
const char *bridge_action_get_slot_role(uint8_t slot_id);
