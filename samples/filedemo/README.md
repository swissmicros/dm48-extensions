<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright © 2026 SwissMicros GmbH -->
# filedemo: files from RPL

`FILEDEMO` gives RPL eight commands for the calculator's disk (the FAT drive you see over USB), built on the
`dx_file_*` functions. Paths are strings such as `"/DM48/DEMO/A.TXT"`: absolute, at most 127 characters, names
without case as FAT has them. A failure is the calculator's own error (`No such file`, `No such folder`, `File in use`, …).

| Command | Stack | What it does |
| --- | --- | --- |
| `FWRITE` | `"text" "path" →` | Makes the file, or empties it, and writes the text. |
| `FAPPEND` | `"text" "path" →` | Adds the text at the end (makes the file when there is none). |
| `FREAD` | `"path" → "text"` | The whole file as a string (8 KB at most in this sample). |
| `FTAIL` | `"path" n → "text"` | The file from byte `n` on. |
| `FDEL` | `"path" →` | Deletes a file, or an empty folder. |
| `FMKDIR` | `"path" →` | Makes a folder (one level; its parent must exist). |
| `FREN` | `"from" "to" →` | Renames or moves a file or a folder. |
| `FEXISTS` | `"path" → 0/1` | 1 when the file exists. |

For example `"Hello" "/HELLO.TXT" FWRITE` then `"/HELLO.TXT" FREAD`.

It shows how to:

- read and write in 1 KB pieces with `dx_poll` between them, so ON breaks a long transfer;
- close what it opened (the calculator closes a command's files anyway when it ends, also after an error);
- turn a string on the stack into a C path, and back.

An extension can reach every file on the disk, the calculator's own included: see GUIDE.md, "Files", before writing
to anything outside a folder of your own.
