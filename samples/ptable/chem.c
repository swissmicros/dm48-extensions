// SPDX-License-Identifier: MIT
// Copyright © 2026 SwissMicros GmbH
/*
 * chem.c - the element table, and the lookups and the formula parser behind ELEM and MOLMASS. Pure C: no dx_* calls
 * and no libc, so the host tests can run it as it is.
 */
#include "chem.h"

/* const: the table goes in the code part of the image, with no copy in RAM beyond the image itself. */
const elem_t elements[NELEM] = {
#include "elements.inc"
};

const char *const cat_names[NCAT] = { CAT_NAMES };
