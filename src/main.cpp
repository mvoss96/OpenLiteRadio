#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <soc.h>

/*
 * Hardware test: battery operation. On battery the power latch PC15 is set
 * at start and released after 60 s or after a 2 s press of the power button.
 * ADC channel 8 (PB0) is sampled twice per second, with shift register
 * output 0x10 high and low, and kept in RAM together with the USB supply,
 * power button and PB2 levels. The character 'd' on the console prints the
 * stored samples. No CRSF frames are sent and no flash is written.
 *
 * Status LED: green on battery, blue on USB power, red before power-off.
 */

/* Shift register for board control lines, as used by the vendor bootloader. */
#define SHIFT_CLOCK_PIN 12
#define SHIFT_LATCH_PIN 13
#define SHIFT_DATA_PIN  14

/* High: the USB connector reaches this MCU instead of the CH9102 bridge. */
#define SHIFT_USB_TO_MCU 0x40
/*
 * The stock firmware keeps this output high and pulls it low for 50 ms
 * before it measures PB0 while USB power is present.
 */
#define SHIFT_MEASURE 0x10

#define USB_SUPPLY   BIT(15) /* port A, high while USB power is present */
#define POWER_BUTTON BIT(14) /* port C, low while pressed */
#define POWER_LATCH  BIT(15) /* port C, high keeps the radio on */
#define PB2_INPUT    BIT(2)  /* port B, purpose unknown; stock uses a pull-down */
#define STATUS_LED_PIN BIT(3) /* port B */

#define BATTERY_ADC_CHANNEL 8 /* PB0 */
#define AUTO_OFF_SAMPLES    120 /* 60 s on battery */
#define POWER_OFF_SAMPLES   4   /* power button held for 2 s */

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

/* Busy-wait for a fixed number of CPU cycles (13.9 ns each at 72 MHz). */
#define DELAY_CYCLES(count) __asm__ volatile(".rept " #count "\n nop\n .endr")

/*
 * Send one colour to the WS2812-type status LED: 24 bits, green first, most
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

/* Read the battery ADC channel; return the sample or a negative errno. */
static int battery_read(const struct device *adc)
{
    int16_t sample = 0;
    struct adc_sequence sequence = {
        .channels = BIT(BATTERY_ADC_CHANNEL),
        .buffer = &sample,
        .buffer_size = sizeof(sample),
        .resolution = 12,
    };
    int error = adc_read(adc, &sequence);

    return (error < 0) ? error : sample;
}

struct battery_sample {
    uint16_t number;
    int16_t measure_high; /* ADC value with SHIFT_MEASURE high */
    int16_t measure_low;  /* ADC value 50 ms after SHIFT_MEASURE went low */
    uint8_t usb_supply;
    uint8_t power_pressed;
    uint8_t pb2;
    uint8_t latch;
};

static battery_sample history[120];

static void print_sample(const battery_sample &sample)
{
    printk("n %u high %d low %d usb %u power %u pb2 %u latch %u\n", sample.number,
           sample.measure_high, sample.measure_low, sample.usb_supply,
           sample.power_pressed, sample.pb2, sample.latch);
}

int main(void)
{
    const struct device *adc = DEVICE_DT_GET(DT_NODELABEL(adc1));
    const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    uint16_t samples = 0;
    uint16_t battery_samples = 0;
    uint16_t pressed_samples = 0;
    uint8_t extra_shift = 0;

    /* Keep the radio on when it was started from battery with the power button. */
    bool started_on_battery = (GPIOA->IDR & USB_SUPPLY) == 0u;

    if (started_on_battery) {
        GPIOC->BSRR = POWER_LATCH;
    }

#ifdef CONFIG_USBD_HID_SUPPORT
    usb_probe_init();
#endif

    gpio_pin_configure(gpiob, SHIFT_CLOCK_PIN, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpiob, SHIFT_LATCH_PIN, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpiob, SHIFT_DATA_PIN, GPIO_OUTPUT_LOW);
    shift_write(SHIFT_USB_TO_MCU | SHIFT_MEASURE);

    /* PB3 is a JTAG pin after reset; keep SWD and release the JTAG pins as GPIO. */
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
    AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
    GPIOB->BRR = STATUS_LED_PIN | PB2_INPUT; /* PB2: ODR bit 0 selects the pull-down */
    configure_pin(GPIOB, 3, 0x3u);           /* PB3 push-pull output, 50 MHz */
    configure_pin(GPIOB, 2, 0x8u);           /* PB2 input with pull-down */
    configure_pin(GPIOB, 0, 0x0u);           /* PB0 analog */

    struct adc_channel_cfg channel = {
        .gain = ADC_GAIN_1,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id = BATTERY_ADC_CHANNEL,
    };
    int adc_status = adc_channel_setup(adc, &channel);

    printk("battery test, started on %s, adc setup %d\n",
           started_on_battery ? "battery" : "USB", adc_status);

    while (true) {
        battery_sample &sample = history[samples % ARRAY_SIZE(history)];

        sample.number = samples;
        sample.measure_high = battery_read(adc);
        shift_write(SHIFT_USB_TO_MCU | extra_shift);
        k_msleep(50);
        sample.measure_low = battery_read(adc);
        shift_write(SHIFT_USB_TO_MCU | SHIFT_MEASURE | extra_shift);
        sample.usb_supply = (GPIOA->IDR & USB_SUPPLY) != 0u;
        sample.power_pressed = (GPIOC->IDR & POWER_BUTTON) == 0u;
        sample.pb2 = (GPIOB->IDR & PB2_INPUT) != 0u;
        sample.latch = (GPIOC->ODR & POWER_LATCH) != 0u;
        print_sample(sample);
        samples++;

        battery_samples = sample.usb_supply ? 0 : battery_samples + 1;
        pressed_samples = sample.power_pressed ? pressed_samples + 1 : 0;
        if (battery_samples >= AUTO_OFF_SAMPLES || pressed_samples >= POWER_OFF_SAMPLES) {
            status_led_write(0x30, 0, 0);
            k_msleep(1000);
            GPIOC->BRR = POWER_LATCH;
            /* On USB power the radio keeps running; start counting again. */
            battery_samples = 0;
            pressed_samples = 0;
        } else if (sample.usb_supply) {
            status_led_write(0, 0, 0x30);
        } else {
            status_led_write(0, 0x30, 0);
        }

        unsigned char command = 0;

        uart_poll_in(console, &command);
        /* Experiment keys: toggle PC13, or one extra shift register output. */
        if (command == 'c') {
            GPIOC->ODR ^= BIT(13);
        } else if (command == '2') {
            extra_shift ^= 0x20;
        } else if (command == '8') {
            extra_shift ^= 0x80;
        }
        if (command == 'c' || command == '2' || command == '8') {
            shift_write(SHIFT_USB_TO_MCU | SHIFT_MEASURE | extra_shift);
            printk("pc13 %u extra shift %02x\n", (unsigned int)((GPIOC->ODR >> 13) & 1u),
                   extra_shift);
        }
        if (command == 'd') {
            for (const battery_sample &stored : history) {
                print_sample(stored);
            }
            printk("end of history\n");
        }
#ifdef CONFIG_UPDATER_ENTRY
        uint32_t baud = 0;

        uart_line_ctrl_get(console, UART_LINE_CTRL_BAUD_RATE, &baud);
        if (baud == UPDATER_REQUEST_BAUD) {
            updater_enter();
        }
#endif
        k_msleep(450);
    }
}
