# Firmware architecture

Agreed on 2026-11-17. The scope is in [FEATURES.md](FEATURES.md), the hardware
in [WIRING.md](WIRING.md).

## Constraints

- 20 KiB RAM. Zephyr with the USB serial port takes about 14 KiB, which leaves
  about 6 KiB for application data and thread stacks.
- The ELRS module sets the frame rate: 250 Hz by default, up to 1000 Hz. The
  path from stick to CRSF frame has to keep that rate.
- C++20 without exceptions, RTTI, heap or C++ standard library: fixed arrays
  and plain classes.

## Layers

| Layer | Directory | Content | Touches hardware |
|-------|-----------|---------|------------------|
| board | `src/board/` | sticks, switches, buttons, trim keys, shift register, status LED, buzzer, power latch, battery input, module enable | yes, and only this layer |
| core | `src/core/` | calibration, dead zone, channel assignment, trim, CRSF encoding and decoding, parameter access, action table, settings model | no |
| app | `src/` | operating modes, threads, host protocol | through board only |

The core layer contains everything where an error is dangerous or hard to see
on the radio. Because it touches no hardware it is also compiled for the PC and
checked there with fixed inputs.

## Threads

- Control thread, high priority, at the module's frame rate: read sticks and
  switches, compute channels, send the CRSF frame.
- Main thread, low priority, 50 Hz: buttons and actions, LED and buzzer
  patterns, battery, power, CRSF parameters, host protocol.
- Bytes from the module arrive by interrupt in a ring buffer and are parsed in
  the main thread.

Each further thread would cost about 1 KiB of stack.

## Decisions

- Tests for the core layer run on the PC.
- Settings use Zephyr's NVS as long as flash allows, in pages at the top of
  the application area. The vendor updater erases only as many pages as the
  image needs, so settings survive a firmware update. Flash below 0x08008000
  is read only; the factory calibration there is the default.
- The USB serial port is always available for configuration and update when a
  cable is plugged in, also while the radio is switched on.
- The status LED is driven by TIM2 with DMA, not by timed loops with
  interrupts masked, so that no CRSF byte is lost while a colour is sent.

## Stages

| Stage | Result | State |
|-------|--------|-------|
| 1 | board layer, power on/off, LED, buzzer, host protocol with live view | built, see below |
| 2 | flying: factory calibration, channels, CRSF at the module's rate, throttle lock, binding | open |
| 3 | settings storage, trim, calibration on the radio | open |
| 4 | USB joystick, then Xbox mode | open |
| 5 | configuration protocol for the web tool | open |
| 6 | Bluetooth, WiFi, battery display, remaining features | open |

Stage 1 as built: one thread only, `src/main.cpp`. The power button switches
the radio on after 1 s and off after 2 s. The status LED is blue while the
radio is on. The ELRS module stays off unless the host switches it on.

## Host protocol

Text lines on the USB serial port, one message per line, fields separated by
spaces. It is readable in a terminal and parsed by `web/live.html`. Opening
the port at 1200 baud restarts the radio into the vendor USB updater.

From the radio:

- `hello OpenLiteRadio <version> proto=1`, at start and on request.
- `state key=value ...`, 25 times per second while streaming. Keys: `sticks`
  (four raw 12-bit values: left vertical, left horizontal, right vertical,
  right horizontal), `sa`, `sd` (0 or 1), `sb`, `sc` (0, 1, 2), `se`, `sf`,
  `bind`, `setup`, `power` (1 while pressed), `tl`, `tr` (trim buttons: bit 0
  up, 1 down, 2 left, 3 right, 4 centre), `keys` (1 if the trim key device
  answers), `battery` (raw 12-bit), `usb` (1 with USB supply), `on`, `module`.

To the radio:

| Line | Effect |
|------|--------|
| `hello` | send the hello line |
| `stream 0` / `stream 1` | stop or start the state lines |
| `led R G B` | status LED colour, 0 to 255 each; `led auto` returns control to the firmware |
| `beep HZ MS` | tone |
| `batleds MASK` | battery LEDs, bit 0 is the leftmost |
| `module 0` / `module 1` | ELRS module off or on |

The protocol number in the hello line changes when a message changes meaning.
New keys and new commands do not change it; a reader ignores what it does not
know.
