// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
/*
 * chem.h - the ptable sample's element data and the pure C behind its commands (no dx_* calls in chem.c and
 * table.c, so they can be tested on the host).
 */
#ifndef CHEM_H
#define CHEM_H

#include "dm48ext.h"

enum { NELEM = 118, NCAT = 10 };

typedef struct {
  uint8_t z, cat, row, col;           /* row 0-6 the periods, 7-8 the f rows; col 0-17 */
  uint8_t gas;                        /* density in g/L, not g/cm3 */
  uint8_t subl;                       /* bp is a sublimation point (no mp at 1 atm) */
  uint32_t mass_e5;                   /* the mass in units of 1e-5 u: 55.845 -> 5584500 */
  const char *sym, *name, *mass, *en, *density, *mp, *bp, *config;   /* display text, "" when missing; mass as
                                         shown: "55.845", "[98]" */
} elem_t;

extern const elem_t elements[NELEM];  /* index z - 1 */
extern const char *const cat_names[NCAT];

const elem_t *elem_by_z(int z);                        /* NULL outside 1-118 */
const elem_t *elem_by_text(const char *s, int n);      /* a symbol (exact case), else a name (any case); NULL */
/* The molar mass of the formula s (n bytes) in 1e-5 g/mol: 0, or -1 with the message in err (a C string).
   formula := part (('*' | '.') [count] part)*    part := group+    count := 1-9999, no leading 0
   group := (element | '(' part ')' | '[' part ']') [count]          element := Upper [lower]
   At most 8 nested brackets. */
int molmass(const char *s, int n, int64_t *mass_e5, char *err, int errcap);
int fmt_mass(int64_t mass_e5, char *buf, int cap);     /* "74.09216": no trailing zeros, a C string; its length */

#endif /* CHEM_H */
