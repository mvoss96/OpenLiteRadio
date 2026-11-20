/*
 * Tunes for the buzzer and the trim feedback on the status LED. No hardware
 * access; src/indicators.cpp plays them.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sound {

/* One tone or pause. */
struct Note {
    uint16_t frequency; /* hertz; 0 is a pause. For an LED flash: a FlashColour */
    uint16_t ms;        /* duration in milliseconds */
};

/* What the status LED shows during one note of a trim flash. */
enum FlashColour : uint16_t {
    FLASH_DARK = 0,   // LED off
    FLASH_WHITE = 1,  // a step, or the end
    FLASH_GREEN = 2,  // the centre
    FLASH_ORANGE = 3, // a weak battery
};

constexpr size_t MAX_NOTES = 10;

struct Tune {
    uint8_t count;
    Note notes[MAX_NOTES];
};

/* The built-in tunes; the settings sound.on to sound.connect hold one of these. */
enum TuneId : uint8_t {
    TUNE_NONE = 0,     // no tune
    TUNE_BEEP,         // one short high tone
    TUNE_DOUBLE_BEEP,  // two short high tones
    TUNE_SHORT_HIGH,   // one higher, longer tone
    TUNE_LOW,          // one short low tone
    TUNE_LOW_LONG,     // one long low tone
    TUNE_RISING,       // G5 A5 B5, the stock firmware's power-on tune
    TUNE_FALLING,      // B5 A5 G5, the stock firmware's power-off tune
    TUNE_TRIAD,        // C6 E6 G6
    TUNE_WARNING,      // four short tones at 1480 Hz, the stock firmware's weak-battery warning
    TUNE_COUNT,        // number of tunes
};

/* How trim steps sound; the setting sound.trim. */
enum TrimStyle : uint8_t {
    TRIM_SILENT = 0,  // no tone
    TRIM_STOCK = 1,   // the stock firmware's tones, plus one for the end
    TRIM_EDGETX = 2,  // EdgeTX: the pitch follows the trim value
};

/* The tune with this id; TUNE_NONE for an id out of range. */
const Tune &tune(int id);

/* Language-neutral keys of the tunes, indexed by TuneId; the configurator translates them. */
extern const char *const TUNE_KEYS[TUNE_COUNT];

/* Tone for a trim step from before to value, the trim ranging over -limit..limit. */
Tune trim_tone(int style, int before, int value, int limit);

/* A few trim steps away from the centre and back, to hear a style. */
Tune trim_demo(int style);

/* Status LED flash for a trim step to value: short white, long green at the centre, double white at the end. */
Tune trim_flash(int value, int limit);

/* Status LED flash while the battery is weak: orange four times, as the stock firmware shows it. */
Tune weak_battery_flash();

/* Duty cycle of the buzzer in percent for the setting sound.volume (0 off to 3 loud). */
uint8_t volume_duty(int volume);

} // namespace sound
