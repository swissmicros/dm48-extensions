// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
/*
 * bench.c - three small benchmarks to set against the same work in User RPL (BENCH.TXT times both with TICKS):
 *   SIEVE  ( n -> count )      the number of primes up to n (n up to 1 000 000), by the sieve of Eratosthenes.
 *   PIDIG  ( n -> "3.14..." )  the first n digits of pi (n from 1 to 1000), by the Rabinowitz-Wagon spigot.
 *   PISUM  ( n -> x )          4 * (1 - 1/3 + 1/5 - ...), n terms (n up to 10 000 000), in double.
 * SIEVE and PIDIG are integer work. PISUM is double work: the DM48's FPU is single precision only, so every double
 * operation is a call into libgcc's software arithmetic. Their timings say how far apart the two kinds are.
 *
 * Memory: an extension has 64 KB in all (image, .bss and its 2 KB stack). The working buffers live in .bss, and the
 * two commands' buffers share it (a union): a command never runs while another one does.
 */
#include "dm48ext.h"

/* ---- arguments ---- */

/* The real at level 1 as a whole number from lo to hi; -1 with the error recorded otherwise (a fraction too). */
static int arg_int(int lo, int hi, int *out) {
  dx_obj x = dx_peek(1);
  double d;
  if (dx_type(x) != DX_REAL) return dx_error(DX_ERR_BAD_TYPE);
  if (dx_get_real(x, &d)) return -1;
  if (!(d >= lo && d <= hi)) return dx_error(DX_ERR_BAD_VALUE);
  *out = (int)d;
  if (*out != d) return dx_error(DX_ERR_BAD_VALUE);   /* not a whole number */
  return 0;
}

/* Drop the argument and push r. */
static int replace(dx_obj r) {
  if (!r || dx_drop(1) || dx_push(r)) return -1;
  return 0;
}

/* ---- the working memory, shared by SIEVE and PIDIG ---- */

enum {
  SIEVE_MAX = 1000000,
  BASE_MAX = 1000,                               /* sqrt(SIEVE_MAX): the base primes go up to here */
  SEG_BITS = 32768,                              /* one segment: 32768 odd numbers, 4 KB of bits */
  PI_MAX = 1000,                                 /* PIDIG's most digits */
  PI_EXTRA = 8,                                  /* spare digits: the spigot's last few may still change */
  PI_LEN = (10 * (PI_MAX + PI_EXTRA)) / 3 + 1    /* the spigot's mixed-radix digits */
};

static union {
  struct {
    uint8_t seg[SEG_BITS / 8];                   /* bit i set: lo + 2i is composite */
    uint8_t small[BASE_MAX / 2 + 1];             /* the base sieve over the odd numbers up to BASE_MAX */
    uint16_t primes[200];                        /* the odd primes up to BASE_MAX (there are 167) */
  } s;
  struct {
    uint32_t a[PI_LEN];
    char out[PI_MAX + PI_EXTRA + 2];             /* the digits, then "3." in front */
  } p;
} w;

/* ---- SIEVE ---- */

/* The sieve runs over the odd numbers only (2 is counted on its own), one segment of SEG_BITS at a time: a bitset
   for the whole range up to 1 000 000 (62.5 KB, odd numbers only) would not fit in the 64 KB. The base primes up to
   sqrt(n) are found first, by a small sieve; each segment is then crossed out by them. */
static int sieve(void) {
  int n;
  if (arg_int(0, SIEVE_MAX, &n)) return -1;
  int count = n >= 2;                            /* the prime 2 */

  /* the odd base primes p with p * p <= n */
  int np = 0;
  for (int i = 0; i <= BASE_MAX / 2; i++) w.s.small[i] = 0;
  for (int p = 3; p * p <= n; p += 2) {
    if (w.s.small[p / 2]) continue;
    w.s.primes[np++] = (uint16_t)p;
    for (int m = p * p; m <= BASE_MAX; m += 2 * p) w.s.small[m / 2] = 1;
  }

  /* the segments: bit i stands for the odd number lo + 2i */
  for (int lo = 3; lo <= n; lo += 2 * SEG_BITS) {
    if (dx_poll()) return -1;                    /* ON: give up (a segment is a few ms of work) */
    int hi = lo + 2 * (SEG_BITS - 1);            /* the last odd number of the segment */
    if (hi > n) hi = n;
    for (int i = 0; i < SEG_BITS / 8; i++) w.s.seg[i] = 0;
    for (int k = 0; k < np; k++) {
      if ((k & 15) == 0 && dx_poll()) return -1;
      int p = w.s.primes[k];
      int m = p * p;                             /* below p * p a smaller prime has crossed it out */
      if (m < lo) {
        m = (lo + p - 1) / p * p;                /* the first multiple of p from lo */
        if (!(m & 1)) m += p;                    /* odd multiples only */
      }
      for (; m <= hi; m += 2 * p) {
        int i = (m - lo) >> 1;
        w.s.seg[i >> 3] |= (uint8_t)(1u << (i & 7));
      }
    }
    for (int i = 0, last = (hi - lo) >> 1; i <= last; i++)
      if (!(w.s.seg[i >> 3] & (1u << (i & 7)))) count++;
  }
  return replace(dx_make_real(count));
}

/* ---- PIDIG ---- */

/* The Rabinowitz-Wagon spigot: pi = 2 + 1/3 (2 + 2/5 (2 + 3/7 (2 + ...))). The array holds pi's digits in that
   mixed radix; each pass multiplies it by 10 and carries from the right, and the carry out of the left is the next
   decimal digit. A digit of 10 is a carry into the digits already written (...399 + 1 = ...400). The integer work
   fits in 32 bits for 1000 digits: a[i] * 10 + q * i < 2^32. */
static int pidig(void) {
  int n;
  if (arg_int(1, PI_MAX, &n)) return -1;
  int want = n + PI_EXTRA, len = (10 * want) / 3 + 1, k = 0;
  for (int i = 0; i < len; i++) w.p.a[i] = 2;
  for (int j = 0; j < want; j++) {
    if (dx_poll()) return -1;                    /* ON: give up (one pass is up to 3361 divisions) */
    uint32_t q = 0;
    for (int i = len; i > 0; i--) {
      uint32_t x = 10 * w.p.a[i - 1] + q * (uint32_t)i;
      uint32_t d = 2 * (uint32_t)i - 1;
      q = x / d;
      w.p.a[i - 1] = x - q * d;
    }
    w.p.a[0] = q % 10;
    q /= 10;
    if (q == 10) {                               /* carry into the digits written: 9s become 0s */
      int b = k - 1;
      while (b >= 0 && w.p.out[b] == '9') w.p.out[b--] = '0';
      if (b >= 0) w.p.out[b]++;
      q = 0;
    }
    w.p.out[k++] = (char)('0' + q);
  }
  /* "3." then the next n - 1 digits: move them one place on, for the point */
  for (int i = n - 1; i >= 1; i--) w.p.out[i + 1] = w.p.out[i];
  w.p.out[1] = '.';
  return replace(dx_make_string(w.p.out, n == 1 ? 1 : n + 1));
}

/* ---- PISUM ---- */

/* The Leibniz series, n terms, in double: 4 - 4/3 + 4/5 - ... Each term is a software division and addition. */
static int pisum(void) {
  int n;
  if (arg_int(0, 10000000, &n)) return -1;
  double s = 0, sign = 4;
  for (int k = 0; k < n; k++) {
    if ((k & 255) == 0 && dx_poll()) return -1;  /* ON: give up (every 256 software-double terms) */
    s += sign / (2 * k + 1);
    sign = -sign;
  }
  return replace(dx_make_real(s));
}

int dx_init(void) {
  if (dx_register("SIEVE", 1, sieve)) return -1;
  if (dx_register("PIDIG", 1, pidig)) return -1;
  if (dx_register("PISUM", 1, pisum)) return -1;
  return 0;
}
