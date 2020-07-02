/*
 * Windows support module which deals with being a named-pipe server.
 */

#include <stdio.h>
#include <assert.h>

#include "tree234.h"
#include "putty.h"
#include "network.h"
#include "proxy.h"
#include "ssh.h"

#if !defined NO_SECURITY

#include "winsecur.h"

typedef struct NamedPipeServerSocket {
    /* Parameters for (repeated) creation of named pipe objects */
    PSECURITY_DESCRIPTOR psd;
    PACL acl;
    char *pipename;

    /* The current named pipe object + attempt to connect to it */
    HANDLE pipehandle;
    OVERLAPPED connect_ovl;
    struct handle *callback_handle;    /* winhandl.c's reference */

    /* PuTTY Socket machinery */
    Plug *plug;
    char *error;

    Socket sock;
} NamedPipeServerSocket;

static Plug *sk_namedpipeserver_plug(Socket *s, Plug *p)
{
    NamedPipeServerSocket *ps = container_of(s, NamedPipeServerSocket, sock);
    Plug *ret = ps->plug;
    if (p)
	ps->plug = p;
    return ret;
}

static void sk_namedpipeserver_close(Socket *s)
{
    NamedPipeServerSocket *ps = container_of(s, NamedPipeServerSocket, sock);

    if (ps->callback_handle)
        handle_free(ps->callback_handle);
    CloseHandle(ps->pipehandle);
    CloseHandle(ps->connect_ovl.hEvent);
    sfree(ps->error);
    sfree(ps->pipename);
    if (ps->acl)
        LocalFree(ps->acl);
    if (ps->psd)
        LocalFree(ps->psd);
    sfree(ps);
}

static const char *sk_namedpipeserver_socket_error(Socket *s)
{
    NamedPipeServerSocket *ps = container_of(s, NamedPipeServerSocket, sock);
    return ps->error;
}

static SocketPeerInfo *sk_namedpipeserver_peer_info(Socket *s)
{
    return NULL;
}

static bool create_named_pipe(NamedPipeServerSocket *ps, bool first_instance)
{
    SECURITY_ATTRIBUTES sa;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = ps->psd;
    sa.bInheritHandle = false;

    ps->pipehandle = CreateNamedPipe
        (/* lpName */
         ps->pipename,

         /* dwOpenMode */
         PIPE_ACCESS_DUPLEX |
         FILE_FLAG_OVERLAPPED |
         (first_instance ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),

         /* dwPipeMode */
         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT
#ifdef PIPE_REJECT_REMOTE_CLIENTS
         | PIPE_REJECT_REMOTE_CLIENTS
#endif
         ,

         /* nMaxInstances */
         PIPE_UNLIMITED_INSTANCES,

         /* nOutBufferSize, nInBufferSize */
         4096, 4096,     /* FIXME: think harder about buffer sizes? */

         /* nDefaultTimeOut */
         0 /* default timeout */,

         /* lpSecurityAttributes */
         &sa);

    return ps->pipehandle != INVALID_HANDLE_VALUE;
}

static Socket *named_pipe_accept(accept_ctx_t ctx, Plug *plug)
{
    HANDLE conn = (HANDLE)ctx.p;

    return make_handle_socket(conn, conn, NULL, plug, true);
}

static void named_pipe_accept_loop(NamedPipeServerSocket *ps,
                                   bool got_one_already)
{
    while (1) {
        int error;
        char *errmsg;

        if (got_one_already) {
            /* If we were called with a connection already waiting,
             * skip this step. */
            got_one_already = false;
            error = 0;
        } else {
            /*
             * Call ConnectNamedPipe, which might succeed or might
             * tell us that an overlapped operation is in progress and
             * we should wait for our event object.
             */
            if (ConnectNamedPipe(ps->pipehandle, &ps->connect_ovl))
                error = 0;
            else
                error = GetLastError();

            if (error == ERROR_IO_PENDING)
                return;
        }

        if (error == 0 || error == ERROR_PIPE_CONNECTED) {
            /*
             * We've successfully retrieved an incoming connection, so
             * ps->pipehandle now refers to that connection. So
             * convert that handle into a separate connection-type
             * Socket, and create a fresh one to be the new listening
             * pipe.
             */
            HANDLE conn = ps->pipehandle;
            accept_ctx_t actx;

            actx.p = (void *)conn;
            if (plug_accepting(ps->plug, named_pipe_accept, actx)) {
                /*
                 * If the plug didn't want the connection, might as
                 * well close this handle.
                 */
                CloseHandle(conn);
            }

            if (!create_named_pipe(ps, false)) {
                error = GetLastError();
            } else {
                /*
                 * Go round again to see if more connections can be
                 * got, or to begin waiting on the event object.
                 */
                continue;
            }
        }

        errmsg = dupprintf("Error while listening to named pipe: %s",
                           win_strerror(error));
        plug_log(ps->plug, 1, sk_namedpipe_addr(ps->pipename), 0,
                 errmsg, error);
        sfree(errmsg);
        break;
    }
}

static void named_pipe_connect_callback(void *vps)
{
    NamedPipeServerSocket *ps = (NamedPipeServerSocket *)vps;
    named_pipe_accept_loop(ps, true);
}

/*
 * This socket type is only used for listening, so it should never
 * be asked to write or set_frozen.
 */
static const SocketVtable NamedPipeServerSocket_sockvt = {
    sk_namedpipeserver_plug,
    sk_namedpipeserver_close,
    NULL /* write */,
    NULL /* write_oob */,
    NULL /* write_eof */,
    NULL /* set_frozen */,
    sk_namedpipeserver_socket_error,
    sk_namedpipeserver_peer_info,
};

Socket *new_named_pipe_listener(const char *pipename, Plug *plug)
{
    NamedPipeServerSocket *ret = snew(NamedPipeServerSocket);
    ret->sock.vt = &NamedPipeServerSocket_sockvt;
    ret->plug = plug;
    ret->error = NULL;
    ret->psd = NULL;
    ret->pipename = dupstr(pipename);
    ret->acl = NULL;
    ret->callback_handle = NULL;

    assert(strncmp(pipename, "\\\\.\\pipe\\", 9) == 0);
    assert(strchr(pipename + 9, '\\') == NULL);

    if (!make_private_security_descriptor(GENERIC_READ | GENERIC_WRITE,
                                          &ret->psd, &ret->acl, &ret->error)) {
        goto cleanup;
    }

    if (!create_named_pipe(ret, true)) {
        ret->error = dupprintf("unable to create named pipe '%s': %s",
                               pipename, win_strerror(GetLastError()));
        goto cleanup;
    }

    memset(&ret->connect_ovl, 0, sizeof(ret->connect_ovl));
    ret->connect_ovl.hEvent = CreateEvent(NULL, true, false, NULL);
    ret->callback_handle =
        handle_add_foreign_event(ret->connect_ovl.hEvent,
                                 named_pipe_connect_callback, ret);
    named_pipe_accept_loop(ret, false);

  cleanup:
    return &ret->sock;
}

#endif /* !defined NO_SECURITY */
