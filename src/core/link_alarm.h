/*
 * When to warn about the link to the receiver: a lost link, and the link
 * coming back. No hardware access.
 */

#pragma once

#include <stdint.h>

namespace link_alarm {

/* What the link looks like. */
enum class Level : uint8_t {
    NONE,  // no receiver seen since the warning became active
    GOOD,  // receiver connected
    LOST,  // receiver was connected and is gone
};

/* What to play now. */
enum class Event : uint8_t {
    NONE,  // nothing
    LOST,  // the link was lost
    BACK,  // the link came back after it was lost
};

struct State {
    Level level = Level::NONE;
};

/*
 * Call every loop. active: the radio sends to a receiver (module on, not
 * binding, WiFi or Bluetooth); while not active the state is reset and
 * nothing is played. connected comes from the link statistics. Each change
 * gives one event, as EdgeTX does.
 */
Event tick(State &state, bool active, bool connected);

} // namespace link_alarm
