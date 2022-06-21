/*
 * Common pieces between the platform console frontend modules.
 */

#include <stdbool.h>
#include <stdarg.h>

#include "putty.h"
#include "misc.h"
#include "console.h"

const char hk_absentmsg_common_fmt[] =
    "The server's host key is not cached. You have no guarantee\n"
    "that the server is the computer you think it is.\n"
    "The server's %s key fingerprint is:\n"
    "%s\n";
const char hk_absentmsg_interactive_intro[] =
    "If you trust this host, enter \"y\" to add the key to\n"
    "PuTTY's cache and carry on connecting.\n"
    "If you want to carry on connecting just once, without\n"
    "adding the key to the cache, enter \"n\".\n"
    "If you do not trust this host, press Return to abandon the\n"
    "connection.\n";
const char hk_absentmsg_interactive_prompt[] =
    "Store key in cache? (y/n, Return cancels connection, "
    "i for more info) ";

const char hk_wrongmsg_common_fmt[] =
    "WARNING - POTENTIAL SECURITY BREACH!\n"
    "The server's host key does not match the one PuTTY has\n"
    "cached. This means that either the server administrator\n"
    "has changed the host key, or you have actually connected\n"
    "to another computer pretending to be the server.\n"
    "The new %s key fingerprint is:\n"
    "%s\n";
const char hk_wrongmsg_interactive_intro[] =
    "If you were expecting this change and trust the new key,\n"
    "enter \"y\" to update PuTTY's cache and continue connecting.\n"
    "If you want to carry on connecting but without updating\n"
    "the cache, enter \"n\".\n"
    "If you want to abandon the connection completely, press\n"
    "Return to cancel. Pressing Return is the ONLY guaranteed\n"
    "safe choice.\n";
const char hk_wrongmsg_interactive_prompt[] =
    "Update cached key? (y/n, Return cancels connection, "
    "i for more info) ";

const char weakcrypto_msg_common_fmt[] =
    "The first %s supported by the server is\n"
    "%s, which is below the configured warning threshold.\n";

const char weakhk_msg_common_fmt[] =
    "The first host key type we have stored for this server\n"
    "is %s, which is below the configured warning threshold.\n"
    "The server also provides the following types of host key\n"
    "above the threshold, which we do not have stored:\n"
    "%s\n";

const char console_continue_prompt[] = "Continue with connection? (y/n) ";
const char console_abandoned_msg[] = "Connection abandoned.\n";

bool console_batch_mode = false;

/*
 * Error message and/or fatal exit functions, all based on
 * console_print_error_msg which the platform front end provides.
 */
void console_print_error_msg_fmt_v(
    const char *prefix, const char *fmt, va_list ap)
{
    char *msg = dupvprintf(fmt, ap);
    console_print_error_msg(prefix, msg);
    sfree(msg);
}

void console_print_error_msg_fmt(const char *prefix, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    console_print_error_msg_fmt_v(prefix, fmt, ap);
    va_end(ap);
}

void modalfatalbox(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    console_print_error_msg_fmt_v("FATAL ERROR", fmt, ap);
    va_end(ap);
    cleanup_exit(1);
}

void nonfatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    console_print_error_msg_fmt_v("ERROR", fmt, ap);
    va_end(ap);
}

void console_connection_fatal(Seat *seat, const char *msg)
{
    console_print_error_msg("FATAL ERROR", msg);
    cleanup_exit(1);
}

/*
 * Console front ends redo their select() or equivalent every time, so
 * they don't need separate timer handling.
 */
void timer_change_notify(unsigned long next)
{
}
