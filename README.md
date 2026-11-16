<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright © 2026 SwissMicros GmbH -->
# DM48 native extension kit

Write new calculator commands in C, build them with `arm-none-eabi-gcc` and copy one file, `NAME.ext`, to the
calculator. [GUIDE.md](GUIDE.md) is the how-to; this file lists the kit and specifies the `.ext` file format.

| Path | What it is |
| --- | --- |
| `include/dm48ext.h` | The interface: the `dx_*` functions the firmware exports, each `name@major`. |
| `include/dm48ext_wasm.h` | Generated from it: each function's WebAssembly import name (module `dx`), for the web build. |
| `ld/ext.ld` | The linker script: one image from address 0 (code and read-only data, then `.data` from a 32-byte boundary), `.bss` last and `NOLOAD`, entry `dx_init`. |
| `tools/extpack.py` | The packer: linked ELF → `NAME.ext`. Python 3, standard library only. `--dump` prints a file. |
| `tools/test_extpack.py` | Its tests: `cd tools && python3 -m unittest test_extpack`. |
| `template/` | A complete extension (`POPCNT`, `REVSTR`) and its Makefile. Copy it to start. |
| `LICENSE` | MIT. Your own extensions may use any licence. |

## The exports

Each is `@1`, declared in `include/dm48ext.h`, which says what each one takes and returns.

| Group | Functions |
| --- | --- |
| The stack | `dx_depth`, `dx_peek`, `dx_drop`, `dx_push`, `dx_type` |
| Reals and complex numbers | `dx_get_real`, `dx_make_real`, `dx_get_real_text`, `dx_make_real_text`, `dx_get_complex`, `dx_make_complex` |
| Binary integers | `dx_get_bint`, `dx_make_bint` |
| Strings | `dx_string_len`, `dx_get_string`, `dx_make_string` |
| Lists | `dx_list_len`, `dx_list_item`, `dx_make_list` |
| Graphics | `dx_grob_size`, `dx_get_grob_bits`, `dx_make_grob`, `dx_show` |
| Errors and ON | `dx_error`, `dx_error_msg`, `dx_poll` |
| Time and keys | `dx_ms`, `dx_key` |
| The calculator | `dx_product`, `dx_fw_version` |
| Registration (in `dx_init` only) | `dx_register` |

## The `.ext` format, version 2

Everything is little-endian. All offsets are from the start of the file. `tools/extpack.py` (`HEADER_FMT`,
`RELOC_FMT`) is the reference implementation.

Version 2 splits the image in two parts: the **code part** (code and read-only data), which the extension can run
but not write, and the **data part** (`.data`), which it can write but not run. Version 1, which had no split, is no
longer loaded: a loader refuses a version 1 file with `NAME: old format - rebuild`.

### Header: 88 bytes

| Offset | Type | Field | Value |
| ---: | --- | --- | --- |
| 0 | `char[4]` | `magic` | `"DMXT"` |
| 4 | `u16` | `format` | `2` |
| 6 | `u16` | `flags` | `0` |
| 8 | `char[16]` | `name` | The extension name, NUL-padded: 1–15 letters and digits, starting with a letter. |
| 24 | `u32` | `version` | The extension's own version (`--version`, default 1). |
| 28 | `u32` | `image_size` | Bytes of the image (the code part, then the data part). |
| 32 | `u32` | `bss_size` | Bytes of zeroed memory the loader places right after the image. |
| 36 | `u32` | `init_off` | The offset of `dx_init` in the code part, **with the Thumb bit set** (odd): call `base + init_off`. |
| 40 | `u32` | `code_size` | Bytes of the code part at the start of the image: a multiple of 32, at most `image_size`. Bytes `code_size` to `image_size` are the data part. (Version 1 had `ncmd`, always 0, here.) |
| 44 | `u32` | `nimport` | The number of imports. |
| 48 | `u32` | `import_off` | Where the import table starts. |
| 52 | `u32` | `nreloc` | The number of relocations. |
| 56 | `u32` | `reloc_off` | Where the relocation table starts. |
| 60 | `u32` | `strtab_off` | Where the string table starts. |
| 64 | `u32` | `strtab_size` | Its size in bytes. |
| 68 | `u32` | `wasm_off` | Where the WebAssembly module starts (4-aligned), `0` when there is none. |
| 72 | `u32` | `wasm_size` | Its size in bytes, `0` when there is none. |
| 76 | `u32` | `sig_off` | `0` (reserved for a signature). |
| 80 | `u32` | `sig_size` | `0` (reserved). |
| 84 | `u32` | `crc32` | IEEE CRC-32 (zlib's `crc32`) of bytes 88 to the end of the file. |

As a C struct (packed, little-endian), in this order: `char magic[4]; uint16_t format, flags; char name[16];
uint32_t version, image_size, bss_size, init_off, code_size, nimport, import_off, nreloc, reloc_off, strtab_off,
strtab_size, wasm_off, wasm_size, sig_off, sig_size, crc32;`.

### Body

The packer writes the parts in this order, but a loader goes by the offsets:

1. **Image** at offset 88, `image_size` bytes, padded with zeros to a multiple of 4. It was linked at address 0, so
   every address in it is an offset from the image's start. Its first `code_size` bytes are the code part (`.text`
   and `.rodata`, padded with zeros to a multiple of 32); the rest is the data part (`.data`). Without `.data`,
   `code_size` equals `image_size`.
2. **Imports** at `import_off`: `nimport` × `u32`, each the file offset of a NUL-terminated string in the string table,
   the name of a firmware export as `name@major` (`dx_push@1`). The packer sorts them by name.
   In C the import is the symbol `nameN` for `name@N`, and the bare `name` for `name@1`; for that to be unambiguous,
   exported base names never end in a digit.
3. **Relocations** at `reloc_off`: `nreloc` × 8 bytes, sorted by offset:

   | Offset | Type | Field |
   | ---: | --- | --- |
   | 0 | `u32` | `offset`: the place, a byte offset in the image |
   | 4 | `u8` | `type`: 1, 2 or 3 (below) |
   | 5 | `u8` | `0` |
   | 6 | `u16` | `import`: an index into the imports, `0xFFFF` for type 1 |

4. **String table** at `strtab_off`, `strtab_size` bytes: the import names, each NUL-terminated.
5. **WebAssembly module** at `wasm_off`, `wasm_size` bytes (optional): the same extension built for the web tester.
   It imports each `dx_*` function it calls from module `dx` under its `name@major` (`dx_push@1`), and exports
   `dx_init`, `memory` and `__indirect_function_table`; a `dx_cmd_fn` passed to `dx_register` is an index in that
   table. Pointers are into its own memory.

### Loading

1. Check the file. A loader **must refuse** it when: `magic` is not `"DMXT"`; `format` is not 2 (for 1, with the
   notice `NAME: old format - rebuild`); `flags` is nonzero; the CRC does not match; any offset or size reaches
   outside the file; `code_size` is not a multiple of 32 or is larger than `image_size`; `init_off` is not in the
   code part; a relocation has an unknown `type` (not 1, 2 or 3), a nonzero reserved byte (offset 5), a
   place outside the image, or an import index out of range (or not `0xFFFF` for type 1); a type 3 place is not
   inside the code part (`offset + 4 ≤ code_size`). Types 1 and 2 may lie anywhere in the image.
2. Allocate `image_size + bss_size` bytes at a **32-aligned** address `base`. Copy the image to `base` and zero the
   `bss_size` bytes after it: `.bss` starts at `base + image_size` (`bss_size` includes any alignment gap between
   the end of the image and the linker's `.bss`).
3. Resolve every import against the firmware's export table; refuse the file with the import's name on a miss.
4. Apply each relocation at `P = base + offset` (the place may be unaligned for type 1 and 2: access it byte by byte):
   - **1 `ABS32`:** the 32-bit word at `P` += `base`. It already holds the target's offset in the image, with the
     Thumb bit for a function.
   - **2 `ABS32_IMPORT`:** the 32-bit word at `P` = the import's address (a function pointer, Thumb bit set). The
     word held 0.
   - **3 `THM_CALL_IMPORT`:** `P` holds a 32-bit Thumb-2 `BL` (a call) or `B.W` (a tail call) as two halfwords. Rewrite
     its 24-bit branch offset so that it reaches the import, through a veneer next to the image when the import is
     beyond the ±16 MB a branch can reach. Keep the kind of branch: bit 14 of the second halfword is 1 for `BL`, 0 for
     `B.W`. The old offset bits are meaningless. The packer guarantees the place holds one of the two
     (`hw1 & 0xF800 == 0xF000`, `hw2 & 0xD000` is `0xD000` for `BL` or `0x9000` for `B.W`).

     The arithmetic (Thumb-2 encoding T4 of `B.W`, T1 of `BL`): `target = P + 4 + offset`, where `offset` is a
     signed 25-bit, even byte count (`-16777216 ≤ offset ≤ 16777214`) and the target is the import's address with
     its Thumb bit cleared. With `S = offset[24]`, `I1 = offset[23]`, `I2 = offset[22]`, `imm10 = offset[21:12]`,
     `imm11 = offset[11:1]`, `J1 = NOT(I1 XOR S)`, `J2 = NOT(I2 XOR S)`:
     - `hw1 = 0xF000 | S << 10 | imm10`;
     - `hw2 = 0x8000 | J1 << 13 | 0x1000 | J2 << 11 | imm11`, plus `0x4000` for `BL` (kept from the old `hw2`).
5. Call `dx_init` at `base + init_off`. It calls `dx_register` once per command and returns 0.

PC-relative references inside the image (calls between the extension's own functions, branches, literal loads) need
no relocation: they stay right wherever the image is placed. The packer refuses every other kind of relocation.

The loader applies every relocation, in both parts, before the extension runs. On the calculator the extension then
runs with the code part read-only and executable, and the data part, `.bss` and its stack read-write and never
executable (the memory protection unit's regions are 32-byte lines, hence the alignment of `code_size`).

## This repository

This repository is the DM48 native extension kit. It is maintained alongside the DM48 firmware, and changes here
are synced from there, so a fix to a kit file is best sent as an issue or a pull request and will be carried over by
hand. `FIRMWARE.txt` names the firmware commit the kit was last synced from.

`samples/` is the place for contributed samples: one directory per sample, each with its own `Makefile` modelled on
`template/Makefile`. A sample copied from `template/` into `samples/NAME/` must set `KIT = ../..` in its Makefile
(the template's `KIT = ..` is relative to `template/`). The CI builds every one of them. Issues and pull requests are welcome on GitHub.
