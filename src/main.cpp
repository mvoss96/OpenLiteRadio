/*
 * OpenLiteRadio firmware: power handling, controls and indicators, channel
 * frames to the ELRS module, a line protocol on the USB serial port for the
 * configurator in web/, and a USB joystick.
 *
 * Two threads: the control thread (output.cpp) reads sticks and switches and
 * sends one channel frame per frame period of the module; the main thread
 * (this file) does everything else every 20 ms.
 *
 *   output.cpp       control thread, holds, throttle lock, test inputs
 *   module_link.cpp  frames from the module, link statistics, binding
 *   host.cpp         line protocol to the PC, state lines
 *   storage.cpp      settings in flash
 *   calibration.cpp  stick calibration run
 *   indicators.cpp   buzzer and status LED
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include "board/board.h"
#include "calibration.h"
#include "core/settings.h"
#include "host.h"
#include "indicators.h"
#include "module_link.h"
#include "output.h"
#include "storage.h"

extern "C" void updater_enter(void);

/* Baud rate a host sets on the serial port to request the vendor USB updater. */
constexpr uint32_t UPDATER_REQUEST_BAUD = 1200;

constexpr int LOOP_MS = 20;
constexpr int STATE_EVERY_LOOPS = 2;   /* 25 state lines per second */
constexpr int POWER_ON_LOOPS = 50;     /* power button held for 1 s */
constexpr int POWER_OFF_LOOPS = 100;   /* power button held for 2 s */
constexpr int BIND_HOLD_LOOPS = 50;    /* BIND held for 1 s */
constexpr int SETUP_HOLD_LOOPS = 100;  /* SETUP held for 2 s */

static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static bool radio_on;

static void set_radio_on(bool on)
{
    radio_on = on;
    board::set_power_latch(on);
    module_link::set_module_on(on);
    indicators::beep(on ? 2700 : 1800, 150);
}

int main(void)
{
    output::Snapshot snapshot = {};
    int power_held_loops = 0;
    bool power_handled = false;
    uint32_t loops = 0;
    bool was_locked = true;
    int bind_held_loops = 0;
    int setup_held_loops = 0;

    board::init();
    storage::load_settings();
    host::print_hello();
    output::start();

    while (true) {
        output::snapshot(snapshot);
        board::Inputs &inputs = snapshot.inputs;

        board::read_trims(inputs);

        /*
         * Power button: hold 1 s to switch on, 2 s to switch off. Started from
         * battery, the radio loses power again if the button is released
         * before the latch is set.
         */
        if (inputs.power) {
            power_held_loops++;
            if (!power_handled && !radio_on && power_held_loops >= POWER_ON_LOOPS) {
                set_radio_on(true);
                power_handled = true;
            } else if (!power_handled && radio_on && power_held_loops >= POWER_OFF_LOOPS) {
                set_radio_on(false);
                power_handled = true;
            }
        } else {
            power_held_loops = 0;
            power_handled = false;
        }

        /* One tone when the throttle lock is released. */
        bool module_is_on = output::module_is_on();

        if (module_is_on && was_locked && !snapshot.locked &&
            settings::values[settings::UNLOCK_BEEP] != 0) {
            indicators::beep(2700, 100);
        }
        was_locked = snapshot.locked;
        indicators::tick({ snapshot.mode, calibration::running(), module_link::receiver_connected() });

        /* BIND held for 1 s starts binding. */
        if (inputs.bind) {
            if (++bind_held_loops == BIND_HOLD_LOOPS) {
                module_link::bind_start();
            }
        } else {
            bind_held_loops = 0;
        }

        /* SETUP held for 2 s starts a calibration run, a short press ends it. */
        if (calibration::running()) {
            calibration::track(inputs.sticks);
        }
        if (inputs.setup) {
            if (++setup_held_loops == SETUP_HOLD_LOOPS) {
                calibration::start();
            }
        } else {
            if (calibration::running() && setup_held_loops > 0 &&
                setup_held_loops < SETUP_HOLD_LOOPS) {
                calibration::finish(inputs.sticks);
            }
            setup_held_loops = 0;
        }

        module_link::tick();
        host::tick();
        if (host::streaming() && loops % STATE_EVERY_LOOPS == 0) {
            host::print_state(snapshot, radio_on);
        }
        loops++;

        uint32_t baud = 0;

        uart_line_ctrl_get(console, UART_LINE_CTRL_BAUD_RATE, &baud);
        if (baud == UPDATER_REQUEST_BAUD) {
            updater_enter();
        }
        k_msleep(LOOP_MS);
    }
}
