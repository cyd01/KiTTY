/*
 * PLink - a Windows command-line (stdin/stdout) variant of PuTTY.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>

#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */
#include "putty.h"
#include "storage.h"
#include "tree234.h"
#include "winsecur.h"

#ifdef MOD_PERSO
// Flag pour le fonctionnement en mode "portable" (gestion par fichiers)
extern int IniFileFlag ;

// Flag permettant la gestion de l'arborscence (dossier=folder) dans le cas d'un savemode=dir
extern int DirectoryBrowseFlag ;


#include "../../kitty_crypt.c"
#include "../../kitty_commun.h"

int GetPuttyFlag() { return 1 ; }
size_t win_seat_output_local(Seat *seat, bool is_stderr, const void *data, size_t len) { return 0 ; }

int get_param( const char * val ) {
	if( !stricmp( val, "INIFILE" ) ) { return IniFileFlag ; }
	else if( !stricmp( val, "DIRECTORYBROWSE" ) ) { return DirectoryBrowseFlag ; }
	return 0 ;
	}

void SetPasswordInConfig( char * password ) {
	int len ;
	if( password!=NULL ) {
		len = strlen( password ) ;
		if( len > 126 ) len = 126 ;
		}
	}

void SetUsernameInConfig( char * username ) {
	int len ;
	if( username!=NULL ) {
		len = strlen( username ) ;
		if( len > 126 ) len = 126 ;
		}
	}
#endif

#define WM_AGENT_CALLBACK (WM_APP + 4)

struct agent_callback {
    void (*callback)(void *, void *, int);
    void *callback_ctx;
    void *data;
    int len;
};

void cmdline_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    console_print_error_msg_fmt_v("plink", fmt, ap);
    va_end(ap);
    exit(1);
}

HANDLE inhandle, outhandle, errhandle;
struct handle *stdin_handle, *stdout_handle, *stderr_handle;
handle_sink stdout_hs, stderr_hs;
StripCtrlChars *stdout_scc, *stderr_scc;
BinarySink *stdout_bs, *stderr_bs;
DWORD orig_console_mode;

WSAEVENT netevent;

static Backend *backend;
Conf *conf;

static void plink_echoedit_update(Seat *seat, bool echo, bool edit)
{
    /* Update stdin read mode to reflect changes in line discipline. */
    DWORD mode;

    mode = ENABLE_PROCESSED_INPUT;
    if (echo)
	mode = mode | ENABLE_ECHO_INPUT;
    else
	mode = mode & ~ENABLE_ECHO_INPUT;
    if (edit)
	mode = mode | ENABLE_LINE_INPUT;
    else
	mode = mode & ~ENABLE_LINE_INPUT;
    SetConsoleMode(inhandle, mode);
}

static size_t plink_output(
    Seat *seat, bool is_stderr, const void *data, size_t len)
{
    BinarySink *bs = is_stderr ? stderr_bs : stdout_bs;
    put_data(bs, data, len);

    return handle_backlog(stdout_handle) + handle_backlog(stderr_handle);
}

static bool plink_eof(Seat *seat)
{
    handle_write_eof(stdout_handle);
    return false;   /* do not respond to incoming EOF with outgoing */
}

static int plink_get_userpass_input(Seat *seat, prompts_t *p, bufchain *input)
{
    int ret;
    ret = cmdline_get_passwd_input(p);
    if (ret == -1)
	ret = console_get_userpass_input(p);
    return ret;
}

static const SeatVtable plink_seat_vt = {
    plink_output,
    plink_eof,
    plink_get_userpass_input,
    nullseat_notify_remote_exit,
    console_connection_fatal,
    nullseat_update_specials_menu,
    nullseat_get_ttymode,
    nullseat_set_busy_status,
    console_verify_ssh_host_key,
    console_confirm_weak_crypto_primitive,
    console_confirm_weak_cached_hostkey,
    nullseat_is_never_utf8,
    plink_echoedit_update,
    nullseat_get_x_display,
    nullseat_get_windowid,
    nullseat_get_window_pixel_size,
    console_stripctrl_new,
    console_set_trust_status,
};
static Seat plink_seat[1] = {{ &plink_seat_vt }};

static DWORD main_thread_id;

void agent_schedule_callback(void (*callback)(void *, void *, int),
			     void *callback_ctx, void *data, int len)
{
    struct agent_callback *c = snew(struct agent_callback);
    c->callback = callback;
    c->callback_ctx = callback_ctx;
    c->data = data;
    c->len = len;
    PostThreadMessage(main_thread_id, WM_AGENT_CALLBACK, 0, (LPARAM)c);
}

/*
 *  Short description of parameters.
 */
static void usage(void)
{
    printf("Plink: command-line connection utility\n");
    printf("%s\n", ver);
    printf("Usage: plink [options] [user@]host [command]\n");
    printf("       (\"host\" can also be a PuTTY saved session name)\n");
    printf("Options:\n");
    printf("  -V        print version information and exit\n");
    printf("  -pgpfp    print PGP key fingerprints and exit\n");
    printf("  -v        show verbose messages\n");
    printf("  -load sessname  Load settings from saved session\n");
    printf("  -ssh -telnet -rlogin -raw -serial\n");
    printf("            force use of a particular protocol\n");
    printf("  -P port   connect to specified port\n");
    printf("  -l user   connect with specified username\n");
    printf("  -batch    disable all interactive prompts\n");
    printf("  -proxycmd command\n");
    printf("            use 'command' as local proxy\n");
    printf("  -sercfg configuration-string (e.g. 19200,8,n,1,X)\n");
    printf("            Specify the serial configuration (serial only)\n");
    printf("The following options only apply to SSH connections:\n");
    printf("  -pw passw login with specified password\n");
    printf("  -D [listen-IP:]listen-port\n");
    printf("            Dynamic SOCKS-based port forwarding\n");
    printf("  -L [listen-IP:]listen-port:host:port\n");
    printf("            Forward local port to remote address\n");
    printf("  -R [listen-IP:]listen-port:host:port\n");
    printf("            Forward remote port to local address\n");
    printf("  -X -x     enable / disable X11 forwarding\n");
    printf("  -A -a     enable / disable agent forwarding\n");
    printf("  -t -T     enable / disable pty allocation\n");
    printf("  -1 -2     force use of particular protocol version\n");
    printf("  -4 -6     force use of IPv4 or IPv6\n");
    printf("  -C        enable compression\n");
    printf("  -i key    private key file for user authentication\n");
    printf("  -noagent  disable use of Pageant\n");
    printf("  -agent    enable use of Pageant\n");
    printf("  -noshare  disable use of connection sharing\n");
    printf("  -share    enable use of connection sharing\n");
    printf("  -hostkey aa:bb:cc:...\n");
    printf("            manually specify a host key (may be repeated)\n");
    printf("  -sanitise-stderr, -sanitise-stdout, "
           "-no-sanitise-stderr, -no-sanitise-stdout\n");
    printf("            do/don't strip control chars from standard "
           "output/error\n");
    printf("  -no-antispoof   omit anti-spoofing prompt after "
           "authentication\n");
    printf("  -m file   read remote command(s) from file\n");
    printf("  -s        remote command is an SSH subsystem (SSH-2 only)\n");
    printf("  -N        don't start a shell/command (SSH-2 only)\n");
    printf("  -nc host:port\n");
    printf("            open tunnel in place of session (SSH-2 only)\n");
    printf("  -sshlog file\n");
    printf("  -sshrawlog file\n");
    printf("            log protocol details to a file\n");
    printf("  -shareexists\n");
    printf("            test whether a connection-sharing upstream exists\n");
#ifdef MOD_PERSO
    printf("  -auto-store-sshkey\n");
    printf("            store automatically the servers ssh keys\n");
#endif
    exit(1);
}

static void version(void)
{
    char *buildinfo_text = buildinfo("\n");
    printf("plink: %s\n%s\n", ver, buildinfo_text);
    sfree(buildinfo_text);
    exit(0);
}

char *do_select(SOCKET skt, bool startup)
{
    int events;
    if (startup) {
	events = (FD_CONNECT | FD_READ | FD_WRITE |
		  FD_OOB | FD_CLOSE | FD_ACCEPT);
    } else {
	events = 0;
    }
    if (p_WSAEventSelect(skt, netevent, events) == SOCKET_ERROR) {
	switch (p_WSAGetLastError()) {
	  case WSAENETDOWN:
	    return "Network is down";
	  default:
	    return "WSAEventSelect(): unknown error";
	}
    }
    return NULL;
}

size_t stdin_gotdata(struct handle *h, const void *data, size_t len, int err)
{
    if (err) {
	char buf[4096];
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0,
		      buf, lenof(buf), NULL);
	buf[lenof(buf)-1] = '\0';
	if (buf[strlen(buf)-1] == '\n')
	    buf[strlen(buf)-1] = '\0';
	fprintf(stderr, "Unable to read from standard input: %s\n", buf);
	cleanup_exit(0);
    }

    noise_ultralight(NOISE_SOURCE_IOLEN, len);
    if (backend_connected(backend)) {
	if (len > 0) {
            return backend_send(backend, data, len);
	} else {
            backend_special(backend, SS_EOF, 0);
	    return 0;
	}
    } else
	return 0;
}

void stdouterr_sent(struct handle *h, size_t new_backlog, int err)
{
    if (err) {
	char buf[4096];
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0,
		      buf, lenof(buf), NULL);
	buf[lenof(buf)-1] = '\0';
	if (buf[strlen(buf)-1] == '\n')
	    buf[strlen(buf)-1] = '\0';
	fprintf(stderr, "Unable to write to standard %s: %s\n",
		(h == stdout_handle ? "output" : "error"), buf);
	cleanup_exit(0);
    }

    if (backend_connected(backend)) {
        backend_unthrottle(backend, (handle_backlog(stdout_handle) +
                                     handle_backlog(stderr_handle)));
    }
}

const bool share_can_be_downstream = true;
const bool share_can_be_upstream = true;


#ifdef MOD_PORTKNOCKING
int ManagePortKnocking( char* host, char *portknockseq ) ;
#endif

#ifdef MOD_PERSO
char * AutoCommand = NULL ;
void ManageAutocommand( struct handle *h ) {
	char c = '\n' ;
	while( strlen(AutoCommand) > 0 ) {
		c = (char)AutoCommand[0] ;
		if( c=='\\' ) {
			if( AutoCommand[1]=='n' ) stdin_gotdata(h, "\n", 1, 0 ) ;
			AutoCommand++ ;
			c = '\n' ;
			}
		else stdin_gotdata(h, AutoCommand, 1, 0 ) ;
		AutoCommand++ ;
		}
	if( c != '\n' ) stdin_gotdata(h, "\n", 1, 0 ) ;
	}
	
int plink_main(int argc, char **argv) ;
	
int main(int argc, char **argv) {
	int arc=0, ret=0, i ;
	char **arv ;
	
	IniFileFlag = 0 ;
	DirectoryBrowseFlag = 0 ;
	
	arv=(char**)malloc( argc*sizeof(char*) ) ;
	for( i=0 ; i<argc ; i++ ) {
		if( !strcmp(argv[i],"-folder" ) && (i<(argc-1)) ) {
			i++ ;
			printf( "Switching folder to %s\n", argv[i] ) ;
			printf( "%s\n",SetSessPath( argv[i] ) );
			SetSessPath( argv[i] ) ;
			}
		else {
			arv[arc]=(char*)malloc( strlen(argv[i]) +1 ) ;
			strcpy(arv[arc],argv[i]) ;
			arc++ ;
			}
		}
	ret = plink_main( arc, arv ) ;
	for( i=0 ; i<arc ; i++ )  free( arv[i] ) ;
	free( arv ) ;
	return ret ;
	}
#endif

#ifdef MOD_PERSO
int plink_main(int argc, char **argv)
{
    LoadParametersLight() ;
#else
int main(int argc, char **argv)
{
#endif
    bool sending;
    SOCKET *sklist;
    size_t skcount, sksize;
    int exitcode;
    bool errors;
    bool use_subsystem = false;
    bool just_test_share_exists = false;
    enum TriState sanitise_stdout = AUTO, sanitise_stderr = AUTO;
    unsigned long now, next, then;
    const struct BackendVtable *vt;


    dll_hijacking_protection();

    sklist = NULL;
    skcount = sksize = 0;
    /*
     * Initialise port and protocol to sensible defaults. (These
     * will be overridden by more or less anything.)
     */
    default_protocol = PROT_SSH;
    default_port = 22;

    flags = 0;
    cmdline_tooltype |=
        (TOOLTYPE_HOST_ARG |
         TOOLTYPE_HOST_ARG_CAN_BE_SESSION |
         TOOLTYPE_HOST_ARG_PROTOCOL_PREFIX |
         TOOLTYPE_HOST_ARG_FROM_LAUNCHABLE_LOAD);

    /*
     * Process the command line.
     */
    conf = conf_new();
    do_defaults(NULL, conf);
    loaded_session = false;
    default_protocol = conf_get_int(conf, CONF_protocol);
    default_port = conf_get_int(conf, CONF_port);
    errors = false;
    {
	/*
	 * Override the default protocol if PLINK_PROTOCOL is set.
	 */
	char *p = getenv("PLINK_PROTOCOL");
	if (p) {
            const struct BackendVtable *vt = backend_vt_from_name(p);
            if (vt) {
                default_protocol = vt->protocol;
                default_port = vt->default_port;
		conf_set_int(conf, CONF_protocol, default_protocol);
		conf_set_int(conf, CONF_port, default_port);
	    }
	}
    }
    while (--argc) {
	char *p = *++argv;
        int ret = cmdline_process_param(p, (argc > 1 ? argv[1] : NULL),
                                        1, conf);
        if (ret == -2) {
            fprintf(stderr,
                    "plink: option \"%s\" requires an argument\n", p);
            errors = true;
        } else if (ret == 2) {
            --argc, ++argv;
        } else if (ret == 1) {
            continue;
        } else if (!strcmp(p, "-batch")) {
            console_batch_mode = true;
        } else if (!strcmp(p, "-s")) {
            /* Save status to write to conf later. */
            use_subsystem = true;
        } else if (!strcmp(p, "-V") || !strcmp(p, "--version")) {
            version();
        } else if (!strcmp(p, "--help")) {
            usage();
        } else if (!strcmp(p, "-pgpfp")) {
            pgp_fingerprints();
            exit(1);
        } else if (!strcmp(p, "-shareexists")) {
            just_test_share_exists = true;
        } else if (!strcmp(p, "-sanitise-stdout") ||
                   !strcmp(p, "-sanitize-stdout")) {
            sanitise_stdout = FORCE_ON;
        } else if (!strcmp(p, "-no-sanitise-stdout") ||
                   !strcmp(p, "-no-sanitize-stdout")) {
            sanitise_stdout = FORCE_OFF;
        } else if (!strcmp(p, "-sanitise-stderr") ||
                   !strcmp(p, "-sanitize-stderr")) {
            sanitise_stderr = FORCE_ON;
        } else if (!strcmp(p, "-no-sanitise-stderr") ||
                   !strcmp(p, "-no-sanitize-stderr")) {
            sanitise_stderr = FORCE_OFF;
        } else if (!strcmp(p, "-no-antispoof")) {
            console_antispoof_prompt = false;
	} else if (*p != '-') {
            strbuf *cmdbuf = strbuf_new();

            while (argc > 0) {
                if (cmdbuf->len > 0)
                    put_byte(cmdbuf, ' '); /* add space separator */
                put_datapl(cmdbuf, ptrlen_from_asciz(p));
                if (--argc > 0)
                    p = *++argv;
            }

            conf_set_str(conf, CONF_remote_cmd, cmdbuf->s);
            conf_set_str(conf, CONF_remote_cmd2, "");
            conf_set_bool(conf, CONF_nopty, true);  /* command => no tty */

            strbuf_free(cmdbuf);
            break;		       /* done with cmdline */
        } else {
            fprintf(stderr, "plink: unknown option \"%s\"\n", p);
            errors = true;
        }
    }

    if (errors)
	return 1;

    if (!cmdline_host_ok(conf)) {
	usage();
    }

    prepare_session(conf);

    /*
     * Perform command-line overrides on session configuration.
     */
    cmdline_run_saved(conf);

    /*
     * Apply subsystem status.
     */
    if (use_subsystem)
        conf_set_bool(conf, CONF_ssh_subsys, true);

    if (!*conf_get_str(conf, CONF_remote_cmd) &&
	!*conf_get_str(conf, CONF_remote_cmd2) &&
	!*conf_get_str(conf, CONF_ssh_nc_host))
	flags |= FLAG_INTERACTIVE;

    /*
     * Select protocol. This is farmed out into a table in a
     * separate file to enable an ssh-free variant.
     */
    vt = backend_vt_from_proto(conf_get_int(conf, CONF_protocol));
    if (vt == NULL) {
	fprintf(stderr,
		"Internal fault: Unsupported protocol found\n");
	return 1;
    }

    sk_init();
    if (p_WSAEventSelect == NULL) {
	fprintf(stderr, "Plink requires WinSock 2\n");
	return 1;
    }

    /*
     * Plink doesn't provide any way to add forwardings after the
     * connection is set up, so if there are none now, we can safely set
     * the "simple" flag.
     */
    if (conf_get_int(conf, CONF_protocol) == PROT_SSH &&
	!conf_get_bool(conf, CONF_x11_forward) &&
	!conf_get_bool(conf, CONF_agentfwd) &&
	!conf_get_str_nthstrkey(conf, CONF_portfwd, 0))
	conf_set_bool(conf, CONF_ssh_simple, true);

    logctx = log_init(default_logpolicy, conf);

    if (just_test_share_exists) {
        if (!vt->test_for_upstream) {
            fprintf(stderr, "Connection sharing not supported for connection "
                    "type '%s'\n", vt->name);
            return 1;
        }
        if (vt->test_for_upstream(conf_get_str(conf, CONF_host),
                                  conf_get_int(conf, CONF_port), conf))
            return 0;
        else
            return 1;
    }
#ifdef MOD_PORTKNOCKING
    ManagePortKnocking(conf_get_str(conf,CONF_host),conf_get_str(conf,CONF_portknockingoptions));
#endif

    if (restricted_acl) {
        lp_eventlog(default_logpolicy, "Running with restricted process ACL");
    }

    inhandle = GetStdHandle(STD_INPUT_HANDLE);
    outhandle = GetStdHandle(STD_OUTPUT_HANDLE);
    errhandle = GetStdHandle(STD_ERROR_HANDLE);

    /*
     * Turn off ECHO and LINE input modes. We don't care if this
     * call fails, because we know we aren't necessarily running in
     * a console.
     */
    GetConsoleMode(inhandle, &orig_console_mode);
    SetConsoleMode(inhandle, ENABLE_PROCESSED_INPUT);

    /*
     * Pass the output handles to the handle-handling subsystem.
     * (The input one we leave until we're through the
     * authentication process.)
     */
    stdout_handle = handle_output_new(outhandle, stdouterr_sent, NULL, 0);
    stderr_handle = handle_output_new(errhandle, stdouterr_sent, NULL, 0);
    handle_sink_init(&stdout_hs, stdout_handle);
    handle_sink_init(&stderr_hs, stderr_handle);
    stdout_bs = BinarySink_UPCAST(&stdout_hs);
    stderr_bs = BinarySink_UPCAST(&stderr_hs);

    /*
     * Decide whether to sanitise control sequences out of standard
     * output and standard error.
     *
     * If we weren't given a command-line override, we do this if (a)
     * the fd in question is pointing at a console, and (b) we aren't
     * trying to allocate a terminal as part of the session.
     *
     * (Rationale: the risk of control sequences is that they cause
     * confusion when sent to a local console, so if there isn't one,
     * no problem. Also, if we allocate a remote terminal, then we
     * sent a terminal type, i.e. we told it what kind of escape
     * sequences we _like_, i.e. we were expecting to receive some.)
     */
    if (sanitise_stdout == FORCE_ON ||
        (sanitise_stdout == AUTO && is_console_handle(outhandle) &&
         conf_get_bool(conf, CONF_nopty))) {
        stdout_scc = stripctrl_new(stdout_bs, true, L'\0');
        stdout_bs = BinarySink_UPCAST(stdout_scc);
    }
    if (sanitise_stderr == FORCE_ON ||
        (sanitise_stderr == AUTO && is_console_handle(errhandle) &&
         conf_get_bool(conf, CONF_nopty))) {
        stderr_scc = stripctrl_new(stderr_bs, true, L'\0');
        stderr_bs = BinarySink_UPCAST(stderr_scc);
    }

    /*
     * Start up the connection.
     */
    netevent = CreateEvent(NULL, false, false, NULL);
    {
        const char *error;
        char *realhost;
        /* nodelay is only useful if stdin is a character device (console) */
        bool nodelay = conf_get_bool(conf, CONF_tcp_nodelay) &&
            (GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_CHAR);

        error = backend_init(vt, plink_seat, &backend, logctx, conf,
                             conf_get_str(conf, CONF_host),
                             conf_get_int(conf, CONF_port),
                             &realhost, nodelay,
                             conf_get_bool(conf, CONF_tcp_keepalives));
        if (error) {
            fprintf(stderr, "Unable to open connection:\n%s", error);
            return 1;
        }
        sfree(realhost);
    }

    main_thread_id = GetCurrentThreadId();

    sending = false;

    now = GETTICKCOUNT();

    while (1) {
	int nhandles;
	HANDLE *handles;	
	int n;
	DWORD ticks;

        if (!sending && backend_sendok(backend)) {
	    stdin_handle = handle_input_new(inhandle, stdin_gotdata, NULL,
					    0);
	    sending = true;
	}

        if (toplevel_callback_pending()) {
            ticks = 0;
            next = now;
        } else if (run_timers(now, &next)) {
	    then = now;
	    now = GETTICKCOUNT();
	    if (now - then > next - then)
		ticks = 0;
	    else
		ticks = next - now;
	} else {
	    ticks = INFINITE;
            /* no need to initialise next here because we can never
             * get WAIT_TIMEOUT */
	}

	handles = handle_get_events(&nhandles);
	handles = sresize(handles, nhandles+1, HANDLE);
	handles[nhandles] = netevent;
	n = MsgWaitForMultipleObjects(nhandles+1, handles, false, ticks,
				      QS_POSTMESSAGE);
	if ((unsigned)(n - WAIT_OBJECT_0) < (unsigned)nhandles) {
	    handle_got_event(handles[n - WAIT_OBJECT_0]);
	} else if (n == WAIT_OBJECT_0 + nhandles) {
	    WSANETWORKEVENTS things;
	    SOCKET socket;
	    int i, socketstate;

	    /*
	     * We must not call select_result() for any socket
	     * until we have finished enumerating within the tree.
	     * This is because select_result() may close the socket
	     * and modify the tree.
	     */
	    /* Count the active sockets. */
	    i = 0;
	    for (socket = first_socket(&socketstate);
		 socket != INVALID_SOCKET;
		 socket = next_socket(&socketstate)) i++;

	    /* Expand the buffer if necessary. */
            sgrowarray(sklist, sksize, i);

	    /* Retrieve the sockets into sklist. */
	    skcount = 0;
	    for (socket = first_socket(&socketstate);
		 socket != INVALID_SOCKET;
		 socket = next_socket(&socketstate)) {
		sklist[skcount++] = socket;
	    }

	    /* Now we're done enumerating; go through the list. */
	    for (i = 0; i < skcount; i++) {
		WPARAM wp;
		socket = sklist[i];
		wp = (WPARAM) socket;
		if (!p_WSAEnumNetworkEvents(socket, NULL, &things)) {
                    static const struct { int bit, mask; } eventtypes[] = {
                        {FD_CONNECT_BIT, FD_CONNECT},
                        {FD_READ_BIT, FD_READ},
                        {FD_CLOSE_BIT, FD_CLOSE},
                        {FD_OOB_BIT, FD_OOB},
                        {FD_WRITE_BIT, FD_WRITE},
                        {FD_ACCEPT_BIT, FD_ACCEPT},
                    };
                    int e;

		    noise_ultralight(NOISE_SOURCE_IOID, socket);

                    for (e = 0; e < lenof(eventtypes); e++)
                        if (things.lNetworkEvents & eventtypes[e].mask) {
                            LPARAM lp;
                            int err = things.iErrorCode[eventtypes[e].bit];
                            lp = WSAMAKESELECTREPLY(eventtypes[e].mask, err);
                            select_result(wp, lp);
                        }
		}
	    }
	} else if (n == WAIT_OBJECT_0 + nhandles + 1) {
	    MSG msg;
	    while (PeekMessage(&msg, INVALID_HANDLE_VALUE,
			       WM_AGENT_CALLBACK, WM_AGENT_CALLBACK,
			       PM_REMOVE)) {
		struct agent_callback *c = (struct agent_callback *)msg.lParam;
		c->callback(c->callback_ctx, c->data, c->len);
		sfree(c);
	    }
	}

        run_toplevel_callbacks();

	if (n == WAIT_TIMEOUT) {
	    now = next;
	} else {
	    now = GETTICKCOUNT();
	}

	sfree(handles);

	if (sending)
            handle_unthrottle(stdin_handle, backend_sendbuffer(backend));

        if (!backend_connected(backend) &&
	    handle_backlog(stdout_handle) + handle_backlog(stderr_handle) == 0)
	    break;		       /* we closed the connection */
    }
    exitcode = backend_exitcode(backend);
    if (exitcode < 0) {
	fprintf(stderr, "Remote process exit code unavailable\n");
	exitcode = 1;		       /* this is an error condition */
    }
    cleanup_exit(exitcode);
    return 0;			       /* placate compiler warning */
}
