/*
 * Windows implementation of SSH connection-sharing IPC setup.
 */

#include <stdio.h>
#include <assert.h>

#if !defined NO_SECURITY

#include "tree234.h"
#include "putty.h"
#include "network.h"
#include "proxy.h"
#include "ssh.h"

#include "cryptoapi.h"
#include "winsecur.h"

#define CONNSHARE_PIPE_PREFIX "\\\\.\\pipe\\putty-connshare"
#define CONNSHARE_MUTEX_PREFIX "Local\\putty-connshare-mutex"

static char *make_name(const char *prefix, const char *name)
{
    char *username, *retname;

    username = get_username();
    retname = dupprintf("%s.%s.%s", prefix, username, name);
    sfree(username);

    return retname;
}

int platform_ssh_share(const char *pi_name, Conf *conf,
                       Plug *downplug, Plug *upplug, Socket **sock,
                       char **logtext, char **ds_err, char **us_err,
                       bool can_upstream, bool can_downstream)
{
    char *name, *mutexname, *pipename;
    HANDLE mutex;
    Socket *retsock;
    PSECURITY_DESCRIPTOR psd;
    PACL acl;

    /*
     * Transform the platform-independent version of the connection
     * identifier into the obfuscated version we'll use for our
     * Windows named pipe and mutex. A side effect of doing this is
     * that it also eliminates any characters illegal in Windows pipe
     * names.
     */
    name = capi_obfuscate_string(pi_name);
    if (!name) {
        *logtext = dupprintf("Unable to call CryptProtectMemory: %s",
                             win_strerror(GetLastError()));
        return SHARE_NONE;
    }

    /*
     * Make a mutex name out of the connection identifier, and lock it
     * while we decide whether to be upstream or downstream.
     */
    {
        SECURITY_ATTRIBUTES sa;

        mutexname = make_name(CONNSHARE_MUTEX_PREFIX, name);
        if (!make_private_security_descriptor(MUTEX_ALL_ACCESS,
                                              &psd, &acl, logtext)) {
            sfree(mutexname);
            sfree(name);
            return SHARE_NONE;
        }

        memset(&sa, 0, sizeof(sa));
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = psd;
        sa.bInheritHandle = false;

        mutex = CreateMutex(&sa, false, mutexname);

        if (!mutex) {
            *logtext = dupprintf("CreateMutex(\"%s\") failed: %s",
                                 mutexname, win_strerror(GetLastError()));
            sfree(mutexname);
            sfree(name);
            LocalFree(psd);
            LocalFree(acl);
            return SHARE_NONE;
        }

        sfree(mutexname);
        LocalFree(psd);
        LocalFree(acl);

        WaitForSingleObject(mutex, INFINITE);
    }

    pipename = make_name(CONNSHARE_PIPE_PREFIX, name);

    *logtext = NULL;

    if (can_downstream) {
        retsock = new_named_pipe_client(pipename, downplug);
        if (sk_socket_error(retsock) == NULL) {
            sfree(*logtext);
            *logtext = pipename;
            *sock = retsock;
            sfree(name);
            ReleaseMutex(mutex);
            CloseHandle(mutex);
            return SHARE_DOWNSTREAM;
        }
        sfree(*ds_err);
        *ds_err = dupprintf("%s: %s", pipename, sk_socket_error(retsock));
        sk_close(retsock);
    }

    if (can_upstream) {
        retsock = new_named_pipe_listener(pipename, upplug);
        if (sk_socket_error(retsock) == NULL) {
            sfree(*logtext);
            *logtext = pipename;
            *sock = retsock;
            sfree(name);
            ReleaseMutex(mutex);
            CloseHandle(mutex);
            return SHARE_UPSTREAM;
        }
        sfree(*us_err);
        *us_err = dupprintf("%s: %s", pipename, sk_socket_error(retsock));
        sk_close(retsock);
    }

    /* One of the above clauses ought to have happened. */
    assert(*logtext || *ds_err || *us_err);

    sfree(pipename);
    sfree(name);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return SHARE_NONE;
}

void platform_ssh_share_cleanup(const char *name)
{
}

#else /* !defined NO_SECURITY */

#include "noshare.c"

#endif /* !defined NO_SECURITY */
