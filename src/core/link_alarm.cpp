#include "core/link_alarm.h"

namespace link_alarm {

Event tick(State &state, bool active, bool connected, int64_t now_ms, uint32_t repeat_ms)
{
    if (!active) {
        state.level = Level::NONE;
        return Event::NONE;
    }

    Level before = state.level;

    if (connected) {
        state.level = Level::GOOD;
        return before == Level::LOST ? Event::BACK : Event::NONE;
    }
    if (before == Level::NONE) {
        return Event::NONE; /* without a receiver seen there is nothing to lose */
    }
    state.level = Level::LOST;
    if (before != Level::LOST || (repeat_ms != 0 && now_ms - state.warned_ms >= (int64_t)repeat_ms)) {
        state.warned_ms = now_ms;
        return Event::LOST;
    }
    return Event::NONE;
}

} // namespace link_alarm
