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

static void test_data(void) {
  const elem_t *fe = &elements[25];
  CHECK(fe->z == 26);
  STREQ(fe->sym, "Fe");
  STREQ(fe->name, "Iron");
  CHECK(fe->mass_e5 == 5584500);
  STREQ(cat_names[fe->cat], "Transition metal");
  CHECK(fe->row == 3 && fe->col == 7);
  CHECK(!fe->gas);
  CHECK(elements[0].gas);                                   /* H */
  STREQ(elements[42].mass, "[98]");                         /* Tc */
  CHECK(elements[42].mass_e5 == 9800000);
  CHECK(elements[56].row == 7 && elements[56].col == 2);    /* La */
  CHECK(elements[70].row == 7 && elements[70].col == 16);   /* Lu */
  CHECK(elements[88].row == 8 && elements[88].col == 2);    /* Ac */
  CHECK(elements[71].row == 5 && elements[71].col == 3);    /* Hf */
  CHECK(elements[117].row == 6 && elements[117].col == 17); /* Og */
  CHECK(elements[1].row == 0 && elements[1].col == 17);     /* He */
  CHECK(elements[5].subl && elements[5].mp[0] == 0);        /* C sublimes */
  for (int i = 0; i < NELEM; i++) {
    CHECK(elements[i].z == i + 1);
    CHECK(elements[i].sym[0] && elements[i].name[0] && elements[i].mass[0]);
    CHECK(elements[i].cat < NCAT);
  }
}

int main(void) {
  test_data();
  if (fails) { printf("%d failed\n", fails); return 1; }
  printf("all passed\n");
  return 0;
}
