/*
 * Implementation of do_select() for winnet.c to use, that uses
 * WSAAsyncSelect to convert network activity into window messages,
 * for integration into a GUI event loop.
 */

#include "putty.h"

static HWND winsel_hwnd = NULL;

void winselgui_set_hwnd(HWND hwnd)
{
    winsel_hwnd = hwnd;
}

void winselgui_clear_hwnd(void)
{
    winsel_hwnd = NULL;
}

const char *do_select(SOCKET skt, bool enable)
{
    int msg, events;
    if (enable) {
        msg = WM_NETEVENT;
        events = (FD_CONNECT | FD_READ | FD_WRITE |
                  FD_OOB | FD_CLOSE | FD_ACCEPT);
    } else {
        msg = events = 0;
    }

    assert(winsel_hwnd);

    if (p_WSAAsyncSelect(skt, winsel_hwnd, msg, events) == SOCKET_ERROR)
        return winsock_error_string(p_WSAGetLastError());

    return NULL;
}
