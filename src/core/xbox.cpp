#include "core/xbox.h"

#include <string.h>

namespace xbox {

namespace {

void put16(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
}

int16_t stick(uint16_t channel)
{
    int32_t value = ((int32_t)channel - 1500) * 32767 / 512;

    return (int16_t)(value < -32767 ? -32767 : value > 32767 ? 32767 : value);
}

uint8_t trigger(uint16_t channel)
{
    int32_t value = ((int32_t)channel - 988) * 255 / 1024;

    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

} // namespace

void fill_report(const uint16_t channels[16], const int16_t assignment[ELEMENT_COUNT],
                 uint8_t report[REPORT_SIZE])
{
    /* In the order of the button elements, ELEMENT_A to ELEMENT_START. */
    static const uint16_t BUTTONS[8] = { BUTTON_A,  BUTTON_B,  BUTTON_X,    BUTTON_Y,
                                         BUTTON_LB, BUTTON_RB, BUTTON_BACK, BUTTON_START };
    /* The channel an element follows; at_rest for an element without one. */
    auto value = [&](int element, uint16_t at_rest) {
        int channel = assignment[element];

        return (channel >= 1 && channel <= 16) ? channels[channel - 1] : at_rest;
    };
    uint16_t buttons = 0;

    for (int i = 0; i < 8; i++) {
        if (value(ELEMENT_A + i, 988) > 1500) {
            buttons |= BUTTONS[i];
        }
    }
    memset(report, 0, REPORT_SIZE);
    report[0] = 0x00; /* message type: input report */
    report[1] = REPORT_SIZE;
    put16(&report[2], buttons);
    report[4] = trigger(value(ELEMENT_LEFT_TRIGGER, 988));
    report[5] = trigger(value(ELEMENT_RIGHT_TRIGGER, 988));
    put16(&report[6], (uint16_t)stick(value(ELEMENT_LEFT_X, 1500)));
    put16(&report[8], (uint16_t)stick(value(ELEMENT_LEFT_Y, 1500)));
    put16(&report[10], (uint16_t)stick(value(ELEMENT_RIGHT_X, 1500)));
    put16(&report[12], (uint16_t)stick(value(ELEMENT_RIGHT_Y, 1500)));
}

} // namespace xbox
