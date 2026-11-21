/*
 * Starting a command of the ELRS module: bind, WiFi, Bluetooth joystick. No
 * hardware access.
 *
 * The commands are parameters of the module whose numbers differ between
 * ELRS versions. The search asks for the number of parameters, reads them
 * one by one until the wanted name comes, then builds the frame that starts
 * the command. Every command met on the way is kept for the next time.
 *
 * The search sends nothing itself. Each call returns a Step: a frame for the
 * caller to send, and whether the command has started or failed.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/crsf.h"
#include "core/mode.h"

namespace module_command {

/* Commands of the module that the radio starts. */
enum Command {
    COMMAND_BIND,       // "Bind": BIND held 1 s, or "bind" from the host
    COMMAND_WIFI,       // "Enable WiFi": right trim centre at switch-on, or "wifi"
    COMMAND_BLUETOOTH,  // "BLE Joystick": left trim centre at switch-on, or "ble"
    COMMAND_COUNT,      // number of commands
};

/* What a call found out about the command being started. */
enum class Result : uint8_t {
    NONE,     // still searching, or nothing to do
    STARTED,  // the frame of this step starts the command
    FAILED,   // the command was not found, or the module did not answer
};

/* What the caller has to do after a call. */
struct Step {
    uint8_t frame[crsf::PARAMETER_FRAME_SIZE]; /* frame to send to the module */
    size_t size;                               /* its length; 0: nothing to send */
    Result result;
};

/* What the module does once this command runs. */
core::ModuleTask task_of(Command command);

/* Calls of tick() the module gets to answer a request, and requests sent before giving up. */
constexpr int ANSWER_TICKS = 2;
constexpr int ATTEMPTS = 5;
/* Time for the module to answer at all after it was switched on. */
constexpr int64_t WAIT_MS = 5000;

class CommandSearch {
public:
    /*
     * Begin starting a command; only while idle(). module_answered: a frame
     * from the module arrived since it was switched on; until then the
     * search waits for up to WAIT_MS.
     */
    Step start(Command command, bool module_answered, int64_t now_ms);

    /* Call once per main loop: repeats a request the module did not answer, and gives up in the end. */
    Step tick(bool module_answered, int64_t now_ms);

    /* Call for every frame from the module. */
    Step frame(const uint8_t *frame, size_t size);

    /* End a running search, e.g. when the module is switched off. Known commands are kept. */
    void reset();

    bool idle() const { return step_ == SEARCH_IDLE; }

    /* The command start() was called with last. */
    Command wanted() const { return wanted_; }

    /* The module's parameter number of a command, 0 while not known. */
    uint8_t parameter(Command command) const { return parameter_[command]; }

private:
    /* Steps of starting a command. */
    enum SearchStep {
        SEARCH_IDLE,   // nothing to do
        SEARCH_WAIT,   // the module has not answered since it was switched on
        SEARCH_COUNT,  // asked for the number of parameters
        SEARCH_ENTRY,  // reading the entries one by one
    };

    Step request();
    Step finish(bool found);

    SearchStep step_ = SEARCH_IDLE;
    Command wanted_ = COMMAND_BIND;
    uint8_t parameter_[COMMAND_COUNT] = {};
    uint8_t parameter_count_ = 0;
    uint8_t candidate_ = 0;
    int wait_ticks_ = 0;
    int attempts_ = 0;
    int64_t wait_since_ms_ = 0;
};

/* Binding ends when a receiver answers, but not before the module has bound (about 1 s). */
constexpr int64_t BIND_SETTLE_MS = 1500;
constexpr int64_t BIND_TIMEOUT_MS = 8000;

/* How a running binding stands. */
enum class Binding : uint8_t {
    RUNNING,    // keep waiting
    BOUND,      // a receiver answered after the module had bound
    TIMED_OUT,  // no receiver within BIND_TIMEOUT_MS
};

/*
 * started_ms: when the bind command was sent. link_statistics_ms: when the
 * last link statistics arrived. receiver_connected: they are fresh and show
 * a link.
 */
Binding binding_state(int64_t now_ms, int64_t started_ms, int64_t link_statistics_ms,
                      bool receiver_connected);

} // namespace module_command
