#include "sticky_power.h"

#include <cstdio>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace {

constexpr char kTag[] = "sticky_power";
constexpr TickType_t kReleasePollInterval = pdMS_TO_TICKS(20);
constexpr TickType_t kReleaseDebounceTime = pdMS_TO_TICKS(60);

esp_err_t hold_output(gpio_num_t pin, int level)
{
    gpio_hold_dis(pin);
    ESP_RETURN_ON_ERROR(gpio_set_direction(pin, GPIO_MODE_OUTPUT),
                        kTag, "configure held output");
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, level),
                        kTag, "set held output");
    return gpio_hold_en(pin);
}

void wait_for_ai_button_release()
{
    ESP_LOGI(kTag, "Waiting for AI button release");
    while (gpio_get_level(static_cast<gpio_num_t>(PIN_POWER_BTN)) == 0) {
        vTaskDelay(kReleasePollInterval);
    }
    vTaskDelay(kReleaseDebounceTime);
}

}  // namespace

void sticky_power_log_wakeup_reason()
{
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(kTag, "Woke from deep sleep by AI button");
    }
}

void sticky_power_prepare_after_wakeup()
{
    // Release the wake pin's deep-sleep hold before the button driver starts.
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) {
        return;
    }

    const gpio_num_t wake_pin = static_cast<gpio_num_t>(PIN_POWER_BTN);
    // 唤醒引脚在 deep sleep 期间被 hold，先释放才能读到真实电平
    gpio_hold_dis(wake_pin);
    ESP_ERROR_CHECK(gpio_set_direction(wake_pin, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_pullup_en(wake_pin));
    ESP_ERROR_CHECK(gpio_pulldown_dis(wake_pin));

    ESP_LOGI(kTag, "AI button GPIO ready after deep-sleep wake");
}

[[noreturn]] void sticky_power_enter_deep_sleep()
{
    const gpio_num_t wake_pin = static_cast<gpio_num_t>(PIN_POWER_BTN);
    wait_for_ai_button_release();

    ESP_ERROR_CHECK(hold_output(
        static_cast<gpio_num_t>(PIN_POWER_HOLD), 1));
    ESP_ERROR_CHECK(hold_output(
        static_cast<gpio_num_t>(PIN_POWER_LOCK), 1));
    ESP_ERROR_CHECK(hold_output(static_cast<gpio_num_t>(PIN_EPD_EN), 0));
    ESP_ERROR_CHECK(hold_output(static_cast<gpio_num_t>(PIN_TOUCH_EN), 0));
    ESP_ERROR_CHECK(hold_output(static_cast<gpio_num_t>(PIN_TOUCH_RST), 0));
    ESP_ERROR_CHECK(hold_output(static_cast<gpio_num_t>(PIN_MIC_EN), 0));
    ESP_ERROR_CHECK(hold_output(static_cast<gpio_num_t>(PIN_SD_EN), 0));
    ESP_ERROR_CHECK(hold_output(static_cast<gpio_num_t>(PIN_BUZZER), 0));

    gpio_hold_dis(wake_pin);
    ESP_ERROR_CHECK(gpio_set_direction(wake_pin, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_pullup_en(wake_pin));
    ESP_ERROR_CHECK(gpio_pulldown_dis(wake_pin));
    ESP_ERROR_CHECK(gpio_hold_en(wake_pin));

    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(
        1ULL << PIN_POWER_BTN, ESP_EXT1_WAKEUP_ANY_LOW));
    gpio_deep_sleep_hold_en();

    ESP_LOGI(kTag, "Wake pin: GPIO%d, active low", PIN_POWER_BTN);
    ESP_LOGI(kTag, "Entering deep sleep now");
    std::fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_deep_sleep_start();
}
