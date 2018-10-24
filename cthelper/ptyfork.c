#include	<sys/types.h>
#include	<termios.h>
#ifndef	TIOCGWINSZ
#include	<sys/ioctl.h>	/* 44BSD requires this too */
#endif
#include	"ourhdr.h"

pid_t
pty_fork(int *ptrfdm, char *slave_name,
		 const struct termios *slave_termios,
		 const struct winsize *slave_winsize)
{
	int		fdm, fds;
	pid_t	pid;
	char	pts_name[20];

	if ( (fdm = ptym_open(pts_name)) < 0)
		err_sys("can't open master pty: %s", pts_name);

	if (slave_name != NULL)
		strcpy(slave_name, pts_name);	/* return name of slave */

	if ( (pid = fork()) < 0)
		return(-1);

	else if (pid == 0) {		/* child */
		if (setsid() < 0)
			err_sys("setsid error");

					/* SVR4 acquires controlling terminal on open() */
		if ( (fds = ptys_open(fdm, pts_name)) < 0)
			err_sys("can't open slave pty");
		close(fdm);		/* all done with master in child */

#if	defined(TIOCSCTTY) && !defined(CIBAUD)
				/* 44BSD way to acquire controlling terminal */
				/* !CIBAUD to avoid doing this under SunOS */
		if (ioctl(fds, TIOCSCTTY, (char *) 0) < 0)
			err_sys("TIOCSCTTY error");
#endif
				/* set slave's termios and window size */
		if (slave_termios != NULL) {
			if (tcsetattr(fds, TCSANOW, slave_termios) < 0)
				err_sys("tcsetattr error on slave pty");
		}
		if (slave_winsize != NULL) {
			if (ioctl(fds, TIOCSWINSZ, slave_winsize) < 0)
				err_sys("TIOCSWINSZ error on slave pty");
		}
				/* slave becomes stdin/stdout/stderr of child */
		if (dup2(fds, STDIN_FILENO) != STDIN_FILENO)
			err_sys("dup2 error to stdin");
		if (dup2(fds, STDOUT_FILENO) != STDOUT_FILENO)
			err_sys("dup2 error to stdout");
		if (dup2(fds, STDERR_FILENO) != STDERR_FILENO)
			err_sys("dup2 error to stderr");
		if (fds > STDERR_FILENO)
			close(fds);
		return(0);		/* child returns 0 just like fork() */

	} else {					/* parent */
		*ptrfdm = fdm;	/* return fd of master */
		return(pid);	/* parent returns pid of child */
	}
}
