/*
 * Server-specific parts of the SSH-1 connection layer.
 */

#include <assert.h>

#include "putty.h"
#include "ssh.h"
#include "sshbpp.h"
#include "sshppl.h"
#include "sshchan.h"
#include "sshcr.h"
#include "ssh1connection.h"
#include "sshserver.h"

static size_t ssh1sesschan_write(SshChannel *c, bool is_stderr,
                                 const void *, size_t);
static void ssh1sesschan_write_eof(SshChannel *c);
static void ssh1sesschan_initiate_close(SshChannel *c, const char *err);
static void ssh1sesschan_send_exit_status(SshChannel *c, int status);
static void ssh1sesschan_send_exit_signal(
    SshChannel *c, ptrlen signame, bool core_dumped, ptrlen msg);

static const struct SshChannelVtable ssh1sesschan_vtable = {
    ssh1sesschan_write,
    ssh1sesschan_write_eof,
    ssh1sesschan_initiate_close,
    NULL /* unthrottle */,
    NULL /* get_conf */,
    NULL /* window_override_removed is only used by SSH-2 sharing */,
    NULL /* x11_sharing_handover, likewise */,
    ssh1sesschan_send_exit_status,
    ssh1sesschan_send_exit_signal,
    NULL /* send_exit_signal_numeric */,
    NULL /* request_x11_forwarding */,
    NULL /* request_agent_forwarding */,
    NULL /* request_pty */,
    NULL /* send_env_var */,
    NULL /* start_shell */,
    NULL /* start_command */,
    NULL /* start_subsystem */,
    NULL /* send_serial_break */,
    NULL /* send_signal */,
    NULL /* send_terminal_size_change */,
    NULL /* hint_channel_is_simple */,
};

void ssh1_connection_direction_specific_setup(
    struct ssh1_connection_state *s)
{
    if (!s->mainchan_chan) {
        s->mainchan_sc.vt = &ssh1sesschan_vtable;
        s->mainchan_sc.cl = &s->cl;
        s->mainchan_chan = sesschan_new(&s->mainchan_sc, s->ppl.logctx, NULL);
    }
}

bool ssh1_handle_direction_specific_packet(
    struct ssh1_connection_state *s, PktIn *pktin)
{
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    PktOut *pktout;
    struct ssh1_channel *c;
    unsigned remid;
    ptrlen host, cmd, data;
    char *host_str, *err;
    int port, listenport;
    bool success;

    switch (pktin->type) {
      case SSH1_CMSG_EXEC_SHELL:
        if (s->finished_setup)
            goto unexpected_setup_packet;

        ppl_logevent("Client requested a shell");
        chan_run_shell(s->mainchan_chan);
        s->finished_setup = true;
        return true;

      case SSH1_CMSG_EXEC_CMD:
        if (s->finished_setup)
            goto unexpected_setup_packet;

        cmd = get_string(pktin);
        ppl_logevent("Client sent command '%.*s'", PTRLEN_PRINTF(cmd));
        chan_run_command(s->mainchan_chan, cmd);
        s->finished_setup = true;
        return true;

      case SSH1_CMSG_REQUEST_COMPRESSION:
        if (s->compressing) {
            pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH1_SMSG_FAILURE);
            pq_push(s->ppl.out_pq, pktout);
        } else {
            pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH1_SMSG_SUCCESS);
            pq_push(s->ppl.out_pq, pktout);
            /* Synchronous run of output formatting, to ensure that
             * success packet is converted into wire format before we
             * start compressing */
            ssh_bpp_handle_output(s->ppl.bpp);
            /* And now ensure that the _next_ packet will be the first
             * compressed one. */
            ssh1_bpp_start_compression(s->ppl.bpp);
            s->compressing = true;
        }

        return true;

      case SSH1_CMSG_REQUEST_PTY:
        if (s->finished_setup)
            goto unexpected_setup_packet;
        {
            ptrlen termtype = get_string(pktin);
            unsigned height = get_uint32(pktin);
            unsigned width = get_uint32(pktin);
            unsigned pixwidth = get_uint32(pktin);
            unsigned pixheight = get_uint32(pktin);
            struct ssh_ttymodes modes = read_ttymodes_from_packet(
                BinarySource_UPCAST(pktin), 1);

            if (get_err(pktin)) {
                ppl_logevent("Unable to decode pty request packet");
                success = false;
            } else if (!chan_allocate_pty(
                           s->mainchan_chan, termtype, width, height,
                           pixwidth, pixheight, modes)) {
                ppl_logevent("Unable to allocate a pty");
                success = false;
            } else {
                success = true;
            }
        }

        pktout = ssh_bpp_new_pktout(
            s->ppl.bpp, (success ? SSH1_SMSG_SUCCESS : SSH1_SMSG_FAILURE));
        pq_push(s->ppl.out_pq, pktout);
        return true;

      case SSH1_CMSG_PORT_FORWARD_REQUEST:
        if (s->finished_setup)
            goto unexpected_setup_packet;

        listenport = toint(get_uint32(pktin));
        host = get_string(pktin);
        port = toint(get_uint32(pktin));

        ppl_logevent("Client requested port %d forward to %.*s:%d",
                     listenport, PTRLEN_PRINTF(host), port);

        host_str = mkstr(host);
        success = portfwdmgr_listen(
            s->portfwdmgr, NULL, listenport, host_str, port, s->conf);
        sfree(host_str);

        pktout = ssh_bpp_new_pktout(
            s->ppl.bpp, (success ? SSH1_SMSG_SUCCESS : SSH1_SMSG_FAILURE));
        pq_push(s->ppl.out_pq, pktout);
        return true;

      case SSH1_CMSG_X11_REQUEST_FORWARDING:
        if (s->finished_setup)
            goto unexpected_setup_packet;

        {
            ptrlen authproto = get_string(pktin);
            ptrlen authdata = get_string(pktin);
            unsigned screen_number = 0;
            if (s->remote_protoflags & SSH1_PROTOFLAG_SCREEN_NUMBER)
                screen_number = get_uint32(pktin);

            success = chan_enable_x11_forwarding(
                s->mainchan_chan, false, authproto, authdata, screen_number);
        }

        pktout = ssh_bpp_new_pktout(
            s->ppl.bpp, (success ? SSH1_SMSG_SUCCESS : SSH1_SMSG_FAILURE));
        pq_push(s->ppl.out_pq, pktout);
        return true;

      case SSH1_CMSG_AGENT_REQUEST_FORWARDING:
        if (s->finished_setup)
            goto unexpected_setup_packet;

        success = chan_enable_agent_forwarding(s->mainchan_chan);

        pktout = ssh_bpp_new_pktout(
            s->ppl.bpp, (success ? SSH1_SMSG_SUCCESS : SSH1_SMSG_FAILURE));
        pq_push(s->ppl.out_pq, pktout);
        return true;

      case SSH1_CMSG_STDIN_DATA:
        data = get_string(pktin);
        chan_send(s->mainchan_chan, false, data.ptr, data.len);
        return true;

      case SSH1_CMSG_EOF:
        chan_send_eof(s->mainchan_chan);
        return true;

      case SSH1_CMSG_WINDOW_SIZE:
        return true;

      case SSH1_MSG_PORT_OPEN:
        remid = get_uint32(pktin);
        host = get_string(pktin);
        port = toint(get_uint32(pktin));

        host_str = mkstr(host);

        ppl_logevent("Received request to connect to port %s:%d",
                     host_str, port);
        c = snew(struct ssh1_channel);
        c->connlayer = s;
        err = portfwdmgr_connect(
            s->portfwdmgr, &c->chan, host_str, port,
            &c->sc, ADDRTYPE_UNSPEC);

        sfree(host_str);

        if (err) {
            ppl_logevent("Port open failed: %s", err);
            sfree(err);
            ssh1_channel_free(c);
            pktout = ssh_bpp_new_pktout(
                s->ppl.bpp, SSH1_MSG_CHANNEL_OPEN_FAILURE);
            put_uint32(pktout, remid);
            pq_push(s->ppl.out_pq, pktout);
        } else {
            ssh1_channel_init(c);
            c->remoteid = remid;
            c->halfopen = false;
            pktout = ssh_bpp_new_pktout(
                s->ppl.bpp, SSH1_MSG_CHANNEL_OPEN_CONFIRMATION);
            put_uint32(pktout, c->remoteid);
            put_uint32(pktout, c->localid);
            pq_push(s->ppl.out_pq, pktout);
            ppl_logevent("Forwarded port opened successfully");
        }

        return true;

      case SSH1_CMSG_EXIT_CONFIRMATION:
        if (!s->sent_exit_status) {
            ssh_proto_error(s->ppl.ssh, "Received SSH1_CMSG_EXIT_CONFIRMATION"
                            " without having sent SSH1_SMSG_EXIT_STATUS");
            return true;
        }
        ppl_logevent("Client sent exit confirmation");
        return true;

      default:
        return false;
    }

  unexpected_setup_packet:
    ssh_proto_error(s->ppl.ssh, "Received unexpected setup packet after the "
                    "setup phase, type %d (%s)", pktin->type,
                    ssh1_pkt_type(pktin->type));
    /* FIXME: ensure caller copes with us just having freed the whole layer */
    return true;
}

SshChannel *ssh1_session_open(ConnectionLayer *cl, Channel *chan)
{
    unreachable("Should never be called in the server");
}

struct ssh_rportfwd *ssh1_rportfwd_alloc(
    ConnectionLayer *cl,
    const char *shost, int sport, const char *dhost, int dport,
    int addressfamily, const char *log_description, PortFwdRecord *pfr,
    ssh_sharing_connstate *share_ctx)
{
    unreachable("Should never be called in the server");
}

static size_t ssh1sesschan_write(SshChannel *sc, bool is_stderr,
                                 const void *data, size_t len)
{
    struct ssh1_connection_state *s =
        container_of(sc, struct ssh1_connection_state, mainchan_sc);
    PktOut *pktout;

    pktout = ssh_bpp_new_pktout(
        s->ppl.bpp,
        (is_stderr ? SSH1_SMSG_STDERR_DATA : SSH1_SMSG_STDOUT_DATA));
    put_string(pktout, data, len);
    pq_push(s->ppl.out_pq, pktout);

    return 0;
}

static void ssh1sesschan_write_eof(SshChannel *sc)
{
    /* SSH-1 can't represent server-side EOF */
    /* FIXME: some kind of check-termination system, whereby once this has been called _and_ we've had an exit status _and_ we've got no other channels open, we send the actual EXIT_STATUS message */
}

static void ssh1sesschan_initiate_close(SshChannel *sc, const char *err)
{
    /* SSH-1 relies on the client to close the connection in the end */
}

static void ssh1sesschan_send_exit_status(SshChannel *sc, int status)
{
    struct ssh1_connection_state *s =
        container_of(sc, struct ssh1_connection_state, mainchan_sc);
    PktOut *pktout;

    pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH1_SMSG_EXIT_STATUS);
    put_uint32(pktout, status);
    pq_push(s->ppl.out_pq, pktout);

    s->sent_exit_status = true;
}

static void ssh1sesschan_send_exit_signal(
    SshChannel *sc, ptrlen signame, bool core_dumped, ptrlen msg)
{
    /* SSH-1 has no separate representation for signals */
    ssh1sesschan_send_exit_status(sc, 128);
}

SshChannel *ssh1_serverside_x11_open(
    ConnectionLayer *cl, Channel *chan, const SocketPeerInfo *pi)
{
    struct ssh1_connection_state *s =
        container_of(cl, struct ssh1_connection_state, cl);
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    struct ssh1_channel *c = snew(struct ssh1_channel);
    PktOut *pktout;

    c->connlayer = s;
    ssh1_channel_init(c);
    c->halfopen = true;
    c->chan = chan;

    ppl_logevent("Forwarding X11 connection to client");

    pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH1_SMSG_X11_OPEN);
    put_uint32(pktout, c->localid);
    pq_push(s->ppl.out_pq, pktout);

    return &c->sc;
}

SshChannel *ssh1_serverside_agent_open(ConnectionLayer *cl, Channel *chan)
{
    struct ssh1_connection_state *s =
        container_of(cl, struct ssh1_connection_state, cl);
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    struct ssh1_channel *c = snew(struct ssh1_channel);
    PktOut *pktout;

    c->connlayer = s;
    ssh1_channel_init(c);
    c->halfopen = true;
    c->chan = chan;

    ppl_logevent("Forwarding agent connection to client");

    pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH1_SMSG_AGENT_OPEN);
    put_uint32(pktout, c->localid);
    pq_push(s->ppl.out_pq, pktout);

    return &c->sc;
}
