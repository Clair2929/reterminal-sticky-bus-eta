#pragma once

// 用户配置：优先从 NVS namespace "config" 读取（Web Flasher / 浏览器烧录时
// 由 Seeed 烧录页写入，键名与 Sticky 官方 HelloWorld 固件保持一致），
// 读不到或为空时回退到 bus_config.h 的编译期默认值。
//
// NVS 键（与 HelloWorld.ino / upstream-integration 的 nvs.js 对齐）：
//   wifiSsid         WiFi 名称
//   wifiPassword     WiFi 密码
//   busProvider      "singapore" 或 "hongkongkmb"（KMB 九巴/龙运）
//   busId            公交站点 ID（新加坡 BusStopCode / 香港 KMB 站柱编码）
//   busName          站点显示名称
//   refreshSeconds   自动刷新间隔（秒），i32，范围 30~3600

struct UserConfig {
    char wifi_ssid[33] = {};
    char wifi_password[65] = {};
    bool use_singapore = true;   // true=新加坡, false=香港 KMB
    char bus_id[24] = {};        // 新加坡 5 位数字 / KMB 16 位 hex，留足余量
    char bus_name[64] = {};
    int refresh_seconds = 120;   // 自动刷新间隔（秒）
};

// 获取全局用户配置（首次调用时从 NVS 加载，之后复用）
const UserConfig &get_user_config();
