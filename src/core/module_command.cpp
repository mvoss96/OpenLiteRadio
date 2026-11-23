#include "core/module_command.h"

#include <string.h>

namespace module_command {

namespace {

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

const Step NOTHING = { {}, 0, Result::NONE };

} // namespace

core::ModuleTask task_of(Command command)
{
    return COMMANDS[command].task;
}

/* Build the request that belongs to the current step. */
Step CommandSearch::request()
{
    Step step = { {}, 0, Result::NONE };

    if (step_ == SEARCH_COUNT) {
        crsf::encode_ping(step.frame);
        step.size = crsf::PING_FRAME_SIZE;
    } else {
        crsf::encode_parameter_read(candidate_, step.frame);
        step.size = crsf::PARAMETER_FRAME_SIZE;
    }
    wait_ticks_ = ANSWER_TICKS;
    return step;
}

/* End the search: with the frame that starts the wanted command if it was found. */
Step CommandSearch::finish(bool found)
{
    Step step = { {}, 0, found ? Result::STARTED : Result::FAILED };

    step_ = SEARCH_IDLE;
    if (found) {
        crsf::encode_parameter_write(parameter_[wanted_], COMMANDS[wanted_].value, step.frame);
        step.size = crsf::PARAMETER_FRAME_SIZE;
    }
    return step;
}

Step CommandSearch::start(Command command, bool module_answered, int64_t now_ms)
{
    if (step_ != SEARCH_IDLE) {
        return NOTHING;
    }
    wanted_ = command;
    /* Also with a known parameter: a module still starting up drops the write. */
    if (!module_answered) {
        step_ = SEARCH_WAIT;
        wait_since_ms_ = now_ms;
        return NOTHING;
    }
    return begin();
}

/* With the module answering: start a known command at once, else search for it. */
Step CommandSearch::begin()
{
    if (parameter_[wanted_] != 0) {
        return finish(true);
    }
    step_ = SEARCH_COUNT;
    attempts_ = ATTEMPTS;
    return request();
}

Step CommandSearch::tick(bool module_answered, int64_t now_ms)
{
    if (step_ == SEARCH_WAIT) {
        if (module_answered) {
            return begin();
        }
        return now_ms - wait_since_ms_ > WAIT_MS ? finish(false) : NOTHING;
    }
    if (step_ == SEARCH_IDLE || --wait_ticks_ > 0) {
        return NOTHING;
    }
    return --attempts_ > 0 ? request() : finish(false);
}

Step CommandSearch::frame(const uint8_t *frame, size_t size)
{
    uint8_t index = 0;
    uint8_t type = 0;
    const char *name = nullptr;

    if (step_ == SEARCH_COUNT && crsf::decode_device_info(frame, size, parameter_count_)) {
        step_ = SEARCH_ENTRY;
        candidate_ = 1;
        attempts_ = ATTEMPTS;
        return request();
    }
    if (step_ != SEARCH_ENTRY || !crsf::decode_parameter_entry(frame, size, index, type, name) ||
        index != candidate_) {
        return NOTHING;
    }
    for (int c = 0; c < COMMAND_COUNT && type == crsf::PARAMETER_TYPE_COMMAND; c++) {
        if (strcmp(name, COMMANDS[c].name) == 0) {
            parameter_[c] = index;
        }
    }
    if (parameter_[wanted_] != 0) {
        return finish(true);
    }
    if (candidate_ >= parameter_count_) {
        return finish(false);
    }
    candidate_++;
    attempts_ = ATTEMPTS;
    return request();
}

void CommandSearch::reset()
{
    step_ = SEARCH_IDLE;
}

Binding binding_state(int64_t now_ms, int64_t started_ms, int64_t link_statistics_ms,
                      bool receiver_connected)
{
    if (link_statistics_ms >= started_ms + BIND_SETTLE_MS && receiver_connected) {
        return Binding::BOUND;
    }
    return now_ms - started_ms >= BIND_TIMEOUT_MS ? Binding::TIMED_OUT : Binding::RUNNING;
}

} // namespace module_command
