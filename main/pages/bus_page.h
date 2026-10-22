#pragma once

#include "page.h"

// Bus ETA dashboard page: big clock + date + stop name + arrival rows.
void bus_page_render(Canvas &canvas, const AppState &state);

// Redraws only the big clock area (for the per-minute partial refresh).
void bus_page_render_clock(Canvas &canvas, const AppState &state);

// Redraws the top-right status area (WiFi icon + battery icon + percent).
// Used by the per-minute partial refresh so battery level updates in place.
void bus_page_render_status_area(Canvas &canvas, const AppState &state);
