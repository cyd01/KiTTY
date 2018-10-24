/*
 * Serial back end (Windows-specific).
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "putty.h"

#define SERIAL_MAX_BACKLOG 4096

typedef struct serial_backend_data {
    HANDLE port;
    struct handle *out, *in;
    void *frontend;
    int bufsize;
    long clearbreak_time;
    int break_in_progress;
} *Serial;

static void serial_terminate(Serial serial)
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

static int serial_gotdata(struct handle *h, void *data, int len)
{
    Serial serial = (Serial)handle_get_privdata(h);
    if (len <= 0) {
	const char *error_msg;

	/*
	 * Currently, len==0 should never happen because we're
	 * ignoring EOFs. However, it seems not totally impossible
	 * that this same back end might be usable to talk to named
	 * pipes or some other non-serial device, in which case EOF
	 * may become meaningful here.
	 */
	if (len == 0)
	    error_msg = "End of file reading from serial device";
	else
	    error_msg = "Error reading from serial device";

	serial_terminate(serial);

	notify_remote_exit(serial->frontend);

	logevent(serial->frontend, error_msg);

	connection_fatal(serial->frontend, "%s", error_msg);

	return 0;		       /* placate optimiser */
    } else {
	return from_backend(serial->frontend, 0, data, len);
    }
}

static void serial_sentdata(struct handle *h, int new_backlog)
{
    Serial serial = (Serial)handle_get_privdata(h);
    if (new_backlog < 0) {
	const char *error_msg = "Error writing to serial device";

	serial_terminate(serial);

	notify_remote_exit(serial->frontend);

	logevent(serial->frontend, error_msg);

	connection_fatal(serial->frontend, "%s", error_msg);
    } else {
	serial->bufsize = new_backlog;
    }
}

static const char *serial_configure(Serial serial, HANDLE serport, Conf *conf)
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
	char *msg;
	const char *str;

	/*
	 * Boilerplate.
	 */
	dcb.fBinary = TRUE;
	dcb.fDtrControl = DTR_CONTROL_ENABLE;
	dcb.fDsrSensitivity = FALSE;
	dcb.fTXContinueOnXoff = FALSE;
	dcb.fOutX = FALSE;
	dcb.fInX = FALSE;
	dcb.fErrorChar = FALSE;
	dcb.fNull = FALSE;
	dcb.fRtsControl = RTS_CONTROL_ENABLE;
	dcb.fAbortOnError = FALSE;
	dcb.fOutxCtsFlow = FALSE;
	dcb.fOutxDsrFlow = FALSE;

	/*
	 * Configurable parameters.
	 */
	dcb.BaudRate = conf_get_int(conf, CONF_serspeed);
	msg = dupprintf("Configuring baud rate %lu", dcb.BaudRate);
	logevent(serial->frontend, msg);
	sfree(msg);

	dcb.ByteSize = conf_get_int(conf, CONF_serdatabits);
	msg = dupprintf("Configuring %u data bits", dcb.ByteSize);
	logevent(serial->frontend, msg);
	sfree(msg);

	switch (conf_get_int(conf, CONF_serstopbits)) {
	  case 2: dcb.StopBits = ONESTOPBIT; str = "1"; break;
	  case 3: dcb.StopBits = ONE5STOPBITS; str = "1.5"; break;
	  case 4: dcb.StopBits = TWOSTOPBITS; str = "2"; break;
	  default: return "Invalid number of stop bits (need 1, 1.5 or 2)";
	}
	msg = dupprintf("Configuring %s data bits", str);
	logevent(serial->frontend, msg);
	sfree(msg);

	switch (conf_get_int(conf, CONF_serparity)) {
	  case SER_PAR_NONE: dcb.Parity = NOPARITY; str = "no"; break;
	  case SER_PAR_ODD: dcb.Parity = ODDPARITY; str = "odd"; break;
	  case SER_PAR_EVEN: dcb.Parity = EVENPARITY; str = "even"; break;
	  case SER_PAR_MARK: dcb.Parity = MARKPARITY; str = "mark"; break;
	  case SER_PAR_SPACE: dcb.Parity = SPACEPARITY; str = "space"; break;
	}
	msg = dupprintf("Configuring %s parity", str);
	logevent(serial->frontend, msg);
	sfree(msg);

	switch (conf_get_int(conf, CONF_serflow)) {
	  case SER_FLOW_NONE:
	    str = "no";
	    break;
	  case SER_FLOW_XONXOFF:
	    dcb.fOutX = dcb.fInX = TRUE;
	    str = "XON/XOFF";
	    break;
	  case SER_FLOW_RTSCTS:
	    dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
	    dcb.fOutxCtsFlow = TRUE;
	    str = "RTS/CTS";
	    break;
	  case SER_FLOW_DSRDTR:
	    dcb.fDtrControl = DTR_CONTROL_HANDSHAKE;
	    dcb.fOutxDsrFlow = TRUE;
	    str = "DSR/DTR";
	    break;
	}
	msg = dupprintf("Configuring %s flow control", str);
	logevent(serial->frontend, msg);
	sfree(msg);

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
static const char *serial_init(void *frontend_handle, void **backend_handle,
			       Conf *conf, const char *host, int port,
			       char **realhost, int nodelay, int keepalive)
{
    Serial serial;
    HANDLE serport;
    const char *err;
    char *serline;

    serial = snew(struct serial_backend_data);
    serial->port = INVALID_HANDLE_VALUE;
    serial->out = serial->in = NULL;
    serial->bufsize = 0;
    serial->break_in_progress = FALSE;
    *backend_handle = serial;

    serial->frontend = frontend_handle;

    serline = conf_get_str(conf, CONF_serline);
    {
	char *msg = dupprintf("Opening serial device %s", serline);
	logevent(serial->frontend, msg);
        sfree(msg);
    }

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
    update_specials_menu(serial->frontend);

    return NULL;
}

static void serial_free(void *handle)
{
    Serial serial = (Serial) handle;

    serial_terminate(serial);
    expire_timer_context(serial);
    sfree(serial);
}

static void serial_reconfig(void *handle, Conf *conf)
{
    Serial serial = (Serial) handle;
    const char *err;

    err = serial_configure(serial, serial->port, conf);

    /*
     * FIXME: what should we do if err returns something?
     */
}

/*
 * Called to send data down the serial connection.
 */
static int serial_send(void *handle, const char *buf, int len)
{
    Serial serial = (Serial) handle;

    if (serial->out == NULL)
	return 0;

    serial->bufsize = handle_write(serial->out, buf, len);
    return serial->bufsize;
}

/*
 * Called to query the current sendability status.
 */
static int serial_sendbuffer(void *handle)
{
    Serial serial = (Serial) handle;
    return serial->bufsize;
}

/*
 * Called to set the size of the window
 */
static void serial_size(void *handle, int width, int height)
{
    /* Do nothing! */
    return;
}

static void serbreak_timer(void *ctx, unsigned long now)
{
    Serial serial = (Serial)ctx;

    if (now == serial->clearbreak_time && serial->port) {
	ClearCommBreak(serial->port);
	serial->break_in_progress = FALSE;
	logevent(serial->frontend, "Finished serial break");
    }
}

/*
 * Send serial special codes.
 */
static void serial_special(void *handle, Telnet_Special code)
{
    Serial serial = (Serial) handle;

    if (serial->port && code == TS_BRK) {
	logevent(serial->frontend, "Starting serial break at user request");
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
	serial->break_in_progress = TRUE;
    }

    return;
}

/*
 * Return a list of the special codes that make sense in this
 * protocol.
 */
static const struct telnet_special *serial_get_specials(void *handle)
{
    static const struct telnet_special specials[] = {
	{"Break", TS_BRK},
	{NULL, TS_EXITMENU}
    };
    return specials;
}

static int serial_connected(void *handle)
{
    return 1;			       /* always connected */
}

static int serial_sendok(void *handle)
{
    return 1;
}

static void serial_unthrottle(void *handle, int backlog)
{
    Serial serial = (Serial) handle;
    if (serial->in)
	handle_unthrottle(serial->in, backlog);
}

static int serial_ldisc(void *handle, int option)
{
    /*
     * Local editing and local echo are off by default.
     */
    return 0;
}

static void serial_provide_ldisc(void *handle, void *ldisc)
{
    /* This is a stub. */
}

static void serial_provide_logctx(void *handle, void *logctx)
{
    /* This is a stub. */
}

static int serial_exitcode(void *handle)
{
    Serial serial = (Serial) handle;
    if (serial->port != INVALID_HANDLE_VALUE)
        return -1;                     /* still connected */
    else
        /* Exit codes are a meaningless concept with serial ports */
        return INT_MAX;
}

/*
 * cfg_info for Serial does nothing at all.
 */
static int serial_cfg_info(void *handle)
{
    return 0;
}

Backend serial_backend = {
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
    serial_provide_logctx,
    serial_unthrottle,
    serial_cfg_info,
    NULL /* test_for_upstream */,
    "serial",
    PROT_SERIAL,
    0
};
