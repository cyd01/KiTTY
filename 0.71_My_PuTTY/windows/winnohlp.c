/*
 * nohelp.c: implement the has_embedded_chm() function for
 * applications that have no help file at all, so that misc.c's
 * buildinfo string knows not to talk meaninglessly about whether the
 * nonexistent help file is present.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "putty.h"

int has_embedded_chm(void) { return -1; }
