#include "core/battery.h"

namespace battery {

namespace {

/* The leftmost count LEDs. */
uint8_t lit(int count)
{
    return (uint8_t)((1u << count) - 1u);
}

} // namespace

int level(uint16_t raw)
{
    int count = 0;

    for (uint16_t threshold : LEVEL_THRESHOLDS) {
        if (raw >= threshold) {
            count++;
        }
    }
    return count;
}

bool weak(uint16_t raw, bool usb)
{
    return !usb && level(raw) == 0;
}

uint8_t bar(uint16_t raw, bool usb, uint32_t now_ms)
{
    bool dark_half = (now_ms / BLINK_MS) % 2 == 1;
    int count = level(raw);

    if (usb) {
        if (raw >= FULL) {
            return lit(4);
        }
        /* Charging: at least one LED, the highest of them blinking. */
        count = count > 0 ? count : 1;
        return dark_half ? lit(count - 1) : lit(count);
    }
    if (count == 0) {
        return dark_half ? 0 : lit(1);
    }
    return lit(count);
}

uint8_t switching_on(uint32_t held_ms)
{
    uint32_t steps = held_ms / HOLD_STEP_MS;

    return lit(steps < 4 ? (int)steps : 4);
}

uint8_t switching_off(uint32_t held_ms, uint8_t shown)
{
    if (held_ms < HOLD_STEP_MS) {
        return shown;
    }
    /* 250 ms: four LEDs, 500 ms: three, ... 1250 ms: none. */
    uint32_t gone = held_ms / HOLD_STEP_MS - 1;

    return gone < 4 ? lit(4 - (int)gone) : 0;
}

uint8_t peek(uint16_t raw, uint32_t since_ms)
{
    /* 100 ms on, 100 ms off, three times. */
    if (since_ms >= PEEK_MS || (since_ms / 100) % 2 == 1) {
        return 0;
    }
    return lit(level(raw));
}

uint8_t link_quality(bool connected, uint8_t quality)
{
    int count = 0;

    if (connected) {
        for (uint8_t threshold : LINK_THRESHOLDS) {
            if (quality >= threshold) {
                count++;
            }
        }
    }
    return lit(count);
}

} // namespace battery
