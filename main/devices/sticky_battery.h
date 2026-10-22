#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

struct BatteryReading {
    int percent = 0;
};

// Attaches the BQ27220 fuel gauge to the shared sensor I2C bus.
esp_err_t sticky_battery_init(i2c_master_bus_handle_t bus);

// Reads the battery state of charge as a percentage from 0 to 100.
esp_err_t sticky_battery_read(BatteryReading &reading);
