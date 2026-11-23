"""Load an application image onto the LiteRadio 4 through the vendor USB updater.

If the controller runs firmware built with CONFIG_UPDATER_ENTRY, its USB
console is opened at 1200 baud, which makes the firmware restart into the
updater while the radio is switched off (ELRS module off). If the updater is already present (entered with SETUP + power), the
image is sent directly.

The image goes only to the updater port whose USB serial number belongs to the
radio: the tool reads the MCU's unique ID from the firmware's hello line first.
Other STM32 devices, such as a Betaflight flight controller, have the updater's
VID and PID too.

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
import re
import sys
import time
from pathlib import Path

import serial
from serial.tools import list_ports

UPDATER_IDS = ((0x0483, 0x5740),)
# The firmware's serial port in each form of its USB device: serial port only (radio on),
# with the joystick, with the Xbox controller (src/usb_device.c).
CONSOLE_IDS = ((0x2FE3, 0x0004), (0x2FE3, 0x0005), (0x2FE3, 0x0007))
UPDATE_REQUEST_BAUD = 1200
# Serial number of the virtual port serbridge (a serial-port proxy) offers with the radio's IDs.
SERBRIDGE_SERIAL = "SERBRIDGE0"

READY = 0x43
ACK = 0x06
VERIFIED = 0x15
TARGET_NAME = b"Bootloader_F.bin\x00"
BLOCK = 1024
# The image must end below the settings pages at 0x0801F000.
MAX_IMAGE = 0x17000


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


def device_ports():
    """Return the serial ports of real devices, without serbridge's virtual port with the radio's IDs."""
    return [port for port in list_ports.comports() if port.serial_number != SERBRIDGE_SERIAL]


def find_port(usb_ids):
    """Return the serial port name of a device with one of these (VID, PID) pairs, or None."""
    for port in device_ports():
        if (port.vid, port.pid) in usb_ids:
            return port.device
    return None


def ports_with(usb_ids):
    """Return the names of all serial ports with one of these (VID, PID) pairs."""
    return {port.device for port in device_ports() if (port.vid, port.pid) in usb_ids}


def wait_port(usb_ids, timeout, serial_number=None):
    """Wait for a port with one of these IDs and, if given, this USB serial number."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for port in device_ports():
            if (port.vid, port.pid) in usb_ids and (
                    serial_number is None or (port.serial_number or "").upper() == serial_number):
                return port.device
        time.sleep(0.2)
    return None


def radio_uid(console):
    """Return the MCU's 96-bit unique ID as three words from the firmware's hello line, or None."""
    with open_port(console, 115200) as port:
        port.reset_input_buffer()
        port.write(b"hello\n")
        deadline = time.monotonic() + 2
        text = b""
        while time.monotonic() < deadline:
            text += port.read(4096)
            match = re.search(rb"hello OpenLiteRadio \S+ proto=\d+ id=([0-9a-fA-F]{24})", text)
            if match:
                digits = match.group(1).decode()
                return [int(digits[i:i + 8], 16) for i in (0, 8, 16)]
    return None


def updater_serial(uid):
    """Return the USB serial number the vendor updater reports for this unique ID.

    ST's USB device library builds it from (UID0 + UID2) as 8 hex digits and
    the upper half of UID1 as 4 hex digits; the ID 123456789ABC000111111111,
    for example, gives 234567899ABC.
    """
    return f"{(uid[0] + uid[2]) & 0xFFFFFFFF:08X}{uid[1] >> 16:04X}"


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
        sys.exit("image must fit into the 92 KiB below the settings pages")
    print(f"{args.image}: {len(image)} bytes, SHA-256 {hashlib.sha256(image).hexdigest()}")

    # Other STM32 devices, e.g. a Betaflight flight controller, use the updater's IDs
    # too. Only the port with the serial number derived from this radio's unique ID is
    # taken for the updater.
    present = ports_with(UPDATER_IDS)
    console = find_port(CONSOLE_IDS)
    if console is not None:
        uid = radio_uid(console)
        if uid is None:
            sys.exit(f"{console}: no hello line with the radio's ID; close other programs using the console port")
        expected = updater_serial(uid)
        request_update_mode(console)
        updater = wait_port(UPDATER_IDS, 15, serial_number=expected)
        if updater is None:
            if find_port(CONSOLE_IDS):
                sys.exit("the updater did not appear; switch the radio off (the firmware ignores the "
                         "request while the ELRS module is on) and close other programs using the console port")
            sys.exit(f"the updater with serial number {expected} did not appear")
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
