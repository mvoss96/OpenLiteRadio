#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/app_version.h>

#include "board/board.h"
#include "core/channels.h"
#include "core/crsf.h"

/*
 * OpenLiteRadio firmware, stage 2: power handling, controls and indicators,
 * channel frames to the ELRS module, and a line protocol on the USB serial
 * port for the live view in web/.
 *
 * Two threads: the control thread reads sticks and switches and sends one
 * channel frame per frame period of the module; the main thread does
 * everything else every 20 ms.
 *
 * Host protocol, one message per line.
 * From the radio:
 *   hello OpenLiteRadio <version> proto=1
 *   cal source=own|factory|default a0=low,centre,high a1=... a2=... a3=...
 *   state sticks=a,b,c,d sa= sb= sc= sd= se= sf= bind= setup= power= tl= tr=
 *         keys= battery= usb= on= module= ch=c1,...,c10 lock= period= frames=
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
 */

extern "C" void updater_enter(void);

/* Baud rate a host sets on the serial port to request the vendor USB updater. */
constexpr uint32_t UPDATER_REQUEST_BAUD = 1200;

constexpr int LOOP_MS = 20;
constexpr int STATE_EVERY_LOOPS = 2;   /* 25 state lines per second */
constexpr int POWER_ON_LOOPS = 50;     /* power button held for 1 s */
constexpr int POWER_OFF_LOOPS = 100;   /* power button held for 2 s */
constexpr int BIND_HOLD_LOOPS = 50;    /* BIND held for 1 s */
constexpr int SETUP_HOLD_LOOPS = 100;  /* SETUP held for 2 s */

static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* Frame period until the module reports its own: 250 Hz. */
constexpr uint32_t DEFAULT_PERIOD_US = 4000;
constexpr uint32_t MIN_PERIOD_US = 1000;
constexpr uint32_t MAX_PERIOD_US = 20000;

/* Used when neither an own nor a plausible factory calibration exists. */
constexpr core::AxisCalibration DEFAULT_CALIBRATION = { 600, 2048, 3500 };

/* Settings record with the own stick calibration: four AxisCalibration. */
constexpr uint16_t STORAGE_CALIBRATION = 1;

/* Changed by the main thread with the scheduler locked, read by the control thread. */
static core::AxisCalibration calibration[core::AXIS_COUNT];
static const char *calibration_source;

/* Written by the main thread, read by the control thread. */
static atomic_t module_on;
/* Set during a calibration run; no channel frames are sent meanwhile. */
static atomic_t calibrating;
static atomic_t frame_period_us = ATOMIC_INIT(DEFAULT_PERIOD_US);
/* Delay in microseconds for one frame after a timing frame; 0 means none pending. */
static atomic_t next_delay_us;

/*
 * Written by the control thread. The main thread copies it with the
 * scheduler locked so that the control thread cannot interrupt the copy.
 */
static struct {
    board::Inputs inputs;
    uint16_t channels[core::CHANNEL_COUNT];
    bool locked;
} control_state;

K_THREAD_STACK_DEFINE(control_stack, 1024);
static struct k_thread control_thread_data;
K_TIMER_DEFINE(frame_timer, nullptr, nullptr);

static bool radio_on;
static bool stream = true;
static bool led_from_host;
static int beep_loops;

static void beep(uint32_t frequency, int milliseconds)
{
    board::set_buzzer(frequency);
    beep_loops = milliseconds / LOOP_MS;
}

/* Read the controls, compute the channels and send them once per frame period. */
static void control_thread(void *, void *, void *)
{
    core::ThrottleLock lock;
    uint32_t period = 0;

    while (true) {
        uint32_t wanted_period = (uint32_t)atomic_get(&frame_period_us);
        uint32_t delay = (uint32_t)atomic_set(&next_delay_us, 0);

        if (wanted_period != period || delay != 0) {
            period = wanted_period;
            k_timer_start(&frame_timer, K_USEC(delay != 0 ? delay : period), K_USEC(period));
        }
        k_timer_status_sync(&frame_timer);

        board::Inputs inputs = {};
        uint16_t channels[core::CHANNEL_COUNT];

        board::read_controls(inputs);

        core::Controls controls = {
            .sticks = { inputs.sticks[0], inputs.sticks[1], inputs.sticks[2], inputs.sticks[3] },
            .sa = inputs.sa,
            .sb = inputs.sb,
            .sc = inputs.sc,
            .sd = inputs.sd,
            .se = inputs.se,
            .sf = inputs.sf,
        };

        core::compute_channels(controls, calibration, channels);

        /* Every start of the module needs the throttle at its lower end again. */
        bool module_is_on = atomic_get(&module_on) != 0;

        if (!module_is_on || atomic_get(&calibrating) != 0) {
            lock.lock();
        }

        bool locked = lock.update(channels[core::CHANNEL_THROTTLE]);

        if (module_is_on && !locked) {
            uint16_t values[crsf::CHANNELS];
            uint8_t frame[crsf::CHANNEL_FRAME_SIZE];

            for (size_t i = 0; i < crsf::CHANNELS; i++) {
                values[i] = crsf::from_microseconds(channels[i]);
            }
            crsf::encode_channels(values, frame);
            board::module_send(frame, sizeof(frame));
        }

        control_state.inputs = inputs;
        memcpy(control_state.channels, channels, sizeof(channels));
        control_state.locked = locked;
    }
}

static bool all_valid(const core::AxisCalibration values[core::AXIS_COUNT])
{
    for (int axis = 0; axis < core::AXIS_COUNT; axis++) {
        if (!core::calibration_valid(values[axis])) {
            return false;
        }
    }
    return true;
}

/* Take the own calibration, else the factory calibration if plausible, else fixed values. */
static void load_calibration()
{
    uint16_t factory[3 * core::AXIS_COUNT];

    if (board::storage_read(STORAGE_CALIBRATION, calibration, sizeof(calibration)) &&
        all_valid(calibration)) {
        calibration_source = "own";
        return;
    }

    board::read_factory_calibration(factory);
    for (int axis = 0; axis < core::AXIS_COUNT; axis++) {
        /* Stored as upper end, centre, lower end. */
        calibration[axis] = { factory[3 * axis + 2], factory[3 * axis + 1], factory[3 * axis] };
    }
    calibration_source = "factory";
    if (!all_valid(calibration)) {
        for (core::AxisCalibration &axis : calibration) {
            axis = DEFAULT_CALIBRATION;
        }
        calibration_source = "default";
    }
}

static void print_hello()
{
    printk("hello OpenLiteRadio %s proto=1\n", APP_VERSION_STRING);
    printk("cal source=%s", calibration_source);
    for (int axis = 0; axis < core::AXIS_COUNT; axis++) {
        printk(" a%d=%u,%u,%u", axis, calibration[axis].low, calibration[axis].centre,
               calibration[axis].high);
    }
    printk("\n");
}

static void show_status_led(bool locked)
{
    /* Colour as 0xRRGGBB; sent to the LED only when it changes. */
    static uint32_t shown = UINT32_MAX;
    uint32_t colour = 0;

    if (led_from_host) {
        shown = UINT32_MAX;
        return;
    }
    if (atomic_get(&calibrating) != 0) {
        colour = 0x302000; /* yellow: calibration run */
    } else if (atomic_get(&module_on) != 0 && locked) {
        colour = 0x300000; /* red: waiting for throttle down */
    } else if (radio_on) {
        colour = 0x000030; /* blue: working */
    }
    if (colour != shown) {
        shown = colour;
        board::set_status_led((uint8_t)(colour >> 16), (uint8_t)(colour >> 8), (uint8_t)colour);
    }
}

/*
 * Stick calibration: SETUP held for 2 s starts a run, a short press of SETUP
 * ends it. During the run the sticks are moved to all ends. At the end the
 * self-centring axes are released and the throttle is at its lower end.
 */
static uint16_t sweep_min[core::AXIS_COUNT];
static uint16_t sweep_max[core::AXIS_COUNT];

static void calibration_start()
{
    for (int axis = 0; axis < core::AXIS_COUNT; axis++) {
        sweep_min[axis] = UINT16_MAX;
        sweep_max[axis] = 0;
    }
    atomic_set(&calibrating, 1);
    beep(2700, 100);
}

static void calibration_track(const uint16_t sticks[core::AXIS_COUNT])
{
    for (int axis = 0; axis < core::AXIS_COUNT; axis++) {
        sweep_min[axis] = MIN(sweep_min[axis], sticks[axis]);
        sweep_max[axis] = MAX(sweep_max[axis], sticks[axis]);
    }
}

/* Use and store the result of the run if it is valid for all axes. */
static void calibration_finish(const uint16_t rest[core::AXIS_COUNT])
{
    core::AxisCalibration result[core::AXIS_COUNT];

    for (int axis = 0; axis < core::AXIS_COUNT; axis++) {
        result[axis] = core::calibration_from_sweep(sweep_min[axis], sweep_max[axis], rest[axis],
                                                    axis != core::AXIS_LEFT_VERTICAL);
    }

    bool stored = false;

    if (all_valid(result)) {
        k_sched_lock();
        memcpy(calibration, result, sizeof(calibration));
        k_sched_unlock();
        calibration_source = "own";
        /* Still locked here, so no channel frame is delayed by the flash write. */
        stored = board::storage_write(STORAGE_CALIBRATION, result, sizeof(result));
    }
    atomic_set(&calibrating, 0);
    beep(stored ? 3200 : 600, stored ? 300 : 600);
    print_hello();
}

/* Step of the search for the module's bind action, see bind_start(). */
enum BindStep { BIND_IDLE, BIND_COUNT, BIND_SEARCH };
static BindStep bind_step;

static void set_module_on(bool on)
{
    if (on) {
        atomic_set(&frame_period_us, DEFAULT_PERIOD_US);
    }
    bind_step = BIND_IDLE;
    atomic_set(&module_on, on);
    board::set_module_enabled(on);
}

static void set_radio_on(bool on)
{
    radio_on = on;
    board::set_power_latch(on);
    set_module_on(on);
    beep(on ? 2700 : 1800, 150);
}

/* Send a frame to the module from the main thread. Returns false if the line is busy. */
static bool send_to_module(const uint8_t *frame, size_t size)
{
    /* The control thread sends too and must not interrupt this call. */
    k_sched_lock();
    bool sent = board::module_send(frame, size);
    k_sched_unlock();
    return sent;
}

/*
 * Binding: the module's "Bind" action is a parameter whose number differs
 * between ELRS versions. Ask for the number of parameters, read them one by
 * one until the name matches, then start the action. The number is kept for
 * the next time.
 */
constexpr int BIND_ANSWER_LOOPS = 2; /* 40 ms for the module to answer */
constexpr int BIND_ATTEMPTS = 5;

static uint8_t bind_parameter; /* 0: not known yet */
static uint8_t parameter_count;
static uint8_t bind_candidate;
static int bind_wait_loops;
static int bind_attempts;

/* Send the request that belongs to the current step. */
static void bind_request()
{
    uint8_t frame[crsf::PARAMETER_FRAME_SIZE];

    if (bind_step == BIND_COUNT) {
        crsf::encode_ping(frame);
        send_to_module(frame, crsf::PING_FRAME_SIZE);
    } else {
        crsf::encode_parameter_read(bind_candidate, frame);
        send_to_module(frame, sizeof(frame));
    }
    bind_wait_loops = BIND_ANSWER_LOOPS;
}

static void bind_finish(bool found)
{
    uint8_t frame[crsf::PARAMETER_FRAME_SIZE];

    bind_step = BIND_IDLE;
    if (found) {
        crsf::encode_parameter_write(bind_parameter, crsf::COMMAND_START, frame);
        found = send_to_module(frame, sizeof(frame));
    }
    beep(found ? 3200 : 600, found ? 300 : 600);
}

static void bind_start()
{
    if (bind_step != BIND_IDLE || atomic_get(&module_on) == 0) {
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

/* Use a frame from the module for the running search. */
static void bind_handle_frame(const uint8_t *frame, size_t size)
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
static void bind_tick()
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
static crsf::Parser module_parser;
static uint32_t module_frames;
static crsf::LinkStatistics link_statistics;
static int32_t timing_offset_us;
static bool show_frames;

static void poll_module()
{
    uint8_t bytes[32];
    size_t count;

    while ((count = board::module_receive(bytes, sizeof(bytes))) > 0) {
        for (size_t i = 0; i < count; i++) {
            if (!module_parser.feed(bytes[i])) {
                continue;
            }
            module_frames++;
            if (show_frames) {
                printk("rx");
                for (size_t k = 0; k < module_parser.size(); k++) {
                    printk(" %02x", module_parser.frame()[k]);
                }
                printk("\n");
            }

            uint32_t interval = 0;

            if (crsf::decode_timing(module_parser.frame(), module_parser.size(), interval,
                                    timing_offset_us) &&
                interval >= MIN_PERIOD_US && interval <= MAX_PERIOD_US) {
                atomic_set(&frame_period_us, interval);
                atomic_set(&next_delay_us, crsf::next_frame_delay(interval, timing_offset_us));
            }
            bind_handle_frame(module_parser.frame(), module_parser.size());
            crsf::decode_link_statistics(module_parser.frame(), module_parser.size(),
                                         link_statistics);
        }
    }
}

/* Run one command line from the host. */
static void handle_command(char *line)
{
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
        led_from_host = arguments[0] != nullptr && strcmp(arguments[0], "auto") != 0;
        if (led_from_host) {
            board::set_status_led((uint8_t)number(0), (uint8_t)number(1), (uint8_t)number(2));
        }
    } else if (strcmp(command, "beep") == 0) {
        beep((uint32_t)number(0), (int)number(1));
    } else if (strcmp(command, "batleds") == 0) {
        board::set_battery_leds((uint8_t)number(0));
    } else if (strcmp(command, "module") == 0) {
        set_module_on(number(0) != 0);
    } else if (strcmp(command, "frames") == 0) {
        show_frames = number(0) != 0;
    } else if (strcmp(command, "bind") == 0) {
        bind_start();
    }
}

/* Collect characters from the host into lines. */
static void poll_host()
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

int main(void)
{
    board::Inputs inputs = {};
    int power_held_loops = 0;
    bool power_handled = false;
    uint32_t loops = 0;

    uint16_t channels[core::CHANNEL_COUNT] = {};
    bool locked = true;
    bool was_locked = true;
    int bind_held_loops = 0;
    int setup_held_loops = 0;

    board::init();
    load_calibration();
    print_hello();
    /* Priority -1: runs before the main thread whenever its timer expires. */
    k_thread_create(&control_thread_data, control_stack, K_THREAD_STACK_SIZEOF(control_stack),
                    control_thread, nullptr, nullptr, nullptr, -1, 0, K_NO_WAIT);

    while (true) {
        k_sched_lock();
        inputs = control_state.inputs;
        memcpy(channels, control_state.channels, sizeof(channels));
        locked = control_state.locked;
        k_sched_unlock();
        board::read_trims(inputs);

        /*
         * Power button: hold 1 s to switch on, 2 s to switch off. Started from
         * battery, the radio loses power again if the button is released
         * before the latch is set.
         */
        if (inputs.power) {
            power_held_loops++;
            if (!power_handled && !radio_on && power_held_loops >= POWER_ON_LOOPS) {
                set_radio_on(true);
                power_handled = true;
            } else if (!power_handled && radio_on && power_held_loops >= POWER_OFF_LOOPS) {
                set_radio_on(false);
                power_handled = true;
            }
        } else {
            power_held_loops = 0;
            power_handled = false;
        }

        if (beep_loops > 0 && --beep_loops == 0) {
            board::set_buzzer(0);
        }

        /* One tone when the throttle lock is released; the LED is red until then. */
        bool module_is_on = atomic_get(&module_on) != 0;

        if (module_is_on && was_locked && !locked) {
            beep(2700, 100);
        }
        was_locked = locked;
        show_status_led(locked);

        /* BIND held for 1 s starts binding. */
        if (inputs.bind) {
            if (++bind_held_loops == BIND_HOLD_LOOPS) {
                bind_start();
            }
        } else {
            bind_held_loops = 0;
        }

        /* SETUP held for 2 s starts a calibration run, a short press ends it. */
        bool is_calibrating = atomic_get(&calibrating) != 0;

        if (is_calibrating) {
            calibration_track(inputs.sticks);
        }
        if (inputs.setup) {
            if (++setup_held_loops == SETUP_HOLD_LOOPS && !is_calibrating) {
                calibration_start();
            }
        } else {
            if (is_calibrating && setup_held_loops > 0 && setup_held_loops < SETUP_HOLD_LOOPS) {
                calibration_finish(inputs.sticks);
            }
            setup_held_loops = 0;
        }

        poll_module();
        bind_tick();
        poll_host();
        if (stream && loops % STATE_EVERY_LOOPS == 0) {
            printk("state sticks=%u,%u,%u,%u sa=%u sb=%u sc=%u sd=%u se=%u sf=%u bind=%u "
                   "setup=%u power=%u tl=%u tr=%u keys=%u battery=%u usb=%u on=%u module=%u",
                   inputs.sticks[0], inputs.sticks[1], inputs.sticks[2], inputs.sticks[3],
                   inputs.sa, inputs.sb, inputs.sc, inputs.sd, inputs.se, inputs.sf,
                   inputs.bind, inputs.setup, inputs.power, inputs.trim_left,
                   inputs.trim_right, inputs.trims_valid, board::battery_raw(),
                   board::usb_supply_present(), radio_on, module_is_on);
            printk(" ch=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u lock=%u period=%u frames=%u crc=%u "
                   "lq=%u rssi=%u offset=%d bindpar=%u calib=%u\n",
                   channels[0], channels[1], channels[2], channels[3], channels[4],
                   channels[5], channels[6], channels[7], channels[8], channels[9], locked,
                   (unsigned int)atomic_get(&frame_period_us), (unsigned int)module_frames,
                   (unsigned int)module_parser.crc_errors(), link_statistics.uplink_quality,
                   link_statistics.uplink_rssi, (int)timing_offset_us, bind_parameter,
                   is_calibrating);
        }
        loops++;

        uint32_t baud = 0;

        uart_line_ctrl_get(console, UART_LINE_CTRL_BAUD_RATE, &baud);
        if (baud == UPDATER_REQUEST_BAUD) {
            updater_enter();
        }
        k_msleep(LOOP_MS);
    }
}
