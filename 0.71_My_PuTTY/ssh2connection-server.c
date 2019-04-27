/*
 * Server-specific parts of the SSH-2 connection layer.
 */

#include <assert.h>

#include "putty.h"
#include "ssh.h"
#include "sshbpp.h"
#include "sshppl.h"
#include "sshchan.h"
#include "sshcr.h"
#include "ssh2connection.h"
#include "sshserver.h"

void ssh2connection_server_configure(
    PacketProtocolLayer *ppl, const SftpServerVtable *sftpserver_vt)
{
    struct ssh2_connection_state *s =
        container_of(ppl, struct ssh2_connection_state, ppl);
    s->sftpserver_vt = sftpserver_vt;
}

static ChanopenResult chan_open_session(
    struct ssh2_connection_state *s, SshChannel *sc)
{
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */

    ppl_logevent("Opened session channel");
    CHANOPEN_RETURN_SUCCESS(sesschan_new(sc, s->ppl.logctx,
                                         s->sftpserver_vt));
}

static ChanopenResult chan_open_direct_tcpip(
    struct ssh2_connection_state *s, SshChannel *sc,
    ptrlen dstaddr, int dstport, ptrlen peeraddr, int peerport)
{
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    Channel *ch;
    char *dstaddr_str, *err;

    dstaddr_str = mkstr(dstaddr);

    ppl_logevent("Received request to connect to port %s:%d (from %.*s:%d)",
                 dstaddr_str, dstport, PTRLEN_PRINTF(peeraddr), peerport);
    err = portfwdmgr_connect(
        s->portfwdmgr, &ch, dstaddr_str, dstport, sc, ADDRTYPE_UNSPEC);

    sfree(dstaddr_str);

    if (err != NULL) {
        ppl_logevent("Port open failed: %s", err);
        sfree(err);
        CHANOPEN_RETURN_FAILURE(
            SSH2_OPEN_CONNECT_FAILED, ("Connection failed"));
    }

    ppl_logevent("Port opened successfully");
    CHANOPEN_RETURN_SUCCESS(ch);
}

ChanopenResult ssh2_connection_parse_channel_open(
    struct ssh2_connection_state *s, ptrlen type,
    PktIn *pktin, SshChannel *sc)
{
    if (ptrlen_eq_string(type, "session")) {
        return chan_open_session(s, sc);
    } else if (ptrlen_eq_string(type, "direct-tcpip")) {
        ptrlen dstaddr = get_string(pktin);
        int dstport = toint(get_uint32(pktin));
        ptrlen peeraddr = get_string(pktin);
        int peerport = toint(get_uint32(pktin));
        return chan_open_direct_tcpip(
            s, sc, dstaddr, dstport, peeraddr, peerport);
    } else {
        CHANOPEN_RETURN_FAILURE(
            SSH2_OPEN_UNKNOWN_CHANNEL_TYPE,
            ("Unsupported channel type requested"));
    }
}

bool ssh2_connection_parse_global_request(
    struct ssh2_connection_state *s, ptrlen type, PktIn *pktin)
{
    if (ptrlen_eq_string(type, "tcpip-forward")) {
        char *host = mkstr(get_string(pktin));
        unsigned port = get_uint32(pktin);
        /* In SSH-2, the host/port we listen on are the same host/port
         * we want reported back to us when a connection comes in,
         * because that's what we tell the client */
        bool toret = portfwdmgr_listen(
            s->portfwdmgr, host, port, host, port, s->conf);
        sfree(host);
        return toret;
    } else if (ptrlen_eq_string(type, "cancel-tcpip-forward")) {
        char *host = mkstr(get_string(pktin));
        unsigned port = get_uint32(pktin);
        bool toret = portfwdmgr_unlisten(s->portfwdmgr, host, port);
        sfree(host);
        return toret;
    } else {
        /* Unrecognised request. */
        return false;
    }
}

PktOut *ssh2_portfwd_chanopen(
    struct ssh2_connection_state *s, struct ssh2_channel *c,
    const char *hostname, int port,
    const char *description, const SocketPeerInfo *pi)
{
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    PktOut *pktout;

    /*
     * In server mode, this function is called by portfwdmgr in
     * response to PortListeners that were set up by calling
     * portfwdmgr_listen, which means that the hostname and port
     * parameters will identify the listening socket on which a
     * connection just came in.
     */

    if (pi && pi->log_text)
        ppl_logevent("Forwarding connection to listening port %s:%d from %s",
                     hostname, port, pi->log_text);
    else
        ppl_logevent("Forwarding connection to listening port %s:%d",
                     hostname, port);

    pktout = ssh2_chanopen_init(c, "forwarded-tcpip");
    put_stringz(pktout, hostname);
    put_uint32(pktout, port);
    put_stringz(pktout, (pi && pi->addr_text ? pi->addr_text : "0.0.0.0"));
    put_uint32(pktout, (pi && pi->port >= 0 ? pi->port : 0));

    return pktout;
}

struct ssh_rportfwd *ssh2_rportfwd_alloc(
    ConnectionLayer *cl,
    const char *shost, int sport, const char *dhost, int dport,
    int addressfamily, const char *log_description, PortFwdRecord *pfr,
    ssh_sharing_connstate *share_ctx)
{
    unreachable("Should never be called in the server");
}

void ssh2_rportfwd_remove(ConnectionLayer *cl, struct ssh_rportfwd *rpf)
{
    unreachable("Should never be called in the server");
}

SshChannel *ssh2_session_open(ConnectionLayer *cl, Channel *chan)
{
    unreachable("Should never be called in the server");
}

SshChannel *ssh2_serverside_x11_open(
    ConnectionLayer *cl, Channel *chan, const SocketPeerInfo *pi)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    struct ssh2_channel *c = snew(struct ssh2_channel);
    PktOut *pktout;

    c->connlayer = s;
    ssh2_channel_init(c);
    c->halfopen = true;
    c->chan = chan;

    ppl_logevent("Forwarding X11 channel to client");

    pktout = ssh2_chanopen_init(c, "x11");
    put_stringz(pktout, (pi && pi->addr_text ? pi->addr_text : "0.0.0.0"));
    put_uint32(pktout, (pi && pi->port >= 0 ? pi->port : 0));
    pq_push(s->ppl.out_pq, pktout);

    return &c->sc;
}

SshChannel *ssh2_serverside_agent_open(ConnectionLayer *cl, Channel *chan)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    struct ssh2_channel *c = snew(struct ssh2_channel);
    PktOut *pktout;

    c->connlayer = s;
    ssh2_channel_init(c);
    c->halfopen = true;
    c->chan = chan;

    ppl_logevent("Forwarding SSH agent to client");

    pktout = ssh2_chanopen_init(c, "auth-agent@openssh.com");
    pq_push(s->ppl.out_pq, pktout);

    return &c->sc;
}

void ssh2channel_start_shell(SshChannel *sc, bool want_reply)
{
    unreachable("Should never be called in the server");
}

void ssh2channel_start_command(
    SshChannel *sc, bool want_reply, const char *command)
{
    unreachable("Should never be called in the server");
}

bool ssh2channel_start_subsystem(
    SshChannel *sc, bool want_reply, const char *subsystem)
{
    unreachable("Should never be called in the server");
}

void ssh2channel_send_exit_status(SshChannel *sc, int status)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    PktOut *pktout = ssh2_chanreq_init(c, "exit-status", NULL, NULL);
    put_uint32(pktout, status);

    pq_push(s->ppl.out_pq, pktout);
}

void ssh2channel_send_exit_signal(
    SshChannel *sc, ptrlen signame, bool core_dumped, ptrlen msg)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    PktOut *pktout = ssh2_chanreq_init(c, "exit-signal", NULL, NULL);
    put_stringpl(pktout, signame);
    put_bool(pktout, core_dumped);
    put_stringpl(pktout, msg);
    put_stringz(pktout, "");           /* language tag */

    pq_push(s->ppl.out_pq, pktout);
}

void ssh2channel_send_exit_signal_numeric(
    SshChannel *sc, int signum, bool core_dumped, ptrlen msg)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    PktOut *pktout = ssh2_chanreq_init(c, "exit-signal", NULL, NULL);
    put_uint32(pktout, signum);
    put_bool(pktout, core_dumped);
    put_stringpl(pktout, msg);
    put_stringz(pktout, "");           /* language tag */

    pq_push(s->ppl.out_pq, pktout);
}

void ssh2channel_request_x11_forwarding(
    SshChannel *sc, bool want_reply, const char *authproto,
    const char *authdata, int screen_number, bool oneshot)
{
    unreachable("Should never be called in the server");
}

void ssh2channel_request_agent_forwarding(SshChannel *sc, bool want_reply)
{
    unreachable("Should never be called in the server");
}

void ssh2channel_request_pty(
    SshChannel *sc, bool want_reply, Conf *conf, int w, int h)
{
    unreachable("Should never be called in the server");
}

bool ssh2channel_send_env_var(
    SshChannel *sc, bool want_reply, const char *var, const char *value)
{
    unreachable("Should never be called in the server");
}

bool ssh2channel_send_serial_break(SshChannel *sc, bool want_reply, int length)
{
    unreachable("Should never be called in the server");
}

bool ssh2channel_send_signal(
    SshChannel *sc, bool want_reply, const char *signame)
{
    unreachable("Should never be called in the server");
}

void ssh2channel_send_terminal_size_change(SshChannel *sc, int w, int h)
{
    unreachable("Should never be called in the server");
}

bool ssh2_connection_need_antispoof_prompt(struct ssh2_connection_state *s)
{
    return false;
}
