#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright © 2026 SwissMicros GmbH
"""Talk to a DM48 extension's USB serial line (dx_serial_*; GUIDE.md "USB serial and infrared").

    python3 dm48serial.py PORT            # e.g. /dev/ttyACM0, COM5, /dev/cu.usbmodem1101

The calculator's USB serial console (SETUP > Settings > USB console: ON) carries several channels in one byte
stream: a byte 0x10-0x1A selects the channel (0x10 + n) for the bytes after it, and a line ends at '\\n'. The
screen and keys are channel 0; an extension's lines are channel 2. This program prints every channel-2 line on
standard output and sends every line of standard input to the calculator as a channel-2 line. Needs pyserial
(pip install pyserial). Only one program can have the port open: close the screen-mirroring page first.
"""
import sys
import threading

CH_BASE, CH_COUNT, CH = 0x10, 11, 2
OVERFLOW = 0x0F


def frames(chunks):
    """The lines of channel CH in a byte stream (an iterable of bytes objects), as bytes without '\\n'."""
    ch, line = -1, bytearray()
    for chunk in chunks:
        for b in chunk:
            if CH_BASE <= b < CH_BASE + CH_COUNT:
                ch = b - CH_BASE
            elif b == 0x0A:
                if ch == CH:
                    yield bytes(line)
                line.clear()
            elif ch == CH:
                line.append(b)


def encode(text):
    """One line for the calculator: the channel id, printable ASCII and tab, '\\n'. ValueError otherwise."""
    data = text.rstrip('\r\n').encode('ascii')
    if any(not (b == 9 or 0x20 <= b <= 0x7E) for b in data):
        raise ValueError('a line is printable ASCII and tab')
    if len(data) > 255:
        raise ValueError('a line is at most 255 bytes')
    return bytes([CH_BASE + CH]) + data + b'\n'


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    import serial  # pyserial
    port = serial.Serial(argv[1], 115200, timeout=0.1)
    port.dtr = True  # the calculator sees a program connected

    def reader():
        def chunks():
            while True:
                b = port.read(4096)
                if b:
                    yield b
        for line in frames(chunks()):
            if line.endswith(bytes([OVERFLOW])):
                continue
            sys.stdout.write(line.decode('ascii', 'replace') + '\n')
            sys.stdout.flush()

    threading.Thread(target=reader, daemon=True).start()
    try:
        for text in sys.stdin:
            try:
                port.write(encode(text))
            except ValueError as e:
                print(f'dm48serial: not sent: {e}', file=sys.stderr)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
