/*
 * window.c - the PuTTY(tel) main program, which runs a PuTTY terminal
 * emulator and backend in a window.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <assert.h>

#ifdef MOD_FAR2L
/* far2l extensions support - base64 encode/decode libs */
#include <../../far2l/cencode.h>
#include <../../far2l/cdecode.h>
#endif

#ifdef __WINE__
#define NO_MULTIMON                    /* winelib doesn't have this */
#endif

#ifndef NO_MULTIMON
#define COMPILE_MULTIMON_STUBS
#endif

#include "putty.h"
#include "terminal.h"
#include "storage.h"
#include "win_res.h"
#include "winsecur.h"
#include "winseat.h"
#include "tree234.h"

#ifndef NO_MULTIMON
#ifdef MOD_PERSO
#include <winuser.h>
#else
#include <multimon.h>
#endif
#endif

#include <imm.h>
#include <commctrl.h>
#include <richedit.h>
#include <mmsystem.h>

/* From MSDN: In the WM_SYSCOMMAND message, the four low-order bits of
 * wParam are used by Windows, and should be masked off, so we shouldn't
 * attempt to store information in them. Hence all these identifiers have
 * the low 4 bits clear. Also, identifiers should < 0xF000. */

#define IDM_SHOWLOG   0x0010
#define IDM_NEWSESS   0x0020
#define IDM_DUPSESS   0x0030
#define IDM_RESTART   0x0040
#define IDM_RECONF    0x0050
#define IDM_CLRSB     0x0060
#define IDM_RESET     0x0070
#define IDM_HELP      0x0140
#define IDM_ABOUT     0x0150
#define IDM_SAVEDSESS 0x0160
#define IDM_COPYALL   0x0170
#define IDM_FULLSCREEN	0x0180
#define IDM_COPY      0x0190
#define IDM_PASTE     0x01A0
#define IDM_NEWDUPSESS   0x01B0

#define IDM_SPECIALSEP 0x0200

#define IDM_SPECIAL_MIN 0x0400
#define IDM_SPECIAL_MAX 0x0800

#define IDM_SAVED_MIN 0x1000
#define IDM_SAVED_MAX 0x5000
#define MENU_SAVED_STEP 16
/* Maximum number of sessions on saved-session submenu */
#define MENU_SAVED_MAX ((IDM_SAVED_MAX-IDM_SAVED_MIN) / MENU_SAVED_STEP)

#define WM_IGNORE_CLIP (WM_APP + 2)
#define WM_FULLSCR_ON_MAX (WM_APP + 3)
#define WM_GOT_CLIPDATA (WM_APP + 4)

/* Needed for Chinese support and apparently not always defined. */
#ifndef VK_PROCESSKEY
#define VK_PROCESSKEY 0xE5
#endif

/* Mouse wheel support. */
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A	       /* not defined in earlier SDKs */
#endif
#ifndef WHEEL_DELTA
#define WHEEL_DELTA 120
#endif

/* DPI awareness support */
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#define WM_DPICHANGED_BEFOREPARENT 0x02E2
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#define WM_GETDPISCALEDSIZE 0x02E4
#endif

/* VK_PACKET, used to send Unicode characters in WM_KEYDOWNs */
#ifndef VK_PACKET
#define VK_PACKET 0xE7
#endif

static Mouse_Button translate_button(Mouse_Button button);
static void show_mouseptr(bool show);
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static int TranslateKey(UINT message, WPARAM wParam, LPARAM lParam,
			unsigned char *output);
static void init_palette(void);
static void init_fonts(int, int);
static void init_dpi_info(void);
static void another_font(int);
static void deinit_fonts(void);
static void set_input_locale(HKL);
static void update_savedsess_menu(void);
static void init_winfuncs(void);

static bool is_full_screen(void);
static void make_full_screen(void);
static void clear_full_screen(void);
static void flip_full_screen(void);
static void process_clipdata(HGLOBAL clipdata, bool unicode);
static void setup_clipboards(Terminal *, Conf *);

/* Window layout information */
static void reset_window(int);
static int extra_width, extra_height;
static int font_width, font_height;
static bool font_dualwidth, font_varpitch;
static int offset_width, offset_height;
static bool was_zoomed = false;
static int prev_rows, prev_cols;
  
static void flash_window(int mode);
static void sys_cursor_update(void);
static bool get_fullscreen_rect(RECT * ss);

static int caret_x = -1, caret_y = -1;

static int kbd_codepage;

static Ldisc *ldisc;
static Backend *backend;

static struct unicode_data ucsdata;
static bool session_closed;
static bool reconfiguring = false;

static const SessionSpecial *specials = NULL;
static HMENU specials_menu = NULL;
static int n_specials = 0;

#define TIMING_TIMER_ID 1234
static long timing_next_time;

static struct {
    HMENU menu;
} popup_menus[2];
enum { SYSMENU, CTXMENU };
static HMENU savedsess_menu;

#ifdef MOD_PERSO
Conf *conf;
#else
static Conf *conf;
#endif
static LogContext *logctx;
static Terminal *term;

struct wm_netevent_params {
    /* Used to pass data to wm_netevent_callback */
    WPARAM wParam;
    LPARAM lParam;
};

static void conf_cache_data(void);
static int cursor_type;
static int vtmode;

static struct sesslist sesslist;       /* for saved-session menu */

#define FONT_NORMAL 0
#define FONT_BOLD 1
#define FONT_UNDERLINE 2
#define FONT_BOLDUND 3

#ifdef MOD_PERSO
#define FONT_ITALIC 4
#define FONT_WIDE	0x08
#define FONT_HIGH	0x10
#define FONT_NARROW	0x20

#define FONT_OEM 	0x40
#define FONT_OEMBOLD 	0x41
#define FONT_OEMUND 	0x42
#define FONT_OEMBOLDUND 0x43
 
#define FONT_MAXNO 	0x4F
#define FONT_SHIFT	6
static int use_italics;
#else
#define FONT_WIDE	0x04
#define FONT_HIGH	0x08
#define FONT_NARROW	0x10

#define FONT_OEM 	0x20
#define FONT_OEMBOLD 	0x21
#define FONT_OEMUND 	0x22
#define FONT_OEMBOLDUND 0x23

#define FONT_MAXNO 	0x40
#define FONT_SHIFT	5
#endif

static HFONT fonts[FONT_MAXNO];
static LOGFONT lfont;
static bool fontflag[FONT_MAXNO];
static enum {
    BOLD_NONE, BOLD_SHADOW, BOLD_FONT
} bold_font_mode;
static bool bold_colours;
static enum {
    UND_LINE, UND_FONT
} und_mode;
static int descent, font_strikethrough_y;

static COLORREF colours[OSC4_NCOLOURS];
static HPALETTE pal;
static LPLOGPALETTE logpal;
bool tried_pal = false;
COLORREF colorref_modifier = 0;

enum MONITOR_DPI_TYPE { MDT_EFFECTIVE_DPI, MDT_ANGULAR_DPI, MDT_RAW_DPI, MDT_DEFAULT };
DECL_WINDOWS_FUNCTION(static, HRESULT, GetDpiForMonitor, (HMONITOR hmonitor, enum MONITOR_DPI_TYPE dpiType, UINT *dpiX, UINT *dpiY));
DECL_WINDOWS_FUNCTION(static, HRESULT, GetSystemMetricsForDpi, (int nIndex, UINT dpi));
DECL_WINDOWS_FUNCTION(static, HRESULT, AdjustWindowRectExForDpi, (LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi));

static struct _dpi_info {
    POINT cur_dpi;
    RECT new_wnd_rect;
} dpi_info;

static HBITMAP caretbm;

#ifdef MOD_HYPERLINK
#include "urlhack.h"
static int urlhack_cursor_is_hand = 0;
void urlhack_enable(void);
#endif
#ifdef MOD_SAVEDUMP
void SaveDump( void ) ;
#endif
#ifdef MOD_PERSO
#include <sys/types.h>
#include <process.h>
#include <math.h>
#include "../../kitty.h"
#include "../../kitty_commun.h"
#include "../../kitty_crypt.h"
#include "../../kitty_launcher.h"
#include "../../kitty_registry.h"
#include "../../kitty_ssh.h"
#include "../../kitty_tools.h"
#include "../../kitty_win.h"
extern int PuttyFlag ;
int GetCursorType() { return cursor_type ; }
void SetCursorType( const int ct ) { cursor_type = ct ; }
extern char BuildVersionTime[256] ;
void SendStrToTerminal( const char * str, const int len ) {
	char c ;
	int i ;
	if( len <= 0 ) return ;
//	if( term!=NULL ) term_seen_key_event(term) ;
	for( i=0 ; i<len ; i++ ) {
		c = (unsigned char)str[i] ;
		if (ldisc)
			//term_keyinputw(term, (const wchar_t *) &c, 1); //lpage_send(ldisc, CP_ACP, &c, 1, 1);
			term_keyinput(term, -1, &c, 1) ;
		}
	}
// resize en convertissant en nombre de lignes et colonnes
void resize( int height, int width ) { 
	int w,h;

	if( width!=-1 ) {
		prev_rows = term->rows;
		prev_cols = term->cols;

		w = width / font_width ;
		if (w < 1) w = 1;
		h = height / font_height ;
		if (h < 1) h = 1;
		}
	else {
		h = prev_rows ;
		w = prev_cols ;
		}

	term_size(term, h, w, conf_get_int(conf,CONF_savelines)) ; 
	reset_window(0);

	w = (width-conf_get_int(conf,CONF_window_border)*2) / font_width ;
	if (w < 1) w = 1;
	h = (height-conf_get_int(conf,CONF_window_border)*2) / font_height ;
	if (h < 1) h = 1;
	conf_set_int(conf,CONF_height,h); 
	conf_set_int(conf,CONF_width,w); 
	}
int Convert1Reg( const char * filename ) ;
int get_param( const char * val ) ;
void SendKeyboardPlus( HWND hwnd, const char * st ) ;
//`colours'
int return_offset_height(void) { return offset_height ; }
int return_offset_width(void) { return offset_width ; }
int return_font_height(void) { return font_height ; }
int return_font_width(void) { return font_width ; }
COLORREF return_colours258(void) { return colours[258]; }
// prototypes de fonctions decrites dans winnet.c
struct netscheduler_tag* netscheduler_new(void) ;
void netscheduler_free(struct netscheduler_tag* netscheduler) ;

void AddDynamicSFTPConnect( Conf *conf ) ;

/* String pour charger automatiquement au démarrage un fichier dans un editeur connecté */
static char *LoadFile = NULL ;

/* Flag pour interdire l'ouverture de boite configuration */
int force_reconf = 1 ;

// Reinitialiser le fichier de logs
void logfile_reinit(void *handle) ;

void set_cmd_line( const char * st ) ;

/* Flag to manage first connection */
bool first_connect = true ;

/* Start a session with conf structure */
void RunSessionWithConfSettings( Conf *conf ) {
	char b[2048];
	char *cl=NULL;
        const char *argprefix;
	bool inherit_handles;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	HANDLE filemap = NULL;
        if (restricted_acl)
		argprefix = "&R";
	else
		argprefix = "";
	/*
	 * Allocate a file-mapping memory chunk for the
	 * config structure.
	 */
	SECURITY_ATTRIBUTES sa;
        strbuf *serbuf;
	void *p;
	int size;
        serbuf = strbuf_new();
	conf_serialise(BinarySink_UPCAST(serbuf), conf);
        size = serbuf->len;

	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = true;
	filemap = CreateFileMapping(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0, size, NULL);
	if (filemap && filemap != INVALID_HANDLE_VALUE) {
		p = MapViewOfFile(filemap, FILE_MAP_WRITE, 0, 0, size);
		if (p) {
		    memcpy(p, serbuf->s, size);
		    UnmapViewOfFile(p);
		}
	}
        strbuf_free(serbuf);
	inherit_handles = true;
	cl = dupprintf("putty %s&%p:%u", argprefix,filemap, (unsigned)size);
	GetModuleFileName(NULL, b, sizeof(b) - 1);
	si.cb = sizeof(si);
	si.lpReserved = NULL;
	si.lpDesktop = NULL;
	si.lpTitle = NULL;
	si.dwFlags = 0;
	si.cbReserved2 = 0;
	si.lpReserved2 = NULL;
	CreateProcess(b, cl, NULL, NULL, inherit_handles,NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi);
	CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
	if (filemap)
		CloseHandle(filemap);
	sfree(cl);
}
/* Start new session with current settings */
void RunSessionWithCurrentSettings( HWND hwnd, Conf * oldconf, const char * host, const char * user, const char * pass, const int port, const char * remotepath ) {
	Conf *newconf = conf_copy(oldconf) ;
	if( host != NULL ) { conf_set_str(newconf,CONF_host,host) ; }
	if( user != NULL ) { conf_set_str(newconf,CONF_username,user) ; }
	if( pass != NULL ) { conf_set_str(newconf,CONF_password,pass) ; }
	
#ifndef MOD_NOPASSWORD
	char pst[4096] ;
	strcpy( pst, conf_get_str(newconf, CONF_password ) ) ;
	MASKPASS(GetCryptSaltFlag(), pst);
	if( pass != NULL ) { strcpy( pst, pass ) ; }
	conf_set_str( newconf, CONF_password, pst ) ;
	memset(pst,0,strlen(pst));
#else
	conf_set_str( newconf, CONF_password, "" ) ;
#endif
	
	
	
	if( remotepath != NULL ) {
		char *buf=(char*)malloc(strlen(remotepath)+5);
		sprintf(buf,"cd %s",remotepath);
		conf_set_str(newconf,CONF_autocommand,buf) ; 
	} //else { conf_set_str(newconf,CONF_autocommand,"") ; }
	//if( port != 0 ) { conf_set_int(newconf,CONF_port,port) ; } else { conf_set_int(newconf,CONF_port,22) ; }

	if( conf_launchable(newconf) ) {
		RunSessionWithConfSettings( newconf ) ;
	} else {
		save_settings("__STARTUP",newconf) ;
		RunSession( hwnd, conf_get_str(conf,CONF_folder), "__STARTUP" ) ;
		del_settings("__STARTUP");
	}
	conf_free( newconf ) ;
}


#ifdef MOD_INTEGRATED_KEYGEN
int WINAPI KeyGen_WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show);
#endif
#ifdef MOD_INTEGRATED_AGENT
int WINAPI Agent_WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show);
#endif

#endif
#ifdef MOD_WTS
typedef enum _WTS_VIRTUAL_CLASS { WTSVirtualClientData, WTSVirtualFileHandle } WTS_VIRTUAL_CLASS; 		// WTS_VIRTUAL_CLASS is not definned in file wtsapi32.h !!!
#include <wtsapi32.h>
#endif
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
#include "../../kitty_image.h"
#endif
#ifdef MOD_RECONNECT
static time_t last_reconnect = 0;
void SetConnBreakIcon( HWND hwnd ) ;
static void close_session(void *ignored_context);
#endif
/* rutty: */
#ifdef MOD_RUTTY
#define IDM_SCRIPT (0x5100)
#define IDM_SCRIPTSEND (0x5110)
#define IDM_SCRIPTHALT (0x5120)

#include "script.h" 
ScriptData scriptdata; 

#include "script_win.c" 
#include "script.c" 

#endif  /* rutty */  
#ifdef MOD_PROXY
#include "kitty_proxy.h"
#endif

static int dbltime, lasttime, lastact;
static Mouse_Button lastbtn;

/* this allows xterm-style mouse handling. */
static bool send_raw_mouse = false;
static int wheel_accumulator = 0;

static bool pointer_indicates_raw_mouse = false;

static BusyStatus busy_status = BUSY_NOT;

static char *window_name, *icon_name;

static int compose_state = 0;

static UINT wm_mousewheel = WM_MOUSEWHEEL;

#define IS_HIGH_VARSEL(wch1, wch2) \
    ((wch1) == 0xDB40 && ((wch2) >= 0xDD00 && (wch2) <= 0xDDEF))
#define IS_LOW_VARSEL(wch) \
    (((wch) >= 0x180B && (wch) <= 0x180D) || /* MONGOLIAN FREE VARIATION SELECTOR */ \
     ((wch) >= 0xFE00 && (wch) <= 0xFE0F)) /* VARIATION SELECTOR 1-16 */

static bool wintw_setup_draw_ctx(TermWin *);
static void wintw_draw_text(TermWin *, int x, int y, wchar_t *text, int len,
                            unsigned long attrs, int lattrs, truecolour tc);
static void wintw_draw_cursor(TermWin *, int x, int y, wchar_t *text, int len,
                              unsigned long attrs, int lattrs, truecolour tc);
static void wintw_draw_trust_sigil(TermWin *, int x, int y);
static int wintw_char_width(TermWin *, int uc);
static void wintw_free_draw_ctx(TermWin *);
static void wintw_set_cursor_pos(TermWin *, int x, int y);
static void wintw_set_raw_mouse_mode(TermWin *, bool enable);
static void wintw_set_raw_mouse_mode_pointer(TermWin *, bool enable);
#ifdef MOD_PERSO
static void wintw_set_focus_reporting_mode(TermWin *, bool enable);
#endif
static void wintw_set_scrollbar(TermWin *, int total, int start, int page);
static void wintw_bell(TermWin *, int mode);
static void wintw_clip_write(
    TermWin *, int clipboard, wchar_t *text, int *attrs,
    truecolour *colours, int len, bool must_deselect);
static void wintw_clip_request_paste(TermWin *, int clipboard);
static void wintw_refresh(TermWin *);
static void wintw_request_resize(TermWin *, int w, int h);
static void wintw_set_title(TermWin *, const char *title);
static void wintw_set_icon_title(TermWin *, const char *icontitle);
static void wintw_set_minimised(TermWin *, bool minimised);
static void wintw_set_maximised(TermWin *, bool maximised);
static void wintw_move(TermWin *, int x, int y);
static void wintw_set_zorder(TermWin *, bool top);
static void wintw_palette_set(TermWin *, unsigned, unsigned, const rgb *);
static void wintw_palette_get_overrides(TermWin *, Terminal *);

static const TermWinVtable windows_termwin_vt = {
    .setup_draw_ctx = wintw_setup_draw_ctx,
    .draw_text = wintw_draw_text,
    .draw_cursor = wintw_draw_cursor,
    .draw_trust_sigil = wintw_draw_trust_sigil,
    .char_width = wintw_char_width,
    .free_draw_ctx = wintw_free_draw_ctx,
    .set_cursor_pos = wintw_set_cursor_pos,
    .set_raw_mouse_mode = wintw_set_raw_mouse_mode,
#ifdef MOD_PERSO
    .set_focus_reporting_mode = wintw_set_focus_reporting_mode,
#endif
    .set_raw_mouse_mode_pointer = wintw_set_raw_mouse_mode_pointer,
    .set_scrollbar = wintw_set_scrollbar,
    .bell = wintw_bell,
    .clip_write = wintw_clip_write,
    .clip_request_paste = wintw_clip_request_paste,
    .refresh = wintw_refresh,
    .request_resize = wintw_request_resize,
    .set_title = wintw_set_title,
    .set_icon_title = wintw_set_icon_title,
    .set_minimised = wintw_set_minimised,
    .set_maximised = wintw_set_maximised,
    .move = wintw_move,
    .set_zorder = wintw_set_zorder,
    .palette_set = wintw_palette_set,
    .palette_get_overrides = wintw_palette_get_overrides,
};

static TermWin wintw[1];
static HDC wintw_hdc;

static HICON trust_icon = INVALID_HANDLE_VALUE;

const bool share_can_be_downstream = true;
const bool share_can_be_upstream = true;

static bool is_utf8(void)
{
    return ucsdata.line_codepage == CP_UTF8;
}

static bool win_seat_is_utf8(Seat *seat)
{
    return is_utf8();
}

static char *win_seat_get_ttymode(Seat *seat, const char *mode)
{
    return term_get_ttymode(term, mode);
}

static StripCtrlChars *win_seat_stripctrl_new(
    Seat *seat, BinarySink *bs_out, SeatInteractionContext sic)
{
    return stripctrl_new_term(bs_out, false, 0, term);
}

static size_t win_seat_output(
    Seat *seat, bool is_stderr, const void *, size_t);
static bool win_seat_eof(Seat *seat);
static int win_seat_get_userpass_input(
    Seat *seat, prompts_t *p, bufchain *input);
static void win_seat_notify_remote_exit(Seat *seat);
static void win_seat_connection_fatal(Seat *seat, const char *msg);
static void win_seat_update_specials_menu(Seat *seat);
static void win_seat_set_busy_status(Seat *seat, BusyStatus status);
static bool win_seat_set_trust_status(Seat *seat, bool trusted);
static bool win_seat_get_cursor_position(Seat *seat, int *x, int *y);
static bool win_seat_get_window_pixel_size(Seat *seat, int *x, int *y);

static const SeatVtable win_seat_vt = {
    .output = win_seat_output,
    .eof = win_seat_eof,
    .get_userpass_input = win_seat_get_userpass_input,
    .notify_remote_exit = win_seat_notify_remote_exit,
    .connection_fatal = win_seat_connection_fatal,
    .update_specials_menu = win_seat_update_specials_menu,
    .get_ttymode = win_seat_get_ttymode,
    .set_busy_status = win_seat_set_busy_status,
    .verify_ssh_host_key = win_seat_verify_ssh_host_key,
    .confirm_weak_crypto_primitive = win_seat_confirm_weak_crypto_primitive,
    .confirm_weak_cached_hostkey = win_seat_confirm_weak_cached_hostkey,
    .is_utf8 = win_seat_is_utf8,
    .echoedit_update = nullseat_echoedit_update,
    .get_x_display = nullseat_get_x_display,
    .get_windowid = nullseat_get_windowid,
    .get_window_pixel_size = win_seat_get_window_pixel_size,
    .stripctrl_new = win_seat_stripctrl_new,
    .set_trust_status = win_seat_set_trust_status,
    .verbose = nullseat_verbose_yes,
    .interactive = nullseat_interactive_yes,
    .get_cursor_position = win_seat_get_cursor_position,
};
#ifdef MOD_FAR2L
// "static" removed by far2l extensions support patch
// we need to access wgs from terminal.c to open dialog boxes, etc
WinGuiSeat wgs = { .seat.vt = &win_seat_vt,
#else
static WinGuiSeat wgs = { .seat.vt = &win_seat_vt,
#endif
                          .logpolicy.vt = &win_gui_logpolicy_vt };

#ifdef MOD_PERSO
Terminal* GetTerminal() { return term ; }
void do_eventlog( const char * st ) { lp_eventlog(&wgs.logpolicy,st); }
#endif
			  
static void start_backend(void)
{
    const struct BackendVtable *vt;
    char *error, *realhost;
    int i;

    /*
     * Select protocol. This is farmed out into a table in a
     * separate file to enable an ssh-free variant.
     */
    vt = backend_vt_from_proto(conf_get_int(conf, CONF_protocol));
    if (!vt) {
	char *str = dupprintf("%s Internal Error", appname);
	MessageBox(NULL, "Unsupported protocol number found",
		   str, MB_OK | MB_ICONEXCLAMATION);
	sfree(str);
	cleanup_exit(1);
    }

    seat_set_trust_status(&wgs.seat, true);
    error = backend_init(vt, &wgs.seat, &backend, logctx, conf,
                         conf_get_str(conf, CONF_host),
                         conf_get_int(conf, CONF_port),
                         &realhost,
                         conf_get_bool(conf, CONF_tcp_nodelay),
                         conf_get_bool(conf, CONF_tcp_keepalives));
    if (error) {
#ifdef MOD_RECONNECT
	char *str = dupprintf("%s Error", appname);
        char *msg = dupprintf("Unable to open connection to\n%s\n%s",
                              conf_dest(conf), error);
        sfree(error);
	if( GetAutoreconnectFlag() && conf_get_int(conf,CONF_failure_reconnect) && is_backend_first_connected ) {
	    lp_eventlog(&wgs.logpolicy, msg) ; 
        } else {
	    if( GetAutoreconnectFlag() && conf_get_int(conf,CONF_failure_reconnect) ) {
		if( MessageBox(NULL, msg, str, MB_ICONERROR | MB_RETRYCANCEL | MB_DEFBUTTON2) == IDCANCEL ) {
		    sfree(str);
		    sfree(msg);
		    exit(0);
		} else {
		    start_backend() ;
		    return ;
		}
	    } else {
	        MessageBox(NULL, msg, str, MB_OK);
	    }
        }
	sfree(str);
	sfree(msg);
	if( GetAutoreconnectFlag() && conf_get_int(conf,CONF_failure_reconnect) && is_backend_first_connected ) {
	    SetConnBreakIcon(wgs.term_hwnd) ;
	    SetSSHConnected(0) ;
	    queue_toplevel_callback(close_session, NULL);
	    session_closed = true;
	    lp_eventlog(&wgs.logpolicy, "Unable to connect, trying to reconnect...") ; 
	    SetTimer(wgs.term_hwnd, TIMER_RECONNECT, GetReconnectDelay()*1000, NULL) ; 
	    return ;
	}
	else
#else
	char *str = dupprintf("%s Error", appname);
        char *msg = dupprintf("Unable to open connection to\n%s\n%s",
                              conf_dest(conf), error);
        sfree(error);
	MessageBox(NULL, msg, str, MB_ICONERROR | MB_OK);
	sfree(str);
	sfree(msg);
#endif
	exit(0);
    }
    term_setup_window_titles(term, realhost);
    sfree(realhost);

    /*
     * Connect the terminal to the backend for resize purposes.
     */
    term_provide_backend(term, backend);

    /*
     * Set up a line discipline.
     */
    ldisc = ldisc_create(conf, term, backend, &wgs.seat);

    /*
     * Destroy the Restart Session menu item. (This will return
     * failure if it's already absent, as it will be the very first
     * time we call this function. We ignore that, because as long
     * as the menu item ends up not being there, we don't care
     * whether it was us who removed it or not!)
     */
    for (i = 0; i < lenof(popup_menus); i++) {
	DeleteMenu(popup_menus[i].menu, IDM_RESTART, MF_BYCOMMAND);
    }

    session_closed = false;
/* rutty: */
#ifdef MOD_RUTTY
    script_init(&scriptdata, conf);
    if(conf_get_int(conf, CONF_script_mode) == SCRIPT_PLAY && !filename_is_null(conf_get_filename(conf, CONF_script_filename)))
      script_sendfile(&scriptdata, conf_get_filename(conf, CONF_script_filename));
	else if(conf_get_int(conf, CONF_script_mode) == SCRIPT_RECORD && !filename_is_null(conf_get_filename(conf, CONF_script_filename)))
      script_record(&scriptdata, conf_get_filename(conf, CONF_script_filename));
#endif  /* rutty */

}

static void close_session(void *ignored_context)
{
    char *newtitle;
    int i;

    session_closed = true;
    newtitle = dupprintf("%s (inactive)", appname);
#ifdef MOD_PERSO
	newtitle = dupprintf("%s (inactive)", conf_get_str(conf,CONF_wintitle));
#endif
    win_set_icon_title(wintw, newtitle);
    win_set_title(wintw, newtitle);
    sfree(newtitle);

    if (ldisc) {
	ldisc_free(ldisc);
	ldisc = NULL;
    }
    if (backend) {
        backend_free(backend);
        backend = NULL;
        term_provide_backend(term, NULL);
        seat_update_specials_menu(&wgs.seat);
    }

    /*
     * Show the Restart Session menu item. Do a precautionary
     * delete first to ensure we never end up with more than one.
     */
    for (i = 0; i < lenof(popup_menus); i++) {
	DeleteMenu(popup_menus[i].menu, IDM_RESTART, MF_BYCOMMAND);
	InsertMenu(popup_menus[i].menu, IDM_DUPSESS, MF_BYCOMMAND | MF_ENABLED,
		   IDM_RESTART, "&Restart Session");
    }
}

#ifdef MOD_RECONNECT
void RestartSession( void ) {
	if( backend ) { 
		SetSSHConnected(0) ; 
		queue_toplevel_callback(close_session, NULL) ; backend = NULL ; 
	}
	if( GetAutoreconnectFlag() ) {
		lp_eventlog(&wgs.logpolicy, "User request session restart..." ) ;
	} else {
		win_seat_connection_fatal(  &wgs.seat, "User request session restart..." ) ;
		SetTimer(wgs.term_hwnd, TIMER_RECONNECT, 10, NULL) ;
	}
	
	PostMessage(wgs.term_hwnd,WM_KEYDOWN,VK_RETURN ,0) ;
	PostMessage(wgs.term_hwnd,WM_KEYUP,VK_RETURN ,1) ;
}
#endif

const unsigned cmdline_tooltype =
    TOOLTYPE_HOST_ARG |
    TOOLTYPE_PORT_ARG |
    TOOLTYPE_NO_VERBOSE_OPTION;

HINSTANCE hinst;

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    MSG msg;
    HRESULT hr;
    int guess_width, guess_height;
	
    dll_hijacking_protection();
#ifdef MOD_ZMODEM
	struct netscheduler_tag *netsc = NULL ;
#endif

    hinst = inst;

    sk_init();

    init_common_controls();

    /* Set Explicit App User Model Id so that jump lists don't cause
       PuTTY to hang on to removable media. */

    set_explicit_app_user_model_id();

    /* Ensure a Maximize setting in Explorer doesn't maximise the
     * config box. */
    defuse_showwindow();

    init_winver();

    /*
     * If we're running a version of Windows that doesn't support
     * WM_MOUSEWHEEL, find out what message number we should be
     * using instead.
     */
    if (osMajorVersion < 4 ||
	(osMajorVersion == 4 && osPlatformId != VER_PLATFORM_WIN32_NT))
	wm_mousewheel = RegisterWindowMessage("MSWHEEL_ROLLMSG");

    init_help();

    init_winfuncs();
    conf = conf_new();
#ifdef MOD_HYPERLINK
    urlhack_init();
#endif
#ifdef MOD_PERSO
    // Initialisation specifique a KiTTY
    set_cmd_line( cmdline ) ;
    SethInstIcons( hinst ) ;
    InitWinMain();
    if( debug_flag ) {
      if( IniFileFlag == SAVEMODE_REG ) { debug_logevent( "Save mode is set to registry (classic)" ) ; }
      if( IniFileFlag == SAVEMODE_DIR ) { debug_logevent( "Save mode is set to directory (portable)" ) ; }
      debug_logevent( "Command-line: %s", cmdline ) ;
      debug_logevent( "Version: %s", BuildVersionTime ) ;
    }
#endif
#ifdef MOD_NOTRANSPARENCY
	SetTransparencyFlag(0);
#endif

    /*
     * Initialize COM.
     */
    hr = CoInitialize(NULL);
    if (hr != S_OK && hr != S_FALSE) {
        char *str = dupprintf("%s Fatal Error", appname);
	MessageBox(NULL, "Failed to initialize COM subsystem",
		   str, MB_OK | MB_ICONEXCLAMATION);
	sfree(str);
	return 1;
    }

    /*
     * Process the command line.
     */
    {
	char *p;
        bool special_launchable_argument = false;

        settings_set_default_protocol(be_default_protocol);
	/* Find the appropriate default port. */
	{
            const struct BackendVtable *vt =
                backend_vt_from_proto(be_default_protocol);
            settings_set_default_port(0); /* illegal */
            if (vt)
                settings_set_default_port(vt->default_port);
	}
	conf_set_int(conf, CONF_logtype, LGTYP_NONE);
	do_defaults(NULL, conf);

	p = cmdline;
	/*
	 * Process a couple of command-line options which are more
	 * easily dealt with before the line is broken up into words.
	 * These are the old-fashioned but convenient @sessionname and
	 * the internal-use-only &sharedmemoryhandle, plus the &R
	 * prefix for -restrict-acl, all of which are used by PuTTYs
	 * auto-launching each other via System-menu options.
	 */
	while (*p && isspace(*p))
	    p++;
        if (*p == '&' && p[1] == 'R' &&
            (!p[2] || p[2] == '@' || p[2] == '&')) {
            /* &R restrict-acl prefix */
            restrict_process_acl();
            p += 2;
        }

	if (*p == '@') {
            /*
             * An initial @ means that the whole of the rest of the
             * command line should be treated as the name of a saved
             * session, with _no quoting or escaping_. This makes it a
             * very convenient means of automated saved-session
             * launching, via IDM_SAVEDSESS or Windows 7 jump lists.
             */
	    int i = strlen(p);
	    while (i > 1 && isspace(p[i - 1]))
		i--;
	    p[i] = '\0';
#ifdef MOD_PERSO
	if( GetDirectoryBrowseFlag() ) {
		char *pst = strstr( p+1, "/" ) ;
		if( pst!=NULL ) {
			pst[0]='\0';
			char *name = (char*)malloc( strlen(p)+1 ) ;
			strcpy( name, p+1 ) ;
			conf_set_str(conf,CONF_folder,name) ;
			SetSessPath( name ) ;
			SetInitCurrentFolder( name ) ;
			free(name) ;
			pst[0]='@' ;
			p=pst;
		}
		char * pfolder ;
		if( (pfolder=strstr(p+1," -folder \"")) ) {
			pfolder[0]='\0';
			pfolder += 10 ;
			pfolder[strlen(pfolder)-1]='\0';
			SetSessPath( pfolder ) ;
			SetInitCurrentFolder( pfolder ) ;
			}
		}
#endif
	    do_defaults(p + 1, conf);
            if (!conf_launchable(conf) && !do_config(conf)) {
		cleanup_exit(0);
	    }
            special_launchable_argument = true;
	} else if (*p == '&') {
	    /*
	     * An initial & means we've been given a command line
	     * containing the hex value of a HANDLE for a file
	     * mapping object, which we must then interpret as a
	     * serialised Conf.
	     */
	    HANDLE filemap;
	    void *cp;
	    unsigned cpsize;
	    if (sscanf(p + 1, "%p:%u", &filemap, &cpsize) == 2 &&
		(cp = MapViewOfFile(filemap, FILE_MAP_READ,
				    0, 0, cpsize)) != NULL) {
                BinarySource src[1];
                BinarySource_BARE_INIT(src, cp, cpsize);
		if (!conf_deserialise(conf, src))
                    modalfatalbox("Serialised configuration data was invalid");
		UnmapViewOfFile(cp);
		CloseHandle(filemap);
            } else if (!do_config(conf)) {
		cleanup_exit(0);
	    }
            special_launchable_argument = true;
	} else if (!*p) {
            /* Do-nothing case for an empty command line - or rather,
             * for a command line that's empty _after_ we strip off
             * the &R prefix. */
        } else {
	    /*
	     * Otherwise, break up the command line and deal with
	     * it sensibly.
	     */
	    int argc, i;
	    char **argv;
	    
	    split_into_argv(cmdline, &argc, &argv, NULL);

	    for (i = 0; i < argc; i++) {
		char *p = argv[i];
		int ret;

		ret = cmdline_process_param(p, i+1<argc?argv[i+1]:NULL,
					    1, conf);
		if (ret == -2) {
		    cmdline_error("option \"%s\" requires an argument", p);
		} else if (ret == 2) {
		    i++;	       /* skip next argument */
		} else if (ret == 1) {
		    continue;	       /* nothing further needs doing */
#ifdef MOD_PERSO
#ifndef MOD_NOPASSWORD
		} else if( !strcmp(p, "-pass") ) {
			i++ ;
			char bufpass[1024] ;
			strcpy( bufpass, argv[i] );
			MASKPASS( GetCryptSaltFlag(), bufpass ) ;
			conf_set_str( conf, CONF_password, bufpass ) ;
			memset( bufpass, 0, strlen(bufpass) ) ;
			memset( argv[i], 0, strlen(argv[i]) ) ;
#endif
		} else if( !strcmp(p, "-auto_store_sshkey") || !strcmp(p, "-auto-store-sshkey") ) {
			SetAutoStoreSSHKeyFlag( 1 ) ;
		} else if( !strcmp(p, "-clearjumplist" ) ) {
			clear_jumplist();
			exit(0);
		} else if( !strcmp(p, "-bgcolor" ) ) {
			i++ ;
			int j = 2, r,g,b ;
			sscanf( argv[i], "%d:%d:%d", &r, &g, &b ) ;
			if( r<0 ) { r=0 ; } if( r>255 ) { r=255; }
			if( g<0 ) { g=0 ; } if( g>255 ) { g=255; }
			if( b<0 ) { b=0 ; } if( b>255 ) { b=255; }
			conf_set_int_int(conf, CONF_colours, j*3+0, r);
			conf_set_int_int(conf, CONF_colours, j*3+1, g);
			conf_set_int_int(conf, CONF_colours, j*3+2, b);
		} else if( !strcmp(p, "-cmd") ) {
			i++ ;
			conf_set_str( conf, CONF_autocommand, argv[i] ) ;
		} else if( !strcmp(p, "-codepage") ) {
			i++ ;
			conf_set_str( conf, CONF_line_codepage, argv[i] ) ;
		} else if( !strcmp(p, "-defini") ) {
			CreateIniFile( "kitty.template.ini" ) ;
			exit(0);
		} else if( !strcmp(p, "-rcmd") ) {
			i++ ;
			conf_set_str( conf, CONF_remote_cmd, argv[i] ) ;
		} else if( !strcmp(p, "-sftpconnect") ) {
			i++ ;
			conf_set_str( conf, CONF_sftpconnect, argv[i] ) ;
		} else if( !strcmp(p, "-debug") ) {
			debug_flag = 1 ;
		} else if( !strcmp(p, "-fullscreen") ) {
			conf_set_int( conf, CONF_fullscreen, 1 ) ;
		} else if( !strcmp(p, "-funky") ) {
			i++;
			conf_set_int( conf, CONF_funky_type, atoi(argv[1]) ) ;
		} else if( !strcmp(p, "-initdelay") ) {
			i++ ;
			init_delay = (int)(1000*atof( argv[i] )) ;
		} else if( !strcmp(p, "-icon") ) {
			i++;
			conf_set_int( conf, CONF_icone, atoi(argv[i])) ;
		} else if( !strcmp(p, "-iconfile") ) {
			i++;
			if( existfile( argv[i] ) ) {
				conf_set_filename( conf, CONF_iconefile,filename_from_str(argv[i])) ;
			}
		} else if( !strcmp(p, "-log") ) {
			i++ ;
			conf_set_filename( conf, CONF_logfilename,filename_from_str(argv[i])) ;
			conf_set_int( conf, CONF_logtype,1 ) ;
			conf_set_int( conf, CONF_logxfovr,1 ) ;
			conf_set_bool( conf, CONF_logflush, true ) ;
		} else if( !strcmp(p, "-nofiles") ) {
			SetNoKittyFileFlag( 1 ) ;
		} else if( !strcmp(p, "-edit") ) {
			i++ ;
			if( existfile(argv[i]) ) {
				LoadFile = (char*) malloc( strlen(argv[i])+1 ) ;
				strcpy( LoadFile, argv[i] ) ;
				RunPuttyEd( wgs.term_hwnd, LoadFile ) ; 
				free( LoadFile ) ;
			} else {
				MessageBox(wgs.term_hwnd,"Unable to find requested file","Error",MB_OK|MB_ICONERROR) ;
			}
			exit( 0 ) ;
		} else if( !strcmp(p, "-folder") ) {
			char *pfolder ;
			pfolder=p+7;
			pfolder[0]='\0';
			pfolder += 1 ;
			if( pfolder[0]=='\"' ) pfolder++;
			if( pfolder[strlen(pfolder)-1]=='\"' ) pfolder[strlen(pfolder)-1]='\0';
			if( GetDirectoryBrowseFlag() ) SetSessPath( pfolder ) ;
			SetInitCurrentFolder( pfolder ) ;
			i++ ;
		} else if( !strcmp(p, "-localproxy") ) {
			i++ ;
			conf_set_str( conf, CONF_host, "localhost" ) ;
			conf_set_int( conf, CONF_port, 23 ) ;
			conf_set_int( conf, CONF_protocol, I(PROT_TELNET) ) ;
			conf_set_int( conf, CONF_proxy_type, 5 ) ;
			conf_set_str( conf, CONF_proxy_host, "proxy" ) ;
			conf_set_int( conf, CONF_proxy_port, 80 ) ;
			conf_set_bool( conf, CONF_even_proxy_localhost, true ) ;
			conf_set_str( conf, CONF_proxy_telnet_command, argv[i] ) ;
			SetBackgroundImageFlag(0) ;
		} else if( !strcmp(p, "-loginscript") ) {
			i++ ;
			if( existfile( argv[i] ) ) {
				conf_set_filename( conf, CONF_scriptfile, filename_from_str(argv[i])) ;
				ReadInitScript(argv[i]);
				Filename * fn = filename_from_str( "" ) ;
				conf_set_filename(conf,CONF_scriptfile,fn) ;
				filename_free(fn) ;
			}

		} else if( !strcmp(p, "-version") || !strcmp(p, "-about") ) {
			showabout(wgs.term_hwnd) ; exit( 0 ) ;
		} else if( !strcmp(p, "-noctrltab") ) {
			SetCtrlTabFlag( 0 ) ;
		} else if( !strcmp(p, "-noicon") ) {
			SetIconeFlag( -1 ) ;
		} else if( !strcmp(p, "-noshortcuts") ) {
			SetShortcutsFlag( 0 ) ;
			SetMouseShortcutsFlag( 0 ) ;
		} else if( !strcmp(p, "-notrans") ) {
			SetTransparencyFlag( 0 ) ;
			conf_set_int(conf,CONF_transparencynumber, -1) ;
#ifdef MOD_RECONNECT
		} else if( !strcmp(p, "-autorecnt") ) {
			SetAutoreconnectFlag(1) ;
			conf_set_int(conf,CONF_wakeup_reconnect, 1) ;
			conf_set_int(conf,CONF_failure_reconnect, 1) ;
#endif
#ifdef MOD_SAVEDUMP
		} else if( !strcmp(p, "-savedump") ) {			
			SaveDump() ;
			return 0;
#endif
#ifdef MOD_RUTTY	
		} else if( !strcmp(p, "-norutty") ) {			
			SetRuttyFlag( 0 ) ;
#endif
#ifdef MOD_ADB	
		} else if( !strcmp(p, "-adb") ) {			
			SetADBFlag( 1 ) ;
#endif
#ifdef MOD_ZMODEM
		} else if( !strcmp(p, "-zmodem") ) {
			SetZModemFlag( 1 ) ;
		} else if( !strcmp(p, "-nozmodem") ) {
			SetZModemFlag( 0 ) ;
#endif
#ifdef MOD_HYPERLINK
#ifndef MOD_NOHYPERLINK
		} else if( !strcmp(p, "-hyperlink") ) {
			HyperlinkFlag = 1 ;
		} else if( !strcmp(p, "-nohyperlink") ) {
			HyperlinkFlag = 0 ;
		} else if( !strcmp(p, "-hyperlinkfix") ) {
			FixWrongRegex();
#endif
#endif
		} else if( !strcmp(p, "-xpos") ) {
			i++ ;
			if( atoi(argv[i])>=0 ) { 
				conf_set_int( conf, CONF_xpos,atoi(argv[i]));
				if( conf_get_int( conf, CONF_ypos)<0 ) conf_set_int( conf, CONF_ypos,0);
				conf_set_bool( conf, CONF_save_windowpos, true ) ;
				}
		} else if( !strcmp(p, "-ypos") ) {
			i++ ;
			if( atoi(argv[i])>=0 ) { 
				conf_set_int( conf, CONF_ypos, atoi(argv[i]));
				if( conf_get_int( conf, CONF_xpos)<0) conf_set_int( conf, CONF_xpos,0);
				conf_set_bool( conf, CONF_save_windowpos, true ) ;
				}
		} else if( !strcmp(p, "-mungestr") ) {
			i++ ;
			char *b = (char*)malloc(4*strlen(argv[i])+1);
			mungestr(argv[i],b);
			MessageBox(NULL,b,"mungestr",MB_OK);
			SetTextToClipboard(b);
			free(b) ;
			return 0;
		} else if( !strcmp(p, "-unmungestr") ) {
			i++ ;
			char *b = (char*)malloc(4*strlen(argv[i])+1);
			unmungestr(argv[i],b,MAX_VALUE_NAME) ;
			MessageBox(NULL,b,"unmungestr",MB_OK);
			SetTextToClipboard(b);
			free(b);
			return 0;
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
		} else if( !strcmp(p, "-nobgimage") ) {
			SetBackgroundImageFlag(0) ;
		} else if( !strcmp(p, "-bgimage") ) {
			SetBackgroundImageFlag(1) ;
#endif
		} else if( !strcmp(p, "-putty") ) {
			SetAutoStoreSSHKeyFlag( 0 ) ;
			SetUserPassSSHNoSave( 1 ) ;
			SetNoKittyFileFlag( 1 ) ;
			HyperlinkFlag = 0 ;
			SetIconeFlag( -1 ) ;
			SethInstIcons( hinst ) ; 
			SetTransparencyFlag( 0 ) ;
			conf_set_int(conf,CONF_transparencynumber, -1) ;
			SetShortcutsFlag( 0 ) ;
			SetMouseShortcutsFlag( 0 ) ;
			SetSizeFlag( 0 ) ;
			SetWinrolFlag( 0 ) ;
			SetCapsLockFlag( 0 ) ;
			SetWinHeight( -1 ) ;
			SetConfigBoxHeight( 7 ) ;
			SetCtrlTabFlag( 0 ) ;
			SetRuttyFlag( 0 ) ;
			SetDefaultSettingsFlag(1);
			SetReadOnlyFlag(0);
#ifdef MOD_PROXY
			SetProxySelectionFlag(0);
#endif
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
			SetBackgroundImageFlag(0) ;
#endif
#ifdef MOD_RECONNECT
			SetAutoreconnectFlag( 0 ) ;
#endif
#ifdef MOD_ADB
			SetADBFlag(0);
#endif
			SetSessionFilterFlag( 0 ) ;
			PuttyFlag = 1 ;
			//SetWindowPos(GetForegroundWindow(), HWND_TOP, 0, 0, 0, 252, SWP_SHOWWINDOW|SWP_NOMOVE);
		} else if( !strcmp(p, "-readonly") ) {
			SetDefaultSettingsFlag(0);
			SetReadOnlyFlag(1);
		} else if( !strcmp(p, "-send-to-tray") ) {
			SetAutoSendToTray( 1 ) ;
		} else if( !strcmp(p, "-sshhandler") ) {
			CreateSSHHandler() ; return 0 ;
		} else if( !strcmp(p, "-fileassoc") ) {
			CreateFileAssoc() ; return 0 ;
		} else if( !strcmp(p, "-ed") ) {
			char buffer[1024];
			sprintf(buffer, "%s|%s|0|0", (char*)get_param_str("INI"), (char*)get_param_str("SAV") ) ;
			return Notepad_WinMain(inst, prev, buffer, show) ;
		} else if( !strcmp(p, "-edb") ) {
			i++;
			if( existfile(argv[i]) || !strcmp(argv[i],"1") ) {
				char buffer[1024];
				sprintf(buffer, "%s|%s|%s|0", (char*)get_param_str("INI"), (char*)get_param_str("SAV"), argv[i] ) ;
				return Notepad_WinMain(inst, prev, buffer, show) ;
			} else {
				MessageBox(wgs.term_hwnd,"Unable to find requested file","Error",MB_OK|MB_ICONERROR) ;
			}
			exit(0);
		} else if( !strcmp(p, "-help") || !strcmp(p, "-h") ) {
			char *hmsg ;
			if( debug_flag ) {
				hmsg=(char*)malloc(strlen(GetHelpMessage())+1024);
				sprintf(hmsg,"%s",GetHelpMessage());
				strcat(hmsg,"\r\nDebug Mode\r\n\r\n");
				strcat(hmsg,"* -mungestr\r\n");
				strcat(hmsg,"* -unmungestr\r\n");
				
			} else {
				hmsg = GetHelpMessage() ;
			}
			SetTextToClipboard(hmsg) ;
			char buffer[1024];
			sprintf(buffer, "%s|%s|1|1", (char*)get_param_str("INI"), (char*)get_param_str("SAV") ) ;
			if( hmsg!=GetHelpMessage()) { free(hmsg); }
			return Notepad_WinMain(inst, prev, buffer, show) ;
#ifdef MOD_LAUNCHER
		} else if( !strcmp(p, "-launcher") ) {
			char *np;
			np=strstr(cmdline,"-launcher")+9;
			return Launcher_WinMain(inst, prev, np, show) ;
#endif
#ifdef MOD_INTEGRATED_KEYGEN
		} else if( !strcmp(p, "-keygen") ) {
			char *np;
			np=strstr(cmdline,"-keygen")+7;
			return KeyGen_WinMain(inst, prev, np, show) ;
#endif
#ifdef MOD_INTEGRATED_AGENT
		} else if( !strcmp(p, "-runagent") ) {
			char *np;
			np=strstr(cmdline,"-runagent")+9;
			return Agent_WinMain(inst, prev, np, show) ;
#endif
		} else if( !strcmp(p, "-convert-dir") ) {
			return Convert2Dir( ConfigDirectory ) ;
		} else if( !strcmp(p, "-convert-reg") ) {		// NE FONCTIONNE PAS / preferer le parametre -convert1reg
			return Convert2Reg( ConfigDirectory ) ;
		} else if( !strcmp(p, "-convert1reg") ) {
			i++ ;
			return Convert1Reg(argv[i]) ;
		} else if( !strcmp(p, "-title") ) {
			i++ ;
			conf_set_str( conf, CONF_wintitle, argv[i] );
#ifndef FLJ
		} else if( !strcmp(p, "-classname") ) {
			i++ ;
			if( strlen( argv[i] ) > 0 ) {
				strcpy( KiTTYClassName, argv[i] ) ;
				appname = KiTTYClassName ;
				}
#endif
		} else if( !strcmp(p, "-sendcmd") ) {
			i++ ;
			if( strlen(argv[i])>0 )
				SendCommandAllWindows( wgs.term_hwnd, argv[i] ) ;
			exit(0);
		} else if( !strcmp(p, "-kload") ) {
			i++ ;
			if( strlen(argv[i])>0 ) {
				load_open_settings_forced( argv[i], conf ) ;
				special_launchable_argument = true ;
			}
		} else if( !strcmp(p, "-loadfile") ) {
			i++ ;
			if( strlen(argv[i])>0 ) {
				load_open_settings_forced( argv[i], conf ) ;
				special_launchable_argument = true ;
			}
#endif
		} else if (!strcmp(p, "-cleanup")) {
		    /*
		     * `putty -cleanup'. Remove all registry
		     * entries associated with PuTTY, and also find
		     * and delete the random seed file.
		     */
		    char *s1, *s2;
		    s1 = dupprintf("This procedure will remove ALL Registry entries\n"
				   "associated with %s, and will also remove\n"
				   "the random seed file. (This only affects the\n"
				   "currently logged-in user.)\n"
				   "\n"
				   "THIS PROCESS WILL DESTROY YOUR SAVED SESSIONS.\n"
				   "Are you really sure you want to continue?",
				   appname);
		    s2 = dupprintf("%s Warning", appname);
                    if (message_box(NULL, s1, s2,
				    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2,
				    HELPCTXID(option_cleanup)) == IDYES) {
			cleanup_all();
		    }
		    sfree(s1);
		    sfree(s2);
		    exit(0);
		} else if (!strcmp(p, "-pgpfp")) {
                    pgp_fingerprints_msgbox(NULL);
		    exit(1);
		} else if (*p != '-') {
		    cmdline_error("unexpected argument \"%s\"", p);
		} else {
		    cmdline_error("unknown option \"%s\"", p);
		}
	    }
	}
#ifdef MOD_PERSO
	// Creation du fichier kitty.ini par defaut si besoin
	CreateDefaultIniFile() ;
#endif
	cmdline_run_saved(conf);

	/*
         * Bring up the config dialog if the command line hasn't
         * (explicitly) specified a launchable configuration.
         */
        if (!(special_launchable_argument || cmdline_host_ok(conf))) {
            if (!do_config(conf))
                cleanup_exit(0);
	}

        prepare_session(conf);
    }

#ifdef MOD_PERSO
    /* Generate a dump file just after load the configuration to be able to analyse crashes */
    //SaveDump() ;
#endif
#ifdef MOD_PERSO
    if( debug_flag ) {
      if( strlen(conf_get_str(conf,CONF_sessionname))>0 ) {
        debug_logevent( "Session name: %s", conf_get_str(conf,CONF_sessionname) ) ;
      } else {
        debug_logevent( "Session name: -" ) ;
      }
    }
	HICON icon_m ;
	if( GetIconeFlag() > 0 ) 
		icon_m = LoadIcon( GethInstIcons(), MAKEINTRESOURCE(IDI_MAINICON_0 + GetIconeNum() ) );
	else
		icon_m = LoadIcon(inst, MAKEINTRESOURCE(IDI_MAINICON));
int xpos_init=0, ypos_init=0 ;
	if( (conf_get_bool(conf,CONF_saveonexit)||conf_get_bool(conf,CONF_save_windowpos) )
		/*&& (conf_get_int(conf,CONF_xpos)>=0) && (conf_get_int(conf,CONF_ypos)>=0)*/ ) {
		xpos_init=conf_get_int(conf,CONF_xpos) ;
		ypos_init=conf_get_int(conf,CONF_ypos) ;
		}

while( conf_get_int(conf,CONF_icone) > GetNumberOfIcons() ) { 
    conf_set_int( conf, CONF_icone, conf_get_int( conf, CONF_icone) - GetNumberOfIcons() ) ;
}
    
if( conf_get_int(conf,CONF_icone) == 0 ) {
	if( GetIconeFlag() > 0 ) SetIconeNum( ( GetCurrentProcessId() * time( NULL ) ) % GetNumberOfIcons() ) ; else SetIconeNum( 0 ) ; 
} else{
	if( GetIconeFlag() > 0 ) SetIconeNum( conf_get_int(conf,CONF_icone) - 1 ) ; else SetIconeNum( 0 ) ; 
}
#endif

    if (!prev) {
        WNDCLASSW wndclass;

	wndclass.style = 0;
	wndclass.lpfnWndProc = WndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
#ifdef MOD_PERSO
	if( conf_get_int(conf, CONF_ctrl_tab_switch) && GetCtrlTabFlag() )
	    wndclass.cbWndExtra += 8;
#endif
	wndclass.hInstance = inst;
#ifdef MOD_PERSO
	wndclass.hIcon = icon_m;
#else
	wndclass.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_MAINICON));
#endif
	wndclass.hCursor = LoadCursor(NULL, IDC_IBEAM);
	wndclass.hbrBackground = NULL;
	wndclass.lpszMenuName = NULL;
#if (defined MOD_PERSO) && (!defined FLJ)
	wndclass.lpszClassName = dup_mb_to_wc(DEFAULT_CODEPAGE, 0, KiTTYClassName);
#else
	wndclass.lpszClassName = dup_mb_to_wc(DEFAULT_CODEPAGE, 0, appname);
#endif
	    
	RegisterClassW(&wndclass);
    }

#ifdef MOD_PERSO
// Initialisation de la structure NOTIFYICONDATA
TrayIcone.cbSize = sizeof(TrayIcone);					// On alloue la taille necessaire pour la structure
TrayIcone.uID = IDI_MAINICON_0 ;					// On lui donne un ID
TrayIcone.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;			// On lui indique les champs valables
	// On lui dit qu'il devra "ecouter" son environement (clique de souris, etc)
TrayIcone.uCallbackMessage = MYWM_NOTIFYICON;
TrayIcone.hIcon = LoadIcon(NULL, NULL);					// On ne load aucune icone pour le moment
//TrayIcone.szTip[1024] = "PuTTY That\'s all folks!\0" ;			// Le tooltip par defaut, soit rien
strcpy( TrayIcone.szTip, "PuTTY That\'s all folks!\0") ;			// Le tooltip par defaut, soit rien
TrayIcone.hWnd = wgs.term_hwnd ;
#endif

    memset(&ucsdata, 0, sizeof(ucsdata));

    conf_cache_data();

    /*
     * Guess some defaults for the window size. This all gets
     * updated later, so we don't really care too much. However, we
     * do want the font width/height guesses to correspond to a
     * large font rather than a small one...
     */

    font_width = 10;
    font_height = 20;
    extra_width = 25;
    extra_height = 28;
    guess_width = extra_width + font_width * conf_get_int(conf, CONF_width);
    guess_height = extra_height + font_height*conf_get_int(conf, CONF_height);
    {
	RECT r;
	get_fullscreen_rect(&r);
	if (guess_width > r.right - r.left)
	    guess_width = r.right - r.left;
	if (guess_height > r.bottom - r.top)
	    guess_height = r.bottom - r.top;
    }
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
    	const char* winname = appname;
#endif
    {
	int winmode = WS_OVERLAPPEDWINDOW | WS_VSCROLL;
	int exwinmode = 0;
        const struct BackendVtable *vt =
            backend_vt_from_proto(be_default_protocol);
        bool resize_forbidden = false;
        if (vt && vt->flags & BACKEND_RESIZE_FORBIDDEN)
            resize_forbidden = true;
        wchar_t *uappname = dup_mb_to_wc(DEFAULT_CODEPAGE, 0, appname);
	if (!conf_get_bool(conf, CONF_scrollbar))
	    winmode &= ~(WS_VSCROLL);
        if (conf_get_int(conf, CONF_resize_action) == RESIZE_DISABLED ||
            resize_forbidden)
	    winmode &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
	if (conf_get_bool(conf, CONF_alwaysontop))
	    exwinmode |= WS_EX_TOPMOST;
#ifdef MOD_TUTTY
	if (!conf_get_int(conf, CONF_window_has_sysmenu))
	    winmode &= ~(WS_SYSMENU);
	if (!conf_get_int(conf, CONF_window_minimizable))
	    winmode &= ~(WS_MINIMIZEBOX);
	if (!conf_get_int(conf, CONF_window_maximizable))
	    winmode &= ~(WS_MAXIMIZEBOX);
#endif
	if (conf_get_bool(conf, CONF_sunken_edge))
	    exwinmode |= WS_EX_CLIENTEDGE;
#ifdef MOD_PERSO
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
	// TODO: This is the beginning of some work to have windows with fancy
	// no-client-edge borders.  It's not ready yet.
	if( GetBackgroundImageFlag() && (!GetPuttyFlag()) )
	if(0)
	{
		winmode = WS_POPUP;
		exwinmode = 0;
		winname = NULL;

		// TODO: This is proof-of-concept.  For this to really work, we'll
		// have to do some additional mods, like creating our own title/move
		// window to glue to the top, and some kind of drag-resizing window
		// to glue to the bottom-right.  Otherwise there'll be no way to move
		// or resize the window, which will get old extremely quickly.  Finally,
		// this won't work as written anyway, b/c when you call SetWindowText
		// anywhere Win32 forces a window border to appear anyway.  So, we'll
		// want to create a new function, set_window_text, that checks whether
		// to really call SetWindowText or to set the window text in whatever
		// other location is necessary for our custom window text display for
		// when we're handling our own border here.
	}

	if( GetBackgroundImageFlag() && (!GetPuttyFlag()) ) {
		wgs.term_hwnd = CreateWindowExW(exwinmode|WS_EX_ACCEPTFILES, uappname, (wchar_t *)winname,
			      winmode, CW_USEDEFAULT, CW_USEDEFAULT,
			      guess_width, guess_height,
			      NULL, NULL, inst, NULL);
			      init_dc_blend();
		}
	else 
		wgs.term_hwnd = CreateWindowExW(exwinmode|WS_EX_ACCEPTFILES, uappname, uappname,
			      winmode, CW_USEDEFAULT, CW_USEDEFAULT,
			      guess_width, guess_height,
			      NULL, NULL, inst, NULL);
			      
	if( GetBackgroundImageFlag() ) init_dc_blend() ;
#else
	wgs.term_hwnd = CreateWindowExW(exwinmode|WS_EX_ACCEPTFILES, uappname, uappname,
			      winmode, CW_USEDEFAULT, CW_USEDEFAULT,
			      guess_width, guess_height,
			      NULL, NULL, inst, NULL);
#endif
#else
        wgs.term_hwnd = CreateWindowExW(
            exwinmode, uappname, uappname, winmode, CW_USEDEFAULT,
            CW_USEDEFAULT, guess_width, guess_height, NULL, NULL, inst, NULL);
        memset(&dpi_info, 0, sizeof(struct _dpi_info));
#endif
        init_dpi_info();
        sfree(uappname);
    }

    /*
     * Initialise the fonts, simultaneously correcting the guesses
     * for font_{width,height}.
     */
    init_fonts(0,0);

    /*
     * Prepare a logical palette.
     */
    init_palette();

    /*
     * Initialise the terminal. (We have to do this _after_
     * creating the window, since the terminal is the first thing
     * which will call schedule_timer(), which will in turn call
     * timer_change_notify() which will expect hwnd to exist.)
     */
    wintw->vt = &windows_termwin_vt;
    term = term_init(conf, &ucsdata, wintw);
    setup_clipboards(term, conf);
    logctx = log_init(&wgs.logpolicy, conf);
    term_provide_logctx(term, logctx);
    term_size(term, conf_get_int(conf, CONF_height),
	      conf_get_int(conf, CONF_width),
	      conf_get_int(conf, CONF_savelines));

    /*
     * Correct the guesses for extra_{width,height}.
     */
    {
	RECT cr, wr;
        GetWindowRect(wgs.term_hwnd, &wr);
        GetClientRect(wgs.term_hwnd, &cr);
	offset_width = offset_height = conf_get_int(conf, CONF_window_border);
	extra_width = wr.right - wr.left - cr.right + cr.left + offset_width*2;
	extra_height = wr.bottom - wr.top - cr.bottom + cr.top +offset_height*2;
    }

    /*
     * Resize the window, now we know what size we _really_ want it
     * to be.
     */
    guess_width = extra_width + font_width * term->cols;
    guess_height = extra_height + font_height * term->rows;
    SetWindowPos(wgs.term_hwnd, NULL, 0, 0, guess_width, guess_height,
		 SWP_NOMOVE | SWP_NOREDRAW | SWP_NOZORDER);
#ifdef MOD_PERSO
    if( !PuttyFlag )
    if( (conf_get_bool(conf,CONF_saveonexit)||conf_get_bool(conf,CONF_save_windowpos)) 
	/*&& (xpos_init>=0) && (ypos_init>=0)*/ ) {
	MoveWindow(wgs.term_hwnd, xpos_init, ypos_init, guess_width, guess_height, TRUE );
	}
#endif

    /*
     * Set up a caret bitmap, with no content.
     */
    {
	char *bits;
	int size = (font_width + 15) / 16 * 2 * font_height;
	bits = snewn(size, char);
	memset(bits, 0, size);
	caretbm = CreateBitmap(font_width, font_height, 1, 1, bits);
	sfree(bits);
    }
    CreateCaret(wgs.term_hwnd, caretbm, font_width, font_height);

    /*
     * Initialise the scroll bar.
     */
    {
	SCROLLINFO si;

	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
	si.nMin = 0;
	si.nMax = term->rows - 1;
	si.nPage = term->rows;
	si.nPos = 0;
        SetScrollInfo(wgs.term_hwnd, SB_VERT, &si, false);
    }

    /*
     * Prepare the mouse handler.
     */
    lastact = MA_NOTHING;
    lastbtn = MBT_NOTHING;
    dbltime = GetDoubleClickTime();

    /*
     * Set up the session-control options on the system menu.
     */
    {
	HMENU m;
	int j;
	char *str;

        popup_menus[SYSMENU].menu = GetSystemMenu(wgs.term_hwnd, false);
#ifdef MOD_TUTTY
	EnableMenuItem(popup_menus[SYSMENU].menu, SC_CLOSE, conf_get_int(conf,CONF_window_closable) ? MF_ENABLED : MF_GRAYED);
#endif
	popup_menus[CTXMENU].menu = CreatePopupMenu();
	AppendMenu(popup_menus[CTXMENU].menu, MF_ENABLED, IDM_COPY, "&Copy");
	AppendMenu(popup_menus[CTXMENU].menu, MF_ENABLED, IDM_PASTE, "&Paste");

	savedsess_menu = CreateMenu();
	get_sesslist(&sesslist, true);
	update_savedsess_menu();

	for (j = 0; j < lenof(popup_menus); j++) {
	    m = popup_menus[j].menu;

	    AppendMenu(m, MF_SEPARATOR, 0, 0);
#ifdef MOD_PERSO
		if( !PuttyFlag ) InitSpecialMenu( m, conf_get_str(conf,CONF_folder), conf_get_str(conf,CONF_sessionname) ) ;
#endif
/* rutty: */
#ifdef MOD_RUTTY 
        AppendMenu(m, MF_ENABLED, IDM_SCRIPTSEND, "Send recorded &script file" ) ;
        AppendMenu(m, MF_SEPARATOR, 0, 0);
#endif /* rutty */  
	    AppendMenu(m, MF_ENABLED, IDM_SHOWLOG, "&Event Log");
#ifdef MOD_PERSO
	AppendMenu(m, MF_ENABLED, IDM_EXPORTSETTINGS, "Export &current settings" ) ;
#endif
	    AppendMenu(m, MF_SEPARATOR, 0, 0);
	    AppendMenu(m, MF_ENABLED, IDM_NEWSESS, "Ne&w Session...");
	    AppendMenu(m, MF_ENABLED, IDM_DUPSESS, "&Duplicate Session");
#ifdef MOD_RECONNECT
	AppendMenu(m, MF_ENABLED, IDM_NEWDUPSESS, "New duplicated session...");
	AppendMenu(m, MF_ENABLED, IDM_RESTARTSESSION, "Close+Restart") ;
#endif
	    AppendMenu(m, MF_POPUP | MF_ENABLED, (UINT_PTR) savedsess_menu,
		       "Sa&ved Sessions");
	    AppendMenu(m, MF_ENABLED, IDM_RECONF, "Chan&ge Settings...");
	    AppendMenu(m, MF_SEPARATOR, 0, 0);
	    AppendMenu(m, MF_ENABLED, IDM_COPYALL, "C&opy All to Clipboard");
	    AppendMenu(m, MF_ENABLED, IDM_CLRSB, "C&lear Scrollback");
#ifdef MOD_PERSO
	if( conf_get_int(conf, CONF_logtype)!=LGTYP_NONE ) {
		AppendMenu(m, MF_ENABLED, IDM_CLEARLOGFILE, "Clear log file");
	} else {
		AppendMenu(m, MF_DISABLED|MF_GRAYED, IDM_CLEARLOGFILE, "Clear log file");
	}
#endif
	    AppendMenu(m, MF_ENABLED, IDM_RESET, "Rese&t Terminal");
	    AppendMenu(m, MF_SEPARATOR, 0, 0);
	    AppendMenu(m, (conf_get_int(conf, CONF_resize_action)
			   == RESIZE_DISABLED) ? MF_GRAYED : MF_ENABLED,
		       IDM_FULLSCREEN, "&Full Screen");
#ifdef MOD_PERSO
    if( !PuttyFlag ) {
        // if( !IsWow64() ) { AppendMenu(m, MF_ENABLED, IDM_PRINT, "Print clip&board") ; }  // Le menu print clipboard avait été desactivé un temps sur les machine 64bits
	AppendMenu(m, MF_ENABLED, IDM_PRINT, "Print clip&board") ;
        AppendMenu(m, MF_ENABLED, IDM_TOTRAY, "Send to tra&y");
        if( conf_get_bool(conf,CONF_alwaysontop) )
            AppendMenu(m, MF_ENABLED|MF_CHECKED, IDM_VISIBLE, "Always visi&ble");
        else
            AppendMenu(m, MF_ENABLED, IDM_VISIBLE, "Always visi&ble");
        AppendMenu(m, MF_ENABLED, IDM_PROTECT, "Prote&ct");
//char b[256];sprintf(b,"%d",GetWinrolFlag());MessageBox(NULL,b,"info",MB_OK);
        if( GetWinrolFlag() ) AppendMenu(m, MF_ENABLED, IDM_WINROL, "Roll-u&p");
	HMENU FontMenu = CreateMenu(); 
		AppendMenu(FontMenu, MF_ENABLED, IDM_FONTUP, "Font up");
		AppendMenu(FontMenu, MF_ENABLED, IDM_FONTDOWN, "Font down");
		AppendMenu(FontMenu, MF_ENABLED, IDM_FONTNEGATIVE, "Negative");
		AppendMenu(FontMenu, MF_ENABLED, IDM_FONTBLACKANDWHITE, "Black and White");
	AppendMenu(m, MF_POPUP | MF_ENABLED, (UINT) FontMenu, "Font settings");
	
	if( GetShortcutsFlag() )
            AppendMenu(m, MF_ENABLED|MF_CHECKED, IDM_SHORTCUTSTOGGLE, "Shortcuts toggle");
        else
            AppendMenu(m, MF_ENABLED, IDM_SHORTCUTSTOGGLE, "Shortcuts toggle");
	    
	AppendMenu(m, MF_SEPARATOR, 0, 0);
	
	AppendMenu(m, MF_ENABLED, IDM_SCRIPTFILE, "Send scr&ipt file" ) ;
        if( PSCPPath!=NULL ) AppendMenu(m, MF_ENABLED, IDM_PSCP, "Send wit&h pscp");
        else AppendMenu(m, MF_DISABLED|MF_GRAYED, IDM_PSCP, "Send wit&h pscp");
        if( WinSCPPath!=NULL ) AppendMenu(m, MF_ENABLED, IDM_WINSCP, "&Start WinSCP");
        else AppendMenu(m, MF_DISABLED|MF_GRAYED, IDM_WINSCP, "&Start WinSCP");
	
	
        AppendMenu(m, MF_ENABLED, IDM_SHOWPORTFWD, "Po&rt forwarding");
#ifdef MOD_PORTKNOCKING
	if( strlen(conf_get_str(conf,CONF_portknockingoptions))>0 ) {
		AppendMenu(m, MF_ENABLED, IDM_PORTKNOCK, "Port &knock");
	}
#endif
        }
#endif
#ifdef MOD_ZMODEM
	if( (!GetPuttyFlag()) && GetZModemFlag() ) {
	    AppendMenu(m, MF_SEPARATOR, 0, 0);
	    AppendMenu(m, term->xyz_transfering?MF_GRAYED:MF_ENABLED, IDM_XYZSTART, "&Zmodem Receive");
	    AppendMenu(m, term->xyz_transfering?MF_GRAYED:MF_ENABLED, IDM_XYZUPLOAD, "Zmodem &Upload");
	    AppendMenu(m, !term->xyz_transfering?MF_GRAYED:MF_ENABLED, IDM_XYZABORT, "Zmodem &Abort");
	}
#endif
	    AppendMenu(m, MF_SEPARATOR, 0, 0);
	    if (has_help())
		AppendMenu(m, MF_ENABLED, IDM_HELP, "&Help");
	    str = dupprintf("&About %s", appname);
	    AppendMenu(m, MF_ENABLED, IDM_ABOUT, str);
	    sfree(str);
	}
    }
#ifdef MOD_PERSO
	// SETTINGS specifique a la session
    
	if( !PuttyFlag ) {
		// Lancement automatique dans le Tray
		if( conf_get_int(conf,CONF_sendtotray) ) SetAutoSendToTray( 1 ) ;
			
		// Charge le fichier de script d'initialisation si il existe
		ScriptFileContent = NULL ;
		ReadInitScript( NULL ) ;
		
		static char reg_buffer[4096];
    
		// Lancement du serveur de chat
		if( ReadParameter( INIT_SECTION, "chat", reg_buffer ) ) {
			int chat_flag = atoi( reg_buffer ) ;
			if ( chat_flag > 0 ) {
				//if( chat_flag != 1 ) PORT = chat_flag ; 
				_beginthread( routine_server, 0, NULL ) ;
			}
		}
		
		// Parametrage specifique a la session
		if( GetSessionField( conf_get_str(conf,CONF_sessionname), conf_get_str(conf,CONF_folder), "InitDelay", reg_buffer ) ) {
			if( init_delay != (int)(1000*atof( reg_buffer ) ) ) { 
					init_delay = (int)(1000*atof( reg_buffer ) ) ; 
					conf_set_int(conf,CONF_initdelay,init_delay) ; 
				}
			}
		if( GetSessionField( conf_get_str(conf,CONF_sessionname), conf_get_str(conf,CONF_folder), "BCDelay", reg_buffer ) ) {
			if( between_char_delay != atof( reg_buffer ) ) { 
				between_char_delay = atof( reg_buffer ) ;
				conf_set_int(conf,CONF_bcdelay, atof( reg_buffer ) ); 
				}
			}

#ifndef MOD_NOTRANSPARENCY
		if( GetTransparencyFlag() && conf_get_int(conf,CONF_transparencynumber) != -1 ) {
			if( conf_get_int(conf,CONF_transparencynumber) > 0 ) { 
				SetTransparency( wgs.term_hwnd, 255-conf_get_int(conf,CONF_transparencynumber) ) ;
				}
			}
#endif
		// Lancement du timer auto-command pour les connexions non SSH
		if(conf_get_int(conf,CONF_protocol) != PROT_SSH) { is_backend_connected = 1 ; }
		SetTimer(wgs.term_hwnd, TIMER_INIT, init_delay, NULL) ;

		if( IniFileFlag == SAVEMODE_REG ) {
			sprintf( reg_buffer, "%s@%s:%d (prot=%d) name=%s", conf_get_str(conf,CONF_username), conf_get_str(conf,CONF_host), conf_get_int(conf,CONF_port), conf_get_int(conf,CONF_protocol), conf_get_str(conf,CONF_sessionname)) ;
			cryptstring( GetCryptSaltFlag(), reg_buffer, MASTER_PASSWORD ) ;
			WriteParameter( INIT_SECTION, "KiLastSe", reg_buffer ) ;
			}
		
		// Lancement des timer (changement image de fond, rafraichissement)
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
		if( (!GetBackgroundImageFlag()) || GetPuttyFlag() ) conf_set_int(conf,CONF_bg_type,0); 
		if( conf_get_int(conf,CONF_bg_type)!=0 ) {
			if(conf_get_int(conf,CONF_bg_slideshow)>0)
			SetTimer(wgs.term_hwnd, TIMER_SLIDEBG, (int)(conf_get_int(conf,CONF_bg_slideshow)*1000), NULL) ;
			else 
			if(ImageSlideDelay>0)
			SetTimer(wgs.term_hwnd, TIMER_SLIDEBG, (int)(ImageSlideDelay*1000), NULL) ;
			}

		// Lancement du rafraichissement toutes les 10 minutes (pour l'image de fond, pour pallier bug d'affichage)
		if( GetBackgroundImageFlag() && ReadParameter( INIT_SECTION, "redraw", reg_buffer ) ) {
			if( stricmp( reg_buffer, "NO" ) ) SetTimer(wgs.term_hwnd, TIMER_REDRAW, (int)(600*1000), NULL) ;
		} else SetTimer(wgs.term_hwnd, TIMER_REDRAW, (int)(600*1000), NULL) ;
#endif
		// Lancement du timer d'anti-idle
		SetTimer(wgs.term_hwnd, TIMER_ANTIIDLE, (int)(30*1000), NULL) ;

		} // fin de if( !PuttyFlag )
#ifdef MOD_HYPERLINK
else {
	conf_set_int(conf,CONF_url_underline,URLHACK_UNDERLINE_NEVER);
	}
#endif

#endif

#ifdef MOD_PROXY
if( GetProxySelectionFlag() ) {
	LoadProxyInfo( conf, conf_get_str(conf,CONF_proxyselection) ) ;
}
#endif
#ifdef MOD_PORTKNOCKING
ManagePortKnocking(conf_get_str(conf,CONF_host),conf_get_str(conf,CONF_portknockingoptions));
#endif
#ifdef MOD_PERSO
AddDynamicSFTPConnect( conf ) ;
#endif

    if (restricted_acl()) {
        lp_eventlog(&wgs.logpolicy, "Running with restricted process ACL");
    }

    winselgui_set_hwnd(wgs.term_hwnd);
    start_backend();
#ifdef MOD_RECONNECT
	if (conf_get_int(conf, CONF_protocol) != PROT_SSH) { is_backend_first_connected = 1 ; }
	last_reconnect = time(NULL);
#endif
#ifdef MOD_HYPERLINK
	/*
	 * HACK: PuttyTray / Nutty
	 * Hyperlink stuff: Set the regular expression
	 */
	if( !GetPuttyFlag() && GetHyperlinkFlag() ) {
		if( strlen( conf_get_str(conf,CONF_url_regex))==0 ) { conf_set_str(conf,CONF_url_regex,"@°@°@NO REGEX--") ; }
		if( strlen( conf_get_str(term->conf,CONF_url_regex))==0 ) { conf_set_str(term->conf,CONF_url_regex,"@°@°@NO REGEX--") ; }
		
		if( conf_get_int(term->conf,CONF_url_defregex) != 0 ) {
			urlhack_set_regular_expression(URLHACK_REGEX_CLASSIC, conf_get_str(term->conf,CONF_url_regex) ) ;
		} else {
			urlhack_set_regular_expression(URLHACK_REGEX_CUSTOM, conf_get_str(term->conf, CONF_url_regex) ) ;
		}
	}
#endif

    /*
     * Set up the initial input locale.
     */
    set_input_locale(GetKeyboardLayout(0));

    /*
     * Finally show the window!
     */
    ShowWindow(wgs.term_hwnd, show);
    SetForegroundWindow(wgs.term_hwnd);

    term_set_focus(term, GetForegroundWindow() == wgs.term_hwnd);
    UpdateWindow(wgs.term_hwnd);

#ifdef MOD_ZMODEM
	if( (!GetPuttyFlag()) && GetZModemFlag() ) {
		netsc = netscheduler_new();
	}
#endif

    while (1) {
	HANDLE *handles;
	int nhandles, n;
        DWORD timeout;

        if (toplevel_callback_pending() ||
            PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
            /*
             * If we have anything we'd like to do immediately, set
             * the timeout for MsgWaitForMultipleObjects to zero so
             * that we'll only do a quick check of our handles and
             * then get on with whatever that was.
             *
             * One such option is a pending toplevel callback. The
             * other is a non-empty Windows message queue, which you'd
             * think we could leave to MsgWaitForMultipleObjects to
             * check for us along with all the handles, but in fact we
             * can't because once PeekMessage in one iteration of this
             * loop has removed a message from the queue, the whole
             * queue is considered uninteresting by the next
             * invocation of MWFMO. So we check ourselves whether the
             * message queue is non-empty, and if so, set this timeout
             * to zero to ensure MWFMO doesn't block.
             */
            timeout = 0;
        } else {
            timeout = INFINITE;
            /* The messages seem unreliable; especially if we're being tricky */
            term_set_focus(term, GetForegroundWindow() == wgs.term_hwnd);
        }

	handles = handle_get_events(&nhandles);

	n = MsgWaitForMultipleObjects(nhandles, handles, false,
                                      timeout, QS_ALLINPUT);

	if ((unsigned)(n - WAIT_OBJECT_0) < (unsigned)nhandles) {
	    handle_got_event(handles[n - WAIT_OBJECT_0]);
	    sfree(handles);
#ifdef MOD_ZMODEM
	     if( (!GetPuttyFlag()) && GetZModemFlag() ) { continue ; }
#endif
	} else
	    sfree(handles);

	while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
	    if (msg.message == WM_QUIT)
		goto finished;	       /* two-level break */

            HWND logbox = event_log_window();
	    if (!(IsWindow(logbox) && IsDialogMessage(logbox, &msg)))
		DispatchMessageW(&msg);
#ifdef MOD_ZMODEM
	    	    if( (!GetPuttyFlag()) && GetZModemFlag() && xyz_Process(backend, backend, term))
		    continue;
#endif

            /*
             * WM_NETEVENT messages seem to jump ahead of others in
             * the message queue. I'm not sure why; the docs for
             * PeekMessage mention that messages are prioritised in
             * some way, but I'm unclear on which priorities go where.
             *
             * Anyway, in practice I observe that WM_NETEVENT seems to
             * jump to the head of the queue, which means that if we
             * were to only process one message every time round this
             * loop, we'd get nothing but NETEVENTs if the server
             * flooded us with data, and stop responding to any other
             * kind of window message. So instead, we keep on round
             * this loop until we've consumed at least one message
             * that _isn't_ a NETEVENT, or run out of messages
             * completely (whichever comes first). And we don't go to
             * run_toplevel_callbacks (which is where the netevents
             * are actually processed, causing fresh NETEVENT messages
             * to appear) until we've done this.
             */
            if (msg.message != WM_NETEVENT)
                break;
	}

        run_toplevel_callbacks();
    }

    finished:
#ifdef MOD_ZMODEM
	if( (!GetPuttyFlag()) && GetZModemFlag() ) {
		netscheduler_free(netsc);
	}
#endif
    cleanup_exit(msg.wParam);	       /* this doesn't return... */
    return msg.wParam;		       /* ... but optimiser doesn't know */
}

static void setup_clipboards(Terminal *term, Conf *conf)
{
    assert(term->mouse_select_clipboards[0] == CLIP_LOCAL);

    term->n_mouse_select_clipboards = 1;

    if (conf_get_bool(conf, CONF_mouseautocopy)) {
        term->mouse_select_clipboards[
            term->n_mouse_select_clipboards++] = CLIP_SYSTEM;
    }

    switch (conf_get_int(conf, CONF_mousepaste)) {
      case CLIPUI_IMPLICIT:
        term->mouse_paste_clipboard = CLIP_LOCAL;
        break;
      case CLIPUI_EXPLICIT:
        term->mouse_paste_clipboard = CLIP_SYSTEM;
        break;
      default:
        term->mouse_paste_clipboard = CLIP_NULL;
        break;
    }
}

/*
 * Clean up and exit.
 */
void cleanup_exit(int code)
{
    /*
     * Clean up.
     */
#ifdef MOD_PERSO
	chdir( InitialDirectory ) ;
	if( conf_get_bool(conf,CONF_saveonexit) ) { SaveWindowCoord( conf ) ; }

	if( IniFileFlag == SAVEMODE_REG ) { // Mode de sauvegarde registry
		//SaveRegistryKey() ; 
		}
	else if( IniFileFlag == SAVEMODE_FILE ) { // Mode de sauvegarde fichier
		int nb ;
		nb = WindowsCount( MainHwnd ) ;
		if( nb == 1 ) { // c'est la derniere instance de kitty on vide la cle de registre
			HWND hdlg = InfoBox( hinst, NULL ) ;
			InfoBoxSetText( hdlg, "Cleaning registry" ) ;
			RegDelTree( HKEY_CURRENT_USER, TEXT(PUTTY_REG_POS) ) ;
			if( RegTestKey( HKEY_CURRENT_USER, TEXT(PUTTY_REG_POS_SAVE) ) ) {
				InfoBoxSetText( hdlg, "Restoring backup registry" ) ;
				RegRenameTree( NULL, HKEY_CURRENT_USER, TEXT(PUTTY_REG_POS_SAVE), TEXT(PUTTY_REG_POS) ) ;
				}
			else DelRegistryKey() ;
			InfoBoxClose( hdlg ) ;
			}
		}
	
	
	/*
	Embryon de mecanisme de retour a la config box en sortant d'une session.
	Le probleme est que ca retourne a la config box aussi en sortant ... de la config box si une session est chargee ...
	*/
	if( !PuttyFlag && GetConfigBoxNoExitFlag() )
	if( is_backend_connected && strlen(conf_get_str(conf,CONF_sessionname))>0 ) {
		char buffer[4096]="",shortname[1024]="" ; ;
		if( GetModuleFileName( NULL, (LPTSTR)buffer, 1023 ) ) 
			if( GetShortPathName( buffer, shortname, 1023 ) ) {
				STARTUPINFO si ;
				PROCESS_INFORMATION pi ;
				ZeroMemory( &si, sizeof(si) );
				si.cb = sizeof(si);
				ZeroMemory( &pi, sizeof(pi) );
				if( !CreateProcess(NULL, shortname, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi) ) ;
				}
		}
#endif
#ifdef MOD_HYPERLINK
	urlhack_cleanup();
#endif
    deinit_fonts();
    sfree(logpal);
    if (pal)
	DeleteObject(pal);
    sk_cleanup();

    if (conf_get_int(conf, CONF_protocol) == PROT_SSH) {
	random_save_seed();
    }
    shutdown_help();

    /* Clean up COM. */
    CoUninitialize();

    exit(code);
}

/*
 * Refresh the saved-session submenu from `sesslist'.
 */
static void update_savedsess_menu(void)
{
    int i;
    while (DeleteMenu(savedsess_menu, 0, MF_BYPOSITION)) ;
    /* skip sesslist.sessions[0] == Default Settings */
    for (i = 1;
	 i < ((sesslist.nsessions <= MENU_SAVED_MAX+1) ? sesslist.nsessions
						       : MENU_SAVED_MAX+1);
	 i++)
	AppendMenu(savedsess_menu, MF_ENABLED,
		   IDM_SAVED_MIN + (i-1)*MENU_SAVED_STEP,
		   sesslist.sessions[i]);
    if (sesslist.nsessions <= 1)
	AppendMenu(savedsess_menu, MF_GRAYED, IDM_SAVED_MIN, "(No sessions)");
}

/*
 * Update the Special Commands submenu.
 */
static void win_seat_update_specials_menu(Seat *seat)
{
    HMENU new_menu;
    int i, j;

    if (backend)
        specials = backend_get_specials(backend);
    else
	specials = NULL;

    if (specials) {
	/* We can't use Windows to provide a stack for submenus, so
	 * here's a lame "stack" that will do for now. */
	HMENU saved_menu = NULL;
	int nesting = 1;
	new_menu = CreatePopupMenu();
	for (i = 0; nesting > 0; i++) {
	    assert(IDM_SPECIAL_MIN + 0x10 * i < IDM_SPECIAL_MAX);
	    switch (specials[i].code) {
	      case SS_SEP:
		AppendMenu(new_menu, MF_SEPARATOR, 0, 0);
		break;
	      case SS_SUBMENU:
		assert(nesting < 2);
		nesting++;
		saved_menu = new_menu; /* XXX lame stacking */
		new_menu = CreatePopupMenu();
		AppendMenu(saved_menu, MF_POPUP | MF_ENABLED,
			   (UINT_PTR) new_menu, specials[i].name);
		break;
	      case SS_EXITMENU:
		nesting--;
		if (nesting) {
		    new_menu = saved_menu; /* XXX lame stacking */
		    saved_menu = NULL;
		}
		break;
	      default:
		AppendMenu(new_menu, MF_ENABLED, IDM_SPECIAL_MIN + 0x10 * i,
			   specials[i].name);
		break;
	    }
	}
	/* Squirrel the highest special. */
	n_specials = i - 1;
    } else {
	new_menu = NULL;
	n_specials = 0;
    }

    for (j = 0; j < lenof(popup_menus); j++) {
	if (specials_menu) {
	    /* XXX does this free up all submenus? */
	    DeleteMenu(popup_menus[j].menu, (UINT_PTR)specials_menu,
                       MF_BYCOMMAND);
	    DeleteMenu(popup_menus[j].menu, IDM_SPECIALSEP, MF_BYCOMMAND);
	}
	if (new_menu) {
	    InsertMenu(popup_menus[j].menu, IDM_SHOWLOG,
		       MF_BYCOMMAND | MF_POPUP | MF_ENABLED,
		       (UINT_PTR) new_menu, "S&pecial Command");
	    InsertMenu(popup_menus[j].menu, IDM_SHOWLOG,
		       MF_BYCOMMAND | MF_SEPARATOR, IDM_SPECIALSEP, 0);
	}
    }
    specials_menu = new_menu;
}

static void update_mouse_pointer(void)
{
    LPTSTR curstype = NULL;
    bool force_visible = false;
    static bool forced_visible = false;
    switch (busy_status) {
      case BUSY_NOT:
        if (pointer_indicates_raw_mouse)
	    curstype = IDC_ARROW;
	else
	    curstype = IDC_IBEAM;
	break;
      case BUSY_WAITING:
	curstype = IDC_APPSTARTING; /* this may be an abuse */
	force_visible = true;
	break;
      case BUSY_CPU:
	curstype = IDC_WAIT;
	force_visible = true;
	break;
      default:
	unreachable("Bad busy_status");
    }
    {
	HCURSOR cursor = LoadCursor(NULL, curstype);
        SetClassLongPtr(wgs.term_hwnd, GCLP_HCURSOR, (LONG_PTR)cursor);
	SetCursor(cursor); /* force redraw of cursor at current posn */
    }
    if (force_visible != forced_visible) {
	/* We want some cursor shapes to be visible always.
	 * Along with show_mouseptr(), this manages the ShowCursor()
	 * counter such that if we switch back to a non-force_visible
	 * cursor, the previous visibility state is restored. */
	ShowCursor(force_visible);
	forced_visible = force_visible;
    }
}

static void win_seat_set_busy_status(Seat *seat, BusyStatus status)
{
    busy_status = status;
    update_mouse_pointer();
}

static void wintw_set_raw_mouse_mode(TermWin *tw, bool activate)
{
    send_raw_mouse = activate;
}

static void wintw_set_raw_mouse_mode_pointer(TermWin *tw, bool activate)
{
    pointer_indicates_raw_mouse = activate;
    update_mouse_pointer();
}

#ifdef MOD_PERSO
/*
 * set or clear the "window focus messages" mode
 */
static void wintw_set_focus_reporting_mode(TermWin *tw, bool activate)
{
	activate = activate && !conf_get_bool(conf, CONF_no_focus_rep);

	if (activate != term->report_focus)
	{
		term->report_focus = activate;
		//send first activation notification message
		if (activate)
		{
			SendKeyboard(wgs.term_hwnd, "\033[I");
		}
	}
}
#endif

/*
 * Print a message box and close the connection.
 */
static void win_seat_connection_fatal(Seat *seat, const char *msg)
{
#ifdef MOD_RECONNECT
	if( GetAutoreconnectFlag() && is_backend_first_connected ) {
		SetConnBreakIcon(wgs.term_hwnd) ;
		SetSSHConnected(0);
		queue_toplevel_callback(close_session, NULL);
		session_closed = true;
		ReadInitScript(NULL);
	
    debug_logevent( "%s Fatal Error: %s", appname,msg ) ;
    show_mouseptr(true);
	
		if( conf_get_int(conf,CONF_failure_reconnect) ) {
			queue_toplevel_callback(close_session, NULL);
			session_closed = true;
			lp_eventlog(&wgs.logpolicy, "Lost connection, trying to reconnect...") ;
			SetTimer(wgs.term_hwnd, TIMER_RECONNECT, GetReconnectDelay()*1000, NULL) ;
		}
	} else {
    char *title = dupprintf("%s Fatal Error", appname);
    show_mouseptr(true);
    MessageBox(wgs.term_hwnd, msg, title, MB_ICONERROR | MB_OK);
    sfree(title);

    if (conf_get_int(conf, CONF_close_on_exit) == FORCE_ON)
	PostQuitMessage(1);
    else {
	queue_toplevel_callback(close_session, NULL);
	session_closed = true;
    }
	}
#else
    char *title = dupprintf("%s Fatal Error", appname);
    show_mouseptr(true);
    MessageBox(wgs.term_hwnd, msg, title, MB_ICONERROR | MB_OK);
    sfree(title);

    if (conf_get_int(conf, CONF_close_on_exit) == FORCE_ON)
	PostQuitMessage(1);
    else {
	queue_toplevel_callback(close_session, NULL);
    }
#endif
}

/*
 * Report an error at the command-line parsing stage.
 */
void cmdline_error(const char *fmt, ...)
{
    va_list ap;
    char *message, *title;

    va_start(ap, fmt);
    message = dupvprintf(fmt, ap);
    va_end(ap);
    title = dupprintf("%s Command Line Error", appname);
    MessageBox(wgs.term_hwnd, message, title, MB_ICONERROR | MB_OK);
    sfree(message);
    sfree(title);
    exit(1);
}

/*
 * Actually do the job requested by a WM_NETEVENT
 */
static void wm_netevent_callback(void *vctx)
{
    struct wm_netevent_params *params = (struct wm_netevent_params *)vctx;
    select_result(params->wParam, params->lParam);
    sfree(vctx);
}

static inline rgb rgb_from_colorref(COLORREF cr)
{
    rgb toret;
    toret.r = GetRValue(cr);
    toret.g = GetGValue(cr);
    toret.b = GetBValue(cr);
    return toret;
}

static void wintw_palette_get_overrides(TermWin *tw, Terminal *term)
{
    if (conf_get_bool(conf, CONF_system_colour)) {
        rgb rgb;

        rgb = rgb_from_colorref(GetSysColor(COLOR_WINDOWTEXT));
        term_palette_override(term, OSC4_COLOUR_fg, rgb);
        term_palette_override(term, OSC4_COLOUR_fg_bold, rgb);

        rgb = rgb_from_colorref(GetSysColor(COLOR_WINDOW));
        term_palette_override(term, OSC4_COLOUR_bg, rgb);
        term_palette_override(term, OSC4_COLOUR_bg_bold, rgb);

        rgb = rgb_from_colorref(GetSysColor(COLOR_HIGHLIGHTTEXT));
        term_palette_override(term, OSC4_COLOUR_cursor_fg, rgb);

        rgb = rgb_from_colorref(GetSysColor(COLOR_HIGHLIGHT));
        term_palette_override(term, OSC4_COLOUR_cursor_bg, rgb);
    }
}

/*
 * This is a wrapper to ExtTextOut() to force Windows to display
 * the precise glyphs we give it. Otherwise it would do its own
 * bidi and Arabic shaping, and we would end up uncertain which
 * characters it had put where.
 */
static void exact_textout(HDC hdc, int x, int y, CONST RECT *lprc,
			  unsigned short *lpString, UINT cbCount,
			  CONST INT *lpDx, bool opaque)
{
#ifdef __LCC__
    /*
     * The LCC include files apparently don't supply the
     * GCP_RESULTSW type, but we can make do with GCP_RESULTS
     * proper: the differences aren't important to us (the only
     * variable-width string parameter is one we don't use anyway).
     */
    GCP_RESULTS gcpr;
#else
    GCP_RESULTSW gcpr;
#endif
    char *buffer = snewn(cbCount*2+2, char);
    char *classbuffer = snewn(cbCount, char);
    memset(&gcpr, 0, sizeof(gcpr));
    memset(buffer, 0, cbCount*2+2);
    memset(classbuffer, GCPCLASS_NEUTRAL, cbCount);

    gcpr.lStructSize = sizeof(gcpr);
    gcpr.lpGlyphs = (void *)buffer;
    gcpr.lpClass = (void *)classbuffer;
    gcpr.nGlyphs = cbCount;
    GetCharacterPlacementW(hdc, lpString, cbCount, 0, &gcpr,
			   FLI_MASK | GCP_CLASSIN | GCP_DIACRITIC);

    ExtTextOut(hdc, x, y,
	       ETO_GLYPH_INDEX | ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
	       lprc, buffer, cbCount, lpDx);
}

/*
 * The exact_textout() wrapper, unfortunately, destroys the useful
 * Windows `font linking' behaviour: automatic handling of Unicode
 * code points not supported in this font by falling back to a font
 * which does contain them. Therefore, we adopt a multi-layered
 * approach: for any potentially-bidi text, we use exact_textout(),
 * and for everything else we use a simple ExtTextOut as we did
 * before exact_textout() was introduced.
 */
static void general_textout(HDC hdc, int x, int y, CONST RECT *lprc,
			    unsigned short *lpString, UINT cbCount,
			    CONST INT *lpDx, bool opaque)
{
    int i, j, xp, xn;
    int bkmode = 0;
    bool got_bkmode = false;

    xp = xn = x;

    for (i = 0; i < (int)cbCount ;) {
	bool rtl = is_rtl(lpString[i]);

	xn += lpDx[i];

	for (j = i+1; j < (int)cbCount; j++) {
	    if (rtl != is_rtl(lpString[j]))
		break;
	    xn += lpDx[j];
	}

	/*
	 * Now [i,j) indicates a maximal substring of lpString
	 * which should be displayed using the same textout
	 * function.
	 */
	if (rtl) {
	    exact_textout(hdc, xp, y, lprc, lpString+i, j-i,
                          font_varpitch ? NULL : lpDx+i, opaque);
	} else {
	    ExtTextOutW(hdc, xp, y, ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
			lprc, lpString+i, j-i,
                        font_varpitch ? NULL : lpDx+i);
	}

	i = j;
	xp = xn;

        bkmode = GetBkMode(hdc);
        got_bkmode = true;
        SetBkMode(hdc, TRANSPARENT);
        opaque = false;
    }

    if (got_bkmode)
        SetBkMode(hdc, bkmode);
}

static int get_font_width(HDC hdc, const TEXTMETRIC *tm)
{
    int ret;
    /* Note that the TMPF_FIXED_PITCH bit is defined upside down :-( */
    if (!(tm->tmPitchAndFamily & TMPF_FIXED_PITCH)) {
        ret = tm->tmAveCharWidth;
    } else {
#define FIRST '0'
#define LAST '9'
        ABCFLOAT widths[LAST-FIRST + 1];
        int j;

        font_varpitch = true;
        font_dualwidth = true;
        if (GetCharABCWidthsFloat(hdc, FIRST, LAST, widths)) {
            ret = 0;
            for (j = 0; j < lenof(widths); j++) {
                int width = (int)(0.5 + widths[j].abcfA +
                                  widths[j].abcfB + widths[j].abcfC);
                if (ret < width)
                    ret = width;
            }
        } else {
            ret = tm->tmMaxCharWidth;
        }
#undef FIRST
#undef LAST
    }
    return ret;
}

static void init_dpi_info(void)
{
    if (dpi_info.cur_dpi.x == 0 || dpi_info.cur_dpi.y == 0) {
        if (p_GetDpiForMonitor) {
            UINT dpiX, dpiY;
            HMONITOR currentMonitor = MonitorFromWindow(
                wgs.term_hwnd, MONITOR_DEFAULTTOPRIMARY);
            if (p_GetDpiForMonitor(currentMonitor, MDT_EFFECTIVE_DPI,
                                   &dpiX, &dpiY) == S_OK) {
                dpi_info.cur_dpi.x = (int)dpiX;
                dpi_info.cur_dpi.y = (int)dpiY;
            }
        }

        /* Fall back to system DPI */
        if (dpi_info.cur_dpi.x == 0 || dpi_info.cur_dpi.y == 0) {
            HDC hdc = GetDC(wgs.term_hwnd);
            dpi_info.cur_dpi.x = GetDeviceCaps(hdc, LOGPIXELSX);
            dpi_info.cur_dpi.y = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(wgs.term_hwnd, hdc);
        }
    }
}

/*
 * Initialise all the fonts we will need initially. There may be as many as
 * three or as few as one.  The other (potentially) twenty-one fonts are done
 * if/when they are needed.
 *
 * We also:
 *
 * - check the font width and height, correcting our guesses if
 *   necessary.
 *
 * - verify that the bold font is the same width as the ordinary
 *   one, and engage shadow bolding if not.
 * 
 * - verify that the underlined font is the same width as the
 *   ordinary one (manual underlining by means of line drawing can
 *   be done in a pinch).
 *
 * - find a trust sigil icon that will look OK with the chosen font.
 */
static void init_fonts(int pick_width, int pick_height)
{
    TEXTMETRIC tm;
    OUTLINETEXTMETRIC otm;
    CPINFO cpinfo;
    FontSpec *font;
    int fontsize[3];
    int i;
    int quality;
    HDC hdc;
    int fw_dontcare, fw_bold;

    for (i = 0; i < FONT_MAXNO; i++)
	fonts[i] = NULL;

    bold_font_mode = conf_get_int(conf, CONF_bold_style) & 1 ?
	BOLD_FONT : BOLD_NONE;
    bold_colours = conf_get_int(conf, CONF_bold_style) & 2 ? true : false;
    und_mode = UND_FONT;

    font = conf_get_fontspec(conf, CONF_font);
    if (font->isbold) {
	fw_dontcare = FW_BOLD;
	fw_bold = FW_HEAVY;
    } else {
	fw_dontcare = FW_DONTCARE;
	fw_bold = FW_BOLD;
    }

    hdc = GetDC(wgs.term_hwnd);

    if (pick_height)
	font_height = pick_height;
    else {
	font_height = font->height;
	if (font_height > 0) {
	    font_height =
                -MulDiv(font_height, dpi_info.cur_dpi.y, 72);
	}
    }
    font_width = pick_width;

    quality = conf_get_int(conf, CONF_font_quality);
#ifdef MOD_PERSO
#define f(i,c,w,u,t) \
    fonts[i] = CreateFont (font_height, font_width, 0, 0, w, t, u, FALSE, \
 			   c, OUT_DEFAULT_PRECIS, \
 		           CLIP_DEFAULT_PRECIS, FONT_QUALITY(quality), \
 			   FIXED_PITCH | FF_DONTCARE, font->name)
 
    f(FONT_NORMAL, font->charset, fw_dontcare, FALSE, FALSE);
#else

#define f(i,c,w,u) \
    fonts[i] = CreateFont (font_height, font_width, 0, 0, w, false, u, false, \
			   c, OUT_DEFAULT_PRECIS, \
		           CLIP_DEFAULT_PRECIS, FONT_QUALITY(quality), \
			   FIXED_PITCH | FF_DONTCARE, font->name)

    f(FONT_NORMAL, font->charset, fw_dontcare, false);
#endif

    SelectObject(hdc, fonts[FONT_NORMAL]);
    GetTextMetrics(hdc, &tm);
    if (GetOutlineTextMetrics(hdc, sizeof(otm), &otm))
        font_strikethrough_y = tm.tmAscent - otm.otmsStrikeoutPosition;
    else
        font_strikethrough_y = tm.tmAscent - (tm.tmAscent * 3 / 8);

    GetObject(fonts[FONT_NORMAL], sizeof(LOGFONT), &lfont);

    /* Note that the TMPF_FIXED_PITCH bit is defined upside down :-( */
    if (!(tm.tmPitchAndFamily & TMPF_FIXED_PITCH)) {
        font_varpitch = false;
        font_dualwidth = (tm.tmAveCharWidth != tm.tmMaxCharWidth);
    } else {
        font_varpitch = true;
        font_dualwidth = true;
    }
    if (pick_width == 0 || pick_height == 0) {
	font_height = tm.tmHeight;
        font_width = get_font_width(hdc, &tm);
    }

#ifdef RDB_DEBUG_PATCH
    debug("Primary font H=%d, AW=%d, MW=%d\n",
          tm.tmHeight, tm.tmAveCharWidth, tm.tmMaxCharWidth);
#endif

    {
	CHARSETINFO info;
	DWORD cset = tm.tmCharSet;
	memset(&info, 0xFF, sizeof(info));

	/* !!! Yes the next line is right */
	if (cset == OEM_CHARSET)
	    ucsdata.font_codepage = GetOEMCP();
	else
	    if (TranslateCharsetInfo ((DWORD *)(ULONG_PTR)cset,
                                      &info, TCI_SRCCHARSET))
		ucsdata.font_codepage = info.ciACP;
	else
	    ucsdata.font_codepage = -1;

	GetCPInfo(ucsdata.font_codepage, &cpinfo);
	ucsdata.dbcs_screenfont = (cpinfo.MaxCharSize > 1);
    }
#ifdef MOD_PERSO
    /* Check whether italic face is acceptable... */
    f(FONT_ITALIC, font->charset, fw_dontcare, FALSE, TRUE);

    {
	LPOUTLINETEXTMETRICA regularOtm, italicOtm;

	use_italics = 0;

	i = GetOutlineTextMetricsA(hdc, 0, NULL);
	if (i > 0) {
	    regularOtm = (LPOUTLINETEXTMETRICA)safemalloc(1, i, 0);
	    regularOtm->otmSize = sizeof(OUTLINETEXTMETRICA);
	    if (GetOutlineTextMetricsA(hdc, i, regularOtm)) {
		/* Now get the italic version */
		SelectObject(hdc, fonts[FONT_ITALIC]);
		i = GetOutlineTextMetricsA(hdc, 0, NULL);
		if (i > 0) {
		    italicOtm = (LPOUTLINETEXTMETRICA)safemalloc(1, i, 0);
		    italicOtm->otmSize = sizeof(OUTLINETEXTMETRICA);
		    if (GetOutlineTextMetricsA(hdc, i, italicOtm)) {
			/* Compare... */
			char *regStyle = (char*)regularOtm + (int)regularOtm->otmpStyleName;
			char *itaStyle = (char*)italicOtm + (int)italicOtm->otmpStyleName;

			/* Weed out "italic" fonts that...
			    - Do not specify an italic slant (probably just the regular font)
			    - Have the same style as the regular font.  Then it *is* just the regular
				font with a linear transformation.
			    - Report the style name of "Oblique".
			   
			    My experience is these a) don't look very good b) tend to overhang the
			    next character and get cut off during paints... which doesn't look very good. */
			if (strcmp(regStyle, itaStyle) && stricmp(itaStyle, "Oblique") && italicOtm->otmItalicAngle != 0) {
			    use_italics = 1;
			}
		    }

		    safefree(italicOtm);
		}
	    }

	    safefree(regularOtm);
	}

	if (!use_italics) {
	    DeleteObject(fonts[FONT_ITALIC]);
	    fonts[FONT_ITALIC] = NULL;
	}
    }

    f(FONT_UNDERLINE, font->charset, fw_dontcare, TRUE, FALSE);
#else
    f(FONT_UNDERLINE, font->charset, fw_dontcare, true);
#endif

    /*
     * Some fonts, e.g. 9-pt Courier, draw their underlines
     * outside their character cell. We successfully prevent
     * screen corruption by clipping the text output, but then
     * we lose the underline completely. Here we try to work
     * out whether this is such a font, and if it is, we set a
     * flag that causes underlines to be drawn by hand.
     *
     * Having tried other more sophisticated approaches (such
     * as examining the TEXTMETRIC structure or requesting the
     * height of a string), I think we'll do this the brute
     * force way: we create a small bitmap, draw an underlined
     * space on it, and test to see whether any pixels are
     * foreground-coloured. (Since we expect the underline to
     * go all the way across the character cell, we only search
     * down a single column of the bitmap, half way across.)
     */
    {
	HDC und_dc;
	HBITMAP und_bm, und_oldbm;
	int i;
        bool gotit;
	COLORREF c;

	und_dc = CreateCompatibleDC(hdc);
	und_bm = CreateCompatibleBitmap(hdc, font_width, font_height);
	und_oldbm = SelectObject(und_dc, und_bm);
	SelectObject(und_dc, fonts[FONT_UNDERLINE]);
	SetTextAlign(und_dc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
	SetTextColor(und_dc, RGB(255, 255, 255));
	SetBkColor(und_dc, RGB(0, 0, 0));
	SetBkMode(und_dc, OPAQUE);
	ExtTextOut(und_dc, 0, 0, ETO_OPAQUE, NULL, " ", 1, NULL);
	gotit = false;
	for (i = 0; i < font_height; i++) {
	    c = GetPixel(und_dc, font_width / 2, i);
	    if (c != RGB(0, 0, 0))
		gotit = true;
	}
	SelectObject(und_dc, und_oldbm);
	DeleteObject(und_bm);
	DeleteDC(und_dc);
	if (!gotit) {
	    und_mode = UND_LINE;
	    DeleteObject(fonts[FONT_UNDERLINE]);
	    fonts[FONT_UNDERLINE] = 0;
	}
    }

    if (bold_font_mode == BOLD_FONT) {
#ifdef MOD_PERSO
	f(FONT_BOLD, font->charset, fw_bold, FALSE, FALSE);
#else
	f(FONT_BOLD, font->charset, fw_bold, false);
#endif
    }
#undef f

    descent = tm.tmAscent + 1;
    if (descent >= font_height)
	descent = font_height - 1;

    for (i = 0; i < 3; i++) {
	if (fonts[i]) {
	    if (SelectObject(hdc, fonts[i]) && GetTextMetrics(hdc, &tm))
		fontsize[i] = get_font_width(hdc, &tm) + 256 * tm.tmHeight;
	    else
		fontsize[i] = -i;
	} else
	    fontsize[i] = -i;
    }

    ReleaseDC(wgs.term_hwnd, hdc);

    if (trust_icon != INVALID_HANDLE_VALUE) {
	DestroyIcon(trust_icon);
    }
    trust_icon = LoadImage(hinst, MAKEINTRESOURCE(IDI_MAINICON),
			   IMAGE_ICON, font_width*2, font_height,
			   LR_DEFAULTCOLOR);

    if (fontsize[FONT_UNDERLINE] != fontsize[FONT_NORMAL]) {
	und_mode = UND_LINE;
	DeleteObject(fonts[FONT_UNDERLINE]);
	fonts[FONT_UNDERLINE] = 0;
    }

    if (bold_font_mode == BOLD_FONT &&
	fontsize[FONT_BOLD] != fontsize[FONT_NORMAL]) {
	bold_font_mode = BOLD_SHADOW;
	DeleteObject(fonts[FONT_BOLD]);
	fonts[FONT_BOLD] = 0;
    }
    fontflag[0] = true;
    fontflag[1] = true;
    fontflag[2] = true;

    init_ucs(conf, &ucsdata);
}

static void another_font(int fontno)
{
    int basefont;
    int fw_dontcare, fw_bold, quality;
#ifdef MOD_PERSO
	int c, w, x ;
	bool t;
#else
    int c, w, x;
#endif
    bool u;
    char *s;
    FontSpec *font;

    if (fontno < 0 || fontno >= FONT_MAXNO || fontflag[fontno])
	return;

    basefont = (fontno & ~(FONT_BOLDUND));
    if (basefont != fontno && !fontflag[basefont])
	another_font(basefont);

    font = conf_get_fontspec(conf, CONF_font);

    if (font->isbold) {
	fw_dontcare = FW_BOLD;
	fw_bold = FW_HEAVY;
    } else {
	fw_dontcare = FW_DONTCARE;
	fw_bold = FW_BOLD;
    }

    c = font->charset;
    w = fw_dontcare;
    u = false;
    s = font->name;
    x = font_width;
#ifdef MOD_PERSO
    t = false;
#endif

    if (fontno & FONT_WIDE)
	x *= 2;
    if (fontno & FONT_NARROW)
	x = (x+1)/2;
    if (fontno & FONT_OEM)
	c = OEM_CHARSET;
    if (fontno & FONT_BOLD)
	w = fw_bold;
    if (fontno & FONT_UNDERLINE)
	u = true;
#ifdef MOD_PERSO
    if (fontno & FONT_ITALIC && use_italics)
	t = true;
#endif

    quality = conf_get_int(conf, CONF_font_quality);

    fonts[fontno] =
	CreateFont(font_height * (1 + !!(fontno & FONT_HIGH)), x, 0, 0, w,
#ifdef MOD_PERSO
    		   t, u, FALSE, c, OUT_DEFAULT_PRECIS,
#else
		   false, u, false, c, OUT_DEFAULT_PRECIS,
#endif
		   CLIP_DEFAULT_PRECIS, FONT_QUALITY(quality),
		   DEFAULT_PITCH | FF_DONTCARE, s);

    fontflag[fontno] = true;
}

static void deinit_fonts(void)
{
    int i;
    for (i = 0; i < FONT_MAXNO; i++) {
	if (fonts[i])
	    DeleteObject(fonts[i]);
	fonts[i] = 0;
	fontflag[i] = false;
    }
    if (trust_icon != INVALID_HANDLE_VALUE) {
	DestroyIcon(trust_icon);
    }
    trust_icon = INVALID_HANDLE_VALUE;
}

static void wintw_request_resize(TermWin *tw, int w, int h)
{
    const struct BackendVtable *vt;
    int width, height;

    /* If the window is maximized suppress resizing attempts */
    if (IsZoomed(wgs.term_hwnd)) {
	if (conf_get_int(conf, CONF_resize_action) == RESIZE_TERM)
	    return;
    }

    if (conf_get_int(conf, CONF_resize_action) == RESIZE_DISABLED) return;
    vt = backend_vt_from_proto(be_default_protocol);
    if (vt && vt->flags & BACKEND_RESIZE_FORBIDDEN)
        return;
    if (h == term->rows && w == term->cols) return;

    /* Sanity checks ... */
    {
	static int first_time = 1;
	static RECT ss;

	switch (first_time) {
	  case 1:
	    /* Get the size of the screen */
	    if (get_fullscreen_rect(&ss))
		/* first_time = 0 */ ;
	    else {
		first_time = 2;
		break;
	    }
	  case 0:
	    /* Make sure the values are sane */
	    width = (ss.right - ss.left - extra_width) / 4;
	    height = (ss.bottom - ss.top - extra_height) / 6;

	    if (w > width || h > height)
		return;
	    if (w < 15)
		w = 15;
	    if (h < 1)
		h = 1;
	}
    }

    term_size(term, h, w, conf_get_int(conf, CONF_savelines));

    if (conf_get_int(conf, CONF_resize_action) != RESIZE_FONT &&
        !IsZoomed(wgs.term_hwnd)) {
	width = extra_width + font_width * w;
	height = extra_height + font_height * h;

        SetWindowPos(wgs.term_hwnd, NULL, 0, 0, width, height,
	    SWP_NOACTIVATE | SWP_NOCOPYBITS |
	    SWP_NOMOVE | SWP_NOZORDER);
    } else
	reset_window(0);

    InvalidateRect(wgs.term_hwnd, NULL, true);
}

static void recompute_window_offset(void)
{
    RECT cr;
    GetClientRect(wgs.term_hwnd, &cr);

    int win_width  = cr.right - cr.left;
    int win_height = cr.bottom - cr.top;

    int new_offset_width = (win_width-font_width*term->cols)/2;
    int new_offset_height = (win_height-font_height*term->rows)/2;

    if (offset_width != new_offset_width ||
        offset_height != new_offset_height) {
        offset_width = new_offset_width;
        offset_height = new_offset_height;
        InvalidateRect(wgs.term_hwnd, NULL, true);
    }
}

static void reset_window(int reinit) {
    /*
     * This function decides how to resize or redraw when the 
     * user changes something. 
     *
     * This function doesn't like to change the terminal size but if the
     * font size is locked that may be it's only soluion.
     */
    int win_width, win_height, resize_action, window_border;
    RECT cr, wr;

#ifdef RDB_DEBUG_PATCH
    debug("reset_window()\n");
#endif

    /* Current window sizes ... */
    GetWindowRect(wgs.term_hwnd, &wr);
    GetClientRect(wgs.term_hwnd, &cr);

    win_width  = cr.right - cr.left;
    win_height = cr.bottom - cr.top;

    resize_action = conf_get_int(conf, CONF_resize_action);
    window_border = conf_get_int(conf, CONF_window_border);

    if (resize_action == RESIZE_DISABLED)
	reinit = 2;

    /* Are we being forced to reload the fonts ? */
    if (reinit>1) {
#ifdef RDB_DEBUG_PATCH
	debug("reset_window() -- Forced deinit\n");
#endif
	deinit_fonts();
	init_fonts(0,0);
    }

    /* Oh, looks like we're minimised */
    if (win_width == 0 || win_height == 0)
	return;

    /* Is the window out of position ? */
    if (!reinit) {
        recompute_window_offset();
#ifdef RDB_DEBUG_PATCH
	debug("reset_window() -> Reposition terminal\n");
#endif
    }

    if (IsZoomed(wgs.term_hwnd)) {
	/* We're fullscreen, this means we must not change the size of
	 * the window so it's the font size or the terminal itself.
	 */

	extra_width = wr.right - wr.left - cr.right + cr.left;
	extra_height = wr.bottom - wr.top - cr.bottom + cr.top;

	if (resize_action != RESIZE_TERM) {
	    if (font_width != win_width/term->cols || 
		font_height != win_height/term->rows) {
		deinit_fonts();
		init_fonts(win_width/term->cols, win_height/term->rows);
		offset_width = (win_width-font_width*term->cols)/2;
		offset_height = (win_height-font_height*term->rows)/2;
                InvalidateRect(wgs.term_hwnd, NULL, true);
#ifdef RDB_DEBUG_PATCH
		debug("reset_window() -> Z font resize to (%d, %d)\n",
                      font_width, font_height);
#endif
	    }
	} else {
	    if (font_width * term->cols != win_width || 
		font_height * term->rows != win_height) {
		/* Our only choice at this point is to change the 
		 * size of the terminal; Oh well.
		 */
		term_size(term, win_height/font_height, win_width/font_width,
			  conf_get_int(conf, CONF_savelines));
		offset_width = (win_width-font_width*term->cols)/2;
		offset_height = (win_height-font_height*term->rows)/2;
                InvalidateRect(wgs.term_hwnd, NULL, true);
#ifdef RDB_DEBUG_PATCH
		debug("reset_window() -> Zoomed term_size\n");
#endif
	    }
	}
	return;
    }

    /* Resize window after DPI change */
    if (reinit == 3 && p_GetSystemMetricsForDpi && p_AdjustWindowRectExForDpi) {
        RECT rect;
        rect.left = rect.top = 0;
        rect.right = (font_width * term->cols);
        if (conf_get_bool(conf, CONF_scrollbar))
            rect.right += p_GetSystemMetricsForDpi(SM_CXVSCROLL,
                                                   dpi_info.cur_dpi.x);
        rect.bottom = (font_height * term->rows);
        p_AdjustWindowRectExForDpi(
            &rect, GetWindowLongPtr(wgs.term_hwnd, GWL_STYLE),
            FALSE, GetWindowLongPtr(wgs.term_hwnd, GWL_EXSTYLE),
            dpi_info.cur_dpi.x);
        rect.right += (window_border * 2);
        rect.bottom += (window_border * 2);
        OffsetRect(&dpi_info.new_wnd_rect,
            ((dpi_info.new_wnd_rect.right - dpi_info.new_wnd_rect.left) -
             (rect.right - rect.left)) / 2,
            ((dpi_info.new_wnd_rect.bottom - dpi_info.new_wnd_rect.top) -
             (rect.bottom - rect.top)) / 2);
        SetWindowPos(wgs.term_hwnd, NULL,
                     dpi_info.new_wnd_rect.left, dpi_info.new_wnd_rect.top,
                     rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER);

        InvalidateRect(wgs.term_hwnd, NULL, true);
        return;
    }

    /* Hmm, a force re-init means we should ignore the current window
     * so we resize to the default font size.
     */
    if (reinit>0) {
#ifdef RDB_DEBUG_PATCH
	debug("reset_window() -> Forced re-init\n");
#endif

	offset_width = offset_height = window_border;
	extra_width = wr.right - wr.left - cr.right + cr.left + offset_width*2;
	extra_height = wr.bottom - wr.top - cr.bottom + cr.top +offset_height*2;

	if (win_width != font_width*term->cols + offset_width*2 ||
	    win_height != font_height*term->rows + offset_height*2) {

	    /* If this is too large windows will resize it to the maximum
	     * allowed window size, we will then be back in here and resize
	     * the font or terminal to fit.
	     */
            SetWindowPos(wgs.term_hwnd, NULL, 0, 0,
		         font_width*term->cols + extra_width, 
			 font_height*term->rows + extra_height,
			 SWP_NOMOVE | SWP_NOZORDER);
	}

        InvalidateRect(wgs.term_hwnd, NULL, true);
	return;
    }

    /* Okay the user doesn't want us to change the font so we try the 
     * window. But that may be too big for the screen which forces us
     * to change the terminal.
     */
    if ((resize_action == RESIZE_TERM && reinit<=0) ||
        (resize_action == RESIZE_EITHER && reinit<0) ||
	    reinit>0) {
	offset_width = offset_height = window_border;
	extra_width = wr.right - wr.left - cr.right + cr.left + offset_width*2;
	extra_height = wr.bottom - wr.top - cr.bottom + cr.top +offset_height*2;

	if (win_width != font_width*term->cols + offset_width*2 ||
	    win_height != font_height*term->rows + offset_height*2) {

	    static RECT ss;
	    int width, height;
		
		get_fullscreen_rect(&ss);

	    width = (ss.right - ss.left - extra_width) / font_width;
	    height = (ss.bottom - ss.top - extra_height) / font_height;

	    /* Grrr too big */
	    if ( term->rows > height || term->cols > width ) {
		if (resize_action == RESIZE_EITHER) {
		    /* Make the font the biggest we can */
		    if (term->cols > width)
			font_width = (ss.right - ss.left - extra_width)
			    / term->cols;
		    if (term->rows > height)
			font_height = (ss.bottom - ss.top - extra_height)
			    / term->rows;

		    deinit_fonts();
		    init_fonts(font_width, font_height);

		    width = (ss.right - ss.left - extra_width) / font_width;
		    height = (ss.bottom - ss.top - extra_height) / font_height;
		} else {
		    if ( height > term->rows ) height = term->rows;
		    if ( width > term->cols )  width = term->cols;
		    term_size(term, height, width,
			      conf_get_int(conf, CONF_savelines));
#ifdef RDB_DEBUG_PATCH
		    debug("reset_window() -> term resize to (%d,%d)\n",
                          height, width);
#endif
		}
	    }
	    
            SetWindowPos(wgs.term_hwnd, NULL, 0, 0,
		         font_width*term->cols + extra_width, 
			 font_height*term->rows + extra_height,
			 SWP_NOMOVE | SWP_NOZORDER);

            InvalidateRect(wgs.term_hwnd, NULL, true);
#ifdef RDB_DEBUG_PATCH
	    debug("reset_window() -> window resize to (%d,%d)\n",
                  font_width*term->cols + extra_width,
                  font_height*term->rows + extra_height);
#endif
	}
	return;
    }

    /* We're allowed to or must change the font but do we want to ?  */

    if (font_width != (win_width-window_border*2)/term->cols || 
	font_height != (win_height-window_border*2)/term->rows) {

	deinit_fonts();
	init_fonts((win_width-window_border*2)/term->cols, 
		   (win_height-window_border*2)/term->rows);
	offset_width = (win_width-font_width*term->cols)/2;
	offset_height = (win_height-font_height*term->rows)/2;

	extra_width = wr.right - wr.left - cr.right + cr.left +offset_width*2;
	extra_height = wr.bottom - wr.top - cr.bottom + cr.top+offset_height*2;

        InvalidateRect(wgs.term_hwnd, NULL, true);
#ifdef RDB_DEBUG_PATCH
	debug("reset_window() -> font resize to (%d,%d)\n", 
              font_width, font_height);
#endif
    }
}

#ifdef MOD_PERSO
void ResetWindow(int reinit) { reset_window( reinit ) ; }
#endif

static void set_input_locale(HKL kl)
{
    char lbuf[20];

    GetLocaleInfo(LOWORD(kl), LOCALE_IDEFAULTANSICODEPAGE,
		  lbuf, sizeof(lbuf));

    kbd_codepage = atoi(lbuf);
}

static void click(Mouse_Button b, int x, int y,
                  bool shift, bool ctrl, bool alt)
{
    int thistime = GetMessageTime();

    if (send_raw_mouse &&
	!(shift && conf_get_bool(conf, CONF_mouse_override))) {
	lastbtn = MBT_NOTHING;
	term_mouse(term, b, translate_button(b), MA_CLICK,
		   x, y, shift, ctrl, alt);
	return;
    }

    if (lastbtn == b && thistime - lasttime < dbltime) {
	lastact = (lastact == MA_CLICK ? MA_2CLK :
		   lastact == MA_2CLK ? MA_3CLK :
		   lastact == MA_3CLK ? MA_CLICK : MA_NOTHING);
    } else {
	lastbtn = b;
	lastact = MA_CLICK;
    }
    if (lastact != MA_NOTHING)
	term_mouse(term, b, translate_button(b), lastact,
		   x, y, shift, ctrl, alt);
    lasttime = thistime;
}

/*
 * Translate a raw mouse button designation (LEFT, MIDDLE, RIGHT)
 * into a cooked one (SELECT, EXTEND, PASTE).
 */
static Mouse_Button translate_button(Mouse_Button button)
{
    if (button == MBT_LEFT)
	return MBT_SELECT;
    if (button == MBT_MIDDLE)
	return conf_get_int(conf, CONF_mouse_is_xterm) == 1 ?
	MBT_PASTE : MBT_EXTEND;
    if (button == MBT_RIGHT)
	return conf_get_int(conf, CONF_mouse_is_xterm) == 1 ?
	MBT_EXTEND : MBT_PASTE;
    return 0;			       /* shouldn't happen */
}

static void show_mouseptr(bool show)
{
    /* NB that the counter in ShowCursor() is also frobbed by
     * update_mouse_pointer() */
    static bool cursor_visible = true;
    if (!conf_get_bool(conf, CONF_hide_mouseptr))
	show = true;                   /* override if this feature disabled */
    if (cursor_visible && !show)
	ShowCursor(false);
    else if (!cursor_visible && show)
	ShowCursor(true);
    cursor_visible = show;
}

static bool is_alt_pressed(void)
{
    BYTE keystate[256];
    int r = GetKeyboardState(keystate);
    if (!r)
	return false;
    if (keystate[VK_MENU] & 0x80)
	return true;
    if (keystate[VK_RMENU] & 0x80)
	return true;
    return false;
}
#if (!defined MOD_BACKGROUNDIMAGE) || (defined FLJ)
static bool resizing;
#endif
#ifdef MOD_PERSO
struct ctrl_tab_info {
    int direction;
    HWND  self;
    DWORD self_hi_date_time;
    DWORD self_lo_date_time;
    HWND  next;
    DWORD next_hi_date_time;
    DWORD next_lo_date_time;
    int   next_self;
};

static BOOL CALLBACK CtrlTabWindowProc(HWND hwnd, LPARAM lParam) {
    struct ctrl_tab_info* info = (struct ctrl_tab_info*) lParam;
    char lpszClassName[256];
#if (defined MOD_PERSO) && (!defined FLJ)
	strcpy(lpszClassName,KiTTYClassName) ;
#else
	strcpy(lpszClassName,appname) ;
#endif
    char class_name[16];
    int wndExtra;
    if (info->self != hwnd && (wndExtra = GetClassLong(hwnd, GCL_CBWNDEXTRA)) >= 8 && GetClassName(hwnd, class_name, sizeof class_name) >= 5 && memcmp(class_name, lpszClassName, 5) == 0) {
	DWORD hwnd_hi_date_time = GetWindowLong(hwnd, wndExtra - 8);
	DWORD hwnd_lo_date_time = GetWindowLong(hwnd, wndExtra - 4);
	int hwnd_self, hwnd_next;

	hwnd_self = hwnd_hi_date_time - info->self_hi_date_time;
	if (hwnd_self == 0) 
	    hwnd_self = hwnd_lo_date_time - info->self_lo_date_time;
	hwnd_self *= info->direction;
	hwnd_next = hwnd_hi_date_time - info->next_hi_date_time;
	if (hwnd_next == 0) 
	    hwnd_next = hwnd_lo_date_time - info->next_lo_date_time;
	hwnd_next *= info->direction;
	if( ((hwnd_self > 0) && (hwnd_next < 0)) || (((hwnd_self > 0) || (hwnd_next < 0)) && (info->next_self <= 0)) ) {
	    info->next = hwnd;
	    info->next_hi_date_time = hwnd_hi_date_time;
	    info->next_lo_date_time = hwnd_lo_date_time;
	    info->next_self = hwnd_self;
	}
    }
    return TRUE;
}
#endif

static void win_seat_notify_remote_exit(Seat *seat)
{
    int exitcode, close_on_exit;

    if (!session_closed &&
        (exitcode = backend_exitcode(backend)) >= 0) {
	close_on_exit = conf_get_int(conf, CONF_close_on_exit);
	/* Abnormal exits will already have set session_closed and taken
	 * appropriate action. */
	if (close_on_exit == FORCE_ON ||
	    (close_on_exit == AUTO && exitcode != INT_MAX)) {
	    PostQuitMessage(0);
	} else {
            queue_toplevel_callback(close_session, NULL);
	    session_closed = true;
	    /* exitcode == INT_MAX indicates that the connection was closed
	     * by a fatal error, so an error box will be coming our way and
	     * we should not generate this informational one. */
#ifdef MOD_RECONNECT
	if( GetAutoreconnectFlag() && is_backend_first_connected ) {
		SetConnBreakIcon(wgs.term_hwnd) ;
		SetSSHConnected(0);
		queue_toplevel_callback(close_session, NULL);
		session_closed = true;
		ReadInitScript(NULL);
                show_mouseptr(true);
		lp_eventlog(&wgs.logpolicy, "Connection closed by remote host");
	} else
#endif
            if (exitcode != INT_MAX) {
                show_mouseptr(true);
                MessageBox(wgs.term_hwnd, "Connection closed by remote host",
			   appname, MB_OK | MB_ICONINFORMATION);
	    }
	}
    }
}

void timer_change_notify(unsigned long next)
{
    unsigned long now = GETTICKCOUNT();
    long ticks;
    if (now - next < INT_MAX)
	ticks = 0;
    else
	ticks = next - now;
    KillTimer(wgs.term_hwnd, TIMING_TIMER_ID);
    SetTimer(wgs.term_hwnd, TIMING_TIMER_ID, ticks, NULL);
    timing_next_time = next;
}

static void conf_cache_data(void)
{
    /* Cache some items from conf to speed lookups in very hot code */
    cursor_type = conf_get_int(conf, CONF_cursor_type);
    vtmode = conf_get_int(conf, CONF_vtmode);
}

static const int clips_system[] = { CLIP_SYSTEM };
#ifdef MOD_PERSO
void kitty_term_copyall(Terminal *term) { term_copyall(term,clips_system,lenof(clips_system)) ; }
#endif

static HDC make_hdc(void)
{
    HDC hdc;

    if (!wgs.term_hwnd)
        return NULL;

    hdc = GetDC(wgs.term_hwnd);
    if (!hdc)
        return NULL;

    SelectPalette(hdc, pal, false);
    return hdc;
}

static void free_hdc(HDC hdc)
{
    assert(wgs.term_hwnd);
    SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), false);
    ReleaseDC(wgs.term_hwnd, hdc);
}

    static bool need_backend_resize = false;

static void wm_size_resize_term(LPARAM lParam, bool border)
{
    int width = LOWORD(lParam);
    int height = HIWORD(lParam);
    int border_size = border ? conf_get_int(conf, CONF_window_border) : 0;

    int w = (width - border_size*2) / font_width;
    int h = (height - border_size*2) / font_height;

    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if (resizing) {
        /*
         * If we're in the middle of an interactive resize, we don't
         * call term_size. This means that, firstly, the user can drag
         * the size back and forth indecisively without wiping out any
         * actual terminal contents, and secondly, the Terminal
         * doesn't call back->size in turn for each increment of the
         * resizing drag, so we don't spam the server with huge
         * numbers of resize events.
         */
        need_backend_resize = true;
        conf_set_int(conf, CONF_height, h);
        conf_set_int(conf, CONF_width, w);
    } else {
        term_size(term, h, w,
                  conf_get_int(conf, CONF_savelines));
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message,
                                WPARAM wParam, LPARAM lParam)
{
    HDC hdc;
    static bool ignore_clip = false;
    static bool fullscr_on_max = false;
    static bool processed_resize = false;
    static bool in_scrollbar_loop = false;
    static UINT last_mousemove = 0;
    int resize_action;
#ifdef MOD_HYPERLINK
	/*
	 * HACK: PuttyTray / Nutty
	 */ 
	POINT cursor_pt;
#endif
    switch (message) {
      case WM_TIMER:
	if ((UINT_PTR)wParam == TIMING_TIMER_ID) {
	    unsigned long next;

	    KillTimer(hwnd, TIMING_TIMER_ID);
	    if (run_timers(timing_next_time, &next)) {
		timer_change_notify(next);
	    } else {
	    }
	}
#ifdef MOD_PERSO
else if((UINT_PTR)wParam == TIMER_INIT) {  // Initialisation
	char buffer[4096] = "" ;

	// On charge automatiquement au démarrage (-edit) un fichier dans l'editeur connecté
	if( !PuttyFlag ) if( LoadFile!=NULL ) {
		RunPuttyEd( hwnd, LoadFile ) ;
		free( LoadFile ) ;
		LoadFile = NULL ; 
	}
	
	if( debug_flag ) { debug_logevent( "Starting post connection operations" ) ; }
	
	if( (conf_get_int(conf,CONF_protocol) == PROT_SSH) && (!is_backend_connected) ) break ; // On sort si en SSH on n'est pas connecte
	// Lancement d'une (ou plusieurs separees par \\n) commande(s) automatique(s) a l'initialisation

	KillTimer( hwnd, TIMER_INIT ) ;

	// Fullscreen automatic
	if( conf_get_int(conf,CONF_fullscreen) ) { PostMessage( hwnd, WM_COMMAND, IDM_FULLSCREEN, (LPARAM)NULL ) ; }

	// Maximize automatic
	if( conf_get_int(conf,CONF_maximize) ) { PostMessage( hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, (LPARAM)NULL ) ; }

	// Affichage d'une note de la session s'il y en a une
	if( GetSessionField( conf_get_str( conf, CONF_sessionname), conf_get_str( conf, CONF_folder), "Notes", buffer )  )
		{ if( strlen( buffer ) > 0 ) MessageBox( hwnd, buffer, "Notes", MB_OK ) ; }

	// On met le focus sur la fenêtre
	if( first_connect ) {
		if( GetVisibleFlag()!=VISIBLE_TRAY ) {
			BringWindowToTop( hwnd ) ;
			SetForegroundWindow( hwnd ) ;
		}
	}
	
	// Envoi automatiquement dans le systeme tray si besoin
	if( GetAutoSendToTray() ) ManageToTray( hwnd ) ;

	// Si telnet on essaie d'envoyer le password
	if( conf_get_int(conf,CONF_protocol) == PROT_TELNET ) {
		if( strlen( conf_get_str(conf,CONF_username) ) > 0 ) {
			if( strlen( conf_get_str(conf,CONF_password) ) > 0 ) {
				char bufpass[1024]; strcpy(bufpass,conf_get_str(conf,CONF_password)) ;
				MASKPASS(GetCryptSaltFlag(), bufpass); 
				strcat(buffer,bufpass); 
				memset(bufpass,0,strlen(bufpass));
				strcat( buffer, "\\n" ) ;
			}
		}
		if( strlen( conf_get_str(conf,CONF_autocommand) ) > 0 ) {
			strcat( buffer, "\\n\\p" ) ; 
			strcat( buffer, conf_get_str(conf,CONF_autocommand) ) ;
			strcat( buffer, "\\n" ) ;
		}
		if( strlen(buffer) > 0 ) { 
			conf_set_str( conf, CONF_autocommand, buffer ); 
		}
	}
	
	RenewPassword( conf ) ;

	// On envoie l'autocommand
	if( strlen( conf_get_str(conf,CONF_autocommand) ) > 0 ) {
		lp_eventlog(&wgs.logpolicy, "Start autocommand timer" );
		SetTimer(hwnd, TIMER_AUTOCOMMAND, autocommand_delay, NULL) ;
	}
	if( conf_get_int(conf,CONF_logtimerotation) > 0 ) {
		SetTimer(hwnd, TIMER_LOGROTATION, (int)( conf_get_int(conf,CONF_logtimerotation)*1000), NULL) ;
		lp_eventlog(&wgs.logpolicy, "Start log rotation" );
	}

	RefreshBackground( hwnd ) ;
	
	first_connect = false ;
	}

else if((UINT_PTR)wParam == TIMER_DND){
	KillTimer(hwnd, TIMER_DND) ;
	recupNomFichierDragDrop(hwnd, &hDropInf) ;
	if( hDropInf != NULL ) { free(hDropInf) ; hDropInf = NULL ; }
	InvalidateRect( hwnd, NULL, true ) ;
	}

else if((UINT_PTR)wParam == TIMER_AUTOCOMMAND) {  // Autocommand au demarrage

	KillTimer( hwnd, TIMER_AUTOCOMMAND ) ; 
	if( AutoCommand == NULL ) {
		ValidateRect( hwnd, NULL ) ; 
		AutoCommand = (char*) malloc( strlen(conf_get_str(conf,CONF_autocommand))+10 ) ;
		strcpy( AutoCommand, conf_get_str(conf,CONF_autocommand) ) ;
	}


	char buffer[8192] = "" ;
	int i = 0  ;
	while( AutoCommand[i] != '\0' ) {
		if( AutoCommand[i]=='\n' ) { i++ ; break ;
		} else if( (AutoCommand[i] == '\\') && (AutoCommand[i+1] == '\\') ) {
			strcat( buffer, "\\\\" ) ;
			i += 2 ;
		} else if( (AutoCommand[i] == '\\') && (AutoCommand[i+1] == 'n') ) { i += 2 ; break ;
		} else if( (AutoCommand[i] == '\\') && (AutoCommand[i+1] == 'p') ) {
			strcat( buffer, "\\p" ) ;
			i += 2 ;
			break ;
		} else if( (AutoCommand[i] == '\\') && (AutoCommand[i+1] == 's') ) {
			strcat( buffer, "\\s" ) ;
			i += 2 ;
			buffer[i] = AutoCommand[i] ; buffer[i+1] = '\0' ; i++ ;
			buffer[i] = AutoCommand[i] ; buffer[i+1] = '\0' ; i++ ;
			break ;
		} else { buffer[i] = AutoCommand[i] ; buffer[i+1] = '\0' ; i++ ; 
		}
	}
		
	del( AutoCommand, 1, i ) ; //AutoCommand += i ;
	if( strlen( buffer ) > 0 ) { SendAutoCommand( hwnd, buffer ) ; }
	if( AutoCommand[0] == '\0' ) {
		free( AutoCommand ) ; AutoCommand = NULL ; 
		InvalidateRect( hwnd, NULL, true ) ;
		}
	else { SetTimer(hwnd, TIMER_AUTOCOMMAND, autocommand_delay, NULL) ; }
	}

else if((UINT_PTR)wParam == TIMER_AUTOPASTE) {  // AutoPaste
	char buffer[4096] = "" ;
	int i = 0, j  ;
	KillTimer( hwnd, TIMER_AUTOPASTE ) ; 
	if( PasteCommand == NULL ) { ValidateRect( hwnd, NULL ) ; }
	else if( strlen( PasteCommand ) == 0 ) { free( PasteCommand ) ; PasteCommand = NULL ; }
	else {
		while( PasteCommand[0] != '\0' ) { 
			buffer[i] = PasteCommand[0] ; buffer[i+1] = '\0' ;
			j = 0 ; do { PasteCommand[j] = PasteCommand[j+1] ; j++ ; } while( PasteCommand[j] != '\0' ) ;
			if( buffer[i] == '\n' ) { buffer[i] = '\0' ; break ; }
			i++ ;
			}
		if( strlen( buffer ) > 0 ) { 
			SendAutoCommand( hwnd, buffer ) ;
			SetTimer(hwnd, TIMER_AUTOPASTE, autocommand_delay, NULL) ;
			}
		}
	}
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
else if( GetBackgroundImageFlag() && ((UINT_PTR)wParam == TIMER_SLIDEBG) ) {  // Changement automatique de l'image de fond
	NextBgImage( hwnd ) ;
	InvalidateRect( hwnd, NULL, true ) ;
	}
#endif
else if((UINT_PTR)wParam == TIMER_REDRAW) {  // rafraichissement automatique (bug d'affichage)
	RefreshBackground( hwnd ) ; // On inhibe cette fonction a cause du probleme de fuite memoire due a l'image de fond !!! 
	//InvalidateRect( hwnd, NULL, true ) ; // On remplace par
	}
else if((UINT_PTR)wParam == TIMER_ANTIIDLE) {  // Envoi de l'anti-idle
	AntiIdleCount += 1 ;
	if( AntiIdleCount >= AntiIdleCountMax ) {
		AntiIdleCount = 0 ;
		if(strlen( conf_get_str(conf,CONF_antiidle) ) > 0) SendAutoCommand( hwnd, conf_get_str(conf,CONF_antiidle) ) ;
		else if( strlen( AntiIdleStr ) > 0 ) SendAutoCommand( hwnd, AntiIdleStr ) ;
		}
#ifdef MOD_RECONNECT
	if(!backend||!is_backend_connected) { // On essaie de se reconnecter en cas de problème de connexion
		if ( conf_get_int(conf,CONF_failure_reconnect) && is_backend_first_connected ) {
			lp_eventlog(&wgs.logpolicy, "No connection, trying to reconnect...") ; 
			SetTimer(hwnd, TIMER_RECONNECT, GetReconnectDelay()*1000, NULL) ; 
			}
			break;
		}
#endif
	}
else if((UINT_PTR)wParam == TIMER_BLINKTRAYICON) {  // Clignotement de l'icone dans le systeme tray sur reception d'un signal BELL (printf '\007' pour simuler)
	static int BlinkingState = 0 ;
	static HICON hBlinkingIcon = NULL ; 

	if( GetVisibleFlag()!=VISIBLE_TRAY ) {
		KillTimer( hwnd, TIMER_BLINKTRAYICON ) ; TrayIcone.hIcon = hBlinkingIcon ; BlinkingState = 0 ; break ; 
	}
	if( (BlinkingState%2)==0 ) {
		hBlinkingIcon = TrayIcone.hIcon ;
		TrayIcone.hIcon = LoadIcon(NULL, NULL) ;
		Shell_NotifyIcon(NIM_MODIFY, &TrayIcone) ;
		BlinkingState++ ;
	} else {
		if( hBlinkingIcon ) {
			TrayIcone.hIcon = hBlinkingIcon ;
			Shell_NotifyIcon(NIM_MODIFY, &TrayIcone) ;
			BlinkingState++ ;
			if(MaxBlinkingTime) if(BlinkingState>=MaxBlinkingTime) { KillTimer( hwnd, TIMER_BLINKTRAYICON ) ; BlinkingState = 0 ; break ; }
		}
	}
}
#ifdef MOD_RECONNECT
else if((UINT_PTR)wParam == TIMER_RECONNECT) {
	if( !backend ) { 
		lp_eventlog(&wgs.logpolicy, "No backend connection, reconnecting...") ;
		PostMessage( hwnd, WM_COMMAND, IDM_RESTART, 0 ) ; 
	}
	KillTimer( hwnd, TIMER_RECONNECT ) ;
}
#endif
else if((UINT_PTR)wParam == TIMER_LOGROTATION) {  // log rotation
	timestamp_change_filename() ;
	}
#endif
	return 0;
      case WM_CREATE:
#ifdef MOD_PERSO
	      {
		// Initialiation
		MainHwnd = hwnd ;
		if( debug_flag ) { lp_eventlog(&wgs.logpolicy,"Starting window creation") ; }
      		if( GetIconeFlag() != -1 )
			SetNewIcon( hwnd, conf_get_filename(conf,CONF_iconefile)->path, conf_get_int(conf,CONF_icone), SI_INIT ) ;

		// Gestion de la transparence
		if( GetTransparencyFlag() && conf_get_int(conf,CONF_transparencynumber) >= 0 ) {
			SetWindowLongPtr(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED ) ;
			//SetLayeredWindowAttributes(hwnd, 0, TransparencyNumber, LWA_ALPHA) ;
			SetTransparency( hwnd, 255-conf_get_int(conf,CONF_transparencynumber) ) ;
			}

#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
		if( GetIconeFlag() == -1 ) conf_set_int( conf, CONF_bg_type, 0 ) ; 
		if( !GetBackgroundImageFlag() ) conf_set_int( conf, CONF_bg_type, 0 ); 
#endif
		if( !PuttyFlag )
		if( conf_get_bool( conf,CONF_saveonexit) && conf_get_int( conf,CONF_windowstate) ) 
			PostMessage( hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, (LPARAM)NULL );
		if( conf_get_int(conf, CONF_ctrl_tab_switch) && GetCtrlTabFlag() ) {
			int wndExtra = GetClassLong(hwnd, GCL_CBWNDEXTRA);
			FILETIME filetime;
			GetSystemTimeAsFileTime(&filetime);
			SetWindowLong(hwnd, wndExtra - 8, filetime.dwHighDateTime);
			SetWindowLong(hwnd, wndExtra - 4, filetime.dwLowDateTime);
			}
#ifdef MOD_WTS
		WTSRegisterSessionNotification(hwnd,NOTIFY_FOR_THIS_SESSION);
#endif
		//SetFocus(hwnd);
		//PostMessage(hwnd,WM_SETFOCUS,0,0);
		}
#endif
	break;
      case WM_CLOSE: {
#ifdef MOD_PERSO
	if( GetProtectFlag() ) {
		MessageBox(hwnd,
			   "You are not allowed to close a protected window",
			   "Exit warning", MB_ICONERROR | MB_OK ) ;
		return 0 ;
		}
	    char *str;
	    show_mouseptr(1);
	    str = dupprintf("%s Exit Confirmation", appname);
		
#ifdef MOD_RUTTY
/* rutty: */
	    if (scriptdata.runs)
		{
		  if (session_closed ||
		  MessageBox(hwnd,
			   "session scripting is running !\n Are you sure you want to close this session?",
			   str, MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1) == IDOK)
		  {
        script_close(&scriptdata);
        script_record_stop(&scriptdata);
		  	DestroyWindow(hwnd);
		  }
		}
		else
#endif /* rutty */
	    if ( !conf_get_bool(conf,CONF_warn_on_close) || session_closed ||
		MessageBox(hwnd,
			   "Are you sure you want to close this session?",
			   str, MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1)
		== IDOK) {
#ifdef MOD_RUTTY
			script_record_stop(&scriptdata);
#endif
			if( strlen( conf_get_str(conf,CONF_autocommandout) ) > 0 ) { //Envoie d'une command automatique en sortant
				SendAutoCommand( hwnd, conf_get_str(conf,CONF_autocommandout) ) ;
				conf_set_str(conf,CONF_autocommandout,"" );
				}
			DestroyWindow(hwnd);
			}
	    sfree(str);
#else
        char *title, *msg, *additional = NULL;
	show_mouseptr(true);
        title = dupprintf("%s Exit Confirmation", appname);
        if (backend && backend->vt->close_warn_text) {
            additional = backend->vt->close_warn_text(backend);
        }
        msg = dupprintf("Are you sure you want to close this session?%s%s",
                        additional ? "\n" : "",
                        additional ? additional : "");
        if (session_closed || !conf_get_bool(conf, CONF_warn_on_close) ||
            MessageBox(hwnd, msg, title,
                       MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1)
		== IDOK)
	DestroyWindow(hwnd);
        sfree(title);
        sfree(msg);
        sfree(additional);
#endif
	return 0;
	}
      case WM_DESTROY:
	show_mouseptr(true);
	PostQuitMessage(0);
	return 0;
      case WM_INITMENUPOPUP:
	if ((HMENU)wParam == savedsess_menu) {
	    /* About to pop up Saved Sessions sub-menu.
	     * Refresh the session list. */
	    get_sesslist(&sesslist, false); /* free */
	    get_sesslist(&sesslist, true);
	    update_savedsess_menu();
	    return 0;
	}
	break;
      case WM_COMMAND:
      case WM_SYSCOMMAND:
#ifdef MOD_PERSO
      	if( strlen( conf_get_str(conf,CONF_folder) )>0 ) SetInitCurrentFolder( conf_get_str(conf,CONF_folder) );
	if( (wParam>=IDM_USERCMD)&&(wParam<(IDM_USERCMD+NB_MENU_MAX)) ) {
	  	ManageSpecialCommand( hwnd, wParam-IDM_USERCMD ) ;
	        break ;
		}
#endif
	switch (wParam & ~0xF) {       /* low 4 bits reserved to Windows */
          case SC_VSCROLL:
          case SC_HSCROLL:
            if (message == WM_SYSCOMMAND) {
                /* As per the long comment in WM_VSCROLL handler: give
                 * this message the default handling, which starts a
                 * subsidiary message loop, but set a flag so that
                 * when we're re-entered from that loop, scroll events
                 * within an interactive scrollbar-drag can be handled
                 * differently. */
                in_scrollbar_loop = true;
                LRESULT result = DefWindowProcW(hwnd, message, wParam, lParam);
                in_scrollbar_loop = false;
                return result;
            }
            break;
	  case IDM_SHOWLOG:
	    showeventlog(hwnd);
	    break;
	  case IDM_NEWSESS:
	  case IDM_DUPSESS:
          case IDM_SAVEDSESS: {
		char b[2048];
#ifdef MOD_PERSO
		char *cl=NULL;
#else
		char *cl;
#endif
                const char *argprefix;
		bool inherit_handles;
		STARTUPINFO si;
		PROCESS_INFORMATION pi;
		HANDLE filemap = NULL;

            if (restricted_acl())
                    argprefix = "&R";
	    else
                    argprefix = "";

		if (wParam == IDM_DUPSESS) {
		    /*
		     * Allocate a file-mapping memory chunk for the
		     * config structure.
		     */
		    SECURITY_ATTRIBUTES sa;
                    strbuf *serbuf;
		    void *p;
		    int size;

                    serbuf = strbuf_new();
#ifdef MOD_PERSO
/*On gère le répertoire distant*/
char *cmd, *cmd2=NULL ;
cmd = (char*)malloc( strlen(conf_get_str(conf,CONF_autocommand))+1 ) ;
strcpy( cmd, conf_get_str(conf,CONF_autocommand) ) ;

if( GetRemotePath()!=NULL ) {
	cmd2 = (char*)malloc( strlen(conf_get_str(conf,CONF_autocommand))+strlen(GetRemotePath())+10 ) ;
	strcpy( cmd2, cmd ) ;	
	strcat(cmd2,"\n");
	strcat(cmd2,"cd ");
	strcat(cmd2, GetRemotePath());
} else {
	cmd2 = (char*)malloc( strlen(conf_get_str(conf,CONF_autocommand))+1 ) ;
	strcpy( cmd2, cmd ) ;
}
conf_set_str( conf,CONF_autocommand,cmd2 ) ;
#endif
		    conf_serialise(BinarySink_UPCAST(serbuf), conf);
                    size = serbuf->len;

		    sa.nLength = sizeof(sa);
		    sa.lpSecurityDescriptor = NULL;
		    sa.bInheritHandle = true;
		    filemap = CreateFileMapping(INVALID_HANDLE_VALUE,
						&sa,
						PAGE_READWRITE,
						0, size, NULL);
		    if (filemap && filemap != INVALID_HANDLE_VALUE) {
			p = MapViewOfFile(filemap, FILE_MAP_WRITE, 0, 0, size);
			if (p) {
			    memcpy(p, serbuf->s, size);
			    UnmapViewOfFile(p);
			}
		    }

#ifdef MOD_PERSO
conf_set_str( conf,CONF_autocommand,cmd ) ;
free(cmd2);
free(cmd);
#endif
                    strbuf_free(serbuf);
		    inherit_handles = true;
		    cl = dupprintf("putty %s&%p:%u", argprefix,
                                   filemap, (unsigned)size);
		} else if (wParam == IDM_SAVEDSESS) {
		    unsigned int sessno = ((lParam - IDM_SAVED_MIN)
					   / MENU_SAVED_STEP) + 1;
		    if (sessno < (unsigned)sesslist.nsessions) {
			const char *session = sesslist.sessions[sessno];
			cl = dupprintf("putty %s@%s", argprefix, session);
#ifdef MOD_PERSO
			GetSessionFolderName( conf_get_str(conf,CONF_sessionname), conf_get_str(conf,CONF_folder) ) ;
			if( GetDirectoryBrowseFlag() ) if( strcmp(conf_get_str(conf,CONF_folder), "")&&strcmp(conf_get_str(conf,CONF_folder),"Default") )
				{ strcat( cl, " -folder \"" ) ; strcat( cl, conf_get_str(conf,CONF_folder) ) ; strcat( cl, "\"" ) ; }
#endif
			inherit_handles = false;
		    } else
			break;
#ifdef MOD_PERSO
	    } else /* IDM_NEWSESS */ {
		if( strcmp(conf_get_str(conf,CONF_folder), "") && strcmp(conf_get_str(conf,CONF_folder),"Default") ) {
			cl = realloc( cl, 20 + strlen(conf_get_str(conf,CONF_folder)) ) ;
			sprintf(cl, "putty -folder \"%s\"", conf_get_str(conf,CONF_folder) ) ;
		} else { if( cl==NULL ) { cl = (char*)malloc(2) ; cl[0]='\0' ; } }
#else
		} else /* IDM_NEWSESS */ {
                    cl = dupprintf("putty%s%s",
                                   *argprefix ? " " : "",
                                   argprefix);
#endif
		    inherit_handles = false;
		}

		GetModuleFileName(NULL, b, sizeof(b) - 1);
		si.cb = sizeof(si);
		si.lpReserved = NULL;
		si.lpDesktop = NULL;
		si.lpTitle = NULL;
		si.dwFlags = 0;
		si.cbReserved2 = 0;
		si.lpReserved2 = NULL;
		CreateProcess(b, cl, NULL, NULL, inherit_handles,
			      NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);

		if (filemap)
		    CloseHandle(filemap);
                sfree(cl);
	    break;
          }
	  case IDM_RESTART:
            if (!backend) {
                lp_eventlog(&wgs.logpolicy, "----- Session restarted -----");
		term_pwron(term, false);
#ifdef MOD_PERSO
#ifdef MOD_PORTKNOCKING
		ManagePortKnocking(conf_get_str(conf,CONF_host),conf_get_str(conf,CONF_portknockingoptions));
#endif
#ifdef MOD_RECONNECT
		SetNewIcon( hwnd, conf_get_filename(conf,CONF_iconefile)->path, 0, SI_INIT ) ;
		is_backend_connected = 0 ;
		start_backend();
		//if( (!backend || !is_backend_connected) && GetAutoreconnectFlag() ) {
		if( !backend ) {
		    if ( conf_get_int(conf,CONF_failure_reconnect) && is_backend_first_connected ) {
			SetTimer(hwnd, TIMER_RECONNECT, GetReconnectDelay()*1000, NULL) ; 
			lp_eventlog(&wgs.logpolicy, "Unable to connect, trying to reconnect...") ; 
		    }
		    break;
		}
#else
		is_backend_connected = 0 ;
		start_backend();
#endif
		is_backend_connected = 1 ;
		SetTimer(hwnd, TIMER_INIT, init_delay, NULL) ;
#else
		start_backend();
#endif
	    }

	    break;
          case IDM_RECONF: {
		Conf *prev_conf;
		int init_lvl = 1;
		bool reconfig_result;
		if (reconfiguring)
		    break;
		else
		    reconfiguring = true;
#ifndef MOD_PERSO
		char buftitle[1024] = "" ;
		GetWindowText(hwnd, buftitle, sizeof(buftitle));
		conf_set_str( conf, CONF_wintitle, buftitle ) ;

            term_pre_reconfig(term, conf);
#endif
	    prev_conf = conf_copy(conf);
#ifdef MOD_PERSO
	if( force_reconf++ ) {
		reconfig_result =
                    do_reconfig(hwnd, conf,  backend ? backend_cfg_info(backend) : 0);
		reconfiguring = false;
		if (!reconfig_result) {
                    conf_free(prev_conf);
		    break;
                }
	} else {
		reconfig_result = true ;
		reconfiguring = false ;
	}
		SaveRegistryKey() ;
#else
            reconfig_result = do_reconfig(
                hwnd, conf, backend ? backend_cfg_info(backend) : 0);
		reconfiguring = false;
		if (!reconfig_result) {
                    conf_free(prev_conf);
		    break;
                }
#endif
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
		if( GetBackgroundImageFlag() && (!GetPuttyFlag()) ) {
			if(textdc) {
				DeleteObject(textbm);
				DeleteDC(textdc);
				textdc = NULL;
				textbm = NULL;
			}
			if(backgrounddc) {
				DeleteObject(backgroundbm);
				DeleteDC(backgrounddc);
				backgrounddc = NULL;
				backgroundbm = NULL;
			}
			if(backgroundblenddc) {
				DeleteObject(backgroundblendbm);
				DeleteDC(backgroundblenddc);
				backgroundblenddc = NULL;
				backgroundblendbm = NULL;
			}
		}
#endif

		conf_cache_data();

		resize_action = conf_get_int(conf, CONF_resize_action);
		{
		    /* Disable full-screen if resizing forbidden */
		    int i;
		    for (i = 0; i < lenof(popup_menus); i++)
			EnableMenuItem(popup_menus[i].menu, IDM_FULLSCREEN,
				       MF_BYCOMMAND | 
                                       (resize_action == RESIZE_DISABLED
                                        ? MF_GRAYED : MF_ENABLED));
		    /* Gracefully unzoom if necessary */
		    if (IsZoomed(hwnd) && (resize_action == RESIZE_DISABLED))
			ShowWindow(hwnd, SW_RESTORE);
#ifdef MOD_TUTTY
		    HMENU m = GetSystemMenu (hwnd, FALSE);
		    EnableMenuItem(m, SC_CLOSE, conf_get_int(conf,CONF_window_closable) ? MF_ENABLED : MF_GRAYED);
#endif

		}

		/* Pass new config data to the logging module */
		log_reconfig(logctx, conf);

		sfree(logpal);
		/*
		 * Flush the line discipline's edit buffer in the
		 * case where local editing has just been disabled.
		 */
		if (ldisc) {
                    ldisc_configure(ldisc, conf);
		    ldisc_echoedit_update(ldisc);
                }
            if (conf_get_bool(conf, CONF_system_colour) !=
                conf_get_bool(prev_conf, CONF_system_colour))
                term_notify_palette_changed(term);

		/* Pass new config data to the terminal */
		term_reconfig(term, conf);
                setup_clipboards(term, conf);

            /* Reinitialise the colour palette, in case the terminal
             * just read new settings out of Conf */
            if (pal)
                DeleteObject(pal);
            logpal = NULL;
            pal = NULL;
            init_palette();

		/* Pass new config data to the back end */
                if (backend)
                    backend_reconfig(backend, conf);
#ifdef MOD_HYPERLINK
		/*
		 * HACK: PuttyTray / Nutty
		 * Reconfigure
		 */
		if( !GetPuttyFlag() && GetHyperlinkFlag() ) {
			if( conf_get_int(conf, CONF_url_defregex) != 0 ) {
				urlhack_set_regular_expression(URLHACK_REGEX_CLASSIC, conf_get_str(term->conf,CONF_url_regex) ) ;
			} else {
				urlhack_set_regular_expression(URLHACK_REGEX_CUSTOM, conf_get_str(term->conf, CONF_url_regex) ) ;
			}
			term->url_update = TRUE;
			term_update(term);
			urlhack_enable();
		}
#endif

		/* Screen size changed ? */
		if (conf_get_int(conf, CONF_height) !=
		    conf_get_int(prev_conf, CONF_height) ||
		    conf_get_int(conf, CONF_width) !=
		    conf_get_int(prev_conf, CONF_width) ||
		    conf_get_int(conf, CONF_savelines) !=
		    conf_get_int(prev_conf, CONF_savelines) ||
		    resize_action == RESIZE_FONT ||
		    (resize_action == RESIZE_EITHER && IsZoomed(hwnd)) ||
		    resize_action == RESIZE_DISABLED)
		    term_size(term, conf_get_int(conf, CONF_height),
			      conf_get_int(conf, CONF_width),
			      conf_get_int(conf, CONF_savelines));

		/* Enable or disable the scroll bar, etc */
		{
		    LONG nflg, flag = GetWindowLongPtr(hwnd, GWL_STYLE);
		    LONG nexflag, exflag =
			GetWindowLongPtr(hwnd, GWL_EXSTYLE);

		    nexflag = exflag;
		    if (conf_get_bool(conf, CONF_alwaysontop) !=
			conf_get_bool(prev_conf, CONF_alwaysontop)) {
			if (conf_get_bool(conf, CONF_alwaysontop)) {
			    nexflag |= WS_EX_TOPMOST;
			    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE);
			} else {
			    nexflag &= ~(WS_EX_TOPMOST);
			    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE);
			}
		    }
		    if (conf_get_bool(conf, CONF_sunken_edge))
			nexflag |= WS_EX_CLIENTEDGE;
		    else
			nexflag &= ~(WS_EX_CLIENTEDGE);

		    nflg = flag;
		    if (conf_get_bool(conf, is_full_screen() ?
                                      CONF_scrollbar_in_fullscreen :
                                      CONF_scrollbar))
			nflg |= WS_VSCROLL;
		    else
			nflg &= ~WS_VSCROLL;

		    if (resize_action == RESIZE_DISABLED ||
                        is_full_screen())
			nflg &= ~WS_THICKFRAME;
		    else
			nflg |= WS_THICKFRAME;

#ifdef MOD_TUTTY
		    if (!conf_get_int(conf,CONF_window_has_sysmenu))
			nflg &= ~WS_SYSMENU;
		    else
			nflg |= WS_SYSMENU;
		    if (!conf_get_int(conf,CONF_window_minimizable))
			nflg &= ~WS_MINIMIZEBOX;
		    else
			nflg |= WS_MINIMIZEBOX;
		    if (resize_action == RESIZE_DISABLED || !conf_get_int(conf,CONF_window_maximizable))
#else		    
		    if (resize_action == RESIZE_DISABLED)
#endif
			nflg &= ~WS_MAXIMIZEBOX;
		    else
			nflg |= WS_MAXIMIZEBOX;

		    if (nflg != flag || nexflag != exflag) {
			if (nflg != flag)
			    SetWindowLongPtr(hwnd, GWL_STYLE, nflg);
			if (nexflag != exflag)
			    SetWindowLongPtr(hwnd, GWL_EXSTYLE, nexflag);

			SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
				     SWP_NOACTIVATE | SWP_NOCOPYBITS |
				     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
				     SWP_FRAMECHANGED);

			init_lvl = 2;
		    }
		}

		/* Oops */
		if (resize_action == RESIZE_DISABLED && IsZoomed(hwnd)) {
		    force_normal(hwnd);
		    init_lvl = 2;
		}

		{
		    FontSpec *font = conf_get_fontspec(conf, CONF_font);
		    FontSpec *prev_font = conf_get_fontspec(prev_conf,
                                                             CONF_font);

		    if (!strcmp(font->name, prev_font->name) ||
			!strcmp(conf_get_str(conf, CONF_line_codepage),
				conf_get_str(prev_conf, CONF_line_codepage)) ||
			font->isbold != prev_font->isbold ||
			font->height != prev_font->height ||
			font->charset != prev_font->charset ||
			conf_get_int(conf, CONF_font_quality) !=
			conf_get_int(prev_conf, CONF_font_quality) ||
			conf_get_int(conf, CONF_vtmode) !=
			conf_get_int(prev_conf, CONF_vtmode) ||
			conf_get_int(conf, CONF_bold_style) !=
			conf_get_int(prev_conf, CONF_bold_style) ||
			resize_action == RESIZE_DISABLED ||
			resize_action == RESIZE_EITHER ||
			resize_action != conf_get_int(prev_conf,
						      CONF_resize_action))
			init_lvl = 2;
		}

		InvalidateRect(hwnd, NULL, true);
		reset_window(init_lvl);
#ifdef MOD_PERSO
		if( GetIconeFlag() != -1 ) {
			SetNewIcon( hwnd, conf_get_filename(conf,CONF_iconefile)->path, conf_get_int(conf,CONF_icone), SI_INIT ) ;
		}
		{
			HMENU m = GetSystemMenu(hwnd, FALSE);
			if( conf_get_int(conf, CONF_logtype)!=LGTYP_NONE ) {
				EnableMenuItem(m, IDM_CLEARLOGFILE, MF_BYCOMMAND|MF_ENABLED) ;
			} else {
				EnableMenuItem(m, IDM_CLEARLOGFILE, MF_BYCOMMAND|MF_DISABLED|MF_GRAYED) ;
			}
		}
#endif

		conf_free(prev_conf);
	    break;
	  }
  	  case IDM_COPYALL:
            term_copyall(term, clips_system, lenof(clips_system));
	    break;
	  case IDM_COPY:
            term_request_copy(term, clips_system, lenof(clips_system));
	    break;
	  case IDM_PASTE:
#ifdef MOD_PERSO
	    if( !GetProtectFlag() ) 
#endif
	    term_request_paste(term, CLIP_SYSTEM);
	    break;
	  case IDM_CLRSB:
	    term_clrsb(term);
	    break;
	  case IDM_RESET:
	    term_pwron(term, true);
	    if (ldisc)
		ldisc_echoedit_update(ldisc);
	    break;
	  case IDM_ABOUT:
	    showabout(hwnd);
	    break;
/* rutty: */
#ifdef MOD_RUTTY
	  case IDM_SCRIPTHALT:
		script_close(&scriptdata);
		lp_eventlog(&wgs.logpolicy, "script stopped");
		break;
	  case IDM_SCRIPTSEND:
    {
      char scriptfilename[FILENAME_MAX]; 
      Filename * scriptfile;
		  if(prompt_scriptfile(hwnd, scriptfilename))
      {
		    script_init(&scriptdata, conf);	
    	  scriptfile = filename_from_str(scriptfilename);
        script_sendfile(&scriptdata, scriptfile);
        filename_free(scriptfile);
      }
     } 
	   break;
#endif  /* rutty */
#ifdef MOD_PERSO
	  case IDM_NEWDUPSESS:
		conf_set_str( conf, CONF_host_alt, conf_get_str( conf, CONF_host ) ) ;
	        RunSessionWithCurrentSettings( hwnd, conf, "", NULL, NULL, 0, NULL ) ;
		break;
	  /*case IDM_USERCMD:  
	  	ManageSpecialCommand( hwnd, wParam-IDM_USERCMD ) ;
	        break ;*/
	  case IDM_QUIT:
		SendMessage( hwnd, MYWM_NOTIFYICON, 0, WM_LBUTTONDBLCLK ) ;
		PostQuitMessage( 0 ) ;
		break ;
	  case IDM_PROTECT:
	  	ManageProtect( hwnd, wintw, conf_get_str(conf, CONF_wintitle) ) ;
		break ;
	  case IDM_VISIBLE: 
	  	ManageVisible( hwnd, wintw, conf_get_str(conf, CONF_wintitle) ) ;
		break ;
	  case IDM_SHORTCUTSTOGGLE:
		ManageShortcutsFlag( hwnd ) ;
		break ;
          case IDM_TOTRAY: 
		if( GetVisibleFlag()==VISIBLE_YES ) {
			SetVisibleFlag( VISIBLE_TRAY ) ;
			return ManageToTray( hwnd ) ;
			}
		break ;
	  case IDM_FROMTRAY:{
		if( GetVisibleFlag()==VISIBLE_TRAY ) {
			SetVisibleFlag( VISIBLE_YES ) ;
			return SendMessage( hwnd, MYWM_NOTIFYICON, 0, WM_LBUTTONDBLCLK ) ;
			}
		}
		break ;
#ifdef MOD_LAUNCHER
	  case IDM_HIDE:
		if( GetVisibleFlag()==VISIBLE_YES ) {
			/*if (IsWindowVisible(hwnd) )*/ ShowWindow(hwnd, SW_HIDE) ;
			SetVisibleFlag( VISIBLE_NO ) ;
			}
		break ;
	  case IDM_UNHIDE:
		if( GetVisibleFlag()==VISIBLE_NO ) {
			ShowWindow(hwnd, SW_RESTORE ) ;
			SetVisibleFlag( VISIBLE_YES ) ;
			}
		break ;
	  case IDM_SWITCH_HIDE:
		if( GetVisibleFlag()==VISIBLE_YES ) SendMessage( hwnd, WM_COMMAND, IDM_HIDE, 0 ) ;
		else if( GetVisibleFlag()==VISIBLE_NO ) SendMessage( hwnd, WM_COMMAND, IDM_UNHIDE, 0 ) ;
		break ;
	  case IDM_GONEXT:
		GoNext( hwnd ) ;
		break ;
	  case IDM_GOPREVIOUS:
		GoPrevious( hwnd ) ;
		break ;
#endif
          case IDM_WINROL: 
		if( GetWinrolFlag() ) {
			ManageWinrol( hwnd, conf_get_int(conf,CONF_resize_action) ) ;
		}
		break ;
	  case IDM_SCRIPTFILE :
		OpenAndSendScriptFile( hwnd ) ;
		break ;
	  case IDM_EXPORTSETTINGS :
		SaveCurrentSetting( hwnd ) ;
		break ;
	  case IDM_PSCP:
		SendFile( hwnd ) ;
		break ;
	  case IDM_WINSCP:
		StartWinSCP( hwnd, NULL, NULL, NULL ) ;
		break ;
	  case IDM_PRINT:
		ManagePrint( hwnd ) ;
		break ;
	  case IDM_FONTUP:
		ChangeFontSize(term,conf,hwnd,1) ;
		break ;
	  case IDM_FONTDOWN:
		ChangeFontSize(term,conf,hwnd,-1) ;
		break ;
	  case IDM_FONTBLACKANDWHITE:
		BlackOnWhiteColours(hwnd) ;
		break ;
	  case IDM_FONTNEGATIVE:
		NegativeColours(hwnd) ;
		break ;
	  case IDM_CLEARLOGFILE:
		if( conf_get_int(conf, CONF_logtype)!=LGTYP_NONE ) {
			logfile_reinit(logctx);
		}
		break;
#ifdef MOD_PORTKNOCKING
	  case IDM_PORTKNOCK:
		ManagePortKnocking(conf_get_str(conf,CONF_host),conf_get_str(conf,CONF_portknockingoptions));
		break;
#endif
	  case IDM_SHOWPORTFWD:
		ShowPortfwd( hwnd, conf ) ;
		break ;
#ifndef MOD_NOTRANSPARENCY
	  case IDM_TRANSPARUP: // Augmenter l'opacite (diminuer la transparence)
		if( GetTransparencyFlag() && conf_get_int(conf,CONF_transparencynumber) > 0 ) {
			conf_set_int( conf, CONF_transparencynumber, conf_get_int(conf,CONF_transparencynumber)-10 ) ;
			if( conf_get_int(conf,CONF_transparencynumber)<0) conf_set_int( conf, CONF_transparencynumber, 0 );
			SetTransparency( hwnd, 255-conf_get_int(conf,CONF_transparencynumber) ) ;
			}
		break ;
	  case IDM_TRANSPARDOWN: // Diminuer l'opacite (augmenter la transparence)
		if( GetTransparencyFlag() && (conf_get_int(conf,CONF_transparencynumber)!=-1) && (conf_get_int(conf,CONF_transparencynumber)<255) ) {
			if( conf_get_int(conf,CONF_transparencynumber)==245 ) MessageBox( hwnd, "      KiTTY made by      \r\nCyril Dupont\r\nLeonard Nero", "About", MB_OK ) ;
			conf_set_int( conf, CONF_transparencynumber, conf_get_int(conf,CONF_transparencynumber)+10 ) ;
			if( conf_get_int(conf,CONF_transparencynumber)>255) conf_set_int( conf, CONF_transparencynumber, 255 ) ;
			SetTransparency( hwnd, 255-conf_get_int(conf,CONF_transparencynumber) ) ;
			}
		break ;
#endif
	  case IDM_RESIZE: //Redmensionner
		{
		int w = LOWORD( lParam ), h = HIWORD( lParam ) ;
		if (w < 1) { w = 1 ; } if (h < 1) { h = 1 ; }
		conf_set_int( conf, CONF_width, w ) ; 
		conf_set_int( conf, CONF_height, h ) ; 
		term_size( term, h, w, conf_get_int( conf, CONF_savelines)  ) ;
		reset_window(0);
		}
		break ;
	  case IDM_REPOS: //Redmensionner
		{
		int x = LOWORD( lParam ), y = HIWORD( lParam ) ;
		if (x < 1) { x = 1 ; } if (y < 1) { y = 1 ; }
		conf_set_int( conf, CONF_xpos, x ) ; 
		conf_set_int( conf, CONF_ypos, y ) ; 
		SetWindowPos( hwnd, 0, x, y, 0, 0, SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOACTIVATE ) ;
		//MoveWindow(hwnd, x, y, 0, 0, TRUE ) ;
		//reset_window(0);
		}
		break ;
#endif
#ifdef MOD_RECONNECT
	case IDM_RESTARTSESSION: // Redemarre une session en cours
		if(!is_backend_connected) {
			PostMessage( hwnd, WM_COMMAND, IDM_RESTART, 0 ) ;
		} else {
			RestartSession() ;
		}
		break ;
#endif

	  case IDM_HELP:
	    launch_help(hwnd, NULL);
	    break;
	  case SC_MOUSEMENU:
	    /*
	     * We get this if the System menu has been activated
	     * using the mouse.
	     */
	    show_mouseptr(true);
	    break;
          case SC_KEYMENU:
	    /*
	     * We get this if the System menu has been activated
	     * using the keyboard. This might happen from within
	     * TranslateKey, in which case it really wants to be
	     * followed by a `space' character to actually _bring
	     * the menu up_ rather than just sitting there in
	     * `ready to appear' state.
	     */
	    show_mouseptr(true);    /* make sure pointer is visible */
	    if( lParam == 0 )
		PostMessage(hwnd, WM_CHAR, ' ', 0);
	    break;
	  case IDM_FULLSCREEN:
	    flip_full_screen();
	    break;
#ifdef MOD_ZMODEM
	case IDM_XYZSTART:
		if( (!GetPuttyFlag()) && GetZModemFlag() ) {
		xyz_ReceiveInit(term);
		xyz_updateMenuItems(term);
		}
		break;
	case IDM_XYZUPLOAD:
		if( (!GetPuttyFlag()) && GetZModemFlag() ) {
		xyz_StartSending(term);
		xyz_updateMenuItems(term);
		}
		break;
	case IDM_XYZABORT:
		xyz_Cancel(term);
		xyz_updateMenuItems(term);
		break;
#endif
	  default:
	    if (wParam >= IDM_SAVED_MIN && wParam < IDM_SAVED_MAX) {
		SendMessage(hwnd, WM_SYSCOMMAND, IDM_SAVEDSESS, wParam);
	    }
	    if (wParam >= IDM_SPECIAL_MIN && wParam <= IDM_SPECIAL_MAX) {
		int i = (wParam - IDM_SPECIAL_MIN) / 0x10;
		/*
		 * Ensure we haven't been sent a bogus SYSCOMMAND
		 * which would cause us to reference invalid memory
		 * and crash. Perhaps I'm just too paranoid here.
		 */
		if (i >= n_specials)
		    break;
                if (backend)
                    backend_special(
                        backend, specials[i].code, specials[i].arg);
	    }
	}
	break;
#ifdef MOD_PERSO
	case WM_DROPFILES: {
        	OnDropFiles(hwnd, (HDROP) wParam);
        	}
         	break;
/*
	case WM_LBUTTONDBLCLK: {
		URLclick( hwnd ) ;
		}
		break;
*/
	case MYWM_NOTIFYICON :
		switch (lParam)	{
			case WM_LBUTTONDBLCLK : 
				SetVisibleFlag( VISIBLE_YES ) ;
				//ShowWindow(hwnd, SW_SHOWNORMAL);
				ShowWindow(hwnd, SW_RESTORE);
				SetForegroundWindow( hwnd ) ;
				int ResShell;
				ResShell = Shell_NotifyIcon(NIM_DELETE, &TrayIcone);
				if( ResShell ) return 1 ;
				else return 0 ;
			break ;
			case WM_RBUTTONUP:
			case WM_LBUTTONUP: 
				DisplaySystemTrayMenu( hwnd ) ;
			break ;
		}
	break ;
	case WM_NCLBUTTONDBLCLK:
		return DefWindowProc(hwnd, message, wParam, lParam) ;
	break ;
	case WM_NCLBUTTONDOWN:
		if( ( GetKeyState( VK_CONTROL ) & 0x8000 ) && ( wParam == HTCAPTION ) ) {
			if( GetWinrolFlag() ) { ManageWinrol( hwnd, conf_get_int(conf,CONF_resize_action) ) ; }
		}
		else return DefWindowProc(hwnd, message, wParam, lParam);
	break;
	
	case WM_COPYDATA: {  // Reception d'un de donnees dans un message
		PCOPYDATASTRUCT pMyCDS = (PCOPYDATASTRUCT) lParam;
			switch( pMyCDS->dwData ) {	
				case 1: // Reception d'une chaine de caracteres a envoyer dans le terminal
					if( pMyCDS->cbData > 0 ) {
						SendKeyboardPlus( hwnd, (char*)pMyCDS->lpData ) ;
					}
					break ;
			}
		}
	break ;

#endif
#ifdef MOD_WTS
	case WM_WTSSESSION_CHANGE:
		if( wParam == WTS_SESSION_UNLOCK ) {
			RefreshBackground(hwnd);
		}
	break ;
#endif

#define X_POS(l) ((int)(short)LOWORD(l))
#define Y_POS(l) ((int)(short)HIWORD(l))

#define TO_CHR_X(x) ((((x)<0 ? (x)-font_width+1 : (x))-offset_width) / font_width)
#define TO_CHR_Y(y) ((((y)<0 ? (y)-font_height+1: (y))-offset_height) / font_height)
      case WM_LBUTTONDOWN:
      case WM_MBUTTONDOWN:
      case WM_RBUTTONDOWN:
#ifdef MOD_RECONNECT
	if( ((!backend) || (!is_backend_connected)) && GetAutoreconnectFlag() && is_backend_first_connected ) { // trying to reconnect
		PostMessage( hwnd, WM_COMMAND, IDM_RESTART, 0 ) ; 
		lp_eventlog(&wgs.logpolicy, "No connection on mouse click, trying to reconnect...") ;
		break ; 
	} 
#endif
      case WM_LBUTTONUP:
      case WM_MBUTTONUP:
      case WM_RBUTTONUP:
#ifdef MOD_PERSO
	if( GetProtectFlag() ) { break ; }
        if(!PuttyFlag && GetMouseShortcutsFlag() ) {
	if( (message == WM_LBUTTONUP) && ((wParam & MK_SHIFT) && (wParam & MK_CONTROL) ) ) { // shift + CTRL + left button => duplicate session
		if( backend && is_backend_connected ) {
			lp_eventlog(&wgs.logpolicy, "Duplicate session") ;
			SendMessage( hwnd, WM_COMMAND, IDM_DUPSESS, 0 ) ;
		}
#ifdef MOD_RECONNECT
		else { 
			if( is_backend_first_connected && GetAutoreconnectFlag() ) {
				SendMessage( hwnd, WM_COMMAND, IDM_RESTART, 0 ) ; 
				lp_eventlog(&wgs.logpolicy, "No connection on mouse click, trying to reconnect...") ;
			} 
		}
#endif
		break ;
	}

	else if (message == WM_LBUTTONUP && ((wParam & MK_CONTROL) ) ) { 				// ctrl + left button => new icon ?
		if( !GetHyperlinkFlag() || !conf_get_int(conf,CONF_url_ctrl_click) ) { 			// if no hyperlink feature or if ctrl + link is disable
			if( GetIconeFlag() != -1 ) {
				SetNewIcon( hwnd, conf_get_filename(conf,CONF_iconefile)->path, conf_get_int(conf,CONF_icone), SI_NEXT ) ;
			}
			break ;
		} else {
			if( !urlhack_is_in_link_region(TO_CHR_X(cursor_pt.x), TO_CHR_Y(cursor_pt.y)) ) { // If the is no hyperlink under mouse
				if( GetIconeFlag() != -1 ) {
					SetNewIcon( hwnd, conf_get_filename(conf,CONF_iconefile)->path, conf_get_int(conf,CONF_icone), SI_NEXT ) ;
				}
				break ;
			}
		}
	}

	else if (message == WM_MBUTTONUP && ((wParam & MK_CONTROL) ) ) { // ctrl+bouton milieu => send to tray
		SendMessage( hwnd, WM_COMMAND, IDM_TOTRAY, 0 ) ;
		break ;
		}

	else if ( GetPasteCommandFlag() && (message == WM_RBUTTONDOWN) && ((wParam & MK_SHIFT) ) ) {// shift+bouton droit => coller (paste) ameliore (pour serveur lent, on utilise la methode autocommand par TIMER)
		SetPasteCommand(wgs.term_hwnd) ;
		break ;
		}
	}
#endif
	if (message == WM_RBUTTONDOWN &&
	    ((wParam & MK_CONTROL) ||
	     (conf_get_int(conf, CONF_mouse_is_xterm) == 2))) {
	    POINT cursorpos;

	    show_mouseptr(true);    /* make sure pointer is visible */
	    GetCursorPos(&cursorpos);
	    TrackPopupMenu(popup_menus[CTXMENU].menu,
			   TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
			   cursorpos.x, cursorpos.y,
			   0, hwnd, NULL);
	    break;
	}
	{
	    int button;
            bool press;

	    switch (message) {
	      case WM_LBUTTONDOWN:
		button = MBT_LEFT;
		wParam |= MK_LBUTTON;
		press = true;
		break;
	      case WM_MBUTTONDOWN:
		button = MBT_MIDDLE;
		wParam |= MK_MBUTTON;
		press = true;
		break;
	      case WM_RBUTTONDOWN:
#ifdef MOD_PERSO
		if( !GetProtectFlag() ) { button = MBT_RIGHT ; wParam |= MK_RBUTTON ; press = true ; }
#else
		button = MBT_RIGHT;
		wParam |= MK_RBUTTON;
		press = true;
#endif
		break;
	      case WM_LBUTTONUP:
		button = MBT_LEFT;
		wParam &= ~MK_LBUTTON;
		press = false;
		break;
	      case WM_MBUTTONUP:
		button = MBT_MIDDLE;
		wParam &= ~MK_MBUTTON;
		press = false;
		break;
	      case WM_RBUTTONUP:
#ifdef MOD_PERSO
		if( !GetProtectFlag() ) { button = MBT_RIGHT ; wParam &= ~MK_RBUTTON ; press = false ; }
#else
		button = MBT_RIGHT;
		wParam &= ~MK_RBUTTON;
		press = false;
#endif
		break;
	      default: /* shouldn't happen */
		button = 0;
                press = false;
	    }
	    show_mouseptr(true);
	    /*
	     * Special case: in full-screen mode, if the left
	     * button is clicked in the very top left corner of the
	     * window, we put up the System menu instead of doing
	     * selection.
	     */
	    {
		bool mouse_on_hotspot = false;
		POINT pt;

		GetCursorPos(&pt);
#ifndef NO_MULTIMON
		{
		    HMONITOR mon;
		    MONITORINFO mi;

		    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);

		    if (mon != NULL) {
			mi.cbSize = sizeof(MONITORINFO);
			GetMonitorInfo(mon, &mi);

			if (mi.rcMonitor.left == pt.x &&
			    mi.rcMonitor.top == pt.y) {
			    mouse_on_hotspot = true;
			}
		    }
		}
#else
		if (pt.x == 0 && pt.y == 0) {
		    mouse_on_hotspot = true;
		}
#endif
		if (is_full_screen() && press &&
		    button == MBT_LEFT && mouse_on_hotspot) {
		    SendMessage(hwnd, WM_SYSCOMMAND, SC_MOUSEMENU,
				MAKELPARAM(pt.x, pt.y));
		    return 0;
		}
	    }

	    if (press) {
		click(button,
		      TO_CHR_X(X_POS(lParam)), TO_CHR_Y(Y_POS(lParam)),
		      wParam & MK_SHIFT, wParam & MK_CONTROL,
		      is_alt_pressed());
		SetCapture(hwnd);
	    } else {
		term_mouse(term, button, translate_button(button), MA_RELEASE,
			   TO_CHR_X(X_POS(lParam)),
			   TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
			   wParam & MK_CONTROL, is_alt_pressed());
		if (!(wParam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON)))
		    ReleaseCapture();
	    }
	}
	return 0;
      case WM_MOUSEMOVE: {
	    /*
	     * Windows seems to like to occasionally send MOUSEMOVE
	     * events even if the mouse hasn't moved. Don't unhide
	     * the mouse pointer in this case.
	     */
	    static WPARAM wp = 0;
	    static LPARAM lp = 0;
	    if (wParam != wp || lParam != lp ||
		last_mousemove != WM_MOUSEMOVE) {
		show_mouseptr(true);
		wp = wParam; lp = lParam;
		last_mousemove = WM_MOUSEMOVE;
	    }
	/*
	 * Add the mouse position and message time to the random
	 * number noise.
	 */
	noise_ultralight(NOISE_SOURCE_MOUSEPOS, lParam);
#ifdef MOD_HYPERLINK
	if( !GetPuttyFlag() && GetHyperlinkFlag() ) {
	/*
	 * HACK: PuttyTray / Nutty
	 * Hyperlink stuff: Change cursor type if hovering over link
	 */ 
	if (urlhack_mouse_old_x != TO_CHR_X(X_POS(lParam)) || urlhack_mouse_old_y != TO_CHR_Y(Y_POS(lParam))) {
		urlhack_mouse_old_x = TO_CHR_X(X_POS(lParam));
		urlhack_mouse_old_y = TO_CHR_Y(Y_POS(lParam));
		if ((!conf_get_int(term->conf, CONF_url_ctrl_click) || urlhack_is_ctrl_pressed()) &&
			urlhack_is_in_link_region(urlhack_mouse_old_x, urlhack_mouse_old_y)) {
				if (urlhack_cursor_is_hand == 0) {
					SetClassLongPtr(hwnd,  GCLP_HCURSOR, (LONG_PTR)LoadCursor(NULL, IDC_HAND));
					urlhack_cursor_is_hand = 1;
					term_update(term); // Force the terminal to update, otherwise the underline will not show (bug somewhere, this is an ugly fix)
				}
		}
		else if (urlhack_cursor_is_hand == 1) {
			SetClassLongPtr(hwnd,  GCLP_HCURSOR, (LONG_PTR)LoadCursor(NULL, IDC_IBEAM));
			urlhack_cursor_is_hand = 0;
			term_update(term); // Force the terminal to update, see above
		}
		// If mouse jumps from one link directly into another, we need a forced terminal update too
		if (urlhack_is_in_link_region(urlhack_mouse_old_x, urlhack_mouse_old_y) != urlhack_current_region) {
			urlhack_current_region = urlhack_is_in_link_region(urlhack_mouse_old_x, urlhack_mouse_old_y);
			term_update(term);
		}
	}
	/* HACK: PuttyTray / Nutty : END */
	}
#endif
#ifdef MOD_PERSO
	{
            // debug("X %d Y %d => %d:%d\n", X_POS(lParam), Y_POS(lParam), TO_CHR_X(X_POS(lParam)), TO_CHR_Y(Y_POS(lParam)));
            Mouse_Button b;
            if (wParam & MK_LBUTTON)
                b = MBT_LEFT;
            else if (wParam & MK_MBUTTON)
               b = MBT_MIDDLE;
            else if (wParam & MK_RBUTTON)
 	       b = MBT_RIGHT;
            else
                b = MBT_NOTHING;
            term_mouse(term, b, translate_button(b),
                       b == MBT_NOTHING ? MA_MOVE : MA_DRAG,
		       TO_CHR_X(X_POS(lParam)),
		       TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
		       wParam & MK_CONTROL, is_alt_pressed());
#else
	if (wParam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON) &&
	    GetCapture() == hwnd) {
	    Mouse_Button b;
	    if (wParam & MK_LBUTTON)
		b = MBT_LEFT;
	    else if (wParam & MK_MBUTTON)
		b = MBT_MIDDLE;
	    else
		b = MBT_RIGHT;
	    term_mouse(term, b, translate_button(b), MA_DRAG,
		       TO_CHR_X(X_POS(lParam)),
		       TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
		       wParam & MK_CONTROL, is_alt_pressed());
#endif
	}
	return 0;
      }
      case WM_NCMOUSEMOVE: {
	    static WPARAM wp = 0;
	    static LPARAM lp = 0;
	    if (wParam != wp || lParam != lp ||
		last_mousemove != WM_NCMOUSEMOVE) {
		show_mouseptr(true);
		wp = wParam; lp = lParam;
		last_mousemove = WM_NCMOUSEMOVE;
	    }
	noise_ultralight(NOISE_SOURCE_MOUSEPOS, lParam);
	break;
      }
      case WM_IGNORE_CLIP:
	ignore_clip = wParam;	       /* don't panic on DESTROYCLIPBOARD */
	break;
      case WM_DESTROYCLIPBOARD:
#ifdef MOD_FAR2L
        /* far2l extensions support */

        // In far2l extensions mode we should not do anything here,
        // clipboard is handled by far2l extensions.
        if (!(term->far2l_ext == 1) || (!term->clip_allowed)) {
            if (!ignore_clip)
                term_lost_clipboard_ownership(term, CLIP_SYSTEM);
            ignore_clip = false;
        }

        return 0;
#else
	if (!ignore_clip)
	    term_lost_clipboard_ownership(term, CLIP_SYSTEM);
	ignore_clip = false;
	return 0;
#endif
      case WM_PAINT: {
	    PAINTSTRUCT p;
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
        HDC hdccod=0, hdcScreen, hdcBack = 0;
	if( GetBackgroundImageFlag() && (!GetPuttyFlag()) ) {
		HideCaret(hwnd);
		hdcScreen = BeginPaint(hwnd, &p);

        // We'll draw into a temporary buffer then copy to the screen.  After
        // this point, the rest of this routine is written to use hdc and not
        // care whether hdc is a screen or back buffer, until the very end,
        // where the back buffer, if it exists, is blitted to the screen.  That
        // keeps the routine flexible for use with different drawing policies,
        // though right now the only policy we ever use is the one implied by
        // the next line, where we always use a back buffer.
        //hdc = hdcBack = CreateCompatibleDC(hdcScreen);
		hdccod = hdcScreen;

		if(pal)  {
			SelectPalette(hdcScreen, pal, true);
			RealizePalette(hdcScreen);
		}
	} else {
		HideCaret(hwnd);
		hdc = BeginPaint(hwnd, &p);
		if (pal) {
			SelectPalette(hdc, pal, true);
			RealizePalette(hdc);
		}
	}
#else
	    HideCaret(hwnd);
	    hdc = BeginPaint(hwnd, &p);
	    if (pal) {
		SelectPalette(hdc, pal, true);
		RealizePalette(hdc);
	    }
#endif

	    /*
	     * We have to be careful about term_paint(). It will
	     * set a bunch of character cells to INVALID and then
	     * call do_paint(), which will redraw those cells and
	     * _then mark them as done_. This may not be accurate:
	     * when painting in WM_PAINT context we are restricted
	     * to the rectangle which has just been exposed - so if
	     * that only covers _part_ of a character cell and the
	     * rest of it was already visible, that remainder will
	     * not be redrawn at all. Accordingly, we must not
	     * paint any character cell in a WM_PAINT context which
	     * already has a pending update due to terminal output.
	     * The simplest solution to this - and many, many
	     * thanks to Hung-Te Lin for working all this out - is
	     * not to do any actual painting at _all_ if there's a
	     * pending terminal update: just mark the relevant
	     * character cells as INVALID and wait for the
	     * scheduled full update to sort it out.
	     * 
	     * I have a suspicion this isn't the _right_ solution.
	     * An alternative approach would be to have terminal.c
	     * separately track what _should_ be on the terminal
	     * screen and what _is_ on the terminal screen, and
	     * have two completely different types of redraw (one
	     * for full updates, which syncs the former with the
	     * terminal itself, and one for WM_PAINT which syncs
	     * the latter with the former); yet another possibility
	     * would be to have the Windows front end do what the
	     * GTK one already does, and maintain a bitmap of the
	     * current terminal appearance so that WM_PAINT becomes
	     * completely trivial. However, this should do for now.
	     */
            //assert(!wintw_hdc);
            wintw_hdc = hdc;
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
	if( GetBackgroundImageFlag() && (!GetPuttyFlag()) ) {
        term_paint(term,
            (p.rcPaint.left-offset_width)/font_width,
            (p.rcPaint.top-offset_height)/font_height,
            (p.rcPaint.right-offset_width-1)/font_width,
            (p.rcPaint.bottom-offset_height-1)/font_height,
            !term->window_update_pending);
	
        SelectObject(hdccod, GetStockObject(SYSTEM_FONT));
        SelectObject(hdccod, GetStockObject(WHITE_PEN));

        if(hdcBack) {
            // Blit the back buffer to the real DC.
            BitBlt(
                hdcScreen,
                p.rcPaint.left - offset_width,
                p.rcPaint.top - offset_height,
                p.rcPaint.right - p.rcPaint.left + offset_width,
                p.rcPaint.bottom - p.rcPaint.top + offset_height,
                hdcBack, p.rcPaint.left, p.rcPaint.top, SRCCOPY
            );
            
            DeleteDC(hdcBack);
            hdccod = hdcScreen;
        }
        
        // Last paint edges
        paint_term_edges(term,hdccod, p.rcPaint.left, p.rcPaint.top, p.rcPaint.right, p.rcPaint.bottom);
	
        EndPaint(hwnd, &p);
        ShowCaret(hwnd);
	} else {
#endif
	    term_paint(term, 
		       (p.rcPaint.left-offset_width)/font_width,
		       (p.rcPaint.top-offset_height)/font_height,
		       (p.rcPaint.right-offset_width-1)/font_width,
		       (p.rcPaint.bottom-offset_height-1)/font_height,
		       !term->window_update_pending);
            wintw_hdc = NULL;

	    if (p.fErase ||
	        p.rcPaint.left  < offset_width  ||
		p.rcPaint.top   < offset_height ||
		p.rcPaint.right >= offset_width + font_width*term->cols ||
		p.rcPaint.bottom>= offset_height + font_height*term->rows)
	    {
		HBRUSH fillcolour, oldbrush;
		HPEN   edge, oldpen;
		fillcolour = CreateSolidBrush (
				    colours[ATTR_DEFBG>>ATTR_BGSHIFT]);
		oldbrush = SelectObject(hdc, fillcolour);
		edge = CreatePen(PS_SOLID, 0, 
				    colours[ATTR_DEFBG>>ATTR_BGSHIFT]);
		oldpen = SelectObject(hdc, edge);

		/*
		 * Jordan Russell reports that this apparently
		 * ineffectual IntersectClipRect() call masks a
		 * Windows NT/2K bug causing strange display
		 * problems when the PuTTY window is taller than
		 * the primary monitor. It seems harmless enough...
		 */

		IntersectClipRect(hdc,
			p.rcPaint.left, p.rcPaint.top,
			p.rcPaint.right, p.rcPaint.bottom);

		ExcludeClipRect(hdc, 
			offset_width, offset_height,
			offset_width+font_width*term->cols,
			offset_height+font_height*term->rows);

		Rectangle(hdc, p.rcPaint.left, p.rcPaint.top, 
			  p.rcPaint.right, p.rcPaint.bottom);

		/* SelectClipRgn(hdc, NULL); */

		SelectObject(hdc, oldbrush);
		DeleteObject(fillcolour);
		SelectObject(hdc, oldpen);
		DeleteObject(edge);
	    }
	    SelectObject(hdc, GetStockObject(SYSTEM_FONT));
	    SelectObject(hdc, GetStockObject(WHITE_PEN));
	    EndPaint(hwnd, &p);
	    ShowCaret(hwnd);
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
    }
#endif
	return 0;
      }
      case WM_NETEVENT: {
            /*
             * To protect against re-entrancy when Windows's recv()
             * immediately triggers a new WSAAsyncSelect window
             * message, we don't call select_result directly from this
             * handler but instead wait until we're back out at the
             * top level of the message loop.
             */
            struct wm_netevent_params *params =
                snew(struct wm_netevent_params);
            params->wParam = wParam;
            params->lParam = lParam;
            queue_toplevel_callback(wm_netevent_callback, params);
	return 0;
      }
      case WM_SETFOCUS:
#ifdef MOD_PERSO
        if( GetTransparencyFlag() && conf_get_int(conf,CONF_transparencynumber) >= 0 ) 
		{ SetTransparency( hwnd, 255-conf_get_int(conf,CONF_transparencynumber) ) ; }
	//RefreshBackground( hwnd ) ;
#endif
	term_set_focus(term, true);
	CreateCaret(hwnd, caretbm, font_width, font_height);
	ShowCaret(hwnd);
	flash_window(0);	       /* stop */
	compose_state = 0;
	term_update(term);
	break;
      case WM_KILLFOCUS:
	show_mouseptr(true);
	term_set_focus(term, false);
	DestroyCaret();
	caret_x = caret_y = -1;	       /* ensure caret is replaced next time */
	term_update(term);
	break;
      case WM_ENTERSIZEMOVE:
#ifdef RDB_DEBUG_PATCH
	debug("WM_ENTERSIZEMOVE\n");
#endif
	EnableSizeTip(true);
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
	if( GetBackgroundImageFlag() && (!PuttyFlag) ) GetClientRect(hwnd, &size_before);
#endif
	resizing = true;
	need_backend_resize = false;
	break;
      case WM_EXITSIZEMOVE:
	EnableSizeTip(false);
	resizing = false;
#ifdef RDB_DEBUG_PATCH
	debug("WM_EXITSIZEMOVE\n");
#endif
	if (need_backend_resize) {
	    term_size(term, conf_get_int(conf, CONF_height),
		      conf_get_int(conf, CONF_width),
		      conf_get_int(conf, CONF_savelines));
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
	if( GetBackgroundImageFlag() && (conf_get_int(conf,CONF_bg_image_abs_fixed)==1) && (conf_get_int(conf,CONF_bg_type)!=0) ) RefreshBackground( hwnd ) ;
#endif
	    InvalidateRect(hwnd, NULL, true);
	}
        recompute_window_offset();
	break;
      case WM_SIZING:
	/*
	 * This does two jobs:
	 * 1) Keep the sizetip uptodate
	 * 2) Make sure the window size is _stepped_ in units of the font size.
	 */
	resize_action = conf_get_int(conf, CONF_resize_action);
	if (resize_action == RESIZE_TERM ||
            (resize_action == RESIZE_EITHER && !is_alt_pressed())) {
	    int width, height, w, h, ew, eh;
	    LPRECT r = (LPRECT) lParam;

	    if (!need_backend_resize && resize_action == RESIZE_EITHER &&
		(conf_get_int(conf, CONF_height) != term->rows ||
		 conf_get_int(conf, CONF_width) != term->cols)) {
		/* 
		 * Great! It seems that both the terminal size and the
		 * font size have been changed and the user is now dragging.
		 * 
		 * It will now be difficult to get back to the configured
		 * font size!
		 *
		 * This would be easier but it seems to be too confusing.
		 */
	        conf_set_int(conf, CONF_height, term->rows);
	        conf_set_int(conf, CONF_width, term->cols);

		InvalidateRect(hwnd, NULL, true);
		need_backend_resize = true;
	    }

	    width = r->right - r->left - extra_width;
	    height = r->bottom - r->top - extra_height;
	    w = (width + font_width / 2) / font_width;
	    if (w < 1)
		w = 1;
	    h = (height + font_height / 2) / font_height;
	    if (h < 1)
		h = 1;
	    UpdateSizeTip(hwnd, w, h);
	    ew = width - w * font_width;
	    eh = height - h * font_height;
	    if (ew != 0) {
		if (wParam == WMSZ_LEFT ||
		    wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT)
		    r->left += ew;
		else
		    r->right -= ew;
	    }
	    if (eh != 0) {
		if (wParam == WMSZ_TOP ||
		    wParam == WMSZ_TOPRIGHT || wParam == WMSZ_TOPLEFT)
		    r->top += eh;
		else
		    r->bottom -= eh;
	    }
	    if (ew || eh)
		return 1;
	    else
		return 0;
	} else {
	    int width, height, w, h, rv = 0;
	    int window_border = conf_get_int(conf, CONF_window_border);
	    int ex_width = extra_width + (window_border - offset_width) * 2;
	    int ex_height = extra_height + (window_border - offset_height) * 2;
	    LPRECT r = (LPRECT) lParam;

	    width = r->right - r->left - ex_width;
	    height = r->bottom - r->top - ex_height;

	    w = (width + term->cols/2)/term->cols;
	    h = (height + term->rows/2)/term->rows;
	    if ( r->right != r->left + w*term->cols + ex_width)
		rv = 1;

	    if (wParam == WMSZ_LEFT ||
		wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT)
		r->left = r->right - w*term->cols - ex_width;
	    else
		r->right = r->left + w*term->cols + ex_width;

	    if (r->bottom != r->top + h*term->rows + ex_height)
		rv = 1;

	    if (wParam == WMSZ_TOP ||
		wParam == WMSZ_TOPRIGHT || wParam == WMSZ_TOPLEFT)
		r->top = r->bottom - h*term->rows - ex_height;
	    else
		r->bottom = r->top + h*term->rows + ex_height;

	    return rv;
	}
	/* break;  (never reached) */
      case WM_FULLSCR_ON_MAX:
	fullscr_on_max = true;
	break;
#ifdef MOD_PERSO
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
	case WM_DISPLAYCHANGE:
		if( (!GetBackgroundImageFlag()) || GetPuttyFlag() ) return DefWindowProc(hwnd, message, wParam, lParam) ;
	case WM_MOVE:
	      if( (!GetBackgroundImageFlag()) || GetPuttyFlag() ) {
		sys_cursor_update();
		if( conf_get_bool(conf,CONF_saveonexit) ) GetWindowCoord( hwnd ) ;
		break;
		}
    if(backgrounddc)
    {
        // When using a background image based on the desktop, correct display
        // may depend on the current position of the window.
        InvalidateRect(hwnd, NULL, true);
    }
	sys_cursor_update();
	
	if( conf_get_bool(conf,CONF_saveonexit) ) GetWindowCoord( hwnd ) ;

	break;


	case WM_SETTINGCHANGE:
    // It's sometimes hard to tell what setting changed, but our decisions
    // regarding background drawing depends on some system settings, so force
    // it to be redone.

    if(textdc)
    {
        DeleteObject(textbm);
        DeleteDC(textdc);
        textdc = NULL;
        textbm = NULL;
    }

    if(backgrounddc)
    {
        DeleteObject(backgroundbm);
        DeleteDC(backgrounddc);
        backgrounddc = NULL;
        backgroundbm = NULL;
    }

    if(backgroundblenddc)
    {
        DeleteObject(backgroundblendbm);
        DeleteDC(backgroundblenddc);
        backgroundblenddc = NULL;
        backgroundblendbm = NULL;
    }

    if(textdc || backgrounddc || backgroundblenddc)
    {
        InvalidateRect(hwnd, NULL, true );
    }

    break;
#else
      case WM_MOVE:
	sys_cursor_update();
	if( conf_get_bool(conf,CONF_saveonexit) ) GetWindowCoord( hwnd ) ;
	break;
#endif
#else
      case WM_MOVE:
        term_notify_window_pos(term, LOWORD(lParam), HIWORD(lParam));
	sys_cursor_update();
	break;
#endif
      case WM_SIZE:
	resize_action = conf_get_int(conf, CONF_resize_action);
#ifdef RDB_DEBUG_PATCH
	debug("WM_SIZE %s (%d,%d)\n",
              (wParam == SIZE_MINIMIZED) ? "SIZE_MINIMIZED":
              (wParam == SIZE_MAXIMIZED) ? "SIZE_MAXIMIZED":
              (wParam == SIZE_RESTORED && resizing) ? "to":
              (wParam == SIZE_RESTORED) ? "SIZE_RESTORED":
              "...",
              LOWORD(lParam), HIWORD(lParam));
#endif
              term_notify_minimised(term, wParam == SIZE_MINIMIZED);
        {
            /*
             * WM_SIZE's lParam tells us the size of the client area.
             * But historic PuTTY practice is that we want to tell the
             * terminal the size of the overall window.
             */
            RECT r;
            GetWindowRect(hwnd, &r);
            term_notify_window_size_pixels(
                term, r.right - r.left, r.bottom - r.top);
        }

#ifdef MOD_PERSO
	if (wParam == SIZE_MINIMIZED) {
		if( GetKeyState( VK_CONTROL ) & 0x8000 ) {
			SendMessage( hwnd, WM_COMMAND, IDM_TOTRAY, 0 ) ; return 0 ;
		}
		else SetWindowText(hwnd, conf_get_bool(conf,CONF_win_name_always) ? window_name : icon_name);
	}
#else
	if (wParam == SIZE_MINIMIZED)
	    SetWindowText(hwnd,
			  conf_get_bool(conf, CONF_win_name_always) ?
			  window_name : icon_name);
#endif
	if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
	    SetWindowText(hwnd, window_name);
        if (wParam == SIZE_RESTORED) {
            processed_resize = false;
            clear_full_screen();
            if (processed_resize) {
                /*
                 * Inhibit normal processing of this WM_SIZE; a
                 * secondary one was triggered just now by
                 * clear_full_screen which contained the correct
                 * client area size.
                 */
                return 0;
            }
        }
        if (wParam == SIZE_MAXIMIZED && fullscr_on_max) {
            fullscr_on_max = false;
            processed_resize = false;
            make_full_screen();
            if (processed_resize) {
                /*
                 * Inhibit normal processing of this WM_SIZE; a
                 * secondary one was triggered just now by
                 * make_full_screen which contained the correct client
                 * area size.
                 */
                return 0;
            }
        }

        processed_resize = true;

	if (resize_action == RESIZE_DISABLED) {
	    /* A resize, well it better be a minimize. */
	    reset_window(-1);
	} else {
            if (wParam == SIZE_MAXIMIZED) {
                was_zoomed = true;
                prev_rows = term->rows;
                prev_cols = term->cols;
                if (resize_action == RESIZE_TERM)
                    wm_size_resize_term(lParam, false);
                reset_window(0);
            } else if (wParam == SIZE_RESTORED && was_zoomed) {
                was_zoomed = false;
                if (resize_action == RESIZE_TERM) {
                    wm_size_resize_term(lParam, true);
                    reset_window(2);
                } else if (resize_action != RESIZE_FONT)
                    reset_window(2);
                else
                    reset_window(0);
            } else if (wParam == SIZE_MINIMIZED) {
                /* do nothing */
	    } else if (resize_action == RESIZE_TERM ||
                       (resize_action == RESIZE_EITHER &&
                        !is_alt_pressed())) {
                wm_size_resize_term(lParam, true);

                /*
                 * Sometimes, we can get a spontaneous resize event
                 * outside a WM_SIZING interactive drag which wants to
                 * set us to a new specific SIZE_RESTORED size. An
                 * example is what happens if you press Windows+Right
                 * and then Windows+Up: the first operation fits the
                 * window to the right-hand half of the screen, and
                 * the second one changes that for the top right
                 * quadrant. In that situation, if we've responded
                 * here by resizing the terminal, we may still need to
                 * recompute the border around the window and do a
                 * full redraw to clear the new border.
                 */
                if (!resizing)
                    recompute_window_offset();
#ifdef MOD_PERSO
		else
		    SetWinHeight( -1 ) ;
#endif
            } else {
                reset_window(0);
	    }
	}
#ifdef MOD_PERSO
	if( GetTitleBarFlag() )  wintw_set_title( NULL, conf_get_str(conf,CONF_wintitle) ) ;		// Pour refaire la barre de titre si option SizeFlag
	if( conf_get_bool(conf,CONF_saveonexit) ) GetWindowCoord( hwnd ) ;
#endif
#ifdef MOD_BACKGROUNDIMAGE
	/*
	if( ((wParam == SIZE_RESTORED)&&(resizing!=TRUE)) || (wParam == SIZE_MAXIMIZED) ) {
		if( GetBackgroundImageFlag() && (conf_get_int(conf,CONF_bg_image_abs_fixed)==1) && (conf_get_int(conf,CONF_bg_type)!=0) ) RefreshBackground( hwnd ) ;
	}*/
	if( (wParam == SIZE_RESTORED) || (wParam == SIZE_MAXIMIZED) ) {
		RefreshBackground( hwnd ) ;
	}
#endif
	sys_cursor_update();
        return 0;
      case WM_DPICHANGED:
        dpi_info.cur_dpi.x = LOWORD(wParam);
        dpi_info.cur_dpi.y = HIWORD(wParam);
        dpi_info.new_wnd_rect = *(RECT*)(lParam);
        reset_window(3);
	return 0;
      case WM_VSCROLL:
	switch (LOWORD(wParam)) {
	  case SB_BOTTOM:
	    term_scroll(term, -1, 0);
	    break;
	  case SB_TOP:
	    term_scroll(term, +1, 0);
	    break;
	  case SB_LINEDOWN:
	    term_scroll(term, 0, +1);
	    break;
	  case SB_LINEUP:
	    term_scroll(term, 0, -1);
	    break;
	  case SB_PAGEDOWN:
	    term_scroll(term, 0, +term->rows / 2);
	    break;
	  case SB_PAGEUP:
	    term_scroll(term, 0, -term->rows / 2);
	    break;
	  case SB_THUMBPOSITION:
          case SB_THUMBTRACK: {
	    /*
	     * Use GetScrollInfo instead of HIWORD(wParam) to get
	     * 32-bit scroll position.
	     */
		SCROLLINFO si;

		si.cbSize = sizeof(si);
		si.fMask = SIF_TRACKPOS;
		if (GetScrollInfo(hwnd, SB_VERT, &si) == 0)
		    si.nTrackPos = HIWORD(wParam);
		term_scroll(term, 1, si.nTrackPos);
             break;
          }
        }

	if (in_scrollbar_loop) {
            /*
             * Allow window updates to happen during interactive
             * scroll.
             *
             * When the user takes hold of our window's scrollbar and
             * wobbles it interactively back and forth, or presses on
             * one of the arrow buttons at the ends, the first thing
             * that happens is that this window procedure receives
             * WM_SYSCOMMAND / SC_VSCROLL. [1] The default handler for
             * that window message starts a subsidiary message loop,
             * which continues to run until the user lets go of the
             * scrollbar again. All WM_VSCROLL / SB_THUMBTRACK
             * messages are generated by the handlers within that
             * subsidiary message loop.
             *
             * So, during that time, _our_ message loop is not
             * running, which means toplevel callbacks and timers and
             * so forth are not happening, which means that when we
             * redraw the window and set a timer to clear the cooldown
             * flag 20ms later, that timer never fires, and we aren't
             * able to keep redrawing the window.
             *
             * The 'obvious' answer would be to seize that SYSCOMMAND
             * ourselves and inhibit the default handler, so that our
             * message loop carries on running. But that would mean
             * we'd have to reimplement the whole of the scrollbar
             * handler!
             *
             * So instead we apply a bodge: set a static variable that
             * indicates that we're _in_ that sub-loop, and if so,
             * decide it's OK to manually call term_update() proper,
             * bypassing the timer and cooldown and rate-limiting
             * systems completely, whenever we see an SB_THUMBTRACK.
             * This shouldn't cause a rate overload, because we're
             * only doing it once per UI event!
             *
             * [1] Actually, there's an extra oddity where SC_HSCROLL
             * and SC_VSCROLL have their documented values the wrong
             * way round. Many people on the Internet have noticed
             * this, e.g. https://stackoverflow.com/q/55528397
             */
                term_update(term);
	}
	break;
      case WM_PALETTECHANGED:
	if ((HWND) wParam != hwnd && pal != NULL) {
	    HDC hdc = make_hdc();
	    if (hdc) {
		if (RealizePalette(hdc) > 0)
		    UpdateColors(hdc);
		free_hdc(hdc);
	    }
	}
	break;
      case WM_QUERYNEWPALETTE:
	if (pal != NULL) {
	    HDC hdc = make_hdc();
	    if (hdc) {
		if (RealizePalette(hdc) > 0)
		    UpdateColors(hdc);
		free_hdc(hdc);
		return true;
	    }
	}
	return false;
#ifdef MOD_HYPERLINK
	/*
	 * HACK: PuttyTray / Nutty
	 * Hyperlink stuff: Change cursor if we are in ctrl+click link mode
	 *
	 * WARNING: Spans over multiple CASEs
	 */
	case WM_KEYDOWN:
		if( GetPuttyFlag() || !GetHyperlinkFlag() ) goto KEY_END;
		if(wParam == VK_CONTROL && conf_get_int(term->conf,CONF_url_ctrl_click)) {
			GetCursorPos(&cursor_pt);
			ScreenToClient(hwnd, &cursor_pt);

			if (urlhack_is_in_link_region(TO_CHR_X(cursor_pt.x), TO_CHR_Y(cursor_pt.y))) {
				SetCursor(LoadCursor(NULL, IDC_HAND));
				term_update(term);
			}
			goto KEY_END;
		}	
	case WM_KEYUP:
		if( GetPuttyFlag() || !GetHyperlinkFlag() ) goto KEY_END;
		if (wParam == VK_CONTROL && conf_get_int(term->conf,CONF_url_ctrl_click)) {
			SetCursor(LoadCursor(NULL, IDC_IBEAM));
			term_update(term);
			goto KEY_END;
		}
	KEY_END:
	case WM_SYSKEYUP:
	case WM_SYSKEYDOWN:
	/* HACK: PuttyTray / Nutty : END */
#else
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
      case WM_KEYUP:
      case WM_SYSKEYUP:
#endif
#ifdef MOD_RECONNECT
	//if( !back && GetAutoreconnectFlag() && is_backend_first_connected && (WM_COMMAND==WM_KEYDOWN) && !(GetKeyState(VK_CONTROL)&0x8000) && !(GetKeyState(VK_SHIFT)&0x8000) && !(GetKeyState(VK_MENU)&0x8000) && (wParam!=VK_TAB) && (wParam!=VK_LEFT) && (wParam!=VK_UP) && (wParam!=VK_RIGHT) && (wParam!=VK_DOWN) && !((wParam>=VK_F1)&&(wParam<=VK_F16)) ) { 
        if( (!backend || !is_backend_connected) && (message==WM_KEYDOWN) && GetAutoreconnectFlag() && is_backend_first_connected && (wParam!=VK_CONTROL) && (wParam!=VK_SHIFT) && (wParam!=VK_MENU) && (wParam!=VK_TAB) && (wParam!=VK_LEFT) && (wParam!=VK_UP) && (wParam!=VK_RIGHT) && (wParam!=VK_DOWN) && !((wParam>=VK_F1)&&(wParam<=VK_F16)) ) { 
 		lp_eventlog(&wgs.logpolicy, "No connection on key pressed, trying to reconnect...") ; 
		PostMessage( hwnd, WM_COMMAND, IDM_RESTART, 0 ) ;  
		break ;
	}
#endif
#ifdef MOD_PERSO
	if( (wParam == VK_TAB) && (GetKeyState(VK_CONTROL) & 0x8000) ) {					// CTRL + TAB to switch between windows
		if (conf_get_int(conf, CONF_ctrl_tab_switch) && GetCtrlTabFlag()) {
			if( message==WM_KEYUP ) {
				struct ctrl_tab_info info = { (GetKeyState(VK_SHIFT) & 0x8000) ? 1 : -1, hwnd, } ;

				info.next_hi_date_time = info.self_hi_date_time = GetWindowLong(hwnd, 0);
				info.next_lo_date_time = info.self_lo_date_time = GetWindowLong(hwnd, 4);
				EnumWindows(CtrlTabWindowProc, (LPARAM) &info);
				if (info.next != NULL) 
					if( info.next != hwnd )
						SetForegroundWindow(info.next);
			return 0;
			}
			return DefWindowProc(hwnd, message, wParam, lParam);
		}
	}

		AntiIdleCount = 0 ;
      
		/* Permet de sauvegarder tous les caracteres tapes au clavier pour les avoir dans le /savedump */
#ifdef MOD_SAVEDUMP
		if( debug_flag ) addkeypressed( message, wParam, lParam, GetKeyState(VK_SHIFT)&0x8000, GetKeyState(VK_CONTROL)&0x8000, (GetKeyState(VK_MENU)&0x8000)||(GetKeyState(VK_LMENU)&0x8000),GetKeyState(VK_RMENU)&0x8000, (GetKeyState(VK_RWIN)&0x8000)||(GetKeyState(VK_LWIN)&0x8000 ) );
#endif

#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
		if( (wParam==VK_SNAPSHOT)&&(GetKeyState(VK_CONTROL)&0x8000) ) {					// CTRL + PrintScreen => Screenshot
			char screenShotFile[1024] ;
			sprintf( screenShotFile, "%s\\screenshot-%d-%lld.jpg", InitialDirectory, getpid(), time(0) );
			screenCaptureClientRect( hwnd, screenShotFile, 100 ) ;
		}
#endif

/*
		if((wParam==VK_F1)&&(message==WM_KEYDOWN)&&(GetKeyState(VK_CONTROL)&0x8000)&&(GetKeyState(VK_SHIFT)&0x8000))  // CTRL+SHIFT+F1 => Manage shortcut flag
			{ SetShortcutsFlag( abs(GetShortcutsFlag()-1) ) ; return 0 ; }
*/
		if( GetShortcutsFlag() ) { 
			if ( (message==WM_KEYDOWN)||(message==WM_SYSKEYDOWN) ) {

				if( ManageShortcuts(term, conf, hwnd, clips_system, wParam
					, GetKeyState(VK_SHIFT)&0x8000
					, GetKeyState(VK_CONTROL)&0x8000
					, (GetKeyState(VK_MENU)&0x8000)||(GetKeyState(VK_LMENU)&0x8000)
					, GetKeyState(VK_RMENU)&0x8000
					//, is_alt_pressed()
					, (GetKeyState(VK_RWIN)&0x8000)||(GetKeyState(VK_LWIN)&0x8000)
					) )  return 0 ;
			} 
		} else { 
			if( GetProtectFlag() == 1 ) return 0 ; 
		}

		// Majuscule uniquement
		if( GetCapsLockFlag() ) {
			if( ( wParam>='A' ) && ( wParam <= 'Z' ) 
					    && !(GetKeyState( VK_CAPITAL ) & 0x0001) 
					    && !(GetKeyState( VK_SHIFT ) & 0x8000) ) { 
				keybd_event( VK_CAPITAL, 0x45, KEYEVENTF_EXTENDEDKEY | 0, 0 ) ;
				keybd_event( VK_CAPITAL, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0) ;
				SendMessage(hwnd, WM_CHAR, wParam, 0) ;
				return 0 ;
			}
		}
/*
if( (GetKeyState(VK_MENU)&0x8000) && (wParam==VK_SPACE) ) {
	PopUpSystemMenu( hwnd, 0 ) ;
	return 0;
}
*/
#endif

	/*
	 * Add the scan code and keypress timing to the random
	 * number noise.
	 */
	noise_ultralight(NOISE_SOURCE_KEY, lParam);
#ifdef MOD_FAR2L
  /* far2l extensions support */
    if (term->far2l_ext) {

        // extensions mode enabled

        // far2l_ext keyboard input event structure
        WORD repeat;      // 2 bytes
        WORD vkc;         // 2 bytes
        WORD vsc;         // 2 bytes
        DWORD ctrl;       // 4 bytes
        DWORD uchar;      // 4 bytes
        CHAR type;        // 1 byte

        // set repeat, virtual keycode, virtual scancode
        repeat = LOWORD(lParam);
        vsc = HIWORD(lParam) & 0xFF;
        vkc = LOWORD(wParam);

        // this fixes far2l's "editor autocomplete" plugin behavior
        if ((vkc == VK_TAB) || (vkc == VK_BACK) || (vkc == VK_ESCAPE) || (vkc == VK_DELETE)) {
            vsc = 0;
        }

        // fixes strange alt+arrows behavior
        if ((vkc == VK_LEFT) || (vkc == VK_RIGHT) || (vkc == VK_UP) || (vkc == VK_DOWN)) {
            vsc = 0;
        }

        // set control keys state
        ctrl = 0;
        if (GetAsyncKeyState(VK_LCONTROL)) { ctrl |= LEFT_CTRL_PRESSED; }
        if (GetAsyncKeyState(VK_RCONTROL)) { ctrl |= RIGHT_CTRL_PRESSED; }
        if (GetAsyncKeyState(VK_LMENU)) { ctrl |= LEFT_ALT_PRESSED; }
        if (GetAsyncKeyState(VK_RMENU)) { ctrl |= RIGHT_ALT_PRESSED; }
        if (GetAsyncKeyState(VK_SHIFT)) { ctrl |= SHIFT_PRESSED; }
        // begin: reserved for future usage
        // Console WinAPI does not allow us to distinguish between left and right
        // shift keys. But PuTTY is not a console app, so why not to send
        // all information about control keys state that we actually have here?
        // Using bits not used by any other status for backward compatibility.
        #define RIGHT_SHIFT_PRESSED 0x1000
        #define LEFT_SHIFT_PRESSED 0x2000
        if (GetAsyncKeyState(VK_LSHIFT)) { ctrl |= RIGHT_SHIFT_PRESSED; }
        if (GetAsyncKeyState(VK_RSHIFT)) { ctrl |= LEFT_SHIFT_PRESSED; }
        // end
        if ((lParam & ( 1 << 24 )) >> 24) { ctrl |= ENHANCED_KEY; }
        if ((((u_short)GetKeyState(VK_NUMLOCK)) & 0xffff) != 0) { ctrl |= NUMLOCK_ON; }
        if ((((u_short)GetKeyState(VK_SCROLL)) & 0xffff) != 0) { ctrl |= SCROLLLOCK_ON; }
        if ((((u_short)GetKeyState(VK_CAPITAL)) & 0xffff) != 0) { ctrl |= CAPSLOCK_ON; }

        // set unicode character
        BYTE kb[256];
        GetKeyboardState(kb);
        WCHAR uc[5] = {};
        ToUnicode(wParam, MapVirtualKey(wParam, MAPVK_VK_TO_VSC), kb, uc, 4, 0);
        // todo: check result
        //int result = ToUnicode(wParam, MapVirtualKey(wParam, MAPVK_VK_TO_VSC), kb, uc, 4, 0);
        uchar = uc[0];

        // set event type
        if ((message == WM_KEYDOWN) || (message == WM_SYSKEYDOWN)) {
            type = 'K';
        } else {
            type = 'k';
        }

        char* kev = malloc(15); // keyboard event structure length
        memcpy(kev, &repeat, sizeof(repeat));
        memcpy(kev + 2, &vkc, sizeof(vkc));
        memcpy(kev + 4, &vsc, sizeof(vsc));
        memcpy(kev + 6, &ctrl, sizeof(ctrl));
        memcpy(kev + 10, &uchar, sizeof(uchar));
        memcpy(kev + 14, &type, sizeof(type));

        // base64-encode kev
        // result in null-terminated char* out
        base64_encodestate _state;
        base64_init_encodestate(&_state);
        char* out = malloc(15*2);
        int count = base64_encode_block(kev, 15, out, &_state);
#ifdef MOD_PERSO
        count += base64_encode_blockend(out + count, &_state);
#else
        // finishing '=' characters
        char* next_char = out + count;
        switch (_state.step)
        {
            case step_B:
                *next_char++ = base64_encode_value(_state.result);
                *next_char++ = '=';
                *next_char++ = '=';
                break;
            case step_C:
                *next_char++ = base64_encode_value(_state.result);
                *next_char++ = '=';
                break;
            case step_A:
                break;
        }
        count = next_char - out;
        out[count] = 0;
#endif

        // send escape seq

        char* str = "\x1b_f2l";
        backend_send(backend, str, strlen(str));

        backend_send(backend, out, count);

        char* str2 = "\x07";
        backend_send(backend, str2, strlen(str2));

        // don't forget to free memory :)
        free(out);

        // we should not do any other key processing in this mode
        return 0;
    }


/*
 * We don't do TranslateMessage since it disassociates the
 * resulting CHAR message from the KEYDOWN that sparked it,
 * which we occasionally don't want. Instead, we process
 * KEYDOWN, and call the Win32 translator functions so that
 * we get the translations under _our_ control.
 */
#else
	/*
	 * We don't do TranslateMessage since it disassociates the
	 * resulting CHAR message from the KEYDOWN that sparked it,
	 * which we occasionally don't want. Instead, we process
	 * KEYDOWN, and call the Win32 translator functions so that
	 * we get the translations under _our_ control.
	 */
#endif
	{
	    unsigned char buf[20];
	    int len;

	    if (wParam == VK_PROCESSKEY || /* IME PROCESS key */
                wParam == VK_PACKET) {     /* 'this key is a Unicode char' */
		if (message == WM_KEYDOWN) {
		    MSG m;
		    m.hwnd = hwnd;
		    m.message = WM_KEYDOWN;
		    m.wParam = wParam;
		    m.lParam = lParam & 0xdfff;
		    TranslateMessage(&m);
		} else break; /* pass to Windows for default processing */
	    } else {
		len = TranslateKey(message, wParam, lParam, buf);
		if (len == -1)
		    return DefWindowProcW(hwnd, message, wParam, lParam);

		if (len != 0) {
		    /*
		     * We need not bother about stdin backlogs
		     * here, because in GUI PuTTY we can't do
		     * anything about it anyway; there's no means
		     * of asking Windows to hold off on KEYDOWN
		     * messages. We _have_ to buffer everything
		     * we're sent.
		     */
                    term_keyinput(term, -1, buf, len);
		    show_mouseptr(false);
		}
	    }
	}
	return 0;
      case WM_INPUTLANGCHANGE:
	/* wParam == Font number */
	/* lParam == Locale */
	set_input_locale((HKL)lParam);
	sys_cursor_update();
	break;
      case WM_IME_STARTCOMPOSITION: {
	    HIMC hImc = ImmGetContext(hwnd);
	    ImmSetCompositionFont(hImc, &lfont);
	    ImmReleaseContext(hwnd, hImc);
	break;
      }
      case WM_IME_COMPOSITION: {
	    HIMC hIMC;
	    int n;
	    char *buff;

	    if (osPlatformId == VER_PLATFORM_WIN32_WINDOWS || 
	        osPlatformId == VER_PLATFORM_WIN32s)
                break; /* no Unicode */

	    if ((lParam & GCS_RESULTSTR) == 0) /* Composition unfinished. */
		break; /* fall back to DefWindowProc */

	    hIMC = ImmGetContext(hwnd);
	    n = ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, NULL, 0);

	    if (n > 0) {
		int i;
		buff = snewn(n, char);
		ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, buff, n);
		/*
		 * Jaeyoun Chung reports that Korean character
		 * input doesn't work correctly if we do a single
		 * term_keyinputw covering the whole of buff. So
		 * instead we send the characters one by one.
		 */
		/* don't divide SURROGATE PAIR */
		if (ldisc) {
                    for (i = 0; i < n; i += 2) {
			WCHAR hs = *(unsigned short *)(buff+i);
			if (IS_HIGH_SURROGATE(hs) && i+2 < n) {
			    WCHAR ls = *(unsigned short *)(buff+i+2);
			    if (IS_LOW_SURROGATE(ls)) {
                                term_keyinputw(
                                    term, (unsigned short *)(buff+i), 2);
				i += 2;
				continue;
			    }
			}
                        term_keyinputw(
                            term, (unsigned short *)(buff+i), 1);
                    }
		}
		free(buff);
	    }
	    ImmReleaseContext(hwnd, hIMC);
	    return 1;
	}

      case WM_IME_CHAR:
	if (wParam & 0xFF00) {
	    char buf[2];

	    buf[1] = wParam;
	    buf[0] = wParam >> 8;
            term_keyinput(term, kbd_codepage, buf, 2);
	} else {
	    char c = (unsigned char) wParam;
	    term_seen_key_event(term);
            term_keyinput(term, kbd_codepage, &c, 1);
	}
	return (0);
      case WM_CHAR:
      case WM_SYSCHAR:
	/*
	 * Nevertheless, we are prepared to deal with WM_CHAR
	 * messages, should they crop up. So if someone wants to
	 * post the things to us as part of a macro manoeuvre,
	 * we're ready to cope.
	 */
	{
            static wchar_t pending_surrogate = 0;
	    wchar_t c = wParam;

            if (IS_HIGH_SURROGATE(c)) {
                pending_surrogate = c;
            } else if (IS_SURROGATE_PAIR(pending_surrogate, c)) {
                wchar_t pair[2];
                pair[0] = pending_surrogate;
                pair[1] = c;
                term_keyinputw(term, pair, 2);
            } else if (!IS_SURROGATE(c)) {
                term_keyinputw(term, &c, 1);
            }
	}
	return 0;
      case WM_SYSCOLORCHANGE:
	if (conf_get_bool(conf, CONF_system_colour)) {
	    /* Refresh palette from system colours. */
            term_notify_palette_changed(term);
	    init_palette();
	    /* Force a repaint of the terminal window. */
	    term_invalidate(term);
	}
	break;
      case WM_GOT_CLIPDATA:
	process_clipdata((HGLOBAL)lParam, wParam);
	return 0;
#ifdef MOD_RECONNECT
      case WM_POWERBROADCAST:
	if( GetAutoreconnectFlag() && conf_get_int(conf,CONF_wakeup_reconnect) && is_backend_first_connected) {
		switch(wParam) {
			case PBT_APMRESUMESUSPEND:
			case PBT_APMRESUMEAUTOMATIC:
			case PBT_APMRESUMECRITICAL:
			case PBT_APMQUERYSUSPENDFAILED:
				if(session_closed && !backend) {
					/*
					time_t tnow = time(NULL);
					
					if(last_reconnect && ((tnow - last_reconnect) < GetReconnectDelay()) ) {
						lp_eventlog(default_logpolicy, "Woken up from suspend, waiting for delay..." );
						Sleep(GetReconnectDelay()*1000);
					}
					last_reconnect = tnow;
					lp_eventlog(default_logpolicy, "Woken up from suspend, reconnecting...");
					term_pwron(term, FALSE);
					is_backend_connected = 0 ;
					start_backend();
					SetTimer(hwnd, TIMER_INIT, init_delay, NULL) ;
					*/
					lp_eventlog(&wgs.logpolicy, "Unable to connect on wakeup, trying to reconnect...") ; 
					SetTimer(hwnd, TIMER_RECONNECT, GetReconnectDelay()*1000, NULL) ;
				}
				break;
			case PBT_APMSUSPEND:
				if(!session_closed && backend) {
					lp_eventlog(&wgs.logpolicy, "Suspend detected, disconnecting cleanly...");
					queue_toplevel_callback(close_session, NULL); // close_session();
					session_closed = true;
				}
				break;
		}
	}
	break;	
#endif

      default:
	if (message == wm_mousewheel || message == WM_MOUSEWHEEL) {
	    bool shift_pressed = false, control_pressed = false;

	    if (message == WM_MOUSEWHEEL) {
		wheel_accumulator += (short)HIWORD(wParam);
		shift_pressed=LOWORD(wParam) & MK_SHIFT;
		control_pressed=LOWORD(wParam) & MK_CONTROL;
	    } else {
		BYTE keys[256];
		wheel_accumulator += (int)wParam;
		if (GetKeyboardState(keys)!=0) {
		    shift_pressed=keys[VK_SHIFT]&0x80;
		    control_pressed=keys[VK_CONTROL]&0x80;
		}
	    }

	    /* process events when the threshold is reached */
	    while (abs(wheel_accumulator) >= WHEEL_DELTA) {
		int b;

		/* reduce amount for next time */
		if (wheel_accumulator > 0) {
		    b = MBT_WHEEL_UP;
		    wheel_accumulator -= WHEEL_DELTA;
		} else if (wheel_accumulator < 0) {
		    b = MBT_WHEEL_DOWN;
		    wheel_accumulator += WHEEL_DELTA;
		} else
		    break;

		if (send_raw_mouse &&
		    !(conf_get_bool(conf, CONF_mouse_override) &&
                      shift_pressed)) {
		    /* Mouse wheel position is in screen coordinates for
		     * some reason */
		    POINT p;
		    p.x = X_POS(lParam); p.y = Y_POS(lParam);
		    if (ScreenToClient(hwnd, &p)) {
			/* send a mouse-down followed by a mouse up */
			term_mouse(term, b, translate_button(b),
				   MA_CLICK,
				   TO_CHR_X(p.x),
				   TO_CHR_Y(p.y), shift_pressed,
				   control_pressed, is_alt_pressed());
		    } /* else: not sure when this can fail */
#ifdef MOD_PERSO
		    } else if ( control_pressed && shift_pressed ) {
		    /*
		    // region tray-zoom
			conf_get_fontspec(conf, CONF_font)->height += MBT_WHEEL_UP == b ? 1 : -1;
			// short version of IDM_RECONF's reconfig:
			term_size(term,
				conf_get_int(conf, CONF_height),
				conf_get_int(conf, CONF_width),
				conf_get_int(conf, CONF_savelines));
			reset_window(2);
		    // endregion
		    */
		    } else if (control_pressed) {
			ChangeFontSize(term,conf,hwnd, MBT_WHEEL_UP == b ? 1 : -1);
		} else {
				/*
				 * PuttyFeatures: Scroll Lines
				 */
				// if the wheel accumulator has multiples of the wheel delta, deal with it in one scroll call
				// this will fix the laggy scrolling in original PuTTY.
				int multiple = 1;
				if (abs(wheel_accumulator) >= WHEEL_DELTA) {
					multiple = abs(wheel_accumulator) / WHEEL_DELTA;

					if (wheel_accumulator > 0) {
						wheel_accumulator -= multiple * WHEEL_DELTA;
					} else {
						wheel_accumulator += multiple * WHEEL_DELTA;
					}

					// We started with a 1x already. Accounting for that here.
					multiple++;
					// to make scrolling more smooth, square the multiple value so that fast wheels scrolls more
					multiple *= multiple;
				}
				// determine how much to scroll.
				// -1 = half screen, -2 = full screen, and any positive value is the number of lines to scroll
				int conf_scroll = conf_get_int(conf, CONF_scrolllines);
				int scrollLines = conf_scroll == -1 ? term->rows / 2
					: conf_scroll == -2 ? term->rows
					: conf_scroll < -2 ? 3
					: conf_scroll;
				// account for the multiples from the wheel accumulator
				scrollLines *= multiple;

		    /* trigger a scroll */
		    term_scroll(term, 0,
				b == MBT_WHEEL_UP ?
				-scrollLines : scrollLines);
#else
		} else {
		    /* trigger a scroll */
		    term_scroll(term, 0,
				b == MBT_WHEEL_UP ?
				-term->rows / 2 : term->rows / 2);
#endif
		}
	    }
	    return 0;
	}
    }

    /*
     * Any messages we don't process completely above are passed through to
     * DefWindowProc() for default processing.
     */
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

/*
 * Move the system caret. (We maintain one, even though it's
 * invisible, for the benefit of blind people: apparently some
 * helper software tracks the system caret, so we should arrange to
 * have one.)
 */
static void wintw_set_cursor_pos(TermWin *tw, int x, int y)
{
    int cx, cy;

    if (!term->has_focus) return;

    /*
     * Avoid gratuitously re-updating the cursor position and IMM
     * window if there's no actual change required.
     */
    cx = x * font_width + offset_width;
    cy = y * font_height + offset_height;
    if (cx == caret_x && cy == caret_y)
	return;
    caret_x = cx;
    caret_y = cy;

    sys_cursor_update();
}

static void sys_cursor_update(void)
{
    COMPOSITIONFORM cf;
    HIMC hIMC;

    if (!term->has_focus) return;

    if (caret_x < 0 || caret_y < 0)
	return;

    SetCaretPos(caret_x, caret_y);

    /* IMM calls on Win98 and beyond only */
    if (osPlatformId == VER_PLATFORM_WIN32s) return; /* 3.11 */
    
    if (osPlatformId == VER_PLATFORM_WIN32_WINDOWS &&
        osMinorVersion == 0) return; /* 95 */

    /* we should have the IMM functions */
    hIMC = ImmGetContext(wgs.term_hwnd);
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = caret_x;
    cf.ptCurrentPos.y = caret_y;
    ImmSetCompositionWindow(hIMC, &cf);

    ImmReleaseContext(wgs.term_hwnd, hIMC);
}

static void draw_horizontal_line_on_text(int y, int lattr, RECT line_box,
                                         COLORREF colour)
{
    if (lattr == LATTR_TOP || lattr == LATTR_BOT) {
        y *= 2;
        if (lattr == LATTR_BOT)
            y -= font_height;
    }

    if (!(0 <= y && y < font_height))
        return;

    HPEN oldpen = SelectObject(wintw_hdc, CreatePen(PS_SOLID, 0, colour));
    MoveToEx(wintw_hdc, line_box.left, line_box.top + y, NULL);
    LineTo(wintw_hdc, line_box.right, line_box.top + y);
    oldpen = SelectObject(wintw_hdc, oldpen);
    DeleteObject(oldpen);
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
static void do_text_internal(
    int x, int y, wchar_t *text, int len,
    unsigned long attr, int lattr, truecolour truecolour)
{
    COLORREF fg, bg, t;
    int nfg, nbg, nfont;
    RECT line_box;
    bool force_manual_underline = false;
    int fnt_width, char_width;
    int text_adjust = 0;
    int xoffset = 0;
    int maxlen, remaining;
    bool opaque;
    bool is_cursor = false;
    static int *lpDx = NULL;
    static size_t lpDx_len = 0;
    int *lpDx_maybe;
    int len2; /* for SURROGATE PAIR */
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
    int transBg = backgrounddc ? 1 : 0;
    UINT etoFlagOpaque = transBg ? 0 : ETO_OPAQUE;
#endif

    lattr &= LATTR_MODE;

    char_width = fnt_width = font_width * (1 + (lattr != LATTR_NORM));

    if (attr & ATTR_WIDE)
	char_width *= 2;

    /* Only want the left half of double width lines */
    if (lattr != LATTR_NORM && x*2 >= term->cols)
	return;

    x *= fnt_width;
    y *= font_height;
    x += offset_width;
    y += offset_height;

    if ((attr & TATTR_ACTCURS) && (cursor_type == 0 || term->big_cursor)) {
        truecolour.fg = truecolour.bg = optionalrgb_none;
	attr &= ~(ATTR_REVERSE|ATTR_BLINK|ATTR_COLOURS|ATTR_DIM);
	/* cursor fg and bg */
	attr |= (260 << ATTR_FGSHIFT) | (261 << ATTR_BGSHIFT);
        is_cursor = true;
    }
#ifdef MOD_TUTTYCOLOR
    if (!conf_get_int(conf,CONF_sel_colour) && (attr & ATTR_SELECTED)) {
	attr &= ~ATTR_SELECTED;
	attr |= ATTR_REVERSE;
    };
#endif

    nfont = 0;
    if (vtmode == VT_POORMAN && lattr != LATTR_NORM) {
	/* Assume a poorman font is borken in other ways too. */
	lattr = LATTR_WIDE;
    } else
	switch (lattr) {
	  case LATTR_NORM:
	    break;
	  case LATTR_WIDE:
	    nfont |= FONT_WIDE;
	    break;
	  default:
	    nfont |= FONT_WIDE + FONT_HIGH;
	    break;
	}
    if (attr & ATTR_NARROW)
	nfont |= FONT_NARROW;

#ifdef USES_VTLINE_HACK
    /* Special hack for the VT100 linedraw glyphs. */
    if (text[0] >= 0x23BA && text[0] <= 0x23BD) {
	switch ((unsigned char) (text[0])) {
	  case 0xBA:
	    text_adjust = -2 * font_height / 5;
	    break;
	  case 0xBB:
	    text_adjust = -1 * font_height / 5;
	    break;
	  case 0xBC:
	    text_adjust = font_height / 5;
	    break;
	  case 0xBD:
	    text_adjust = 2 * font_height / 5;
	    break;
	}
	if (lattr == LATTR_TOP || lattr == LATTR_BOT)
	    text_adjust *= 2;
	text[0] = ucsdata.unitab_xterm['q'];
	if (attr & ATTR_UNDER) {
	    attr &= ~ATTR_UNDER;
	    force_manual_underline = true;
	}
    }
#endif

    /* Anything left as an original character set is unprintable. */
    if (DIRECT_CHAR(text[0]) &&
        (len < 2 || !IS_SURROGATE_PAIR(text[0], text[1]))) {
	int i;
	for (i = 0; i < len; i++)
	    text[i] = 0xFFFD;
    }

    /* OEM CP */
    if ((text[0] & CSET_MASK) == CSET_OEMCP)
	nfont |= FONT_OEM;

    nfg = ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT);
    nbg = ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT);
    if (bold_font_mode == BOLD_FONT && (attr & ATTR_BOLD))
	nfont |= FONT_BOLD;
#ifdef MOD_PERSO
    if (attr & ATTR_ITALIC)
	nfont |= FONT_ITALIC;
#endif
    if (und_mode == UND_FONT && (attr & ATTR_UNDER))
	nfont |= FONT_UNDERLINE;
    another_font(nfont);
    if (!fonts[nfont]) {
	if (nfont & FONT_UNDERLINE)
	    force_manual_underline = true;
	/* Don't do the same for manual bold, it could be bad news. */

	nfont &= ~(FONT_BOLD | FONT_UNDERLINE);
    }
    another_font(nfont);
    if (!fonts[nfont])
	nfont = FONT_NORMAL;
    if (attr & ATTR_REVERSE) {
        struct optionalrgb trgb;

	t = nfg;
	nfg = nbg;
	nbg = t;

        trgb = truecolour.fg;
        truecolour.fg = truecolour.bg;
        truecolour.bg = trgb;
    }
    if (bold_colours && (attr & ATTR_BOLD) && !is_cursor) {
	if (nfg < 16) nfg |= 8;
	else if (nfg >= 256) nfg |= 1;
    }
    if (bold_colours && (attr & ATTR_BLINK)) {
	if (nbg < 16) nbg |= 8;
	else if (nbg >= 256) nbg |= 1;
    }
#ifdef MOD_TUTTYCOLOR
    /*
     * quick & dirty hack: underlined text has colour preference over bold & normal
     * another one: selected text have absolute preference over all other attributes
     */
    if (attr & ATTR_SELECTED) {
	nfg = 272;
	nbg = 273;
    } else {
	if (conf_get_int(conf,CONF_under_colour) && (nfont & FONT_UNDERLINE)) {
	    if (nfg < 7)
		nfg += 264;
	    else if (nfg > 7 && nfg < 16)
		nfg += 256;
	    else
		switch (nfg) {
		case 256:
		    nfg = 262;
		    break;
		case 257:
		    nfg = 262;
		    break;
		case 258:
		    nfg = 263;
		    break;
		case 259:
		    nfg = 263;
		};
	    if (nbg < 7)
		nbg += 264;
	    else if (nbg > 7 && nbg < 16)
		nbg += 256;
	    else
		switch (nbg) {
		case 256:
		    nbg = 262;
		    break;
		case 257:
		    nbg = 262;
		    break;
		case 258:
		    nbg = 263;
		    break;
		case 259:
		    nbg = 263;
		};
	};
    };
#endif
    if (!pal && truecolour.fg.enabled)
	fg = RGB(truecolour.fg.r, truecolour.fg.g, truecolour.fg.b);
    else
	fg = colours[nfg];

    if (!pal && truecolour.bg.enabled)
	bg = RGB(truecolour.bg.r, truecolour.bg.g, truecolour.bg.b);
    else
	bg = colours[nbg];

    if (!pal && (attr & ATTR_DIM)) {
        fg = RGB(GetRValue(fg) * 2 / 3,
                 GetGValue(fg) * 2 / 3,
                 GetBValue(fg) * 2 / 3);
    }
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
    HDC hdc = wintw_hdc ; 
    if( GetBackgroundImageFlag() && (!GetPuttyFlag()) ) {
	line_box.left = x;
	line_box.top = y;
	line_box.right = x + char_width * len;
	line_box.bottom = y + font_height;
    
	if(textdc) {
		int x = line_box.left;
		int y = line_box.top;
		int width = line_box.right - line_box.left;
		int height = line_box.bottom - line_box.top;

		POINT bgloc = { x, y };
		COLORREF backgroundcolor = colours[258]; // Default Background
        
		if(!bBgRelToTerm)
			ClientToScreen(wgs.term_hwnd, &bgloc);
        
		if(bg == backgroundcolor) {
		// Use fast screen fill for default background.
			BitBlt(textdc, x, y, width, height, backgroundblenddc, bgloc.x, bgloc.y, SRCCOPY);
		} else {
			BitBlt(textdc, x, y, width, height, backgrounddc, bgloc.x, bgloc.y, SRCCOPY);
			color_blend(textdc, x, y, width, height, bg, conf_get_int(conf,CONF_bg_opacity)) ;
		}
		hdc = textdc;
	}
	SelectObject(hdc, fonts[nfont]);
	SetTextColor(hdc, fg);
	SetBkColor(hdc, bg);
	if (transBg || attr & TATTR_COMBINING)
		SetBkMode(hdc, TRANSPARENT);
	else
		SetBkMode(hdc, OPAQUE);
    } else {
	SelectObject(hdc, fonts[nfont]);
	SetTextColor(hdc, fg);
	SetBkColor(hdc, bg);
	if (attr & TATTR_COMBINING)
		SetBkMode(hdc, TRANSPARENT);
	else
		SetBkMode(hdc, OPAQUE);
	line_box.left = x;
	line_box.top = y;
	line_box.right = x + char_width * len;
	line_box.bottom = y + font_height;
    }
#else
    SelectObject(wintw_hdc, fonts[nfont]);
    SetTextColor(wintw_hdc, fg);
    SetBkColor(wintw_hdc, bg);
    if (attr & TATTR_COMBINING)
	SetBkMode(wintw_hdc, TRANSPARENT);
    else
	SetBkMode(wintw_hdc, OPAQUE);
    line_box.left = x;
    line_box.top = y;
    line_box.right = x + char_width * len;
    line_box.bottom = y + font_height;
#endif
    /* adjust line_box.right for SURROGATE PAIR & VARIATION SELECTOR */
    {
	int i;
	int rc_width = 0;
	for (i = 0; i < len ; i++) {
	    if (i+1 < len && IS_HIGH_VARSEL(text[i], text[i+1])) {
		i++;
	    } else if (i+1 < len && IS_SURROGATE_PAIR(text[i], text[i+1])) {
		rc_width += char_width;
		i++;
	    } else if (IS_LOW_VARSEL(text[i])) {
		/* do nothing */
            } else {
		rc_width += char_width;
            }
	}
	line_box.right = line_box.left + rc_width;
    }

    /* Only want the left half of double width lines */
    if (line_box.right > font_width*term->cols+offset_width)
	line_box.right = font_width*term->cols+offset_width;

    if (font_varpitch) {
        /*
         * If we're using a variable-pitch font, we unconditionally
         * draw the glyphs one at a time and centre them in their
         * character cells (which means in particular that we must
         * disable the lpDx mechanism). This gives slightly odd but
         * generally reasonable results.
         */
        xoffset = char_width / 2;
        SetTextAlign(wintw_hdc, TA_TOP | TA_CENTER | TA_NOUPDATECP);
        lpDx_maybe = NULL;
        maxlen = 1;
    } else {
        /*
         * In a fixed-pitch font, we draw the whole string in one go
         * in the normal way.
         */
        xoffset = 0;
        SetTextAlign(wintw_hdc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
        lpDx_maybe = lpDx;
        maxlen = len;
    }

    opaque = true;                     /* start by erasing the rectangle */
    for (remaining = len; remaining > 0;
         text += len, remaining -= len, x += char_width * len2) {
        len = (maxlen < remaining ? maxlen : remaining);
        /* don't divide SURROGATE PAIR and VARIATION SELECTOR */
        len2 = len;
        if (maxlen == 1) {
            if (remaining >= 1 && IS_SURROGATE_PAIR(text[0], text[1]))
                len++;
            if (remaining-len >= 1 && IS_LOW_VARSEL(text[len]))
                len++;
            else if (remaining-len >= 2 &&
                     IS_HIGH_VARSEL(text[len], text[len+1]))
                len += 2;
        }

	if (len > lpDx_len) {
            sgrowarray(lpDx, lpDx_len, len);
	    if (lpDx_maybe) lpDx_maybe = lpDx;
	}

        {
            int i;
            /* only last char has dx width in SURROGATE PAIR and
             * VARIATION sequence */
            for (i = 0; i < len; i++) {
                lpDx[i] = char_width;
                if (i+1 < len && IS_HIGH_VARSEL(text[i], text[i+1])) {
                    if (i > 0) lpDx[i-1] = 0;
                    lpDx[i] = 0;
                    i++;
                    lpDx[i] = char_width;
                } else if (i+1 < len && IS_SURROGATE_PAIR(text[i],text[i+1])) {
                    lpDx[i] = 0;
                    i++;
                    lpDx[i] = char_width;
                } else if (IS_LOW_VARSEL(text[i])) {
                    if (i > 0) lpDx[i-1] = 0;
                    lpDx[i] = char_width;
                }
            }
        }

        /* We're using a private area for direct to font. (512 chars.) */
        if (ucsdata.dbcs_screenfont && (text[0] & CSET_MASK) == CSET_ACP) {
            /* Ho Hum, dbcs fonts are a PITA! */
            /* To display on W9x I have to convert to UCS */
            static wchar_t *uni_buf = 0;
            static int uni_len = 0;
            int nlen, mptr;
            if (len > uni_len) {
                sfree(uni_buf);
                uni_len = len;
                uni_buf = snewn(uni_len, wchar_t);
            }

            for(nlen = mptr = 0; mptr<len; mptr++) {
                uni_buf[nlen] = 0xFFFD;
                if (IsDBCSLeadByteEx(ucsdata.font_codepage,
                                     (BYTE) text[mptr])) {
                    char dbcstext[2];
                    dbcstext[0] = text[mptr] & 0xFF;
                    dbcstext[1] = text[mptr+1] & 0xFF;
                    lpDx[nlen] += char_width;
                    MultiByteToWideChar(ucsdata.font_codepage, MB_USEGLYPHCHARS,
                                        dbcstext, 2, uni_buf+nlen, 1);
                    mptr++;
                }
                else
                {
                    char dbcstext[1];
                    dbcstext[0] = text[mptr] & 0xFF;
                    MultiByteToWideChar(ucsdata.font_codepage, MB_USEGLYPHCHARS,
                                        dbcstext, 1, uni_buf+nlen, 1);
                }
                nlen++;
            }
            if (nlen <= 0)
                return;		       /* Eeek! */
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
	if( GetBackgroundImageFlag() && (!GetPuttyFlag()) )
	// ExtTextOutW(hdc, x,
	//	    y - font_height * (lattr == LATTR_BOT) + text_adjust,
	//	    ETO_CLIPPED | etoFlagOpaque, &line_box, uni_buf, nlen, lpDx);
		ExtTextOutW(hdc, x + xoffset,
		    y - font_height * (lattr == LATTR_BOT) + text_adjust,
		    ETO_CLIPPED | etoFlagOpaque, 
	            &line_box, uni_buf, nlen, 
	            lpDx_maybe);
	else
#endif
            ExtTextOutW(wintw_hdc, x + xoffset,
                        y - font_height * (lattr == LATTR_BOT) + text_adjust,
                        ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
                        &line_box, uni_buf, nlen,
                        lpDx_maybe);
            if (bold_font_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
                SetBkMode(wintw_hdc, TRANSPARENT);
                ExtTextOutW(wintw_hdc, x + xoffset - 1,
                            y - font_height * (lattr ==
                                               LATTR_BOT) + text_adjust,
                            ETO_CLIPPED, &line_box, uni_buf, nlen, lpDx_maybe);
            }

            lpDx[0] = -1;
        } else if (DIRECT_FONT(text[0])) {
            static char *directbuf = NULL;
            static size_t directlen = 0;

            sgrowarray(directbuf, directlen, len);
            for (size_t i = 0; i < len; i++)
                directbuf[i] = text[i] & 0xFF;
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
	if( GetBackgroundImageFlag() && (!GetPuttyFlag()) )
	// ExtTextOut(hdc, x,
	// 	   y - font_height * (lattr == LATTR_BOT) + text_adjust,
	// 	   ETO_CLIPPED | etoFlagOpaque, &line_box, directbuf, len, lpDx);
		ExtTextOut(hdc, x + xoffset,
		   y - font_height * (lattr == LATTR_BOT) + text_adjust,
		   ETO_CLIPPED | etoFlagOpaque, 
		   &line_box, directbuf, len, lpDx_maybe);
	else
#endif
            ExtTextOut(wintw_hdc, x + xoffset,
                       y - font_height * (lattr == LATTR_BOT) + text_adjust,
                       ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
                       &line_box, directbuf, len, lpDx_maybe);
            if (bold_font_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
                SetBkMode(wintw_hdc, TRANSPARENT);

                /* GRR: This draws the character outside its box and
                 * can leave 'droppings' even with the clip box! I
                 * suppose I could loop it one character at a time ...
                 * yuk.
                 * 
                 * Or ... I could do a test print with "W", and use +1
                 * or -1 for this shift depending on if the leftmost
                 * column is blank...
                 */
                ExtTextOut(wintw_hdc, x + xoffset - 1,
                           y - font_height * (lattr ==
                                              LATTR_BOT) + text_adjust,
                           ETO_CLIPPED, &line_box, directbuf, len, lpDx_maybe);
            }
        } else {
            /* And 'normal' unicode characters */
            static WCHAR *wbuf = NULL;
            static int wlen = 0;
            int i;

            if (wlen < len) {
                sfree(wbuf);
                wlen = len;
                wbuf = snewn(wlen, WCHAR);
            }

            for (i = 0; i < len; i++)
                wbuf[i] = text[i];
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
 	/* print Glyphs as they are, without Windows' Shaping*/
	if( GetBackgroundImageFlag() && (!PuttyFlag) )
 	// exact_textout(hdc, x, y - font_height * (lattr == LATTR_BOT) + text_adjust,
	// 	      &line_box, wbuf, len, lpDx, !(attr & TATTR_COMBINING) &&!transBg);
		exact_textout(hdc, x + xoffset, 
			y - font_height * (lattr == LATTR_BOT) + text_adjust,
			&line_box, wbuf, len, lpDx, 
			!(attr & TATTR_COMBINING) &&!transBg);
	else
#endif
            /* print Glyphs as they are, without Windows' Shaping*/
            general_textout(wintw_hdc, x + xoffset,
                            y - font_height * (lattr==LATTR_BOT) + text_adjust,
                            &line_box, wbuf, len, lpDx,
                            opaque && !(attr & TATTR_COMBINING));

            /* And the shadow bold hack. */
            if (bold_font_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
                SetBkMode(wintw_hdc, TRANSPARENT);
                ExtTextOutW(wintw_hdc, x + xoffset - 1,
                            y - font_height * (lattr ==
                                               LATTR_BOT) + text_adjust,
                            ETO_CLIPPED, &line_box, wbuf, len, lpDx_maybe);
            }
        }

        /*
         * If we're looping round again, stop erasing the background
         * rectangle.
         */
        SetBkMode(wintw_hdc, TRANSPARENT);
        opaque = false;
    }
    if (lattr != LATTR_TOP && (force_manual_underline ||
                               (und_mode == UND_LINE && (attr & ATTR_UNDER))))
        draw_horizontal_line_on_text(descent, lattr, line_box, fg);

    if (attr & ATTR_STRIKE)
        draw_horizontal_line_on_text(font_strikethrough_y, lattr, line_box, fg);
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
	if( GetBackgroundImageFlag() && (!PuttyFlag) )
    if(textdc)
    {
        int x = line_box.left;
        int y = line_box.top;
        int width = line_box.right - line_box.left;
        int height = line_box.bottom - line_box.top;

        // Copy the result to the working DC.
        //BitBlt(ctx, x, y, width, height, hdc, x, y, SRCCOPY);
	BitBlt(wintw_hdc, x, y, width, height, hdc, x, y, SRCCOPY);
    }
#endif
}

/*
 * Wrapper that handles combining characters.
 */
static void wintw_draw_text(
    TermWin *tw, int x, int y, wchar_t *text, int len,
    unsigned long attr, int lattr, truecolour truecolour)
{
    if (attr & TATTR_COMBINING) {
	unsigned long a = 0;
	int len0 = 1;
        /* don't divide SURROGATE PAIR and VARIATION SELECTOR */
	if (len >= 2 && IS_SURROGATE_PAIR(text[0], text[1]))
	    len0 = 2;
	if (len-len0 >= 1 && IS_LOW_VARSEL(text[len0])) {
	    attr &= ~TATTR_COMBINING;
	    do_text_internal(x, y, text, len0+1, attr, lattr, truecolour);
	    text += len0+1;
	    len -= len0+1;
	    a = TATTR_COMBINING;
	} else if (len-len0 >= 2 && IS_HIGH_VARSEL(text[len0], text[len0+1])) {
	    attr &= ~TATTR_COMBINING;
	    do_text_internal(x, y, text, len0+2, attr, lattr, truecolour);
	    text += len0+2;
	    len -= len0+2;
	    a = TATTR_COMBINING;
	} else {
            attr &= ~TATTR_COMBINING;
        }

	while (len--) {
	    if (len >= 1 && IS_SURROGATE_PAIR(text[0], text[1])) {
		do_text_internal(x, y, text, 2, attr | a, lattr, truecolour);
		len--;
		text++;
	    } else
                do_text_internal(x, y, text, 1, attr | a, lattr, truecolour);

	    text++;
	    a = TATTR_COMBINING;
	}
    } else
	do_text_internal(x, y, text, len, attr, lattr, truecolour);
}

static void wintw_draw_cursor(
    TermWin *tw, int x, int y, wchar_t *text, int len,
    unsigned long attr, int lattr, truecolour truecolour)
{
    int fnt_width;
    int char_width;
    int ctype = cursor_type;

    lattr &= LATTR_MODE;

    if ((attr & TATTR_ACTCURS) && (ctype == 0 || term->big_cursor)) {
	if (*text != UCSWIDE) {
	    win_draw_text(tw, x, y, text, len, attr, lattr, truecolour);
	    return;
	}
	ctype = 2;
	attr |= TATTR_RIGHTCURS;
    }

    fnt_width = char_width = font_width * (1 + (lattr != LATTR_NORM));
    if (attr & ATTR_WIDE)
	char_width *= 2;
    x *= fnt_width;
    y *= font_height;
    x += offset_width;
    y += offset_height;

    if ((attr & TATTR_PASCURS) && (ctype == 0 || term->big_cursor)) {
	POINT pts[5];
	HPEN oldpen;
	pts[0].x = pts[1].x = pts[4].x = x;
	pts[2].x = pts[3].x = x + char_width - 1;
	pts[0].y = pts[3].y = pts[4].y = y;
	pts[1].y = pts[2].y = y + font_height - 1;
	oldpen = SelectObject(wintw_hdc, CreatePen(PS_SOLID, 0, colours[261]));
	Polyline(wintw_hdc, pts, 5);
	oldpen = SelectObject(wintw_hdc, oldpen);
	DeleteObject(oldpen);
    } else if ((attr & (TATTR_ACTCURS | TATTR_PASCURS)) && ctype != 0) {
	int startx, starty, dx, dy, length, i;
	if (ctype == 1) {
	    startx = x;
	    starty = y + descent;
	    dx = 1;
	    dy = 0;
	    length = char_width;
	} else {
	    int xadjust = 0;
	    if (attr & TATTR_RIGHTCURS)
		xadjust = char_width - 1;
	    startx = x + xadjust;
	    starty = y;
	    dx = 0;
	    dy = 1;
	    length = font_height;
	}
	if (attr & TATTR_ACTCURS) {
	    HPEN oldpen;
	    oldpen =
		SelectObject(wintw_hdc, CreatePen(PS_SOLID, 0, colours[261]));
	    MoveToEx(wintw_hdc, startx, starty, NULL);
	    LineTo(wintw_hdc, startx + dx * length, starty + dy * length);
	    oldpen = SelectObject(wintw_hdc, oldpen);
	    DeleteObject(oldpen);
	} else {
	    for (i = 0; i < length; i++) {
		if (i % 2 == 0) {
		    SetPixel(wintw_hdc, startx, starty, colours[261]);
		}
		startx += dx;
		starty += dy;
	    }
	}
    }
}

static void wintw_draw_trust_sigil(TermWin *tw, int x, int y)
{
    x *= font_width;
    y *= font_height;
    x += offset_width;
    y += offset_height;

    DrawIconEx(wintw_hdc, x, y, trust_icon, font_width * 2, font_height,
               0, NULL, DI_NORMAL);
}

/* This function gets the actual width of a character in the normal font.
 */
static int wintw_char_width(TermWin *tw, int uc)
{
    int ibuf = 0;

    /* If the font max is the same as the font ave width then this
     * function is a no-op.
     */
    if (!font_dualwidth) return 1;

    switch (uc & CSET_MASK) {
      case CSET_ASCII:
	uc = ucsdata.unitab_line[uc & 0xFF];
	break;
      case CSET_LINEDRW:
	uc = ucsdata.unitab_xterm[uc & 0xFF];
	break;
      case CSET_SCOACS:
	uc = ucsdata.unitab_scoacs[uc & 0xFF];
	break;
    }
    if (DIRECT_FONT(uc)) {
	if (ucsdata.dbcs_screenfont) return 1;

	/* Speedup, I know of no font where ascii is the wrong width */
	if ((uc&~CSET_MASK) >= ' ' && (uc&~CSET_MASK)<= '~')
	    return 1;

	if ( (uc & CSET_MASK) == CSET_ACP ) {
	    SelectObject(wintw_hdc, fonts[FONT_NORMAL]);
	} else if ( (uc & CSET_MASK) == CSET_OEMCP ) {
	    another_font(FONT_OEM);
	    if (!fonts[FONT_OEM]) return 0;

	    SelectObject(wintw_hdc, fonts[FONT_OEM]);
	} else
	    return 0;

	if (GetCharWidth32(wintw_hdc, uc & ~CSET_MASK,
                           uc & ~CSET_MASK, &ibuf) != 1 &&
	    GetCharWidth(wintw_hdc, uc & ~CSET_MASK,
                         uc & ~CSET_MASK, &ibuf) != 1)
	    return 0;
    } else {
	/* Speedup, I know of no font where ascii is the wrong width */
	if (uc >= ' ' && uc <= '~') return 1;

	SelectObject(wintw_hdc, fonts[FONT_NORMAL]);
	if (GetCharWidth32W(wintw_hdc, uc, uc, &ibuf) == 1)
	    /* Okay that one worked */ ;
	else if (GetCharWidthW(wintw_hdc, uc, uc, &ibuf) == 1)
	    /* This should work on 9x too, but it's "less accurate" */ ;
	else
	    return 0;
    }

    ibuf += font_width / 2 -1;
    ibuf /= font_width;

    return ibuf;
}

DECL_WINDOWS_FUNCTION(static, BOOL, FlashWindowEx, (PFLASHWINFO));
DECL_WINDOWS_FUNCTION(static, BOOL, ToUnicodeEx,
                      (UINT, UINT, const BYTE *, LPWSTR, int, UINT, HKL));
DECL_WINDOWS_FUNCTION(static, BOOL, PlaySound, (LPCTSTR, HMODULE, DWORD));

static void init_winfuncs(void)
{
    HMODULE user32_module = load_system32_dll("user32.dll");
    HMODULE winmm_module = load_system32_dll("winmm.dll");
    HMODULE shcore_module = load_system32_dll("shcore.dll");
    GET_WINDOWS_FUNCTION(user32_module, FlashWindowEx);
    GET_WINDOWS_FUNCTION(user32_module, ToUnicodeEx);
    GET_WINDOWS_FUNCTION_PP(winmm_module, PlaySound);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(shcore_module, GetDpiForMonitor);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, GetSystemMetricsForDpi);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, AdjustWindowRectExForDpi);
}

/*
 * Translate a WM_(SYS)?KEY(UP|DOWN) message into a string of ASCII
 * codes. Returns number of bytes used, zero to drop the message,
 * -1 to forward the message to Windows, or another negative number
 * to indicate a NUL-terminated "special" string.
 */
static int TranslateKey(UINT message, WPARAM wParam, LPARAM lParam,
			unsigned char *output)
{
    BYTE keystate[256];
    int scan, shift_state;
    bool left_alt = false, key_down;
    int r, i;
    unsigned char *p = output;
    static int alt_sum = 0;
    int funky_type = conf_get_int(conf, CONF_funky_type);
    bool no_applic_k = conf_get_bool(conf, CONF_no_applic_k);
    bool ctrlaltkeys = conf_get_bool(conf, CONF_ctrlaltkeys);
    bool nethack_keypad = conf_get_bool(conf, CONF_nethack_keypad);
    char keypad_key = '\0';

    HKL kbd_layout = GetKeyboardLayout(0);

    static wchar_t keys_unicode[3];
    static int compose_char = 0;
    static WPARAM compose_keycode = 0;

    r = GetKeyboardState(keystate);
    if (!r)
	memset(keystate, 0, sizeof(keystate));
    else {
#if 0
#define SHOW_TOASCII_RESULT
	{			       /* Tell us all about key events */
	    static BYTE oldstate[256];
	    static int first = 1;
	    static int scan;
	    int ch;
	    if (first)
		memcpy(oldstate, keystate, sizeof(oldstate));
	    first = 0;

	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT) {
		debug("+");
	    } else if ((HIWORD(lParam) & KF_UP)
		       && scan == (HIWORD(lParam) & 0xFF)) {
		debug(". U");
	    } else {
		debug(".\n");
		if (wParam >= VK_F1 && wParam <= VK_F20)
		    debug("K_F%d", wParam + 1 - VK_F1);
		else
		    switch (wParam) {
		      case VK_SHIFT:
			debug("SHIFT");
			break;
		      case VK_CONTROL:
			debug("CTRL");
			break;
		      case VK_MENU:
			debug("ALT");
			break;
		      default:
			debug("VK_%02x", wParam);
		    }
		if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
		    debug("*");
		debug(", S%02x", scan = (HIWORD(lParam) & 0xFF));

		ch = MapVirtualKeyEx(wParam, 2, kbd_layout);
		if (ch >= ' ' && ch <= '~')
		    debug(", '%c'", ch);
		else if (ch)
		    debug(", $%02x", ch);

		if (keys_unicode[0])
		    debug(", KB0=%04x", keys_unicode[0]);
		if (keys_unicode[1])
		    debug(", KB1=%04x", keys_unicode[1]);
		if (keys_unicode[2])
		    debug(", KB2=%04x", keys_unicode[2]);

		if ((keystate[VK_SHIFT] & 0x80) != 0)
		    debug(", S");
		if ((keystate[VK_CONTROL] & 0x80) != 0)
		    debug(", C");
		if ((HIWORD(lParam) & KF_EXTENDED))
		    debug(", E");
		if ((HIWORD(lParam) & KF_UP))
		    debug(", U");
	    }

	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT);
	    else if ((HIWORD(lParam) & KF_UP))
		oldstate[wParam & 0xFF] ^= 0x80;
	    else
		oldstate[wParam & 0xFF] ^= 0x81;

	    for (ch = 0; ch < 256; ch++)
		if (oldstate[ch] != keystate[ch])
		    debug(", M%02x=%02x", ch, keystate[ch]);

	    memcpy(oldstate, keystate, sizeof(oldstate));
	}
#endif

	if (wParam == VK_MENU && (HIWORD(lParam) & KF_EXTENDED)) {
	    keystate[VK_RMENU] = keystate[VK_MENU];
	}

#ifdef MOD_DISABLEALTGR
/*disable altgr*/
if( !get_param("PUTTY") && conf_get_int(conf, CONF_disablealtgr) ) {
	keystate[VK_RMENU] = 0;
}
#endif

	/* Nastyness with NUMLock - Shift-NUMLock is left alone though */
	if ((funky_type == FUNKY_VT400 ||
	     (funky_type <= FUNKY_LINUX && term->app_keypad_keys &&
	      !no_applic_k))
	    && wParam == VK_NUMLOCK && !(keystate[VK_SHIFT] & 0x80)) {

	    wParam = VK_EXECUTE;

	    /* UnToggle NUMLock */
	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == 0)
		keystate[VK_NUMLOCK] ^= 1;
	}

	/* And write back the 'adjusted' state */
	SetKeyboardState(keystate);
    }

    /* Disable Auto repeat if required */
    if (term->repeat_off &&
	(HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT)
	return 0;

    if ((HIWORD(lParam) & KF_ALTDOWN) && (keystate[VK_RMENU] & 0x80) == 0)
	left_alt = true;

    key_down = ((HIWORD(lParam) & KF_UP) == 0);

    /* Make sure Ctrl-ALT is not the same as AltGr for ToAscii unless told. */
    if (left_alt && (keystate[VK_CONTROL] & 0x80)) {
	if (ctrlaltkeys)
	    keystate[VK_MENU] = 0;
	else {
	    keystate[VK_RMENU] = 0x80;
	    left_alt = false;
	}
    }

    scan = (HIWORD(lParam) & (KF_UP | KF_EXTENDED | 0xFF));
    shift_state = ((keystate[VK_SHIFT] & 0x80) != 0)
	+ ((keystate[VK_CONTROL] & 0x80) != 0) * 2;

    /* Note if AltGr was pressed and if it was used as a compose key */
    if (!compose_state) {
	compose_keycode = 0x100;
	if (conf_get_bool(conf, CONF_compose_key)) {
	    if (wParam == VK_MENU && (HIWORD(lParam) & KF_EXTENDED))
		compose_keycode = wParam;
	}
	if (wParam == VK_APPS)
	    compose_keycode = wParam;
    }

    if (wParam == compose_keycode) {
	if (compose_state == 0
	    && (HIWORD(lParam) & (KF_UP | KF_REPEAT)) == 0) compose_state =
		1;
	else if (compose_state == 1 && (HIWORD(lParam) & KF_UP))
	    compose_state = 2;
	else
	    compose_state = 0;
    } else if (compose_state == 1 && wParam != VK_CONTROL)
	compose_state = 0;

    if (compose_state > 1 && left_alt)
	compose_state = 0;

    /* Sanitize the number pad if not using a PC NumPad */
    if (left_alt || (term->app_keypad_keys && !no_applic_k
		     && funky_type != FUNKY_XTERM)
	|| funky_type == FUNKY_VT400 || nethack_keypad || compose_state) {
	if ((HIWORD(lParam) & KF_EXTENDED) == 0) {
	    int nParam = 0;
	    switch (wParam) {
	      case VK_INSERT:
		nParam = VK_NUMPAD0;
		break;
	      case VK_END:
		nParam = VK_NUMPAD1;
		break;
	      case VK_DOWN:
		nParam = VK_NUMPAD2;
		break;
	      case VK_NEXT:
		nParam = VK_NUMPAD3;
		break;
	      case VK_LEFT:
		nParam = VK_NUMPAD4;
		break;
	      case VK_CLEAR:
		nParam = VK_NUMPAD5;
		break;
	      case VK_RIGHT:
		nParam = VK_NUMPAD6;
		break;
	      case VK_HOME:
		nParam = VK_NUMPAD7;
		break;
	      case VK_UP:
		nParam = VK_NUMPAD8;
		break;
	      case VK_PRIOR:
		nParam = VK_NUMPAD9;
		break;
	      case VK_DELETE:
		nParam = VK_DECIMAL;
		break;
	    }
	    if (nParam) {
		if (keystate[VK_NUMLOCK] & 1)
		    shift_state |= 1;
		wParam = nParam;
	    }
	}
    }

    /* If a key is pressed and AltGr is not active */
    if (key_down && (keystate[VK_RMENU] & 0x80) == 0 && !compose_state) {
	/* Okay, prepare for most alts then ... */
#ifdef MOD_KEYMAPPING
	if( !GetPuttyFlag() ) {
		if (left_alt && shift_state != 1 && !(wParam == VK_UP || wParam == VK_DOWN || wParam == VK_RIGHT || wParam == VK_LEFT)) {
			int fkey = 0;
			switch (wParam) {
				case VK_F1:
				case VK_F2:
				case VK_F3:
				case VK_F4:
				case VK_F5:
				case VK_F6:
				case VK_F7:
				case VK_F8:
				case VK_F9:
				case VK_F10:
				case VK_F11:
				case VK_F12:
				case VK_INSERT:
				case VK_DELETE:
				case VK_HOME:
				case VK_END:
				case VK_PRIOR:
				case VK_NEXT:
					fkey = 1;
					break;
				default:
					break;
			}

			if (!(term->funky_type == FUNKY_XTERM && !term->vt52_mode) || fkey == 0) {
				*p++ = '\033';
			}
		}
	}
	else
#endif
	if (left_alt)
	    *p++ = '\033';

	/* Lets see if it's a pattern we know all about ... */
	if (wParam == VK_PRIOR && shift_state == 1) {
            SendMessage(wgs.term_hwnd, WM_VSCROLL, SB_PAGEUP, 0);
	    return 0;
	}
        if (wParam == VK_PRIOR && shift_state == 3) { /* ctrl-shift-pageup */
            SendMessage(wgs.term_hwnd, WM_VSCROLL, SB_TOP, 0);
            return 0;
        }
        if (wParam == VK_NEXT && shift_state == 3) { /* ctrl-shift-pagedown */
            SendMessage(wgs.term_hwnd, WM_VSCROLL, SB_BOTTOM, 0);
            return 0;
        }
	if (wParam == VK_PRIOR && shift_state == 2) {
            SendMessage(wgs.term_hwnd, WM_VSCROLL, SB_LINEUP, 0);
	    return 0;
	}
	if (wParam == VK_NEXT && shift_state == 1) {
            SendMessage(wgs.term_hwnd, WM_VSCROLL, SB_PAGEDOWN, 0);
	    return 0;
	}
	if (wParam == VK_NEXT && shift_state == 2) {
            SendMessage(wgs.term_hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
	    return 0;
	}
	if ((wParam == VK_PRIOR || wParam == VK_NEXT) && shift_state == 3) {
	    term_scroll_to_selection(term, (wParam == VK_PRIOR ? 0 : 1));
	    return 0;
	}
	if (wParam == VK_INSERT && shift_state == 2) {
            switch (conf_get_int(conf, CONF_ctrlshiftins)) {
              case CLIPUI_IMPLICIT:
                break;          /* no need to re-copy to CLIP_LOCAL */
              case CLIPUI_EXPLICIT:
                term_request_copy(term, clips_system, lenof(clips_system));
                break;
              default:
                break;
            }
	    return 0;
	}
	if (wParam == VK_INSERT && shift_state == 1) {
            switch (conf_get_int(conf, CONF_ctrlshiftins)) {
              case CLIPUI_IMPLICIT:
                term_request_paste(term, CLIP_LOCAL);
                break;
              case CLIPUI_EXPLICIT:
                term_request_paste(term, CLIP_SYSTEM);
                break;
              default:
                break;
            }
	    return 0;
	}
	if (wParam == 'C' && shift_state == 3) {
            switch (conf_get_int(conf, CONF_ctrlshiftcv)) {
              case CLIPUI_IMPLICIT:
                break;          /* no need to re-copy to CLIP_LOCAL */
              case CLIPUI_EXPLICIT:
                term_request_copy(term, clips_system, lenof(clips_system));
                break;
              default:
                break;
            }
	    return 0;
	}
	if (wParam == 'V' && shift_state == 3) {
            switch (conf_get_int(conf, CONF_ctrlshiftcv)) {
              case CLIPUI_IMPLICIT:
                term_request_paste(term, CLIP_LOCAL);
                break;
              case CLIPUI_EXPLICIT:
                term_request_paste(term, CLIP_SYSTEM);
                break;
              default:
                break;
            }
	    return 0;
	}
	if (left_alt && wParam == VK_F4 && conf_get_bool(conf, CONF_alt_f4)) {
	    return -1;
	}
	if (left_alt && wParam == VK_SPACE && conf_get_bool(conf,
                                                            CONF_alt_space)) {
            SendMessage(wgs.term_hwnd, WM_SYSCOMMAND, SC_KEYMENU, 0);
	    return -1;
	}
	if (left_alt && wParam == VK_RETURN &&
	    conf_get_bool(conf, CONF_fullscreenonaltenter) &&
	    (conf_get_int(conf, CONF_resize_action) != RESIZE_DISABLED)) {
 	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) != KF_REPEAT)
 		flip_full_screen();
	    return -1;
	}
	/* Control-Numlock for app-keypad mode switch */
	if (wParam == VK_PAUSE && shift_state == 2) {
	    term->app_keypad_keys = !term->app_keypad_keys;
	    return 0;
	}

#ifdef MOD_KEYMAPPING
	if (wParam == VK_BACK && shift_state <= 1) {	/* Backspace of Shift Backspace */
#else
	if (wParam == VK_BACK && shift_state == 0) {	/* Backspace */
#endif
	    *p++ = (conf_get_bool(conf, CONF_bksp_is_delete) ? 0x7F : 0x08);
	    *p++ = 0;
	    return -2;
	}
#ifdef MOD_KEYMAPPING
	if (wParam == VK_BACK) {	/* Ctrl Backspace or Ctrl+Shift Backspace */
#else
	if (wParam == VK_BACK && shift_state == 1) {	/* Shift Backspace */
#endif
	    /* We do the opposite of what is configured */
	    *p++ = (conf_get_bool(conf, CONF_bksp_is_delete) ? 0x08 : 0x7F);
	    *p++ = 0;
	    return -2;
	}
#ifdef MOD_KEYMAPPING
	if( !GetPuttyFlag() ) {
		if (wParam == VK_TAB && shift_state == 2) { 	/* Ctrl-Tab */
			p += sprintf((char *) p, "\x1B[27;5;9~");
			return p - output;
		}
		if (wParam == VK_TAB && shift_state == 3) {	/* Ctrl-Shift-Tab */
			p += sprintf((char *) p, "\x1B[27;6;9~");
			return p - output;
		}
	}
#endif
	if (wParam == VK_TAB && shift_state == 1) {	/* Shift tab */
	    *p++ = 0x1B;
	    *p++ = '[';
	    *p++ = 'Z';
	    return p - output;
	}
	if (wParam == VK_SPACE && shift_state == 2) {	/* Ctrl-Space */
	    *p++ = 0;
	    return p - output;
	}
	if (wParam == VK_SPACE && shift_state == 3) {	/* Ctrl-Shift-Space */
	    *p++ = 160;
	    return p - output;
	}
	if (wParam == VK_CANCEL && shift_state == 2) {	/* Ctrl-Break */
            if (backend)
                backend_special(backend, SS_BRK, 0);
	    return 0;
	}
	if (wParam == VK_PAUSE) {      /* Break/Pause */
	    *p++ = 26;
	    *p++ = 0;
	    return -2;
	}
	/* Control-2 to Control-8 are special */
	if (shift_state == 2 && wParam >= '2' && wParam <= '8') {
	    *p++ = "\000\033\034\035\036\037\177"[wParam - '2'];
	    return p - output;
	}
	if (shift_state == 2 && (wParam == 0xBD || wParam == 0xBF)) {
	    *p++ = 0x1F;
	    return p - output;
	}
	if (shift_state == 2 && (wParam == 0xDF || wParam == 0xDC)) {
	    *p++ = 0x1C;
	    return p - output;
	}
	if (shift_state == 3 && wParam == 0xDE) {
	    *p++ = 0x1E;	       /* Ctrl-~ == Ctrl-^ in xterm at least */
	    return p - output;
	}

	switch (wParam) {
          case VK_NUMPAD0: keypad_key = '0'; goto numeric_keypad;
          case VK_NUMPAD1: keypad_key = '1'; goto numeric_keypad;
          case VK_NUMPAD2: keypad_key = '2'; goto numeric_keypad;
          case VK_NUMPAD3: keypad_key = '3'; goto numeric_keypad;
          case VK_NUMPAD4: keypad_key = '4'; goto numeric_keypad;
          case VK_NUMPAD5: keypad_key = '5'; goto numeric_keypad;
          case VK_NUMPAD6: keypad_key = '6'; goto numeric_keypad;
          case VK_NUMPAD7: keypad_key = '7'; goto numeric_keypad;
          case VK_NUMPAD8: keypad_key = '8'; goto numeric_keypad;
          case VK_NUMPAD9: keypad_key = '9'; goto numeric_keypad;
          case VK_DECIMAL: keypad_key = '.'; goto numeric_keypad;
          case VK_ADD: keypad_key = '+'; goto numeric_keypad;
          case VK_SUBTRACT: keypad_key = '-'; goto numeric_keypad;
          case VK_MULTIPLY: keypad_key = '*'; goto numeric_keypad;
          case VK_DIVIDE: keypad_key = '/'; goto numeric_keypad;
          case VK_EXECUTE: keypad_key = 'G'; goto numeric_keypad;
            /* also the case for VK_RETURN below can sometimes come here */
          numeric_keypad:
            /* Left Alt overrides all numeric keypad usage to act as
             * numeric character code input */
            if (left_alt) {
                if (keypad_key >= '0' && keypad_key <= '9')
                    alt_sum = alt_sum * 10 + keypad_key - '0';
                else
                    alt_sum = 0;
                break;
            }

            {
                int nchars = format_numeric_keypad_key(
                (char *)p, term, keypad_key,
                shift_state & 1, shift_state & 2);
                if (!nchars) {
                    /*
                     * If we didn't get an escape sequence out of the
                     * numeric keypad key, then that must be because
                     * we're in Num Lock mode without application
                     * keypad enabled. In that situation we leave this
                     * keypress to the ToUnicode/ToAsciiEx handler
                     * below, which will translate it according to the
                     * appropriate keypad layout (e.g. so that what a
                     * Brit thinks of as keypad '.' can become ',' in
                     * the German layout).
                     *
                     * An exception is the keypad Return key: if we
                     * didn't get an escape sequence for that, we
                     * treat it like ordinary Return, taking into
                     * account Telnet special new line codes and
                     * config options.
                     */
                    if (keypad_key == '\r')
                        goto ordinary_return_key;
                    break;
                }

                p += nchars;
            return p - output;
            }

            int fkey_number;
	  case VK_F1: fkey_number = 1; goto numbered_function_key;
	  case VK_F2: fkey_number = 2; goto numbered_function_key;
	  case VK_F3: fkey_number = 3; goto numbered_function_key;
	  case VK_F4: fkey_number = 4; goto numbered_function_key;
	  case VK_F5: fkey_number = 5; goto numbered_function_key;
	  case VK_F6: fkey_number = 6; goto numbered_function_key;
	  case VK_F7: fkey_number = 7; goto numbered_function_key;
	  case VK_F8: fkey_number = 8; goto numbered_function_key;
	  case VK_F9: fkey_number = 9; goto numbered_function_key;
	  case VK_F10: fkey_number = 10; goto numbered_function_key;
	  case VK_F11: fkey_number = 11; goto numbered_function_key;
	  case VK_F12: fkey_number = 12; goto numbered_function_key;
	  case VK_F13: fkey_number = 13; goto numbered_function_key;
	  case VK_F14: fkey_number = 14; goto numbered_function_key;
	  case VK_F15: fkey_number = 15; goto numbered_function_key;
	  case VK_F16: fkey_number = 16; goto numbered_function_key;
	  case VK_F17: fkey_number = 17; goto numbered_function_key;
	  case VK_F18: fkey_number = 18; goto numbered_function_key;
	  case VK_F19: fkey_number = 19; goto numbered_function_key;
	  case VK_F20: fkey_number = 20; goto numbered_function_key;
          numbered_function_key:
#ifdef MOD_KEYMAPPING
	    p += format_function_key((char *)p, term, fkey_number, shift_state, left_alt);
#else
            p += format_function_key((char *)p, term, fkey_number,
                                     shift_state & 1, shift_state & 2);
#endif
            return p - output;

            SmallKeypadKey sk_key;
	  case VK_HOME: sk_key = SKK_HOME; goto small_keypad_key;
	  case VK_END: sk_key = SKK_END; goto small_keypad_key;
	  case VK_INSERT: sk_key = SKK_INSERT; goto small_keypad_key;
	  case VK_DELETE: sk_key = SKK_DELETE; goto small_keypad_key;
	  case VK_PRIOR: sk_key = SKK_PGUP; goto small_keypad_key;
	  case VK_NEXT: sk_key = SKK_PGDN; goto small_keypad_key;
          small_keypad_key:
#ifdef MOD_KEYMAPPING
            p += format_small_keypad_key((char *)p, term, sk_key, shift_state, left_alt);
#else
            /* These keys don't generate terminal input with Ctrl */
            if (shift_state & 2)
                break;
            p += format_small_keypad_key((char *)p, term, sk_key);
#endif
            return p - output;

	    char xkey;
          case VK_UP: xkey = 'A'; goto arrow_key;
          case VK_DOWN: xkey = 'B'; goto arrow_key;
          case VK_RIGHT: xkey = 'C'; goto arrow_key;
          case VK_LEFT: xkey = 'D'; goto arrow_key;
          case VK_CLEAR: xkey = 'G'; goto arrow_key; /* close enough */
          arrow_key:
#ifdef MOD_KEYMAPPING
		p += format_arrow_key((char*)p, term, xkey, shift_state, left_alt);
#else
            p += format_arrow_key((char *)p, term, xkey, shift_state & 2);
#endif
            return p - output;

          case VK_RETURN:
            if (HIWORD(lParam) & KF_EXTENDED) {
                keypad_key = '\r';
                goto numeric_keypad;
            }
          ordinary_return_key:
            if (shift_state == 0 && term->cr_lf_return) {
                *p++ = '\r';
                *p++ = '\n';
                return p - output;
            } else {
	    *p++ = 0x0D;
#ifdef MOD_PERSO
		if (conf_get_int(conf, CONF_enter_sends_crlf)) *p++ = 0x0A;
#endif
	    *p++ = 0;
	    return -2;
	}
    }
    }

    /* Okay we've done everything interesting; let windows deal with 
     * the boring stuff */
    {
	bool capsOn = false;

	/* helg: clear CAPS LOCK state if caps lock switches to cyrillic */
	if(keystate[VK_CAPITAL] != 0 &&
	   conf_get_bool(conf, CONF_xlat_capslockcyr)) {
	    capsOn= !left_alt;
	    keystate[VK_CAPITAL] = 0;
	}

	/* XXX how do we know what the max size of the keys array should
	 * be is? There's indication on MS' website of an Inquire/InquireEx
	 * functioning returning a KBINFO structure which tells us. */
	if (osPlatformId == VER_PLATFORM_WIN32_NT && p_ToUnicodeEx) {
	    r = p_ToUnicodeEx(wParam, scan, keystate, keys_unicode,
                              lenof(keys_unicode), 0, kbd_layout);
	} else {
	    /* XXX 'keys' parameter is declared in MSDN documentation as
	     * 'LPWORD lpChar'.
	     * The experience of a French user indicates that on
	     * Win98, WORD[] should be passed in, but on Win2K, it should
	     * be BYTE[]. German WinXP and my Win2K with "US International"
	     * driver corroborate this.
	     * Experimentally I've conditionalised the behaviour on the
	     * Win9x/NT split, but I suspect it's worse than that.
	     * See wishlist item `win-dead-keys' for more horrible detail
	     * and speculations. */
	    int i;
	    static WORD keys[3];
	    static BYTE keysb[3];
	    r = ToAsciiEx(wParam, scan, keystate, keys, 0, kbd_layout);
	    if (r > 0) {
	        for (i = 0; i < r; i++) {
	            keysb[i] = (BYTE)keys[i];
	        }
	        MultiByteToWideChar(CP_ACP, 0, (LPCSTR)keysb, r,
                                    keys_unicode, lenof(keys_unicode));
	    }
	}
#ifdef SHOW_TOASCII_RESULT
	if (r == 1 && !key_down) {
	    if (alt_sum) {
		if (in_utf(term) || ucsdata.dbcs_screenfont)
		    debug(", (U+%04x)", alt_sum);
		else
		    debug(", LCH(%d)", alt_sum);
	    } else {
		debug(", ACH(%d)", keys_unicode[0]);
	    }
	} else if (r > 0) {
	    int r1;
	    debug(", ASC(");
	    for (r1 = 0; r1 < r; r1++) {
		debug("%s%d", r1 ? "," : "", keys_unicode[r1]);
	    }
	    debug(")");
	}
#endif
	if (r > 0) {
	    WCHAR keybuf;

	    p = output;
	    for (i = 0; i < r; i++) {
		wchar_t wch = keys_unicode[i];

		if (compose_state == 2 && wch >= ' ' && wch < 0x80) {
		    compose_char = wch;
		    compose_state++;
		    continue;
		}
		if (compose_state == 3 && wch >= ' ' && wch < 0x80) {
		    int nc;
		    compose_state = 0;

		    if ((nc = check_compose(compose_char, wch)) == -1) {
			MessageBeep(MB_ICONHAND);
			return 0;
		    }
		    keybuf = nc;
                    term_keyinputw(term, &keybuf, 1);
		    continue;
		}

		compose_state = 0;

		if (!key_down) {
		    if (alt_sum) {
			if (in_utf(term) || ucsdata.dbcs_screenfont) {
			    keybuf = alt_sum;
                            term_keyinputw(term, &keybuf, 1);
			} else {
			    char ch = (char) alt_sum;
			    /*
			     * We need not bother about stdin
			     * backlogs here, because in GUI PuTTY
			     * we can't do anything about it
			     * anyway; there's no means of asking
			     * Windows to hold off on KEYDOWN
			     * messages. We _have_ to buffer
			     * everything we're sent.
			     */
			    term_keyinput(term, -1, &ch, 1);
			}
			alt_sum = 0;
		    } else {
			term_keyinputw(term, &wch, 1);
		    }
		} else {
		    if(capsOn && wch < 0x80) {
			WCHAR cbuf[2];
			cbuf[0] = 27;
			cbuf[1] = xlat_uskbd2cyrllic(wch);
			term_keyinputw(term, cbuf+!left_alt, 1+!!left_alt);
		    } else {
			WCHAR cbuf[2];
			cbuf[0] = '\033';
			cbuf[1] = wch;
			term_keyinputw(term, cbuf+!left_alt, 1+!!left_alt);
		    }
		}
		show_mouseptr(false);
	    }

	    /* This is so the ALT-Numpad and dead keys work correctly. */
	    keys_unicode[0] = 0;

	    return p - output;
	}
	/* If we're definitely not building up an ALT-54321 then clear it */
	if (!left_alt)
	    keys_unicode[0] = 0;
	/* If we will be using alt_sum fix the 256s */
	else if (keys_unicode[0] && (in_utf(term) || ucsdata.dbcs_screenfont))
	    keys_unicode[0] = 10;
    }

    /*
     * ALT alone may or may not want to bring up the System menu.
     * If it's not meant to, we return 0 on presses or releases of
     * ALT, to show that we've swallowed the keystroke. Otherwise
     * we return -1, which means Windows will give the keystroke
     * its default handling (i.e. bring up the System menu).
     */
    if (wParam == VK_MENU && !conf_get_bool(conf, CONF_alt_only))
	return 0;

    return -1;
}

#ifdef MOD_PERSO
/* Creer un titre de fenetre a partir d'un schema donne
	%%f: le folder auquel apprtient le session
	%%h: le hostname
	%%i: le pid du process
	%%p: le port
	%%P: le protocole
	%%s: nom de la session (vide sinon)
	%%u: le user
	%%l: la liste des port forward locaux
	%%d: la liste des port forward dynamic
Ex: %%P://%%u@%%h:%%p
Ex: %%f / %%s
*/
void make_title( char * res, char * fmt, const char * title ) {
	int p;
	char b[1024] ;
	int port ;
	
	sprintf( res, fmt, title ) ;

	while( (p=poss( "%%s", res)) > 0 ) { del(res,p,3); if(strlen(conf_get_str(conf,CONF_sessionname))>0) insert(res,conf_get_str(conf,CONF_sessionname),p); }
	while( (p=poss( "%%h", res)) > 0 ) { 
		del(res,p,3);
		if( conf_get_int(conf,CONF_protocol) == PROT_SERIAL ) {
			insert(res,conf_get_str(conf,CONF_serline),p);
		} else {
			insert(res,conf_get_str(conf,CONF_host),p);
		}
	}
	while( (p=poss( "%%u", res)) > 0 ) { del(res,p,3); insert(res,conf_get_str(conf,CONF_username),p); }
	while( (p=poss( "%%f", res)) > 0 ) { del(res,p,3); if(strlen(conf_get_str(conf,CONF_folder))>0) insert(res,conf_get_str(conf,CONF_folder),p); }
	
	port = conf_get_int(conf,CONF_port); 
	switch(conf_get_int(conf,CONF_protocol)) {
		case PROT_RAW: strcpy(b,"raw"); break;
		case PROT_TELNET: strcpy(b,"telnet"); if(port==-1) port=23 ; break;
		case PROT_RLOGIN: strcpy(b,"rlogin"); break;
		case PROT_SSH: strcpy(b,"ssh"); if(port==-1) port=22 ; break;
		case PROT_SERIAL: strcpy(b,"serial"); break;
	}
	while( (p=poss( "%%P", res)) > 0 ) { del(res,p,3); insert(res,b,p); }
	
	sprintf(b,"%d", port ) ; 
	while( (p=poss( "%%p", res)) > 0 ) { del(res,p,3); insert(res,b,p); }
	
	sprintf(b,"%ld", GetCurrentProcessId() ) ; 
	while( (p=poss( "%%i", res)) > 0 ) { del(res,p,3); insert(res,b,p); }
	while( (p=poss( "%%d", res)) > 0 ) { // forward port dynamic
		char *key, *val, *k;
		int nb=0 ;
		del(res,p,3) ;
		b[0]='\0';
		for (val = conf_get_str_strs(conf, CONF_portfwd, NULL, &key);
			val != NULL;
			val = conf_get_str_strs(conf, CONF_portfwd, key, &key)) {
			k=key; if( (k[0]=='4')||(k[0]=='6') ) { k++; }
			if( (k[0]=='L') && (!strcmp(val,"D")) ) {
				if(nb!=0) {strcat(b,"|");}
				strcat(b,k+1);
				switch( GetPortFwdState( atoi(k+1), GetCurrentProcessId() ) ) {
					case -1: 
						break ;
					case 0: 
						strcat(b,"+");
						break ;
					default:
						strcat(b,"~");
				}
				nb++;
			}
		}
		insert(res,b,p) ;
	}
	while( (p=poss( "%%l", res)) > 0 ) { // forward port locaux
		char *key, *val, *k;
		int nb=0 ;
		del(res,p,3) ;
		b[0]='\0';
		for (val = conf_get_str_strs(conf, CONF_portfwd, NULL, &key);
			val != NULL;
			val = conf_get_str_strs(conf, CONF_portfwd, key, &key)) {
			k=key; if( (k[0]=='4')||(k[0]=='6') ) { k++; }
			if( (k[0]=='L') && (strcmp(val,"D")) ) {
				if(nb!=0) {strcat(b,"|");}
				strcat(b,k+1);
				nb++;
			}
			res[1023]='\0';
		}
		insert(res,b,p) ;
	}
	res[1023]='\0';
}


void set_title_internal(TermWin *tw, const char *title) {
    sfree(window_name);
    window_name = dupstr(title);
    if (conf_get_bool(conf, CONF_win_name_always) || !IsIconic(wgs.term_hwnd))
        SetWindowText(wgs.term_hwnd, title);
}

static void wintw_set_title(TermWin *tw, const char *title_in) {
	char *buffer, fmt[256]="%s" ;
	char *title ;

	if( title_in==NULL ) { return ; }
	title = (char*)malloc(strlen(title_in)+1024); strcpy(title,title_in);

	if( (title[0]=='_')&&(title[1]=='_') ) { // Mode commande a distance
		if( ManageLocalCmd( MainHwnd, title+2 ) ) { free(title); return ; }
	}
	
	if( !GetTitleBarFlag() ) { set_title_internal( tw, title ) ; free(title) ; return ; }
		
	if( strstr(title, " (PROTECTED)")==(title+strlen(title)-12) ) 
		{ title[strlen(title)-12]='\0' ; }

#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
	buffer = (char*) malloc( strlen( title ) + strlen( conf_get_str(conf,CONF_host)) + strlen( conf_get_filename(conf,CONF_bg_image_filename)->path ) + 40 ) ; 
	if( GetBackgroundImageFlag() && GetImageViewerFlag() && (!PuttyFlag) ) { sprintf( buffer, "%s", conf_get_filename(conf,CONF_bg_image_filename)->path ) ; }
	else 
#else
	buffer = (char*) malloc( strlen( title ) + strlen( conf_get_str(conf,CONF_host)) + 40 ) ; 
#endif
	if( GetSizeFlag() && (!IsZoomed( MainHwnd )) ) {
		if( strlen( title ) > 0 ) {
			if( title[strlen(title)-1] == ']' ) {
				make_title( buffer, "%s", title ) ;
			} else { 
				sprintf( fmt, "%%s [%dx%d]", conf_get_int(conf,CONF_height), conf_get_int(conf,CONF_width)) ;
				make_title( buffer, fmt, title ) ;
			}
		} else {
			sprintf( buffer, "%s [%dx%d] - %s", conf_get_str(conf,CONF_host), conf_get_int(conf,CONF_height), conf_get_int(conf,CONF_width), appname ) ;
		}
	} else {
		if( strlen( title ) > 0 ) { 
			make_title( buffer, "%s", title ) ; 
		} else {
			sprintf( buffer, "%s - %s", conf_get_str(conf,CONF_host), appname ) ;
		}
	}
	if( GetProtectFlag() ) if( strstr(buffer, " (PROTECTED)")==NULL ) { strcat( buffer, " (PROTECTED)" ) ; }
	if( conf_get_bool(conf, CONF_alwaysontop) ) if( strstr(buffer, " (ONTOP)")==NULL ) { strcat( buffer, " (ONTOP)" ) ; }
	if( conf_get_bool(conf, CONF_ssh_tunnel_print_in_title) ) if( strstr(buffer, " (SOCKS: ")==NULL ) { 
		make_title( fmt, " (SOCKS: %s)", "%%d") ; 
		strcat( buffer, fmt ) ;
	}
	set_title_internal( tw, buffer ) ;
	free(title);
	free(buffer);
}
	
static void wintw_set_icon_title(TermWin *tw, const char *title2)
{
	char title[1024]="",buf[512]=""; ;
	int i=0;
	do { buf[i]=title2[i]; i++ ; }
	while ( (i<511)&&(title2[i]!='\0') ) ;
	buf[i+1]='\0';
	
	make_title( title, "%s", buf ) ;
    sfree(icon_name);
    icon_name = snewn(1 + strlen(title), char);
    strcpy(icon_name, title);
    if (conf_get_bool(conf, CONF_win_name_always) || !IsIconic(wgs.term_hwnd))
        SetWindowText(wgs.term_hwnd, title);
}
#else
static void wintw_set_title(TermWin *tw, const char *title)
{
    sfree(window_name);
    window_name = dupstr(title);
    if (conf_get_bool(conf, CONF_win_name_always) || !IsIconic(wgs.term_hwnd))
        SetWindowText(wgs.term_hwnd, title);
}

static void wintw_set_icon_title(TermWin *tw, const char *title)
{
    sfree(icon_name);
    icon_name = dupstr(title);
    if (!conf_get_bool(conf, CONF_win_name_always) && IsIconic(wgs.term_hwnd))
        SetWindowText(wgs.term_hwnd, title);
}
#endif

static void wintw_set_scrollbar(TermWin *tw, int total, int start, int page)
{
    SCROLLINFO si;

    if (!conf_get_bool(conf, is_full_screen() ?
                       CONF_scrollbar_in_fullscreen : CONF_scrollbar))
	return;

    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = total - 1;
    si.nPage = page;
    si.nPos = start;
    if (wgs.term_hwnd)
        SetScrollInfo(wgs.term_hwnd, SB_VERT, &si, true);
}

static bool wintw_setup_draw_ctx(TermWin *tw)
{
//    assert(!wintw_hdc);
    wintw_hdc = make_hdc();
    return wintw_hdc != NULL;
}

static void wintw_free_draw_ctx(TermWin *tw)
{
    assert(wintw_hdc);
    free_hdc(wintw_hdc);
    wintw_hdc = NULL;
}

/*
 * Set up the colour palette.
 */
static void init_palette(void)
{
    pal = NULL;
    logpal = snew_plus(LOGPALETTE, (OSC4_NCOLOURS - 1) * sizeof(PALETTEENTRY));
    logpal->palVersion = 0x300;
    logpal->palNumEntries = OSC4_NCOLOURS;
    for (unsigned i = 0; i < OSC4_NCOLOURS; i++)
        logpal->palPalEntry[i].peFlags = PC_NOCOLLAPSE;
}

static void wintw_palette_set(TermWin *win, unsigned start,
                              unsigned ncolours, const rgb *colours_in)
{
    assert(start <= OSC4_NCOLOURS);
    assert(ncolours <= OSC4_NCOLOURS - start);

    for (unsigned i = 0; i < ncolours; i++) {
        const rgb *in = &colours_in[i];
        PALETTEENTRY *out = &logpal->palPalEntry[i + start];
        out->peRed = in->r;
        out->peGreen = in->g;
        out->peBlue = in->b;
        colours[i + start] = RGB(in->r, in->g, in->b) ^ colorref_modifier;
    }

    bool got_new_palette = false;

    if (!tried_pal && conf_get_bool(conf, CONF_try_palette)) {
        HDC hdc = GetDC(wgs.term_hwnd);
        if (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE) {
            pal = CreatePalette(logpal);
            if (pal) {
                SelectPalette(hdc, pal, false);
                RealizePalette(hdc);
                SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), false);

                /* Convert all RGB() values in colours[] into PALETTERGB(),
                 * and ensure we stick to that later */
                colorref_modifier = PALETTERGB(0, 0, 0) ^ RGB(0, 0, 0);
                for (unsigned i = 0; i < OSC4_NCOLOURS; i++)
                    colours[i] ^= colorref_modifier;

                /* Inhibit the SetPaletteEntries call below */
                got_new_palette = true;
            }
        }
        ReleaseDC(wgs.term_hwnd, hdc);
        tried_pal = true;
    }

    if (pal && !got_new_palette) {
        /* We already had a palette, so replace the changed colours in the
         * existing one. */
        SetPaletteEntries(pal, start, ncolours, logpal->palPalEntry + start);

	HDC hdc = make_hdc();
	UnrealizeObject(pal);
	RealizePalette(hdc);
	free_hdc(hdc);
    }

    if (start <= OSC4_COLOUR_bg && OSC4_COLOUR_bg < start + ncolours) {
        /* If Default Background changes, we need to ensure any space between
         * the text area and the window border is redrawn. */
        InvalidateRect(wgs.term_hwnd, NULL, true);
    }
}

void write_aclip(int clipboard, char *data, int len, bool must_deselect)
{
    HGLOBAL clipdata;
    void *lock;

    if (clipboard != CLIP_SYSTEM)
        return;

    clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len + 1);
    if (!clipdata)
	return;
    lock = GlobalLock(clipdata);
    if (!lock)
	return;
    memcpy(lock, data, len);
    ((unsigned char *) lock)[len] = 0;
    GlobalUnlock(clipdata);

    if (!must_deselect)
        SendMessage(wgs.term_hwnd, WM_IGNORE_CLIP, true, 0);

    if (OpenClipboard(wgs.term_hwnd)) {
	EmptyClipboard();
	SetClipboardData(CF_TEXT, clipdata);
	CloseClipboard();
    } else
	GlobalFree(clipdata);

    if (!must_deselect)
        SendMessage(wgs.term_hwnd, WM_IGNORE_CLIP, false, 0);
}

typedef struct _rgbindex {
    int index;
    COLORREF ref;
} rgbindex;

int cmpCOLORREF(void *va, void *vb)
{
    COLORREF a = ((rgbindex *)va)->ref;
    COLORREF b = ((rgbindex *)vb)->ref;
    return (a < b) ? -1 : (a > b) ? +1 : 0;
}

/*
 * Note: unlike write_aclip() this will not append a nul.
 */
static void wintw_clip_write(
    TermWin *tw, int clipboard, wchar_t *data, int *attr,
    truecolour *truecolour, int len, bool must_deselect)
{
    HGLOBAL clipdata, clipdata2, clipdata3;
    int len2;
    void *lock, *lock2, *lock3;

    if (clipboard != CLIP_SYSTEM)
        return;

    len2 = WideCharToMultiByte(CP_ACP, 0, data, len, 0, 0, NULL, NULL);

    clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE,
			   len * sizeof(wchar_t));
    clipdata2 = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len2);

    if (!clipdata || !clipdata2) {
	if (clipdata)
	    GlobalFree(clipdata);
	if (clipdata2)
	    GlobalFree(clipdata2);
	return;
    }
    if (!(lock = GlobalLock(clipdata))) {
        GlobalFree(clipdata);
        GlobalFree(clipdata2);
	return;
    }
    if (!(lock2 = GlobalLock(clipdata2))) {
        GlobalUnlock(clipdata);
        GlobalFree(clipdata);
        GlobalFree(clipdata2);
	return;
    }

    memcpy(lock, data, len * sizeof(wchar_t));
    WideCharToMultiByte(CP_ACP, 0, data, len, lock2, len2, NULL, NULL);

    if (conf_get_bool(conf, CONF_rtf_paste)) {
	wchar_t unitab[256];
	strbuf *rtf = strbuf_new();
	unsigned char *tdata = (unsigned char *)lock2;
	wchar_t *udata = (wchar_t *)lock;
	int uindex = 0, tindex = 0;
	int multilen, blen, alen, totallen, i;
	char before[16], after[4];
	int fgcolour,  lastfgcolour  = -1;
	int bgcolour,  lastbgcolour  = -1;
	COLORREF fg,   lastfg = -1;
	COLORREF bg,   lastbg = -1;
	int attrBold,  lastAttrBold  = 0;
	int attrUnder, lastAttrUnder = 0;
        int palette[OSC4_NCOLOURS];
	int numcolours;
	tree234 *rgbtree = NULL;
	FontSpec *font = conf_get_fontspec(conf, CONF_font);

	get_unitab(CP_ACP, unitab, 0);

	strbuf_catf(
            rtf, "{\\rtf1\\ansi\\deff0{\\fonttbl\\f0\\fmodern %s;}\\f0\\fs%d",
            font->name, font->height*2);

	/*
	 * Add colour palette
	 * {\colortbl ;\red255\green0\blue0;\red0\green0\blue128;}
	 */

	/*
	 * First - Determine all colours in use
	 *    o  Foregound and background colours share the same palette
	 */
	if (attr) {
	    memset(palette, 0, sizeof(palette));
	    for (i = 0; i < (len-1); i++) {
		fgcolour = ((attr[i] & ATTR_FGMASK) >> ATTR_FGSHIFT);
		bgcolour = ((attr[i] & ATTR_BGMASK) >> ATTR_BGSHIFT);

		if (attr[i] & ATTR_REVERSE) {
		    int tmpcolour = fgcolour;	/* Swap foreground and background */
		    fgcolour = bgcolour;
		    bgcolour = tmpcolour;
		}

		if (bold_colours && (attr[i] & ATTR_BOLD)) {
		    if (fgcolour  <   8)	/* ANSI colours */
			fgcolour +=   8;
		    else if (fgcolour >= 256)	/* Default colours */
			fgcolour ++;
		}

		if ((attr[i] & ATTR_BLINK)) {
		    if (bgcolour  <   8)	/* ANSI colours */
			bgcolour +=   8;
    		    else if (bgcolour >= 256)	/* Default colours */
			bgcolour ++;
		}

		palette[fgcolour]++;
		palette[bgcolour]++;
	    }

	    if (truecolour) {
		rgbtree = newtree234(cmpCOLORREF);
		for (i = 0; i < (len-1); i++) {
		    if (truecolour[i].fg.enabled) {
			rgbindex *rgbp = snew(rgbindex);
			rgbp->ref = RGB(truecolour[i].fg.r,
			                truecolour[i].fg.g,
			                truecolour[i].fg.b);
			if (add234(rgbtree, rgbp) != rgbp)
			    sfree(rgbp);
		    }
		    if (truecolour[i].bg.enabled) {
			rgbindex *rgbp = snew(rgbindex);
			rgbp->ref = RGB(truecolour[i].bg.r,
			                truecolour[i].bg.g,
			                truecolour[i].bg.b);
			if (add234(rgbtree, rgbp) != rgbp)
			    sfree(rgbp);
		    }
		}
	    }

	    /*
	     * Next - Create a reduced palette
	     */
	    numcolours = 0;
            for (i = 0; i < OSC4_NCOLOURS; i++) {
		if (palette[i] != 0)
		    palette[i]  = ++numcolours;
	    }

	    if (rgbtree) {
		rgbindex *rgbp;
		for (i = 0; (rgbp = index234(rgbtree, i)) != NULL; i++)
		    rgbp->index = ++numcolours;
	    }

	    /*
	     * Finally - Write the colour table
	     */
	    put_datapl(rtf, PTRLEN_LITERAL("{\\colortbl ;"));

            for (i = 0; i < OSC4_NCOLOURS; i++) {
		if (palette[i] != 0) {
                    const PALETTEENTRY *pe = &logpal->palPalEntry[i];
                    strbuf_catf(rtf, "\\red%d\\green%d\\blue%d;",
                                pe->peRed, pe->peGreen, pe->peBlue);
		}
	    }
	    if (rgbtree) {
		rgbindex *rgbp;
		for (i = 0; (rgbp = index234(rgbtree, i)) != NULL; i++)
                    strbuf_catf(rtf, "\\red%d\\green%d\\blue%d;",
                                GetRValue(rgbp->ref), GetGValue(rgbp->ref),
                                GetBValue(rgbp->ref));
	    }
	    put_datapl(rtf, PTRLEN_LITERAL("}"));
	}

	/*
	 * We want to construct a piece of RTF that specifies the
	 * same Unicode text. To do this we will read back in
	 * parallel from the Unicode data in `udata' and the
	 * non-Unicode data in `tdata'. For each character in
	 * `tdata' which becomes the right thing in `udata' when
	 * looked up in `unitab', we just copy straight over from
	 * tdata. For each one that doesn't, we must WCToMB it
	 * individually and produce a \u escape sequence.
	 * 
	 * It would probably be more robust to just bite the bullet
	 * and WCToMB each individual Unicode character one by one,
	 * then MBToWC each one back to see if it was an accurate
	 * translation; but that strikes me as a horrifying number
	 * of Windows API calls so I want to see if this faster way
	 * will work. If it screws up badly we can always revert to
	 * the simple and slow way.
	 */
	while (tindex < len2 && uindex < len &&
	       tdata[tindex] && udata[uindex]) {
	    if (tindex + 1 < len2 &&
		tdata[tindex] == '\r' &&
		tdata[tindex+1] == '\n') {
		tindex++;
		uindex++;
            }

            /*
             * Set text attributes
             */
            if (attr) {
                /*
                 * Determine foreground and background colours
                 */
		if (truecolour && truecolour[tindex].fg.enabled) {
		    fgcolour = -1;
		    fg = RGB(truecolour[tindex].fg.r,
		             truecolour[tindex].fg.g,
		             truecolour[tindex].fg.b);
		} else {
		    fgcolour = ((attr[tindex] & ATTR_FGMASK) >> ATTR_FGSHIFT);
		    fg = -1;
		}

		if (truecolour && truecolour[tindex].bg.enabled) {
		    bgcolour = -1;
		    bg = RGB(truecolour[tindex].bg.r,
		             truecolour[tindex].bg.g,
		             truecolour[tindex].bg.b);
		} else {
		    bgcolour = ((attr[tindex] & ATTR_BGMASK) >> ATTR_BGSHIFT);
		    bg = -1;
		}

		if (attr[tindex] & ATTR_REVERSE) {
		    int tmpcolour = fgcolour;	    /* Swap foreground and background */
		    fgcolour = bgcolour;
		    bgcolour = tmpcolour;

		    COLORREF tmpref = fg;
		    fg = bg;
		    bg = tmpref;
		}

		if (bold_colours && (attr[tindex] & ATTR_BOLD) && (fgcolour >= 0)) {
		    if (fgcolour  <   8)	    /* ANSI colours */
			fgcolour +=   8;
		    else if (fgcolour >= 256)	    /* Default colours */
			fgcolour ++;
                }

		if ((attr[tindex] & ATTR_BLINK) && (bgcolour >= 0)) {
		    if (bgcolour  <   8)	    /* ANSI colours */
			bgcolour +=   8;
		    else if (bgcolour >= 256)	    /* Default colours */
			bgcolour ++;
                }

                /*
                 * Collect other attributes
                 */
		if (bold_font_mode != BOLD_NONE)
		    attrBold  = attr[tindex] & ATTR_BOLD;
		else
		    attrBold  = 0;
                
		attrUnder = attr[tindex] & ATTR_UNDER;

                /*
                 * Reverse video
		 *   o  If video isn't reversed, ignore colour attributes for default foregound
	         *	or background.
		 *   o  Special case where bolded text is displayed using the default foregound
		 *      and background colours - force to bolded RTF.
                 */
		if (!(attr[tindex] & ATTR_REVERSE)) {
		    if (bgcolour >= 256)	    /* Default color */
			bgcolour  = -1;		    /* No coloring */

		    if (fgcolour >= 256) {	    /* Default colour */
			if (bold_colours && (fgcolour & 1) && bgcolour == -1)
			    attrBold = ATTR_BOLD;   /* Emphasize text with bold attribute */

			fgcolour  = -1;		    /* No coloring */
		    }
		}

                /*
                 * Write RTF text attributes
                 */
		if ((lastfgcolour != fgcolour) || (lastfg != fg)) {
		    lastfgcolour  = fgcolour;
		    lastfg        = fg;
                    if (fg == -1) {
                        strbuf_catf(rtf, "\\cf%d ",
                                    (fgcolour >= 0) ? palette[fgcolour] : 0);
                    } else {
			rgbindex rgb, *rgbp;
			rgb.ref = fg;
			if ((rgbp = find234(rgbtree, &rgb, NULL)) != NULL)
			    strbuf_catf(rtf, "\\cf%d ", rgbp->index);
		    }
		}

		if ((lastbgcolour != bgcolour) || (lastbg != bg)) {
		    lastbgcolour  = bgcolour;
		    lastbg        = bg;
		    if (bg == -1)
			strbuf_catf(rtf, "\\highlight%d ",
                                    (bgcolour >= 0) ? palette[bgcolour] : 0);
		    else {
			rgbindex rgb, *rgbp;
			rgb.ref = bg;
			if ((rgbp = find234(rgbtree, &rgb, NULL)) != NULL)
			    strbuf_catf(rtf, "\\highlight%d ", rgbp->index);
		    }
		}

		if (lastAttrBold != attrBold) {
		    lastAttrBold  = attrBold;
		    put_datapl(rtf, attrBold ?
                               PTRLEN_LITERAL("\\b ") :
                               PTRLEN_LITERAL("\\b0 "));
		}

                if (lastAttrUnder != attrUnder) {
                    lastAttrUnder  = attrUnder;
		    put_datapl(rtf, attrUnder ?
                               PTRLEN_LITERAL("\\ul ") :
                               PTRLEN_LITERAL("\\ulnone "));
                }
	    }

	    if (unitab[tdata[tindex]] == udata[uindex]) {
		multilen = 1;
		before[0] = '\0';
		after[0] = '\0';
		blen = alen = 0;
	    } else {
		multilen = WideCharToMultiByte(CP_ACP, 0, unitab+uindex, 1,
					       NULL, 0, NULL, NULL);
		if (multilen != 1) {
		    blen = sprintf(before, "{\\uc%d\\u%d", (int)multilen,
				   (int)udata[uindex]);
		    alen = 1; strcpy(after, "}");
		} else {
                    blen = sprintf(before, "\\u%d", (int)udata[uindex]);
		    alen = 0; after[0] = '\0';
		}
	    }
	    assert(tindex + multilen <= len2);
	    totallen = blen + alen;
	    for (i = 0; i < multilen; i++) {
		if (tdata[tindex+i] == '\\' ||
		    tdata[tindex+i] == '{' ||
		    tdata[tindex+i] == '}')
		    totallen += 2;
		else if (tdata[tindex+i] == 0x0D || tdata[tindex+i] == 0x0A)
		    totallen += 6;     /* \par\r\n */
		else if (tdata[tindex+i] > 0x7E || tdata[tindex+i] < 0x20)
		    totallen += 4;
		else
		    totallen++;
	    }

	    put_data(rtf, before, blen);
	    for (i = 0; i < multilen; i++) {
		if (tdata[tindex+i] == '\\' ||
		    tdata[tindex+i] == '{' ||
		    tdata[tindex+i] == '}') {
                    put_byte(rtf, '\\');
                    put_byte(rtf, tdata[tindex+i]);
		} else if (tdata[tindex+i] == 0x0D || tdata[tindex+i] == 0x0A) {
                    put_datapl(rtf, PTRLEN_LITERAL("\\par\r\n"));
		} else if (tdata[tindex+i] > 0x7E || tdata[tindex+i] < 0x20) {
                    strbuf_catf(rtf, "\\'%02x", tdata[tindex+i]);
		} else {
                    put_byte(rtf, tdata[tindex+i]);
		}
	    }
	    put_data(rtf, after, alen);

	    tindex += multilen;
	    uindex++;
	}

        put_datapl(rtf, PTRLEN_LITERAL("}\0\0")); /* Terminate RTF stream */

	clipdata3 = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, rtf->len);
	if (clipdata3 && (lock3 = GlobalLock(clipdata3)) != NULL) {
	    memcpy(lock3, rtf->u, rtf->len);
	    GlobalUnlock(clipdata3);
	}
	strbuf_free(rtf);

	if (rgbtree) {
	    rgbindex *rgbp;
	    while ((rgbp = delpos234(rgbtree, 0)) != NULL)
		sfree(rgbp);
	    freetree234(rgbtree);
	}
    } else
	clipdata3 = NULL;

    GlobalUnlock(clipdata);
    GlobalUnlock(clipdata2);

    if (!must_deselect)
        SendMessage(wgs.term_hwnd, WM_IGNORE_CLIP, true, 0);

    if (OpenClipboard(wgs.term_hwnd)) {
	EmptyClipboard();
	SetClipboardData(CF_UNICODETEXT, clipdata);
	SetClipboardData(CF_TEXT, clipdata2);
	if (clipdata3)
	    SetClipboardData(RegisterClipboardFormat(CF_RTF), clipdata3);
	CloseClipboard();
    } else {
	GlobalFree(clipdata);
	GlobalFree(clipdata2);
    }

    if (!must_deselect)
        SendMessage(wgs.term_hwnd, WM_IGNORE_CLIP, false, 0);
}

static DWORD WINAPI clipboard_read_threadfunc(void *param)
{
    HWND hwnd = (HWND)param;
    HGLOBAL clipdata;

    if (OpenClipboard(NULL)) {
	if ((clipdata = GetClipboardData(CF_UNICODETEXT))) {
	    SendMessage(hwnd, WM_GOT_CLIPDATA,
                        (WPARAM)true, (LPARAM)clipdata);
	} else if ((clipdata = GetClipboardData(CF_TEXT))) {
	    SendMessage(hwnd, WM_GOT_CLIPDATA,
                        (WPARAM)false, (LPARAM)clipdata);
	}
	CloseClipboard();
    }

    return 0;
}

static void process_clipdata(HGLOBAL clipdata, bool unicode)
{
    wchar_t *clipboard_contents = NULL;
    size_t clipboard_length = 0;

    if (unicode) {
	wchar_t *p = GlobalLock(clipdata);
	wchar_t *p2;

	if (p) {
	    /* Unwilling to rely on Windows having wcslen() */
	    for (p2 = p; *p2; p2++);
	    clipboard_length = p2 - p;
	    clipboard_contents = snewn(clipboard_length + 1, wchar_t);
	    memcpy(clipboard_contents, p, clipboard_length * sizeof(wchar_t));
	    clipboard_contents[clipboard_length] = L'\0';
#ifdef MOD_PERSO
		if( (GetPasteSize()==0) || (clipboard_length<=GetPasteSize()) || MessageBox(NULL,"Clipboard content is very large.\nAre you sure ?","Confirmation",MB_YESNO|MB_ICONWARNING)==IDYES )
#endif
	    term_do_paste(term, clipboard_contents, clipboard_length);
	}
    } else {
	char *s = GlobalLock(clipdata);
	int i;

	if (s) {
	    i = MultiByteToWideChar(CP_ACP, 0, s, strlen(s) + 1, 0, 0);
	    clipboard_contents = snewn(i, wchar_t);
	    MultiByteToWideChar(CP_ACP, 0, s, strlen(s) + 1,
				clipboard_contents, i);
	    clipboard_length = i - 1;
	    clipboard_contents[clipboard_length] = L'\0';
#ifdef MOD_PERSO
		if( (GetPasteSize()==0) || (clipboard_length<=GetPasteSize()) || MessageBox(NULL,"Clipboard content is very large.\nAre you sure ?","Confirmation",MB_YESNO|MB_ICONWARNING)==IDYES )
#endif
	    term_do_paste(term, clipboard_contents, clipboard_length);
	}
    }

    sfree(clipboard_contents);
}

static void wintw_clip_request_paste(TermWin *tw, int clipboard)
{
    assert(clipboard == CLIP_SYSTEM);

    /*
     * I always thought pasting was synchronous in Windows; the
     * clipboard access functions certainly _look_ synchronous,
     * unlike the X ones. But in fact it seems that in some
     * situations the contents of the clipboard might not be
     * immediately available, and the clipboard-reading functions
     * may block. This leads to trouble if the application
     * delivering the clipboard data has to get hold of it by -
     * for example - talking over a network connection which is
     * forwarded through this very PuTTY.
     *
     * Hence, we spawn a subthread to read the clipboard, and do
     * our paste when it's finished. The thread will send a
     * message back to our main window when it terminates, and
     * that tells us it's OK to paste.
     */
    DWORD in_threadid; /* required for Win9x */
    HANDLE hThread = CreateThread(NULL, 0, clipboard_read_threadfunc,
                 wgs.term_hwnd, 0, &in_threadid);
    if (hThread)
        CloseHandle(hThread);          /* we don't need the thread handle */
}

/*
 * Print a modal (Really Bad) message box and perform a fatal exit.
 */
void modalfatalbox(const char *fmt, ...)
{
    va_list ap;
    char *message, *title;

    va_start(ap, fmt);
    message = dupvprintf(fmt, ap);
    va_end(ap);
    show_mouseptr(true);
    title = dupprintf("%s Fatal Error", appname);
    MessageBox(wgs.term_hwnd, message, title,
               MB_SYSTEMMODAL | MB_ICONERROR | MB_OK);
    sfree(message);
    sfree(title);
    cleanup_exit(1);
}

/*
 * Print a message box and don't close the connection.
 */
void nonfatal(const char *fmt, ...)
{
    va_list ap;
    char *message, *title;

    va_start(ap, fmt);
    message = dupvprintf(fmt, ap);
    va_end(ap);
    show_mouseptr(true);
    title = dupprintf("%s Error", appname);
    MessageBox(wgs.term_hwnd, message, title, MB_ICONERROR | MB_OK);
    sfree(message);
    sfree(title);
}

static bool flash_window_ex(DWORD dwFlags, UINT uCount, DWORD dwTimeout)
{
    if (p_FlashWindowEx) {
	FLASHWINFO fi;
	fi.cbSize = sizeof(fi);
        fi.hwnd = wgs.term_hwnd;
	fi.dwFlags = dwFlags;
	fi.uCount = uCount;
	fi.dwTimeout = dwTimeout;
	return (*p_FlashWindowEx)(&fi);
    }
    else
	return false; /* shrug */
}

static void flash_window(int mode);
static long next_flash;
static bool flashing = false;

/*
 * Timer for platforms where we must maintain window flashing manually
 * (e.g., Win95).
 */
static void flash_window_timer(void *ctx, unsigned long now)
{
    if (flashing && now == next_flash) {
	flash_window(1);
    }
}

/*
 * Manage window caption / taskbar flashing, if enabled.
 * 0 = stop, 1 = maintain, 2 = start
 */
static void flash_window(int mode)
{
#ifdef MOD_PERSO
	static int BlinkingState = 0 ;
	if(MaxBlinkingTime && (mode!=0)) {
		if( BlinkingState>=MaxBlinkingTime ) {
			BlinkingState = 0 ;
			flash_window(0);
			return ; 
		} else {
			BlinkingState++ ;
		}
	}
	if( mode==0 ) { BlinkingState=0 ; }
#endif
    int beep_ind = conf_get_int(conf, CONF_beep_ind);
    if ((mode == 0) || (beep_ind == B_IND_DISABLED)) {
	/* stop */
	if (flashing) {
	    flashing = false;
	    if (p_FlashWindowEx)
		flash_window_ex(FLASHW_STOP, 0, 0);
	    else
                FlashWindow(wgs.term_hwnd, false);
	}

    } else if (mode == 2) {
	/* start */
	if (!flashing) {
	    flashing = true;
	    if (p_FlashWindowEx) {
		/* For so-called "steady" mode, we use uCount=2, which
		 * seems to be the traditional number of flashes used
		 * by user notifications (e.g., by Explorer).
		 * uCount=0 appears to enable continuous flashing, per
		 * "flashing" mode, although I haven't seen this
		 * documented. */
#ifdef MOD_PERSO
		flash_window_ex(FLASHW_ALL | FLASHW_TIMER,
				MaxBlinkingTime,
				0 /* system cursor blink rate */);
#else
		flash_window_ex(FLASHW_ALL | FLASHW_TIMER,
				(beep_ind == B_IND_FLASH ? 0 : 2),
				0 /* system cursor blink rate */);
#endif
		/* No need to schedule timer */
	    } else {
                FlashWindow(wgs.term_hwnd, true);
                next_flash = schedule_timer(450, flash_window_timer,
                                            wgs.term_hwnd);
	    }
	}

    } else if ((mode == 1) && (beep_ind == B_IND_FLASH)) {
	/* maintain */
	if (flashing && !p_FlashWindowEx) {
            FlashWindow(wgs.term_hwnd, true);    /* toggle */
            next_flash = schedule_timer(450, flash_window_timer,
                                        wgs.term_hwnd);
	}
    }
}

/*
 * Beep.
 */
static void wintw_bell(TermWin *tw, int mode)
{
    if (mode == BELL_DEFAULT) {
	/*
	 * For MessageBeep style bells, we want to be careful of
	 * timing, because they don't have the nice property of
	 * PlaySound bells that each one cancels the previous
	 * active one. So we limit the rate to one per 50ms or so.
	 */
	static long lastbeep = 0;
	long beepdiff;

	beepdiff = GetTickCount() - lastbeep;
	if (beepdiff >= 0 && beepdiff < 50)
	    return;
	MessageBeep(MB_OK);
	/*
	 * The above MessageBeep call takes time, so we record the
	 * time _after_ it finishes rather than before it starts.
	 */
	lastbeep = GetTickCount();
    } else if (mode == BELL_WAVEFILE) {
	Filename *bell_wavefile = conf_get_filename(conf, CONF_bell_wavefile);
	if (!p_PlaySound || !p_PlaySound(bell_wavefile->path, NULL,
                         SND_ASYNC | SND_FILENAME)) {
            char *buf, *otherbuf;
            show_mouseptr(true);
            buf = dupprintf(
                "Unable to play sound file\n%s\nUsing default sound instead",
                bell_wavefile->path);
            otherbuf = dupprintf("%s Sound Error", appname);
            MessageBox(wgs.term_hwnd, buf, otherbuf,
                       MB_OK | MB_ICONEXCLAMATION);
            sfree(buf);
            sfree(otherbuf);
	    conf_set_int(conf, CONF_beep, BELL_DEFAULT);
	}
    } else if (mode == BELL_PCSPEAKER) {
	static long lastbeep = 0;
	long beepdiff;

	beepdiff = GetTickCount() - lastbeep;
	if (beepdiff >= 0 && beepdiff < 50)
	    return;

	/*
	 * We must beep in different ways depending on whether this
	 * is a 95-series or NT-series OS.
	 */
	if (osPlatformId == VER_PLATFORM_WIN32_NT)
	    Beep(800, 100);
	else
	    MessageBeep(-1);
	lastbeep = GetTickCount();
    }
    /* Otherwise, either visual bell or disabled; do nothing here */
    if (!term->has_focus) {
#ifdef MOD_PERSO
	if( GetVisibleFlag()!=VISIBLE_TRAY ) {
		if(conf_get_bool(conf,CONF_foreground_on_bell) ) {		// Tester avec   sleep 4 ; echo -e '\a'
			if( IsIconic(wgs.term_hwnd) ) SwitchToThisWindow( wgs.term_hwnd, TRUE ) ; 
			else SetForegroundWindow( wgs.term_hwnd ) ;
		} else { 
			if( mode == BELL_VISUAL ) {
				if( IsIconic(wgs.term_hwnd) ) 
					FlashWindow(wgs.term_hwnd, TRUE) ;
				else 
					flash_window(2) ; 
			} else { 
				if( conf_get_int(conf,CONF_beep_ind)==B_IND_FLASH ) flash_window(2) ; 
			}
		}
	} else if( GetVisibleFlag()==VISIBLE_TRAY ) {
		if( conf_get_bool(conf,CONF_foreground_on_bell) ) {
			SendMessage( wgs.term_hwnd, WM_COMMAND, IDM_FROMTRAY, 0 ) ;
		} else if(mode == BELL_VISUAL)
			SetTimer(wgs.term_hwnd, TIMER_BLINKTRAYICON, (int)500, NULL) ;
		//SendMessage( MainHwnd, WM_COMMAND, IDM_FROMTRAY, 0 );
		//flash_window(2);	       /* start */
	    	//ShowWindow( MainHwnd, SW_MINIMIZE);
	} else
#endif
	flash_window(2);	       /* start */
    }
}

/*
 * Minimise or restore the window in response to a server-side
 * request.
 */
static void wintw_set_minimised(TermWin *tw, bool minimised)
{
    if (IsIconic(wgs.term_hwnd)) {
	if (!minimised)
            ShowWindow(wgs.term_hwnd, SW_RESTORE);
    } else {
	if (minimised)
            ShowWindow(wgs.term_hwnd, SW_MINIMIZE);
    }
}

/*
 * Move the window in response to a server-side request.
 */
static void wintw_move(TermWin *tw, int x, int y)
{
    int resize_action = conf_get_int(conf, CONF_resize_action);
    if (resize_action == RESIZE_DISABLED || 
	resize_action == RESIZE_FONT ||
        IsZoomed(wgs.term_hwnd))
       return;

    SetWindowPos(wgs.term_hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
static void wintw_set_zorder(TermWin *tw, bool top)
{
    if (conf_get_bool(conf, CONF_alwaysontop))
	return;			       /* ignore */
    SetWindowPos(wgs.term_hwnd, top ? HWND_TOP : HWND_BOTTOM, 0, 0, 0, 0,
		 SWP_NOMOVE | SWP_NOSIZE);
}

/*
 * Refresh the window in response to a server-side request.
 */
static void wintw_refresh(TermWin *tw)
{
    InvalidateRect(wgs.term_hwnd, NULL, true);
}

/*
 * Maximise or restore the window in response to a server-side
 * request.
 */
static void wintw_set_maximised(TermWin *tw, bool maximised)
{
    if (IsZoomed(wgs.term_hwnd)) {
        if (!maximised)
            ShowWindow(wgs.term_hwnd, SW_RESTORE);
    } else {
	if (maximised)
            ShowWindow(wgs.term_hwnd, SW_MAXIMIZE);
    }
}

/*
 * See if we're in full-screen mode.
 */
static bool is_full_screen()
{
    if (!IsZoomed(wgs.term_hwnd))
	return false;
    if (GetWindowLongPtr(wgs.term_hwnd, GWL_STYLE) & WS_CAPTION)
	return false;
    return true;
}

/* Get the rect/size of a full screen window using the nearest available
 * monitor in multimon systems; default to something sensible if only
 * one monitor is present. */
static bool get_fullscreen_rect(RECT * ss)
{
#if defined(MONITOR_DEFAULTTONEAREST) && !defined(NO_MULTIMON)
	HMONITOR mon;
	MONITORINFO mi;
        mon = MonitorFromWindow(wgs.term_hwnd, MONITOR_DEFAULTTONEAREST);
	mi.cbSize = sizeof(mi);
	GetMonitorInfo(mon, &mi);

	/* structure copy */
	*ss = mi.rcMonitor;
	return true;
#else
/* could also use code like this:
	ss->left = ss->top = 0;
	ss->right = GetSystemMetrics(SM_CXSCREEN);
	ss->bottom = GetSystemMetrics(SM_CYSCREEN);
*/ 
	return GetClientRect(GetDesktopWindow(), ss);
#endif
}


/*
 * Go full-screen. This should only be called when we are already
 * maximised.
 */
static void make_full_screen()
{
    DWORD style;
	RECT ss;

    assert(IsZoomed(wgs.term_hwnd));

	if (is_full_screen())
		return;
	
    /* Remove the window furniture. */
    style = GetWindowLongPtr(wgs.term_hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME);
    if (conf_get_bool(conf, CONF_scrollbar_in_fullscreen))
	style |= WS_VSCROLL;
    else
	style &= ~WS_VSCROLL;
    SetWindowLongPtr(wgs.term_hwnd, GWL_STYLE, style);

    /* Resize ourselves to exactly cover the nearest monitor. */
	get_fullscreen_rect(&ss);
    SetWindowPos(wgs.term_hwnd, HWND_TOP, ss.left, ss.top,
                 ss.right - ss.left, ss.bottom - ss.top, SWP_FRAMECHANGED);

    /* We may have changed size as a result */

    reset_window(0);

    /* Tick the menu item in the System and context menus. */
    {
	int i;
	for (i = 0; i < lenof(popup_menus); i++)
	    CheckMenuItem(popup_menus[i].menu, IDM_FULLSCREEN, MF_CHECKED);
    }
}

/*
 * Clear the full-screen attributes.
 */
static void clear_full_screen()
{
    DWORD oldstyle, style;

    /* Reinstate the window furniture. */
    style = oldstyle = GetWindowLongPtr(wgs.term_hwnd, GWL_STYLE);
    style |= WS_CAPTION | WS_BORDER;
    if (conf_get_int(conf, CONF_resize_action) == RESIZE_DISABLED)
        style &= ~WS_THICKFRAME;
    else
        style |= WS_THICKFRAME;
    if (conf_get_bool(conf, CONF_scrollbar))
	style |= WS_VSCROLL;
    else
	style &= ~WS_VSCROLL;
    if (style != oldstyle) {
        SetWindowLongPtr(wgs.term_hwnd, GWL_STYLE, style);
        SetWindowPos(wgs.term_hwnd, NULL, 0, 0, 0, 0,
		     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
		     SWP_FRAMECHANGED);
    }

    /* Untick the menu item in the System and context menus. */
    {
	int i;
	for (i = 0; i < lenof(popup_menus); i++)
	    CheckMenuItem(popup_menus[i].menu, IDM_FULLSCREEN, MF_UNCHECKED);
    }
}

/*
 * Toggle full-screen mode.
 */
static void flip_full_screen()
{
    if (is_full_screen()) {
        ShowWindow(wgs.term_hwnd, SW_RESTORE);
    } else if (IsZoomed(wgs.term_hwnd)) {
	make_full_screen();
    } else {
        SendMessage(wgs.term_hwnd, WM_FULLSCR_ON_MAX, 0, 0);
        ShowWindow(wgs.term_hwnd, SW_MAXIMIZE);
    }
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
	if( GetBackgroundImageFlag()&&(!PuttyFlag)&&(conf_get_int(conf,CONF_bg_image_abs_fixed)==1)&&(conf_get_int(conf,CONF_bg_type)!=0) ) RefreshBackground( wgs.term_hwnd ) ;
#endif
}

#ifdef MOD_PERSO
/* rutty: special entry point for ldisc.c - local backend
 modified version for all other backends
 */
#ifdef MOD_RUTTY
size_t win_seat_output_local(Seat *seat, bool is_stderr, const void *data, size_t len)
{
	int res = term_data(term, is_stderr, data, len) ;
	if( (!GetPuttyFlag()) && (ScriptFileContent!=NULL) ) ManageInitScript( data, len ) ;
	return res ;
}

static size_t win_seat_output(Seat *seat, bool is_stderr, const void *data, size_t len)
{
	if( (!GetPuttyFlag()) && (GetRuttyFlag()) ) script_remote(&scriptdata, data, len);
	int res = term_data(term, is_stderr, data, len) ;
	if( (!GetPuttyFlag()) && (ScriptFileContent!=NULL) ) ManageInitScript( data, len ) ;
	return res ;
}
#else
/*   Version avec MOD_PERSO seule sans MOD_RUTTY */
static size_t win_seat_output(Seat *seat, bool is_stderr, const void *data, size_t len)
{
	int res = term_data(term, is_stderr, data, len) ;
	if( (!GetPuttyFlag()) && (ScriptFileContent!=NULL) ) ManageInitScript( data, len ) ;
	return res ;
}
#endif
#else
static size_t win_seat_output(Seat *seat, bool is_stderr,
                              const void *data, size_t len)
{
    return term_data(term, is_stderr, data, len);
}
#endif

static bool win_seat_eof(Seat *seat)
{
    return true;   /* do respond to incoming EOF with outgoing */
}

static int win_seat_get_userpass_input(
    Seat *seat, prompts_t *p, bufchain *input)
{
    int ret;
    ret = cmdline_get_passwd_input(p);
    if (ret == -1)
	ret = term_get_userpass_input(term, p, input);
    return ret;
}

static bool win_seat_set_trust_status(Seat *seat, bool trusted)
{
    term_set_trust_status(term, trusted);
    return true;
}

static bool win_seat_get_cursor_position(Seat *seat, int *x, int *y)
{
    term_get_cursor_position(term, x, y);
    return true;
}

static bool win_seat_get_window_pixel_size(Seat *seat, int *x, int *y)
{
    RECT r;
    GetWindowRect(wgs.term_hwnd, &r);
    *x = r.right - r.left;
    *y = r.bottom - r.top;
    return true;
}

#include "../../kitty_light.c"
