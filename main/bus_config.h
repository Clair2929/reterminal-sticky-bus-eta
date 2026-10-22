#pragma once

// -----------------------------------------------------------------------------
// Bus ETA 用户配置区 —— 移植自 HelloWorld.ino 顶部的 LOCAL_* 配置
//
// 配置优先级：
//   1) NVS namespace "config"（优先）：由 Playground 网页烧录器在烧录前
//      写入，键名 wifiSsid / wifiPassword / busProvider / busId / busName /
//      refreshSeconds，与 Sticky 官方 HelloWorld 固件完全一致。
//   2) 本文件宏（兜底）：NVS 中没有配置或为空时使用。
//
// 注意：提交到 Playground 仓库时不得包含真实 WiFi 密码（No secrets 规则）。
// 本地开发时把下面的占位符改成你自己的值，或直接用网页烧录器写入 NVS。
// -----------------------------------------------------------------------------

// WiFi 热点名称和密码（占位符；本地开发请改成自己的值，或用配网页写入 NVS。
// 注意：推到 GitHub / Playground 仓库前必须保持占位，禁止提交真实凭据。）
#define BUS_WIFI_SSID      "YOUR_WIFI_SSID"
#define BUS_WIFI_PASSWORD  ""

// 公交数据源：
//   1 = 新加坡 busrouter.sg（接口 https://arrivelah2.busrouter.sg/?id=<站点ID>）
//   0 = 香港 KMB 九巴/龙运（接口 https://data.etabus.gov.hk/v1/transport/kmb/stop-eta/<站点ID>）
#define BUS_USE_SINGAPORE  1

// 公交站点 ID
// 新加坡：DataMall 的 BusStopCode，例如 "10011"
// 香港：KMB 站柱编码，例如 "TS000001"
#define BUS_STOP_ID        "10011"

// 站点显示名称（左上角大字显示）
#define BUS_STOP_NAME      "Bef Neil Rd"

// 自动刷新间隔（秒），范围 30 ~ 3600。
// 这是编译期兜底值；运行时可用 NVS 键 refreshSeconds（i32）覆盖，
// 与 HelloWorld.ino 的 Preferences.getInt("refreshSeconds", 120) 对齐。
#define BUS_REFRESH_SECONDS 120

// NTP 时间服务器
#define BUS_NTP_SERVER     "pool.ntp.org"

// 公交站所在时区相对 UTC 的偏移（秒）。
// 新加坡 SGT / 香港 HKT 都是 UTC+8（无夏令时），即 8 * 3600。
// NTP 同步的是 UTC 时间，设备 RTC 会先加这个偏移再写入，
// 让屏幕上显示的时间和公交站本地时间一致。
#define BUS_UTC_OFFSET_SECONDS  (8 * 3600)
