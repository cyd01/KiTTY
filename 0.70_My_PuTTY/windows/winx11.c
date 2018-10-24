/*
 * winx11.c: fetch local auth data for X forwarding.
 */

#include <ctype.h>
#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "ssh.h"

void platform_get_x11_auth(struct X11Display *disp, Conf *conf)
{
    char *xauthpath = conf_get_filename(conf, CONF_xauthfile)->path;
    if (xauthpath[0])
	x11_get_auth_from_authfile(disp, xauthpath);
}

const int platform_uses_x11_unix_by_default = FALSE;
