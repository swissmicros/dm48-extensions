// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
/*
 * test_ptable.c - the ptable sample's host tests: `make test`. Built with the host's cc and libc (the extension
 * itself uses neither).
 */
#include <stdio.h>
#include <string.h>
#include "chem.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define STREQ(a, b) CHECK(strcmp((a), (b)) == 0)

/* ---- stub dx_* functions: a small fake calculator with a stack of objects ---- */

typedef struct {
  int type;                     /* DX_REAL, DX_STRING, DX_LIST */
  char text[512];               /* a real as text (dx_make_real_text, or %.12g of dx_make_real), or a string's bytes */
  int n;                        /* a string's length, or a list's item count */
  dx_obj items[16];
} fake_obj;

static fake_obj objs[4096];
static int nobjs;
static dx_obj stack[64];
static int depth;
static int last_error;          /* the code recorded by dx_error, 0x70000 for dx_error_msg */
static char last_msg[256];

static fake_obj *O(dx_obj h) { return h >= 1 && (int)h <= nobjs ? &objs[h - 1] : NULL; }
static dx_obj new_obj(int type) { objs[nobjs].type = type; objs[nobjs].n = 0; objs[nobjs].text[0] = 0; return (dx_obj)++nobjs; }

int dx_depth(void) { return depth; }
dx_obj dx_peek(int level) { return level >= 1 && level <= depth ? stack[depth - level] : 0; }
int dx_drop(int n) { if (n > depth) return -1; depth -= n; return 0; }
int dx_push(dx_obj o) { stack[depth++] = o; return 0; }
int dx_type(dx_obj o) { return O(o) ? O(o)->type : DX_OTHER; }
int dx_get_real(dx_obj o, double *out) {
  if (dx_type(o) != DX_REAL) return dx_error(DX_ERR_BAD_TYPE);
  sscanf(O(o)->text, "%lf", out);
  return 0;
}
dx_obj dx_make_real(double v) { dx_obj h = new_obj(DX_REAL); snprintf(O(h)->text, 512, "%.12g", v); return h; }
dx_obj dx_make_real_text(const char *text) {
  double v;
  char extra;
  if (sscanf(text, "%lf%c", &v, &extra) != 1) { dx_error(DX_ERR_BAD_VALUE); return 0; }   /* a number, all of it */
  dx_obj h = new_obj(DX_REAL);
  snprintf(O(h)->text, 512, "%s", text);
  return h;
}
int dx_string_len(dx_obj o) { return dx_type(o) == DX_STRING ? O(o)->n : dx_error(DX_ERR_BAD_TYPE); }
int dx_get_string(dx_obj o, char *buf, int cap) {
  if (dx_type(o) != DX_STRING) return dx_error(DX_ERR_BAD_TYPE);
  int n = O(o)->n < cap ? O(o)->n : cap;
  memcpy(buf, O(o)->text, (size_t)n);
  return n;
}
dx_obj dx_make_string(const char *bytes, int n) {
  dx_obj h = new_obj(DX_STRING);
  memcpy(O(h)->text, bytes, (size_t)n);
  O(h)->text[n] = 0;
  O(h)->n = n;
  return h;
}
int dx_list_len(dx_obj o) { return dx_type(o) == DX_LIST ? O(o)->n : -1; }
dx_obj dx_list_item(dx_obj o, int i) { return dx_type(o) == DX_LIST && i >= 1 && i <= O(o)->n ? O(o)->items[i - 1] : 0; }
dx_obj dx_make_list(const dx_obj *items, int n) {
  dx_obj h = new_obj(DX_LIST);
  for (int i = 0; i < n; i++) O(h)->items[i] = items[i];
  O(h)->n = n;
  return h;
}
int dx_error(int code) { last_error = code; return -1; }
int dx_error_msg(const char *msg) { last_error = 0x70000; snprintf(last_msg, sizeof last_msg, "%s", msg); return -1; }
/* Keys for PTABLE's loop: dx_key gives them in order, then 0. Once they have all been taken, dx_poll reports ON,
   so a loop that never stops by itself ends the test instead of hanging it. */
static int keys[32], nkeys, keyi, shows, show_w, show_h;
static void queue_keys(const int *k, int n) { memcpy(keys, k, sizeof(int) * (size_t)n); nkeys = n; keyi = 0; }
int dx_key(void) { return keyi < nkeys ? keys[keyi++] : (keyi++, 0); }
int dx_poll(void) { return keyi > nkeys; }
int dx_show(int w, int h, const uint8_t *bits) { (void)bits; shows++; show_w = w; show_h = h; return 0; }
uint32_t dx_ms(void) { return 0; }

static const char *reg_names[8];
static dx_cmd_fn reg_fns[8];
static int nreg;
int dx_register(const char *name, int nargs, dx_cmd_fn fn) { (void)nargs; reg_names[nreg] = name; reg_fns[nreg++] = fn; return 0; }

/* Run a registered command on the fake stack: its return value, with the stack restored on failure as the
   calculator does. */
static int run(const char *name) {
  if (!nreg) dx_init();
  for (int i = 0; i < nreg; i++)
    if (strcmp(reg_names[i], name) == 0) {
      dx_obj saved[64];
      int d = depth;
      memcpy(saved, stack, sizeof stack);
      last_error = 0;
      last_msg[0] = 0;
      int r = reg_fns[i]();
      if (r) { memcpy(stack, saved, sizeof stack); depth = d; }
      return r;
    }
  printf("no command %s\n", name);
  fails++;
  return -1;
}
static void push_real(double v) { dx_push(dx_make_real(v)); }
static void push_str(const char *s) { dx_push(dx_make_string(s, (int)strlen(s))); }
static const char *item_text(dx_obj list, int i) { fake_obj *o = O(dx_list_item(list, i)); return o ? o->text : "?"; }
static int item_type(dx_obj list, int i) { return dx_type(dx_list_item(list, i)); }

static void test_data(void) {
  const elem_t *fe = &elements[25];
  CHECK(fe->z == 26);
  STREQ(ES(fe, sym), "Fe");
  STREQ(ES(fe, name), "Iron");
  CHECK(fe->mass_e5 == 5584500);
  STREQ(cat_names[fe->cat], "Transition metal");
  CHECK(fe->row == 3 && fe->col == 7);
  CHECK(!fe->gas);
  CHECK(elements[0].gas);                                   /* H */
  STREQ(ES(&elements[42], mass), "[98]");                         /* Tc */
  CHECK(elements[42].mass_e5 == 9800000);
  CHECK(elements[56].row == 7 && elements[56].col == 2);    /* La */
  CHECK(elements[70].row == 7 && elements[70].col == 16);   /* Lu */
  CHECK(elements[88].row == 8 && elements[88].col == 2);    /* Ac */
  CHECK(elements[71].row == 5 && elements[71].col == 3);    /* Hf */
  CHECK(elements[117].row == 6 && elements[117].col == 17); /* Og */
  CHECK(elements[1].row == 0 && elements[1].col == 17);     /* He */
  CHECK(elements[5].subl && ES(&elements[5], mp)[0] == 0);        /* C sublimes */
  STREQ(ES(&elements[70], en), "1.27");                    /* Lu: Pauling 1.27 (the source had 1.0) */
  for (int i = 0; i < NELEM; i++) {
    CHECK(elements[i].z == i + 1);
    CHECK(ES(&elements[i], sym)[0] && ES(&elements[i], name)[0] && ES(&elements[i], mass)[0]);
    CHECK(elements[i].cat < NCAT);
  }
}

static void test_lookup(void) {
  CHECK(elem_by_text("Fe", 2) && elem_by_text("Fe", 2)->z == 26);
  CHECK(elem_by_text("iron", 4) && elem_by_text("iron", 4)->z == 26);
  CHECK(elem_by_text("IRON", 4) && elem_by_text("IRON", 4)->z == 26);
  CHECK(elem_by_text("fe", 2) == NULL);                 /* a symbol is exact case, and "fe" is no name */
  CHECK(elem_by_text("Co", 2) && elem_by_text("Co", 2)->z == 27);
  CHECK(elem_by_text("Ironx", 5) == NULL);
  CHECK(elem_by_text("", 0) == NULL);
  CHECK(elem_by_z(0) == NULL && elem_by_z(119) == NULL);
  CHECK(elem_by_z(1) == &elements[0] && elem_by_z(118) == &elements[117]);
}

/* The mass of a symbol, from the table: the expected values follow the CSV. */
static int64_t M(const char *sym) {
  const elem_t *e = elem_by_text(sym, (int)strlen(sym));
  return e ? (int64_t)e->mass_e5 : -1;
}

static int64_t mm(const char *s, char *err) {
  int64_t m = -1;
  err[0] = 0;
  return molmass(s, (int)strlen(s), &m, err, 64) ? -1 : m;
}

static void test_molmass(void) {
  char err[64];
  CHECK(mm("H2O", err) == 2 * M("H") + M("O"));
  CHECK(mm("Ca(OH)2", err) == M("Ca") + 2 * (M("O") + M("H")));
  CHECK(mm("CuSO4*5H2O", err) == M("Cu") + M("S") + 4 * M("O") + 5 * (2 * M("H") + M("O")));
  CHECK(mm("CuSO4.5H2O", err) == mm("CuSO4*5H2O", err));
  CHECK(mm("K4[Fe(CN)6]", err) == 4 * M("K") + M("Fe") + 6 * (M("C") + M("N")));
  CHECK(mm("Tc", err) == 9800000);
  CHECK(mm("CO", err) == M("C") + M("O"));
  CHECK(mm("Co", err) == M("Co"));
  CHECK(mm("((((((((H))))))))", err) == M("H"));       /* 8 deep: allowed */
  CHECK(mm("Na2CO3*10H2O", err) == 2 * M("Na") + M("C") + 3 * M("O") + 10 * (2 * M("H") + M("O")));
  /* errors: never a number */
  CHECK(mm("", err) == -1 && strcmp(err, "Bad formula") == 0);
  CHECK(mm("h2o", err) == -1 && strcmp(err, "Bad formula") == 0);
  CHECK(mm("H2O)", err) == -1 && strcmp(err, "Bad formula") == 0);
  CHECK(mm("(H2O", err) == -1 && strcmp(err, "Bad formula") == 0);
  CHECK(mm("(H2O]", err) == -1 && strcmp(err, "Bad formula") == 0);
  CHECK(mm("()", err) == -1 && strcmp(err, "Bad formula") == 0);
  CHECK(mm("Xx", err) == -1 && strcmp(err, "Unknown element: Xx") == 0);
  CHECK(mm("H0", err) == -1 && strcmp(err, "Bad formula") == 0);
  CHECK(mm("H02", err) == -1 && strcmp(err, "Bad formula") == 0);
  CHECK(mm("H99999", err) == -1 && strcmp(err, "Bad formula") == 0);
  CHECK(mm("H9999", err) == 9999 * M("H"));
  CHECK(mm("H2O*", err) == -1 && strcmp(err, "Bad formula") == 0);
  CHECK(mm("*H2O", err) == -1 && strcmp(err, "Bad formula") == 0);
  CHECK(mm("H 2O", err) == -1 && strcmp(err, "Bad formula") == 0);
  CHECK(mm("(((((((((H)))))))))", err) == -1 && strcmp(err, "Formula too deep") == 0);
  CHECK(mm("Og9999(Og9999(Og9999(Og9999)9999)9999)9999", err) == -1 && strcmp(err, "Bad formula") == 0);
}

static void test_fmt_mass(void) {
  char b[32];
  CHECK(fmt_mass(7409300, b, sizeof b) == 6); STREQ(b, "74.093");
  CHECK(fmt_mass(9800000, b, sizeof b) == 2); STREQ(b, "98");
  fmt_mass(1, b, sizeof b); STREQ(b, "0.00001");
  fmt_mass(0, b, sizeof b); STREQ(b, "0");
  fmt_mass(2496850012345LL, b, sizeof b); STREQ(b, "24968500.12345");
}

static void check_fe_list(dx_obj l) {
  CHECK(dx_type(l) == DX_LIST && dx_list_len(l) == 10);
  CHECK(item_type(l, 1) == DX_REAL); STREQ(item_text(l, 1), "26");
  CHECK(item_type(l, 2) == DX_STRING); STREQ(item_text(l, 2), "Fe");
  STREQ(item_text(l, 3), "Iron");
  CHECK(item_type(l, 4) == DX_REAL); STREQ(item_text(l, 4), "55.845");
  STREQ(item_text(l, 5), "Transition metal");
  CHECK(item_type(l, 6) == DX_REAL); STREQ(item_text(l, 6), "1.83");
  STREQ(item_text(l, 7), "7.87");
  STREQ(item_text(l, 8), "1538");
  STREQ(item_text(l, 9), "2861");
  CHECK(item_type(l, 10) == DX_STRING); STREQ(item_text(l, 10), "[Ar] 3d6 4s2");
}

static void test_commands(void) {
  depth = 0;
  push_real(26);
  CHECK(run("ELEM") == 0 && depth == 1);
  check_fe_list(dx_peek(1));
  depth = 0;
  push_str("Fe");
  CHECK(run("ELEM") == 0 && depth == 1);
  check_fe_list(dx_peek(1));
  depth = 0;
  push_str("iron");
  CHECK(run("ELEM") == 0);
  check_fe_list(dx_peek(1));

  depth = 0;                                              /* Tc: the bracketed mass is the number 98 */
  push_real(43);
  CHECK(run("ELEM") == 0);
  STREQ(item_text(dx_peek(1), 4), "98");
  depth = 0;                                              /* He: no mp, so the empty string */
  push_str("He");
  CHECK(run("ELEM") == 0);
  CHECK(item_type(dx_peek(1), 8) == DX_STRING); STREQ(item_text(dx_peek(1), 8), "");
  STREQ(item_text(dx_peek(1), 7), "0.164");               /* g/L */

  depth = 0;
  push_real(0);
  CHECK(run("ELEM") == -1 && last_error == DX_ERR_BAD_VALUE && depth == 1);
  push_real(119);
  CHECK(run("ELEM") == -1 && last_error == DX_ERR_BAD_VALUE);
  push_real(2.5);
  CHECK(run("ELEM") == -1 && last_error == DX_ERR_BAD_VALUE);
  push_str("Xx");
  CHECK(run("ELEM") == -1 && last_error == DX_ERR_BAD_VALUE);
  dx_push(dx_make_list(NULL, 0));
  CHECK(run("ELEM") == -1 && last_error == DX_ERR_BAD_TYPE);

  depth = 0;
  push_str("H2O");
  CHECK(run("MOLMASS") == 0 && depth == 1);
  CHECK(dx_type(dx_peek(1)) == DX_REAL); STREQ(O(dx_peek(1))->text, "18.015");
  push_str("Ca(OH)2");
  CHECK(run("MOLMASS") == 0); STREQ(O(dx_peek(1))->text, "74.092");
  push_str("H2Xx");
  CHECK(run("MOLMASS") == -1 && last_error == 0x70000); STREQ(last_msg, "Unknown element: Xx");
  push_str("H2O)");
  CHECK(run("MOLMASS") == -1); STREQ(last_msg, "Bad formula");
  push_real(1);
  CHECK(run("MOLMASS") == -1 && last_error == DX_ERR_BAD_TYPE);
  char longf[300];                                        /* longer than MOLMASS's 256-byte buffer */
  for (int i = 0; i < 299; i++) longf[i] = 'H';
  longf[299] = 0;
  push_str(longf);
  CHECK(run("MOLMASS") == -1); STREQ(last_msg, "Formula too long");
  longf[256] = 0;                                         /* 256 bytes: still taken */
  push_str(longf);
  CHECK(run("MOLMASS") == 0);
  push_str("H");
  O(dx_peek(1))->n = 0;                                   /* "" */
  CHECK(run("MOLMASS") == -1); STREQ(last_msg, "Bad formula");
}

static void test_nav(void) {
  CHECK(nav(2, DIR_UP) == 2);                             /* He: nothing above */
  CHECK(nav(103, DIR_DOWN) == 103);                       /* Lr: the last row */
  CHECK(nav(1, DIR_LEFT) == 1);
  CHECK(nav(1, DIR_RIGHT) == 2);                          /* H -> He across the gap */
  CHECK(nav(1, DIR_DOWN) == 3);
  CHECK(nav(39, DIR_DOWN) == 57);                         /* Y -> La: the markers are skipped */
  CHECK(nav(57, DIR_UP) == 39);                           /* La -> Y */
  CHECK(nav(58, DIR_UP) == 104);                          /* Ce (col 3) -> Rf */
  CHECK(nav(56, DIR_RIGHT) == 72);                        /* Ba -> Hf, past the marker */
  CHECK(nav(72, DIR_LEFT) == 56);
  CHECK(nav(71, DIR_UP) == 117);                          /* Lu (col 16) -> Ts */
  CHECK(nav(26, DIR_DOWN) == 44);                         /* Fe -> Ru */
  CHECK(nav(118, DIR_RIGHT) == 118);
  CHECK(nav(57, DIR_DOWN) == 89);                         /* La -> Ac */
}

static int ends_with(const char *s, const char *t) {
  size_t a = strlen(s), b = strlen(t);
  return a >= b && strcmp(s + a - b, t) == 0;
}

static void test_info(void) {
  char b[64];
  info_line(26, 0, b, sizeof b); CHECK(strstr(b, "26") && strstr(b, "Iron"));
  info_line(26, 1, b, sizeof b); CHECK(strstr(b, "55.845"));
  info_line(26, 2, b, sizeof b); STREQ(b, "Transition metal");
  info_line(26, 3, b, sizeof b); STREQ(b, "[Ar] 3d6 4s2");
  info_line(26, 4, b, sizeof b); CHECK(strstr(b, "1.83") && ends_with(b, "7.87 g/cm3"));
  info_line(1, 4, b, sizeof b); CHECK(ends_with(b, "0.082 g/L"));
  info_line(26, 5, b, sizeof b); CHECK(strstr(b, "1538") && strstr(b, "2861"));
  info_line(6, 5, b, sizeof b); CHECK(strstr(b, "sublimes") && strstr(b, "3825"));   /* C */
  info_line(118, 4, b, sizeof b); CHECK(strstr(b, "-"));                              /* Og: nothing known */
  info_line(43, 1, b, sizeof b); CHECK(strstr(b, "[98]"));
  for (int z = 1; z <= NELEM; z++)                        /* every line fits the info box: 172 px at 6 px a character */
    for (int i = 0; i < 6; i++) CHECK(info_line(z, i, b, sizeof b) <= (i < 3 ? 28 : 35));
}

/* The share of dark pixels in element z's cell. */
static double cell_dark(int z) {
  const elem_t *e = elem_by_z(z);
  int x0 = 2 + e->col * 22, y0 = e->row < 7 ? e->row * 22 : 160 + (e->row - 7) * 22, dark = 0;
  for (int y = y0; y < y0 + 21; y++)
    for (int x = x0; x < x0 + 21; x++) dark += frame[y * 50 + (x >> 3)] >> (x & 7) & 1;
  return dark / (21.0 * 21.0);
}

static void write_pbm(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P4\n400 240\n");
  for (int i = 0; i < 50 * 240; i++) {                    /* GROB bytes are lowest-bit-leftmost; PBM is MSB-first */
    uint8_t b = frame[i], r = 0;
    for (int k = 0; k < 8; k++) r |= (uint8_t)(((b >> k) & 1) << (7 - k));
    fputc(r, f);
  }
  fclose(f);
}

static void test_draw(void) {
  draw_table(26);
  CHECK(cell_dark(26) > 0.5);                             /* the cursor: Fe's cell inverted */
  CHECK(cell_dark(27) < 0.5);
  CHECK(cell_dark(1) < 0.5);
  write_pbm("ptable.pbm");
}

static void test_ptable_cmd(void) {
  depth = 0;
  int k1[] = { 35, 36, 51 };                              /* down, right, ENTER: H -> Li -> Be */
  queue_keys(k1, 3);
  shows = 0;
  CHECK(run("PTABLE") == 0 && depth == 1);
  CHECK(dx_type(dx_peek(1)) == DX_LIST); STREQ(item_text(dx_peek(1), 1), "4");
  CHECK(shows == 3 && show_w == 400 && show_h == 240);  /* the first frame, then one for each move */
  int k2[] = { 51 };                                      /* again: it starts at Be */
  queue_keys(k2, 1);
  CHECK(run("PTABLE") == 0 && depth == 2); STREQ(item_text(dx_peek(1), 1), "4");
  int k3[] = { 36, 55 };                                  /* right, backspace: nothing pushed */
  queue_keys(k3, 2);
  CHECK(run("PTABLE") == 0 && depth == 2);
  int k4[] = { 25, 25, 25 };                              /* up from B (it moved to B), then ON: -1, nothing pushed */
  queue_keys(k4, 3);
  CHECK(run("PTABLE") == -1 && depth == 2);
  int k5[] = { 11, 99, 51 };                              /* keys that are not moves are ignored */
  queue_keys(k5, 3);
  CHECK(run("PTABLE") == 0 && depth == 3); STREQ(item_text(dx_peek(1), 1), "5");
}

int main(void) {
  test_data();
  test_nav();
  test_info();
  test_draw();
  test_ptable_cmd();
  test_commands();
  test_lookup();
  test_molmass();
  test_fmt_mass();
  if (fails) { printf("%d failed\n", fails); return 1; }
  printf("all passed\n");
  return 0;
}
