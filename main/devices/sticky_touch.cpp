#include "sticky_touch.h"

#include <algorithm>
#include <cstdint>

#include "app_event.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gt911.h"
#include "pin_config.h"

namespace {

constexpr char kTag[] = "sticky_touch";
constexpr uint16_t kDisplayWidth = 800;
constexpr uint16_t kDisplayHeight = 480;
constexpr uint16_t kPortraitWidth = 480;
constexpr uint16_t kPortraitHeight = 800;
constexpr int kMinimumSwipeDistance = 120;
constexpr uint16_t kTapMaxDistance = 60;
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(30);
constexpr uint32_t kTaskStackSize = 4096;
constexpr UBaseType_t kTaskPriority = 5;
constexpr TickType_t kStopTimeout = pdMS_TO_TICKS(200);

i2c_master_bus_handle_t s_touch_bus = nullptr;
GT911 s_controller;
TaskHandle_t s_touch_task = nullptr;
volatile bool s_stop_requested = false;
bool s_touching = false;
uint16_t s_start_x = 0;
uint16_t s_start_y = 0;
uint16_t s_last_x = 0;
uint16_t s_last_y = 0;
int s_max_dx = 0;  // 本次触摸期间相对起点的最大水平偏离
int s_max_dy = 0;  // 本次触摸期间相对起点的最大垂直偏离
TouchTapCallback s_tap_callback = nullptr;

uint16_t scale_coordinate(uint16_t value,
                          uint16_t source_max,
                          uint16_t target_max)
{
    const uint16_t clamped = std::min(value, source_max);
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(clamped) * target_max + source_max / 2U) /
        source_max);
}

void transform_touch_coordinate(uint16_t touch_x,
                                uint16_t touch_y,
                                uint16_t &display_x,
                                uint16_t &display_y)
{
    // Convert the GT911 portrait-oriented coordinates into the display's
    // original landscape framebuffer coordinates.
    const uint16_t portrait_x =
        scale_coordinate(touch_x, kDisplayWidth, kPortraitWidth - 1U);
    const uint16_t mapped_y = std::min(touch_y, kDisplayHeight);
    const uint16_t portrait_y =
        scale_coordinate(kDisplayHeight - mapped_y,
                         kDisplayHeight,
                         kPortraitHeight - 1U);

    const uint16_t framebuffer_x = kDisplayWidth - portrait_y - 1U;
    const uint16_t framebuffer_y = portrait_x;

    // sticky_display rotates the framebuffer by 180 degrees before sending it
    // to the panel. GT911 coordinates are independent of that display-layer
    // rotation, so apply the same rotation here and expose normal physical
    // screen coordinates to the app.
    display_x = kDisplayWidth - framebuffer_x - 1U;
    display_y = kDisplayHeight - framebuffer_y - 1U;
}

bool classify_horizontal_swipe(AppEvent &event)
{
    const int delta_x = static_cast<int>(s_last_x) - s_start_x;
    const int delta_y = static_cast<int>(s_last_y) - s_start_y;
    const int horizontal_distance = delta_x < 0 ? -delta_x : delta_x;
    const int vertical_distance = delta_y < 0 ? -delta_y : delta_y;

    // Require a deliberate horizontal gesture. Taps, short movements, and
    // primarily vertical gestures do not produce page events.
    if (horizontal_distance < kMinimumSwipeDistance ||
        horizontal_distance <= vertical_distance * 2) {
        return false;
    }

    event = delta_x < 0 ? AppEvent::NextPage : AppEvent::PreviousPage;
    return true;
}

const char *event_name(AppEvent event)
{
    switch (event) {
    case AppEvent::PreviousPage:
        return "PreviousPage";
    case AppEvent::NextPage:
        return "NextPage";
    case AppEvent::RefreshPage:
        return "RefreshPage";
    case AppEvent::ClockMinuteTick:
        return "ClockMinuteTick";
    case AppEvent::BusDataReady:
        return "BusDataReady";
    case AppEvent::WifiScanDone:
        return "WifiScanDone";
    case AppEvent::EnterDeepSleep:
        return "EnterDeepSleep";
    case AppEvent::AutoSleep:
        return "AutoSleep";
    }
    return "Unknown";
}

void touch_task(void *)
{
    TickType_t next_poll = xTaskGetTickCount();
    while (true) {
        if (s_stop_requested) {
            break;
        }

        GTPoint point = {};
        const int8_t count = s_controller.read_points(&point, 1);

        if (count > 0) {
            transform_touch_coordinate(point.x, point.y, s_last_x, s_last_y);
            if (!s_touching) {
                s_start_x = s_last_x;
                s_start_y = s_last_y;
                s_max_dx = 0;
                s_max_dy = 0;
                ESP_LOGD(kTag, "Touch down at (%u,%u)", s_start_x, s_start_y);
            } else {
                // 持续记录相对起点的最大偏离：GT911 点按时坐标会有抖动，
                // 用全程最大偏离判断 tap 比只看抬起点更稳。
                int dx = static_cast<int>(s_last_x) - s_start_x;
                int dy = static_cast<int>(s_last_y) - s_start_y;
                if (dx < 0) {
                    dx = -dx;
                }
                if (dy < 0) {
                    dy = -dy;
                }
                if (dx > s_max_dx) {
                    s_max_dx = dx;
                }
                if (dy > s_max_dy) {
                    s_max_dy = dy;
                }
            }
            s_touching = true;
        } else if (count == 0 && s_touching) {
            s_touching = false;
            AppEvent event = AppEvent::NextPage;
            if (classify_horizontal_swipe(event)) {
                ESP_LOGD(kTag,
                         "Swipe: (%u,%u) -> (%u,%u), event=%s",
                         s_start_x, s_start_y, s_last_x, s_last_y,
                         event_name(event));
                if (!app_event_post(event)) {
                    ESP_LOGW(kTag, "App event queue is full");
                }
            } else {
                // 全程最大偏离较小视为点按（tap）：供 WiFi 配置页等需要
                // 精确点击的界面使用。坐标已是物理屏幕坐标。
                const uint16_t distance =
                    static_cast<uint16_t>(s_max_dx + s_max_dy);
                if (distance <= kTapMaxDistance && s_tap_callback != nullptr) {
                    ESP_LOGD(kTag, "Tap detected at (%u,%u)",
                             s_start_x, s_start_y);
                    s_tap_callback(s_start_x, s_start_y);
                } else {
                    ESP_LOGD(kTag,
                             "Touch release ignored: max_dx=%d max_dy=%d "
                             "dist=%u",
                             s_max_dx, s_max_dy, distance);
                }
            }
        } else if (count < 0) {
            ESP_LOGW(kTag, "GT911 read failed");
            s_touching = false;
        }

        vTaskDelayUntil(&next_poll, kPollInterval);
    }

    s_touch_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

esp_err_t sticky_touch_init()
{
    s_stop_requested = false;
    gpio_config_t power_config = {};
    power_config.pin_bit_mask = 1ULL << PIN_TOUCH_EN;
    power_config.mode = GPIO_MODE_OUTPUT;
    power_config.pull_up_en = GPIO_PULLUP_DISABLE;
    power_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    power_config.intr_type = GPIO_INTR_DISABLE;

    esp_err_t result = gpio_config(&power_config);
    if (result != ESP_OK) {
        return result;
    }
    result = gpio_set_level(static_cast<gpio_num_t>(PIN_TOUCH_EN), 1);
    if (result != ESP_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(250));

    i2c_master_bus_config_t bus_config = {};
    // Match Sticky_dashboard_demo: keep GT911 on I2C0 and sensors on I2C1.
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = static_cast<gpio_num_t>(PIN_TOUCH_SDA);
    bus_config.scl_io_num = static_cast<gpio_num_t>(PIN_TOUCH_SCL);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = 1;
    result = i2c_new_master_bus(&bus_config, &s_touch_bus);
    if (result != ESP_OK) {
        return result;
    }

    if (!s_controller.begin(PIN_TOUCH_INT,
                            PIN_TOUCH_RST,
                            kDisplayWidth,
                            kDisplayHeight,
                            s_touch_bus)) {
        ESP_LOGE(kTag, "GT911 initialization failed");
        return ESP_FAIL;
    }

    uint16_t sensor_width = 0;
    uint16_t sensor_height = 0;
    s_controller.readResolution(sensor_width, sensor_height);
    ESP_LOGI(kTag, "GT911 ready: sensor=%ux%u display=%ux%u address=0x%02X",
             sensor_width, sensor_height,
             kDisplayWidth, kDisplayHeight, s_controller.address());

    if (xTaskCreate(touch_task,
                    "sticky_touch",
                    kTaskStackSize,
                    nullptr,
                    kTaskPriority,
                    &s_touch_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t sticky_touch_register_tap_callback(TouchTapCallback callback)
{
    s_tap_callback = callback;
    return ESP_OK;
}

esp_err_t sticky_touch_stop()
{
    if (s_touch_task == nullptr) {
        return ESP_OK;
    }

    s_stop_requested = true;
    const TickType_t deadline = xTaskGetTickCount() + kStopTimeout;
    while (s_touch_task != nullptr) {
        if (static_cast<int32_t>(xTaskGetTickCount() - deadline) >= 0) {
            ESP_LOGE(kTag, "Timed out while stopping touch task");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(kTag, "Touch polling stopped");
    return ESP_OK;
}
