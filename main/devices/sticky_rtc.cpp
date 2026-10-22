#include "sticky_rtc.h"

#include <cstdint>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"
#include "sensor_i2c_lock.h"

namespace {

constexpr char kTag[] = "sticky_rtc";
// 传感器总线（BQ27220/RTC/SHT40/IMU 共 4 个器件）只有 ESP32 内部上拉，
// 400 kHz 时序裕量不足，首条事务容易超时；降到 100 kHz 更稳。
constexpr uint32_t kI2cClockHz = 100000;
constexpr int kI2cTimeoutMs = 100;
constexpr TickType_t kRetryDelayMs = pdMS_TO_TICKS(20);
constexpr uint8_t kTimeRegister = 0x02;
constexpr uint8_t kControl1Register = 0x00;
constexpr uint8_t kControl2Register = 0x01;
constexpr uint8_t kClockOutputRegister = 0x0D;
constexpr uint8_t kStopFlag = 0x20;
constexpr uint8_t kLowVoltageFlag = 0x80;

i2c_master_bus_handle_t s_bus = nullptr;
i2c_master_dev_handle_t s_device = nullptr;

bool valid_bcd(uint8_t value)
{
    return (value & 0x0FU) <= 9U && ((value >> 4) & 0x0FU) <= 9U;
}

int bcd_to_decimal(uint8_t value)
{
    return static_cast<int>((value >> 4) * 10U + (value & 0x0FU));
}

uint8_t decimal_to_bcd(int value)
{
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

bool valid_date_time(const RtcDateTime &value)
{
    if (value.year < 1900 || value.year > 2099 ||
        value.month < 1 || value.month > 12 ||
        value.day < 1 || value.hour < 0 || value.hour > 23 ||
        value.minute < 0 || value.minute > 59 ||
        value.second < 0 || value.second > 59) {
        return false;
    }

    constexpr int days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    int maximum_day = days_per_month[value.month - 1];
    const bool leap_year =
        (value.year % 4 == 0 && value.year % 100 != 0) || value.year % 400 == 0;
    if (value.month == 2 && leap_year) {
        maximum_day = 29;
    }
    return value.day <= maximum_day;
}

// Gregorian calendar weekday, with Sunday = 0 (PCF8563 convention).
int weekday_for_date(int year, int month, int day)
{
    constexpr int kMonthOffsets[] = {0, 3, 2, 5, 0, 3,
                                      5, 1, 4, 6, 2, 4};
    if (month < 1 || month > 12) {
        return 0;
    }
    year -= month < 3 ? 1 : 0;
    return (year + year / 4 - year / 100 + year / 400 +
            kMonthOffsets[month - 1] + day) % 7;
}

void log_sensor_bus_levels(const char *when)
{
    ESP_LOGW(kTag, "%s: SDA(GPIO%d)=%d SCL(GPIO%d)=%d", when,
             PIN_SENSOR_SDA, gpio_get_level(static_cast<gpio_num_t>(PIN_SENSOR_SDA)),
             PIN_SENSOR_SCL, gpio_get_level(static_cast<gpio_num_t>(PIN_SENSOR_SCL)));
}

}  // namespace

esp_err_t sticky_rtc_init(i2c_master_bus_handle_t bus)
{
    if (bus == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_device != nullptr) {
        return ESP_OK;
    }
    s_bus = bus;

    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = PCF8563_I2C_ADDR;
    config.scl_speed_hz = kI2cClockHz;

    const esp_err_t result = i2c_master_bus_add_device(bus, &config, &s_device);
    if (result == ESP_OK) {
        ESP_LOGI(kTag, "Initialized: I2C address=0x%02X", PCF8563_I2C_ADDR);
        SensorI2cLock bus_lock;
        if (bus_lock.result() != ESP_OK) {
            ESP_LOGW(kTag, "RTC probe skipped: sensor I2C bus is locked");
            return ESP_OK;
        }

        ESP_LOGI(kTag, "Probing PCF8563 on sensor I2C bus at 0x%02X",
                 PCF8563_I2C_ADDR);
        const esp_err_t probe_result =
            i2c_master_probe(bus, PCF8563_I2C_ADDR, kI2cTimeoutMs);
        if (probe_result != ESP_OK) {
            ESP_LOGW(kTag, "PCF8563 probe failed at 0x%02X: %s",
                     PCF8563_I2C_ADDR, esp_err_to_name(probe_result));
            log_sensor_bus_levels("RTC probe failure");
            return ESP_OK;
        }
        ESP_LOGI(kTag, "PCF8563 acknowledged at 0x%02X", PCF8563_I2C_ADDR);

        // Ensure a previous interrupted setup has not left the oscillator
        // stopped, clear pending alarm/timer flags, and disable CLKOUT.
        uint8_t control1 = 0;
        esp_err_t init_result = i2c_master_transmit_receive(
            s_device, &kControl1Register, 1, &control1, 1, kI2cTimeoutMs);
        if (init_result == ESP_OK && (control1 & kStopFlag) != 0U) {
            const uint8_t start_clock[] = {
                kControl1Register,
                static_cast<uint8_t>(control1 & ~kStopFlag),
            };
            init_result = i2c_master_transmit(
                s_device, start_clock, sizeof(start_clock), kI2cTimeoutMs);
        }
        const uint8_t clear_control2[] = {kControl2Register, 0x00};
        if (init_result == ESP_OK) {
            init_result = i2c_master_transmit(
                s_device, clear_control2, sizeof(clear_control2), kI2cTimeoutMs);
        }
        const uint8_t disable_clock_output[] = {kClockOutputRegister, 0x00};
        if (init_result == ESP_OK) {
            init_result = i2c_master_transmit(s_device, disable_clock_output,
                                              sizeof(disable_clock_output),
                                              kI2cTimeoutMs);
        }
        if (init_result != ESP_OK) {
            ESP_LOGW(kTag, "PCF8563 initialization failed: %s",
                     esp_err_to_name(init_result));
            log_sensor_bus_levels("RTC init failure");
        } else {
            ESP_LOGI(kTag, "PCF8563 initialized: oscillator running, alarms cleared, CLKOUT off");
        }
    }
    return result;
}

esp_err_t sticky_rtc_read(RtcDateTime &date_time)
{
    if (s_device == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    SensorI2cLock bus_lock;
    if (bus_lock.result() != ESP_OK) {
        return bus_lock.result();
    }

    uint8_t raw[7] = {};
    ESP_LOGD(kTag, "Reading PCF8563 registers 0x%02X-0x%02X",
             kTimeRegister, static_cast<uint8_t>(kTimeRegister + sizeof(raw) - 1));
    esp_err_t result = i2c_master_transmit_receive(
        s_device, &kTimeRegister, 1, raw, sizeof(raw), kI2cTimeoutMs);
    if (result != ESP_OK) {
        // 总线可能被前一次失败事务卡住：复位总线后重试一次，
        // 与 sticky_sht40 的恢复策略保持一致。
        ESP_LOGW(kTag, "Read failed: %s; resetting sensor I2C bus and retrying",
                 esp_err_to_name(result));
        log_sensor_bus_levels("RTC read failure before reset");
        if (s_bus != nullptr) {
            const esp_err_t reset_result = i2c_master_bus_reset(s_bus);
            if (reset_result != ESP_OK) {
                ESP_LOGE(kTag, "Sensor I2C bus reset failed: %s",
                         esp_err_to_name(reset_result));
            }
        }
        vTaskDelay(kRetryDelayMs);
        log_sensor_bus_levels("RTC read retry");
        result = i2c_master_transmit_receive(
            s_device, &kTimeRegister, 1, raw, sizeof(raw), kI2cTimeoutMs);
    }
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Read failed: %s", esp_err_to_name(result));
        return result;
    }

    // PCF8563 sets VL when the clock integrity is not guaranteed.
    if ((raw[0] & kLowVoltageFlag) != 0U) {
        ESP_LOGW(kTag, "RTC time invalid: low-voltage flag is set");
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t second = raw[0] & 0x7FU;
    const uint8_t minute = raw[1] & 0x7FU;
    const uint8_t hour = raw[2] & 0x3FU;
    const uint8_t day = raw[3] & 0x3FU;
    const uint8_t month = raw[5] & 0x1FU;
    const uint8_t year = raw[6];
    if (!valid_bcd(second) || !valid_bcd(minute) || !valid_bcd(hour) ||
        !valid_bcd(day) || !valid_bcd(month) || !valid_bcd(year)) {
        ESP_LOGW(kTag, "RTC time invalid: malformed BCD data");
        return ESP_ERR_INVALID_RESPONSE;
    }

    RtcDateTime value = {};
    value.second = bcd_to_decimal(second);
    value.minute = bcd_to_decimal(minute);
    value.hour = bcd_to_decimal(hour);
    value.day = bcd_to_decimal(day);
    value.month = bcd_to_decimal(month);
    const int short_year = bcd_to_decimal(year);
    value.year = (raw[5] & 0x80U) != 0U ? 1900 + short_year
                                        : 2000 + short_year;
    // Recompute from the calendar date. This repairs stale or incorrectly
    // initialized weekday registers left by older firmware.
    value.weekday = weekday_for_date(value.year, value.month, value.day);

    if (!valid_date_time(value)) {
        ESP_LOGW(kTag, "RTC time invalid: value is out of range");
        return ESP_ERR_INVALID_RESPONSE;
    }

    date_time = value;
    ESP_LOGD(kTag, "Read success: %04d-%02d-%02d %02d:%02d:%02d",
             value.year, value.month, value.day,
             value.hour, value.minute, value.second);
    return ESP_OK;
}

esp_err_t sticky_rtc_write(const RtcDateTime &date_time)
{
    if (s_device == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    SensorI2cLock bus_lock;
    if (bus_lock.result() != ESP_OK) {
        return bus_lock.result();
    }
    if (!valid_date_time(date_time)) {
        ESP_LOGW(kTag, "Write rejected: value is out of range");
        return ESP_ERR_INVALID_ARG;
    }

    // Derive weekday from the date instead of trusting caller state. This
    // prevents a default-initialized weekday from writing Sunday every time.
    const int weekday = weekday_for_date(date_time.year, date_time.month,
                                         date_time.day);

    // PCF8563 寄存器布局：0x02秒 0x03分 0x04时 0x05日 0x06星期 0x07月 0x08年
    // 秒寄存器 bit7=0 同时清除 VL 低电压标志
    const uint8_t payload[8] = {
        kTimeRegister,
        decimal_to_bcd(date_time.second),
        decimal_to_bcd(date_time.minute),
        decimal_to_bcd(date_time.hour),
        decimal_to_bcd(date_time.day),
        static_cast<uint8_t>(weekday & 0x07),
        static_cast<uint8_t>(decimal_to_bcd(date_time.month) |
                             (date_time.year < 2000 ? 0x80U : 0x00U)),
        decimal_to_bcd(date_time.year % 100),
    };

    ESP_LOGI(kTag, "Writing PCF8563 time %04d-%02d-%02d %02d:%02d:%02d (weekday=%d)",
             date_time.year, date_time.month, date_time.day, date_time.hour,
             date_time.minute, date_time.second, weekday);
    esp_err_t result =
        i2c_master_transmit(s_device, payload, sizeof(payload), kI2cTimeoutMs);
    if (result != ESP_OK) {
        // 与读路径一致：先复位总线再重试一次。
        ESP_LOGW(kTag, "Write failed: %s; resetting sensor I2C bus and retrying",
                 esp_err_to_name(result));
        log_sensor_bus_levels("RTC write failure before reset");
        if (s_bus != nullptr) {
            const esp_err_t reset_result = i2c_master_bus_reset(s_bus);
            if (reset_result != ESP_OK) {
                ESP_LOGE(kTag, "Sensor I2C bus reset failed: %s",
                         esp_err_to_name(reset_result));
            }
        }
        vTaskDelay(kRetryDelayMs);
        log_sensor_bus_levels("RTC write retry");
        result = i2c_master_transmit(s_device, payload, sizeof(payload),
                                     kI2cTimeoutMs);
    }
    if (result == ESP_OK) {
        ESP_LOGI(kTag, "Write success: %04d-%02d-%02d %02d:%02d:%02d",
                 date_time.year, date_time.month, date_time.day,
                 date_time.hour, date_time.minute, date_time.second);
    } else {
        ESP_LOGE(kTag, "Write failed: %s", esp_err_to_name(result));
    }
    return result;
}
