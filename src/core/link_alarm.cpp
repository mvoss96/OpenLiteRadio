#include "core/link_alarm.h"

namespace link_alarm {

Event tick(State &state, bool active, bool connected)
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
    if (before != Level::GOOD) {
        return Event::NONE; /* no receiver seen yet, or already reported as lost */
    }
    state.level = Level::LOST;
    return Event::LOST;
}

} // namespace link_alarm
