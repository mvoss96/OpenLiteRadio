#include "core/sound.h"

namespace sound {

namespace {

const Tune TUNES[TUNE_COUNT] = {
    { 0, {} },
    { 1, { { 2700, 120 } } },
    { 3, { { 2700, 100 }, { 0, 80 }, { 2700, 100 } } },
    { 1, { { 3200, 300 } } },
    { 1, { { 1800, 150 } } },
    { 1, { { 600, 600 } } },
    { 3, { { 784, 250 }, { 880, 250 }, { 988, 250 } } },
    { 3, { { 988, 250 }, { 880, 250 }, { 784, 250 } } },
    { 3, { { 1047, 120 }, { 1319, 120 }, { 1568, 200 } } },
};

/* EdgeTX (radio/src/audio.cpp): trim value * 8 + 1920 Hz, the value ranging over -128..128. */
constexpr int EDGETX_TRIM_MAX = 128;
constexpr int EDGETX_CENTRE_HZ = 1920;
constexpr uint16_t EDGETX_STEP_MS = 40;
constexpr uint16_t EDGETX_MARK_MS = 80;

uint16_t edgetx_frequency(int value, int limit)
{
    return (uint16_t)(EDGETX_CENTRE_HZ + value * EDGETX_TRIM_MAX / limit * 8);
}

} // namespace

const Tune &tune(int id)
{
    return TUNES[(id >= 0 && id < TUNE_COUNT) ? id : TUNE_NONE];
}

const char *const TUNE_KEYS[TUNE_COUNT] = {
    "none", "beep", "double_beep", "short_high", "low", "low_long", "rising", "falling", "triad",
};

Tune trim_tone(int style, int before, int value, int limit)
{
    bool at_end = value == limit || value == -limit;

    if (style == TRIM_STOCK) {
        if (value == 0) {
            return { 1, { { 989, 300 } } }; /* B5 */
        }
        if (at_end) {
            return { 1, { { 659, 300 } } }; /* E5, also for every press beyond the end */
        }
        bool away = (value > 0 ? value : -value) > (before > 0 ? before : -before);

        return { 1, { { (uint16_t)(away ? 1176 : 784), 100 } } }; /* D6 away, G5 towards the centre */
    }
    if (style == TRIM_EDGETX) {
        /* EdgeTX gives the centre one 80 ms tone; here two, since one hardly differs from a step. */
        if (value == 0) {
            return { 3,
                     { { EDGETX_CENTRE_HZ, EDGETX_MARK_MS },
                       { 0, EDGETX_STEP_MS },
                       { EDGETX_CENTRE_HZ, EDGETX_MARK_MS } } };
        }
        return { 1, { { edgetx_frequency(value, limit), at_end ? EDGETX_MARK_MS : EDGETX_STEP_MS } } };
    }
    return { 0, {} };
}

Tune trim_demo(int style)
{
    /* Steps 0 -> 1 -> 2, then back 2 -> 1 -> 0, of 25, with a pause between the steps. */
    static const int VALUES[] = { 1, 2, 1, 0 };
    constexpr int LIMIT = 25;
    Tune demo = { 0, {} };
    int before = 0;

    for (int value : VALUES) {
        Tune step = trim_tone(style, before, value, LIMIT);

        if (step.count == 0 || demo.count + step.count + 1 > (int)MAX_NOTES) {
            break;
        }
        if (demo.count > 0) {
            demo.notes[demo.count++] = { 0, 150 };
        }
        for (int i = 0; i < step.count; i++) {
            demo.notes[demo.count++] = step.notes[i];
        }
        before = value;
    }
    return demo;
}

Tune trim_flash(int value, int limit)
{
    if (value == 0) {
        return { 1, { { FLASH_GREEN, 300 } } };
    }
    if (value == limit || value == -limit) {
        return { 3, { { FLASH_WHITE, 60 }, { FLASH_DARK, 60 }, { FLASH_WHITE, 60 } } };
    }
    return { 1, { { FLASH_WHITE, 60 } } };
}

uint8_t volume_duty(int volume)
{
    static const uint8_t DUTY[] = { 0, 10, 25, 50 };

    return (volume >= 0 && volume < 4) ? DUTY[volume] : 0;
}

} // namespace sound
