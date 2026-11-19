/*
 * Colour of the status LED from the radio's state, see "LEDs" in
 * docs/ARCHITECTURE.md: one colour steady is a normal state, slow blinking
 * a special mode or a notice, fast blinking an error (red) or PC control
 * (blue). No hardware access.
 */

#pragma once

#include <stdint.h>

#include "core/mode.h"

namespace core {

/* What decides the colour. */
struct LedInputs {
    Mode mode;
    bool calibrating;        // a stick calibration run holds the output
    bool receiver_connected; // the module reported link statistics from a receiver within 1 s
};

/* Colour as 0xRRGGBB at uptime ms. */
uint32_t led_colour(const LedInputs &in, uint32_t ms);

} // namespace core
