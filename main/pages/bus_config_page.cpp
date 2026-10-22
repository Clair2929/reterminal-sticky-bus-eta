#include "bus_config_page.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app_event.h"
#include "app_state.h"
#include "canvas.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "stop_resolver.h"

namespace {

constexpr char kTag[] = "bus_cfg";

// 左栏布局（与 WiFi 配置页一致）：四行输入/选择框
constexpr int kTitleX = 16;
constexpr int kTitleY = 14;

constexpr int kRowX = 16;
constexpr int kRowW = 320;
constexpr int kRowH = 52;

constexpr int kCityY = 66;
constexpr int kNameY = 130;
constexpr int kIdY = 194;
constexpr int kRefreshY = 258;

// 刷新频率范围：1~60 分钟（对应 NVS refreshSeconds 的 60~3600 秒，与
// user_config.cpp 的 30~3600 秒校验兼容）
constexpr int kRefreshMinMinutes = 1;
constexpr int kRefreshMaxMinutes = 60;

// 键盘：与 WiFi 配置页完全相同的布局
constexpr int kKeyX = 350;
constexpr int kKeyY = 66;
constexpr int kKeyColW = 41;
constexpr int kKeyColGap = 2;
constexpr int kKeyRowH = 54;
constexpr int kKeyRowGap = 5;
constexpr int kKeyCols = 10;
constexpr int kKeyRows = 5;

// 右下角状态区：HH:MM 时钟（右上角 (670,52) 与键盘第一行重叠会被盖住）
constexpr int kClockX = 700;
constexpr int kClockY = 440;

// 输入目标：Station Name / Station ID / 刷新频率（City 行是点击切换，不进入键盘）
enum class InputTarget {
    Name,
    Id,
    Refresh,
};

// 键盘键定义（同 WiFi 页：字母、数字、@ . - _ 空格、SFT/BS/CLEAR/SAVE）
struct KeyDef {
    const char *label;
    int width;
};

constexpr KeyDef kKeyRowsDef[kKeyRows][kKeyCols] = {
    {{"q", 1}, {"w", 1}, {"e", 1}, {"r", 1}, {"t", 1},
     {"y", 1}, {"u", 1}, {"i", 1}, {"o", 1}, {"p", 1}},
    {{"SFT", 1}, {"a", 1}, {"s", 1}, {"d", 1}, {"f", 1},
     {"g", 1}, {"h", 1}, {"j", 1}, {"k", 1}, {"l", 1}},
    {{"SFT", 1}, {"z", 1}, {"x", 1}, {"c", 1}, {"v", 1},
     {"b", 1}, {"n", 1}, {"m", 1}, {"BS", 2}, {nullptr, 0}},
    {{"0", 1}, {"1", 1}, {"2", 1}, {"3", 1}, {"4", 1},
     {"5", 1}, {"6", 1}, {"7", 1}, {"8", 1}, {"9", 1}},
    {{"@", 1}, {".", 1}, {"-", 1}, {"_", 1}, {" ", 2},
     {"CLEAR", 2}, {"SAVE", 2}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}},
};

// 模块内 UI 状态（不进 AppState：触摸回调在这里更新）
struct BusConfigUi {
    bool use_singapore = true;   // true=新加坡, false=香港 KMB
    char bus_name[64] = {};
    char bus_id[24] = {};        // 新加坡 5 位数字 / KMB 16 位 hex，留足余量
    char refresh_buf[12] = {};   // 刷新频率（分钟）数字字符串，如 "2"
    InputTarget target = InputTarget::Name;
    bool saved = false;
    bool shift = false;          // 大小写切换状态
    char status[40] = {};
    char loaded_bus_name[64] = {};
    char loaded_bus_id[24] = {};
    bool loaded_use_singapore = true;
    bool loaded_config_valid = false;
};
BusConfigUi s_ui;

// 5x7 字体在指定 scale 下的文本像素宽度
int text_width(const char *text, int scale)
{
    return static_cast<int>(std::strlen(text)) * 6 * scale;
}

// ---------- 键盘布局（与 WiFi 配置页同一套几何逻辑） ----------

struct KeyRect {
    int row;
    int col;
    int x;
    int y;
    int w;
    int h;
};

KeyRect key_rect(int row, int col_start, int width)
{
    int x = kKeyX;
    for (int c = 0; c < col_start; ++c) {
        x += kKeyColW + kKeyColGap;
    }
    KeyRect r = {};
    r.row = row;
    r.col = col_start;
    r.x = x;
    r.y = kKeyY + row * (kKeyRowH + kKeyRowGap);
    r.w = width * kKeyColW + (width - 1) * kKeyColGap;
    r.h = kKeyRowH;
    return r;
}

const KeyDef *hit_test_key(int x, int y, KeyRect &out_rect)
{
    for (int row = 0; row < kKeyRows; ++row) {
        int col = 0;
        for (int i = 0; i < kKeyCols; ++i) {
            const KeyDef &key = kKeyRowsDef[row][i];
            if (key.label == nullptr) {
                break;
            }
            const KeyRect r = key_rect(row, col, key.width);
            if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                out_rect = r;
                return &key;
            }
            col += key.width;
        }
    }
    return nullptr;
}

// ---------- 输入操作 ----------

void append_char(char *dst, size_t capacity, char c)
{
    const size_t len = std::strlen(dst);
    if (len + 1 < capacity) {
        dst[len] = c;
        dst[len + 1] = '\0';
    }
}

void backspace(char *dst)
{
    const size_t len = std::strlen(dst);
    if (len > 0) {
        dst[len - 1] = '\0';
    }
}

char *target_buffer()
{
    switch (s_ui.target) {
    case InputTarget::Id:
        return s_ui.bus_id;
    case InputTarget::Refresh:
        return s_ui.refresh_buf;
    case InputTarget::Name:
    default:
        return s_ui.bus_name;
    }
}

size_t target_capacity()
{
    switch (s_ui.target) {
    case InputTarget::Id:
        return sizeof(s_ui.bus_id);
    case InputTarget::Refresh:
        return sizeof(s_ui.refresh_buf);
    case InputTarget::Name:
    default:
        return sizeof(s_ui.bus_name);
    }
}

// ---------- 保存到 NVS ----------

void load_saved_config()
{
    nvs_handle_t handle = 0;
    if (nvs_open("config", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    char provider[24] = {};
    size_t len = sizeof(provider);
    if (nvs_get_str(handle, "busProvider", provider, &len) == ESP_OK) {
        // 与 user_config::parse_provider_is_singapore 相同的小写匹配逻辑
        for (char &c : provider) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        s_ui.use_singapore =
            std::strstr(provider, "hong") == nullptr &&
            std::strstr(provider, "kmb") == nullptr;
    }
    len = sizeof(s_ui.bus_name);
    if (nvs_get_str(handle, "busName", s_ui.bus_name, &len) != ESP_OK) {
        s_ui.bus_name[0] = '\0';
    }
    len = sizeof(s_ui.bus_id);
    if (nvs_get_str(handle, "busId", s_ui.bus_id, &len) != ESP_OK) {
        s_ui.bus_id[0] = '\0';
    }
    // 刷新频率：NVS 存的是秒（refreshSeconds），页面上用分钟显示/输入
    int32_t refresh_seconds = 0;
    if (nvs_get_i32(handle, "refreshSeconds", &refresh_seconds) == ESP_OK &&
        refresh_seconds >= 60) {
        std::snprintf(s_ui.refresh_buf, sizeof(s_ui.refresh_buf), "%d",
                      static_cast<int>(refresh_seconds / 60));
    } else {
        // Empty means keep the configured/default interval when saving.
        s_ui.refresh_buf[0] = '\0';
    }
    nvs_close(handle);
    std::snprintf(s_ui.loaded_bus_name, sizeof(s_ui.loaded_bus_name), "%s",
                  s_ui.bus_name);
    std::snprintf(s_ui.loaded_bus_id, sizeof(s_ui.loaded_bus_id), "%s",
                  s_ui.bus_id);
    s_ui.loaded_use_singapore = s_ui.use_singapore;
    s_ui.loaded_config_valid = true;
    ESP_LOGI(kTag, "Loaded saved config: provider=%s name='%s' id='%s' "
                   "refresh=%smin",
             s_ui.use_singapore ? "singapore" : "hongkongkmb",
             s_ui.bus_name, s_ui.bus_id, s_ui.refresh_buf);
}

bool save_to_nvs(bool write_stop_config = true)
{
    nvs_handle_t handle = 0;
    if (nvs_open("config", NVS_READWRITE, &handle) != ESP_OK) {
        std::snprintf(s_ui.status, sizeof(s_ui.status), "NVS open failed");
        return false;
    }
    const esp_err_t provider_err = write_stop_config
        ? nvs_set_str(handle, "busProvider",
                      s_ui.use_singapore ? "singapore" : "hongkongkmb")
        : ESP_OK;
    const esp_err_t name_err = write_stop_config
        ? nvs_set_str(handle, "busName", s_ui.bus_name)
        : ESP_OK;
    const esp_err_t id_err = write_stop_config
        ? nvs_set_str(handle, "busId", s_ui.bus_id)
        : ESP_OK;
    const bool update_refresh = s_ui.refresh_buf[0] != '\0';
    const int refresh_minutes = update_refresh ? std::atoi(s_ui.refresh_buf) : 0;
    const esp_err_t refresh_err = update_refresh
        ? nvs_set_i32(handle, "refreshSeconds", refresh_minutes * 60)
        : ESP_OK;
    const esp_err_t commit_err = nvs_commit(handle);
    nvs_close(handle);
    if (provider_err != ESP_OK || name_err != ESP_OK ||
        id_err != ESP_OK || refresh_err != ESP_OK || commit_err != ESP_OK) {
        std::snprintf(s_ui.status, sizeof(s_ui.status), "NVS write failed");
        return false;
    }
    s_ui.saved = true;
    std::snprintf(s_ui.status, sizeof(s_ui.status), "Saved! Rebooting...");
    if (write_stop_config) {
        ESP_LOGI(kTag, "Saved station configuration; refresh interval=%s",
                 update_refresh ? s_ui.refresh_buf : "unchanged");
    } else {
        ESP_LOGI(kTag, "Saved refresh interval only: %d min", refresh_minutes);
    }
    return true;
}

void reboot_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

void schedule_reboot()
{
    if (xTaskCreate(reboot_task, "bus_cfg_reboot", 2048, nullptr, 5,
                    nullptr) != pdPASS) {
        esp_restart();
    }
}

// 保存任务的入参：复制一份 UI 输入，避免与触摸任务竞争 s_ui。
struct SaveParams {
    bool use_singapore = true;
    char bus_name[64] = {};
    char bus_id[24] = {};
    char refresh_buf[12] = {};
};

// 独立大栈任务：在线解析站名/ID（TLS + 大 JSON 解析需要 >4KB 栈，
// 触摸任务栈只有 4KB，不能在其上下文中跑），解析完成后保存并重启。
void resolve_and_save_task(void *arg)
{
    SaveParams p = *static_cast<const SaveParams *>(arg);
    free(arg);

    // 在线解析（需要 WiFi）：按"正确的一栏"整理站名与 ID。
    // - 只填 ID：用 ID 查询站名，ID 有效则覆盖显示名（不显示手输 name）；
    // - 只填 name：用站名反查 ID，唯一匹配则补全；
    // - 两个都填：优先 ID（ID 有效按 ID 的站名显示）；ID 无效回退 name。
    // 网络不可用时保留原输入直接保存。
    const StopResolveResult resolve_result = resolve_stop(
        p.use_singapore, p.bus_id, sizeof(p.bus_id),
        p.bus_name, sizeof(p.bus_name));

    // 解析结果写回 UI（保存后 1.5s 重启，期间的新输入随重启丢弃）
    std::memcpy(s_ui.bus_id, p.bus_id, sizeof(s_ui.bus_id));
    std::memcpy(s_ui.bus_name, p.bus_name, sizeof(s_ui.bus_name));

    switch (resolve_result) {
    case StopResolveResult::Ok:
        break;  // 解析成功，继续保存
    case StopResolveResult::IdNotFound:
        std::snprintf(s_ui.status, sizeof(s_ui.status),
                      "Station ID not found!");
        app_event_post(AppEvent::RefreshPage);
        vTaskDelete(nullptr);
        return;
    case StopResolveResult::NameNotFound:
        std::snprintf(s_ui.status, sizeof(s_ui.status), "Name not found!");
        app_event_post(AppEvent::RefreshPage);
        vTaskDelete(nullptr);
        return;
    case StopResolveResult::Ambiguous:
        std::snprintf(s_ui.status, sizeof(s_ui.status),
                      "Multiple matches; use Station ID");
        app_event_post(AppEvent::RefreshPage);
        vTaskDelete(nullptr);
        return;
    case StopResolveResult::NetworkError:
        if (p.bus_id[0] != '\0') {
            std::snprintf(s_ui.status, sizeof(s_ui.status),
                          "Station ID lookup failed");
            app_event_post(AppEvent::RefreshPage);
            vTaskDelete(nullptr);
            return;
        }
        std::snprintf(s_ui.status, sizeof(s_ui.status),
                      "Offline: saved as-is");
        break;  // 网络不可用：按当前输入保存
    }

    if (!save_to_nvs()) {
        app_event_post(AppEvent::RefreshPage);
        vTaskDelete(nullptr);
        return;
    }
    app_event_post(AppEvent::RefreshPage);
    schedule_reboot();
    vTaskDelete(nullptr);
}

void request_save()
{
    // Empty refresh interval keeps the existing NVS value or firmware default.
    if (s_ui.refresh_buf[0] != '\0') {
        char *refresh_end = nullptr;
        const long refresh_minutes =
            std::strtol(s_ui.refresh_buf, &refresh_end, 10);
        if (refresh_end == s_ui.refresh_buf || *refresh_end != '\0' ||
            refresh_minutes < kRefreshMinMinutes ||
            refresh_minutes > kRefreshMaxMinutes) {
            std::snprintf(s_ui.status, sizeof(s_ui.status),
                          "Refresh 1-%d min or blank", kRefreshMaxMinutes);
            app_event_post(AppEvent::RefreshPage);
            return;
        }
    }

    const bool has_stop =
        s_ui.bus_name[0] != '\0' || s_ui.bus_id[0] != '\0';
    const bool has_refresh = s_ui.refresh_buf[0] != '\0';
    if (!has_stop && !has_refresh) {
        std::snprintf(s_ui.status, sizeof(s_ui.status),
                      "Set Name, Station ID or Refresh");
        app_event_post(AppEvent::RefreshPage);
        return;
    }
    if (!has_stop) {
        // Refresh-only save: leave provider, station name and ID untouched.
        if (save_to_nvs(false)) {
            app_event_post(AppEvent::RefreshPage);
            schedule_reboot();
        } else {
            app_event_post(AppEvent::RefreshPage);
        }
        return;
    }
    const bool station_config_unchanged = s_ui.loaded_config_valid &&
        s_ui.use_singapore == s_ui.loaded_use_singapore &&
        std::strcmp(s_ui.bus_name, s_ui.loaded_bus_name) == 0 &&
        std::strcmp(s_ui.bus_id, s_ui.loaded_bus_id) == 0;
    if (has_refresh && station_config_unchanged) {
        // Existing values are displayed for reference; an untouched station
        // config means this SAVE is only changing the refresh interval.
        if (save_to_nvs(false)) {
            app_event_post(AppEvent::RefreshPage);
            schedule_reboot();
        } else {
            app_event_post(AppEvent::RefreshPage);
        }
        return;
    }

    SaveParams *p = static_cast<SaveParams *>(std::malloc(sizeof(SaveParams)));
    if (p == nullptr) {
        std::snprintf(s_ui.status, sizeof(s_ui.status), "Save failed");
        app_event_post(AppEvent::RefreshPage);
        return;
    }
    std::memcpy(p->bus_name, s_ui.bus_name, sizeof(s_ui.bus_name));
    std::memcpy(p->bus_id, s_ui.bus_id, sizeof(s_ui.bus_id));
    std::memcpy(p->refresh_buf, s_ui.refresh_buf, sizeof(s_ui.refresh_buf));
    p->use_singapore = s_ui.use_singapore;

    std::snprintf(s_ui.status, sizeof(s_ui.status), "Resolving stop...");
    app_event_post(AppEvent::RefreshPage);
    if (xTaskCreate(resolve_and_save_task, "bus_cfg_save", 8192, p, 5,
                    nullptr) != pdPASS) {
        free(p);
        std::snprintf(s_ui.status, sizeof(s_ui.status), "Save failed");
        app_event_post(AppEvent::RefreshPage);
        return;
    }
}

// ---------- 触摸处理 ----------

void handle_tap(int x, int y)
{
    // 1) City 选择行：点击切换 新加坡 / 香港
    if (x >= kRowX && x < kRowX + kRowW && y >= kCityY && y < kCityY + kRowH) {
        ESP_LOGD(kTag, "Tap hit: City row (%d,%d)", x, y);
        s_ui.use_singapore = !s_ui.use_singapore;
        app_event_post(AppEvent::RefreshPage);
        return;
    }

    // 2) Station Name 输入行
    if (x >= kRowX && x < kRowX + kRowW && y >= kNameY && y < kNameY + kRowH) {
        ESP_LOGD(kTag, "Tap hit: Name row (%d,%d)", x, y);
        s_ui.target = InputTarget::Name;
        app_event_post(AppEvent::RefreshPage);
        return;
    }

    // 3) Station ID 输入行
    if (x >= kRowX && x < kRowX + kRowW && y >= kIdY && y < kIdY + kRowH) {
        ESP_LOGD(kTag, "Tap hit: ID row (%d,%d)", x, y);
        s_ui.target = InputTarget::Id;
        app_event_post(AppEvent::RefreshPage);
        return;
    }

    // 4) 刷新频率输入行
    if (x >= kRowX && x < kRowX + kRowW && y >= kRefreshY &&
        y < kRefreshY + kRowH) {
        ESP_LOGD(kTag, "Tap hit: Refresh row (%d,%d)", x, y);
        s_ui.target = InputTarget::Refresh;
        app_event_post(AppEvent::RefreshPage);
        return;
    }

    // 5) 虚拟键盘
    KeyRect rect = {};
    const KeyDef *key = hit_test_key(x, y, rect);
    if (key == nullptr) {
        ESP_LOGD(kTag, "Tap missed all controls (%d,%d)", x, y);
        return;
    }
    ESP_LOGD(kTag, "Tap hit: key '%s' (%d,%d)", key->label, x, y);
    const char *label = key->label;
    if (std::strcmp(label, "BS") == 0) {
        backspace(target_buffer());
    } else if (std::strcmp(label, "SFT") == 0) {
        s_ui.shift = !s_ui.shift;
    } else if (std::strcmp(label, "CLEAR") == 0) {
        target_buffer()[0] = '\0';
    } else if (std::strcmp(label, "SAVE") == 0) {
        request_save();
        return;  // save 内部已触发刷新
    } else {
        char c = label[0];
        // 刷新频率只接受数字（0-9）；字母/符号键直接忽略
        if (s_ui.target == InputTarget::Refresh) {
            if (c >= '0' && c <= '9') {
                append_char(s_ui.refresh_buf, sizeof(s_ui.refresh_buf), c);
            }
        } else {
            // 字母键按下时按 shift 状态输入大小写
            if (c >= 'a' && c <= 'z' && s_ui.shift) {
                c = static_cast<char>(c - 'a' + 'A');
            }
            append_char(target_buffer(), target_capacity(), c);
        }
    }
    app_event_post(AppEvent::RefreshPage);
}

// ---------- 渲染 ----------

void draw_row_box(Canvas &canvas, int x, int y, bool focused)
{
    if (focused) {
        canvas.fill_rect(x, y, kRowW, kRowH, GrayLevel::Black);
    } else {
        canvas.fill_rect(x, y, kRowW, kRowH, GrayLevel::White);
        canvas.draw_rect(x, y, kRowW, kRowH, GrayLevel::Black);
    }
}

void draw_input_row(Canvas &canvas, int y, const char *label,
                    const char *value, bool focused)
{
    draw_row_box(canvas, kRowX, y, focused);
    const GrayLevel text_color = focused ? GrayLevel::White : GrayLevel::Black;
    const bool station_id_label = std::strcmp(label, "Station ID") == 0;
    canvas.draw_text(kRowX + 8, y + (station_id_label ? 22 : 19), label,
                     station_id_label ? 1 : 2, text_color);
    char clipped[40] = {};
    const size_t len = std::strlen(value);
    const size_t max_chars = 18;  // 18 字符 * 12px <= 330-100 可用宽度
    const size_t copy_len = len > max_chars ? max_chars : len;
    std::memcpy(clipped, value, copy_len);
    if (len > max_chars) {
        std::snprintf(clipped + copy_len, sizeof(clipped) - copy_len, "...");
    }
    canvas.draw_text(kRowX + 100, y + 19, clipped[0] != '\0' ? clipped : " ",
                     2, text_color);
}

// City 行：反白显示当前城市，点击即切换
void draw_city_row(Canvas &canvas)
{
    draw_row_box(canvas, kRowX, kCityY, true);
    canvas.draw_text(kRowX + 8, kCityY + 19, "City", 2, GrayLevel::White);
    const char *city = s_ui.use_singapore ? "Singapore" : "Hong Kong";
    canvas.draw_text(kRowX + 100, kCityY + 19, city, 2, GrayLevel::White);
}

void draw_keyboard(Canvas &canvas)
{
    for (int row = 0; row < kKeyRows; ++row) {
        int col = 0;
        for (int i = 0; i < kKeyCols; ++i) {
            const KeyDef &key = kKeyRowsDef[row][i];
            if (key.label == nullptr) {
                break;
            }
            const KeyRect r = key_rect(row, col, key.width);

            // 键面文本：字母键随 shift 显示大小写；Shift 键激活时反白
            char display[8] = {};
            std::snprintf(display, sizeof(display), "%s", key.label);
            bool invert = false;
            if (std::strcmp(key.label, "SFT") == 0) {
                invert = s_ui.shift;
            } else if (display[0] >= 'a' && display[0] <= 'z' &&
                       display[1] == '\0') {
                if (s_ui.shift) {
                    display[0] = static_cast<char>(display[0] - 'a' + 'A');
                }
            }

            if (invert) {
                canvas.fill_rect(r.x, r.y, r.w, r.h, GrayLevel::Black);
            } else {
                canvas.fill_rect(r.x, r.y, r.w, r.h, GrayLevel::White);
            }
            canvas.draw_rect(r.x, r.y, r.w, r.h, GrayLevel::Black);
            const int text_w = text_width(display, 2);
            canvas.draw_text(r.x + (r.w - text_w) / 2,
                             r.y + (r.h - 14) / 2, display, 2,
                             invert ? GrayLevel::White : GrayLevel::Black);
            col += key.width;
        }
    }
}

}  // namespace

// -----------------------------------------------------------------------------
// 公共接口
// -----------------------------------------------------------------------------
void bus_config_page_start()
{
    // 回填上次保存的 city/站名/站ID，方便确认当前生效的配置
    load_saved_config();
}

void bus_config_page_render(Canvas &canvas, const AppState &state)
{
    (void)state;
    canvas.clear(GrayLevel::White);

    canvas.draw_text(kTitleX, kTitleY, "Bus Setup", 3, GrayLevel::Black);

    // City 选择行（反白，点击切换）
    draw_city_row(canvas);

    // 输入行
    draw_input_row(canvas, kNameY, "Name", s_ui.bus_name,
                   s_ui.target == InputTarget::Name);
    draw_input_row(canvas, kIdY, "Station ID", s_ui.bus_id,
                   s_ui.target == InputTarget::Id);
    char refresh_text[24] = {};
    if (s_ui.refresh_buf[0] != '\0') {
        std::snprintf(refresh_text, sizeof(refresh_text), "%s min",
                      s_ui.refresh_buf);
    } else {
        std::snprintf(refresh_text, sizeof(refresh_text), "-- min (optional)");
    }
    draw_input_row(canvas, kRefreshY, "Refresh", refresh_text,
                   s_ui.target == InputTarget::Refresh);

    // 虚拟键盘
    draw_keyboard(canvas);

    // 提示：当前输入目标
    canvas.draw_text(kKeyX, 378, "Target:", 2, GrayLevel::Black);
    const char *target_text = "Name";
    if (s_ui.target == InputTarget::Id) {
        target_text = "Station ID";
    } else if (s_ui.target == InputTarget::Refresh) {
        target_text = "Refresh";
    }
    canvas.draw_text(kKeyX + 90, 378, target_text, 2, GrayLevel::Black);

    // High-contrast prompt/status banner in the lower-right corner.
    canvas.fill_rect(kKeyX, 406, 430, 32, GrayLevel::Black);
    canvas.draw_text(kKeyX + 10, 415,
                     s_ui.status[0] != '\0'
                         ? s_ui.status
                         : "Save: Name / Station ID / Refresh",
                     2, GrayLevel::White);

    // 右下角 HH:MM 时钟（与 WiFi 页一致，右上角会被键盘盖住）
    bus_config_page_render_time(canvas, state);
}

// 右下角 HH:MM（独立于右上角的状态栏时间，每分钟由 ClockMinuteTick 局刷）
void bus_config_page_render_time(Canvas &canvas, const AppState &state)
{
    canvas.fill_rect(kClockX - 8, kClockY - 4, 88, 28, GrayLevel::White);
    char time_text[6] = "--:--";
    if (state.date_time.valid) {
        std::snprintf(time_text, sizeof(time_text), "%02d:%02d",
                      state.date_time.hour, state.date_time.minute);
    }
    canvas.draw_text(kClockX, kClockY, time_text, 2, GrayLevel::Black);
}

void bus_config_page_handle_tap(uint16_t x, uint16_t y)
{
    handle_tap(static_cast<int>(x), static_cast<int>(y));
}

// 局部刷新计数：与 WiFi 页相同，每 25 次局部刷新插一次整屏全刷清残影
bool bus_config_page_needs_full_refresh()
{
    constexpr int kPartialCountBeforeFull = 25;
    static int s_partial_refresh_count = 0;
    ++s_partial_refresh_count;
    if (s_partial_refresh_count >= kPartialCountBeforeFull) {
        s_partial_refresh_count = 0;
        return true;
    }
    return false;
}
