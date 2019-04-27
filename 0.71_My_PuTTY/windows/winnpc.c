/*
 * Windows support module which deals with being a named-pipe client.
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

Socket *new_named_pipe_client(const char *pipename, Plug *plug)
{
    HANDLE pipehandle;
    PSID usersid, pipeowner;
    PSECURITY_DESCRIPTOR psd;

    assert(strncmp(pipename, "\\\\.\\pipe\\", 9) == 0);
    assert(strchr(pipename + 9, '\\') == NULL);

    while (1) {
        pipehandle = CreateFile(pipename, GENERIC_READ | GENERIC_WRITE,
                                0, NULL, OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED, NULL);

        if (pipehandle != INVALID_HANDLE_VALUE)
            break;

        if (GetLastError() != ERROR_PIPE_BUSY) {
            return new_error_socket_fmt(
                plug, "Unable to open named pipe '%s': %s",
                pipename, win_strerror(GetLastError()));
        }

        /*
         * If we got ERROR_PIPE_BUSY, wait for the server to
         * create a new pipe instance. (Since the server is
         * expected to be winnps.c, which will do that immediately
         * after a previous connection is accepted, that shouldn't
         * take excessively long.)
         */
        if (!WaitNamedPipe(pipename, NMPWAIT_USE_DEFAULT_WAIT)) {
            return new_error_socket_fmt(
                plug, "Error waiting for named pipe '%s': %s",
                pipename, win_strerror(GetLastError()));
        }
    }

    if ((usersid = get_user_sid()) == NULL) {
        CloseHandle(pipehandle);
        return new_error_socket_fmt(
            plug, "Unable to get user SID: %s", win_strerror(GetLastError()));
    }

    if (p_GetSecurityInfo(pipehandle, SE_KERNEL_OBJECT,
                          OWNER_SECURITY_INFORMATION,
                          &pipeowner, NULL, NULL, NULL,
                          &psd) != ERROR_SUCCESS) {
        CloseHandle(pipehandle);
        return new_error_socket_fmt(
            plug, "Unable to get named pipe security information: %s",
            win_strerror(GetLastError()));
    }

    if (!EqualSid(pipeowner, usersid)) {
        CloseHandle(pipehandle);
        LocalFree(psd);
        return new_error_socket_fmt(
            plug, "Owner of named pipe '%s' is not us", pipename);
    }

    LocalFree(psd);

    return make_handle_socket(pipehandle, pipehandle, NULL, plug, true);
}

#endif /* !defined NO_SECURITY */
