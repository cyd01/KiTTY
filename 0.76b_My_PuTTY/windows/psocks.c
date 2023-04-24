/*
 * Main program for Windows psocks.
 */

#include "putty.h"
#include "ssh.h"
#include "psocks.h"

static const PsocksPlatform platform = {
    NULL /* open_pipes */,
    NULL /* start_subcommand */,
};

int main(int argc, char **argv)
{
    psocks_state *ps = psocks_new(&platform);
    psocks_cmdline(ps, argc, argv);

    sk_init();
    winselcli_setup();
    psocks_start(ps);

    cli_main_loop(cliloop_null_pre, cliloop_null_post, NULL);
}
