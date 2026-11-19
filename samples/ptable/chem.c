// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
/*
 * chem.c - the element table, and the lookups and the formula parser behind ELEM and MOLMASS. Pure C: no dx_* calls
 * and no libc, so the host tests can run it as it is.
 */
#include "chem.h"

/* const: the table goes in the code part of the image. Its texts are offsets in one pool, not pointers: no relocation
   for each, and 2 bytes instead of 4. */
const char elem_pool[] =
#define ELEM_POOL
#include "elements.inc"
#undef ELEM_POOL
;

const elem_t elements[NELEM] = {
#include "elements.inc"
};

const char *const cat_names[NCAT] = { CAT_NAMES };

/* ---- lookups ---- */

const elem_t *elem_by_z(int z) { return z >= 1 && z <= NELEM ? &elements[z - 1] : 0; }

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* s (n bytes) equals the C string t, exactly or in any case */
static int same(const char *s, int n, const char *t, int any_case) {
  int i = 0;
  for (; i < n && t[i]; i++)
    if (any_case ? lower(s[i]) != lower(t[i]) : s[i] != t[i]) return 0;
  return i == n && !t[i];
}

const elem_t *elem_by_text(const char *s, int n) {
  for (int i = 0; i < NELEM; i++) if (same(s, n, ES(&elements[i], sym), 0)) return &elements[i];
  for (int i = 0; i < NELEM; i++) if (same(s, n, ES(&elements[i], name), 1)) return &elements[i];
  return 0;
}

/* ---- MOLMASS: a recursive-descent parser over the byte range, summing in integers (1e-5 g/mol) ---- */

enum { DEPTH_MAX = 8 };
#define MASS_LIMIT 1000000000000000LL                /* 1e10 g/mol: far beyond any real formula, far inside int64 */

typedef struct {
  const char *s;
  int n, i, depth;
  char *err;
  int errcap;
} parser;

static int fail(parser *p, const char *msg, const char *sym, int symlen) {
  int k = 0;
  for (; *msg && k < p->errcap - 1; msg++) p->err[k++] = *msg;
  for (int j = 0; j < symlen && k < p->errcap - 1; j++) p->err[k++] = sym[j];
  p->err[k] = 0;
  return -1;
}
static int bad(parser *p) { return fail(p, "Bad formula", 0, 0); }

static int at(parser *p) { return p->i < p->n ? (unsigned char)p->s[p->i] : -1; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }

/* An optional count, 1 to 9999 with no leading 0: 1 when there is none. */
static int count(parser *p, int *k) {
  *k = 1;
  if (!is_digit(at(p))) return 0;
  if (at(p) == '0') return bad(p);
  int v = 0, digits = 0;
  while (is_digit(at(p))) {
    if (++digits > 4) return bad(p);
    v = v * 10 + (at(p) - '0');
    p->i++;
  }
  *k = v;
  return 0;
}

/* *m = *m * k + add, refused beyond MASS_LIMIT */
static int scale_add(parser *p, int64_t *m, int k, int64_t add) {
  if (*m > MASS_LIMIT / k) return bad(p);
  *m *= k;
  if (*m > MASS_LIMIT - add) return bad(p);
  *m += add;
  return 0;
}

static int part(parser *p, int64_t *m);

/* One group: an element or a bracketed part, then its count. */
static int group(parser *p, int64_t *m) {
  int c = at(p), k;
  int64_t g = 0;
  if (c == '(' || c == '[') {
    int close = c == '(' ? ')' : ']';
    if (++p->depth > DEPTH_MAX) return fail(p, "Formula too deep", 0, 0);
    p->i++;
    if (part(p, &g)) return -1;
    if (at(p) != close) return bad(p);
    p->i++;
    p->depth--;
  } else if (c >= 'A' && c <= 'Z') {
    int start = p->i++;
    if (at(p) >= 'a' && at(p) <= 'z') p->i++;
    const elem_t *e = 0;
    for (int j = 0; j < NELEM && !e; j++)
      if (same(p->s + start, p->i - start, ES(&elements[j], sym), 0)) e = &elements[j];
    if (!e) return fail(p, "Unknown element: ", p->s + start, p->i - start);
    g = e->mass_e5;
  } else {
    return bad(p);
  }
  if (count(p, &k)) return -1;
  if (scale_add(p, &g, k, 0)) return -1;
  return scale_add(p, m, 1, g);
}

/* One or more groups, up to the end, a closing bracket or a hydrate's separator. */
static int part(parser *p, int64_t *m) {
  *m = 0;
  do {
    if (group(p, m)) return -1;
  } while (at(p) == '(' || at(p) == '[' || (at(p) >= 'A' && at(p) <= 'Z'));
  return 0;
}

int molmass(const char *s, int n, int64_t *mass_e5, char *err, int errcap) {
  parser p = { s, n, 0, 0, err, errcap };
  int64_t total = 0, m;
  int k = 1;
  for (;;) {
    if (part(&p, &m) || scale_add(&p, &m, k, 0) || scale_add(&p, &total, 1, m)) return -1;
    if (at(&p) != '*' && at(&p) != '.') break;
    p.i++;
    if (count(&p, &k)) return -1;
  }
  if (p.i != n) return bad(&p);                         /* a stray ')' or ']', or anything else left over */
  *mass_e5 = total;
  return 0;
}

int fmt_mass(int64_t mass_e5, char *buf, int cap) {
  char t[24];
  int n = 0;
  int64_t whole = mass_e5 / 100000, frac = mass_e5 % 100000;
  do { t[n++] = (char)('0' + whole % 10); whole /= 10; } while (whole);
  int k = 0;
  while (n && k < cap - 1) buf[k++] = t[--n];
  if (frac) {
    char d[5];
    for (int j = 4; j >= 0; j--) { d[j] = (char)('0' + frac % 10); frac /= 10; }
    int last = 4;
    while (d[last] == '0') last--;
    if (k < cap - 1) buf[k++] = '.';
    for (int j = 0; j <= last && k < cap - 1; j++) buf[k++] = d[j];
  }
  buf[k] = 0;
  return k;
}
