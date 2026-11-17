#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <soc.h>

/*
 * Hardware test: CRSF link to the ELRS module on USART1 (PA9 TX, PA10 RX,
 * 400000 baud). Twice per second it sends a CRSF device ping, if enabled,
 * and prints the bytes received since the last report. Console keys change
 * the control lines whose function is not established:
 *   p  ping on/off          c  toggle PC13
 *   1  toggle shift 0x10    2  toggle shift 0x20    8  toggle shift 0x80
 * No channel frames are sent and no flash is written.
 */

/* Shift register for board control lines, as used by the vendor bootloader. */
#define SHIFT_CLOCK_PIN 12
#define SHIFT_LATCH_PIN 13
#define SHIFT_DATA_PIN  14

/* High: the USB connector reaches this MCU instead of the CH9102 bridge. */
#define SHIFT_USB_TO_MCU 0x40

#define STATUS_LED_PIN BIT(3) /* port B */

/* CRSF addresses and frame types used here. */
#define CRSF_ADDRESS_MODULE  0xEE
#define CRSF_ADDRESS_HANDSET 0xEA
#define CRSF_TYPE_PING       0x28

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

/* CRC-8 with polynomial 0xD5 (DVB-S2), as CRSF uses over type and payload. */
static uint8_t crsf_crc(const uint8_t *data, size_t length)
{
    uint8_t crc = 0;

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0xD5u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* Bytes received from the module since the last report. */
static uint8_t received[192];
static volatile uint16_t received_count;
static volatile uint32_t received_total;

static void module_uart_isr(const struct device *uart, void *)
{
    uart_irq_update(uart);
    while (uart_irq_rx_ready(uart) > 0) {
        uint8_t chunk[16];
        int count = uart_fifo_read(uart, chunk, sizeof(chunk));

        for (int i = 0; i < count; i++) {
            if (received_count < sizeof(received)) {
                received[received_count++] = chunk[i];
            }
        }
        received_total += (count > 0) ? count : 0;
    }
}

/* Ask all CRSF devices to identify themselves; the module answers with frame type 0x29. */
static void send_ping(const struct device *uart)
{
    uint8_t frame[] = { CRSF_ADDRESS_MODULE, 4, CRSF_TYPE_PING, 0x00, CRSF_ADDRESS_HANDSET, 0 };

    frame[5] = crsf_crc(&frame[2], 3);
    for (uint8_t byte : frame) {
        uart_poll_out(uart, byte);
    }
}

int main(void)
{
    const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(usart1));
    const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    uint8_t extra_shift = 0x10 | 0x20;
    bool ping = false;

#ifdef CONFIG_USBD_HID_SUPPORT
    usb_probe_init();
#endif

    gpio_pin_configure(gpiob, SHIFT_CLOCK_PIN, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpiob, SHIFT_LATCH_PIN, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpiob, SHIFT_DATA_PIN, GPIO_OUTPUT_LOW);
    shift_write(SHIFT_USB_TO_MCU | extra_shift);

    /* PB3 is a JTAG pin after reset; keep SWD and release the JTAG pins as GPIO. */
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
    AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
    GPIOB->BRR = STATUS_LED_PIN;
    configure_pin(GPIOB, 3, 0x3u); /* PB3 push-pull output, 50 MHz */
    status_led_write(0, 0, 0x30);

    uart_irq_callback_set(uart, module_uart_isr);
    uart_irq_rx_enable(uart);

    while (true) {
        unsigned char command = 0;

        uart_poll_in(console, &command);
        if (command == 'p') {
            ping = !ping;
        } else if (command == 'c') {
            GPIOC->ODR ^= BIT(13);
        } else if (command == '1') {
            extra_shift ^= 0x10;
        } else if (command == '2') {
            extra_shift ^= 0x20;
        } else if (command == '8') {
            extra_shift ^= 0x80;
        }
        shift_write(SHIFT_USB_TO_MCU | extra_shift);

        if (ping) {
            send_ping(uart);
        }
        k_msleep(450);

        unsigned int key = irq_lock();
        uint16_t count = received_count;
        uint8_t copy[48];
        uint16_t shown = MIN(count, sizeof(copy));

        memcpy(copy, received, shown);
        received_count = 0;
        irq_unlock(key);

        printk("pc13 %u shift %02x ping %u total %u rx %u:",
               (unsigned int)((GPIOC->ODR >> 13) & 1u), extra_shift, ping,
               (unsigned int)received_total, count);
        for (uint16_t i = 0; i < shown; i++) {
            printk(" %02x", copy[i]);
        }
        printk("\n");

#ifdef CONFIG_UPDATER_ENTRY
        uint32_t baud = 0;

        uart_line_ctrl_get(console, UART_LINE_CTRL_BAUD_RATE, &baud);
        if (baud == UPDATER_REQUEST_BAUD) {
            updater_enter();
        }
#endif
    }
}
