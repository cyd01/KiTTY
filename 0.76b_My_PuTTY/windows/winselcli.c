/*
 * Implementation of do_select() for winnet.c to use, suitable for use
 * when there's no GUI window to have network activity reported to.
 *
 * It uses WSAEventSelect, where available, to convert network
 * activity into activity on an event object, for integration into an
 * event loop that includes WaitForMultipleObjects.
 *
 * It also maintains a list of currently active sockets, which can be
 * retrieved by a front end that wants to use WinSock's synchronous
 * select() function.
 */

#include "putty.h"

static tree234 *winselcli_sockets;

static int socket_cmp(void *av, void *bv)
{
    return memcmp(av, bv, sizeof(SOCKET));
} 

HANDLE winselcli_event = INVALID_HANDLE_VALUE;

void winselcli_setup(void)
{
    if (!winselcli_sockets)
        winselcli_sockets = newtree234(socket_cmp);

    if (p_WSAEventSelect && winselcli_event == INVALID_HANDLE_VALUE)
        winselcli_event = CreateEvent(NULL, false, false, NULL);
}

SOCKET winselcli_unique_socket(void)
{
    if (!winselcli_sockets)
        return INVALID_SOCKET;

    assert(count234(winselcli_sockets) <= 1);

    SOCKET *p = index234(winselcli_sockets, 0);
    if (!p)
        return INVALID_SOCKET;

    return *p;
}

const char *do_select(SOCKET skt, bool enable)
{
    /* Check everything's been set up, for convenience of callers. */
    winselcli_setup();

    if (enable) {
        SOCKET *ptr = snew(SOCKET);
        *ptr = skt;
        if (add234(winselcli_sockets, ptr) != ptr)
            sfree(ptr);                /* already there */
    } else {
        SOCKET *ptr = del234(winselcli_sockets, &skt);
        if (ptr)
            sfree(ptr);
    }

    if (p_WSAEventSelect) {
        int events;
        if (enable) {
            events = (FD_CONNECT | FD_READ | FD_WRITE |
                      FD_OOB | FD_CLOSE | FD_ACCEPT);
        } else {
            events = 0;
        }

        if (p_WSAEventSelect(skt, winselcli_event, events) == SOCKET_ERROR)
            return winsock_error_string(p_WSAGetLastError());
    }

    return NULL;
}
