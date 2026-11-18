/*
 * USB joystick report: channels 1 to 8 as axes, channels 9 to 16 as buttons.
 * No hardware access; src/usb_device.c sends the report.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace joystick {

/* One byte of buttons, then eight axes of 16 bits, little endian. */
constexpr size_t REPORT_SIZE = 1 + 8 * 2;
constexpr uint16_t AXIS_MAX = 2047;

/*
 * Fill the report from 16 channel outputs in microseconds (988 to 2012).
 * An axis maps 988..2012 to 0..AXIS_MAX; a button is pressed above 1500.
 */
void fill_report(const uint16_t channels[16], uint8_t report[REPORT_SIZE]);

} // namespace joystick
