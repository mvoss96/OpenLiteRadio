/*
 * What the four battery LEDs show: the charge level, charging, a weak battery,
 * and the animations while the power button is held. No hardware access.
 *
 * Masks have bit 0 for the leftmost LED, as board::set_battery_leds() takes
 * them. The thresholds are raw ADC counts from a full discharge with the ELRS
 * module on (docs/HARDWARE.md): four, three and two LEDs down to 75, 50 and
 * 25 % of the running time left, one LED down to 15 %.
 */

#pragma once

#include <stdint.h>

namespace battery {

/* Lowest raw reading for one, two, three and four LEDs. */
constexpr uint16_t LEVEL_THRESHOLDS[4] = { 3350, 3415, 3517, 3730 };
/* From this reading on, with USB supply, the battery counts as charged. */
constexpr uint16_t FULL = 4070;

/*
 * The ADC measures against the 3.3 V supply, which falls with the battery
 * below about 3.4 V; the battery reading then stops falling. The internal
 * reference reading rises by the same factor, which corrects the battery.
 * From REGULATED_FROM on the supply is regulated and the reference reading
 * is taken as the regulated one; VREFINT_DEFAULT stands in until then.
 */
constexpr uint16_t REGULATED_FROM = 3600;
constexpr uint16_t VREFINT_DEFAULT = 1505;

/* The battery reading as with a regulated supply; unchanged without a reference reading. */
uint16_t compensate(uint16_t raw, uint16_t vrefint, uint16_t vrefint_regulated);

/* Power button held while off: one more LED every step, switched on after ON_HOLD_MS. */
constexpr uint32_t HOLD_STEP_MS = 250;
constexpr uint32_t ON_HOLD_MS = 1000;
/* Power button held while on: from HOLD_STEP_MS on one LED less every step, off after OFF_HOLD_MS. */
constexpr uint32_t OFF_HOLD_MS = 1250;
/* Short press while off: the level blinks three times. */
constexpr uint32_t PEEK_MS = 600;
/* Half period of the blinking LED while charging and while the battery is weak. */
constexpr uint32_t BLINK_MS = 500;

/* Number of LEDs for a reading, 0 to 4. */
int level(uint16_t raw);

/* True when the battery is weak: below one LED and no USB supply. */
bool weak(uint16_t raw, bool usb);

/*
 * The LEDs while nothing is held: the level; with USB supply its highest LED
 * blinks until the battery is charged, then all four are lit; a weak battery
 * blinks the leftmost LED.
 */
uint8_t bar(uint16_t raw, bool usb, uint32_t now_ms);

/* The LEDs while the power button has been held for held_ms with the radio off: they fill from the left. */
uint8_t switching_on(uint32_t held_ms);

/*
 * The LEDs while the power button has been held for held_ms with the radio
 * on: shown is what was lit before; from HOLD_STEP_MS on they empty from the right.
 */
uint8_t switching_off(uint32_t held_ms, uint8_t shown);

/* The LEDs since_ms after a short press with the radio off: the level, blinking three times. */
uint8_t peek(uint16_t raw, uint32_t since_ms);

/* Lowest link quality in percent for one, two, three and four LEDs. */
constexpr uint8_t LINK_THRESHOLDS[4] = { 1, 50, 70, 90 };

/* The LEDs for the link quality, lit from the left; all dark without a receiver. */
uint8_t link_quality(bool connected, uint8_t quality);

} // namespace battery
