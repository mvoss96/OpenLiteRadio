#include "storage.h"

#include "board/board.h"
#include "core/channels.h"
#include "core/settings.h"
#include "output.h"

namespace storage {

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

namespace {

/* Store count settings from first on. The storage skips a record that already holds the same value. */
bool save(int first, int count)
{
    bool stored = true;
    /* A calibration run may already hold the output; it keeps holding it afterwards. */
    bool held = output::set_hold(true);

    for (int index = first; index < first + count; index++) {
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

} // namespace

bool save_settings()
{
    return save(0, settings::COUNT);
}

bool save_trim()
{
    return save(settings::TRIM_FIRST, core::AXIS_COUNT);
}

bool save_setting(int index)
{
    return save(index, 1);
}

} // namespace storage
