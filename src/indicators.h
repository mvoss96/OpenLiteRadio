/* Buzzer and status LED. */

#pragma once

#include <stdint.h>

#include "core/led.h"

namespace indicators {

/* Tone; nothing if the buzzer is switched off (buzzer.on). */
void beep(uint32_t frequency, int milliseconds);

/* Let the host set the LED colour, or give it back to the firmware. */
void set_led_from_host(bool from_host, uint8_t red, uint8_t green, uint8_t blue);

/* Call every loop: ends a tone and shows the radio's state on the LED (core::led_colour). */
void tick(const core::LedInputs &state);

} // namespace indicators
