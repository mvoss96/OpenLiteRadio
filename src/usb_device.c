/*
 * The USB device: one configuration with the serial port (console and host
 * protocol) and a HID joystick. Replaces Zephyr's CDC_ACM_SERIAL_INITIALIZE_AT_BOOT,
 * which registers the serial port only.
 *
 * The product ID differs from the serial-only firmware (0x0004) so that
 * Windows does not reuse the cached description of the older device.
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/hid.h>
#include <zephyr/usb/class/usbd_hid.h>

#define USB_VID 0x2fe3
#define USB_PID 0x0005
#define REPORT_SIZE 17

USBD_DEVICE_DEFINE(radio_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), USB_VID, USB_PID);
USBD_DESC_LANG_DEFINE(radio_lang);
USBD_DESC_MANUFACTURER_DEFINE(radio_mfr, "OpenLiteRadio");
USBD_DESC_PRODUCT_DEFINE(radio_product, "LiteRadio 4");
USBD_DESC_CONFIG_DEFINE(radio_cfg_desc, "Serial port and joystick");
/* 125 * 2 mA, as the serial-only firmware declared. */
USBD_CONFIGURATION_DEFINE(radio_config, 0, 125, &radio_cfg_desc);

/* Joystick: 8 buttons, then 8 axes X, Y, Z, Rx, Ry, Rz, slider, dial of 16 bits, 0 to 2047. */
static const uint8_t report_descriptor[] = {
	0x05, 0x01,       /* usage page: generic desktop */
	0x09, 0x04,       /* usage: joystick */
	0xa1, 0x01,       /* collection: application */
	0x05, 0x09,       /*   usage page: buttons */
	0x19, 0x01,       /*   usage minimum 1 */
	0x29, 0x08,       /*   usage maximum 8 */
	0x15, 0x00,       /*   logical minimum 0 */
	0x25, 0x01,       /*   logical maximum 1 */
	0x75, 0x01,       /*   report size 1 */
	0x95, 0x08,       /*   report count 8 */
	0x81, 0x02,       /*   input: data, variable, absolute */
	0x05, 0x01,       /*   usage page: generic desktop */
	0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x33,
	0x09, 0x34, 0x09, 0x35, 0x09, 0x36, 0x09, 0x37,
	0x15, 0x00,       /*   logical minimum 0 */
	0x26, 0xff, 0x07, /*   logical maximum 2047 */
	0x75, 0x10,       /*   report size 16 */
	0x95, 0x08,       /*   report count 8 */
	0x81, 0x02,       /*   input: data, variable, absolute */
	0xc0,             /* end collection */
};

static const struct device *const joystick = DEVICE_DT_GET(DT_NODELABEL(joystick_hid));
/* The stack sends from this buffer without copying it; it is not touched while busy. */
static uint8_t __aligned(sizeof(void *)) report[REPORT_SIZE];
static atomic_t report_busy;

static void joystick_ready(const struct device *dev, const bool ready)
{
	ARG_UNUSED(dev);
	if (!ready) {
		atomic_clear(&report_busy);
	}
}

/* The host may ask for the input report; answer with the last one. */
static int joystick_get_report(const struct device *dev, const uint8_t type, const uint8_t id,
			       const uint16_t len, uint8_t *const buf)
{
	ARG_UNUSED(dev);
	if (type != HID_REPORT_TYPE_INPUT || id != 0U || len < REPORT_SIZE) {
		return -ENOTSUP;
	}
	memcpy(buf, report, REPORT_SIZE);
	return REPORT_SIZE;
}

static void joystick_report_done(const struct device *dev, const uint8_t *const sent)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(sent);
	atomic_clear(&report_busy);
}

static const struct hid_device_ops joystick_ops = {
	.iface_ready = joystick_ready,
	.get_report = joystick_get_report,
	.input_report_done = joystick_report_done,
};

/* Send a joystick report; dropped while the previous one is still on its way. */
void usb_joystick_send(const uint8_t *data)
{
	if (!atomic_cas(&report_busy, 0, 1)) {
		return;
	}
	memcpy(report, data, REPORT_SIZE);
	if (hid_device_submit_report(joystick, REPORT_SIZE, report) != 0) {
		atomic_clear(&report_busy);
	}
}

static int usb_device_init(void)
{
	int err = hid_device_register(joystick, report_descriptor, sizeof(report_descriptor),
				      &joystick_ops);

	if (err == 0) {
		err = usbd_add_descriptor(&radio_usbd, &radio_lang);
	}
	if (err == 0) {
		err = usbd_add_descriptor(&radio_usbd, &radio_mfr);
	}
	if (err == 0) {
		err = usbd_add_descriptor(&radio_usbd, &radio_product);
	}
	if (err == 0) {
		err = usbd_add_configuration(&radio_usbd, USBD_SPEED_FS, &radio_config);
	}
	if (err == 0) {
		err = usbd_register_all_classes(&radio_usbd, USBD_SPEED_FS, 1, NULL);
	}
	if (err == 0) {
		/* Interface association descriptors, needed for the serial port's two interfaces. */
		err = usbd_device_set_code_triple(&radio_usbd, USBD_SPEED_FS,
						  USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	}
	if (err == 0) {
		err = usbd_init(&radio_usbd);
	}
	if (err == 0) {
		err = usbd_enable(&radio_usbd);
	}
	return err;
}

SYS_INIT(usb_device_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
