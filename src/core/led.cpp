#include "core/led.h"

namespace core {

namespace {

/* Colours at the brightness the LED is driven with. */
constexpr uint32_t RED = 0x300000;
constexpr uint32_t GREEN = 0x003000;
constexpr uint32_t BLUE = 0x000030;
constexpr uint32_t YELLOW = 0x302000;
constexpr uint32_t ORANGE = 0x300500; /* with more green it looks yellow on this LED */
constexpr uint32_t CYAN = 0x002424;
constexpr uint32_t PURPLE = 0x200030;
constexpr uint32_t WHITE = 0x202020;

constexpr uint32_t ALTERNATE_MS = 500; /* each colour of a special mode */
constexpr uint32_t PULSE_MS = 2000;    /* one rise and fall of a notice */

/* A special mode: the colour and white, half a second each. */
uint32_t alternate(uint32_t colour, uint32_t ms)
{
    return (ms / ALTERNATE_MS) % 2 == 0 ? colour : WHITE;
}

/* A notice: the colour rises and falls between a fifth and full brightness. */
uint32_t pulse(uint32_t colour, uint32_t ms)
{
    uint32_t phase = ms % PULSE_MS;
    uint32_t level = phase < PULSE_MS / 2 ? phase : PULSE_MS - phase; /* 0 .. PULSE_MS / 2 */
    uint32_t scale = 200 + 800 * level / (PULSE_MS / 2);              /* per mille */
    uint32_t out = 0;

    for (int shift = 0; shift <= 16; shift += 8) {
        out |= (((colour >> shift) & 0xffu) * scale / 1000) << shift;
    }
    return out;
}

} // namespace

uint32_t led_colour(const LedInputs &in, uint32_t ms)
{
    /* Ordered by priority: the first state that applies is shown. */
    if (in.calibrating) {
        return alternate(YELLOW, ms);
    }
    switch (in.mode) {
    case Mode::ELRS_BINDING:
        return alternate(CYAN, ms);
    case Mode::WIFI:
        return alternate(GREEN, ms);
    case Mode::BLUETOOTH:
        return PURPLE;
    case Mode::ELRS_LOCKED:
        return RED;
    case Mode::ELRS_HOST_READY:
    case Mode::ELRS_HOST:
        return alternate(BLUE, ms);
    case Mode::ELRS_ACTIVE:
        return in.receiver_connected ? BLUE : pulse(BLUE, ms);
    case Mode::USB_JOYSTICK:
        return ORANGE;
    case Mode::USB_XBOX:
        return YELLOW;
    case Mode::OFF:
        return 0;
    }
    return 0;
}

} // namespace core
