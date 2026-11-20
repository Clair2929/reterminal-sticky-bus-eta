#include "app.h"
#include "app_clock.h"
#include "app_data.h"
#include "board.h"
#include "bus_config_page.h"
#include "bus_eta.h"
#include "canvas.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sticky_buzzer.h"
#include "sticky_battery.h"
#include "sticky_buttons.h"
#include "sticky_charger.h"
#include "sticky_display.h"
#include "sticky_power.h"
#include "sticky_rtc.h"
#include "sticky_sht40.h"
#include "sticky_touch.h"
#include "wifi_config_page.h"

namespace {

constexpr char kTag[] = "sticky_bus_eta";

void show_initial_page(Canvas &canvas, const AppState &state)
{
    render_current_page(canvas, state);
    ESP_ERROR_CHECK(refresh_current_page(state));
}

esp_err_t init_nvs()
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    return result;
}

}  // namespace

extern "C" void app_main()
{
    ESP_LOGI(kTag, "Starting reTerminal Sticky Bus ETA");
    sticky_power_log_wakeup_reason();
    sticky_power_prepare_after_wakeup();

    // WiFi 需要 NVS 存储校准数据
    ESP_ERROR_CHECK(init_nvs());

    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(sticky_display_init());
    ESP_ERROR_CHECK(app_event_init());
    ESP_ERROR_CHECK(sticky_buttons_init());
    ESP_ERROR_CHECK(sticky_touch_init());
    // 触摸点按（tap）回调：WiFi 配置页用，其他页面忽略
    ESP_ERROR_CHECK(sticky_touch_register_tap_callback(app_handle_touch_tap));
    // WiFi 配置页后台扫描任务（等待 bus_eta 初始化 WiFi 后自动开始）
    wifi_config_page_start();
    // Bus 配置页：回填上次保存的 City / 站名 / 站 ID / 刷新频率
    bus_config_page_start();
    ESP_ERROR_CHECK(sticky_buzzer_init());
    ESP_ERROR_CHECK(sticky_charger_init());
    const esp_err_t battery_init_result =
        sticky_battery_init(board_sensor_i2c_bus());
    if (battery_init_result != ESP_OK) {
        // The fuel gauge is optional. A missing BQ27220 must not abort boot:
        // otherwise every configuration save is followed by a crash/reboot.
        ESP_LOGW(kTag, "Battery monitor unavailable: %s",
                 esp_err_to_name(battery_init_result));
    }
    ESP_ERROR_CHECK(sticky_sht40_init(board_sensor_i2c_bus()));
    ESP_ERROR_CHECK(sticky_rtc_init(board_sensor_i2c_bus()));

    Canvas *canvas = sticky_display_canvas();
    ESP_ERROR_CHECK(canvas != nullptr ? ESP_OK : ESP_ERR_INVALID_STATE);

    AppState state;
    restore_app_state_after_deep_sleep(state);
    update_date_time(state);
    // 上电先读一次电池：BQ27220 刚上电可能未就绪，读失败也不阻塞，
    // 之后 Bus 页每分钟局部刷新会继续补读。
    update_battery(state);
    show_initial_page(*canvas, state);
    ESP_ERROR_CHECK(
        app_clock_start(state.date_time.valid ? state.date_time.second : -1));
    ESP_ERROR_CHECK(bus_eta_start());
    app_idle_activity_reset();

    while (true) {
        AppEvent event;
        if (!app_event_wait(event, pdMS_TO_TICKS(1000))) {
            if (app_idle_sleep_due()) {
                const esp_err_t sleep_result =
                    handle_app_event(*canvas, state, AppEvent::AutoSleep);
                if (sleep_result != ESP_OK) {
                    ESP_LOGE(kTag, "Auto sleep failed: %s",
                             esp_err_to_name(sleep_result));
                }
            }
            continue;
        }

        const esp_err_t result = handle_app_event(*canvas, state, event);
        if (result != ESP_OK) {
            ESP_LOGE(kTag, "App event failed: %s", esp_err_to_name(result));
        }
        if (app_idle_sleep_due()) {
            const esp_err_t sleep_result =
                handle_app_event(*canvas, state, AppEvent::AutoSleep);
            if (sleep_result != ESP_OK) {
                ESP_LOGE(kTag, "Auto sleep failed: %s",
                         esp_err_to_name(sleep_result));
            }
        }
    }
}
