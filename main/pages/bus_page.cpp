#include "bus_page.h"

#include <cstdio>
#include <cstring>

#include "app_state.h"
#include "canvas.h"
#include "font.h"
#include "icons.h"

namespace {

constexpr char kWeekdayNames[7][4] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};

// 计算 5x7 字体在指定 scale 下的文本像素宽度（含字符间距）
int text_pixel_width(const char *text, int scale)
{
    return static_cast<int>(std::strlen(text)) * (kFontWidth + kFontSpacing) * scale;
}

// 把 1-bit 位图（每行打包，MSB 在前）画到 Canvas 上
void draw_bitmap(Canvas &canvas,
                 const uint8_t *bitmap,
                 int width,
                 int height,
                 int x,
                 int y)
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

// 公交车图标（SVG 位图，200x200，替代原矩形近似）
void draw_bus_icon(Canvas &canvas, int x, int y)
{
    draw_bitmap(canvas, &kBUS[0][0], 200, 200, x, y);
}

// WiFi 状态图标：电池左侧，满格 = 已连接，信号柱 = 未连接
void draw_wifi_icon(Canvas &canvas, const AppState &state)
{
    constexpr int kX = 640;
    constexpr int kY = 10;
    constexpr int kSize = 32;

    if (state.bus_eta.wifi_connected) {
        draw_bitmap(canvas, &kWIFI_OK[0][0], kSize, kSize, kX, kY);
    } else {
        draw_bitmap(canvas, &kWIFI_NO[0][0], kSize, kSize, kX, kY);
    }
}

// 电池图标 + 百分比（移植自 HelloWorld 的 drawBatteryStatus）
void draw_battery_status(Canvas &canvas, const AppState &state)
{
    constexpr int kX = 744;
    constexpr int kY = 16;
    constexpr int kWidth = 42;
    constexpr int kHeight = 19;

    canvas.draw_rect(kX, kY, kWidth, kHeight, GrayLevel::Black);
    canvas.fill_rect(kX + kWidth, kY + 6, 4, 7, GrayLevel::Black);

    int percent = 0;
    if (state.battery.valid) {
        percent = state.battery.percent;
        if (percent < 0) {
            percent = 0;
        } else if (percent > 100) {
            percent = 100;
        }
    }
    const int fill_width = percent * (kWidth - 6) / 100;
    if (fill_width > 0) {
        canvas.fill_rect(kX + 3, kY + 3, fill_width, kHeight - 6,
                         GrayLevel::Black);
    }

    char text[12] = "--%";
    if (state.battery.valid) {
        std::snprintf(text, sizeof(text), "%d%%", percent);
    }
    canvas.draw_text(kX - 8 - text_pixel_width(text, 2), kY + 2, text, 2,
                     GrayLevel::Black);
}

// 定位图标 + 站名自动换行（站名整体右移到图标右侧）
void draw_stop_name(Canvas &canvas, const char *name)
{
    constexpr int kPinX = 32;
    constexpr int kPinY = 196;
    constexpr int kPinSize = 56;
    constexpr int kX = 96;
    // 站名下边框与定位图标下边框对齐：图标底边 = 196+56 = 252，
    // 5x7 字体 scale4 字高 = 28px -> kY = 252-28 = 224；视觉上略低，微调上移
    constexpr int kY = 222;
    constexpr int kMaxWidth = 274;
    constexpr int kLineHeight = 36;
    constexpr int kMaxLines = 2;
    constexpr int kScale = 4;
    constexpr int kCharStep = (kFontWidth + kFontSpacing) * kScale;

    draw_bitmap(canvas, &kPIN[0][0], kPinSize, kPinSize, kPinX, kPinY);

    const size_t length = std::strlen(name);
    size_t index = 0;
    for (int line = 0; line < kMaxLines && index < length; ++line) {
        size_t line_end = index;
        int width = 0;
        while (line_end < length && width + kCharStep <= kMaxWidth) {
            width += kCharStep;
            ++line_end;
        }
        if (line_end == index) {
            line_end = index + 1;  // 超长单词强制截断，避免死循环
        }

        char line_text[64] = {};
        const size_t copy_len = line_end - index;
        std::memcpy(line_text, name + index, copy_len);
        if (line == kMaxLines - 1 && line_end < length) {
            std::snprintf(line_text + copy_len,
                          sizeof(line_text) - copy_len, "...");
        }
        canvas.draw_text(kX, kY + line * kLineHeight, line_text, kScale,
                         GrayLevel::Black);
        index = line_end;
    }
}

}  // namespace

void bus_page_render(Canvas &canvas, const AppState &state)
{
    canvas.clear(GrayLevel::White);

    // --- 左列：大时钟 + 星期/日期 + 站名 + 公交车图标 ---
    bus_page_render_clock(canvas, state);

    char date_line[40] = "Time N/A";
    if (state.date_time.valid) {
        const int weekday = state.date_time.weekday;
        std::snprintf(date_line, sizeof(date_line), "%s%s  %04d-%02d-%02d",
                      state.date_time.rtc_valid ? "" : "NTP  ",
                      (weekday >= 0 && weekday < 7) ? kWeekdayNames[weekday] : "---",
                      state.date_time.year, state.date_time.month,
                      state.date_time.day);
    }
    canvas.draw_text(40, 120, date_line, 3, GrayLevel::Black);

    draw_stop_name(canvas, state.bus_eta.stop_name);
    draw_bus_icon(canvas, 40, 260);

    // --- 右列：标题 + 电池 + WiFi 图标 + 到站列表 ---
    canvas.draw_text(400, 20, "Bus ETA", 3, GrayLevel::Black);
    bus_page_render_status_area(canvas, state);

    // 线路列表紧贴标题下方（原 y=100 会与 "Bus ETA" 之间空一行）
    int row_y = 60;
    if (state.bus_eta.bus_count == 0) {
        const char *hint =
            state.bus_eta.wifi_connected ? state.bus_eta.status : "No WiFi";
        canvas.draw_text(400, row_y, hint[0] != '\0' ? hint : "Loading...",
                         3, GrayLevel::Black);
    } else {
        const size_t shown =
            state.bus_eta.bus_count < 9 ? state.bus_eta.bus_count : 9;
        for (size_t i = 0; i < shown; ++i) {
            const BusArrival &bus = state.bus_eta.buses[i];

            // 线路号徽章（放大：76x38）
            canvas.fill_rect(400, row_y, 76, 38, GrayLevel::Black);
            const int badge_text_x =
                400 + (76 - text_pixel_width(bus.route, 3)) / 2;
            canvas.draw_text(badge_text_x, row_y + 6, bus.route, 3,
                             GrayLevel::White);

            // 到站时刻 + 分钟数（放大到 3x）
            char arrival_text[40] = {};
            if (state.date_time.rtc_valid) {
                std::snprintf(arrival_text, sizeof(arrival_text), "%s  %dmin",
                              bus.arrival, bus.eta_minutes);
            } else {
                // Keep stop and route information useful when the RTC cannot
                // provide trusted time, but do not show a misleading countdown.
                std::snprintf(arrival_text, sizeof(arrival_text), "%s",
                              bus.arrival);
            }
            canvas.draw_text(485, row_y + 6, arrival_text, 3,
                             GrayLevel::Black);

            row_y += 46;
        }
    }
}

void bus_page_render_clock(Canvas &canvas, const AppState &state)
{
    // 时钟区域：scale=8 时 5x7 字体为 40x56，5 个字符含间距约 240 宽
    constexpr int kClockX = 40;
    constexpr int kClockY = 30;
    constexpr int kClockWidth = 250;
    constexpr int kClockHeight = 60;

    // 先擦除旧时间再画新时间（与 status_bar 的做法一致）
    canvas.fill_rect(kClockX, kClockY, kClockWidth, kClockHeight,
                     GrayLevel::White);

    char clock_text[6] = "--:--";
    if (state.date_time.valid) {
        std::snprintf(clock_text, sizeof(clock_text), "%02d:%02d",
                      state.date_time.hour, state.date_time.minute);
    }
    canvas.draw_text(kClockX, kClockY, clock_text, 8, GrayLevel::Black);
}

void bus_page_render_status_area(Canvas &canvas, const AppState &state)
{
    // 右上角状态区：WiFi 图标 + 电池图标 + 百分比（顶右排列）。
    // 先整体擦白再重画，供分钟局部刷新和电池补读复用。
    constexpr int kAreaX = 628;
    constexpr int kAreaY = 6;
    constexpr int kAreaWidth = 164;
    constexpr int kAreaHeight = 40;

    canvas.fill_rect(kAreaX, kAreaY, kAreaWidth, kAreaHeight,
                     GrayLevel::White);
    draw_wifi_icon(canvas, state);
    draw_battery_status(canvas, state);
}
