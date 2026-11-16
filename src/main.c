#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/uart.h>

/* Debugger-visible probe results; no frames are sent to the module. */
volatile int probe_status;
volatile int16_t probe_axes[4];

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
int usb_probe_init(void);
#endif

int main(void)
{
    const struct device *adc = DEVICE_DT_GET(DT_NODELABEL(adc1));
    const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(usart1));
    int16_t samples[4];
    struct adc_sequence sequence = {
        .channels = 0x0f,
        .buffer = samples,
        .buffer_size = sizeof(samples),
        .resolution = 12,
    };

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
    probe_status = usb_probe_init();
    if (probe_status < 0) {
        return 0;
    }
#endif

    if (!device_is_ready(adc) || !device_is_ready(uart)) {
        probe_status = -ENODEV;
        return 0;
    }
    for (uint8_t channel = 0; channel < 4; channel++) {
        struct adc_channel_cfg config = {
            .gain = ADC_GAIN_1,
            .reference = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME_DEFAULT,
            .channel_id = channel,
        };
        probe_status = adc_channel_setup(adc, &config);
        if (probe_status < 0) {
            return 0;
        }
    }
    while (true) {
        probe_status = adc_read(adc, &sequence);
        if (probe_status == 0) {
            for (int axis = 0; axis < 4; axis++) {
                probe_axes[axis] = samples[axis];
            }
        }
        k_msleep(10);
    }
}
