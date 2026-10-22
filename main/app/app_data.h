#pragma once

struct AppState;

// Device-to-state adapters. Hardware errors are stored in AppState so pages
// can render a stable fallback such as N/A instead of handling hardware.
void update_date_time(AppState &state);
void update_environment(AppState &state);
void update_battery(AppState &state);
void update_bus_eta(AppState &state);
