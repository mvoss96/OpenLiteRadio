# OpenLiteRadio

Experimental open controller firmware for the BETAFPV LiteRadio 4.
The first milestone evaluates Zephyr on the Geehy APM32F103CBT6.
This is not flight-ready firmware. A hardware test image has run on the radio;
it reads the sticks and prints them on a USB console, nothing more.

The controller sends CRSF to a separate ESP32 ExpressLRS module. Module
hardware configuration belongs in the sibling `literadio4-elrs-target`
repository; changing controller firmware must not require a different target.

## First milestone

- Build a minimal Zephyr application using STM32F103-compatible drivers.
- Link vectors at `0x08008000` with a 96 KiB application limit and 20 KiB RAM.
- Compile ADC, GPIO, and 400000-baud USART1 support.
- Assess USB HID plus CDC memory use separately.
- Verify actual Geehy peripheral behaviour on hardware, with explicit flash
  approval for every image.

The probe reads four ADC inputs. It does not transmit CRSF, enable RF
deliberately, write flash, or expose a configuration interface.

Hardware test of 2026-11-17: Zephyr starts from the vendor bootloader, the USB
device stack enumerates, and ADC channels 0 to 3 (PA0 to PA3) follow the four
stick axes. Battery operation, CRSF output, switches, LEDs and USB HID reports
are untested. See `docs/HARDWARE.md` for the observations.

Build result with pinned upstream Zephyr v4.5.0: 15108 bytes flash / 3288 bytes
RAM for the basic probe; 46008 bytes flash / 13096 bytes RAM with USB HID and
CDC class initialization; 48440 bytes flash / 13896 bytes RAM for the hardware
test with a USB console.
All fit. See `docs/ZEPHYR.md` for exact versions, commands, hashes, and limits.
The optional USB probe has placeholder IDs and sends no joystick reports.

## Build

`west` is Zephyr's workspace and build command-line tool. `west/west.yml` pins
the Zephyr version; the sources are downloaded into the ignored `.deps/`
directory. Set up the Python environment, the workspace and the Zephyr SDK once
as described under Setup in `docs/ZEPHYR.md`, then:

```powershell
./build.ps1
./build.ps1 -Usb
./build.ps1 -HwTest
```

`build.ps1` builds into `build/` (or `build-usb/`, `build-hwtest/`) and runs
`tools/check_image.py` on the result. `-HwTest` builds the hardware test with
a console on USB.

Devicetree (`boards/`) describes the hardware; Kconfig (`prj.conf`) selects
compiled features. Application logic is C. Do not run `west flash`: the board
intentionally provides no flashing runner. Keep the vendor USB bootloader.

## Loading an image

```powershell
.venv/Scripts/python tools/flash.py build-hwtest/zephyr/zephyr.bin
```

`tools/flash.py` sends the image through the vendor USB updater, which only
writes the application area. While the hardware test firmware is running, the
tool switches the radio into update mode and back by itself. Otherwise enter
update mode with the buttons first: USB unplugged, hold SETUP and power until
the double beep, plug USB in. Details are in `docs/ZEPHYR.md`.

## Evidence and limits

See `docs/HARDWARE.md` and `docs/ZEPHYR.md`. A successful build does not
establish that a peripheral works on the APM32; only what the hardware test
section lists has been observed.
No vendor binaries, personal settings, or device dumps belong in this repo.
