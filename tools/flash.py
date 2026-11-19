"""Load an application image onto the LiteRadio 4 through the vendor USB updater.

If the controller runs firmware built with CONFIG_UPDATER_ENTRY, its USB
console is opened at 1200 baud, which makes the firmware restart into the
updater. If the updater is already present (entered with SETUP + power), the
image is sent directly.

Updater protocol, as used by the BETAFPV Configurator:

    host -> "update\\r\\n"
    dev  -> 'C' (0x43), repeated while it waits
    host -> 133-byte header: 0x01 0x00 0xFF, "Bootloader_F.bin\\0" at offset 3,
            file size as ASCII decimal at offset 20, CRC16 of bytes 3..130
    dev  -> 0x06, then erases the application area
    host -> per block n = 1, 2, ...: 0x02, n, ~n, 1024 data bytes, CRC16
    dev  -> 0x06
    host -> 0x04
    dev  -> 0x15 when the written image has been verified
    host -> 133-byte closing packet: 0x01 0x00 0xFF, rest zero

CRC is CRC-16/XMODEM, sent high byte first. The updater only writes the
application area from 0x08008000.
"""

import argparse
import hashlib
import sys
import time
from pathlib import Path

import serial
from serial.tools import list_ports

UPDATER_IDS = ((0x0483, 0x5740),)
# Firmware from 0.7.0 on is a serial port plus joystick (PID 0x0005); older ones were 0x0004.
CONSOLE_IDS = ((0x2FE3, 0x0005), (0x2FE3, 0x0004))
UPDATE_REQUEST_BAUD = 1200

READY = 0x43
ACK = 0x06
VERIFIED = 0x15
TARGET_NAME = b"Bootloader_F.bin\x00"
BLOCK = 1024
MAX_IMAGE = 0x18000


def crc16_xmodem(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build_header(size):
    header = bytearray(133)
    header[0:3] = b"\x01\x00\xff"
    header[3:3 + len(TARGET_NAME)] = TARGET_NAME
    digits = str(size).encode("ascii")
    header[20:20 + len(digits)] = digits
    header[131:133] = crc16_xmodem(header[3:131]).to_bytes(2, "big")
    return bytes(header)


def build_block(number, payload):
    data = payload.ljust(BLOCK, b"\x00")
    return bytes([0x02, number & 0xFF, ~number & 0xFF]) + data + crc16_xmodem(data).to_bytes(2, "big")


def find_port(usb_ids):
    """Return the serial port name of a device with one of these (VID, PID) pairs, or None."""
    for port in list_ports.comports():
        if (port.vid, port.pid) in usb_ids:
            return port.device
    return None


def ports_with(usb_ids):
    """Return the names of all serial ports with one of these (VID, PID) pairs."""
    return {port.device for port in list_ports.comports() if (port.vid, port.pid) in usb_ids}


def wait_port(usb_ids, timeout, ignore=()):
    """Wait for a port with one of these IDs that is not in ignore."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        new = ports_with(usb_ids) - set(ignore)
        if new:
            return sorted(new)[0]
        time.sleep(0.2)
    return None


def open_port(device, baudrate, timeout=5.0):
    """Open a port, retrying while Windows lists it but cannot open it yet."""
    deadline = time.monotonic() + timeout
    while True:
        try:
            return serial.Serial(device, baudrate, timeout=0.05, write_timeout=2)
        except (serial.SerialException, OSError):
            if time.monotonic() > deadline:
                raise
            time.sleep(0.1)


def wait_byte(port, wanted, timeout):
    """Read until the wanted byte arrives; other bytes, such as 'C', are dropped."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if wanted in port.read(256):
            return True
    return False


def request_update_mode(console):
    print(f"{console}: requesting update mode")
    try:
        with open_port(console, UPDATE_REQUEST_BAUD):
            time.sleep(1.5)
    except (serial.SerialException, OSError):
        # The port disappears when the controller restarts.
        pass


def send_image(device, image):
    """Run the updater protocol; return an error text or None."""
    blocks = (len(image) + BLOCK - 1) // BLOCK
    with open_port(device, 115200) as port:
        port.reset_input_buffer()
        port.write(b"update\r\n")
        if not wait_byte(port, READY, 3):
            return "no answer to the update request"
        port.write(build_header(len(image)))
        if not wait_byte(port, ACK, 10):
            return "header not acknowledged"
        for number in range(1, blocks + 1):
            port.write(build_block(number, image[(number - 1) * BLOCK:number * BLOCK]))
            if not wait_byte(port, ACK, 10):
                return f"block {number} of {blocks} not acknowledged"
        port.write(b"\x04")
        if not wait_byte(port, VERIFIED, 20):
            return "image not verified"
        try:
            port.write(b"\x01\x00\xff".ljust(133, b"\x00"))
            port.flush()
        except (serial.SerialException, OSError):
            # Firmware with CONFIG_UPDATER_ENTRY resets as soon as the image is complete.
            pass
    return None


def main():
    parser = argparse.ArgumentParser(description="Load an application image onto the LiteRadio 4.")
    parser.add_argument("image", type=Path, help="application binary linked at 0x08008000")
    parser.add_argument("--port", help="port of a radio already in update mode, e.g. COM7")
    args = parser.parse_args()

    image = args.image.read_bytes()
    if not 8 <= len(image) <= MAX_IMAGE:
        sys.exit("image must fit into the 96 KiB application area")
    print(f"{args.image}: {len(image)} bytes, SHA-256 {hashlib.sha256(image).hexdigest()}")

    # Other STM32 devices, e.g. a Betaflight flight controller, use the updater's IDs
    # too. Only a port that appears after the update request is taken for the updater.
    present = ports_with(UPDATER_IDS)
    console = find_port(CONSOLE_IDS)
    if console is not None:
        request_update_mode(console)
        updater = wait_port(UPDATER_IDS, 15, ignore=present)
        if updater is None:
            if find_port(CONSOLE_IDS):
                sys.exit("the updater did not appear; is another program using the console port?")
            sys.exit("the updater did not appear")
        time.sleep(1.0)
    elif args.port:
        updater = args.port
    elif present:
        sys.exit(f"no controller console found; ports with the updater's IDs: {', '.join(sorted(present))}. "
                 "If the radio is in update mode, name its port with --port")
    else:
        sys.exit("no controller found; unplug USB, hold SETUP and power until "
                 "the double beep, then plug USB in")

    print(f"{updater}: sending image")
    error = send_image(updater, image)
    if error:
        sys.exit(error)
    print("image written and verified")

    if wait_port(CONSOLE_IDS, 10):
        print("the controller restarted into the new firmware")
    else:
        print("no USB console appeared; if the updater was entered with the buttons, "
              "unplug USB to start the firmware")


if __name__ == "__main__":
    main()
