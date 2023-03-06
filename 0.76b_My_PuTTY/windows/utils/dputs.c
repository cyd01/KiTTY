/*
 * Implementation of dputs() for Windows.
 *
 * The debug messages are written to STD_OUTPUT_HANDLE, except that
 * first it has to make sure that handle _exists_, by calling
 * AllocConsole first if necessary.
 *
 * They also go into a file called debug.log.
 */

#include "putty.h"
#include "utils/utils.h"

static HANDLE debug_fp = INVALID_HANDLE_VALUE;
static HANDLE debug_hdl = INVALID_HANDLE_VALUE;
static int debug_got_console = 0;

void dputs(const char *buf)
{
    DWORD dw;

    if (!debug_got_console) {
        if (AllocConsole()) {
            debug_got_console = 1;
            debug_hdl = GetStdHandle(STD_OUTPUT_HANDLE);
        }
    }
    if (debug_fp == INVALID_HANDLE_VALUE) {
        debug_fp = CreateFile("debug.log", GENERIC_WRITE, FILE_SHARE_READ,
                              NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    if (debug_fp != INVALID_HANDLE_VALUE) {
        WriteFile(debug_fp, buf, strlen(buf), &dw, NULL);
    }
    if (debug_hdl != INVALID_HANDLE_VALUE) {
        WriteFile(debug_hdl, buf, strlen(buf), &dw, NULL);
    }
}
