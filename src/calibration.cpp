#include "calibration.h"

#include <zephyr/sys/util.h>

#include "core/settings.h"
#include "indicators.h"
#include "output.h"
#include "storage.h"

namespace calibration {

namespace {

bool active;
uint16_t sweep_min[core::AXIS_COUNT];
uint16_t sweep_max[core::AXIS_COUNT];

} // namespace

void start()
{
    if (active) {
        return;
    }
    for (int axis = 0; axis < core::AXIS_COUNT; axis++) {
        sweep_min[axis] = UINT16_MAX;
        sweep_max[axis] = 0;
    }
    active = true;
    output::set_hold(true);
    indicators::play_event(settings::SOUND_ACCEPTED);
}

bool running()
{
    return active;
}

void track(const uint16_t sticks[core::AXIS_COUNT])
{
    for (int axis = 0; axis < core::AXIS_COUNT; axis++) {
        sweep_min[axis] = MIN(sweep_min[axis], sticks[axis]);
        sweep_max[axis] = MAX(sweep_max[axis], sticks[axis]);
    }
}

bool finish(const uint16_t rest[core::AXIS_COUNT])
{
    core::AxisCalibration result[core::AXIS_COUNT];
    bool stored = active;

    for (int axis = 0; axis < core::AXIS_COUNT; axis++) {
        result[axis] = core::calibration_from_sweep(sweep_min[axis], sweep_max[axis], rest[axis],
                                                    axis != core::AXIS_LEFT_VERTICAL);
        stored = stored && core::calibration_valid(result[axis]);
    }
    if (stored) {
        for (int axis = 0; axis < core::AXIS_COUNT; axis++) {
            int16_t *values =
                &settings::values[settings::CALIBRATION_FIRST + settings::CALIBRATION_FIELDS * axis];

            values[0] = (int16_t)result[axis].low;
            values[1] = (int16_t)result[axis].centre;
            values[2] = (int16_t)result[axis].high;
        }
        stored = storage::save_settings();
    }
    active = false;
    output::set_hold(false);
    indicators::play_event(stored ? settings::SOUND_SUCCESS : settings::SOUND_ERROR);
    return stored;
}

void cancel()
{
    active = false;
    output::set_hold(false);
}

} // namespace calibration
