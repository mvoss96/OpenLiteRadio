#include "indicators.h"

#include <zephyr/kernel.h>

#include "board/board.h"
#include "core/settings.h"

namespace indicators {

namespace {

/* LED colour of each sound::FlashColour, as bright as the state colours of core/led.cpp. */
constexpr uint32_t FLASH_COLOURS[] = { 0x000000, 0x181818, 0x003000, 0x300800 };

/* A tune being played, one note after the other. */
struct Player {
    sound::Tune tune;
    uint8_t current; /* index of the note playing now */
    int64_t until;   /* uptime in ms when that note ends; 0 while nothing plays */
};

Player buzzer;
Player flash;
bool led_from_host;
/* Colour as 0xRRGGBB last sent to the LED. */
uint32_t led_now;

void start(Player &player, const sound::Tune &tune)
{
    player.tune = tune;
    player.current = 0;
    player.until = tune.count > 0 ? k_uptime_get() + tune.notes[0].ms : 0;
}

/* Go to the next note once the current one has ended; true when the note changed. */
bool advance(Player &player, int64_t now)
{
    if (player.until == 0 || now < player.until) {
        return false;
    }
    if (++player.current < player.tune.count) {
        player.until = now + player.tune.notes[player.current].ms;
    } else {
        player.until = 0;
    }
    return true;
}

/* Frequency of the note playing now; 0 for a pause or when nothing plays. */
uint16_t frequency_now(const Player &player)
{
    return player.until != 0 ? player.tune.notes[player.current].frequency : 0;
}

void sound_buzzer()
{
    board::set_buzzer(frequency_now(buzzer),
                      sound::volume_duty(settings::values[settings::SOUND_VOLUME]));
}

} // namespace

void play(const sound::Tune &tune)
{
    start(buzzer, tune);
    sound_buzzer();
}

void play_event(int setting)
{
    play(sound::tune(settings::values[setting]));
}

bool play_setting(int setting)
{
    if (setting == settings::SOUND_VOLUME) {
        play(sound::tune(sound::TUNE_BEEP));
    } else if (setting == settings::SOUND_TRIM) {
        play(sound::trim_demo(settings::values[setting]));
    } else if (setting >= settings::SOUND_POWER_ON && setting <= settings::SOUND_CONNECTED) {
        play_event(setting);
    } else {
        return false;
    }
    return true;
}

void trim_step(int before, int value, int limit)
{
    sound::Tune tone = sound::trim_tone(settings::values[settings::SOUND_TRIM], before, value, limit);

    if (tone.count > 0) {
        play(tone);
    }
    if (settings::values[settings::TRIM_LED] != 0) {
        start(flash, sound::trim_flash(value, limit));
    }
}

void weak_battery()
{
    play_event(settings::SOUND_WEAK_BATTERY);
    start(flash, sound::weak_battery_flash());
}

bool playing()
{
    return buzzer.until != 0;
}

void beep(uint32_t frequency, int milliseconds)
{
    play({ 1, { { (uint16_t)frequency, (uint16_t)milliseconds } } });
}

void set_led_from_host(bool from_host, uint8_t red, uint8_t green, uint8_t blue)
{
    led_from_host = from_host;
    if (from_host) {
        board::set_status_led(red, green, blue);
        led_now = ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
    }
}

void tick(const core::LedInputs &state)
{
    /* Colour as 0xRRGGBB; sent to the LED only when it changes. */
    static uint32_t shown = UINT32_MAX;
    int64_t now = k_uptime_get();

    if (advance(buzzer, now)) {
        sound_buzzer();
    }
    advance(flash, now);
    if (led_from_host) {
        shown = UINT32_MAX;
        return;
    }

    /* A trim flash replaces the state colour for its duration. */
    uint32_t colour = flash.until != 0 ? FLASH_COLOURS[frequency_now(flash) % ARRAY_SIZE(FLASH_COLOURS)]
                                       : core::led_colour(state, (uint32_t)now);

    if (colour != shown) {
        shown = colour;
        led_now = colour;
        board::set_status_led((uint8_t)(colour >> 16), (uint8_t)(colour >> 8), (uint8_t)colour);
    }
}

uint32_t led_colour_now()
{
    return led_now;
}

} // namespace indicators
