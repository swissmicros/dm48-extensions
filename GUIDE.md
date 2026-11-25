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
   - `-mlong-calls`: a call to a `dx_*` function goes through such a word. Without it, newer linkers (binutils
     2.42 and later, as in gcc 14 toolchains) refuse the build with "Unknown destination type (ARM/Thumb)".
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
- A command must not be named like a built-in command (`SHOW`, `KEY`, ...): the extension is refused at load with
  `NAME: name conflict CMD` (`NAME` the extension, `CMD` the command). Pick a distinct name such as `XSHOW`.
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
top left (11 is the first menu key). The first four rows have six keys, columns 1–6; the ENTER row has five (51 ENTER
to 55 backspace); the four rows below have five keys each, columns 1–5 (ON is 91). The DM48 has the HP 48's keys in the
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

## Files

Eleven calls read and write files on the calculator's disk, the one you see over USB disk mode:

| Call | Does |
| --- | --- |
| `dx_file_open(path, mode)` | opens a file: its file number (1 or 2), or -1 |
| `dx_file_read(f, buf, n)` | reads up to `n` bytes at the position: how many (fewer only at the end, 0 there) |
| `dx_file_write(f, buf, n)` | writes `n` bytes at the position, extending the file at its end: `n` |
| `dx_file_seek(f, pos)` | moves the position, 0 to the size, counted from the start |
| `dx_file_tell(f)`, `dx_file_size(f)` | the position, the size |
| `dx_file_close(f)` | closes it |
| `dx_file_delete(path)` | deletes a file, or an empty folder |
| `dx_file_rename(from, to)` | renames or moves a file or a folder; `to` must not exist |
| `dx_file_mkdir(path)` | makes a folder (its parent must exist); one that exists already is fine |
| `dx_file_exists(path)` | 1 when a file of that name exists, else 0 (a folder is not a file) |

The modes of `dx_file_open`:

| Mode | The file | Starts at | Writes |
| --- | --- | --- | --- |
| `DX_FILE_READ` | must exist | 0 | no |
| `DX_FILE_WRITE` | created, or emptied | 0 | yes |
| `DX_FILE_APPEND` | created when missing | the end | yes |
| `DX_FILE_UPDATE` | must exist, kept | 0 | yes |

Every mode reads. A file cannot be made shorter except by opening it with `DX_FILE_WRITE`, and the position cannot go
past the end (Bad Argument Value): a file grows only by writing at its end.

**Paths** are C strings, **absolute**, at most 127 bytes: `/DM48/MYEXT/DATA.BIN`. Names are separated by `/` and
may use printable ASCII except `" * : < > ? \ |`; there is no drive letter, no `.` or `..`, no empty name and no `/`
at the end. Names compare without case, as on any FAT disk (`/dm48/a.txt` is `/DM48/A.TXT`). Long names work;
`NAME.EXT` names (8 + 3) are the safest for files a PC also reads. Anything else is Bad file name.

**File numbers** last one command, like `dx_obj` handles: at most 2 files are open at once, and when your command
returns, the calculator closes every file it left open, also when it failed, was stopped with ON or faulted. Never
keep a file number in a static variable for the next call; open the file again. A file open already cannot be opened
a second time, deleted or renamed (File in use).

**Errors.** A failed call returns -1 and records the error, which your command then gives (return -1 at once, as with
every `dx_*` call):

| Error | Number | When |
| --- | --- | --- |
| No such file | #E811h | opening a missing file to read or update, deleting or renaming a missing one |
| No such folder | #E812h | a folder on the way is missing |
| File exists | #E813h | renaming onto an existing name; a folder where a file is |
| Disk full | #E814h | the disk took only part of a write (what came before it may be on the disk) |
| Access denied | #E815h | a read-only file, a folder that is not empty, a full folder |
| Disk error | #E816h | the disk failed, or is invalid after a failed write until the calculator checks it |
| File in use | #E817h | the file is open already |
| Too many open files | #E818h | a third file |
| Bad file name | #E819h | a path that breaks the rules above |

A bad file number, mode, count or buffer is Bad Argument Value, as everywhere in the interface. `dm48ext.h` names them
`DX_ERR_NO_FILE` ... `DX_ERR_BAD_NAME`, so your command can give one itself with `dx_error`.

**No listing.** The calculator's system cannot list a folder, so neither can an extension. Keep an index file of your
own when you need to know which files you made (the firmware does the same with `LOAD.TXT`).

**Long transfers.** A single read or write runs to its end: ON cannot break it. Move large files in pieces of a few
KB and call `dx_poll` between them, so ON stops the command and the system's clock keeps up:

```c
static uint8_t buf[4096];

static int copy(const char *from, const char *to) {
  int a = dx_file_open(from, DX_FILE_READ);
  if (a < 0) return -1;
  int b = dx_file_open(to, DX_FILE_WRITE);
  if (b < 0) return -1;                  /* a is closed when the command returns */
  for (;;) {
    int n = dx_file_read(a, buf, sizeof buf);
    if (n < 0 || (n && dx_file_write(b, buf, n) < 0)) return -1;
    if (n == 0) break;
    if (dx_poll()) return -1;            /* ON: abandon (the half-written file stays) */
  }
  return dx_file_close(b);               /* close a written file yourself: its error is yours to give */
}
```

On the calculator writing runs at about 64 KB/s and reading much faster.

**Any path: take care.** An extension may read, overwrite, rename and delete any file on the disk, the calculator's
own too: saved states (`*.D48`), `LOAD.TXT` and the other extensions in `/DM48/EXT/`, language files, the system's
files. This is deliberate (an extension can exchange files with any program), and it means:

- Write under a folder of your own, `/DM48/NAME/` (`NAME` your extension's), and delete only files you made.
- Tell your users which files your commands write.
- Users: load only extensions you trust, as with any program you run.

A `LOAD.TXT` an extension deleted loads no extensions at the next start; a state file it damaged is refused when
loaded, as any damaged file. A failed write can leave the disk invalid until the calculator checks it again; every
file call, and the calculator's own state save, then gives Disk error.

**Testing.** In the host simulator, `fwcli --ext DIR --disk DISK` puts the calculator's disk in the folder `DISK`
(`/A.TXT` is `DISK/A.TXT`); without `--disk` it is a temporary folder, deleted when `fwcli` ends. The web tester keeps
its own disk in the browser, kept for the next visit: SETUP → Programs → Extension files… lists its folders, imports
a file from your PC into the folder shown, exports a file and deletes one, so you can try your commands with real
data. A firmware without these calls refuses an extension that uses them with `NAME: needs dx_file_open@1 - update
firmware`.

## USB serial and infrared

Four calls let a command talk to a program on a PC over the USB cable, and send bytes through the infrared LED:

| Call | Does |
| --- | --- |
| `dx_serial_connected()` | 1 when a program on the PC has the line open, else 0 |
| `dx_serial_write(line, n)` | sends one line of `n` bytes (its line end is added): 0 |
| `dx_serial_read(buf, cap)` | the next line received, without its line end: its length, or 0 when none has come yet |
| `dx_ir_send(bytes, n)` | sends `n` bytes (up to 4096) for an HP 82240B printer through the infrared LED: 0 |

**The serial line** is the calculator's USB serial console, the one SETUP → Settings → USB console turns on for
screen mirroring; your extension's lines travel on a channel of their own (channel 2) beside the screen's, so
mirroring keeps working. It needs:

- SETUP's **USB console ON** (it is off by default), the cable in, and
- a program on the PC that opens the calculator's serial port and speaks the console's framing. A plain terminal
  shows the framing bytes; use `tools/dm48serial.py` from this kit (below), or your own program.

A line is text: printable ASCII (space to `~`) and tab, at most `DX_SERIAL_LINE` (127) bytes out and 255 in. A line
end or any other byte in a line you write is Bad Argument Value; send binary data as hex or base64. An empty line can
be sent but never arrives: received empty lines are dropped, so 0 from `dx_serial_read` always means "nothing yet".

`dx_serial_read` never waits. To wait for an answer, loop with `dx_poll`, and give up after a while with `dx_ms`:

```c
static char line[256];

static int ask(const char *q, int n) {          /* sends q, then waits up to 2 s for the answer */
  if (dx_serial_write(q, n)) return -1;
  uint32_t t0 = dx_ms();
  for (;;) {
    int k = dx_serial_read(line, sizeof line);
    if (k < 0) return -1;
    if (k > 0) return k;
    if (dx_ms() - t0 > 2000) return dx_error_msg("No answer");
    if (dx_poll()) return -1;                     /* ON */
  }
}
```

Without a connection (USB console off, no cable, no program on the PC), `dx_serial_write` and `dx_serial_read` fail
with **Not connected** (#E821h, `DX_ERR_NOT_CONNECTED`); ask `dx_serial_connected` first when your command can do
without the PC. A buffer too small for the next line is Bad Argument Value, and the line stays for a larger one: give
`dx_serial_read` 255 bytes and that never happens.

What the calculator's system allows sets the pace: a write waits until 40 ms have passed since the last one, so about
25 lines a second go out (some 3 KB/s), and lines that arrive while no command reads them wait in a small buffer
(about 256 bytes, 16 lines); when it is full, newer lines are lost. Keep exchanges short, question and answer.

**On the PC**, `tools/dm48serial.py` (Python 3 with `pyserial`) opens the port and joins your program's standard
input and output to channel 2:

```sh
python3 tools/dm48serial.py /dev/ttyACM0            # Linux; COM5 on Windows, /dev/cu.usbmodem* on a Mac
```

Each line your extension writes appears on its standard output; each line typed (or piped) into it goes to the
calculator. The screen's channel is ignored. Only one program can have the port open at a time, so close the screen
mirroring page first.

**Infrared.** The DM48's infrared LED is the printer port: the calculator's system turns each byte into the HP 82240B
printer's infrared frames, and a print command's bytes go the same way. `dx_ir_send` hands it raw bytes, so your
command can print what it likes, text or graphics, in the 82240B's codes (text bytes, 04 or 0A to end a line, `1B n`
and n bytes for graphics). It is paced by PRTPAR's delay, as PRINT is, and it waits while the printer's line takes:
the command does not return before the bytes are out. There is no other use of the LED: the system offers no raw
control of it, so remote-control codes or IrDA cannot be sent.

**Testing.** In the host simulator, `fwcli --serial FILE` connects the line: each line of `FILE` arrives in turn, and
each line written is logged as `serial <line>` on standard error. The infrared bytes go to `fwcli`'s print capture
(`print` replies with them in hex). The web tester has no serial line (`dx_serial_connected` is 0, the others give Not
connected), and prints the infrared bytes on its paper strip. A firmware without these calls refuses an extension
that uses them with `NAME: needs dx_serial_write@1 - update firmware`.

## Install

Copy `NAME.ext` to the `/DM48/EXT/` folder on the calculator's disk (USB disk mode), then choose it once in
SETUP → Programs → Extensions…: it loads at once and is added to `/DM48/EXT/LOAD.TXT`. From then on it loads at
every start. (The DM48's system cannot list a folder, so `LOAD.TXT` says which files to load.)

`LOAD.TXT` is a plain text file you may edit yourself: one file name per line (`popcnt.ext`), in load order; blank
lines and lines starting with `#` are ignored. At most 8 extensions load.

One that cannot load (a firmware export it needs is missing, a damaged or missing file, not enough memory) gets a
one-line notice at startup, for example `fft: needs dx_frob@2 - update firmware`, or `fft: old format - rebuild`
for a file built with an older kit (format 1: build it again with this one); a missing file keeps its line, so
copying the file back is enough.

SETUP → Programs → Remove extensions (shown while an extension is set to load) lists them: the ones loaded, and the
ones `LOAD.TXT` names that did not load this time, marked `(not loaded)`; All extensions is last. Picking one asks
first (F1 removes it): it is unloaded and its line dropped from `LOAD.TXT`, the other lines and comments kept. All
extensions unloads them all and deletes `LOAD.TXT`. Programs that use a removed extension's commands then give
`Extension missing: NAME`. With a single extension its question shows at once. The list shows at most 7
extensions (one screen, with All): remove the others with All, or by deleting their lines from `LOAD.TXT` and
restarting. On the web tester the same menu lists the extensions this browser keeps.

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
