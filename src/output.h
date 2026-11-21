/*
 * The control thread: reads sticks and switches, computes the channels and
 * sends them once per frame period to the ELRS module or, with the module
 * off, every 4 ms as a USB joystick or Xbox controller report. The other
 * functions are called from the main thread.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board/board.h"
#include "core/channels.h"
#include "core/mode.h"

namespace output {

/* What the control thread saw last. */
struct Snapshot {
    board::Inputs inputs;
    uint16_t channels[core::CHANNEL_COUNT];
    core::Mode mode;
    bool locked; /* the controls do not reach a model: every mode except ELRS active */
};

/* Test inputs that replace the readings, see inject(); one bit each. */
enum InjectField : uint32_t {
    INJECT_STICKS = 1u << 0,  // the four raw stick readings
    INJECT_SA = 1u << 1,      // switch SA
    INJECT_SB = 1u << 2,      // switch SB
    INJECT_SC = 1u << 3,      // switch SC
    INJECT_SD = 1u << 4,      // switch SD
    INJECT_SE = 1u << 5,      // button SE
    INJECT_SF = 1u << 6,      // button SF
};

void start();

void set_module_on(bool on);
bool module_is_on();

/* Called for every frame received from the module: it is running and answering. */
void note_module_frame();

/* True once a frame from the module arrived since it was switched on. */
bool module_answered_once();

/* The command running on the module, which sets the mode; module_link keeps it. */
void set_module_task(core::ModuleTask task);
core::ModuleTask module_task_now();

/* Frames the control thread sent to the module, and those refused because the line was busy. */
void sent_counts(uint32_t &sent, uint32_t &not_sent);

/*
 * Hold the output or release it, e.g. for a calibration run or a flash
 * write; while held the module gets safe channel frames. Returns the
 * previous state, so that a caller can restore it. After a hold the unlock
 * gesture is needed again.
 */
bool set_hold(bool on);

/* Lock the output once, e.g. after a changed setting. */
void relock();

/* Interval and offset from the module's timing frame. */
void set_frame_timing(uint32_t interval_us, int32_t offset_us);
uint32_t frame_period_us();

void snapshot(Snapshot &snapshot);

/*
 * Send a frame to the module from the main thread. A busy line is tried
 * again for up to 2 ms; returns false if it stays busy.
 */
bool send_to_module(const uint8_t *frame, size_t size);

/*
 * Replace the readings named in fields (InjectField bits) with values for
 * 10 s, only while the module is off; fields 0 ends it. Returns false if the
 * module is on. injection() returns what is active now.
 */
bool inject(const board::Inputs &values, uint32_t fields);
void injection(board::Inputs &values, uint32_t &fields);

} // namespace output
