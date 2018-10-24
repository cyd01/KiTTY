#include	<sys/types.h>
#include	<sys/stat.h>
#include	<errno.h>
#include	<fcntl.h>
#include	"ourhdr.h"

extern char	*ptsname(int);	/* prototype not in any system header */

int
ptym_open(char *pts_name)
{
	char	*ptr;
	int		fdm;

	strcpy(pts_name, "/dev/ptmx");	/* in case open fails */
	if ( (fdm = open(pts_name, O_RDWR)) < 0)
		return(-1);

	if (grantpt(fdm) < 0) {		/* grant access to slave */
		close(fdm);
		return(-2);
	}
	if (unlockpt(fdm) < 0) {	/* clear slave's lock flag */
		close(fdm);
		return(-3);
	}
	if ( (ptr = ptsname(fdm)) == NULL) {	/* get slave's name */
		close(fdm);
		return(-4);
	}

	strcpy(pts_name, ptr);	/* return name of slave */
	return(fdm);			/* return fd of master */
}
int
ptys_open(int fdm, char *pts_name)
{
	int		fds;

			/* following should allocate controlling terminal */
	if ( (fds = open(pts_name, O_RDWR)) < 0) {
		close(fdm);
		return(-5);
	}

	return(fds);
}
