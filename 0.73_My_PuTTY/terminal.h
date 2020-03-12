/*
 * Internals of the Terminal structure, for those other modules
 * which need to look inside it. It would be nice if this could be
 * folded back into terminal.c in future, with an abstraction layer
 * to handle everything that other modules need to know about it;
 * but for the moment, this will do.
 */

#ifndef PUTTY_TERMINAL_H
#define PUTTY_TERMINAL_H

#include "tree234.h"

struct beeptime {
    struct beeptime *next;
    unsigned long ticks;
};

#define TRUST_SIGIL_WIDTH 3
#define TRUST_SIGIL_CHAR 0xDFFE

typedef struct {
    int y, x;
} pos;

typedef struct termchar termchar;
typedef struct termline termline;

struct termchar {
    /*
     * Any code in terminal.c which definitely needs to be changed
     * when extra fields are added here is labelled with a comment
     * saying FULL-TERMCHAR.
     */
    unsigned long chr;
    unsigned long attr;
    truecolour truecolour;

    /*
     * The cc_next field is used to link multiple termchars
     * together into a list, so as to fit more than one character
     * into a character cell (Unicode combining characters).
     * 
     * cc_next is a relative offset into the current array of
     * termchars. I.e. to advance to the next character in a list,
     * one does `tc += tc->next'.
     * 
     * Zero means end of list.
     */
    int cc_next;
};

struct termline {
    unsigned short lattr;
    int cols;			       /* number of real columns on the line */
    int size;			       /* number of allocated termchars
					* (cc-lists may make this > cols) */
    bool temporary;                    /* true if decompressed from scrollback */
    int cc_free;		       /* offset to first cc in free list */
    struct termchar *chars;
    bool trusted;
};

struct bidi_cache_entry {
    int width;
    bool trusted;
    struct termchar *chars;
    int *forward, *backward;	       /* the permutations of line positions */
};

struct term_utf8_decode {
    int state;                         /* Is there a pending UTF-8 character */
    int chr;                           /* and what is it so far? */
    int size;                          /* The size of the UTF character. */
};

struct terminal_tag {

    int compatibility_level;

    tree234 *scrollback;	       /* lines scrolled off top of screen */
    tree234 *screen;		       /* lines on primary screen */
    tree234 *alt_screen;	       /* lines on alternate screen */
    int disptop;		       /* distance scrolled back (0 or -ve) */
    int tempsblines;		       /* number of lines of .scrollback that
					  can be retrieved onto the terminal
					  ("temporary scrollback") */

    termline **disptext;	       /* buffer of text on real screen */
    int dispcursx, dispcursy;	       /* location of cursor on real screen */
    int curstype;		       /* type of cursor on real screen */

#define VBELL_TIMEOUT (TICKSPERSEC/10) /* visual bell lasts 1/10 sec */

    struct beeptime *beephead, *beeptail;
    int nbeeps;
    bool beep_overloaded;
    long lastbeep;

#define TTYPE termchar
#define TSIZE (sizeof(TTYPE))

    int default_attr, curr_attr, save_attr;
    truecolour curr_truecolour, save_truecolour;
    termchar basic_erase_char, erase_char;

    bufchain inbuf;		       /* terminal input buffer */

    pos curs;			       /* cursor */
    pos savecurs;		       /* saved cursor position */
    int marg_t, marg_b;		       /* scroll margins */
    bool dec_om;                       /* DEC origin mode flag */
    bool wrap, wrapnext;               /* wrap flags */
    bool insert;                       /* insert-mode flag */
    int cset;			       /* 0 or 1: which char set */
    int save_cset, save_csattr;	       /* saved with cursor position */
    bool save_utf, save_wnext;         /* saved with cursor position */
    bool rvideo;                       /* global reverse video flag */
    unsigned long rvbell_startpoint;   /* for ESC[?5hESC[?5l vbell */
    bool cursor_on;                    /* cursor enabled flag */
    bool reset_132;                    /* Flag ESC c resets to 80 cols */
    bool use_bce;                      /* Use Background coloured erase */
    bool cblinker;                     /* When blinking is the cursor on ? */
    bool tblinker;                     /* When the blinking text is on */
    bool blink_is_real;                /* Actually blink blinking text */
    int sco_acs, save_sco_acs;	       /* CSI 10,11,12m -> OEM charset */
    bool vt52_bold;                    /* Force bold on non-bold colours */
    bool utf;                          /* Are we in toggleable UTF-8 mode? */
    term_utf8_decode utf8;             /* If so, here's our decoding state */
    bool printing, only_printing;      /* Are we doing ANSI printing? */
    int print_state;		       /* state of print-end-sequence scan */
    bufchain printer_buf;	       /* buffered data for printer */
    printer_job *print_job;

    /* ESC 7 saved state for the alternate screen */
    pos alt_savecurs;
    int alt_save_attr;
    truecolour alt_save_truecolour;
    int alt_save_cset, alt_save_csattr;
    bool alt_save_utf;
    bool alt_save_wnext;
    int alt_save_sco_acs;

    int rows, cols, savelines;
    bool has_focus;
    bool in_vbell;
    long vbell_end;
    bool app_cursor_keys, app_keypad_keys, vt52_mode;
    bool repeat_off, srm_echo, cr_lf_return;
    bool seen_disp_event;
    bool big_cursor;

    int xterm_mouse_mode;              /* mouse event mode */
    int xterm_mouse_protocol;          /* mouse protocol */
    int mouse_is_down;                 /* used while tracking mouse buttons */

    bool bracketed_paste, bracketed_paste_active;

    int cset_attr[2];

/*
 * Saved settings on the alternate screen.
 */
    int alt_x, alt_y;
    bool alt_wnext, alt_ins;
    bool alt_om, alt_wrap;
    int alt_cset, alt_sco_acs;
    bool alt_utf;
    int alt_t, alt_b;
    int alt_which;
    int alt_sblines; /* # of lines on alternate screen that should be used for scrollback. */

#define ARGS_MAX 32		       /* max # of esc sequence arguments */
#define ARG_DEFAULT 0		       /* if an arg isn't specified */
#define def(a,d) ( (a) == ARG_DEFAULT ? (d) : (a) )
    unsigned esc_args[ARGS_MAX];
    int esc_nargs;
    int esc_query;
#define ANSI(x,y)	((x)+((y)*256))
#define ANSI_QUE(x)	ANSI(x,1)

#define OSC_STR_MAX 2048
    int osc_strlen;
    char osc_string[OSC_STR_MAX + 1];
    bool osc_w;

    char id_string[1024];

    unsigned char *tabs;

    enum {
	TOPLEVEL,
	SEEN_ESC,
	SEEN_CSI,
	SEEN_OSC,
	SEEN_OSC_W,

	DO_CTRLS,

	SEEN_OSC_P,
	OSC_STRING, OSC_MAYBE_ST,
	VT52_ESC,
	VT52_Y1,
	VT52_Y2,
	VT52_FG,
	VT52_BG
    } termstate;

    enum {
	NO_SELECTION, ABOUT_TO, DRAGGING, SELECTED
    } selstate;
    enum {
	LEXICOGRAPHIC, RECTANGULAR
    } seltype;
    enum {
	SM_CHAR, SM_WORD, SM_LINE
    } selmode;
    pos selstart, selend, selanchor;

    short wordness[256];

    /* Mask of attributes to pay attention to when painting. */
    int attr_mask;

    wchar_t *paste_buffer;
    int paste_len, paste_pos;

    Backend *backend;

    Ldisc *ldisc;

    TermWin *win;

    LogContext *logctx;

    struct unicode_data *ucsdata;

    unsigned long last_graphic_char;

#ifdef MOD_ZMODEM
    int xyz_transfering;
    struct zModemInternals *xyz_Internals;
#endif
    /*
     * We maintain a full copy of a Conf here, not merely a pointer
     * to it. That way, when we're passed a new one for
     * reconfiguration, we can check the differences and adjust the
     * _current_ setting of (e.g.) auto wrap mode rather than only
     * the default.
     */
    Conf *conf;

    /*
     * GUI implementations of seat_output call term_out, but it can
     * also be called from the ldisc if the ldisc is called _within_
     * term_out. So we have to guard against re-entrancy - if
     * seat_output is called recursively like this, it will simply add
     * data to the end of the buffer term_out is in the process of
     * working through.
     */
    bool in_term_out;

    /*
     * We schedule a window update shortly after receiving terminal
     * data. This tracks whether one is currently pending.
     */
    bool window_update_pending;
    long next_update;

    /*
     * Track pending blinks and tblinks.
     */
    bool tblink_pending, cblink_pending;
    long next_tblink, next_cblink;

    /*
     * These are buffers used by the bidi and Arabic shaping code.
     */
    termchar *ltemp;
    int ltemp_size;
    bidi_char *wcFrom, *wcTo;
    int wcFromTo_size;
    struct bidi_cache_entry *pre_bidi_cache, *post_bidi_cache;
    size_t bidi_cache_size;

    /*
     * Current trust state, used to annotate every line of the
     * terminal that a graphic character is output to.
     */
    bool trusted;

    /*
     * We copy a bunch of stuff out of the Conf structure into local
     * fields in the Terminal structure, to avoid the repeated
     * tree234 lookups which would be involved in fetching them from
     * the former every time.
     */
    bool ansi_colour;
    char *answerback;
    int answerbacklen;
    bool no_arabicshaping;
    int beep;
    bool bellovl;
    int bellovl_n;
    int bellovl_s;
    int bellovl_t;
    bool no_bidi;
    bool bksp_is_delete;
    bool blink_cur;
    bool blinktext;
    bool cjk_ambig_wide;
    int conf_height;
    int conf_width;
    bool crhaslf;
    bool erase_to_scrollback;
    int funky_type;
    bool lfhascr;
    bool logflush;
    int logtype;
    bool mouse_override;
    bool nethack_keypad;
    bool no_alt_screen;
    bool no_applic_c;
    bool no_applic_k;
    bool no_dbackspace;
    bool no_mouse_rep;
    bool no_remote_charset;
    bool no_remote_resize;
    bool no_remote_wintitle;
    bool no_remote_clearscroll;
    bool rawcnp;
    bool utf8linedraw;
    bool rect_select;
    int remote_qtitle_action;
#ifdef MOD_PERSO
    int enter_sends_crlf;
    int rxvt_homeend;
#else
    bool rxvt_homeend;
#endif
    bool scroll_on_disp;
    bool scroll_on_key;
    bool xterm_256_colour;
    bool true_colour;

    wchar_t *last_selected_text;
    int *last_selected_attr;
    truecolour *last_selected_tc;
    size_t last_selected_len;
    int mouse_select_clipboards[N_CLIPBOARDS];
    int n_mouse_select_clipboards;
    int mouse_paste_clipboard;
#ifdef MOD_HYPERLINK
 	/*
	 * HACK: PuttyTray / Nutty
	 */
	int url_update;
#endif
};

static inline bool in_utf(Terminal *term)
{
    return term->utf || term->ucsdata->line_codepage == CP_UTF8;
}

unsigned long term_translate(
    Terminal *term, term_utf8_decode *utf8, unsigned char c);
static inline int term_char_width(Terminal *term, unsigned int c)
{
    return term->cjk_ambig_wide ? mk_wcwidth_cjk(c) : mk_wcwidth(c);
}

/*
 * UCSINCOMPLETE is returned from term_translate if it's successfully
 * absorbed a byte but not emitted a complete character yet.
 * UCSTRUNCATED indicates a truncated multibyte sequence (so the
 * caller emits an error character and then calls term_translate again
 * with the same input byte). UCSINVALID indicates some other invalid
 * multibyte sequence, such as an overlong synonym, or a standalone
 * continuation byte, or a completely illegal thing like 0xFE. These
 * values are not stored in the terminal data structures at all.
 */
#define UCSINCOMPLETE 0x8000003FU    /* '?' */
#define UCSTRUNCATED  0x80000021U    /* '!' */
#define UCSINVALID    0x8000002AU    /* '*' */

/*
 * Maximum number of combining characters we're willing to store in a
 * character cell. Our linked-list data representation permits an
 * unlimited number of these in principle, but if we allowed that in
 * practice then it would be an easy DoS to just squirt a squillion
 * identical combining characters to someone's terminal and cause
 * their PuTTY or pterm to consume lots of memory and CPU pointlessly.
 *
 * The precise figure of 32 is more or less arbitrary, but one point
 * supporting it is UAX #15's comment that 30 combining characters is
 * "significantly beyond what is required for any linguistic or
 * technical usage".
 */
#define CC_LIMIT 32

#endif
