/* Tests for src/core, built and run on the PC by test.ps1. */

#include <cstdio>
#include <cstring>

#include "core/channels.h"
#include "core/crsf.h"
#include "core/joystick.h"
#include "core/led.h"
#include "core/mode.h"
#include "core/settings.h"

static int failures;

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            std::printf("FAILED line %d: %s\n", __LINE__, #condition);      \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static void test_crc()
{
    /* Device ping as sent to the module: the CRC covers 28 00 EA. */
    const uint8_t ping[] = { 0x28, 0x00, 0xEA };

    CHECK(crsf::crc8(ping, sizeof(ping)) == 0x54);
}

static void test_conversion()
{
    CHECK(crsf::from_microseconds(1500) == 992);
    CHECK(crsf::from_microseconds(988) == 172);
    CHECK(crsf::from_microseconds(2012) == 1811);
}

static void test_channel_frame()
{
    /* All channels at the centre. */
    const uint8_t expected[crsf::CHANNEL_FRAME_SIZE] = {
        0xEE, 0x18, 0x16, 0xE0, 0x03, 0x1F, 0xF8, 0xC0, 0x07, 0x3E, 0xF0, 0x81, 0x0F,
        0x7C, 0xE0, 0x03, 0x1F, 0xF8, 0xC0, 0x07, 0x3E, 0xF0, 0x81, 0x0F, 0x7C, 0xAD,
    };
    uint16_t channels[crsf::CHANNELS];
    uint8_t frame[crsf::CHANNEL_FRAME_SIZE];

    for (uint16_t &channel : channels) {
        channel = 992;
    }
    crsf::encode_channels(channels, frame);
    CHECK(std::memcmp(frame, expected, sizeof(expected)) == 0);

    /* Each channel lands in its own 11 bits. */
    for (size_t i = 0; i < crsf::CHANNELS; i++) {
        channels[i] = (uint16_t)(100 + 100 * i);
    }
    crsf::encode_channels(channels, frame);
    for (size_t i = 0; i < crsf::CHANNELS; i++) {
        size_t bit = 11 * i;
        uint32_t window = frame[3 + bit / 8] | (frame[4 + bit / 8] << 8) |
                          ((uint32_t)frame[5 + bit / 8] << 16);

        CHECK(((window >> (bit % 8)) & 0x7FF) == channels[i]);
    }
}

/* Timing frame as received from the module on the radio: 250 Hz. */
static const uint8_t TIMING_FRAME[] = { 0xEA, 0x0D, 0x3A, 0xEA, 0xEE, 0x10, 0x00, 0x00,
                                        0x9C, 0x40, 0xFF, 0xFF, 0xFC, 0x18, 0xEC };

static void test_parser()
{
    crsf::Parser parser;
    int frames = 0;

    /* Noise before the frame, then the frame twice. */
    const uint8_t noise[] = { 0x00, 0x72, 0x73, 0x74 };

    for (uint8_t byte : noise) {
        CHECK(!parser.feed(byte));
    }
    for (int repeat = 0; repeat < 2; repeat++) {
        for (uint8_t byte : TIMING_FRAME) {
            if (parser.feed(byte)) {
                frames++;
                CHECK(parser.size() == sizeof(TIMING_FRAME));
                CHECK(parser.type() == crsf::TYPE_TIMING);

                uint32_t interval = 0;
                int32_t offset = 0;

                CHECK(crsf::decode_timing(parser.frame(), parser.size(), interval, offset));
                CHECK(interval == 4000);
                /* FF FF FC 18 is -1000 in units of 0.1 us. */
                CHECK(offset == -100);
            }
        }
    }
    CHECK(frames == 2);
    CHECK(parser.crc_errors() == 0);

    /* A damaged frame is counted and dropped; the next one is accepted. */
    uint8_t damaged[sizeof(TIMING_FRAME)];

    std::memcpy(damaged, TIMING_FRAME, sizeof(damaged));
    damaged[8] ^= 0x01;
    for (uint8_t byte : damaged) {
        CHECK(!parser.feed(byte));
    }
    CHECK(parser.crc_errors() == 1);

    bool accepted = false;

    for (uint8_t byte : TIMING_FRAME) {
        accepted = parser.feed(byte);
    }
    CHECK(accepted);
}

static void test_frame_delay()
{
    /* At the target nothing changes; half of the difference is corrected. */
    CHECK(crsf::next_frame_delay(4000, crsf::SYNC_TARGET_US) == 4000);
    CHECK(crsf::next_frame_delay(4000, crsf::SYNC_TARGET_US + 600) == 4300);
    CHECK(crsf::next_frame_delay(4000, crsf::SYNC_TARGET_US - 600) == 3700);
    /* Limited to a quarter of the interval. */
    /* Limited to a quarter of the interval. */
    CHECK(crsf::next_frame_delay(4000, 3000) == 5000);
    CHECK(crsf::next_frame_delay(4000, -3000) == 3000);
}

static void test_parameters()
{
    /* The ping the hardware test sent and the module answered. */
    const uint8_t expected_ping[] = { 0xEE, 0x04, 0x28, 0x00, 0xEA, 0x54 };
    uint8_t ping[crsf::PING_FRAME_SIZE];

    crsf::encode_ping(ping);
    CHECK(std::memcmp(ping, expected_ping, sizeof(ping)) == 0);

    uint8_t read[crsf::PARAMETER_FRAME_SIZE];

    crsf::encode_parameter_read(7, read);
    CHECK(read[0] == 0xEE && read[1] == 6 && read[2] == 0x2C && read[3] == 0xEE && read[4] == 0xEA);
    CHECK(read[5] == 7 && read[6] == 0);
    CHECK(read[7] == crsf::crc8(&read[2], 5));

    uint8_t write[crsf::PARAMETER_FRAME_SIZE];

    crsf::encode_parameter_write(7, crsf::COMMAND_START, write);
    CHECK(write[2] == 0x2D && write[5] == 7 && write[6] == 1);
    CHECK(write[7] == crsf::crc8(&write[2], 5));

    /* Device information: name, serial, hardware, software, 21 parameters, protocol 0. */
    const uint8_t info[] = { 0xEA, 0x00, 0x29, 0xEA, 0xEE, 'L', 'R', '4', 0,   'E', 'L', 'R',
                             'S',  0,    0,    0,    0,    0,   3,   6,   4,   21,  0,   0 };
    uint8_t count = 0;

    CHECK(crsf::decode_device_info(info, sizeof(info), count));
    CHECK(count == 21);
    CHECK(!crsf::decode_device_info(TIMING_FRAME, sizeof(TIMING_FRAME), count));

    /* Parameter entry 16, no further parts, parent 0, type command, name "Bind". */
    const uint8_t entry[] = { 0xEA, 0x00, 0x2B, 0xEA, 0xEE, 16,  0, 0, 13,
                              'B',  'i',  'n',  'd',  0,    0,   200, 0, 0 };
    uint8_t index = 0;
    uint8_t type = 0;
    const char *name = nullptr;

    CHECK(crsf::decode_parameter_entry(entry, sizeof(entry), index, type, name));
    CHECK(index == 16 && type == crsf::PARAMETER_TYPE_COMMAND);
    CHECK(std::strcmp(name, "Bind") == 0);

    /* A name without its zero byte inside the frame is refused. */
    const uint8_t open_name[] = { 0xEA, 0x00, 0x2B, 0xEA, 0xEE, 16, 0, 0, 13, 'B', 'i', 'n', 'd' };

    CHECK(!crsf::decode_parameter_entry(open_name, sizeof(open_name), index, type, name));
}

static void test_link_statistics()
{
    uint8_t frame[14] = { 0xEA, 0x0C, 0x14, (uint8_t)-67, (uint8_t)-70, 98, 5, 0, 4, 1, 0, 0, 0 };
    crsf::LinkStatistics statistics = {};

    frame[13] = crsf::crc8(&frame[2], 11);
    CHECK(crsf::decode_link_statistics(frame, sizeof(frame), statistics));
    CHECK(statistics.uplink_rssi == 67);
    CHECK(statistics.uplink_quality == 98);
    CHECK(!crsf::decode_link_statistics(TIMING_FRAME, sizeof(TIMING_FRAME), statistics));
}

static void test_scale_axis()
{
    const core::AxisCalibration calibration = { 600, 2000, 3500 };

    /* Centred axis: ends, centre, clamping beyond the ends. */
    CHECK(core::scale_axis(600, calibration, true, false) == 988);
    CHECK(core::scale_axis(2000, calibration, true, false) == 1500);
    CHECK(core::scale_axis(3500, calibration, true, false) == 2012);
    CHECK(core::scale_axis(0, calibration, true, false) == 988);
    CHECK(core::scale_axis(4095, calibration, true, false) == 2012);

    /* The two halves have different slopes. */
    CHECK(core::scale_axis(1300, calibration, true, false) == 1244);
    CHECK(core::scale_axis(2750, calibration, true, false) == 1756);

    /* Dead zone: 1 % around the centre, and a value just outside it. */
    CHECK(core::scale_axis(2010, calibration, true, false) == 1500);
    CHECK(core::scale_axis(1990, calibration, true, false) == 1500);
    CHECK(core::scale_axis(2030, calibration, true, false) == 1510);

    /* Dead zone as a parameter: none, and 10 % of the half range (51 us). */
    CHECK(core::scale_axis(2010, calibration, true, false, 0) == 1503);
    CHECK(core::scale_axis(2130, calibration, true, false, 10) == 1500);
    CHECK(core::scale_axis(2200, calibration, true, false, 10) == 1568);
    CHECK(core::scale_axis(600, calibration, false, false, 10) == 988);

    /* Inverted. */
    CHECK(core::scale_axis(600, calibration, true, true) == 2012);
    CHECK(core::scale_axis(3500, calibration, true, true) == 988);
    CHECK(core::scale_axis(2000, calibration, true, true) == 1500);

    /* Not centred: one straight line, no dead zone, the centre value is ignored. */
    CHECK(core::scale_axis(600, calibration, false, false) == 988);
    CHECK(core::scale_axis(3500, calibration, false, false) == 2012);
    CHECK(core::scale_axis(2050, calibration, false, false) == 1500);
    CHECK(core::scale_axis(2060, calibration, false, false) == 1503);

    /* Unusable calibration: centred axes rest, the throttle stays at its lower end. */
    const core::AxisCalibration erased = { 0xFFFF, 0xFFFF, 0xFFFF };
    const core::AxisCalibration reversed = { 3500, 2000, 600 };

    CHECK(!core::calibration_valid(erased));
    CHECK(!core::calibration_valid(reversed));
    CHECK(core::calibration_valid(calibration));
    CHECK(core::scale_axis(3000, erased, true, false) == 1500);
    CHECK(core::scale_axis(3000, reversed, false, false) == 988);
}

static void test_calibration_from_sweep()
{
    /* Centred axis: ends 25 counts inward, the rest reading is the centre. */
    core::AxisCalibration centred = core::calibration_from_sweep(616, 3586, 2080, true);

    CHECK(centred.low == 641 && centred.centre == 2080 && centred.high == 3561);
    CHECK(core::calibration_valid(centred));

    /* Throttle: the rest reading above the smallest one becomes the lower end. */
    core::AxisCalibration throttle = core::calibration_from_sweep(544, 3577, 673, false);

    CHECK(throttle.low == 698 && throttle.high == 3552);
    CHECK(core::calibration_valid(throttle));
    CHECK(core::scale_axis(673, throttle, false, false) == 988);

    /* Not moved, throttle left in the middle, centred stick held at an end. */
    CHECK(!core::calibration_valid(core::calibration_from_sweep(2080, 2085, 2082, true)));
    CHECK(!core::calibration_valid(core::calibration_from_sweep(544, 3577, 2000, false)));
    CHECK(!core::calibration_valid(core::calibration_from_sweep(616, 3586, 3580, true)));
}

static void test_settings()
{
    char key[settings::KEY_SIZE];

    /* Positions and keys agree. */
    CHECK(settings::find("unlock.mode") == settings::UNLOCK_MODE);
    CHECK(settings::find("buzzer.on") == settings::BUZZER_ON);
    CHECK(settings::find("ch.1.source") == settings::CHANNEL_FIRST);
    CHECK(settings::find("ch.16.reverse") == settings::CALIBRATION_FIRST - 1);
    CHECK(settings::find("cal.0.low") == settings::CALIBRATION_FIRST);
    CHECK(settings::find("cal.3.deadzone") == settings::COUNT - 1);
    CHECK(settings::find("cal.1.deadzone") == settings::CALIBRATION_FIRST + 4 + 3);
    CHECK(settings::find("ch.17.source") == -1);
    CHECK(settings::find("ch.0.source") == -1);
    settings::key(settings::CHANNEL_FIRST + 3 * 9 + 1, key);
    CHECK(std::strcmp(key, "ch.10.weight") == 0);

    /* Storage numbers are distinct. */
    for (int a = 0; a < settings::COUNT; a++) {
        for (int b = a + 1; b < settings::COUNT; b++) {
            CHECK(settings::info(a).id != settings::info(b).id);
        }
    }
    CHECK(settings::info(settings::UNLOCK_MODE).id == 0x100);
    CHECK(settings::info(settings::find("ch.2.weight")).id == 0x209);
    /* Adding the dead zone kept the numbers of the stored calibration values. */
    CHECK(settings::info(settings::find("cal.3.high")).id == 0x31a);
    CHECK(settings::info(settings::find("cal.3.deadzone")).id == 0x31b);
    CHECK(settings::info(settings::find("cal.2.deadzone")).default_value == 1);

    /* Defaults: channels 1 to 10 follow sources 1 to 10, the others none. */
    settings::set_defaults();
    CHECK(settings::values[settings::UNLOCK_MODE] == 1);
    CHECK(settings::values[settings::find("ch.3.source")] == core::SOURCE_THROTTLE);
    CHECK(settings::values[settings::find("ch.10.source")] == core::SOURCE_SF);
    CHECK(settings::values[settings::find("ch.11.source")] == core::SOURCE_NONE);
    CHECK(settings::values[settings::find("ch.11.weight")] == 100);

    /* Values outside the range are refused. */
    CHECK(settings::set(settings::UNLOCK_MODE, 2));
    CHECK(!settings::set(settings::UNLOCK_MODE, 3));
    CHECK(!settings::set(settings::UNLOCK_MODE, -1));
    CHECK(settings::values[settings::UNLOCK_MODE] == 2);
    CHECK(!settings::set(settings::COUNT, 0));
}

/* Default settings with the same calibration for all four axes. */
static void use_test_settings()
{
    settings::set_defaults();
    for (int axis = 0; axis < core::AXIS_COUNT; axis++) {
        int16_t *values = &settings::values[settings::CALIBRATION_FIRST + 4 * axis];

        values[settings::CALIBRATION_LOW] = 600;
        values[settings::CALIBRATION_CENTRE] = 2000;
        values[settings::CALIBRATION_HIGH] = 3500;
    }
}

static void test_channels()
{
    core::Controls controls = {};
    uint16_t channels[core::CHANNEL_COUNT];

    use_test_settings();

    /* Throttle down, other axes at rest, all switches in position 0. */
    controls.sticks[core::AXIS_LEFT_VERTICAL] = 600;
    controls.sticks[core::AXIS_LEFT_HORIZONTAL] = 2000;
    controls.sticks[core::AXIS_RIGHT_VERTICAL] = 2000;
    controls.sticks[core::AXIS_RIGHT_HORIZONTAL] = 2000;
    core::compute_channels(controls, settings::values, channels);
    CHECK(channels[0] == 1500 && channels[1] == 1500 && channels[2] == 988 && channels[3] == 1500);
    for (int channel = 4; channel < 10; channel++) {
        CHECK(channels[channel] == 988);
    }
    for (int channel = 10; channel < core::CHANNEL_COUNT; channel++) {
        CHECK(channels[channel] == 1500);
    }

    /* Full throttle; right stick up and right; left stick right. */
    controls.sticks[core::AXIS_LEFT_VERTICAL] = 3500;
    controls.sticks[core::AXIS_LEFT_HORIZONTAL] = 600;
    controls.sticks[core::AXIS_RIGHT_VERTICAL] = 600;
    controls.sticks[core::AXIS_RIGHT_HORIZONTAL] = 3500;
    controls.sa = 1;
    controls.sb = 1;
    controls.sc = 2;
    controls.sd = 1;
    controls.se = true;
    core::compute_channels(controls, settings::values, channels);
    CHECK(channels[0] == 2012 && channels[1] == 2012 && channels[2] == 2012 && channels[3] == 2012);
    CHECK(channels[4] == 2012 && channels[5] == 1500 && channels[6] == 2012);
    CHECK(channels[7] == 2012 && channels[8] == 2012 && channels[9] == 988);

    /* Custom: another source, half weight and reverse on channel 1; a switch on channel 12. */
    settings::values[settings::MAP_MODE] = core::MAPPING_CUSTOM;
    settings::values[settings::find("ch.1.source")] = core::SOURCE_THROTTLE;
    settings::values[settings::find("ch.1.weight")] = 50;
    settings::values[settings::find("ch.1.reverse")] = 1;
    settings::values[settings::find("ch.12.source")] = core::SOURCE_SC;
    core::compute_channels(controls, settings::values, channels);
    CHECK(channels[0] == 1244);
    CHECK(channels[11] == 2012);
    CHECK(core::source_value(core::SOURCE_THROTTLE, controls, settings::values) == 2012);
}

static void test_led()
{
    using core::Mode;
    core::LedInputs in = { Mode::OFF, false, false };

    CHECK(core::led_colour(in, 0) == 0);
    in.mode = Mode::USB_JOYSTICK;
    CHECK(core::led_colour(in, 0) == 0x300500);
    in.mode = Mode::ELRS_LOCKED;
    CHECK(core::led_colour(in, 0) == 0x300000);

    /* Sending with a receiver: blue, steady. */
    in.mode = Mode::ELRS_ACTIVE;
    in.receiver_connected = true;
    CHECK(core::led_colour(in, 0) == 0x000030 && core::led_colour(in, 1000) == 0x000030);

    /* Without a receiver: blue pulsing, a fifth of the brightness at 0 ms, full at 1 s, never off. */
    in.receiver_connected = false;
    CHECK(core::led_colour(in, 0) == 0x000009);
    CHECK(core::led_colour(in, 1000) == 0x000030);
    CHECK(core::led_colour(in, 500) != core::led_colour(in, 1000));
    for (uint32_t ms = 0; ms < 2000; ms += 20) {
        CHECK(core::led_colour(in, ms) != 0 && (core::led_colour(in, ms) & 0xffff00u) == 0);
    }

    /* Calibration wins over every mode: yellow and white, half a second each. */
    in.calibrating = true;
    CHECK(core::led_colour(in, 0) == 0x302000);
    CHECK(core::led_colour(in, 499) == 0x302000);
    CHECK(core::led_colour(in, 500) == 0x202020);
    CHECK(core::led_colour(in, 1000) == 0x302000);
}

static void test_safe_controls()
{
    uint16_t channels[core::CHANNEL_COUNT];

    /* Default mapping AETR: throttle on channel 3 low, sticks centred, switches low. */
    settings::set_defaults();
    core::compute_channels(core::safe_controls(settings::values), settings::values, channels);
    CHECK(channels[0] == 1500 && channels[1] == 1500 && channels[3] == 1500);
    CHECK(channels[2] == 988);
    for (int i = 4; i < 10; i++) {
        CHECK(channels[i] == 988);
    }
    /* Channels without a source stay at the centre. */
    CHECK(channels[10] == 1500);

    /* A Custom TAER with the throttle reversed: the safe value follows the mapping. */
    settings::values[settings::MAP_MODE] = core::MAPPING_CUSTOM;
    settings::values[settings::find("ch.1.source")] = core::SOURCE_THROTTLE;
    settings::values[settings::find("ch.1.reverse")] = 1;
    settings::values[settings::find("ch.3.source")] = core::SOURCE_AILERON;
    core::compute_channels(core::safe_controls(settings::values), settings::values, channels);
    CHECK(channels[0] == 2012);
    CHECK(channels[2] == 1500);
    settings::set_defaults();
}

static void test_mode()
{
    using core::Mode;
    core::ModeMachine machine;
    core::ModeInputs in = {};

    in.unlock_mode = 1;
    in.throttle = 988;

    /* Without the module: off, or the USB joystick at a PC. */
    CHECK(machine.update(in) == Mode::OFF);
    in.usb_host = true;
    CHECK(machine.update(in) == Mode::USB_JOYSTICK);

    /* Module on with the throttle up: locked until the throttle is low. */
    in.module_on = true;
    in.throttle = 1500;
    CHECK(machine.update(in) == Mode::ELRS_LOCKED);
    CHECK(machine.update(in) == Mode::ELRS_LOCKED);
    in.throttle = 988;
    CHECK(machine.update(in) == Mode::ELRS_ACTIVE);
    CHECK(machine.update(in) == Mode::ELRS_ACTIVE);

    /* A changed setting locks; with the throttle still low mode 1 opens again in the next period. */
    in.relock = true;
    in.throttle = 1500;
    CHECK(machine.update(in) == Mode::ELRS_LOCKED);
    in.relock = false;
    CHECK(machine.update(in) == Mode::ELRS_LOCKED);
    in.throttle = 988;
    CHECK(machine.update(in) == Mode::ELRS_ACTIVE);

    /* A hold keeps it locked although the throttle is low (fixed in 0.7.2). */
    in.held = true;
    CHECK(machine.update(in) == Mode::ELRS_LOCKED);
    CHECK(machine.update(in) == Mode::ELRS_LOCKED);
    in.held = false;
    CHECK(machine.update(in) == Mode::ELRS_ACTIVE);

    /* Mode 2 needs the upper end again after a hold. */
    in.unlock_mode = 2;
    in.held = true;
    in.throttle = 2012;
    CHECK(machine.update(in) == Mode::ELRS_LOCKED);
    in.held = false;
    in.throttle = 988;
    CHECK(machine.update(in) == Mode::ELRS_LOCKED);
    in.throttle = 2012;
    CHECK(machine.update(in) == Mode::ELRS_LOCKED);
    in.throttle = 988;
    CHECK(machine.update(in) == Mode::ELRS_ACTIVE);

    /* Module off leads back to USB, and switching on again locks. */
    in.module_on = false;
    CHECK(machine.update(in) == Mode::USB_JOYSTICK);
    in.usb_host = false;
    CHECK(machine.update(in) == Mode::OFF);
    in.module_on = true;
    in.throttle = 2012;
    CHECK(machine.update(in) == Mode::ELRS_LOCKED);
    /* Mode 0 opens wherever the throttle is. */
    in.unlock_mode = 0;
    CHECK(machine.update(in) == Mode::ELRS_ACTIVE);

    CHECK(std::strcmp(core::mode_name(Mode::ELRS_ACTIVE), "elrs_active") == 0);
}

static void test_throttle_lock()
{
    core::ThrottleLock lock;

    /* Mode 1: throttle at its lower end. */
    CHECK(lock.update(1500, 1));
    CHECK(lock.update(1009, 1));
    CHECK(!lock.update(1008, 1));
    /* Once released it stays released. */
    CHECK(!lock.update(2012, 1));
    lock.lock();
    CHECK(lock.update(1200, 1));
    CHECK(!lock.update(988, 1));

    /* Mode 2: the lower end counts only after the upper end. */
    lock.lock();
    CHECK(lock.update(988, 2));
    CHECK(lock.update(2000, 2));
    CHECK(lock.update(1500, 2));
    CHECK(!lock.update(1000, 2));
    /* Locking again forgets the upper end. */
    lock.lock();
    CHECK(lock.update(988, 2));

    /* Mode 0: unlocked wherever the throttle is. */
    CHECK(!lock.update(1700, 0));
}

static void test_mapping()
{
    uint16_t channels[core::CHANNEL_COUNT];
    /* Throttle up, aileron right, SA on; the other controls at rest. */
    core::Controls controls = {};

    controls.sticks[core::AXIS_LEFT_VERTICAL] = 3500;
    controls.sticks[core::AXIS_LEFT_HORIZONTAL] = 2048;
    controls.sticks[core::AXIS_RIGHT_VERTICAL] = 2048;
    controls.sticks[core::AXIS_RIGHT_HORIZONTAL] = 3500;
    controls.sa = 1;

    /* AETR is the default, and the default ch.N.* settings are AETR as well. */
    settings::set_defaults();
    CHECK(settings::values[settings::MAP_MODE] == core::MAPPING_AETR);
    CHECK(settings::find("map.mode") == settings::MAP_MODE);
    core::compute_channels(controls, settings::values, channels);
    CHECK(channels[0] == 2012 && channels[2] == 2012 && channels[4] == 2012);

    /* A Custom assignment is ignored while AETR or TAER is chosen and kept for later. */
    settings::set(settings::find("ch.1.source"), core::SOURCE_RUDDER);
    settings::set(settings::find("ch.3.reverse"), 1);
    core::compute_channels(controls, settings::values, channels);
    CHECK(channels[0] == 2012 && channels[2] == 2012);

    settings::set(settings::MAP_MODE, core::MAPPING_TAER);
    core::compute_channels(controls, settings::values, channels);
    CHECK(channels[0] == 2012); /* throttle */
    CHECK(channels[1] == 2012); /* aileron */
    CHECK(channels[2] == 1500); /* elevator */
    CHECK(channels[4] == 2012); /* SA */

    settings::set(settings::MAP_MODE, core::MAPPING_CUSTOM);
    core::compute_channels(controls, settings::values, channels);
    CHECK(channels[0] == 1500); /* rudder at rest */
    CHECK(channels[2] == 988);  /* throttle reversed */
    CHECK(!settings::set(settings::MAP_MODE, 3));
    settings::set_defaults();
}

static void test_joystick()
{
    uint16_t channels[16] = { 988, 1500, 2012, 900, 2100, 1000, 1500, 1500,
                              2012, 988, 1501, 1500, 988, 988, 988, 2012 };
    uint8_t report[joystick::REPORT_SIZE];

    joystick::fill_report(channels, report);
    /* Buttons: channels 9, 11 and 16 above 1500. */
    CHECK(report[0] == 0x85);
    auto axis = [&](int i) { return report[1 + 2 * i] | (report[2 + 2 * i] << 8); };
    CHECK(axis(0) == 0);
    CHECK(axis(1) == 1023);
    CHECK(axis(2) == 2047);
    CHECK(axis(3) == 0);
    CHECK(axis(4) == 2047);
    CHECK(axis(5) == 23);
}

int main()
{
    test_crc();
    test_conversion();
    test_channel_frame();
    test_parser();
    test_frame_delay();
    test_parameters();
    test_link_statistics();
    test_scale_axis();
    test_calibration_from_sweep();
    test_settings();
    test_channels();
    test_throttle_lock();
    test_mode();
    test_safe_controls();
    test_led();
    test_mapping();
    test_joystick();

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all core tests passed\n");
    return 0;
}
