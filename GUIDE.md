<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright © 2026 SwissMicros GmbH -->
# Writing a DM48 extension

An extension is a small C program that adds commands to the calculator. Its commands appear on a page of the
LIBRARY menu, can be typed by name and can be used in programs. This guide takes you from the template to a file on
the calculator. The file format is in [README.md](README.md).

## Build

You need `arm-none-eabi-gcc` (the GNU Arm Embedded toolchain), GNU `make` and Python 3.

```sh
cd template
make                # popcnt.ext
python3 ../tools/extpack.py --dump popcnt.ext   # what is inside: the imports and relocations
```

To start your own, copy `template/` next to it, rename `popcnt.c`, and set `NAME`, `VERSION` and `SRCS` in the
Makefile. `NAME` is the extension's name on the calculator: 1–15 letters and digits, starting with a letter.

The Makefile does three things:

1. **Compiles** with the calculator's ABI: `-mthumb -march=armv7e-m -mfloat-abi=hard -mfpu=fpv4-sp-d16`. Keep these:
   the `dx_*` functions pass `double`s in floating-point registers. It adds `-Os -ffunction-sections -fdata-sections
   -fno-common -nostdlib -ffreestanding`, and three flags that keep the code loadable:
   - `-mword-relocations`: addresses become literal words the loader can patch, never `MOVW`/`MOVT` pairs.
   - `-fno-builtin -fno-tree-loop-distribute-patterns`: gcc must not turn your loops into `memcpy` or `memset` calls.
   - `-Werror=implicit-function-declaration`: a misspelt `dx_` name fails the build.
2. **Links** with `ld/ext.ld` and `-Wl,-q`, which keeps the relocations in the ELF, and `-lgcc`, which supplies the
   `double` arithmetic and 64-bit division helpers (the FPU is single-precision only).
3. **Packs** the ELF with `tools/extpack.py` into `NAME.ext`. The packer refuses, by name, anything the calculator
   could not load: a call to a function that is neither yours nor a `dx_*` export (`memcpy`, `printf`...), or an
   unsupported relocation. Fix these at build time rather than finding them on the calculator.

When the Emscripten SDK is installed (its `clang` at `$EMSDK/upstream/bin/clang`, `EMSDK` defaulting to `~/emsdk`;
or name another wasm32 clang with `WASM_CC`), `make` also builds `NAME.wasm`, the same source for the web tester, and
packs it into `NAME.ext` as its WebAssembly section. Without it, `make` packs the calculator's part alone; `make wasm`
fails and says why. The wasm build uses `--target=wasm32 -O2 -nostdlib -ffreestanding` and no `--allow-undefined`: a
call of anything but a `dx_*` function fails the link, as on the calculator. `dm48ext.h` maps each `dx_*` declaration
to its import name for this build (`dm48ext_wasm.h`), and the module exports `dx_init`, its memory and its function
table (your `dx_cmd_fn` pointers are indices in it).

`make host` builds `NAME.so`, a native shared object of the same source for the host simulator: `fwcli --ext DIR`
(Linux) loads every `.so` of DIR and provides the `dx_*` functions itself.

## The shape of an extension

```c
#include "dm48ext.h"

static int twice(void) {                 /* ( x -> 2x ) */
  double d;
  if (dx_get_real(dx_peek(1), &d)) return -1;
  dx_obj r = dx_make_real(2 * d);
  if (!r || dx_drop(1) || dx_push(r)) return -1;
  return 0;
}

int dx_init(void) { return dx_register("TWICE", 1, twice); }
```

- `dx_init` runs once, when the extension is loaded. It only registers commands: `dx_register(name, nargs, fn)`.
- A command is `int fn(void)`. The firmware has already checked that the stack holds `nargs` arguments (else Too
  Few Arguments). The command reads them with `dx_peek`, drops them and pushes its results.
- Return 0 for success. A nonzero return is a failure: the error you recorded with `dx_error` / `dx_error_msg`, or
  Bad Argument Value if you recorded none. The stack is restored to what it was before the command.

## The rules

- **Handles last one call.** A `dx_obj` is valid only until your command returns. Never keep one in a static
  variable for the next call.
- **After a failed `dx_*` call, return -1.** A failed call (-1, or a 0 handle) has already recorded the error. Do not
  carry on and do not record another one; return at once.
- **Call `dx_poll()` in long loops, at least every few milliseconds.** It returns 1 when the user has pressed ON; then
  return -1 promptly. It looks for ON on every call, and a call is cheap (on the calculator it reads the millisecond
  clock and looks at the keyboard at most every 50 ms; in the web tester it is one read of shared memory), so call it
  as often as you like: once per row of a picture, or every few hundred steps of an inner loop. On the DM48 the system's clock tick waits while your own code runs and catches up at each `dx_*`
  call, so an extension that computes for long without any `dx_*` call cannot be interrupted, and one that runs for
  about a second without one may reset the calculator.
- **No libc.** There is no `malloc`, `printf`, `strlen` or `memcpy`: write the loop yourself, and use static
  buffers (in `.bss`) for working memory. The kit provides only `dm48ext.h` and libgcc's arithmetic helpers.
- **Never declare a `dx_*` function `weak`**, and do not test its address: the packer refuses it. A newer export
  is a new name or a new `@major`: `dx_fooN` in C is the import `dx_foo@N` (export base names never end in a digit).
- **No global constructors, no C++ exceptions**, and no data that needs more than 8-byte alignment.
- **Two kinds of string.** `dx_make_string` and `dx_get_string` take a *byte range* (a pointer and a length) in the
  calculator's character set, byte for byte, with no terminating NUL (`dx_get_string` adds none). `dx_register`'s
  name, `dx_error_msg` and `dx_make_real_text` take a NUL-terminated *C string*; `dx_get_real_text` and
  `dx_fw_version` write one, NUL included, so `cap` must leave room for it. `dm48ext.h` marks each function.
- Calls and tail calls (`BL`, `B.W`) to `dx_*` functions are fine. Should the packer ever refuse another branch to
  one (a conditional `B<cond>.W`, reported as `R_ARM_THM_JUMP19`), add `-fno-optimize-sibling-calls` to `CFLAGS`.

## Animation and keys

Three calls let a command show pictures while it runs, time itself and read keys:

- **`dx_show(w, h, bits)`** shows your own `w`×`h` frame on the whole screen at once, over the status area. `bits`
  is in HP 48 GROB row layout: `(w + 7) / 8` bytes a row, top row first, the lowest bit of each byte leftmost (as
  `dx_get_grob_bits` gives a GROB's bits). The frame is scaled by the largest whole factor at which it fits (131×64
  shows at ×3 on the DM48's 400×240, 400×240 at ×1) and centred, with the rest of the screen blank; a frame too big
  even at ×1 is cut at the right and the bottom. Each call replaces the last picture, so a loop that draws into a
  frame and shows it is an animation. `dx_show` uses no calculator memory: it reads your buffer and makes no
  object. It returns 0, or -1 with Bad Argument Value when `w` or `h` is outside 0–2048 or the frame does not lie
  in your extension's own memory. When the command returns, the calculator draws its normal screen again. On the
  calculator most of a call's time is the transfer to the LCD, so show a frame only when it has changed. To show a
  GROB from the stack, copy its bits into a buffer with `dx_get_grob_bits` first.
- **`dx_ms()`** is a millisecond count from an arbitrary start, as a `uint32_t` that wraps (after about 49 days):
  only the difference of two readings means anything, `(uint32_t)(t1 - t0)`.
- **`dx_key()`** takes the next key from the buffer the `KEY` command reads, or returns 0 when there is none. It
  never waits; each call consumes the key it returns. ON is never a key: `dx_poll` reports it, as always, even
  when `dx_key` met it in the buffer first (the next `dx_poll` of the same command returns 1).

The three are `dx_*` calls like any other, so a loop that calls them also lets the system's tick catch up; it must
still call `dx_poll` and return -1 when ON was pressed.

```c
static uint8_t frame[50 * 240];          /* 400 x 240: rows of (400 + 7) / 8 bytes, the lowest bit leftmost */

static int anim(void) {                  /* ( -> frames ms ) */
  int n = 0;
  uint32_t t0 = dx_ms();
  for (;;) {
    draw(frame, n++);                    /* your code: the next frame into the buffer */
    if (dx_show(400, 240, frame)) return -1;
    if (dx_key() == 55) break;           /* backspace: a clean stop */
    if (dx_poll()) return -1;            /* ON: abandon */
  }
  dx_obj f = dx_make_real(n), ms = dx_make_real(dx_ms() - t0);
  return !f || !ms || dx_push(f) || dx_push(ms) ? -1 : 0;
}
```

`dx_key`'s numbers are those of `KEY`: *rc*, the key's row then its column, counted on the HP 48 keyboard from the
top left (11 is the first menu key; the rows of five keys are numbered 1–5). The DM48 has the HP 48's keys in the
HP 48's places, so the numbers are the same:

| Key | Code | Key | Code |
| --- | --- | --- | --- |
| ▲ | 25 | 0 | 92 |
| ◀ | 34 | 1 2 3 | 82 83 84 |
| ▼ | 35 | 4 5 6 | 72 73 74 |
| ▶ | 36 | 7 8 9 | 62 63 64 |
| ENTER | 51 | + | 95 |
| ← (backspace) | 55 | − | 85 |
| ◀ shift (left shift) | 71 | ▶ shift (right shift) | 81 |

A shift key is a key of its own here (71, 81): `dx_key` gives no shifted numbers such as 25.2. There is no EXIT
code: on the DM48, EXIT is ON, which breaks the program, and only `dx_poll` sees it. Give your command a clean
"stop" key of its own; ← (55) is the usual one.

**On the web tester,** `dx_key` needs the page to be cross-origin isolated (`SharedArrayBuffer`); without it,
`dx_key` always returns 0, while `dx_show` and `dx_ms` still work. A fast show loop draws only the newest
frame. In the host simulator (`fwcli --ext`), which has no screen, each `dx_show` is logged as
`show <w>x<h>` on standard error.

## Install

Copy `NAME.ext` to the `/DM48/EXT/` folder on the calculator's disk (USB disk mode), then choose it once in
SETUP → Programs → Extensions…: it loads at once and is added to `/DM48/EXT/LOAD.TXT`. From then on it loads at
every start. (The DM48's system cannot list a folder, so `LOAD.TXT` says which files to load.)

`LOAD.TXT` is a plain text file you may edit yourself: one file name per line (`popcnt.ext`), in load order; blank
lines and lines starting with `#` are ignored. At most 8 extensions load.

One that cannot load (a firmware export it needs is missing, a damaged or missing file, not enough memory) gets a
one-line notice at startup, for example `fft: needs dx_frob@2 - update firmware`, or `fft: old format - rebuild`
for a file built with an older kit (format 1: build it again with this one); a missing file keeps its line, so
copying the file back is enough. SETUP → Programs → Remove extensions unloads them all and deletes `LOAD.TXT`
(programs that use their commands then give `Extension missing: NAME`); to remove one, delete its line from
`LOAD.TXT` and restart.

Its commands are typed by their names. When two loaded extensions have a command of the same name, write
`ext:CMD` (`popcnt:POPCNT`); that form means the command only while the extension `ext` is loaded, and without it
`A:B` is invalid input, as on the HP 48. A program shown or saved as text while its extension is missing names the
command bare; read back, it runs the command again once the extension is loaded.

If an extension faults (a bad pointer, a division by zero trap), the command ends with `Extension fault: NAME`, the
stack is restored, and the extension is unloaded until the next start.

On the calculator an extension runs unprivileged under the memory protection unit: it can reach only its own
image, `.bss` and stack (2 KB; keep large buffers in `.bss`), and any access to the calculator's memory or
peripherals is a fault. Buffers you pass to `dx_*` functions for output must be
in writable memory: initialised data, `.bss` or the stack, never const data or code. The file (format 2) keeps code and data apart: the code and
read-only data are read-only and executable to the extension, and its initialised data, `.bss` and stack are writable
and never executable, so an extension can neither rewrite its own code nor run its data. There is no timer guard: a
loop that never calls `dx_poll` cannot be stopped with ON.

## Test in the web tester

The web tester loads the same `.ext` file, through its WebAssembly section: SETUP → Programs → Extensions…, then pick
the file. The tester keeps it in the browser for the next visit (Remove extensions forgets them). Its commands then run in the
browser, so you can try them without copying files to the calculator. A file without the WebAssembly section shows
the notice `NAME: no web build` there and loads only on the calculator.
