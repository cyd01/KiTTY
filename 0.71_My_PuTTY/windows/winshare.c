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

#include "wincapi.h"
#include "winsecur.h"

#ifdef COVERITY
/*
 * The hack I use to build for Coverity scanning, using winegcc and
 * Makefile.mgw, didn't provide some defines in wincrypt.h last time I
 * looked. Therefore, define them myself here, but enclosed in #ifdef
 * COVERITY to ensure I don't make up random nonsense values for any
 * real build.
 */
#ifndef CRYPTPROTECTMEMORY_BLOCK_SIZE
#define CRYPTPROTECTMEMORY_BLOCK_SIZE 16
#endif
#ifndef CRYPTPROTECTMEMORY_CROSS_PROCESS
#define CRYPTPROTECTMEMORY_CROSS_PROCESS 1
#endif
#endif

#define CONNSHARE_PIPE_PREFIX "\\\\.\\pipe\\putty-connshare"
#define CONNSHARE_MUTEX_PREFIX "Local\\putty-connshare-mutex"

static char *obfuscate_name(const char *realname)
{
    /*
     * Windows's named pipes all live in the same namespace, so one
     * user can see what pipes another user has open. This is an
     * undesirable privacy leak and in particular permits one user to
     * know what username@host another user is SSHing to, so we
     * protect that information by using CryptProtectMemory (which
     * uses a key built in to each user's account).
     */
    char *cryptdata;
    int cryptlen;
    unsigned char digest[32];
    char retbuf[65];
    int i;

    cryptlen = strlen(realname) + 1;
    cryptlen += CRYPTPROTECTMEMORY_BLOCK_SIZE - 1;
    cryptlen /= CRYPTPROTECTMEMORY_BLOCK_SIZE;
    cryptlen *= CRYPTPROTECTMEMORY_BLOCK_SIZE;

    cryptdata = snewn(cryptlen, char);
    memset(cryptdata, 0, cryptlen);
    strcpy(cryptdata, realname);

    /*
     * CRYPTPROTECTMEMORY_CROSS_PROCESS causes CryptProtectMemory to
     * use the same key in all processes with this user id, meaning
     * that the next PuTTY process calling this function with the same
     * input will get the same data.
     *
     * (Contrast with CryptProtectData, which invents a new session
     * key every time since its API permits returning more data than
     * was input, so calling _that_ and hashing the output would not
     * be stable.)
     *
     * We don't worry too much if this doesn't work for some reason.
     * Omitting this step still has _some_ privacy value (in that
     * another user can test-hash things to confirm guesses as to
     * where you might be connecting to, but cannot invert SHA-256 in
     * the absence of any plausible guess). So we don't abort if we
     * can't call CryptProtectMemory at all, or if it fails.
     */
    if (got_crypt())
        p_CryptProtectMemory(cryptdata, cryptlen,
                             CRYPTPROTECTMEMORY_CROSS_PROCESS);

    /*
     * We don't want to give away the length of the hostname either,
     * so having got it back out of CryptProtectMemory we now hash it.
     */
    {
        ssh_hash *h = ssh_hash_new(&ssh_sha256);
        put_string(h, cryptdata, cryptlen);
        ssh_hash_final(h, digest);
    }

    sfree(cryptdata);

    /*
     * Finally, make printable.
     */
    for (i = 0; i < 32; i++) {
        sprintf(retbuf + 2*i, "%02x", digest[i]);
        /* the last of those will also write the trailing NUL */
    }

    return dupstr(retbuf);
}

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
    name = obfuscate_name(pi_name);
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
