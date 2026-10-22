#pragma once

#include "esp_err.h"

// Enables the onboard charger and configures external-power detection.
esp_err_t sticky_charger_init();

// Returns true while USB/external power is present.
esp_err_t sticky_charger_read(bool &charging);
