// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
/*
 * mine.c - a minefield to cross, on the whole screen.
 *   MINE  ( k -> score )   k mines (1 to 120; 30 when the argument is not a real) hidden in a field of 12 x 24
 *                          squares. You start at the top left and must reach the bottom right, one square at a time.
 *                          The top band shows how many mines are next to you, and your score: the squares visited.
 *   Keys: the arrows move straight; the digits 1-9 move like a numeric keypad (7, 9, 1 and 3 diagonally);
 *         <- (backspace, KEY 55) stops and gives the score; ON abandons (nothing pushed: dx_poll's rule).
 *   Reaching the goal wins; stepping on a mine loses and shows every mine. Either way the last picture stays until
 *   a key, and then the score is given.
 */
#include "dm48ext.h"

enum {
  W = 400, H = 240, ROW = W / 8,                 /* the screen, in HP 48 GROB rows */
  ROWS = 12, COLS = 24, SQ = 16,                 /* the field: 12 x 24 squares of 16 x 16 pixels, 384 x 192 */
  TOP = 48, LEFT = (W - COLS * SQ) / 2,          /* under a 48-pixel band, centred across */
  MINES_MAX = 120, MINES_DEFAULT = 30,
  DEALS_MAX = 100                                /* new_field's tries at a field with a way through */
};

/* The keys, as dx_key numbers them (the GUIDE's table: KEY's row-and-column codes). */
enum {
  KEY_UP = 25, KEY_LEFT = 34, KEY_DOWN = 35, KEY_RIGHT = 36, KEY_BACKSPACE = 55,
  KEY_7 = 62, KEY_8 = 63, KEY_9 = 64, KEY_4 = 72, KEY_6 = 74, KEY_1 = 82, KEY_2 = 83, KEY_3 = 84
};

static uint8_t frame[ROW * H];                   /* in .bss, never on the stack */
static uint8_t mine[ROWS][COLS], seen[ROWS][COLS];
static int pr, pc;                               /* the player's square */

/* ---- random numbers: xorshift32 (life.c has the same few lines: each sample stands alone) ---- */

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

/* ---- the game ---- */

/* k mines at random, never on the start (top left) or the goal (bottom right). */
static void deal(int k) {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) mine[r][c] = seen[r][c] = 0;
  for (int placed = 0; placed < k;) {
    int r = (int)rnd_below(ROWS), c = (int)rnd_below(COLS);
    if ((r == 0 && c == 0) || (r == ROWS - 1 && c == COLS - 1) || mine[r][c]) continue;
    mine[r][c] = 1;
    placed++;
  }
}

/* Whether the goal can be reached from the start without stepping on a mine, by the 8 moves: a flood fill from the
   start, marking in seen (cleared again by the caller). */
static int way_through(void) {
  seen[0][0] = 1;
  for (int grew = 1; grew;) {
    grew = 0;
    for (int r = 0; r < ROWS; r++)
      for (int c = 0; c < COLS; c++) {
        if (seen[r][c] || mine[r][c]) continue;
        for (int dr = -1; dr <= 1 && !seen[r][c]; dr++)
          for (int dc = -1; dc <= 1; dc++) {
            int y = r + dr, x = c + dc;
            if (y >= 0 && y < ROWS && x >= 0 && x < COLS && seen[y][x]) { seen[r][c] = 1; grew = 1; break; }
          }
      }
  }
  return seen[ROWS - 1][COLS - 1];
}

/* A new field with a way through (dealt again until there is one; after DEALS_MAX tries, as it is), the player at
   the start. */
static void new_field(int k) {
  for (int t = 0; t < DEALS_MAX; t++) {
    deal(k);
    if (way_through()) break;
  }
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) seen[r][c] = 0;
  pr = pc = 0;
  seen[0][0] = 1;
}

/* The mines on the (up to) 8 squares around r, c. */
static int near(int r, int c) {
  int n = 0;
  for (int dr = -1; dr <= 1; dr++)
    for (int dc = -1; dc <= 1; dc++) {
      int y = r + dr, x = c + dc;
      if ((dr || dc) && y >= 0 && y < ROWS && x >= 0 && x < COLS) n += mine[y][x];
    }
  return n;
}

/* The score: the squares visited. */
static int score(void) {
  int n = 0;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) n += seen[r][c];
  return n;
}

enum { PLAYING, WON, LOST };

/* One step by dr, dc: off the field it does nothing. */
static int step(int dr, int dc) {
  int r = pr + dr, c = pc + dc;
  if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return PLAYING;
  pr = r;
  pc = c;
  if (mine[r][c]) return LOST;
  seen[r][c] = 1;
  return r == ROWS - 1 && c == COLS - 1 ? WON : PLAYING;
}

/* The move of a key, as KEY numbers it (the GUIDE's dx_key table); 0 when the key is not a move. */
static int move_of(int key, int *dr, int *dc) {
  switch (key) {
  case KEY_UP: case KEY_8: *dr = -1; *dc = 0; return 1;
  case KEY_DOWN: case KEY_2: *dr = 1; *dc = 0; return 1;
  case KEY_LEFT: case KEY_4: *dr = 0; *dc = -1; return 1;
  case KEY_RIGHT: case KEY_6: *dr = 0; *dc = 1; return 1;
  case KEY_7: *dr = -1; *dc = -1; return 1;
  case KEY_9: *dr = -1; *dc = 1; return 1;
  case KEY_1: *dr = 1; *dc = -1; return 1;
  case KEY_3: *dr = 1; *dc = 1; return 1;
  }
  return 0;
}

/* ---- drawing ---- */

static void pixel(int x, int y) { frame[y * ROW + (x >> 3)] |= (uint8_t)(1u << (x & 7)); }
static void unpixel(int x, int y) { frame[y * ROW + (x >> 3)] &= (uint8_t)~(1u << (x & 7)); }
static void box(int x, int y, int w, int h) {    /* filled */
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) pixel(i, j);
}
static void unbox(int x, int y, int w, int h) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) unpixel(i, j);
}

/* A 3 x 5 font: 15 bits a glyph, row 0 in bits 0-2, the lowest bit leftmost. */
#define G(a, b, c, d, e) ((a) | (b) << 3 | (c) << 6 | (d) << 9 | (e) << 12)
static const char glyph_chars[] = "0123456789ABCEGIKLMNOPRSWY!";
static const uint16_t glyphs[] = {
  G(7, 5, 5, 5, 7), G(2, 3, 2, 2, 7), G(7, 4, 7, 1, 7), G(7, 4, 7, 4, 7), G(5, 5, 7, 4, 4),
  G(7, 1, 7, 4, 7), G(7, 1, 7, 5, 7), G(7, 4, 4, 4, 4), G(7, 5, 7, 5, 7), G(7, 5, 7, 4, 7),
  G(2, 5, 7, 5, 5), G(3, 5, 3, 5, 3), G(6, 1, 1, 1, 6), G(7, 1, 3, 1, 7), G(6, 1, 5, 5, 6),   /* A B C E G */
  G(7, 2, 2, 2, 7), G(5, 5, 3, 5, 5), G(1, 1, 1, 1, 7), G(5, 7, 7, 5, 5), G(3, 5, 5, 5, 5),   /* I K L M N */
  G(2, 5, 5, 5, 2), G(3, 5, 3, 1, 1), G(3, 5, 3, 5, 5), G(6, 1, 2, 4, 3), G(5, 5, 7, 7, 5),   /* O P R S W */
  G(5, 5, 2, 2, 2), G(2, 2, 2, 0, 2)                                                          /* Y ! */
};

/* text at x, y, each font pixel s x s; a space is a gap. Returns the x after it. */
static int text(int x, int y, int s, const char *t) {
  for (; *t; t++, x += 4 * s) {
    int g = -1;
    for (int i = 0; glyph_chars[i]; i++) if (glyph_chars[i] == *t) g = i;
    if (g < 0) continue;
    for (int r = 0; r < 5; r++)
      for (int c = 0; c < 3; c++)
        if (glyphs[g] >> (r * 3 + c) & 1) box(x + c * s, y + r * s, s, s);
  }
  return x;
}
static int number(int x, int y, int s, int v) {
  char d[8], t[8];
  int n = 0;
  do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
  for (int i = 0; i < n; i++) t[i] = d[n - 1 - i];
  t[n] = 0;
  return text(x, y, s, t);
}

/* One square: its outline, then what is on it. */
static void square(int r, int c, int over) {
  int x = LEFT + c * SQ, y = TOP + r * SQ;
  for (int i = 0; i < SQ; i++) {                 /* the grid: each square's bottom and right edges; the field's top
                                                    and left edges, just outside it, at the end */
    pixel(x + i, y + SQ - 1);
    pixel(x + SQ - 1, y + i);
  }
  int here = r == pr && c == pc, goal = r == ROWS - 1 && c == COLS - 1;
  if (over && mine[r][c]) {                      /* a mine: a disc with four spikes; the one stepped on, inverted */
    if (here) box(x + 2, y + 2, 12, 12);
    for (int j = -4; j <= 4; j++)
      for (int i = -4; i <= 4; i++)
        if (i * i + j * j <= 13 || (i == 0 && (j == 4 || j == -4)) || (j == 0 && (i == 4 || i == -4))) {
          if (here) unpixel(x + 8 + i, y + 8 + j); else pixel(x + 8 + i, y + 8 + j);
        }
    return;
  }
  if (here) {                                    /* the player: a solid square with a hole and a dot */
    box(x + 2, y + 2, 12, 12);
    unbox(x + 5, y + 5, 6, 6);
    box(x + 7, y + 7, 2, 2);
  } else if (seen[r][c]) {                       /* visited: filled */
    box(x + 3, y + 3, 10, 10);
  } else if (goal) {                             /* the goal: a chequered flag */
    for (int j = 0; j < 4; j++)
      for (int i = 0; i < 4; i++)
        if ((i + j) & 1) box(x + 4 + 2 * i, y + 4 + 2 * j, 2, 2);
    for (int i = 3; i < 13; i++) { pixel(x + i, y + 3); pixel(x + i, y + 12); pixel(x + 3, y + i); pixel(x + 12, y + i); }
  }
}

/* The whole screen: the band (mines near, the number of mines, the score, the end) and the field. */
static void draw(int k, int state) {
  for (int i = 0; i < ROW * H; i++) frame[i] = 0;
  text(LEFT, 6, 2, "NEAR");
  number(LEFT, 18, 5, near(pr, pc));             /* 15 x 25: the figure to watch */
  int x = text(LEFT + 64, 6, 2, "MINES") + 8;
  number(x, 6, 2, k);
  if (state == WON) text(LEFT + 64, 22, 4, "GOAL!");
  else if (state == LOST) text(LEFT + 64, 22, 4, "BOOM!");
  text(LEFT + COLS * SQ - 76, 6, 2, "SCORE");
  number(LEFT + COLS * SQ - 76, 18, 5, score());
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) square(r, c, state == LOST);
  for (int i = -1; i < COLS * SQ; i++) pixel(LEFT + i, TOP - 1);   /* the field's top and left edges */
  for (int i = 0; i < ROWS * SQ; i++) pixel(LEFT - 1, TOP + i);
}

/* ---- MINE ---- */

static int give(int v) {
  dx_obj s = dx_make_real(v);
  if (!s || dx_drop(1) || dx_push(s)) return -1;
  return 0;
}

static int mine_cmd(void) {
  int k = MINES_DEFAULT;
  dx_obj a = dx_peek(1);
  if (dx_type(a) == DX_REAL) {
    double d;
    if (dx_get_real(a, &d)) return -1;
    if (!(d >= 1 && d <= MINES_MAX)) return dx_error(DX_ERR_BAD_VALUE);
    k = (int)d;
  }
  rng = seed();
  new_field(k);

  int state = PLAYING;
  draw(k, state);
  if (dx_show(W, H, frame)) return -1;
  while (state == PLAYING) {
    if (dx_poll()) return -1;                    /* ON: abandon, nothing pushed */
    int key = dx_key(), dr, dc;
    if (!key) continue;
    if (key == KEY_BACKSPACE) return give(score());
    if (!move_of(key, &dr, &dc)) continue;
    state = step(dr, dc);
    draw(k, state);                              /* a frame only when something changed */
    if (dx_show(W, H, frame)) return -1;
  }
  while (dx_key()) {}                            /* keys pressed before the end don't count: the picture stays */
  for (;;) {                                     /* until a new key (this waits at full speed: there is no dx_ call
                                                    that sleeps) */
    if (dx_poll()) return -1;
    if (dx_key()) return give(score());
  }
}

int dx_init(void) { return dx_register("MINE", 1, mine_cmd); }
