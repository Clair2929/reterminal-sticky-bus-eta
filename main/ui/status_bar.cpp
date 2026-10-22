#include "status_bar.h"

#include <cstdio>

#include "app_state.h"
#include "canvas.h"

void status_bar_render_time(Canvas &canvas, const AppState &state)
{
    canvas.fill_rect(kStatusBarTimeX,
                     kStatusBarTimeY,
                     kStatusBarTimeWidth,
                     kStatusBarTimeHeight,
                     GrayLevel::White);

    char time_text[6] = "--:--";
    if (state.date_time.valid) {
        std::snprintf(time_text, sizeof(time_text), "%02d:%02d",
                      state.date_time.hour, state.date_time.minute);
    }
    canvas.draw_text(670, 52, time_text, 2, GrayLevel::Black);
}

void status_bar_render(Canvas &canvas, const AppState &state)
{
    status_bar_render_time(canvas, state);
}
