/*
 * Miscellaneous stuff to include in all .rc files.
 */

#ifndef PUTTY_RCSTUFF_H
#define PUTTY_RCSTUFF_H

#ifdef __LCC__
#include <win.h>
#else

/* Some compilers, like Borland, don't have winresrc.h */
#ifndef NO_WINRESRC_H
#ifndef MSVC4
#include <winresrc.h>
#else
#include <winres.h>
#endif
#endif

#endif /* end #ifdef __LCC__ */

/* Some systems don't define this, so I do it myself if necessary */
#ifndef TCS_MULTILINE
#define TCS_MULTILINE 0x0200
#endif

/* Likewise */
#ifndef RT_MANIFEST
#define RT_MANIFEST 24
#endif

/* LCC is the offender here. */
#ifndef VS_FF_DEBUG
#define VS_FF_DEBUG        1
#endif
#ifndef VS_FF_PRERELEASE
#define VS_FF_PRERELEASE   2
#endif
#ifndef VS_FF_PRIVATEBUILD
#define VS_FF_PRIVATEBUILD 8
#endif
#ifndef VOS__WINDOWS32
#define VOS__WINDOWS32     4
#endif
#ifndef VFT_APP
#define VFT_APP            1
#endif

#endif /* PUTTY_RCSTUFF_H */
