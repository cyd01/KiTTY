/*
 * Linking module for PuTTY proper: list the available backends
 * including ssh.
 */

#include <stdio.h>
#include "putty.h"

/*
 * This appname is not strictly in the right place, since Plink
 * also uses this module. However, Plink doesn't currently use any
 * of the dialog-box sorts of things that make use of appname, so
 * it shouldn't do any harm here. I'm trying to avoid having to
 * have tiny little source modules containing nothing but
 * declarations of appname, for as long as I can...
 */
const char *const appname = "PuTTY";

const int be_default_protocol = PROT_SSH;

const struct BackendVtable *const backends[] = {
    &ssh_backend,
    &telnet_backend,
    &rlogin_backend,
    &supdup_backend,
    &raw_backend,
    &sshconn_backend,
#ifdef MOD_ADB
    &adb_backend,
#endif
    NULL
};

const size_t n_ui_backends = 1;
