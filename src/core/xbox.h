/*
 * Xbox 360 controller report (XInput) from the channels, with a fixed
 * assignment: left stick channels 4 and 3, right stick channels 1 and 2,
 * triggers channels 5 and 6, buttons A, B, X, Y, LB, RB, Back, Start
 * channels 9 to 16. No hardware access; src/usb_xinput.c sends the report.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace xbox {

/* Message type, length, buttons, two triggers, four stick axes, six unused bytes. */
constexpr size_t REPORT_SIZE = 20;

/* Button bits in the report. */
enum Button : uint16_t {
    BUTTON_START = 1u << 4,   // channel 16
    BUTTON_BACK = 1u << 5,    // channel 15
    BUTTON_LB = 1u << 8,      // channel 13
    BUTTON_RB = 1u << 9,      // channel 14
    BUTTON_A = 1u << 12,      // channel 9
    BUTTON_B = 1u << 13,      // channel 10
    BUTTON_X = 1u << 14,      // channel 11
    BUTTON_Y = 1u << 15,      // channel 12
};

/*
 * Fill the report from 16 channel outputs in microseconds (988 to 2012). A
 * stick axis maps 988..2012 to -32767..32767, up and right positive as XInput
 * counts them; a trigger maps 988..2012 to 0..255; a button is pressed above
 * 1500.
 */
void fill_report(const uint16_t channels[16], uint8_t report[REPORT_SIZE]);

} // namespace xbox
