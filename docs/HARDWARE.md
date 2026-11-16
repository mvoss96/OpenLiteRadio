# Controller hardware and module contract

Evidence source: the parent investigation's analysis/CONTROLLER.md, especially
section 10a (SWD readback, 2026-11-16). Earlier sections contain superseded claims.

## Observed

- Geehy APM32F103CBT6; 128 KiB flash, 20 KiB RAM.
- Vendor application vectors at 0x08008000; stock file length 0xE530.
- Stock application SHA-256:
  e0e9f86d009c7f5730d9bff94edff6b6bb1ada61475ca14723ede9f777be75ff.
- Full SWD readback matches the vendor application byte-for-byte.
- Below 0x08008000: preserve bootloader and settings, including channel records
  at 0x08007000 and calibration at 0x08007800. 0x08007466 was erased in the dump;
  this does not prove that no firmware path can ever use it.
- Calibration contains descending endpoints and centre in 12-bit ADC units.
- CDC fields identify AETR physically; the throttle does not self-centre.
- The application works with erased flash above 0x08016530. Earlier claims
  about required compressed initialization data are superseded.
- Vendor USB updater recovered the application after an experimental dumper.

## Common contract with the ELRS target repository

- ESP32 module hardware JSON stays identical for stock and custom controllers.
- Controller USART1 uses PA9 TX / PA10 RX at 400000 baud (8N1 to verify).
- CRSF channel frame: address 0xEE, length 0x18, type 0x16, 16 packed 11-bit
  channels, CRC8 DVB-S2 over type and payload; 26 bytes total.
- CRSF centre 992; nominal endpoints approximately 172..1811. Stock conversion
  is (internal - 1000) * 1.6 + 192.
- Stock parameter client discovers names rather than relying on fixed indices.
- A custom controller needs explicit throttle safety, suitable frame timing,
  receive parsing, and deliberate policy for module power/settings.
- Do not advertise EdgeTX passthrough merely because the target lists `etx`.

## Unverified in this implementation

STM32 driver compatibility on Geehy, ADC pin-to-stick mapping, clock transitions
from the bootloader, watchdog state, power latch, USB switching/pull-up,
switch/button pins, calibrated voltage monitoring, CRSF timing and USB runtime.
No settings writes are enabled during feasibility work. The stock USB updater
must be checked for maximum accepted image size before loading a larger image.
