#include "core/channels.h"

namespace core {

namespace {

constexpr int32_t HALF_RANGE = CHANNEL_MAX - CHANNEL_CENTRE;
constexpr int32_t DEAD_ZONE = HALF_RANGE / 100; /* 1 % of the half range */
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

uint16_t scale_axis(uint16_t raw, const AxisCalibration &calibration, bool centred, bool inverted)
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

    if (centred && result >= CHANNEL_CENTRE - DEAD_ZONE && result <= CHANNEL_CENTRE + DEAD_ZONE) {
        result = CHANNEL_CENTRE;
    }
    if (inverted) {
        result = CHANNEL_MIN + CHANNEL_MAX - result;
    }
    return (uint16_t)result;
}

void compute_channels(const Controls &controls, const AxisCalibration calibration[AXIS_COUNT],
                      uint16_t channels[CHANNEL_COUNT])
{
    /*
     * Right and up give 2012. The raw value rises when the left stick moves up
     * and the right stick moves right, and falls for the other two movements.
     */
    channels[CHANNEL_AILERON] = scale_axis(controls.sticks[AXIS_RIGHT_HORIZONTAL],
                                           calibration[AXIS_RIGHT_HORIZONTAL], true, false);
    channels[CHANNEL_ELEVATOR] = scale_axis(controls.sticks[AXIS_RIGHT_VERTICAL],
                                            calibration[AXIS_RIGHT_VERTICAL], true, true);
    channels[CHANNEL_THROTTLE] = scale_axis(controls.sticks[AXIS_LEFT_VERTICAL],
                                            calibration[AXIS_LEFT_VERTICAL], false, false);
    channels[CHANNEL_RUDDER] = scale_axis(controls.sticks[AXIS_LEFT_HORIZONTAL],
                                          calibration[AXIS_LEFT_HORIZONTAL], true, true);

    channels[4] = two_positions(controls.sa != 0);
    channels[5] = three_positions(controls.sb);
    channels[6] = three_positions(controls.sc);
    channels[7] = two_positions(controls.sd != 0);
    channels[8] = two_positions(controls.se);
    channels[9] = two_positions(controls.sf);

    for (int channel = 10; channel < CHANNEL_COUNT; channel++) {
        channels[channel] = CHANNEL_CENTRE;
    }
}

} // namespace core
