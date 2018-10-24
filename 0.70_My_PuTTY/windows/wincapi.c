/*
 * wincapi.c: implementation of wincapi.h.
 */

#include "putty.h"

#if !defined NO_SECURITY

#define WINCAPI_GLOBAL
#include "wincapi.h"

int got_crypt(void)
{
    static int attempted = FALSE;
    static int successful;
    static HMODULE crypt;

    if (!attempted) {
        attempted = TRUE;
        crypt = load_system32_dll("crypt32.dll");
        successful = crypt &&
#ifdef COVERITY
            /* The build toolchain I use with Coverity doesn't know
             * about this function, so can't type-check it */
            GET_WINDOWS_FUNCTION_NO_TYPECHECK(crypt, CryptProtectMemory)
#else
            GET_WINDOWS_FUNCTION(crypt, CryptProtectMemory)
#endif
            ;
    }
    return successful;
}

#endif /* !defined NO_SECURITY */
