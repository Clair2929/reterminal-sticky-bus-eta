#pragma once

#include <cstdint>

#include "page.h"

// WiFi configuration page: live AP scan list + SSID/password input rows +
// a virtual keyboard. Tap interactions use the GT911 touch callback.
//
// The saved values are written to NVS namespace "config" with the same keys
// (wifiSsid / wifiPassword) used by the Playground web flasher, so the device
// picks them up on next boot via user_config.
void wifi_config_page_start();
void wifi_config_page_render(Canvas &canvas, const AppState &state);
void wifi_config_page_handle_tap(uint16_t x, uint16_t y);
// 请求立即扫描一次（进入页面时调用，不用等 20 秒周期）
void wifi_config_page_request_scan();
void wifi_config_page_handle_scan_done();
// 更新当前已连接 WiFi 的 SSID（bus_eta 的 WiFi 事件回调调用；ssid 为空表示未连接）
void wifi_config_page_set_connected_ssid(const char *ssid, bool connected);
// 配网页局部刷新计数：每 N 次局部刷新返回一次 true，提示上层插一次全刷清残影
bool wifi_config_page_needs_full_refresh();
// 配网页右下角 HH:MM（每分钟局刷，右上角位置会被键盘盖住）
void wifi_config_page_render_time(Canvas &canvas, const AppState &state);
