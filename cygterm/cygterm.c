#ifdef CYGTERMPORT

#include <stdio.h> /* sprintf */
#include <string.h>
#include <limits.h> /* INT_MAX */
#include "putty.h"
#include "cthelper/cthelper.h"
#include "cthelper/message.h"

void SearchCtHelper( void ) ;

#define CYGTERM_MAX_BACKLOG 16384

#define CYGTERM_NAME "Cygterm"

#ifdef __INTERIX
#define CTHELPER "posix.exe /u /c cthelper.exe"
#define CTHELPER64 "posix.exe /u /c cthelper64.exe"
#else
#define CTHELPER "cthelper"
#define CTHELPER64 "cthelper64"
#endif

#if !defined(DEBUG)
#define cygterm_debug(f,...)
#elif !defined(cygterm_debug)
#define cygterm_debug(f,...) debug(("%s:%d:%s: "f"\n",__FILE__,__LINE__,__FUNCTION__,##__VA_ARGS__))
#endif

#define putenv _putenv

typedef struct cygterm_backend_data {
	const struct plug_function_table *fn;
	void *frontend;
	Socket a;
	Socket s;
	PROCESS_INFORMATION pi;
	HANDLE ctl;
	Conf * conf;
	int bufsize;
	int editing, echoing;
	int exitcode;
} *Local;

/* Plug functions for cthelper data connection */
static void
cygterm_log(Plug p, int type, SockAddr addr, int port, const char *error_msg, int error_code)
{
	/* Do nothing */
}

static int
cygterm_closing(Plug plug, const char *error_msg, int error_code, int calling_back)
{
	Local local = (Local)plug;
	cygterm_debug("top");
	if (local->s) {
		sk_close(local->s);
		local->s = NULL;
	}
	/* check for errors from cthelper */
	CloseHandle(local->ctl);
	/* wait for cthelper */
	if (local->pi.hProcess != INVALID_HANDLE_VALUE) {
		if (WAIT_OBJECT_0 == WaitForSingleObject(local->pi.hProcess, 2000)) {
			DWORD status;
			GetExitCodeProcess(local->pi.hProcess, &status);
			switch (status) {
			case CthelperSuccess:
				break;
			case CthelperInvalidUsage:
			case CthelperInvalidPort:
			case CthelperConnectFailed:
				local->exitcode = INT_MAX;
				error_msg = "Internal error";
				break;
			case CthelperPtyforkFailure:
				local->exitcode = INT_MAX;
				error_msg = "Failed to allocate pseudoterminal";
				break;
			case CthelperExecFailure:
				local->exitcode = INT_MAX;
				error_msg = "Failed to execute command";
				break;
			}
		}
	}
	/* this calls cygterm_exitcode() */
	notify_remote_exit(local->frontend);
	if (error_msg) {
		cygterm_debug("error_msg: %s", error_msg);
		connection_fatal(local->frontend, "%s", error_msg);
	}
	return 0;
}

static int
cygterm_receive(Plug plug, int urgent, char *data, int len)
{
	Local local = (Local)plug;
	int backlog;
	cygterm_debug("backend -> display %u", len);
//	dmemdump(data, len);
	backlog = from_backend(local->frontend, 0, data, len);
//	dmemdumpl(data, len);
	sk_set_frozen(local->s, backlog > CYGTERM_MAX_BACKLOG);
	cygterm_debug("OK");
	return 1;
}

static void
cygterm_sent(Plug plug, int bufsize)
{
	Local local = (Local)plug;
	local->bufsize = bufsize;
}

static void
cygterm_size(void *handle, int width, int height);


Socket sk_tcp_accept(accept_ctx_t ctx, Plug plug);
static int
cygterm_accepting(Plug plug, accept_fn_t constructor, accept_ctx_t ctx)
{
	Local local = (Local)plug;
	cygterm_debug("top");
	local->s = constructor(ctx, plug);
	sk_set_frozen(local->s, 0);
	// Reset terminal size
	cygterm_size(local, conf_get_int(local->conf,CONF_width), conf_get_int(local->conf,CONF_height));
	cygterm_debug("OK");
	return 0;
}


static char *getCygwinBin(int use64);
void appendPath(const char *append);
static size_t makeAttributes(char *buf, Conf *conf);
static const char *spawnChild(char *cmd, Conf *conf, LPPROCESS_INFORMATION ppi, PHANDLE pin);

/* Backend functions for the cygterm backend */
void RunCommand( HWND hwnd, char * cmd ) ;
	
static const char *
cygterm_init(void *frontend_handle, void **backend_handle,
             Conf *conf,
             const char *unused_host, int unused_port,
             char **realhost, int nodelay, int keepalive)
{
	/* XXX: I'm not sure if it is OK to overload Plug like this.
	 * cygterm_accepting should only be used for the listening socket
	 * (local->a) while the cygterm_closing, cygterm_receive, and cygterm_sent
	 * should be used only for the actual connection (local->s).
	 */
	static const struct plug_function_table fn_table = {
		cygterm_log,
		cygterm_closing,
		cygterm_receive,
		cygterm_sent,
		cygterm_accepting
	};
	Local local;
	const char *command;
	char cmdline[2 * MAX_PATH];
	int cport;
	const char *err;
	int cmdlinelen;

	cygterm_debug("top");

	local = snew(struct cygterm_backend_data);
	local->fn = &fn_table;
	local->a = NULL;
	local->s = NULL;
	local->conf = conf;
	local->editing = 0;
	local->echoing = 0;
	local->exitcode = 0;
	*backend_handle = local;

	local->frontend = frontend_handle;
	
	/* set up listen socket for communication with child */
	cygterm_debug("setupCygTerm");

	/* let sk use INADDR_LOOPBACK and let WinSock choose a port */
	local->a = sk_newlistener(0, 0, (Plug)local, 1, ADDRTYPE_IPV4);
	if ((err = sk_socket_error(local->a)) != NULL)
		goto fail_free;

	/* now, get the port that WinSock chose */
	/* XXX: Is there another function in PuTTY to do this? */
	cygterm_debug("getting port");
	cport = sk_getport(local->a);
	if (cport == -1) {
		err = "Failed to get port number for cthelper";
		goto fail_close;
	}

	if (strchr(conf_get_str(local->conf,CONF_termtype), ' ')) {
		err = "term type contains spaces";
		goto fail_close;
	}

	/*  Build cthelper command line */
char * CTHELPER_PATH = getenv( "CTHELPER_PATH" ) ;
	if( CTHELPER_PATH==NULL ) { SearchCtHelper() ; CTHELPER_PATH = getenv( "CTHELPER_PATH" ) ; }
if( CTHELPER_PATH!=NULL ) cmdlinelen = sprintf(cmdline, "\"%s\" %u %s ", CTHELPER_PATH, cport, conf_get_str(local->conf,CONF_termtype));
else {
	//cmdlinelen = sprintf(cmdline, CTHELPER" %u %s ", cport,conf_get_str(local->conf,CONF_termtype));
	if(conf_get_int(conf, CONF_cygterm64)) {
		cmdlinelen = sprintf(cmdline, CTHELPER64" %u %s ", cport, conf_get_str(local->conf, CONF_termtype));
		}
	else {
		cmdlinelen = sprintf(cmdline, CTHELPER" %u %s ", cport, conf_get_str(local->conf, CONF_termtype));
		}
	}
	cmdlinelen += makeAttributes(cmdline + cmdlinelen, local->conf);

	command = conf_get_str(conf,CONF_cygcmd);
	
	if( command[0]=='?' ) {
		RunCommand(NULL,(char*)(command+1));
		exit(0);
		}
	else {
	cygterm_debug("command is :%s:", command);
	/*  A command of  "."  or  "-"  tells us to pass no command arguments to
	 *  cthelper which will then run the user's shell under Cygwin.  */
	if ((command[0]=='-'||command[0]=='.') && command[1]=='\0')
		;
	else if (cmdlinelen + strlen(command) + 2 > sizeof cmdline) {
		err = "command is too long";
		goto fail_close;
	}
	else {
		cmdlinelen += sprintf(cmdline + cmdlinelen, " %s", command);
	}

	/* Add the Cygwin /bin path to the PATH. */
	if (conf_get_int(conf,CONF_cygautopath)) {
		char *cygwinBinPath = getCygwinBin(conf_get_int(conf, CONF_cygterm64));
		if (!cygwinBinPath) {
			/* we'll try anyway */
			cygterm_debug("cygwin bin directory not found");
		}
		else {
			cygterm_debug("found cygwin directory: %s", cygwinBinPath);
			appendPath(cygwinBinPath);
			sfree(cygwinBinPath);
		}
	}
	
	cygterm_debug("starting cthelper: %s", cmdline);
	{ char buffer[1024] ; sprintf( buffer,"starting cthelper: %s", cmdline ) ; logevent( NULL,buffer ) ; }
	if ((err = spawnChild(cmdline, conf, &local->pi, &local->ctl)))
		goto fail_close;

	/*  This should be set to the local hostname, Apparently, realhost is used
	 *  only to set the window title.
	 */
	strcpy(*realhost = smalloc(sizeof CYGTERM_NAME), CYGTERM_NAME);
	cygterm_debug("OK");
	}
	
	return 0;

fail_close:
	sk_close(local->a);
fail_free:
	sfree(local);
	return err;
}

static void
cygterm_free(void *handle)
{
	Local local = handle;
	cygterm_debug("top");
	sfree(local);
}

static void
cygterm_reconfig(void *handle, Conf *conf)
{
	Local local = handle;
	cygterm_debug("top");
	local->conf = conf;
}

static int
cygterm_send(void *handle, const char *buf, int len)
{
	Local local = handle;
	cygterm_debug("frontend -> pty %u", len);
//	dmemdump(buf, len);
#if 0
	/* HACK */
	{
		int i;
		for (i = 0; i < len - 1; i++)
			if (buf[i] == 033 && !(buf[i+1]&0x80)) {
				memmove(buf + i, buf + i + 1, --len - i);
				buf[i] |= 0x80;
			}
	}
#endif
	if (local->s != 0)
		local->bufsize = sk_write(local->s, buf, len);
	cygterm_debug("OK");
	return local->bufsize;
}

static int
cygterm_sendbuffer(void *handle)
{
	Local local = handle;
	cygterm_debug("top");
	return local->bufsize;
}

static void
cygterm_size(void *handle, int width, int height)
{
	Local local = handle;
	cygterm_debug("top");
	cygterm_debug("size=%d,%d (last=%d,%d)",
	              width, height, local->cfg.width, local->cfg.height);
	conf_set_int(local->conf,CONF_width,width);
	conf_set_int(local->conf,CONF_height,height);
	if (local->s) {
		DWORD n;
		Message m;
		m.size = MESSAGE_MIN + sizeof(m.msg);
		m.type = MSG_RESIZE;
		m.msg.resize.width = width;
		m.msg.resize.height = height;
		cygterm_debug("WriteFile %p %p:%u", local->ctl, &m, m.size);
		WriteFile(local->ctl, (const char *)&m, m.size, &n, 0);
		cygterm_debug("WriteFile returns %d");
	}
}

static void
cygterm_special(void *handle, Telnet_Special code)
{
	cygterm_debug("top");
}

static const struct telnet_special *
cygterm_get_specials(void *handle)
{
	cygterm_debug("top");
	return NULL;
}

static int
cygterm_connected(void *handle)
{
	Local local = handle;
	cygterm_debug("top");
	return local->s != NULL;
}

static int
cygterm_exitcode(void *handle)
{
	Local local = handle;
	cygterm_debug("top");
	return local->exitcode;
}

static int
cygterm_sendok(void *handle)
{
	cygterm_debug("top");
	return 1;
}

static void
cygterm_unthrottle(void *handle, int backlog)
{
	Local local = handle;
	cygterm_debug("top");
	sk_set_frozen(local->s, backlog > CYGTERM_MAX_BACKLOG);
}

static int
cygterm_ldisc(void *handle, int option)
{
	Local local = handle;
	cygterm_debug("cygterm_ldisc: %d", option);
	switch (option) {
	case LD_EDIT:
		return local->editing;
	case LD_ECHO:
		return local->echoing;
	}
	return 0;
}

static void
cygterm_provide_ldisc(void *handle, void *ldisc)
{
	cygterm_debug("top");
}

static void
cygterm_provide_logctx(void *handle, void *logctx)
{
	cygterm_debug("top");
}

static int
cygterm_cfg_info(void *handle)
{
	return 0;
}

Backend cygterm_backend = {
	cygterm_init,
	cygterm_free,
	cygterm_reconfig,
	cygterm_send,
	cygterm_sendbuffer,
	cygterm_size,
	cygterm_special,
	cygterm_get_specials,
	cygterm_connected,
	cygterm_exitcode,
	cygterm_sendok,
	cygterm_ldisc,
	cygterm_provide_ldisc,
	cygterm_provide_logctx,
	cygterm_unthrottle,
	cygterm_cfg_info,
	NULL /* test_for_upstream */,
	"cygterm",
	PROT_CYGTERM,
	1
};

/* like strcpy(), but return pointer to terminating null character */
static char *
strecpy(char *d,const char *s)
{
	while ((*d = *s++))
		d++;
	return d;
}

/* Make cthelper attribute string from PuTTY Config */
static size_t
makeAttributes(char *buf, Conf *conf)
{
	char *e = buf;

	if (conf_get_int(conf,CONF_bksp_is_delete))
		e = strecpy(e, ":erase=^?");
	else
		e = strecpy(e, ":erase=^H");

	e += sprintf(e, ":size=%d,%d", conf_get_int(conf,CONF_height), conf_get_int(conf,CONF_width));

	/* TODO: other options? localedit? localecho? */

	return e - buf;
}

#ifndef KEY_WOW64_64KEY
#define KEY_WOW64_64KEY 0x0100
#endif
#ifndef KEY_WOW64_32KEY
#define KEY_WOW64_32KEY 0x0200
#endif

/* Utility functions for spawning cthelper process */
static BOOL
getRegistry(char *valueData, LPDWORD psize, HKEY key, const char *subKey, const char *valueName, int use64)
{
	HKEY k;
	LONG ret;

	if (ERROR_SUCCESS != (ret = RegOpenKeyEx(key, subKey, 0, KEY_READ | (use64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY), &k )))
		return ret;

	ERROR_SUCCESS == (ret = RegQueryInfoKey(k, 0, 0, 0, 0, 0, 0, 0, 0, psize, 0, 0))
		&& ERROR_SUCCESS == (ret = RegQueryValueEx(k, valueName, 0, 0, (LPBYTE)valueData, psize)) ;

	RegCloseKey(k);
	return ret;
}

/* Cygwin 1.5 definition */
#define CYGWIN_SYS_ROOT_MOUNT \
	HKEY_LOCAL_MACHINE,\
	"Software\\Cygnus Solutions\\Cygwin\\mounts v2\\/",\
	"native"
#define CYGWIN_USER_ROOT_MOUNT \
	HKEY_CURRENT_USER,\
	"Software\\Cygnus Solutions\\Cygwin\\mounts v2\\/",\
	"native"

/* As of Cygwin 1.7, one of these keys contains the Cygwin install root. */
#define CYGWIN_U_SETUP_ROOTDIR \
	HKEY_CURRENT_USER,\
	"Software\\Cygwin\\setup",\
	"rootdir"
#define CYGWIN_S_SETUP_ROOTDIR \
	HKEY_LOCAL_MACHINE,\
	"Software\\Cygwin\\setup",\
	"rootdir"

static char *
getCygwinBin(int use64)
{
	char *dir;
	DWORD size = MAX_PATH;

	dir = smalloc(size);
	dir[0] = '\0';

	if (ERROR_SUCCESS == getRegistry(dir, &size, CYGWIN_U_SETUP_ROOTDIR, use64) || ERROR_SUCCESS == getRegistry(dir, &size, CYGWIN_S_SETUP_ROOTDIR, use64))
	{
		strcat(dir, "\\bin");
	}
	else if (ERROR_SUCCESS == getRegistry(dir, &size, CYGWIN_SYS_ROOT_MOUNT, use64) || ERROR_SUCCESS == getRegistry(dir, &size, CYGWIN_USER_ROOT_MOUNT, use64))
	{
		strcat(dir, "\\bin");
	}
	else
	{
		sfree(dir);
		dir = 0;
	}

	return dir;
}

void
appendPath(const char *append)
{
	char *path;
	char *newPath;

	cygterm_debug("getting PATH");
	if (!(path = getenv("PATH"))) {
		cygterm_debug("hmm.. PATH not set");
		path = "";
	}
	cygterm_debug("alloc newPath");
	newPath = smalloc(5 + strlen(append) + 1 + strlen(path) + 1);
	cygterm_debug("init newPath");
	sprintf(newPath, "PATH=%s;%s", path, append);
	cygterm_debug("set newPath");
	putenv(newPath);
	cygterm_debug("free newPath");
	sfree(newPath);
}

static const char *
spawnChild(char *cmd, Conf *conf, LPPROCESS_INFORMATION ppi, PHANDLE pin)
{
	STARTUPINFO si = {sizeof(si)};
	SECURITY_ATTRIBUTES sa = {sizeof(sa)};
	HANDLE in;

	/* Create an anonymous pipe over which to send events such as resize */
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;
	if (!CreatePipe(&in, pin, &sa, 1))
		return "failed to create event pipe";

	/* cthelper will use stdin to get event messages */
	si.dwFlags = STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
	si.hStdInput = in;
	si.wShowWindow = SW_HIDE;

	/* Add the Cygwin /bin path to the PATH env var. */
    if (!getenv("NOCYGWIN")) {
        char *cygwinBinPath = getCygwinBin(conf_get_int(conf, CONF_cygterm64));
        if (!cygwinBinPath) {
            /* we'll try anyway */
            cygterm_debug("cygwin bin directory not found");
        }
        else {
            cygterm_debug("found cygwin directory: %s", cygwinBinPath);
            appendPath(cygwinBinPath);
            sfree(cygwinBinPath);
        }
    }
	{ char buffer[1024] ; sprintf(buffer, "Create process %s", cmd ) ; logevent(NULL,buffer); }
	/* We allow cthelper to inherit handles.  I have no idea if there are
	 * other inheritable handles in PuTTY that this will effect.  cthelper
	 * will attempt to close all open descriptors.
	 */
	if (!CreateProcess(
		NULL, cmd,  /* command line */
		NULL, NULL, /* no process or thread security attributes */
		TRUE,       /* inherit handles */
		CREATE_NEW_CONSOLE, /* create a new console window */
		NULL,       /* use parent environment */
		0,          /* use parent working directory */
		&si,        /* STARTUPINFO sets stdin to read end of pipe */
		ppi))
	{
		CloseHandle(in);
		CloseHandle(*pin);
		*pin = INVALID_HANDLE_VALUE;
		return "failed to run cthelper";
	}

	/* close the read end of the pipe */
	CloseHandle(in);

	return 0;
}
#endif
