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
- Not configurable: power on/off on the power button, and a fixed button
  combination at power-on that ignores all own settings.
- The radio starts locked and sends no channel data until it is unlocked. How
  it unlocks is a setting: throttle at its lower end (the default), or
  throttle up and down again. Further gestures may follow.
- Flash below 0x08008000 is not written. The factory calibration there is
  not used because it does not fit the stick readings (docs/HARDWARE.md); own
  settings are stored in the application area. The stock firmware therefore
  finds its data unchanged when it is flashed back.
- The radio reports its settings with key, range and default; the web tool
  holds labels and layout. ELRS parameters will be handled in the web tool,
  with the radio passing raw CRSF frames through.
- The vendor bootloader stays. The RF stack stays in the ELRS module.

## Inputs

| Prio | Feature | Stock | Status |
|---|---|---|---|
| M | Four stick axes with averaging | code: 5 samples | raw readings in the board layer, hw |
| M | Stick calibration (upper end, centre, lower end), output 988..2012 | code; factory values read | hw |
| - | Read the factory calibration at 0x08007800 as default | - | dropped; the values do not fit the readings, see docs/HARDWARE.md |
| M | Centre dead zone 1 %, not on throttle | manual, code | setting per axis, 0 to 10 %, default 1; shown and edited in the web tool; PC tests only |
| M | Switches SA, SD (toggle), SB, SC (three positions), buttons SE, SF | manual; pins measured | pins known, hw |
| M | Buttons: power, BIND, SETUP with short and long press | manual, code | missing |
| S | Two five-way trim buttons | manual; key device on a software two-wire bus, measured | read in the test build, hw |
| S | Stick calibration started on the radio | manual: SETUP long, short, short | SETUP held 2 s, a short press ends it, hw |

## Channels and mixing

| Prio | Feature | Stock | Status |
|---|---|---|---|
| M | Channel order AETR, switches on channels 5 to 10 | manual, measured | default assignment, hw |
| M | Channel assignment table with per-channel weight and reverse | code; table read | settings ch.N.*, editable in the web tool; defaults seen on the radio, changes only in the PC tests |
| S | Trim in 4 µs steps up to ±25, reset, stored | manual; steps and range agreed | built in 0.11.0, one step per press, no repeat on long press, hw |
| S | Stick mode selection (mode 2 by default) | code: mode flag, meaning open | missing |
| S | Response curve per stick (expo) | code: 512-entry table | missing |
| C | Dual rates | no | missing |
| C | Several model memories | no | dropped: without a display the radio cannot show which one is active; one Custom assignment beside the fixed AETR and TAER (0.9.0), export and import as a file planned |
| C | Free mixes beyond the assignment table | no | missing |

## Link to the ELRS module

| Prio | Feature | Stock | Status |
|---|---|---|---|
| M | CRSF channel frames on USART1, 400000 baud, 16 channels | code | missing |
| M | Frame period follows the module's packet rate (50 to 1000 Hz) | code | missing |
| M | Receive link statistics, device info and parameter entries | code | device info received by the test build, hw |
| M | Parameter access by name: Bind, BLE, packet rate, power, telemetry ratio, WiFi | code | missing |
| M | Binding | manual: BIND long, up to 8 s | missing |
| S | Bluetooth gamepad through the module | manual: BIND held at power-on | missing |
| S | WiFi mode of the module | manual names the LED state only | missing |
| S | Disconnection warning | manual: tone | missing |
| C | Telemetry warnings (link quality, receiver voltage) | no | missing |

## Safety and power

| Prio | Feature | Stock | Status |
|---|---|---|---|
| M | No output while the throttle is not at its lower end at power-on | manual | lock seen held and released in the state lines, hw; not seen with the module on |
| S | Unlock gesture selectable: throttle down, throttle up and down | - | setting unlock.mode; mode 2 only in the PC tests |
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
| S | Buzzer can be switched off | manual | setting buzzer.on, not heard |
| S | Tone on release of the throttle lock can be switched off in the web tool | - | setting unlock.beep, not heard |

## USB

| Prio | Feature | Stock | Status |
|---|---|---|---|
| M | Update without buttons and without unplugging | no | hw |
| M | USB connector switching between controller and bridge | measured | hw |
| S | Gamepad as HID (DirectInput) | measured | channels 1 to 8 as axes, 9 to 16 as buttons, beside the serial port; checked against the channels with injected inputs (0.7.1), not yet tried in a simulator |
| S | Gamepad as XInput | manual: stick position while plugging in; class found in the image | Xbox 360 controller beside the serial port, chosen with `usb.mode` or both trim centres held 2 s with the radio off; checked against the channels with injected inputs over XInput (0.14.0), not yet tried in a game |
| S | Serial configuration interface with own protocol | stock uses the Configurator protocol | list, get, set, save, revert, defaults, hw |
| S | Live stream of stick and switch values for the configuration tool | measured: 10 values at about 500 Hz | state lines 25 times per second, shown by web/index.html, hw |
| S | USB serial port for configuration and update whenever a cable is plugged in | no: stock has USB only when off | hw |
| C | PC control: a program on the PC moves virtual sticks and switches over the serial port, sent to the model through the ELRS module (keyboard control, ground station, test stand). Safeguards: only while an enable switch on the radio is set, any switch-back returns to the sticks at once; the sticks take over again when no update arrives for about 200 ms; the throttle lock stays; own LED colour and a tone on every change | no | planned; the `inject` test command (module off only) is the starting point |
| - | Update with SETUP + power | measured; bootloader | kept |
| - | Flashing the ELRS module through the USB bridge | bootloader code | kept |

## Settings

| Prio | Feature | Stock | Status |
|---|---|---|---|
| M | Persistent settings in the application area | stock stores below 0x08008000 | NVS in the top four pages, one record per setting; survives a firmware update, hw |
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
- Meaning of the shift register outputs 0x20 and 0x80; whether output 0x10
  enables charging.
- Whether PC13 switches the module's supply or its reset, and whether the
  module transmits before it receives channel frames.
- How the stock firmware enters the module's WiFi mode.
