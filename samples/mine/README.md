<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright © 2026 SwissMicros GmbH -->
# mine: cross the minefield

`MINE ( k → score )` is a small game at the DM48's full resolution. A field of 12 rows × 24 columns of 16-pixel
squares (384 × 192 pixels) sits under a 48-pixel band. `k` mines (1 to 120; 30 when the argument is not a real) are
hidden in it at random, never on the start (top left) or the goal (bottom right), and always with a way through:
a field where the mines wall off the goal is dealt again. You walk from the start to the
goal one square at a time, and you can't see the mines: the band only tells you how many are on the squares next to
you.

| Key | What it does |
| --- | --- |
| ▲ ▼ ◀ ▶ | Move one square straight. |
| 1–9 | Move like a numeric keypad: 8 up, 2 down, 4 left, 6 right, and 7, 9, 1 and 3 diagonally. 5 does nothing. |
| ← (backspace, `KEY` 55) | Stop, and give the score. |
| ON | Abandon: `MINE` returns -1 and pushes nothing, as `dx_poll` requires. |

The squares you have visited are drawn filled, and the top band shows the mines next to you (`NEAR`), the number of
mines, and your score: the number of squares visited, the start included. Reaching the goal wins (`GOAL!`). Stepping
on a mine loses (`BOOM!`) and shows every mine. Either way the last picture stays until you press a key, and then
`MINE` gives the score. There is no EXIT key for a clean stop: on the DM48, EXIT is ON, which breaks the program, so
← does that job.

It shows how to:

- write an interactive command: a loop that reads `dx_key`, changes the game, and draws and shows a new frame only
  when something has changed;
- draw text and pictures into your own frame in `.bss` (12 000 bytes, far too big for the 2 KB stack), with a small
  3 × 5 font of the sample's own;
- use the `KEY` numbers of the GUIDE's `dx_key` table.

The mines come from an xorshift generator seeded from `dx_ms`, so each game is different.

## Build and load

```sh
make            # mine.ext, with the web tester's WebAssembly build inside when the Emscripten SDK is installed
```

- **On the calculator:** copy `mine.ext` to `/DM48/EXT/` and choose it once in SETUP → Programs → Extensions….
  Copy `MINE.TXT` to `/DM48/` as `MGAME.TXT` and load it with SETUP → Programs → Load program…, which stores it as the
  variable `MGAME`, named after the file. (Under the name `MINE` it would still run from the VAR menu, but a typed
  `MINE` runs the command, not the variable.)
- **In the web tester:** choose `mine.ext` in SETUP → Programs → Extensions…, and `MINE.TXT` in SETUP → Programs →
  Load program…, with a name like `MGAME` in the name field. The keys reach `dx_key` only when the page is
  cross-origin isolated (it has `SharedArrayBuffer`). Without it `dx_key` always returns 0: you can't move, and only
  ON leaves the game, with no score.

## Expected output

`MINE.TXT` runs `30 MINE` and tags the score. After a game that reached the goal along the shortest path:

```
1: :score: 24
```

`MINE` waits for keys at full speed, calling `dx_poll` and `dx_key` in a loop, because the kit has no call that
sleeps until a key comes. On the calculator that keeps the processor busy (and the calculator awake) for as long as
the game is open, so don't leave it running.

`MINE` measures no time: it waits for your keys, so its speed is that of your fingers, on the calculator and in the
web tester alike.
