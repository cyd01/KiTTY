/*
 * Implement the "session" channel type for the SSH server.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "putty.h"
#include "ssh.h"
#include "sshchan.h"
#include "sshserver.h"
#include "sftp.h"

typedef struct sesschan {
    SshChannel *c;

    LogContext *parent_logctx, *child_logctx;
    Conf *conf;
    const SftpServerVtable *sftpserver_vt;

    LogPolicy logpolicy;
    Seat seat;

    bool want_pty;
    struct ssh_ttymodes ttymodes;
    int wc, hc, wp, hp;
    strbuf *termtype;

    bool ignoring_input;
    bool seen_eof, seen_exit;

    Plug xfwd_plug;
    int n_x11_sockets;
    Socket *x11_sockets[MAX_X11_SOCKETS];

    Plug agentfwd_plug;
    Socket *agentfwd_socket;

    Backend *backend;

    bufchain subsys_input;
    SftpServer *sftpsrv;
    ScpServer *scpsrv;

    Channel chan;
} sesschan;

static void sesschan_free(Channel *chan);
static size_t sesschan_send(
    Channel *chan, bool is_stderr, const void *, size_t);
static void sesschan_send_eof(Channel *chan);
static char *sesschan_log_close_msg(Channel *chan);
static bool sesschan_want_close(Channel *, bool, bool);
static void sesschan_set_input_wanted(Channel *chan, bool wanted);
static bool sesschan_run_shell(Channel *chan);
static bool sesschan_run_command(Channel *chan, ptrlen command);
static bool sesschan_run_subsystem(Channel *chan, ptrlen subsys);
static bool sesschan_enable_x11_forwarding(
    Channel *chan, bool oneshot, ptrlen authproto, ptrlen authdata,
    unsigned screen_number);
static bool sesschan_enable_agent_forwarding(Channel *chan);
static bool sesschan_allocate_pty(
    Channel *chan, ptrlen termtype, unsigned width, unsigned height,
    unsigned pixwidth, unsigned pixheight, struct ssh_ttymodes modes);
static bool sesschan_set_env(Channel *chan, ptrlen var, ptrlen value);
static bool sesschan_send_break(Channel *chan, unsigned length);
static bool sesschan_send_signal(Channel *chan, ptrlen signame);
static bool sesschan_change_window_size(
    Channel *chan, unsigned width, unsigned height,
    unsigned pixwidth, unsigned pixheight);

static const struct ChannelVtable sesschan_channelvt = {
    sesschan_free,
    chan_remotely_opened_confirmation,
    chan_remotely_opened_failure,
    sesschan_send,
    sesschan_send_eof,
    sesschan_set_input_wanted,
    sesschan_log_close_msg,
    sesschan_want_close,
    chan_no_exit_status,
    chan_no_exit_signal,
    chan_no_exit_signal_numeric,
    sesschan_run_shell,
    sesschan_run_command,
    sesschan_run_subsystem,
    sesschan_enable_x11_forwarding,
    sesschan_enable_agent_forwarding,
    sesschan_allocate_pty,
    sesschan_set_env,
    sesschan_send_break,
    sesschan_send_signal,
    sesschan_change_window_size,
    chan_no_request_response,
};

static size_t sftp_chan_send(
    Channel *chan, bool is_stderr, const void *, size_t);
static void sftp_chan_send_eof(Channel *chan);
static char *sftp_log_close_msg(Channel *chan);

static const struct ChannelVtable sftp_channelvt = {
    sesschan_free,
    chan_remotely_opened_confirmation,
    chan_remotely_opened_failure,
    sftp_chan_send,
    sftp_chan_send_eof,
    sesschan_set_input_wanted,
    sftp_log_close_msg,
    chan_default_want_close,
    chan_no_exit_status,
    chan_no_exit_signal,
    chan_no_exit_signal_numeric,
    chan_no_run_shell,
    chan_no_run_command,
    chan_no_run_subsystem,
    chan_no_enable_x11_forwarding,
    chan_no_enable_agent_forwarding,
    chan_no_allocate_pty,
    chan_no_set_env,
    chan_no_send_break,
    chan_no_send_signal,
    chan_no_change_window_size,
    chan_no_request_response,
};

static size_t scp_chan_send(
    Channel *chan, bool is_stderr, const void *, size_t);
static void scp_chan_send_eof(Channel *chan);
static void scp_set_input_wanted(Channel *chan, bool wanted);
static char *scp_log_close_msg(Channel *chan);

static const struct ChannelVtable scp_channelvt = {
    sesschan_free,
    chan_remotely_opened_confirmation,
    chan_remotely_opened_failure,
    scp_chan_send,
    scp_chan_send_eof,
    scp_set_input_wanted,
    scp_log_close_msg,
    chan_default_want_close,
    chan_no_exit_status,
    chan_no_exit_signal,
    chan_no_exit_signal_numeric,
    chan_no_run_shell,
    chan_no_run_command,
    chan_no_run_subsystem,
    chan_no_enable_x11_forwarding,
    chan_no_enable_agent_forwarding,
    chan_no_allocate_pty,
    chan_no_set_env,
    chan_no_send_break,
    chan_no_send_signal,
    chan_no_change_window_size,
    chan_no_request_response,
};

static void sesschan_eventlog(LogPolicy *lp, const char *event) {}
static void sesschan_logging_error(LogPolicy *lp, const char *event) {}
static int sesschan_askappend(
    LogPolicy *lp, Filename *filename,
    void (*callback)(void *ctx, int result), void *ctx) { return 2; }

static const LogPolicyVtable sesschan_logpolicy_vt = {
    sesschan_eventlog,
    sesschan_askappend,
    sesschan_logging_error,
};

static size_t sesschan_seat_output(
    Seat *, bool is_stderr, const void *, size_t);
static bool sesschan_seat_eof(Seat *);
static void sesschan_notify_remote_exit(Seat *seat);
static void sesschan_connection_fatal(Seat *seat, const char *message);
static bool sesschan_get_window_pixel_size(Seat *seat, int *w, int *h);

static const SeatVtable sesschan_seat_vt = {
    sesschan_seat_output,
    sesschan_seat_eof,
    nullseat_get_userpass_input,
    sesschan_notify_remote_exit,
    sesschan_connection_fatal,
    nullseat_update_specials_menu,
    nullseat_get_ttymode,
    nullseat_set_busy_status,
    nullseat_verify_ssh_host_key,
    nullseat_confirm_weak_crypto_primitive,
    nullseat_confirm_weak_cached_hostkey,
    nullseat_is_never_utf8,
    nullseat_echoedit_update,
    nullseat_get_x_display,
    nullseat_get_windowid,
    sesschan_get_window_pixel_size,
    nullseat_stripctrl_new,
    nullseat_set_trust_status,
};

Channel *sesschan_new(SshChannel *c, LogContext *logctx,
                      const SftpServerVtable *sftpserver_vt)
{
    sesschan *sess = snew(sesschan);
    memset(sess, 0, sizeof(sesschan));

    sess->c = c;
    sess->chan.vt = &sesschan_channelvt;
    sess->chan.initial_fixed_window_size = 0;
    sess->parent_logctx = logctx;

    /* Start with a completely default Conf */
    sess->conf = conf_new();
    load_open_settings(NULL, sess->conf);

    /* Set close-on-exit = true to suppress uxpty.c's "[pterm: process
     * terminated with status x]" message */
    conf_set_int(sess->conf, CONF_close_on_exit, FORCE_ON);

    sess->seat.vt = &sesschan_seat_vt;
    sess->logpolicy.vt = &sesschan_logpolicy_vt;
    sess->child_logctx = log_init(&sess->logpolicy, sess->conf);

    sess->sftpserver_vt = sftpserver_vt;

    bufchain_init(&sess->subsys_input);

    return &sess->chan;
}

static void sesschan_free(Channel *chan)
{
    sesschan *sess = container_of(chan, sesschan, chan);
    int i;

    delete_callbacks_for_context(sess);
    conf_free(sess->conf);
    if (sess->backend)
        backend_free(sess->backend);
    bufchain_clear(&sess->subsys_input);
    if (sess->sftpsrv)
        sftpsrv_free(sess->sftpsrv);
    for (i = 0; i < sess->n_x11_sockets; i++)
        sk_close(sess->x11_sockets[i]);
    if (sess->agentfwd_socket)
        sk_close(sess->agentfwd_socket);

    sfree(sess);
}

static size_t sesschan_send(Channel *chan, bool is_stderr,
                            const void *data, size_t length)
{
    sesschan *sess = container_of(chan, sesschan, chan);

    if (!sess->backend || sess->ignoring_input)
        return 0;

    return backend_send(sess->backend, data, length);
}

static void sesschan_send_eof(Channel *chan)
{
    sesschan *sess = container_of(chan, sesschan, chan);
    if (sess->backend)
        backend_special(sess->backend, SS_EOF, 0);
}

static char *sesschan_log_close_msg(Channel *chan)
{
    return dupstr("Session channel closed");
}

static void sesschan_set_input_wanted(Channel *chan, bool wanted)
{
    /* I don't think we need to do anything here */
}

static void sesschan_start_backend(sesschan *sess, const char *cmd)
{
    sess->backend = pty_backend_create(
        &sess->seat, sess->child_logctx, sess->conf, NULL, cmd,
        sess->ttymodes, !sess->want_pty);
    backend_size(sess->backend, sess->wc, sess->hc);
}

bool sesschan_run_shell(Channel *chan)
{
    sesschan *sess = container_of(chan, sesschan, chan);

    if (sess->backend)
        return false;

    sesschan_start_backend(sess, NULL);
    return true;
}

bool sesschan_run_command(Channel *chan, ptrlen command)
{
    sesschan *sess = container_of(chan, sesschan, chan);

    if (sess->backend)
        return false;

    /* FIXME: make this possible to configure off */
    if ((sess->scpsrv = scp_recognise_exec(sess->c, sess->sftpserver_vt,
                                           command)) != NULL) {
        sess->chan.vt = &scp_channelvt;
        logevent(sess->parent_logctx, "Starting built-in SCP server");
        return true;
    }

    char *command_str = mkstr(command);
    sesschan_start_backend(sess, command_str);
    sfree(command_str);

    return true;
}

bool sesschan_run_subsystem(Channel *chan, ptrlen subsys)
{
    sesschan *sess = container_of(chan, sesschan, chan);

    if (ptrlen_eq_string(subsys, "sftp") && sess->sftpserver_vt) {
        sess->sftpsrv = sftpsrv_new(sess->sftpserver_vt);
        sess->chan.vt = &sftp_channelvt;
        logevent(sess->parent_logctx, "Starting built-in SFTP subsystem");
        return true;
    }

    return false;
}

static void fwd_log(Plug *plug, int type, SockAddr *addr, int port,
                    const char *error_msg, int error_code)
{ /* don't expect any weirdnesses from a listening socket */ }
static void fwd_closing(Plug *plug, const char *error_msg, int error_code,
                        bool calling_back)
{ /* not here, either */ }

static int xfwd_accepting(Plug *p, accept_fn_t constructor, accept_ctx_t ctx)
{
    sesschan *sess = container_of(p, sesschan, xfwd_plug);
    Plug *plug;
    Channel *chan;
    Socket *s;
    SocketPeerInfo *pi;
    const char *err;

    chan = portfwd_raw_new(sess->c->cl, &plug);
    s = constructor(ctx, plug);
    if ((err = sk_socket_error(s)) != NULL) {
	portfwd_raw_free(chan);
        return 1;
    }
    pi = sk_peer_info(s);
    portfwd_raw_setup(chan, s, ssh_serverside_x11_open(sess->c->cl, chan, pi));
    sk_free_peer_info(pi);

    return 0;
}

static const PlugVtable xfwd_plugvt = {
    fwd_log,
    fwd_closing,
    NULL, /* recv */
    NULL, /* send */
    xfwd_accepting,
};

bool sesschan_enable_x11_forwarding(
    Channel *chan, bool oneshot, ptrlen authproto, ptrlen authdata_hex,
    unsigned screen_number)
{
    sesschan *sess = container_of(chan, sesschan, chan);
    strbuf *authdata_bin;
    size_t i;
    char screensuffix[32];

    if (oneshot)
        return false;                  /* not supported */

    snprintf(screensuffix, sizeof(screensuffix), ".%u", screen_number);

    /*
     * Decode the authorisation data from ASCII hex into binary.
     */
    if (authdata_hex.len % 2)
        return false;                  /* expected an even number of digits */
    authdata_bin = strbuf_new_nm();
    for (i = 0; i < authdata_hex.len; i += 2) {
        const unsigned char *hex = authdata_hex.ptr;
        char hexbuf[3];

        if (!isxdigit(hex[i]) || !isxdigit(hex[i+1])) {
            strbuf_free(authdata_bin);
            return false;              /* not hex */
        }

        hexbuf[0] = hex[i];
        hexbuf[1] = hex[i+1];
        hexbuf[2] = '\0';
        put_byte(authdata_bin, strtoul(hexbuf, NULL, 16));
    }

    sess->xfwd_plug.vt = &xfwd_plugvt;

    sess->n_x11_sockets = platform_make_x11_server(
        &sess->xfwd_plug, appname, 10, screensuffix,
        authproto, ptrlen_from_strbuf(authdata_bin),
        sess->x11_sockets, sess->conf);

    strbuf_free(authdata_bin);
    return sess->n_x11_sockets != 0;
}

static int agentfwd_accepting(
    Plug *p, accept_fn_t constructor, accept_ctx_t ctx)
{
    sesschan *sess = container_of(p, sesschan, agentfwd_plug);
    Plug *plug;
    Channel *chan;
    Socket *s;
    const char *err;

    chan = portfwd_raw_new(sess->c->cl, &plug);
    s = constructor(ctx, plug);
    if ((err = sk_socket_error(s)) != NULL) {
	portfwd_raw_free(chan);
        return 1;
    }
    portfwd_raw_setup(chan, s, ssh_serverside_agent_open(sess->c->cl, chan));

    return 0;
}

static const PlugVtable agentfwd_plugvt = {
    fwd_log,
    fwd_closing,
    NULL, /* recv */
    NULL, /* send */
    agentfwd_accepting,
};

bool sesschan_enable_agent_forwarding(Channel *chan)
{
    sesschan *sess = container_of(chan, sesschan, chan);
    char *error, *socketname, *dir_prefix;

    dir_prefix = dupprintf("/tmp/%s-agentfwd", appname);

    sess->agentfwd_plug.vt = &agentfwd_plugvt;
    sess->agentfwd_socket = platform_make_agent_socket(
        &sess->agentfwd_plug, dir_prefix, &error, &socketname);

    sfree(dir_prefix);

    if (sess->agentfwd_socket) {
        conf_set_str_str(sess->conf, CONF_environmt,
                         "SSH_AUTH_SOCK", socketname);
    }

    sfree(error);
    sfree(socketname);

    return sess->agentfwd_socket != NULL;
}

bool sesschan_allocate_pty(
    Channel *chan, ptrlen termtype, unsigned width, unsigned height,
    unsigned pixwidth, unsigned pixheight, struct ssh_ttymodes modes)
{
    sesschan *sess = container_of(chan, sesschan, chan);
    char *s;

    if (sess->want_pty)
        return false;

    s = mkstr(termtype);
    conf_set_str(sess->conf, CONF_termtype, s);
    sfree(s);

    sess->want_pty = true;
    sess->ttymodes  = modes;
    sess->wc = width;
    sess->hc = height;
    sess->wp = pixwidth;
    sess->hp = pixheight;

    return true;
}

bool sesschan_set_env(Channel *chan, ptrlen var, ptrlen value)
{
    sesschan *sess = container_of(chan, sesschan, chan);

    char *svar = mkstr(var), *svalue = mkstr(value);
    conf_set_str_str(sess->conf, CONF_environmt, svar, svalue);
    sfree(svar);
    sfree(svalue);

    return true;
}

bool sesschan_send_break(Channel *chan, unsigned length)
{
    sesschan *sess = container_of(chan, sesschan, chan);

    if (sess->backend) {
        /* We ignore the break length. We could pass it through as the
         * 'arg' parameter, and have uxpty.c collect it and pass it on
         * to tcsendbreak, but since tcsendbreak in turn assigns
         * implementation-defined semantics to _its_ duration
         * parameter, this all just sounds too difficult. */
        backend_special(sess->backend, SS_BRK, 0);
        return true;
    }
    return false;
}

bool sesschan_send_signal(Channel *chan, ptrlen signame)
{
    sesschan *sess = container_of(chan, sesschan, chan);

    /* Start with a code that definitely isn't a signal (or indeed a
     * special command at all), to indicate 'nothing matched'. */
    SessionSpecialCode code = SS_EXITMENU;

    #define SIGNAL_SUB(name) \
        if (ptrlen_eq_string(signame, #name)) code = SS_SIG ## name;
    #define SIGNAL_MAIN(name, text) SIGNAL_SUB(name)
    #include "sshsignals.h"
    #undef SIGNAL_MAIN
    #undef SIGNAL_SUB

    if (code == SS_EXITMENU)
        return false;

    backend_special(sess->backend, code, 0);
    return true;
}

bool sesschan_change_window_size(
    Channel *chan, unsigned width, unsigned height,
    unsigned pixwidth, unsigned pixheight)
{
    sesschan *sess = container_of(chan, sesschan, chan);

    if (!sess->want_pty)
        return false;

    sess->wc = width;
    sess->hc = height;
    sess->wp = pixwidth;
    sess->hp = pixheight;

    if (sess->backend)
        backend_size(sess->backend, sess->wc, sess->hc);

    return true;
}

static size_t sesschan_seat_output(
    Seat *seat, bool is_stderr, const void *data, size_t len)
{
    sesschan *sess = container_of(seat, sesschan, seat);
    return sshfwd_write_ext(sess->c, is_stderr, data, len);
}

static void sesschan_check_close_callback(void *vctx)
{
    sesschan *sess = (sesschan *)vctx;

    /*
     * Once we've seen incoming EOF from the backend (aka EIO from the
     * pty master) and also passed on the process's exit status, we
     * should proactively initiate closure of the session channel.
     */
    if (sess->seen_eof && sess->seen_exit)
        sshfwd_initiate_close(sess->c, NULL);
}

static bool sesschan_want_close(Channel *chan, bool seen_eof, bool rcvd_eof)
{
    sesschan *sess = container_of(chan, sesschan, chan);

    /*
     * Similarly to above, we don't want to initiate channel closure
     * until we've sent the process's exit status, _even_ if EOF of
     * the actual data stream has happened in both directions.
     */
    return (sess->seen_eof && sess->seen_exit);
}

static bool sesschan_seat_eof(Seat *seat)
{
    sesschan *sess = container_of(seat, sesschan, seat);

    sshfwd_write_eof(sess->c);
    sess->seen_eof = true;

    queue_toplevel_callback(sesschan_check_close_callback, sess);
    return true;
}

static void sesschan_notify_remote_exit(Seat *seat)
{
    sesschan *sess = container_of(seat, sesschan, seat);
    ptrlen signame;
    char *sigmsg;

    if (!sess->backend)
        return;

    signame = pty_backend_exit_signame(sess->backend, &sigmsg);
    if (signame.len) {
        if (!sigmsg)
            sigmsg = dupstr("");

        sshfwd_send_exit_signal(
            sess->c, signame, false, ptrlen_from_asciz(sigmsg));

        sfree(sigmsg);
    } else {
        sshfwd_send_exit_status(sess->c, backend_exitcode(sess->backend));
    }

    sess->seen_exit = true;
    queue_toplevel_callback(sesschan_check_close_callback, sess);
}

static void sesschan_connection_fatal(Seat *seat, const char *message)
{
    sesschan *sess = container_of(seat, sesschan, seat);

    /* Closest translation I can think of */
    sshfwd_send_exit_signal(
        sess->c, PTRLEN_LITERAL("HUP"), false, ptrlen_from_asciz(message));

    sess->ignoring_input = true;
}

static bool sesschan_get_window_pixel_size(Seat *seat, int *width, int *height)
{
    sesschan *sess = container_of(seat, sesschan, seat);

    *width = sess->wp;
    *height = sess->hp;

    return true;
}

/* ----------------------------------------------------------------------
 * Built-in SFTP subsystem.
 */

static size_t sftp_chan_send(Channel *chan, bool is_stderr,
                             const void *data, size_t length)
{
    sesschan *sess = container_of(chan, sesschan, chan);

    bufchain_add(&sess->subsys_input, data, length);

    while (bufchain_size(&sess->subsys_input) >= 4) {
        char lenbuf[4];
        unsigned pktlen;
        struct sftp_packet *pkt, *reply;

        bufchain_fetch(&sess->subsys_input, lenbuf, 4);
        pktlen = GET_32BIT_MSB_FIRST(lenbuf);

        if (bufchain_size(&sess->subsys_input) - 4 < pktlen)
            break;                     /* wait for more data */

        bufchain_consume(&sess->subsys_input, 4);
        pkt = sftp_recv_prepare(pktlen);
        bufchain_fetch_consume(&sess->subsys_input, pkt->data, pktlen);
        sftp_recv_finish(pkt);
        reply = sftp_handle_request(sess->sftpsrv, pkt);
        sftp_pkt_free(pkt);

        sftp_send_prepare(reply);
        sshfwd_write(sess->c, reply->data, reply->length);
        sftp_pkt_free(reply);
    }

    return 0;
}

static void sftp_chan_send_eof(Channel *chan)
{
    sesschan *sess = container_of(chan, sesschan, chan);
    sshfwd_write_eof(sess->c);
}

static char *sftp_log_close_msg(Channel *chan)
{
    return dupstr("Session channel (SFTP) closed");
}

/* ----------------------------------------------------------------------
 * Built-in SCP subsystem.
 */

static size_t scp_chan_send(Channel *chan, bool is_stderr,
                            const void *data, size_t length)
{
    sesschan *sess = container_of(chan, sesschan, chan);
    return scp_send(sess->scpsrv, data, length);
}

static void scp_chan_send_eof(Channel *chan)
{
    sesschan *sess = container_of(chan, sesschan, chan);
    scp_eof(sess->scpsrv);
}

static char *scp_log_close_msg(Channel *chan)
{
    return dupstr("Session channel (SCP) closed");
}

static void scp_set_input_wanted(Channel *chan, bool wanted)
{
    sesschan *sess = container_of(chan, sesschan, chan);
    scp_throttle(sess->scpsrv, !wanted);
}
