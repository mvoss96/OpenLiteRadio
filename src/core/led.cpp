#include "core/led.h"

namespace core {

namespace {

/* Colours at the brightness the LED is driven with. */
constexpr uint32_t RED = 0x300000;
constexpr uint32_t GREEN = 0x003000;
constexpr uint32_t BLUE = 0x000030;
constexpr uint32_t YELLOW = 0x302000;
constexpr uint32_t WHITE = 0x181818; /* half of each part, about as bright as one colour */
constexpr uint32_t CYAN = 0x002424;
constexpr uint32_t PURPLE = 0x200030;

constexpr uint32_t SLOW_MS = 500; /* on and off of slow blinking */
constexpr uint32_t FAST_MS = 100; /* on and off of fast blinking */

/* The colour on for half_period_ms, then off as long. */
uint32_t blink(uint32_t colour, uint32_t ms, uint32_t half_period_ms)
{
    return (ms / half_period_ms) % 2 == 0 ? colour : 0;
}

} // namespace

uint32_t led_colour(const LedInputs &in, uint32_t ms)
{
    /* Ordered by priority: the first state that applies is shown. */
    if (in.calibrating) {
        return blink(YELLOW, ms, SLOW_MS);
    }
    switch (in.mode) {
    case Mode::ELRS_BINDING:
        return blink(CYAN, ms, SLOW_MS);
    case Mode::WIFI:
        return blink(GREEN, ms, SLOW_MS);
    case Mode::BLUETOOTH:
        return PURPLE;
    case Mode::ELRS_LOCKED:
        return in.receiver_connected ? RED : blink(RED, ms, SLOW_MS);
    case Mode::ELRS_HOST_READY:
    case Mode::ELRS_HOST:
        return blink(BLUE, ms, FAST_MS);
    case Mode::ELRS_ACTIVE:
        return in.receiver_connected ? BLUE : blink(BLUE, ms, SLOW_MS);
    case Mode::USB_JOYSTICK:
        return WHITE;
    case Mode::USB_XBOX:
        return YELLOW;
    case Mode::OFF:
        return 0;
    }
    return 0;
}

} // namespace core
