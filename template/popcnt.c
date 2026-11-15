// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
/*
 * popcnt.c - the template extension. Two commands:
 *   POPCNT  ( #n -> r )   ( r -> r )   the number of set bits of a binary integer, or of a real that is a
 *                                      whole number from 0 to 2^64-1, as a real.
 *   REVSTR  ( "abc" -> "cba" )         the string reversed, byte by byte.
 * Copy this directory to start your own extension: rename the file and NAME in the Makefile.
 */
#include "dm48ext.h"

/* The number of set bits. */
static int bits(uint64_t v) {
  int n = 0;
  for (; v; v >>= 1) n += (int)(v & 1);
  return n;
}

static int popcnt(void) {
  dx_obj x = dx_peek(1);                        /* the firmware has checked that 1 argument is there */
  uint64_t v;
  switch (dx_type(x)) {
  case DX_BINT:
    if (dx_get_bint(x, &v)) return -1;          /* a failed dx_* call: the error is recorded, just return */
    break;
  case DX_REAL: {
    double d;
    if (dx_get_real(x, &d)) return -1;
    if (!(d >= 0 && d < 18446744073709551616.0)) return dx_error(DX_ERR_BAD_VALUE);
    v = (uint64_t)d;
    if ((double)v != d) return dx_error(DX_ERR_BAD_VALUE);   /* not a whole number */
    break;
  }
  default:
    return dx_error(DX_ERR_BAD_TYPE);
  }
  dx_obj r = dx_make_real(bits(v));
  if (!r || dx_drop(1) || dx_push(r)) return -1;
  return 0;
}

/* The longest string REVSTR takes: the extension's own buffer (in .bss; there is no malloc). */
enum { REV_MAX = 1024 };
static char buf[REV_MAX];

static int revstr(void) {
  dx_obj s = dx_peek(1);
  if (dx_type(s) != DX_STRING) return dx_error(DX_ERR_BAD_TYPE);
  int n = dx_string_len(s);
  if (n < 0) return -1;
  if (n > REV_MAX) return dx_error_msg("String too long");
  if (dx_get_string(s, buf, n) != n) return -1;
  for (int i = 0, j = n - 1; i < j; i++, j--) {
    char c = buf[i];
    buf[i] = buf[j];
    buf[j] = c;
  }
  dx_obj r = dx_make_string(buf, n);
  if (!r || dx_drop(1) || dx_push(r)) return -1;
  return 0;
}

/* Called once when the extension is loaded: register the commands, nothing else. */
int dx_init(void) {
  if (dx_register("POPCNT", 1, popcnt)) return -1;
  if (dx_register("REVSTR", 1, revstr)) return -1;
  return 0;
}
