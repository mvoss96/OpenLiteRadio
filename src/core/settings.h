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
    UNLOCK_MODE = 0,      /* 1 throttle down, 2 throttle up and down */
    SOUND_VOLUME = 1,     /* 0 off, 1 quiet, 2 medium, 3 loud */
    SOUND_TRIM = 2,       /* tones of the trim buttons, a sound::TrimStyle */
    SOUND_POWER_ON = 3,   /* tune when the radio is switched on, a sound::TuneId */
    SOUND_POWER_OFF = 4,  /* tune when the radio is switched off */
    SOUND_UNLOCK = 5,     /* tune when the throttle lock releases the output */
    SOUND_ACCEPTED = 6,   /* tune when a command starts: binding, WiFi, Bluetooth, calibration, USB form */
    SOUND_SUCCESS = 7,    /* tune when a receiver is bound or a calibration is stored */
    SOUND_CANCEL = 8,     /* tune when binding is cancelled */
    SOUND_ERROR = 9,      /* tune when a command, a calibration or the USB device fails */
    SOUND_WEAK_BATTERY = 10, /* tune every 15 s while the battery is weak */
    MAP_MODE = 11,        /* the assignment in use, a core::Mapping */
    TRIM_FIRST = 12,      /* trim of the 4 stick axes in steps, in the order of core::Axis */
    TRIM_LED = 16,        /* 1: the status LED flashes on every trim step */
    USB_MODE = 17,        /* USB device with the radio off: 0 joystick, 1 Xbox controller */
    CHANNEL_FIRST = 18,   /* 16 channels with CHANNEL_FIELDS values each: the Custom assignment */
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
    /* Name of the option list that names the values, or nullptr for a plain number. */
    const char *options;
};

/* One named value of an option list; key is one language-neutral word the configurator translates. */
struct Option {
    const char *list;
    int16_t value;
    const char *key;
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

/* Number of named values over all option lists. */
int option_count();

/* The n-th named value over all option lists, list by list. */
Option option(int n);

} // namespace settings
