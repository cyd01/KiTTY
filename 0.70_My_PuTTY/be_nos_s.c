/*
 * Linking module for PuTTYtel: list the available backends not
 * including ssh.
 */

#include <stdio.h>
#include "putty.h"

const int be_default_protocol = PROT_TELNET;

const char *const appname = "PuTTYtel";

Backend *backends[] = {
    &telnet_backend,
    &rlogin_backend,
    &raw_backend,
#ifdef ADBPORT
    &adb_backend,
#endif
    &serial_backend,
#ifdef CYGTERMPORT
    &cygterm_backend,
#endif
    NULL
};

/*
 * Stub implementations of functions not used in non-ssh versions.
 */
void random_save_seed(void)
{
}

void random_destroy_seed(void)
{
}

void noise_ultralight(unsigned long data)
{
}
