/*
 * Entry into the vendor bootloader's USB update mode without pressing buttons,
 * and automatic return from it after a completed download.
 *
 * The bootloader offers no software trigger. updater_enter() therefore stores
 * a marker in RAM and resets. The bootloader starts this image again; its
 * first code finds the marker and continues in the bootloader's update branch.
 *
 * The bootloader never leaves update mode by itself. Before handing over, a
 * copy of its vector table with an added timer handler is installed in RAM.
 * That handler resets the MCU once the bootloader has stored the size of a
 * received image, or after 30 seconds in which no download has started.
 *
 * The addresses below belong to the bootloader shipped with LiteRadio 4
 * firmware 1.1.3 and are not valid for any other bootloader build.
 */

#include <zephyr/kernel.h>
#include <soc.h>

/* Bootloader: update branch of its main function, after the button check. */
#define BOOT_UPDATE_BRANCH 0x080057ddu
/* Bootloader: stack pointer inside its main function. */
#define BOOT_MAIN_STACK    0x20001f40u
/* Bootloader: variable receiving the size of a completed download. */
#define BOOT_DOWNLOAD_SIZE 0x2000002cu
#define BOOT_VECTORS       0x08000000u
#define VECTOR_COUNT       (16 + 43)

/* RAM above everything the bootloader uses (it ends below 0x20002000). */
#define MARKER         (*(volatile uint32_t *)0x20004000u)
#define MARKER_VALUE   0xB007C0DEu
#define RAM_VECTORS    0x20004800u
#define RAM_HANDLER    0x20004A00u
/* Timer interrupts since update mode was entered; negative once a download started. */
#define IDLE_TICKS     (*(volatile uint32_t *)0x200049F8u)
#define HOOK_STACK_TOP 0x20005000u

extern const uint8_t updater_handler_start[];
extern const uint8_t updater_handler_end[];

/*
 * TIM2 handler, copied to RAM because the update erases this image.
 * Position independent: the code only uses absolute addresses from its own
 * literal pool. The timer fires every 100 ms; 300 ticks are 30 s. An erased
 * first application word means a download has started.
 */
__asm__(".pushsection .text.updater_handler, \"ax\"\n"
        ".balign 4\n"
        ".global updater_handler_start\n"
        ".global updater_handler_end\n"
        "updater_handler_start:\n"
        "    mov   r0, #0x40000000\n" /* TIM2 */
        "    movs  r1, #0\n"
        "    strh  r1, [r0, #0x10]\n" /* clear the update flag in SR */
        "    ldr   r2, =0x200049f8\n" /* IDLE_TICKS */
        "    ldr   r0, =0x2000002c\n" /* BOOT_DOWNLOAD_SIZE */
        "    ldr   r1, [r0]\n"
        "    cmp   r1, #0\n"
        "    ble   4f\n"
        "    ldr   r1, [r2]\n"        /* download complete: reset 0.4 s later, */
        "    cmp   r1, #296\n"        /* so that the last reply reaches the host */
        "    bge   3f\n"
        "    movw  r1, #296\n"
        "    str   r1, [r2]\n"
        "    b     3f\n"
        "4:  ldr   r0, =0x08008000\n" /* first word of the application */
        "    ldr   r1, [r0]\n"
        "    adds  r1, #1\n"          /* zero if that word is erased */
        "    bne   3f\n"
        "    mov   r1, #0x80000000\n" /* negative count: no timeout any more */
        "    str   r1, [r2]\n"
        "3:  ldr   r1, [r2]\n"
        "    adds  r1, #1\n"
        "    str   r1, [r2]\n"
        "    cmp   r1, #300\n"
        "    ble   1f\n"
        "    ldr   r0, =0xE000ED0C\n" /* SCB AIRCR */
        "    ldr   r1, =0x05FA0004\n" /* SYSRESETREQ */
        "    str   r1, [r0]\n"
        "1:  bx    lr\n"
        "    .ltorg\n"
        "updater_handler_end:\n"
        ".popsection\n");

/* Store the marker and reset; the next start continues in update mode. */
void updater_enter(void)
{
    irq_lock();
    MARKER = MARKER_VALUE;
    NVIC_SystemReset();
}

/*
 * Continue in the bootloader's main function. That function keeps a loop
 * variable in r4, initially 0, and never returns. The bootloader masked
 * interrupts before it started this image.
 */
__attribute__((naked, noreturn)) static void jump_to_updater(void)
{
    __asm__("ldr	r0, =0x20001f40\n" /* BOOT_MAIN_STACK */
        "msr	msp, r0\n"
        "movs	r4, #0\n"
        "cpsie	i\n"
        "ldr	r0, =0x080057dd\n" /* BOOT_UPDATE_BRANCH */
        "bx	r0\n"
        ".ltorg\n");
}

/*
 * Install the RAM vector table and the timer, then enter the bootloader.
 * Runs before RAM is initialised, so it must not use global variables.
 */
__attribute__((noreturn, used)) void updater_prepare(void)
{
    uint32_t *vectors = (uint32_t *)RAM_VECTORS;
    const uint32_t *boot_vectors = (const uint32_t *)BOOT_VECTORS;
    uint8_t *handler = (uint8_t *)RAM_HANDLER;
    size_t handler_size = updater_handler_end - updater_handler_start;

    for (size_t i = 0; i < VECTOR_COUNT; i++) {
        vectors[i] = boot_vectors[i];
    }
    for (size_t i = 0; i < handler_size; i++) {
        handler[i] = updater_handler_start[i];
    }
    IDLE_TICKS = 0u;
    vectors[16 + TIM2_IRQn] = RAM_HANDLER | 1u; /* bit 0 marks Thumb code */
    SCB->VTOR = RAM_VECTORS;

    /* TIM2 runs from 72 MHz: 10 kHz after the prescaler, one interrupt per 100 ms. */
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    TIM2->PSC = 7199u;
    TIM2->ARR = 999u;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0u;
    TIM2->DIER = TIM_DIER_UIE;
    TIM2->CR1 = TIM_CR1_CEN;
    NVIC->ISER[0] = BIT(TIM2_IRQn);

    jump_to_updater();
}

/*
 * Called first in the reset code. Uses no stack until the marker is found,
 * because the initial stack overlaps RAM the bootloader still needs.
 */
__attribute__((naked)) void soc_early_reset_hook(void)
{
    __asm__("ldr	r0, =0x20004000\n" /* MARKER */
        "ldr	r1, [r0]\n"
        "ldr	r2, =0xB007C0DE\n" /* MARKER_VALUE */
        "cmp	r1, r2\n"
        "bne	1f\n"
        "movs	r1, #0\n"
        "str	r1, [r0]\n"
        "ldr	r0, =0x20005000\n" /* HOOK_STACK_TOP */
        "msr	msp, r0\n"
        "b	updater_prepare\n"
        "1:	bx	lr\n"
        ".ltorg\n");
}
