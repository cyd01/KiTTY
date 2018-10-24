/*
 * pty.h - FIXME
 */

#ifndef FIXME_PTY_H
#define FIXME_PTY_H

#include "telnet.h"		       /* for struct shdata */

/*
 * Called at program startup to actually allocate a pty, so that
 * we can start passing in resize events as soon as they arrive.
 */
void pty_preinit(void);

/*
 * Set the terminal size for the pty.
 */
void pty_resize(int w, int h);

/*
 * Start a program in a subprocess running in the pty we allocated.
 * Returns the fd of the pty master.
 */
int run_program_in_pty(const struct shell_data *shdata,
                       char *directory, char **program_args);

#endif /* FIXME_PTY_H */
