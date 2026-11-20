#include "sleep_page.h"

#include "bus_stop_icon.h"
#include "canvas.h"
#include "font.h"

namespace {

constexpr int kIconSize = 192;
constexpr int kIconGap = 20;
constexpr int kTextScale = 4;

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

}  // namespace

void sleep_page_render(Canvas *canvas)
{
    canvas->clear(GrayLevel::White);

    // 布局：公交站台图标（用户提供的 SVG 位图）+ "Sleep..."，整体居中
    const char kLabel[] = "Sleep...";
    constexpr int kTextChars = 8;
    const int text_width =
        kTextChars * (kFontWidth + kFontSpacing) * kTextScale;
    const int total_width = kIconSize + kIconGap + text_width;
    const int block_height = kIconSize;
    const int x0 = (static_cast<int>(canvas->width()) - total_width) / 2;
    const int icon_y = (static_cast<int>(canvas->height()) - block_height) / 2;

    draw_bitmap(*canvas, &kBUS_STOP_SVG[0][0], kIconSize, kIconSize, x0,
                icon_y);

    const int text_height = 7 * kTextScale;
    canvas->draw_text(x0 + kIconSize + kIconGap,
                      icon_y + (kIconSize - text_height) / 2, kLabel,
                      kTextScale, GrayLevel::Black);
}
