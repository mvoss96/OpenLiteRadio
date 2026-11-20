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

void fill_report(const uint16_t channels[16], uint8_t report[REPORT_SIZE])
{
    /* Channels 9 to 16 in order. */
    static const uint16_t BUTTONS[8] = { BUTTON_A,  BUTTON_B,  BUTTON_X,    BUTTON_Y,
                                         BUTTON_LB, BUTTON_RB, BUTTON_BACK, BUTTON_START };
    uint16_t buttons = 0;

    for (int i = 0; i < 8; i++) {
        if (channels[8 + i] > 1500) {
            buttons |= BUTTONS[i];
        }
    }
    memset(report, 0, REPORT_SIZE);
    report[0] = 0x00; /* message type: input report */
    report[1] = REPORT_SIZE;
    put16(&report[2], buttons);
    report[4] = trigger(channels[4]);
    report[5] = trigger(channels[5]);
    put16(&report[6], (uint16_t)stick(channels[3]));  /* left X: yaw */
    put16(&report[8], (uint16_t)stick(channels[2]));  /* left Y: throttle */
    put16(&report[10], (uint16_t)stick(channels[0])); /* right X: roll */
    put16(&report[12], (uint16_t)stick(channels[1])); /* right Y: pitch */
}

} // namespace xbox
