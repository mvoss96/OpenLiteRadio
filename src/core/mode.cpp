#include "core/mode.h"

namespace core {

const char *mode_name(Mode mode)
{
    switch (mode) {
    case Mode::OFF:
        return "off";
    case Mode::USB_JOYSTICK:
        return "usb_joystick";
    case Mode::USB_XBOX:
        return "usb_xbox";
    case Mode::ELRS_LOCKED:
        return "elrs_locked";
    case Mode::ELRS_ACTIVE:
        return "elrs_active";
    case Mode::ELRS_HOST_READY:
        return "elrs_host_ready";
    case Mode::ELRS_HOST:
        return "elrs_host";
    case Mode::ELRS_BINDING:
        return "elrs_binding";
    case Mode::BLUETOOTH:
        return "bluetooth";
    case Mode::WIFI:
        return "wifi";
    }
    return "?";
}

/*
 * Rules in order: the module decides between USB and ELRS; a hold locks and
 * ignores the throttle, so that no part of the unlock gesture is made while
 * held; entering ELRS and a changed setting lock; the unlock gesture opens.
 */
Mode ModeMachine::update(const ModeInputs &in)
{
    if (!in.module_on) {
        mode_ = in.usb_host ? Mode::USB_JOYSTICK : Mode::OFF;
        return mode_;
    }
    bool entering = mode_ != Mode::ELRS_LOCKED && mode_ != Mode::ELRS_ACTIVE;

    if (entering || in.relock || in.held) {
        lock_.lock();
        mode_ = Mode::ELRS_LOCKED;
    }
    if (in.held) {
        return mode_;
    }
    /* Evaluated every period, so that mode 2 sees the throttle go up first. */
    if (!lock_.update(in.throttle, in.unlock_mode)) {
        mode_ = Mode::ELRS_ACTIVE;
    }
    return mode_;
}

} // namespace core
