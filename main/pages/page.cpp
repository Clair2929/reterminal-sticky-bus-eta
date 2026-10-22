#include "page.h"

#include "app_state.h"
#include "canvas.h"
#include "status_bar.h"

void page_draw_layout(Canvas &canvas,
                      const AppState &state,
                      const char *title,
                      const char *subtitle)
{
    canvas.clear(GrayLevel::White);

    // Use one shared frame so every dashboard page has a consistent layout.
    canvas.draw_rect(28, 28, 744, 424, GrayLevel::Black);
    canvas.draw_rect(36, 36, 728, 408, GrayLevel::Black);
    status_bar_render(canvas, state);
    canvas.draw_text(70, 78, title, 4, GrayLevel::Black);
    canvas.draw_line(70, 120, 730, 120, GrayLevel::Black);
    canvas.draw_text(72, 150, subtitle, 3, GrayLevel::Black);
}
