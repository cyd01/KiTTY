/*
 * clicons.c: definitions limited to tools that link against both
 * console.c and cmdline.c.
 */

#include "putty.h"

static const LogPolicyVtable console_cli_logpolicy_vt = {
    .eventlog = console_eventlog,
    .askappend = console_askappend,
    .logging_error = console_logging_error,
    .verbose = cmdline_lp_verbose,
};
LogPolicy console_cli_logpolicy[1] = {{ &console_cli_logpolicy_vt }};
