#include "module_link.h"

#include <string.h>

#include <zephyr/sys/printk.h>

#include "board/board.h"
#include "core/crsf.h"
#include "indicators.h"
#include "output.h"

namespace module_link {

namespace {

constexpr uint32_t MIN_PERIOD_US = 1000;
constexpr uint32_t MAX_PERIOD_US = 20000;

/*
 * Binding: the module's "Bind" action is a parameter whose number differs
 * between ELRS versions. Ask for the number of parameters, read them one by
 * one until the name matches, then start the action. The number is kept for
 * the next time.
 */
enum BindStep { BIND_IDLE, BIND_COUNT, BIND_SEARCH };
constexpr int BIND_ANSWER_LOOPS = 2; /* 40 ms for the module to answer */
constexpr int BIND_ATTEMPTS = 5;

BindStep bind_step;
uint8_t bind_parameter; /* 0: not known yet */
uint8_t parameter_count;
uint8_t bind_candidate;
int bind_wait_loops;
int bind_attempts;

crsf::Parser parser;
uint32_t frames;
crsf::LinkStatistics link_statistics;
int32_t timing_offset_us;
bool show_frames;

/* Send the request that belongs to the current step. */
void bind_request()
{
    uint8_t frame[crsf::PARAMETER_FRAME_SIZE];

    if (bind_step == BIND_COUNT) {
        crsf::encode_ping(frame);
        output::send_to_module(frame, crsf::PING_FRAME_SIZE);
    } else {
        crsf::encode_parameter_read(bind_candidate, frame);
        output::send_to_module(frame, sizeof(frame));
    }
    bind_wait_loops = BIND_ANSWER_LOOPS;
}

void bind_finish(bool found)
{
    uint8_t frame[crsf::PARAMETER_FRAME_SIZE];

    bind_step = BIND_IDLE;
    if (found) {
        crsf::encode_parameter_write(bind_parameter, crsf::COMMAND_START, frame);
        found = output::send_to_module(frame, sizeof(frame));
    }
    indicators::beep(found ? 3200 : 600, found ? 300 : 600);
}

/* Use a frame from the module for the running search. */
void bind_handle_frame(const uint8_t *frame, size_t size)
{
    uint8_t index = 0;
    uint8_t type = 0;
    const char *name = nullptr;

    if (bind_step == BIND_COUNT && crsf::decode_device_info(frame, size, parameter_count)) {
        bind_step = BIND_SEARCH;
        bind_candidate = 1;
        bind_attempts = BIND_ATTEMPTS;
        bind_request();
    } else if (bind_step == BIND_SEARCH &&
               crsf::decode_parameter_entry(frame, size, index, type, name) &&
               index == bind_candidate) {
        if (type == crsf::PARAMETER_TYPE_COMMAND && strcmp(name, "Bind") == 0) {
            bind_parameter = index;
            bind_finish(true);
        } else if (bind_candidate >= parameter_count) {
            bind_finish(false);
        } else {
            bind_candidate++;
            bind_attempts = BIND_ATTEMPTS;
            bind_request();
        }
    }
}

/* Repeat a request the module did not answer in time, and give up after a few tries. */
void bind_tick()
{
    if (bind_step == BIND_IDLE || --bind_wait_loops > 0) {
        return;
    }
    if (--bind_attempts > 0) {
        bind_request();
    } else {
        bind_finish(false);
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
            bind_handle_frame(parser.frame(), parser.size());
            crsf::decode_link_statistics(parser.frame(), parser.size(), link_statistics);
        }
    }
}

} // namespace

void set_module_on(bool on)
{
    bind_step = BIND_IDLE;
    output::set_module_on(on);
}

void bind_start()
{
    if (bind_step != BIND_IDLE || !output::module_is_on()) {
        return;
    }
    if (bind_parameter != 0) {
        bind_finish(true);
        return;
    }
    bind_step = BIND_COUNT;
    bind_attempts = BIND_ATTEMPTS;
    bind_request();
}

void set_show_frames(bool show)
{
    show_frames = show;
}

void tick()
{
    poll();
    bind_tick();
}

Status status()
{
    return { frames, parser.crc_errors(), link_statistics.uplink_quality,
             link_statistics.uplink_rssi, timing_offset_us, bind_parameter };
}

} // namespace module_link
