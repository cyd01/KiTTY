/*
 * pty.c - pseudo-terminal handling
 */

#define _XOPEN_SOURCE
#include <features.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <pwd.h>

#include "pty.h"
#include "malloc.h"

static char ptyname[FILENAME_MAX];
int master = -1;

void pty_preinit(void)
{
    /*
     * Allocate the pty.
     */
    master = open("/dev/ptmx", O_RDWR);
    if (master < 0) {
	perror("/dev/ptmx: open");
	exit(1);
    }

    if (grantpt(master) < 0) {
	perror("grantpt");
	exit(1);
    }
    
    if (unlockpt(master) < 0) {
	perror("unlockpt");
	exit(1);
    }
}

void pty_resize(int w, int h)
{
    struct winsize sz;

    assert(master >= 0);

    sz.ws_row = h;
    sz.ws_col = w;
    sz.ws_xpixel = sz.ws_ypixel = 0;
    ioctl(master, TIOCSWINSZ, &sz);
}

int run_program_in_pty(const struct shell_data *shdata,
                       char *directory, char **program_args)
{
    int slave, pid;
    char *fallback_args[2];

    assert(master >= 0);

    ptyname[FILENAME_MAX-1] = '\0';
    strncpy(ptyname, ptsname(master), FILENAME_MAX-1);

#if 0
    {
	struct winsize ws;
	struct termios ts;

	/*
	 * FIXME: think up some good defaults here
	 */

	if (!ioctl(0, TIOCGWINSZ, &ws))
	    ioctl(master, TIOCSWINSZ, &ws);
	if (!tcgetattr(0, &ts))
	    tcsetattr(master, TCSANOW, &ts);
    }
#endif

    slave = open(ptyname, O_RDWR | O_NOCTTY);
    if (slave < 0) {
	perror("slave pty: open");
	return 1;
    }

    /*
     * Fork and execute the command.
     */
    pid = fork();
    if (pid < 0) {
	perror("fork");
	return 1;
    }

    if (pid == 0) {
	int i, fd;

	/*
	 * We are the child.
	 */
	close(master);

	fcntl(slave, F_SETFD, 0);    /* don't close on exec */
	dup2(slave, 0);
	dup2(slave, 1);
	if (slave != 0 && slave != 1)
	    close(slave);
	dup2(1, 2);
	setsid();
	setpgrp();
        i = 0;
#ifdef TIOCNOTTY
        if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
            ioctl(fd, TIOCNOTTY, &i);
            close(fd);
        }
#endif
        /*
         * Make the new pty our controlling terminal. On some OSes
         * this is done with TIOCSCTTY; Cygwin doesn't have that, so
         * instead it's done by simply opening the pty without
         * O_NOCTTY. This code is primarily intended for Cygwin, but
         * it's useful to have it work in other contexts for testing
         * purposes, so I leave the TIOCSCTTY here anyway.
         */
        if ((fd = open(ptyname, O_RDWR)) >= 0) {
#ifdef TIOCSCTTY
            ioctl(fd, TIOCSCTTY, &i);
#endif
            close(fd);
        } else {
            perror("slave pty: open");
            exit(127);
        }
	tcsetpgrp(0, getpgrp());

	for (i = 0; i < shdata->nenvvars; i++)
            putenv(shdata->envvars[i]);
	if (shdata->termtype)
            putenv(shdata->termtype);

        if (directory)
            chdir(directory);

	/*
	 * Use the provided shell program name, if the user gave
	 * one. Failing that, use $SHELL; failing that, look up
	 * the user's default shell in the password file; failing
	 * _that_, revert to the bog-standard /bin/sh.
	 */
	if (!program_args) {
            char *shell;
            
	    shell = getenv("SHELL");
            if (!shell) {
                const char *login;
                uid_t uid;
                struct passwd *pwd;

                /*
                 * For maximum generality in the face of multiple
                 * /etc/passwd entries with different login names and
                 * shells but a shared uid, we start by using
                 * getpwnam(getlogin()) if it's available - but we
                 * insist that its uid must match our real one, or we
                 * give up and fall back to getpwuid(getuid()).
                 */
                uid = getuid();
                login = getlogin();
                if (login && (pwd = getpwnam(login)) && pwd->pw_uid == uid)
                    shell = pwd->pw_shell;
                else if ((pwd = getpwuid(uid)))
                    shell = pwd->pw_shell;
            }
            if (!shell)
                shell = "/bin/sh";

            fallback_args[0] = shell;
            fallback_args[1] = NULL;
            program_args = fallback_args;
        }

        execv(program_args[0], program_args);

	/*
	 * If we're here, exec has gone badly foom.
	 */
	perror("exec");
	exit(127);
    }

    close(slave);

    return master;
}
