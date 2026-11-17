#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/app_version.h>

#include "board/board.h"

/*
 * OpenLiteRadio firmware, stage 1: power handling, controls and indicators,
 * and a line protocol on the USB serial port for the live view in web/.
 * No CRSF frames are sent yet.
 *
 * Host protocol, one message per line.
 * From the radio:
 *   hello OpenLiteRadio <version> proto=1
 *   state sticks=a,b,c,d sa= sb= sc= sd= se= sf= bind= setup= power= tl= tr=
 *         keys= battery= usb= on= module=
 * To the radio:
 *   hello             ask for the hello line
 *   stream 0|1        stop or start the state lines
 *   led R G B         status LED colour, 0..255 each; "led auto" returns control
 *   beep HZ MS        tone
 *   batleds MASK      battery LEDs, bit 0 is the leftmost
 *   module 0|1        ELRS module off or on
 */

extern "C" void updater_enter(void);

/* Baud rate a host sets on the serial port to request the vendor USB updater. */
constexpr uint32_t UPDATER_REQUEST_BAUD = 1200;

constexpr int LOOP_MS = 20;
constexpr int STATE_EVERY_LOOPS = 2;   /* 25 state lines per second */
constexpr int POWER_ON_LOOPS = 50;     /* power button held for 1 s */
constexpr int POWER_OFF_LOOPS = 100;   /* power button held for 2 s */

static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static bool radio_on;
static bool module_on;
static bool stream = true;
static bool led_from_host;
static int beep_loops;

static void beep(uint32_t frequency, int milliseconds)
{
    board::set_buzzer(frequency);
    beep_loops = milliseconds / LOOP_MS;
}

static void show_status_led()
{
    if (led_from_host) {
        return;
    }
    if (radio_on) {
        board::set_status_led(0, 0, 0x30); /* blue: working */
    } else {
        board::set_status_led(0, 0, 0);
    }
}

static void set_radio_on(bool on)
{
    radio_on = on;
    board::set_power_latch(on);
    if (!on) {
        module_on = false;
        board::set_module_enabled(false);
    }
    beep(on ? 2700 : 1800, 150);
    show_status_led();
}

/* Run one command line from the host. */
static void handle_command(char *line)
{
    char *arguments[4] = {};
    char *command = strtok(line, " ");

    if (command == nullptr) {
        return;
    }
    for (char *&argument : arguments) {
        argument = strtok(nullptr, " ");
    }
    auto number = [&](int index) {
        return (arguments[index] != nullptr) ? strtol(arguments[index], nullptr, 0) : 0;
    };

    if (strcmp(command, "hello") == 0) {
        printk("hello OpenLiteRadio %s proto=1\n", APP_VERSION_STRING);
    } else if (strcmp(command, "stream") == 0) {
        stream = number(0) != 0;
    } else if (strcmp(command, "led") == 0) {
        led_from_host = arguments[0] != nullptr && strcmp(arguments[0], "auto") != 0;
        if (led_from_host) {
            board::set_status_led((uint8_t)number(0), (uint8_t)number(1), (uint8_t)number(2));
        } else {
            show_status_led();
        }
    } else if (strcmp(command, "beep") == 0) {
        beep((uint32_t)number(0), (int)number(1));
    } else if (strcmp(command, "batleds") == 0) {
        board::set_battery_leds((uint8_t)number(0));
    } else if (strcmp(command, "module") == 0) {
        module_on = number(0) != 0;
        board::set_module_enabled(module_on);
    }
}

/* Collect characters from the host into lines. */
static void poll_host()
{
    static char line[48];
    static size_t length;
    unsigned char character;

    while (uart_poll_in(console, &character) == 0) {
        if (character == '\n' || character == '\r') {
            line[length] = '\0';
            handle_command(line);
            length = 0;
        } else if (length < sizeof(line) - 1) {
            line[length++] = (char)character;
        }
    }
}

int main(void)
{
    board::Inputs inputs = {};
    int power_held_loops = 0;
    bool power_handled = false;
    uint32_t loops = 0;

    board::init();
    printk("hello OpenLiteRadio %s proto=1\n", APP_VERSION_STRING);

    while (true) {
        board::read_inputs(inputs);

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

        if (beep_loops > 0 && --beep_loops == 0) {
            board::set_buzzer(0);
        }

        poll_host();
        if (stream && loops % STATE_EVERY_LOOPS == 0) {
            printk("state sticks=%u,%u,%u,%u sa=%u sb=%u sc=%u sd=%u se=%u sf=%u bind=%u "
                   "setup=%u power=%u tl=%u tr=%u keys=%u battery=%u usb=%u on=%u module=%u\n",
                   inputs.sticks[0], inputs.sticks[1], inputs.sticks[2], inputs.sticks[3],
                   inputs.sa, inputs.sb, inputs.sc, inputs.sd, inputs.se, inputs.sf,
                   inputs.bind, inputs.setup, inputs.power, inputs.trim_left,
                   inputs.trim_right, inputs.trims_valid, board::battery_raw(),
                   board::usb_supply_present(), radio_on, module_on);
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
