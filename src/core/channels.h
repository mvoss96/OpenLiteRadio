/*
 * From raw control readings to channel values in microseconds. No hardware
 * access.
 */

#pragma once

#include <stdint.h>

namespace core {

constexpr uint16_t CHANNEL_MIN = 988;
constexpr uint16_t CHANNEL_CENTRE = 1500;
constexpr uint16_t CHANNEL_MAX = 2012;
constexpr int CHANNEL_COUNT = 16;

/* Stick axes in the order the board layer reads them. */
enum Axis {
    AXIS_LEFT_VERTICAL = 0,
    AXIS_LEFT_HORIZONTAL = 1,
    AXIS_RIGHT_VERTICAL = 2,
    AXIS_RIGHT_HORIZONTAL = 3,
    AXIS_COUNT = 4,
};

/* Channel numbers, counted from 0. */
enum Channel {
    CHANNEL_AILERON = 0,
    CHANNEL_ELEVATOR = 1,
    CHANNEL_THROTTLE = 2,
    CHANNEL_RUDDER = 3,
};

/* Raw 12-bit readings of one axis at its two ends and at rest. */
struct AxisCalibration {
    uint16_t low;
    uint16_t centre;
    uint16_t high;
};

/* True if the three values are in ascending order and far enough apart to divide by. */
bool calibration_valid(const AxisCalibration &calibration);

/*
 * Build the calibration of one axis from a calibration run.
 * min, max: smallest and largest raw reading while the stick was moved to all ends.
 * rest: raw reading at the end of the run, with the stick released.
 * centred: the axis returns to its centre by itself; rest is then the centre.
 *   Otherwise the stick was left at its lower end and rest becomes that end,
 *   because the stick springs back a little from the stop.
 * The ends are moved inward by 25 counts so that the output reaches both limits.
 * The result is not valid if the stick was not moved or not left where required.
 */
AxisCalibration calibration_from_sweep(uint16_t min, uint16_t max, uint16_t rest, bool centred);

/*
 * Scale a raw reading to 988..2012.
 * centred: the axis returns to its centre by itself. The two halves are then
 *   scaled separately and readings within 1 % of the centre give exactly 1500.
 *   Otherwise the whole range is scaled in one piece.
 * inverted: the high raw end gives 988.
 */
uint16_t scale_axis(uint16_t raw, const AxisCalibration &calibration, bool centred, bool inverted);

/* What the channel computation needs from the controls. */
struct Controls {
    uint16_t sticks[AXIS_COUNT];
    uint8_t sa; /* 0 or 1 */
    uint8_t sb; /* 0, 1, 2 */
    uint8_t sc; /* 0, 1, 2 */
    uint8_t sd; /* 0 or 1 */
    bool se;
    bool sf;
};

/*
 * Compute all channels: aileron, elevator, throttle, rudder from the sticks
 * (mode 2), then SA, SB, SC, SD, SE, SF on channels 5 to 10. The remaining
 * channels are at the centre.
 */
void compute_channels(const Controls &controls, const AxisCalibration calibration[AXIS_COUNT],
                      uint16_t channels[CHANNEL_COUNT]);

/*
 * Holds back the output after switching on until the throttle has been at its
 * lower end once.
 */
class ThrottleLock {
public:
    /* Returns true while the output has to stay off. */
    bool update(uint16_t throttle)
    {
        if (throttle <= RELEASE_BELOW) {
            locked_ = false;
        }
        return locked_;
    }

    void lock() { locked_ = true; }
    bool locked() const { return locked_; }

private:
    /* About 2 % of the range above the lower end. */
    static constexpr uint16_t RELEASE_BELOW = CHANNEL_MIN + 20;

    bool locked_ = true;
};

} // namespace core
