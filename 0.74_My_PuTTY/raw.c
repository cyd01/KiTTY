/*
 * "Raw" backend.
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "putty.h"

#define RAW_MAX_BACKLOG 4096

typedef struct Raw Raw;
struct Raw {
    Socket *s;
    bool closed_on_socket_error;
    size_t bufsize;
    Seat *seat;
    LogContext *logctx;
    bool sent_console_eof, sent_socket_eof, session_started;

    Conf *conf;

    Plug plug;
    Backend backend;
};

static void raw_size(Backend *be, int width, int height);

static void c_write(Raw *raw, const void *buf, size_t len)
{
    size_t backlog = seat_stdout(raw->seat, buf, len);
    sk_set_frozen(raw->s, backlog > RAW_MAX_BACKLOG);
}

static void raw_log(Plug *plug, int type, SockAddr *addr, int port,
		    const char *error_msg, int error_code)
{
    Raw *raw = container_of(plug, Raw, plug);
    backend_socket_log(raw->seat, raw->logctx, type, addr, port,
                       error_msg, error_code, raw->conf, raw->session_started);
}

static void raw_check_close(Raw *raw)
{
    /*
     * Called after we send EOF on either the socket or the console.
     * Its job is to wind up the session once we have sent EOF on both.
     */
    if (raw->sent_console_eof && raw->sent_socket_eof) {
        if (raw->s) {
            sk_close(raw->s);
            raw->s = NULL;
            seat_notify_remote_exit(raw->seat);
        }
    }
}

static void raw_closing(Plug *plug, const char *error_msg, int error_code,
			bool calling_back)
{
    Raw *raw = container_of(plug, Raw, plug);

    if (error_msg) {
        /* A socket error has occurred. */
        if (raw->s) {
            sk_close(raw->s);
            raw->s = NULL;
            raw->closed_on_socket_error = true;
            seat_notify_remote_exit(raw->seat);
        }
        logevent(raw->logctx, error_msg);
        seat_connection_fatal(raw->seat, "%s", error_msg);
    } else {
        /* Otherwise, the remote side closed the connection normally. */
        if (!raw->sent_console_eof && seat_eof(raw->seat)) {
            /*
             * The front end wants us to close the outgoing side of the
             * connection as soon as we see EOF from the far end.
             */
            if (!raw->sent_socket_eof) {
                if (raw->s)
                    sk_write_eof(raw->s);
                raw->sent_socket_eof= true;
            }
        }
        raw->sent_console_eof = true;
        raw_check_close(raw);
    }
}

static void raw_receive(Plug *plug, int urgent, const char *data, size_t len)
{
    Raw *raw = container_of(plug, Raw, plug);
    c_write(raw, data, len);
    /* We count 'session start', for proxy logging purposes, as being
     * when data is received from the network and printed. */
    raw->session_started = true;
}

static void raw_sent(Plug *plug, size_t bufsize)
{
    Raw *raw = container_of(plug, Raw, plug);
    raw->bufsize = bufsize;
}

static const PlugVtable Raw_plugvt = {
    raw_log,
    raw_closing,
    raw_receive,
    raw_sent
};

/*
 * Called to set up the raw connection.
 * 
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'. It must be
 * freed by the caller.
 */
static const char *raw_init(Seat *seat, Backend **backend_handle,
                            LogContext *logctx, Conf *conf,
			    const char *host, int port, char **realhost,
                            bool nodelay, bool keepalive)
{
    SockAddr *addr;
    const char *err;
    Raw *raw;
    int addressfamily;
    char *loghost;

    /* No local authentication phase in this protocol */
    seat_set_trust_status(seat, false);

    raw = snew(Raw);
    raw->plug.vt = &Raw_plugvt;
    raw->backend.vt = &raw_backend;
    raw->s = NULL;
    raw->closed_on_socket_error = false;
    *backend_handle = &raw->backend;
    raw->sent_console_eof = raw->sent_socket_eof = false;
    raw->bufsize = 0;
    raw->session_started = false;
    raw->conf = conf_copy(conf);

    raw->seat = seat;
    raw->logctx = logctx;

    addressfamily = conf_get_int(conf, CONF_addressfamily);
    /*
     * Try to find host.
     */
    addr = name_lookup(host, port, realhost, conf, addressfamily,
                       raw->logctx, "main connection");
    if ((err = sk_addr_error(addr)) != NULL) {
	sk_addr_free(addr);
	return err;
    }

    if (port < 0)
	port = 23;		       /* default telnet port */

    /*
     * Open socket.
     */
    raw->s = new_connection(addr, *realhost, port, false, true, nodelay,
                            keepalive, &raw->plug, conf);
    if ((err = sk_socket_error(raw->s)) != NULL)
	return err;

    loghost = conf_get_str(conf, CONF_loghost);
    if (*loghost) {
	char *colon;

	sfree(*realhost);
	*realhost = dupstr(loghost);

	colon = host_strrchr(*realhost, ':');
	if (colon)
	    *colon++ = '\0';
    }

    return NULL;
}

static void raw_free(Backend *be)
{
    Raw *raw = container_of(be, Raw, backend);

    if (raw->s)
	sk_close(raw->s);
    conf_free(raw->conf);
    sfree(raw);
}

/*
 * Stub routine (we don't have any need to reconfigure this backend).
 */
static void raw_reconfig(Backend *be, Conf *conf)
{
}

/*
 * Called to send data down the raw connection.
 */
static size_t raw_send(Backend *be, const char *buf, size_t len)
{
    Raw *raw = container_of(be, Raw, backend);

    if (raw->s == NULL)
	return 0;

    raw->bufsize = sk_write(raw->s, buf, len);

    return raw->bufsize;
}

/*
 * Called to query the current socket sendability status.
 */
static size_t raw_sendbuffer(Backend *be)
{
    Raw *raw = container_of(be, Raw, backend);
    return raw->bufsize;
}

/*
 * Called to set the size of the window
 */
static void raw_size(Backend *be, int width, int height)
{
    /* Do nothing! */
    return;
}

/*
 * Send raw special codes. We only handle outgoing EOF here.
 */
static void raw_special(Backend *be, SessionSpecialCode code, int arg)
{
    Raw *raw = container_of(be, Raw, backend);
    if (code == SS_EOF && raw->s) {
        sk_write_eof(raw->s);
        raw->sent_socket_eof= true;
        raw_check_close(raw);
    }

    return;
}

/*
 * Return a list of the special codes that make sense in this
 * protocol.
 */
static const SessionSpecial *raw_get_specials(Backend *be)
{
    return NULL;
}

static bool raw_connected(Backend *be)
{
    Raw *raw = container_of(be, Raw, backend);
    return raw->s != NULL;
}

static bool raw_sendok(Backend *be)
{
    return true;
}

static void raw_unthrottle(Backend *be, size_t backlog)
{
    Raw *raw = container_of(be, Raw, backend);
    sk_set_frozen(raw->s, backlog > RAW_MAX_BACKLOG);
}

static bool raw_ldisc(Backend *be, int option)
{
    if (option == LD_EDIT || option == LD_ECHO)
	return true;
    return false;
}

static void raw_provide_ldisc(Backend *be, Ldisc *ldisc)
{
    /* This is a stub. */
}

static int raw_exitcode(Backend *be)
{
    Raw *raw = container_of(be, Raw, backend);
    if (raw->s != NULL)
        return -1;                     /* still connected */
    else if (raw->closed_on_socket_error)
        return INT_MAX;     /* a socket error counts as an unclean exit */
    else
        /* Exit codes are a meaningless concept in the Raw protocol */
        return 0;
}

/*
 * cfg_info for Raw does nothing at all.
 */
static int raw_cfg_info(Backend *be)
{
    return 0;
}

const struct BackendVtable raw_backend = {
    raw_init,
    raw_free,
    raw_reconfig,
    raw_send,
    raw_sendbuffer,
    raw_size,
    raw_special,
    raw_get_specials,
    raw_connected,
    raw_exitcode,
    raw_sendok,
    raw_ldisc,
    raw_provide_ldisc,
    raw_unthrottle,
    raw_cfg_info,
    NULL /* test_for_upstream */,
    "raw",
    PROT_RAW,
    0
};
