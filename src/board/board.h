/*
 * Hardware access for the LiteRadio 4 controller. Everything that touches a
 * pin or a peripheral register lives behind these functions; the wiring is
 * described in docs/WIRING.md.
 */

#pragma once

#include <stdint.h>

namespace board {

/* Bits of Inputs::trim_left and Inputs::trim_right. */
enum TrimDirection : uint8_t {
    TRIM_UP = 1 << 0,
    TRIM_DOWN = 1 << 1,
    TRIM_LEFT = 1 << 2,
    TRIM_RIGHT = 1 << 3,
    TRIM_CENTRE = 1 << 4,
};

/* One reading of all controls, unscaled. */
struct Inputs {
    /* Raw 12-bit values: left vertical (throttle), left horizontal, right vertical, right horizontal. */
    uint16_t sticks[4];
    /* Toggle switches: 0 or 1. */
    uint8_t sa;
    uint8_t sd;
    /* Three-position switches: 0 and 2 are the ends, 1 is the centre. */
    uint8_t sb;
    uint8_t sc;
    bool se;
    bool sf;
    bool bind;
    bool setup;
    bool power;
    /* Pressed directions as TrimDirection bits; valid only if trims_valid. */
    uint8_t trim_left;
    uint8_t trim_right;
    bool trims_valid;
};

/* Configure all pins and peripherals. Call once before anything else. */
void init();

void read_inputs(Inputs &inputs);

/* Raw 12-bit reading of the battery input. */
uint16_t battery_raw();

bool usb_supply_present();

/* High keeps the radio on after the power button is released. */
void set_power_latch(bool on);

/* Start or stop the ELRS module. */
void set_module_enabled(bool enabled);

/* Route the USB connector to this MCU (true) or to the CH9102 bridge (false). */
void set_usb_to_controller(bool controller);

/* Light the four battery LEDs; bit 0 is the leftmost LED. */
void set_battery_leds(uint8_t mask);

/* Set the colour of the status LED. */
void set_status_led(uint8_t red, uint8_t green, uint8_t blue);

/* Sound the buzzer at a frequency in hertz; 0 switches it off. */
void set_buzzer(uint32_t frequency);

} // namespace board
