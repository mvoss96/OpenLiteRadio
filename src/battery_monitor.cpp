#include "battery_monitor.h"

#include "board/board.h"
#include "core/battery.h"

namespace battery_monitor {

namespace {

constexpr int64_t PERIOD_MS = 15000;
constexpr int64_t CHARGER_PAUSE_MS = 100;

uint16_t last_raw;
uint16_t last_vrefint;
/* Internal reference reading with a regulated 3.3 V supply; learned while the battery is high. */
uint16_t vrefint_regulated = battery::VREFINT_DEFAULT;
/* Uptime of the next reading. */
int64_t next_ms;
/* Uptime since which the charger is paused for a reading; -1 while it is not. */
int64_t paused_since = -1;
bool from_host;
/* Mask last sent to the LEDs; 0xff before the first. */
uint8_t shown = 0xff;

/* Read the battery, corrected for a supply below 3.3 V. */
uint16_t read()
{
    uint16_t raw = board::battery_raw();

    last_vrefint = board::vrefint_raw();
    if (raw >= battery::REGULATED_FROM && last_vrefint != 0) {
        vrefint_regulated = last_vrefint;
    }
    return battery::compensate(raw, last_vrefint, vrefint_regulated);
}

} // namespace

bool tick(int64_t now_ms)
{
    if (paused_since >= 0) {
        if (now_ms - paused_since < CHARGER_PAUSE_MS) {
            return false;
        }
        last_raw = read();
        board::set_charger_enabled(true);
        paused_since = -1;
        next_ms = now_ms + PERIOD_MS;
        return true;
    }
    if (now_ms < next_ms) {
        return false;
    }
    if (board::usb_supply_present()) {
        board::set_charger_enabled(false);
        paused_since = now_ms;
        return false;
    }
    last_raw = read();
    next_ms = now_ms + PERIOD_MS;
    return true;
}

uint16_t raw()
{
    return last_raw;
}

uint16_t vrefint()
{
    return last_vrefint;
}

void show(uint8_t mask)
{
    if (!from_host && mask != shown) {
        shown = mask;
        board::set_battery_leds(mask);
    }
}

void set_from_host(bool host, uint8_t mask)
{
    from_host = host;
    shown = 0xff;
    if (host) {
        board::set_battery_leds(mask);
    }
}

} // namespace battery_monitor
