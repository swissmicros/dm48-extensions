<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright © 2026 SwissMicros GmbH -->
# mandel: the Mandelbrot set on the whole screen

`MANDEL ( → grob ms )` draws the Mandelbrot set at the DM48's full resolution, 400 × 240 pixels, with 64 iterations,
for x from −2.5 to 1 and y from −1.05 to 1.05. It shows the picture as it goes, after every band of 8 rows, with
`dx_show`. At the end it gives the picture as a GROB and the time it took in milliseconds. Points in the set are
black; the others are shaded by how slowly they escape, with a 4 × 4 ordered dither.

It shows how to:

- draw into your own frame in `.bss` (12 000 bytes, far too big for the 2 KB stack) and show it with `dx_show`, which
  uses no calculator memory however often you call it;
- time a command with `dx_ms`;
- stop on ON: `dx_poll` returns 1, and the command returns -1 at once and pushes nothing. `MANDEL` calls it after
  every row, so ON stops it within a few milliseconds.

**Fixed point, not `double`.** The DM48's FPU is single precision only, so a `double` in C is software arithmetic
(libgcc), many times slower, and a `float` has too few digits for the set's detail. `MANDEL` iterates in 32-bit fixed
point, Q4.27: 4 integer bits with the sign, then 27 fraction bits. A product of two such numbers is one 32 × 32 → 64-bit
multiply and a shift.

## Build and load

```sh
make            # mandel.ext, with the web tester's WebAssembly build inside when the Emscripten SDK is installed
```

- **On the calculator:** copy `mandel.ext` to `/DM48/EXT/` and choose it once in SETUP → Programs → Extensions….
  Copy `MANDEL.TXT` to `/DM48/` as `MDEMO.TXT` and load it with SETUP → Programs → Load program…, which stores it as
  the variable `MDEMO`, named after the file. Under the name `MANDEL` it would still run from the VAR menu, but a
  typed `MANDEL` runs the command, not the variable.
- **In the web tester:** choose `mandel.ext` in SETUP → Programs → Extensions…, and `MANDEL.TXT` in SETUP → Programs
  → Load program…, and give the variable a name like `MDEMO` in the name field. `MANDEL` reads no keys, so it does not need the page to be
  cross-origin isolated (`dx_key` does; ON works either way).

## Expected output

`MANDEL.TXT` runs `MANDEL`, tags the time `:ms:`, stores the picture in `PICT` and shows it with `{ } PVIEW`. When
you leave the picture, the time is on the stack:

```
1: :ms: 5
```

**What is measured:** the time is the difference of two `dx_ms` readings, from the start of the drawing to the end of
the last band. It includes the 30 calls of `dx_show`, so on the calculator it includes the transfers to the LCD.

**The web tester's times are not the calculator's.** There `MANDEL` runs as WebAssembly on your computer, which is much
faster than the calculator (the 5 ms above was measured in the web tester). Compare times only between runs on the
same platform.
