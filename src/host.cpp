/*
 * Host protocol on the USB serial port: text lines, one message per line.
 * Every line and command is described in docs/ARCHITECTURE.md, "Host
 * protocol"; that section is the one description, to be changed with the
 * code here.
 */

#include "host.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/app_version.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/sys/printk.h>

#include <cmsis_core.h>
#include <zephyr/sys/util.h>

#include "battery_monitor.h"
#include "board/board.h"
#include "calibration.h"
#include "core/crsf.h"
#include "core/settings.h"
#include "indicators.h"
#include "module_link.h"
#include "storage.h"

namespace host {

namespace {

const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

bool stream = true;
/* Next line the "list" command still has to report, settings first, then options; -1 when done. */
int list_next = -1;

/* Keys of "inject", in the order of the output::InjectField bits. */
const char *const INJECT_NAMES[] = { "sticks", "sa", "sb", "sc", "sd", "se", "sf" };

void print_setting(int index)
{
    char key[settings::KEY_SIZE];
    settings::Info info = settings::info(index);

    settings::key(index, key);
    printk("setting %s %s %d %d %d %d\n", key,
           info.options != nullptr ? info.options : (info.min == 0 && info.max == 1) ? "bool" : "int",
           info.min, info.max, info.default_value, settings::values[index]);
}

/*
 * Report a few lines per loop so that the USB buffer keeps up: the settings,
 * then the options, then "end" with the number of lines.
 */
void list_tick()
{
    int lines = settings::COUNT + settings::option_count();

    for (int i = 0; i < 4 && list_next >= 0; i++) {
        if (list_next < settings::COUNT) {
            print_setting(list_next);
        } else {
            settings::Option option = settings::option(list_next - settings::COUNT);

            printk("option %s %d %s\n", option.list, option.value, option.key);
        }
        if (++list_next == lines) {
            printk("end %d\n", lines);
            list_next = -1;
        }
    }
}

/* Run "inject off" or "inject KEY=VALUE ...", with the keys of the state line. */
void handle_inject(char *text)
{
    /* Keys add to the ones still active, so a test can span several short lines. */
    board::Inputs values;
    uint32_t fields;

    output::injection(values, fields);
    for (char *word = strtok(text, " "); word != nullptr; word = strtok(nullptr, " ")) {
        char *value = strchr(word, '=');

        if (strcmp(word, "off") == 0) {
            fields = 0;
            continue;
        }
        if (value == nullptr) {
            printk("error unknown key\n");
            return;
        }
        *value++ = '\0';

        int field = -1;

        for (int i = 0; i < (int)ARRAY_SIZE(INJECT_NAMES); i++) {
            if (strcmp(word, INJECT_NAMES[i]) == 0) {
                field = i;
            }
        }
        long number = strtol(value, &value, 0);

        if (field == 0) {
            values.sticks[0] = (uint16_t)number;
            for (int axis = 1; axis < 4 && *value == ','; axis++) {
                values.sticks[axis] = (uint16_t)strtol(value + 1, &value, 0);
            }
        } else if (field > 0) {
            uint8_t *switches[] = { &values.sa, &values.sb, &values.sc, &values.sd };

            if (field <= 4) {
                *switches[field - 1] = (uint8_t)number;
            } else if (field == 5) {
                values.se = number != 0;
            } else {
                values.sf = number != 0;
            }
        } else {
            printk("error unknown key\n");
            return;
        }
        fields |= BIT(field);
    }
    if (!output::inject(values, fields)) {
        printk("error module on\n");
        return;
    }
    printk("ok inject\n");
}

/* Run one command line from the host. */
void handle_command(char *line)
{
    if (strncmp(line, "inject ", 7) == 0) {
        handle_inject(line + 7);
        return;
    }

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
        print_hello();
    } else if (strcmp(command, "stream") == 0) {
        stream = number(0) != 0;
    } else if (strcmp(command, "led") == 0) {
        indicators::set_led_from_host(arguments[0] != nullptr && strcmp(arguments[0], "auto") != 0,
                                      (uint8_t)number(0), (uint8_t)number(1), (uint8_t)number(2));
    } else if (strcmp(command, "beep") == 0) {
        indicators::beep((uint32_t)number(0), (int)number(1));
    } else if (strcmp(command, "play") == 0) {
        int index = (arguments[0] != nullptr) ? settings::find(arguments[0]) : -1;

        printk(indicators::play_setting(index) ? "ok play\n" : "error unknown key\n");
    } else if (strcmp(command, "batleds") == 0) {
        battery_monitor::set_from_host(arguments[0] != nullptr && strcmp(arguments[0], "auto") != 0,
                                       (uint8_t)number(0));
    } else if (strcmp(command, "module") == 0) {
        module_link::set_module_on(number(0) != 0);
    } else if (strcmp(command, "frames") == 0) {
        module_link::set_show_frames(number(0) != 0);
    } else if (strcmp(command, "crsf") == 0 && arguments[0] != nullptr) {
        /* One complete frame in hex, CRC included, passed to the module unchanged. */
        uint8_t frame[26];
        size_t size = strlen(arguments[0]) / 2;
        bool valid = size >= 4 && size <= sizeof(frame) && strlen(arguments[0]) % 2 == 0;

        for (size_t i = 0; valid && i < size; i++) {
            char pair[3] = { arguments[0][2 * i], arguments[0][2 * i + 1], 0 };
            char *end = nullptr;

            frame[i] = (uint8_t)strtoul(pair, &end, 16);
            valid = end == pair + 2;
        }
        /* Only what reads and writes the module's parameters; channel frames come from the control thread alone. */
        valid = valid && (frame[2] == crsf::TYPE_PING || frame[2] == crsf::TYPE_PARAMETER_READ ||
                          frame[2] == crsf::TYPE_PARAMETER_WRITE);
        printk(valid && output::send_to_module(frame, size) ? "ok crsf\n" : "error crsf\n");
    } else if (strcmp(command, "bind") == 0) {
        module_link::command_start(module_link::COMMAND_BIND);
    } else if (strcmp(command, "wifi") == 0) {
        module_link::command_start(module_link::COMMAND_WIFI);
    } else if (strcmp(command, "ble") == 0) {
        module_link::command_start(module_link::COMMAND_BLUETOOTH);
    } else if (strcmp(command, "list") == 0) {
        list_next = 0;
    } else if (strcmp(command, "get") == 0 || strcmp(command, "set") == 0) {
        int index = (arguments[0] != nullptr) ? settings::find(arguments[0]) : -1;

        if (index < 0) {
            printk("error unknown key\n");
        } else if (command[0] == 'g') {
            printk("value %s %d\n", arguments[0], settings::values[index]);
        } else if (arguments[1] == nullptr || !settings::set(index, number(1))) {
            printk("error value out of range\n");
        } else {
            output::relock();
            /*
             * The USB device changes its form in the next loop and this port
             * goes away with it, before a following "save" may arrive.
             */
            if (index == settings::USB_MODE && !storage::save_setting(index)) {
                printk("error storage\n");
            } else {
                printk("ok %s %d\n", arguments[0], settings::values[index]);
            }
        }
    } else if (strcmp(command, "save") == 0) {
        printk(storage::save_settings() ? "ok save\n" : "error storage\n");
    } else if (strcmp(command, "revert") == 0 || strcmp(command, "defaults") == 0) {
        /* The output locks first, and the control thread never sees half of the change. */
        output::relock();
        int16_t usb_mode = settings::values[settings::USB_MODE];

        k_sched_lock();
        if (command[0] == 'r') {
            storage::load_settings();
        } else {
            /* The stick calibration stays: without it the radio is not usable. */
            for (int index = 0; index < settings::CALIBRATION_FIRST; index++) {
                settings::values[index] = settings::info(index).default_value;
            }
        }
        k_sched_unlock();
        /* As with "set usb.mode": the port goes away with the USB form, before a "save" may arrive. */
        if (command[0] == 'd' && settings::values[settings::USB_MODE] != usb_mode &&
            !storage::save_settings()) {
            printk("error storage\n");
        } else {
            printk("ok %s\n", command);
        }
    } else if (strcmp(command, "calibrate") == 0 && arguments[0] != nullptr) {
        if (strcmp(arguments[0], "start") == 0) {
            /* A refused start has reported its reason already. */
            if (calibration::start()) {
                printk("ok calibrate start\n");
            }
        } else if (strcmp(arguments[0], "end") == 0) {
            output::Snapshot snapshot;

            output::snapshot(snapshot);
            printk(calibration::finish(snapshot.inputs.sticks) ? "ok calibrate end\n"
                                                                : "error calibration rejected\n");
        } else if (strcmp(arguments[0], "cancel") == 0) {
            calibration::cancel();
            printk("ok calibrate cancel\n");
        }
    }
}

/* Collect characters from the host into lines. */
void poll()
{
    static char line[64]; /* "crsf" with a frame of 26 bytes needs 57 characters */
    static size_t length;
    static bool too_long; /* the line did not fit and is not run */
    unsigned char character;

    while (uart_poll_in(console, &character) == 0) {
        if (character == '\n' || character == '\r') {
            if (too_long) {
                printk("error line too long\n");
            } else {
                line[length] = '\0';
                handle_command(line);
            }
            length = 0;
            too_long = false;
        } else if (length < sizeof(line) - 1) {
            line[length++] = (char)character;
        } else {
            too_long = true;
        }
    }
}

} // namespace

/*
 * A fatal error is kept across the restart that follows it and reported once
 * after "hello", so that a crash can be read from the host.
 */
namespace {

constexpr uint32_t FAULT_MARKER = 0x46415554u; /* "FAUT" */
__noinit uint32_t fault_marker;
__noinit uint32_t fault_reason;
__noinit uint32_t fault_pc;
__noinit uint32_t fault_lr;

/* Name and version as one string in the image; the configurator reads it from a firmware file. */
const char IMAGE_ID[] = "OpenLiteRadio " APP_VERSION_STRING;

} // namespace

void print_hello()
{
    /* The protocol number changes with every change of a message; test.ps1 checks web/index.html against it. */
    /* The MCU's 96-bit unique ID, by which the configurator finds this radio again among several. */
    const volatile uint32_t *uid = (const volatile uint32_t *)0x1FFFF7E8;
    printk("hello %s proto=4 id=%08x%08x%08x\n", IMAGE_ID, (unsigned int)uid[0], (unsigned int)uid[1],
           (unsigned int)uid[2]);
    if (fault_marker == FAULT_MARKER) {
        printk("fault reason=%u pc=0x%08x lr=0x%08x\n", (unsigned int)fault_reason,
               (unsigned int)fault_pc, (unsigned int)fault_lr);
        fault_marker = 0;
    }
}

} // namespace host

/* Replaces Zephyr's handler, which halts: keep the cause and restart. */
extern "C" void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    host::fault_reason = reason;
    host::fault_pc = esf != nullptr ? esf->basic.pc : 0;
    host::fault_lr = esf != nullptr ? esf->basic.lr : 0;
    host::fault_marker = host::FAULT_MARKER;
    NVIC_SystemReset();
}

namespace host {

void tick()
{
    poll();
    list_tick();
}

bool streaming()
{
    return stream;
}

void print_state(const output::Snapshot &snapshot, bool radio_on)
{
    const board::Inputs &inputs = snapshot.inputs;
    module_link::Status link = module_link::status();

    printk("state sticks=%u,%u,%u,%u sa=%u sb=%u sc=%u sd=%u se=%u sf=%u bind=%u "
           "setup=%u power=%u tl=%u tr=%u keys=%u battery=%u usb=%u on=%u module=%u",
           inputs.sticks[0], inputs.sticks[1], inputs.sticks[2], inputs.sticks[3],
           inputs.sa, inputs.sb, inputs.sc, inputs.sd, inputs.se, inputs.sf,
           inputs.bind, inputs.setup, inputs.power, inputs.trim_left,
           inputs.trim_right, inputs.trims_valid, battery_monitor::raw(),
           board::usb_supply_present(), radio_on, output::module_is_on());
    const int16_t *trim = &settings::values[settings::TRIM_FIRST];

    printk(" trim=%d,%d,%d,%d ch=", trim[0], trim[1], trim[2], trim[3]);
    for (int channel = 0; channel < core::CHANNEL_COUNT; channel++) {
        printk(channel == 0 ? "%u" : ",%u", snapshot.channels[channel]);
    }
    uint32_t sent = 0;
    uint32_t not_sent = 0;

    output::sent_counts(sent, not_sent);
    printk(" lock=%u period=%u frames=%u crc=%u lq=%u rssi=%u offset=%d bindpar=%u "
           "tx=%u txbusy=%u mode=%s led=%06x calib=%u\n",
           snapshot.locked, (unsigned int)output::frame_period_us(), (unsigned int)link.frames,
           (unsigned int)link.crc_errors, link.link_quality, link.rssi,
           (int)link.timing_offset_us, link.bind_parameter, (unsigned int)sent,
           (unsigned int)not_sent, core::mode_name(snapshot.mode),
           (unsigned int)indicators::led_colour_now(), calibration::running());
}

} // namespace host
