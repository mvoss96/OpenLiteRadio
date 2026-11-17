#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <soc.h>

/*
 * Hardware test: reports the loop count, ADC readings and reset flags on the
 * console twice per second. No CRSF frames are sent to the module and no flash
 * is written.
 */

/* Shift register for board control lines, as used by the vendor bootloader. */
#define SHIFT_CLOCK_PIN 12
#define SHIFT_LATCH_PIN 13
#define SHIFT_DATA_PIN  14

/*
 * While this output is high the CH9102 bridge is absent from the USB
 * connector, so the connector is expected to reach this MCU instead. All
 * other outputs stay low.
 */
#define SHIFT_USB_TO_MCU 0x40

/* The ADC is first used after this many loops, so a blocking ADC call shows
 * as console output that stops after ten seconds.
 */
#define ADC_START_LOOP 20

static const struct device *const gpiob = DEVICE_DT_GET(DT_NODELABEL(gpiob));

/* Functions from the C sources. */
extern "C" {
int usb_probe_init(void);
void updater_enter(void);
}

#ifdef CONFIG_UPDATER_ENTRY

/* Baud rate a host sets on the console port to request update mode. */
#define UPDATER_REQUEST_BAUD 1200
#endif

/* Shift eight bits out, most significant first, then latch them. */
static void shift_write(uint8_t bits)
{
    gpio_pin_set_raw(gpiob, SHIFT_LATCH_PIN, 0);
    for (int bit = 7; bit >= 0; bit--) {
        gpio_pin_set_raw(gpiob, SHIFT_CLOCK_PIN, 0);
        gpio_pin_set_raw(gpiob, SHIFT_DATA_PIN, (bits >> bit) & 1);
        k_busy_wait(50);
        gpio_pin_set_raw(gpiob, SHIFT_CLOCK_PIN, 1);
        k_busy_wait(50);
    }
    gpio_pin_set_raw(gpiob, SHIFT_LATCH_PIN, 1);
}

/* Configure the four stick channels; return 0 or a negative errno. */
static int adc_init(const struct device *adc)
{
    if (!device_is_ready(adc)) {
        return -ENODEV;
    }
    for (uint8_t channel = 0; channel < 4; channel++) {
        struct adc_channel_cfg config = {
            .gain = ADC_GAIN_1,
            .reference = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME_DEFAULT,
            .channel_id = channel,
        };
        int error = adc_channel_setup(adc, &config);

        if (error < 0) {
            return error;
        }
    }
    return 0;
}

/*
 * Read the four stick channels one at a time; return 0 or a negative errno.
 * Without DMA the STM32F1 ADC driver rejects sequences of several channels.
 */
static int adc_read_axes(const struct device *adc, int16_t samples[4])
{
    for (uint8_t channel = 0; channel < 4; channel++) {
        struct adc_sequence sequence = {
            .channels = BIT(channel),
            .buffer = &samples[channel],
            .buffer_size = sizeof(samples[channel]),
            .resolution = 12,
        };
        int error = adc_read(adc, &sequence);

        if (error < 0) {
            return error;
        }
    }
    return 0;
}

int main(void)
{
    const struct device *adc = DEVICE_DT_GET(DT_NODELABEL(adc1));
    const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(usart1));
    /* Reset flags: bit 29 is set after an independent watchdog reset. */
    uint32_t reset_flags = RCC->CSR;
    int16_t samples[4] = { 0 };
    int adc_status = -EAGAIN;
    uint32_t loops = 0;

#ifdef CONFIG_USBD_HID_SUPPORT
    usb_probe_init();
#endif

    gpio_pin_configure(gpiob, SHIFT_CLOCK_PIN, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpiob, SHIFT_LATCH_PIN, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpiob, SHIFT_DATA_PIN, GPIO_OUTPUT_LOW);
    shift_write(SHIFT_USB_TO_MCU);

    while (true) {
        if (loops == ADC_START_LOOP) {
            adc_status = adc_init(adc);
        }
        if (adc_status == 0) {
            adc_status = adc_read_axes(adc, samples);
        }
        printk("loops=%u adc=%d axes=%d %d %d %d uart=%d csr=%08x\n",
               loops, adc_status, samples[0], samples[1], samples[2], samples[3],
               device_is_ready(uart), reset_flags);
        loops++;
#ifdef CONFIG_UPDATER_ENTRY
        uint32_t baud = 0;

        uart_line_ctrl_get(DEVICE_DT_GET(DT_CHOSEN(zephyr_console)),
                           UART_LINE_CTRL_BAUD_RATE, &baud);
        if (baud == UPDATER_REQUEST_BAUD) {
            updater_enter();
        }
#endif
        k_msleep(500);
    }
}
