#include "storage.h"

#include "board/board.h"
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

} // namespace storage
