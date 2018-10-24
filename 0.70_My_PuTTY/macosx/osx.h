#ifndef PUTTY_OSX_H
#define PUTTY_OSX_H

/*
 * Cocoa defines `FontSpec' itself, so we must change its name.
 * (Arrgh.)
 */
#define FontSpec FontSpec_OSX_Proof

/*
 * Define the various compatibility symbols to make uxpty.c compile
 * correctly on OS X.
 */
#define BSD_PTYS
#define OMIT_UTMP
#define HAVE_NO_SETRESUID
#define NOT_X_WINDOWS

/*
 * OS X is largely just Unix, so we can include most of this
 * unchanged.
 */
#include "unix.h"

/*
 * Functions exported by osxsel.m. (Both of these functions are
 * expected to be called in the _main_ thread: the select subthread
 * is an implementation detail of osxsel.m and ideally should not
 * be visible at all outside it.)
 */
void osxsel_init(void);		       /* call this to kick things off */
void osxsel_process_results(void);     /* call this on receipt of a netevent */

#endif
