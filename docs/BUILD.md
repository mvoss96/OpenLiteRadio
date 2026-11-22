# Building and development tools

## Build

`west` is Zephyr's workspace and build command-line tool. `west/west.yml` pins
the Zephyr version; the sources are downloaded into the ignored `.deps/`
directory. Set up the Python environment, the workspace and the Zephyr SDK once
as described under Setup in [ZEPHYR.md](ZEPHYR.md), then:

```powershell
./build.ps1
```

`build.ps1` builds into `build/` and runs `tools/check_image.py` on the result.
The image is `build/zephyr/zephyr.bin`.

Devicetree (`boards/`) describes the MCU, its clock and the peripherals Zephyr
drives; Kconfig (`prj.conf`) selects compiled features. Application logic is
C++20, built without exceptions, RTTI and the C++ standard library; code tied
to the vendor bootloader stays C. `VERSION` holds the firmware version. Do not
run `west flash`: the board intentionally provides no flashing runner.

## Tests

`./test.ps1` runs the tests of the hardware-free code in `src/core/` on the
PC. It needs `g++` on PATH. It also checks that `web/index.html` carries the
version from `VERSION` and the protocol number of `src/host.cpp`.

## Loading an image from the command line

```powershell
.venv/Scripts/python tools/flash.py build/zephyr/zephyr.bin
```

`tools/flash.py` sends the image through the vendor USB updater, as the
configurator does. While this firmware is running and the radio is switched
off, the tool switches the radio into update mode and back by itself.
Otherwise enter update mode with the buttons first: USB unplugged, hold SETUP
and power until the double beep, plug USB in. Details are in
[ZEPHYR.md](ZEPHYR.md).

The updater only writes the application area from 0x08008000. Do not write
below that address by any other way (SWD, option bytes): the area holds the
vendor bootloader and the factory calibration.

## USB joystick tools

```powershell
.venv/Scripts/python tools/joystick_test.py
```

shows the axes and buttons as Windows reads them. That interface shows six
of the eight axes (channels 1, 2, 3, 4, 6, 7); simulators see all eight.
`tools/joystick_check.py` checks the joystick without touching the radio:
it sets stick and switch readings over the serial port and compares the
joystick with the channel outputs.

## Configurator

The configurator is the one file `web/index.html`; it needs no build step and
also works opened as a local file.

The page is written in German. The table `EN` in it holds the English text of
every German one, and every text shown goes through `t()`. A new text needs an
entry in `EN`; without one it stays German in the English page.

## Publishing

`.github/workflows/pages.yml` runs on every push to `master`. It sets up the
Zephyr workspace as described in [ZEPHYR.md](ZEPHYR.md), builds the firmware,
and publishes `web/` to GitHub Pages with the image beside the page as
`firmware/openliteradio.bin`.
