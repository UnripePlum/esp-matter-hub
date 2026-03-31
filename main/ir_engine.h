#pragma once

#include <stddef.h>
#include <stdint.h>

#include <esp_err.h>
#include <esp_matter.h>

typedef enum ir_learning_state {
    IR_LEARNING_IDLE = 0,
    IR_LEARNING_IN_PROGRESS,
    IR_LEARNING_READY,
    IR_LEARNING_FAILED,
} ir_learning_state_t;

typedef struct ir_learning_status {
    ir_learning_state_t state;
    uint32_t elapsed_ms;
    uint32_t timeout_ms;
    uint32_t last_signal_id;
    uint8_t rx_source;
    uint16_t captured_len;
    uint16_t quality_score;
} ir_learning_status_t;

esp_err_t ir_engine_init();
esp_err_t ir_engine_send_signal(uint32_t signal_id, uint8_t slot_id, uint32_t cluster_id, uint32_t attribute_id,
                                const esp_matter_attr_val_t *val);
esp_err_t ir_engine_send_raw(uint32_t signal_id, uint32_t carrier_hz, uint8_t repeat, const uint16_t *ticks,
                              size_t tick_count);
esp_err_t ir_engine_persist_binding(uint8_t slot_id, const char *binding_suffix, uint32_t signal_id,
                                    uint32_t carrier_hz, uint8_t repeat, const uint16_t *ticks, size_t tick_count);
int ir_engine_read_all_nvs_signals(char *out_json, size_t out_size);

esp_err_t ir_engine_start_learning(uint32_t timeout_ms);
void ir_engine_get_learning_status(ir_learning_status_t *status);
const uint16_t *ir_engine_get_learned_ticks(uint8_t *out_len, uint32_t *out_carrier);
