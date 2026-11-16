// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
/*
 * life.c - Conway's Game of Life on the whole screen.
 *   LIFE  ( n -> gens ms )   n% of the 400 x 240 cells alive at random (n from 0 to 100), then one generation after
 *                            another, each shown at once, with the frames per second so far at the top left. The edges
 *                            wrap round (a torus). Keys while it runs:
 *                              <- (backspace, KEY 55)  stop: gives the generations and the time in ms
 *                              +  (95) / -  (85)       the density 5 more / 5 less, and a new random start
 *                              any other key           a glider at a random place
 *                              ON                      abandon: nothing pushed (dx_poll's rule)
 *
 * A generation is computed 8 cells at a time: the frame's bytes are used as they are (HP 48 GROB rows, the lowest bit
 * leftmost), and the 8 neighbours of 8 cells are added with bitwise full adders.
 */
#include "dm48ext.h"

enum { W = 400, H = 240, ROW = W / 8, KEY_BACKSPACE = 55, KEY_PLUS = 95, KEY_MINUS = 85 };
/* The left and right edges wrap byte to byte (row_bits): that needs whole bytes in a row. */
_Static_assert(W % 8 == 0, "W must be a multiple of 8");

static uint8_t frames[2][ROW * H];               /* this generation and the next: in .bss, never on the stack */

/* ---- random numbers: xorshift32 (mine.c has the same few lines: each sample stands alone) ---- */

static uint32_t rng;
static uint32_t rnd(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
static uint32_t rnd_below(uint32_t n) { return (uint32_t)(((uint64_t)rnd() * n) >> 32); }

/* A seed from the clock: dx_ms mixed (the murmur3 finaliser), so that runs started a few ms apart differ from the
   first number on; xorshift alone would start nearly alike from nearby seeds. Never 0, which xorshift can't leave. */
static uint32_t seed(void) {
  uint32_t s = dx_ms() * 0x9E3779B9u;
  s ^= s >> 16;
  s *= 0x85EBCA6Bu;
  s ^= s >> 13;
  s *= 0xC2B2AE35u;
  s ^= s >> 16;
  return s ? s : 1;
}

/* ---- cells ---- */

static void set_cell(uint8_t *f, int x, int y) {
  x = (x % W + W) % W;                           /* wrap round */
  y = (y % H + H) % H;
  f[y * ROW + (x >> 3)] |= (uint8_t)(1u << (x & 7));
}

/* Each cell alive with probability pct %. */
static void randomize(uint8_t *f, int pct) {
  uint32_t lim = (uint32_t)((uint64_t)pct * 0xFFFFFFFFu / 100);
  for (int i = 0; i < ROW * H; i++) {
    uint8_t b = 0;
    for (int k = 0; k < 8; k++) if (pct && rnd() <= lim) b |= (uint8_t)(1u << k);
    f[i] = b;
  }
}

/* A glider, heading down and to the right, its top-left corner at x, y. */
static void glider(uint8_t *f, int x, int y) {
  set_cell(f, x + 1, y);
  set_cell(f, x + 2, y + 1);
  set_cell(f, x, y + 2);
  set_cell(f, x + 1, y + 2);
  set_cell(f, x + 2, y + 2);
}

/* ---- one generation ---- */

/* Bitwise adders: each bit position is a separate sum. */
static void full_add(uint8_t a, uint8_t b, uint8_t c, uint8_t *sum, uint8_t *carry) {
  uint8_t t = a ^ b;
  *sum = t ^ c;
  *carry = (uint8_t)((a & b) | (t & c));
}

/* The byte at row y, byte column j, with its west and east neighbours' bits shifted in (wrapping). In a byte the
   lowest bit is the leftmost cell, so a cell's west neighbour is the next lower bit. */
static void row_bits(const uint8_t *f, int y, int j, uint8_t *w, uint8_t *c, uint8_t *e) {
  const uint8_t *r = f + y * ROW;
  uint8_t left = r[(j + ROW - 1) % ROW], mid = r[j], right = r[(j + 1) % ROW];
  *c = mid;
  *w = (uint8_t)((mid << 1) | (left >> 7));      /* bit k: the cell at k - 1 */
  *e = (uint8_t)((mid >> 1) | (right << 7));     /* bit k: the cell at k + 1 */
}

/* Row y of the next generation of cur into nxt: a cell lives with 3 neighbours, or with 2 when it is alive. */
static void step_row(const uint8_t *cur, uint8_t *nxt, int y) {
  int up = (y + H - 1) % H, down = (y + 1) % H;
  for (int j = 0; j < ROW; j++) {
    uint8_t nw, n, ne, w, c, e, sw, s, se;
    row_bits(cur, up, j, &nw, &n, &ne);
    row_bits(cur, y, j, &w, &c, &e);
    row_bits(cur, down, j, &sw, &s, &se);
    /* the 8 neighbours: ones + 2 * (c1 + c2 + c3 + c4) */
    uint8_t s1, c1, s2, c2, s3, c3, ones, c4, t, d1, twos, d2;
    full_add(nw, n, ne, &s1, &c1);
    full_add(w, e, sw, &s2, &c2);
    s3 = s ^ se;
    c3 = s & se;
    full_add(s1, s2, s3, &ones, &c4);
    /* c1 + c2 + c3 + c4 = twos + 2 * (d1 + d2): with d1 or d2 set there are 4 or more neighbours */
    full_add(c1, c2, c3, &t, &d1);
    twos = t ^ c4;
    d2 = t & c4;
    nxt[y * ROW + j] = (uint8_t)(~(d1 | d2) & twos & (ones | c));
  }
}

/* ---- the frames-per-second label: a 3 x 5 digit font, drawn twice the size ---- */

static const uint16_t digits[10] = {             /* 15 bits a digit: row 0 in bits 0-2, the lowest bit leftmost */
  0x7B6F, 0x7493, 0x73E7, 0x79E7, 0x49ED, 0x79CF, 0x7BCF, 0x4927, 0x7BEF, 0x79EF
};
enum { LBL_ROWS = 14, LBL_BYTES = 5 };           /* the label's box: 40 x 14 pixels at the top left */
static uint8_t under[LBL_ROWS * LBL_BYTES];      /* the cells the label covers, put back after the show */

static void pixel(uint8_t *f, int x, int y, int on) {
  uint8_t *b = f + y * ROW + (x >> 3), m = (uint8_t)(1u << (x & 7));
  *b = on ? (uint8_t)(*b | m) : (uint8_t)(*b & ~m);
}

/* v (0 .. 9999) in a white box at the top left of f, its cells saved in under first. */
static void label(uint8_t *f, uint32_t v) {
  for (int y = 0; y < LBL_ROWS; y++)
    for (int j = 0; j < LBL_BYTES; j++) { under[y * LBL_BYTES + j] = f[y * ROW + j]; f[y * ROW + j] = 0; }
  if (v > 9999) v = 9999;
  char d[4];
  int n = 0;
  do { d[n++] = (char)(v % 10); v /= 10; } while (v);
  for (int i = 0; i < n; i++) {
    uint16_t g = digits[(int)d[n - 1 - i]];
    for (int r = 0; r < 5; r++)
      for (int col = 0; col < 3; col++)
        if (g >> (r * 3 + col) & 1) {
          int x = 2 + i * 8 + col * 2, y = 2 + r * 2;
          pixel(f, x, y, 1); pixel(f, x + 1, y, 1); pixel(f, x, y + 1, 1); pixel(f, x + 1, y + 1, 1);
        }
  }
}
static void unlabel(uint8_t *f) {
  for (int y = 0; y < LBL_ROWS; y++)
    for (int j = 0; j < LBL_BYTES; j++) f[y * ROW + j] = under[y * LBL_BYTES + j];
}

/* ---- LIFE ---- */

static int life(void) {
  dx_obj a = dx_peek(1);
  double d;
  if (dx_type(a) != DX_REAL) return dx_error(DX_ERR_BAD_TYPE);
  if (dx_get_real(a, &d)) return -1;
  if (!(d >= 0 && d <= 100)) return dx_error(DX_ERR_BAD_VALUE);
  int pct = (int)d;

  rng = seed();
  int cur = 0;
  randomize(frames[cur], pct);

  uint32_t gens = 0, t0 = dx_ms();
  for (;;) {
    /* frames per second over the whole run so far, not over one frame */
    uint32_t ms = dx_ms() - t0;
    label(frames[cur], ms ? (uint32_t)((uint64_t)gens * 1000 / ms) : 0);
    int r = dx_show(W, H, frames[cur]);
    unlabel(frames[cur]);
    if (r) return -1;

    for (int k; (k = dx_key()) != 0;) {
      if (k == KEY_BACKSPACE) {                  /* the clean stop */
        dx_obj g = dx_make_real(gens), t = dx_make_real(dx_ms() - t0);
        if (!g || !t || dx_drop(1) || dx_push(g) || dx_push(t)) return -1;
        return 0;
      }
      if (k == KEY_PLUS || k == KEY_MINUS) {     /* another density, a new start */
        pct += k == KEY_PLUS ? 5 : -5;
        if (pct < 5) pct = 5;
        if (pct > 95) pct = 95;
        randomize(frames[cur], pct);
      } else {
        glider(frames[cur], (int)rnd_below(W), (int)rnd_below(H));
      }
    }

    if (dx_poll()) return -1;                    /* ON: abandon, nothing pushed */
    for (int y = 0; y < H; y++) step_row(frames[cur], frames[cur ^ 1], y);   /* the next generation */
    cur ^= 1;
    gens++;
  }
}

int dx_init(void) { return dx_register("LIFE", 1, life); }
