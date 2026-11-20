/*
 * Xbox 360 controller function (XInput) for the USB device in usb_device.c.
 * Windows binds its xusb22 driver to it through the compatible id XUSB10 in
 * the MS OS 2.0 descriptors that usb_device.c adds for this function.
 *
 * One vendor interface (class 0xff, subclass 0x5d, protocol 0x01) with an
 * interrupt IN endpoint for 20-byte reports and an interrupt OUT endpoint
 * for LED and rumble commands, which are read and dropped.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/usb/udc.h>

#define XINPUT_REPORT_SIZE 20
#define XINPUT_EP_MPS      32

/* Class-specific descriptor of a wired Xbox 360 controller, copied from its interface 0. */
struct xinput_unknown_desc {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t data[15];
} __packed;

struct xinput_desc {
	struct usb_if_descriptor if0;
	struct xinput_unknown_desc vendor;
	struct usb_ep_descriptor in_ep;
	struct usb_ep_descriptor out_ep;
	struct usb_desc_header nil_desc;
};

static struct xinput_desc xinput_desc = {
	.if0 = {
		.bLength = sizeof(struct usb_if_descriptor),
		.bDescriptorType = USB_DESC_INTERFACE,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_BCC_VENDOR,
		.bInterfaceSubClass = 0x5d,
		.bInterfaceProtocol = 0x01,
		.iInterface = 0,
	},
	.vendor = {
		.bLength = sizeof(struct xinput_unknown_desc),
		.bDescriptorType = 0x21,
		.data = {0x00, 0x01, 0x01, 0x25, 0x81, 0x14, 0x00, 0x00,
			 0x00, 0x00, 0x13, 0x01, 0x08, 0x00, 0x00},
	},
	.in_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = 0x81,
		.bmAttributes = USB_EP_TYPE_INTERRUPT,
		.wMaxPacketSize = sys_cpu_to_le16(XINPUT_EP_MPS),
		.bInterval = 4,
	},
	.out_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = 0x01,
		.bmAttributes = USB_EP_TYPE_INTERRUPT,
		.wMaxPacketSize = sys_cpu_to_le16(XINPUT_EP_MPS),
		.bInterval = 8,
	},
	.nil_desc = {
		.bLength = 0,
		.bDescriptorType = 0,
	},
};

static const struct usb_desc_header *const xinput_fs_desc[] = {
	(struct usb_desc_header *)&xinput_desc.if0,
	(struct usb_desc_header *)&xinput_desc.vendor,
	(struct usb_desc_header *)&xinput_desc.in_ep,
	(struct usb_desc_header *)&xinput_desc.out_ep,
	(struct usb_desc_header *)&xinput_desc.nil_desc,
};

static atomic_t xinput_enabled;
static atomic_t xinput_in_busy;

static int xinput_enqueue_out(struct usbd_class_data *const c_data)
{
	struct net_buf *buf = usbd_ep_buf_alloc(c_data, xinput_desc.out_ep.bEndpointAddress,
						XINPUT_EP_MPS);
	int err;

	if (buf == NULL) {
		return -ENOMEM;
	}
	err = usbd_ep_enqueue(c_data, buf);
	if (err != 0) {
		net_buf_unref(buf);
	}
	return err;
}

static int xinput_request(struct usbd_class_data *const c_data, struct net_buf *buf, int err)
{
	struct udc_buf_info *bi = (struct udc_buf_info *)net_buf_user_data(buf);
	uint8_t ep = bi->ep;

	net_buf_unref(buf);
	if (ep == xinput_desc.in_ep.bEndpointAddress) {
		atomic_clear(&xinput_in_busy);
	} else if (err != -ECONNABORTED && atomic_get(&xinput_enabled) != 0) {
		/* LED and rumble commands: read the next one. */
		(void)xinput_enqueue_out(c_data);
	}
	return 0;
}

static void xinput_enable(struct usbd_class_data *const c_data)
{
	atomic_set(&xinput_enabled, 1);
	(void)xinput_enqueue_out(c_data);
}

static void xinput_disable(struct usbd_class_data *const c_data)
{
	ARG_UNUSED(c_data);
	atomic_clear(&xinput_enabled);
	atomic_clear(&xinput_in_busy);
}

static const void *xinput_get_desc(struct usbd_class_data *const c_data,
				   const enum usbd_speed speed)
{
	ARG_UNUSED(c_data);
	ARG_UNUSED(speed);
	return xinput_fs_desc;
}

static int xinput_init(struct usbd_class_data *const c_data)
{
	ARG_UNUSED(c_data);
	return 0;
}

static const struct usbd_class_api xinput_api = {
	.request = xinput_request,
	.enable = xinput_enable,
	.disable = xinput_disable,
	.get_desc = xinput_get_desc,
	.init = xinput_init,
};

USBD_DEFINE_CLASS(xinput_0, &xinput_api, NULL, NULL);

/* Send one 20-byte report; dropped while the previous one is on its way. */
void usb_xinput_send(const uint8_t *report)
{
	struct net_buf *buf;

	if (atomic_get(&xinput_enabled) == 0 || !atomic_cas(&xinput_in_busy, 0, 1)) {
		return;
	}
	buf = usbd_ep_buf_alloc(&xinput_0, xinput_desc.in_ep.bEndpointAddress,
				XINPUT_REPORT_SIZE);
	if (buf == NULL) {
		atomic_clear(&xinput_in_busy);
		return;
	}
	net_buf_add_mem(buf, report, XINPUT_REPORT_SIZE);
	if (usbd_ep_enqueue(&xinput_0, buf) != 0) {
		net_buf_unref(buf);
		atomic_clear(&xinput_in_busy);
	}
}

/* True while the host has set up the Xbox interface. */
bool usb_xinput_ready(void)
{
	return atomic_get(&xinput_enabled) != 0;
}
