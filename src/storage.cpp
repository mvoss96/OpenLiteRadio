#include "storage.h"

#include "board/board.h"
#include "core/settings.h"
#include "output.h"

namespace storage {

namespace {

uint8_t active;

} // namespace

void load_settings()
{
    for (int index = 0; index < settings::COUNT; index++) {
        settings::Info info = settings::info(index);
        int16_t stored;

        if (!board::storage_read(info.id, &stored, sizeof(stored)) ||
            !settings::set(index, stored)) {
            settings::values[index] = info.default_value;
        }
    }
}

/* The storage skips a record that already holds the same value. */
bool save_settings()
{
    bool stored = true;
    /* A calibration run may already hold the output; it keeps holding it afterwards. */
    bool held = output::set_hold(true);

    for (int index = 0; index < settings::COUNT; index++) {
        settings::Info info = settings::info(index);
        int16_t value = settings::values[index];

        if (value == info.default_value) {
            stored = board::storage_delete(info.id) && stored;
        } else {
            stored = board::storage_write(info.id, &value, sizeof(value)) && stored;
        }
    }
    output::set_hold(held);
    return stored;
}

uint8_t active_profile()
{
    return active;
}

void load_active_profile()
{
    if (!board::storage_read(profiles::ACTIVE_ID, &active, sizeof(active)) ||
        active > profiles::SLOTS) {
        active = 0;
    }
}

bool store_active_profile(uint8_t slot)
{
    active = slot;
    return board::storage_write(profiles::ACTIVE_ID, &active, sizeof(active));
}

bool read_profile(int slot, profiles::Profile &profile)
{
    if (!board::storage_read(profiles::FIRST_ID + slot - 1, &profile, sizeof(profile))) {
        return false;
    }
    profile.name[profiles::NAME_SIZE - 1] = '\0';
    return profiles::name_valid(profile.name);
}

bool write_profile(int slot, const profiles::Profile &profile)
{
    bool held = output::set_hold(true);
    bool stored = board::storage_write(profiles::FIRST_ID + slot - 1, &profile, sizeof(profile));

    output::set_hold(held);
    return stored;
}

bool delete_profile(int slot)
{
    return board::storage_delete(profiles::FIRST_ID + slot - 1);
}

} // namespace storage
