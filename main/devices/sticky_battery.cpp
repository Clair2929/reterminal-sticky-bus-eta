#include "sticky_battery.h"

#include <algorithm>
#include <cstdint>

#include "bq27220.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"
#include "sensor_i2c_lock.h"

namespace {

constexpr char kTag[] = "sticky_battery";
// 传感器总线（BQ27220/RTC/SHT40/IMU 共 4 个器件）只有 ESP32 内部上拉，
// 400 kHz 时序裕量不足，首条事务容易超时；降到 100 kHz 各器件均满足要求。
constexpr uint32_t kI2cClockHz = 100000;
constexpr int kI2cTimeoutMs = 100;
// BQ27220 上电后需要时间完成内部初始化（参考 TRM），过早探测会超时。
constexpr TickType_t kPowerOnSettleMs = pdMS_TO_TICKS(300);

i2c_master_bus_handle_t s_bus = nullptr;
i2c_master_dev_handle_t s_device = nullptr;

void log_sensor_bus_levels(const char *when)
{
    ESP_LOGD(kTag, "%s: SDA(GPIO%d)=%d SCL(GPIO%d)=%d", when,
             PIN_SENSOR_SDA,
             gpio_get_level(static_cast<gpio_num_t>(PIN_SENSOR_SDA)),
             PIN_SENSOR_SCL,
             gpio_get_level(static_cast<gpio_num_t>(PIN_SENSOR_SCL)));
}

}  // namespace

esp_err_t sticky_battery_init(i2c_master_bus_handle_t bus)
{
    if (bus == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_device != nullptr) {
        return ESP_OK;
    }
    s_bus = bus;

    // 上电先等 BQ27220 稳定，再开始探测。
    vTaskDelay(kPowerOnSettleMs);

    SensorI2cLock bus_lock;
    if (bus_lock.result() != ESP_OK) {
        return bus_lock.result();
    }

    // Probe the address before issuing the BQ27220 control subcommand. If the
    // optional gauge is absent, a plain address probe returns NOT_FOUND and
    // avoids leaving the shared sensor bus in a failed device transaction.
    log_sensor_bus_levels("Before BQ27220 probe");
    const esp_err_t probe_result =
        i2c_master_probe(bus, BQ27220_I2C_ADDR, kI2cTimeoutMs);
    if (probe_result != ESP_OK) {
        log_sensor_bus_levels("After BQ27220 address probe");
        ESP_LOGW(kTag, "BQ27220 address probe failed at 0x%02X: %s",
                 BQ27220_I2C_ADDR, esp_err_to_name(probe_result));
        return probe_result;
    }

    ESP_LOGI(kTag, "BQ27220 acknowledged at 0x%02X", BQ27220_I2C_ADDR);
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = BQ27220_I2C_ADDR;
    config.scl_speed_hz = kI2cClockHz;

    const esp_err_t result = i2c_master_bus_add_device(bus, &config, &s_device);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Add I2C device failed: %s", esp_err_to_name(result));
        return result;
    }

    if (!bq27220_probe(s_device)) {
        log_sensor_bus_levels("After BQ27220 probe failure");
        ESP_LOGE(kTag, "BQ27220 not detected at address 0x%02X",
                 BQ27220_I2C_ADDR);
        // The fuel gauge is optional. Avoid repeatedly resetting the shared
        // sensor bus when it is absent; RTC/SHT40 must remain usable.
        i2c_master_bus_rm_device(s_device);
        s_device = nullptr;
        return ESP_ERR_NOT_FOUND;
    }

    log_sensor_bus_levels("After BQ27220 probe success");
    ESP_LOGI(kTag, "Initialized: I2C address=0x%02X", BQ27220_I2C_ADDR);
    return ESP_OK;
}

esp_err_t sticky_battery_read(BatteryReading &reading)
{
    if (s_device == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    SensorI2cLock bus_lock;
    if (bus_lock.result() != ESP_OK) {
        return bus_lock.result();
    }

    uint16_t percent = 0;
    if (!bq27220_read_state_of_charge(s_device, percent)) {
        ESP_LOGE(kTag, "Failed to read state of charge");
        return ESP_FAIL;
    }

    reading.percent = std::clamp<int>(percent, 0, 100);
    ESP_LOGD(kTag, "Read success: %d%%", reading.percent);
    return ESP_OK;
}
