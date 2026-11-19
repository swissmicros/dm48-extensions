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

int main(void) {
  test_data();
  test_lookup();
  test_molmass();
  test_fmt_mass();
  if (fails) { printf("%d failed\n", fails); return 1; }
  printf("all passed\n");
  return 0;
}
