// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
/*
 * mandel.c - the Mandelbrot set on the whole screen.
 *   MANDEL  ( -> grob ms )   draws the set, 400 x 240 pixels at 64 iterations, x from -2.5 to 1 and y from -1.05 to
 *                            1.05, showing it as it goes, 8 rows at a time; then gives the picture as a GROB and the
 *                            time it took in milliseconds (MANDEL.TXT tags it :ms:). ON abandons it: nothing pushed.
 *
 * The arithmetic is 32-bit fixed point, Q4.27 (4 integer bits with the sign, 27 fraction bits): the DM48's FPU is
 * single precision only, so a double would be software arithmetic, many times slower, and a float has too few digits
 * for a deep zoom. A product of two Q4.27 numbers is one 32 x 32 -> 64-bit multiply (SMULL) and a shift.
 */
#include "dm48ext.h"

enum { W = 400, H = 240, ROW = (W + 7) / 8, MAXIT = 64, BAND = 8, FRAC = 27 };

static uint8_t frame[ROW * H];                   /* the picture, HP 48 GROB rows: in .bss, never on the stack */

#define FIX(v) ((int32_t)((v) * (double)(1 << FRAC)))   /* a constant in Q4.27, worked out by the compiler */

/* Iterations before z = z^2 + c leaves |z| <= 2, or MAXIT when it stays (c is in the set). */
static int escape(int32_t cx, int32_t cy) {
  int32_t x = 0, y = 0;
  for (int n = 0; n < MAXIT; n++) {
    int64_t xx = ((int64_t)x * x) >> FRAC, yy = ((int64_t)y * y) >> FRAC;
    if (xx + yy > ((int64_t)4 << FRAC)) return n;
    /* |z| <= 2 here, so the new x and y stay well inside Q4.27's +-16 */
    y = (int32_t)(((int64_t)x * y) >> (FRAC - 1)) + cy;   /* 2xy + cy */
    x = (int32_t)(xx - yy) + cx;
  }
  return MAXIT;
}

/* A 4 x 4 ordered dither: a pixel outside the set is dark when its shade passes the threshold at its place. */
static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

static int dark(int n, int px, int py) {
  if (n >= MAXIT) return 1;                      /* in the set: black */
  int shade = n <= 2 ? 0 : n - 2 < 16 ? n - 2 : 16;   /* 0 (escapes at once: white) .. 16 (slow to escape: dark) */
  return shade > bayer[py & 3][px & 3];
}

static int mandel(void) {
  const int32_t x0 = FIX(-2.5), y0 = FIX(1.05);  /* the top-left pixel's centre is half a step in from here */
  const int32_t step = FIX(3.5 / W);             /* 3.5 / 400 = 2.1 / 240: square pixels */
  uint32_t t0 = dx_ms();
  for (int i = 0; i < ROW * H; i++) frame[i] = 0;
  for (int band = 0; band < H; band += BAND) {
    for (int py = band; py < band + BAND; py++) {
      int32_t cy = y0 - step * py - step / 2;
      uint8_t *row = frame + py * ROW;
      for (int px = 0; px < W; px++) {
        int32_t cx = x0 + step * px + step / 2;
        if (dark(escape(cx, cy), px, py)) row[px >> 3] |= (uint8_t)(1u << (px & 7));   /* lowest bit leftmost */
      }
      if (dx_poll()) return -1;                  /* ON: give up, nothing pushed (a row at a time) */
    }
    if (dx_show(W, H, frame)) return -1;         /* the picture so far, at once */
    if (dx_poll()) return -1;                    /* ON: give up, nothing pushed */
  }
  uint32_t ms = dx_ms() - t0;
  dx_obj g = dx_make_grob(W, H, frame);
  if (!g || dx_push(g)) return -1;
  dx_obj t = dx_make_real(ms);
  if (!t || dx_push(t)) return -1;
  return 0;
}

int dx_init(void) { return dx_register("MANDEL", 0, mandel); }
