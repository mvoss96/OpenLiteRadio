/*
 * The control thread: reads sticks and switches, computes the channels and
 * sends them once per frame period to the ELRS module and as a USB joystick
 * report. The other functions are called from the main thread.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board/board.h"
#include "core/channels.h"

namespace output {

/* What the control thread saw last. */
struct Snapshot {
    board::Inputs inputs;
    uint16_t channels[core::CHANNEL_COUNT];
    bool locked; /* no channel frames go out */
};

/* Test inputs that replace the readings, see inject(). */
enum InjectField : uint32_t {
    INJECT_STICKS = 1u << 0,
    INJECT_SA = 1u << 1,
    INJECT_SB = 1u << 2,
    INJECT_SC = 1u << 3,
    INJECT_SD = 1u << 4,
    INJECT_SE = 1u << 5,
    INJECT_SF = 1u << 6,
};

void start();

void set_module_on(bool on);
bool module_is_on();

/*
 * Stop or allow channel frames, e.g. for a calibration run or a flash write.
 * Returns the previous state, so that a caller can restore it. After a hold
 * the unlock gesture is needed again.
 */
bool set_hold(bool on);

/* Lock the output once, e.g. after a changed setting. */
void relock();

/* Interval and offset from the module's timing frame. */
void set_frame_timing(uint32_t interval_us, int32_t offset_us);
uint32_t frame_period_us();

void snapshot(Snapshot &snapshot);

/* Send a frame to the module from the main thread. Returns false if the line is busy. */
bool send_to_module(const uint8_t *frame, size_t size);

/*
 * Replace the readings named in fields (InjectField bits) with values for
 * 10 s, only while the module is off; fields 0 ends it. Returns false if the
 * module is on. injection() returns what is active now.
 */
bool inject(const board::Inputs &values, uint32_t fields);
void injection(board::Inputs &values, uint32_t &fields);

} // namespace output
