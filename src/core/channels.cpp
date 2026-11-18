#include "core/channels.h"

#include "core/settings.h"

namespace core {

namespace {

constexpr int32_t HALF_RANGE = CHANNEL_MAX - CHANNEL_CENTRE;
constexpr uint16_t MIN_SPAN = 200; /* raw counts between two calibration points */

uint16_t two_positions(bool on)
{
    return on ? CHANNEL_MAX : CHANNEL_MIN;
}

uint16_t three_positions(uint8_t position)
{
    static const uint16_t values[] = { CHANNEL_MIN, CHANNEL_CENTRE, CHANNEL_MAX };

    return values[(position < 3) ? position : 1];
}

AxisCalibration axis_calibration(const int16_t *settings_values, Axis axis)
{
    const int16_t *values =
        &settings_values[settings::CALIBRATION_FIRST + settings::CALIBRATION_FIELDS * (int)axis];

    return { (uint16_t)values[0], (uint16_t)values[1], (uint16_t)values[2] };
}

} // namespace

bool calibration_valid(const AxisCalibration &calibration)
{
    return calibration.high <= 4095 && calibration.centre <= calibration.high &&
           calibration.centre - calibration.low >= MIN_SPAN &&
           calibration.high - calibration.centre >= MIN_SPAN &&
           calibration.low < calibration.centre;
}

AxisCalibration calibration_from_sweep(uint16_t min, uint16_t max, uint16_t rest, bool centred)
{
    constexpr uint16_t MARGIN = 25;

    if (max < min + 4 * MIN_SPAN || max > 4095) {
        return {};
    }

    uint16_t high = max - MARGIN;

    if (centred) {
        return { (uint16_t)(min + MARGIN), rest, high };
    }
    /* Left anywhere above the lowest quarter, the stick was not at its lower end. */
    if (rest > min + (max - min) / 4) {
        return {};
    }

    uint16_t low = rest + MARGIN;

    return { low, (uint16_t)((low + high) / 2), high };
}

uint16_t scale_axis(uint16_t raw, const AxisCalibration &calibration, bool centred, bool inverted,
                    int dead_zone)
{
    if (!calibration_valid(calibration)) {
        return centred ? CHANNEL_CENTRE : CHANNEL_MIN;
    }

    int32_t value = raw;
    int32_t low = calibration.low;
    int32_t centre = calibration.centre;
    int32_t high = calibration.high;
    int32_t result;

    if (value < low) {
        value = low;
    }
    if (value > high) {
        value = high;
    }

    if (!centred) {
        result = CHANNEL_MIN + (value - low) * (2 * HALF_RANGE) / (high - low);
    } else if (value >= centre) {
        result = CHANNEL_CENTRE + (value - centre) * HALF_RANGE / (high - centre);
    } else {
        result = CHANNEL_CENTRE - (centre - value) * HALF_RANGE / (centre - low);
    }

    int32_t band = HALF_RANGE * dead_zone / 100;

    if (centred && result >= CHANNEL_CENTRE - band && result <= CHANNEL_CENTRE + band) {
        result = CHANNEL_CENTRE;
    }
    if (inverted) {
        result = CHANNEL_MIN + CHANNEL_MAX - result;
    }
    return (uint16_t)result;
}

uint16_t source_value(Source source, const Controls &controls, const int16_t *settings_values)
{
    /*
     * The raw value rises when the left stick moves up and the right stick
     * moves right, and falls for the other two movements.
     */
    auto stick = [&](Axis axis, bool centred, bool inverted) {
        int dead_zone = settings_values[settings::CALIBRATION_FIRST +
                                        settings::CALIBRATION_FIELDS * (int)axis +
                                        settings::CALIBRATION_DEAD_ZONE];

        return scale_axis(controls.sticks[axis], axis_calibration(settings_values, axis), centred,
                          inverted, dead_zone);
    };

    switch (source) {
    case SOURCE_AILERON:
        return stick(AXIS_RIGHT_HORIZONTAL, true, false);
    case SOURCE_ELEVATOR:
        return stick(AXIS_RIGHT_VERTICAL, true, true);
    case SOURCE_THROTTLE:
        return stick(AXIS_LEFT_VERTICAL, false, false);
    case SOURCE_RUDDER:
        return stick(AXIS_LEFT_HORIZONTAL, true, true);
    case SOURCE_SA:
        return two_positions(controls.sa != 0);
    case SOURCE_SB:
        return three_positions(controls.sb);
    case SOURCE_SC:
        return three_positions(controls.sc);
    case SOURCE_SD:
        return two_positions(controls.sd != 0);
    case SOURCE_SE:
        return two_positions(controls.se);
    case SOURCE_SF:
        return two_positions(controls.sf);
    default:
        return CHANNEL_CENTRE;
    }
}

void compute_channels(const Controls &controls, const int16_t *settings_values,
                      uint16_t channels[CHANNEL_COUNT])
{
    for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
        const int16_t *fields =
            &settings_values[settings::CHANNEL_FIRST + settings::CHANNEL_FIELDS * channel];
        int32_t deflection =
            (int32_t)source_value((Source)fields[settings::CHANNEL_SOURCE], controls,
                                  settings_values) - CHANNEL_CENTRE;

        deflection = deflection * fields[settings::CHANNEL_WEIGHT] / 100;
        if (fields[settings::CHANNEL_REVERSE] != 0) {
            deflection = -deflection;
        }
        channels[channel] = (uint16_t)(CHANNEL_CENTRE + deflection);
    }
}

} // namespace core
