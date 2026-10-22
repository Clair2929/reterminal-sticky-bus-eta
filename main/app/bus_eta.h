#pragma once

#include "esp_err.h"

struct AppState;

// Starts the background bus-data task (WiFi connect, NTP sync, periodic HTTP
// fetch). New data is published into AppState via update_bus_eta() and a
// BusDataReady event is posted to the app queue. Safe to call once.
esp_err_t bus_eta_start();

// True after SNTP has set the system clock, even if writing the hardware RTC
// failed. Used to display the network time without trusting it for ETA math.
bool bus_eta_system_time_valid();
