/*
 * Rlogin backend.
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>

#include "putty.h"

#define RLOGIN_MAX_BACKLOG 4096

typedef struct Rlogin Rlogin;
struct Rlogin {
    Socket *s;
    bool closed_on_socket_error;
    int bufsize;
    bool firstbyte;
    bool cansize;
    int term_width, term_height;
    Seat *seat;
    LogContext *logctx;

    Conf *conf;

    /* In case we need to read a username from the terminal before starting */
    prompts_t *prompt;

    Plug plug;
    Backend backend;
};

static void c_write(Rlogin *rlogin, const void *buf, size_t len)
{
    size_t backlog = seat_stdout(rlogin->seat, buf, len);
    sk_set_frozen(rlogin->s, backlog > RLOGIN_MAX_BACKLOG);
}

static void rlogin_log(Plug *plug, int type, SockAddr *addr, int port,
		       const char *error_msg, int error_code)
{
    Rlogin *rlogin = container_of(plug, Rlogin, plug);
    backend_socket_log(rlogin->seat, rlogin->logctx, type, addr, port,
                       error_msg, error_code,
                       rlogin->conf, !rlogin->firstbyte);
}

static void rlogin_closing(Plug *plug, const char *error_msg, int error_code,
			   bool calling_back)
{
    Rlogin *rlogin = container_of(plug, Rlogin, plug);

    /*
     * We don't implement independent EOF in each direction for Telnet
     * connections; as soon as we get word that the remote side has
     * sent us EOF, we wind up the whole connection.
     */

    if (rlogin->s) {
        sk_close(rlogin->s);
        rlogin->s = NULL;
        if (error_msg)
            rlogin->closed_on_socket_error = true;
	seat_notify_remote_exit(rlogin->seat);
    }
    if (error_msg) {
	/* A socket error has occurred. */
        logevent(rlogin->logctx, error_msg);
        seat_connection_fatal(rlogin->seat, "%s", error_msg);
    }				       /* Otherwise, the remote side closed the connection normally. */
}

static void rlogin_receive(
    Plug *plug, int urgent, const char *data, size_t len)
{
    Rlogin *rlogin = container_of(plug, Rlogin, plug);
    if (len == 0)
        return;
    if (urgent == 2) {
	char c;

	c = *data++;
	len--;
	if (c == '\x80') {
	    rlogin->cansize = true;
            backend_size(&rlogin->backend,
                         rlogin->term_width, rlogin->term_height);
        }
	/*
	 * We should flush everything (aka Telnet SYNCH) if we see
	 * 0x02, and we should turn off and on _local_ flow control
	 * on 0x10 and 0x20 respectively. I'm not convinced it's
	 * worth it...
	 */
    } else {
	/*
	 * Main rlogin protocol. This is really simple: the first
	 * byte is expected to be NULL and is ignored, and the rest
	 * is printed.
	 */
	if (rlogin->firstbyte) {
	    if (data[0] == '\0') {
		data++;
		len--;
	    }
	    rlogin->firstbyte = false;
	}
	if (len > 0)
            c_write(rlogin, data, len);
    }
}

static void rlogin_sent(Plug *plug, size_t bufsize)
{
    Rlogin *rlogin = container_of(plug, Rlogin, plug);
    rlogin->bufsize = bufsize;
}

static void rlogin_startup(Rlogin *rlogin, const char *ruser)
{
    char z = 0;
    char *p;

    sk_write(rlogin->s, &z, 1);
    p = conf_get_str(rlogin->conf, CONF_localusername);
    sk_write(rlogin->s, p, strlen(p));
    sk_write(rlogin->s, &z, 1);
    sk_write(rlogin->s, ruser, strlen(ruser));
    sk_write(rlogin->s, &z, 1);
    p = conf_get_str(rlogin->conf, CONF_termtype);
    sk_write(rlogin->s, p, strlen(p));
    sk_write(rlogin->s, "/", 1);
    p = conf_get_str(rlogin->conf, CONF_termspeed);
    sk_write(rlogin->s, p, strspn(p, "0123456789"));
    rlogin->bufsize = sk_write(rlogin->s, &z, 1);

    rlogin->prompt = NULL;
}

static const PlugVtable Rlogin_plugvt = {
    rlogin_log,
    rlogin_closing,
    rlogin_receive,
    rlogin_sent
};

/*
 * Called to set up the rlogin connection.
 * 
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'. It must be
 * freed by the caller.
 */
static const char *rlogin_init(Seat *seat, Backend **backend_handle,
                               LogContext *logctx, Conf *conf,
			       const char *host, int port, char **realhost,
			       bool nodelay, bool keepalive)
{
    SockAddr *addr;
    const char *err;
    Rlogin *rlogin;
    char *ruser;
    int addressfamily;
    char *loghost;

    rlogin = snew(Rlogin);
    rlogin->plug.vt = &Rlogin_plugvt;
    rlogin->backend.vt = &rlogin_backend;
    rlogin->s = NULL;
    rlogin->closed_on_socket_error = false;
    rlogin->seat = seat;
    rlogin->logctx = logctx;
    rlogin->term_width = conf_get_int(conf, CONF_width);
    rlogin->term_height = conf_get_int(conf, CONF_height);
    rlogin->firstbyte = true;
    rlogin->cansize = false;
    rlogin->prompt = NULL;
    rlogin->conf = conf_copy(conf);
    *backend_handle = &rlogin->backend;

    addressfamily = conf_get_int(conf, CONF_addressfamily);
    /*
     * Try to find host.
     */
    addr = name_lookup(host, port, realhost, conf, addressfamily,
                       rlogin->logctx, "rlogin connection");
    if ((err = sk_addr_error(addr)) != NULL) {
	sk_addr_free(addr);
	return err;
    }

    if (port < 0)
	port = 513;		       /* default rlogin port */

    /*
     * Open socket.
     */
    rlogin->s = new_connection(addr, *realhost, port, true, false,
			       nodelay, keepalive, &rlogin->plug, conf);
    if ((err = sk_socket_error(rlogin->s)) != NULL)
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

    /*
     * Send local username, remote username, terminal type and
     * terminal speed - unless we don't have the remote username yet,
     * in which case we prompt for it and may end up deferring doing
     * anything else until the local prompt mechanism returns.
     */
    if ((ruser = get_remote_username(conf)) != NULL) {
        rlogin_startup(rlogin, ruser);
        sfree(ruser);
    } else {
        int ret;

        rlogin->prompt = new_prompts();
        rlogin->prompt->to_server = true;
        rlogin->prompt->from_server = false;
        rlogin->prompt->name = dupstr("Rlogin login name");
        add_prompt(rlogin->prompt, dupstr("rlogin username: "), true); 
        ret = seat_get_userpass_input(rlogin->seat, rlogin->prompt, NULL);
        if (ret >= 0) {
            rlogin_startup(rlogin, rlogin->prompt->prompts[0]->result);
        }
    }

    return NULL;
}

static void rlogin_free(Backend *be)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);

    if (rlogin->prompt)
        free_prompts(rlogin->prompt);
    if (rlogin->s)
	sk_close(rlogin->s);
    conf_free(rlogin->conf);
    sfree(rlogin);
}

/*
 * Stub routine (we don't have any need to reconfigure this backend).
 */
static void rlogin_reconfig(Backend *be, Conf *conf)
{
}

/*
 * Called to send data down the rlogin connection.
 */
static size_t rlogin_send(Backend *be, const char *buf, size_t len)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);
    bufchain bc;

    if (rlogin->s == NULL)
	return 0;

    bufchain_init(&bc);
    bufchain_add(&bc, buf, len);

    if (rlogin->prompt) {
        /*
         * We're still prompting for a username, and aren't talking
         * directly to the network connection yet.
         */
        int ret = seat_get_userpass_input(rlogin->seat, rlogin->prompt, &bc);
        if (ret >= 0) {
            rlogin_startup(rlogin, rlogin->prompt->prompts[0]->result);
            /* that nulls out rlogin->prompt, so then we'll start sending
             * data down the wire in the obvious way */
        }
    }

    if (!rlogin->prompt) {
        while (bufchain_size(&bc) > 0) {
            ptrlen data = bufchain_prefix(&bc);
            rlogin->bufsize = sk_write(rlogin->s, data.ptr, data.len);
            bufchain_consume(&bc, len);
        }
    }

    bufchain_clear(&bc);

    return rlogin->bufsize;
}

/*
 * Called to query the current socket sendability status.
 */
static size_t rlogin_sendbuffer(Backend *be)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);
    return rlogin->bufsize;
}

/*
 * Called to set the size of the window
 */
static void rlogin_size(Backend *be, int width, int height)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);
    char b[12] = { '\xFF', '\xFF', 0x73, 0x73, 0, 0, 0, 0, 0, 0, 0, 0 };

    rlogin->term_width = width;
    rlogin->term_height = height;

    if (rlogin->s == NULL || !rlogin->cansize)
	return;

    b[6] = rlogin->term_width >> 8;
    b[7] = rlogin->term_width & 0xFF;
    b[4] = rlogin->term_height >> 8;
    b[5] = rlogin->term_height & 0xFF;
    rlogin->bufsize = sk_write(rlogin->s, b, 12);
    return;
}

/*
 * Send rlogin special codes.
 */
static void rlogin_special(Backend *be, SessionSpecialCode code, int arg)
{
    /* Do nothing! */
    return;
}

/*
 * Return a list of the special codes that make sense in this
 * protocol.
 */
static const SessionSpecial *rlogin_get_specials(Backend *be)
{
    return NULL;
}

static bool rlogin_connected(Backend *be)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);
    return rlogin->s != NULL;
}

static bool rlogin_sendok(Backend *be)
{
    /* Rlogin *rlogin = container_of(be, Rlogin, backend); */
    return true;
}

static void rlogin_unthrottle(Backend *be, size_t backlog)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);
    sk_set_frozen(rlogin->s, backlog > RLOGIN_MAX_BACKLOG);
}

static bool rlogin_ldisc(Backend *be, int option)
{
    /* Rlogin *rlogin = container_of(be, Rlogin, backend); */
    return false;
}

static void rlogin_provide_ldisc(Backend *be, Ldisc *ldisc)
{
    /* This is a stub. */
}

static int rlogin_exitcode(Backend *be)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);
    if (rlogin->s != NULL)
        return -1;                     /* still connected */
    else if (rlogin->closed_on_socket_error)
        return INT_MAX;     /* a socket error counts as an unclean exit */
    else
        /* If we ever implement RSH, we'll probably need to do this properly */
        return 0;
}

/*
 * cfg_info for rlogin does nothing at all.
 */
static int rlogin_cfg_info(Backend *be)
{
    return 0;
}

const struct BackendVtable rlogin_backend = {
    rlogin_init,
    rlogin_free,
    rlogin_reconfig,
    rlogin_send,
    rlogin_sendbuffer,
    rlogin_size,
    rlogin_special,
    rlogin_get_specials,
    rlogin_connected,
    rlogin_exitcode,
    rlogin_sendok,
    rlogin_ldisc,
    rlogin_provide_ldisc,
    rlogin_unthrottle,
    rlogin_cfg_info,
    NULL /* test_for_upstream */,
    "rlogin",
    PROT_RLOGIN,
    513
};
