/*
 * osxwin.m: code to manage a session window in Mac OS X PuTTY.
 */

#import <Cocoa/Cocoa.h>
#include "putty.h"
#include "terminal.h"
#include "osxclass.h"

/* Colours come in two flavours: configurable, and xterm-extended. */
#define NCFGCOLOURS (lenof(((Config *)0)->colours))
#define NEXTCOLOURS 240 /* 216 colour-cube plus 24 shades of grey */
#define NALLCOLOURS (NCFGCOLOURS + NEXTCOLOURS)

/*
 * The key component of the per-session data is the SessionWindow
 * class. A pointer to this is used as the frontend handle, to be
 * passed to all the platform-independent subsystems that require
 * one.
 */

@interface TerminalView : NSImageView
{
    NSFont *font;
    NSImage *image;
    Terminal *term;
    Config cfg;
    NSColor *colours[NALLCOLOURS];
    float fw, fasc, fdesc, fh;
}
- (void)drawStartFinish:(BOOL)start;
- (void)setColour:(int)n r:(float)r g:(float)g b:(float)b;
- (void)doText:(wchar_t *)text len:(int)len x:(int)x y:(int)y
    attr:(unsigned long)attr lattr:(int)lattr;
@end

@implementation TerminalView
- (BOOL)isFlipped
{
    return YES;
}
- (id)initWithTerminal:(Terminal *)aTerm config:(Config)aCfg
{
    float w, h;

    self = [self initWithFrame:NSMakeRect(0,0,100,100)];

    term = aTerm;
    cfg = aCfg;

    /*
     * Initialise the fonts we're going to use.
     * 
     * FIXME: for the moment I'm sticking with exactly one default font.
     */
    font = [NSFont userFixedPitchFontOfSize:0];

    /*
     * Now determine the size of the primary font.
     * 
     * FIXME: If we have multiple fonts, we may need to set fasc
     * and fdesc to the _maximum_ asc and desc out of all the
     * fonts, _before_ adding them together to get fh.
     */
    fw = [font widthOfString:@"A"];
    fasc = [font ascender];
    fdesc = -[font descender];
    fh = fasc + fdesc;
    fh = (int)fh + (fh > (int)fh);     /* round up, ickily */

    /*
     * Use this to figure out the size of the terminal view.
     */
    w = fw * term->cols;
    h = fh * term->rows;

    /*
     * And set our size and subimage.
     */
    image = [[NSImage alloc] initWithSize:NSMakeSize(w,h)];
    [image setFlipped:YES];
    [self setImage:image];
    [self setFrame:NSMakeRect(0,0,w,h)];

    term_invalidate(term);

    return self;
}
- (void)drawStartFinish:(BOOL)start
{
    if (start)
	[image lockFocus];
    else
	[image unlockFocus];
}
- (void)doText:(wchar_t *)text len:(int)len x:(int)x y:(int)y
    attr:(unsigned long)attr lattr:(int)lattr
{
    int nfg, nbg, rlen, widefactor;
    float ox, oy, tw, th;
    NSDictionary *attrdict;

    /* FIXME: TATTR_COMBINING */

    nfg = ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT);
    nbg = ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT);
    if (attr & ATTR_REVERSE) {
	int t = nfg;
	nfg = nbg;
	nbg = t;
    }
    if ((cfg.bold_style & 2) && (attr & ATTR_BOLD)) {
	if (nfg < 16) nfg |= 8;
	else if (nfg >= 256) nfg |= 1;
    }
    if ((cfg.bold_style & 2) && (attr & ATTR_BLINK)) {
	if (nbg < 16) nbg |= 8;
	else if (nbg >= 256) nbg |= 1;
    }
    if (attr & TATTR_ACTCURS) {
	nfg = 260;
	nbg = 261;
    }

    if (attr & ATTR_WIDE) {
	widefactor = 2;
	/* FIXME: what do we actually have to do about wide characters? */
    } else {
	widefactor = 1;
    }

    /* FIXME: ATTR_BOLD if cfg.bold_style & 1 */

    if ((lattr & LATTR_MODE) != LATTR_NORM) {
	x *= 2;
	if (x >= term->cols)
	    return;
	if (x + len*2*widefactor > term->cols)
	    len = (term->cols-x)/2/widefactor;/* trim to LH half */
	rlen = len * 2;
    } else
	rlen = len;

    /* FIXME: how do we actually implement double-{width,height} lattrs? */

    ox = x * fw;
    oy = y * fh;
    tw = rlen * widefactor * fw;
    th = fh;

    /*
     * Set the clipping rectangle.
     */
    [[NSGraphicsContext currentContext] saveGraphicsState];
    [NSBezierPath clipRect:NSMakeRect(ox, oy, tw, th)];

    attrdict = [NSDictionary dictionaryWithObjectsAndKeys:
		colours[nfg], NSForegroundColorAttributeName,
		colours[nbg], NSBackgroundColorAttributeName,
		font, NSFontAttributeName, nil];

    /*
     * Create an NSString and draw it.
     * 
     * Annoyingly, although our input is wchar_t which is four
     * bytes wide on OS X and terminal.c supports 32-bit Unicode,
     * we must convert into the two-byte type `unichar' to store in
     * NSString, so we lose display capability for extra-BMP stuff
     * at this point.
     */
    {
	NSString *string;
	unichar *utext;
	int i;

	utext = snewn(len, unichar);
	for (i = 0; i < len; i++)
	    utext[i] = (text[i] >= 0x10000 ? 0xFFFD : text[i]);

	string = [NSString stringWithCharacters:utext length:len];
	[string drawAtPoint:NSMakePoint(ox, oy) withAttributes:attrdict];

	sfree(utext);
    }

    /*
     * Restore the graphics state from before the clipRect: call.
     */
    [[NSGraphicsContext currentContext] restoreGraphicsState];

    /*
     * And flag this area as needing display.
     */
    [self setNeedsDisplayInRect:NSMakeRect(ox, oy, tw, th)];
}

- (void)setColour:(int)n r:(float)r g:(float)g b:(float)b
{
    assert(n >= 0 && n < lenof(colours));
    colours[n] = [[NSColor colorWithDeviceRed:r green:g blue:b alpha:1.0]
		  retain];
}
@end

@implementation SessionWindow
- (id)initWithConfig:(Config)aCfg
{
    NSRect rect = { {0,0}, {0,0} };

    alert_ctx = NULL;

    cfg = aCfg;			       /* structure copy */

    init_ucs(&ucsdata, cfg.line_codepage, cfg.utf8_override,
	     CS_UTF8, cfg.vtmode);
    term = term_init(&cfg, &ucsdata, self);
    logctx = log_init(self, &cfg);
    term_provide_logctx(term, logctx);
    term_size(term, cfg.height, cfg.width, cfg.savelines);

    termview = [[[TerminalView alloc] initWithTerminal:term config:cfg]
		autorelease];

    /*
     * Now work out the size of the window.
     */
    rect = [termview frame];
    rect.origin = NSMakePoint(0,0);
    rect.size.width += 2 * cfg.window_border;
    rect.size.height += 2 * cfg.window_border;

    /*
     * Set up a backend.
     */
    back = backend_from_proto(cfg.protocol);
    if (!back)
	back = &pty_backend;

    {
	const char *error;
	char *realhost = NULL;
	error = back->init(self, &backhandle, &cfg, cfg.host, cfg.port,
			   &realhost, cfg.tcp_nodelay, cfg.tcp_keepalives);
	if (error) {
	    fatalbox("%s\n", error);   /* FIXME: connection_fatal at worst */
	}

	if (realhost)
	    sfree(realhost);	       /* FIXME: do something with this */
    }
    back->provide_logctx(backhandle, logctx);

    /*
     * Create a line discipline. (This must be done after creating
     * the terminal _and_ the backend, since it needs to be passed
     * pointers to both.)
     */
    ldisc = ldisc_create(&cfg, term, back, backhandle, self);

    /*
     * FIXME: Set up a scrollbar.
     */

    self = [super initWithContentRect:rect
	    styleMask:(NSTitledWindowMask | NSMiniaturizableWindowMask |
		       NSClosableWindowMask)
	    backing:NSBackingStoreBuffered
	    defer:YES];
    [self setTitle:@"PuTTY"];

    [self setIgnoresMouseEvents:NO];

    /*
     * Put the terminal view in the window.
     */
    rect = [termview frame];
    rect.origin = NSMakePoint(cfg.window_border, cfg.window_border);
    [termview setFrame:rect];
    [[self contentView] addSubview:termview];

    /*
     * Set up the colour palette.
     */
    palette_reset(self);

    /*
     * FIXME: Only the _first_ document window should be centred.
     * The subsequent ones should appear down and to the right of
     * it, probably using the cascade function provided by Cocoa.
     * Also we're apparently required by the HIG to remember and
     * reuse previous positions of windows, although I'm not sure
     * how that works if the user opens more than one of the same
     * session type.
     */
    [self center];		       /* :-) */

    exited = FALSE;

    return self;
}

- (void)dealloc
{
    /*
     * FIXME: Here we must deallocate all sorts of stuff: the
     * terminal, the backend, the ldisc, the logctx, you name it.
     * Do so.
     */
    sfree(alert_ctx);
    if (back)
	back->free(backhandle);
    if (ldisc)
	ldisc_free(ldisc);
    /* ldisc must be freed before term, since ldisc_free expects term
     * still to be around. */
    if (logctx)
	log_free(logctx);
    if (term)
	term_free(term);
    [super dealloc];
}

- (void)drawStartFinish:(BOOL)start
{
    [termview drawStartFinish:start];
}

- (void)setColour:(int)n r:(float)r g:(float)g b:(float)b
{
    [termview setColour:n r:r g:g b:b];
}

- (void)doText:(wchar_t *)text len:(int)len x:(int)x y:(int)y
    attr:(unsigned long)attr lattr:(int)lattr
{
    /* Pass this straight on to the TerminalView. */
    [termview doText:text len:len x:x y:y attr:attr lattr:lattr];
}

- (Config *)cfg
{
    return &cfg;
}

- (void)keyDown:(NSEvent *)ev
{
    NSString *s = [ev characters];
    int i;
    int n = [s length], c = [s characterAtIndex:0], m = [ev modifierFlags];
    int cm = [[ev charactersIgnoringModifiers] characterAtIndex:0];
    wchar_t output[32];
    char coutput[32];
    int use_coutput = FALSE, special = FALSE, start, end;

//printf("n=%d c=U+%04x cm=U+%04x m=%08x\n", n, c, cm, m);

    /*
     * FIXME: Alt+numberpad codes.
     */

    /*
     * Shift and Ctrl with PageUp/PageDown for scrollback.
     */
    if (n == 1 && c == NSPageUpFunctionKey && (m & NSShiftKeyMask)) {
	term_scroll(term, 0, -term->rows/2);
	return;
    }
    if (n == 1 && c == NSPageUpFunctionKey && (m & NSControlKeyMask)) {
	term_scroll(term, 0, -1);
	return;
    }
    if (n == 1 && c == NSPageDownFunctionKey && (m & NSShiftKeyMask)) {
	term_scroll(term, 0, +term->rows/2);
	return;
    }
    if (n == 1 && c == NSPageDownFunctionKey && (m & NSControlKeyMask)) {
	term_scroll(term, 0, +1);
	return;
    }

    /*
     * FIXME: Shift-Ins for paste? Or is that not Maccy enough?
     */

    /*
     * FIXME: Alt (Option? Command?) prefix in general.
     * 
     * (Note that Alt-Shift-thing will work just by looking at
     * charactersIgnoringModifiers; but Alt-Ctrl-thing will need
     * processing properly, and Alt-as-in-Option won't happen at
     * all. Hmmm.)
     * 
     * (Note also that we need to be able to override menu key
     * equivalents before this is particularly useful.)
     */
    start = 1;
    end = start;

    /*
     * Ctrl-` is the same as Ctrl-\, unless we already have a
     * better idea.
     */
    if ((m & NSControlKeyMask) && n == 1 && cm == '`' && c == '`') {
	output[1] = '\x1c';
	end = 2;
    }

    /* We handle Return ourselves, because it needs to be flagged as
     * special to ldisc. */
    if (n == 1 && c == '\015') {
	coutput[1] = '\015';
	use_coutput = TRUE;
	end = 2;
	special = TRUE;
    }

    /* Control-Shift-Space is 160 (ISO8859 nonbreaking space) */
    if (n == 1 && (m & NSControlKeyMask) && (m & NSShiftKeyMask) &&
	cm == ' ') {
	output[1] = '\240';
	end = 2;
    }

    /* Control-2, Control-Space and Control-@ are all NUL. */
    if ((m & NSControlKeyMask) && n == 1 &&
	(cm == '2' || cm == '@' || cm == ' ') && c == cm) {
	output[1] = '\0';
	end = 2;
    }

    /* We don't let MacOS tell us what Backspace is! We know better. */
    if (cm == 0x7F && !(m & NSShiftKeyMask)) {
	coutput[1] = cfg.bksp_is_delete ? '\x7F' : '\x08';
	end = 2;
	use_coutput = special = TRUE;
    }
    /* For Shift Backspace, do opposite of what is configured. */
    if (cm == 0x7F && (m & NSShiftKeyMask)) {
	coutput[1] = cfg.bksp_is_delete ? '\x08' : '\x7F';
	end = 2;
	use_coutput = special = TRUE;
    }

    /* Shift-Tab is ESC [ Z. Oddly, this combination generates ^Y by
     * default on MacOS! */
    if (cm == 0x19 && (m & NSShiftKeyMask) && !(m & NSControlKeyMask)) {
	end = 1;
	output[end++] = '\033';
	output[end++] = '[';
	output[end++] = 'Z';
    }

    /*
     * NetHack keypad mode.
     */
    if (cfg.nethack_keypad && (m & NSNumericPadKeyMask)) {
	wchar_t *keys = NULL;
	switch (cm) {
	  case '1': keys = L"bB"; break;
	  case '2': keys = L"jJ"; break;
	  case '3': keys = L"nN"; break;
	  case '4': keys = L"hH"; break;
	  case '5': keys = L".."; break;
	  case '6': keys = L"lL"; break;
	  case '7': keys = L"yY"; break;
	  case '8': keys = L"kK"; break;
	  case '9': keys = L"uU"; break;
	}
	if (keys) {
	    end = 2;
	    if (m & NSShiftKeyMask)
		output[1] = keys[1];
	    else
		output[1] = keys[0];
	    goto done;
	}
    }

    /*
     * Application keypad mode.
     */
    if (term->app_keypad_keys && !cfg.no_applic_k &&
	(m & NSNumericPadKeyMask)) {
	int xkey = 0;
	switch (cm) {
	  case NSClearLineFunctionKey: xkey = 'P'; break;
	  case '=': xkey = 'Q'; break;
	  case '/': xkey = 'R'; break;
	  case '*': xkey = 'S'; break;
	    /*
	     * FIXME: keypad - and + need to be mapped to ESC O l
	     * and ESC O k, or ESC O l and ESC O m, depending on
	     * xterm function key mode, and I can't remember which
	     * goes where.
	     */
	  case '\003': xkey = 'M'; break;
	  case '0': xkey = 'p'; break;
	  case '1': xkey = 'q'; break;
	  case '2': xkey = 'r'; break;
	  case '3': xkey = 's'; break;
	  case '4': xkey = 't'; break;
	  case '5': xkey = 'u'; break;
	  case '6': xkey = 'v'; break;
	  case '7': xkey = 'w'; break;
	  case '8': xkey = 'x'; break;
	  case '9': xkey = 'y'; break;
	  case '.': xkey = 'n'; break;
	}
	if (xkey) {
	    if (term->vt52_mode) {
		if (xkey >= 'P' && xkey <= 'S') {
		    output[end++] = '\033';
		    output[end++] = xkey;
		} else {
		    output[end++] = '\033';
		    output[end++] = '?';
		    output[end++] = xkey;
		}
	    } else {
		output[end++] = '\033';
		output[end++] = 'O';
		output[end++] = xkey;
	    }
	    goto done;
	}
    }

    /*
     * Next, all the keys that do tilde codes. (ESC '[' nn '~',
     * for integer decimal nn.)
     *
     * We also deal with the weird ones here. Linux VCs replace F1
     * to F5 by ESC [ [ A to ESC [ [ E. rxvt doesn't do _that_, but
     * does replace Home and End (1~ and 4~) by ESC [ H and ESC O w
     * respectively.
     */
    {
	int code = 0;
	switch (cm) {
	  case NSF1FunctionKey:
	    code = (m & NSShiftKeyMask ? 23 : 11);
	    break;
	  case NSF2FunctionKey:
	    code = (m & NSShiftKeyMask ? 24 : 12);
	    break;
	  case NSF3FunctionKey:
	    code = (m & NSShiftKeyMask ? 25 : 13);
	    break;
	  case NSF4FunctionKey:
	    code = (m & NSShiftKeyMask ? 26 : 14);
	    break;
	  case NSF5FunctionKey:
	    code = (m & NSShiftKeyMask ? 28 : 15);
	    break;
	  case NSF6FunctionKey:
	    code = (m & NSShiftKeyMask ? 29 : 17);
	    break;
	  case NSF7FunctionKey:
	    code = (m & NSShiftKeyMask ? 31 : 18);
	    break;
	  case NSF8FunctionKey:
	    code = (m & NSShiftKeyMask ? 32 : 19);
	    break;
	  case NSF9FunctionKey:
	    code = (m & NSShiftKeyMask ? 33 : 20);
	    break;
	  case NSF10FunctionKey:
	    code = (m & NSShiftKeyMask ? 34 : 21);
	    break;
	  case NSF11FunctionKey:
	    code = 23;
	    break;
	  case NSF12FunctionKey:
	    code = 24;
	    break;
	  case NSF13FunctionKey:
	    code = 25;
	    break;
	  case NSF14FunctionKey:
	    code = 26;
	    break;
	  case NSF15FunctionKey:
	    code = 28;
	    break;
	  case NSF16FunctionKey:
	    code = 29;
	    break;
	  case NSF17FunctionKey:
	    code = 31;
	    break;
	  case NSF18FunctionKey:
	    code = 32;
	    break;
	  case NSF19FunctionKey:
	    code = 33;
	    break;
	  case NSF20FunctionKey:
	    code = 34;
	    break;
	}
	if (!(m & NSControlKeyMask)) switch (cm) {
	  case NSHomeFunctionKey:
	    code = 1;
	    break;
#ifdef FIXME
	  case GDK_Insert: case GDK_KP_Insert:
	    code = 2;
	    break;
#endif
	  case NSDeleteFunctionKey:
	    code = 3;
	    break;
	  case NSEndFunctionKey:
	    code = 4;
	    break;
	  case NSPageUpFunctionKey:
	    code = 5;
	    break;
	  case NSPageDownFunctionKey:
	    code = 6;
	    break;
	}
	/* Reorder edit keys to physical order */
	if (cfg.funky_type == FUNKY_VT400 && code <= 6)
	    code = "\0\2\1\4\5\3\6"[code];

	if (term->vt52_mode && code > 0 && code <= 6) {
	    output[end++] = '\033';
	    output[end++] = " HLMEIG"[code];
	    goto done;
	}

	if (cfg.funky_type == FUNKY_SCO &&     /* SCO function keys */
	    code >= 11 && code <= 34) {
	    char codes[] = "MNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@[\\]^_`{";
	    int index = 0;
	    switch (cm) {
	      case NSF1FunctionKey: index = 0; break;
	      case NSF2FunctionKey: index = 1; break;
	      case NSF3FunctionKey: index = 2; break;
	      case NSF4FunctionKey: index = 3; break;
	      case NSF5FunctionKey: index = 4; break;
	      case NSF6FunctionKey: index = 5; break;
	      case NSF7FunctionKey: index = 6; break;
	      case NSF8FunctionKey: index = 7; break;
	      case NSF9FunctionKey: index = 8; break;
	      case NSF10FunctionKey: index = 9; break;
	      case NSF11FunctionKey: index = 10; break;
	      case NSF12FunctionKey: index = 11; break;
	    }
	    if (m & NSShiftKeyMask) index += 12;
	    if (m & NSControlKeyMask) index += 24;
	    output[end++] = '\033';
	    output[end++] = '[';
	    output[end++] = codes[index];
	    goto done;
	}
	if (cfg.funky_type == FUNKY_SCO &&     /* SCO small keypad */
	    code >= 1 && code <= 6) {
	    char codes[] = "HL.FIG";
	    if (code == 3) {
		output[1] = '\x7F';
		end = 2;
	    } else {
		output[end++] = '\033';
		output[end++] = '[';
		output[end++] = codes[code-1];
	    }
	    goto done;
	}
	if ((term->vt52_mode || cfg.funky_type == FUNKY_VT100P) &&
	    code >= 11 && code <= 24) {
	    int offt = 0;
	    if (code > 15)
		offt++;
	    if (code > 21)
		offt++;
	    if (term->vt52_mode) {
		output[end++] = '\033';
		output[end++] = code + 'P' - 11 - offt;
	    } else {
		output[end++] = '\033';
		output[end++] = 'O';
		output[end++] = code + 'P' - 11 - offt;
	    }
	    goto done;
	}
	if (cfg.funky_type == FUNKY_LINUX && code >= 11 && code <= 15) {
	    output[end++] = '\033';
	    output[end++] = '[';
	    output[end++] = '[';	
	    output[end++] = code + 'A' - 11;
	    goto done;
	}
	if (cfg.funky_type == FUNKY_XTERM && code >= 11 && code <= 14) {
	    if (term->vt52_mode) {
		output[end++] = '\033';
		output[end++] = code + 'P' - 11;
	    } else {
		output[end++] = '\033';
		output[end++] = 'O';
		output[end++] = code + 'P' - 11;
	    }
	    goto done;
	}
	if ((cfg.rxvt_homeend == 1) && (code == 1 || code == 4)) {
		// rxvt
	    if (code == 1) {
		output[end++] = '\033';
		output[end++] = '[';
		output[end++] = 'H';
	    } else {
		output[end++] = '\033';
		output[end++] = 'O';
		output[end++] = 'w';
	    }
	    goto done;
	}
	if ((cfg.rxvt_homeend == 2) && (code == 1 || code == 4)) {
	    // urxvt
	    if (code == 1) {
		output[end++] = '\033';
		output[end++] = '[';
		output[end++] = '7';
		output[end++] = '~';
	    } else {
		output[end++] = '\033';
		output[end++] = '[';
		output[end++] = '8';
		output[end++] = '~';
	    }
	    goto done;
	}
	if ((cfg.rxvt_homeend == 3) && (code == 1 || code == 4)) {
	    // xterm
	    if (code == 1) {
		output[end++] = '\033';
		output[end++] = 'O';
		output[end++] = 'H';
	    } else {
		output[end++] = '\033';
		output[end++] = 'O';
		output[end++] = 'F';
	    }
	    goto done;
	}
	if ((cfg.rxvt_homeend == 4) && (code == 1 || code == 4)) {
	    // FreeBSD1
	    if (code == 1) {
		output[end++] = '\033';
		output[end++] = '[';
		output[end++] = 'H';
	    } else {
		output[end++] = '\033';
		output[end++] = '[';
		output[end++] = 'F';
	    }
	    goto done;
	}
	if ((cfg.rxvt_homeend == 5) && (code == 1 || code == 4)) {
	    // FreeBSD2
	    if (code == 1) {
		output[end++] = '\033';
		output[end++] = 'O';
		output[end++] = 'H';
	    } else {
		output[end++] = '\033';
		output[end++] = '[';
		output[end++] = '?';
		output[end++] = '1';
		output[end++] = 'l';
		output[end++] = '\033';
		output[end++] = '>';
	    }
	    goto done;
	}
	if (code) {
	    char buf[20];
	    sprintf(buf, "\x1B[%d~", code);
	    for (i = 0; buf[i]; i++)
		output[end++] = buf[i];
	    goto done;
	}
    }

    /*
     * Cursor keys. (This includes the numberpad cursor keys,
     * if we haven't already done them due to app keypad mode.)
     */
    {
	int xkey = 0;
	switch (cm) {
	  case NSUpArrowFunctionKey: xkey = 'A'; break;
	  case NSDownArrowFunctionKey: xkey = 'B'; break;
	  case NSRightArrowFunctionKey: xkey = 'C'; break;
	  case NSLeftArrowFunctionKey: xkey = 'D'; break;
	}
	if (xkey) {
	    end += format_arrow_key(output+end, term, xkey,
				    m & NSControlKeyMask);
	    goto done;
	}
    }

    done:

    /*
     * Failing everything else, send the exact Unicode we got from
     * OS X.
     */
    if (end == start) {
	if (n > lenof(output)-start)
	    n = lenof(output)-start;   /* _shouldn't_ happen! */
	for (i = 0; i < n; i++) {
	    output[i+start] = [s characterAtIndex:i];
	}
	end = n+start;
    }

    if (use_coutput) {
	assert(special);
	assert(end < lenof(coutput));
	coutput[end] = '\0';
	ldisc_send(ldisc, coutput+start, -2, TRUE);
    } else {
	luni_send(ldisc, output+start, end-start, TRUE);
    }
}

- (int)fromBackend:(const char *)data len:(int)len isStderr:(int)is_stderr
{
    return term_data(term, is_stderr, data, len);
}

- (int)fromBackendUntrusted:(const char *)data len:(int)len
{
    return term_data_untrusted(term, data, len);
}

- (void)startAlert:(NSAlert *)alert
    withCallback:(void (*)(void *, int))callback andCtx:(void *)ctx
{
    if (alert_ctx || alert_qhead) {
	/*
	 * Queue this alert to be shown later.
	 */
	struct alert_queue *qitem = snew(struct alert_queue);
	qitem->next = NULL;
	qitem->alert = alert;
	qitem->callback = callback;
	qitem->ctx = ctx;
	if (alert_qtail)
	    alert_qtail->next = qitem;
	else
	    alert_qhead = qitem;
	alert_qtail = qitem;
    } else {
	alert_callback = callback;
	alert_ctx = ctx;	       /* NB this is assumed to need freeing! */
	[alert beginSheetModalForWindow:self modalDelegate:self
	 didEndSelector:@selector(alertSheetDidEnd:returnCode:contextInfo:)
	 contextInfo:NULL];
    }
}

- (void)alertSheetDidEnd:(NSAlert *)alert returnCode:(int)returnCode
    contextInfo:(void *)contextInfo
{
    [self performSelectorOnMainThread:
     @selector(alertSheetDidFinishEnding:)
     withObject:[NSNumber numberWithInt:returnCode]
     waitUntilDone:NO];
}

- (void)alertSheetDidFinishEnding:(id)object
{
    int returnCode = [object intValue];

    alert_callback(alert_ctx, returnCode);   /* transfers ownership of ctx */

    /*
     * If there's an alert in our queue (either already or because
     * the callback just queued it), start it.
     */
    if (alert_qhead) {
	struct alert_queue *qnext;

	alert_callback = alert_qhead->callback;
	alert_ctx = alert_qhead->ctx;
	[alert_qhead->alert beginSheetModalForWindow:self modalDelegate:self
	 didEndSelector:@selector(alertSheetDidEnd:returnCode:contextInfo:)
	 contextInfo:NULL];

	qnext = alert_qhead->next;
	sfree(alert_qhead);
	alert_qhead = qnext;
	if (!qnext)
	    alert_qtail = NULL;
    } else {
	alert_ctx = NULL;
    }
}

- (void)notifyRemoteExit
{
    int exitcode;

    if (!exited && (exitcode = back->exitcode(backhandle)) >= 0)
	[self endSession:(exitcode == 0)];
}

- (void)endSession:(int)clean
{
    exited = TRUE;
    if (ldisc) {
	ldisc_free(ldisc);
	ldisc = NULL;
    }
    if (back) {
	back->free(backhandle);
	backhandle = NULL;
	back = NULL;
	//FIXME: update specials menu;
    }
    if (cfg.close_on_exit == FORCE_ON ||
	(cfg.close_on_exit == AUTO && clean))
	[self close];
    // FIXME: else show restart menu item
}

- (Terminal *)term
{
    return term;
}

@end

int from_backend(void *frontend, int is_stderr, const char *data, int len)
{
    SessionWindow *win = (SessionWindow *)frontend;
    return [win fromBackend:data len:len isStderr:is_stderr];
}

int from_backend_untrusted(void *frontend, const char *data, int len)
{
    SessionWindow *win = (SessionWindow *)frontend;
    return [win fromBackendUntrusted:data len:len];
}

int get_userpass_input(prompts_t *p, unsigned char *in, int inlen)
{
    SessionWindow *win = (SessionWindow *)p->frontend;
    Terminal *term = [win term];
    return term_get_userpass_input(term, p, in, inlen);
}

void frontend_keypress(void *handle)
{
    /* FIXME */
}

void notify_remote_exit(void *frontend)
{
    SessionWindow *win = (SessionWindow *)frontend;

    [win notifyRemoteExit];
}

void ldisc_update(void *frontend, int echo, int edit)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /*
     * In a GUI front end, this need do nothing.
     */
}

char *get_ttymode(void *frontend, const char *mode)
{
    SessionWindow *win = (SessionWindow *)frontend;
    Terminal *term = [win term];
    return term_get_ttymode(term, mode);
}

void update_specials_menu(void *frontend)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

/*
 * This is still called when mode==BELL_VISUAL, even though the
 * visual bell is handled entirely within terminal.c, because we
 * may want to perform additional actions on any kind of bell (for
 * example, taskbar flashing in Windows).
 */
void do_beep(void *frontend, int mode)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    if (mode != BELL_VISUAL)
	NSBeep();
}

int char_width(Context ctx, int uc)
{
    /*
     * Under X, any fixed-width font really _is_ fixed-width.
     * Double-width characters will be dealt with using a separate
     * font. For the moment we can simply return 1.
     */
    return 1;
}

void palette_set(void *frontend, int n, int r, int g, int b)
{
    SessionWindow *win = (SessionWindow *)frontend;

    if (n >= 16)
	n += 256 - 16;
    if (n >= NALLCOLOURS)
	return;
    [win setColour:n r:r/255.0 g:g/255.0 b:b/255.0];

    /*
     * FIXME: do we need an OS X equivalent of set_window_background?
     */
}

void palette_reset(void *frontend)
{
    SessionWindow *win = (SessionWindow *)frontend;
    Config *cfg = [win cfg];

    /* This maps colour indices in cfg to those used in colours[]. */
    static const int ww[] = {
	256, 257, 258, 259, 260, 261,
	0, 8, 1, 9, 2, 10, 3, 11,
	4, 12, 5, 13, 6, 14, 7, 15
    };

    int i;

    for (i = 0; i < NCFGCOLOURS; i++) {
	[win setColour:ww[i] r:cfg->colours[i][0]/255.0
	 g:cfg->colours[i][1]/255.0 b:cfg->colours[i][2]/255.0];
    }

    for (i = 0; i < NEXTCOLOURS; i++) {
	if (i < 216) {
	    int r = i / 36, g = (i / 6) % 6, b = i % 6;
	    r = r ? r*40+55 : 0; g = g ? b*40+55 : 0; b = b ? b*40+55 : 0;
	    [win setColour:i+16 r:r/255.0 g:g/255.0 b:b/255.0];
	} else {
	    int shade = i - 216;
	    float fshade = (shade * 10 + 8) / 255.0;
	    [win setColour:i+16 r:fshade g:fshade b:fshade];
	}
    }

    /*
     * FIXME: do we need an OS X equivalent of set_window_background?
     */
}

Context get_ctx(void *frontend)
{
    SessionWindow *win = (SessionWindow *)frontend;

    /*
     * Lock the drawing focus on the image inside the TerminalView.
     */
    [win drawStartFinish:YES];

    [[NSGraphicsContext currentContext] setShouldAntialias:YES];

    /*
     * Cocoa drawing functions don't take a graphics context: that
     * parameter is implicit. Therefore, we'll use the frontend
     * handle itself as the context, on the grounds that it's as
     * good a thing to use as any.
     */
    return frontend;
}

void free_ctx(Context ctx)
{
    SessionWindow *win = (SessionWindow *)ctx;

    [win drawStartFinish:NO];
}

void do_text(Context ctx, int x, int y, wchar_t *text, int len,
	     unsigned long attr, int lattr)
{
    SessionWindow *win = (SessionWindow *)ctx;

    [win doText:text len:len x:x y:y attr:attr lattr:lattr];
}

void do_cursor(Context ctx, int x, int y, wchar_t *text, int len,
	       unsigned long attr, int lattr)
{
    SessionWindow *win = (SessionWindow *)ctx;
    Config *cfg = [win cfg];
    int active, passive;

    if (attr & TATTR_PASCURS) {
	attr &= ~TATTR_PASCURS;
	passive = 1;
    } else
	passive = 0;
    if ((attr & TATTR_ACTCURS) && cfg->cursor_type != 0) {
	attr &= ~TATTR_ACTCURS;
        active = 1;
    } else
        active = 0;

    [win doText:text len:len x:x y:y attr:attr lattr:lattr];

    /*
     * FIXME: now draw the various cursor types (both passive and
     * active underlines and vertical lines, plus passive blocks).
     */
}

/*
 * Minimise or restore the window in response to a server-side
 * request.
 */
void set_iconic(void *frontend, int iconic)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

/*
 * Move the window in response to a server-side request.
 */
void move_window(void *frontend, int x, int y)
{
    //SessionWindow *win = (SessionWindow *)frontend; 
    /* FIXME */
}

/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
void set_zorder(void *frontend, int top)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

/*
 * Refresh the window in response to a server-side request.
 */
void refresh_window(void *frontend)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

/*
 * Maximise or restore the window in response to a server-side
 * request.
 */
void set_zoomed(void *frontend, int zoomed)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

/*
 * Report whether the window is iconic, for terminal reports.
 */
int is_iconic(void *frontend)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    return NO; 			       /* FIXME */
}

/*
 * Report the window's position, for terminal reports.
 */
void get_window_pos(void *frontend, int *x, int *y)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

/*
 * Report the window's pixel size, for terminal reports.
 */
void get_window_pixels(void *frontend, int *x, int *y)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

/*
 * Return the window or icon title.
 */
char *get_window_title(void *frontend, int icon)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    return NULL; /* FIXME */
}

void set_title(void *frontend, char *title)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

void set_icon(void *frontend, char *title)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

void set_sbar(void *frontend, int total, int start, int page)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

void get_clip(void *frontend, wchar_t ** p, int *len)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

void write_clip(void *frontend, wchar_t *data, int *attr, int len, int must_deselect)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

void request_paste(void *frontend)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

void set_raw_mouse_mode(void *frontend, int activate)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

void request_resize(void *frontend, int w, int h)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
}

void sys_cursor(void *frontend, int x, int y)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /*
     * This is probably meaningless under OS X. FIXME: find out for
     * sure.
     */
}

void logevent(void *frontend, const char *string)
{
    //SessionWindow *win = (SessionWindow *)frontend;
    /* FIXME */
printf("logevent: %s\n", string);
}

int font_dimension(void *frontend, int which)/* 0 for width, 1 for height */
{
    //SessionWindow *win = (SessionWindow *)frontend;
    return 1; /* FIXME */
}

void set_busy_status(void *frontend, int status)
{
    /*
     * We need do nothing here: the OS X `application is busy'
     * beachball pointer appears _automatically_ when the
     * application isn't responding to GUI messages.
     */
}
