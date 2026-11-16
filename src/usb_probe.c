#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>

/* Placeholder IDs for a compile-only probe, not a distributable USB identity. */
USBD_DEVICE_DEFINE(probe_usb, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0xffff, 0xffff);
USBD_DESC_LANG_DEFINE(probe_language);
USBD_DESC_MANUFACTURER_DEFINE(probe_manufacturer, "OpenLiteRadio");
USBD_DESC_PRODUCT_DEFINE(probe_product, "Compile-only feasibility probe");
USBD_DESC_CONFIG_DEFINE(probe_config_description, "HID and CDC");
USBD_CONFIGURATION_DEFINE(probe_configuration, 0, 100, &probe_config_description);

static const uint8_t report_descriptor[] = {
    0x05, 0x01, 0x09, 0x04, 0xa1, 0x01,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
    0x15, 0x00, 0x26, 0xff, 0x0f,
    0x75, 0x10, 0x95, 0x04, 0x81, 0x02, 0xc0,
};
static const struct hid_device_ops hid_ops;

int usb_probe_init(void)
{
    int error = hid_device_register(DEVICE_DT_GET(DT_NODELABEL(hid_probe)),
                                    report_descriptor, sizeof(report_descriptor), &hid_ops);
    if (error) { return error; }
    error = usbd_add_descriptor(&probe_usb, &probe_language);
    if (error) { return error; }
    error = usbd_add_descriptor(&probe_usb, &probe_manufacturer);
    if (error) { return error; }
    error = usbd_add_descriptor(&probe_usb, &probe_product);
    if (error) { return error; }
    error = usbd_add_configuration(&probe_usb, USBD_SPEED_FS, &probe_configuration);
    if (error) { return error; }
    error = usbd_register_all_classes(&probe_usb, USBD_SPEED_FS, 1, NULL);
    if (error) { return error; }
    usbd_device_set_code_triple(&probe_usb, USBD_SPEED_FS, 0xef, 0x02, 0x01);
    error = usbd_init(&probe_usb);
    if (error) { return error; }
    return usbd_enable(&probe_usb);
}
