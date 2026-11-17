<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright © 2026 SwissMicros GmbH -->
# DM48 language files

A language file (`.lng`) translates the calculator's drawn text, so a new language needs no new firmware. It is for
translators and for anyone who wants the DM48 in a language other than English, German or French (those are built in
and always win over a file with the same code).

What you need is in this kit: the template, [`lang/template.json`](lang/template.json), and the builder,
[`tools/lngpack.py`](tools/lngpack.py) (Python 3, standard library only).

## Making one

1. Copy `lang/template.json` and name it after the language code, for example `es.json`.
2. Set `"@name"` to the language's own name (at most 16 bytes, for example `Español`).
3. Replace each English key's entry with the translation: the file becomes a flat `{ "English": "Translation" }`
   map. Leave out what you do not translate; it stays English. The template's entries carry a `note` (where the
   string is drawn), a width budget (`px`, `cells`, `upper` = drawn in capitals) and what the translation must keep
   (`keep`, `names`: see the rules below). Replace the whole entry: only the key and its translation are read.
4. Run the builder:

   ```sh
   python3 tools/lngpack.py es.json            # writes es.lng next to es.json
   python3 tools/lngpack.py es.json --code es --name Español --out es.lng
   python3 tools/lngpack.py --dump es.lng      # prints a file: its header and each string with its English
   ```

   The code defaults to the file's stem and the name to the file's `"@name"`. `--template` names another template
   (the default is the kit's `lang/template.json`). The builder prints one warning per translation wider than its
   budget and still writes the file; it writes nothing when it refuses the file, and lists every reason.

A file made for an older firmware still works: it translates every string it knows, and new strings stay English.
Use the template of the kit that matches your firmware: a key that is not in the template is refused.

You can translate the copy of the template in place and a little at a time: an entry still in the template's form
(not yet replaced by a text) stays English, and the builder prints how many such entries are left. The template's
`"@"` keys (`@format`, `@charset`, `@px` and the others) are ignored in a translation.

## The rules the builder enforces

- Code: 2 or 3 letters a-z. `en`, `de` and `fr` are built in and refused.
- Name: at most 16 bytes of the HP 48 character set, on one line: no newline (0x0A) and no 0x1F.
- Every `{placeholder}` of the English must appear in the translation, unchanged. Device text (all-caps words such as
  SETUP, file extensions such as .D48: the entry's `keep`) must be kept. In a form or box text, the RPL names (the
  entry's `names`, such as `INDEP` or `ΣDAT`) must be kept as whole words. Leading and trailing spaces must match (a
  narrow no-break space before `:`, `;`, `!`, `?`, `»` or `%` does not count as a leading space). A translation of
  only spaces is refused.
- Characters: the HP 48 8-bit set (the template's `@charset`, one text per byte). Allowed bytes are newline (0x0A),
  0x1F and 0x20 to 0xFF. The narrow no-break
  space U+202F is stored as 0xA0 (the template's `@aliases`).
- An entry is at most 255 bytes. A file is at most 131 072 bytes and 4 096 entries.
- A key that is not in the template is refused.
- A translation drawn wider than its budget is a warning, not an error. It is drawn clipped. The budgets: `px`, the
  width in the calculator's small font (the sum of the template's `@px` of each byte; with `upper`, of each byte in
  capitals: a-z and à-þ but ÷, less 0x20); `cells`, the bytes; an error message (no budget) gets max(`@cells`, its
  English's bytes) cells. A text whose note starts `board:` has no budget here.
- At load the firmware also ignores any entry longer than max(2 x English, English + 16) bytes; that one string stays
  English. The builder warns of it.

## Format, version 1 (little-endian)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `RLNG` |
| 4 | 1 + 3 | version (1), three zero bytes |
| 8 | 4 | catalogue: FNV-1a over the template's keys (but the `"@"` ones), sorted, joined by `\n`, as HP 48 bytes (informational) |
| 12 | 4 | count |
| 16 | 4 | strings_off |
| 20 | 4 | size (the file length) |
| 24 | 8 | code, ASCII, NUL padded |
| 32 | 16 | name, HP bytes, NUL padded |
| 48 | 8 x count | index: hash u32, off u32 (relative to strings_off), sorted by hash, hashes unique |
| strings_off | | the strings, each NUL terminated |

The key hash is FNV-1a (32 bit: offset basis 0x811C9DC5, prime 0x01000193) over the English string's HP 48 bytes.
`tools/lngpack.py` (`HEADER_FMT`) is the reference implementation.

## What stays English

Only drawn text is translated. Everything a program can read stays English, so a program behaves the same in every
language: error messages as returned by `ERRM`, the output of `→STR`, command names, menu labels. The Equation
Library's INFO texts are not in the template and stay English, as do the DMCP menus and the texts `ERRM` and `→STR`
give. The device's SETUP menu stays English, ASCII only.

## Where to put the file

The calculator reads language files from the same place as its other files; the language then appears in the
Language setting after the built-in ones.

- Device: copy `es.lng` to the `LANG` folder of the calculator's state folder (`/DM48/LANG/es.lng`; `/DM50/LANG` on
  the DM50), with the DM48 as a USB disk. The calculator cannot list a folder, so it looks for each two-letter name
  `aa.lng` to `zz.lng` (`en`, `de` and `fr` skipped): the file name must be a two-letter code and `.lng`, as
  `lngpack.py` writes it. It looks at start and after USB disk, and only when the `LANG` folder exists. A file whose
  code is `en`, `de` or `fr`, or a code another file has already, is ignored (only its header is read). Each other
  file is checked whole; a file that is damaged, too large, of an unknown version or with a character outside the HP
  set is not listed, and the start shows one notice naming the first such file and why (`(+N more)` when there are
  several). At most 8 files are listed, the first 8 in name order; the calculator looks no further. SETUP > Settings >
  Language then cycles English, Deutsch, Francais and each file's language in code order; the menu shows its name
  without accents (DMCP's font). The choice is kept in `/DM48/LANGUAGE` as the code. If the chosen file is gone at
  the next start, the calculator is English, without a notice.
- Web tester: SETUP > Settings > Language files... adds a file (a picker, `.lng`; the page checks it and says why if it
  refuses it) or removes one. At most 8 files and 200 KB in all, kept in this browser only (not synced, and cleared with
  the site's data). Three-letter codes work here. The language then appears in SETUP > Settings > Language after the
  built-in ones, under the name the file carries.
- Android: SETUP > Settings > Language files... opens the system file picker; the chosen file is copied into the app's
  own storage. The same limits as on the web: 8 files, 200 KB; three-letter codes work here too. A file that is bad at
  the next start is refused with one notice, naming the file in the calculator's language, and the language falls back
  to English.
