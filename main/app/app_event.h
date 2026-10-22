#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

enum class AppEvent {
    PreviousPage,
    NextPage,
    RefreshPage,
    ClockMinuteTick,
    BusDataReady,
    WifiScanDone,
    EnterDeepSleep,
    AutoSleep,
};

// Small shared queue used by physical buttons and touch input.
esp_err_t app_event_init();
bool app_event_post(AppEvent event);
bool app_event_wait(AppEvent &event, TickType_t timeout);
