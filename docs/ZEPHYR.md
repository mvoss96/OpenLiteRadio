# Zephyr feasibility

The initial probe uses an installed Nordic Zephyr workspace, not a claim of
upstream APM32 support. No hardware test was performed by this milestone.

Acceptance criteria:

1. ADC/GPIO/UART application links within 96 KiB flash and 20 KiB RAM.
2. Binary begins with Cortex-M vectors at 0x08008000; no MCUboot header.
3. ELF contains no loadable flash segment below that address.
4. A separate USB HID + CDC build measures the remaining memory budget.
5. Explicitly separate compile results from future Geehy/USB hardware tests.

Do not conclude that FreeRTOS is required merely from default Zephyr sample
sizes; compare a deliberately small configuration with the required drivers.

## Build results, 2026-11-16

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

## Exact environment

- Installed NCS v3.4.1, Zephyr VERSION 4.4.2, Git
  33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6.
- Zephyr SDK 1.0.1, GNU ARM compiler 14.3.0, west 1.5.0.
- hal_stm32 fc11896dd39cfca37bf9b4aeaaa2df8861b81875, the revision in this
  Zephyr checkout's west.yml; downloaded into ignored .deps/hal_stm32.
- CMSIS 6 module 30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74 from that workspace.

The Nordic distribution omits hal_stm32 and warns that STM32 is unsupported
by that distribution. Its Zephyr core plus the matching upstream STM32 module
nevertheless compiled both variants. This is not an upstream support claim.
Before adopting a release baseline, reproduce with a pinned upstream Zephyr
workspace rather than depending permanently on the Nordic installation.

For the installed environment, prepare the missing module inside this repo:

```powershell
git init .deps/hal_stm32
git -C .deps/hal_stm32 remote add origin https://github.com/zephyrproject-rtos/hal_stm32.git
git -C .deps/hal_stm32 fetch --depth 1 origin fc11896dd39cfca37bf9b4aeaaa2df8861b81875
git -C .deps/hal_stm32 checkout --detach FETCH_HEAD
./build.ps1 -ZephyrBase C:/ncs/v3.4.1/zephyr -ToolchainRoot C:/ncs/toolchains/4f5b6ad6dd
./build.ps1 -ZephyrBase C:/ncs/v3.4.1/zephyr -ToolchainRoot C:/ncs/toolchains/4f5b6ad6dd -Usb
```

build.ps1 sets process-local tool paths and invokes west with sysbuild disabled;
it does not install software, change the SDK, or invoke a flash command.
The Windows sandbox blocked dtc's signal pipe; both successful compilations
used an approved execution outside that restriction.

Build warnings retained: Nordic support warning, long generated object paths,
and PRINTK selected by another feature despite CONFIG_PRINTK=n. UART console
is disabled in both builds, so CRSF UART is not used as a logging console.

## Hardware gates before functional firmware

1. Verify vendor bootloader handoff, interrupt state, clock reinitialization,
   watchdog and power latch with a deliberately limited, approved test.
2. Verify ADC readings and physical input order against saved calibration.
3. Verify CRSF electrical timing and a throttle-low startup policy.
4. USB: resolve board switching/pull-up and clock accuracy. The compile probe
   uses HSI/PLL at 48 MHz; successful compilation does not prove a clock good
   enough for USB. IDs 0xffff:0xffff are placeholders, not a release identity.
5. Verify restored stock operation through the preserved USB updater. Check its
   accepted application size before using images larger than the vendor file.

No vendor settings are written by either probe. Do not infer a working joystick
from the USB build: its classes initialize, but it sends no axis reports.
