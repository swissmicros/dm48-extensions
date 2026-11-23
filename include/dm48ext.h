// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
/*
 * dm48ext.h - public stage-1 interface for native extensions (DM48 / DM50).
 *
 * This interface is permanent: a name@major, once released, is never changed. Incompatible changes
 * get a new major (dx_push2 is dx_push@2). Each exported declaration ends in a trailing "@N"
 * annotation comment; tools/ext-exports.ts reads exactly those to build the firmware export table.
 * Status ints: 0 = ok, -1 = failed (an error has been recorded).
 *
 * Strings come in two kinds; each function below says which it takes:
 *   - C string: NUL-terminated (ASCII is enough). Taken by dx_make_real_text (at most 64 characters),
 *     dx_error_msg (at most 255) and dx_register's name (1-15). Written, with its NUL, by dx_get_real_text
 *     and dx_fw_version: cap must hold the text and the NUL; they return the length without it.
 *   - Byte range: (pointer, length), in the calculator's character set, no NUL added or needed (a NUL byte is
 *     an ordinary character). dx_make_string takes one; dx_get_string fills one, at most cap bytes, and
 *     returns how many it wrote (dx_string_len gives the whole length).
 */
#ifndef DM48EXT_H
#define DM48EXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t dx_obj;                       /* opaque handle; 0 = none/failed */

enum { DX_REAL = 0, DX_COMPLEX = 1, DX_STRING = 2, DX_LIST = 5, DX_BINT = 10, DX_GROB = 11, DX_OTHER = -1 };
enum { DX_DM48 = 48, DX_DM50 = 50 };
enum { DX_ERR_TOO_FEW = 0x201, DX_ERR_BAD_TYPE = 0x202, DX_ERR_BAD_VALUE = 0x203, DX_ERR_NO_MEM = 0x001 };
/* dx_file_open's modes: READ the file must exist, read only; WRITE created or emptied; APPEND created when missing,
   the position at the end; UPDATE must exist. All but READ read and write. */
enum { DX_FILE_READ = 1, DX_FILE_WRITE = 2, DX_FILE_APPEND = 3, DX_FILE_UPDATE = 4 };
enum { DX_ERR_NO_FILE = 0xE811, DX_ERR_NO_FOLDER = 0xE812, DX_ERR_FILE_EXISTS = 0xE813, DX_ERR_DISK_FULL = 0xE814,
       DX_ERR_DENIED = 0xE815, DX_ERR_DISK = 0xE816, DX_ERR_IN_USE = 0xE817, DX_ERR_TOO_MANY_FILES = 0xE818,
       DX_ERR_BAD_NAME = 0xE819 };
/* The USB serial line's error (GUIDE.md "USB serial and infrared"). */
enum { DX_ERR_NOT_CONNECTED = 0xE821 };
enum { DX_SERIAL_LINE = 127 };   /* the longest line dx_serial_write sends */

/* Exported by the firmware, @1 each. */
int    dx_depth(void);                                     /* @1 */
dx_obj dx_peek(int level);                                 /* 1 = top; 0 when level out of range */ /* @1 */
int    dx_drop(int n);                                     /* @1 */
int    dx_push(dx_obj o);                                  /* @1 */
/* dx_type of an invalid handle returns DX_OTHER and records Bad Argument Value (#203). */
int    dx_type(dx_obj o);                                  /* the DX_ type, matching the calculator's TYPE */ /* @1 */
int    dx_get_real(dx_obj o, double *out);                 /* @1 */
dx_obj dx_make_real(double v);                             /* @1 */
int    dx_get_real_text(dx_obj o, char *buf, int cap);     /* C string written: exact, 12-digit mantissa and exponent, d.dddddddddddEx (1/3 -> 3.33333333333E-1) */ /* @1 */
dx_obj dx_make_real_text(const char *text);                /* C string */ /* @1 */
int    dx_get_complex(dx_obj o, double *re, double *im);   /* @1 */
dx_obj dx_make_complex(double re, double im);              /* @1 */
int    dx_get_bint(dx_obj o, uint64_t *out);               /* @1 */
dx_obj dx_make_bint(uint64_t v);                           /* @1 */
int    dx_string_len(dx_obj o);                            /* @1 */
int    dx_get_string(dx_obj o, char *buf, int cap);        /* byte range filled, no NUL: bytes copied, in the calculator charset */ /* @1 */
dx_obj dx_make_string(const char *bytes, int n);           /* byte range: n bytes, no NUL needed */ /* @1 */
int    dx_list_len(dx_obj o);                              /* @1 */
dx_obj dx_list_item(dx_obj o, int i);                      /* 1-based */ /* @1 */
dx_obj dx_make_list(const dx_obj *items, int n);           /* @1 */
int    dx_grob_size(dx_obj o, int *w, int *h);             /* @1 */
int    dx_get_grob_bits(dx_obj o, uint8_t *buf, int cap);  /* HP 48 GROB row layout */ /* @1 */
dx_obj dx_make_grob(int w, int h, const uint8_t *bits);    /* @1 */
int    dx_error(int code);                                 /* record a built-in error number; returns -1 */ /* @1 */
int    dx_error_msg(const char *msg);                      /* record a custom message (C string); returns -1 */ /* @1 */
int    dx_poll(void);                                      /* 1 when ON was pressed: return -1 promptly */ /* @1 */
int    dx_product(void);                                   /* @1 */
int    dx_fw_version(char *buf, int cap);                  /* C string written (e.g. "DM48-0.1"); its length */ /* @1 */
/* Animation and keys (GUIDE.md "Animation and keys"). dx_show: your own w x h frame (0-2048 each; HP 48 GROB row
   layout, (w+7)/8 bytes a row, lowest bit leftmost) on the whole screen at once, scaled by the largest whole factor
   that fits, centred, cut at x1 when too big. It uses no calculator memory. 0, or -1 with Bad Argument Value for a
   size out of range or a frame outside your memory. The normal screen comes back when the command returns. */
int    dx_show(int w, int h, const uint8_t *bits);         /* @1 */
uint32_t dx_ms(void);                                      /* ms from an arbitrary start, wrapping: use differences */ /* @1 */
int    dx_key(void);                                       /* next queued key as KEY numbers it (rc), 0 = none; never waits; never ON */ /* @1 */
/* Files (GUIDE.md "Files"). A path is a C string of at most 127 bytes, absolute ("/DM48/DATA/LOG.TXT"): names of
   printable ASCII without " * : < > ? \ |, separated by /, compared without case; a bad one is Bad file name (#E819).
   ANY path may be read, written, renamed and deleted: the calculator's own files too. A file number (1 or 2: at most 2
   open) is valid only during the command that opened it; files left open are closed when the command returns. Errors:
   No such file #E811, No such folder #E812, File exists #E813, Disk full #E814, Access denied #E815, Disk error #E816,
   File in use #E817, Too many open files #E818, Bad file name #E819; a bad file number, mode, count or buffer is Bad
   Argument Value. No folder can be listed (the calculator's system cannot). */
int    dx_file_open(const char *path, int mode);           /* DX_FILE_ mode: a file number, or -1 */ /* @1 */
int    dx_file_read(int f, void *buf, int n);              /* bytes read: fewer than n only at the end (0 there), or -1 */ /* @1 */
int    dx_file_write(int f, const void *buf, int n);       /* n, or -1 (a short write is Disk full) */ /* @1 */
int    dx_file_seek(int f, int pos);                       /* the position 0 .. size from the start: 0, or -1 */ /* @1 */
int    dx_file_tell(int f);                                /* the position, or -1 */ /* @1 */
int    dx_file_size(int f);                                /* the size now, or -1 */ /* @1 */
int    dx_file_close(int f);                               /* 0, or -1 (a written file may then be incomplete) */ /* @1 */
int    dx_file_delete(const char *path);                   /* a file, or an empty folder */ /* @1 */
int    dx_file_rename(const char *from, const char *to);   /* to must not exist; it may be in another folder */ /* @1 */
int    dx_file_mkdir(const char *path);                    /* 0 also when it exists; its parent must exist */ /* @1 */
int    dx_file_exists(const char *path);                   /* 1 a file of that name, 0 none, -1 a bad path */ /* @1 */
/* USB serial and infrared (GUIDE.md "USB serial and infrared"). Lines go over DMCP's USB serial console, channel 2,
   while SETUP's USB console is ON and a program on the PC has the port open (dx_serial_connected); a line is printable
   ASCII (0x20-0x7E) and tab, at most DX_SERIAL_LINE bytes out, up to 255 in, without its line end. Not connected:
   Not connected #E821. dx_ir_send: bytes for an HP 82240B printer through the infrared LED, paced by PRTPAR's delay
   (as PRINT's), at most 4096 a call. */
int    dx_serial_connected(void);                          /* 1 connected, 0 not */ /* @1 */
int    dx_serial_write(const char *line, int n);           /* one line of n bytes (its end added): 0, or -1 */ /* @1 */
int    dx_serial_read(char *buf, int cap);                 /* the next line received: its length, 0 none yet, or -1 */ /* @1 */
int    dx_ir_send(const uint8_t *bytes, int n);            /* 0, or -1 */ /* @1 */

/* An extension provides: */
typedef int (*dx_cmd_fn)(void);                /* 0 ok, nonzero = failed (error recorded or Bad Argument Value) */
int dx_init(void);                             /* calls dx_register for each command; returns 0. Not exported. */

/* Exported by the firmware; only valid inside dx_init. */
int dx_register(const char *name, int nargs, dx_cmd_fn fn);   /* name: C string */ /* @1 */

#if defined(__wasm__) && !defined(DX_FIRMWARE)   /* DX_FIRMWARE: the firmware's own side, which defines them */
#include "dm48ext_wasm.h"                      /* the web build: each export's import name (generated) */
#endif

#ifdef __cplusplus
}
#endif

#endif /* DM48EXT_H */
