<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright © 2026 SwissMicros GmbH -->
# bench: native commands against User RPL

Three commands that do the same work as small User RPL programs, so you can see how much faster C is on the DM48.

| Command | Stack | What it does |
| --- | --- | --- |
| `SIEVE` | `n → count` | The number of primes up to `n` (0 to 1 000 000), by the sieve of Eratosthenes. |
| `PIDIG` | `n → "3.14…"` | The first `n` digits of π (1 to 1000) as a string, by the Rabinowitz–Wagon spigot. |
| `PISUM` | `n → x` | `4 × (1 − 1/3 + 1/5 − …)`, `n` terms (0 to 10 000 000), in `double`. |

`SIEVE` and `PIDIG` are integer work. `PISUM` is `double` work, and that is on purpose: the DM48's FPU is single
precision only, so every `double` operation in C is a call into libgcc's software arithmetic. The integer and the
double timings are given separately so you can see the difference.

Both sieves, the native one and `BENCH`'s RPL one, keep a flag for each odd number only (2 is counted on its own) and
cross out the odd multiples of each odd prime, so they do the same work. The native one also works one segment of
32 768 odd numbers at a time, for memory, not for speed: a bitset for the whole range up to 1 000 000 would take
62.5 KB, and an extension has 64 KB in all, its 2 KB stack included. The two commands' buffers share the same `.bss`
(a `union`), because only one command runs at a time.

All three commands take a whole number; a fraction gives Bad Argument Value.

## Build and load

```sh
make            # bench.ext, with the web tester's WebAssembly build inside when the Emscripten SDK is installed
```

- **On the calculator:** copy `bench.ext` to `/DM48/EXT/` and choose it once in SETUP → Programs → Extensions….
  Copy `BENCH.TXT` to `/DM48/` and load it with SETUP → Programs → Load program…, which stores it as `BENCH`.
- **In the web tester:** choose `bench.ext` in SETUP → Programs → Extensions…, and `BENCH.TXT` in SETUP → Programs
  → Load program…. `bench` reads no keys, so it does not need the page to be cross-origin isolated (the
  other samples' `dx_key` does).

## Expected output

```
10000 SIEVE   →  1229
20 PIDIG      →  "3.1415926535897932384"
1000 PISUM    →  3.14059265384
```

`BENCH` runs each benchmark twice, once with the native command (`n = 10000` for `SIEVE` and `PISUM`, `n = 100` for
`PIDIG`) and once with the same work in User RPL. It leaves one list per benchmark:

```
{ "SIEVE 10000" 1229 :ms: 1.1 :ms: 526 :x: 478.2 }
```

That is the name, the native command's result, the native command's time, the RPL program's time, and how many
times faster the native command was. When the RPL program's result differs (for `PISUM`, by more than 1 part in 10⁸), it is added at the end, tagged
`:RPL:`.

**What is measured:** each time is the difference of two `TICKS` readings around the command or the RPL program,
converted to milliseconds. `TICKS` counts 8192 a second, so a time shorter than about 0.12 ms reads as 0. For the
speed ratio a time of 0 counts as 0.12 ms, so a ratio can be higher than the one shown. The times are tagged `:ms:`,
so you can copy them and compare them between firmware versions, or between the calculator and the web tester.

**The web tester's times are not the calculator's.** There the commands run as WebAssembly on your computer, which is
much faster than the calculator. Compare times only between runs on the same platform. The sample above was
measured in the web tester.
