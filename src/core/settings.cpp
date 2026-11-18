#include "core/settings.h"

#include <stdio.h>
#include <string.h>

namespace settings {

namespace {

struct Field {
    const char *name;
    int16_t min;
    int16_t max;
    int16_t default_value;
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

const Field UNLOCK_FIELDS[] = { { "mode", 0, 2, 1 }, { "beep", 0, 1, 1 } };
const Field BUZZER_FIELDS[] = { { "on", 0, 1, 1 } };
/* The default source depends on the channel, see info(). */
const Field CHANNEL_FIELD_LIST[] = { { "source", 0, 10, 0 }, { "weight", 0, 100, 100 },
                                     { "reverse", 0, 1, 0 } };
const Field CALIBRATION_FIELD_LIST[] = { { "low", 0, 4095, 600 }, { "centre", 0, 4095, 2048 },
                                         { "high", 0, 4095, 3500 }, { "deadzone", 0, 10, 1 } };

const Group GROUPS[] = {
    { "unlock", 0, 0, 2, UNLOCK_FIELDS, 0x100 },
    { "buzzer", 0, 0, 1, BUZZER_FIELDS, 0x110 },
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

    /* Channels 1 to 10 take sources 1 to 10: AETR, then SA to SF. */
    if (index >= CHANNEL_FIRST && index < CALIBRATION_FIRST && position.field == CHANNEL_SOURCE &&
        position.instance < 10) {
        default_value = (int16_t)(position.instance + 1);
    }
    return { field.min, field.max, default_value,
             (uint16_t)(position.group->first_id + position.instance * IDS_PER_INSTANCE +
                        position.field) };
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

} // namespace settings
