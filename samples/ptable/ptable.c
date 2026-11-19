// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
/*
 * ptable.c - a periodic table. Three commands:
 *   ELEM     ( z → list )  ( "Fe" → list )  ( "iron" → list )   an element's data:
 *            { Z "Sym" "Name" mass "Category" EN density mp bp "Config" }, "" for a value that is not known.
 *   MOLMASS  ( "Ca(OH)2" → 74.092 )   the molar mass of a formula in g/mol, exact in decimal.
 *   PTABLE   ( → list ) or ( → )      the table on the whole screen: ENTER gives the element's list, ← gives nothing.
 * The data and the parser are in chem.c, the screen in table.c; this file is the glue to the calculator.
 */
#include "chem.h"

/* ---- the element list ---- */

static dx_obj text_or_empty(const char *t, int number) {
  if (!t[0]) return dx_make_string("", 0);
  if (!number) {
    int n = 0;
    while (t[n]) n++;
    return dx_make_string(t, n);
  }
  return dx_make_real_text(t);
}

dx_obj elem_list(const elem_t *e) {
  char mass[16];                                         /* the mass without the brackets of "[98]" */
  int k = 0;
  for (const char *p = ES(e, mass); *p && k < (int)sizeof mass - 1; p++)
    if (*p != '[' && *p != ']') mass[k++] = *p;
  mass[k] = 0;
  dx_obj it[10];
  it[0] = dx_make_real(e->z);
  it[1] = text_or_empty(ES(e, sym), 0);
  it[2] = text_or_empty(ES(e, name), 0);
  it[3] = text_or_empty(mass, 1);
  it[4] = text_or_empty(cat_names[e->cat], 0);
  it[5] = text_or_empty(ES(e, en), 1);
  it[6] = text_or_empty(ES(e, density), 1);
  it[7] = text_or_empty(ES(e, mp), 1);
  it[8] = text_or_empty(ES(e, bp), 1);
  it[9] = text_or_empty(ES(e, config), 0);
  for (int i = 0; i < 10; i++) if (!it[i]) return 0;     /* a failed dx_* call: its error is recorded */
  return dx_make_list(it, 10);
}

/* Drop the argument and push r. */
static int replace(dx_obj r) {
  if (!r || dx_drop(1) || dx_push(r)) return -1;
  return 0;
}

/* ---- ELEM ---- */

enum { TEXT_MAX = 256 };
static char text[TEXT_MAX];                              /* a string argument: in .bss, not on the 2 KB stack */

static int elem(void) {
  dx_obj a = dx_peek(1);
  const elem_t *e = 0;
  switch (dx_type(a)) {
  case DX_REAL: {
    double d;
    if (dx_get_real(a, &d)) return -1;
    if (!(d >= 1 && d <= NELEM) || d != (int)d) return dx_error(DX_ERR_BAD_VALUE);
    e = elem_by_z((int)d);
    break;
  }
  case DX_STRING: {
    int n = dx_string_len(a);
    if (n < 0) return -1;
    if (n > TEXT_MAX) return dx_error(DX_ERR_BAD_VALUE);
    if (dx_get_string(a, text, n) != n) return -1;
    e = elem_by_text(text, n);
    if (!e) return dx_error(DX_ERR_BAD_VALUE);
    break;
  }
  default:
    return dx_error(DX_ERR_BAD_TYPE);
  }
  return replace(elem_list(e));
}

/* ---- MOLMASS ---- */

static int molmass_cmd(void) {
  dx_obj a = dx_peek(1);
  if (dx_type(a) != DX_STRING) return dx_error(DX_ERR_BAD_TYPE);
  int n = dx_string_len(a);
  if (n < 0) return -1;
  if (n > TEXT_MAX) return dx_error_msg("Formula too long");
  if (dx_get_string(a, text, n) != n) return -1;
  int64_t m;
  char err[40];
  if (molmass(text, n, &m, err, sizeof err)) return dx_error_msg(err);
  char num[24];
  fmt_mass(m, num, sizeof num);
  return replace(dx_make_real_text(num));
}

int dx_init(void) {
  if (dx_register("ELEM", 1, elem)) return -1;
  if (dx_register("MOLMASS", 1, molmass_cmd)) return -1;
  return 0;
}
