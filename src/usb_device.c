/*
 * The USB device, built at run time in one of the forms of usb_device.h.
 * Replaces Zephyr's CDC_ACM_SERIAL_INITIALIZE_AT_BOOT, which registers the
 * serial port only.
 *
 * Each form has its own product id, because Windows caches the description
 * of a device per vendor and product id. The Xbox form carries MS OS 2.0
 * descriptors with the compatible id XUSB10 for its first interface, which
 * makes Windows load its Xbox 360 controller driver for our own vendor id.
 */

#include "usb_device.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>
#include <zephyr/usb/msos_desc.h>
#include <zephyr/usb/class/hid.h>
#include <zephyr/usb/class/usbd_hid.h>

#define USB_VID 0x2fe3
/* Also joystick::REPORT_SIZE in src/core/joystick.h; output.cpp checks that they agree. */
#define REPORT_SIZE 17
BUILD_ASSERT(REPORT_SIZE == DT_PROP(DT_NODELABEL(joystick_hid), in_report_size),
	     "in-report-size of joystick_hid in the devicetree must be the report size");
#define MSOS2_VENDOR_CODE 0x01

/* Product id of each form, indexed by enum usb_function. */
static const uint16_t product_ids[] = {0x0004, 0x0005, 0x0007};

USBD_DEVICE_DEFINE(radio_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), USB_VID, 0x0004);
USBD_DESC_LANG_DEFINE(radio_lang);
USBD_DESC_MANUFACTURER_DEFINE(radio_mfr, "OpenLiteRadio");
USBD_DESC_PRODUCT_DEFINE(radio_product, "LiteRadio 4");
USBD_DESC_CONFIG_DEFINE(radio_cfg_desc, "LiteRadio 4");
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
static atomic_t joystick_is_ready;
/* The form presented, an enum usb_function; USB_SERIAL_ONLY while it changes. */
static atomic_t presented = ATOMIC_INIT(USB_SERIAL_ONLY);
static bool built;

static void joystick_ready(const struct device *dev, const bool ready)
{
	ARG_UNUSED(dev);
	atomic_set(&joystick_is_ready, ready ? 1 : 0);
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

/* MS OS 2.0 descriptor set of the Xbox form: XUSB10 for interface 0, the Xbox interface. */
struct msos2_set {
	struct msosv2_descriptor_set_header header;
	struct msosv2_configuration_subset_header config;
	struct msosv2_function_subset_header function;
	struct msosv2_compatible_id compatible_id;
} __packed;

static const struct msos2_set msos2_set = {
	.header = {
		.wLength = sizeof(struct msosv2_descriptor_set_header),
		.wDescriptorType = MS_OS_20_SET_HEADER_DESCRIPTOR,
		.dwWindowsVersion = sys_cpu_to_le32(0x0A000000UL),
		.wTotalLength = sizeof(struct msos2_set),
	},
	.config = {
		.wLength = sizeof(struct msosv2_configuration_subset_header),
		.wDescriptorType = MS_OS_20_SUBSET_HEADER_CONFIGURATION,
		.bConfigurationValue = 0,
		.bReserved = 0,
		.wTotalLength = sizeof(struct msos2_set) - sizeof(struct msosv2_descriptor_set_header),
	},
	.function = {
		.wLength = sizeof(struct msosv2_function_subset_header),
		.wDescriptorType = MS_OS_20_SUBSET_HEADER_FUNCTION,
		.bFirstInterface = 0,
		.bReserved = 0,
		.wSubsetLength = sizeof(struct msosv2_function_subset_header) +
				 sizeof(struct msosv2_compatible_id),
	},
	.compatible_id = {
		.wLength = sizeof(struct msosv2_compatible_id),
		.wDescriptorType = MS_OS_20_FEATURE_COMPATIBLE_ID,
		.CompatibleID = {'X', 'U', 'S', 'B', '1', '0', 0x00, 0x00},
	},
};

struct bos_msos2 {
	struct usb_bos_platform_descriptor platform;
	struct usb_bos_capability_msos cap;
} __packed;

static struct bos_msos2 bos_msos2 = {
	.platform = {
		.bLength = sizeof(struct bos_msos2),
		.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
		.bDevCapabilityType = USB_BOS_CAPABILITY_PLATFORM,
		.bReserved = 0,
		/* MS OS 2.0 platform capability UUID D8DD60DF-4589-4CC7-9CD2-659D9E648A9F */
		.PlatformCapabilityUUID = {0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
					   0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F},
	},
	.cap = {
		.dwWindowsVersion = sys_cpu_to_le32(0x0A000000UL),
		.wMSOSDescriptorSetTotalLength = sys_cpu_to_le16(sizeof(struct msos2_set)),
		.bMS_VendorCode = MSOS2_VENDOR_CODE,
		.bAltEnumCode = 0,
	},
};

static struct net_buf *msos2_to_host(const struct usbd_context *const ctx,
				     const struct usb_setup_packet *const setup)
{
	struct net_buf *buf;
	size_t size = MIN(setup->wLength, sizeof(msos2_set));

	if (setup->bRequest != MSOS2_VENDOR_CODE || setup->wIndex != MS_OS_20_DESCRIPTOR_INDEX) {
		return NULL;
	}
	buf = usbd_ep_ctrl_data_in_alloc(ctx, size);
	if (buf != NULL) {
		net_buf_add_mem(buf, &msos2_set, size);
	}
	return buf;
}

USBD_DESC_BOS_VREQ_DEFINE(bos_vreq_msos2, sizeof(bos_msos2), &bos_msos2, MSOS2_VENDOR_CODE,
			  msos2_to_host, NULL);

void usb_xinput_send(const uint8_t *report);
bool usb_xinput_ready(void);

/* In src/board/board.cpp: makes the host see the device unplugged. */
void board_signal_usb_disconnect(void);

/* Describe and start the device in one form; the stack is shut down before. */
static int build(enum usb_function function)
{
	int err = usbd_device_set_pid(&radio_usbd, product_ids[function]);

	/* USB 2.01 announces the BOS descriptor, which only the Xbox form has. */
	if (err == 0) {
		err = usbd_device_set_bcd_usb(&radio_usbd, USBD_SPEED_FS,
					      function == USB_XBOX ? 0x0201 : 0x0200);
	}
	if (err == 0) {
		err = usbd_add_descriptor(&radio_usbd, &radio_lang);
	}
	if (err == 0) {
		err = usbd_add_descriptor(&radio_usbd, &radio_mfr);
	}
	if (err == 0) {
		err = usbd_add_descriptor(&radio_usbd, &radio_product);
	}
	if (err == 0 && function == USB_XBOX) {
		err = usbd_add_descriptor(&radio_usbd, &bos_vreq_msos2);
	}
	if (err == 0) {
		err = usbd_add_configuration(&radio_usbd, USBD_SPEED_FS, &radio_config);
	}
	/* The Xbox interface first, so that it is interface 0 as the MS OS descriptor says. */
	if (err == 0 && function == USB_XBOX) {
		err = usbd_register_class(&radio_usbd, "xinput_0", USBD_SPEED_FS, 1);
	}
	if (err == 0) {
		err = usbd_register_class(&radio_usbd, "cdc_acm_0", USBD_SPEED_FS, 1);
	}
	if (err == 0 && function == USB_JOYSTICK) {
		err = usbd_register_class(&radio_usbd, "hid_0", USBD_SPEED_FS, 1);
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

int usb_device_select(enum usb_function function)
{
	int err;

	if (built && atomic_get(&presented) == function) {
		return 0;
	}
	/* No reports go out while the device changes. */
	atomic_set(&presented, USB_SERIAL_ONLY);
	atomic_clear(&joystick_is_ready);
	if (built) {
		(void)usbd_disable(&radio_usbd);
		(void)usbd_shutdown(&radio_usbd);
		board_signal_usb_disconnect();
	}
	built = true;
	err = build(function);
	if (err == 0) {
		atomic_set(&presented, function);
	} else if (function != USB_SERIAL_ONLY) {
		/* Keep at least the serial port, so that the host can still reach the radio. */
		(void)usbd_disable(&radio_usbd);
		(void)usbd_shutdown(&radio_usbd);
		board_signal_usb_disconnect();
		(void)build(USB_SERIAL_ONLY);
	}
	return err;
}

enum usb_function usb_device_function(void)
{
	return (enum usb_function)atomic_get(&presented);
}

bool usb_controller_ready(void)
{
	switch (usb_device_function()) {
	case USB_JOYSTICK:
		return atomic_get(&joystick_is_ready) != 0;
	case USB_XBOX:
		return usb_xinput_ready();
	default:
		return false;
	}
}

/* Send a joystick report; dropped while the previous one is still on its way. */
void usb_joystick_send(const uint8_t *data)
{
	if (usb_device_function() != USB_JOYSTICK || !atomic_cas(&report_busy, 0, 1)) {
		return;
	}
	memcpy(report, data, REPORT_SIZE);
	if (hid_device_submit_report(joystick, REPORT_SIZE, report) != 0) {
		atomic_clear(&report_busy);
	}
}

void usb_xbox_send(const uint8_t *data)
{
	if (usb_device_function() == USB_XBOX) {
		usb_xinput_send(data);
	}
}

/* The joystick's HID description is set once; its class is registered only in the joystick form. */
static int usb_device_init(void)
{
	return hid_device_register(joystick, report_descriptor, sizeof(report_descriptor),
				   &joystick_ops);
}

SYS_INIT(usb_device_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
