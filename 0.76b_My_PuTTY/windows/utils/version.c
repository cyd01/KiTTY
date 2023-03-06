#include "putty.h"

DWORD osMajorVersion, osMinorVersion, osPlatformId;

void init_winver(void)
{
    static bool initialised = false;
    if (initialised)
        return;
    initialised = true;

    OSVERSIONINFO osVersion;
    static HMODULE kernel32_module;
    DECL_WINDOWS_FUNCTION(static, BOOL, GetVersionExA, (LPOSVERSIONINFO));

    if (!kernel32_module) {
        kernel32_module = load_system32_dll("kernel32.dll");
        /* Deliberately don't type-check this function, because that
         * would involve using its declaration in a header file which
         * triggers a deprecation warning. I know it's deprecated (see
         * below) and don't need telling. */
        GET_WINDOWS_FUNCTION_NO_TYPECHECK(kernel32_module, GetVersionExA);
    }

    ZeroMemory(&osVersion, sizeof(osVersion));
    osVersion.dwOSVersionInfoSize = sizeof (OSVERSIONINFO);
    if (p_GetVersionExA && p_GetVersionExA(&osVersion)) {
        osMajorVersion = osVersion.dwMajorVersion;
        osMinorVersion = osVersion.dwMinorVersion;
        osPlatformId = osVersion.dwPlatformId;
    } else {
        /*
         * GetVersionEx is deprecated, so allow for it perhaps going
         * away in future API versions. If it's not there, simply
         * assume that's because Windows is too _new_, so fill in the
         * variables we care about to a value that will always compare
         * higher than any given test threshold.
         *
         * Normally we should be checking against the presence of a
         * specific function if possible in any case.
         */
        osMajorVersion = osMinorVersion = UINT_MAX; /* a very high number */
        osPlatformId = VER_PLATFORM_WIN32_NT; /* not Win32s or Win95-like */
    }
}
