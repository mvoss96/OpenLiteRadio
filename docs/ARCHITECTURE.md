# Firmware architecture

Agreed on 2026-11-17. The scope is in [FEATURES.md](FEATURES.md), the hardware
in [WIRING.md](WIRING.md).

## Constraints

- 20 KiB RAM. Zephyr with the USB serial port takes about 14 KiB, which leaves
  about 6 KiB for application data and thread stacks.
- The ELRS module sets the frame rate: 250 Hz by default, up to 1000 Hz. The
  path from stick to CRSF frame has to keep that rate.
- C++20 without exceptions, RTTI, heap or C++ standard library: fixed arrays
  and plain classes.

## Layers

| Layer | Directory | Content | Touches hardware |
|-------|-----------|---------|------------------|
| board | `src/board/` | sticks, switches, buttons, trim keys, shift register, status LED, buzzer, power latch, battery input, module enable | yes, and only this layer |
| core | `src/core/` | calibration, dead zone, channel assignment, trim, CRSF encoding and decoding, parameter access, action table, settings model | no |
| app | `src/` | operating modes, threads, host protocol | through board only |

The app layer is one file per task: `output.cpp` (control thread, holds,
throttle lock, test inputs), `module_link.cpp` (frames from the module, link
statistics, binding), `host.cpp` (line protocol and state lines),
`storage.cpp` (settings and profiles in flash), `calibration.cpp` (stick
calibration run), `indicators.cpp` (buzzer and status LED), `usb_device.c`
(USB serial port and joystick) and `main.cpp` (start, power button, BIND and
SETUP, the main loop). Only `output.cpp` touches the state shared between the
two threads; the others go through its functions.

The core layer contains everything where an error is dangerous or hard to see
on the radio. Because it touches no hardware it is also compiled for the PC and
checked there with fixed inputs.

## Threads

- Control thread, high priority, at the module's frame rate: read sticks and
  switches, compute channels, send the CRSF frame and the joystick report.
- Main thread, low priority, 50 Hz: buttons and actions, LED and buzzer
  patterns, battery, power, CRSF parameters, host protocol.
- Bytes from the module arrive by interrupt in a ring buffer and are parsed in
  the main thread.

Each further thread would cost about 1 KiB of stack.

## Decisions

- Tests for the core layer run on the PC.
- Settings use Zephyr's NVS as long as flash allows, in the four 1 KiB pages
  at the top of the application area (0x0801F000 to 0x08020000); the image
  is limited to the 92 KiB below them. The vendor updater erases only as many pages as the
  image needs, so settings survive a firmware update. Flash below 0x08008000
  is not touched. The factory calibration there is not used: it does not fit
  the stick readings (docs/HARDWARE.md).
- The radio knows its settings: `src/core/settings.cpp` lists key, range and
  default of each one and reports them on request. The configurator adds
  labels and layout only, and shows a setting it has no label for on its
  Advanced tab. Each setting is a 16-bit value with its own NVS id; only
  values that differ from the default are stored, so settings added later do
  not disturb stored ones.
- The configurator is `web/index.html` with `web/app.js`: plain HTML and
  JavaScript without a build step, opened from the file system. Its main view
  edits the assignment from the control's side ("SE sends on channel 9");
  the page translates that into the per-channel `ch.N.source` settings. A
  control can therefore sit on several channels, and a channel can follow
  nothing.
- The USB serial port is always available for configuration and update when a
  cable is plugged in, also while the radio is switched on.
- The status LED is driven by TIM2 with DMA, not by timed loops with
  interrupts masked, so that no CRSF byte is lost while a colour is sent.

## Stages

| Stage | Result | State |
|-------|--------|-------|
| 1 | board layer, power on/off, LED, buzzer, host protocol with live view | built, see below |
| 2 | flying: factory calibration, channels, CRSF at the module's rate, throttle lock, binding | in progress, see below |
| 3 | settings storage, trim, calibration on the radio | storage and calibration built, trim open |
| 4 | USB joystick, then Xbox mode | joystick built (DirectInput, beside the serial port), Xbox open |
| 5 | configuration protocol for the web tool | settings commands and the configurator's views built; ELRS parameters, buttons, USB modes, update and backup open |
| 6 | Bluetooth, WiFi, battery display, remaining features | open |

Stage 1 as built: the power button switches the radio on after 1 s and off
after 2 s. The status LED is blue while the radio is on.

Stage 2 as built so far:

- `src/core/channels.cpp` scales the sticks with the calibration in the
  settings `cal.N.*`. Each of the 16 channels follows the source in
  `ch.N.source` (0 none, 1 aileron, 2 elevator, 3 throttle, 4 rudder, 5 to 10
  SA to SF), scaled around the centre by `ch.N.weight` percent and reversed
  with `ch.N.reverse`. By default channels 1 to 10 follow sources 1 to 10.
  Self-centring axes have a dead zone around the centre, set per axis in
  `cal.N.deadzone` as a percentage of the half range (0 to 10, default 1);
  inside it the output is exactly 1500. The throttle has none.
- `src/core/crsf.cpp` builds channel frames and parses what the module sends.
- The control thread sends one channel frame per frame period while the
  module is on. The period starts at 4 ms and follows the interval in the
  module's timing frames. The offset in those frames says how early or late
  the last frame arrived; one frame period is stretched or shortened by it,
  by at most a quarter of the interval.
- Throttle lock: the radio starts locked. After every start of the module
  and after every changed setting no frame is sent until the unlock gesture
  in `unlock.mode` was made: 0 none, 1 throttle at its lower end (default),
  2 throttle at its upper and then its lower end. The status LED is red while
  locked; one tone marks the release unless `unlock.beep` is 0. `buzzer.on`
  0 silences all tones.
- Switching the radio on starts the module, switching it off stops it.
- Binding: BIND held for 1 s, or the host command `bind`. The firmware asks
  the module for its number of parameters, reads the entries until one is a
  command named "Bind", and starts it by writing 1. The parameter number is
  kept until the next reset. A high tone reports that the command was sent, a
  low one that the search failed.
- Stick calibration, taken from stage 3 because the factory calibration does
  not fit the readings (docs/HARDWARE.md): SETUP held for 2 s starts a run,
  the status LED turns yellow and no channel frames are sent. The sticks are
  moved to all ends. A short press of SETUP ends the run with the right stick
  released and the throttle left at its lower end. The released readings
  become the centres and the throttle's lower end, the other ends are the
  extremes moved inward by 25 counts. A high tone reports that the result is
  in the settings `cal.N.*` and all settings are stored, a low tone that it
  was rejected or not stored. Without a run the built-in values 600, 2048
  and 3500 apply.
- Missing from stage 2: the other module parameters (packet rate, power,
  telemetry ratio), averaging of the stick readings.

## Tests

`./test.ps1` compiles `src/core/` and `tests/core_tests.cpp` with the PC's
g++ and runs the result. The tests use fixed inputs, among them a timing frame
recorded from the module.

## Host protocol

Text lines on the USB serial port, one message per line, fields separated by
spaces. It is readable in a terminal and parsed by `web/app.js`. Opening
the port at 1200 baud restarts the radio into the vendor USB updater.

From the radio:

- `hello OpenLiteRadio <version> proto=1`, at start and on request.
- `state key=value ...`, 25 times per second while streaming. Keys: `sticks`
  (four raw 12-bit values: left vertical, left horizontal, right vertical,
  right horizontal), `sa`, `sd` (0 or 1), `sb`, `sc` (0, 1, 2), `se`, `sf`,
  `bind`, `setup`, `power` (1 while pressed), `tl`, `tr` (trim buttons: bit 0
  up, 1 down, 2 left, 3 right, 4 centre), `keys` (1 if the trim key device
  answers), `battery` (raw 12-bit), `usb` (1 with USB supply), `on`, `module`,
  `ch` (channels 1 to 16 in microseconds, 988 to 2012), `lock` (1 while the
  throttle lock holds the output back), `period` (frame period in
  microseconds), `frames` and `crc` (frames received from the module with a
  correct and with a wrong checksum), `lq` (percent) and `rssi` (dBm without
  the sign) from the receiver's link statistics, `offset` (microseconds, from
  the last timing frame), `bindpar` (number of the module's bind parameter, 0
  until it was searched), `calib` (1 during a calibration run).
- `setting KEY TYPE MIN MAX DEFAULT VALUE`, one line per setting after `list`,
  followed by `end`. TYPE is `bool` or `int`.
- `value KEY VALUE` after `get`; `ok ...` or `error REASON` after the other
  settings commands.

To the radio:

| Line | Effect |
|------|--------|
| `hello` | send the hello line |
| `stream 0` / `stream 1` | stop or start the state lines |
| `led R G B` | status LED colour, 0 to 255 each; `led auto` returns control to the firmware |
| `beep HZ MS` | tone |
| `batleds MASK` | battery LEDs, bit 0 is the leftmost |
| `bind` | start binding, as holding BIND for 1 s does |
| `module 0` / `module 1` | ELRS module off or on; channel frames follow once the throttle lock is released |
| `list` | report all settings |
| `get KEY` | report one setting |
| `set KEY VALUE` | change a setting; answers `ok KEY VALUE`. It applies at once, locks the output and is lost at power-off unless stored |
| `save` | store all settings |
| `revert` | return to the stored settings |
| `defaults` | set all settings except the stick calibration (`cal.N.*`) to their defaults without storing them |
| `calibrate start` / `calibrate end` | stick calibration run, as SETUP on the radio |
| `calibrate cancel` | end the run and keep the previous calibration |
| `profiles` | report `profile N NAME` for each used slot, then `active N` (0: none) |
| `profile save N NAME` | store the current `ch.N.*` settings as profile N (1 to 4) and make it the active one |
| `profile load N` | put profile N into the `ch.N.*` settings, store all settings and make it the active one |
| `profile name N NAME` | rename profile N |
| `profile delete N` | remove profile N |
| `profile none` | make no profile the active one, e.g. after the configurator wrote a template |
| `inject KEY=VALUE ...` | test inputs: replace the readings of `sticks` (four raw values, comma-separated) and `sa` to `sf`, with the keys of the state line. Keys add up over several lines; the replacement ends 10 s after the last `inject` line, with `inject off`, or when the ELRS module starts. Refused with `error module on` while the module is on |

Profiles (`src/core/profiles.cpp`) are saved channel assignments: a name of 1
to 15 bytes of UTF-8 and source, weight and reverse of all 16 channels, one
NVS record per slot (ids 0x400 to 0x403) plus the active slot (0x410). The
running assignment is always the settings `ch.N.*`; a profile is a copy that
`profile load` puts back. Templates such as AETR are not stored on the radio;
the configurator writes their values with `set`.

The protocol number in the hello line changes when a message changes meaning.
New keys and new commands do not change it; a reader ignores what it does not
know.
