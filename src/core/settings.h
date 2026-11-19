/*
 * The settings: a fixed list of 16-bit values, each with a key, a range and a
 * default. No hardware access; storing them is the caller's business.
 *
 * Keys are "group.field" or "group.instance.field", e.g. "unlock.mode" and
 * "ch.3.weight".
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace settings {

/* Positions of the values, in the order the list is reported. */
enum Index {
    UNLOCK_MODE = 0,   /* 0 at once, 1 throttle down, 2 throttle up and down */
    UNLOCK_BEEP = 1,   /* tone when the output is released */
    BUZZER_ON = 2,     /* 0 silences all tones */
    MAP_MODE = 3,      /* the assignment in use, a core::Mapping */
    CHANNEL_FIRST = 4, /* 16 channels with CHANNEL_FIELDS values each: the Custom assignment */
    CALIBRATION_FIRST = CHANNEL_FIRST + 16 * 3, /* 4 axes with CALIBRATION_FIELDS values each */
    COUNT = CALIBRATION_FIRST + 4 * 4,
};

/* Values per channel, added to CHANNEL_FIRST + 3 * channel. */
enum ChannelField {
    CHANNEL_SOURCE = 0,  /* a core::Source */
    CHANNEL_WEIGHT = 1,  /* percent of the deflection from the centre */
    CHANNEL_REVERSE = 2, /* 0 or 1 */
    CHANNEL_FIELDS = 3,
};

/* Values per stick axis, added to CALIBRATION_FIRST + 4 * axis. */
enum CalibrationField {
    CALIBRATION_LOW = 0,
    CALIBRATION_CENTRE = 1,
    CALIBRATION_HIGH = 2,
    CALIBRATION_DEAD_ZONE = 3, /* percent of the half range around the centre; not used for the throttle */
    CALIBRATION_FIELDS = 4,
};

constexpr size_t KEY_SIZE = 20;

struct Info {
    int16_t min;
    int16_t max;
    int16_t default_value;
    /* Number for the storage; stays the same when settings are added. */
    uint16_t id;
};

/* Current values. Change them through set() only. */
extern int16_t values[COUNT];

Info info(int index);

/* Write the key of a setting into a buffer of KEY_SIZE bytes. */
void key(int index, char *buffer);

/* Position of the setting with this key, or -1. */
int find(const char *key);

/* Change a value; returns false and changes nothing if it is out of range. */
bool set(int index, int32_t value);

void set_defaults();

} // namespace settings
