# Building and development tools

The firmware builds against upstream Zephyr with its STM32F103 drivers; Zephyr
has no Geehy APM32 support of its own. `west` is Zephyr's workspace and build
command-line tool. `west/west.yml` pins Zephyr v4.5.0 and imports only the
modules this board needs (cmsis, cmsis_6, hal_stm32). The repository root is
the west workspace; the sources are downloaded into the ignored `.deps/`
directory.

## Setup

Requires Python 3.12 or newer, Git and 7-Zip on PATH. Run once from the
repository root:

```powershell
python -m venv .venv
.venv/Scripts/python -m pip install west
.venv/Scripts/python -m west init -l west
.venv/Scripts/python -m west update --narrow -o=--depth=1
.venv/Scripts/python -m pip install -r .deps/zephyr/scripts/requirements-base.txt cmake ninja
$env:ZEPHYR_BASE = "$PWD/.deps/zephyr"
.venv/Scripts/python -m west sdk install -t arm-zephyr-eabi
```

- `python -m venv .venv` creates a private Python environment in `.venv/`, so
  nothing is installed into the system Python.
- `west init -l west` registers `west/west.yml` as the local manifest and
  creates the ignored `.west/` directory.
- `west update` clones the pinned revisions; `--narrow -o=--depth=1` fetches
  only those commits without history.
- `west sdk install` downloads the Zephyr SDK version named in
  `.deps/zephyr/SDK_VERSION` into the home directory (`~/zephyr-sdk-1.0.1`,
  about 800 MB). Zephyr's build finds it there without further configuration.

dtc need not be installed. Zephyr then skips an optional extra devicetree
check; the devicetree itself is processed by Zephyr's Python scripts.

## Build

```powershell
./build.ps1
```

`build.ps1` puts `.venv/Scripts` on the process PATH, invokes west with
sysbuild disabled, builds into `build/` and runs `tools/check_image.py` on the
result. The image is `build/zephyr/zephyr.bin`. The script does not install
software or flash anything.

Devicetree (`boards/`) describes the MCU, its clock and the peripherals Zephyr
drives; Kconfig (`prj.conf`) selects compiled features. Application logic is
C++20, built without exceptions, RTTI and the C++ standard library; code tied
to the vendor bootloader stays C. `VERSION` holds the firmware version. Do not
run `west flash`: the board intentionally provides no flashing runner.

Notes on the board files:

- The devicetree uses the clock the vendor bootloader sets up: 8 MHz HSE,
  PLL x9, 72 MHz system clock, APB1 divided by 2.
- Zephyr 4.5 keeps the STM32 ADC instances below a common-block node. The
  devicetree therefore enables `&adc12_common` in addition to `&adc1`; without
  it the ADC driver is not compiled and the link fails. On the F1 series the
  ADC prescaler is given as a second clock named `adc_pre`.
- `CONFIG_INIT_ARCH_HW_AT_BOOT=y`, because the bootloader starts the
  application with interrupts masked (see [HARDWARE.md](HARDWARE.md)).
- The CRSF UART is never used as a logging console; the console is the USB
  serial port.

## Tests

`./test.ps1` runs the tests of the hardware-free code in `src/core/` on the
PC. It needs `g++` on PATH. It also checks that `web/index.html` carries the
version from `VERSION` and the protocol number of `src/host.cpp`.

## Loading an image from the command line

```powershell
.venv/Scripts/python tools/flash.py build/zephyr/zephyr.bin
```

`tools/flash.py` sends the image through the vendor USB updater, as the
configurator does; pyserial is already in the Python environment. It needs one
of two starting states:

- This firmware is running with the radio switched off. The tool opens the
  serial port at 1200 baud, the firmware restarts into the updater, the image
  is written, and the radio restarts into it. Nothing on the radio has to be
  touched. The tool reads the radio's unique ID from the hello line first and
  writes only to the updater port with the USB serial number derived from it,
  so that a flight controller with the same USB IDs is never written.
- The updater (0483:5740) is already there after the button combination: USB
  unplugged, hold SETUP and switch on until the double beep, plug USB in. This
  works with any or no application. Afterwards USB has to be unplugged once to
  start the application.

If a download is interrupted, the radio stays in update mode and the tool can
be run again. If an image does not start at all, use the button combination.

The updater only writes the application area from 0x08008000. Do not write
below that address by any other way (SWD, option bytes): the area holds the
vendor bootloader and the factory calibration.

### Update mode without buttons

The vendor bootloader has no command to enter update mode and never leaves it
by itself. `CONFIG_UPDATER_ENTRY`, defined in `Kconfig` and set in `prj.conf`,
adds `src/updater_entry.c`:

- `updater_enter()` stores a marker in RAM and resets. The firmware calls it
  when the serial port's baud rate is 1200 and the ELRS module is off.
- `soc_early_reset_hook()`, the first code Zephyr runs after reset, finds the
  marker and continues in the bootloader's update branch. Zephyr provides this
  hook through `CONFIG_SOC_EARLY_RESET_HOOK`.
- Before that it installs a timer handler in RAM that resets the radio after a
  completed download, or after 30 s if no download has started.

The file contains addresses inside the vendor bootloader. They were taken from
one bootloader build and must be checked before use with another.

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
workspace as above, builds the firmware, and publishes `web/` to GitHub Pages
with the image beside the page as `firmware/openliteradio.bin`.
