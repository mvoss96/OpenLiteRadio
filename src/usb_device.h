/*
 * The USB device in one of three forms, chosen at run time; the serial port
 * is in every one. Changing the form makes the device leave the bus and come
 * back with the new description, so the host sees a new device.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Form of the USB device. */
enum usb_function {
	USB_SERIAL_ONLY = 0, /* serial port only, product id 0x0004 */
	USB_JOYSTICK = 1,    /* serial port and HID joystick, product id 0x0005 */
	USB_XBOX = 2,        /* Xbox 360 controller and serial port, product id 0x0007 */
};

/* Present the device in this form; nothing happens if it already is. Returns 0 or an error. */
int usb_device_select(enum usb_function function);

/* The form presented now. */
enum usb_function usb_device_function(void);

/* True while the host has set up the controller of the form now presented. */
bool usb_controller_ready(void);

/* Send a joystick report of joystick::REPORT_SIZE bytes; only in USB_JOYSTICK. */
void usb_joystick_send(const uint8_t *report);

/* Send an Xbox report of xbox::REPORT_SIZE bytes; only in USB_XBOX. */
void usb_xbox_send(const uint8_t *report);

#ifdef __cplusplus
}
#endif
