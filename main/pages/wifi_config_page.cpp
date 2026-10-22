#include "wifi_config_page.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app_event.h"
#include "app_state.h"
#include "canvas.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "icons.h"
#include "nvs.h"

namespace {

constexpr char kTag[] = "wifi_cfg";

// 左栏窄一些，给右侧键盘腾出更大空间
constexpr int kTitleX = 16;
constexpr int kTitleY = 14;

constexpr int kRowX = 16;
constexpr int kRowY = 52;    // SSID 输入行
constexpr int kRowW = 330;
constexpr int kRowH = 42;

// SSID 与密码两行上下紧挨在一起
constexpr int kPwdY = 104;

// WiFi 列表从两行输入框下方开始
constexpr int kListY = 160;
constexpr int kListRowH = 44;
constexpr int kListRowCount = 7;

// 键盘：从 360 开始，键宽 40、间距 3 -> 10 列共 427px，到 787
constexpr int kKeyX = 360;
constexpr int kKeyY = 48;
constexpr int kKeyColW = 40;
constexpr int kKeyColGap = 3;
constexpr int kKeyRowH = 44;
constexpr int kKeyRowGap = 4;
constexpr int kKeyCols = 10;
constexpr int kKeyRows = 5;

// 右下角状态区：loading 图标（扫描中）在上，HH:MM 时钟在下
// （右上角 (670,52) 与键盘第一行重叠会盖住字母，所以放右下角空白区）
constexpr int kLoadingX = 700;
constexpr int kLoadingY = 378;
constexpr int kLoadingSize = 48;
constexpr int kClockX = 700;
constexpr int kClockY = 440;

constexpr size_t kMaxAps = 12;
constexpr uint32_t kScanIntervalMs = 20000;
constexpr uint32_t kStartupDelayMs = 3000;

enum class InputTarget {
    None,
    Ssid,
    Password,
};

struct ApEntry {
    char ssid[33] = {};
    int rssi = -100;
};

// 键盘键定义：label 为显示文本，width 为占用列数
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

// 模块内 UI 状态（不放进 AppState：扫描任务 + 触摸回调都在这里更新）
struct WifiConfigUi {
    char ssid[33] = {};
    char password[65] = {};
    InputTarget target = InputTarget::Password;
    ApEntry aps[kMaxAps] = {};
    size_t ap_count = 0;
    bool scanning = false;
    bool saved = false;
    bool shift = false;          // 大小写切换状态
    uint32_t last_input_ms = 0;  // 最近一次输入/点按时间，用于抑制扫描重绘
    char connected_ssid[33] = {};  // 当前已连接 AP 的 SSID（空表示未连接）
    bool connected = false;
    char status[40] = {};
};
WifiConfigUi s_ui;
volatile bool s_scan_now = false;

// 5x7 字体在指定 scale 下的文本像素宽度
int text_width(const char *text, int scale)
{
    return static_cast<int>(std::strlen(text)) * 6 * scale;
}

// 把 1-bit 位图（每行打包，MSB 在前）画到 Canvas 上
void draw_bitmap(Canvas &canvas, const uint8_t *bitmap, int width, int height,
                 int x, int y)
{
    const int cols = (width + 7) / 8;
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            if ((bitmap[row * cols + col / 8] & (0x80U >> (col % 8))) != 0U) {
                canvas.draw_pixel(x + col, y + row, GrayLevel::Black);
            }
        }
    }
}

// ---------- 键盘布局 ----------

struct KeyRect {
    int row;
    int col;
    int x;
    int y;
    int w;
    int h;
};

// 计算第 row 行、从 col_start 起占 width 列的键矩形
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

// 遍历行/列，找到包含 (x,y) 的键，返回其定义指针
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
    case InputTarget::Ssid:
        return s_ui.ssid;
    case InputTarget::Password:
    case InputTarget::None:
    default:
        return s_ui.password;
    }
}

// ---------- 保存到 NVS ----------

// 启动时把已保存的 SSID/密码回填到输入框，用户重启后能确认当前配置
void load_saved_config()
{
    nvs_handle_t handle = 0;
    if (nvs_open("config", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_ui.ssid);
    if (nvs_get_str(handle, "wifiSsid", s_ui.ssid, &len) != ESP_OK) {
        s_ui.ssid[0] = '\0';
    }
    len = sizeof(s_ui.password);
    if (nvs_get_str(handle, "wifiPassword", s_ui.password, &len) != ESP_OK) {
        s_ui.password[0] = '\0';
    }
    nvs_close(handle);
    ESP_LOGI(kTag, "Loaded saved WiFi configuration");
}

void save_to_nvs()
{
    nvs_handle_t handle = 0;
    if (nvs_open("config", NVS_READWRITE, &handle) != ESP_OK) {
        std::snprintf(s_ui.status, sizeof(s_ui.status), "NVS open failed");
        return;
    }
    const esp_err_t ssid_err = nvs_set_str(handle, "wifiSsid", s_ui.ssid);
    const esp_err_t pwd_err =
        nvs_set_str(handle, "wifiPassword", s_ui.password);
    const esp_err_t commit_err = nvs_commit(handle);
    nvs_close(handle);
    if (ssid_err != ESP_OK || pwd_err != ESP_OK || commit_err != ESP_OK) {
        std::snprintf(s_ui.status, sizeof(s_ui.status), "NVS write failed");
        return;
    }
    s_ui.saved = true;
    std::snprintf(s_ui.status, sizeof(s_ui.status), "Saved! Rebooting...");
    ESP_LOGI(kTag, "Saved WiFi configuration to NVS");
}

void reboot_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

void request_save()
{
    if (s_ui.ssid[0] == '\0') {
        std::snprintf(s_ui.status, sizeof(s_ui.status), "SSID empty!");
        app_event_post(AppEvent::RefreshPage);
        return;
    }
    save_to_nvs();
    app_event_post(AppEvent::RefreshPage);
    if (xTaskCreate(reboot_task, "wifi_reboot", 2048, nullptr, 5, nullptr) !=
        pdPASS) {
        esp_restart();  // 任务创建失败也直接重启
    }
}

// ---------- 触摸处理 ----------

void handle_tap(int x, int y)
{
    // 任何点按都视为输入活动：扫描完成时据此跳过重绘，避免打断输入
    s_ui.last_input_ms =
        static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

    // 1) SSID 输入行
    if (x >= kRowX && x < kRowX + kRowW && y >= kRowY && y < kRowY + kRowH) {
        ESP_LOGD(kTag, "Tap hit: SSID row (%d,%d)", x, y);
        s_ui.target = InputTarget::Ssid;
        app_event_post(AppEvent::RefreshPage);
        return;
    }

    // 2) 密码输入行
    if (x >= kRowX && x < kRowX + kRowW && y >= kPwdY && y < kPwdY + kRowH) {
        ESP_LOGD(kTag, "Tap hit: PWD row (%d,%d)", x, y);
        s_ui.target = InputTarget::Password;
        app_event_post(AppEvent::RefreshPage);
        return;
    }

    // 3) WiFi 列表项：选中即填充 SSID，并把输入目标切到密码
    if (x >= kRowX && x < kRowX + kRowW && y >= kListY) {
        const int index = (y - kListY) / kListRowH;
        if (index >= 0 && static_cast<size_t>(index) < s_ui.ap_count) {
            ESP_LOGD(kTag, "Tap hit: AP list idx=%d (%d,%d)",
                     index, x, y);
            std::snprintf(s_ui.ssid, sizeof(s_ui.ssid), "%s",
                          s_ui.aps[index].ssid);
            s_ui.target = InputTarget::Password;
            app_event_post(AppEvent::RefreshPage);
            return;
        }
    }

    // 4) 虚拟键盘
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
        // 字母键按下时按 shift 状态输入大小写
        if (c >= 'a' && c <= 'z' && s_ui.shift) {
            c = static_cast<char>(c - 'a' + 'A');
        }
        append_char(target_buffer(),
                    s_ui.target == InputTarget::Ssid
                        ? sizeof(s_ui.ssid)
                        : sizeof(s_ui.password),
                    c);
    }
    app_event_post(AppEvent::RefreshPage);
}

// ---------- WiFi 扫描 ----------

int compare_ap_by_rssi(const void *a, const void *b)
{
    const ApEntry *pa = static_cast<const ApEntry *>(a);
    const ApEntry *pb = static_cast<const ApEntry *>(b);
    return pb->rssi - pa->rssi;  // 信号强在前
}

void wifi_scan_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(kStartupDelayMs));
    while (true) {
        wifi_mode_t mode = WIFI_MODE_NULL;
        if (esp_wifi_get_mode(&mode) == ESP_OK && mode != WIFI_MODE_NULL) {
            s_ui.scanning = true;

            wifi_scan_config_t scan_config = {};
            scan_config.show_hidden = false;
            scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
            // A scan can be rejected while STA is associating or obtaining an
            // IP. Never disconnect STA to force a scan: that delays bus fetches
            // and can create a connect/scan/disconnect loop. Defer to the next
            // scheduled scan instead.
            const esp_err_t scan_result =
                esp_wifi_scan_start(&scan_config, true);
            if (scan_result != ESP_OK) {
                ESP_LOGW(kTag, "Scan deferred (%s); keeping STA connection",
                         esp_err_to_name(scan_result));
            }
            if (scan_result == ESP_OK) {
                uint16_t count = 0;
                if (esp_wifi_scan_get_ap_num(&count) == ESP_OK) {
                    if (count > kMaxAps) {
                        count = kMaxAps;
                    }
                    wifi_ap_record_t *records = static_cast<wifi_ap_record_t *>(
                        std::calloc(count, sizeof(wifi_ap_record_t)));
                    if (records != nullptr &&
                        esp_wifi_scan_get_ap_records(&count, records) == ESP_OK) {
                        std::memset(s_ui.aps, 0, sizeof(s_ui.aps));
                        s_ui.ap_count = count;
                        for (uint16_t i = 0; i < count; ++i) {
                            std::snprintf(s_ui.aps[i].ssid,
                                          sizeof(s_ui.aps[i].ssid), "%s",
                                          records[i].ssid);
                            s_ui.aps[i].rssi = records[i].rssi;
                        }
                        std::qsort(s_ui.aps, s_ui.ap_count,
                                   sizeof(ApEntry), compare_ap_by_rssi);
                        std::snprintf(s_ui.status, sizeof(s_ui.status),
                                      "%u network(s)", count);
                        ESP_LOGI(kTag, "Scan done: %u APs", count);
                    }
                    std::free(records);
                }
            }
            s_ui.scanning = false;
            app_event_post(AppEvent::WifiScanDone);
        }
        // 页面激活时请求立即扫描：跳过周期等待直接下一轮
        if (s_scan_now) {
            s_scan_now = false;
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(kScanIntervalMs));
    }
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
    canvas.draw_text(kRowX + 8, y + 14, label, 2, text_color);
    char clipped[40] = {};
    const size_t len = std::strlen(value);
    const size_t max_chars = 18;  // 18 字符 * 12px <= 330-100 可用宽度
    const size_t copy_len = len > max_chars ? max_chars : len;
    std::memcpy(clipped, value, copy_len);
    if (len > max_chars) {
        std::snprintf(clipped + copy_len, sizeof(clipped) - copy_len, "...");
    }
    canvas.draw_text(kRowX + 100, y + 14, clipped[0] != '\0' ? clipped : " ",
                     2, text_color);
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
void wifi_config_page_start()
{
    // 回填上次保存的 SSID/密码，方便确认当前生效的配置
    load_saved_config();
    if (xTaskCreate(wifi_scan_task, "wifi_scan", 4096, nullptr, 5, nullptr) !=
        pdPASS) {
        ESP_LOGE(kTag, "Failed to create scan task");
    }
}

void wifi_config_page_render(Canvas &canvas, const AppState &state)
{
    (void)state;
    canvas.clear(GrayLevel::White);

    canvas.draw_text(kTitleX, kTitleY, "WiFi Setup", 3, GrayLevel::Black);

    // 输入行
    draw_input_row(canvas, kRowY, "SSID", s_ui.ssid,
                   s_ui.target == InputTarget::Ssid);
    draw_input_row(canvas, kPwdY, "PWD", s_ui.password,
                   s_ui.target == InputTarget::Password);

    // WiFi 列表
    if (s_ui.scanning && s_ui.ap_count == 0) {
        canvas.draw_text(kRowX, kListY + 8, "Scanning...", 2,
                         GrayLevel::Black);
    } else if (s_ui.ap_count == 0) {
        canvas.draw_text(kRowX, kListY + 8, "No AP found", 2,
                         GrayLevel::Black);
    } else {
        const size_t shown =
            s_ui.ap_count < kListRowCount ? s_ui.ap_count : kListRowCount;
        for (size_t i = 0; i < shown; ++i) {
            const int y = kListY + static_cast<int>(i) * kListRowH;
            const bool selected = std::strcmp(s_ui.aps[i].ssid, s_ui.ssid) == 0;
            if (selected) {
                canvas.fill_rect(kRowX, y, kRowW, kListRowH - 4,
                                 GrayLevel::Black);
            }
            const GrayLevel color =
                selected ? GrayLevel::White : GrayLevel::Black;
            char line[40] = {};
            // SSID 可能很长：截断到 22 字符（22*12=264px，预留右侧 RSSI）
            std::snprintf(line, sizeof(line), "%.22s", s_ui.aps[i].ssid);
            canvas.draw_text(kRowX + 8, y + 6, line, 2, color);
            char sig[12] = {};
            std::snprintf(sig, sizeof(sig), "%d", s_ui.aps[i].rssi);
            canvas.draw_text(kRowX + kRowW - 8 - text_width(sig, 2), y + 6,
                             sig, 2, color);
        }
    }

    // 状态行（键盘下方）：当前连接状态
    if (s_ui.connected && s_ui.connected_ssid[0] != '\0') {
        char line[48] = {};
        std::snprintf(line, sizeof(line), "Connected: %s",
                      s_ui.connected_ssid);
        canvas.draw_text(kKeyX, 316, line, 2, GrayLevel::Black);
    } else {
        canvas.draw_text(kKeyX, 316, "Not connected", 2, GrayLevel::Black);
    }

    // 操作反馈（扫描网络数 / 保存结果等），放在连接状态行下方
    if (s_ui.status[0] != '\0') {
        canvas.draw_text(kKeyX, 348, s_ui.status, 2, GrayLevel::Black);
    }

    // 虚拟键盘
    draw_keyboard(canvas);

    // 提示：当前输入目标
    canvas.draw_text(kKeyX, 284, "Target:", 2, GrayLevel::Black);
    const char *target_text =
        s_ui.target == InputTarget::Ssid ? "SSID" : "Password";
    canvas.draw_text(kKeyX + 90, 284, target_text, 2,
                     s_ui.target == InputTarget::Ssid ? GrayLevel::Black
                                                      : GrayLevel::Black);

    // 右下角状态区：扫描中显示 loading 图标；HH:MM 时钟常驻
    // （右上角位置与键盘第一行重叠，所以放在右下角空白处）
    if (s_ui.scanning) {
        draw_bitmap(canvas, &kLOADING[0][0], kLoadingSize, kLoadingSize,
                    kLoadingX, kLoadingY);
    }
    wifi_config_page_render_time(canvas, state);
}

// 配网页右下角 HH:MM（独立于右上角的状态栏时间，每分钟由 ClockMinuteTick 局刷）
void wifi_config_page_render_time(Canvas &canvas, const AppState &state)
{
    canvas.fill_rect(kClockX - 8, kClockY - 4, 88, 28, GrayLevel::White);
    char time_text[6] = "--:--";
    if (state.date_time.valid) {
        std::snprintf(time_text, sizeof(time_text), "%02d:%02d",
                      state.date_time.hour, state.date_time.minute);
    }
    canvas.draw_text(kClockX, kClockY, time_text, 2, GrayLevel::Black);
}

void wifi_config_page_handle_tap(uint16_t x, uint16_t y)
{
    handle_tap(static_cast<int>(x), static_cast<int>(y));
}

void wifi_config_page_request_scan()
{
    s_scan_now = true;
}

void wifi_config_page_set_connected_ssid(const char *ssid, bool connected)
{
    s_ui.connected = connected;
    if (connected && ssid != nullptr) {
        std::snprintf(s_ui.connected_ssid, sizeof(s_ui.connected_ssid),
                      "%s", ssid);
    } else {
        s_ui.connected_ssid[0] = '\0';
    }
    ESP_LOGI(kTag, "Connection state: %s",
             connected ? "connected" : "disconnected");
    app_event_post(AppEvent::RefreshPage);
}

void wifi_config_page_handle_scan_done()
{
    // 扫描结果已写入 s_ui；正在输入（3 秒内有过点按）时跳过整页重绘，
    // 避免 ePaper 全刷打断键盘操作，列表内容在下次按键刷新时自然更新。
    const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    if (now_ms - s_ui.last_input_ms < 3000) {
        ESP_LOGI(kTag, "Scan done; input active, skipping redraw");
        return;
    }
    app_event_post(AppEvent::RefreshPage);
}

// 配网页局部刷新计数：A2 局部波形驱动多了会在屏幕上累积残影，
// 每 25 次局部刷新返回一次 true，让上层插一次整屏全刷清除残影。
bool wifi_config_page_needs_full_refresh()
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
