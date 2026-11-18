/*
 * Saved channel assignments ("profiles"): a name plus source, weight and
 * reverse of all 16 channels. No hardware access; storing them is the
 * caller's business. The live assignment stays in the settings ch.N.*.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/settings.h"

namespace profiles {

constexpr int SLOTS = 4;          /* numbered 1 to SLOTS in the host protocol */
constexpr size_t NAME_SIZE = 16;  /* up to 15 bytes of UTF-8 plus the terminator */
constexpr int CHANNELS = 16;

/* Storage numbers: one record per slot, and the number of the active slot. */
constexpr uint16_t FIRST_ID = 0x400;
constexpr uint16_t ACTIVE_ID = 0x410;

/* The stored record. */
struct Profile {
    char name[NAME_SIZE];
    uint8_t source[CHANNELS];
    uint8_t weight[CHANNELS];
    uint16_t reverse; /* bit n: channel n + 1 */
};

/* True for 1 to 15 bytes without control characters. */
bool name_valid(const char *name);

/* Copy the channel settings in values into a profile with this name. */
void capture(const int16_t values[settings::COUNT], const char *name, Profile &profile);

/*
 * Put the profile's channel settings into settings::values. Returns false
 * and changes nothing if one of them is out of range.
 */
bool apply(const Profile &profile);

} // namespace profiles
