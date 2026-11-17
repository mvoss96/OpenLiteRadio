# Controller hardware and module contract

Evidence source: the parent investigation's analysis/CONTROLLER.md, especially
section 10a (SWD readback, 2026-11-16). Earlier sections contain superseded claims.

## Observed

- Geehy APM32F103CBT6; 128 KiB flash, 20 KiB RAM.
- Vendor application vectors at 0x08008000; stock file length 0xE530.
- Stock application SHA-256:
  e0e9f86d009c7f5730d9bff94edff6b6bb1ada61475ca14723ede9f777be75ff.
- Full SWD readback matches the vendor application byte-for-byte.
- Below 0x08008000: preserve bootloader and settings, including channel records
  at 0x08007000 and calibration at 0x08007800. 0x08007466 was erased in the dump;
  this does not prove that no firmware path can ever use it.
- Calibration contains descending endpoints and centre in 12-bit ADC units.
- CDC fields identify AETR physically; the throttle does not self-centre.
- The application works with erased flash above 0x08016530. Earlier claims
  about required compressed initialization data are superseded.
- Vendor USB updater recovered the application after an experimental dumper.

## Bootloader (static analysis of the SWD readback, 2026-11-16)

Read from the bootloader disassembly. Pin functions are interpretations of
that code unless the hardware test section below confirms them.

- Clock setup: HSE on, PLL x9 from HSE, PCLK1 divided by 2, flash latency 2,
  USB prescaler 1.5. This implies an 8 MHz crystal and a 72 MHz system clock.
- The application is started only if `(initial SP & 0x2FFE0000) == 0x20000000`.
- Before the jump (code at 0x08002e5c) the bootloader stops SysTick, executes
  `cpsid i`, loads MSP from 0x08008000 and branches to the reset vector at
  0x08008004. The application therefore starts with PRIMASK set, and VTOR
  still points to the bootloader's vector table. Zephyr on Cortex-M3 masks
  interrupts with BASEPRI only and clears PRIMASK solely when
  `CONFIG_INIT_ARCH_HW_AT_BOOT=y`, which prj.conf sets.
- If PA15 is high before the jump, the bootloader powers down USB, drives PA12
  low for 700 ms and then jumps. Interpretation: PA15 senses USB supply voltage.
- PB15 and PC14 are inputs with pull-up; both low selects update mode,
  otherwise the application is started. Interpretation: PB15 is the SETUP
  button and PC14 the power button, matching the key combination that enters
  update mode.
- The button check is the only decision between update mode and application.
  There is no RAM marker, flash flag or backup register that an application
  could set to request update mode.
- If the initial SP check fails, the jump routine returns and execution
  continues in the update branch at 0x080057dc.
- The update loop never starts the application; the routine that jumps to it
  is called from one place only. A USB command to leave update mode was not
  found. The closing packet of the updater protocol does not cause a restart.
- The update branch polls SysTick for its delays and uses interrupts for USB
  (vector 20) and USART1 (vector 37). The vector table has 43 interrupt entries.
- All RAM addresses found in the code lie below 0x20002000; the initial stack
  pointer is 0x20001f48.
- PC15 is set on a button power-on and cleared when PA15 is high.
  Interpretation: PC15 is the power latch that keeps the radio on from battery.
- PC13 is set high during initialisation. Its function is not established.
- An 8-bit shift register is written on PB12 (clock), PB13 (latch) and PB14
  (data), most significant bit first. The bootloader uses the output bits
  0x10, 0x20, 0x40 and 0x80. It toggles 0x10 and 0x20 in a 40/70 ms sequence
  around USB passthrough. Hypothesis: these two drive the ESP32 module's EN
  and IO0 lines.
- The bootloader contains no literal for the IWDG base address 0x40003000 and
  no watchdog key values. Hypothesis: it does not start the independent
  watchdog. The option bytes (hardware watchdog bit) have not been read.

## Hardware tests, 2026-11-17

All images were loaded with the vendor USB updater; no debugger was attached.
The only instruments were the host's view of the USB devices and the Zephyr
USB console.

Observed:

- A Zephyr v4.5.0 image built with the STM32F103 drivers starts from the
  vendor bootloader on the Geehy APM32F103CBT6. Over 47 seconds it printed 95
  status lines at a steady 500 ms interval.
- RCC_CSR read 0x0c000000 after a USB power-up: power-on and pin reset flags,
  no independent watchdog reset.
- Shift register output 0x40 controls the USB connector. While it is high the
  CH9102 bridge (1A86:55D4) is absent from the host; while it is low the bridge
  is present. This held for PC13 low and PC13 high, with the other outputs at
  0x00, 0x10, 0x20 and 0x80. PC13 had no visible effect.
- With 0x40 held high, the Zephyr USB device stack enumerates on the connector
  as a CDC ACM port, using the 8 MHz HSE / 72 MHz PLL clock.
- Switching 0x40 every 500 ms makes the host report a device whose descriptor
  request failed.
- ADC1 channels 0 to 3 (PA0 to PA3) are the four stick axes. Each channel is
  read separately; the Zephyr STM32F1 ADC driver returns -EINVAL for a
  sequence of several channels unless DMA is used.

| Channel | Pin | Stick movement        | At rest | Deflected  |
|---------|-----|-----------------------|---------|------------|
| 0       | PA0 | left stick up/down    | 678     | up 3445    |
| 1       | PA1 | left stick left/right | 2076    | right 606  |
| 2       | PA2 | right stick up/down   | 2074    | up 480     |
| 3       | PA3 | right stick left/right| 2075    | right 3576 |

Values are raw 12-bit readings. Channel 0 does not return to centre and was
at its lower end at rest, so it is the throttle. Channels 1 and 2 decrease
for right and up. Noise at rest was 1 to 2 counts. Only one direction per
axis was measured.

Update mode entered from the application (`src/updater_entry.c`), observed:

- A RAM word at 0x20004000 keeps its value across a software reset and the
  bootloader's start.
- Branching from the application's first instructions to 0x080057dc, with
  interrupts enabled, r4 = 0 and MSP = 0x20001f40, starts a working updater:
  it enumerates as 0483:5740 about 3.6 s after the request and accepts images.
- A TIM2 handler in RAM, reached through a RAM copy of the bootloader's vector
  table, keeps running during the update. Its reset after a completed download
  brought the new firmware up about 2 s after the last block; its reset after
  30 s without a download returned to the unchanged firmware.
- A software reset sets RCC_CSR to 0x1c000000.
- With a USB host attached the bootloader's USB interrupt fires continuously,
  so it cannot serve as a sign of activity.
- A complete cycle with a 48 KiB image takes about 14 s.

Hypotheses from these tests:

- Output 0x40 selects between the CH9102 bridge and the controller's own USB
  pins. The test shows the bridge leaving and the controller enumerating, not
  the switching circuit itself.
- The USART1 driver reports ready; nothing was transmitted and no CRSF timing
  was measured.

## Common contract with the ELRS target repository

- ESP32 module hardware JSON stays identical for stock and custom controllers.
- Controller USART1 uses PA9 TX / PA10 RX at 400000 baud (8N1 to verify).
- CRSF channel frame: address 0xEE, length 0x18, type 0x16, 16 packed 11-bit
  channels, CRC8 DVB-S2 over type and payload; 26 bytes total.
- CRSF centre 992; nominal endpoints approximately 172..1811. Stock conversion
  is (internal - 1000) * 1.6 + 192.
- Stock parameter client discovers names rather than relying on fixed indices.
- A custom controller needs explicit throttle safety, suitable frame timing,
  receive parsing, and deliberate policy for module power/settings.
- Do not advertise EdgeTX passthrough merely because the target lists `etx`.

## Unverified in this implementation

Operation from battery and the power latch, option-byte watchdog setting,
switch and button pins, LEDs and buzzer, the function of PC13 and of the
shift register outputs other than 0x40, stick readings against the saved
calibration, calibrated voltage monitoring, CRSF output and timing, USB HID
reports, and STM32 drivers other than clock, GPIO, ADC and USB device.
No settings writes are enabled during feasibility work. The largest image
loaded through the stock USB updater and seen running is 48516 bytes; the
stock application is 58672 bytes. Larger images are untested.
