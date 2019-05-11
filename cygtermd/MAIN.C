/*
 * Main program.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "sel.h"
#include "pty.h"
#include "telnet.h"

int signalpipe[2];

sel *asel;
sel_rfd *netr, *ptyr, *sigr;
int ptyfd;
sel_wfd *netw, *ptyw;
Telnet telnet;

#define BUF 65536

void sigchld(int signum)
{
    write(signalpipe[1], "C", 1);
}

void fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "FIXME: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

void net_readdata(sel_rfd *rfd, void *data, size_t len)
{
    if (len == 0)
	exit(0);		       /* EOF on network - client went away */
    telnet_from_net(telnet, data, len);
    if (sel_write(netw, NULL, 0) > BUF)
	sel_rfd_freeze(ptyr);
    if (sel_write(ptyw, NULL, 0) > BUF)
	sel_rfd_freeze(netr);
}

void net_readerr(sel_rfd *rfd, int error)
{
    fprintf(stderr, "standard input: read: %s\n", strerror(errno));
    exit(1);
}

void net_written(sel_wfd *wfd, size_t bufsize)
{
    if (bufsize < BUF)
	sel_rfd_unfreeze(ptyr);
}

void net_writeerr(sel_wfd *wfd, int error)
{
    fprintf(stderr, "standard input: write: %s\n", strerror(errno));
    exit(1);
}

void pty_readdata(sel_rfd *rfd, void *data, size_t len)
{
    if (len == 0)
	exit(0);		       /* EOF on pty */
    telnet_from_pty(telnet, data, len);
    if (sel_write(netw, NULL, 0) > BUF)
	sel_rfd_freeze(ptyr);
    if (sel_write(ptyw, NULL, 0) > BUF)
	sel_rfd_freeze(netr);
}

void pty_readerr(sel_rfd *rfd, int error)
{
    if (error == EIO)		       /* means EOF, on a pty */
	exit(0);
    fprintf(stderr, "pty: read: %s\n", strerror(errno));
    exit(1);
}

void pty_written(sel_wfd *wfd, size_t bufsize)
{
    if (bufsize < BUF)
	sel_rfd_unfreeze(netr);
}

void pty_writeerr(sel_wfd *wfd, int error)
{
    fprintf(stderr, "pty: write: %s\n", strerror(errno));
    exit(1);
}

void sig_readdata(sel_rfd *rfd, void *data, size_t len)
{
    char *p = data;

    while (len > 0) {
	if (*p == 'C') {
	    int status;
	    waitpid(-1, &status, WNOHANG);
	    if (WIFEXITED(status) || WIFSIGNALED(status))
		exit(0);	       /* child process vanished */
	}
    }
}

void sig_readerr(sel_rfd *rfd, int error)
{
    fprintf(stderr, "signal pipe: read: %s\n", strerror(errno));
    exit(1);
}

int main(int argc, char **argv)
{
    int ret;
    int shell_started = 0;
    char *directory = NULL;
    char **program_args = NULL;

    if (argc > 1 && argv[1][0]) {
        directory = argv[1];
        argc--, argv++;
    }
    if (argc > 1) {
        program_args = argv + 1;
    }

    pty_preinit();

    asel = sel_new(NULL);
    netr = sel_rfd_add(asel, 0, net_readdata, net_readerr, NULL);
    netw = sel_wfd_add(asel, 1, net_written, net_writeerr, NULL);
    ptyr = sel_rfd_add(asel, -1, pty_readdata, pty_readerr, NULL);
    ptyw = sel_wfd_add(asel, -1, pty_written, pty_writeerr, NULL);

    telnet = telnet_new(netw, ptyw);

    if (pipe(signalpipe) < 0) {
	perror("pipe");
	return 1;
    }
    sigr = sel_rfd_add(asel, signalpipe[0], sig_readdata,
		       sig_readerr, NULL);

    signal(SIGCHLD, sigchld);

    do {
	struct shell_data shdata;

	ret = sel_iterate(asel, -1);
	if (!shell_started && telnet_shell_ok(telnet, &shdata)) {
	    ptyfd = run_program_in_pty(&shdata, directory, program_args);
	    sel_rfd_setfd(ptyr, ptyfd);
	    sel_wfd_setfd(ptyw, ptyfd);
	    shell_started = 1;
	}
    } while (ret == 0);

    return 0;
}
