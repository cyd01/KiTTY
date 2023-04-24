/*
 * Handy wrapper around GetDlgItemText which doesn't make you invent
 * an arbitrary length limit on the output string. Returned string is
 * dynamically allocated; caller must free.
 */

#include "putty.h"

char *GetDlgItemText_alloc(HWND hwnd, int id)
{
    char *ret = NULL;
    size_t size = 0;

    do {
        sgrowarray_nm(ret, size, size);
        GetDlgItemText(hwnd, id, ret, size);
    } while (!memchr(ret, '\0', size-1));

    return ret;
}
