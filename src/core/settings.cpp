#include "core/settings.h"

#include "core/sound.h"
#include <stdio.h>
#include <string.h>

namespace settings {

namespace {

/* Keys for the values first .. first + count - 1 of the settings that use the list. */
struct OptionList {
    const char *name;
    int16_t first;
    uint8_t count;
    const char *const *keys;
};

const char *const UNLOCK_KEYS[] = { "throttle_low", "throttle_up_down" };
const char *const TRIM_TONE_KEYS[] = { "none", "stock", "edgetx" };
const char *const MAP_KEYS[] = { "aetr", "taer", "custom" };
const char *const USB_KEYS[] = { "joystick", "xbox" };

const OptionList OPTION_LISTS[] = {
    { "unlock", 1, 2, UNLOCK_KEYS },
    { "trimtone", 0, 3, TRIM_TONE_KEYS },
    { "tune", 0, sound::TUNE_COUNT, sound::TUNE_KEYS },
    { "map", 0, 3, MAP_KEYS },
    { "usb", 0, 2, USB_KEYS },
};
const OptionList *const UNLOCK_OPTIONS = &OPTION_LISTS[0];
const OptionList *const TRIM_TONE_OPTIONS = &OPTION_LISTS[1];
const OptionList *const TUNE_OPTIONS = &OPTION_LISTS[2];
const OptionList *const MAP_OPTIONS = &OPTION_LISTS[3];
const OptionList *const USB_OPTIONS = &OPTION_LISTS[4];

struct Field {
    const char *name;
    int16_t min;
    int16_t max;
    int16_t default_value;
    const OptionList *options = nullptr; /* names the values from min to max */
};

/* A group of settings; with instances it repeats its fields once per instance. */
struct Group {
    const char *name;
    uint8_t instances;      /* 0: the fields exist once, without a number in the key */
    uint8_t first_instance; /* number of the first instance in the key */
    uint8_t field_count;
    const Field *fields;
    uint16_t first_id;
};

/* Each instance has room for this many fields in the storage numbers. */
constexpr uint16_t IDS_PER_INSTANCE = 8;

const Field UNLOCK_FIELDS[] = { { "mode", 1, 2, 1, UNLOCK_OPTIONS } };
/* Volume 0..3, a sound::TrimStyle, one sound::TuneId per event, then the link warning's repeat in seconds. */
constexpr int16_t LAST_TUNE = sound::TUNE_COUNT - 1;
const Field SOUND_FIELDS[] = {
    { "volume", 0, 3, 3 },
    { "trim", 0, 2, sound::TRIM_STOCK, TRIM_TONE_OPTIONS },
    { "on", 0, LAST_TUNE, sound::TUNE_RISING, TUNE_OPTIONS },
    { "off", 0, LAST_TUNE, sound::TUNE_FALLING, TUNE_OPTIONS },
    { "unlock", 0, LAST_TUNE, sound::TUNE_BEEP, TUNE_OPTIONS },
    { "accept", 0, LAST_TUNE, sound::TUNE_BEEP, TUNE_OPTIONS },
    { "success", 0, LAST_TUNE, sound::TUNE_SHORT_HIGH, TUNE_OPTIONS },
    { "cancel", 0, LAST_TUNE, sound::TUNE_LOW, TUNE_OPTIONS },
    { "error", 0, LAST_TUNE, sound::TUNE_LOW_LONG, TUNE_OPTIONS },
    { "battery", 0, LAST_TUNE, sound::TUNE_WARNING, TUNE_OPTIONS },
    { "lost", 0, LAST_TUNE, sound::TUNE_LOW_LONG, TUNE_OPTIONS },
    { "back", 0, LAST_TUNE, sound::TUNE_TRIAD, TUNE_OPTIONS },
    { "repeat", 0, 30, 3 },
};
const Field MAP_FIELDS[] = { { "mode", 0, 2, 0, MAP_OPTIONS } };
/* 0 joystick, 1 Xbox controller: the USB device with the radio off. */
const Field USB_FIELDS[] = { { "mode", 0, 1, 0, USB_OPTIONS } };
/* In the order of core::Axis; steps of core::TRIM_STEP_US. Then the LED flash on a step. */
const Field TRIM_FIELDS[] = { { "thr", -25, 25, 0 }, { "rud", -25, 25, 0 }, { "ele", -25, 25, 0 },
                              { "ail", -25, 25, 0 }, { "led", 0, 1, 1 } };
/* The default source depends on the channel, see info(). */
const Field CHANNEL_FIELD_LIST[] = { { "source", 0, 10, 0 }, { "weight", 0, 100, 100 },
                                     { "reverse", 0, 1, 0 } };
const Field CALIBRATION_FIELD_LIST[] = { { "low", 0, 4095, 600 }, { "centre", 0, 4095, 2048 },
                                         { "high", 0, 4095, 3500 }, { "deadzone", 0, 10, 1 } };

const Group GROUPS[] = {
    { "unlock", 0, 0, 1, UNLOCK_FIELDS, 0x100 },
    { "sound", 0, 0, 13, SOUND_FIELDS, 0x110 },
    { "map", 0, 0, 1, MAP_FIELDS, 0x120 },
    { "trim", 0, 0, 5, TRIM_FIELDS, 0x130 },
    { "usb", 0, 0, 1, USB_FIELDS, 0x140 },
    { "ch", 16, 1, 3, CHANNEL_FIELD_LIST, 0x200 },
    { "cal", 4, 0, 4, CALIBRATION_FIELD_LIST, 0x300 },
};

struct Position {
    const Group *group;
    int instance; /* counted from 0 */
    int field;
};

Position locate(int index)
{
    for (const Group &group : GROUPS) {
        int size = group.field_count * (group.instances != 0 ? group.instances : 1);

        if (index < size) {
            return { &group, index / group.field_count, index % group.field_count };
        }
        index -= size;
    }
    return { nullptr, 0, 0 };
}

} // namespace

int16_t values[COUNT];

Info info(int index)
{
    Position position = locate(index);
    const Field &field = position.group->fields[position.field];
    int16_t default_value = field.default_value;

    /*
     * Channels 1 to 10 take sources 1 to 10: AETR, then SA to SF, the same
     * as the fixed AETR mapping (core/channels.cpp), so that a new Custom
     * assignment starts from it.
     */
    if (index >= CHANNEL_FIRST && index < CALIBRATION_FIRST && position.field == CHANNEL_SOURCE &&
        position.instance < 10) {
        default_value = (int16_t)(position.instance + 1);
    }
    return { field.min, field.max, default_value,
             (uint16_t)(position.group->first_id + position.instance * IDS_PER_INSTANCE +
                        position.field),
             field.options != nullptr ? field.options->name : nullptr };
}

void key(int index, char *buffer)
{
    Position position = locate(index);
    const Group &group = *position.group;

    if (group.instances == 0) {
        snprintf(buffer, KEY_SIZE, "%s.%s", group.name, group.fields[position.field].name);
    } else {
        snprintf(buffer, KEY_SIZE, "%s.%d.%s", group.name, position.instance + group.first_instance,
                 group.fields[position.field].name);
    }
}

int find(const char *wanted)
{
    char buffer[KEY_SIZE];

    for (int index = 0; index < COUNT; index++) {
        key(index, buffer);
        if (strcmp(buffer, wanted) == 0) {
            return index;
        }
    }
    return -1;
}

bool set(int index, int32_t value)
{
    if (index < 0 || index >= COUNT) {
        return false;
    }

    Info limits = info(index);

    if (value < limits.min || value > limits.max) {
        return false;
    }
    values[index] = (int16_t)value;
    return true;
}

void set_defaults()
{
    for (int index = 0; index < COUNT; index++) {
        values[index] = info(index).default_value;
    }
}

int option_count()
{
    int count = 0;

    for (const OptionList &list : OPTION_LISTS) {
        count += list.count;
    }
    return count;
}

Option option(int n)
{
    for (const OptionList &list : OPTION_LISTS) {
        if (n < list.count) {
            return { list.name, (int16_t)(list.first + n), list.keys[n] };
        }
        n -= list.count;
    }
    return { nullptr, 0, nullptr };
}

} // namespace settings
