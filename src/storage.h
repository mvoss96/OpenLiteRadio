/*
 * Settings and profiles in the radio's flash (board::storage_*). Every write
 * holds the channel output for its duration.
 */

#pragma once

#include <stdint.h>

#include "core/profiles.h"

namespace storage {

/* Take each setting from the storage, or its default if it is not stored or out of range. */
void load_settings();

/* Store all settings; only values that differ from their default take up space. */
bool save_settings();

/* Slot of the profile last loaded or saved, 1 to profiles::SLOTS; 0 for none. */
uint8_t active_profile();
void load_active_profile();
bool store_active_profile(uint8_t slot);

/* Read a profile; false if the slot is empty or the record is damaged. */
bool read_profile(int slot, profiles::Profile &profile);
bool write_profile(int slot, const profiles::Profile &profile);
bool delete_profile(int slot);

} // namespace storage
