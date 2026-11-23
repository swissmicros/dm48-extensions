<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright © 2026 SwissMicros GmbH -->
# Samples

One folder per sample extension. Each folder holds its C sources and a Makefile shaped like `template/Makefile`
(`make` builds `NAME.ext`), so CI can build it with `make -C samples/NAME all`.

A sample copied from `template/` sets `KIT = ../..` in its Makefile, since it sits two levels below the kit's root.

| Sample | What it shows |
| --- | --- |
| [bench](bench/) | `SIEVE`, `PIDIG` and `PISUM` against the same work in User RPL: integer and software-`double` speed. |
| [mandel](mandel/) | `MANDEL`: the Mandelbrot set at 400 × 240 in fixed point, shown band by band with `dx_show`, timed with `dx_ms`. |
| [life](life/) | `LIFE`: Conway's Game of Life on the whole screen, 8 cells at a time, with keys (`dx_key`) and an fps label. |
| [mine](mine/) | `MINE`: a minefield to cross, a small game driven by `dx_key`, with its own font and drawing. |
| [ptable](ptable/) | `PTABLE`, `ELEM` and `MOLMASS`: an interactive periodic table, element data as lists, and exact molar masses of formulas. A `const` table generated from a CSV, and host tests. |
| [filedemo](filedemo/) | `FWRITE`, `FREAD`, `FDEL` and five more: the calculator's files from RPL with `dx_file_*`. |
| [serdemo](serdemo/) | `USBSEND`, `USBRECV` and four more: lines to and from a PC over USB (`dx_serial_*`, with `tools/dm48serial.py`), and an HP 82240B printer by infrared (`dx_ir_send`). |
