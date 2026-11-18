#include "core/profiles.h"

#include <string.h>

namespace profiles {

namespace {

int channel_index(int channel, int field)
{
    return settings::CHANNEL_FIRST + settings::CHANNEL_FIELDS * channel + field;
}

bool in_range(int index, int32_t value)
{
    settings::Info info = settings::info(index);

    return value >= info.min && value <= info.max;
}

} // namespace

bool name_valid(const char *name)
{
    size_t length = strlen(name);

    if (length == 0 || length >= NAME_SIZE) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        unsigned char character = (unsigned char)name[i];

        if (character < 0x20 || character == 0x7f) {
            return false;
        }
    }
    return true;
}

void capture(const int16_t values[settings::COUNT], const char *name, Profile &profile)
{
    memset(&profile, 0, sizeof(profile));
    strncpy(profile.name, name, NAME_SIZE - 1);
    for (int channel = 0; channel < CHANNELS; channel++) {
        profile.source[channel] = (uint8_t)values[channel_index(channel, settings::CHANNEL_SOURCE)];
        profile.weight[channel] = (uint8_t)values[channel_index(channel, settings::CHANNEL_WEIGHT)];
        if (values[channel_index(channel, settings::CHANNEL_REVERSE)] != 0) {
            profile.reverse |= (uint16_t)(1u << channel);
        }
    }
}

bool apply(const Profile &profile)
{
    for (int channel = 0; channel < CHANNELS; channel++) {
        if (!in_range(channel_index(channel, settings::CHANNEL_SOURCE), profile.source[channel]) ||
            !in_range(channel_index(channel, settings::CHANNEL_WEIGHT), profile.weight[channel])) {
            return false;
        }
    }
    for (int channel = 0; channel < CHANNELS; channel++) {
        settings::set(channel_index(channel, settings::CHANNEL_SOURCE), profile.source[channel]);
        settings::set(channel_index(channel, settings::CHANNEL_WEIGHT), profile.weight[channel]);
        settings::set(channel_index(channel, settings::CHANNEL_REVERSE),
                      (profile.reverse >> channel) & 1);
    }
    return true;
}

} // namespace profiles
