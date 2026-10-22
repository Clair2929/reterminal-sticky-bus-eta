#pragma once

#include <cstdint>

// Logs whether this boot followed deep sleep.
void sticky_power_log_wakeup_reason();

// Called after wakeup to release the AI button GPIO's deep-sleep hold so the
// button driver can accept a short press and refresh the Bus page.
void sticky_power_prepare_after_wakeup();

// Waits for the AI button release, configures it as the wake source, preserves
// the board power latch, and enters deep sleep. This function does not return.
[[noreturn]] void sticky_power_enter_deep_sleep();
