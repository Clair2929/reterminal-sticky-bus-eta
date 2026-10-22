#include "sleep_page.h"

#include "font.h"

namespace {

void draw_bus_stop(Canvas &canvas, int x, int y)
{
    // Board, bus pictogram, pole and base make this read as a bus stop sign.
    canvas.draw_rect(x + 5, y + 3, 52, 40, GrayLevel::Black);
    canvas.draw_rect(x + 16, y + 10, 30, 22, GrayLevel::Black);
    canvas.fill_rect(x + 20, y + 14, 22, 8, GrayLevel::Black);
    canvas.fill_rect(x + 20, y + 25, 5, 4, GrayLevel::Black);
    canvas.fill_rect(x + 37, y + 25, 5, 4, GrayLevel::Black);
    canvas.fill_rect(x + 30, y + 43, 3, 31, GrayLevel::Black);
    canvas.fill_rect(x + 21, y + 72, 21, 3, GrayLevel::Black);
}

}  // namespace

void sleep_page_render(Canvas *canvas)
{
    canvas->clear(GrayLevel::White);

    // 布局：公交站牌图标 + "Sleep..."，整体水平居中
    constexpr int kIconWidth = 64;
    constexpr int kIconY = 202;
    constexpr int kGap = 16;
    constexpr int kTextScale = 3;
    constexpr int kTextWidth = 8 * (kFontWidth + kFontSpacing) * kTextScale;
    constexpr int kTextY = 230;
    constexpr int kTotalWidth = kIconWidth + kGap + kTextWidth;
    const int x0 = (static_cast<int>(canvas->width()) - kTotalWidth) / 2;

    draw_bus_stop(*canvas, x0, kIconY);
    canvas->draw_text(x0 + kIconWidth + kGap, kTextY, "Sleep...", kTextScale,
                      GrayLevel::Black);
}
