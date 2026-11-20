/*
 * OpenLiteRadio firmware: power handling, controls and indicators, channel
 * frames to the ELRS module, a line protocol on the USB serial port for the
 * configurator in web/, and a USB joystick or Xbox controller.
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
 *   battery_monitor.cpp  battery readings and the battery LEDs
 */


#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "battery_monitor.h"
#include "board/board.h"
#include "calibration.h"
#include "core/battery.h"
#include "core/channels.h"
#include "core/link_alarm.h"
#include "core/settings.h"
#include "host.h"
#include "indicators.h"
#include "module_link.h"
#include "output.h"
#include "storage.h"
#include "usb_device.h"

extern "C" void updater_enter(void);

/* Baud rate a host sets on the serial port to request the vendor USB updater. */
constexpr uint32_t UPDATER_REQUEST_BAUD = 1200;

constexpr int LOOP_MS = 20;
constexpr int STATE_EVERY_LOOPS = 2;   /* 25 state lines per second */
constexpr int POWER_ON_LOOPS = battery::ON_HOLD_MS / LOOP_MS;                   /* 1 s */
constexpr int POWER_OFF_LOOPS = (battery::OFF_HOLD_MS + LOOP_MS - 1) / LOOP_MS;  /* 1.25 s */
/* After switching on the battery LEDs stay dark this long, as in the stock firmware. */
constexpr int64_t BAR_AFTER_ON_MS = 500;
constexpr int BIND_HOLD_LOOPS = 50;    /* BIND held for 1 s */
constexpr int SETUP_HOLD_LOOPS = 100;  /* SETUP held for 2 s */
constexpr int USB_SWITCH_LOOPS = 100;  /* both trim centres held for 2 s */
constexpr int TRIM_RESET_LOOPS = 50;   /* a trim centre held for 1 s */

static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static bool radio_on;
/* The trim was changed with the trim buttons and is not stored yet. */
static bool trim_unsaved;
/* Switched off: the power latch is released once the power-off tune has ended. */
static bool latch_release_pending;
/* Uptime when the radio was switched on. */
static int64_t on_since;
/* The battery LEDs show the link quality instead of the battery; SETUP pressed briefly switches. */
static bool show_link_quality;

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
    if (on) {
        board::set_power_latch(true);
    }
    latch_release_pending = !on;
    show_link_quality = false;
    on_since = k_uptime_get();
    indicators::play_event(on ? settings::SOUND_POWER_ON : settings::SOUND_POWER_OFF);
}

/*
 * The USB device's form: only the serial port while the ELRS module is on
 * (radio on, or the host command "module 1"), else the controller chosen in
 * usb.mode. With the module off, both trim centres held for 2 s switch
 * between joystick and Xbox controller and store it. Each change of the
 * form makes the device enumerate again.
 */
static void usb_tick(const board::Inputs &inputs)
{
    static int centres_held_loops;
    static int requested = -1; /* enum usb_function asked for last, -1 before the first */
    bool both = (inputs.trim_left & board::TRIM_CENTRE) && (inputs.trim_right & board::TRIM_CENTRE);
    bool module_on = output::module_is_on();

    if (!module_on && both && !inputs.power) {
        if (++centres_held_loops == USB_SWITCH_LOOPS) {
            settings::set(settings::USB_MODE, settings::values[settings::USB_MODE] == 0 ? 1 : 0);
            storage::save_setting(settings::USB_MODE);
            indicators::play_event(settings::SOUND_ACCEPTED);
        }
    } else {
        centres_held_loops = 0;
    }

    usb_function wanted = module_on ? USB_SERIAL_ONLY
                          : settings::values[settings::USB_MODE] != 0 ? USB_XBOX
                                                                     : USB_JOYSTICK;

    if (wanted != requested) {
        requested = wanted;
        int err = usb_device_select(wanted);

        if (err != 0) {
            /* The device fell back to the serial port only; tell the host and the user. */
            k_msleep(1500);
            printk("error usb %d %d\n", (int)wanted, err);
            indicators::play_event(settings::SOUND_ERROR);
        }
    }
}

/*
 * Trim buttons, one step per press while the radio is on: left button up and
 * down trim the throttle, left and right the yaw; the right button pitch and
 * roll. Each step, also a press beyond the end, gives the tone of sound.trim
 * and the LED flash of trim.led. A centre held for 1 s resets both axes of
 * its button; it counts only once released after switching on, since a
 * centre held at switch-on starts Bluetooth or WiFi.
 */
static void trim_tick(uint8_t left, uint8_t right)
{
    static uint8_t previous_left;
    static uint8_t previous_right;
    static bool centre_armed[2];   /* the centre was released while the radio is on */
    static int centre_held_loops[2];
    const uint8_t buttons[2] = { left, right };
    /* Axes of each button: left throttle and yaw, right pitch and roll. */
    const core::Axis button_axes[2][2] = {
        { core::AXIS_LEFT_VERTICAL, core::AXIS_LEFT_HORIZONTAL },
        { core::AXIS_RIGHT_VERTICAL, core::AXIS_RIGHT_HORIZONTAL },
    };

    for (int side = 0; side < 2; side++) {
        bool held = radio_on && (buttons[side] & board::TRIM_CENTRE);

        if (!held) {
            centre_armed[side] = radio_on;
            centre_held_loops[side] = 0;
        } else if (centre_armed[side] && ++centre_held_loops[side] == TRIM_RESET_LOOPS) {
            centre_armed[side] = false;
            for (core::Axis axis : button_axes[side]) {
                int index = settings::TRIM_FIRST + (int)axis;

                if (settings::values[index] != 0 && settings::set(index, 0)) {
                    trim_unsaved = true;
                }
            }
            indicators::trim_step(1, 0, settings::info(settings::TRIM_FIRST).max);
        }
    }
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
        indicators::trim_step(before, settings::values[index], settings::info(index).max);
    }
}

/*
 * Link to the receiver while the radio sends to one: the tune of
 * sound.connect when a receiver answers, the first time or after a loss, and
 * of sound.lost once when no link statistics came for 1 s.
 */
static void link_tick(core::Mode mode)
{
    static link_alarm::State state;
    bool active = (mode == core::Mode::ELRS_LOCKED || mode == core::Mode::ELRS_ACTIVE ||
                   mode == core::Mode::ELRS_HOST_READY || mode == core::Mode::ELRS_HOST) &&
                  module_link::starting_task() == core::ModuleTask::NONE;
    link_alarm::Event event = link_alarm::tick(state, active, module_link::receiver_connected());

    if (event == link_alarm::Event::LOST) {
        indicators::play_event(settings::SOUND_LINK_LOST);
    } else if (event == link_alarm::Event::CONNECTED) {
        indicators::play_event(settings::SOUND_CONNECTED);
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
    /*
     * Started from battery by the power button: keep the power on after the
     * button is released, so that a short press can show the battery level
     * before the radio switches off again.
     */
    bool peek_pending = !board::usb_supply_present();
    /* Uptime when the battery level started to blink after a short press; -1 while it does not. */
    int64_t peek_since = -1;

    if (peek_pending) {
        board::set_power_latch(true);
    }
    storage::load_settings();
    host::print_hello();
    output::start();

    while (true) {
        output::snapshot(snapshot);
        board::Inputs &inputs = snapshot.inputs;

        int64_t now = k_uptime_get();
        bool usb = board::usb_supply_present();

        board::read_trims(inputs);
        trim_tick(inputs.trim_left, inputs.trim_right);
        usb_tick(inputs);

        /*
         * Power button: hold 1 s to switch on, 1.25 s to switch off. A shorter
         * press with the radio off shows the battery level, then the latch is
         * released, which on battery switches the radio off again.
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
            bool short_press = power_held_loops > 0 && !power_handled;

            if (!radio_on && (short_press || peek_pending)) {
                peek_since = now;
            }
            peek_pending = false;
            power_held_loops = 0;
            power_handled = false;
        }
        if (peek_since >= 0 && now - peek_since >= battery::PEEK_MS) {
            peek_since = -1;
            if (!radio_on && !inputs.power) {
                board::set_power_latch(false);
            }
        }

        /* Battery: a reading every 15 s, the warning on each weak reading while on, the LEDs. */
        if (battery_monitor::tick(now) && radio_on && battery::weak(battery_monitor::raw(), usb)) {
            indicators::weak_battery();
        }
        uint16_t battery_raw = battery_monitor::raw();
        uint8_t bar = battery::bar(battery_raw, usb, (uint32_t)now);
        uint32_t held_ms = (uint32_t)(power_held_loops * LOOP_MS);
        uint8_t battery_leds = 0;

        if (latch_release_pending || (power_handled && !radio_on)) {
            battery_leds = 0; /* switched off; with USB the charge shows again after the tune */
        } else if (inputs.power && !power_handled) {
            battery_leds = radio_on ? battery::switching_off(held_ms, bar) : battery::switching_on(held_ms);
        } else if (peek_since >= 0) {
            battery_leds = battery::peek(battery_raw, (uint32_t)(now - peek_since));
        } else if (radio_on && show_link_quality) {
            battery_leds = battery::link_quality(module_link::receiver_connected(),
                                                 module_link::status().link_quality);
        } else if (battery_raw != 0 && (radio_on ? now - on_since >= BAR_AFTER_ON_MS : usb)) {
            battery_leds = bar;
        }
        battery_monitor::show(battery_leds);

        /* One tune when the throttle lock is released. */
        bool module_is_on = output::module_is_on();

        if (module_is_on && was_locked && !snapshot.locked) {
            indicators::play_event(settings::SOUND_UNLOCK);
        }
        was_locked = snapshot.locked;
        link_tick(snapshot.mode);
        /* While WiFi or Bluetooth waits for the module, the LED shows it already; the output stays locked. */
        core::Mode shown = snapshot.mode;

        if (module_link::starting_task() == core::ModuleTask::WIFI) {
            shown = core::Mode::WIFI;
        } else if (module_link::starting_task() == core::ModuleTask::BLUETOOTH) {
            shown = core::Mode::BLUETOOTH;
        }
        indicators::tick({ shown, calibration::running(), module_link::receiver_connected() });
        /* On battery the radio loses power here unless the power button is still held. */
        if (latch_release_pending && !indicators::playing()) {
            latch_release_pending = false;
            board::set_power_latch(false);
        }

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

        /*
         * SETUP held for 2 s starts a calibration run, a short press ends it.
         * Otherwise a short press with the radio on switches the battery LEDs
         * between battery and link quality.
         */
        if (calibration::running()) {
            calibration::track(inputs.sticks);
        }
        if (inputs.setup) {
            if (++setup_held_loops == SETUP_HOLD_LOOPS) {
                calibration::start();
            }
        } else {
            bool short_press = setup_held_loops > 0 && setup_held_loops < SETUP_HOLD_LOOPS;

            if (calibration::running() && short_press) {
                calibration::finish(inputs.sticks);
            } else if (radio_on && short_press) {
                show_link_quality = !show_link_quality;
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
