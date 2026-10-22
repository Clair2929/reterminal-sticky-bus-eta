#pragma once

#include "esp_err.h"

// Initializes the onboard buzzer for a single short feedback tone.
esp_err_t sticky_buzzer_init();

// Plays one short, blocking button-feedback beep.
esp_err_t sticky_buzzer_beep();
