#include "app_clock.h"

#include <cstdint>

#include "app_event.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {

constexpr char kTag[] = "app_clock";
constexpr int64_t kMicrosecondsPerSecond = 1000000;
constexpr int64_t kMinutePeriodUs = 60 * kMicrosecondsPerSecond;
constexpr int64_t kRtcBoundaryMarginUs = 250000;

esp_timer_handle_t s_minute_timer = nullptr;

void minute_timer_callback(void *)
{
    if (!app_event_post(AppEvent::ClockMinuteTick)) {
        ESP_LOGW(kTag, "Clock minute event queue is full");
    }

    const esp_err_t result =
        esp_timer_start_once(s_minute_timer, kMinutePeriodUs);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Restart minute timer failed: %s",
                 esp_err_to_name(result));
    }
}

}  // namespace

esp_err_t app_clock_start(int rtc_second)
{
    if (s_minute_timer != nullptr) {
        return ESP_OK;
    }

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = minute_timer_callback;
    timer_args.arg = nullptr;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "sticky_minute";
    timer_args.skip_unhandled_events = true;
    esp_err_t result = esp_timer_create(&timer_args, &s_minute_timer);
    if (result != ESP_OK) {
        return result;
    }

    const bool rtc_second_valid = rtc_second >= 0 && rtc_second < 60;
    const int seconds_until_next_minute =
        rtc_second_valid ? 60 - rtc_second : 60;
    const int64_t first_tick_delay =
        seconds_until_next_minute * kMicrosecondsPerSecond +
        (rtc_second_valid ? kRtcBoundaryMarginUs : 0);

    result = esp_timer_start_once(s_minute_timer, first_tick_delay);
    if (result != ESP_OK) {
        esp_timer_delete(s_minute_timer);
        s_minute_timer = nullptr;
    }
    return result;
}
