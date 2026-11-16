# OpenLiteRadio

Experimental open controller firmware for the BETAFPV LiteRadio 4.
The first milestone evaluates Zephyr on the Geehy APM32F103CBT6.
This is not flight-ready firmware and has not been tested on hardware.

The controller sends CRSF to a separate ESP32 ExpressLRS module. Module
hardware configuration belongs in the sibling `literadio4-elrs-target`
repository; changing controller firmware must not require a different target.

## First milestone

- Build a minimal Zephyr application using STM32F103-compatible drivers.
- Link vectors at `0x08008000` with a 96 KiB application limit and 20 KiB RAM.
- Compile ADC, GPIO, and 400000-baud USART1 support.
- Assess USB HID plus CDC memory use separately.
- Verify actual Geehy peripheral behaviour later, with explicit flash approval.

The probe reads four ADC inputs into RAM. It does not transmit CRSF, enable
RF deliberately, write flash, or expose a configuration interface.
ADC pin order is provisional: measured CDC field order does not by itself
prove the physical ADC channel order.

First build result: 17680 bytes flash / 3216 bytes RAM for the basic probe;
48776 bytes flash / 14104 bytes RAM with USB HID and CDC class initialization.
Both fit. See `docs/ZEPHYR.md` for exact versions, commands, hashes, and limits.
The optional USB probe has placeholder IDs and sends no joystick reports.

## Build

Requires a Zephyr workspace, its Python dependencies, CMake, Ninja, and an
ARM toolchain. `west` is Zephyr's workspace and build command-line tool.
From an environment configured for that workspace:

```powershell
west build --no-sysbuild -b openliteradio -d build -- -DBOARD_ROOT=. -DZEPHYR_BASE=<zephyr-directory>
python tools/check_image.py build/zephyr/zephyr.bin --elf build/zephyr/zephyr.elf
```

Devicetree (`boards/`) describes the hardware; Kconfig (`prj.conf`) selects
compiled features. Application logic is C. Do not run `west flash`: the board
intentionally provides no flashing runner. Keep the vendor USB bootloader.

## Evidence and limits

See `docs/HARDWARE.md` and `docs/ZEPHYR.md`. A successful STM32-compatible
build does not establish APM32 compatibility or working USB, ADC, or timing.
No vendor binaries, personal settings, or device dumps belong in this repo.
