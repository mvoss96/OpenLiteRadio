/*
 * What the ELRS module sends: timing frames set the frame period, link
 * statistics are kept, and parameter answers drive the search for the
 * module's commands (bind, WiFi, Bluetooth joystick).
 */

#pragma once

#include <stdint.h>

#include "core/mode.h"

namespace module_link {

struct Status {
    uint32_t frames;      /* frames received with a correct checksum */
    uint32_t crc_errors;
    uint8_t link_quality; /* percent, from the receiver's link statistics */
    uint8_t rssi;         /* dBm without the sign */
    int32_t timing_offset_us;
    uint8_t bind_parameter; /* 0 until it was searched */
};

/* Commands of the module that the radio starts. */
enum Command {
    COMMAND_BIND,       // "Bind": BIND held 1 s, or "bind" from the host
    COMMAND_WIFI,       // "Enable WiFi": right trim centre at switch-on, or "wifi"
    COMMAND_BLUETOOTH,  // "BLE Joystick": left trim centre at switch-on, or "ble"
    COMMAND_COUNT,
};

/* Switch the module on or off; a running search and the module's task end. */
void set_module_on(bool on);

/*
 * Start a command on the module, once it answers after switching on: find it
 * by name, write it, and set the module's task, which sets the mode. Nothing
 * happens while another command runs. A tone tells whether it started.
 */
void command_start(Command command);

/* End binding early, as a short press of BIND does. */
void bind_cancel();

/* The task of the command being started, NONE if none; the output stays locked until it runs. */
core::ModuleTask starting_task();

/* Print every frame from the module as "rx" plus hex bytes. */
void set_show_frames(bool show);

/* Call every loop: read the module's frames and repeat unanswered requests. */
void tick();

Status status();

/* True while link statistics with a link quality above 0 arrived within the last second. */
bool receiver_connected();

} // namespace module_link
