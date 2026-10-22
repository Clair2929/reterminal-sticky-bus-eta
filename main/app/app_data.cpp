#include "app_data.h"

#include "app_state.h"
#include "bus_eta.h"
#include "bus_config.h"
#include "esp_log.h"
#include "sticky_battery.h"
#include "sticky_charger.h"
#include "sticky_rtc.h"
#include "sticky_sht40.h"

#include <ctime>

namespace {

constexpr char kTag[] = "app_data";

}  // namespace

void update_date_time(AppState &state)
{
    RtcDateTime reading = {};
    const esp_err_t result = sticky_rtc_read(reading);

    state.date_time.error = result;
    state.date_time.rtc_valid = result == ESP_OK;
    state.date_time.valid = result == ESP_OK;
    if (result == ESP_OK) {
        state.date_time.year = reading.year;
        state.date_time.month = reading.month;
        state.date_time.day = reading.day;
        state.date_time.weekday = reading.weekday;
        state.date_time.hour = reading.hour;
        state.date_time.minute = reading.minute;
        state.date_time.second = reading.second;
    } else {
        if (bus_eta_system_time_valid()) {
            time_t now = 0;
            time(&now);
            now += BUS_UTC_OFFSET_SECONDS;
            struct tm local = {};
            gmtime_r(&now, &local);
            state.date_time.year = local.tm_year + 1900;
            state.date_time.month = local.tm_mon + 1;
            state.date_time.day = local.tm_mday;
            state.date_time.weekday = local.tm_wday;
            state.date_time.hour = local.tm_hour;
            state.date_time.minute = local.tm_min;
            state.date_time.second = local.tm_sec;
            state.date_time.valid = true;
            ESP_LOGW(kTag, "RTC read failed: %s; showing synchronized NTP time",
                     esp_err_to_name(result));
        } else {
            ESP_LOGW(kTag, "RTC update failed: %s; no synchronized time available",
                     esp_err_to_name(result));
        }
    }
}

void update_environment(AppState &state)
{
    ESP_LOGD(kTag, "Updating environment data");
    Sht40Reading reading = {};
    const esp_err_t result = sticky_sht40_read(reading);

    state.environment.error = result;
    state.environment.valid = result == ESP_OK;
    if (result == ESP_OK) {
        state.environment.temperature_c = reading.temperature_c;
        state.environment.humidity_percent = reading.humidity_percent;
        ESP_LOGD(kTag, "State updated: temperature=%.2f C, humidity=%.2f %%",
                 static_cast<double>(state.environment.temperature_c),
                 static_cast<double>(state.environment.humidity_percent));
    } else {
        ESP_LOGE(kTag, "Environment update failed: %s; page will show N/A",
                 esp_err_to_name(result));
    }
}

void update_battery(AppState &state)
{
    BatteryReading reading = {};
    const esp_err_t battery_result = sticky_battery_read(reading);
    state.battery.error = battery_result;
    state.battery.valid = battery_result == ESP_OK;
    if (battery_result == ESP_OK) {
        state.battery.percent = reading.percent;
    }

    bool charging = false;
    const esp_err_t charger_result = sticky_charger_read(charging);
    state.battery.charging_error = charger_result;
    state.battery.charging_valid = charger_result == ESP_OK;
    if (charger_result == ESP_OK) {
        state.battery.charging = charging;
    }
}
