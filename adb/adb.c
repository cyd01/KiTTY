/*
 * "Adb" backend.
 */
#ifdef ADBPORT

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define ADB_MAX_BACKLOG 4096

typedef enum {
    STATE_WARMING_UP,
    STATE_SENT_HELLO,
    STATE_ASKED_FOR_SHELL,
    STATE_CONNECTED,
    STATE_WAITING_FOR_ERROR_MESSAGE,
} adb_state;

typedef struct adb_backend_data {
    const struct plug_function_table *fn;
    /* the above field _must_ be first in the structure */

    Socket s;
    int bufsize;
    adb_state state;
    void *frontend;
} *Adb;

static void adb_size(void *handle, int width, int height);

static void c_write(Adb adb, char *buf, int len)
{
    int backlog = from_backend(adb->frontend, 0, buf, len);
    sk_set_frozen(adb->s, backlog > ADB_MAX_BACKLOG);
}

static void adb_log(Plug plug, int type, SockAddr addr, int port,
                    const char *error_msg, int error_code)
{
    Adb adb = (Adb) plug;
    char addrbuf[256], *msg;

    sk_getaddr(addr, addrbuf, lenof(addrbuf));

    if (type == 0)
        msg = dupprintf("Connecting to %s port %d", addrbuf, port);
    else
        msg = dupprintf("Failed to connect to %s: %s", addrbuf, error_msg);

    logevent(adb->frontend, msg);
}

static int adb_closing(Plug plug, const char *error_msg, int error_code,
                       int calling_back)
{
    Adb adb = (Adb) plug;

    if (adb->s) {
        sk_close(adb->s);
        adb->s = NULL;
        notify_remote_exit(adb->frontend);
    }
    if (error_msg) {
        /* A socket error has occurred. */
        logevent(adb->frontend, error_msg);
        connection_fatal(adb->frontend, "%s", error_msg);
    } /* Otherwise, the remote side closed the connection normally. */
    return 0;
}

static void do_fatal(Adb adb, char *data, int len) {
    char* d = (char*)smalloc(len+1);
    memcpy(d, data, len);
    d[len] = '\0';
    connection_fatal(adb->frontend, "adb failure message: '%s'", d);
    sfree(d);
}

/** the error might not be available when the error occurs; wait
  * a bit for more data to show up then assume that's the error message.
  */
static void handle_fail(Adb adb, char *data, int len) {
    // FAIL0003abc
    char message_length_hex[5];
    unsigned long expected;
    memcpy(message_length_hex, data+4, 4);
    message_length_hex[4] = 0;
    expected = strtoul(message_length_hex, NULL, 16);

    if (len == expected + 8)
        do_fatal(adb, data+8, expected);
    else
        adb->state = STATE_WAITING_FOR_ERROR_MESSAGE;
}

static int adb_receive(Plug plug, int urgent, char *data, int len)
{
    Adb adb = (Adb) plug;
    if (adb->state == STATE_SENT_HELLO) {
        if (data[0]=='O') { // OKAY
            sk_write(adb->s,"0006shell:",10);
            adb->state = STATE_ASKED_FOR_SHELL; // wait for shell start response
        } else {
             if (data[0]=='F') {
                handle_fail(adb, data, len);
            } else {
                connection_fatal(adb->frontend, "Bad response after initial send");
            }
            return 0;
        }
    } else if (adb->state == STATE_ASKED_FOR_SHELL) {
        if (data[0]=='O') { //OKAY
            adb->state = STATE_CONNECTED; // shell started, switch to terminal mode
        } else {
            if (data[0]=='F') {
                handle_fail(adb, data, len);
            } else {
                connection_fatal(adb->frontend, "Bad response waiting for shell start");
            }
            return 0;
        }
    } else if (adb->state == STATE_WAITING_FOR_ERROR_MESSAGE) {
        do_fatal(adb, data, len);
    } else {
        c_write(adb, data, len);
    }
    return 1;
}

static void adb_sent(Plug plug, int bufsize)
{
    Adb adb = (Adb) plug;
    adb->bufsize = bufsize;
}

/*
 * Called to set up the adb connection.
 * 
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'. It must be
 * freed by the caller.
 */
static const char *adb_init(void *frontend_handle, void **backend_handle,
                            Conf *conf,
                            const char *host, int port, char **realhost, int nodelay,
                            int keepalive)
{
    static const struct plug_function_table fn_table = {
        adb_log,
        adb_closing,
        adb_receive,
        adb_sent,
        NULL
    };
    SockAddr addr;
    const char *err;
    Adb adb;

    adb = snew(struct adb_backend_data);
    adb->fn = &fn_table;
    adb->s = NULL;
    adb->state = STATE_WARMING_UP;
    *backend_handle = adb;

    adb->frontend = frontend_handle;

    /*
     * Try to find host.
     */
    {
        char *buf;
        buf = dupprintf("Looking up host \"%s\"%s", "localhost",
                (conf_get_int(conf, CONF_addressfamily) == ADDRTYPE_IPV4 ? " (IPv4)" :
                 (conf_get_int(conf, CONF_addressfamily) == ADDRTYPE_IPV6 ? " (IPv6)" :
                  "")));
        logevent(adb->frontend, buf);
        sfree(buf);
    }
    addr = name_lookup("localhost", port, realhost, conf, conf_get_int(conf, CONF_addressfamily), frontend_handle , "ADB connection");
    if ((err = sk_addr_error(addr)) != NULL) {
        sk_addr_free(addr);
        return err;
    }

    if (port < 0)
        port = 5037; /* default adb port */

    /*
     * Open socket.
     */
    adb->s = new_connection(addr, *realhost, port, 0, 1, nodelay, keepalive,
                            (Plug) adb, conf);
    if ((err = sk_socket_error(adb->s)) != NULL)
        return err;
    if (*conf_get_str(conf, CONF_loghost)) {
        char *colon;

        sfree(*realhost);
        *realhost = conf_get_str(conf, CONF_loghost);
        colon = strrchr(*realhost, ':');
        if (colon) {
            /*
             * FIXME: if we ever update this aspect of ssh.c for
             * IPv6 literal management, this should change in line
             * with it.
             */
            *colon++ = '\0';
        }
    }

    /* send initial data to adb server */
#define ADB_SHELL_DEFAULT_STR "0012" "host:transport-any"
#define ADB_SHELL_DEFAULT_STR_LEN (sizeof(ADB_SHELL_DEFAULT_STR)-1)
#define ADB_SHELL_USB_STR "0012" "host:transport-usb"
#define ADB_SHELL_USB_STR_LEN (sizeof(ADB_SHELL_USB_STR)-1)
#define ADB_SHELL_LOCAL_STR "0015" "host:transport-local"
#define ADB_SHELL_LOCAL_STR_LEN (sizeof(ADB_SHELL_LOCAL_STR)-1)
#define ADB_SHELL_SERIAL_PREFIX "host:transport:"
#define ADB_SHELL_SERIAL_PREFIX_LEN (sizeof(ADB_SHELL_SERIAL_PREFIX)-1)

#   define write_hello(str, len) \
        sk_write(adb->s, str, len); \
        sk_flush(adb->s); \
        adb->state = STATE_SENT_HELLO;

    do {
        size_t len;
        if (host[0] == ':')
            ++host;

        len = strlen(host);

        if (len == 0 || !strcmp("-a", host) || !strcmp(host, "transport-any")) {
            write_hello(ADB_SHELL_DEFAULT_STR, ADB_SHELL_DEFAULT_STR_LEN);
        } else if (!strcmp("-d", host) || !strcmp(host, "transport-usb")) {
            write_hello(ADB_SHELL_USB_STR, ADB_SHELL_USB_STR_LEN);
        } else if (!strcmp("-e", host) || !strcmp(host, "transport-local")) {
            write_hello(ADB_SHELL_LOCAL_STR, ADB_SHELL_LOCAL_STR_LEN);
        } else {
            char sendbuf[512];
#           define ADB_SHELL_HOST_MAX_LEN (sizeof(sendbuf)-4-ADB_SHELL_SERIAL_PREFIX_LEN)
            if (len > ADB_SHELL_HOST_MAX_LEN)
                len = ADB_SHELL_HOST_MAX_LEN;
            sprintf(sendbuf,"%04lx" ADB_SHELL_SERIAL_PREFIX, (unsigned long)(len+ADB_SHELL_SERIAL_PREFIX_LEN));
            memcpy(sendbuf+4+ADB_SHELL_SERIAL_PREFIX_LEN, host, len);
            write_hello(sendbuf, len+4+ADB_SHELL_SERIAL_PREFIX_LEN);
        }
    } while (0);
    return NULL;
}

static void adb_free(void *handle)
{
    Adb adb = (Adb) handle;

    if (adb->s)
        sk_close(adb->s);
    sfree(adb);
}

/*
 * Stub routine (we don't have any need to reconfigure this backend).
 */
static void adb_reconfig(void *handle, Conf *conf)
{
}

/*
 * Called to send data down the adb connection.
 */
static int adb_send(void *handle, const char *buf, int len)
{
    Adb adb = (Adb) handle;

    if (adb->s == NULL)
        return 0;

    adb->bufsize = sk_write(adb->s, buf, len);

    return adb->bufsize;
}

/*
 * Called to query the current socket sendability status.
 */
static int adb_sendbuffer(void *handle)
{
    Adb adb = (Adb) handle;
    return adb->bufsize;
}

/*
 * Called to set the size of the window
 */
static void adb_size(void *handle, int width, int height)
{
    /* Do nothing! */
    return;
}

/*
 * Send adb special codes.
 */
static void adb_special(void *handle, Telnet_Special code)
{
    /* Do nothing! */
    return;
}

/*
 * Return a list of the special codes that make sense in this
 * protocol.
 */
static const struct telnet_special *adb_get_specials(void *handle)
{
    return NULL;
}

static int adb_connected(void *handle)
{
    Adb adb = (Adb) handle;
    return adb->s != NULL;
}

static int adb_sendok(void *handle)
{
    return 1;
}

static void adb_unthrottle(void *handle, int backlog)
{
    Adb adb = (Adb) handle;
    sk_set_frozen(adb->s, backlog > ADB_MAX_BACKLOG);
}

static int adb_ldisc(void *handle, int option)
{
    // Don't allow line discipline options
    return 0;
}

static void adb_provide_ldisc(void *handle, void *ldisc)
{
    /* This is a stub. */
}

static void adb_provide_logctx(void *handle, void *logctx)
{
    /* This is a stub. */
}

static int adb_exitcode(void *handle)
{
    Adb adb = (Adb) handle;
    if (adb->s != NULL)
        return -1;                     /* still connected */
    else
        /* Exit codes are a meaningless concept in the Adb protocol */
        return 0;
}

/*
 * cfg_info for Adb does nothing at all.
 */
static int adb_cfg_info(void *handle)
{
    return 0;
}

Backend adb_backend = {
    adb_init,
    adb_free,
    adb_reconfig,
    adb_send,
    adb_sendbuffer,
    adb_size,
    adb_special,
    adb_get_specials,
    adb_connected,
    adb_exitcode,
    adb_sendok,
    adb_ldisc,
    adb_provide_ldisc,
    adb_provide_logctx,
    adb_unthrottle,
    adb_cfg_info,
    NULL /* test_for_upstream */,
    "adb",
    PROT_ADB,
    5037
};

#endif
