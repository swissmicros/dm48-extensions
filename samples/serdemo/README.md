<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright © 2026 SwissMicros GmbH -->
# serdemo: a PC over USB, a printer by infrared

`SERDEMO` sends lines of text to a PC over the USB cable and reads lines back (`dx_serial_*`), and prints on an
HP 82240B infrared printer (`dx_ir_send`).

| Command | Stack | What it does |
| --- | --- | --- |
| `USBCON` | `→ 1/0` | 1 when a PC program has the calculator's serial port open. |
| `USBSEND` | `"text" →` | Sends the text, one line per line of it; a line longer than 127 characters goes out in pieces. |
| `USBRECV` | `→ "line"` | The next line the PC sent, `""` when there is none. |
| `USBWAIT` | `ms → "line"` | Waits up to `ms` milliseconds for a line, `""` when none came. ON stops it. |
| `IRSEND` | `"bytes" →` | Sends the string's bytes to the printer as they are (82240B codes). |
| `IRPRINT` | `"text" →` | Prints the text as one line. |

**USB:** turn on the USB console on the calculator (SETUP > Settings > USB console: ON), then on the PC run the kit's `tools/dm48serial.py PORT`
(it needs `pip install pyserial`, and only one program can have the port open; the port is e.g. `/dev/ttyACM0`, `COM5` or `/dev/cu.usbmodem1101`). It prints what the
calculator sends, and sends every line you type. Without the console, or with no program on the PC, the commands
give `Not connected`. Lines are printable ASCII (and tab).

**Infrared:** the printer path is the calculator's own (PRTPAR's delay paces the lines). There is no other control of
the IR LED.
