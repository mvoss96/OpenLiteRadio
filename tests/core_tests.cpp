/* Tests for src/core, built and run on the PC by test.ps1. */

#include <cstdio>
#include <cstring>

#include "core/channels.h"
#include "core/crsf.h"

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
    CHECK(crsf::next_frame_delay(4000, 0) == 4000);
    CHECK(crsf::next_frame_delay(4000, 300) == 4300);
    CHECK(crsf::next_frame_delay(4000, -300) == 3700);
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

static void test_channels()
{
    const core::AxisCalibration calibration[core::AXIS_COUNT] = {
        { 600, 2000, 3500 }, { 600, 2000, 3500 }, { 600, 2000, 3500 }, { 600, 2000, 3500 },
    };
    core::Controls controls = {};
    uint16_t channels[core::CHANNEL_COUNT];

    /* Throttle down, other axes at rest, all switches in position 0. */
    controls.sticks[core::AXIS_LEFT_VERTICAL] = 600;
    controls.sticks[core::AXIS_LEFT_HORIZONTAL] = 2000;
    controls.sticks[core::AXIS_RIGHT_VERTICAL] = 2000;
    controls.sticks[core::AXIS_RIGHT_HORIZONTAL] = 2000;
    core::compute_channels(controls, calibration, channels);
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
    core::compute_channels(controls, calibration, channels);
    CHECK(channels[0] == 2012 && channels[1] == 2012 && channels[2] == 2012 && channels[3] == 2012);
    CHECK(channels[4] == 2012 && channels[5] == 1500 && channels[6] == 2012);
    CHECK(channels[7] == 2012 && channels[8] == 2012 && channels[9] == 988);
}

static void test_throttle_lock()
{
    core::ThrottleLock lock;

    CHECK(lock.update(1500));
    CHECK(lock.update(1009));
    CHECK(!lock.update(1008));
    /* Once released it stays released. */
    CHECK(!lock.update(2012));
    lock.lock();
    CHECK(lock.update(1200));
    CHECK(!lock.update(988));
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
    test_channels();
    test_throttle_lock();

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all core tests passed\n");
    return 0;
}
