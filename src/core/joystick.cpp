#include "core/joystick.h"

namespace joystick {

namespace {

constexpr int32_t CHANNEL_MIN = 988;
constexpr int32_t CHANNEL_SPAN = 1024;
constexpr uint16_t BUTTON_THRESHOLD = 1500;

uint16_t axis(uint16_t channel)
{
    int32_t value = ((int32_t)channel - CHANNEL_MIN) * AXIS_MAX / CHANNEL_SPAN;

    if (value < 0) {
        return 0;
    }
    return value > AXIS_MAX ? AXIS_MAX : (uint16_t)value;
}

} // namespace

void fill_report(const uint16_t channels[16], uint8_t report[REPORT_SIZE])
{
    uint8_t buttons = 0;

    for (int i = 0; i < 8; i++) {
        if (channels[8 + i] > BUTTON_THRESHOLD) {
            buttons |= (uint8_t)(1u << i);
        }
    }
    report[0] = buttons;
    for (int i = 0; i < 8; i++) {
        uint16_t value = axis(channels[i]);

        report[1 + 2 * i] = (uint8_t)(value & 0xff);
        report[2 + 2 * i] = (uint8_t)(value >> 8);
    }
}

} // namespace joystick
