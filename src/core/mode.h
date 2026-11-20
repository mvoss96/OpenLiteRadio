#pragma once

#include <stdint.h>

#include "core/channels.h"

/*
 * The mode of the radio: exactly one at a time, see "Operating modes" in
 * docs/ARCHITECTURE.md. Built so far: off, USB joystick, ELRS locked and
 * active, binding, Bluetooth and WiFi; Xbox and PC control have no
 * transitions yet.
 */
namespace core {

/* What the radio is doing; exactly one at a time. */
enum class Mode : uint8_t {
    OFF,              // radio off, no PC: nothing (charging only)
    USB_JOYSTICK,     // radio off at a PC: DirectInput joystick from the controls
    USB_XBOX,         // radio off at a PC: Xbox controller from the controls
    ELRS_LOCKED,      // module on: no channel frames until the unlock gesture
    ELRS_ACTIVE,      // module on: channel frames with the controls
    ELRS_HOST_READY,  // PC control allowed at the radio: as locked until the PC sends values
    ELRS_HOST,        // channel frames with the values from the PC, controls ignored
    ELRS_BINDING,     // bind command sent; the module looks for a receiver
    BLUETOOTH,        // module is a Bluetooth joystick: frames with the controls, no lock
    WIFI,             // module runs its web page for updates: no frames
};

/* Name of a mode for the state line, e.g. "elrs_active". */
const char *mode_name(Mode mode);

/* A command the radio started on the module, which changes what it does. */
enum class ModuleTask : uint8_t {
    NONE,       // the module sends what the radio gives it
    BINDING,    // "Bind" started; ends when a receiver answers, after 8 s or by BIND
    WIFI,       // "Enable WiFi" started; only a restart of the module ends it
    BLUETOOTH,  // "BLE Joystick" started; only a restart of the module ends it
};

/* What the mode machine reads once per frame period. */
struct ModeInputs {
    bool module_on;     // the ELRS module is switched on
    ModuleTask task;    // command running on the module
    bool usb_host;      // a PC has set up the USB controller
    bool usb_xbox;      // the USB controller is the Xbox controller, not the joystick
    bool held;          // calibration or storing holds the output back
    bool relock;        // a setting changed since the last update (an event, true once)
    uint16_t throttle;  // value of SOURCE_THROTTLE in microseconds
    int unlock_mode;    // setting unlock.mode
};

/* Decides the mode from the inputs; called by the control thread once per frame period. */
class ModeMachine {
public:
    Mode update(const ModeInputs &in);

    Mode mode() const
    {
        return mode_;
    }

private:
    Mode mode_ = Mode::OFF;
    ThrottleLock lock_;
};

} // namespace core
