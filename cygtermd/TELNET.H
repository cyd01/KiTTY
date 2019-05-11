/*
 * Header declaring Telnet-handling functions.
 */

#ifndef FIXME_TELNET_H
#define FIXME_TELNET_H

#include "sel.h"

typedef struct telnet_tag *Telnet;

struct shell_data {
    char **envvars;		       /* array of "VAR=value" terms */
    int nenvvars;
    char *termtype;
};

/*
 * Create and destroy a Telnet structure.
 */
Telnet telnet_new(sel_wfd *net, sel_wfd *pty);
void telnet_free(Telnet telnet);

/*
 * Process data read from the pty.
 */
void telnet_from_pty(Telnet telnet, char *buf, int len);

/*
 * Process Telnet protocol data received from the network.
 */
void telnet_from_net(Telnet telnet, char *buf, int len);

/*
 * Return true if pre-shell-startup negotiations are complete and
 * it's safe to start the shell subprocess now. On a true return,
 * also fills in the shell_data structure.
 */
int telnet_shell_ok(Telnet telnet, struct shell_data *shdata);

#endif /* FIXME_TELNET_H */
