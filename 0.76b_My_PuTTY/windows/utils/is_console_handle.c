/*
 * Determine whether a Windows HANDLE points at a console device.
 */

#include "putty.h"

bool is_console_handle(HANDLE handle)
{
    DWORD ignored_output;
    if (GetConsoleMode(handle, &ignored_output))
        return true;
    return false;
}
