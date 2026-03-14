#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <esp_log.h>
#include <nvs.h>

#include "app_priv.h"
#include "bridge_action.h"
#include "ir_engine.h"

static const char *TAG = "bridge_action";
static const char *kNvsNamespace = "bridge_map";
static const char *kNvsKeyRegistry = "registry_v1";
static constexpr uint32_t kRegistryVersion = 1;
static constexpr uint32_t kClusterOnOff = 0x0006;
static constexpr uint32_t kClusterLevelControl = 0x0008;
static constexpr uint32_t kClusterBasicInformation = 0x0028;
static constexpr uint32_t kClusterBridgedDeviceBasicInformation = 0x0039;
static constexpr uint32_t kAttributeOnOff = 0x0000;
static constexpr uint32_t kAttributeCurrentLevel = 0x0000;
static constexpr uint32_t kAttributeNodeLabel = 0x0005;
static constexpr uint32_t kAttributeUniqueID = 0x0012;
static constexpr size_t kMaxNodeLabelLength = 32;

typedef struct bridge_registry_store {
    uint32_t version;
    uint32_t next_device_id;
    uint32_t device_count;
    bridge_device_t devices[BRIDGE_MAX_REGISTERED_DEVICES];
    uint32_t slot_assignments[BRIDGE_SLOT_COUNT];
} bridge_registry_store_t;

static bridge_registry_store_t s_registry = {
    .version = kRegistryVersion,
    .next_device_id = 1,
    .device_count = 0,
};
static bridge_slot_state_t s_slots[BRIDGE_SLOT_COUNT];
static uint8_t s_last_level[BRIDGE_SLOT_COUNT];
static bool s_last_level_valid[BRIDGE_SLOT_COUNT];
static size_t s_slot_count = 0;

static bool read_char_attribute(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return false;
    }
    out[0] = '\0';

    esp_matter_attr_val_t val = {};
    if (esp_matter::attribute::get_val(endpoint_id, cluster_id, attribute_id, &val) != ESP_OK) {
        return false;
    }
    if (val.type != ESP_MATTER_VAL_TYPE_CHAR_STRING && val.type != ESP_MATTER_VAL_TYPE_LONG_CHAR_STRING) {
        return false;
    }
    if (!val.val.a.b || val.val.a.s == 0) {
        return false;
    }

    size_t copy_len = val.val.a.s;
    if (copy_len >= out_size) {
        copy_len = out_size - 1;
    }
    memcpy(out, val.val.a.b, copy_len);
    out[copy_len] = '\0';
    return true;
}

static esp_err_t write_node_label_with_retry(uint16_t endpoint_id, uint32_t cluster_id, esp_matter_attr_val_t *label_val)
{
    static constexpr uint8_t kMaxAttempts = 6;
    for (uint8_t attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        esp_err_t err = esp_matter::attribute::set_val(endpoint_id, cluster_id, kAttributeNodeLabel, label_val);
        if (err == ESP_OK || err == ESP_ERR_NOT_FINISHED) {
            if (esp_matter::is_started()) {
                (void)esp_matter::attribute::report(endpoint_id, cluster_id, kAttributeNodeLabel, label_val);
            }
            return ESP_OK;
        }
        if (err != ESP_ERR_NO_MEM || attempt == kMaxAttempts) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    return ESP_ERR_NO_MEM;
}

static int find_device_index_by_id(uint32_t device_id)
{
    if (device_id == 0) {
        return -1;
    }
    for (uint32_t i = 0; i < s_registry.device_count; ++i) {
        if (s_registry.devices[i].device_id == device_id && s_registry.devices[i].enabled) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static esp_err_t sync_slot_node_label(uint8_t slot_id)
{
    if (slot_id >= s_slot_count) {
        return ESP_ERR_INVALID_ARG;
    }

    const bridge_slot_state_t *slot = &s_slots[slot_id];
    uint16_t endpoint_id = slot->endpoint_id;
    size_t label_len = strnlen(slot->display_name, sizeof(slot->display_name));
    if (label_len > kMaxNodeLabelLength) {
        label_len = kMaxNodeLabelLength;
    }
    esp_matter_attr_val_t label_val = esp_matter_char_str(const_cast<char *>(slot->display_name), static_cast<uint16_t>(label_len));

    esp_matter::attribute_t *attr =
        esp_matter::attribute::get(endpoint_id, kClusterBridgedDeviceBasicInformation, kAttributeNodeLabel);
    if (attr) {
        char before_label[48] = { 0 };
        bool has_before = read_char_attribute(endpoint_id, kClusterBridgedDeviceBasicInformation, kAttributeNodeLabel,
                                              before_label, sizeof(before_label));
        if (has_before && strcmp(before_label, slot->display_name) == 0) {
            ESP_LOGI(TAG,
                     "NodeLabel sync slot=%u endpoint=%u cluster=0x%04lx name='%s' result=SKIP matched=1 readback='%s'",
                     static_cast<unsigned>(slot_id), static_cast<unsigned>(endpoint_id),
                     static_cast<unsigned long>(kClusterBridgedDeviceBasicInformation), slot->display_name, before_label);
            return ESP_OK;
        }

        esp_err_t err = write_node_label_with_retry(endpoint_id, kClusterBridgedDeviceBasicInformation, &label_val);
        char verify_label[48] = { 0 };
        bool matched = read_char_attribute(endpoint_id, kClusterBridgedDeviceBasicInformation, kAttributeNodeLabel,
                                           verify_label, sizeof(verify_label)) &&
                       (strcmp(verify_label, slot->display_name) == 0);

        ESP_LOGI(TAG, "NodeLabel sync slot=%u endpoint=%u cluster=0x%04lx name='%s' result=%s matched=%u readback='%s'",
                 static_cast<unsigned>(slot_id), static_cast<unsigned>(endpoint_id),
                 static_cast<unsigned long>(kClusterBridgedDeviceBasicInformation), slot->display_name, esp_err_to_name(err),
                 matched ? 1U : 0U, verify_label);
        if (err == ESP_ERR_NO_MEM && !matched) {
            ESP_LOGW(TAG,
                     "NodeLabel write out-of-memory slot=%u endpoint=%u; likely NVS full for nonvolatile NodeLabel",
                     static_cast<unsigned>(slot_id), static_cast<unsigned>(endpoint_id));
        }
        return err;
    }

    attr = esp_matter::attribute::get(endpoint_id, kClusterBasicInformation, kAttributeNodeLabel);
    if (attr) {
        char before_label[48] = { 0 };
        bool has_before = read_char_attribute(endpoint_id, kClusterBasicInformation, kAttributeNodeLabel, before_label,
                                              sizeof(before_label));
        if (has_before && strcmp(before_label, slot->display_name) == 0) {
            ESP_LOGI(TAG,
                     "NodeLabel sync slot=%u endpoint=%u cluster=0x%04lx name='%s' result=SKIP matched=1 readback='%s'",
                     static_cast<unsigned>(slot_id), static_cast<unsigned>(endpoint_id),
                     static_cast<unsigned long>(kClusterBasicInformation), slot->display_name, before_label);
            return ESP_OK;
        }

        esp_err_t err = write_node_label_with_retry(endpoint_id, kClusterBasicInformation, &label_val);
        char verify_label[48] = { 0 };
        bool matched = read_char_attribute(endpoint_id, kClusterBasicInformation, kAttributeNodeLabel, verify_label,
                                           sizeof(verify_label)) &&
                       (strcmp(verify_label, slot->display_name) == 0);

        ESP_LOGI(TAG, "NodeLabel sync slot=%u endpoint=%u cluster=0x%04lx name='%s' result=%s matched=%u readback='%s'",
                 static_cast<unsigned>(slot_id), static_cast<unsigned>(endpoint_id),
                 static_cast<unsigned long>(kClusterBasicInformation), slot->display_name, esp_err_to_name(err), matched ? 1U : 0U,
                 verify_label);
        if (err == ESP_ERR_NO_MEM && !matched) {
            ESP_LOGW(TAG,
                     "NodeLabel write out-of-memory slot=%u endpoint=%u; likely NVS full for nonvolatile NodeLabel",
                     static_cast<unsigned>(slot_id), static_cast<unsigned>(endpoint_id));
        }
        return err;
    }

    ESP_LOGW(TAG, "NodeLabel cluster missing for slot=%u endpoint=%u", static_cast<unsigned>(slot_id),
             static_cast<unsigned>(endpoint_id));
    return ESP_ERR_NOT_FOUND;
}

static void set_default_slot_display_name(bridge_slot_state_t *slot)
{
    if (!slot) {
        return;
    }
    snprintf(slot->display_name, sizeof(slot->display_name), "Slot %u", static_cast<unsigned>(slot->slot_id));
}

static void rebuild_slot_view()
{
    for (size_t i = 0; i < s_slot_count; ++i) {
        s_slots[i].assigned_device_id = s_registry.slot_assignments[i];
        set_default_slot_display_name(&s_slots[i]);
        s_slots[i].on_signal_id = 0;
        s_slots[i].off_signal_id = 0;
        s_slots[i].level_up_signal_id = 0;
        s_slots[i].level_down_signal_id = 0;

        int idx = find_device_index_by_id(s_slots[i].assigned_device_id);
        if (idx >= 0) {
            const bridge_device_t *d = &s_registry.devices[idx];
            strlcpy(s_slots[i].display_name, d->name, sizeof(s_slots[i].display_name));
            s_slots[i].on_signal_id = d->on_signal_id;
            s_slots[i].off_signal_id = d->off_signal_id;
            s_slots[i].level_up_signal_id = d->level_up_signal_id;
            s_slots[i].level_down_signal_id = d->level_down_signal_id;
        }

        esp_err_t sync_err = sync_slot_node_label(static_cast<uint8_t>(i));
        if (sync_err != ESP_OK && sync_err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to sync NodeLabel for slot %u: %s", static_cast<unsigned>(i), esp_err_to_name(sync_err));
        }
    }
}

static esp_err_t save_registry()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, kNvsKeyRegistry, &s_registry, sizeof(s_registry));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static bool load_legacy_slot_binding(uint8_t slot_id, bridge_device_t *out_device)
{
    if (!out_device) {
        return false;
    }

    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    char key_on[16];
    char key_off[16];
    char key_up[20];
    char key_down[22];
    snprintf(key_on, sizeof(key_on), "s%u_on", slot_id);
    snprintf(key_off, sizeof(key_off), "s%u_off", slot_id);
    snprintf(key_up, sizeof(key_up), "s%u_level_up", slot_id);
    snprintf(key_down, sizeof(key_down), "s%u_level_down", slot_id);

    uint32_t on = 0, off = 0, up = 0, down = 0;
    esp_err_t on_err = nvs_get_u32(handle, key_on, &on);
    esp_err_t off_err = nvs_get_u32(handle, key_off, &off);
    esp_err_t up_err = nvs_get_u32(handle, key_up, &up);
    esp_err_t down_err = nvs_get_u32(handle, key_down, &down);
    nvs_close(handle);

    bool has_any = (on_err == ESP_OK || off_err == ESP_OK || up_err == ESP_OK || down_err == ESP_OK);
    if (!has_any) {
        return false;
    }

    memset(out_device, 0, sizeof(*out_device));
    out_device->on_signal_id = on;
    out_device->off_signal_id = off;
    out_device->level_up_signal_id = up;
    out_device->level_down_signal_id = down;
    out_device->enabled = true;
    snprintf(out_device->name, sizeof(out_device->name), "Light %u", static_cast<unsigned>(slot_id + 1));
    strlcpy(out_device->device_type, "light", sizeof(out_device->device_type));
    return true;
}

static void migrate_legacy_if_needed()
{
    if (s_registry.device_count > 0) {
        return;
    }

    bool migrated = false;
    for (size_t slot = 0; slot < s_slot_count && s_registry.device_count < BRIDGE_MAX_REGISTERED_DEVICES; ++slot) {
        bridge_device_t d;
        if (!load_legacy_slot_binding(static_cast<uint8_t>(slot), &d)) {
            continue;
        }
        d.device_id = s_registry.next_device_id++;
        s_registry.devices[s_registry.device_count++] = d;
        s_registry.slot_assignments[slot] = d.device_id;
        migrated = true;
    }

    if (migrated) {
        ESP_LOGI(TAG, "Migrated legacy slot bindings to %u devices", static_cast<unsigned>(s_registry.device_count));
        save_registry();
    }
}

static void load_registry_or_default()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return;
    }

    size_t size = sizeof(s_registry);
    err = nvs_get_blob(handle, kNvsKeyRegistry, &s_registry, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(s_registry) || s_registry.version != kRegistryVersion ||
        s_registry.device_count > BRIDGE_MAX_REGISTERED_DEVICES) {
        memset(&s_registry, 0, sizeof(s_registry));
        s_registry.version = kRegistryVersion;
        s_registry.next_device_id = 1;
        s_registry.device_count = 0;
    }
}

esp_err_t bridge_action_init(const uint16_t *endpoint_ids, size_t count)
{
    if (!endpoint_ids || count == 0 || count > BRIDGE_SLOT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(s_slots, 0, sizeof(s_slots));
    memset(s_last_level, 0, sizeof(s_last_level));
    memset(s_last_level_valid, 0, sizeof(s_last_level_valid));
    s_slot_count = count;
    for (size_t i = 0; i < count; ++i) {
        s_slots[i].slot_id = static_cast<uint8_t>(i);
        s_slots[i].endpoint_id = endpoint_ids[i];
        set_default_slot_display_name(&s_slots[i]);
    }

    load_registry_or_default();
    migrate_legacy_if_needed();
    rebuild_slot_view();

    ESP_LOGI(TAG, "Initialized bridge actions: slots=%u devices=%u", static_cast<unsigned>(s_slot_count),
             static_cast<unsigned>(s_registry.device_count));
    return ESP_OK;
}

esp_err_t bridge_action_register_device(const char *name, const char *device_type, uint32_t *out_device_id)
{
    if (!name || !out_device_id) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_registry.device_count >= BRIDGE_MAX_REGISTERED_DEVICES) {
        return ESP_ERR_NO_MEM;
    }

    bridge_device_t *d = &s_registry.devices[s_registry.device_count];
    memset(d, 0, sizeof(*d));
    d->device_id = s_registry.next_device_id++;
    strlcpy(d->name, name, sizeof(d->name));
    strlcpy(d->device_type, (device_type && device_type[0]) ? device_type : "light", sizeof(d->device_type));
    d->enabled = true;
    s_registry.device_count++;

    esp_err_t err = save_registry();
    if (err != ESP_OK) {
        s_registry.device_count--;
        return err;
    }

    *out_device_id = d->device_id;
    return ESP_OK;
}

esp_err_t bridge_action_bind_device(uint32_t device_id, uint32_t on_signal_id, uint32_t off_signal_id, uint32_t level_up_signal_id,
                                    uint32_t level_down_signal_id)
{
    int idx = find_device_index_by_id(device_id);
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    bridge_device_t *d = &s_registry.devices[idx];
    d->on_signal_id = on_signal_id;
    d->off_signal_id = off_signal_id;
    d->level_up_signal_id = level_up_signal_id;
    d->level_down_signal_id = level_down_signal_id;

    esp_err_t err = save_registry();
    if (err != ESP_OK) {
        return err;
    }
    rebuild_slot_view();
    return ESP_OK;
}

esp_err_t bridge_action_unbind_signal_references(uint32_t signal_id)
{
    if (signal_id == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    bool changed = false;
    for (uint32_t i = 0; i < s_registry.device_count; ++i) {
        bridge_device_t *d = &s_registry.devices[i];
        if (!d->enabled) {
            continue;
        }
        if (d->on_signal_id == signal_id) {
            d->on_signal_id = 0;
            changed = true;
        }
        if (d->off_signal_id == signal_id) {
            d->off_signal_id = 0;
            changed = true;
        }
        if (d->level_up_signal_id == signal_id) {
            d->level_up_signal_id = 0;
            changed = true;
        }
        if (d->level_down_signal_id == signal_id) {
            d->level_down_signal_id = 0;
            changed = true;
        }
    }

    if (!changed) {
        return ESP_OK;
    }

    esp_err_t err = save_registry();
    if (err != ESP_OK) {
        return err;
    }

    rebuild_slot_view();
    ESP_LOGI(TAG, "Unbound deleted signal references signal_id=%" PRIu32, signal_id);
    return ESP_OK;
}

esp_err_t bridge_action_rename_device(uint32_t device_id, const char *name)
{
    if (!name || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    int idx = find_device_index_by_id(device_id);
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    bridge_device_t *d = &s_registry.devices[idx];
    strlcpy(d->name, name, sizeof(d->name));

    esp_err_t err = save_registry();
    if (err != ESP_OK) {
        return err;
    }

    rebuild_slot_view();
    return ESP_OK;
}

esp_err_t bridge_action_assign_slot(uint8_t slot_id, uint32_t device_id)
{
    if (slot_id >= s_slot_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (device_id != 0 && find_device_index_by_id(device_id) < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    s_registry.slot_assignments[slot_id] = device_id;
    esp_err_t err = save_registry();
    if (err != ESP_OK) {
        return err;
    }
    rebuild_slot_view();
    return ESP_OK;
}

esp_err_t bridge_action_sync_all_node_labels()
{
    esp_err_t last_err = ESP_OK;
    for (size_t i = 0; i < s_slot_count; ++i) {
        esp_err_t err = sync_slot_node_label(static_cast<uint8_t>(i));
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            last_err = err;
            ESP_LOGW(TAG, "NodeLabel sync failed for slot %u: %s", static_cast<unsigned>(i), esp_err_to_name(err));
        }
    }
    return last_err;
}

void bridge_action_log_slot_identity_dump()
{
    ESP_LOGI(TAG, "---- Slot identity dump start ----");
    for (size_t i = 0; i < s_slot_count; ++i) {
        const bridge_slot_state_t *slot = &s_slots[i];
        char node_label[48] = { 0 };
        char unique_id[48] = { 0 };

        bool has_node_label = read_char_attribute(slot->endpoint_id, kClusterBridgedDeviceBasicInformation, kAttributeNodeLabel,
                                                  node_label, sizeof(node_label));
        bool has_unique_id = read_char_attribute(slot->endpoint_id, kClusterBridgedDeviceBasicInformation, kAttributeUniqueID,
                                                 unique_id, sizeof(unique_id));

        ESP_LOGI(TAG,
                 "slot=%u endpoint=%u device_id=%lu display_name='%s' node_label='%s' unique_id='%s'",
                 static_cast<unsigned>(slot->slot_id), static_cast<unsigned>(slot->endpoint_id),
                 static_cast<unsigned long>(slot->assigned_device_id), slot->display_name,
                 has_node_label ? node_label : "(missing)", has_unique_id ? unique_id : "(missing)");
    }
    ESP_LOGI(TAG, "---- Slot identity dump end ----");
}

esp_err_t bridge_action_bind_slot(uint8_t slot_id, uint32_t on_signal_id, uint32_t off_signal_id, uint32_t level_up_signal_id,
                                  uint32_t level_down_signal_id)
{
    if (slot_id >= s_slot_count) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t device_id = s_registry.slot_assignments[slot_id];
    if (device_id == 0) {
        char name[40];
        snprintf(name, sizeof(name), "Light %u", static_cast<unsigned>(slot_id + 1));
        esp_err_t err = bridge_action_register_device(name, "light", &device_id);
        if (err != ESP_OK) {
            return err;
        }
        err = bridge_action_assign_slot(slot_id, device_id);
        if (err != ESP_OK) {
            return err;
        }
    }

    return bridge_action_bind_device(device_id, on_signal_id, off_signal_id, level_up_signal_id, level_down_signal_id);
}

esp_err_t bridge_action_execute(uint8_t slot_id, uint32_t cluster_id, uint32_t attribute_id, const esp_matter_attr_val_t *val)
{
    if (slot_id >= s_slot_count || !val) {
        return ESP_ERR_INVALID_ARG;
    }

    bridge_slot_state_t *slot = &s_slots[slot_id];
    if (slot->assigned_device_id == 0) {
        ESP_LOGW(TAG, "Slot %u has no assigned device", slot_id);
        return ESP_OK;
    }

    if (cluster_id == kClusterOnOff && attribute_id == kAttributeOnOff) {
        uint32_t signal_id = val->val.b ? slot->on_signal_id : slot->off_signal_id;
        ir_engine_send_signal(signal_id, slot_id, cluster_id, attribute_id, val);
        s_last_level[slot_id] = val->val.b ? 1 : 0;
        s_last_level_valid[slot_id] = true;
    } else if (cluster_id == kClusterLevelControl && attribute_id == kAttributeCurrentLevel) {
        const uint8_t new_level = val->val.u8;
        bool has_prev = s_last_level_valid[slot_id];
        uint8_t prev_level = has_prev ? s_last_level[slot_id] : new_level;

        if (new_level == 0) {
            ir_engine_send_signal(slot->off_signal_id, slot_id, cluster_id, attribute_id, val);
        } else if (!has_prev || prev_level == 0) {
            ir_engine_send_signal(slot->on_signal_id, slot_id, cluster_id, attribute_id, val);
        } else if (new_level > prev_level) {
            ir_engine_send_signal(slot->level_up_signal_id, slot_id, cluster_id, attribute_id, val);
        } else if (new_level < prev_level) {
            ir_engine_send_signal(slot->level_down_signal_id, slot_id, cluster_id, attribute_id, val);
        }

        s_last_level[slot_id] = new_level;
        s_last_level_valid[slot_id] = true;
    }

    return ESP_OK;
}

const bridge_device_t *bridge_action_get_devices(size_t *count)
{
    if (count) {
        *count = s_registry.device_count;
    }
    return s_registry.devices;
}

const bridge_slot_state_t *bridge_action_get_slots(size_t *count)
{
    if (count) {
        *count = s_slot_count;
    }
    return s_slots;
}

const char *bridge_action_get_slot_role(uint8_t slot_id)
{
    if (slot_id >= s_slot_count) {
        return "unknown";
    }
    uint32_t device_id = s_slots[slot_id].assigned_device_id;
    int idx = find_device_index_by_id(device_id);
    if (idx < 0) {
        return "unassigned";
    }
    return s_registry.devices[idx].device_type;
}
