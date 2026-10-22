#include "sticky_charger.h"

#include "driver/gpio.h"
#include "pin_config.h"

esp_err_t sticky_charger_init()
{
    gpio_config_t enable_config = {};
    enable_config.pin_bit_mask = 1ULL << PIN_BAT_CHG_EN;
    enable_config.mode = GPIO_MODE_OUTPUT;
    enable_config.pull_up_en = GPIO_PULLUP_DISABLE;
    enable_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    enable_config.intr_type = GPIO_INTR_DISABLE;

    esp_err_t result = gpio_config(&enable_config);
    if (result != ESP_OK) {
        return result;
    }

    // EN_BAT_CHGn is active low.
    result = gpio_set_level(static_cast<gpio_num_t>(PIN_BAT_CHG_EN), 0);
    if (result != ESP_OK) {
        return result;
    }

    gpio_config_t detect_config = {};
    detect_config.pin_bit_mask = 1ULL << PIN_EXTERNAL_POWER;
    detect_config.mode = GPIO_MODE_INPUT;
    detect_config.pull_up_en = GPIO_PULLUP_DISABLE;
    detect_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    detect_config.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&detect_config);
}

esp_err_t sticky_charger_read(bool &charging)
{
    const int level = gpio_get_level(static_cast<gpio_num_t>(PIN_EXTERNAL_POWER));
    charging = level != 0;
    return ESP_OK;
}
