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
- PC13 is set high during initialisation. See the CRSF test below: the
  module only answers while PC13 is low.
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

Stick ranges, observed 2026-11-18 on USB supply with a 25 s recording of the
raw readings at 25 samples per second while both sticks were moved to all
corners and along all edges:

| Channel | Smallest | Largest | Released |
|---------|----------|---------|----------|
| 0       | 544      | 3577    | 671 to 678 at the lower end |
| 1       | 616      | 3586    | 2078 to 2081 |
| 2       | 464      | 3669    | 2075 to 2080 |
| 3       | 630      | 3594    | 2083 to 2085 |

- The throttle (channel 0) read 547 to 590 while it was held against its
  lower stop and 671 to 678 once released there, before and after the run.
- The calibration stored at 0x08007800 does not describe these readings.
  Its four centres are 1993 to 2025, 55 to 90 counts below the released
  readings of channels 1 to 3. No assignment of the four records to the four
  channels, with or without the 25 or 40 counts the stock firmware moves the
  ends inward before storing, brings all ends within 50 counts.
- With that calibration applied record by record, the released sticks give
  1534, 1477 and 1481 instead of 1500, and the released throttle 1065.

Stock ADC setup (code at 0x0800d1c0): ADC1 converts channels 0, 1, 2, 3, 4,
5 and 8 in this order as ranks 1 to 7, sampling time code 2, results by DMA.
The axis number of the stock firmware is the rank, so axis 0 to 3 are ADC
channels 0 to 3.

Hypotheses: the stored calibration was taken under conditions that shift
the readings (stock scan mode with a short sampling time, another supply),
or the gimbals have moved since. Which one is not established.

Settings storage, observed 2026-11-18 with firmware 0.3.0 (59340 bytes, the
largest image loaded so far):

- Zephyr's STM32F1 flash driver writes the Geehy chip: a calibration record
  written through NVS into the pages at 0x0801F000 was read back unchanged
  after a reset.
- The record was also unchanged after the same image had been loaded again
  with the vendor USB updater. The updater therefore left those pages alone
  for an image of this size.

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

Controls, observed with a pin scanner and matching the stock firmware's code
(switch function at 0x0800a1c4, key read at 0x080120e4):

| Control | Pin | Levels |
|---------|-----|--------|
| SA (toggle) | PB6 | one signal, changes with the switch |
| SB (three positions) | PB4, PB5 | both high in the centre, one low at each end |
| SC (three positions) | PB7, PB9 | both high in the centre, one low at each end |
| SD (toggle) | PB8 | one signal, changes with the switch |
| SE (button) | PA5 | low while pressed, needs the internal pull-up |
| SF (button) | PA4 | low while pressed, needs the internal pull-up |
| BIND | PA8 | low while pressed, needs the internal pull-up |
| SETUP | PB15 | low while pressed, needs the internal pull-up |
| Power | PC14 | low while pressed, pulled up externally |

PA8, PB10, PB11 and PB15 follow the internal pulls when idle. PB2 reads low,
PA15 high on USB power. The scanner read PA4, PA5, PB4 to PB9 and PC14 as
high 300 us after switching from pull-up to pull-down, which does not show
an external pull-up: PA5 as a plain floating input stayed low after one
press, and as an analog input it took tens of seconds to drift back up.
Configure all switch and button inputs with the internal pull-up.

The two five-way trim buttons are read from a key device on a two-wire bus
driven by software: PA6 clock, PA7 data, I2C framing, device address 0x1d, the
only address that answers. A read without a register address returns two
bytes, 0x05 0x00 when idle. Taken as a little-endian word, the low four bits
are always 5 and one bit is set per pressed direction:

| Trim button | up | down | left | right | centre |
|-------------|----|------|------|-------|--------|
| left        | 6  | 7    | 4    | 5     | 8 (code only) |
| right       | 11 | 12   | 9    | 10    | 13 (code only) |

The centre bits come from the stock code; no centre press was recorded. The
type of the key device is not known.

Outputs, observed with an output probe:

| Output | Effect |
|--------|--------|
| Shift register 0x08, 0x04, 0x02, 0x01 | white battery LEDs 1, 2, 3, 4 from the left, lit while high |
| PB1, square wave of 2.7 kHz | buzzer sounds |
| PB3, WS2812 bit stream | status LED shows the colour; byte order green, red, blue |
| PB3, PB10 or PB11 held high | nothing visible |
| Shift register 0x80, 0x20, 0x10 | nothing visible |

The status LED accepted bit-banged data with about 0.3 us high for a 0,
0.7 us high for a 1 and 1.25 us per bit. The stock firmware configures PB1
as TIM3 channel 4 and PB3 as TIM2 channel 2 with the TIM2 partial remap
(function at 0x0800c49c), and drives the battery LEDs through shift register
mask 0x0f.

Battery operation, observed with a test that stores samples in RAM while USB
is unplugged:

- PC15 is the power latch. Started with the power button and no USB supply,
  the test set PC15 high and the radio stayed on for 18 s after the button
  was released, until USB was plugged in again. In a second run the test
  set PC15 low after the power button had been held for 2 s, and the radio
  switched off once the button was released.
- PA15 reads low without USB supply and high with it.
- ADC channel 8 (PB0) read 4067 on battery, 4072 on USB with shift register
  output 0x10 low and 4078 with 0x10 high. PC13 and the outputs 0x20 and 0x80
  did not change the reading.

The stock firmware reads the same ADC input every 15 s (routines at
0x08012174 and 0x080121bc); with USB supply it first pulls output 0x10 low
for 100 ms and sets it high again afterwards. It compares the raw value with
3552, 3391, 3298 and 3232 to light four, three, two or one battery LED, and
also with 3800.

Hypotheses: PB0 is the battery voltage through a divider, and the battery
was nearly full during the test, which is why the reading sits near full
scale. Output 0x10 enables charging; the stock code pauses it to measure.
The volts per count are not known.

CRSF link to the ELRS module, observed with a test that sends a device ping
(`EE 04 28 00 EA` plus CRC) on USART1 at 400000 baud twice per second:

- With PC13 high, as the bootloader leaves it, nothing is received.
- With PC13 low the module answers every ping with a 37-byte DEVICE_INFO
  frame (type 0x29, addressed to 0xEA from 0xEE): name `BETAFPV LiteRadi`,
  serial `ELRS`, software version bytes 00 03 06 04, 0x15 parameters.
- Each change of PC13 from high to low is followed by about 1370 other
  bytes before the first answer, and once by the frame
  `EA 0D 3A EA EE 10 00 00 9C 40 FF FF FC 18 EC`.
- Shift register outputs 0x10, 0x20 and 0x80 did not affect the answers in
  either state.

Listening on the CH9102 bridge at 115200 baud while the test changed the
control lines showed:

- Each change of PC13 to low is followed by the ESP32 boot message
  `rst:0x1 (POWERON_RESET), boot:0x13 (SPI_FAST_FLASH_BOOT)`. With PC13 high
  the bridge receives nothing.
- The boot message is the same with shift register outputs 0x10, 0x20 and
  0x80 in any state before the start, and those outputs cause no restart
  while the module runs. The pattern the bootloader plays on its second
  SETUP press (0x80 high, 0x10 low, 0x20 low, 0x10 high, 0x20 high) produced
  no output with the module off and no restart with it on.
- The host does not notice a change of the USB switch while the controller's
  USB device stays active; the console port then fails until the next
  enumeration. Powering the USB peripheral down and holding PA11 and PA12
  low for 700 ms before switching makes the host enumerate the bridge.
- D+ (PA12) has a pull-up the firmware cannot switch off, and the board has
  no pin for it. Observed 2026-11-20 with firmware 0.14.0 on Windows 11:
  after `usbd_disable()` and `usbd_shutdown()` and a new device description,
  the host noticed a change from the joystick (2FE3:0005) to the Xbox
  controller (2FE3:0007), but not the change back; it kept the Xbox device,
  whose serial port then failed with "Ein an das System angeschlossenes Gerät
  funktioniert nicht". With PA12 driven low for 20 ms after the shutdown,
  both directions enumerated anew in each of more than ten changes.

Hypotheses: PC13 high removes power from the module or holds it in reset.
The 1370 bytes are the boot messages seen on the bridge. The 0x3A frame is
the module's timing request; 0x9C40 is 40000 units of 0.1 us, the 4 ms
interval of the 250 Hz packet rate. Whether the module transmits RF while it only receives pings
was not checked.

Other observations from the scanner:

- The bootloader's pin setup at application start, as CRL/CRH values:
  port A 0x44440000 / 0x88844494 (after Zephyr's own pin setup), port B
  0x4448b444 / 0x83334444, port C CRH 0x38344444.

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

Option-byte watchdog setting, volts per count of the battery input, the
function of the shift register outputs 0x20 and 0x80, calibrated voltage monitoring, CRSF output and timing, USB HID
reports, and STM32 drivers other than clock, GPIO, ADC and USB device.
No settings writes are enabled during feasibility work. The largest image
loaded through the stock USB updater and seen running is 48516 bytes; the
stock application is 58672 bytes.
