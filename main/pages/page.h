#pragma once

class Canvas;
struct AppState;

// All pages share this small, hardware-independent render signature.
using PageRenderer = void (*)(Canvas &canvas, const AppState &state);

// Draws the shared Display Basics frame, title, divider, and subtitle.
void page_draw_layout(Canvas &canvas,
                      const AppState &state,
                      const char *title,
                      const char *subtitle);
