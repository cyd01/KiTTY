/*
 * Linking module for programs that do not support selection of backend
 * (such as pterm).
 */

#include <stdio.h>
#include "putty.h"

const int be_default_protocol = -1;

const struct BackendVtable *const backends[] = {
    NULL
};

const size_t n_ui_backends = 0;
