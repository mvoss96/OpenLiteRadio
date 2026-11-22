# OpenLiteRadio

Open firmware for the BETAFPV LiteRadio 4, set up in the browser.

**Configurator: <https://mvoss96.github.io/OpenLiteRadio/>** (Chrome or Edge,
in English and German)

**Experimental:** tested on the bench with a Betaflight quad, not flown yet.

![The configurator with a radio connected](docs/configurator.png)

## Features

- Sticks with calibration and dead zone, all switches, trim
- Channel order AETR, TAER or your own, with weight and reverse
- ExpressLRS: binding, WiFi, Bluetooth joystick, all module settings
- Throttle lock at power-on
- USB joystick or Xbox 360 controller for simulators
- Tones and volume, battery level, link and low-battery warnings
- Firmware update from the browser

Details and test status: [docs/FEATURES.md](docs/FEATURES.md).

## Install

1. Open the [configurator](https://mvoss96.github.io/OpenLiteRadio/) and press
   "Load firmware".
2. Switch the radio off and unplug USB. Hold SETUP and switch it on until the
   double beep. Plug USB in.
3. Press "Next" and choose the updater in the browser's window.
4. Press "Load".
5. When it is done, unplug USB.
6. Calibrate the sticks: press "Connect", then "Calibrate sticks".

**Update:** switch the radio off, connect, then System → "Update" → "Load".

**Back to stock:** System → "Update" → "Other file", choose the
`LiteRadio_4_ELRS_…bin` file from
[BETAFPV's download page](https://support.betafpv.com/hc/en-us/articles/36869017436953-LiteRadio-4).

## Development

- [docs/BUILD.md](docs/BUILD.md): build, tests, command-line tools
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): firmware layers and the USB protocol
- [docs/WIRING.md](docs/WIRING.md), [docs/HARDWARE.md](docs/HARDWARE.md): pins and measurements
- [docs/ZEPHYR.md](docs/ZEPHYR.md): Zephyr setup
- [literadio4-elrs-target](https://github.com/mvoss96/literadio4-elrs-target): ELRS target for the module

## License

GPL-3.0, see [LICENSE](LICENSE).
