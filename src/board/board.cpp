#include "board/board.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/uart.h>
#include <soc.h>
#include <string.h>

namespace board {

namespace {

/* Values for the 4-bit pin configuration fields in GPIOx_CRL and GPIOx_CRH. */
constexpr uint32_t PIN_ANALOG = 0x0;
constexpr uint32_t PIN_INPUT_PULL = 0x8; /* pull direction from the ODR bit */
constexpr uint32_t PIN_OUTPUT = 0x2;
constexpr uint32_t PIN_OUTPUT_OPEN_DRAIN = 0x6;
constexpr uint32_t PIN_ALTERNATE = 0xB; /* alternate function, push-pull, 50 MHz */

/* Port A */
constexpr uint32_t PA_BUTTON_SF = BIT(4);
constexpr uint32_t PA_BUTTON_SE = BIT(5);
constexpr uint32_t PA_KEYS_CLOCK = BIT(6);
constexpr uint32_t PA_KEYS_DATA = BIT(7);
constexpr uint32_t PA_BUTTON_BIND = BIT(8);
constexpr uint32_t PA_USB_SUPPLY = BIT(15);
/* Port B */
constexpr uint32_t PB_SWITCH_SB_END0 = BIT(4);
constexpr uint32_t PB_SWITCH_SB_END2 = BIT(5);
constexpr uint32_t PB_SWITCH_SA = BIT(6);
constexpr uint32_t PB_SWITCH_SC_END0 = BIT(7);
constexpr uint32_t PB_SWITCH_SD = BIT(8);
constexpr uint32_t PB_SWITCH_SC_END2 = BIT(9);
constexpr uint32_t PB_SHIFT_CLOCK = BIT(12);
constexpr uint32_t PB_SHIFT_LATCH = BIT(13);
constexpr uint32_t PB_SHIFT_DATA = BIT(14);
constexpr uint32_t PB_BUTTON_SETUP = BIT(15);
/* Port C */
constexpr uint32_t PC_MODULE_OFF = BIT(13);
constexpr uint32_t PC_BUTTON_POWER = BIT(14);
constexpr uint32_t PC_POWER_LATCH = BIT(15);

/* Shift register outputs. */
constexpr uint8_t SHIFT_BATTERY_LEDS = 0x0f;
/* The stock firmware keeps this output high; its function is not established. */
constexpr uint8_t SHIFT_STOCK_HIGH = 0x10;
constexpr uint8_t SHIFT_USB_TO_CONTROLLER = 0x40;

constexpr uint8_t ADC_CHANNEL_BATTERY = 8; /* PB0 */
constexpr uint8_t KEYS_ADDRESS = 0x1d;

const struct device *const adc = DEVICE_DT_GET(DT_NODELABEL(adc1));
const struct device *const module_uart = DEVICE_DT_GET(DT_NODELABEL(usart1));
uint8_t shift_state = SHIFT_STOCK_HIGH | SHIFT_USB_TO_CONTROLLER;

void configure_pins(GPIO_TypeDef *port, uint32_t pins, uint32_t mode)
{
    for (int pin = 0; pin < 16; pin++) {
        if ((pins & BIT(pin)) == 0u) {
            continue;
        }
        volatile uint32_t *reg = (pin < 8) ? &port->CRL : &port->CRH;
        int shift = 4 * (pin % 8);

        *reg = (*reg & ~(0xFu << shift)) | (mode << shift);
    }
}

void configure_pull_up_inputs(GPIO_TypeDef *port, uint32_t pins)
{
    port->BSRR = pins; /* ODR bit 1 selects the pull-up */
    configure_pins(port, pins, PIN_INPUT_PULL);
}

/* Shift the eight outputs out, most significant bit first, then latch them. */
void shift_write()
{
    GPIOB->BRR = PB_SHIFT_LATCH;
    for (int bit = 7; bit >= 0; bit--) {
        GPIOB->BRR = PB_SHIFT_CLOCK;
        if ((shift_state >> bit) & 1u) {
            GPIOB->BSRR = PB_SHIFT_DATA;
        } else {
            GPIOB->BRR = PB_SHIFT_DATA;
        }
        k_busy_wait(2);
        GPIOB->BSRR = PB_SHIFT_CLOCK;
        k_busy_wait(2);
    }
    GPIOB->BSRR = PB_SHIFT_LATCH;
}

/* Read one ADC channel; 0 on error. Without DMA the driver takes one channel per call. */
uint16_t adc_read_channel(uint8_t channel)
{
    int16_t sample = 0;
    struct adc_sequence sequence = {
        .channels = BIT(channel),
        .buffer = &sample,
        .buffer_size = sizeof(sample),
        .resolution = 12,
    };

    if (adc_read(adc, &sequence) < 0 || sample < 0) {
        return 0;
    }
    return (uint16_t)sample;
}

/* Two-wire bus to the trim key device, driven by software with I2C framing. */
void keys_set(uint32_t pin, bool high)
{
    if (high) {
        GPIOA->BSRR = pin;
    } else {
        GPIOA->BRR = pin;
    }
    k_busy_wait(5);
}

bool keys_write_byte(uint8_t value)
{
    for (int bit = 7; bit >= 0; bit--) {
        keys_set(PA_KEYS_DATA, (value >> bit) & 1);
        keys_set(PA_KEYS_CLOCK, true);
        keys_set(PA_KEYS_CLOCK, false);
    }
    keys_set(PA_KEYS_DATA, true);
    keys_set(PA_KEYS_CLOCK, true);
    bool acknowledged = (GPIOA->IDR & PA_KEYS_DATA) == 0u;

    keys_set(PA_KEYS_CLOCK, false);
    return acknowledged;
}

uint8_t keys_read_byte(bool last)
{
    uint8_t value = 0;

    keys_set(PA_KEYS_DATA, true);
    for (int bit = 0; bit < 8; bit++) {
        keys_set(PA_KEYS_CLOCK, true);
        value = (uint8_t)((value << 1) | ((GPIOA->IDR & PA_KEYS_DATA) ? 1u : 0u));
        keys_set(PA_KEYS_CLOCK, false);
    }
    keys_set(PA_KEYS_DATA, last);
    keys_set(PA_KEYS_CLOCK, true);
    keys_set(PA_KEYS_CLOCK, false);
    keys_set(PA_KEYS_DATA, true);
    return value;
}

/*
 * Read the key device's 16-bit word. The low four bits are always 5; bits 4
 * to 13 are one pressed trim direction each. Returns false if the device did
 * not answer or the word is malformed.
 */
bool keys_read(uint16_t &word)
{
    keys_set(PA_KEYS_DATA, true);
    keys_set(PA_KEYS_CLOCK, true);
    keys_set(PA_KEYS_DATA, false); /* start */
    keys_set(PA_KEYS_CLOCK, false);

    bool present = keys_write_byte((KEYS_ADDRESS << 1) | 1u);
    uint8_t low = 0;
    uint8_t high = 0;

    if (present) {
        low = keys_read_byte(false);
        high = keys_read_byte(true);
    }
    keys_set(PA_KEYS_DATA, false); /* stop */
    keys_set(PA_KEYS_CLOCK, true);
    keys_set(PA_KEYS_DATA, true);

    word = (uint16_t)(low | (high << 8));
    return present && (word & 0x000fu) == 5u;
}

/* Map the key word's bits for one trim button to TrimDirection bits. */
uint8_t trim_bits(uint16_t word, int left, int right, int up, int down, int centre)
{
    uint8_t bits = 0;

    bits |= (word & BIT(up)) ? TRIM_UP : 0;
    bits |= (word & BIT(down)) ? TRIM_DOWN : 0;
    bits |= (word & BIT(left)) ? TRIM_LEFT : 0;
    bits |= (word & BIT(right)) ? TRIM_RIGHT : 0;
    bits |= (word & BIT(centre)) ? TRIM_CENTRE : 0;
    return bits;
}

/* Three-position switch: one input is low at each end, both are high in the centre. */
uint8_t three_positions(uint32_t levels, uint32_t end0, uint32_t end2)
{
    if ((levels & end0) == 0u) {
        return 0;
    }
    return ((levels & end2) == 0u) ? 2 : 1;
}

/*
 * Status LED of WS2812 type on PB3, TIM2 channel 2. The timer runs at
 * 800 kHz (90 counts at 72 MHz); DMA writes one duty value per bit into the
 * compare register on each timer update: 60 counts for a 1, 30 for a 0. Two
 * trailing zeros leave the line low, which latches the colour.
 */
constexpr uint16_t LED_PERIOD = 90;
constexpr uint16_t LED_DUTY_ONE = 60;
constexpr uint16_t LED_DUTY_ZERO = 30;
uint16_t led_duty[26];

void status_led_init()
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    configure_pins(GPIOB, BIT(3), PIN_ALTERNATE);

    TIM2->PSC = 0;
    TIM2->ARR = LED_PERIOD - 1;
    TIM2->CCR2 = 0;
    TIM2->CCMR1 = TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2PE; /* PWM mode 1, preload */
    TIM2->CCER = TIM_CCER_CC2E;
    TIM2->DIER = TIM_DIER_UDE; /* DMA request on each update */
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

/* Tone on PB1, TIM3 channel 4, counting at 1 MHz. */
void buzzer_init()
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    GPIOB->BRR = BIT(1);
    configure_pins(GPIOB, BIT(1), PIN_OUTPUT);

    TIM3->PSC = 71;
    TIM3->CCMR2 = TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4PE; /* PWM mode 1 */
    TIM3->CCER = TIM_CCER_CC4E;
}

/*
 * USART1 to the ELRS module. Received bytes go into a ring buffer; one frame
 * at a time is sent from module_tx.
 */
uint8_t module_rx[256];
volatile size_t module_rx_head;
volatile size_t module_rx_tail;
uint8_t module_tx[64];
volatile size_t module_tx_position;
volatile size_t module_tx_length;

void module_uart_interrupt(const struct device *device, void *)
{
    uart_irq_update(device);

    if (uart_irq_rx_ready(device)) {
        uint8_t bytes[16];
        int count = uart_fifo_read(device, bytes, sizeof(bytes));

        for (int i = 0; i < count; i++) {
            size_t next = (module_rx_head + 1) % sizeof(module_rx);

            /* A full buffer drops the byte. */
            if (next != module_rx_tail) {
                module_rx[module_rx_head] = bytes[i];
                module_rx_head = next;
            }
        }
    }

    if (uart_irq_tx_ready(device)) {
        if (module_tx_position < module_tx_length) {
            module_tx_position += (size_t)uart_fifo_fill(
                device, &module_tx[module_tx_position],
                (int)(module_tx_length - module_tx_position));
        } else {
            uart_irq_tx_disable(device);
        }
    }
}

/*
 * Settings records in Zephyr's NVS, in the partition at the top of the
 * application flash. NVS appends each new version of a record and erases a
 * page only when it is full.
 */
struct nvs_fs storage;
bool storage_ready;

void storage_init()
{
    struct flash_pages_info page;

    storage.flash_device = FIXED_PARTITION_DEVICE(storage_partition);
    storage.offset = FIXED_PARTITION_OFFSET(storage_partition);
    if (!device_is_ready(storage.flash_device) ||
        flash_get_page_info_by_offs(storage.flash_device, storage.offset, &page) != 0) {
        return;
    }
    storage.sector_size = (uint16_t)page.size;
    storage.sector_count = (uint16_t)(FIXED_PARTITION_SIZE(storage_partition) / page.size);

    /* Pages holding something other than NVS data are erased once. */
    if (nvs_mount(&storage) != 0) {
        if (flash_erase(storage.flash_device, storage.offset,
                        FIXED_PARTITION_SIZE(storage_partition)) != 0 ||
            nvs_mount(&storage) != 0) {
            return;
        }
    }
    storage_ready = true;
}

} // namespace

void init()
{
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN |
                    RCC_APB2ENR_IOPCEN;
    /*
     * PB3 and PB4 are JTAG pins after reset: keep SWD, release JTAG, and move
     * TIM2 channel 2 to PB3 (partial remap 1).
     */
    AFIO->MAPR = (AFIO->MAPR & ~(AFIO_MAPR_SWJ_CFG | AFIO_MAPR_TIM2_REMAP)) |
                 AFIO_MAPR_SWJ_CFG_JTAGDISABLE | AFIO_MAPR_TIM2_REMAP_PARTIALREMAP1;

    configure_pull_up_inputs(GPIOA, PA_BUTTON_SF | PA_BUTTON_SE | PA_BUTTON_BIND);
    configure_pull_up_inputs(GPIOB, PB_SWITCH_SA | PB_SWITCH_SB_END0 | PB_SWITCH_SB_END2 |
                                        PB_SWITCH_SC_END0 | PB_SWITCH_SC_END2 | PB_SWITCH_SD |
                                        PB_BUTTON_SETUP);
    configure_pull_up_inputs(GPIOC, PC_BUTTON_POWER);

    GPIOA->BSRR = PA_KEYS_CLOCK | PA_KEYS_DATA;
    configure_pins(GPIOA, PA_KEYS_CLOCK | PA_KEYS_DATA, PIN_OUTPUT_OPEN_DRAIN);

    GPIOB->BSRR = PB_SHIFT_CLOCK | PB_SHIFT_LATCH;
    configure_pins(GPIOB, PB_SHIFT_CLOCK | PB_SHIFT_LATCH | PB_SHIFT_DATA, PIN_OUTPUT);
    shift_write();

    /* The module stays off and the power latch keeps its level until told otherwise. */
    GPIOC->BSRR = PC_MODULE_OFF;
    configure_pins(GPIOC, PC_MODULE_OFF | PC_POWER_LATCH, PIN_OUTPUT);

    configure_pins(GPIOB, BIT(0), PIN_ANALOG);
    static const uint8_t adc_channels[] = { 0, 1, 2, 3, ADC_CHANNEL_BATTERY };

    for (uint8_t channel : adc_channels) {
        struct adc_channel_cfg config = {
            .gain = ADC_GAIN_1,
            .reference = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME_DEFAULT,
            .channel_id = channel,
        };

        adc_channel_setup(adc, &config);
    }

    status_led_init();
    buzzer_init();

    uart_irq_callback_user_data_set(module_uart, module_uart_interrupt, nullptr);
    uart_irq_rx_enable(module_uart);

    storage_init();
}

void read_controls(Inputs &inputs)
{
    for (uint8_t axis = 0; axis < 4; axis++) {
        inputs.sticks[axis] = adc_read_channel(axis);
    }

    uint32_t a = GPIOA->IDR;
    uint32_t b = GPIOB->IDR;
    uint32_t c = GPIOC->IDR;

    inputs.sa = (b & PB_SWITCH_SA) ? 1 : 0;
    inputs.sd = (b & PB_SWITCH_SD) ? 1 : 0;
    inputs.sb = three_positions(b, PB_SWITCH_SB_END0, PB_SWITCH_SB_END2);
    inputs.sc = three_positions(b, PB_SWITCH_SC_END0, PB_SWITCH_SC_END2);
    inputs.se = (a & PA_BUTTON_SE) == 0u;
    inputs.sf = (a & PA_BUTTON_SF) == 0u;
    inputs.bind = (a & PA_BUTTON_BIND) == 0u;
    inputs.setup = (b & PB_BUTTON_SETUP) == 0u;
    inputs.power = (c & PC_BUTTON_POWER) == 0u;
}

void read_trims(Inputs &inputs)
{
    uint16_t word = 0;

    inputs.trims_valid = keys_read(word);
    inputs.trim_left = inputs.trims_valid ? trim_bits(word, 4, 5, 6, 7, 8) : 0;
    inputs.trim_right = inputs.trims_valid ? trim_bits(word, 9, 10, 11, 12, 13) : 0;
}

bool storage_read(uint16_t id, void *data, size_t size)
{
    return storage_ready && nvs_read(&storage, id, data, size) == (ssize_t)size;
}

bool storage_write(uint16_t id, const void *data, size_t size)
{
    /* nvs_write returns 0 if the record already holds this data. */
    return storage_ready && nvs_write(&storage, id, data, size) >= 0;
}

bool storage_delete(uint16_t id)
{
    return storage_ready && nvs_delete(&storage, id) == 0;
}

bool module_send(const uint8_t *data, size_t length)
{
    if (module_tx_position < module_tx_length || length > sizeof(module_tx)) {
        return false;
    }
    /* Keep the interrupt away from the buffer while it changes. */
    uart_irq_tx_disable(module_uart);
    memcpy(module_tx, data, length);
    module_tx_position = 0;
    module_tx_length = length;
    uart_irq_tx_enable(module_uart);
    return true;
}

size_t module_receive(uint8_t *data, size_t capacity)
{
    size_t count = 0;

    /* Only this function moves the tail, only the interrupt moves the head. */
    while (count < capacity && module_rx_tail != module_rx_head) {
        data[count++] = module_rx[module_rx_tail];
        module_rx_tail = (module_rx_tail + 1) % sizeof(module_rx);
    }
    return count;
}

uint16_t battery_raw()
{
    return adc_read_channel(ADC_CHANNEL_BATTERY);
}

bool usb_supply_present()
{
    return (GPIOA->IDR & PA_USB_SUPPLY) != 0u;
}

void set_power_latch(bool on)
{
    if (on) {
        GPIOC->BSRR = PC_POWER_LATCH;
    } else {
        GPIOC->BRR = PC_POWER_LATCH;
    }
}

void set_module_enabled(bool enabled)
{
    if (enabled) {
        GPIOC->BRR = PC_MODULE_OFF;
    } else {
        GPIOC->BSRR = PC_MODULE_OFF;
    }
}

void set_usb_to_controller(bool controller)
{
    shift_state = controller ? (shift_state | SHIFT_USB_TO_CONTROLLER)
                             : (shift_state & ~SHIFT_USB_TO_CONTROLLER);
    shift_write();
}

void set_battery_leds(uint8_t mask)
{
    /* Output 0x08 is the leftmost LED, 0x01 the rightmost. */
    uint8_t outputs = (uint8_t)(((mask & 1u) << 3) | ((mask & 2u) << 1) | ((mask & 4u) >> 1) |
                                ((mask & 8u) >> 3));

    shift_state = (uint8_t)((shift_state & ~SHIFT_BATTERY_LEDS) | outputs);
    shift_write();
}

void set_status_led(uint8_t red, uint8_t green, uint8_t blue)
{
    /* Wait for a transfer that is still running; one takes 33 us. */
    while ((DMA1_Channel2->CCR & DMA_CCR_EN) != 0u && DMA1_Channel2->CNDTR != 0u) {
    }

    uint32_t bits = ((uint32_t)green << 16) | ((uint32_t)red << 8) | blue;

    for (int i = 0; i < 24; i++) {
        led_duty[i] = (bits & BIT(23 - i)) ? LED_DUTY_ONE : LED_DUTY_ZERO;
    }
    led_duty[24] = 0;
    led_duty[25] = 0;

    /* TIM2 update requests are served by DMA1 channel 2. */
    DMA1_Channel2->CCR = 0;
    DMA1_Channel2->CPAR = (uint32_t)&TIM2->CCR2;
    DMA1_Channel2->CMAR = (uint32_t)led_duty;
    DMA1_Channel2->CNDTR = ARRAY_SIZE(led_duty);
    DMA1_Channel2->CCR = DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0 |
                         DMA_CCR_EN;
}

void set_buzzer(uint32_t frequency)
{
    if (frequency == 0) {
        TIM3->CR1 = 0;
        GPIOB->BRR = BIT(1);
        configure_pins(GPIOB, BIT(1), PIN_OUTPUT);
        return;
    }

    uint32_t period = 1000000u / frequency;

    TIM3->ARR = period - 1;
    TIM3->CCR4 = period / 2;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    configure_pins(GPIOB, BIT(1), PIN_ALTERNATE);
}

} // namespace board
