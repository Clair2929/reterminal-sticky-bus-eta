#pragma once

#include "esp_err.h"

// Initializes the active-low UP, DOWN, and AI/OK buttons.
esp_err_t sticky_buttons_init();
