#include "app.h"

#include <cstdint>
#include <cstring>

#include "app_data.h"
#include "canvas.h"
#include "bus_config_page.h"
#include "bus_page.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "freertos/task.h"
#include "page.h"
#include "sleep_page.h"
#include "sticky_buzzer.h"
#include "sticky_display.h"
#include "sticky_power.h"
#include "sticky_touch.h"
#include "status_bar.h"
#include "wifi_config_page.h"

namespace {

constexpr char kTag[] = "app";
constexpr uint32_t kSavedStateMagic = 0x53544943U;  // "STIC"

RTC_NOINIT_ATTR uint32_t s_saved_state_magic;
RTC_NOINIT_ATTR uint8_t s_saved_state[sizeof(AppState)];
TickType_t s_last_button_activity_tick = 0;
constexpr TickType_t kIdleSleepTimeoutTicks = pdMS_TO_TICKS(60UL * 60UL * 1000UL);

// 当前激活页面：触摸回调（在 GT911 任务上下文）据此判断是否需要处理
PageId s_active_page = PageId::Bus;

void save_app_state_for_deep_sleep(const AppState &state)
{
    std::memcpy(s_saved_state, &state, sizeof(state));
    s_saved_state_magic = kSavedStateMagic;
}

const char *page_name(PageId page)
{
    switch (page) {
    case PageId::Bus:
        return "Bus";
    case PageId::BusConfig:
        return "Bus Setup";
    case PageId::WifiConfig:
        return "WiFi Config";
    }
    return "Unknown";
}

}  // namespace

void app_idle_activity_reset()
{
    s_last_button_activity_tick = xTaskGetTickCount();
}

bool app_idle_sleep_due()
{
    if (s_last_button_activity_tick == 0) {
        app_idle_activity_reset();
    }
    return static_cast<TickType_t>(xTaskGetTickCount() -
                                   s_last_button_activity_tick) >=
           kIdleSleepTimeoutTicks;
}

void app_handle_touch_tap(uint16_t x, uint16_t y)
{
    app_idle_activity_reset();
    if (s_active_page == PageId::WifiConfig) {
        wifi_config_page_handle_tap(x, y);
    } else if (s_active_page == PageId::BusConfig) {
        bus_config_page_handle_tap(x, y);
    }
}

esp_err_t refresh_current_page(const AppState &state)
{
    // WiFi 配置页与 Bus 配置页是高频交互页（每次按键/扫描/WiFi 事件都重绘）：
    // 整屏全刷会频繁闪黑->白->黑，观感差。改用局部刷新，只驱动
    // 变化的像素（A2 波形，几乎不闪）；每 25 次局部刷新插一次全刷
    // 清除 A2 累积的残影。其余页面保持单色全刷。
    if (state.current_page == PageId::WifiConfig &&
        !wifi_config_page_needs_full_refresh()) {
        return sticky_display_refresh_partial();
    }
    if (state.current_page == PageId::BusConfig &&
        !bus_config_page_needs_full_refresh()) {
        return sticky_display_refresh_partial();
    }
    return sticky_display_refresh_monochrome();
}

bool restore_app_state_after_deep_sleep(AppState &state)
{
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1 ||
        s_saved_state_magic != kSavedStateMagic) {
        return false;
    }

    std::memcpy(&state, s_saved_state, sizeof(state));
    s_saved_state_magic = 0;
    ESP_LOGI(kTag, "Restored %s Page state after deep sleep",
             page_name(state.current_page));
    return true;
}

void render_current_page(Canvas &canvas, const AppState &state)
{
    PageRenderer renderer = nullptr;

    switch (state.current_page) {
    case PageId::Bus:
        renderer = bus_page_render;
        break;
    case PageId::BusConfig:
        renderer = bus_config_page_render;
        break;
    case PageId::WifiConfig:
        renderer = wifi_config_page_render;
        break;
    }

    if (renderer != nullptr) {
        renderer(canvas, state);
    }
}

void select_previous_page(AppState &state)
{
    switch (state.current_page) {
    case PageId::Bus:
        state.current_page = PageId::WifiConfig;
        break;
    case PageId::BusConfig:
        state.current_page = PageId::Bus;
        break;
    case PageId::WifiConfig:
        state.current_page = PageId::BusConfig;
        break;
    }
}

void select_next_page(AppState &state)
{
    switch (state.current_page) {
    case PageId::Bus:
        state.current_page = PageId::BusConfig;
        break;
    case PageId::BusConfig:
        state.current_page = PageId::WifiConfig;
        break;
    case PageId::WifiConfig:
        state.current_page = PageId::Bus;
        break;
    }
}

esp_err_t handle_app_event(Canvas &canvas,
                           AppState &state,
                           AppEvent event)
{
    if (event == AppEvent::PreviousPage || event == AppEvent::NextPage) {
        app_idle_activity_reset();
    }
    if (event == AppEvent::ClockMinuteTick) {
        const bool previous_valid = state.date_time.valid;
        const int previous_hour = state.date_time.hour;
        const int previous_minute = state.date_time.minute;

        update_date_time(state);
        const bool time_changed =
            previous_valid != state.date_time.valid ||
            (state.date_time.valid &&
             (previous_hour != state.date_time.hour ||
              previous_minute != state.date_time.minute));
        if (!time_changed) {
            return ESP_OK;
        }

        // Bus page draws its own large clock: redraw only the clock area,
        // then partial refresh (matches the wiki partial-refresh pattern).
        if (state.current_page == PageId::Bus) {
            bus_page_render_clock(canvas, state);
            // 每分钟补读一次电池：上电时 BQ27220 可能未就绪导致首帧为空，
            // 这样即使一直停在 Bus 页，电量也会在 1 分钟内自动补上。
            update_battery(state);
            bus_page_render_status_area(canvas, state);
            ESP_LOGD(kTag, "Partially refreshing Bus Page clock");
            return sticky_display_refresh_partial();
        }

        // WiFi 配置页：时间在右下角（右上角会被键盘第一行盖住）
        if (state.current_page == PageId::WifiConfig) {
            wifi_config_page_render_time(canvas, state);
            ESP_LOGD(kTag, "Partially refreshing WiFi config clock");
            return sticky_display_refresh_partial();
        }

        // Bus 配置页：同样右下角时钟（布局与 WiFi 页一致）
        if (state.current_page == PageId::BusConfig) {
            bus_config_page_render_time(canvas, state);
            ESP_LOGD(kTag, "Partially refreshing Bus config clock");
            return sticky_display_refresh_partial();
        }

        status_bar_render_time(canvas, state);
        ESP_LOGD(kTag, "Partially refreshing RTC time");
        return sticky_display_refresh_partial();
    }

    if (event == AppEvent::BusDataReady) {
        update_bus_eta(state);
        if (state.current_page != PageId::Bus) {
            // 数据已存入 AppState，切到 Bus 页时自然会渲染最新数据
            return ESP_OK;
        }
        ESP_LOGD(kTag, "Bus data updated, refreshing Bus Page");
        update_date_time(state);
        render_current_page(canvas, state);
        return refresh_current_page(state);
    }

    if (event == AppEvent::WifiScanDone) {
        // 扫描结果已写入 WiFi 配置页内部状态：页面可见时立即重绘一次
        if (s_active_page == PageId::WifiConfig) {
            return handle_app_event(canvas, state, AppEvent::RefreshPage);
        }
        return ESP_OK;
    }

    const PageId previous_page = state.current_page;

    if (event == AppEvent::EnterDeepSleep || event == AppEvent::AutoSleep) {
        const bool automatic = event == AppEvent::AutoSleep;
        ESP_LOGI(kTag, "Preparing for deep sleep");
        if (automatic) {
            state.current_page = PageId::Bus;
        } else {
            const esp_err_t beep_result = sticky_buzzer_beep();
            if (beep_result != ESP_OK) {
                ESP_LOGW(kTag, "Deep-sleep feedback beep failed: %s",
                         esp_err_to_name(beep_result));
            }
        }
        save_app_state_for_deep_sleep(state);
        sleep_page_render(&canvas);
        ESP_RETURN_ON_ERROR(
            sticky_display_refresh_monochrome(),
            "app", "refresh deep sleep page");
        ESP_RETURN_ON_ERROR(
            sticky_touch_stop(), "app", "stop touch polling");
        ESP_RETURN_ON_ERROR(
            sticky_display_sleep(), "app", "sleep display");
        sticky_power_enter_deep_sleep();
    }

    switch (event) {
    case AppEvent::PreviousPage:
        select_previous_page(state);
        break;
    case AppEvent::NextPage:
        select_next_page(state);
        break;
    case AppEvent::RefreshPage:
    case AppEvent::WifiScanDone:
    case AppEvent::ClockMinuteTick:
    case AppEvent::BusDataReady:
        break;
    case AppEvent::EnterDeepSleep:
    case AppEvent::AutoSleep:
        // Handled before this switch because deep sleep does not return.
        break;
    }

    if (state.current_page != previous_page) {
        ESP_LOGI(kTag, "Page changed: %s -> %s",
                 page_name(previous_page), page_name(state.current_page));
        // 只在真正切页进入配置页时请求立即扫描。若放在渲染路径上，
        // 扫描完成→重绘→再次请求→立即扫描 会形成死循环，列表一直刷新。
        if (state.current_page == PageId::WifiConfig) {
            wifi_config_page_request_scan();
        }
    } else {
        ESP_LOGD(kTag, "Refreshing %s Page", page_name(state.current_page));
    }

    s_active_page = state.current_page;

    ESP_LOGD(kTag, "Rendering %s Page", page_name(state.current_page));

    // RTC failure changes only the status bar; page rendering still proceeds.
    update_date_time(state);

    if (state.current_page == PageId::Bus) {
        // 后台任务已把最新数据写入共享区，这里同步到 AppState
        update_bus_eta(state);
        // Bus 页右上角显示电池百分比，进入/刷新时读取一次
        update_battery(state);
    } else if (state.current_page == PageId::WifiConfig) {
        // WiFi 配置页状态由模块内部维护，无需同步 AppState
    } else if (state.current_page == PageId::BusConfig) {
        // Bus 配置页状态由模块内部维护，无需同步 AppState
    }

    render_current_page(canvas, state);
    ESP_RETURN_ON_ERROR(
        refresh_current_page(state), "app", "refresh current page");
    ESP_LOGD(kTag, "%s Page display refresh completed",
             page_name(state.current_page));
    // 键盘输入/扫描刷新时静音，避免每按一次键都响一声
    return state.current_page == PageId::WifiConfig ||
                   state.current_page == PageId::BusConfig
               ? ESP_OK
               : sticky_buzzer_beep();
}
