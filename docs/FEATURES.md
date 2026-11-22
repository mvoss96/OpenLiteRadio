# Open features

What the firmware does is in the [README](../README.md) and in
[ARCHITECTURE.md](ARCHITECTURE.md). This file lists what is missing. The
reference is the stock firmware 1.1.3: OpenLiteRadio is meant to do nothing
less, and a few things more.

## Not built yet

- Stick mode selection (mode 2 is fixed).
- Response curve per stick (expo); the stock firmware has a 512-entry table.
- Dual rates and free mixes beyond the mapping.
- Export and import of the mapping as a file.
- Averaging of the stick readings; the stock firmware averages 5 samples.
- Telemetry warnings (link quality, receiver voltage).
- Inactivity alarm and automatic power-off.
- Tones for key presses and before the automatic power-off.
- Charge indication on the status LED while the radio is off; the charge
  shows on the battery LEDs only.
- Configurable button and switch actions.
- A fixed button combination at power-on that ignores all own settings.
- PC control: a program on the PC moves virtual sticks and switches over the
  serial port, sent to the model through the ELRS module. Allowed only at the
  radio; see "Operating modes" in ARCHITECTURE.md.

## Built, not yet tested on a radio

- Flying. The radio has controlled a Betaflight quad on the bench only.
- The weak-battery warning on a discharged battery.
- The link quality on the battery LEDs.
- The Xbox controller in a game.

## Dropped

- Several model memories: without a display the radio cannot show which one
  is active. There is one Custom mapping beside the fixed AETR and TAER.
- The factory calibration at 0x08007800 as default: its values do not fit the
  stick readings (HARDWARE.md).

## Not planned

- Own RF stack or changes to the ELRS module firmware.
- XInput over Bluetooth; the stock firmware does not offer it either.
- Replacing the vendor bootloader.
- Compatibility with the BETAFPV Configurator.
