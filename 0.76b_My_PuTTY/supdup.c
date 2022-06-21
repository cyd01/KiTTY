/*
* Supdup backend
*/

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "putty.h"

/*
 * TTYOPT FUNCTION BITS (36-bit bitmasks)
*/
#define TOALT   0200000000000LL         //  Characters 0175 and 0176 are converted to altmode (0033) on input
#define TOCLC   0100000000000LL         //  (user option bit) Convert lower-case input to upper-case
#define TOERS   0040000000000LL         //  Selective erase is supported
#define TOMVB   0010000000000LL         //  Backspacing is supported
#define TOSAI   0004000000000LL         //  Stanford/ITS extended ASCII graphics character set is supported
#define TOSA1   0002000000000LL         //  (user option bit) Characters 0001-0037 displayed using Stanford/ITS chars
#define TOOVR   0001000000000LL         //  Overprinting is supported
#define TOMVU   0000400000000LL         //  Moving cursor upwards is supported
#define TOMOR   0000200000000LL         //  (user option bit) System should provide **MORE** processing
#define TOROL   0000100000000LL         //  (user option bit) Terminal should scroll instead of wrapping
#define TOLWR   0000020000000LL         //  Lowercase characters are supported
#define TOFCI   0000010000000LL         //  Terminal can generate CONTROL and META characters
#define TOLID   0000002000000LL         //  Line insert/delete operations supported
#define TOCID   0000001000000LL         //  Character insert/delete operations supported
#define TPCBS   0000000000040LL         //  Terminal is using the "intelligent terminal protocol" (must be on)
#define TPORS   0000000000010LL         //  Server should process output resets

// Initialization words (36-bit constants)
#define WORDS   0777773000000           //  Negative number of config words to send (6) in high 18 bits
#define TCTYP   0000000000007           //  Defines the terminal type (MUST be 7)
#define TTYROL  0000000000001           //  Scroll amount for terminal (1 line at a time)


// %TD opcodes
//
#define TDMOV   0200                    // Cursor positioning
#define TDMV1   0201                    // Internal cursor positioning
#define TDEOF   0202                    // Erase to end of screen
#define TDEOL   0203                    // Erase to end of line
#define TDDLF   0204                    // Clear the character the cursor is on
#define TDCRL   0207                    // Carriage return
#define TDNOP   0210                    // No-op; should be ignored.
#define TDBS    0211                    // Backspace (not in official SUPDUP spec)
#define TDLF    0212                    // Linefeed (not in official SUPDUP spec)
#define TDCR    0213                    // Carriage Return (ditto)
#define TDORS   0214                    // Output reset
#define TDQOT   0215                    // Quotes the following character
#define TDFS    0216                    // Non-destructive forward space
#define TDMV0   0217                    // General cursor positioning code
#define TDCLR   0220                    // Erase the screen, home cursor
#define TDBEL   0221                    // Generate an audio tone, bell, whatever
#define TDILP   0223                    // Insert blank lines at the cursor
#define TDDLP   0224                    // Delete lines at the cursor
#define TDICP   0225                    // Insert blanks at cursor
#define TDDCP   0226                    // Delete characters at cursor
#define TDBOW   0227                    // Display black chars on white screen
#define TDRST   0230                    // Reset %TDBOW

/* Maximum number of octets following a %TD code. */
#define TD_ARGS_MAX 4

typedef struct supdup_tag Supdup;
struct supdup_tag
{
    Socket *s;
    bool closed_on_socket_error;

    Seat *seat;
    LogContext *logctx;
    int term_width, term_height;

    long long ttyopt;
    long tcmxv;
    long tcmxh;

    bool sent_location;

    Conf *conf;

    int bufsize;

    enum {
        CONNECTING,     // waiting for %TDNOP from server after sending connection params
        CONNECTED       // %TDNOP received, connected.
    } state;

    enum {
        TD_TOPLEVEL,
        TD_ARGS,
        TD_ARGSDONE
    } tdstate;

    int td_code;
    int td_argcount;
    char td_args[TD_ARGS_MAX];
    int td_argindex;

    void (*print) (strbuf *outbuf, int c);

    Pinger *pinger;

    Plug plug;
    Backend backend;
};

#define SUPDUP_MAX_BACKLOG 4096

static void c_write(Supdup *supdup, unsigned char *buf, int len)
{
    size_t backlog = seat_stdout(supdup->seat, buf, len);
    sk_set_frozen(supdup->s, backlog > SUPDUP_MAX_BACKLOG);
}

static void supdup_send_location(Supdup *supdup)
{
    char locHeader[] = { 0300, 0302 };
    char* locString = conf_get_str(supdup->conf, CONF_supdup_location);

    sk_write(supdup->s, locHeader, sizeof(locHeader));
    sk_write(supdup->s, locString, strlen(locString) + 1);      // include NULL terminator
}

static void print_ascii(strbuf *outbuf, int c)
{
    /* In ASCII mode, ignore control characters.  The server shouldn't
       send them. */
    if (c >= 040 && c < 0177)
        put_byte (outbuf, c);
}

static void print_its(strbuf *outbuf, int c)
{
    /* The ITS character set is documented in RFC 734. */
    static const char *map[] = {
        "\xc2\xb7",     "\342\206\223", "\316\261",     "\316\262",
        "\342\210\247", "\302\254",     "\316\265",     "\317\200",
        "\316\273",     "\xce\xb3",     "\xce\xb4",     "\xe2\x86\x91",
        "\xc2\xb1",     "\xe2\x8a\x95", "\342\210\236", "\342\210\202",
        "\342\212\202", "\342\212\203", "\342\210\251", "\342\210\252",
        "\342\210\200", "\342\210\203", "\xe2\x8a\x97", "\342\206\224",
        "\xe2\x86\x90", "\342\206\222", "\xe2\x89\xa0", "\xe2\x97\x8a",
        "\342\211\244", "\342\211\245", "\342\211\241", "\342\210\250",
        " ", "!", "\"", "#", "$", "%", "&", "'",
        "(", ")", "*", "+", ",", "-", ".", "/",
        "0", "1", "2", "3", "4", "5", "6", "7",
        "8", "9", ":", ";", "<", "=", ">", "?",
        "@", "A", "B", "C", "D", "E", "F", "G",
        "H", "I", "J", "K", "L", "M", "N", "O",
        "P", "Q", "R", "S", "T", "U", "V", "W",
        "X", "Y", "Z", "[", "\\", "]", "^", "_",
        "`", "a", "b", "c", "d", "e", "f", "g",
        "h", "i", "j", "k", "l", "m", "n", "o",
        "p", "q", "r", "s", "t", "u", "v", "w",
        "x", "y", "z", "{", "|", "}", "~", "\xe2\x88\xab"
    };

    put_data (outbuf, map[c], strlen(map[c]));
}

static void print_waits(strbuf *outbuf, int c)
{
    /* The WAITS character set used at the Stanford AI Lab is documented
       here: https://www.saildart.org/allow/sail-charset-utf8.html */
    static const char *map[] = {
        "",             "\342\206\223", "\316\261",     "\316\262",
        "\342\210\247", "\302\254",     "\316\265",     "\317\200",
        "\316\273",     "",             "",             "",
        "",             "",             "\342\210\236", "\342\210\202",
        "\342\212\202", "\342\212\203", "\342\210\251", "\342\210\252",
        "\342\210\200", "\342\210\203", "\xe2\x8a\x97", "\342\206\224",
        "_",            "\342\206\222", "~",            "\xe2\x89\xa0",
        "\342\211\244", "\342\211\245", "\342\211\241", "\342\210\250",
        " ", "!", "\"", "#", "$", "%", "&", "'",
        "(", ")", "*", "+", ",", "-", ".", "/",
        "0", "1", "2", "3", "4", "5", "6", "7",
        "8", "9", ":", ";", "<", "=", ">", "?",
        "@", "A", "B", "C", "D", "E", "F", "G",
        "H", "I", "J", "K", "L", "M", "N", "O",
        "P", "Q", "R", "S", "T", "U", "V", "W",
        "X", "Y", "Z", "[", "\\", "]", "\xe2\x86\x91", "\xe2\x86\x90",
        "`", "a", "b", "c", "d", "e", "f", "g",
        "h", "i", "j", "k", "l", "m", "n", "o",
        "p", "q", "r", "s", "t", "u", "v", "w",
        "x", "y", "z", "{", "|", "\xe2\x97\x8a", "}", ""
    };

    put_data (outbuf, map[c], strlen(map[c]));
}

static void do_toplevel(Supdup *supdup, strbuf *outbuf, int c)
{
    // Toplevel: Waiting for a %TD code or a printable character
    if (c >= 0200) {
        // Handle SUPDUP %TD codes (codes greater than or equal to 200)
        supdup->td_argindex = 0;
        supdup->td_code = c;
        switch (c) {
        case TDMOV:
            // %TD codes using 4 arguments
            supdup->td_argcount = 4;
            supdup->tdstate = TD_ARGS;
            break;

        case TDMV0:
        case TDMV1:
            // %TD codes using 2 arguments
            supdup->td_argcount = 2;
            supdup->tdstate = TD_ARGS;
            break;

        case TDQOT:
        case TDILP:
        case TDDLP:
        case TDICP:
        case TDDCP:
            // %TD codes using 1 argument
            supdup->td_argcount = 1;
            supdup->tdstate = TD_ARGS;
            break;

        case TDEOF:
        case TDEOL:
        case TDDLF:
        case TDCRL:
        case TDNOP:
        case TDORS:
        case TDFS:
        case TDCLR:
        case TDBEL:
        case TDBOW:
        case TDRST:
        case TDBS:
        case TDCR:
        case TDLF:
            // %TD codes using 0 arguments
            supdup->td_argcount = 0;
            supdup->tdstate = TD_ARGSDONE;
            break;

        default:
            // Unhandled, ignore
            break;
        }
    } else {
        supdup->print(outbuf, c);
    }
}

static void do_args(Supdup *supdup, strbuf *outbuf, int c)
{
    // Collect up args for %TD code
    if (supdup->td_argindex < TD_ARGS_MAX) {
        supdup->td_args[supdup->td_argindex] = c;
        supdup->td_argindex++;

        if (supdup->td_argcount == supdup->td_argindex) {
            // No more args, %TD code is ready to go.
            supdup->tdstate = TD_ARGSDONE;
        }
    } else {
        // Should never hit this state, if we do we will just
        // return to TOPLEVEL.
        supdup->tdstate = TD_TOPLEVEL;
    }
}

static void do_argsdone(Supdup *supdup, strbuf *outbuf, int c)
{
    char buf[4];
    int x, y;

    // Arguments for %TD code have been collected; dispatch based
    // on the %TD code we're handling.
    switch (supdup->td_code) {
    case TDMOV:
        /*
          General cursor position code.  Followed by four bytes;
          the first two are the "old" vertical and horizontal
          positions and may be ignored.  The next two are the new
          vertical and horizontal positions.  The cursor should be
          moved to this position.
        */

        // We only care about the new position.
        strbuf_catf(outbuf, "\033[%d;%dH", supdup->td_args[2]+1, supdup->td_args[3]+1);
        break;

    case TDMV0:
    case TDMV1:
        /*
          General cursor position code.  Followed by two bytes;
          the new vertical and horizontal positions.
        */
        strbuf_catf(outbuf, "\033[%d;%dH", supdup->td_args[0]+1, supdup->td_args[1]+1);
        break;

    case TDEOF:
        /*
          Erase to end of screen.  This is an optional function
          since many terminals do not support this.  If the
          terminal does not support this function, it should be
          treated the same as %TDEOL.

          %TDEOF does an erase to end of line, then erases all
          lines lower on the screen than the cursor.  The cursor
          does not move.
        */
        strbuf_catf(outbuf, "\033[J");
        break;

    case TDEOL:
        /*
          Erase to end of line.  This erases the character
          position the cursor is at and all positions to the right
          on the same line.  The cursor does not move.
        */
        strbuf_catf(outbuf, "\033[K");
        break;

    case TDDLF:
        /*
          Clear the character position the cursor is on.  The
          cursor does not move.
        */
        strbuf_catf(outbuf, "\033[X");
        break;

    case TDCRL:
        /*
          If the cursor is not on the bottom line of the screen,
          move cursor to the beginning of the next line and clear
          that line.  If the cursor is at the bottom line, scroll
          up.
        */
        strbuf_catf(outbuf, "\015\012");
        break;

    case TDNOP:
        /*
          No-op; should be ignored.
        */
        break;

    case TDORS:
        /*
          Output reset.  This code serves as a data mark for
          aborting output much as IAC DM does in the ordinary
          TELNET protocol.
        */
        outbuf->len = 0;
        if (!seat_get_cursor_position(supdup->seat, &x, &y))
            x = y = 0;
        buf[0] = 034;
        buf[1] = 020;
        buf[2] = y;
        buf[3] = x;
        sk_write(supdup->s, buf, 4);
        break;

    case TDQOT:
        /*
          Quotes the following character.  This is used when
          sending 8-bit codes which are not %TD codes, for
          instance when loading programs into an intelligent
          terminal.  The following character should be passed
          through intact to the terminal.
        */

        put_byte(outbuf, supdup->td_args[0]);
        break;

    case TDFS:
        /*
          Non-destructive forward space.  The cursor moves right
          one position; this code will not be sent at the end of a
          line.
        */

        strbuf_catf(outbuf, "\033[C");
        break;

    case TDCLR:
        /*
          Erase the screen.  Home the cursor to the top left hand
          corner of the screen.
        */
        strbuf_catf(outbuf, "\033[2J\033[H");
        break;

    case TDBEL:
        /*
          Generate an audio tone, bell, whatever.
        */

        strbuf_catf(outbuf, "\007");
        break;

    case TDILP:
        /*
          Insert blank lines at the cursor; followed by a byte
          containing a count of the number of blank lines to
          insert.  The cursor is unmoved.  The line the cursor is
          on and all lines below it move down; lines moved off the
          bottom of the screen are lost.
        */
        strbuf_catf(outbuf, "\033[%dL", supdup->td_args[0]);
        break;

    case TDDLP:
        /*
          Delete lines at the cursor; followed by a count.  The
          cursor is unmoved.  The first line deleted is the one
          the cursor is on.  Lines below those deleted move up.
          Newly- created lines at the bottom of the screen are
          blank.
        */
        strbuf_catf(outbuf, "\033[%dM", supdup->td_args[0]);
        break;

    case TDICP:
        /*
          Insert blank character positions at the cursor; followed
          by a count.  The cursor is unmoved.  The character the
          cursor is on and all characters to the right on the
          current line move to the right; characters moved off the
          end of the line are lost.
        */
        strbuf_catf(outbuf, "\033[%d@", supdup->td_args[0]);
        break;

    case TDDCP:
        /*
          Delete characters at the cursor; followed by a count.
          The cursor is unmoved.  The first character deleted is
          the one the cursor is on.  Newly-created characters at
          the end of the line are blank.
        */
        strbuf_catf(outbuf, "\033[%dP", supdup->td_args[0]);
        break;

    case TDBOW:
    case TDRST:
        /*
          Display black characters on white screen.
          HIGHLY OPTIONAL.
        */

        // Since this is HIGHLY OPTIONAL, I'm not going
        // to implement it yet.
        break;

        /*
         * Non-standard (whatever "standard" means here) SUPDUP
         * commands.  These are used (at the very least) by
         * Genera's SUPDUP implementation.  Cannot find any
         * official documentation, behavior is based on UNIX
         * SUPDUP implementation from MIT.
         */
    case TDBS:
        /*
         * Backspace -- move cursor back one character (does not
         * appear to wrap...)
         */
        put_byte(outbuf, '\010');
        break;

    case TDLF:
        /*
         * Linefeed -- move cursor down one line (again, no wrapping)
         */
        put_byte(outbuf, '\012');
        break;

    case TDCR:
        /*
         * Carriage return -- move cursor to start of current line.
         */
        put_byte(outbuf, '\015');
        break;
    }

    // Return to top level to pick up the next %TD code or
    // printable character.
    supdup->tdstate = TD_TOPLEVEL;
}

static void term_out_supdup(Supdup *supdup, strbuf *outbuf, int c)
{
    if (supdup->tdstate == TD_TOPLEVEL) {
        do_toplevel (supdup, outbuf, c);
    } else if (supdup->tdstate == TD_ARGS) {
        do_args (supdup, outbuf, c);
    }

    // If all arguments for a %TD code are ready, we will execute the code now.
    if (supdup->tdstate == TD_ARGSDONE) {
        do_argsdone (supdup, outbuf, c);
    }
}

static void do_supdup_read(Supdup *supdup, const char *buf, size_t len)
{
    strbuf *outbuf = strbuf_new();

    while (len--) {
        int c = (unsigned char)*buf++;
        switch (supdup->state) {
        case CONNECTING:
            // "Following the transmission of the terminal options by
            //  the user, the server should respond with an ASCII
            //  greeting message, terminated with a %TDNOP code..."
            if (TDNOP == c) {
                // Greeting done, switch to the CONNECTED state.
                supdup->state = CONNECTED;
                supdup->tdstate = TD_TOPLEVEL;
            } else {
                // Forward the greeting message (which is straight
                // ASCII, no controls) on so it gets displayed TODO:
                // filter out only printable chars?
                put_byte(outbuf, c);
            }
            break;

        case CONNECTED:
            // "All transmissions from the server after the %TDNOP
            //  [see above] are either printing characters or virtual
            //  terminal display codes."  Forward these on to the
            //  frontend which will decide what to do with them.
            term_out_supdup(supdup, outbuf, c);
            /*
             * Hack to make Symbolics Genera SUPDUP happy: Wait until
             * after we're connected (finished the initial handshake
             * and have gotten additional data) before sending the
             * location string.  For some reason doing so earlier
             * causes the Symbolics SUPDUP to end up in an odd state.
             */
            if (!supdup->sent_location) {
                supdup_send_location(supdup);
                supdup->sent_location = true;
            }
            break;
        }

        if (outbuf->len >= 4096) {
            c_write(supdup, outbuf->u, outbuf->len);
            outbuf->len = 0;
        }
    }

    if (outbuf->len)
        c_write(supdup, outbuf->u, outbuf->len);
    strbuf_free(outbuf);
}

static void supdup_log(Plug *plug, PlugLogType type, SockAddr *addr, int port,
                       const char *error_msg, int error_code)
{
    Supdup *supdup = container_of(plug, Supdup, plug);
    backend_socket_log(supdup->seat, supdup->logctx, type, addr, port,
                       error_msg, error_code,
                       supdup->conf, supdup->state != CONNECTING);
}

static void supdup_closing(Plug *plug, const char *error_msg, int error_code,
                           bool calling_back)
{
    Supdup *supdup = container_of(plug, Supdup, plug);

    /*
     * We don't implement independent EOF in each direction for Telnet
     * connections; as soon as we get word that the remote side has
     * sent us EOF, we wind up the whole connection.
     */

    if (supdup->s) {
        sk_close(supdup->s);
        supdup->s = NULL;
        if (error_msg)
            supdup->closed_on_socket_error = true;
        seat_notify_remote_exit(supdup->seat);
    }
    if (error_msg) {
        logevent(supdup->logctx, error_msg);
        seat_connection_fatal(supdup->seat, "%s", error_msg);
    }
    /* Otherwise, the remote side closed the connection normally. */
}

static void supdup_receive(Plug *plug, int urgent, const char *data, size_t len)
{
    Supdup *supdup = container_of(plug, Supdup, plug);
    do_supdup_read(supdup, data, len);
}

static void supdup_sent(Plug *plug, size_t bufsize)
{
    Supdup *supdup = container_of(plug, Supdup, plug);
    supdup->bufsize = bufsize;
}

static void supdup_send_36bits(Supdup *supdup, unsigned long long thirtysix)
{
    //
    // From RFC734:
    // "Each word is sent through the 8-bit connection as six
    //  6-bit bytes, most-significant first."
    //
    // Split the 36-bit word into 6 6-bit "bytes", packed into
    // 8-bit bytes and send, most-significant byte first.
    //
    for (int i = 5; i >= 0; i--) {
        char sixBits = (thirtysix >> (i * 6)) & 077;
        sk_write(supdup->s, &sixBits, 1);
    }
}

static void supdup_send_config(Supdup *supdup)
{
    supdup_send_36bits(supdup, WORDS);          // negative length
    supdup_send_36bits(supdup, TCTYP);          // terminal type
    supdup_send_36bits(supdup, supdup->ttyopt); // options
    supdup_send_36bits(supdup, supdup->tcmxv);  // height
    supdup_send_36bits(supdup, supdup->tcmxh);  // width
    supdup_send_36bits(supdup, TTYROL);         // scroll amount
}

/*
* Called to set up the Supdup connection.
*
* Returns an error message, or NULL on success.
*
* Also places the canonical host name into `realhost'. It must be
* freed by the caller.
*/
static char *supdup_init(const BackendVtable *x, Seat *seat,
                         Backend **backend_handle,
                         LogContext *logctx, Conf *conf,
                         const char *host, int port, char **realhost,
                         bool nodelay, bool keepalive)
{
    static const PlugVtable fn_table = {
        .log = supdup_log,
        .closing = supdup_closing,
        .receive = supdup_receive,
        .sent = supdup_sent,
    };
    SockAddr *addr;
    const char *err;
    Supdup *supdup;
    char *loghost;
    int addressfamily;
    const char *utf8 = "\033%G";

    supdup = snew(struct supdup_tag);
    supdup->plug.vt = &fn_table;
    supdup->backend.vt = &supdup_backend;
    supdup->logctx = logctx;
    supdup->conf = conf_copy(conf);
    supdup->s = NULL;
    supdup->closed_on_socket_error = false;
    supdup->seat = seat;
    supdup->term_width = conf_get_int(supdup->conf, CONF_width);
    supdup->term_height = conf_get_int(supdup->conf, CONF_height);
    supdup->pinger = NULL;
    supdup->sent_location = false;
    *backend_handle = &supdup->backend;

    switch (conf_get_int(supdup->conf, CONF_supdup_ascii_set)) {
    case SUPDUP_CHARSET_ASCII:
      supdup->print = print_ascii;
      break;
    case SUPDUP_CHARSET_ITS:
      supdup->print = print_its;
      break;
    case SUPDUP_CHARSET_WAITS:
      supdup->print = print_waits;
      break;
    }

    /*
     * Try to find host.
     */
    {
        char *buf;
        addressfamily = conf_get_int(supdup->conf, CONF_addressfamily);
        buf = dupprintf("Looking up host \"%s\"%s", host,
                        (addressfamily == ADDRTYPE_IPV4 ? " (IPv4)" :
                         (addressfamily == ADDRTYPE_IPV6 ? " (IPv6)" :
                          "")));
        logevent(supdup->logctx, buf);
        sfree(buf);
    }
    addr = name_lookup(host, port, realhost, supdup->conf, addressfamily, NULL, "");
    if ((err = sk_addr_error(addr)) != NULL) {
        sk_addr_free(addr);
        return dupstr(err);
    }

    if (port < 0)
        port = 0137;            /* default supdup port */

    /*
     * Open socket.
     */
    supdup->s = new_connection(addr, *realhost, port, false, true,
                               nodelay, keepalive, &supdup->plug, supdup->conf);
    if ((err = sk_socket_error(supdup->s)) != NULL)
        return dupstr(err);

    supdup->pinger = pinger_new(supdup->conf, &supdup->backend);

    /*
     * We can send special commands from the start.
     */
    seat_update_specials_menu(supdup->seat);

    /*
     * loghost overrides realhost, if specified.
     */
    loghost = conf_get_str(supdup->conf, CONF_loghost);
    if (*loghost) {
        char *colon;

        sfree(*realhost);
        *realhost = dupstr(loghost);

        colon = host_strrchr(*realhost, ':');
        if (colon)
            *colon++ = '\0';
    }

    /*
     * Set up TTYOPTS based on config
     */
    int ascii_set = conf_get_int(supdup->conf, CONF_supdup_ascii_set);
    int more_processing = conf_get_bool(supdup->conf, CONF_supdup_more);
    int scrolling = conf_get_bool(supdup->conf, CONF_supdup_scroll);
    supdup->ttyopt =
        TOERS |
        TOMVB |
        (ascii_set == SUPDUP_CHARSET_ASCII ? 0 : TOSAI | TOSA1) |
        TOMVU |
        TOLWR |
        TOLID |
        TOCID |
        TPCBS |
        (scrolling ? TOROL : 0) |
        (more_processing ? TOMOR : 0) |
        TPORS;

    supdup->tcmxh = supdup->term_width - 1;             // -1 "..one column is used to indicate line continuation."
    supdup->tcmxv = supdup->term_height;

    /*
     * Send our configuration words to the server
     */
    supdup_send_config(supdup);

    /*
     * We next expect a connection message followed by %TDNOP from the server
     */
    supdup->state = CONNECTING;
    seat_set_trust_status(supdup->seat, false);

    /* Make sure the terminal is in UTF-8 mode. */
    c_write(supdup, (unsigned char *)utf8, strlen(utf8));

    return NULL;
}


static void supdup_free(Backend *be)
{
    Supdup *supdup = container_of(be, Supdup, backend);

    if (supdup->s)
        sk_close(supdup->s);
    if (supdup->pinger)
        pinger_free(supdup->pinger);
    conf_free(supdup->conf);
    sfree(supdup);
}

/*
* Reconfigure the Supdup backend.
*/
static void supdup_reconfig(Backend *be, Conf *conf)
{
    /* Nothing to do; SUPDUP cannot be reconfigured while running. */
}

/*
* Called to send data down the Supdup connection.
*/
static size_t supdup_send(Backend *be, const char *buf, size_t len)
{
    Supdup *supdup = container_of(be, Supdup, backend);
    char c;
    int i;

    if (supdup->s == NULL)
        return 0;

    for (i = 0; i < len; i++) {
        if (buf[i] == 034)
            supdup->bufsize = sk_write(supdup->s, "\034\034", 2);
        else {
            c = buf[i] & 0177;
            supdup->bufsize = sk_write(supdup->s, &c, 1);
        }
    }
    return supdup->bufsize;
}

/*
* Called to query the current socket sendability status.
*/
static size_t supdup_sendbuffer(Backend *be)
{
    Supdup *supdup = container_of(be, Supdup, backend);
    return supdup->bufsize;
}

/*
* Called to set the size of the window from Supdup's POV.
*/
static void supdup_size(Backend *be, int width, int height)
{
    Supdup *supdup = container_of(be, Supdup, backend);

    supdup->term_width = width;
    supdup->term_height = height;

    //
    // SUPDUP does not support resizing the terminal after connection
    // establishment.
    //
}

/*
* Send Telnet special codes.
*/
static void supdup_special(Backend *be, SessionSpecialCode code, int arg)
{
}

static const SessionSpecial *supdup_get_specials(Backend *be)
{
    return NULL;
}

static bool supdup_connected(Backend *be)
{
    Supdup *supdup = container_of(be, Supdup, backend);
    return supdup->s != NULL;
}

static bool supdup_sendok(Backend *be)
{
    return 1;
}

static void supdup_unthrottle(Backend *be, size_t backlog)
{
    Supdup *supdup = container_of(be, Supdup, backend);
    sk_set_frozen(supdup->s, backlog > SUPDUP_MAX_BACKLOG);
}

static bool supdup_ldisc(Backend *be, int option)
{
    /* No support for echoing or local editing. */
    return false;
}

static void supdup_provide_ldisc(Backend *be, Ldisc *ldisc)
{
}

static int supdup_exitcode(Backend *be)
{
    Supdup *supdup = container_of(be, Supdup, backend);
    if (supdup->s != NULL)
        return -1;                     /* still connected */
    else if (supdup->closed_on_socket_error)
        return INT_MAX;     /* a socket error counts as an unclean exit */
    else
        /* Supdup doesn't transmit exit codes back to the client */
        return 0;
}

/*
* cfg_info for Dupdup does nothing at all.
*/
static int supdup_cfg_info(Backend *be)
{
    return 0;
}

const BackendVtable supdup_backend = {
    .init = supdup_init,
    .free = supdup_free,
    .reconfig = supdup_reconfig,
    .send = supdup_send,
    .sendbuffer = supdup_sendbuffer,
    .size = supdup_size,
    .special = supdup_special,
    .get_specials = supdup_get_specials,
    .connected = supdup_connected,
    .exitcode = supdup_exitcode,
    .sendok = supdup_sendok,
    .ldisc_option_state = supdup_ldisc,
    .provide_ldisc = supdup_provide_ldisc,
    .unthrottle = supdup_unthrottle,
    .cfg_info = supdup_cfg_info,
    .id = "supdup",
    .displayname = "SUPDUP",
    .protocol = PROT_SUPDUP,
    .default_port = 0137,
    .flags = BACKEND_RESIZE_FORBIDDEN | BACKEND_NEEDS_TERMINAL,
};
