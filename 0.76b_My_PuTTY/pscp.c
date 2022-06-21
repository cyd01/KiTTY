/*
 * scp.c  -  Scp (Secure Copy) client for PuTTY.
 * Joris van Rantwijk, Simon Tatham
 *
 * This is mainly based on ssh-1.2.26/scp.c by Timo Rinne & Tatu Ylonen.
 * They, in turn, used stuff from BSD rcp.
 * 
 * (SGT, 2001-09-10: Joris van Rantwijk assures me that although
 * this file as originally submitted was inspired by, and
 * _structurally_ based on, ssh-1.2.26's scp.c, there wasn't any
 * actual code duplicated, so the above comment shouldn't give rise
 * to licensing issues.)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <assert.h>

#include "putty.h"
#include "psftp.h"
#include "ssh.h"
#include "sftp.h"
#include "storage.h"

static bool list = false;
static bool verbose = false;
static bool recursive = false;
static bool preserve = false;
static bool targetshouldbedirectory = false;
static bool statistics = true;
static int prev_stats_len = 0;
static bool scp_unsafe_mode = false;
static int errs = 0;
static bool try_scp = true;
static bool try_sftp = true;
static bool main_cmd_is_sftp = false;
static bool fallback_cmd_is_sftp = false;
static bool using_sftp = false;
static bool uploading = false;

static Backend *backend;
#ifdef MOD_PERSO
Conf *conf;
#else
static Conf *conf;
#endif
static bool sent_eof = false;

static void source(const char *src);
static void rsource(const char *src);
static void sink(const char *targ, const char *src);

#ifdef MOD_PERSO
void SetAutoStoreSSHKey( void ) ;
#if (defined MOD_PERSO) && (!defined FLJ)
char *appname = "PSCP";
#else
const char *const appname = "PSCP";
#endif
#ifdef MOD_PORTKNOCKING
int ManagePortKnocking( char* host, char *portstr ) ;
#endif
#endif

/*
 * The maximum amount of queued data we accept before we stop and
 * wait for the server to process some.
 */
#define MAX_SCP_BUFSIZE 16384

void ldisc_echoedit_update(Ldisc *ldisc) { }

static size_t pscp_output(Seat *, bool is_stderr, const void *, size_t);
static bool pscp_eof(Seat *);

static const SeatVtable pscp_seat_vt = {
    .output = pscp_output,
    .eof = pscp_eof,
    .get_userpass_input = filexfer_get_userpass_input,
    .notify_remote_exit = nullseat_notify_remote_exit,
    .connection_fatal = console_connection_fatal,
    .update_specials_menu = nullseat_update_specials_menu,
    .get_ttymode = nullseat_get_ttymode,
    .set_busy_status = nullseat_set_busy_status,
    .verify_ssh_host_key = console_verify_ssh_host_key,
    .confirm_weak_crypto_primitive = console_confirm_weak_crypto_primitive,
    .confirm_weak_cached_hostkey = console_confirm_weak_cached_hostkey,
    .is_utf8 = nullseat_is_never_utf8,
    .echoedit_update = nullseat_echoedit_update,
    .get_x_display = nullseat_get_x_display,
    .get_windowid = nullseat_get_windowid,
    .get_window_pixel_size = nullseat_get_window_pixel_size,
    .stripctrl_new = console_stripctrl_new,
    .set_trust_status = nullseat_set_trust_status_vacuously,
    .verbose = cmdline_seat_verbose,
    .interactive = nullseat_interactive_no,
    .get_cursor_position = nullseat_get_cursor_position,
};
static Seat pscp_seat[1] = {{ &pscp_seat_vt }};

static void tell_char(FILE *stream, char c)
{
    fputc(c, stream);
}

static void tell_str(FILE *stream, const char *str)
{
    unsigned int i;

    for (i = 0; i < strlen(str); ++i)
	tell_char(stream, str[i]);
}

static void abandon_stats(void)
{
    /*
     * Output a \n to stdout (which is where we've been sending
     * transfer statistics) so that the cursor will move to the next
     * line. We should do this before displaying any other kind of
     * output like an error message.
     */
    if (prev_stats_len) {
        putchar('\n');
        fflush(stdout);
        prev_stats_len = 0;
    }
}

static PRINTF_LIKE(2, 3) void tell_user(FILE *stream, const char *fmt, ...)
{
    char *str, *str2;
    va_list ap;
    va_start(ap, fmt);
    str = dupvprintf(fmt, ap);
    va_end(ap);
    str2 = dupcat(str, "\n");
    sfree(str);
    abandon_stats();
    tell_str(stream, str2);
    sfree(str2);
}

/*
 * Receive a block of data from the SSH link. Block until all data
 * is available.
 *
 * To do this, we repeatedly call the SSH protocol module, with our
 * own pscp_output() function to catch the data that comes back. We do
 * this until we have enough data.
 */

static bufchain received_data;
static BinarySink *stderr_bs;
static size_t pscp_output(
    Seat *seat, bool is_stderr, const void *data, size_t len)
{
    /*
     * stderr data is just spouted to local stderr (optionally via a
     * sanitiser) and otherwise ignored.
     */
    if (is_stderr) {
        put_data(stderr_bs, data, len);
	return 0;
    }

    bufchain_add(&received_data, data, len);
    return 0;
}
static bool pscp_eof(Seat *seat)
{
    /*
     * We usually expect to be the party deciding when to close the
     * connection, so if we see EOF before we sent it ourselves, we
     * should panic. The exception is if we're using old-style scp and
     * downloading rather than uploading.
     */
    if ((using_sftp || uploading) && !sent_eof) {
        seat_connection_fatal(
            pscp_seat, "Received unexpected end-of-file from server");
    }
    return false;
}
static bool ssh_scp_recv(void *vbuf, size_t len)
{
    char *buf = (char *)vbuf;
    while (len > 0) {
        while (bufchain_size(&received_data) == 0) {
            if (backend_exitcode(backend) >= 0 ||
                ssh_sftp_loop_iteration() < 0)
                return false;          /* doom */
        }

        size_t got = bufchain_fetch_consume_up_to(&received_data, buf, len);
        buf += got;
        len -= got;
    }

    return true;
}

/*
 * Loop through the ssh connection and authentication process.
 */
static void ssh_scp_init(void)
{
    while (!backend_sendok(backend)) {
        if (backend_exitcode(backend) >= 0) {
            errs++;
            return;
        }
	if (ssh_sftp_loop_iteration() < 0) {
            errs++;
	    return;		       /* doom */
        }
    }

    /* Work out which backend we ended up using. */
    if (!ssh_fallback_cmd(backend))
	using_sftp = main_cmd_is_sftp;
    else
	using_sftp = fallback_cmd_is_sftp;

    if (verbose) {
	if (using_sftp)
	    tell_user(stderr, "Using SFTP");
	else
	    tell_user(stderr, "Using SCP1");
    }
}

/*
 *  Print an error message and exit after closing the SSH link.
 */
static NORETURN PRINTF_LIKE(1, 2) void bump(const char *fmt, ...)
{
    char *str, *str2;
    va_list ap;
    va_start(ap, fmt);
    str = dupvprintf(fmt, ap);
    va_end(ap);
    str2 = dupcat(str, "\n");
    sfree(str);
    abandon_stats();
    tell_str(stderr, str2);
    sfree(str2);
    errs++;

    if (backend && backend_connected(backend)) {
	char ch;
        backend_special(backend, SS_EOF, 0);
        sent_eof = true;
	ssh_scp_recv(&ch, 1);
    }

    cleanup_exit(1);
}

/*
 * A nasty loop macro that lets me get an escape-sequence sanitised
 * version of a string for display, and free it automatically
 * afterwards.
 */
static StripCtrlChars *string_scc;
#define with_stripctrl(varname, input)                                  \
    for (char *varname = stripctrl_string(string_scc, input); varname;  \
         sfree(varname), varname = NULL)

/*
 * Wait for the reply to a single SFTP request. Parallels the same
 * function in psftp.c (but isn't centralised into sftp.c because the
 * latter module handles SFTP only and shouldn't assume that SFTP is
 * the only thing going on by calling seat_connection_fatal).
 */
struct sftp_packet *sftp_wait_for_reply(struct sftp_request *req)
{
    struct sftp_packet *pktin;
    struct sftp_request *rreq;

    sftp_register(req);
    pktin = sftp_recv();
    if (pktin == NULL) {
        seat_connection_fatal(
            pscp_seat, "did not receive SFTP response packet from server");
    }
    rreq = sftp_find_request(pktin);
    if (rreq != req) {
        seat_connection_fatal(
            pscp_seat,
            "unable to understand SFTP response packet from server: %s",
            fxp_error());
    }
    return pktin;
}

/*
 *  Open an SSH connection to user@host and execute cmd.
 */
static void do_cmd(char *host, char *user, char *cmd)
{
    const char *err;
    char *realhost;
    LogContext *logctx;

    if (host == NULL || host[0] == '\0')
	bump("Empty host name");

    /*
     * Remove a colon suffix.
     */
    host[host_strcspn(host, ":")] = '\0';

    /*
     * If we haven't loaded session details already (e.g., from -load),
     * try looking for a session called "host".
     */
    if (!cmdline_loaded_session()) {
	/* Try to load settings for `host' into a temporary config */
	Conf *conf2 = conf_new();
	conf_set_str(conf2, CONF_host, "");
	do_defaults(host, conf2);
	if (conf_get_str(conf2, CONF_host)[0] != '\0') {
	    /* Settings present and include hostname */
	    /* Re-load data into the real config. */
	    do_defaults(host, conf);
	} else {
	    /* Session doesn't exist or mention a hostname. */
	    /* Use `host' as a bare hostname. */
	    conf_set_str(conf, CONF_host, host);
	}
        conf_free(conf2);
    } else {
	/* Patch in hostname `host' to session details. */
	conf_set_str(conf, CONF_host, host);
    }

    /*
     * Force protocol to SSH if the user has somehow contrived to
     * select one we don't support (e.g. by loading an inappropriate
     * saved session). In that situation we assume the port number is
     * useless too.)
     */
    if (!backend_vt_from_proto(conf_get_int(conf, CONF_protocol))) {
        conf_set_int(conf, CONF_protocol, PROT_SSH);
        conf_set_int(conf, CONF_port, 22);
    }

    /*
     * Enact command-line overrides.
     */
    cmdline_run_saved(conf);

    /*
     * Muck about with the hostname in various ways.
     */
    {
	char *hostbuf = dupstr(conf_get_str(conf, CONF_host));
	char *host = hostbuf;
	char *p, *q;

	/*
	 * Trim leading whitespace.
	 */
	host += strspn(host, " \t");

	/*
	 * See if host is of the form user@host, and separate out
	 * the username if so.
	 */
	if (host[0] != '\0') {
	    char *atsign = strrchr(host, '@');
	    if (atsign) {
		*atsign = '\0';
		conf_set_str(conf, CONF_username, host);
		host = atsign + 1;
	    }
	}

	/*
	 * Remove any remaining whitespace.
	 */
	p = hostbuf;
	q = host;
	while (*q) {
	    if (*q != ' ' && *q != '\t')
		*p++ = *q;
	    q++;
	}
	*p = '\0';

	conf_set_str(conf, CONF_host, hostbuf);
	sfree(hostbuf);
    }

    /* Set username */
    if (user != NULL && user[0] != '\0') {
	conf_set_str(conf, CONF_username, user);
    } else if (conf_get_str(conf, CONF_username)[0] == '\0') {
	user = get_username();
	if (!user)
	    bump("Empty user name");
	else {
	    if (verbose)
		tell_user(stderr, "Guessing user name: %s", user);
	    conf_set_str(conf, CONF_username, user);
	    sfree(user);
	}
    }

    /*
     * Force protocol to SSH if the user has somehow contrived to
     * select one we don't support (e.g. by loading an inappropriate
     * saved session). In that situation we assume the port number is
     * useless too.)
     */
    if (!backend_vt_from_proto(conf_get_int(conf, CONF_protocol))) {
        conf_set_int(conf, CONF_protocol, PROT_SSH);
        conf_set_int(conf, CONF_port, 22);
    }

    /*
     * Disable scary things which shouldn't be enabled for simple
     * things like SCP and SFTP: agent forwarding, port forwarding,
     * X forwarding.
     */
    conf_set_bool(conf, CONF_x11_forward, false);
    conf_set_bool(conf, CONF_agentfwd, false);
    conf_set_bool(conf, CONF_ssh_simple, true);
    {
	char *key;
	while ((key = conf_get_str_nthstrkey(conf, CONF_portfwd, 0)) != NULL)
	    conf_del_str_str(conf, CONF_portfwd, key);
    }

    /*
     * Set up main and possibly fallback command depending on
     * options specified by user.
     * Attempt to start the SFTP subsystem as a first choice,
     * falling back to the provided scp command if that fails.
     */
    conf_set_str(conf, CONF_remote_cmd2, "");
    if (try_sftp) {
	/* First choice is SFTP subsystem. */
	main_cmd_is_sftp = true;
	conf_set_str(conf, CONF_remote_cmd, "sftp");
	conf_set_bool(conf, CONF_ssh_subsys, true);
	if (try_scp) {
	    /* Fallback is to use the provided scp command. */
	    fallback_cmd_is_sftp = false;
	    conf_set_str(conf, CONF_remote_cmd2, cmd);
	    conf_set_bool(conf, CONF_ssh_subsys2, false);
	} else {
	    /* Since we're not going to try SCP, we may as well try
	     * harder to find an SFTP server, since in the current
	     * implementation we have a spare slot. */
	    fallback_cmd_is_sftp = true;
	    /* see psftp.c for full explanation of this kludge */
	    conf_set_str(conf, CONF_remote_cmd2,
			 "test -x /usr/lib/sftp-server &&"
			 " exec /usr/lib/sftp-server\n"
			 "test -x /usr/local/lib/sftp-server &&"
			 " exec /usr/local/lib/sftp-server\n"
			 "exec sftp-server");
	    conf_set_bool(conf, CONF_ssh_subsys2, false);
	}
    } else {
	/* Don't try SFTP at all; just try the scp command. */
	main_cmd_is_sftp = false;
	conf_set_str(conf, CONF_remote_cmd, cmd);
	conf_set_bool(conf, CONF_ssh_subsys, false);
    }
    conf_set_bool(conf, CONF_nopty, true);

    logctx = log_init(console_cli_logpolicy, conf);

    platform_psftp_pre_conn_setup(console_cli_logpolicy);

#ifdef MOD_PORTKNOCKING
    ManagePortKnocking(conf_get_str(conf,CONF_host),conf_get_str(conf,CONF_portknockingoptions));
#endif

    err = backend_init(backend_vt_from_proto(
                           conf_get_int(conf, CONF_protocol)),
                       pscp_seat, &backend, logctx, conf,
                       conf_get_str(conf, CONF_host),
                       conf_get_int(conf, CONF_port),
                       &realhost, 0,
                       conf_get_bool(conf, CONF_tcp_keepalives));
    if (err != NULL)
	bump("ssh_init: %s", err);
    ssh_scp_init();
    if (verbose && realhost != NULL && errs == 0)
	tell_user(stderr, "Connected to %s", realhost);
    sfree(realhost);
}

/*
 *  Update statistic information about current file.
 */
static void print_stats(const char *name, uint64_t size, uint64_t done,
			time_t start, time_t now)
{
    float ratebs;
    unsigned long eta;
    char *etastr;
    int pct;
    int len;
    int elap;

    elap = (unsigned long) difftime(now, start);

    if (now > start)
	ratebs = (float)done / elap;
    else
	ratebs = (float)done;

    if (ratebs < 1.0)
	eta = size - done;
    else
        eta = (unsigned long)((size - done) / ratebs);

    etastr = dupprintf("%02ld:%02ld:%02ld",
		       eta / 3600, (eta % 3600) / 60, eta % 60);

    pct = (int) (100.0 * done / size);

    {
	/* divide by 1024 to provide kB */
	len = printf("\r%-25.25s | %"PRIu64" kB | %5.1f kB/s | "
                     "ETA: %8s | %3d%%", name, done >> 10,
                     ratebs / 1024.0, etastr, pct);
	if (len < prev_stats_len)
	    printf("%*s", prev_stats_len - len, "");
	prev_stats_len = len;

	if (done == size)
            abandon_stats();

	fflush(stdout);
    }

    free(etastr);
}

/*
 *  Find a colon in str and return a pointer to the colon.
 *  This is used to separate hostname from filename.
 */
static char *colon(char *str)
{
    /* We ignore a leading colon, since the hostname cannot be
       empty. We also ignore a colon as second character because
       of filenames like f:myfile.txt. */
    if (str[0] == '\0' || str[0] == ':' ||
        (str[0] != '[' && str[1] == ':'))
	return (NULL);
    str += host_strcspn(str, ":/\\");
    if (*str == ':')
	return (str);
    else
	return (NULL);
}

/*
 * Determine whether a string is entirely composed of dots.
 */
static bool is_dots(char *str)
{
    return str[strspn(str, ".")] == '\0';
}

/*
 *  Wait for a response from the other side.
 *  Return 0 if ok, -1 if error.
 */
static int response(void)
{
    char ch, resp, rbuf[2048];
    int p;

    if (!ssh_scp_recv(&resp, 1))
	bump("Lost connection");

    p = 0;
    switch (resp) {
      case 0:			       /* ok */
	return (0);
      default:
	rbuf[p++] = resp;
	/* fallthrough */
      case 1:			       /* error */
      case 2:			       /* fatal error */
	do {
	    if (!ssh_scp_recv(&ch, 1))
		bump("Protocol error: Lost connection");
	    rbuf[p++] = ch;
	} while (p < sizeof(rbuf) && ch != '\n');
	rbuf[p - 1] = '\0';
	if (resp == 1)
	    tell_user(stderr, "%s", rbuf);
	else
	    bump("%s", rbuf);
	errs++;
	return (-1);
    }
}

bool sftp_recvdata(char *buf, size_t len)
{
    return ssh_scp_recv(buf, len);
}
bool sftp_senddata(const char *buf, size_t len)
{
    backend_send(backend, buf, len);
    return true;
}
size_t sftp_sendbuffer(void)
{
    return backend_sendbuffer(backend);
}

/* ----------------------------------------------------------------------
 * sftp-based replacement for the hacky `pscp -ls'.
 */
void list_directory_from_sftp_warn_unsorted(void)
{
    fprintf(stderr,
            "Directory is too large to sort; writing file names unsorted\n");
}

void list_directory_from_sftp_print(struct fxp_name *name)
{
    with_stripctrl(san, name->longname)
        printf("%s\n", san);
}
void scp_sftp_listdir(const char *dirname)
{
    struct fxp_handle *dirh;
    struct fxp_names *names;
    struct sftp_packet *pktin;
    struct sftp_request *req;

    if (!fxp_init()) {
	tell_user(stderr, "unable to initialise SFTP: %s", fxp_error());
	errs++;
	return;
    }

    printf("Listing directory %s\n", dirname);

    req = fxp_opendir_send(dirname);
    pktin = sftp_wait_for_reply(req);
    dirh = fxp_opendir_recv(pktin, req);

    if (dirh == NULL) {
		tell_user(stderr, "Unable to open %s: %s\n", dirname, fxp_error());
		errs++;
    } else {
        struct list_directory_from_sftp_ctx *ctx =
            list_directory_from_sftp_new();

	while (1) {

	    req = fxp_readdir_send(dirh);
            pktin = sftp_wait_for_reply(req);
	    names = fxp_readdir_recv(pktin, req);

	    if (names == NULL) {
		if (fxp_error_type() == SSH_FX_EOF)
		    break;
		printf("Reading directory %s: %s\n", dirname, fxp_error());
		break;
	    }
	    if (names->nnames == 0) {
		fxp_free_names(names);
		break;
	    }

	    for (size_t i = 0; i < names->nnames; i++)
                list_directory_from_sftp_feed(ctx, &names->names[i]);

	    fxp_free_names(names);
	}
	req = fxp_close_send(dirh);
        pktin = sftp_wait_for_reply(req);
	fxp_close_recv(pktin, req);

        list_directory_from_sftp_finish(ctx);
        list_directory_from_sftp_free(ctx);
    }
}

/* ----------------------------------------------------------------------
 * Helper routines that contain the actual SCP protocol elements,
 * implemented both as SCP1 and SFTP.
 */

static struct scp_sftp_dirstack {
    struct scp_sftp_dirstack *next;
    struct fxp_name *names;
    int namepos, namelen;
    char *dirpath;
    char *wildcard;
    bool matched_something;     /* wildcard match set was non-empty */
} *scp_sftp_dirstack_head;
static char *scp_sftp_remotepath, *scp_sftp_currentname;
static char *scp_sftp_wildcard;
static bool scp_sftp_targetisdir, scp_sftp_donethistarget;
static bool scp_sftp_preserve, scp_sftp_recursive;
static unsigned long scp_sftp_mtime, scp_sftp_atime;
static bool scp_has_times;
static struct fxp_handle *scp_sftp_filehandle;
static struct fxp_xfer *scp_sftp_xfer;
static uint64_t scp_sftp_fileoffset;

int scp_source_setup(const char *target, bool shouldbedir)
{
    if (using_sftp) {
	/*
	 * Find out whether the target filespec is in fact a
	 * directory.
	 */
	struct sftp_packet *pktin;
	struct sftp_request *req;
	struct fxp_attrs attrs;
	bool ret;

	if (!fxp_init()) {
	    tell_user(stderr, "unable to initialise SFTP: %s", fxp_error());
	    errs++;
	    return 1;
	}

	req = fxp_stat_send(target);
        pktin = sftp_wait_for_reply(req);
	ret = fxp_stat_recv(pktin, req, &attrs);

	if (!ret || !(attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS))
	    scp_sftp_targetisdir = false;
	else
	    scp_sftp_targetisdir = (attrs.permissions & 0040000) != 0;

	if (shouldbedir && !scp_sftp_targetisdir) {
	    bump("pscp: remote filespec %s: not a directory\n", target);
	}

	scp_sftp_remotepath = dupstr(target);

	scp_has_times = false;
    } else {
	(void) response();
    }
    return 0;
}

int scp_send_errmsg(char *str)
{
    if (using_sftp) {
	/* do nothing; we never need to send our errors to the server */
    } else {
        backend_send(backend, "\001", 1);/* scp protocol error prefix */
        backend_send(backend, str, strlen(str));
    }
    return 0;			       /* can't fail */
}

int scp_send_filetimes(unsigned long mtime, unsigned long atime)
{
    if (using_sftp) {
	scp_sftp_mtime = mtime;
	scp_sftp_atime = atime;
	scp_has_times = true;
	return 0;
    } else {
	char buf[80];
	sprintf(buf, "T%lu 0 %lu 0\n", mtime, atime);
        backend_send(backend, buf, strlen(buf));
	return response();
    }
}

int scp_send_filename(const char *name, uint64_t size, int permissions)
{
    if (using_sftp) {
	char *fullname;
	struct sftp_packet *pktin;
	struct sftp_request *req;
        struct fxp_attrs attrs;

	if (scp_sftp_targetisdir) {
            fullname = dupcat(scp_sftp_remotepath, "/", name);
	} else {
	    fullname = dupstr(scp_sftp_remotepath);
	}

        attrs.flags = 0;
        PUT_PERMISSIONS(attrs, permissions);

	req = fxp_open_send(fullname,
                            SSH_FXF_WRITE | SSH_FXF_CREAT | SSH_FXF_TRUNC,
                            &attrs);
        pktin = sftp_wait_for_reply(req);
	scp_sftp_filehandle = fxp_open_recv(pktin, req);

	if (!scp_sftp_filehandle) {
	    tell_user(stderr, "pscp: unable to open %s: %s",
		      fullname, fxp_error());
            sfree(fullname);
	    errs++;
	    return 1;
	}
	scp_sftp_fileoffset = 0;
	scp_sftp_xfer = xfer_upload_init(scp_sftp_filehandle,
					 scp_sftp_fileoffset);
	sfree(fullname);
	return 0;
    } else {
	char *buf;
        if (permissions < 0)
            permissions = 0644;
	buf = dupprintf("C%04o %"PRIu64" ", (int)(permissions & 07777), size);
        backend_send(backend, buf, strlen(buf));
        sfree(buf);
        backend_send(backend, name, strlen(name));
        backend_send(backend, "\n", 1);
	return response();
    }
}

int scp_send_filedata(char *data, int len)
{
    if (using_sftp) {
	int ret;
	struct sftp_packet *pktin;

	if (!scp_sftp_filehandle) {
	    return 1;
	}

	while (!xfer_upload_ready(scp_sftp_xfer)) {
            if (toplevel_callback_pending()) {
                /* If we have pending callbacks, they might make
                 * xfer_upload_ready start to return true. So we should
                 * run them and then re-check xfer_upload_ready, before
                 * we go as far as waiting for an entire packet to
                 * arrive. */
                run_toplevel_callbacks();
                continue;
            }
	    pktin = sftp_recv();
	    ret = xfer_upload_gotpkt(scp_sftp_xfer, pktin);
	    if (ret <= 0) {
		tell_user(stderr, "error while writing: %s", fxp_error());
                if (ret == INT_MIN)        /* pktin not even freed */
                    sfree(pktin);
		errs++;
		return 1;
	    }
	}

	xfer_upload_data(scp_sftp_xfer, data, len);

	scp_sftp_fileoffset += len;
	return 0;
    } else {
        int bufsize = backend_send(backend, data, len);

	/*
	 * If the network transfer is backing up - that is, the
	 * remote site is not accepting data as fast as we can
	 * produce it - then we must loop on network events until
	 * we have space in the buffer again.
	 */
	while (bufsize > MAX_SCP_BUFSIZE) {
	    if (ssh_sftp_loop_iteration() < 0)
		return 1;
            bufsize = backend_sendbuffer(backend);
	}

	return 0;
    }
}

int scp_send_finish(void)
{
    if (using_sftp) {
	struct fxp_attrs attrs;
	struct sftp_packet *pktin;
	struct sftp_request *req;

	while (!xfer_done(scp_sftp_xfer)) {
	    pktin = sftp_recv();
	    int ret = xfer_upload_gotpkt(scp_sftp_xfer, pktin);
	    if (ret <= 0) {
		tell_user(stderr, "error while writing: %s", fxp_error());
                if (ret == INT_MIN)        /* pktin not even freed */
                    sfree(pktin);
		errs++;
		return 1;
	    }
	}
	xfer_cleanup(scp_sftp_xfer);

	if (!scp_sftp_filehandle) {
	    return 1;
	}
	if (scp_has_times) {
	    attrs.flags = SSH_FILEXFER_ATTR_ACMODTIME;
	    attrs.atime = scp_sftp_atime;
	    attrs.mtime = scp_sftp_mtime;
	    req = fxp_fsetstat_send(scp_sftp_filehandle, attrs);
            pktin = sftp_wait_for_reply(req);
	    bool ret = fxp_fsetstat_recv(pktin, req);
	    if (!ret) {
		tell_user(stderr, "unable to set file times: %s", fxp_error());
		errs++;
	    }
	}
	req = fxp_close_send(scp_sftp_filehandle);
        pktin = sftp_wait_for_reply(req);
	fxp_close_recv(pktin, req);
	scp_has_times = false;
	return 0;
    } else {
        backend_send(backend, "", 1);
	return response();
    }
}

char *scp_save_remotepath(void)
{
    if (using_sftp)
	return scp_sftp_remotepath;
    else
	return NULL;
}

void scp_restore_remotepath(char *data)
{
    if (using_sftp)
	scp_sftp_remotepath = data;
}

int scp_send_dirname(const char *name, int modes)
{
    if (using_sftp) {
	char *fullname;
	char const *err;
	struct fxp_attrs attrs;
	struct sftp_packet *pktin;
	struct sftp_request *req;
        bool ret;

	if (scp_sftp_targetisdir) {
            fullname = dupcat(scp_sftp_remotepath, "/", name);
	} else {
	    fullname = dupstr(scp_sftp_remotepath);
	}

	/*
	 * We don't worry about whether we managed to create the
	 * directory, because if it exists already it's OK just to
	 * use it. Instead, we will stat it afterwards, and if it
	 * exists and is a directory we will assume we were either
	 * successful or it didn't matter.
	 */
	req = fxp_mkdir_send(fullname, NULL);
        pktin = sftp_wait_for_reply(req);
	ret = fxp_mkdir_recv(pktin, req);

	if (!ret)
	    err = fxp_error();
	else
	    err = "server reported no error";

	req = fxp_stat_send(fullname);
        pktin = sftp_wait_for_reply(req);
	ret = fxp_stat_recv(pktin, req, &attrs);

	if (!ret || !(attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS) ||
	    !(attrs.permissions & 0040000)) {
	    tell_user(stderr, "unable to create directory %s: %s",
		      fullname, err);
            sfree(fullname);
	    errs++;
	    return 1;
	}

	scp_sftp_remotepath = fullname;

	return 0;
    } else {
	char buf[40];
	sprintf(buf, "D%04o 0 ", modes);
        backend_send(backend, buf, strlen(buf));
        backend_send(backend, name, strlen(name));
        backend_send(backend, "\n", 1);
	return response();
    }
}

int scp_send_enddir(void)
{
    if (using_sftp) {
	sfree(scp_sftp_remotepath);
	return 0;
    } else {
        backend_send(backend, "E\n", 2);
	return response();
    }
}

/*
 * Yes, I know; I have an scp_sink_setup _and_ an scp_sink_init.
 * That's bad. The difference is that scp_sink_setup is called once
 * right at the start, whereas scp_sink_init is called to
 * initialise every level of recursion in the protocol.
 */
int scp_sink_setup(const char *source, bool preserve, bool recursive)
{
    if (using_sftp) {
	char *newsource;

	if (!fxp_init()) {
	    tell_user(stderr, "unable to initialise SFTP: %s", fxp_error());
	    errs++;
	    return 1;
	}
	/*
	 * It's possible that the source string we've been given
	 * contains a wildcard. If so, we must split the directory
	 * away from the wildcard itself (throwing an error if any
	 * wildcardness comes before the final slash) and arrange
	 * things so that a dirstack entry will be set up.
	 */
	newsource = snewn(1+strlen(source), char);
	if (!wc_unescape(newsource, source)) {
	    /* Yes, here we go; it's a wildcard. Bah. */
	    char *dupsource, *lastpart, *dirpart, *wildcard;

	    sfree(newsource);

	    dupsource = dupstr(source);
	    lastpart = stripslashes(dupsource, false);
	    wildcard = dupstr(lastpart);
	    *lastpart = '\0';
	    if (*dupsource && dupsource[1]) {
		/*
		 * The remains of dupsource are at least two
		 * characters long, meaning the pathname wasn't
		 * empty or just `/'. Hence, we remove the trailing
		 * slash.
		 */
		lastpart[-1] = '\0';
	    } else if (!*dupsource) {
		/*
		 * The remains of dupsource are _empty_ - the whole
		 * pathname was a wildcard. Hence we need to
		 * replace it with ".".
		 */
		sfree(dupsource);
		dupsource = dupstr(".");
	    }

	    /*
	     * Now we have separated our string into dupsource (the
	     * directory part) and wildcard. Both of these will
	     * need freeing at some point. Next step is to remove
	     * wildcard escapes from the directory part, throwing
	     * an error if it contains a real wildcard.
	     */
	    dirpart = snewn(1+strlen(dupsource), char);
	    if (!wc_unescape(dirpart, dupsource)) {
		tell_user(stderr, "%s: multiple-level wildcards unsupported",
			  source);
		errs++;
		sfree(dirpart);
		sfree(wildcard);
		sfree(dupsource);
		return 1;
	    }

	    /*
	     * Now we have dirpart (unescaped, ie a valid remote
	     * path), and wildcard (a wildcard). This will be
	     * sufficient to arrange a dirstack entry.
	     */
	    scp_sftp_remotepath = dirpart;
	    scp_sftp_wildcard = wildcard;
	    sfree(dupsource);
	} else {
	    scp_sftp_remotepath = newsource;
	    scp_sftp_wildcard = NULL;
	}
	scp_sftp_preserve = preserve;
	scp_sftp_recursive = recursive;
	scp_sftp_donethistarget = false;
	scp_sftp_dirstack_head = NULL;
    }
    return 0;
}

int scp_sink_init(void)
{
    if (!using_sftp) {
        backend_send(backend, "", 1);
    }
    return 0;
}

#define SCP_SINK_FILE   1
#define SCP_SINK_DIR    2
#define SCP_SINK_ENDDIR 3
#define SCP_SINK_RETRY  4	       /* not an action; just try again */
struct scp_sink_action {
    int action;			       /* FILE, DIR, ENDDIR */
    strbuf *buf;                       /* will need freeing after use */
    char *name;			       /* filename or dirname (not ENDDIR) */
    long permissions;  	       /* access permissions (not ENDDIR) */
    uint64_t size;                     /* file size (not ENDDIR) */
    bool settime;                /* true if atime and mtime are filled */
    unsigned long atime, mtime;	       /* access times for the file */
};

int scp_get_sink_action(struct scp_sink_action *act)
{
    if (using_sftp) {
	char *fname;
	bool must_free_fname;
	struct fxp_attrs attrs;
	struct sftp_packet *pktin;
	struct sftp_request *req;
	bool ret;

	if (!scp_sftp_dirstack_head) {
	    if (!scp_sftp_donethistarget) {
		/*
		 * Simple case: we are only dealing with one file.
		 */
		fname = scp_sftp_remotepath;
		must_free_fname = false;
		scp_sftp_donethistarget = true;
	    } else {
		/*
		 * Even simpler case: one file _which we've done_.
		 * Return 1 (finished).
		 */
		return 1;
	    }
	} else {
	    /*
	     * We're now in the middle of stepping through a list
	     * of names returned from fxp_readdir(); so let's carry
	     * on.
	     */
	    struct scp_sftp_dirstack *head = scp_sftp_dirstack_head;
	    while (head->namepos < head->namelen &&
		   (is_dots(head->names[head->namepos].filename) ||
		    (head->wildcard &&
		     !wc_match(head->wildcard,
			       head->names[head->namepos].filename))))
		head->namepos++;       /* skip . and .. */
	    if (head->namepos < head->namelen) {
		head->matched_something = true;
		fname = dupcat(head->dirpath, "/",
                               head->names[head->namepos++].filename);
		must_free_fname = true;
	    } else {
		/*
		 * We've come to the end of the list; pop it off
		 * the stack and return an ENDDIR action (or RETRY
		 * if this was a wildcard match).
		 */
		if (head->wildcard) {
		    act->action = SCP_SINK_RETRY;
		    if (!head->matched_something) {
			tell_user(stderr, "pscp: wildcard '%s' matched "
				  "no files", head->wildcard);
			errs++;
		    }
		    sfree(head->wildcard);

		} else {
		    act->action = SCP_SINK_ENDDIR;
		}

		sfree(head->dirpath);
		sfree(head->names);
		scp_sftp_dirstack_head = head->next;
		sfree(head);

		return 0;
	    }
	}

	/*
	 * Now we have a filename. Stat it, and see if it's a file
	 * or a directory.
	 */
	req = fxp_stat_send(fname);
        pktin = sftp_wait_for_reply(req);
	ret = fxp_stat_recv(pktin, req, &attrs);

	if (!ret || !(attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS)) {
            with_stripctrl(san, fname)
                tell_user(stderr, "unable to identify %s: %s", san,
                          ret ? "file type not supplied" : fxp_error());
            if (must_free_fname) sfree(fname);
	    errs++;
	    return 1;
	}

	if (attrs.permissions & 0040000) {
	    struct scp_sftp_dirstack *newitem;
	    struct fxp_handle *dirhandle;
	    size_t nnames, namesize;
	    struct fxp_name *ournames;
	    struct fxp_names *names;

	    /*
	     * It's a directory. If we're not in recursive mode,
	     * this merits a complaint (which is fatal if the name
	     * was specified directly, but not if it was matched by
	     * a wildcard).
	     * 
	     * We skip this complaint completely if
	     * scp_sftp_wildcard is set, because that's an
	     * indication that we're not actually supposed to
	     * _recursively_ transfer the dir, just scan it for
	     * things matching the wildcard.
	     */
	    if (!scp_sftp_recursive && !scp_sftp_wildcard) {
                with_stripctrl(san, fname)
                    tell_user(stderr, "pscp: %s: is a directory", san);
		errs++;
		if (must_free_fname) sfree(fname);
		if (scp_sftp_dirstack_head) {
		    act->action = SCP_SINK_RETRY;
		    return 0;
		} else {
		    return 1;
		}
	    }

	    /*
	     * Otherwise, the fun begins. We must fxp_opendir() the
	     * directory, slurp the filenames into memory, return
	     * SCP_SINK_DIR (unless this is a wildcard match), and
	     * set targetisdir. The next time we're called, we will
	     * run through the list of filenames one by one,
	     * matching them against a wildcard if present.
	     * 
	     * If targetisdir is _already_ set (meaning we're
	     * already in the middle of going through another such
	     * list), we must push the other (target,namelist) pair
	     * on a stack.
	     */
	    req = fxp_opendir_send(fname);
            pktin = sftp_wait_for_reply(req);
	    dirhandle = fxp_opendir_recv(pktin, req);

	    if (!dirhandle) {
                with_stripctrl(san, fname)
                    tell_user(stderr, "pscp: unable to open directory %s: %s",
                              san, fxp_error());
		if (must_free_fname) sfree(fname);
		errs++;
		return 1;
	    }
	    nnames = namesize = 0;
	    ournames = NULL;
	    while (1) {
		int i;

		req = fxp_readdir_send(dirhandle);
                pktin = sftp_wait_for_reply(req);
		names = fxp_readdir_recv(pktin, req);

		if (names == NULL) {
		    if (fxp_error_type() == SSH_FX_EOF)
			break;
                    with_stripctrl(san, fname)
                        tell_user(stderr, "pscp: reading directory %s: %s",
                                  san, fxp_error());

                    req = fxp_close_send(dirhandle);
                    pktin = sftp_wait_for_reply(req);
                    fxp_close_recv(pktin, req);

		    if (must_free_fname) sfree(fname);
		    sfree(ournames);
		    errs++;
		    return 1;
		}
		if (names->nnames == 0) {
		    fxp_free_names(names);
		    break;
		}
                sgrowarrayn(ournames, namesize, nnames, names->nnames);
		for (i = 0; i < names->nnames; i++) {
		    if (!strcmp(names->names[i].filename, ".") ||
			!strcmp(names->names[i].filename, "..")) {
			/*
			 * . and .. are normal consequences of
			 * reading a directory, and aren't worth
			 * complaining about.
			 */
		    } else if (!vet_filename(names->names[i].filename)) {
                        with_stripctrl(san, names->names[i].filename)
                            tell_user(stderr, "ignoring potentially dangerous "
                                      "server-supplied filename '%s'", san);
		    } else
			ournames[nnames++] = names->names[i];
		}
		names->nnames = 0;	       /* prevent free_names */
		fxp_free_names(names);
	    }
	    req = fxp_close_send(dirhandle);
            pktin = sftp_wait_for_reply(req);
	    fxp_close_recv(pktin, req);

	    newitem = snew(struct scp_sftp_dirstack);
	    newitem->next = scp_sftp_dirstack_head;
	    newitem->names = ournames;
	    newitem->namepos = 0;
	    newitem->namelen = nnames;
	    if (must_free_fname)
		newitem->dirpath = fname;
	    else
		newitem->dirpath = dupstr(fname);
	    if (scp_sftp_wildcard) {
		newitem->wildcard = scp_sftp_wildcard;
		newitem->matched_something = false;
		scp_sftp_wildcard = NULL;
	    } else {
		newitem->wildcard = NULL;
	    }
	    scp_sftp_dirstack_head = newitem;

	    if (newitem->wildcard) {
		act->action = SCP_SINK_RETRY;
	    } else {
		act->action = SCP_SINK_DIR;
                strbuf_clear(act->buf);
                put_asciz(act->buf, stripslashes(fname, false));
		act->name = act->buf->s;
		act->size = 0;     /* duhh, it's a directory */
		act->permissions = 07777 & attrs.permissions;
		if (scp_sftp_preserve &&
		    (attrs.flags & SSH_FILEXFER_ATTR_ACMODTIME)) {
		    act->atime = attrs.atime;
		    act->mtime = attrs.mtime;
		    act->settime = true;
		} else
		    act->settime = false;
	    }
	    return 0;

	} else {
	    /*
	     * It's a file. Return SCP_SINK_FILE.
	     */
	    act->action = SCP_SINK_FILE;
            strbuf_clear(act->buf);
            put_asciz(act->buf, stripslashes(fname, false));
	    act->name = act->buf->s;
	    if (attrs.flags & SSH_FILEXFER_ATTR_SIZE) {
		act->size = attrs.size;
	    } else
		act->size = UINT64_MAX;   /* no idea */
	    act->permissions = 07777 & attrs.permissions;
	    if (scp_sftp_preserve &&
		(attrs.flags & SSH_FILEXFER_ATTR_ACMODTIME)) {
		act->atime = attrs.atime;
		act->mtime = attrs.mtime;
		act->settime = true;
	    } else
		act->settime = false;
	    if (must_free_fname)
		scp_sftp_currentname = fname;
	    else
		scp_sftp_currentname = dupstr(fname);
	    return 0;
	}

    } else {
	bool done = false;
	int action;
	char ch;

	act->settime = false;
        strbuf_clear(act->buf);

	while (!done) {
	    if (!ssh_scp_recv(&ch, 1))
		return 1;
	    if (ch == '\n')
		bump("Protocol error: Unexpected newline");
	    action = ch;
            while (1) {
		if (!ssh_scp_recv(&ch, 1))
		    bump("Lost connection");
                if (ch == '\n')
                    break;
                put_byte(act->buf, ch);
            }
	    switch (action) {
	      case '\01':		       /* error */
                with_stripctrl(san, act->buf->s)
                    tell_user(stderr, "%s", san);
		errs++;
		continue;		       /* go round again */
	      case '\02':		       /* fatal error */
                with_stripctrl(san, act->buf->s)
                    bump("%s", san);
	      case 'E':
                backend_send(backend, "", 1);
		act->action = SCP_SINK_ENDDIR;
		return 0;
	      case 'T':
		if (sscanf(act->buf->s, "%lu %*d %lu %*d",
			   &act->mtime, &act->atime) == 2) {
		    act->settime = true;
                    backend_send(backend, "", 1);
                    strbuf_clear(act->buf);
		    continue;	       /* go round again */
		}
		bump("Protocol error: Illegal time format");
	      case 'C':
	      case 'D':
		act->action = (action == 'C' ? SCP_SINK_FILE : SCP_SINK_DIR);
                if (act->action == SCP_SINK_DIR && !recursive) {
                    bump("security violation: remote host attempted to create "
                         "a subdirectory in a non-recursive copy!");
                }
		break;
	      default:
		bump("Protocol error: Expected control record");
	    }
	    /*
	     * We will go round this loop only once, unless we hit
	     * `continue' above.
	     */
	    done = true;
	}

	/*
	 * If we get here, we must have seen SCP_SINK_FILE or
	 * SCP_SINK_DIR.
	 */
	{
            int i;
            if (sscanf(act->buf->s, "%lo %"SCNu64" %n", &act->permissions,
                       &act->size, &i) != 2)
		bump("Protocol error: Illegal file descriptor format");
	    act->name = act->buf->s + i;
	    return 0;
	}
    }
}

int scp_accept_filexfer(void)
{
    if (using_sftp) {
	struct sftp_packet *pktin;
	struct sftp_request *req;

	req = fxp_open_send(scp_sftp_currentname, SSH_FXF_READ, NULL);
        pktin = sftp_wait_for_reply(req);
	scp_sftp_filehandle = fxp_open_recv(pktin, req);

	if (!scp_sftp_filehandle) {
            with_stripctrl(san, scp_sftp_currentname)
                tell_user(stderr, "pscp: unable to open %s: %s",
                          san, fxp_error());
	    errs++;
	    return 1;
	}
	scp_sftp_fileoffset = 0;
	scp_sftp_xfer = xfer_download_init(scp_sftp_filehandle,
					   scp_sftp_fileoffset);
	sfree(scp_sftp_currentname);
	return 0;
    } else {
        backend_send(backend, "", 1);
	return 0;		       /* can't fail */
    }
}

int scp_recv_filedata(char *data, int len)
{
    if (using_sftp) {
	struct sftp_packet *pktin;
	int ret, actuallen;
	void *vbuf;

	xfer_download_queue(scp_sftp_xfer);
	pktin = sftp_recv();
	ret = xfer_download_gotpkt(scp_sftp_xfer, pktin);
	if (ret <= 0) {
	    tell_user(stderr, "pscp: error while reading: %s", fxp_error());
            if (ret == INT_MIN)        /* pktin not even freed */
                sfree(pktin);
	    errs++;
	    return -1;
	}

	if (xfer_download_data(scp_sftp_xfer, &vbuf, &actuallen)) {
            if (actuallen <= 0) {
                tell_user(stderr, "pscp: end of file while reading");
                errs++;
                sfree(vbuf);
                return -1;
            }
	    /*
	     * This assertion relies on the fact that the natural
	     * block size used in the xfer manager is at most that
	     * used in this module. I don't like crossing layers in
	     * this way, but it'll do for now.
	     */
	    assert(actuallen <= len);
	    memcpy(data, vbuf, actuallen);
	    sfree(vbuf);
	} else
	    actuallen = 0;

	scp_sftp_fileoffset += actuallen;

	return actuallen;
    } else {
	return ssh_scp_recv(data, len) ? len : 0;
    }
}

int scp_finish_filerecv(void)
{
    if (using_sftp) {
	struct sftp_packet *pktin;
	struct sftp_request *req;

	/*
	 * Ensure that xfer_done() will work correctly, so we can
	 * clean up any outstanding requests from the file
	 * transfer.
	 */
	xfer_set_error(scp_sftp_xfer);
	while (!xfer_done(scp_sftp_xfer)) {
	    void *vbuf;
	    int ret, len;

	    pktin = sftp_recv();
	    ret = xfer_download_gotpkt(scp_sftp_xfer, pktin);
            if (ret <= 0) {
                tell_user(stderr, "pscp: error while reading: %s", fxp_error());
                if (ret == INT_MIN)        /* pktin not even freed */
                    sfree(pktin);
                errs++;
                return -1;
            }
	    if (xfer_download_data(scp_sftp_xfer, &vbuf, &len))
		sfree(vbuf);
	}
	xfer_cleanup(scp_sftp_xfer);

	req = fxp_close_send(scp_sftp_filehandle);
        pktin = sftp_wait_for_reply(req);
	fxp_close_recv(pktin, req);
	return 0;
    } else {
        backend_send(backend, "", 1);
	return response();
    }
}

/* ----------------------------------------------------------------------
 *  Send an error message to the other side and to the screen.
 *  Increment error counter.
 */
static PRINTF_LIKE(1, 2) void run_err(const char *fmt, ...)
{
    char *str, *str2;
    va_list ap;
    va_start(ap, fmt);
    errs++;
    str = dupvprintf(fmt, ap);
    str2 = dupcat("pscp: ", str, "\n");
    sfree(str);
    scp_send_errmsg(str2);
    abandon_stats();
    tell_user(stderr, "%s", str2);
    va_end(ap);
    sfree(str2);
}

/*
 *  Execute the source part of the SCP protocol.
 */
static void source(const char *src)
{
    uint64_t size;
    unsigned long mtime, atime;
    long permissions;
    const char *last;
    RFile *f;
    int attr;
    uint64_t i;
    uint64_t stat_bytes;
    time_t stat_starttime, stat_lasttime;

    attr = file_type(src);
    if (attr == FILE_TYPE_NONEXISTENT ||
	attr == FILE_TYPE_WEIRD) {
	run_err("%s: %s file or directory", src,
		(attr == FILE_TYPE_WEIRD ? "Not a" : "No such"));
	return;
    }

    if (attr == FILE_TYPE_DIRECTORY) {
	if (recursive) {
	    /*
	     * Avoid . and .. directories.
	     */
	    const char *p;
	    p = strrchr(src, '/');
	    if (!p)
		p = strrchr(src, '\\');
	    if (!p)
		p = src;
	    else
		p++;
	    if (!strcmp(p, ".") || !strcmp(p, ".."))
		/* skip . and .. */ ;
	    else
		rsource(src);
	} else {
	    run_err("%s: not a regular file", src);
	}
	return;
    }

    if ((last = strrchr(src, '/')) == NULL)
	last = src;
    else
	last++;
    if (strrchr(last, '\\') != NULL)
	last = strrchr(last, '\\') + 1;
    if (last == src && strchr(src, ':') != NULL)
	last = strchr(src, ':') + 1;

    f = open_existing_file(src, &size, &mtime, &atime, &permissions);
    if (f == NULL) {
	run_err("%s: Cannot open file", src);
	return;
    }
    if (preserve) {
	if (scp_send_filetimes(mtime, atime)) {
            close_rfile(f);
	    return;
        }
    }

    if (verbose) {
	tell_user(stderr, "Sending file %s, size=%"PRIu64, last, size);
    }
    if (scp_send_filename(last, size, permissions)) {
        close_rfile(f);
	return;
    }

    stat_bytes = 0;
    stat_starttime = time(NULL);
    stat_lasttime = 0;

#define PSCP_SEND_BLOCK 4096
    for (i = 0; i < size; i += PSCP_SEND_BLOCK) {
	char transbuf[PSCP_SEND_BLOCK];
	int j, k = PSCP_SEND_BLOCK;

	if (i + k > size)
	    k = size - i;
	if ((j = read_from_file(f, transbuf, k)) != k) {
	    bump("%s: Read error", src);
	}
	if (scp_send_filedata(transbuf, k))
	    bump("%s: Network error occurred", src);

	if (statistics) {
	    stat_bytes += k;
	    if (time(NULL) != stat_lasttime || i + k == size) {
		stat_lasttime = time(NULL);
		print_stats(last, size, stat_bytes,
			    stat_starttime, stat_lasttime);
	    }
	}

    }
    close_rfile(f);

    (void) scp_send_finish();
}

/*
 *  Recursively send the contents of a directory.
 */
static void rsource(const char *src)
{
    const char *last;
    char *save_target;
    DirHandle *dir;

    if ((last = strrchr(src, '/')) == NULL)
	last = src;
    else
	last++;
    if (strrchr(last, '\\') != NULL)
	last = strrchr(last, '\\') + 1;
    if (last == src && strchr(src, ':') != NULL)
	last = strchr(src, ':') + 1;

    /* maybe send filetime */

    save_target = scp_save_remotepath();

    if (verbose)
	tell_user(stderr, "Entering directory: %s", last);
    if (scp_send_dirname(last, 0755))
	return;

    const char *opendir_err;
    dir = open_directory(src, &opendir_err);
    if (dir != NULL) {
	char *filename;
	while ((filename = read_filename(dir)) != NULL) {
            char *foundfile = dupcat(src, "/", filename);
	    source(foundfile);
	    sfree(foundfile);
	    sfree(filename);
	}
        close_directory(dir);
    } else {
        tell_user(stderr, "Error opening directory %s: %s", src, opendir_err);
    }

    (void) scp_send_enddir();

    scp_restore_remotepath(save_target);
}

/*
 * Execute the sink part of the SCP protocol.
 */
static void sink(const char *targ, const char *src)
{
    char *destfname;
    bool targisdir = false;
    bool exists;
    int attr;
    WFile *f;
    uint64_t received;
    bool wrerror = false;
    uint64_t stat_bytes;
    time_t stat_starttime, stat_lasttime;
    char *stat_name;

    attr = file_type(targ);
    if (attr == FILE_TYPE_DIRECTORY)
	targisdir = true;

    if (targetshouldbedirectory && !targisdir)
	bump("%s: Not a directory", targ);

    scp_sink_init();

    struct scp_sink_action act;
    act.buf = strbuf_new();

    while (1) {

	if (scp_get_sink_action(&act))
            goto out;

	if (act.action == SCP_SINK_ENDDIR)
            goto out;

	if (act.action == SCP_SINK_RETRY)
	    continue;

	if (targisdir) {
	    /*
	     * Prevent the remote side from maliciously writing to
	     * files outside the target area by sending a filename
	     * containing `../'. In fact, it shouldn't be sending
	     * filenames with any slashes or colons in at all; so
	     * we'll find the last slash, backslash or colon in the
	     * filename and use only the part after that. (And
	     * warn!)
	     * 
	     * In addition, we also ensure here that if we're
	     * copying a single file and the target is a directory
	     * (common usage: `pscp host:filename .') the remote
	     * can't send us a _different_ file name. We can
	     * distinguish this case because `src' will be non-NULL
	     * and the last component of that will fail to match
	     * (the last component of) the name sent.
	     * 
	     * Well, not always; if `src' is a wildcard, we do
	     * expect to get back filenames that don't correspond
	     * exactly to it. Ideally in this case, we would like
	     * to ensure that the returned filename actually
	     * matches the wildcard pattern - but one of SCP's
	     * protocol infelicities is that wildcard matching is
	     * done at the server end _by the server's rules_ and
	     * so in general this is infeasible. Hence, we only
	     * accept filenames that don't correspond to `src' if
	     * unsafe mode is enabled or we are using SFTP (which
	     * resolves remote wildcards on the client side and can
	     * be trusted).
	     */
	    char *striptarget, *stripsrc;

	    striptarget = stripslashes(act.name, true);
	    if (striptarget != act.name) {
                with_stripctrl(sanname, act.name) {
                    with_stripctrl(santarg, act.name) {
                        tell_user(stderr, "warning: remote host sent a"
                                  " compound pathname '%s'", sanname);
                        tell_user(stderr, "         renaming local"
                                  " file to '%s'", santarg);
                    }
                }
	    }

	    /*
	     * Also check to see if the target filename is '.' or
	     * '..', or indeed '...' and so on because Windows
	     * appears to interpret those like '..'.
	     */
	    if (is_dots(striptarget)) {
		bump("security violation: remote host attempted to write to"
		     " a '.' or '..' path!");
	    }

	    if (src) {
		stripsrc = stripslashes(src, true);
		if (strcmp(striptarget, stripsrc) &&
		    !using_sftp && !scp_unsafe_mode) {
                    with_stripctrl(san, striptarget)
                        tell_user(stderr, "warning: remote host tried to "
                                  "write  to a file called '%s'", san);
		    tell_user(stderr, "         when we requested a file "
			      "called '%s'.", stripsrc);
		    tell_user(stderr, "         If this is a wildcard, "
			      "consider upgrading to SSH-2 or using");
		    tell_user(stderr, "         the '-unsafe' option. Renaming"
			      " of this file has been disallowed.");
		    /* Override the name the server provided with our own. */
		    striptarget = stripsrc;
		}
	    }

	    if (targ[0] != '\0')
		destfname = dir_file_cat(targ, striptarget);
	    else
		destfname = dupstr(striptarget);
	} else {
	    /*
	     * In this branch of the if, the target area is a
	     * single file with an explicitly specified name in any
	     * case, so there's no danger.
	     */
	    destfname = dupstr(targ);
	}
	attr = file_type(destfname);
	exists = (attr != FILE_TYPE_NONEXISTENT);

	if (act.action == SCP_SINK_DIR) {
	    if (exists && attr != FILE_TYPE_DIRECTORY) {
                with_stripctrl(san, destfname)
                    run_err("%s: Not a directory", san);
                sfree(destfname);
		continue;
	    }
	    if (!exists) {
		if (!create_directory(destfname)) {
                    with_stripctrl(san, destfname)
                        run_err("%s: Cannot create directory", san);
                    sfree(destfname);
		    continue;
		}
	    }
	    sink(destfname, NULL);
	    /* can we set the timestamp for directories ? */
            sfree(destfname);
	    continue;
	}

	f = open_new_file(destfname, act.permissions);
	if (f == NULL) {
            with_stripctrl(san, destfname)
                run_err("%s: Cannot create file", san);
            sfree(destfname);
	    continue;
	}

	if (scp_accept_filexfer()) {
            sfree(destfname);
            close_wfile(f);
	    goto out;
        }

	stat_bytes = 0;
	stat_starttime = time(NULL);
	stat_lasttime = 0;
        stat_name = stripctrl_string(
            string_scc, stripslashes(destfname, true));

	received = 0;
	while (received < act.size) {
	    char transbuf[32768];
	    uint64_t blksize;
	    int read;
	    blksize = 32768;
	    if (blksize > act.size - received)
                blksize = act.size - received;
	    read = scp_recv_filedata(transbuf, (int)blksize);
	    if (read <= 0)
		bump("Lost connection");
	    if (wrerror) {
                received += read;
		continue;
            }
	    if (write_to_file(f, transbuf, read) != (int)read) {
		wrerror = true;
		/* FIXME: in sftp we can actually abort the transfer */
		if (statistics)
		    printf("\r%-25.25s | %50s\n",
			   stat_name,
			   "Write error.. waiting for end of file");
                received += read;
		continue;
	    }
	    if (statistics) {
		stat_bytes += read;
		if (time(NULL) > stat_lasttime ||
                    received + read == act.size) {
		    stat_lasttime = time(NULL);
		    print_stats(stat_name, act.size, stat_bytes,
				stat_starttime, stat_lasttime);
		}
	    }
	    received += read;
	}
	if (act.settime) {
	    set_file_times(f, act.mtime, act.atime);
	}

	close_wfile(f);
	if (wrerror) {
            with_stripctrl(san, destfname)
                run_err("%s: Write error", san);
            sfree(destfname);
	    continue;
	}
	(void) scp_finish_filerecv();
	sfree(stat_name);
	sfree(destfname);
    }
  out:
    strbuf_free(act.buf);
}

/*
 * We will copy local files to a remote server.
 */
static void toremote(int argc, char *argv[])
{
    char *src, *wtarg, *host, *user;
    const char *targ;
    char *cmd;
    int i, wc_type;

    uploading = true;

    wtarg = argv[argc - 1];

    /* Separate host from filename */
    host = wtarg;
    wtarg = colon(wtarg);
    if (wtarg == NULL)
	bump("wtarg == NULL in toremote()");
    *wtarg++ = '\0';
    /* Substitute "." for empty target */
    if (*wtarg == '\0')
	targ = ".";
    else
        targ = wtarg;

    /* Separate host and username */
    user = host;
    host = strrchr(host, '@');
    if (host == NULL) {
	host = user;
	user = NULL;
    } else {
	*host++ = '\0';
	if (*user == '\0')
	    user = NULL;
    }

    if (argc == 2) {
	if (colon(argv[0]) != NULL)
	    bump("%s: Remote to remote not supported", argv[0]);

	wc_type = test_wildcard(argv[0], true);
	if (wc_type == WCTYPE_NONEXISTENT)
	    bump("%s: No such file or directory\n", argv[0]);
	else if (wc_type == WCTYPE_WILDCARD)
	    targetshouldbedirectory = true;
    }

    cmd = dupprintf("scp%s%s%s%s -t %s",
		    verbose ? " -v" : "",
		    recursive ? " -r" : "",
		    preserve ? " -p" : "",
		    targetshouldbedirectory ? " -d" : "", targ);
    do_cmd(host, user, cmd);
    sfree(cmd);

    if (scp_source_setup(targ, targetshouldbedirectory))
	return;

    for (i = 0; i < argc - 1; i++) {
	src = argv[i];
	if (colon(src) != NULL) {
	    tell_user(stderr, "%s: Remote to remote not supported\n", src);
	    errs++;
	    continue;
	}

	wc_type = test_wildcard(src, true);
	if (wc_type == WCTYPE_NONEXISTENT) {
	    run_err("%s: No such file or directory", src);
	    continue;
	} else if (wc_type == WCTYPE_FILENAME) {
	    source(src);
	    continue;
	} else {
	    WildcardMatcher *wc;
	    char *filename;

	    wc = begin_wildcard_matching(src);
	    if (wc == NULL) {
		run_err("%s: No such file or directory", src);
		continue;
	    }

	    while ((filename = wildcard_get_filename(wc)) != NULL) {
		source(filename);
		sfree(filename);
	    }

	    finish_wildcard_matching(wc);
	}
    }
}

/*
 *  We will copy files from a remote server to the local machine.
 */
static void tolocal(int argc, char *argv[])
{
    char *wsrc, *host, *user;
    const char *src, *targ;
    char *cmd;

    uploading = false;

    if (argc != 2)
	bump("More than one remote source not supported");

    wsrc = argv[0];
    targ = argv[1];

    /* Separate host from filename */
    host = wsrc;
    wsrc = colon(wsrc);
    if (wsrc == NULL)
	bump("Local to local copy not supported");
    *wsrc++ = '\0';
    /* Substitute "." for empty filename */
    if (*wsrc == '\0')
	src = ".";
    else
        src = wsrc;

    /* Separate username and hostname */
    user = host;
    host = strrchr(host, '@');
    if (host == NULL) {
	host = user;
	user = NULL;
    } else {
	*host++ = '\0';
	if (*user == '\0')
	    user = NULL;
    }

    cmd = dupprintf("scp%s%s%s%s -f %s",
		    verbose ? " -v" : "",
		    recursive ? " -r" : "",
		    preserve ? " -p" : "",
		    targetshouldbedirectory ? " -d" : "", src);
    do_cmd(host, user, cmd);
    sfree(cmd);

    if (scp_sink_setup(src, preserve, recursive))
	return;

    sink(targ, src);
}

/*
 *  We will issue a list command to get a remote directory.
 */
static void get_dir_list(int argc, char *argv[])
{
    char *wsrc, *host, *user;
    const char *src;
    char *cmd, *p;
    const char *q;
    char c;

    wsrc = argv[0];

    /* Separate host from filename */
    host = wsrc;
    wsrc = colon(wsrc);
    if (wsrc == NULL)
	bump("Local file listing not supported");
    *wsrc++ = '\0';
    /* Substitute "." for empty filename */
    if (*wsrc == '\0')
	src = ".";
    else
        src = wsrc;

    /* Separate username and hostname */
    user = host;
    host = strrchr(host, '@');
    if (host == NULL) {
	host = user;
	user = NULL;
    } else {
	*host++ = '\0';
	if (*user == '\0')
	    user = NULL;
    }

    cmd = snewn(4 * strlen(src) + 100, char);
    strcpy(cmd, "ls -la '");
    p = cmd + strlen(cmd);
    for (q = src; *q; q++) {
	if (*q == '\'') {
	    *p++ = '\'';
	    *p++ = '\\';
	    *p++ = '\'';
	    *p++ = '\'';
	} else {
	    *p++ = *q;
	}
    }
    *p++ = '\'';
    *p = '\0';

    do_cmd(host, user, cmd);
    sfree(cmd);

    if (using_sftp) {
	scp_sftp_listdir(src);
    } else {
        stdio_sink ss;
        stdio_sink_init(&ss, stdout);
        StripCtrlChars *scc = stripctrl_new(
            BinarySink_UPCAST(&ss), false, L'\0');
        while (ssh_scp_recv(&c, 1))
            put_byte(scc, c);
        stripctrl_free(scc);
    }
}

/*
 *  Short description of parameters.
 */
static void usage(void)
{
    printf("PuTTY Secure Copy client\n");
    printf("%s\n", ver);
    printf("Usage: pscp [options] [user@]host:source target\n");
    printf
	("       pscp [options] source [source...] [user@]host:target\n");
    printf("       pscp [options] -ls [user@]host:filespec\n");
    printf("Options:\n");
    printf("  -V        print version information and exit\n");
    printf("  -pgpfp    print PGP key fingerprints and exit\n");
    printf("  -p        preserve file attributes\n");
    printf("  -q        quiet, don't show statistics\n");
    printf("  -r        copy directories recursively\n");
    printf("  -v        show verbose messages\n");
    printf("  -load sessname  Load settings from saved session\n");
    printf("  -P port   connect to specified port\n");
    printf("  -l user   connect with specified username\n");
    printf("  -pw passw login with specified password\n");
    printf("  -1 -2     force use of particular SSH protocol version\n");
    printf("  -4 -6     force use of IPv4 or IPv6\n");
    printf("  -C        enable compression\n");
    printf("  -i key    private key file for user authentication\n");
    printf("  -noagent  disable use of Pageant\n");
    printf("  -agent    enable use of Pageant\n");
    printf("  -no-trivial-auth\n");
    printf("            disconnect if SSH authentication succeeds trivially\n");
    printf("  -hostkey keyid\n");
    printf("            manually specify a host key (may be repeated)\n");
    printf("  -batch    disable all interactive prompts\n");
    printf("  -no-sanitise-stderr  don't strip control chars from"
           " standard error\n");
    printf("  -proxycmd command\n");
    printf("            use 'command' as local proxy\n");
    printf("  -unsafe   allow server-side wildcards (DANGEROUS)\n");
    printf("  -sftp     force use of SFTP protocol\n");
    printf("  -scp      force use of SCP protocol\n");
    printf("  -sshlog file\n");
    printf("  -sshrawlog file\n");
    printf("            log protocol details to a file\n");
#ifdef MOD_PERSO
    printf("  -auto-store-sshkey\n");
    printf("            store automatically the servers ssh keys\n");
#endif
    cleanup_exit(1);
}

void version(void)
{
    char *buildinfo_text = buildinfo("\n");
    printf("pscp: %s\n%s\n", ver, buildinfo_text);
    sfree(buildinfo_text);
    exit(0);
}

void cmdline_error(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "pscp: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fprintf(stderr, "\n      try typing just \"pscp\" for help\n");
    exit(1);
}

const bool share_can_be_downstream = true;
const bool share_can_be_upstream = false;

static stdio_sink stderr_ss;
static StripCtrlChars *stderr_scc;

const unsigned cmdline_tooltype = TOOLTYPE_FILETRANSFER;

/*
 * Main program. (Called `psftp_main' because it gets called from
 * *sftp.c; bit silly, I know, but it had to be called _something_.)
 */
int psftp_main(int argc, char *argv[])
{
    int i;
    bool sanitise_stderr = true;

    sk_init();

    /* Load Default Settings before doing anything else. */
    conf = conf_new();
    do_defaults(NULL, conf);

    for (i = 1; i < argc; i++) {
	int ret;
	if (argv[i][0] != '-')
	    break;
	ret = cmdline_process_param(argv[i], i+1<argc?argv[i+1]:NULL, 1, conf);
	if (ret == -2) {
	    cmdline_error("option \"%s\" requires an argument", argv[i]);
	} else if (ret == 2) {
	    i++;	       /* skip next argument */
	} else if (ret == 1) {
	    /* We have our own verbosity in addition to `flags'. */
            if (cmdline_verbose())
		verbose = true;
        } else if (strcmp(argv[i], "-pgpfp") == 0) {
            pgp_fingerprints();
            return 1;
	} else if (strcmp(argv[i], "-r") == 0) {
	    recursive = true;
	} else if (strcmp(argv[i], "-p") == 0) {
	    preserve = true;
	} else if (strcmp(argv[i], "-q") == 0) {
	    statistics = false;
	} else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "-?") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
	    usage();
	} else if (strcmp(argv[i], "-V") == 0 ||
                   strcmp(argv[i], "--version") == 0) {
            version();
        } else if (strcmp(argv[i], "-ls") == 0) {
	    list = true;
	} else if (strcmp(argv[i], "-batch") == 0) {
	    console_batch_mode = true;
	} else if (strcmp(argv[i], "-unsafe") == 0) {
	    scp_unsafe_mode = true;
	} else if (strcmp(argv[i], "-sftp") == 0) {
	    try_scp = false; try_sftp = true;
	} else if (strcmp(argv[i], "-scp") == 0) {
	    try_scp = true; try_sftp = false;
        } else if (strcmp(argv[i], "-sanitise-stderr") == 0) {
            sanitise_stderr = true;
        } else if (strcmp(argv[i], "-no-sanitise-stderr") == 0) {
            sanitise_stderr = false;
#ifdef MOD_PERSO
    } else if( (strcmp(argv[i], "-auto_store_sshkey")==0) || (strcmp(argv[i], "-auto-store-sshkey") == 0) ) {
	SetAutoStoreSSHKey();
#endif
	} else if (strcmp(argv[i], "--") == 0) {
	    i++;
	    break;
	} else {
	    cmdline_error("unknown option \"%s\"", argv[i]);
	}
    }
    argc -= i;
    argv += i;
    backend = NULL;

    stdio_sink_init(&stderr_ss, stderr);
    stderr_bs = BinarySink_UPCAST(&stderr_ss);
    if (sanitise_stderr) {
        stderr_scc = stripctrl_new(stderr_bs, false, L'\0');
        stderr_bs = BinarySink_UPCAST(stderr_scc);
    }

    string_scc = stripctrl_new(NULL, false, L'\0');

    if (list) {
	if (argc != 1)
	    usage();
	get_dir_list(argc, argv);

    } else {

	if (argc < 2)
	    usage();
	if (argc > 2)
	    targetshouldbedirectory = true;

	if (colon(argv[argc - 1]) != NULL)
	    toremote(argc, argv);
	else
	    tolocal(argc, argv);
    }

    if (backend && backend_connected(backend)) {
	char ch;
        backend_special(backend, SS_EOF, 0);
        sent_eof = true;
	ssh_scp_recv(&ch, 1);
    }
    random_save_seed();

    cmdline_cleanup();
    if (backend) {
    backend_free(backend);
    backend = NULL;
    }
    sk_cleanup();
    return (errs == 0 ? 0 : 1);
}

/* end */
