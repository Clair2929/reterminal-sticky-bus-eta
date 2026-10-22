#pragma once

class Canvas;
struct AppState;

// Logical Canvas area occupied by HH:MM.
constexpr int kStatusBarTimeX = 664;
constexpr int kStatusBarTimeY = 48;
constexpr int kStatusBarTimeWidth = 80;
constexpr int kStatusBarTimeHeight = 24;

// Draws the shared hardware-independent header status information.
void status_bar_render(Canvas &canvas, const AppState &state);

// Clears and redraws only the HH:MM area.
void status_bar_render_time(Canvas &canvas, const AppState &state);
