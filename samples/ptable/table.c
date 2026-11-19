// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
/*
 * table.c - PTABLE's screen: the layout of the periodic table on 400 x 240, moving the cursor, and drawing it all
 * into one frame with two small fonts. Pure C: no dx_* calls, so the host tests can draw it and look at it.
 *
 * Layout: cells of 22 x 22 pixels, 18 columns from x = 2. Periods 1-7 from y = 0, then a 6-pixel gap and the
 * lanthanide and actinide rows from y = 160, under groups 3-17. The info box fills the empty corner of groups 3-12,
 * periods 1-3. The legend and the keys are at the bottom, from y = 206.
 */
#include "chem.h"

enum { W = 400, H = 240, ROW = W / 8, CELL = 22, X0 = 2, FROW_Y = 160, LEGEND_Y = 206,
       BOX_X = X0 + 2 * CELL, BOX_W = 10 * CELL, BOX_H = 3 * CELL };

uint8_t frame[ROW * H];                         /* the picture, HP 48 GROB rows: in .bss, never on the stack */

/* ---- layout and navigation ---- */

static int cell_y(int row) { return row < 7 ? row * CELL : FROW_Y + (row - 7) * CELL; }

/* The element at row, col, or 0. */
static int z_at(int row, int col) {
  for (int i = 0; i < NELEM; i++)
    if (elements[i].row == row && elements[i].col == col) return i + 1;
  return 0;
}

int nav(int z, int dir) {
  const elem_t *e = elem_by_z(z);
  if (!e) return 1;
  int dr = dir == DIR_UP ? -1 : dir == DIR_DOWN ? 1 : 0, dc = dir == DIR_LEFT ? -1 : dir == DIR_RIGHT ? 1 : 0;
  for (int r = e->row + dr, c = e->col + dc; r >= 0 && r <= 8 && c >= 0 && c <= 17; r += dr, c += dc) {
    int n = z_at(r, c);
    if (n) return n;
  }
  return z;                                     /* nothing that way: stay */
}

/* ---- pixels ---- */

static void put(int x, int y, int on) {
  if (x < 0 || x >= W || y < 0 || y >= H) return;
  uint8_t *b = frame + y * ROW + (x >> 3), m = (uint8_t)(1u << (x & 7));   /* the lowest bit leftmost */
  *b = on ? (uint8_t)(*b | m) : (uint8_t)(*b & ~m);
}
static void flip(int x, int y) {
  if (x >= 0 && x < W && y >= 0 && y < H) frame[y * ROW + (x >> 3)] ^= (uint8_t)(1u << (x & 7));
}
static void fill(int x, int y, int w, int h, int on) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) put(i, j, on);
}
static void outline(int x, int y, int w, int h) {
  for (int i = x; i < x + w; i++) { put(i, y, 1); put(i, y + h - 1, 1); }
  for (int j = y; j < y + h; j++) { put(x, j, 1); put(x + w - 1, j, 1); }
}

/* ---- fonts ---- */

/* 5 x 7, ASCII 32-126 and then the degree sign (DEG): 5 columns a glyph, the lowest bit the top row. */
enum { DEG = 127 };
static const uint8_t font5x7[96][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
  {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
  {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
  {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
  {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
  {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x02,0x01,0x02,0x04,0x02},{0x00,0x06,0x09,0x09,0x06},
};

/* Text at x, y (the top left), each font pixel s x s, on = 1 dark or 0 light. Returns the x after it. */
static int text(int x, int y, int s, const char *t, int on) {
  for (; *t; t++, x += 6 * s) {
    int c = (unsigned char)*t;
    if (c < 32 || c > DEG) c = '?';
    const uint8_t *g = font5x7[c - 32];
    for (int col = 0; col < 5; col++)
      for (int r = 0; r < 7; r++)
        if (g[col] >> r & 1) fill(x + col * s, y + r * s, s, s, on);
  }
  return x;
}
static int text_w(const char *t) { int n = 0; while (t[n]) n++; return n ? n * 6 - 1 : 0; }

/* 3 x 5 digits for the atomic numbers: 15 bits a digit, row 0 in bits 0-2, the lowest bit leftmost. */
static const uint16_t digits3x5[10] = { 0x7B6F, 0x7493, 0x73E7, 0x79E7, 0x49ED, 0x79CF, 0x7BCF, 0x4927, 0x7BEF, 0x79EF };
static void number3x5(int x, int y, int v) {
  char d[4];
  int n = 0;
  do { d[n++] = (char)(v % 10); v /= 10; } while (v && n < 4);
  for (int i = n - 1; i >= 0; i--, x += 4)
    for (int b = 0; b < 15; b++)
      if (digits3x5[(int)d[i]] >> b & 1) put(x + b % 3, y + b / 3, 1);
}

/* ---- the info box's text ---- */

/* Append the C string t to buf (k bytes so far); the new length. */
static int cat(char *buf, int k, int cap, const char *t) {
  while (*t && k < cap - 1) buf[k++] = *t++;
  buf[k] = 0;
  return k;
}

int info_line(int z, int i, char *buf, int cap) {
  const elem_t *e = elem_by_z(z);
  char num[4] = { 0 };
  int k = 0;
  if (cap < 1) return 0;
  buf[0] = 0;
  if (!e) return 0;
  const char *deg = "\x7F" "C";
  switch (i) {
  case 0:                                       /* 26 Iron */
    num[0] = (char)('0' + z / 100); num[1] = (char)('0' + z / 10 % 10); num[2] = (char)('0' + z % 10);
    k = cat(buf, k, cap, num + (z < 10 ? 2 : z < 100 ? 1 : 0));
    k = cat(buf, k, cap, " ");
    return cat(buf, k, cap, ES(e, name));
  case 1:                                       /* 55.845 u */
    k = cat(buf, k, cap, ES(e, mass));
    return cat(buf, k, cap, " u");
  case 2:
    return cat(buf, k, cap, cat_names[e->cat]);
  case 3:
    return cat(buf, k, cap, ES(e, config));
  case 4:                                       /* EN 1.83  7.87 g/cm3 */
    k = cat(buf, k, cap, "EN ");
    k = cat(buf, k, cap, *ES(e, en) ? ES(e, en) : "-");
    k = cat(buf, k, cap, "  ");
    if (!*ES(e, density)) return cat(buf, k, cap, "density -");
    k = cat(buf, k, cap, ES(e, density));
    return cat(buf, k, cap, e->gas ? " g/L" : " g/cm3");
  case 5:                                       /* mp 1538  bp 2861 °C, or sublimes 3825 °C */
    if (e->subl) {
      k = cat(buf, k, cap, "sublimes ");
      k = cat(buf, k, cap, ES(e, bp));
      k = cat(buf, k, cap, " ");
      return cat(buf, k, cap, deg);
    }
    k = cat(buf, k, cap, "mp ");
    k = cat(buf, k, cap, *ES(e, mp) ? ES(e, mp) : "-");
    k = cat(buf, k, cap, "  bp ");
    k = cat(buf, k, cap, *ES(e, bp) ? ES(e, bp) : "-");
    if (*ES(e, mp) || *ES(e, bp)) { k = cat(buf, k, cap, " "); k = cat(buf, k, cap, deg); }
    return k;
  }
  return 0;
}

/* ---- drawing ---- */

/* The categories' background patterns: 4 x 4, bit y * 4 + x. Light, so the text stays readable. */
static const uint16_t patterns[NCAT] = {
  0x8421,                                       /* alkali metal: diagonal lines */
  0x1248,                                       /* alkaline earth metal: the other diagonal */
  0x0000,                                       /* transition metal: plain (the most numerous) */
  0x000F,                                       /* post-transition metal: horizontal lines */
  0x1111,                                       /* metalloid: vertical lines */
  0x0001,                                       /* nonmetal: sparse dots */
  0x0505,                                       /* halogen: dense dots */
  0x111F,                                       /* noble gas: a grid */
  0x0401,                                       /* lanthanide: staggered dots */
  0x0A05,                                       /* actinide: a denser stagger */
};
static const char *const legend[NCAT] = {
  "Alkali", "Alk.earth", "Transition", "Post-tr.", "Metalloid", "Nonmetal", "Halogen", "Noble gas", "Lanthan.",
  "Actinide"
};

static void pattern(int x, int y, int w, int h, uint16_t p) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++)
      if (p >> ((j & 3) * 4 + (i & 3)) & 1) put(i, j, 1);
}

static void cell(const elem_t *e, int selected) {
  int x = X0 + e->col * CELL, y = cell_y(e->row);
  outline(x, y, CELL - 1, CELL - 1);
  pattern(x + 1, y + 1, CELL - 3, CELL - 3, patterns[e->cat]);
  const char *sym = ES(e, sym);
  int sw = text_w(sym), sx = x + (CELL - 1 - sw) / 2;
  fill(x + 1, y + 1, e->z >= 100 ? 13 : e->z >= 10 ? 9 : 5, 7, 0);   /* clear behind the number and the symbol */
  fill(sx - 1, y + 10, sw + 2, 9, 0);
  number3x5(x + 2, y + 2, e->z);
  text(sx, y + 11, 1, sym, 1);
  if (selected)
    for (int j = y; j < y + CELL - 1; j++)
      for (int i = x; i < x + CELL - 1; i++) flip(i, j);
}

static void info_box(int z) {
  const elem_t *e = elem_by_z(z);
  char line[48];
  outline(BOX_X, 0, BOX_W - 1, BOX_H - 1);
  text(BOX_X + 4, 4, 3, ES(e, sym), 1);       /* the symbol, three times the size */
  for (int i = 0; i < 6; i++) {
    info_line(z, i, line, sizeof line);
    if (i < 3) text(BOX_X + 46, 4 + i * 10, 1, line, 1);
    else text(BOX_X + 4, 33 + (i - 3) * 10, 1, line, 1);
  }
}

void draw_table(int z) {
  for (int i = 0; i < ROW * H; i++) frame[i] = 0;
  for (int i = 0; i < NELEM; i++) cell(&elements[i], i + 1 == z);
  /* the markers: where the lanthanides and the actinides belong, and their rows' labels */
  text(X0 + 2 * CELL + 8, cell_y(5) + 7, 1, "*", 1);
  text(X0 + 2 * CELL + 5, cell_y(6) + 7, 1, "**", 1);
  text(X0 + CELL + 8, cell_y(7) + 7, 1, "*", 1);
  text(X0 + CELL + 5, cell_y(8) + 7, 1, "**", 1);
  info_box(z);
  /* the legend: two rows of five, a swatch and a name */
  for (int i = 0; i < NCAT; i++) {
    int x = X0 + (i % 5) * 80, y = LEGEND_Y + 1 + (i / 5) * 10;
    outline(x, y, 9, 9);
    pattern(x + 1, y + 1, 7, 7, patterns[i]);
    text(x + 12, y + 1, 1, legend[i], 1);
  }
  text(X0, LEGEND_Y + 24, 1, "Arrows: move   ENTER: element to the stack   <-: quit", 1);
}
