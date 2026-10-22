#include "sticky_buttons.h"

#include <cstdint>

#include "app_event.h"
#include "app.h"
#include "button_gpio.h"
#include "iot_button.h"
#include "pin_config.h"

namespace {

constexpr uint16_t kLongPressTimeMs = 2000;
constexpr uint16_t kShortPressTimeMs = 180;
button_handle_t s_up_button = nullptr;
button_handle_t s_down_button = nullptr;
button_handle_t s_refresh_button = nullptr;

void button_click_callback(void *button_handle, void *user_data)
{
    (void)button_handle;
    app_idle_activity_reset();
    const auto event = static_cast<AppEvent>(
        reinterpret_cast<uintptr_t>(user_data));
    app_event_post(event);
}

esp_err_t create_button(int gpio,
                        AppEvent event,
                        button_handle_t *button,
                        bool register_long_press = false,
                        AppEvent long_press_event = AppEvent::EnterDeepSleep)
{
    button_config_t button_config = {};
    button_config.long_press_time = kLongPressTimeMs;
    button_config.short_press_time = kShortPressTimeMs;

    button_gpio_config_t gpio_config = {};
    gpio_config.gpio_num = gpio;
    gpio_config.active_level = 0;
    gpio_config.enable_power_save = false;
    gpio_config.disable_pull = false;

    esp_err_t result =
        iot_button_new_gpio_device(&button_config, &gpio_config, button);
    if (result != ESP_OK) {
        return result;
    }

    result = iot_button_register_cb(
        *button,
        BUTTON_SINGLE_CLICK,
        nullptr,
        button_click_callback,
        reinterpret_cast<void *>(static_cast<uintptr_t>(event)));
    if (result != ESP_OK || !register_long_press) {
        return result;
    }

    return iot_button_register_cb(
        *button,
        BUTTON_LONG_PRESS_START,
        nullptr,
        button_click_callback,
        reinterpret_cast<void *>(
            static_cast<uintptr_t>(long_press_event)));
}

}  // namespace

esp_err_t sticky_buttons_init()
{
    if (s_up_button != nullptr) {
        return ESP_OK;
    }

    esp_err_t result = create_button(
        PIN_BTN_UP, AppEvent::PreviousPage, &s_up_button);
    if (result != ESP_OK) {
        return result;
    }
    result = create_button(
        PIN_BTN_DOWN, AppEvent::NextPage, &s_down_button);
    if (result != ESP_OK) {
        return result;
    }
    return create_button(PIN_BTN_OK,
                         AppEvent::RefreshPage,
                         &s_refresh_button,
                         true,
                         AppEvent::EnterDeepSleep);
}
