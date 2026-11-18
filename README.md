# OpenLiteRadio

Experimental open controller firmware for the BETAFPV LiteRadio 4, built on
Zephyr for the radio's Geehy APM32F103CBT6. It is not flight-ready: the
firmware reads all controls, drives the indicators, handles power and sends
channel frames to the ELRS module, but binding has not been tried with a
receiver and it has not been flown. Calibrate the sticks first: hold SETUP for
2 s, move both sticks to all ends, release the right stick, leave the throttle
at its lower end and press SETUP briefly.

The controller talks CRSF to a separate ESP32 ExpressLRS module on the RF
board. That module's hardware configuration belongs in the sibling
`literadio4-elrs-target` repository; changing controller firmware must not
require a different target.

## Documents

| File | Content |
|------|---------|
| [docs/FEATURES.md](docs/FEATURES.md) | planned scope against the stock firmware, priorities, decisions |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | layers, threads, build stages, host protocol |
| [docs/WIRING.md](docs/WIRING.md) | what each pin is connected to, with a diagram |
| [docs/HARDWARE.md](docs/HARDWARE.md) | observations and analysis behind the wiring |
| [docs/ZEPHYR.md](docs/ZEPHYR.md) | Zephyr workspace setup, versions, feasibility results |

## Build

`west` is Zephyr's workspace and build command-line tool. `west/west.yml` pins
the Zephyr version; the sources are downloaded into the ignored `.deps/`
directory. Set up the Python environment, the workspace and the Zephyr SDK once
as described under Setup in `docs/ZEPHYR.md`, then:

```powershell
./build.ps1
```

`build.ps1` builds into `build/` and runs `tools/check_image.py` on the result.

`./test.ps1` runs the tests of the hardware-free code in `src/core/` on the
PC. It needs `g++` on PATH.

Devicetree (`boards/`) describes the MCU, its clock and the peripherals Zephyr
drives; Kconfig (`prj.conf`) selects compiled features. Application logic is
C++20, built without exceptions, RTTI and the C++ standard library; code tied
to the vendor bootloader stays C. `VERSION` holds the firmware version. Do not
run `west flash`: the board intentionally provides no flashing runner. Keep the
vendor USB bootloader.

## Loading an image

```powershell
.venv/Scripts/python tools/flash.py build/zephyr/zephyr.bin
```

`tools/flash.py` sends the image through the vendor USB updater, which only
writes the application area. While this firmware is running, the tool switches
the radio into update mode and back by itself. Otherwise enter update mode with
the buttons first: USB unplugged, hold SETUP and power until the double beep,
plug USB in. Details are in `docs/ZEPHYR.md`.

## Configurator

Open `web/index.html` in Chrome or Edge, plug the radio in and press
"Verbinden". Press "Trennen" before loading firmware; the serial port can only
be open in one program. The page is in German.

- "Belegung" shows the profile and a drawing of the radio. The profile menu
  loads, creates, renames and deletes up to four own profiles stored on the
  radio and applies the templates AETR and TAER. Each control carries a label
  with its channel and live value; clicking a label opens a card with its
  channel, dead zone or switch positions, response curve, and under
  "Erweitert" weight and reverse. Choosing a taken channel asks whether to
  swap or replace. In a narrow window the labels become a list below the
  drawing. "Knüppel kalibrieren" starts the stick calibration run.
- "Einstellungen" holds the unlock gesture and the tones.
- "System" shows firmware and battery readings, test buttons for tone,
  module and binding, the fixed button combinations, and every setting the
  radio reports in raw form.

Every change is stored on the radio at once, in the active own profile if
there is one. The page needs no installation; the browser reaches the
radio's USB serial port through the Web Serial API.

## USB joystick

Plugged in, the radio is also a game controller: channels 1 to 8 are its
axes, channels 9 to 16 its buttons, so a simulator sees the same assignment
as the receiver. It works as soon as USB is connected, without switching the
radio on and without the throttle lock.

```powershell
.venv/Scripts/python tools/joystick_test.py
```

shows the axes and buttons as Windows reads them. That interface shows six
of the eight axes (channels 1, 2, 3, 4, 6, 7); simulators see all eight.
`tools/joystick_check.py` checks the joystick without touching the radio:
it sets stick and switch readings over the serial port and compares the
joystick with the channel outputs.

## Limits

A successful build does not establish that something works on the radio; what
has been observed on hardware is recorded in `docs/HARDWARE.md`. No vendor
binaries, personal settings, or device dumps belong in this repo.
