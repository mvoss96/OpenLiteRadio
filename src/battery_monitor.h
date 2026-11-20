/* Battery readings every 15 s, and the four battery LEDs. */

#pragma once

#include <stdint.h>

namespace battery_monitor {

/*
 * Call every loop. Takes a reading at the first call and every 15 s after;
 * with USB supply output 0x10 is set low for 100 ms first, as the stock
 * firmware does. Returns true when a new reading was taken.
 */
bool tick(int64_t now_ms);

/* The last reading, raw ADC counts; 0 before the first one. */
uint16_t raw();

/* Light the battery LEDs (bit 0 leftmost); ignored while the host sets them. */
void show(uint8_t mask);

/* Let the host set the battery LEDs, or give them back to the firmware. */
void set_from_host(bool from_host, uint8_t mask);

} // namespace battery_monitor
