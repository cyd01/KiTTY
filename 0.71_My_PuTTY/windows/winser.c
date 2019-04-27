/*
 * Serial back end (Windows-specific).
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "putty.h"

#define SERIAL_MAX_BACKLOG 4096

typedef struct Serial Serial;
struct Serial {
    HANDLE port;
    struct handle *out, *in;
    Seat *seat;
    LogContext *logctx;
    int bufsize;
    long clearbreak_time;
    bool break_in_progress;
    Backend backend;
};

static void serial_terminate(Serial *serial)
{
    if (serial->out) {
	handle_free(serial->out);
	serial->out = NULL;
    }
    if (serial->in) {
	handle_free(serial->in);
	serial->in = NULL;
    }
    if (serial->port != INVALID_HANDLE_VALUE) {
	if (serial->break_in_progress)
	    ClearCommBreak(serial->port);
	CloseHandle(serial->port);
	serial->port = INVALID_HANDLE_VALUE;
    }
}

static size_t serial_gotdata(
    struct handle *h, const void *data, size_t len, int err)
{
    Serial *serial = (Serial *)handle_get_privdata(h);
    if (err || len == 0) {
	const char *error_msg;

	/*
	 * Currently, len==0 should never happen because we're
	 * ignoring EOFs. However, it seems not totally impossible
	 * that this same back end might be usable to talk to named
	 * pipes or some other non-serial device, in which case EOF
	 * may become meaningful here.
	 */
        if (!err)
	    error_msg = "End of file reading from serial device";
	else
	    error_msg = "Error reading from serial device";

	serial_terminate(serial);

	seat_notify_remote_exit(serial->seat);

        logevent(serial->logctx, error_msg);

	seat_connection_fatal(serial->seat, "%s", error_msg);

	return 0;
    } else {
	return seat_stdout(serial->seat, data, len);
    }
}

static void serial_sentdata(struct handle *h, size_t new_backlog, int err)
{
    Serial *serial = (Serial *)handle_get_privdata(h);
    if (err) {
	const char *error_msg = "Error writing to serial device";

	serial_terminate(serial);

	seat_notify_remote_exit(serial->seat);

        logevent(serial->logctx, error_msg);

	seat_connection_fatal(serial->seat, "%s", error_msg);
    } else {
	serial->bufsize = new_backlog;
    }
}

static const char *serial_configure(Serial *serial, HANDLE serport, Conf *conf)
{
    DCB dcb;
    COMMTIMEOUTS timeouts;

    /*
     * Set up the serial port parameters. If we can't even
     * GetCommState, we ignore the problem on the grounds that the
     * user might have pointed us at some other type of two-way
     * device instead of a serial port.
     */
    if (GetCommState(serport, &dcb)) {
	const char *str;

	/*
	 * Boilerplate.
	 */
	dcb.fBinary = true;
	dcb.fDtrControl = DTR_CONTROL_ENABLE;
	dcb.fDsrSensitivity = false;
	dcb.fTXContinueOnXoff = false;
	dcb.fOutX = false;
	dcb.fInX = false;
	dcb.fErrorChar = false;
	dcb.fNull = false;
	dcb.fRtsControl = RTS_CONTROL_ENABLE;
	dcb.fAbortOnError = false;
	dcb.fOutxCtsFlow = false;
	dcb.fOutxDsrFlow = false;

	/*
	 * Configurable parameters.
	 */
	dcb.BaudRate = conf_get_int(conf, CONF_serspeed);
        logeventf(serial->logctx, "Configuring baud rate %lu", dcb.BaudRate);

	dcb.ByteSize = conf_get_int(conf, CONF_serdatabits);
        logeventf(serial->logctx, "Configuring %u data bits", dcb.ByteSize);

	switch (conf_get_int(conf, CONF_serstopbits)) {
	  case 2: dcb.StopBits = ONESTOPBIT; str = "1"; break;
	  case 3: dcb.StopBits = ONE5STOPBITS; str = "1.5"; break;
	  case 4: dcb.StopBits = TWOSTOPBITS; str = "2"; break;
	  default: return "Invalid number of stop bits (need 1, 1.5 or 2)";
	}
        logeventf(serial->logctx, "Configuring %s data bits", str);

	switch (conf_get_int(conf, CONF_serparity)) {
	  case SER_PAR_NONE: dcb.Parity = NOPARITY; str = "no"; break;
	  case SER_PAR_ODD: dcb.Parity = ODDPARITY; str = "odd"; break;
	  case SER_PAR_EVEN: dcb.Parity = EVENPARITY; str = "even"; break;
	  case SER_PAR_MARK: dcb.Parity = MARKPARITY; str = "mark"; break;
	  case SER_PAR_SPACE: dcb.Parity = SPACEPARITY; str = "space"; break;
	}
        logeventf(serial->logctx, "Configuring %s parity", str);

	switch (conf_get_int(conf, CONF_serflow)) {
	  case SER_FLOW_NONE:
	    str = "no";
	    break;
	  case SER_FLOW_XONXOFF:
	    dcb.fOutX = dcb.fInX = true;
	    str = "XON/XOFF";
	    break;
	  case SER_FLOW_RTSCTS:
	    dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
	    dcb.fOutxCtsFlow = true;
	    str = "RTS/CTS";
	    break;
	  case SER_FLOW_DSRDTR:
	    dcb.fDtrControl = DTR_CONTROL_HANDSHAKE;
	    dcb.fOutxDsrFlow = true;
	    str = "DSR/DTR";
	    break;
	}
        logeventf(serial->logctx, "Configuring %s flow control", str);

	if (!SetCommState(serport, &dcb))
	    return "Unable to configure serial port";

	timeouts.ReadIntervalTimeout = 1;
	timeouts.ReadTotalTimeoutMultiplier = 0;
	timeouts.ReadTotalTimeoutConstant = 0;
	timeouts.WriteTotalTimeoutMultiplier = 0;
	timeouts.WriteTotalTimeoutConstant = 0;
	if (!SetCommTimeouts(serport, &timeouts))
	    return "Unable to configure serial timeouts";
    }

    return NULL;
}

/*
 * Called to set up the serial connection.
 * 
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'. It must be
 * freed by the caller.
 */
static const char *serial_init(Seat *seat, Backend **backend_handle,
                               LogContext *logctx, Conf *conf,
                               const char *host, int port,
			       char **realhost, bool nodelay, bool keepalive)
{
    Serial *serial;
    HANDLE serport;
    const char *err;
    char *serline;

    /* No local authentication phase in this protocol */
    seat_set_trust_status(seat, false);

    serial = snew(Serial);
    serial->port = INVALID_HANDLE_VALUE;
    serial->out = serial->in = NULL;
    serial->bufsize = 0;
    serial->break_in_progress = false;
    serial->backend.vt = &serial_backend;
    *backend_handle = &serial->backend;

    serial->seat = seat;
    serial->logctx = logctx;

    serline = conf_get_str(conf, CONF_serline);
    logeventf(serial->logctx, "Opening serial device %s", serline);

    {
	/*
	 * Munge the string supplied by the user into a Windows filename.
	 *
	 * Windows supports opening a few "legacy" devices (including
	 * COM1-9) by specifying their names verbatim as a filename to
	 * open. (Thus, no files can ever have these names. See
	 * <http://msdn2.microsoft.com/en-us/library/aa365247.aspx>
	 * ("Naming a File") for the complete list of reserved names.)
	 *
	 * However, this doesn't let you get at devices COM10 and above.
	 * For that, you need to specify a filename like "\\.\COM10".
	 * This is also necessary for special serial and serial-like
	 * devices such as \\.\WCEUSBSH001. It also works for the "legacy"
	 * names, so you can do \\.\COM1 (verified as far back as Win95).
	 * See <http://msdn2.microsoft.com/en-us/library/aa363858.aspx>
	 * (CreateFile() docs).
	 *
	 * So, we believe that prepending "\\.\" should always be the
	 * Right Thing. However, just in case someone finds something to
	 * talk to that doesn't exist under there, if the serial line
	 * contains a backslash, we use it verbatim. (This also lets
	 * existing configurations using \\.\ continue working.)
	 */
	char *serfilename =
	    dupprintf("%s%s", strchr(serline, '\\') ? "" : "\\\\.\\", serline);
	serport = CreateFile(serfilename, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			     OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	sfree(serfilename);
    }

    if (serport == INVALID_HANDLE_VALUE)
	return "Unable to open serial port";

    err = serial_configure(serial, serport, conf);
    if (err)
	return err;

    serial->port = serport;
    serial->out = handle_output_new(serport, serial_sentdata, serial,
				    HANDLE_FLAG_OVERLAPPED);
    serial->in = handle_input_new(serport, serial_gotdata, serial,
				  HANDLE_FLAG_OVERLAPPED |
				  HANDLE_FLAG_IGNOREEOF |
				  HANDLE_FLAG_UNITBUFFER);

    *realhost = dupstr(serline);

    /*
     * Specials are always available.
     */
    seat_update_specials_menu(serial->seat);

    return NULL;
}

static void serial_free(Backend *be)
{
    Serial *serial = container_of(be, Serial, backend);

    serial_terminate(serial);
    expire_timer_context(serial);
    sfree(serial);
}

static void serial_reconfig(Backend *be, Conf *conf)
{
    Serial *serial = container_of(be, Serial, backend);

    serial_configure(serial, serial->port, conf);

    /*
     * FIXME: what should we do if that call returned a non-NULL error
     * message?
     */
}

/*
 * Called to send data down the serial connection.
 */
static size_t serial_send(Backend *be, const char *buf, size_t len)
{
    Serial *serial = container_of(be, Serial, backend);

    if (serial->out == NULL)
	return 0;

    serial->bufsize = handle_write(serial->out, buf, len);
    return serial->bufsize;
}

/*
 * Called to query the current sendability status.
 */
static size_t serial_sendbuffer(Backend *be)
{
    Serial *serial = container_of(be, Serial, backend);
    return serial->bufsize;
}

/*
 * Called to set the size of the window
 */
static void serial_size(Backend *be, int width, int height)
{
    /* Do nothing! */
    return;
}

static void serbreak_timer(void *ctx, unsigned long now)
{
    Serial *serial = (Serial *)ctx;

    if (now == serial->clearbreak_time && serial->port) {
	ClearCommBreak(serial->port);
	serial->break_in_progress = false;
        logevent(serial->logctx, "Finished serial break");
    }
}

/*
 * Send serial special codes.
 */
static void serial_special(Backend *be, SessionSpecialCode code, int arg)
{
    Serial *serial = container_of(be, Serial, backend);

    if (serial->port && code == SS_BRK) {
        logevent(serial->logctx, "Starting serial break at user request");
	SetCommBreak(serial->port);
	/*
	 * To send a serial break on Windows, we call SetCommBreak
	 * to begin the break, then wait a bit, and then call
	 * ClearCommBreak to finish it. Hence, I must use timing.c
	 * to arrange a callback when it's time to do the latter.
	 * 
	 * SUS says that a default break length must be between 1/4
	 * and 1/2 second. FreeBSD apparently goes with 2/5 second,
	 * and so will I. 
	 */
	serial->clearbreak_time =
	    schedule_timer(TICKSPERSEC * 2 / 5, serbreak_timer, serial);
	serial->break_in_progress = true;
    }

    return;
}

/*
 * Return a list of the special codes that make sense in this
 * protocol.
 */
static const SessionSpecial *serial_get_specials(Backend *be)
{
    static const SessionSpecial specials[] = {
	{"Break", SS_BRK},
	{NULL, SS_EXITMENU}
    };
    return specials;
}

static bool serial_connected(Backend *be)
{
    return true;                       /* always connected */
}

static bool serial_sendok(Backend *be)
{
    return true;
}

static void serial_unthrottle(Backend *be, size_t backlog)
{
    Serial *serial = container_of(be, Serial, backend);
    if (serial->in)
	handle_unthrottle(serial->in, backlog);
}

static bool serial_ldisc(Backend *be, int option)
{
    /*
     * Local editing and local echo are off by default.
     */
    return false;
}

static void serial_provide_ldisc(Backend *be, Ldisc *ldisc)
{
    /* This is a stub. */
}

static int serial_exitcode(Backend *be)
{
    Serial *serial = container_of(be, Serial, backend);
    if (serial->port != INVALID_HANDLE_VALUE)
        return -1;                     /* still connected */
    else
        /* Exit codes are a meaningless concept with serial ports */
        return INT_MAX;
}

/*
 * cfg_info for Serial does nothing at all.
 */
static int serial_cfg_info(Backend *be)
{
    return 0;
}

const struct BackendVtable serial_backend = {
    serial_init,
    serial_free,
    serial_reconfig,
    serial_send,
    serial_sendbuffer,
    serial_size,
    serial_special,
    serial_get_specials,
    serial_connected,
    serial_exitcode,
    serial_sendok,
    serial_ldisc,
    serial_provide_ldisc,
    serial_unthrottle,
    serial_cfg_info,
    NULL /* test_for_upstream */,
    "serial",
    PROT_SERIAL,
    0
};
