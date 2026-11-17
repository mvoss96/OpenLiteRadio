# Feature list

Planned scope of the firmware, agreed on 2026-11-17. The reference is the stock
firmware 1.1.3: OpenLiteRadio is meant to do nothing less, and a few things
more.

Columns:

- **Prio**: M = needed for the first flyable version, S = should follow soon,
  C = could come later.
- **Stock**: whether the stock firmware has it, and how that is known.
  *manual* = BETAFPV user manual (2025-03-18), not checked on the device.
  *code* = disassembly of the stock firmware or bootloader.
  *measured* = observed on the device.
- **Status**: what exists in this repository. *hw* means it ran on the radio.

Evidence for the stock column is in docs/HARDWARE.md and in a controller
analysis kept outside this repository.

## Decisions

- Configuration uses an own protocol over the USB serial port. Compatibility
  with the BETAFPV Configurator is not a goal. A web configuration tool is
  planned as a separate project; a browser can reach a USB serial port through
  the Web Serial API (Chrome, Edge).
- Button and switch assignments are settings, editable from that tool. Without
  own settings the radio behaves as the stock manual describes.
- Not configurable: power on/off on the power button, the throttle lock at
  power-on (it can only be switched off as a whole), and a fixed button
  combination at power-on that ignores all own settings.
- Flash below 0x08008000 is only read. The factory calibration there is used
  as the default; own settings are stored in the application area. The stock
  firmware therefore finds its data unchanged when it is flashed back.
- The vendor bootloader stays. The RF stack stays in the ELRS module.

## Inputs

| Prio | Feature | Stock | Status |
|---|---|---|---|
| M | Four stick axes with averaging | code: 5 samples | raw readings, hw |
| M | Stick calibration (upper end, centre, lower end), output 988..2012 | code; factory values read | missing |
| M | Read the factory calibration at 0x08007800 as default | - | missing |
| M | Centre dead zone 1 %, not on throttle | manual, code | missing |
| M | Switches SA, SD (toggle), SB, SC (three positions), buttons SE, SF | manual; pins measured | pins known, hw |
| M | Buttons: power, BIND, SETUP with short and long press | manual, code | missing |
| S | Two five-way trim buttons | manual; key device on a software two-wire bus, measured | read in the test build, hw |
| S | Stick calibration started on the radio | manual: SETUP long, short, short | missing |

## Channels and mixing

| Prio | Feature | Stock | Status |
|---|---|---|---|
| M | Channel order AETR, switches on channels 5 to 10 | manual, measured | missing |
| M | Channel assignment table with per-channel weight and reverse | code; table read | missing |
| S | Trim in 1 % steps, continuous on long press, reset, stored | manual | missing |
| S | Stick mode selection (mode 2 by default) | code: mode flag, meaning open | missing |
| S | Response curve per stick (expo) | code: 512-entry table | missing |
| C | Dual rates | no | missing |
| C | Several model memories | no | missing |
| C | Free mixes beyond the assignment table | no | missing |

## Link to the ELRS module

| Prio | Feature | Stock | Status |
|---|---|---|---|
| M | CRSF channel frames on USART1, 400000 baud, 16 channels | code | missing |
| M | Frame period follows the module's packet rate (50 to 1000 Hz) | code | missing |
| M | Receive link statistics, device info and parameter entries | code | missing |
| M | Parameter access by name: Bind, BLE, packet rate, power, telemetry ratio, WiFi | code | missing |
| M | Binding | manual: BIND long, up to 8 s | missing |
| S | Bluetooth gamepad through the module | manual: BIND held at power-on | missing |
| S | WiFi mode of the module | manual names the LED state only | missing |
| S | Disconnection warning | manual: tone | missing |
| C | Telemetry warnings (link quality, receiver voltage) | no | missing |

## Safety and power

| Prio | Feature | Stock | Status |
|---|---|---|---|
| M | No output while the throttle is not at its lower end at power-on | manual | missing |
| M | Power on/off: short press, then long press | manual; latch on PC15, measured | latch set and released by the test build, hw |
| M | Battery voltage measurement and low-battery warning | manual; PB0 with the stock thresholds in raw counts, code | raw value read, hw; not checked on a discharged battery |
| S | Battery level on the four-LED indicator on a short power press | manual; LEDs on shift register 0x0f, measured | LEDs switched by the test build, hw |
| S | Charging and charge-complete indication | manual | missing |
| C | Inactivity alarm and automatic power-off | no | missing |

## Indicators

| Prio | Feature | Stock | Status |
|---|---|---|---|
| M | Status LED: blue working, purple Bluetooth, green WiFi, red throttle warning | manual; WS2812-type LED on PB3, measured | colours shown by the test build, hw |
| M | Buzzer patterns for power, binding, calibration, warnings | manual, code; buzzer on PB1, measured | single tone in the test build, hw |
| S | Buzzer can be switched off | manual | missing |

## USB

| Prio | Feature | Stock | Status |
|---|---|---|---|
| M | Update without buttons and without unplugging | no | hw (hardware test build) |
| M | USB connector switching between controller and bridge | measured | hw |
| S | Gamepad as HID (DirectInput) | measured | classes build, no reports |
| S | Gamepad as XInput | manual: stick position while plugging in; class found in the image | missing |
| S | Serial configuration interface with own protocol | stock uses the Configurator protocol | missing |
| S | Live stream of stick and switch values for the configuration tool | measured: 10 values at about 500 Hz | missing |
| C | USB functions while the radio is switched on | no: stock has USB only when off | console in the test build, hw |
| - | Update with SETUP + power | measured; bootloader | kept |
| - | Flashing the ELRS module through the USB bridge | bootloader code | kept |

## Settings

| Prio | Feature | Stock | Status |
|---|---|---|---|
| M | Persistent settings in the application area | stock stores below 0x08008000 | missing |
| S | Configurable button and switch actions | no | missing |
| S | Fixed reset to default behaviour at power-on | no | missing |

## Not planned

- Own RF stack or changes to the ELRS module firmware.
- XInput over Bluetooth; the stock firmware does not offer it either.
- Replacing the vendor bootloader.
- Compatibility with the BETAFPV Configurator.

## Hardware facts still to be established

These block features above and can be found with test images:

- Battery input PB0 on a partly discharged battery, and its volts per count.
- Meaning of PC13 and of the shift register outputs 0x20 and 0x80; whether
  output 0x10 enables charging.
- How the stock firmware enters the module's WiFi mode.
