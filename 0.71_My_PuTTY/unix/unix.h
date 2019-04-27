#ifndef PUTTY_UNIX_H
#define PUTTY_UNIX_H

#ifdef HAVE_CONFIG_H
# include "uxconfig.h" /* Space to hide it from mkfiles.pl */
#endif

#include <stdio.h>		       /* for FILENAME_MAX */
#include <stdint.h>		       /* C99 int types */
#ifndef NO_LIBDL
#include <dlfcn.h>		       /* Dynamic library loading */
#endif /*  NO_LIBDL */
#include "charset.h"
#include <sys/types.h>         /* for mode_t */

#ifdef OSX_GTK
/*
 * Assorted tweaks to various parts of the GTK front end which all
 * need to be enabled when compiling on OS X. Because I might need the
 * same tweaks on other systems in future, I don't want to
 * conditionalise all of them on OSX_GTK directly, so instead, each
 * one has its own name and we enable them all centrally here if
 * OSX_GTK is defined at configure time.
 */
#define NOT_X_WINDOWS /* of course, all the X11 stuff should be disabled */
#define NO_PTY_PRE_INIT /* OS X gets very huffy if we try to set[ug]id */
#define SET_NONBLOCK_VIA_OPENPT /* work around missing fcntl functionality */
#define OSX_META_KEY_CONFIG /* two possible Meta keys to choose from */
/* this potential one of the Meta keys needs manual handling */
#define META_MANUAL_MASK (GDK_MOD1_MASK)
#define JUST_USE_GTK_CLIPBOARD_UTF8 /* low-level gdk_selection_* fails */

#define BUILDINFO_PLATFORM_GTK "OS X (GTK)"
#define BUILDINFO_GTK

#elif defined NOT_X_WINDOWS

#define BUILDINFO_PLATFORM_GTK "Unix (pure GTK)"
#define BUILDINFO_GTK

#else

#define BUILDINFO_PLATFORM_GTK "Unix (GTK + X11)"
#define BUILDINFO_GTK

#endif

/* BUILDINFO_PLATFORM varies its expansion between the GTK and
 * pure-CLI utilities, so that Unix Plink, PSFTP etc don't announce
 * themselves incongruously as having something to do with GTK. */
#define BUILDINFO_PLATFORM_CLI "Unix"
extern const bool buildinfo_gtk_relevant;
#define BUILDINFO_PLATFORM (buildinfo_gtk_relevant ? \
                            BUILDINFO_PLATFORM_GTK : BUILDINFO_PLATFORM_CLI)

char *buildinfo_gtk_version(void);

struct Filename {
    char *path;
};
FILE *f_open(const struct Filename *, char const *, bool);

struct FontSpec {
    char *name;    /* may be "" to indicate no selected font at all */
};
struct FontSpec *fontspec_new(const char *name);

extern const struct BackendVtable pty_backend;

#define BROKEN_PIPE_ERROR_CODE EPIPE   /* used in sshshare.c */

/*
 * Under GTK, we send MA_CLICK _and_ MA_2CLK, or MA_CLICK _and_
 * MA_3CLK, when a button is pressed for the second or third time.
 */
#define MULTICLICK_ONLY_EVENT 0

/*
 * Under GTK, there is no context help available.
 */
#define HELPCTX(x) P(NULL)
#define FILTER_KEY_FILES NULL          /* FIXME */
#define FILTER_DYNLIB_FILES NULL       /* FIXME */

/*
 * Under X, selection data must not be NUL-terminated.
 */
#define SELECTION_NUL_TERMINATED 0

/*
 * Under X, copying to the clipboard terminates lines with just LF.
 */
#define SEL_NL { 10 }

/* Simple wraparound timer function */
unsigned long getticks(void);
#define GETTICKCOUNT getticks
#define TICKSPERSEC    1000	       /* we choose to use milliseconds */
#define CURSORBLINK     450	       /* no standard way to set this */

#define WCHAR wchar_t
#define BYTE unsigned char

/*
 * Unix-specific global flag
 *
 * FLAG_STDERR_TTY indicates that standard error might be a terminal and
 * might get its configuration munged, so anything trying to output plain
 * text (i.e. with newlines in it) will need to put it back into cooked
 * mode first.  Applications setting this flag should also call
 * stderr_tty_init() before messing with any terminal modes, and can call
 * premsg() before outputting text to stderr and postmsg() afterwards.
 */
#define FLAG_STDERR_TTY 0x1000

#define PLATFORM_CLIPBOARDS(X)                            \
    X(CLIP_PRIMARY, "X11 primary selection")              \
    X(CLIP_CLIPBOARD, "XDG clipboard")                    \
    X(CLIP_CUSTOM_1, "<custom#1>")                        \
    X(CLIP_CUSTOM_2, "<custom#2>")                        \
    X(CLIP_CUSTOM_3, "<custom#3>")                        \
    /* end of list */

#ifdef OSX_GTK
/* OS X has no PRIMARY selection */
#define MOUSE_SELECT_CLIPBOARD CLIP_NULL
#define MOUSE_PASTE_CLIPBOARD CLIP_LOCAL
#define CLIPNAME_IMPLICIT "Last selected text"
#define CLIPNAME_EXPLICIT "System clipboard"
#define CLIPNAME_EXPLICIT_OBJECT "system clipboard"
/* These defaults are the ones that more or less comply with the OS X
 * Human Interface Guidelines, i.e. copy/paste to the system clipboard
 * is _not_ implicit but requires a specific UI action. This is at
 * odds with all other PuTTY front ends' defaults, but on OS X there
 * is no multi-decade precedent for PuTTY working the other way. */
#define CLIPUI_DEFAULT_AUTOCOPY false
#define CLIPUI_DEFAULT_MOUSE CLIPUI_IMPLICIT
#define CLIPUI_DEFAULT_INS CLIPUI_EXPLICIT
#define MENU_CLIPBOARD CLIP_CLIPBOARD
#define COPYALL_CLIPBOARDS CLIP_CLIPBOARD
#else
#define MOUSE_SELECT_CLIPBOARD CLIP_PRIMARY
#define MOUSE_PASTE_CLIPBOARD CLIP_PRIMARY
#define CLIPNAME_IMPLICIT "PRIMARY"
#define CLIPNAME_EXPLICIT "CLIPBOARD"
#define CLIPNAME_EXPLICIT_OBJECT "CLIPBOARD"
/* These defaults are the ones Unix PuTTY has historically had since
 * it was first thought of in 2002 */
#define CLIPUI_DEFAULT_AUTOCOPY false
#define CLIPUI_DEFAULT_MOUSE CLIPUI_IMPLICIT
#define CLIPUI_DEFAULT_INS CLIPUI_IMPLICIT
#define MENU_CLIPBOARD CLIP_CLIPBOARD
#define COPYALL_CLIPBOARDS CLIP_PRIMARY, CLIP_CLIPBOARD
/* X11 supports arbitrary named clipboards */
#define NAMED_CLIPBOARDS
#endif

/* The per-session frontend structure managed by gtkwin.c */
typedef struct GtkFrontend GtkFrontend;

/* Callback when a dialog box finishes, and a no-op implementation of it */
typedef void (*post_dialog_fn_t)(void *ctx, int result);
void trivial_post_dialog_fn(void *vctx, int result);

/* Start up a session window, with or without a preliminary config box */
void initial_config_box(Conf *conf, post_dialog_fn_t after, void *afterctx);
void new_session_window(Conf *conf, const char *geometry_string);

/* Defined in gtkmain.c */
void launch_duplicate_session(Conf *conf);
void launch_new_session(void);
void launch_saved_session(const char *str);
void session_window_closed(void);
void window_setup_error(const char *errmsg);
#ifdef MAY_REFER_TO_GTK_IN_HEADERS
GtkWidget *make_gtk_toplevel_window(GtkFrontend *frontend);
#endif

const struct BackendVtable *select_backend(Conf *conf);

/* Defined in gtkcomm.c */
void gtkcomm_setup(void);

/* Used to pass application-menu operations from gtkapp.c to gtkwin.c */
enum MenuAction {
    MA_COPY, MA_PASTE, MA_COPY_ALL, MA_DUPLICATE_SESSION,
    MA_RESTART_SESSION, MA_CHANGE_SETTINGS, MA_CLEAR_SCROLLBACK,
    MA_RESET_TERMINAL, MA_EVENT_LOG
};
void app_menu_action(GtkFrontend *frontend, enum MenuAction);

/* Arrays of pixmap data used for GTK window icons. (main_icon is for
 * the process's main window; cfg_icon is the modified icon used for
 * its config box.) */
extern const char *const *const main_icon[];
extern const char *const *const cfg_icon[];
extern const int n_main_icon, n_cfg_icon;

/* Things gtkdlg.c needs from gtkwin.c */
#ifdef MAY_REFER_TO_GTK_IN_HEADERS
enum DialogSlot {
    DIALOG_SLOT_RECONFIGURE,
    DIALOG_SLOT_NETWORK_PROMPT,
    DIALOG_SLOT_LOGFILE_PROMPT,
    DIALOG_SLOT_WARN_ON_CLOSE,
    DIALOG_SLOT_CONNECTION_FATAL,
    DIALOG_SLOT_LIMIT /* must remain last */
};
GtkWidget *gtk_seat_get_window(Seat *seat);
void register_dialog(Seat *seat, enum DialogSlot slot, GtkWidget *dialog);
void unregister_dialog(Seat *seat, enum DialogSlot slot);
void set_window_icon(GtkWidget *window, const char *const *const *icon,
                     int n_icon);
extern GdkAtom compound_text_atom;
#endif

/* Things gtkwin.c needs from gtkdlg.c */
#ifdef MAY_REFER_TO_GTK_IN_HEADERS
GtkWidget *create_config_box(const char *title, Conf *conf,
                             bool midsession, int protcfginfo,
                             post_dialog_fn_t after, void *afterctx);
#endif
void nonfatal_message_box(void *window, const char *msg);
void about_box(void *window);
typedef struct eventlog_stuff eventlog_stuff;
eventlog_stuff *eventlogstuff_new(void);
void eventlogstuff_free(eventlog_stuff *);
void showeventlog(eventlog_stuff *estuff, void *parentwin);
void logevent_dlg(eventlog_stuff *estuff, const char *string);
int gtkdlg_askappend(Seat *seat, Filename *filename,
                     void (*callback)(void *ctx, int result), void *ctx);
int gtk_seat_verify_ssh_host_key(
    Seat *seat, const char *host, int port,
    const char *keytype, char *keystr, char *fingerprint,
    void (*callback)(void *ctx, int result), void *ctx);
int gtk_seat_confirm_weak_crypto_primitive(
    Seat *seat, const char *algtype, const char *algname,
    void (*callback)(void *ctx, int result), void *ctx);
int gtk_seat_confirm_weak_cached_hostkey(
    Seat *seat, const char *algname, const char *betteralgs,
    void (*callback)(void *ctx, int result), void *ctx);
#ifdef MAY_REFER_TO_GTK_IN_HEADERS
struct message_box_button {
    const char *title;
    char shortcut;
    int type; /* more negative means more appropriate to be the Esc action */
    int value;     /* message box's return value if this is pressed */
};
struct message_box_buttons {
    const struct message_box_button *buttons;
    int nbuttons;
};
extern const struct message_box_buttons buttons_yn, buttons_ok;
GtkWidget *create_message_box(
    GtkWidget *parentwin, const char *title, const char *msg, int minwid,
    bool selectable, const struct message_box_buttons *buttons,
    post_dialog_fn_t after, void *afterctx);
#endif

/* Things gtkwin.c needs from {ptermm,uxputty}.c */
char *make_default_wintitle(char *hostname);

/* gtkwin.c needs this special function in xkeysym.c */
int keysym_to_unicode(int keysym);

/* Things uxstore.c needs from gtkwin.c */
char *x_get_default(const char *key);

/* Things uxstore.c provides to gtkwin.c */
void provide_xrm_string(char *string);

/* Function that {gtkapp,gtkmain}.c needs from ux{pterm,putty}.c. Does
 * early process setup that varies between applications (e.g.
 * pty_pre_init or sk_init), and is passed a boolean by the caller
 * indicating whether this is an OS X style multi-session monolithic
 * process or an ordinary Unix one-shot. */
void setup(bool single_session_in_this_process);

/*
 * Per-application constants that affect behaviour of shared modules.
 */
/* Do we need an Event Log menu item? (yes for PuTTY, no for pterm) */
extern const bool use_event_log;
/* Do we need a New Session menu item? (yes for PuTTY, no for pterm) */
extern const bool new_session;
/* Do we need a Saved Sessions menu item? (yes for PuTTY, no for pterm) */
extern const bool saved_sessions;
/* When we Duplicate Session, do we need to double-check that the Conf
 * is in a launchable state? (no for pterm, because conf_launchable
 * returns an irrelevant answer, since we'll force use of the pty
 * backend which ignores all the relevant settings) */
extern const bool dup_check_launchable;
/* In the Duplicate Session serialised data, do we send/receive an
 * argv array after the main Conf? (yes for pterm, no for PuTTY) */
extern const bool use_pty_argv;

/*
 * OS X environment munging: this is the prefix we expect to find on
 * environment variable names that were changed by osxlaunch.
 * Extracted from the command line of the OS X pterm main binary, and
 * used in uxpty.c to restore the original environment before
 * launching its subprocess.
 */
extern char *pty_osx_envrestore_prefix;

/* Things provided by uxcons.c */
struct termios;
void stderr_tty_init(void);
void premsg(struct termios *);
void postmsg(struct termios *);

/* The interface used by uxsel.c */
typedef struct uxsel_id uxsel_id;
void uxsel_init(void);
typedef void (*uxsel_callback_fn)(int fd, int event);
void uxsel_set(int fd, int rwx, uxsel_callback_fn callback);
void uxsel_del(int fd);
enum { SELECT_R = 1, SELECT_W = 2, SELECT_X = 4 };
void select_result(int fd, int event);
int first_fd(int *state, int *rwx);
int next_fd(int *state, int *rwx);
/* The following are expected to be provided _to_ uxsel.c by the frontend */
uxsel_id *uxsel_input_add(int fd, int rwx);  /* returns an id */
void uxsel_input_remove(uxsel_id *id);

/* uxcfg.c */
struct controlbox;
void unix_setup_config_box(
    struct controlbox *b, bool midsession, int protocol);

/* gtkcfg.c */
void gtk_setup_config_box(
    struct controlbox *b, bool midsession, void *window);

/*
 * In the Unix Unicode layer, DEFAULT_CODEPAGE is a special value
 * which causes mb_to_wc and wc_to_mb to call _libc_ rather than
 * libcharset. That way, we can interface the various charsets
 * supported by libcharset with the one supported by mbstowcs and
 * wcstombs (which will be the character set in which stuff read
 * from the command line or config files is assumed to be encoded).
 */
#define DEFAULT_CODEPAGE 0xFFFF
#define CP_UTF8 CS_UTF8		       /* from libcharset */

#define strnicmp strncasecmp
#define stricmp strcasecmp

/* BSD-semantics version of signal(), and another helpful function */
void (*putty_signal(int sig, void (*func)(int)))(int);
void block_signal(int sig, bool block_it);

/* uxmisc.c */
void cloexec(int);
void noncloexec(int);
bool nonblock(int);
bool no_nonblock(int);
char *make_dir_and_check_ours(const char *dirname);
char *make_dir_path(const char *path, mode_t mode);

/*
 * Exports from unicode.c.
 */
struct unicode_data;
bool init_ucs(struct unicode_data *ucsdata, char *line_codepage,
              bool utf8_override, int font_charset, int vtmode);

/*
 * Spare functions exported directly from uxnet.c.
 */
void *sk_getxdmdata(Socket *sock, int *lenp);
SockAddr *unix_sock_addr(const char *path);
Socket *new_unix_listener(SockAddr *listenaddr, Plug *plug);

/*
 * General helpful Unix stuff: more helpful version of the FD_SET
 * macro, which also handles maxfd.
 */
#define FD_SET_MAX(fd, max, set) do { \
    FD_SET(fd, &set); \
    if (max < fd + 1) max = fd + 1; \
} while (0)

/*
 * Exports from uxser.c.
 */
extern const struct BackendVtable serial_backend;

/*
 * uxpeer.c, wrapping getsockopt(SO_PEERCRED).
 */
bool so_peercred(int fd, int *pid, int *uid, int *gid);

/*
 * uxfdsock.c.
 */
Socket *make_fd_socket(int infd, int outfd, int inerrfd, Plug *plug);

/*
 * Default font setting, which can vary depending on NOT_X_WINDOWS.
 */
#ifdef NOT_X_WINDOWS
#define DEFAULT_GTK_FONT "client:Monospace 12"
#else
#define DEFAULT_GTK_FONT "server:fixed"
#endif

/*
 * uxpty.c.
 */
void pty_pre_init(void);    /* pty+utmp setup before dropping privilege */
/* Pass in the argv[] for an instance of the pty backend created by
 * the standard vtable constructor. Only called from (non-OSX) pterm,
 * which will construct exactly one such instance, and initialises
 * this from the command line. */
extern char **pty_argv;

/*
 * gtkask.c.
 */
char *gtk_askpass_main(const char *display, const char *wintitle,
                       const char *prompt, bool *success);

/*
 * uxsftpserver.c.
 */
extern const SftpServerVtable unix_live_sftpserver_vt;

/*
 * uxpoll.c.
 */
typedef struct pollwrapper pollwrapper;
pollwrapper *pollwrap_new(void);
void pollwrap_free(pollwrapper *pw);
void pollwrap_clear(pollwrapper *pw);
void pollwrap_add_fd_events(pollwrapper *pw, int fd, int events);
void pollwrap_add_fd_rwx(pollwrapper *pw, int fd, int rwx);
int pollwrap_poll_instant(pollwrapper *pw);
int pollwrap_poll_endless(pollwrapper *pw);
int pollwrap_poll_timeout(pollwrapper *pw, int milliseconds);
int pollwrap_get_fd_events(pollwrapper *pw, int fd);
int pollwrap_get_fd_rwx(pollwrapper *pw, int fd);
static inline bool pollwrap_check_fd_rwx(pollwrapper *pw, int fd, int rwx)
{
    return (pollwrap_get_fd_rwx(pw, fd) & rwx) != 0;
}

#endif /* PUTTY_UNIX_H */
