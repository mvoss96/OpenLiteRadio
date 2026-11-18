/*
 * Host protocol, one message per line.
 * From the radio:
 *   hello OpenLiteRadio <version> proto=1
 *   setting KEY TYPE MIN MAX DEFAULT VALUE   one per setting after "list", then "end"
 *   value KEY VALUE                          answer to "get"
 *   ok ... / error REASON                    answer to the other settings commands
 *   state sticks=a,b,c,d sa= sb= sc= sd= se= sf= bind= setup= power= tl= tr=
 *         keys= battery= usb= on= module= ch=c1,...,c16 lock= period= frames=
 *         crc= lq= rssi= offset= bindpar= calib=
 * To the radio:
 *   hello             ask for the hello line
 *   stream 0|1        stop or start the state lines
 *   led R G B         status LED colour, 0..255 each; "led auto" returns control
 *   beep HZ MS        tone
 *   batleds MASK      battery LEDs, bit 0 is the leftmost
 *   module 0|1        ELRS module off or on
 *   bind              start binding, as a long press of BIND does
 *   frames 0|1        print every frame received from the module as "rx" plus hex bytes
 *   list              report all settings
 *   get KEY           report one setting
 *   set KEY VALUE     change a setting; it applies at once and is lost at power-off
 *   save              store all settings
 *   revert            return to the stored settings
 *   defaults          set all settings except the stick calibration (cal.N.*) to their
 *                     defaults, without storing them
 *   calibrate start|end|cancel   stick calibration run, as SETUP does; cancel keeps the old values
 *   profiles          report "profile N NAME" for each used slot, then "active N" (0: none)
 *   profile load N    put profile N into the ch.N.* settings and store them
 *   profile save N NAME   store the ch.N.* settings as profile N and make it the active one
 *   profile name N NAME   rename profile N
 *   profile delete N  remove profile N
 *   profile none      make no profile the active one
 *   inject KEY=VALUE ...   test inputs: replace sticks (a,b,c,d), sa..sf for 10 s
 *                     after the last inject line, only while the module is off;
 *                     "inject off" ends it
 *
 * Every change of a setting locks the channel output again.
 */

#include "host.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/app_version.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "board/board.h"
#include "calibration.h"
#include "core/profiles.h"
#include "core/settings.h"
#include "indicators.h"
#include "module_link.h"
#include "storage.h"

namespace host {

namespace {

const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

bool stream = true;
/* Next setting the "list" command still has to report; COUNT when it is done. */
int list_next = settings::COUNT;

/* Keys of "inject", in the order of the output::InjectField bits. */
const char *const INJECT_NAMES[] = { "sticks", "sa", "sb", "sc", "sd", "se", "sf" };

void print_setting(int index)
{
    char key[settings::KEY_SIZE];
    settings::Info info = settings::info(index);

    settings::key(index, key);
    printk("setting %s %s %d %d %d %d\n", key, (info.min == 0 && info.max == 1) ? "bool" : "int",
           info.min, info.max, info.default_value, settings::values[index]);
}

/* Report a few settings per loop so that the USB buffer keeps up. */
void list_tick()
{
    for (int i = 0; i < 4 && list_next < settings::COUNT; i++) {
        print_setting(list_next++);
        if (list_next == settings::COUNT) {
            printk("end\n");
        }
    }
}

void print_profiles()
{
    profiles::Profile profile;

    for (int slot = 1; slot <= profiles::SLOTS; slot++) {
        if (storage::read_profile(slot, profile)) {
            printk("profile %d %s\n", slot, profile.name);
        }
    }
    printk("active %u\n", storage::active_profile());
}

void reply_profile(bool stored, const char *action, long slot)
{
    if (stored) {
        printk("ok profile %s %ld\n", action, slot);
    } else {
        printk("error storage\n");
    }
}

/*
 * Run "profile ACTION N [NAME]". The name is the rest of the line and may
 * contain spaces, so this runs before the line is split into words.
 */
void handle_profile(char *text)
{
    char *end = nullptr;
    char *action = text;

    while (*text != '\0' && *text != ' ') {
        text++;
    }
    if (*text == ' ') {
        *text++ = '\0';
    }
    /* No profile is active any more, e.g. after the host wrote a template. */
    if (strcmp(action, "none") == 0) {
        if (storage::store_active_profile(0)) {
            printk("ok profile none\n");
        } else {
            printk("error storage\n");
        }
        return;
    }

    long slot = strtol(text, &end, 10);
    const char *name = end;

    while (*name == ' ') {
        name++;
    }
    if (end == text || slot < 1 || slot > profiles::SLOTS) {
        printk("error unknown profile\n");
        return;
    }

    profiles::Profile profile;

    if (strcmp(action, "save") == 0) {
        if (!profiles::name_valid(name)) {
            printk("error invalid name\n");
            return;
        }
        profiles::capture(settings::values, name, profile);
        bool stored = storage::write_profile((int)slot, profile) &&
                      storage::store_active_profile((uint8_t)slot) && storage::save_settings();
        reply_profile(stored, "save", slot);
    } else if (strcmp(action, "load") == 0) {
        if (!storage::read_profile((int)slot, profile)) {
            printk("error unknown profile\n");
            return;
        }
        /* The output locks first, and the control thread never sees half of the change. */
        output::relock();
        k_sched_lock();
        bool applied = profiles::apply(profile);
        k_sched_unlock();

        if (!applied) {
            printk("error invalid profile\n");
        } else {
            bool stored = storage::store_active_profile((uint8_t)slot) && storage::save_settings();
            reply_profile(stored, "load", slot);
        }
    } else if (strcmp(action, "name") == 0) {
        if (!profiles::name_valid(name)) {
            printk("error invalid name\n");
        } else if (!storage::read_profile((int)slot, profile)) {
            printk("error unknown profile\n");
        } else {
            memset(profile.name, 0, sizeof(profile.name));
            strncpy(profile.name, name, profiles::NAME_SIZE - 1);
            reply_profile(storage::write_profile((int)slot, profile), "name", slot);
        }
    } else if (strcmp(action, "delete") == 0) {
        bool stored = storage::delete_profile((int)slot);

        if (stored && storage::active_profile() == slot) {
            stored = storage::store_active_profile(0);
        }
        reply_profile(stored, "delete", slot);
    } else {
        printk("error unknown command\n");
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
    if (strncmp(line, "profile ", 8) == 0) {
        handle_profile(line + 8);
        return;
    }
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
    } else if (strcmp(command, "batleds") == 0) {
        board::set_battery_leds((uint8_t)number(0));
    } else if (strcmp(command, "module") == 0) {
        module_link::set_module_on(number(0) != 0);
    } else if (strcmp(command, "frames") == 0) {
        module_link::set_show_frames(number(0) != 0);
    } else if (strcmp(command, "bind") == 0) {
        module_link::bind_start();
    } else if (strcmp(command, "list") == 0) {
        list_next = 0;
    } else if (strcmp(command, "profiles") == 0) {
        print_profiles();
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
            printk("ok %s %d\n", arguments[0], settings::values[index]);
        }
    } else if (strcmp(command, "save") == 0) {
        printk(storage::save_settings() ? "ok save\n" : "error storage\n");
    } else if (strcmp(command, "revert") == 0 || strcmp(command, "defaults") == 0) {
        /* The output locks first, and the control thread never sees half of the change. */
        output::relock();
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
        printk("ok %s\n", command);
    } else if (strcmp(command, "calibrate") == 0 && arguments[0] != nullptr) {
        if (strcmp(arguments[0], "start") == 0) {
            calibration::start();
            printk("ok calibrate start\n");
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

} // namespace

void print_hello()
{
    printk("hello OpenLiteRadio %s proto=1\n", APP_VERSION_STRING);
}

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
           inputs.trim_right, inputs.trims_valid, board::battery_raw(),
           board::usb_supply_present(), radio_on, output::module_is_on());
    printk(" ch=");
    for (int channel = 0; channel < core::CHANNEL_COUNT; channel++) {
        printk(channel == 0 ? "%u" : ",%u", snapshot.channels[channel]);
    }
    printk(" lock=%u period=%u frames=%u crc=%u lq=%u rssi=%u offset=%d bindpar=%u "
           "calib=%u\n",
           snapshot.locked, (unsigned int)output::frame_period_us(), (unsigned int)link.frames,
           (unsigned int)link.crc_errors, link.link_quality, link.rssi,
           (int)link.timing_offset_us, link.bind_parameter, calibration::running());
}

} // namespace host
