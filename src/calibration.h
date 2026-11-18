/*
 * Stick calibration run: started with SETUP held for 2 s or by the host.
 * During the run the sticks are moved to all ends and no channel frames go
 * out. At the end the self-centring axes are released and the throttle is at
 * its lower end.
 */

#pragma once

#include <stdint.h>

#include "core/channels.h"

namespace calibration {

void start();
bool running();

/* Call every loop during a run with the raw stick readings. */
void track(const uint16_t sticks[core::AXIS_COUNT]);

/*
 * End the run. If the result is valid for all axes it goes into the settings
 * cal.N.* and all settings are stored. Returns false otherwise.
 */
bool finish(const uint16_t rest[core::AXIS_COUNT]);

/* End the run and keep the previous calibration. */
void cancel();

} // namespace calibration
