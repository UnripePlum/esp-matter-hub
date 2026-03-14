#include "status_led.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <device.h>
#include <led_driver.h>

namespace {

static constexpr uint32_t kSlowBlinkMs = 500;
static constexpr uint32_t kFastBlinkMs = 200;
static constexpr uint32_t kLearnBlinkMs = 150;
static constexpr uint32_t kLedTaskStackSize = 6144;
static constexpr uint64_t kLearnSuccessDurationUs = 900000;
static constexpr uint64_t kLearnFailedDurationUs = 2000000;
static constexpr uint64_t kIrTxPulseDurationUs = 120000;

static constexpr uint16_t kHueRed = 0;
static constexpr uint16_t kHueGreen = 120;
static constexpr uint16_t kHueBlue = 240;
static constexpr uint16_t kHueYellow = 50;

static constexpr uint8_t kSatColor = 100;
static constexpr uint8_t kSatWhite = 0;
static constexpr uint8_t kBrightStrong = 10;

static const char *TAG = "status_led";

enum class visual_state_t : uint8_t {
    BOOTING,
    COMMISSIONING,
    READY,
    LEARNING_IN_PROGRESS,
    LEARNING_SUCCESS,
    LEARNING_FAILED,
    IR_TX_PULSE,
};

enum class learning_effect_t : uint8_t {
    NONE,
    IN_PROGRESS,
    SUCCESS,
    FAILED,
};

struct led_frame_t {
    bool on;
    uint16_t hue;
    uint8_t saturation;
    uint8_t brightness;
};

static bool s_initialized = false;
static bool s_system_ready = false;
static bool s_commissioning_open = false;
static learning_effect_t s_learning_effect = learning_effect_t::NONE;
static uint64_t s_learning_effect_until_us = 0;
static uint64_t s_ir_tx_pulse_until_us = 0;
static visual_state_t s_last_visual_state = visual_state_t::BOOTING;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_led_task = nullptr;
static led_driver_handle_t s_led_handle = nullptr;

static bool blink_phase_on(uint32_t period_ms, uint64_t now_us)
{
    if (period_ms == 0) {
        return true;
    }
    return (((now_us / 1000ULL) / period_ms) % 2ULL) == 0ULL;
}

static void apply_frame(const led_frame_t &frame)
{
    if (!s_led_handle) {
        return;
    }

    if (!frame.on || frame.brightness == 0) {
        (void)led_driver_set_power(s_led_handle, false);
    } else {
        (void)led_driver_set_power(s_led_handle, true);
        (void)led_driver_set_hue(s_led_handle, frame.hue);
        (void)led_driver_set_saturation(s_led_handle, frame.saturation);
        (void)led_driver_set_brightness(s_led_handle, frame.brightness);
    }

}

static led_frame_t frame_for_state(visual_state_t state, uint64_t now_us)
{
    switch (state) {
    case visual_state_t::BOOTING:
        return { blink_phase_on(kSlowBlinkMs, now_us), kHueBlue, kSatColor, kBrightStrong };
    case visual_state_t::COMMISSIONING:
        return { blink_phase_on(kFastBlinkMs, now_us), kHueBlue, kSatColor, kBrightStrong };
    case visual_state_t::READY:
        return { true, kHueGreen, kSatColor, kBrightStrong };
    case visual_state_t::LEARNING_IN_PROGRESS:
        return { blink_phase_on(kLearnBlinkMs, now_us), kHueYellow, kSatColor, kBrightStrong };
    case visual_state_t::LEARNING_SUCCESS:
        return { blink_phase_on(kLearnBlinkMs, now_us), kHueGreen, kSatColor, kBrightStrong };
    case visual_state_t::LEARNING_FAILED:
        return { blink_phase_on(kLearnBlinkMs, now_us), kHueRed, kSatColor, kBrightStrong };
    case visual_state_t::IR_TX_PULSE:
        return { true, 0, kSatWhite, kBrightStrong };
    default:
        return { false, 0, 0, 0 };
    }
}

static visual_state_t resolve_visual_state(uint64_t now_us)
{
    bool system_ready;
    bool commissioning_open;
    learning_effect_t effect;
    uint64_t effect_until;
    uint64_t ir_tx_until;

    taskENTER_CRITICAL(&s_lock);
    system_ready = s_system_ready;
    commissioning_open = s_commissioning_open;
    effect = s_learning_effect;
    effect_until = s_learning_effect_until_us;
    ir_tx_until = s_ir_tx_pulse_until_us;

    if ((effect == learning_effect_t::SUCCESS || effect == learning_effect_t::FAILED) && effect_until != 0 &&
        now_us >= effect_until) {
        s_learning_effect = learning_effect_t::NONE;
        s_learning_effect_until_us = 0;
        effect = learning_effect_t::NONE;
    }
    if (ir_tx_until != 0 && now_us >= ir_tx_until) {
        s_ir_tx_pulse_until_us = 0;
        ir_tx_until = 0;
    }
    taskEXIT_CRITICAL(&s_lock);

    if (effect == learning_effect_t::IN_PROGRESS) {
        return visual_state_t::LEARNING_IN_PROGRESS;
    }
    if (effect == learning_effect_t::SUCCESS) {
        return visual_state_t::LEARNING_SUCCESS;
    }
    if (effect == learning_effect_t::FAILED) {
        return visual_state_t::LEARNING_FAILED;
    }
    if (ir_tx_until != 0) {
        return visual_state_t::IR_TX_PULSE;
    }
    if (!system_ready) {
        return visual_state_t::BOOTING;
    }
    if (commissioning_open) {
        return visual_state_t::COMMISSIONING;
    }
    return visual_state_t::READY;
}

static void led_task(void *arg)
{
    (void)arg;

    while (true) {
        const uint64_t now_us = esp_timer_get_time();
        const visual_state_t state = resolve_visual_state(now_us);
        const led_frame_t frame = frame_for_state(state, now_us);
        apply_frame(frame);

        taskENTER_CRITICAL(&s_lock);
        s_last_visual_state = state;
        taskEXIT_CRITICAL(&s_lock);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

} // namespace

esp_err_t status_led_init()
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_log_level_set("led_driver_ws2812", ESP_LOG_WARN);

    led_driver_config_t config = led_driver_get_config();
    s_led_handle = led_driver_init(&config);
    if (!s_led_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)led_driver_set_power(s_led_handle, false);

    if (xTaskCreate(led_task, "status_led", kLedTaskStackSize, nullptr, 5, &s_led_task) != pdPASS) {
        s_led_handle = nullptr;
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Status LED ready (onboard LED gpio=%d channel=%d)", config.gpio, config.channel);
    return ESP_OK;
}

void status_led_set_system_ready(bool ready)
{
    taskENTER_CRITICAL(&s_lock);
    s_system_ready = ready;
    taskEXIT_CRITICAL(&s_lock);
}

void status_led_set_commissioning(bool open)
{
    taskENTER_CRITICAL(&s_lock);
    s_commissioning_open = open;
    taskEXIT_CRITICAL(&s_lock);
}

void status_led_set_learning(ir_learning_state_t state)
{
    const uint64_t now_us = esp_timer_get_time();

    taskENTER_CRITICAL(&s_lock);
    switch (state) {
    case IR_LEARNING_IN_PROGRESS:
        s_learning_effect = learning_effect_t::IN_PROGRESS;
        s_learning_effect_until_us = 0;
        break;
    case IR_LEARNING_READY:
        s_learning_effect = learning_effect_t::SUCCESS;
        s_learning_effect_until_us = now_us + kLearnSuccessDurationUs;
        break;
    case IR_LEARNING_FAILED:
        s_learning_effect = learning_effect_t::FAILED;
        s_learning_effect_until_us = now_us + kLearnFailedDurationUs;
        break;
    case IR_LEARNING_IDLE:
        if (s_learning_effect == learning_effect_t::IN_PROGRESS) {
            s_learning_effect = learning_effect_t::NONE;
            s_learning_effect_until_us = 0;
        }
        break;
    default:
        break;
    }
    taskEXIT_CRITICAL(&s_lock);
}

void status_led_notify_ir_tx()
{
    const uint64_t now_us = esp_timer_get_time();
    taskENTER_CRITICAL(&s_lock);
    s_ir_tx_pulse_until_us = now_us + kIrTxPulseDurationUs;
    taskEXIT_CRITICAL(&s_lock);
}

const char *status_led_get_state_str()
{
    visual_state_t state;
    taskENTER_CRITICAL(&s_lock);
    state = s_last_visual_state;
    taskEXIT_CRITICAL(&s_lock);

    switch (state) {
    case visual_state_t::BOOTING:
        return "booting";
    case visual_state_t::COMMISSIONING:
        return "commissioning";
    case visual_state_t::READY:
        return "ready";
    case visual_state_t::LEARNING_IN_PROGRESS:
        return "learning_in_progress";
    case visual_state_t::LEARNING_SUCCESS:
        return "learning_success";
    case visual_state_t::LEARNING_FAILED:
        return "learning_failed";
    case visual_state_t::IR_TX_PULSE:
        return "ir_tx";
    default:
        return "unknown";
    }
}
