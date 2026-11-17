# Zephyr feasibility

The probe builds against pinned upstream Zephyr with STM32F103 drivers. Zephyr
has no Geehy APM32 support of its own. A hardware test on 2026-11-17 showed the
kernel, the USB device stack and the ADC working on the APM32F103CBT6; see
"Hardware test" below and docs/HARDWARE.md.

Acceptance criteria:

1. ADC/GPIO/UART application links within 96 KiB flash and 20 KiB RAM.
2. Binary begins with Cortex-M vectors at 0x08008000; no MCUboot header.
3. ELF contains no loadable flash segment below that address.
4. A separate USB HID + CDC build measures the remaining memory budget.
5. Explicitly separate compile results from future Geehy/USB hardware tests.

Do not conclude that FreeRTOS is required merely from default Zephyr sample
sizes; compare a deliberately small configuration with the required drivers.

## First probe results, 2026-11-16 (Nordic SDK core)

Both variants built successfully and passed tools/check_image.py, including
ELF load-address checks. All file-backed load segments lie in the application
slot at 0x08008000..0x08020000. Initial stack values lie within 20 KiB SRAM.

- ADC/GPIO/UART probe: 17680 bytes flash, 3216 bytes statically reserved RAM.
  Binary SHA-256:
  1e7acecbc5495a4745c0e841b672181f887255fc04b40b230c883232e55abe4b.
  Initial SP 0x20000c90, reset vector 0x08008a75.
- Probe plus USB HID/CDC: 48776 bytes flash, 14104 bytes statically reserved RAM.
  Binary SHA-256:
  d9254577dbba877e3944e5d4e04a47033c2619f5e55b03f00c56336b5d38394e.
  Initial SP 0x20002350, reset vector 0x08008b29.

The USB variant leaves 6276 bytes of RAM and 49528 bytes of flash. These
figures include configured stacks and buffers, but do not measure runtime
stack high-water marks. Mixer, CRSF protocol handling, safety, switches,
configuration protocol, and persistence are not implemented. No claim about
their final footprint follows from this probe alone.

Conclusion: Zephyr passes the first compile/layout/memory feasibility check.
RAM deserves attention with USB enabled; there is no build-based reason yet
to switch to FreeRTOS.

The figures above come from the first probe, built with the Zephyr core of an
installed Nordic SDK (NCS v3.4.1, Zephyr 4.4.2 at
33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6) plus upstream hal_stm32. That
environment is superseded by the pinned upstream workspace below.

## Pinned upstream workspace, 2026-11-16

`west/west.yml` pins upstream Zephyr v4.5.0 and imports only the modules this
board needs. The repository root is the west workspace; sources are checked out
below the ignored `.deps/` directory. No part of the Nordic installation is
used: Python tools come from a virtual environment in `.venv/`, the compiler
from a standalone Zephyr SDK.

- zephyr v4.5.0, 41d10b59730406bbab4ca9f1fbbe9e96aafb4966
- hal_stm32 c9550199186236d610092f4005f8c9bae2a6bfcf
- cmsis 512cc7e895e8491696b61f7ba8066b4a182569b8
- cmsis_6 1c1840af7a7e757d6e2fec3ddb0e5ce0dfcc93c8
- Zephyr SDK 1.0.1 (the version in Zephyr's `SDK_VERSION` file), GNU ARM
  compiler 14.3.0, only the `arm-zephyr-eabi` toolchain installed.
- Python 3.13.12, west 1.5.0, CMake 4.4.5, Ninja 1.13.2.

Results of 2026-11-17, all passing tools/check_image.py:

- ADC/GPIO/UART probe: 15132 bytes flash, 3288 bytes statically reserved RAM.
  Binary SHA-256:
  3cf00d77e6461a2562c83a313d38db0ead1f4b4cf39b9010e713d4e40243d9b9.
  Initial SP 0x20000cd8, reset vector 0x08008919.
- Probe plus USB HID/CDC: 46088 bytes flash, 13208 bytes statically reserved
  RAM. Binary SHA-256:
  ca2069554fcc49f1323c071a8618a1f8c60b57ff51752d1d1fcec9648b4f3042.
  Initial SP 0x20002788, reset vector 0x080089c5.
- Hardware test with USB CDC console and updater entry: 48516 bytes flash,
  14008 bytes statically reserved RAM. Binary SHA-256:
  e5f8f62e2777a14eff4e571ba7f8e557f17aff59943590184abe7e32421954f1.

The USB HID/CDC variant leaves 7272 bytes of RAM and 52216 bytes of flash.
Only the hardware test image has been flashed.

The application's main source is C++20 (`CONFIG_CPP`, `CONFIG_STD_CPP20`).
Zephyr compiles it without exceptions and RTTI and links no C++ standard
library. Compared with the same source built as C, flash grew by 24 bytes in
the basic probe and by about 80 bytes in the two USB builds; reserved RAM is
unchanged in the basic probe and 112 bytes larger in the USB builds.

Zephyr 4.5 moved the STM32 ADC instances below a common-block node. The board
devicetree therefore enables `&adc12_common` in addition to `&adc1`; without it
the ADC driver is not compiled and the link fails. On the F1 series the ADC
prescaler is given as a second clock named `adc_pre`.

The board devicetree uses the clock the vendor bootloader sets up: 8 MHz HSE,
PLL x9, 72 MHz system clock, APB1 divided by 2.

dtc is not installed. Zephyr then skips an optional extra devicetree check;
the devicetree itself is processed by Zephyr's Python scripts.

### Setup

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

### Build

```powershell
./build.ps1
./build.ps1 -Usb
./build.ps1 -HwTest
```

build.ps1 puts `.venv/Scripts` on the process PATH, invokes west with sysbuild
disabled and runs tools/check_image.py. It does not install software or invoke
a flash command. `-Usb` adds usb.conf and usb.overlay and builds into
`build-usb/`; `-HwTest` adds hwtest.conf and hwtest.overlay and builds into
`build-hwtest/`. The CRSF UART is never used as a logging console; only the
hardware test has a console, on USB CDC ACM.

## Hardware test, 2026-11-17

The `-HwTest` image was loaded with the vendor USB updater and observed over
USB only. It sets shift register output 0x40, which puts the controller on the
USB connector, and prints one status line every 500 ms.

Observed: the image starts from the vendor bootloader, enumerates as
2FE3:0004 (Zephyr's default test IDs, not a release identity), keeps a steady
500 ms interval, and reads all four stick axes. Details and the axis mapping
are in docs/HARDWARE.md.

Two faults in earlier test images, both in the test program:

- Reading four ADC channels in one sequence returned -EINVAL. The STM32F1 ADC
  driver supports several channels per sequence only with DMA. The test reads
  the channels one at a time.
- Cycling the shift register outputs disconnected the controller from the USB
  connector every two seconds, so enumeration failed.

Not covered: runtime stack high-water marks, operation from battery, CRSF
output, USB HID reports.

## Loading an image

```powershell
.venv/Scripts/python tools/flash.py build-hwtest/zephyr/zephyr.bin
```

tools/flash.py speaks the vendor updater's protocol; pyserial is already in
the Python environment. It needs one of two starting states:

- Firmware built with `CONFIG_UPDATER_ENTRY` is running and shows its USB
  console (2FE3:0004). The tool opens the console at 1200 baud, the firmware
  restarts into the updater, the image is written, and the controller restarts
  into it. Nothing on the radio has to be touched.
- The updater (0483:5740) is already present after the button combination:
  USB unplugged, hold SETUP and power until the double beep, plug USB in. This
  works with any or no application. Afterwards USB has to be unplugged once to
  start the application.

`CONFIG_UPDATER_ENTRY` is defined in the application's Kconfig file and set in
hwtest.conf. It adds src/updater_entry.c:

- `updater_enter()` stores a marker in RAM and resets. The hardware test calls
  it when the console's baud rate is 1200.
- `soc_early_reset_hook()`, the first code Zephyr runs after reset, finds the
  marker and continues in the bootloader's update branch. Zephyr provides this
  hook through `CONFIG_SOC_EARLY_RESET_HOOK`.
- Before that it installs a timer handler in RAM that resets the controller
  after a completed download, or after 30 s if no download has started.

The file contains addresses inside the vendor bootloader. They were taken from
one bootloader build and must be checked before use with another.

If a download is interrupted, the controller stays in update mode and the tool
can be run again. If an image does not start at all, use the button
combination.

## Hardware gates before functional firmware

1. Done: vendor bootloader handoff, interrupt state and clock setup. Open:
   power latch for battery operation and the option-byte watchdog setting.
2. Done: ADC readings and physical input order. Open: comparison against the
   saved calibration, both directions of each axis.
3. Verify CRSF electrical timing and a throttle-low startup policy.
4. Done: USB switching and clock; the controller enumerates with the HSE-based
   72 MHz clock. Open: HID reports and a release USB identity. The IDs
   0xffff:0xffff of the `-Usb` build are placeholders.
5. Verify restored stock operation through the preserved USB updater after
   these tests. The largest image loaded and seen running is 48516 bytes;
   the stock application is 58672 bytes.

No vendor settings are written by any of the builds. Do not infer a working
joystick from the USB build: its classes initialize, but it sends no axis
reports.
