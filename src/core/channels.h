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

/*
 * What a channel can follow; the value of the setting ch.N.source. web/app.js
 * uses the same numbers (CONTROLS), so they must not change.
 */
enum Source {
    SOURCE_NONE = 0,     /* the channel stays at the centre */
    SOURCE_AILERON = 1,  /* right stick, right gives 2012 */
    SOURCE_ELEVATOR = 2, /* right stick, up gives 2012 */
    SOURCE_THROTTLE = 3, /* left stick, up gives 2012 */
    SOURCE_RUDDER = 4,   /* left stick, right gives 2012 */
    SOURCE_SA = 5,
    SOURCE_SB = 6,
    SOURCE_SC = 7,
    SOURCE_SD = 8,
    SOURCE_SE = 9,
    SOURCE_SF = 10,
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
 *   scaled separately and readings within dead_zone percent of the half range
 *   around the centre give exactly 1500. Otherwise the whole range is scaled
 *   in one piece and dead_zone is ignored.
 * inverted: the high raw end gives 988.
 */
uint16_t scale_axis(uint16_t raw, const AxisCalibration &calibration, bool centred, bool inverted,
                    int dead_zone = 1);

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
 * Controls with the throttle at its lower end, the other sticks at their
 * calibrated centres and every switch in its first position: what a model
 * gets while nobody controls it. compute_channels() turns them into channels
 * with the mapping, weight and reverse of each channel.
 */
Controls safe_controls(const int16_t *settings_values);

/*
 * Value of a source in 988..2012, from the controls and the calibration in
 * the settings (settings::values).
 */
uint16_t source_value(Source source, const Controls &controls, const int16_t *settings_values);

/*
 * Compute all channels from the settings: each channel follows its source,
 * scaled by its weight around the centre and reversed if set.
 */
void compute_channels(const Controls &controls, const int16_t *settings_values,
                      uint16_t channels[CHANNEL_COUNT]);

/* Holds back the output after switching on until the throttle stick has unlocked it. */
class ThrottleLock {
public:
    /*
     * throttle: value of SOURCE_THROTTLE. mode: the setting unlock.mode, 0
     * unlocks at once, 1 with the throttle at its lower end, 2 with the
     * throttle at its upper and then at its lower end.
     * Returns true while the output has to stay off.
     */
    bool update(uint16_t throttle, int mode)
    {
        if (throttle >= CHANNEL_MAX - END_ZONE) {
            seen_high_ = true;
        }
        if (mode == 0 || (throttle <= CHANNEL_MIN + END_ZONE && (mode == 1 || seen_high_))) {
            locked_ = false;
        }
        return locked_;
    }

    void lock()
    {
        locked_ = true;
        seen_high_ = false;
    }

private:
    /* About 2 % of the range from each end. */
    static constexpr uint16_t END_ZONE = 20;

    bool locked_ = true;
    bool seen_high_ = false;
};

} // namespace core
