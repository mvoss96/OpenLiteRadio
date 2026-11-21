/*
 * Xbox 360 controller report (XInput) from the channels. Each element of the
 * controller follows one channel or none, as the settings xbox.* say. No
 * hardware access; src/usb_xinput.c sends the report.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace xbox {

/* Message type, length, buttons, two triggers, four stick axes, six unused bytes. */
constexpr size_t REPORT_SIZE = 20;

/* Button bits in the report. */
enum Button : uint16_t {
    BUTTON_START = 1u << 4,   // Start
    BUTTON_BACK = 1u << 5,    // Back
    BUTTON_LB = 1u << 8,      // left shoulder button
    BUTTON_RB = 1u << 9,      // right shoulder button
    BUTTON_A = 1u << 12,      // A
    BUTTON_B = 1u << 13,      // B
    BUTTON_X = 1u << 14,      // X
    BUTTON_Y = 1u << 15,      // Y
};

/* The elements that can follow a channel, in the order of the settings xbox.*. */
enum Element {
    ELEMENT_LEFT_X,         // left stick, right positive
    ELEMENT_LEFT_Y,         // left stick, up positive
    ELEMENT_RIGHT_X,        // right stick, right positive
    ELEMENT_RIGHT_Y,        // right stick, up positive
    ELEMENT_LEFT_TRIGGER,   // LT
    ELEMENT_RIGHT_TRIGGER,  // RT
    ELEMENT_A,              // button A
    ELEMENT_B,              // button B
    ELEMENT_X,              // button X
    ELEMENT_Y,              // button Y
    ELEMENT_LB,             // left shoulder button
    ELEMENT_RB,             // right shoulder button
    ELEMENT_BACK,           // button Back
    ELEMENT_START,          // button Start
    ELEMENT_COUNT,          // number of elements
};

/*
 * Fill the report from 16 channel outputs in microseconds (988 to 2012).
 * assignment holds ELEMENT_COUNT values, one per Element: the channel it
 * follows, 1 to 16, or 0 for none; an element without a channel stays at
 * rest. A stick axis maps 988..2012 to -32767..32767, up and right positive
 * as XInput counts them; a trigger maps 988..2012 to 0..255; a button is
 * pressed above 1500.
 */
void fill_report(const uint16_t channels[16], const int16_t assignment[ELEMENT_COUNT],
                 uint8_t report[REPORT_SIZE]);

} // namespace xbox
