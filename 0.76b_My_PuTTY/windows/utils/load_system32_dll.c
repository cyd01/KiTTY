/*
 * Wrapper function to load a DLL out of c:\windows\system32 without
 * going through the full DLL search path. (Hence no attack is
 * possible by placing a substitute DLL earlier on that path.)
 */

#include "putty.h"

HMODULE load_system32_dll(const char *libname)
{
    /*
     * Wrapper function to load a DLL out of c:\windows\system32
     * without going through the full DLL search path. (Hence no
     * attack is possible by placing a substitute DLL earlier on that
     * path.)
     */
    static char *sysdir = NULL;
    static size_t sysdirsize = 0;
    char *fullpath;
    HMODULE ret;

    if (!sysdir) {
        size_t len;
        while ((len = GetSystemDirectory(sysdir, sysdirsize)) >= sysdirsize)
            sgrowarray(sysdir, sysdirsize, len);
    }

    fullpath = dupcat(sysdir, "\\", libname);
    ret = LoadLibrary(fullpath);
    sfree(fullpath);
    return ret;
}
