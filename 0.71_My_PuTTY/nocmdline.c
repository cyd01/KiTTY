/*
 * nocmdline.c - stubs in applications which don't do the
 * standard(ish) PuTTY tools' command-line parsing
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "putty.h"

/*
 * Stub version of the function in cmdline.c which provides the
 * password to SSH authentication by remembering it having been passed
 * as a command-line option. If we're not doing normal command-line
 * handling, then there is no such option, so that function always
 * returns failure.
 */
int cmdline_get_passwd_input(prompts_t *p)
{
    return -1;
}

/*
 * The main cmdline_process_param function is normally called from
 * applications' main(). An application linking against this stub
 * module shouldn't have a main() that calls it in the first place :-)
 * but it is just occasionally called by other supporting functions,
 * such as one in uxputty.c which sometimes handles a non-option
 * argument by making up equivalent options and passing them back to
 * this function. So we have to provide a link-time stub of this
 * function, but it had better not end up being called at run time.
 */
int cmdline_process_param(const char *p, char *value,
                          int need_save, Conf *conf)
{
    unreachable("cmdline_process_param should never be called");
}

/*
 * This variable will be referred to, so it has to exist. It's ignored.
 */
int cmdline_tooltype = 0;
