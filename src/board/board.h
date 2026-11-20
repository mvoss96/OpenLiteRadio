/*
 * Hardware access for the LiteRadio 4 controller. Everything that touches a
 * pin or a peripheral register lives behind these functions; the wiring is
 * described in docs/WIRING.md.
 */

#pragma once

#include <stddef.h>
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

/* Read sticks, switches and buttons. Fast enough for the control thread. */
void read_controls(Inputs &inputs);

/* Read the two trim buttons; takes about 0.5 ms. */
void read_trims(Inputs &inputs);

/*
 * Read a settings record from the settings area at the top of the application
 * flash. Returns false if the record does not exist or has another size.
 */
bool storage_read(uint16_t id, void *data, size_t size);

/*
 * Store a settings record. The processor stands still for up to 40 ms while a
 * flash page is erased, so call this only while no channel frames are sent.
 */
bool storage_write(uint16_t id, const void *data, size_t size);

/* Remove a settings record; true also if it did not exist. Same timing as storage_write. */
bool storage_delete(uint16_t id);

/*
 * Queue bytes for the ELRS module; they are sent by interrupt. Returns false
 * and sends nothing if the previous bytes are still going out or the length
 * exceeds 64.
 */
bool module_send(const uint8_t *data, size_t length);

/* Take received bytes from the ELRS module; returns how many were copied. */
size_t module_receive(uint8_t *data, size_t capacity);

/* Raw 12-bit reading of the battery input. */
uint16_t battery_raw();

bool usb_supply_present();

/* High keeps the radio on after the power button is released. */
void set_power_latch(bool on);

/* Start or stop the ELRS module. */
void set_module_enabled(bool enabled);

/*
 * Shift register output 0x10, high by default. The stock firmware sets it low
 * for 100 ms before it reads the battery with USB supply. Hypothesis: it
 * enables the charger, so that the reading is taken without charge current.
 */
void set_charger_enabled(bool enabled);

/* Route the USB connector to this MCU (true) or to the CH9102 bridge (false). */
void set_usb_to_controller(bool controller);

/*
 * Make the host see the USB device unplugged: D+ (PA12) low for 20 ms. The
 * board ties D+ to its pull-up, so stopping the USB peripheral alone leaves
 * the device on the bus. Only while the USB peripheral is shut down.
 */
void signal_usb_disconnect();

/* Light the four battery LEDs; bit 0 is the leftmost LED. */
void set_battery_leds(uint8_t mask);

/* Set the colour of the status LED. */
void set_status_led(uint8_t red, uint8_t green, uint8_t blue);

/*
 * Sound the buzzer at a frequency in hertz with a duty cycle in percent,
 * which sets the loudness; frequency or duty 0 switches it off.
 */
void set_buzzer(uint32_t frequency, uint32_t duty_percent);

} // namespace board

/* board::signal_usb_disconnect() for src/usb_device.c, which is C. */
extern "C" void board_signal_usb_disconnect(void);
