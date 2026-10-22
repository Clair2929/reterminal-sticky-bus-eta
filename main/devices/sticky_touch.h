#pragma once

#include <cstdint>

#include "esp_err.h"

// Powers and initializes GT911, then starts the lightweight polling task.
esp_err_t sticky_touch_init();

// Stops the polling task before touch power is removed for deep sleep.
esp_err_t sticky_touch_stop();

// Callback invoked on a tap (press + release with little movement).
// Coordinates are physical display coordinates (800x480 landscape).
using TouchTapCallback = void (*)(uint16_t x, uint16_t y);

// Registers (or replaces) the tap callback. Pass nullptr to clear.
esp_err_t sticky_touch_register_tap_callback(TouchTapCallback callback);
