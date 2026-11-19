#include "module_link.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "board/board.h"
#include "core/crsf.h"
#include "core/mode.h"
#include "indicators.h"
#include "output.h"

namespace module_link {

namespace {

constexpr uint32_t MIN_PERIOD_US = 1000;
constexpr uint32_t MAX_PERIOD_US = 20000;

/*
 * The module's commands are parameters whose numbers differ between ELRS
 * versions. Ask for the number of parameters, read them one by one until the
 * wanted name comes, then start the command. Every command name met on the
 * way is kept for the next time.
 */
struct CommandInfo {
    const char *name;
    uint8_t value;         /* written to start it */
    core::ModuleTask task; /* what the module does afterwards */
};
/*
 * In the order of Command. WiFi and Bluetooth are written as confirmed: the
 * module asks before them while a receiver is connected, and starts them at
 * once when the start comes confirmed (ELRS 3.6.4, tx_devLUA.cpp).
 */
const CommandInfo COMMANDS[] = {
    { "Bind", crsf::COMMAND_START, core::ModuleTask::BINDING },
    { "Enable WiFi", crsf::COMMAND_CONFIRM, core::ModuleTask::WIFI },
    { "BLE Joystick", crsf::COMMAND_CONFIRM, core::ModuleTask::BLUETOOTH },
};
static_assert(sizeof(COMMANDS) / sizeof(COMMANDS[0]) == COMMAND_COUNT, "one entry per Command");

/* Steps of starting a command. */
enum SearchStep {
    SEARCH_IDLE,    // nothing to do
    SEARCH_WAIT,    // the module has not answered since it was switched on
    SEARCH_COUNT,   // asked for the number of parameters
    SEARCH_ENTRY,   // reading the entries one by one
};
constexpr int ANSWER_LOOPS = 2;       /* 40 ms for the module to answer */
constexpr int ATTEMPTS = 5;
constexpr int64_t WAIT_MS = 5000;     /* for the module to answer after switching on */
/* Binding ends when a receiver answers, but not before the module has bound (about 1 s). */
constexpr int64_t BIND_SETTLE_MS = 1500;
constexpr int64_t BIND_TIMEOUT_MS = 8000;

SearchStep search_step;
Command wanted;
int64_t wait_since_ms;
uint8_t command_parameter[COMMAND_COUNT]; /* 0: not known yet */
uint8_t parameter_count;
uint8_t candidate;
int wait_loops;
int attempts;
int64_t bind_started_ms;

crsf::Parser parser;
uint32_t frames;
crsf::LinkStatistics link_statistics;
/* Uptime of the last link statistics frame; the module sends them only while a receiver answers. */
int64_t link_statistics_ms = -1;
int32_t timing_offset_us;
bool show_frames;

/* Send the request that belongs to the current step. */
void search_request()
{
    uint8_t frame[crsf::PARAMETER_FRAME_SIZE];

    if (search_step == SEARCH_COUNT) {
        crsf::encode_ping(frame);
        output::send_to_module(frame, crsf::PING_FRAME_SIZE);
    } else {
        crsf::encode_parameter_read(candidate, frame);
        output::send_to_module(frame, sizeof(frame));
    }
    wait_loops = ANSWER_LOOPS;
}

/* Start the wanted command if it was found; the module then does its task. */
void search_finish(bool found)
{
    uint8_t frame[crsf::PARAMETER_FRAME_SIZE];
    const CommandInfo &command = COMMANDS[wanted];

    search_step = SEARCH_IDLE;
    if (found) {
        crsf::encode_parameter_write(command_parameter[wanted], command.value, frame);
        found = output::send_to_module(frame, sizeof(frame));
    }
    if (found) {
        bind_started_ms = k_uptime_get();
        output::set_module_task(command.task);
    }
    indicators::beep(found ? 2700 : 600, found ? 150 : 600);
}

/* Use a frame from the module for the running search. */
void search_handle_frame(const uint8_t *frame, size_t size)
{
    uint8_t index = 0;
    uint8_t type = 0;
    const char *name = nullptr;

    if (search_step == SEARCH_COUNT && crsf::decode_device_info(frame, size, parameter_count)) {
        search_step = SEARCH_ENTRY;
        candidate = 1;
        attempts = ATTEMPTS;
        search_request();
    } else if (search_step == SEARCH_ENTRY &&
               crsf::decode_parameter_entry(frame, size, index, type, name) &&
               index == candidate) {
        for (int c = 0; c < COMMAND_COUNT && type == crsf::PARAMETER_TYPE_COMMAND; c++) {
            if (strcmp(name, COMMANDS[c].name) == 0) {
                command_parameter[c] = index;
            }
        }
        if (command_parameter[wanted] != 0) {
            search_finish(true);
        } else if (candidate >= parameter_count) {
            search_finish(false);
        } else {
            candidate++;
            attempts = ATTEMPTS;
            search_request();
        }
    }
}

/*
 * Start the search once the module answers, repeat a request the module did
 * not answer in time, and give up after a few tries.
 */
void search_tick()
{
    if (search_step == SEARCH_WAIT) {
        if (output::module_answered_once()) {
            search_step = SEARCH_COUNT;
            attempts = ATTEMPTS;
            search_request();
        } else if (k_uptime_get() - wait_since_ms > WAIT_MS) {
            search_finish(false);
        }
        return;
    }
    if (search_step == SEARCH_IDLE || --wait_loops > 0) {
        return;
    }
    if (--attempts > 0) {
        search_request();
    } else {
        search_finish(false);
    }
}

/* Binding ends with a receiver that answers after the module has bound, or after 8 s. */
void binding_tick()
{
    if (output::module_task_now() != core::ModuleTask::BINDING) {
        return;
    }
    if (link_statistics_ms >= bind_started_ms + BIND_SETTLE_MS && receiver_connected()) {
        output::set_module_task(core::ModuleTask::NONE);
        indicators::beep(3200, 500);
    } else if (k_uptime_get() - bind_started_ms >= BIND_TIMEOUT_MS) {
        output::set_module_task(core::ModuleTask::NONE);
        indicators::beep(600, 600);
    }
}

/* Parse what the module sent and keep its frame period and link statistics. */
void poll()
{
    uint8_t bytes[32];
    size_t count;

    while ((count = board::module_receive(bytes, sizeof(bytes))) > 0) {
        for (size_t i = 0; i < count; i++) {
            if (!parser.feed(bytes[i])) {
                continue;
            }
            frames++;
            output::note_module_frame();
            if (show_frames) {
                printk("rx");
                for (size_t k = 0; k < parser.size(); k++) {
                    printk(" %02x", parser.frame()[k]);
                }
                printk("\n");
            }

            uint32_t interval = 0;

            if (crsf::decode_timing(parser.frame(), parser.size(), interval, timing_offset_us) &&
                interval >= MIN_PERIOD_US && interval <= MAX_PERIOD_US) {
                output::set_frame_timing(interval, timing_offset_us);
            }
            search_handle_frame(parser.frame(), parser.size());
            if (crsf::decode_link_statistics(parser.frame(), parser.size(), link_statistics)) {
                link_statistics_ms = k_uptime_get();
            }
        }
    }
}

} // namespace

void set_module_on(bool on)
{
    search_step = SEARCH_IDLE;
    output::set_module_on(on);
}

void command_start(Command command)
{
    /* One command at a time; WiFi and Bluetooth end only with the module. */
    if (search_step != SEARCH_IDLE || !output::module_is_on() ||
        output::module_task_now() != core::ModuleTask::NONE) {
        return;
    }
    wanted = command;
    if (command_parameter[command] != 0) {
        search_finish(true);
    } else if (!output::module_answered_once()) {
        search_step = SEARCH_WAIT;
        wait_since_ms = k_uptime_get();
    } else {
        search_step = SEARCH_COUNT;
        attempts = ATTEMPTS;
        search_request();
    }
}

core::ModuleTask starting_task()
{
    return search_step == SEARCH_IDLE ? core::ModuleTask::NONE : COMMANDS[wanted].task;
}

void bind_cancel()
{
    if (output::module_task_now() == core::ModuleTask::BINDING) {
        output::set_module_task(core::ModuleTask::NONE);
        indicators::beep(1800, 150);
    }
}

void set_show_frames(bool show)
{
    show_frames = show;
}

void tick()
{
    poll();
    search_tick();
    binding_tick();
}

bool receiver_connected()
{
    constexpr int64_t STALE_MS = 1000;

    return link_statistics_ms >= 0 && k_uptime_get() - link_statistics_ms < STALE_MS &&
           link_statistics.uplink_quality > 0;
}

Status status()
{
    /* Without fresh link statistics there is no receiver, whatever the last ones said. */
    bool fresh = receiver_connected();

    return { frames, parser.crc_errors(), fresh ? link_statistics.uplink_quality : (uint8_t)0,
             fresh ? link_statistics.uplink_rssi : (uint8_t)0, timing_offset_us,
             command_parameter[COMMAND_BIND] };
}

} // namespace module_link
