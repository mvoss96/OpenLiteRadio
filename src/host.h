/* The line protocol on the USB serial port; described in host.cpp and docs/ARCHITECTURE.md. */

#pragma once

#include "output.h"

namespace host {

void print_hello();

/* Call every loop: run the commands that arrived and continue a "list". */
void tick();

/* Whether the host wants state lines ("stream 0|1"). */
bool streaming();

void print_state(const output::Snapshot &snapshot, bool radio_on);

} // namespace host
