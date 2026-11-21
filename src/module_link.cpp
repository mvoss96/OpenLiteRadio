#include "module_link.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "board/board.h"
#include "core/crsf.h"
#include "core/mode.h"
#include "core/settings.h"
#include "indicators.h"
#include "output.h"

namespace module_link {

namespace {

constexpr uint32_t MIN_PERIOD_US = 1000;
constexpr uint32_t MAX_PERIOD_US = 20000;

/* Finds and starts the module's commands; see src/core/module_command.h. */
module_command::CommandSearch search;
int64_t bind_started_ms;

crsf::Parser parser;
uint32_t frames;
crsf::LinkStatistics link_statistics;
/* Uptime of the last link statistics frame; the module sends them only while a receiver answers. */
int64_t link_statistics_ms = -1;
int32_t timing_offset_us;
bool show_frames;

/*
 * Do what a step of the search asks for: send its frame, and once the
 * command has started or failed, set the module's task and play the tune.
 */
void apply(const module_command::Step &step)
{
    bool sent = step.size == 0 || output::send_to_module(step.frame, step.size);

    if (step.result == module_command::Result::NONE) {
        return;
    }
    if (step.result == module_command::Result::STARTED && sent) {
        bind_started_ms = k_uptime_get();
        output::set_module_task(module_command::task_of(search.wanted()));
        indicators::play_event(settings::SOUND_ACCEPTED);
    } else {
        indicators::play_event(settings::SOUND_ERROR);
    }
}

/* Binding ends with a receiver that answers after the module has bound, or after 8 s. */
void binding_tick()
{
    if (output::module_task_now() != core::ModuleTask::BINDING) {
        return;
    }
    module_command::Binding binding = module_command::binding_state(
        k_uptime_get(), bind_started_ms, link_statistics_ms, receiver_connected());

    if (binding == module_command::Binding::RUNNING) {
        return;
    }
    output::set_module_task(core::ModuleTask::NONE);
    indicators::play_event(binding == module_command::Binding::BOUND ? settings::SOUND_SUCCESS
                                                                     : settings::SOUND_ERROR);
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
            apply(search.frame(parser.frame(), parser.size()));
            if (crsf::decode_link_statistics(parser.frame(), parser.size(), link_statistics)) {
                link_statistics_ms = k_uptime_get();
            }
        }
    }
}

} // namespace

void set_module_on(bool on)
{
    search.reset();
    output::set_module_on(on);
}

void command_start(Command command)
{
    /* One command at a time; WiFi and Bluetooth end only with the module. */
    if (!search.idle() || !output::module_is_on() ||
        output::module_task_now() != core::ModuleTask::NONE) {
        return;
    }
    /* Binding drops the link, so not while a model is connected. */
    if (command == COMMAND_BIND && receiver_connected()) {
        indicators::play_event(settings::SOUND_ERROR);
        printk("error receiver connected\n");
        return;
    }
    apply(search.start(command, output::module_answered_once(), k_uptime_get()));
}

core::ModuleTask starting_task()
{
    return search.idle() ? core::ModuleTask::NONE : module_command::task_of(search.wanted());
}

void bind_cancel()
{
    if (output::module_task_now() == core::ModuleTask::BINDING) {
        output::set_module_task(core::ModuleTask::NONE);
        indicators::play_event(settings::SOUND_CANCEL);
    }
}

void set_show_frames(bool show)
{
    show_frames = show;
}

void tick()
{
    poll();
    apply(search.tick(output::module_answered_once(), k_uptime_get()));
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
             search.parameter(COMMAND_BIND) };
}

} // namespace module_link
