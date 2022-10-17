/*
 * Platform-independent parts of a standalone SOCKS server program
 * based on the PuTTY SOCKS code.
 */

#include <string.h>
#include <errno.h>

#include "putty.h"
#include "misc.h"
#include "ssh.h"
#include "ssh/channel.h"
#include "psocks.h"

/*
 * Possible later TODOs:
 *
 *  - verbosity setting for log messages
 *
 *  - could import proxy.c and use name_lookup rather than
 *    sk_namelookup, to allow forwarding via some other proxy type
 */

#define BUFLIMIT 16384

#define LOGBITS(X)                              \
    X(CONNSTATUS)                               \
    X(DIALOGUE)                                 \
    /* end of list */

#define BITINDEX_ENUM(x) LOG_##x##_bitindex,
enum { LOGBITS(BITINDEX_ENUM) };
#define BITFLAG_ENUM(x) LOG_##x = 1 << LOG_##x##_bitindex,
enum { LOGBITS(BITFLAG_ENUM) };

typedef struct psocks_connection psocks_connection;

typedef enum RecordDestination {
    REC_NONE, REC_FILE, REC_PIPE
} RecordDestination;

struct psocks_state {
    const PsocksPlatform *platform;
    int listen_port;
    bool acceptall;
    PortFwdManager *portfwdmgr;
    uint64_t next_conn_index;
    FILE *logging_fp;
    unsigned log_flags;
    RecordDestination rec_dest;
    char *rec_cmd;
    strbuf *subcmd;

    ConnectionLayer cl;
};

struct psocks_connection {
    psocks_state *ps;
    Channel *chan;
    char *host, *realhost;
    int port;
    SockAddr *addr;
    Socket *socket;
    bool connecting, eof_pfmgr_to_socket, eof_socket_to_pfmgr;
    uint64_t index;
    PsocksDataSink *rec_sink;

    Plug plug;
    SshChannel sc;
};

static SshChannel *psocks_lportfwd_open(
    ConnectionLayer *cl, const char *hostname, int port,
    const char *description, const SocketPeerInfo *pi, Channel *chan);

static const ConnectionLayerVtable psocks_clvt = {
    .lportfwd_open = psocks_lportfwd_open,
    /* everything else is NULL */
};

static size_t psocks_sc_write(SshChannel *sc, bool is_stderr, const void *,
                              size_t);
static void psocks_sc_write_eof(SshChannel *sc);
static void psocks_sc_initiate_close(SshChannel *sc, const char *err);
static void psocks_sc_unthrottle(SshChannel *sc, size_t bufsize);

static const SshChannelVtable psocks_scvt = {
    .write = psocks_sc_write,
    .write_eof = psocks_sc_write_eof,
    .initiate_close = psocks_sc_initiate_close,
    .unthrottle = psocks_sc_unthrottle,
    /* all the rest are NULL */
};

static void psocks_plug_log(Plug *p, PlugLogType type, SockAddr *addr,
                            int port, const char *error_msg, int error_code);
static void psocks_plug_closing(Plug *p, const char *error_msg,
                                int error_code, bool calling_back);
static void psocks_plug_receive(Plug *p, int urgent,
                                const char *data, size_t len);
static void psocks_plug_sent(Plug *p, size_t bufsize);

static const PlugVtable psocks_plugvt = {
    .log = psocks_plug_log,
    .closing = psocks_plug_closing,
    .receive = psocks_plug_receive,
    .sent = psocks_plug_sent,
};

static void psocks_conn_log(psocks_connection *conn, const char *fmt, ...)
{
    if (!conn->ps->logging_fp)
        return;

    va_list ap;
    va_start(ap, fmt);
    char *msg = dupvprintf(fmt, ap);
    va_end(ap);
    fprintf(conn->ps->logging_fp, "c#%"PRIu64": %s\n", conn->index, msg);
    sfree(msg);
    fflush(conn->ps->logging_fp);
}

static void psocks_conn_log_data(psocks_connection *conn, PsocksDirection dir,
                                 const void *vdata, size_t len)
{
    if ((conn->ps->log_flags & LOG_DIALOGUE) && conn->ps->logging_fp) {
        const char *data = vdata;
        while (len > 0) {
            const char *nl = memchr(data, '\n', len);
            size_t thislen = nl ? (nl+1) - data : len;
            const char *thisdata = data;
            data += thislen;
            len -= thislen;

            static const char *const direction_names[2] = {
                [UP] = "send", [DN] = "recv" };

            fprintf(conn->ps->logging_fp, "c#%"PRIu64": %s \"", conn->index,
                    direction_names[dir]);
            write_c_string_literal(conn->ps->logging_fp,
                                   make_ptrlen(thisdata, thislen));
            fprintf(conn->ps->logging_fp, "\"\n");
        }

        fflush(conn->ps->logging_fp);
    }

    if (conn->rec_sink)
        put_data(conn->rec_sink->s[dir], vdata, len);
}

static void psocks_connection_establish(void *vctx);

static SshChannel *psocks_lportfwd_open(
    ConnectionLayer *cl, const char *hostname, int port,
    const char *description, const SocketPeerInfo *pi, Channel *chan)
{
    psocks_state *ps = container_of(cl, psocks_state, cl);
    psocks_connection *conn = snew(psocks_connection);
    memset(conn, 0, sizeof(*conn));
    conn->ps = ps;
    conn->sc.vt = &psocks_scvt;
    conn->plug.vt = &psocks_plugvt;
    conn->chan = chan;
    conn->host = dupstr(hostname);
    conn->port = port;
    conn->index = ps->next_conn_index++;
    if (conn->ps->log_flags & LOG_CONNSTATUS)
        psocks_conn_log(conn, "request from %s for %s port %d",
                        pi->log_text, hostname, port);
    switch (conn->ps->rec_dest) {
      case REC_FILE:
        {
            char *fnames[2];
            FILE *fp[2];
            bool ok = true;

            static const char *const direction_names[2] = {
                [UP] = "sockout", [DN] = "sockin" };

            for (size_t i = 0; i < 2; i++) {
                fnames[i] = dupprintf("%s.%"PRIu64, direction_names[i],
                                      conn->index);
                fp[i] = fopen(fnames[i], "wb");
                if (!fp[i]) {
                    psocks_conn_log(conn, "cannot log this connection: "
                                    "creating file '%s': %s",
                                    fnames[i], strerror(errno));
                    ok = false;
                }
            }
            if (ok) {
                if (conn->ps->log_flags & LOG_CONNSTATUS)
                    psocks_conn_log(conn, "logging to '%s' / '%s'",
                                    fnames[0], fnames[1]);
                conn->rec_sink = pds_stdio(fp);
            } else {
                for (size_t i = 0; i < 2; i++) {
                    if (fp[i]) {
                        remove(fnames[i]);
                        fclose(fp[i]);
                    }
                }
            }
            for (size_t i = 0; i < 2; i++)
                sfree(fnames[i]);
        }
        break;
      case REC_PIPE:
        {
            static const char *const direction_args[2] = {
                [UP] = "out", [DN] = "in" };
            char *index_arg = dupprintf("%"PRIu64, conn->index);
            char *err;
            conn->rec_sink = conn->ps->platform->open_pipes(
                conn->ps->rec_cmd, direction_args, index_arg, &err);
            if (!conn->rec_sink) {
                psocks_conn_log(conn, "cannot log this connection: "
                                "creating pipes: %s", err);
                sfree(err);
            }
            sfree(index_arg);
        }
        break;
      default:
        break;
    }
    queue_toplevel_callback(psocks_connection_establish, conn);
    return &conn->sc;
}

static void psocks_conn_free(psocks_connection *conn)
{
    if (conn->ps->log_flags & LOG_CONNSTATUS)
        psocks_conn_log(conn, "closed");

    sfree(conn->host);
    sfree(conn->realhost);
    if (conn->socket)
        sk_close(conn->socket);
    if (conn->chan)
        chan_free(conn->chan);
    if (conn->rec_sink)
        pds_free(conn->rec_sink);
    delete_callbacks_for_context(conn);
    sfree(conn);
}

static void psocks_connection_establish(void *vctx)
{
    psocks_connection *conn = (psocks_connection *)vctx;

    /*
     * Look up destination host name.
     */
    conn->addr = sk_namelookup(conn->host, &conn->realhost, ADDRTYPE_UNSPEC);

    const char *err = sk_addr_error(conn->addr);
    if (err) {
        char *msg = dupprintf("name lookup failed: %s", err);
        chan_open_failed(conn->chan, msg);
        sfree(msg);

        psocks_conn_free(conn);
        return;
    }

    /*
     * Make the connection.
     */
    conn->connecting = true;
    conn->socket = sk_new(conn->addr, conn->port, false, false, false, false,
                          &conn->plug);
}

static size_t psocks_sc_write(SshChannel *sc, bool is_stderr,
                              const void *data, size_t len)
{
    psocks_connection *conn = container_of(sc, psocks_connection, sc);
    if (!conn->socket) return 0;

    psocks_conn_log_data(conn, UP, data, len);

    return sk_write(conn->socket, data, len);
}

static void psocks_check_close(void *vctx)
{
    psocks_connection *conn = (psocks_connection *)vctx;
    if (chan_want_close(conn->chan, conn->eof_pfmgr_to_socket,
                        conn->eof_socket_to_pfmgr))
        psocks_conn_free(conn);
}

static void psocks_sc_write_eof(SshChannel *sc)
{
    psocks_connection *conn = container_of(sc, psocks_connection, sc);
    if (!conn->socket) return;
    sk_write_eof(conn->socket);
    conn->eof_pfmgr_to_socket = true;

    if (conn->ps->log_flags & LOG_DIALOGUE)
        psocks_conn_log(conn, "send eof");

    queue_toplevel_callback(psocks_check_close, conn);
}

static void psocks_sc_initiate_close(SshChannel *sc, const char *err)
{
    psocks_connection *conn = container_of(sc, psocks_connection, sc);
    sk_close(conn->socket);
    conn->socket = NULL;
}

static void psocks_sc_unthrottle(SshChannel *sc, size_t bufsize)
{
    psocks_connection *conn = container_of(sc, psocks_connection, sc);
    if (bufsize < BUFLIMIT)
	sk_set_frozen(conn->socket, false);
}

static void psocks_plug_log(Plug *plug, PlugLogType type, SockAddr *addr,
                            int port, const char *error_msg, int error_code)
{
    psocks_connection *conn = container_of(plug, psocks_connection, plug);
    char addrbuf[256];

    if (!(conn->ps->log_flags & LOG_CONNSTATUS))
        return;

    switch (type) {
      case PLUGLOG_CONNECT_TRYING:
        sk_getaddr(addr, addrbuf, sizeof(addrbuf));
        if (sk_addr_needs_port(addr))
            psocks_conn_log(conn, "trying to connect to %s port %d",
                            addrbuf, port);
        else
            psocks_conn_log(conn, "trying to connect to %s", addrbuf);
        break;
      case PLUGLOG_CONNECT_FAILED:
        psocks_conn_log(conn, "connection attempt failed: %s", error_msg);
        break;
      case PLUGLOG_CONNECT_SUCCESS:
        psocks_conn_log(conn, "connection established", error_msg);
        if (conn->connecting) {
            chan_open_confirmation(conn->chan);
            conn->connecting = false;
        }
        break;
      case PLUGLOG_PROXY_MSG:
        psocks_conn_log(conn, "connection setup: %s", error_msg);
        break;
    };
}

static void psocks_plug_closing(Plug *plug, const char *error_msg,
                                int error_code, bool calling_back)
{
    psocks_connection *conn = container_of(plug, psocks_connection, plug);
    if (conn->connecting) {
        if (conn->ps->log_flags & LOG_CONNSTATUS)
            psocks_conn_log(conn, "unable to connect: %s", error_msg);

        chan_open_failed(conn->chan, error_msg);
        conn->eof_socket_to_pfmgr = true;
        conn->eof_pfmgr_to_socket = true;
        conn->connecting = false;
    } else {
        if (conn->ps->log_flags & LOG_DIALOGUE)
            psocks_conn_log(conn, "recv eof");

        chan_send_eof(conn->chan);
        conn->eof_socket_to_pfmgr = true;
    }
    queue_toplevel_callback(psocks_check_close, conn);
}

static void psocks_plug_receive(Plug *plug, int urgent,
                                const char *data, size_t len)
{
    psocks_connection *conn = container_of(plug, psocks_connection, plug);
    size_t bufsize = chan_send(conn->chan, false, data, len);
    sk_set_frozen(conn->socket, bufsize > BUFLIMIT);

    psocks_conn_log_data(conn, DN, data, len);
}

static void psocks_plug_sent(Plug *plug, size_t bufsize)
{
    psocks_connection *conn = container_of(plug, psocks_connection, plug);
    sk_set_frozen(conn->socket, bufsize > BUFLIMIT);
}

psocks_state *psocks_new(const PsocksPlatform *platform)
{
    psocks_state *ps = snew(psocks_state);
    memset(ps, 0, sizeof(*ps));

    ps->listen_port = 1080;
    ps->acceptall = false;

    ps->cl.vt = &psocks_clvt;
    ps->portfwdmgr = portfwdmgr_new(&ps->cl);

    ps->logging_fp = stderr; /* could make this configurable later */
    ps->log_flags = LOG_CONNSTATUS;
    ps->rec_dest = REC_NONE;
    ps->platform = platform;
    ps->subcmd = strbuf_new();

    return ps;
}

void psocks_free(psocks_state *ps)
{
    portfwdmgr_free(ps->portfwdmgr);
    strbuf_free(ps->subcmd);
    sfree(ps->rec_cmd);
    sfree(ps);
}

void psocks_cmdline(psocks_state *ps, int argc, char **argv)
{
    bool doing_opts = true;
    bool accumulating_exec_args = false;
    size_t args_seen = 0;

    while (--argc > 0) {
	const char *p = *++argv;

	if (doing_opts && p[0] == '-' && p[1]) {
            if (!strcmp(p, "--")) {
                doing_opts = false;
            } else if (!strcmp(p, "-g")) {
		ps->acceptall = true;
            } else if (!strcmp(p, "-d")) {
		ps->log_flags |= LOG_DIALOGUE;
            } else if (!strcmp(p, "-f")) {
		ps->rec_dest = REC_FILE;
            } else if (!strcmp(p, "-p")) {
                if (!ps->platform->open_pipes) {
		    fprintf(stderr, "psocks: '-p' is not supported on this "
                            "platform\n");
		    exit(1);
                }
		if (--argc > 0) {
		    ps->rec_cmd = dupstr(*++argv);
		} else {
		    fprintf(stderr, "psocks: expected an argument to '-p'\n");
		    exit(1);
		}
		ps->rec_dest = REC_PIPE;
	    } else if (!strcmp(p, "--exec")) {
                if (!ps->platform->start_subcommand) {
		    fprintf(stderr, "psocks: running a subcommand is not "
                            "supported on this platform\n");
		    exit(1);
                }
                accumulating_exec_args = true;
                /* Now consume all further argv words for the
                 * subcommand, even if they look like options */
                doing_opts = false;
	    } else if (!strcmp(p, "--help")) {
                printf("usage: psocks [ -d ] [ -f");
                if (ps->platform->open_pipes)
                    printf(" | -p pipe-cmd");
                printf(" ] [ -g ] port-number");
                printf("\n");
                printf("where: -d           log all connection contents to"
                       " standard output\n");
                printf("       -f           record each half-connection to "
                       "a file sockin.N/sockout.N\n");
                if (ps->platform->open_pipes)
                    printf("       -p pipe-cmd  pipe each half-connection"
                           " to 'pipe-cmd [in|out] N'\n");
                printf("       -g           accept connections from anywhere,"
                       " not just localhost\n");
                if (ps->platform->start_subcommand)
                    printf("       --exec subcmd [args...]   run command, and "
                           "terminate when it exits\n");
                printf("       port-number  listen on this port"
                       " (default 1080)\n");
                printf("also: psocks --help      display this help text\n");
                exit(0);
            } else {
                fprintf(stderr, "psocks: unrecognised option '%s'\n", p);
                exit(1);
            }
	} else {
            if (accumulating_exec_args) {
                put_asciz(ps->subcmd, p);
            } else switch (args_seen++) {
              case 0:
                ps->listen_port = atoi(p);
                break;
              default:
                fprintf(stderr, "psocks: unexpected extra argument '%s'\n", p);
                exit(1);
                break;
            }
	}
    }
}

void psocks_start(psocks_state *ps)
{
    Conf *conf = conf_new();
    conf_set_bool(conf, CONF_lport_acceptall, ps->acceptall);
    char *key = dupprintf("AL%d", ps->listen_port);
    conf_set_str_str(conf, CONF_portfwd, key, "D");
    sfree(key);

    portfwdmgr_config(ps->portfwdmgr, conf);

    if (ps->subcmd->len)
        ps->platform->start_subcommand(ps->subcmd);

    conf_free(conf);
}

/*
 * Some stubs that are needed to link against PuTTY modules.
 */

int verify_host_key(const char *hostname, int port,
                    const char *keytype, const char *key)
{
    unreachable("host keys not handled in this tool");
}

void store_host_key(const char *hostname, int port,
                    const char *keytype, const char *key)
{
    unreachable("host keys not handled in this tool");
}

/*
 * stdio-targeted PsocksDataSink.
 */
typedef struct PsocksDataSinkStdio {
    stdio_sink sink[2];
    PsocksDataSink pds;
} PsocksDataSinkStdio;

static void stdio_free(PsocksDataSink *pds)
{
    PsocksDataSinkStdio *pdss = container_of(pds, PsocksDataSinkStdio, pds);

    for (size_t i = 0; i < 2; i++)
        fclose(pdss->sink[i].fp);

    sfree(pdss);
}

PsocksDataSink *pds_stdio(FILE *fp[2])
{
    PsocksDataSinkStdio *pdss = snew(PsocksDataSinkStdio);

    for (size_t i = 0; i < 2; i++) {
        setvbuf(fp[i], NULL, _IONBF, 0);
        stdio_sink_init(&pdss->sink[i], fp[i]);
        pdss->pds.s[i] = BinarySink_UPCAST(&pdss->sink[i]);
    }

    pdss->pds.free = stdio_free;

    return &pdss->pds;
}
