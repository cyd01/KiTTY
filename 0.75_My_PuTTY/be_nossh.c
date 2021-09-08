/*
 * Linking module for PuTTYtel: list the available backends not
 * including ssh.
 */

#include <stdio.h>
#include "putty.h"

const int be_default_protocol = PROT_TELNET;

const char *const appname = "PuTTYtel";

const struct BackendVtable *const backends[] = {
    &telnet_backend,
    &rlogin_backend,
    &supdup_backend,
    &raw_backend,
#ifdef MOD_ADB
    &adb_backend,
#endif
    NULL
};

const size_t n_ui_backends = 1;
