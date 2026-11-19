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

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include "board/board.h"
#include "calibration.h"
#include "core/channels.h"
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
/* The trim was changed with the trim buttons and is not stored yet. */
static bool trim_unsaved;

static void set_radio_on(bool on)
{
    radio_on = on;
    module_link::set_module_on(on);
    /*
     * Writing to flash holds the output, which locks it, so the trim is only
     * stored here. The power button is still held, which keeps the radio
     * powered until the latch is released.
     */
    if (!on && trim_unsaved) {
        trim_unsaved = !storage::save_trim();
    }
    board::set_power_latch(on);
    indicators::beep(on ? 2700 : 1800, 150);
}

/*
 * Trim buttons, one step per press while the radio is on: left button up and
 * down trim the throttle, left and right the yaw; the right button pitch and
 * roll. A high tone for a step away from the centre, a low one towards it,
 * a long one at the centre and a long low one at ±25. The centre presses do
 * nothing here.
 */
static void trim_tick(uint8_t left, uint8_t right)
{
    static uint8_t previous_left;
    static uint8_t previous_right;
    uint8_t pressed_left = left & ~previous_left;
    uint8_t pressed_right = right & ~previous_right;
    struct {
        uint8_t pressed; /* newly pressed directions of the button */
        uint8_t plus;    /* direction towards 2012 */
        uint8_t minus;   /* direction towards 988 */
        core::Axis axis;
    } const keys[] = {
        { pressed_left, board::TRIM_UP, board::TRIM_DOWN, core::AXIS_LEFT_VERTICAL },
        { pressed_left, board::TRIM_RIGHT, board::TRIM_LEFT, core::AXIS_LEFT_HORIZONTAL },
        { pressed_right, board::TRIM_UP, board::TRIM_DOWN, core::AXIS_RIGHT_VERTICAL },
        { pressed_right, board::TRIM_RIGHT, board::TRIM_LEFT, core::AXIS_RIGHT_HORIZONTAL },
    };

    previous_left = left;
    previous_right = right;
    if (!radio_on) {
        return;
    }
    for (const auto &key : keys) {
        int step = ((key.pressed & key.plus) ? 1 : 0) - ((key.pressed & key.minus) ? 1 : 0);
        int index = settings::TRIM_FIRST + (int)key.axis;

        if (step == 0) {
            continue;
        }
        int before = settings::values[index];

        /* set() refuses a step beyond the end; the value then stays at the end. */
        if (settings::set(index, before + step)) {
            trim_unsaved = true;
        }
        int value = settings::values[index];

        /* The stock firmware's tones, plus one for the end, which it does not have. */
        if (value == 0) {
            indicators::beep(989, 300); /* B5 */
        } else if (value == settings::info(index).min || value == settings::info(index).max) {
            indicators::beep(659, 300); /* E5, also for every press beyond the end */
        } else if (abs(value) > abs(before)) {
            indicators::beep(1176, 100); /* D6, away from the centre */
        } else {
            indicators::beep(784, 100); /* G5, towards the centre */
        }
    }
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
        trim_tick(inputs.trim_left, inputs.trim_right);

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
                /* A trim centre held while switching on: left Bluetooth joystick, right WiFi. */
                if (inputs.trim_left & board::TRIM_CENTRE) {
                    module_link::command_start(module_link::COMMAND_BLUETOOTH);
                } else if (inputs.trim_right & board::TRIM_CENTRE) {
                    module_link::command_start(module_link::COMMAND_WIFI);
                }
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
        /* While WiFi or Bluetooth waits for the module, the LED shows it already; the output stays locked. */
        core::Mode shown = snapshot.mode;

        if (module_link::starting_task() == core::ModuleTask::WIFI) {
            shown = core::Mode::WIFI;
        } else if (module_link::starting_task() == core::ModuleTask::BLUETOOTH) {
            shown = core::Mode::BLUETOOTH;
        }
        indicators::tick({ shown, calibration::running(), module_link::receiver_connected() });

        /* BIND held for 1 s starts binding, a short press ends it. */
        if (inputs.bind) {
            if (++bind_held_loops == BIND_HOLD_LOOPS) {
                module_link::command_start(module_link::COMMAND_BIND);
            }
        } else {
            if (bind_held_loops > 0 && bind_held_loops < BIND_HOLD_LOOPS) {
                module_link::bind_cancel();
            }
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
