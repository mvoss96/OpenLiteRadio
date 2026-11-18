"""Check the USB joystick against the channels, without touching the radio.

Sets stick and switch readings with the "inject" command (firmware 0.7.1 or
later, ELRS module off), then compares what Windows reads from the joystick
with the channel outputs in the state line:

    .venv/Scripts/python tools/joystick_check.py

Windows only; uses winmm through tools/joystick_test.py. winmm shows the
axes of channels 1, 2, 3, 6, 7 and 4 and all eight buttons.
"""
import ctypes
import sys
import time

import serial
from serial.tools import list_ports

import joystick_test as jt

PORT_IDS = ((0x2FE3, 0x0005),)
# winmm axes X, Y, Z, R, U, V carry these channels (1-based).
AXIS_CHANNELS = [1, 2, 3, 6, 7, 4]
TOLERANCE_US = 2
CASES = [
    ("sticks at rest", ["inject sticks=680,2048,2048,2048"]),
    ("sticks at the low ends", ["inject sticks=600,4000,100,100"]),
    ("sticks at the high ends", ["inject sticks=3600,100,4000,4000"]),
    ("SA, SD on; SB, SC up", ["inject sa=1 sd=1 sb=0 sc=0"]),
    ("SB, SC down; SE, SF pressed", ["inject sb=2 sc=2 se=1 sf=1"]),
    ("all switches off", ["inject sa=0 sd=0 sb=1 sc=1 se=0 sf=0"]),
]


def open_radio():
    for port in list_ports.comports():
        if (port.vid, port.pid) in PORT_IDS:
            return serial.Serial(port.device, 115200, timeout=0.05)
    sys.exit("radio not found (firmware 0.7.0 or later, 2FE3:0005)")


def last_state(port, seconds):
    """Read lines for a while; return the fields of the last state line and other lines."""
    state, other = None, []
    end = time.time() + seconds
    while time.time() < end:
        line = port.readline().decode("ascii", "replace").strip()
        if line.startswith("state "):
            fields = dict(pair.split("=", 1) for pair in line.split()[1:] if "=" in pair)
            if "calib" in fields:   # the last key; a line cut off when the port opened lacks it
                state = fields
        elif line:
            other.append(line)
    return state, other


def read_joystick(joy_id):
    info = jt.JOYINFOEX(dwSize=ctypes.sizeof(jt.JOYINFOEX), dwFlags=jt.JOY_RETURNALL)
    jt.winmm.joyGetPosEx(joy_id, ctypes.byref(info))
    axes = [info.dwXpos, info.dwYpos, info.dwZpos, info.dwRpos, info.dwUpos, info.dwVpos]
    return [round(988 + value / 65535 * 1024) for value in axes], info.dwButtons


def main():
    joy_id, caps = jt.find_joystick()
    if joy_id is None:
        sys.exit("no joystick found")
    port = open_radio()
    port.write(b"stream 1\n")
    state, _ = last_state(port, 0.5)
    if state and state.get("module") == "1":
        sys.exit("ELRS module is on; inject works only with the module off")
    read_joystick(joy_id)   # the first read after opening returns centred values

    failures = 0
    for name, commands in CASES:
        for command in commands:
            port.write((command + "\n").encode())
        state, other = last_state(port, 0.4)
        if any(line.startswith("error") for line in other):
            print(f"{name}: radio answered {other}")
            failures += 1
            continue
        channels = [int(v) for v in state["ch"].split(",")]
        axes, buttons = read_joystick(joy_id)
        expected_buttons = sum(1 << i for i in range(8) if channels[8 + i] > 1500)
        axis_ok = all(abs(axes[i] - channels[c - 1]) <= TOLERANCE_US for i, c in enumerate(AXIS_CHANNELS))
        ok = axis_ok and buttons == expected_buttons
        failures += not ok
        shown = " ".join(f"K{c}={channels[c - 1]}/{axes[i]}" for i, c in enumerate(AXIS_CHANNELS))
        print(f"{'ok  ' if ok else 'FAIL'} {name}: {shown}  Tasten {buttons:08b}/{expected_buttons:08b}"
              f"  (sa={state['sa']} sb={state['sb']} sc={state['sc']} sd={state['sd']} se={state['se']} sf={state['sf']})")
    port.write(b"inject off\n")
    print("all cases match" if failures == 0 else f"{failures} case(s) failed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
