#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <soc.h>

/*
 * Hardware test: output probe for the status LED. Each press of button SE
 * (PA5) switches to the next step; a host can step with 'n' on the console.
 * The status LED is driven as a WS2812-type LED on PB3, the pin the stock
 * firmware uses as TIM2 channel 2. No CRSF frames are sent and no flash is
 * written.
 */

/* Shift register for board control lines, as used by the vendor bootloader. */
#define SHIFT_CLOCK_PIN 12
#define SHIFT_LATCH_PIN 13
#define SHIFT_DATA_PIN  14

/*
 * While this output is high the CH9102 bridge is absent from the USB
 * connector and the connector reaches this MCU instead.
 */
#define SHIFT_USB_TO_MCU 0x40

/* GPIO configuration values for the CRL/CRH registers. */
#define BUTTON_SE BIT(5) /* port A, low while pressed */

static const struct device *const gpiob = DEVICE_DT_GET(DT_NODELABEL(gpiob));

/* Functions from the C sources. */
extern "C" {
int usb_probe_init(void);
void updater_enter(void);
}

/* Baud rate a host sets on the console port to request update mode. */
#define UPDATER_REQUEST_BAUD 1200

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

/* Write a 4-bit configuration value for one pin of a port. */
static void configure_pin(GPIO_TypeDef *port, int pin, uint32_t nibble)
{
    volatile uint32_t *reg = (pin < 8) ? &port->CRL : &port->CRH;
    int shift = 4 * (pin % 8);

    *reg = (*reg & ~(0xFu << shift)) | (nibble << shift);
}

#define STATUS_LED_PIN BIT(3) /* port B */

/* Busy-wait for a fixed number of CPU cycles (13.9 ns each at 72 MHz). */
#define DELAY_CYCLES(count) __asm__ volatile(".rept " #count "\n nop\n .endr")

/*
 * Send one colour to a WS2812-type LED: 24 bits, green first, most
 * significant bit first. A 1 is about 0.7 us high, a 0 about 0.3 us high,
 * each bit lasts about 1.25 us. Interrupts are masked for the 30 us burst.
 */
static void status_led_write(uint8_t red, uint8_t green, uint8_t blue)
{
    uint32_t bits = ((uint32_t)green << 16) | ((uint32_t)red << 8) | blue;
    unsigned int key = irq_lock();

    for (int bit = 23; bit >= 0; bit--) {
        if ((bits >> bit) & 1u) {
            GPIOB->BSRR = STATUS_LED_PIN;
            DELAY_CYCLES(44);
            GPIOB->BRR = STATUS_LED_PIN;
            DELAY_CYCLES(30);
        } else {
            GPIOB->BSRR = STATUS_LED_PIN;
            DELAY_CYCLES(16);
            GPIOB->BRR = STATUS_LED_PIN;
            DELAY_CYCLES(58);
        }
    }
    irq_unlock(key);
    k_busy_wait(300); /* latch time between two colours */
}

struct probe_step {
    const char *name;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t shift_bits; /* shift register outputs in addition to SHIFT_USB_TO_MCU */
};

static const probe_step steps[] = {
    { "all off", 0, 0, 0, 0x00 },
    { "status red", 0x30, 0, 0, 0x00 },
    { "status green", 0, 0x30, 0, 0x00 },
    { "status blue", 0, 0, 0x30, 0x00 },
    { "status white", 0x20, 0x20, 0x20, 0x00 },
    { "battery LEDs 1 to 4", 0, 0, 0, 0x0f },
};

/* Apply one step. */
static void apply_step(const probe_step &step)
{
    shift_write(SHIFT_USB_TO_MCU | step.shift_bits);
    status_led_write(step.red, step.green, step.blue);
}

/* Read one ADC channel; return the sample or a negative errno. */
static int adc_read_channel(const struct device *adc, uint8_t channel)
{
    int16_t sample = 0;
    struct adc_channel_cfg config = {
        .gain = ADC_GAIN_1,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id = channel,
    };
    struct adc_sequence sequence = {
        .channels = BIT(channel),
        .buffer = &sample,
        .buffer_size = sizeof(sample),
        .resolution = 12,
    };
    int error = adc_channel_setup(adc, &config);

    if (error == 0) {
        error = adc_read(adc, &sequence);
    }
    return (error < 0) ? error : sample;
}

int main(void)
{
    const struct device *adc = DEVICE_DT_GET(DT_NODELABEL(adc1));
    size_t step = 0;
    bool was_pressed = false;
    uint32_t loops = 0;

#ifdef CONFIG_USBD_HID_SUPPORT
    usb_probe_init();
#endif

    gpio_pin_configure(gpiob, SHIFT_CLOCK_PIN, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpiob, SHIFT_LATCH_PIN, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpiob, SHIFT_DATA_PIN, GPIO_OUTPUT_LOW);

    /* PB3 is a JTAG pin after reset; keep SWD and release the JTAG pins as GPIO. */
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
    AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;

    configure_pin(GPIOB, 0, 0x0u); /* PB0 analog */
    GPIOB->BRR = STATUS_LED_PIN;
    configure_pin(GPIOB, 3, 0x3u); /* PB3 push-pull output, 50 MHz */
    GPIOA->BSRR = BUTTON_SE;       /* ODR bit 1 selects the pull-up */
    configure_pin(GPIOA, 5, 0x8u); /* PA5 input with pull-up */
    k_msleep(10);
    apply_step(steps[step]);

    while (true) {
        bool pressed = (GPIOA->IDR & BUTTON_SE) == 0u;
        unsigned char command = 0;

        /* A host can step with the character 'n' on the console as well. */
        uart_poll_in(DEVICE_DT_GET(DT_CHOSEN(zephyr_console)), &command);
        if ((pressed && !was_pressed) || command == 'n') {
            step = (step + 1) % ARRAY_SIZE(steps);
            apply_step(steps[step]);
            printk("step %u: %s\n", (unsigned int)step, steps[step].name);
        }
        was_pressed = pressed;

        if (loops % 25 == 0) {
            printk("at %u: %s, adc8 %d, A %04x B %04x C %04x\n", (unsigned int)step,
                   steps[step].name, adc_read_channel(adc, 8), (unsigned int)GPIOA->IDR,
                   (unsigned int)GPIOB->IDR, (unsigned int)GPIOC->IDR);
        }
        loops++;
#ifdef CONFIG_UPDATER_ENTRY
        uint32_t baud = 0;

        uart_line_ctrl_get(DEVICE_DT_GET(DT_CHOSEN(zephyr_console)),
                           UART_LINE_CTRL_BAUD_RATE, &baud);
        if (baud == UPDATER_REQUEST_BAUD) {
            updater_enter();
        }
#endif
        k_msleep(20);
    }
}
