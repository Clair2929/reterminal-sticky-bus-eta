#pragma once

#include <cstdint>

#include "page.h"

// Bus configuration page: city (Singapore / Hong Kong) selector + station
// name/ID input rows + the same virtual keyboard as the WiFi page.
//
// Saved values are written to NVS namespace "config" with the same keys
// (busProvider / busName / busId) used by user_config, so the device picks
// them up on next boot.
void bus_config_page_start();
void bus_config_page_render(Canvas &canvas, const AppState &state);
void bus_config_page_handle_tap(uint16_t x, uint16_t y);
// 配网页右下角 HH:MM（右上角会被键盘第一行盖住，跟 WiFi 页一致）
void bus_config_page_render_time(Canvas &canvas, const AppState &state);
// 局部刷新计数：每 N 次局部刷新返回一次 true，提示上层插一次全刷清残影
bool bus_config_page_needs_full_refresh();
