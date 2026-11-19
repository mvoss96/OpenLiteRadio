#include "output.h"

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "core/crsf.h"
#include "core/joystick.h"
#include "core/settings.h"

/* Sends joystick::REPORT_SIZE bytes, see src/usb_device.c. */
extern "C" void usb_joystick_send(const uint8_t *report);
/* True while a PC has set up the joystick interface. */
extern "C" bool usb_joystick_ready(void);
static_assert(joystick::REPORT_SIZE == DT_PROP(DT_NODELABEL(joystick_hid), in_report_size),
              "src/core/joystick.h, src/usb_device.c and the devicetree must agree on the report size");

namespace output {

namespace {

/* Frame period until the module reports its own: 250 Hz. */
constexpr uint32_t DEFAULT_PERIOD_US = 4000;
constexpr int64_t INJECT_MS = 10000;
/* Pings while locked; the module drops the connection after 1 s without frames. */
constexpr int64_t PING_INTERVAL_MS = 20;

/*
 * Written by the main thread, read by the control thread. The same goes for
 * settings::values.
 */
atomic_t module_on;
/* While set no channel frames are sent: calibration run, writing to flash. */
atomic_t hold_output;
/* Set to lock the output once; the control thread clears it. */
atomic_t relock_requested;
/* Set once a frame from the module arrived since it was switched on. */
atomic_t module_answered;
/* Frames the control thread handed to the UART, and those refused because the previous one was still being sent. */
atomic_t frames_sent;
atomic_t frames_not_sent;
atomic_t period_us = ATOMIC_INIT(DEFAULT_PERIOD_US);
/* Microseconds to add to the next frame period after a timing frame; 0 means none pending. */
atomic_t frame_correction_us;

/*
 * Written by the control thread. The main thread copies it with the
 * scheduler locked so that the control thread cannot interrupt the copy.
 */
Snapshot last;

/*
 * Test inputs from the host: replace the readings named in fields, so that
 * channels and joystick can be checked without touching the radio. Written
 * by the main thread with the scheduler locked.
 */
struct {
    uint32_t fields;
    board::Inputs values;
    int64_t until;
} injected;

K_THREAD_STACK_DEFINE(control_stack, 1024);
struct k_thread control_thread_data;

void apply_injection(board::Inputs &inputs)
{
    if (injected.fields == 0) {
        return;
    }
    if (atomic_get(&module_on) != 0 || k_uptime_get() > injected.until) {
        injected.fields = 0;
        return;
    }
    const board::Inputs &v = injected.values;

    if (injected.fields & INJECT_STICKS) {
        memcpy(inputs.sticks, v.sticks, sizeof(inputs.sticks));
    }
    inputs.sa = (injected.fields & INJECT_SA) ? v.sa : inputs.sa;
    inputs.sb = (injected.fields & INJECT_SB) ? v.sb : inputs.sb;
    inputs.sc = (injected.fields & INJECT_SC) ? v.sc : inputs.sc;
    inputs.sd = (injected.fields & INJECT_SD) ? v.sd : inputs.sd;
    inputs.se = (injected.fields & INJECT_SE) ? v.se : inputs.se;
    inputs.sf = (injected.fields & INJECT_SF) ? v.sf : inputs.sf;
}

/* Send one frame from the control thread and count it. */
void send_frame(const uint8_t *frame, size_t size)
{
    atomic_inc(board::module_send(frame, size) ? &frames_sent : &frames_not_sent);
}

/* Send one channel frame with values in microseconds. */
void send_channels(const uint16_t channels[core::CHANNEL_COUNT])
{
    uint16_t values[crsf::CHANNELS];
    uint8_t frame[crsf::CHANNEL_FRAME_SIZE];

    for (size_t i = 0; i < crsf::CHANNELS; i++) {
        values[i] = crsf::from_microseconds(channels[i]);
    }
    crsf::encode_channels(values, frame);
    send_frame(frame, sizeof(frame));
}

/* Read the controls, compute the channels and send them once per frame period. */
void control_thread(void *, void *, void *)
{
    core::ModeMachine machine;
    int64_t last_ping_ms = 0;
    int64_t next_us = k_ticks_to_us_floor64(k_uptime_ticks());

    while (true) {
        /*
         * Each frame time follows from the previous one, not from the moment
         * this loop gets here, so neither the work below nor the rounding to
         * kernel ticks (100 us) adds up from frame to frame. A correction
         * from the module shifts one period.
         */
        next_us += (int64_t)atomic_get(&period_us) + (int64_t)atomic_set(&frame_correction_us, 0);

        int64_t now_us = k_ticks_to_us_floor64(k_uptime_ticks());

        if (next_us < now_us) {
            next_us = now_us; /* fell behind, e.g. while the scheduler was locked */
        }
        k_sleep(K_TIMEOUT_ABS_US(next_us));

        board::Inputs inputs = {};
        uint16_t channels[core::CHANNEL_COUNT];

        board::read_controls(inputs);
        apply_injection(inputs);

        core::Controls controls = {
            .sticks = { inputs.sticks[0], inputs.sticks[1], inputs.sticks[2], inputs.sticks[3] },
            .sa = inputs.sa,
            .sb = inputs.sb,
            .sc = inputs.sc,
            .sd = inputs.sd,
            .se = inputs.se,
            .sf = inputs.sf,
        };

        core::compute_channels(controls, settings::values, channels);

        core::ModeInputs mode_inputs = {
            .module_on = atomic_get(&module_on) != 0,
            .usb_host = usb_joystick_ready(),
            .held = atomic_get(&hold_output) != 0,
            .relock = atomic_set(&relock_requested, 0) != 0,
            .throttle = core::source_value(core::SOURCE_THROTTLE, controls, settings::values),
            .unlock_mode = settings::values[settings::UNLOCK_MODE],
        };
        core::Mode mode = machine.update(mode_inputs);

        /*
         * The joystick follows the controls only in the joystick mode. In
         * every other mode it reports all channels centred, so that a
         * simulator still open on the PC does not keep the last positions.
         */
        uint8_t report[joystick::REPORT_SIZE];

        if (mode == core::Mode::USB_JOYSTICK) {
            joystick::fill_report(channels, report);
        } else {
            uint16_t centred[core::CHANNEL_COUNT];

            for (uint16_t &value : centred) {
                value = core::CHANNEL_CENTRE;
            }
            joystick::fill_report(centred, report);
        }
        usb_joystick_send(report);

        bool locked = mode != core::Mode::ELRS_ACTIVE;

        if (!locked) {
            send_channels(channels);
        } else if (mode == core::Mode::ELRS_LOCKED) {
            /*
             * Keep the module answering without controlling the model: safe
             * channel frames until it has answered once, so that it never
             * transmits its zero-initialised channels, then pings only. The
             * module stops sending channel data 1 s after the last channel
             * frame, and the receiver enters its own failsafe.
             */
            if (atomic_get(&module_answered) == 0) {
                uint16_t safe[core::CHANNEL_COUNT];

                core::compute_channels(core::safe_controls(settings::values), settings::values,
                                       safe);
                send_channels(safe);
            } else if (k_uptime_get() - last_ping_ms >= PING_INTERVAL_MS) {
                uint8_t ping[crsf::PING_FRAME_SIZE];

                crsf::encode_ping(ping);
                send_frame(ping, sizeof(ping));
                last_ping_ms = k_uptime_get();
            }
        }

        last.inputs = inputs;
        memcpy(last.channels, channels, sizeof(channels));
        last.mode = mode;
        last.locked = locked;
    }
}

} // namespace

void start()
{
    /* Priority -1: runs before the main thread whenever its timer expires. */
    k_thread_create(&control_thread_data, control_stack, K_THREAD_STACK_SIZEOF(control_stack),
                    control_thread, nullptr, nullptr, nullptr, -1, 0, K_NO_WAIT);
}

void set_module_on(bool on)
{
    if (on) {
        atomic_set(&period_us, DEFAULT_PERIOD_US);
    }
    atomic_clear(&module_answered);
    atomic_set(&module_on, on);
    board::set_module_enabled(on);
}

void sent_counts(uint32_t &sent, uint32_t &not_sent)
{
    sent = (uint32_t)atomic_get(&frames_sent);
    not_sent = (uint32_t)atomic_get(&frames_not_sent);
}

void note_module_frame()
{
    atomic_set(&module_answered, 1);
}

bool module_is_on()
{
    return atomic_get(&module_on) != 0;
}

bool set_hold(bool on)
{
    return atomic_set(&hold_output, on) != 0;
}

void relock()
{
    atomic_set(&relock_requested, 1);
}

void set_frame_timing(uint32_t interval_us, int32_t offset_us)
{
    atomic_set(&period_us, interval_us);
    atomic_set(&frame_correction_us,
               (atomic_val_t)crsf::next_frame_delay(interval_us, offset_us) - (atomic_val_t)interval_us);
}

uint32_t frame_period_us()
{
    return (uint32_t)atomic_get(&period_us);
}

void snapshot(Snapshot &snapshot)
{
    k_sched_lock();
    snapshot = last;
    k_sched_unlock();
}

bool send_to_module(const uint8_t *frame, size_t size)
{
    /* The control thread sends too and must not interrupt this call. */
    k_sched_lock();
    bool sent = board::module_send(frame, size);
    k_sched_unlock();
    return sent;
}

bool inject(const board::Inputs &values, uint32_t fields)
{
    if (fields != 0 && module_is_on()) {
        return false;
    }
    k_sched_lock();
    injected.values = values;
    injected.fields = fields;
    injected.until = k_uptime_get() + INJECT_MS;
    k_sched_unlock();
    return true;
}

void injection(board::Inputs &values, uint32_t &fields)
{
    k_sched_lock();
    values = injected.values;
    fields = injected.fields;
    k_sched_unlock();
}

} // namespace output
