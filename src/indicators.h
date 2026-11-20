/* Buzzer and status LED. */

#pragma once

#include <stdint.h>

#include "core/led.h"
#include "core/sound.h"

namespace indicators {

/* Play a tune at the volume in sound.volume; it replaces one still playing. */
void play(const sound::Tune &tune);

/* Play the tune chosen in an event's setting, settings::SOUND_POWER_ON to SOUND_WEAK_BATTERY. */
void play_event(int setting);

/*
 * Play what a setting sounds like, for the host command "play": an event's
 * chosen tune, a few trim steps for sound.trim, one beep for sound.volume.
 * Returns false for any other setting.
 */
bool play_setting(int setting);

/* Tone (sound.trim) and LED flash (trim.led) for a trim step from before to value of -limit..limit. */
void trim_step(int before, int value, int limit);

/* Weak battery: the tune in sound.battery and four orange flashes of the status LED. */
void weak_battery();

/* True while a tune is still playing on the buzzer. */
bool playing();

/* One tone at the volume in sound.volume, for the host command "beep". */
void beep(uint32_t frequency, int milliseconds);

/* Let the host set the LED colour, or give it back to the firmware. */
void set_led_from_host(bool from_host, uint8_t red, uint8_t green, uint8_t blue);

/* Call every loop: plays the tunes on and shows the radio's state on the LED (core::led_colour). */
void tick(const core::LedInputs &state);

/* Colour as 0xRRGGBB the LED shows now, also one set by the host. */
uint32_t led_colour_now();

} // namespace indicators
