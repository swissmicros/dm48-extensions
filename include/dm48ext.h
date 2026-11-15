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
