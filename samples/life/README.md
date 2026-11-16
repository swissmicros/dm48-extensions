<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright © 2026 SwissMicros GmbH -->
# life: Conway's Game of Life on the whole screen

`LIFE ( n → gens ms )` fills the 400 × 240 screen with a random start, `n` % of the cells alive (`n` from 0 to 100),
and then runs Conway's Game of Life, showing every generation with `dx_show`. The edges wrap round: a glider that
leaves at the right comes back at the left. At the top left it shows the frames per second so far, in a 3 × 5 digit
font drawn at twice the size.

| Key | What it does |
| --- | --- |
| ← (backspace, `KEY` 55) | Stops, and gives the number of generations and the time in ms. |
| `+` (95) / `−` (85) | The density 5 % more or less (from 5 % to 95 %), and a new random start. |
| any other key | A glider at a random place. |
| ON | Abandons the run: `LIFE` returns -1 and pushes nothing, as `dx_poll` requires. |

There is no EXIT key for a clean stop: on the DM48, EXIT is ON, which breaks the program, so ← does that job.

It shows how to:

- keep two frames in `.bss` (12 000 bytes each, far too big for the 2 KB stack) and swap them each generation;
- compute 8 cells at a time: the frame's bytes are used as they are (HP 48 GROB rows, the lowest bit leftmost), and
  the neighbours of 8 cells are counted with bitwise full adders;
- draw over the picture without changing the board: the cells under the fps label are saved, and put back after
  `dx_show`;
- read keys with `dx_key` and stop on ON with `dx_poll`, once per generation. An ON that `dx_key` meets in the key
  buffer first is not lost: the next `dx_poll` reports it.

The random start comes from an xorshift generator seeded from `dx_ms`, so each run is different.

## Build and load

```sh
make            # life.ext, with the web tester's WebAssembly build inside when the Emscripten SDK is installed
```

- **On the calculator:** copy `life.ext` to `/DM48/EXT/` and choose it once in SETUP → Programs → Extensions….
  Copy `LIFE.TXT` to `/DM48/` as `LDEMO.TXT` and load it with SETUP → Programs → Load program…, which stores it as the
  variable `LDEMO`, named after the file. (Under the name `LIFE` it would still run from the VAR menu, but a typed
  `LIFE` runs the command, not the variable.)
- **In the web tester:** choose `life.ext` in SETUP → Programs → Extensions…, and `LIFE.TXT` in SETUP → Programs →
  Load program…, with a name like `LDEMO` in the name field. The keys reach `dx_key` only when the page is
  cross-origin isolated (it has `SharedArrayBuffer`). Without it `dx_key` always returns 0: the board runs, but only
  ON stops it, and then there are no results.

## Expected output

`LIFE.TXT` runs `25 LIFE`. Press ← to stop it; it then shows the results, tagged:

```
3: :gens: 299
2: :ms: 19
1: :fps: 15736.8
```

**What is measured:** `ms` is the time from the first generation to the stop, read with `dx_ms`, including the new
starts that `+` and `−` make. `fps` is the number of generations divided by that time, so it is measured over the
whole run, not over one frame. It includes everything in the loop: computing the generation, `dx_show` (on the
calculator, the transfer to the LCD), and reading the keys. The label at the top left shows the same figure, for the
run so far; it shows at most 9999.

**The web tester's times are not the calculator's.** There `LIFE` runs as WebAssembly on your computer, which is much
faster than the calculator (the figures above come from the web tester's WebAssembly core). Compare times only
between runs on the same platform.
