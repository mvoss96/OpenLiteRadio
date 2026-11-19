#include "indicators.h"

#include <zephyr/kernel.h>

#include "board/board.h"
#include "core/settings.h"

namespace indicators {

namespace {

int64_t beep_until;
bool led_from_host;

} // namespace

void beep(uint32_t frequency, int milliseconds)
{
    if (settings::values[settings::BUZZER_ON] == 0) {
        return;
    }
    board::set_buzzer(frequency);
    beep_until = k_uptime_get() + milliseconds;
}

void set_led_from_host(bool from_host, uint8_t red, uint8_t green, uint8_t blue)
{
    led_from_host = from_host;
    if (from_host) {
        board::set_status_led(red, green, blue);
    }
}

void tick(const core::LedInputs &state)
{
    /* Colour as 0xRRGGBB; sent to the LED only when it changes. */
    static uint32_t shown = UINT32_MAX;
    int64_t now = k_uptime_get();

    if (beep_until != 0 && now >= beep_until) {
        beep_until = 0;
        board::set_buzzer(0);
    }
    if (led_from_host) {
        shown = UINT32_MAX;
        return;
    }

    uint32_t colour = core::led_colour(state, (uint32_t)now);

    if (colour != shown) {
        shown = colour;
        board::set_status_led((uint8_t)(colour >> 16), (uint8_t)(colour >> 8), (uint8_t)colour);
    }
}

} // namespace indicators
