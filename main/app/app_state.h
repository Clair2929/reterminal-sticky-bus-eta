#pragma once

#include "esp_err.h"

enum class PageId {
    Bus,
    BusConfig,
    WifiConfig,
};

struct DateTimeState {
    int year = 0;
    int month = 0;
    int day = 0;
    int weekday = 0;  // 0=Sunday ... 6=Saturday
    int hour = 0;
    int minute = 0;
    int second = 0;
    bool valid = false;
    // True only when the timestamp was read from the hardware RTC. NTP time
    // can still be displayed when the RTC is unavailable, but must not drive
    // the ETA countdown until the RTC is working.
    bool rtc_valid = false;
    esp_err_t error = ESP_ERR_INVALID_STATE;
};

struct EnvironmentState {
    float temperature_c = 0.0F;
    float humidity_percent = 0.0F;
    bool valid = false;
    esp_err_t error = ESP_ERR_INVALID_STATE;
};

struct BatteryState {
    int percent = 0;
    bool valid = false;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    bool charging = false;
    bool charging_valid = false;
    esp_err_t charging_error = ESP_ERR_INVALID_STATE;
};

// 公交到站数据（移植自 HelloWorld.ino）
enum class BusProvider {
    Singapore,
    HongKongKmb,
};

struct BusArrival {
    char route[16] = {};      // 线路号，如 "15"、"1A"
    char arrival[8] = {};     // 预计到站时刻 "HH:MM"
    int eta_minutes = 0;      // 距离到站分钟数
};

struct BusEtaState {
    char stop_name[64] = {};  // 站点显示名
    char status[32] = {};     // 状态提示：Connecting/No WiFi/HTTP error/OK
    BusArrival buses[10] = {};
    size_t bus_count = 0;
    bool wifi_connected = false;
    bool valid = false;
    esp_err_t error = ESP_ERR_INVALID_STATE;
};

struct AppState {
    PageId current_page = PageId::Bus;  // 开机默认进入公交看板页
    DateTimeState date_time;
    EnvironmentState environment;
    BatteryState battery;
    BusEtaState bus_eta;
};
