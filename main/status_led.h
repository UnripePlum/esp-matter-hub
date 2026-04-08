#pragma once

#include <esp_err.h>

#include "ir_engine.h"

esp_err_t status_led_init();
void status_led_set_system_ready(bool ready);
void status_led_set_commissioning(bool open);
void status_led_set_learning(ir_learning_state_t state);
void status_led_notify_ir_tx();
void status_led_set_ota(bool active);
const char *status_led_get_state_str();
