#pragma once

#include "esp_err.h"

// Starts a minute-aligned timer that posts AppEvent::ClockMinuteTick.
// Pass -1 when the RTC second is unavailable; the first tick then occurs
// after one minute.
esp_err_t app_clock_start(int rtc_second);
