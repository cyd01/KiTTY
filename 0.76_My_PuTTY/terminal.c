/*
 * Terminal emulator.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <wchar.h>

#include <time.h>
#include <assert.h>
#include "putty.h"
#include "terminal.h"

#ifdef MOD_FAR2L
/* base64 library - needed for far2l extensions support */
#include <../far2l/cencode.h>
#include <../far2l/cdecode.h>
#endif
#ifdef MOD_PERSO
#include "charset.h"
int get_param( const char * val ) ;
int frontend_is_utf8(void *frontend) ;
int GetPuttyFlag(void) ;
int GetHyperlinkFlag(void) ;
int GetCursorType() ;
void SetCursorType( const int ct ) ;
#endif
#ifdef MOD_HYPERLINK
/*
 * HACK: PuttyTray / Nutty
 */ 
#include "urlhack.h"
void wintw_clip_write(TermWin *tw, int clipboard, wchar_t *data, int *attr,truecolour *truecolour, int len, bool must_deselect);
#endif
#ifdef MOD_ZMODEM
int xyz_ReceiveData(Terminal *term, const u_char *buffer, int len);
int GetZModemFlag(void) ;
#endif

#define VT52_PLUS

#define CL_ANSIMIN	0x0001	       /* Codes in all ANSI like terminals. */
#define CL_VT100	0x0002	       /* VT100 */
#define CL_VT100AVO	0x0004	       /* VT100 +AVO; 132x24 (not 132x14) & attrs */
#define CL_VT102	0x0008	       /* VT102 */
#define CL_VT220	0x0010	       /* VT220 */
#define CL_VT320	0x0020	       /* VT320 */
#define CL_VT420	0x0040	       /* VT420 */
#define CL_VT510	0x0080	       /* VT510, NB VT510 includes ANSI */
#define CL_VT340TEXT	0x0100	       /* VT340 extensions that appear in the VT420 */
#define CL_SCOANSI	0x1000	       /* SCOANSI not in ANSIMIN. */
#define CL_ANSI		0x2000	       /* ANSI ECMA-48 not in the VT100..VT420 */
#define CL_OTHER	0x4000	       /* Others, Xterm, linux, putty, dunno, etc */

#define TM_VT100	(CL_ANSIMIN|CL_VT100)
#define TM_VT100AVO	(TM_VT100|CL_VT100AVO)
#define TM_VT102	(TM_VT100AVO|CL_VT102)
#define TM_VT220	(TM_VT102|CL_VT220)
#define TM_VTXXX	(TM_VT220|CL_VT340TEXT|CL_VT510|CL_VT420|CL_VT320)
#define TM_SCOANSI	(CL_ANSIMIN|CL_SCOANSI)

#ifdef MOD_PERSO
#define MM_NONE         0x00           /* No tracking */
#define MM_NORMAL       0x01           /* Normal tracking mode */
#define MM_BTN_EVENT    0x02           /* Button event mode */
#define MM_ANY_EVENT    0x03           /* Any event mode */

/** Mouse tracking protocols */
#define MP_NORMAL       0x00           /* CSI M Cb Cx Cy */
#define MP_URXVT        0x01           /* CSI Db ; Dx ; Dy M */
#define MP_SGR          0x02           /* CSI Db ; Dx ; Dy M/m */
#define MP_XTERM        0x03           /* CSI M Cb WCx WCy */
#endif

#define TM_PUTTY	(0xFFFF)

#define UPDATE_DELAY    ((TICKSPERSEC+49)/50)/* ticks to defer window update */
#define TBLINK_DELAY    ((TICKSPERSEC*9+19)/20)/* ticks between text blinks*/
#define CBLINK_DELAY    (CURSORBLINK) /* ticks between cursor blinks */
#define VBELL_DELAY     (VBELL_TIMEOUT) /* visual bell timeout in ticks */

#define compatibility(x) \
    if ( ((CL_##x)&term->compatibility_level) == 0 ) { 	\
       term->termstate=TOPLEVEL;			\
       break;						\
    }
#define compatibility2(x,y) \
    if ( ((CL_##x|CL_##y)&term->compatibility_level) == 0 ) { \
       term->termstate=TOPLEVEL;			\
       break;						\
    }

#define has_compat(x) ( ((CL_##x)&term->compatibility_level) != 0 )

static const char *const EMPTY_WINDOW_TITLE = "";

static const char sco2ansicolour[] = { 0, 4, 2, 6, 1, 5, 3, 7 };

#define sel_nl_sz  (sizeof(sel_nl)/sizeof(wchar_t))
static const wchar_t sel_nl[] = SEL_NL;

/*
 * Fetch the character at a particular position in a line array,
 * for purposes of `wordtype'. The reason this isn't just a simple
 * array reference is that if the character we find is UCSWIDE,
 * then we must look one space further to the left.
 */
#define UCSGET(a, x) \
    ( (x)>0 && (a)[(x)].chr == UCSWIDE ? (a)[(x)-1].chr : (a)[(x)].chr )

/*
 * Detect the various aliases of U+0020 SPACE.
 */
#define IS_SPACE_CHR(chr) \
	((chr) == 0x20 || (DIRECT_CHAR(chr) && ((chr) & 0xFF) == 0x20))

/*
 * Spot magic CSETs.
 */
#define CSET_OF(chr) (DIRECT_CHAR(chr)||DIRECT_FONT(chr) ? (chr)&CSET_MASK : 0)

/*
 * Internal prototypes.
 */
static void resizeline(Terminal *, termline *, int);
static termline *lineptr(Terminal *, int, int, int);
static void unlineptr(termline *);
static void check_line_size(Terminal *, termline *);
static void do_paint(Terminal *);
static void erase_lots(Terminal *, bool, bool, bool);
static int find_last_nonempty_line(Terminal *, tree234 *);
static void swap_screen(Terminal *, int, bool, bool);
static void update_sbar(Terminal *);
static void deselect(Terminal *);
static void term_print_finish(Terminal *);
static void scroll(Terminal *, int, int, int, bool);
static void parse_optionalrgb(optionalrgb *out, unsigned *values);
static void term_added_data(Terminal *term);
static void term_update_raw_mouse_mode(Terminal *term);

static termline *newtermline(Terminal *term, int cols, bool bce)
{
    termline *line;
    int j;

    line = snew(termline);
    line->chars = snewn(cols, termchar);
    for (j = 0; j < cols; j++)
	line->chars[j] = (bce ? term->erase_char : term->basic_erase_char);
    line->cols = line->size = cols;
    line->lattr = LATTR_NORM;
    line->trusted = false;
    line->temporary = false;
    line->cc_free = 0;

    return line;
}

static void freetermline(termline *line)
{
    if (line) {
	sfree(line->chars);
	sfree(line);
    }
}

static void unlineptr(termline *line)
{
    if (line->temporary)
	freetermline(line);
}

const int colour_indices_conf_to_oscp[CONF_NCOLOURS] = {
    #define COLOUR_ENTRY(id,name) OSCP_COLOUR_##id,
    CONF_COLOUR_LIST(COLOUR_ENTRY)
    #undef COLOUR_ENTRY
};

const int colour_indices_conf_to_osc4[CONF_NCOLOURS] = {
    #define COLOUR_ENTRY(id,name) OSC4_COLOUR_##id,
    CONF_COLOUR_LIST(COLOUR_ENTRY)
    #undef COLOUR_ENTRY
};

const int colour_indices_oscp_to_osc4[OSCP_NCOLOURS] = {
    #define COLOUR_ENTRY(id) OSC4_COLOUR_##id,
    OSCP_COLOUR_LIST(COLOUR_ENTRY)
    #undef COLOUR_ENTRY
};

#ifdef TERM_CC_DIAGS
/*
 * Diagnostic function: verify that a termline has a correct
 * combining character structure.
 * 
 * This is a performance-intensive check, so it's no longer enabled
 * by default.
 */
static void cc_check(termline *line)
{
    unsigned char *flags;
    int i, j;

    assert(line->size >= line->cols);

    flags = snewn(line->size, unsigned char);

    for (i = 0; i < line->size; i++)
	flags[i] = (i < line->cols);

    for (i = 0; i < line->cols; i++) {
	j = i;
	while (line->chars[j].cc_next) {
	    j += line->chars[j].cc_next;
	    assert(j >= line->cols && j < line->size);
	    assert(!flags[j]);
	    flags[j] = true;
	}
    }

    j = line->cc_free;
    if (j) {
	while (1) {
	    assert(j >= line->cols && j < line->size);
	    assert(!flags[j]);
	    flags[j] = true;
	    if (line->chars[j].cc_next)
		j += line->chars[j].cc_next;
	    else
		break;
	}
    }

    j = 0;
    for (i = 0; i < line->size; i++)
	j += (flags[i] != 0);

    assert(j == line->size);

    sfree(flags);
}
#endif

static void clear_cc(termline *line, int col);

/*
 * Add a combining character to a character cell.
 */
static void add_cc(termline *line, int col, unsigned long chr)
{
    int newcc;

    assert(col >= 0 && col < line->cols);

    /*
     * Don't add combining characters at all to U+FFFD REPLACEMENT
     * CHARACTER. (Partly it's a slightly incoherent idea in the first
     * place; mostly, U+FFFD is what we generate if a cell already has
     * too many ccs, in which case we want it to be a fixed point when
     * further ccs are added.)
     */
    if (line->chars[col].chr == 0xFFFD)
        return;

    /*
     * Walk the cc list of the cell in question to find its current
     * end point.
     */
    size_t ncc = 0;
    int origcol = col;
    while (line->chars[col].cc_next) {
	col += line->chars[col].cc_next;
        if (++ncc >= CC_LIMIT) {
            /*
             * There are already too many combining characters in this
             * character cell. Change strategy: throw out the entire
             * chain and replace the main character with U+FFFD.
             *
             * (Rationale: extrapolating from UTR #36 section 3.6.2
             * suggests the principle that it's better to substitute
             * U+FFFD than to _ignore_ input completely. Also, if the
             * user copies and pastes an overcombined character cell,
             * this way it will clearly indicate that we haven't
             * reproduced the writer's original intentions, instead of
             * looking as if it was the _writer's_ fault that the 33rd
             * cc is missing.)
             *
             * Per the code above, this will also prevent any further
             * ccs from being added to this cell.
             */
            clear_cc(line, origcol);
            line->chars[origcol].chr = 0xFFFD;
            return;
        }
    }

    /*
     * Extend the cols array if the free list is empty.
     */
    if (!line->cc_free) {
	int n = line->size;

        size_t tmpsize = line->size;
        sgrowarray(line->chars, tmpsize, tmpsize);
        assert(tmpsize <= INT_MAX);
        line->size = tmpsize;

	line->cc_free = n;
	while (n < line->size) {
	    if (n+1 < line->size)
		line->chars[n].cc_next = 1;
	    else
		line->chars[n].cc_next = 0;
	    n++;
	}
    }

    /*
     * `col' now points at the last cc currently in this cell; so
     * we simply add another one.
     */
    newcc = line->cc_free;
    if (line->chars[newcc].cc_next)
	line->cc_free = newcc + line->chars[newcc].cc_next;
    else
	line->cc_free = 0;
    line->chars[newcc].cc_next = 0;
    line->chars[newcc].chr = chr;
    line->chars[col].cc_next = newcc - col;

#ifdef TERM_CC_DIAGS
    cc_check(line);
#endif
}

/*
 * Clear the combining character list in a character cell.
 */
static void clear_cc(termline *line, int col)
{
    int oldfree, origcol = col;

    assert(col >= 0 && col < line->cols);

    if (!line->chars[col].cc_next)
	return;			       /* nothing needs doing */

    oldfree = line->cc_free;
    line->cc_free = col + line->chars[col].cc_next;
    while (line->chars[col].cc_next)
	col += line->chars[col].cc_next;
    if (oldfree)
	line->chars[col].cc_next = oldfree - col;
    else
	line->chars[col].cc_next = 0;

    line->chars[origcol].cc_next = 0;

#ifdef TERM_CC_DIAGS
    cc_check(line);
#endif
}

/*
 * Compare two character cells for equality. Special case required
 * in do_paint() where we override what we expect the chr and attr
 * fields to be.
 */
static bool termchars_equal_override(termchar *a, termchar *b,
                                     unsigned long bchr, unsigned long battr)
{
    /* FULL-TERMCHAR */
    if (!truecolour_equal(a->truecolour, b->truecolour))
	return false;
    if (a->chr != bchr)
	return false;
    if ((a->attr &~ DATTR_MASK) != (battr &~ DATTR_MASK))
	return false;
    while (a->cc_next || b->cc_next) {
	if (!a->cc_next || !b->cc_next)
	    return false;	       /* one cc-list ends, other does not */
	a += a->cc_next;
	b += b->cc_next;
	if (a->chr != b->chr)
	    return false;
    }
    return true;
}

static bool termchars_equal(termchar *a, termchar *b)
{
    return termchars_equal_override(a, b, b->chr, b->attr);
}

/*
 * Copy a character cell. (Requires a pointer to the destination
 * termline, so as to access its free list.)
 */
static void copy_termchar(termline *destline, int x, termchar *src)
{
    clear_cc(destline, x);

    destline->chars[x] = *src;	       /* copy everything except cc-list */
    destline->chars[x].cc_next = 0;    /* and make sure this is zero */

    while (src->cc_next) {
	src += src->cc_next;
	add_cc(destline, x, src->chr);
    }

#ifdef TERM_CC_DIAGS
    cc_check(destline);
#endif
}

/*
 * Move a character cell within its termline.
 */
static void move_termchar(termline *line, termchar *dest, termchar *src)
{
    /* First clear the cc list from the original char, just in case. */
    clear_cc(line, dest - line->chars);

    /* Move the character cell and adjust its cc_next. */
    *dest = *src;		       /* copy everything except cc-list */
    if (src->cc_next)
	dest->cc_next = src->cc_next - (dest-src);

    /* Ensure the original cell doesn't have a cc list. */
    src->cc_next = 0;

#ifdef TERM_CC_DIAGS
    cc_check(line);
#endif
}

/*
 * Compress and decompress a termline into an RLE-based format for
 * storing in scrollback. (Since scrollback almost never needs to
 * be modified and exists in huge quantities, this is a sensible
 * tradeoff, particularly since it allows us to continue adding
 * features to the main termchar structure without proportionally
 * bloating the terminal emulator's memory footprint unless those
 * features are in constant use.)
 */
static void makerle(strbuf *b, termline *ldata,
		    void (*makeliteral)(strbuf *b, termchar *c,
					unsigned long *state))
{
    int hdrpos, hdrsize, n, prevlen, prevpos, thislen, thispos;
    bool prev2;
    termchar *c = ldata->chars;
    unsigned long state = 0, oldstate;

    n = ldata->cols;

    hdrpos = b->len;
    hdrsize = 0;
    put_byte(b, 0);
    prevlen = prevpos = 0;
    prev2 = false;

    while (n-- > 0) {
	thispos = b->len;
	makeliteral(b, c++, &state);
	thislen = b->len - thispos;
	if (thislen == prevlen &&
	    !memcmp(b->u + prevpos, b->u + thispos, thislen)) {
	    /*
	     * This literal precisely matches the previous one.
	     * Turn it into a run if it's worthwhile.
	     * 
	     * With one-byte literals, it costs us two bytes to
	     * encode a run, plus another byte to write the header
	     * to resume normal output; so a three-element run is
	     * neutral, and anything beyond that is unconditionally
	     * worthwhile. With two-byte literals or more, even a
	     * 2-run is a win.
	     */
	    if (thislen > 1 || prev2) {
		int runpos, runlen;

		/*
		 * It's worth encoding a run. Start at prevpos,
		 * unless hdrsize==0 in which case we can back up
		 * another one and start by overwriting hdrpos.
		 */

		hdrsize--;	       /* remove the literal at prevpos */
		if (prev2) {
		    assert(hdrsize > 0);
		    hdrsize--;
		    prevpos -= prevlen;/* and possibly another one */
		}

		if (hdrsize == 0) {
		    assert(prevpos == hdrpos + 1);
		    runpos = hdrpos;
                    strbuf_shrink_to(b, prevpos+prevlen);
		} else {
		    memmove(b->u + prevpos+1, b->u + prevpos, prevlen);
		    runpos = prevpos;
                    strbuf_shrink_to(b, prevpos+prevlen+1);
		    /*
		     * Terminate the previous run of ordinary
		     * literals.
		     */
		    assert(hdrsize >= 1 && hdrsize <= 128);
		    b->u[hdrpos] = hdrsize - 1;
		}

		runlen = prev2 ? 3 : 2;

		while (n > 0 && runlen < 129) {
		    int tmppos, tmplen;
		    tmppos = b->len;
		    oldstate = state;
		    makeliteral(b, c, &state);
		    tmplen = b->len - tmppos;
                    bool match = tmplen == thislen &&
                        !memcmp(b->u + runpos+1, b->u + tmppos, tmplen);
                    strbuf_shrink_to(b, tmppos);
                    if (!match) {
			state = oldstate;
			break;	       /* run over */
		    }
		    n--, c++, runlen++;
		}

		assert(runlen >= 2 && runlen <= 129);
		b->u[runpos] = runlen + 0x80 - 2;

		hdrpos = b->len;
		hdrsize = 0;
		put_byte(b, 0);
		/* And ensure this run doesn't interfere with the next. */
		prevlen = prevpos = 0;
		prev2 = false;

		continue;
	    } else {
		/*
		 * Just flag that the previous two literals were
		 * identical, in case we find a third identical one
		 * we want to turn into a run.
		 */
		prev2 = true;
		prevlen = thislen;
		prevpos = thispos;
	    }
	} else {
	    prev2 = false;
	    prevlen = thislen;
	    prevpos = thispos;
	}

	/*
	 * This character isn't (yet) part of a run. Add it to
	 * hdrsize.
	 */
	hdrsize++;
	if (hdrsize == 128) {
	    b->u[hdrpos] = hdrsize - 1;
	    hdrpos = b->len;
	    hdrsize = 0;
	    put_byte(b, 0);
	    prevlen = prevpos = 0;
	    prev2 = false;
	}
    }

    /*
     * Clean up.
     */
    if (hdrsize > 0) {
	assert(hdrsize <= 128);
	b->u[hdrpos] = hdrsize - 1;
    } else {
        strbuf_shrink_to(b, hdrpos);
    }
}
static void makeliteral_chr(strbuf *b, termchar *c, unsigned long *state)
{
    /*
     * My encoding for characters is UTF-8-like, in that it stores
     * 7-bit ASCII in one byte and uses high-bit-set bytes as
     * introducers to indicate a longer sequence. However, it's
     * unlike UTF-8 in that it doesn't need to be able to
     * resynchronise, and therefore I don't want to waste two bits
     * per byte on having recognisable continuation characters.
     * Also I don't want to rule out the possibility that I may one
     * day use values 0x80000000-0xFFFFFFFF for interesting
     * purposes, so unlike UTF-8 I need a full 32-bit range.
     * Accordingly, here is my encoding:
     * 
     * 00000000-0000007F: 0xxxxxxx (but see below)
     * 00000080-00003FFF: 10xxxxxx xxxxxxxx
     * 00004000-001FFFFF: 110xxxxx xxxxxxxx xxxxxxxx
     * 00200000-0FFFFFFF: 1110xxxx xxxxxxxx xxxxxxxx xxxxxxxx
     * 10000000-FFFFFFFF: 11110ZZZ xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
     * 
     * (`Z' is like `x' but is always going to be zero since the
     * values I'm encoding don't go above 2^32. In principle the
     * five-byte form of the encoding could extend to 2^35, and
     * there could be six-, seven-, eight- and nine-byte forms as
     * well to allow up to 64-bit values to be encoded. But that's
     * completely unnecessary for these purposes!)
     * 
     * The encoding as written above would be very simple, except
     * that 7-bit ASCII can occur in several different ways in the
     * terminal data; sometimes it crops up in the D800 page
     * (CSET_ASCII) but at other times it's in the 0000 page (real
     * Unicode). Therefore, this encoding is actually _stateful_:
     * the one-byte encoding of 00-7F actually indicates `reuse the
     * upper three bytes of the last character', and to encode an
     * absolute value of 00-7F you need to use the two-byte form
     * instead.
     */
    if ((c->chr & ~0x7F) == *state) {
	put_byte(b, (unsigned char)(c->chr & 0x7F));
    } else if (c->chr < 0x4000) {
	put_byte(b, (unsigned char)(((c->chr >> 8) & 0x3F) | 0x80));
	put_byte(b, (unsigned char)(c->chr & 0xFF));
    } else if (c->chr < 0x200000) {
	put_byte(b, (unsigned char)(((c->chr >> 16) & 0x1F) | 0xC0));
	put_uint16(b, c->chr & 0xFFFF);
    } else if (c->chr < 0x10000000) {
	put_byte(b, (unsigned char)(((c->chr >> 24) & 0x0F) | 0xE0));
	put_byte(b, (unsigned char)((c->chr >> 16) & 0xFF));
	put_uint16(b, c->chr & 0xFFFF);
    } else {
	put_byte(b, 0xF0);
	put_uint32(b, c->chr);
    }
    *state = c->chr & ~0xFF;
}
static void makeliteral_attr(strbuf *b, termchar *c, unsigned long *state)
{
    /*
     * My encoding for attributes is 16-bit-granular and assumes
     * that the top bit of the word is never required. I either
     * store a two-byte value with the top bit clear (indicating
     * just that value), or a four-byte value with the top bit set
     * (indicating the same value with its top bit clear).
     * 
     * However, first I permute the bits of the attribute value, so
     * that the eight bits of colour (four in each of fg and bg)
     * which are never non-zero unless xterm 256-colour mode is in
     * use are placed higher up the word than everything else. This
     * ensures that attribute values remain 16-bit _unless_ the
     * user uses extended colour.
     */
    unsigned attr, colourbits;

    attr = c->attr;

    assert(ATTR_BGSHIFT > ATTR_FGSHIFT);

    colourbits = (attr >> (ATTR_BGSHIFT + 4)) & 0xF;
    colourbits <<= 4;
    colourbits |= (attr >> (ATTR_FGSHIFT + 4)) & 0xF;

    attr = (((attr >> (ATTR_BGSHIFT + 8)) << (ATTR_BGSHIFT + 4)) |
	    (attr & ((1 << (ATTR_BGSHIFT + 4))-1)));
    attr = (((attr >> (ATTR_FGSHIFT + 8)) << (ATTR_FGSHIFT + 4)) |
	    (attr & ((1 << (ATTR_FGSHIFT + 4))-1)));

    attr |= (colourbits << (32-9));

    if (attr < 0x8000) {
	put_byte(b, (unsigned char)((attr >> 8) & 0xFF));
	put_byte(b, (unsigned char)(attr & 0xFF));
    } else {
	put_byte(b, (unsigned char)(((attr >> 24) & 0x7F) | 0x80));
	put_byte(b, (unsigned char)((attr >> 16) & 0xFF));
	put_byte(b, (unsigned char)((attr >> 8) & 0xFF));
	put_byte(b, (unsigned char)(attr & 0xFF));
    }
}
static void makeliteral_truecolour(strbuf *b, termchar *c, unsigned long *state)
{
    /*
     * Put the used parts of the colour info into the buffer.
     */
    put_byte(b, ((c->truecolour.fg.enabled ? 1 : 0) |
            (c->truecolour.bg.enabled ? 2 : 0)));
    if (c->truecolour.fg.enabled) {
	put_byte(b, c->truecolour.fg.r);
	put_byte(b, c->truecolour.fg.g);
	put_byte(b, c->truecolour.fg.b);
    }
    if (c->truecolour.bg.enabled) {
	put_byte(b, c->truecolour.bg.r);
	put_byte(b, c->truecolour.bg.g);
	put_byte(b, c->truecolour.bg.b);
    }
}
static void makeliteral_cc(strbuf *b, termchar *c, unsigned long *state)
{
    /*
     * For combining characters, I just encode a bunch of ordinary
     * chars using makeliteral_chr, and terminate with a \0
     * character (which I know won't come up as a combining char
     * itself).
     * 
     * I don't use the stateful encoding in makeliteral_chr.
     */
    unsigned long zstate;
    termchar z;

    while (c->cc_next) {
	c += c->cc_next;

	assert(c->chr != 0);

	zstate = 0;
	makeliteral_chr(b, c, &zstate);
    }

    z.chr = 0;
    zstate = 0;
    makeliteral_chr(b, &z, &zstate);
}

typedef struct compressed_scrollback_line {
    size_t len;
} compressed_scrollback_line;

static termline *decompressline(compressed_scrollback_line *line);

static compressed_scrollback_line *compressline(termline *ldata)
{
    strbuf *b = strbuf_new();

    /* Leave space for the header structure */
    strbuf_append(b, sizeof(compressed_scrollback_line));

    /*
     * First, store the column count, 7 bits at a time, least
     * significant `digit' first, with the high bit set on all but
     * the last.
     */
    {
	int n = ldata->cols;
	while (n >= 128) {
	    put_byte(b, (unsigned char)((n & 0x7F) | 0x80));
	    n >>= 7;
	}
	put_byte(b, (unsigned char)(n));
    }

    /*
     * Next store the lattrs; same principle. We add one extra bit to
     * this to indicate the trust state of the line.
     */
    {
	int n = ldata->lattr | (ldata->trusted ? 0x10000 : 0);
	while (n >= 128) {
	    put_byte(b, (unsigned char)((n & 0x7F) | 0x80));
	    n >>= 7;
	}
	put_byte(b, (unsigned char)(n));
    }

    /*
     * Now we store a sequence of separate run-length encoded
     * fragments, each containing exactly as many symbols as there
     * are columns in the ldata.
     * 
     * All of these have a common basic format:
     * 
     *  - a byte 00-7F indicates that X+1 literals follow it
     * 	- a byte 80-FF indicates that a single literal follows it
     * 	  and expects to be repeated (X-0x80)+2 times.
     * 
     * The format of the `literals' varies between the fragments.
     */
    makerle(b, ldata, makeliteral_chr);
    makerle(b, ldata, makeliteral_attr);
    makerle(b, ldata, makeliteral_truecolour);
    makerle(b, ldata, makeliteral_cc);

    size_t linelen = b->len - sizeof(compressed_scrollback_line);
    compressed_scrollback_line *line =
        (compressed_scrollback_line *)strbuf_to_str(b);
    line->len = linelen;

    /*
     * Diagnostics: ensure that the compressed data really does
     * decompress to the right thing.
     * 
     * This is a bit performance-heavy for production code.
     */
#ifdef TERM_CC_DIAGS
#ifndef CHECK_SB_COMPRESSION
    {
	termline *dcl;
	int i;

#ifdef DIAGNOSTIC_SB_COMPRESSION
	for (i = 0; i < b->len; i++) {
	    printf(" %02x ", b->data[i]);
	}
	printf("\n");
#endif

        dcl = decompressline(line);
	assert(ldata->cols == dcl->cols);
	assert(ldata->lattr == dcl->lattr);
	for (i = 0; i < ldata->cols; i++)
	    assert(termchars_equal(&ldata->chars[i], &dcl->chars[i]));

#ifdef DIAGNOSTIC_SB_COMPRESSION
	printf("%d cols (%d bytes) -> %d bytes (factor of %g)\n",
	       ldata->cols, 4 * ldata->cols, dused,
	       (double)dused / (4 * ldata->cols));
#endif

	freetermline(dcl);
    }
#endif
#endif /* TERM_CC_DIAGS */

    return line;
}

static void readrle(BinarySource *bs, termline *ldata,
		    void (*readliteral)(BinarySource *bs, termchar *c,
					termline *ldata, unsigned long *state))
{
    int n = 0;
    unsigned long state = 0;

    while (n < ldata->cols) {
	int hdr = get_byte(bs);

	if (hdr >= 0x80) {
	    /* A run. */

	    size_t pos = bs->pos, count = hdr + 2 - 0x80;
	    while (count--) {
		assert(n < ldata->cols);
		bs->pos = pos;
		readliteral(bs, ldata->chars + n, ldata, &state);
		n++;
	    }
	} else {
	    /* Just a sequence of consecutive literals. */

	    int count = hdr + 1;
	    while (count--) {
#ifndef MOD_PERSO
		assert(n < ldata->cols);
#endif
		readliteral(bs, ldata->chars + n, ldata, &state);
		n++;
	    }
	}
    }

    assert(n == ldata->cols);
}
static void readliteral_chr(BinarySource *bs, termchar *c, termline *ldata,
			    unsigned long *state)
{
    int byte;

    /*
     * 00000000-0000007F: 0xxxxxxx
     * 00000080-00003FFF: 10xxxxxx xxxxxxxx
     * 00004000-001FFFFF: 110xxxxx xxxxxxxx xxxxxxxx
     * 00200000-0FFFFFFF: 1110xxxx xxxxxxxx xxxxxxxx xxxxxxxx
     * 10000000-FFFFFFFF: 11110ZZZ xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
     */

    byte = get_byte(bs);
    if (byte < 0x80) {
	c->chr = byte | *state;
    } else if (byte < 0xC0) {
	c->chr = (byte &~ 0xC0) << 8;
	c->chr |= get_byte(bs);
    } else if (byte < 0xE0) {
	c->chr = (byte &~ 0xE0) << 16;
	c->chr |= get_uint16(bs);
    } else if (byte < 0xF0) {
	c->chr = (byte &~ 0xF0) << 24;
	c->chr |= get_byte(bs) << 16;
	c->chr |= get_uint16(bs);
    } else {
	assert(byte == 0xF0);
	c->chr = get_uint32(bs);
    }
    *state = c->chr & ~0xFF;
}
static void readliteral_attr(BinarySource *bs, termchar *c, termline *ldata,
			     unsigned long *state)
{
    unsigned val, attr, colourbits;

    val = get_uint16(bs);

    if (val >= 0x8000) {
	val &= ~0x8000;
	val <<= 16;
	val |= get_uint16(bs);
    }

    colourbits = (val >> (32-9)) & 0xFF;
    attr = (val & ((1<<(32-9))-1));

    attr = (((attr >> (ATTR_FGSHIFT + 4)) << (ATTR_FGSHIFT + 8)) |
	    (attr & ((1 << (ATTR_FGSHIFT + 4))-1)));
    attr = (((attr >> (ATTR_BGSHIFT + 4)) << (ATTR_BGSHIFT + 8)) |
	    (attr & ((1 << (ATTR_BGSHIFT + 4))-1)));

    attr |= (colourbits >> 4) << (ATTR_BGSHIFT + 4);
    attr |= (colourbits & 0xF) << (ATTR_FGSHIFT + 4);

    c->attr = attr;
}
static void readliteral_truecolour(
    BinarySource *bs, termchar *c, termline *ldata, unsigned long *state)
{
    int flags = get_byte(bs);

    if (flags & 1) {
        c->truecolour.fg.enabled = true;
	c->truecolour.fg.r = get_byte(bs);
	c->truecolour.fg.g = get_byte(bs);
	c->truecolour.fg.b = get_byte(bs);
    } else {
	c->truecolour.fg = optionalrgb_none;
    }

    if (flags & 2) {
        c->truecolour.bg.enabled = true;
	c->truecolour.bg.r = get_byte(bs);
	c->truecolour.bg.g = get_byte(bs);
	c->truecolour.bg.b = get_byte(bs);
    } else {
	c->truecolour.bg = optionalrgb_none;
    }
}
static void readliteral_cc(BinarySource *bs, termchar *c, termline *ldata,
			   unsigned long *state)
{
    termchar n;
    unsigned long zstate;
    int x = c - ldata->chars;

    c->cc_next = 0;

    while (1) {
	zstate = 0;
	readliteral_chr(bs, &n, ldata, &zstate);
	if (!n.chr)
	    break;
	add_cc(ldata, x, n.chr);
    }
}

static termline *decompressline(compressed_scrollback_line *line)
{
    int ncols, byte, shift;
    BinarySource bs[1];
    termline *ldata;

    BinarySource_BARE_INIT(bs, line+1, line->len);

    /*
     * First read in the column count.
     */
    ncols = shift = 0;
    do {
	byte = get_byte(bs);
	ncols |= (byte & 0x7F) << shift;
	shift += 7;
    } while (byte & 0x80);

    /*
     * Now create the output termline.
     */
    ldata = snew(termline);
    ldata->chars = snewn(ncols, termchar);
    ldata->cols = ldata->size = ncols;
    ldata->temporary = true;
    ldata->cc_free = 0;

    /*
     * We must set all the cc pointers in ldata->chars to 0 right
     * now, so that cc diagnostics that verify the integrity of the
     * whole line will make sense while we're in the middle of
     * building it up.
     */
    {
	int i;
	for (i = 0; i < ldata->cols; i++)
	    ldata->chars[i].cc_next = 0;
    }

    /*
     * Now read in the lattr.
     */
    int lattr = shift = 0;
    do {
	byte = get_byte(bs);
	lattr |= (byte & 0x7F) << shift;
	shift += 7;
    } while (byte & 0x80);
    ldata->lattr = lattr & 0xFFFF;
    ldata->trusted = (lattr & 0x10000) != 0;

    /*
     * Now we read in each of the RLE streams in turn.
     */
    readrle(bs, ldata, readliteral_chr);
    readrle(bs, ldata, readliteral_attr);
    readrle(bs, ldata, readliteral_truecolour);
    readrle(bs, ldata, readliteral_cc);

    /* And we always expect that we ended up exactly at the end of the
     * compressed data. */
    assert(!get_err(bs));
    assert(get_avail(bs) == 0);

    return ldata;
}

/*
 * Resize a line to make it `cols' columns wide.
 */
static void resizeline(Terminal *term, termline *line, int cols)
{
    int i, oldcols;

    if (line->cols != cols) {

	oldcols = line->cols;

	/*
	 * This line is the wrong length, which probably means it
	 * hasn't been accessed since a resize. Resize it now.
	 * 
	 * First, go through all the characters that will be thrown
	 * out in the resize (if we're shrinking the line) and
	 * return their cc lists to the cc free list.
	 */
	for (i = cols; i < oldcols; i++)
	    clear_cc(line, i);

	/*
	 * If we're shrinking the line, we now bodily move the
	 * entire cc section from where it started to where it now
	 * needs to be. (We have to do this before the resize, so
	 * that the data we're copying is still there. However, if
	 * we're expanding, we have to wait until _after_ the
	 * resize so that the space we're copying into is there.)
	 */
	if (cols < oldcols)
	    memmove(line->chars + cols, line->chars + oldcols,
		    (line->size - line->cols) * TSIZE);

	/*
	 * Now do the actual resize, leaving the _same_ amount of
	 * cc space as there was to begin with.
	 */
	line->size += cols - oldcols;
	line->chars = sresize(line->chars, line->size, TTYPE);
	line->cols = cols;

	/*
	 * If we're expanding the line, _now_ we move the cc
	 * section.
	 */
	if (cols > oldcols)
	    memmove(line->chars + cols, line->chars + oldcols,
		    (line->size - line->cols) * TSIZE);

	/*
	 * Go through what's left of the original line, and adjust
	 * the first cc_next pointer in each list. (All the
	 * subsequent ones are still valid because they are
	 * relative offsets within the cc block.) Also do the same
	 * to the head of the cc_free list.
	 */
	for (i = 0; i < oldcols && i < cols; i++)
	    if (line->chars[i].cc_next)
		line->chars[i].cc_next += cols - oldcols;
	if (line->cc_free)
	    line->cc_free += cols - oldcols;

	/*
	 * And finally fill in the new space with erase chars. (We
	 * don't have to worry about cc lists here, because we
	 * _know_ the erase char doesn't have one.)
	 */
	for (i = oldcols; i < cols; i++)
	    line->chars[i] = term->basic_erase_char;

#ifdef TERM_CC_DIAGS
	cc_check(line);
#endif
    }
}

/*
 * Get the number of lines in the scrollback.
 */
static int sblines(Terminal *term)
{
    int sblines = count234(term->scrollback);
    if (term->erase_to_scrollback &&
	term->alt_which && term->alt_screen) {
	    sblines += term->alt_sblines;
    }
    return sblines;
}

static void null_line_error(Terminal *term, int y, int lineno,
                            tree234 *whichtree, int treeindex,
                            const char *varname)
{
    modalfatalbox("%s==NULL in terminal.c\n"
                  "lineno=%d y=%d w=%d h=%d\n"
                  "count(scrollback=%p)=%d\n"
                  "count(screen=%p)=%d\n"
                  "count(alt=%p)=%d alt_sblines=%d\n"
                  "whichtree=%p treeindex=%d\n"
                  "commitid=%s\n\n"
                  "Please contact <putty@projects.tartarus.org> "
                  "and pass on the above information.",
                  varname, lineno, y, term->cols, term->rows,
                  term->scrollback, count234(term->scrollback),
                  term->screen, count234(term->screen),
                  term->alt_screen, count234(term->alt_screen),
                  term->alt_sblines, whichtree, treeindex, commitid);
}

/*
 * Retrieve a line of the screen or of the scrollback, according to
 * whether the y coordinate is non-negative or negative
 * (respectively).
 */
static termline *lineptr(Terminal *term, int y, int lineno, int screen)
{
    termline *line;
    tree234 *whichtree;
    int treeindex;

    if (y >= 0) {
	whichtree = term->screen;
	treeindex = y;
    } else {
	int altlines = 0;
#ifndef MOD_PERSO
	assert(!screen);
#endif
	if (term->erase_to_scrollback &&
	    term->alt_which && term->alt_screen) {
	    altlines = term->alt_sblines;
	}
	if (y < -altlines) {
	    whichtree = term->scrollback;
	    treeindex = y + altlines + count234(term->scrollback);
	} else {
	    whichtree = term->alt_screen;
	    treeindex = y + term->alt_sblines;
	    /* treeindex = y + count234(term->alt_screen); */
	}
    }
    if (whichtree == term->scrollback) {
	compressed_scrollback_line *cline = index234(whichtree, treeindex);
        if (!cline)
            null_line_error(term, y, lineno, whichtree, treeindex, "cline");
	line = decompressline(cline);
    } else {
	line = index234(whichtree, treeindex);
    }

    /* We assume that we don't screw up and retrieve something out of range. */
    if (line == NULL)
        null_line_error(term, y, lineno, whichtree, treeindex, "line");
    assert(line != NULL);

    /*
     * Here we resize lines to _at least_ the right length, but we
     * don't truncate them. Truncation is done as a side effect of
     * modifying the line.
     *
     * The point of this policy is to try to arrange that resizing the
     * terminal window repeatedly - e.g. successive steps in an X11
     * opaque window-resize drag, or resizing as a side effect of
     * retiling by tiling WMs such as xmonad - does not throw away
     * data gratuitously. Specifically, we want a sequence of resize
     * operations with no terminal output between them to have the
     * same effect as a single resize to the ultimate terminal size,
     * and also (for the case in which xmonad narrows a window that's
     * scrolling things) we want scrolling up new text at the bottom
     * of a narrowed window to avoid truncating lines further up when
     * the window is re-widened.
     */
    if (term->cols > line->cols)
        resizeline(term, line, term->cols);

    return line;
}

#define lineptr(x) (lineptr)(term,x,__LINE__,0)
#define scrlineptr(x) (lineptr)(term,x,__LINE__,1)

/*
 * Coerce a termline to the terminal's current width. Unlike the
 * optional resize in lineptr() above, this is potentially destructive
 * of text, since it can shrink as well as grow the line.
 *
 * We call this whenever a termline is actually going to be modified.
 * Helpfully, putting a single call to this function in check_boundary
 * deals with _nearly_ all such cases, leaving only a few things like
 * bulk erase and ESC#8 to handle separately.
 */
static void check_line_size(Terminal *term, termline *line)
{
    if (term->cols != line->cols)      /* trivial optimisation */
        resizeline(term, line, term->cols);
}

static void term_schedule_tblink(Terminal *term);
static void term_schedule_cblink(Terminal *term);
static void term_update_callback(void *ctx);

static void term_timer(void *ctx, unsigned long now)
{
    Terminal *term = (Terminal *)ctx;

    if (term->tblink_pending && now == term->next_tblink) {
	term->tblinker = !term->tblinker;
	term->tblink_pending = false;
	term_schedule_tblink(term);
        term->window_update_pending = true;
    }

    if (term->cblink_pending && now == term->next_cblink) {
	term->cblinker = !term->cblinker;
	term->cblink_pending = false;
	term_schedule_cblink(term);
        term->window_update_pending = true;
    }

    if (term->in_vbell && now == term->vbell_end) {
	term->in_vbell = false;
        term->window_update_pending = true;
    }

    if (term->window_update_cooldown &&
        now == term->window_update_cooldown_end) {
        term->window_update_cooldown = false;
    }

    if (term->window_update_pending)
        term_update_callback(term);
}

static void term_update_callback(void *ctx)
{
    Terminal *term = (Terminal *)ctx;
    if (!term->window_update_pending)
        return;
    if (!term->window_update_cooldown) {
	term_update(term);
        term->window_update_cooldown = true;
        term->window_update_cooldown_end = schedule_timer(
            UPDATE_DELAY, term_timer, term);
    }
}

static void term_schedule_update(Terminal *term)
{
    if (!term->window_update_pending) {
	term->window_update_pending = true;
        queue_toplevel_callback(term_update_callback, term);
    }
}

/*
 * Call this whenever the terminal window state changes, to queue
 * an update.
 */
static void seen_disp_event(Terminal *term)
{
    term->seen_disp_event = true;      /* for scrollback-reset-on-activity */
    term_schedule_update(term);
}

/*
 * Call when the terminal's blinking-text settings change, or when
 * a text blink has just occurred.
 */
static void term_schedule_tblink(Terminal *term)
{
    if (term->blink_is_real) {
	if (!term->tblink_pending)
	    term->next_tblink = schedule_timer(TBLINK_DELAY, term_timer, term);
	term->tblink_pending = true;
    } else {
	term->tblinker = true;         /* reset when not in use */
	term->tblink_pending = false;
    }
}

/*
 * Likewise with cursor blinks.
 */
static void term_schedule_cblink(Terminal *term)
{
    if (term->blink_cur && term->has_focus) {
	if (!term->cblink_pending)
	    term->next_cblink = schedule_timer(CBLINK_DELAY, term_timer, term);
	term->cblink_pending = true;
    } else {
	term->cblinker = true;         /* reset when not in use */
	term->cblink_pending = false;
    }
}

/*
 * Call to reset cursor blinking on new output.
 */
static void term_reset_cblink(Terminal *term)
{
    seen_disp_event(term);
    term->cblinker = true;
    term->cblink_pending = false;
    term_schedule_cblink(term);
}

/*
 * Call to begin a visual bell.
 */
static void term_schedule_vbell(Terminal *term, bool already_started,
				long startpoint)
{
    long ticks_already_gone;

    if (already_started)
	ticks_already_gone = GETTICKCOUNT() - startpoint;
    else
	ticks_already_gone = 0;

    if (ticks_already_gone < VBELL_DELAY) {
	term->in_vbell = true;
	term->vbell_end = schedule_timer(VBELL_DELAY - ticks_already_gone,
					 term_timer, term);
    } else {
	term->in_vbell = false;
    }
}

/*
 * Set up power-on settings for the terminal.
 * If 'clear' is false, don't actually clear the primary screen, and
 * position the cursor below the last non-blank line (scrolling if
 * necessary).
 */
static void power_on(Terminal *term, bool clear)
{
    term->alt_x = term->alt_y = 0;
    term->savecurs.x = term->savecurs.y = 0;
    term->alt_savecurs.x = term->alt_savecurs.y = 0;
    term->alt_t = term->marg_t = 0;
#ifdef MOD_FAR2L
    term->far2l_ext = 0;
#endif
    if (term->rows != -1)
	term->alt_b = term->marg_b = term->rows - 1;
    else
	term->alt_b = term->marg_b = 0;
    if (term->cols != -1) {
	int i;
	for (i = 0; i < term->cols; i++)
	    term->tabs[i] = (i % 8 == 0 ? true : false);
    }
    term->alt_om = term->dec_om = conf_get_bool(term->conf, CONF_dec_om);
    term->alt_ins = false;
    term->insert = false;
    term->alt_wnext = false;
    term->wrapnext = false;
    term->save_wnext = false;
    term->alt_save_wnext = false;
    term->alt_wrap = term->wrap = conf_get_bool(term->conf, CONF_wrap_mode);
    term->alt_cset = term->cset = term->save_cset = term->alt_save_cset = 0;
    term->alt_utf = false;
    term->utf = false;
    term->save_utf = false;
    term->alt_save_utf = false;
    term->utf8.state = 0;
    term->alt_sco_acs = term->sco_acs =
        term->save_sco_acs = term->alt_save_sco_acs = 0;
    term->cset_attr[0] = term->cset_attr[1] =
        term->save_csattr = term->alt_save_csattr = CSET_ASCII;
    term->rvideo = false;
    term->in_vbell = false;
    term->cursor_on = true;
    term->big_cursor = false;
    term->default_attr = term->save_attr =
	term->alt_save_attr = term->curr_attr = ATTR_DEFAULT;
    term->curr_truecolour.fg = term->curr_truecolour.bg = optionalrgb_none;
    term->save_truecolour = term->alt_save_truecolour = term->curr_truecolour;
    term->app_cursor_keys = conf_get_bool(term->conf, CONF_app_cursor);
    term->app_keypad_keys = conf_get_bool(term->conf, CONF_app_keypad);
    term->use_bce = conf_get_bool(term->conf, CONF_bce);
    term->blink_is_real = conf_get_bool(term->conf, CONF_blinktext);
    term->erase_char = term->basic_erase_char;
    term->alt_which = 0;
    term_print_finish(term);
#ifdef MOD_PERSO
    term->xterm_mouse_mode = MM_NONE;
    term->xterm_mouse_protocol = MP_NORMAL;
#else
    term->xterm_mouse = 0;
    term->xterm_extended_mouse = false;
    term->urxvt_extended_mouse = false;
#endif
    win_set_raw_mouse_mode(term->win, false);
    term->win_pointer_shape_pending = true;
    term->win_pointer_shape_raw = false;
    term->bracketed_paste = false;
    term->srm_echo = false;
    {
	int i;
	for (i = 0; i < 256; i++)
	    term->wordness[i] = conf_get_int_int(term->conf, CONF_wordness, i);
    }
    if (term->screen) {
	swap_screen(term, 1, false, false);
	erase_lots(term, false, true, true);
	swap_screen(term, 0, false, false);
	if (clear)
	    erase_lots(term, false, true, true);
	term->curs.y = find_last_nonempty_line(term, term->screen) + 1;
	if (term->curs.y == term->rows) {
	    term->curs.y--;
	    scroll(term, 0, term->rows - 1, 1, true);
	}
    } else {
	term->curs.y = 0;
    }
    term->curs.x = 0;
    term_schedule_tblink(term);
    term_schedule_cblink(term);
    term_schedule_update(term);
}

/*
 * Force a screen update.
 */
void term_update(Terminal *term)
{
    term->window_update_pending = false;

    if (term->win_move_pending) {
        win_move(term->win, term->win_move_pending_x,
                 term->win_move_pending_y);
        term->win_move_pending = false;
    }
    if (term->win_resize_pending) {
        win_request_resize(term->win, term->win_resize_pending_w,
                           term->win_resize_pending_h);
        term->win_resize_pending = false;
    }
    if (term->win_zorder_pending) {
        win_set_zorder(term->win, term->win_zorder_top);
        term->win_zorder_pending = false;
    }
    if (term->win_minimise_pending) {
        win_set_minimised(term->win, term->win_minimise_enable);
        term->win_minimise_pending = false;
    }
    if (term->win_maximise_pending) {
        win_set_maximised(term->win, term->win_maximise_enable);
        term->win_maximise_pending = false;
    }
    if (term->win_title_pending) {
        win_set_title(term->win, term->window_title);
        term->win_title_pending = false;
    }
    if (term->win_icon_title_pending) {
        win_set_icon_title(term->win, term->icon_title);
        term->win_icon_title_pending = false;
    }
    if (term->win_pointer_shape_pending) {
        win_set_raw_mouse_mode_pointer(term->win, term->win_pointer_shape_raw);
        term->win_pointer_shape_pending = false;
    }
    if (term->win_refresh_pending) {
        win_refresh(term->win);
        term->win_refresh_pending = false;
    }
    if (term->win_palette_pending) {
        unsigned start = term->win_palette_pending_min;
        unsigned ncolours = term->win_palette_pending_limit - start;
        win_palette_set(term->win, start, ncolours, term->palette + start);
        term->win_palette_pending = false;
    }

    if (win_setup_draw_ctx(term->win)) {
        bool need_sbar_update = term->seen_disp_event ||
            term->win_scrollbar_update_pending;
        term->win_scrollbar_update_pending = false;
	if (term->seen_disp_event && term->scroll_on_disp) {
	    term->disptop = 0;	       /* return to main screen */
	    term->seen_disp_event = false;
	    need_sbar_update = true;
	}

	if (need_sbar_update)
	    update_sbar(term);
	do_paint(term);
	win_set_cursor_pos(
            term->win, term->curs.x, term->curs.y - term->disptop);
	win_free_draw_ctx(term->win);
    }
}

/*
 * Called from front end when a keypress occurs, to trigger
 * anything magical that needs to happen in that situation.
 */
void term_seen_key_event(Terminal *term)
{
    /*
     * On any keypress, clear the bell overload mechanism
     * completely, on the grounds that large numbers of
     * beeps coming from deliberate key action are likely
     * to be intended (e.g. beeps from filename completion
     * blocking repeatedly).
     */
    term->beep_overloaded = false;
    while (term->beephead) {
	struct beeptime *tmp = term->beephead;
	term->beephead = tmp->next;
	sfree(tmp);
    }
    term->beeptail = NULL;
    term->nbeeps = 0;

    /*
     * Reset the scrollback on keypress, if we're doing that.
     */
    if (term->scroll_on_key) {
	term->disptop = 0;	       /* return to main screen */
	seen_disp_event(term);
    }
}

/*
 * Same as power_on(), but an external function.
 */
void term_pwron(Terminal *term, bool clear)
{
    power_on(term, clear);
    if (term->ldisc)		       /* cause ldisc to notice changes */
	ldisc_echoedit_update(term->ldisc);
    term->disptop = 0;
    deselect(term);
    term_update(term);
}

static void set_erase_char(Terminal *term)
{
    term->erase_char = term->basic_erase_char;
    if (term->use_bce) {
	term->erase_char.attr = (term->curr_attr &
				 (ATTR_FGMASK | ATTR_BGMASK));
        term->erase_char.truecolour.bg = term->curr_truecolour.bg;
    }
}

/*
 * We copy a bunch of stuff out of the Conf structure into local
 * fields in the Terminal structure, to avoid the repeated tree234
 * lookups which would be involved in fetching them from the former
 * every time.
 */
void term_copy_stuff_from_conf(Terminal *term)
{
    term->ansi_colour = conf_get_bool(term->conf, CONF_ansi_colour);
    term->no_arabicshaping = conf_get_bool(term->conf, CONF_no_arabicshaping);
    term->beep = conf_get_int(term->conf, CONF_beep);
    term->bellovl = conf_get_bool(term->conf, CONF_bellovl);
    term->bellovl_n = conf_get_int(term->conf, CONF_bellovl_n);
    term->bellovl_s = conf_get_int(term->conf, CONF_bellovl_s);
    term->bellovl_t = conf_get_int(term->conf, CONF_bellovl_t);
    term->no_bidi = conf_get_bool(term->conf, CONF_no_bidi);
    term->bksp_is_delete = conf_get_bool(term->conf, CONF_bksp_is_delete);
    term->blink_cur = conf_get_bool(term->conf, CONF_blink_cur);
    term->blinktext = conf_get_bool(term->conf, CONF_blinktext);
    term->cjk_ambig_wide = conf_get_bool(term->conf, CONF_cjk_ambig_wide);
    term->conf_height = conf_get_int(term->conf, CONF_height);
    term->conf_width = conf_get_int(term->conf, CONF_width);
    term->crhaslf = conf_get_bool(term->conf, CONF_crhaslf);
    term->erase_to_scrollback = conf_get_bool(term->conf, CONF_erase_to_scrollback);
    term->funky_type = conf_get_int(term->conf, CONF_funky_type);
    term->lfhascr = conf_get_bool(term->conf, CONF_lfhascr);
    term->logflush = conf_get_bool(term->conf, CONF_logflush);
    term->logtype = conf_get_int(term->conf, CONF_logtype);
    term->mouse_override = conf_get_bool(term->conf, CONF_mouse_override);
    term->nethack_keypad = conf_get_bool(term->conf, CONF_nethack_keypad);
    term->no_alt_screen = conf_get_bool(term->conf, CONF_no_alt_screen);
    term->no_applic_c = conf_get_bool(term->conf, CONF_no_applic_c);
    term->no_applic_k = conf_get_bool(term->conf, CONF_no_applic_k);
    term->no_dbackspace = conf_get_bool(term->conf, CONF_no_dbackspace);
    term->no_mouse_rep = conf_get_bool(term->conf, CONF_no_mouse_rep);
#ifdef MOD_PERSO
    term->no_focus_rep = conf_get_bool(term->conf, CONF_no_focus_rep);
#endif
    term->no_remote_charset = conf_get_bool(term->conf, CONF_no_remote_charset);
    term->no_remote_resize = conf_get_bool(term->conf, CONF_no_remote_resize);
    term->no_remote_wintitle = conf_get_bool(term->conf, CONF_no_remote_wintitle);
    term->no_remote_clearscroll = conf_get_bool(term->conf, CONF_no_remote_clearscroll);
    term->rawcnp = conf_get_bool(term->conf, CONF_rawcnp);
    term->utf8linedraw = conf_get_bool(term->conf, CONF_utf8linedraw);
    term->rect_select = conf_get_bool(term->conf, CONF_rect_select);
    term->remote_qtitle_action = conf_get_int(term->conf, CONF_remote_qtitle_action);
#ifdef MOD_PERSO
    term->enter_sends_crlf = conf_get_int(term->conf, CONF_enter_sends_crlf);
    term->rxvt_homeend = conf_get_int(term->conf, CONF_rxvt_homeend);
#else
    term->rxvt_homeend = conf_get_bool(term->conf, CONF_rxvt_homeend);
#endif
    term->scroll_on_disp = conf_get_bool(term->conf, CONF_scroll_on_disp);
    term->scroll_on_key = conf_get_bool(term->conf, CONF_scroll_on_key);
    term->xterm_mouse_forbidden = conf_get_bool(term->conf, CONF_no_mouse_rep);
    term->xterm_256_colour = conf_get_bool(term->conf, CONF_xterm_256_colour);
    term->true_colour = conf_get_bool(term->conf, CONF_true_colour);

    /*
     * Parse the control-character escapes in the configured
     * answerback string.
     */
    {
	char *answerback = conf_get_str(term->conf, CONF_answerback);
	int maxlen = strlen(answerback);

	term->answerback = snewn(maxlen, char);
	term->answerbacklen = 0;

	while (*answerback) {
	    char *n;
	    char c = ctrlparse(answerback, &n);
	    if (n) {
		term->answerback[term->answerbacklen++] = c;
		answerback = n;
	    } else {
		term->answerback[term->answerbacklen++] = *answerback++;
	    }
	}
    }
}

void term_pre_reconfig(Terminal *term, Conf *conf)
{

    /*
     * Copy the current window title into the stored previous
     * configuration, so that doing nothing to the window title field
     * in the config box doesn't reset the title to its startup state.
     */
    conf_set_str(conf, CONF_wintitle, term->window_title);
}

/*
 * When the user reconfigures us, we need to check the forbidden-
 * alternate-screen config option, disable raw mouse mode if the
 * user has disabled mouse reporting, and abandon a print job if
 * the user has disabled printing.
 */
void term_reconfig(Terminal *term, Conf *conf)
{
    /*
     * Before adopting the new config, check all those terminal
     * settings which control power-on defaults; and if they've
     * changed, we will modify the current state as well as the
     * default one. The full list is: Auto wrap mode, DEC Origin
     * Mode, BCE, blinking text, character classes.
     */
    bool reset_wrap, reset_decom, reset_bce, reset_tblink, reset_charclass;
    bool palette_changed = false;
    int i;

    reset_wrap = (conf_get_bool(term->conf, CONF_wrap_mode) !=
		  conf_get_bool(conf, CONF_wrap_mode));
    reset_decom = (conf_get_bool(term->conf, CONF_dec_om) !=
		   conf_get_bool(conf, CONF_dec_om));
    reset_bce = (conf_get_bool(term->conf, CONF_bce) !=
		 conf_get_bool(conf, CONF_bce));
    reset_tblink = (conf_get_bool(term->conf, CONF_blinktext) !=
		    conf_get_bool(conf, CONF_blinktext));
    reset_charclass = false;
    for (i = 0; i < 256; i++)
	if (conf_get_int_int(term->conf, CONF_wordness, i) !=
	    conf_get_int_int(conf, CONF_wordness, i))
	    reset_charclass = true;

    /*
     * If the bidi or shaping settings have changed, flush the bidi
     * cache completely.
     */
    if (conf_get_bool(term->conf, CONF_no_arabicshaping) !=
	conf_get_bool(conf, CONF_no_arabicshaping) ||
	conf_get_bool(term->conf, CONF_no_bidi) !=
	conf_get_bool(conf, CONF_no_bidi)) {
	for (i = 0; i < term->bidi_cache_size; i++) {
	    sfree(term->pre_bidi_cache[i].chars);
	    sfree(term->post_bidi_cache[i].chars);
	    term->pre_bidi_cache[i].width = -1;
	    term->pre_bidi_cache[i].chars = NULL;
	    term->post_bidi_cache[i].width = -1;
	    term->post_bidi_cache[i].chars = NULL;
	}
    }

    {
        const char *old_title = conf_get_str(term->conf, CONF_wintitle);
        const char *new_title = conf_get_str(conf, CONF_wintitle);
        if (strcmp(old_title, new_title)) {
            sfree(term->window_title);
            term->window_title = dupstr(new_title);
            term->win_title_pending = true;
            term_schedule_update(term);
        }
    }

    /*
     * Just setting conf is sufficient to cause colour setting changes
     * to appear on the next ESC]R palette reset. But we should also
     * check whether any colour settings have been changed, so that
     * they can be updated immediately if they haven't been overridden
     * by some escape sequence.
     */
    {
        int i, j;
        for (i = 0; i < CONF_NCOLOURS; i++) {
            for (j = 0; j < 3; j++)
                if (conf_get_int_int(term->conf, CONF_colours, i*3+j) !=
                    conf_get_int_int(conf, CONF_colours, i*3+j))
                    break;
            if (j < 3) {
                /* Actually enacting the change has to be deferred 
                 * until the new conf is installed. */
                palette_changed = true;
                break;
            }
        }
    }

    conf_free(term->conf);
    term->conf = conf_copy(conf);

    if (reset_wrap)
	term->alt_wrap = term->wrap = conf_get_bool(term->conf, CONF_wrap_mode);
    if (reset_decom)
	term->alt_om = term->dec_om = conf_get_bool(term->conf, CONF_dec_om);
    if (reset_bce) {
	term->use_bce = conf_get_bool(term->conf, CONF_bce);
	set_erase_char(term);
    }
    if (reset_tblink) {
	term->blink_is_real = conf_get_bool(term->conf, CONF_blinktext);
    }
    if (reset_charclass)
	for (i = 0; i < 256; i++)
	    term->wordness[i] = conf_get_int_int(term->conf, CONF_wordness, i);

    if (conf_get_bool(term->conf, CONF_no_alt_screen))
	swap_screen(term, 0, false, false);
    if (conf_get_bool(term->conf, CONF_no_remote_charset)) {
	term->cset_attr[0] = term->cset_attr[1] = CSET_ASCII;
	term->sco_acs = term->alt_sco_acs = 0;
	term->utf = false;
    }
#ifdef MOD_PRINTCLIP
    if (!conf_get_str(term->conf, CONF_printer) || conf_get_int(term->conf, CONF_printclip) ) {
#else
    if (!conf_get_str(term->conf, CONF_printer)) {
#endif
	term_print_finish(term);
    }
    if (palette_changed)
        term_notify_palette_changed(term);
    term_schedule_tblink(term);
    term_schedule_cblink(term);
    term_copy_stuff_from_conf(term);
    term_update_raw_mouse_mode(term);
}

/*
 * Clear the scrollback.
 */
void term_clrsb(Terminal *term)
{
    unsigned char *line;
    int i;

    /*
     * Scroll forward to the current screen, if we were back in the
     * scrollback somewhere until now.
     */
    term->disptop = 0;

    /*
     * Clear the actual scrollback.
     */
    while ((line = delpos234(term->scrollback, 0)) != NULL) {
	sfree(line);            /* this is compressed data, not a termline */
    }

    /*
     * When clearing the scrollback, we also truncate any termlines on
     * the current screen which have remembered data from a previous
     * larger window size. Rationale: clearing the scrollback is
     * sometimes done to protect privacy, so the user intention is
     * specifically that we should not retain evidence of what
     * previously happened in the terminal, and that ought to include
     * evidence to the right as well as evidence above.
     */
    for (i = 0; i < term->rows; i++)
        check_line_size(term, scrlineptr(i));

    /*
     * That operation has invalidated the selection, if it overlapped
     * the scrollback at all.
     */
    if (term->selstate != NO_SELECTION && term->selstart.y < 0)
        deselect(term);

    /*
     * There are now no lines of real scrollback which can be pulled
     * back into the screen by a resize, and no lines of the alternate
     * screen which should be displayed as if part of the scrollback.
     */
    term->tempsblines = 0;
    term->alt_sblines = 0;

    /*
     * The scrollbar will need updating to reflect the new state of
     * the world.
     */
    term->win_scrollbar_update_pending = true;
    term_schedule_update(term);
}

const optionalrgb optionalrgb_none = {0, 0, 0, 0};

void term_setup_window_titles(Terminal *term, const char *title_hostname)
{
    const char *conf_title = conf_get_str(term->conf, CONF_wintitle);
    sfree(term->window_title);
    sfree(term->icon_title);
    if (*conf_title) {
        term->window_title = dupstr(conf_title);
        term->icon_title = dupstr(conf_title);
    } else {
        if (title_hostname && *title_hostname)
            term->window_title = dupcat(title_hostname, " - ", appname);
        else
            term->window_title = dupstr(appname);
        term->icon_title = dupstr(term->window_title);
    }
    term->win_title_pending = true;
    term->win_icon_title_pending = true;
}

static void palette_rebuild(Terminal *term)
{
    unsigned min_changed = OSC4_NCOLOURS, max_changed = 0;

    if (term->win_palette_pending) {
        /* Possibly extend existing range. */
        min_changed = term->win_palette_pending_min;
        max_changed = term->win_palette_pending_limit - 1;
    } else {
        /* Start with empty range. */
        min_changed = OSC4_NCOLOURS;
        max_changed = 0;
    }

    for (unsigned i = 0; i < OSC4_NCOLOURS; i++) {
        rgb new_value;
        bool found = false;

        for (unsigned j = lenof(term->subpalettes); j-- > 0 ;) {
            if (term->subpalettes[j].present[i]) {
                new_value = term->subpalettes[j].values[i];
                found = true;
                break;
            }
        }

        assert(found);    /* we expect SUBPAL_CONF to always be set */

        if (new_value.r != term->palette[i].r ||
            new_value.g != term->palette[i].g ||
            new_value.b != term->palette[i].b) {
            term->palette[i] = new_value;
            if (min_changed > i)
                min_changed = i;
            if (max_changed < i)
                max_changed = i;
        }
    }

    if (min_changed <= max_changed) {
        /*
         * At least one colour changed (or we had an update scheduled
         * already). Schedule a redraw event to pass the result back
         * to the TermWin. This also requires invalidating the rest
         * of the window, because usually all the text will need
         * redrawing in the new colours.
         * (If there was an update pending and this palette rebuild
         * didn't actually change anything, we'll harmlessly reinforce
         * the existing update request.)
         */
        term->win_palette_pending = true;
        term->win_palette_pending_min = min_changed;
        term->win_palette_pending_limit = max_changed + 1;
        term_invalidate(term);
        term_schedule_update(term);
    }
}

/*
 * Rebuild the palette from configuration and platform colours.
 * If 'keep_overrides' set, any escape-sequence-specified overrides will
 * remain in place.
 */
static void palette_reset(Terminal *term, bool keep_overrides)
{
        for (unsigned i = 0; i < OSC4_NCOLOURS; i++)
            term->subpalettes[SUBPAL_CONF].present[i] = true;
        /*
         * Copy all the palette information out of the Conf.
         */
        for (unsigned i = 0; i < CONF_NCOLOURS; i++) {
            rgb *col = &term->subpalettes[SUBPAL_CONF].values[
                colour_indices_conf_to_osc4[i]];
            col->r = conf_get_int_int(term->conf, CONF_colours, i*3+0);
            col->g = conf_get_int_int(term->conf, CONF_colours, i*3+1);
            col->b = conf_get_int_int(term->conf, CONF_colours, i*3+2);
        }
        /*
         * Directly invent the rest of the xterm-256 colours.
         */
        for (unsigned i = 0; i < 216; i++) {
            rgb *col = &term->subpalettes[SUBPAL_CONF].values[i + 16];
            int r = i / 36, g = (i / 6) % 6, b = i % 6;
            col->r = r ? r * 40 + 55 : 0;
            col->g = g ? g * 40 + 55 : 0;
            col->b = b ? b * 40 + 55 : 0;
        }
        for (unsigned i = 0; i < 24; i++) {
            rgb *col = &term->subpalettes[SUBPAL_CONF].values[i + 232];
            int shade = i * 10 + 8;
            col->r = col->g = col->b = shade;
        }
    /*
     * Re-fetch any OS-local overrides.
     */
    for (unsigned i = 0; i < OSC4_NCOLOURS; i++)
        term->subpalettes[SUBPAL_PLATFORM].present[i] = false;
    win_palette_get_overrides(term->win, term);

    if (!keep_overrides) {
        /*
         * Get rid of all escape-sequence configuration.
         */
        for (unsigned i = 0; i < OSC4_NCOLOURS; i++)
            term->subpalettes[SUBPAL_SESSION].present[i] = false;
    }

    /*
     * Rebuild the composite palette.
     */
    palette_rebuild(term);
}

void term_palette_override(Terminal *term, unsigned osc4_index, rgb rgb)
{
    /*
     * We never expect to be called except as re-entry from our own
     * call to win_palette_get_overrides above, so we need not mess
     * about calling palette_rebuild.
     */
    term->subpalettes[SUBPAL_PLATFORM].present[osc4_index] = true;
    term->subpalettes[SUBPAL_PLATFORM].values[osc4_index] = rgb;
}

/*
 * Initialise the terminal.
 */
Terminal *term_init(Conf *myconf, struct unicode_data *ucsdata, TermWin *win)
{
    Terminal *term;

    /*
     * Allocate a new Terminal structure and initialise the fields
     * that need it.
     */
    term = snew(Terminal);
#ifdef MOD_FAR2L
    /* far2l */
    term->far2l_ext = 0; // far2l extensions mode is enabled?
    term->is_apc = 0; // currently processing incoming APC sequence?
    term->clip_allowed = -1; // remote clipboard access is enabled?
#endif
    term->win = win;
    term->ucsdata = ucsdata;
    term->conf = conf_copy(myconf);
    term->logctx = NULL;
    term->compatibility_level = TM_PUTTY;
    strcpy(term->id_string, "\033[?6c");
    term->cblink_pending = term->tblink_pending = false;
    term->paste_buffer = NULL;
    term->paste_len = 0;
    bufchain_init(&term->inbuf);
    bufchain_init(&term->printer_buf);
    term->printing = term->only_printing = false;
    term->print_job = NULL;
    term->vt52_mode = false;
    term->cr_lf_return = false;
    term->seen_disp_event = false;
    term->mouse_is_down = 0;
    term->reset_132 = false;
    term->cblinker = false;
    term->tblinker = false;
    term->has_focus = true;
    term->repeat_off = false;
    term->termstate = TOPLEVEL;
    term->selstate = NO_SELECTION;
    term->curstype = 0;
#ifdef MOD_PERSO
	term->no_focus_rep = true;
	term->report_focus = false;
#endif

    term_copy_stuff_from_conf(term);

    term->screen = term->alt_screen = term->scrollback = NULL;
    term->tempsblines = 0;
    term->alt_sblines = 0;
    term->disptop = 0;
    term->disptext = NULL;
    term->dispcursx = term->dispcursy = -1;
    term->tabs = NULL;
    deselect(term);
    term->rows = term->cols = -1;
    power_on(term, true);
    term->beephead = term->beeptail = NULL;
    term->nbeeps = 0;
    term->lastbeep = false;
    term->beep_overloaded = false;
    term->attr_mask = 0xffffffff;
    term->backend = NULL;
    term->in_term_out = false;
    term->ltemp = NULL;
    term->ltemp_size = 0;
    term->wcFrom = NULL;
    term->wcTo = NULL;
    term->wcFromTo_size = 0;

    term->window_update_pending = false;
    term->window_update_cooldown = false;

    term->bidi_cache_size = 0;
    term->pre_bidi_cache = term->post_bidi_cache = NULL;
#ifdef MOD_HYPERLINK
	/*
	 * HACK: PuttyTray / Nutty
	 */
	if( !GetPuttyFlag() && GetHyperlinkFlag() ) term->url_update = TRUE;
#endif

    /* FULL-TERMCHAR */
    term->basic_erase_char.chr = CSET_ASCII | ' ';
    term->basic_erase_char.attr = ATTR_DEFAULT;
    term->basic_erase_char.cc_next = 0;
    term->basic_erase_char.truecolour.fg = optionalrgb_none;
    term->basic_erase_char.truecolour.bg = optionalrgb_none;
    term->erase_char = term->basic_erase_char;

    term->last_selected_text = NULL;
    term->last_selected_attr = NULL;
    term->last_selected_tc = NULL;
    term->last_selected_len = 0;
    /* TermWin implementations will typically extend these with
     * clipboard ids they know about */
    term->mouse_select_clipboards[0] = CLIP_LOCAL;
    term->n_mouse_select_clipboards = 1;
    term->mouse_paste_clipboard = CLIP_NULL;

    term->last_graphic_char = 0;

    term->trusted = true;
#ifdef MOD_ZMODEM
    term->xyz_transfering = 0;
    term->xyz_Internals = NULL;
#endif

    term->bracketed_paste_active = false;

    term->window_title = dupstr("");
    term->icon_title = dupstr("");
    term->minimised = false;
    term->winpos_x = term->winpos_y = 0;
    term->winpixsize_x = term->winpixsize_y = 0;

    term->win_move_pending = false;
    term->win_resize_pending = false;
    term->win_zorder_pending = false;
    term->win_minimise_pending = false;
    term->win_maximise_pending = false;
    term->win_title_pending = false;
    term->win_icon_title_pending = false;
    term->win_pointer_shape_pending = false;
    term->win_refresh_pending = false;
    term->win_scrollbar_update_pending = false;
    term->win_palette_pending = false;

    palette_reset(term, false);

    return term;
}

void term_free(Terminal *term)
{
    termline *line;
    struct beeptime *beep;
    int i;

    while ((line = delpos234(term->scrollback, 0)) != NULL)
	sfree(line);		       /* compressed data, not a termline */
    freetree234(term->scrollback);
    while ((line = delpos234(term->screen, 0)) != NULL)
	freetermline(line);
    freetree234(term->screen);
    while ((line = delpos234(term->alt_screen, 0)) != NULL)
	freetermline(line);
    freetree234(term->alt_screen);
    if (term->disptext) {
	for (i = 0; i < term->rows; i++)
	    freetermline(term->disptext[i]);
    }
    sfree(term->disptext);
    while (term->beephead) {
	beep = term->beephead;
	term->beephead = beep->next;
	sfree(beep);
    }
    bufchain_clear(&term->inbuf);
    if(term->print_job)
	printer_finish_job(term->print_job);
    bufchain_clear(&term->printer_buf);
    sfree(term->paste_buffer);
    sfree(term->ltemp);
    sfree(term->wcFrom);
    sfree(term->wcTo);
    sfree(term->answerback);

    for (i = 0; i < term->bidi_cache_size; i++) {
	sfree(term->pre_bidi_cache[i].chars);
	sfree(term->post_bidi_cache[i].chars);
        sfree(term->post_bidi_cache[i].forward);
        sfree(term->post_bidi_cache[i].backward);
    }
    sfree(term->pre_bidi_cache);
    sfree(term->post_bidi_cache);

    sfree(term->tabs);

    expire_timer_context(term);
    delete_callbacks_for_context(term);

    conf_free(term->conf);

    sfree(term->window_title);
    sfree(term->icon_title);

    sfree(term);
}

void term_set_trust_status(Terminal *term, bool trusted)
{
    term->trusted = trusted;
}

void term_get_cursor_position(Terminal *term, int *x, int *y)
{
    *x = term->curs.x;
    *y = term->curs.y;
}

/*
 * Set up the terminal for a given size.
 */
void term_size(Terminal *term, int newrows, int newcols, int newsavelines)
{
    tree234 *newalt;
    termline **newdisp, *line;
    int i, j, oldrows = term->rows;
    int sblen;
    int save_alt_which = term->alt_which;

    if (newrows == term->rows && newcols == term->cols &&
	newsavelines == term->savelines)
	return;			       /* nothing to do */

    /* Behave sensibly if we're given zero (or negative) rows/cols */

    if (newrows < 1) newrows = 1;
    if (newcols < 1) newcols = 1;

    deselect(term);
    swap_screen(term, 0, false, false);

    term->alt_t = term->marg_t = 0;
    term->alt_b = term->marg_b = newrows - 1;

    if (term->rows == -1) {
	term->scrollback = newtree234(NULL);
	term->screen = newtree234(NULL);
	term->tempsblines = 0;
	term->rows = 0;
    }

    /*
     * Resize the screen and scrollback. We only need to shift
     * lines around within our data structures, because lineptr()
     * will take care of resizing each individual line if
     * necessary. So:
     * 
     *  - If the new screen is longer, we shunt lines in from temporary
     *    scrollback if possible, otherwise we add new blank lines at
     *    the bottom.
     *
     *  - If the new screen is shorter, we remove any blank lines at
     *    the bottom if possible, otherwise shunt lines above the cursor
     *    to scrollback if possible, otherwise delete lines below the
     *    cursor.
     * 
     *  - Then, if the new scrollback length is less than the
     *    amount of scrollback we actually have, we must throw some
     *    away.
     */
    sblen = count234(term->scrollback);
    /* Do this loop to expand the screen if newrows > rows */
    assert(term->rows == count234(term->screen));
    while (term->rows < newrows) {
	if (term->tempsblines > 0) {
	    compressed_scrollback_line *cline;
	    /* Insert a line from the scrollback at the top of the screen. */
	    assert(sblen >= term->tempsblines);
	    cline = delpos234(term->scrollback, --sblen);
	    line = decompressline(cline);
	    sfree(cline);
	    line->temporary = false;   /* reconstituted line is now real */
	    term->tempsblines -= 1;
	    addpos234(term->screen, line, 0);
	    term->curs.y += 1;
	    term->savecurs.y += 1;
	    term->alt_y += 1;
	    term->alt_savecurs.y += 1;
	} else {
	    /* Add a new blank line at the bottom of the screen. */
	    line = newtermline(term, newcols, false);
	    addpos234(term->screen, line, count234(term->screen));
	}
	term->rows += 1;
    }
    /* Do this loop to shrink the screen if newrows < rows */
    while (term->rows > newrows) {
	if (term->curs.y < term->rows - 1) {
	    /* delete bottom row, unless it contains the cursor */
            line = delpos234(term->screen, term->rows - 1);
            freetermline(line);
	} else {
	    /* push top row to scrollback */
	    line = delpos234(term->screen, 0);
	    addpos234(term->scrollback, compressline(line), sblen++);
	    freetermline(line);
	    term->tempsblines += 1;
	    term->curs.y -= 1;
	    term->savecurs.y -= 1;
	    term->alt_y -= 1;
	    term->alt_savecurs.y -= 1;
	}
	term->rows -= 1;
    }
    assert(term->rows == newrows);
    assert(count234(term->screen) == newrows);

    /* Delete any excess lines from the scrollback. */
    while (sblen > newsavelines) {
	line = delpos234(term->scrollback, 0);
	sfree(line);
	sblen--;
    }
    if (sblen < term->tempsblines)
	term->tempsblines = sblen;
    assert(count234(term->scrollback) <= newsavelines);
    assert(count234(term->scrollback) >= term->tempsblines);
    term->disptop = 0;

    /* Make a new displayed text buffer. */
    newdisp = snewn(newrows, termline *);
    for (i = 0; i < newrows; i++) {
	newdisp[i] = newtermline(term, newcols, false);
	for (j = 0; j < newcols; j++)
	    newdisp[i]->chars[j].attr = ATTR_INVALID;
    }
    if (term->disptext) {
	for (i = 0; i < oldrows; i++)
	    freetermline(term->disptext[i]);
    }
    sfree(term->disptext);
    term->disptext = newdisp;
    term->dispcursx = term->dispcursy = -1;

    /* Make a new alternate screen. */
    newalt = newtree234(NULL);
    for (i = 0; i < newrows; i++) {
	line = newtermline(term, newcols, true);
	addpos234(newalt, line, i);
    }
    if (term->alt_screen) {
	while (NULL != (line = delpos234(term->alt_screen, 0)))
	    freetermline(line);
	freetree234(term->alt_screen);
    }
    term->alt_screen = newalt;
    term->alt_sblines = 0;

    term->tabs = sresize(term->tabs, newcols, unsigned char);
    {
	int i;
	for (i = (term->cols > 0 ? term->cols : 0); i < newcols; i++)
	    term->tabs[i] = (i % 8 == 0 ? true : false);
    }

    /* Check that the cursor positions are still valid. */
    if (term->savecurs.y < 0)
	term->savecurs.y = 0;
    if (term->savecurs.y >= newrows)
	term->savecurs.y = newrows - 1;
    if (term->savecurs.x >= newcols)
	term->savecurs.x = newcols - 1;
    if (term->alt_savecurs.y < 0)
	term->alt_savecurs.y = 0;
    if (term->alt_savecurs.y >= newrows)
	term->alt_savecurs.y = newrows - 1;
    if (term->alt_savecurs.x >= newcols)
	term->alt_savecurs.x = newcols - 1;
    if (term->curs.y < 0)
	term->curs.y = 0;
    if (term->curs.y >= newrows)
	term->curs.y = newrows - 1;
    if (term->curs.x >= newcols)
	term->curs.x = newcols - 1;
    if (term->alt_y < 0)
	term->alt_y = 0;
    if (term->alt_y >= newrows)
	term->alt_y = newrows - 1;
    if (term->alt_x >= newcols)
	term->alt_x = newcols - 1;
    term->alt_x = term->alt_y = 0;
    term->wrapnext = false;
    term->alt_wnext = false;

    term->rows = newrows;
    term->cols = newcols;
    term->savelines = newsavelines;

    swap_screen(term, save_alt_which, false, false);

    term->win_scrollbar_update_pending = true;
    term_schedule_update(term);
    if (term->backend)
        backend_size(term->backend, term->cols, term->rows);
}

/*
 * Hand a backend to the terminal, so it can be notified of resizes.
 */
void term_provide_backend(Terminal *term, Backend *backend)
{
    term->backend = backend;
    if (term->backend && term->cols > 0 && term->rows > 0)
        backend_size(term->backend, term->cols, term->rows);
}

/* Find the bottom line on the screen that has any content.
 * If only the top line has content, returns 0.
 * If no lines have content, return -1.
 */ 
static int find_last_nonempty_line(Terminal * term, tree234 * screen)
{
    int i;
    for (i = count234(screen) - 1; i >= 0; i--) {
	termline *line = index234(screen, i);
	int j;
#ifdef MOD_HYPERLINK
	assert(term->erase_char.cc_next == 0);
#endif
	for (j = 0; j < line->cols; j++)
	    if (!termchars_equal(&line->chars[j], &term->erase_char))
		break;
	if (j != line->cols) break;
    }
    return i;
}

/*
 * Swap screens. If `reset' is true and we have been asked to
 * switch to the alternate screen, we must bring most of its
 * configuration from the main screen and erase the contents of the
 * alternate screen completely. (This is even true if we're already
 * on it! Blame xterm.)
 */
static void swap_screen(Terminal *term, int which,
                        bool reset, bool keep_cur_pos)
{
    int t;
    bool bt;
    pos tp;
    truecolour ttc;
    tree234 *ttr;

    if (!which)
	reset = false;		       /* do no weird resetting if which==0 */

    if (which != term->alt_which) {
        if (term->erase_to_scrollback && term->alt_screen &&
            term->alt_which && term->disptop < 0) {
            /*
             * We're swapping away from the alternate screen, so some
             * lines are about to vanish from the virtual scrollback.
             * Adjust disptop by that much, so that (if we're not
             * resetting the scrollback anyway on a display event) the
             * current scroll position still ends up pointing at the
             * same text.
             */
            term->disptop += term->alt_sblines;
            if (term->disptop > 0)
                term->disptop = 0;
        }

	term->alt_which = which;

	ttr = term->alt_screen;
	term->alt_screen = term->screen;
	term->screen = ttr;
        term->alt_sblines = (
            term->alt_screen ?
            find_last_nonempty_line(term, term->alt_screen) + 1 : 0);
	t = term->curs.x;
	if (!reset && !keep_cur_pos)
	    term->curs.x = term->alt_x;
	term->alt_x = t;
	t = term->curs.y;
	if (!reset && !keep_cur_pos)
	    term->curs.y = term->alt_y;
	term->alt_y = t;
	t = term->marg_t;
	if (!reset) term->marg_t = term->alt_t;
	term->alt_t = t;
	t = term->marg_b;
	if (!reset) term->marg_b = term->alt_b;
	term->alt_b = t;
	bt = term->dec_om;
	if (!reset) term->dec_om = term->alt_om;
	term->alt_om = bt;
	bt = term->wrap;
	if (!reset) term->wrap = term->alt_wrap;
	term->alt_wrap = bt;
	bt = term->wrapnext;
	if (!reset) term->wrapnext = term->alt_wnext;
	term->alt_wnext = bt;
	bt = term->insert;
	if (!reset) term->insert = term->alt_ins;
	term->alt_ins = bt;
	t = term->cset;
	if (!reset) term->cset = term->alt_cset;
	term->alt_cset = t;
	bt = term->utf;
	if (!reset) term->utf = term->alt_utf;
	term->alt_utf = bt;
	t = term->sco_acs;
	if (!reset) term->sco_acs = term->alt_sco_acs;
	term->alt_sco_acs = t;

	tp = term->savecurs;
        if (!reset)
	    term->savecurs = term->alt_savecurs;
	term->alt_savecurs = tp;
        t = term->save_cset;
        if (!reset)
            term->save_cset = term->alt_save_cset;
        term->alt_save_cset = t;
        t = term->save_csattr;
        if (!reset)
            term->save_csattr = term->alt_save_csattr;
        term->alt_save_csattr = t;
        t = term->save_attr;
        if (!reset)
            term->save_attr = term->alt_save_attr;
        term->alt_save_attr = t;
        ttc = term->save_truecolour;
        if (!reset)
            term->save_truecolour = term->alt_save_truecolour;
        term->alt_save_truecolour = ttc;
        bt = term->save_utf;
        if (!reset)
            term->save_utf = term->alt_save_utf;
        term->alt_save_utf = bt;
        bt = term->save_wnext;
        if (!reset)
            term->save_wnext = term->alt_save_wnext;
        term->alt_save_wnext = bt;
        t = term->save_sco_acs;
        if (!reset)
            term->save_sco_acs = term->alt_save_sco_acs;
        term->alt_save_sco_acs = t;

        if (term->erase_to_scrollback && term->alt_screen &&
            term->alt_which && term->disptop < 0) {
            /*
             * Inverse of the adjustment at the top of this function.
             * This time, we're swapping _to_ the alternate screen, so
             * some lines are about to _appear_ in the virtual
             * scrollback, and we adjust disptop in the other
             * direction.
             *
             * Both these adjustments depend on the value stored in
             * term->alt_sblines while the alt screen is selected,
             * which is why we had to do one _before_ switching away
             * from it and the other _after_ switching to it.
             */
            term->disptop -= term->alt_sblines;
            int limit = -sblines(term);
            if (term->disptop < limit)
                term->disptop = limit;
        }
    }

    if (reset && term->screen) {
	/*
	 * Yes, this _is_ supposed to honour background-colour-erase.
	 */
	erase_lots(term, false, true, true);
    }
}

/*
 * Update the scroll bar.
 */
static void update_sbar(Terminal *term)
{
    int nscroll = sblines(term);
    win_set_scrollbar(term->win, nscroll + term->rows,
                      nscroll + term->disptop, term->rows);
}

/*
 * Check whether the region bounded by the two pointers intersects
 * the scroll region, and de-select the on-screen selection if so.
 */
static void check_selection(Terminal *term, pos from, pos to)
{
    if (poslt(from, term->selend) && poslt(term->selstart, to))
	deselect(term);
}

static void clear_line(Terminal *term, termline *line)
{
    resizeline(term, line, term->cols);
    for (int i = 0; i < term->cols; i++)
        copy_termchar(line, i, &term->erase_char);
    line->lattr = LATTR_NORM;
}

static void check_trust_status(Terminal *term, termline *line)
{
    if (line->trusted != term->trusted) {
        /*
         * If we're displaying trusted output on a previously
         * untrusted line, or vice versa, we need to switch the
         * 'trusted' attribute on this terminal line, and also clear
         * all its previous contents.
         */
        clear_line(term, line);
        line->trusted = term->trusted;
    }
}

/*
 * Scroll the screen. (`lines' is +ve for scrolling forward, -ve
 * for backward.) `sb' is true if the scrolling is permitted to
 * affect the scrollback buffer.
 */
static void scroll(Terminal *term, int topline, int botline,
                   int lines, bool sb)
{
    termline *line;
    int seltop, scrollwinsize;

    if (topline != 0 || term->alt_which != 0)
	sb = false;

    scrollwinsize = botline - topline + 1;

    if (lines < 0) {
        lines = -lines;
        if (lines > scrollwinsize)
            lines = scrollwinsize;
	while (lines-- > 0) {
	    line = delpos234(term->screen, botline);
            resizeline(term, line, term->cols);
            clear_line(term, line);
	    addpos234(term->screen, line, topline);

	    if (term->selstart.y >= topline && term->selstart.y <= botline) {
		term->selstart.y++;
		if (term->selstart.y > botline) {
		    term->selstart.y = botline + 1;
		    term->selstart.x = 0;
		}
	    }
	    if (term->selend.y >= topline && term->selend.y <= botline) {
		term->selend.y++;
		if (term->selend.y > botline) {
		    term->selend.y = botline + 1;
		    term->selend.x = 0;
		}
	    }
	}
    } else {
        if (lines > scrollwinsize)
            lines = scrollwinsize;
	while (lines-- > 0) {
	    line = delpos234(term->screen, topline);
#ifdef TERM_CC_DIAGS
	    cc_check(line);
#endif
	    if (sb && term->savelines > 0) {
		int sblen = count234(term->scrollback);
		/*
		 * We must add this line to the scrollback. We'll
		 * remove a line from the top of the scrollback if
		 * the scrollback is full.
		 */
		if (sblen == term->savelines) {
		    unsigned char *cline;

		    sblen--;
		    cline = delpos234(term->scrollback, 0);
		    sfree(cline);
		} else
		    term->tempsblines += 1;

		addpos234(term->scrollback, compressline(line), sblen);

		/* now `line' itself can be reused as the bottom line */

		/*
		 * If the user is currently looking at part of the
		 * scrollback, and they haven't enabled any options
		 * that are going to reset the scrollback as a
		 * result of this movement, then the chances are
		 * they'd like to keep looking at the same line. So
		 * we move their viewpoint at the same rate as the
		 * scroll, at least until their viewpoint hits the
		 * top end of the scrollback buffer, at which point
		 * we don't have the choice any more.
		 * 
		 * Thanks to Jan Holmen Holsten for the idea and
		 * initial implementation.
		 */
		if (term->disptop > -term->savelines && term->disptop < 0)
		    term->disptop--;
	    }
            resizeline(term, line, term->cols);
#ifdef MOD_HYPERLINK
            assert(term->erase_char.cc_next == 0);
#endif
            clear_line(term, line);
            check_trust_status(term, line);
	    addpos234(term->screen, line, botline);

	    /*
	     * If the selection endpoints move into the scrollback,
	     * we keep them moving until they hit the top. However,
	     * of course, if the line _hasn't_ moved into the
	     * scrollback then we don't do this, and cut them off
	     * at the top of the scroll region.
	     * 
	     * This applies to selstart and selend (for an existing
	     * selection), and also selanchor (for one being
	     * selected as we speak).
	     */
	    seltop = sb ? -term->savelines : topline;

	    if (term->selstate != NO_SELECTION) {
		if (term->selstart.y >= seltop &&
		    term->selstart.y <= botline) {
		    term->selstart.y--;
		    if (term->selstart.y < seltop) {
			term->selstart.y = seltop;
			term->selstart.x = 0;
		    }
		}
		if (term->selend.y >= seltop && term->selend.y <= botline) {
		    term->selend.y--;
		    if (term->selend.y < seltop) {
			term->selend.y = seltop;
			term->selend.x = 0;
		    }
		}
		if (term->selanchor.y >= seltop &&
		    term->selanchor.y <= botline) {
		    term->selanchor.y--;
		    if (term->selanchor.y < seltop) {
			term->selanchor.y = seltop;
			term->selanchor.x = 0;
		    }
		}
	    }
	}
    }
}

/*
 * Move the cursor to a given position, clipping at boundaries. We
 * may or may not want to clip at the scroll margin: marg_clip is 0
 * not to, 1 to disallow _passing_ the margins, and 2 to disallow
 * even _being_ outside the margins.
 */
static void move(Terminal *term, int x, int y, int marg_clip)
{
    if (x < 0)
	x = 0;
    if (x >= term->cols)
	x = term->cols - 1;
    if (marg_clip) {
	if ((term->curs.y >= term->marg_t || marg_clip == 2) &&
	    y < term->marg_t)
	    y = term->marg_t;
	if ((term->curs.y <= term->marg_b || marg_clip == 2) &&
	    y > term->marg_b)
	    y = term->marg_b;
    }
    if (y < 0)
	y = 0;
    if (y >= term->rows)
	y = term->rows - 1;
    term->curs.x = x;
    term->curs.y = y;
    term->wrapnext = false;
}

/*
 * Save or restore the cursor and SGR mode.
 */
static void save_cursor(Terminal *term, bool save)
{
    if (save) {
	term->savecurs = term->curs;
	term->save_attr = term->curr_attr;
	term->save_truecolour = term->curr_truecolour;
	term->save_cset = term->cset;
	term->save_utf = term->utf;
	term->save_wnext = term->wrapnext;
	term->save_csattr = term->cset_attr[term->cset];
	term->save_sco_acs = term->sco_acs;
    } else {
	term->curs = term->savecurs;
	/* Make sure the window hasn't shrunk since the save */
	if (term->curs.x >= term->cols)
	    term->curs.x = term->cols - 1;
	if (term->curs.y >= term->rows)
	    term->curs.y = term->rows - 1;

	term->curr_attr = term->save_attr;
	term->curr_truecolour = term->save_truecolour;
	term->cset = term->save_cset;
	term->utf = term->save_utf;
	term->wrapnext = term->save_wnext;
	/*
	 * wrapnext might reset to False if the x position is no
	 * longer at the rightmost edge.
	 */
	if (term->wrapnext && term->curs.x < term->cols-1)
	    term->wrapnext = false;
	term->cset_attr[term->cset] = term->save_csattr;
	term->sco_acs = term->save_sco_acs;
	set_erase_char(term);
    }
}

/*
 * This function is called before doing _anything_ which affects
 * only part of a line of text. It is used to mark the boundary
 * between two character positions, and it indicates that some sort
 * of effect is going to happen on only one side of that boundary.
 * 
 * The effect of this function is to check whether a CJK
 * double-width character is straddling the boundary, and to remove
 * it and replace it with two spaces if so. (Of course, one or
 * other of those spaces is then likely to be replaced with
 * something else again, as a result of whatever happens next.)
 * 
 * Also, if the boundary is at the right-hand _edge_ of the screen,
 * it implies something deliberate is being done to the rightmost
 * column position; hence we must clear LATTR_WRAPPED2.
 * 
 * The input to the function is the coordinates of the _second_
 * character of the pair.
 */
static void check_boundary(Terminal *term, int x, int y)
{
    termline *ldata;

    /* Validate input coordinates, just in case. */
    if (x <= 0 || x > term->cols)
	return;

    ldata = scrlineptr(y);
    check_trust_status(term, ldata);
    check_line_size(term, ldata);
    if (x == term->cols) {
	ldata->lattr &= ~LATTR_WRAPPED2;
    } else {
	if (ldata->chars[x].chr == UCSWIDE) {
	    clear_cc(ldata, x-1);
	    clear_cc(ldata, x);
	    ldata->chars[x-1].chr = ' ' | CSET_ASCII;
	    ldata->chars[x] = ldata->chars[x-1];
	}
    }
}

/*
 * Erase a large portion of the screen: the whole screen, or the
 * whole line, or parts thereof.
 */
static void erase_lots(Terminal *term,
		       bool line_only, bool from_begin, bool to_end)
{
    pos start, end;
    bool erase_lattr;
    bool erasing_lines_from_top = false;

    if (line_only) {
	start.y = term->curs.y;
	start.x = 0;
	end.y = term->curs.y + 1;
	end.x = 0;
	erase_lattr = false;
    } else {
	start.y = 0;
	start.x = 0;
	end.y = term->rows;
	end.x = 0;
	erase_lattr = true;
    }
    /* This is the endpoint of the clearing operation that is not
     * either the start or end of the line / screen. */
    pos boundary = term->curs;

    if (!from_begin) {
        /*
         * If we're erasing from the current char to the end of
         * line/screen, then we take account of wrapnext, so as to
         * maintain the invariant that writing a printing character
         * followed by ESC[K should not overwrite the character you
         * _just wrote_. That is, when wrapnext says the cursor is
         * 'logically' at the very rightmost edge of the screen
         * instead of just before the last printing char, ESC[K should
         * do nothing at all, and ESC[J should clear the next line but
         * leave this one unchanged.
         *
         * This adjusted position will also be the position we use for
         * check_boundary (i.e. the thing we ensure isn't in the
         * middle of a double-width printing char).
         */
        if (term->wrapnext)
            incpos(boundary);

        start = boundary;
    }
    if (!to_end) {
        /*
         * If we're erasing from the start of (at least) the line _to_
         * the current position, then that is taken to mean 'inclusive
         * of the cell under the cursor', which means we don't
         * consider wrapnext at all: whether it's set or not, we still
         * clear the cell under the cursor.
         *
         * Again, that incremented boundary position is where we
         * should be careful of a straddling wide character.
         */
        incpos(boundary);
        end = boundary;
    }
    if (!from_begin || !to_end)
        check_boundary(term, boundary.x, boundary.y);
    check_selection(term, start, end);

    /* Clear screen also forces a full window redraw, just in case. */
    if (start.y == 0 && start.x == 0 && end.y == term->rows)
	term_invalidate(term);

    /* Lines scrolled away shouldn't be brought back on if the terminal
     * resizes. */
    if (start.y == 0 && start.x == 0 && end.x == 0 && erase_lattr)
	erasing_lines_from_top = true;

    if (term->erase_to_scrollback && erasing_lines_from_top) {
	/* If it's a whole number of lines, starting at the top, and
	 * we're fully erasing them, erase by scrolling and keep the
	 * lines in the scrollback. */
	int scrolllines = end.y;
	if (end.y == term->rows) {
	    /* Shrink until we find a non-empty row.*/
	    scrolllines = find_last_nonempty_line(term, term->screen) + 1;
	}
	if (scrolllines > 0)
	    scroll(term, 0, scrolllines - 1, scrolllines, true);
    } else {
	termline *ldata = scrlineptr(start.y);
        check_trust_status(term, ldata);
	while (poslt(start, end)) {
            check_line_size(term, ldata);
	    if (start.x == term->cols) {
		if (!erase_lattr)
		    ldata->lattr &= ~(LATTR_WRAPPED | LATTR_WRAPPED2);
		else
		    ldata->lattr = LATTR_NORM;
	    } else {
#ifdef MOD_HYPERLINK
                assert(term->erase_char.cc_next == 0);
#endif
		copy_termchar(ldata, start.x, &term->erase_char);
	    }
	    if (incpos(start) && start.y < term->rows) {
		ldata = scrlineptr(start.y);
                check_trust_status(term, ldata);
	    }
	}
    }

    /* After an erase of lines from the top of the screen, we shouldn't
     * bring the lines back again if the terminal enlarges (since the user or
     * application has explicitly thrown them away). */
    if (erasing_lines_from_top && !(term->alt_which))
	term->tempsblines = 0;
}

/*
 * Insert or delete characters within the current line. n is +ve if
 * insertion is desired, and -ve for deletion.
 */
static void insch(Terminal *term, int n)
{
    int dir = (n < 0 ? -1 : +1);
    int m, j;
    pos eol;
    termline *ldata;

    n = (n < 0 ? -n : n);
    if (n > term->cols - term->curs.x)
	n = term->cols - term->curs.x;
    m = term->cols - term->curs.x - n;

    /*
     * We must de-highlight the selection if it overlaps any part of
     * the region affected by this operation, i.e. the region from the
     * current cursor position to end-of-line, _unless_ the entirety
     * of the selection is going to be moved to the left or right by
     * this operation but otherwise unchanged, in which case we can
     * simply move the highlight with the text.
     */
    eol.y = term->curs.y;
    eol.x = term->cols;
    if (poslt(term->curs, term->selend) && poslt(term->selstart, eol)) {
        pos okstart = term->curs;
        pos okend = eol;
        if (dir > 0) {
            /* Insertion: n characters at EOL will be splatted. */
            okend.x -= n;
        } else {
            /* Deletion: n characters at cursor position will be splatted. */
            okstart.x += n;
        }
        if (posle(okstart, term->selstart) && posle(term->selend, okend)) {
            /* Selection is contained entirely in the interval
             * [okstart,okend), so we need only adjust the selection
             * bounds. */
            term->selstart.x += dir * n;
            term->selend.x += dir * n;
            assert(term->selstart.x >= term->curs.x);
            assert(term->selstart.x < term->cols);
            assert(term->selend.x > term->curs.x);
            assert(term->selend.x <= term->cols);
        } else {
            /* Selection is not wholly contained in that interval, so
             * we must unhighlight it. */
            deselect(term);
        }
    }

    check_boundary(term, term->curs.x, term->curs.y);
    if (dir < 0)
	check_boundary(term, term->curs.x + n, term->curs.y);
    ldata = scrlineptr(term->curs.y);
    check_trust_status(term, ldata);
    if (dir < 0) {
	for (j = 0; j < m; j++)
	    move_termchar(ldata,
			  ldata->chars + term->curs.x + j,
			  ldata->chars + term->curs.x + j + n);
	while (n--)
	    copy_termchar(ldata, term->curs.x + m++, &term->erase_char);
    } else {
	for (j = m; j-- ;)
	    move_termchar(ldata,
			  ldata->chars + term->curs.x + j + n,
			  ldata->chars + term->curs.x + j);
#ifdef MOD_HYPERLINK
	assert(term->erase_char.cc_next == 0);
#endif
	while (n--)
	    copy_termchar(ldata, term->curs.x + n, &term->erase_char);
    }
}

static void term_update_raw_mouse_mode(Terminal *term)
{
#ifdef MOD_PERSO
    bool want_raw = (term->xterm_mouse_mode && !term->xterm_mouse_forbidden);
#else
    bool want_raw = (term->xterm_mouse != 0 && !term->xterm_mouse_forbidden);
#endif
    win_set_raw_mouse_mode(term->win, want_raw);
    term->win_pointer_shape_pending = true;
    term->win_pointer_shape_raw = want_raw;
    term_schedule_update(term);
}

/*
 * Toggle terminal mode `mode' to state `state'. (`query' indicates
 * whether the mode is a DEC private one or a normal one.)
 */
static void toggle_mode(Terminal *term, int mode, int query, bool state)
{
    if (query == 1) {
	switch (mode) {
	  case 1:		       /* DECCKM: application cursor keys */
	    term->app_cursor_keys = state;
	    break;
	  case 2:		       /* DECANM: VT52 mode */
	    term->vt52_mode = !state;
	    if (term->vt52_mode) {
		term->blink_is_real = false;
		term->vt52_bold = false;
	    } else {
		term->blink_is_real = term->blinktext;
	    }
	    term_schedule_tblink(term);
	    break;
	  case 3:		       /* DECCOLM: 80/132 columns */
	    deselect(term);
            if (!term->no_remote_resize) {
                term->win_resize_pending = true;
                term->win_resize_pending_w = state ? 132 : 80;
                term->win_resize_pending_h = term->rows;
                term_schedule_update(term);
            }
	    term->reset_132 = state;
	    term->alt_t = term->marg_t = 0;
	    term->alt_b = term->marg_b = term->rows - 1;
	    move(term, 0, 0, 0);
	    erase_lots(term, false, true, true);
	    break;
	  case 5:		       /* DECSCNM: reverse video */
	    /*
	     * Toggle reverse video. If we receive an OFF within the
	     * visual bell timeout period after an ON, we trigger an
	     * effective visual bell, so that ESC[?5hESC[?5l will
	     * always be an actually _visible_ visual bell.
	     */
	    if (term->rvideo && !state) {
		/* This is an OFF, so set up a vbell */
		term_schedule_vbell(term, true, term->rvbell_startpoint);
	    } else if (!term->rvideo && state) {
		/* This is an ON, so we notice the time and save it. */
		term->rvbell_startpoint = GETTICKCOUNT();
	    }
	    term->rvideo = state;
	    seen_disp_event(term);
	    break;
	  case 6:		       /* DECOM: DEC origin mode */
	    term->dec_om = state;
	    break;
	  case 7:		       /* DECAWM: auto wrap */
	    term->wrap = state;
	    break;
	  case 8:		       /* DECARM: auto key repeat */
	    term->repeat_off = !state;
	    break;
	  case 25:		       /* DECTCEM: enable/disable cursor */
	    compatibility2(OTHER, VT220);
	    term->cursor_on = state;
	    seen_disp_event(term);
	    break;
	  case 47:		       /* alternate screen */
	    compatibility(OTHER);
	    deselect(term);
	    swap_screen(term, term->no_alt_screen ? 0 : state, false, false);
            if (term->scroll_on_disp)
                term->disptop = 0;
	    break;
#ifdef MOD_PERSO
          case 1000:                   /* xterm mouse 1 (normal) */
            term->xterm_mouse_mode = state ? MM_NORMAL : MM_NONE;
            term_update_raw_mouse_mode(term);
            break;
          case 1002:                   /* xterm mouse 2 (inc. button drags) */
            term->xterm_mouse_mode = state ? MM_BTN_EVENT : MM_NONE;
            term_update_raw_mouse_mode(term);
            break;
          case 1003:                   /* xterm any event tracking */
            term->xterm_mouse_mode = state ? MM_ANY_EVENT : MM_NONE;
            term_update_raw_mouse_mode(term);
            break;
          case 1004:
            win_set_focus_reporting_mode(term->win, state);
            break;
          case 1005:                   /* use XTERM 1005 mouse protocol */
            term->xterm_mouse_protocol = state ? MP_XTERM : MP_NORMAL;
            break;
          case 1006:                   /* use SGR 1006 mouse protocol */
            term->xterm_mouse_protocol = state ? MP_SGR : MP_NORMAL;
	    break;
          case 1015:                   /* use URXVT 1015 mouse protocol */
            term->xterm_mouse_protocol = state ? MP_URXVT : MP_NORMAL;
	    break;
#else
	  case 1000:		       /* xterm mouse 1 (normal) */
	    term->xterm_mouse = state ? 1 : 0;
            term_update_raw_mouse_mode(term);
	    break;
	  case 1002:		       /* xterm mouse 2 (inc. button drags) */
	    term->xterm_mouse = state ? 2 : 0;
            term_update_raw_mouse_mode(term);
	    break;
	  case 1006:		       /* xterm extended mouse */
	    term->xterm_extended_mouse = state;
	    break;
	  case 1015:		       /* urxvt extended mouse */
	    term->urxvt_extended_mouse = state;
	    break;
#endif
	  case 1047:                   /* alternate screen */
	    compatibility(OTHER);
	    deselect(term);
	    swap_screen(term, term->no_alt_screen ? 0 : state, true, true);
            if (term->scroll_on_disp)
                term->disptop = 0;
	    break;
	  case 1048:                   /* save/restore cursor */
	    if (!term->no_alt_screen)
                save_cursor(term, state);
	    if (!state) seen_disp_event(term);
	    break;
	  case 1049:                   /* cursor & alternate screen */
	    if (state && !term->no_alt_screen)
		save_cursor(term, state);
	    if (!state) seen_disp_event(term);
	    compatibility(OTHER);
	    deselect(term);
	    swap_screen(term, term->no_alt_screen ? 0 : state, true, false);
	    if (!state && !term->no_alt_screen)
		save_cursor(term, state);
            if (term->scroll_on_disp)
                term->disptop = 0;
	    break;
	  case 2004:		       /* xterm bracketed paste */
	    term->bracketed_paste = state ? true : false;
	    break;
        }
    } else if (query == 0) {
	switch (mode) {
	  case 4:		       /* IRM: set insert mode */
	    compatibility(VT102);
	    term->insert = state;
	    break;
	  case 12:		       /* SRM: set echo mode */
            term->srm_echo = !state;
	    break;
	  case 20:		       /* LNM: Return sends ... */
	    term->cr_lf_return = state;
	    break;
	  case 34:		       /* WYULCURM: Make cursor BIG */
	    compatibility2(OTHER, VT220);
	    term->big_cursor = !state;
	}
    }
}

/*
 * Process an OSC sequence: set window title or icon name.
 */
static void do_osc(Terminal *term)
{
#ifdef MOD_FAR2L
    /* far2l extensions support */
    if (term->is_apc) {

        #ifndef _WINDOWS
        #define DWORD unsigned int
        #define WORD unsigned short
        #endif

        if (strncmp(term->osc_string, "far2l", 5) == 0) {

            // okay, this is far2l terminal extensions APC sequence

            // for more info about far2l terminal extensions please see:
            // https://github.com/cyd01/KiTTY/issues/74
            // https://github.com/elfmz/far2l/blob/master/WinPort/FarTTY.h

            if (strncmp(term->osc_string+5, "1", 1) == 0) {

                // extensions mode on

                term->far2l_ext = 1;

                char *reply_buf = dupprintf(
                        "\x1b_far2lok\x07");
                ldisc_send(term->ldisc, reply_buf, strlen(reply_buf),
                           false);
                sfree(reply_buf);

                // reset clipboard state; todo: do it on session init!
                term->clip_allowed = -1;

            } else if (strncmp(term->osc_string+5, "0", 1) == 0) {

                // extensions mode off

                term->far2l_ext = 0;

                // reset clipboard state; todo: do it on session init!
                term->clip_allowed = -1;

            } else if (strncmp(term->osc_string+5, ":", 1) == 0) {

                // processing actual payload

                // base64-decode payload
                base64_decodestate _d_state;
                base64_init_decodestate(&_d_state);
                char* d_out = malloc(term->osc_strlen);
                int d_count = base64_decode_block(
                    term->osc_string+6, term->osc_strlen-6, d_out, &_d_state);

                // last byte is id
                BYTE id = d_out[d_count-1];
                char* reply = 0;
                int reply_size = 0;

                if (term->osc_strlen == OSC_STR_MAX) {

                    // it's possibly too large clipboard

                    // we can't deal with it
                    // until implementing dynamic osc_string allocation

                    #ifdef _WINDOWS
                    MessageBox(NULL, "Too large clipboard :(", "Error", MB_OK);
                    #endif

                    // correct request id is lost forever
                    // so we can not prevent far2l from hanging
                    // so sad

                    // fixme: good idea is to free all allocated memory here, though
                    exit(100);
                }

                DWORD len;
#ifdef MOD_FAR2L
		DWORD zero = 0;
#endif

                // next from the end byte is command
                switch (d_out[d_count-2]) {

                    case 'f':; // FARTTY_INTERRACT_SET_FKEY_TITLES

                        reply_size = 5;
                        reply = malloc(reply_size);

#ifndef MOD_FAR2L
                        // fixme: unimplemented
                        DWORD zero = 0;
#endif

                        memcpy(reply, &zero, sizeof(DWORD));

                        break;

                    case 'n':; // FARTTY_INTERRACT_DESKTOP_NOTIFICATION

                        /* // not ready yet
                        // todo: generate some reply
                        // todo: show notification only if window is out of focus
                        // todo: remove icon after notification timeout or by mouse click
                        // title length, source, utf8, no zero-terminate, bytes
                        DWORD len1;
                        memcpy(&len1, d_out+d_count-6, sizeof(len1));
                        // text length, source, utf8, no zero-terminate, bytes
                        DWORD len2;
                        memcpy(&len2, d_out+d_count-6-4-len1, sizeof(len2));
                        // destination (wide char)
                        LPWSTR text_wc, title_wc;
                        int textsz_wc, titlesz_wc;
                        // notification may contain file names in non-latin
                        // or may have localized title
                        // so we can not assume ascii here and should do
                        // full utf8->multibyte conversion
                        titlesz_wc = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)(d_out+len2+4), len1, 0, 0);
                        textsz_wc = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)d_out, len2, 0, 0);
                        if (titlesz_wc && textsz_wc) {
                            title_wc = malloc((titlesz_wc+1)*sizeof(wchar_t));
                            MultiByteToWideChar(CP_UTF8, 0, (LPCCH)(d_out+len2+4), len1, title_wc, titlesz_wc);
                            text_wc = malloc((textsz_wc+1)*sizeof(wchar_t));
                            MultiByteToWideChar(CP_UTF8, 0, (LPCCH)d_out, len2, text_wc, textsz_wc);
                            title_wc[titlesz_wc] = 0;
                            text_wc[textsz_wc] = 0;
                            NOTIFYICONDATAW pnid;
                            // todo: do this on window focus also
                            pnid.cbSize = sizeof(pnid);
                            pnid.hWnd = NULL;
                            pnid.hIcon = LoadIcon(0, IDI_APPLICATION);
                            pnid.uID = 200;
                            Shell_NotifyIconW(NIM_DELETE, &pnid);
                            // todo: use putty icon
                            pnid.cbSize = sizeof(pnid);
                            pnid.hWnd = NULL;
                            pnid.hIcon = LoadIcon(0, IDI_APPLICATION);
                            pnid.uID = 200;
                            pnid.uFlags = NIF_ICON | NIF_INFO | NIF_MESSAGE;
                            pnid.uCallbackMessage = (WM_USER + 200);
                            pnid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
                            memcpy(pnid.szInfoTitle, title_wc, (titlesz_wc+1)*sizeof(wchar_t));
                            memcpy(pnid.szInfo, text_wc, (textsz_wc+1)*sizeof(wchar_t));
                            Shell_NotifyIconW(NIM_ADD, &pnid);
                            free(text_wc);
                            free(title_wc);
                        }
                        */

                        break;

                    case 'w': // FARTTY_INTERRACT_GET_WINDOW_MAXSIZE

                        // get largest console window size

                        reply_size = 5;
                        reply = malloc(reply_size);

                        // fixme: unimplemented
                        // here should be short x and short y
                        DWORD none = 0;

                        memcpy(reply, &none, sizeof(DWORD));

                        break;

                    case 'c': // FARTTY_INTERRACT_CLIPBOARD

                        // clipboard interaction
                        // next from the end byte is subcommand

                        switch (d_out[d_count-3]) {

                            case 'r':; // FARTTY_INTERRACT_CLIP_REGISTER_FORMAT

                                // register format

                                memcpy(&len, d_out + d_count - 3 - 4, sizeof(DWORD));
                                d_out[len] = 0; // zero-terminate format name

                                #ifdef _WINDOWS
                                // far2l sends format name as (utf8?) string,
                                // which actually containing ascii only
                                // so we can just call ascii function
                                uint32_t status = RegisterClipboardFormatA(d_out);
                                #endif

                                reply_size = 5;
                                reply = malloc(reply_size);

                                #ifdef _WINDOWS
                                memcpy(reply, &status, sizeof(uint32_t));
                                #else
                                bzero(reply, sizeof(uint32_t));
                                #endif

                                break;

                            case 'e':; // FARTTY_INTERRACT_CLIP_EMPTY

                                #ifdef _WINDOWS
                                char ec_status = 0;
                                if (term->clip_allowed == 1) {
                                    OpenClipboard(NULL);
                                    ec_status = EmptyClipboard() ? 1 : 0;
                                    CloseClipboard();
                                }
                                #endif

                                reply_size = 2;
                                reply = malloc(reply_size);

                                #ifdef _WINDOWS
                                reply[0] = ec_status;
                                #else
                                reply[0] = 0;
                                #endif

                                break;

                            case 'a':; // FARTTY_INTERRACT_CLIP_ISAVAIL

                                uint32_t a_fmt;
                                memcpy(&a_fmt, d_out + d_count - 3 - 4, sizeof(uint32_t));

                                #ifdef _WINDOWS
                                char out = IsClipboardFormatAvailable(a_fmt) ? 1 : 0;
                                #endif

                                reply_size = 2;
                                reply = malloc(reply_size);

                                #ifdef _WINDOWS
                                reply[0] = out;
                                #else
                                reply[0] = 0;
                                #endif

                                break;

                            case 'o':; // FARTTY_INTERRACT_CLIP_OPEN

                                // open
                                // next from the end 4 bytes is client_id length
                                memcpy(&len, d_out + d_count - 3 - 4, sizeof(DWORD));
                                d_out[len] = 0; // all remaining string is client id, make it zero terminated

                                // todo: check/store client id, all that stuff

                                reply_size = 2;
                                reply = malloc(reply_size);

                                #ifdef _WINDOWS
                                if (term->clip_allowed == -1) {
                                    int status = MessageBox(NULL,
                                        "Allow far2l clipboard sync?", "PyTTY", MB_OKCANCEL);
                                    if (status == IDOK) {
                                        term->clip_allowed = 1;
                                    } else {
                                        // IDCANCEL
                                        term->clip_allowed = 0;
                                    }
                                }

                                // status is first response byte
                                if (term->clip_allowed == 1) {
                                    reply[0] = 1;
                                } else {
                                    reply[0] = -1;
                                }
                                #else
                                reply[0] = -1;
                                #endif

                                break;

                            case 's':; // FARTTY_INTERRACT_CLIP_SETDATA

                                // set data

                                if (term->clip_allowed == 1 && d_count >= 4 + 4 + 3) {

                                    #ifdef _WINDOWS

                                    uint32_t fmt;
                                    char* buffer = NULL;
                                    int BufferSize = 0;
                                    memcpy(&fmt, d_out + d_count - 3 - 4, sizeof(uint32_t));

                                    // id, 'c', 's', 4-byte fmt, next goes 4-byte len
                                    memcpy(&len, d_out + d_count - 3 - 4 - 4, sizeof(DWORD));
                                    if (len > d_count - 3 - 4 - 4)
                                        len = d_count - 3 - 4 - 4;

                                    if (fmt == CF_TEXT) {
                                        int cnt = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)d_out, len, NULL, 0);
                                        if (cnt > 0) {
                                            buffer = calloc(cnt + 1, sizeof(wchar_t));
                                            MultiByteToWideChar(CP_UTF8, 0, (LPCCH)d_out, len, (PWCHAR)buffer, cnt);
                                        }
                                        fmt = CF_UNICODETEXT;
                                        BufferSize = (wcslen((PWCHAR)buffer) + 1) * sizeof(WCHAR);

                                    } else if (fmt == CF_UNICODETEXT) {
                                        // very stupid utf32->utf16 'conversion'
                                        buffer = calloc((len / sizeof(uint32_t)) + 1, sizeof(wchar_t));
                                        for (int i=0; i < len / sizeof(uint32_t); ++i) {
                                            memcpy(
                                                &buffer[i * sizeof(wchar_t)],
                                                &d_out[i * sizeof(uint32_t)],
                                                sizeof(wchar_t)
                                            );
                                        }
                                        BufferSize = (wcslen((PWCHAR)buffer) + 1) * sizeof(WCHAR);

                                    } else if (fmt >= 0xC000) {
                                        // no transcoding - copy it as is
                                        buffer = malloc(len);
                                        memcpy(buffer, &d_out[0], len);
                                        BufferSize = len;
                                    }

                                    // clipboard stuff itself

                                    HGLOBAL hData;
                                    void *GData;

                                    bool set_successful = 0;

                                    if (buffer && (hData=GlobalAlloc(GMEM_MOVEABLE,BufferSize))) {

                                        if ((GData=GlobalLock(hData))) {

                                            memcpy(GData,buffer,BufferSize);
                                            GlobalUnlock(hData);

                                            if (OpenClipboard(NULL)) {

                                                if (!SetClipboardData(fmt, (HANDLE)hData)) {
                                                    GlobalFree(hData);
                                                } else {
                                                    set_successful = 1;
                                                }

                                                CloseClipboard();

                                            } else {
                                                GlobalFree(hData);
                                            }

                                        } else {
                                            GlobalFree(hData);
                                        }
                                    }

                                    free(buffer);

                                    #endif

                                    // prepare reply
                                    reply_size = 2;
                                    reply = malloc(reply_size);

                                    // first reply byte is status
                                    #ifdef _WINDOWS
                                    reply[0] = set_successful;
                                    #else
                                    reply[0] = 0;
                                    #endif

                                } else {

                                    reply_size = 2;
                                    reply = malloc(reply_size);

                                    reply[0] = 0;
                                }

                                break;

                            case 'g':; // FARTTY_INTERRACT_CLIP_GETDATA

                                if (term->clip_allowed == 1) {

                                    #ifdef _WINDOWS

                                    uint32_t gfmt;
                                    memcpy(&gfmt, d_out + d_count - 3 - 4, sizeof(uint32_t));

                                    // clipboard stuff itself

                                    void *ClipText = NULL;
                                    int32_t ClipTextSize = 0;

                                    if ((gfmt == CF_TEXT || gfmt == CF_UNICODETEXT || gfmt >= 0xC000) &&
                                        OpenClipboard(NULL))
                                    {
                                        HANDLE hClipData = GetClipboardData((gfmt == CF_TEXT) ? CF_UNICODETEXT : gfmt);

                                        if (hClipData)
                                        {
                                            void *pClipData=GlobalLock(hClipData);

                                            if (pClipData)
                                            {
                                                size_t n = GlobalSize(hClipData);

                                                if (gfmt == CF_TEXT) {

                                                    ClipTextSize = WideCharToMultiByte(
                                                        CP_UTF8,
                                                        0,
                                                        (wchar_t *)pClipData,
                                                        n,
                                                        NULL,
                                                        0,
                                                        NULL,
                                                        NULL
                                                    ) + 1;

                                                    if (ClipTextSize >= 0) {
                                                        n = wcsnlen((wchar_t *)pClipData, n / sizeof(wchar_t));
                                                        ClipText = calloc(ClipTextSize + 1, 1);
                                                        if (ClipText) {
                                                            WideCharToMultiByte(
                                                                CP_UTF8,
                                                                0,
                                                                (wchar_t *)pClipData,
                                                                n,
                                                                (char *)ClipText,
                                                                ClipTextSize,
                                                                NULL,
                                                                NULL
                                                            );
                                                            ClipTextSize = strlen((char *)ClipText) + 1;
                                                        }
                                                    }

                                                } else  if (gfmt == CF_UNICODETEXT) {
                                                    n = wcsnlen((wchar_t *)pClipData, n / sizeof(wchar_t));
                                                    ClipText = calloc((n + 1), sizeof(uint32_t));
                                                    if (ClipText) {
                                                        for (size_t i = 0; i < n; ++i) {
                                                            ((uint32_t *)ClipText)[i] = ((uint16_t *)pClipData)[i];
                                                        }
                                                        ClipTextSize = (n + 1) * sizeof(uint32_t);
                                                    }

                                                } else {
                                                    ClipText = malloc(n);
                                                    if (ClipText) {
                                                        memcpy(ClipText, pClipData, n);
                                                        ClipTextSize = n;
                                                    }
                                                }

                                                GlobalUnlock(hClipData);
                                            }

                                        } else {
                                            // todo: process errors
                                        }
                                        CloseClipboard();
                                    }


                                    if (!ClipText || ClipTextSize <= 0) {

                                        // clipboard is empty
                                        reply_size = 5; // 4 bytes for size and one for id
                                        reply = calloc(1, reply_size);

                                    } else {

                                        // + length (4 bytes) + id (1 byte)
                                        reply_size = ClipTextSize + 5;
                                        reply = calloc(1, reply_size);
                                        memcpy(reply, ClipText, ClipTextSize);

                                        // set size
                                        memcpy(reply + ClipTextSize, &ClipTextSize, sizeof(ClipTextSize));
                                    }

                                    free(ClipText);

                                    #else
                                    reply_size = 5;
                                    reply = calloc(1, reply_size);
                                    #endif

                                } else {

                                    // we should never reach here
                                    // anyway, mimic empty clipboard

                                    reply_size = 5;
                                    reply = calloc(1, reply_size);
                                }

                                break;
                        }
#ifdef MOD_FAR2L
                        break;
                    case 'p':

                        reply_size = 3; // reserved byte, bits byte, id byte
                        reply = malloc(reply_size);

                        reply[0] = 0;
                        reply[1] = 24;

                        break;

                    default:

                        // not implemented

                        reply_size = 5;
                        reply = malloc(reply_size);

                        memcpy(reply, &zero, sizeof(DWORD));
#endif

                        break;
                }

                free(d_out);

                if (reply_size > 0) {
                    // request is correct and we should send reply

                    // last byte is always id
                    reply[reply_size-1] = id;

                    // ok, let us try to answer something

                    // base64-encode
                    // result in null-terminated char* out
                    base64_encodestate _state;
                    base64_init_encodestate(&_state);

                    char* out = malloc(reply_size*2);
                    int count = base64_encode_block((char*)reply, reply_size, out, &_state);
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

                    char* str = "\x1b_far2l";
                    backend_send(term->backend, str, strlen(str));

                    backend_send(term->backend, out, count);

                    char* str2 = "\x07";
                    backend_send(term->backend, str2, strlen(str2));

                    // don't forget to free memory :)
                    free(reply);
                    free(out);

                }
            }
        }

        term->osc_strlen = 0;
        term->is_apc = 0;

    } else
#endif
    if (term->osc_w) {
	while (term->osc_strlen--)
	    term->wordness[(unsigned char)
		term->osc_string[term->osc_strlen]] = term->esc_args[0];
    } else {
	term->osc_string[term->osc_strlen] = '\0';
	switch (term->esc_args[0]) {
	  case 0:
	  case 1:
            if (!term->no_remote_wintitle) {
                sfree(term->icon_title);
                term->icon_title = dupstr(term->osc_string);
                term->win_icon_title_pending = true;
                term_schedule_update(term);
            }
	    if (term->esc_args[0] == 1)
		break;
	    /* fall through: parameter 0 means set both */
	  case 2:
	  case 21:
            if (!term->no_remote_wintitle) {
                sfree(term->window_title);
                term->window_title = dupstr(term->osc_string);
                term->win_title_pending = true;
                term_schedule_update(term);
            }
	    break;
          case 4:
            if (term->ldisc && !strcmp(term->osc_string, "?")) {
                unsigned index = term->esc_args[1];
                if (index < OSC4_NCOLOURS) {
                    rgb colour = term->palette[index];
                    char *reply_buf = dupprintf(
                        "\033]4;%u;rgb:%04x/%04x/%04x\007", index,
                        (unsigned)colour.r * 0x0101,
                        (unsigned)colour.g * 0x0101,
                        (unsigned)colour.b * 0x0101);
                    ldisc_send(term->ldisc, reply_buf, strlen(reply_buf),
                               false);
                    sfree(reply_buf);
                }
            }
            break;
	}
    }
}

#ifdef MOD_PRINTCLIP
 /*
  * Windows clipboard support
  * Diomidis Spinellis, June 2003
  */
 static char *clip_b, *clip_bp;		/* Buffer, pointer to buffer insertion point */
 static size_t clip_bsiz, clip_remsiz;	/* Buffer, size, remaining size */
 static size_t clip_total;		/* Total read */
 
 #define CLIP_CHUNK 16384
 
 static void clipboard_init(void)
 {
 	if (clip_b)
 		sfree(clip_b);
 	clip_bp = clip_b = smalloc(clip_remsiz = clip_bsiz = CLIP_CHUNK);
 	clip_total = 0;
 }
 
 static void clipboard_data(const void *buff,  int len)
 {
 	memcpy(clip_bp, buff, len);
 	clip_remsiz -= len;
 	clip_total += len;
 	clip_bp += len;
 	if (clip_remsiz < CLIP_CHUNK) {
 		clip_b = srealloc(clip_b, clip_bsiz *= 2);
 		clip_remsiz = clip_bsiz - clip_total;
 		clip_bp = clip_b + clip_total;
 	}
 }
 
 static void clipboard_copy(void)
 {
 	HANDLE hglb;
 
 	if (!OpenClipboard(NULL))
 		return; // error("Unable to open the clipboard");
 	if (!EmptyClipboard()) {
 		CloseClipboard(); 
 		return; // error("Unable to empty the clipboard");
 	}
 
 	hglb = GlobalAlloc(GMEM_DDESHARE, clip_total + 1);
 	if (hglb == NULL) { 
 		CloseClipboard(); 
 		return; // error("Unable to allocate clipboard memory");
 	}
 	memcpy(hglb, clip_b, clip_total);
 	((char *)hglb)[clip_total] = '\0';
 	SetClipboardData(CF_TEXT, hglb); 
 	CloseClipboard(); 
 }
#endif

/*
 * ANSI printing routines.
 */
static void term_print_setup(Terminal *term, char *printer)
{
    bufchain_clear(&term->printer_buf);
#ifdef MOD_PRINTCLIP
     if ( conf_get_int(term->conf,CONF_printclip) )
 		clipboard_init();
 	else
#endif
    term->print_job = printer_start_job(printer);
}
static void term_print_flush(Terminal *term)
{
    size_t size;
    while ((size = bufchain_size(&term->printer_buf)) > 5) {
	ptrlen data = bufchain_prefix(&term->printer_buf);
	if (data.len > size-5)
	    data.len = size-5;
#ifdef MOD_PRINTCLIP
 	if ( conf_get_int(term->conf,CONF_printclip) )
 		clipboard_data(data.ptr, data.len);
 	else
#endif
	printer_job_data(term->print_job, data.ptr, data.len);
	bufchain_consume(&term->printer_buf, data.len);
    }
}
static void term_print_finish(Terminal *term)
{
    size_t size;
    char c;

    if (!term->printing && !term->only_printing)
	return;			       /* we need do nothing */

    term_print_flush(term);
    while ((size = bufchain_size(&term->printer_buf)) > 0) {
	ptrlen data = bufchain_prefix(&term->printer_buf);
	c = *(char *)data.ptr;
	if (c == '\033' || c == '\233') {
	    bufchain_consume(&term->printer_buf, size);
	    break;
	} else {
#ifdef MOD_PRINTCLIP
 		if ( conf_get_int(term->conf,CONF_printclip) )
 			clipboard_data(&c, 1);
 		else
#endif
	    printer_job_data(term->print_job, &c, 1);
	    bufchain_consume(&term->printer_buf, 1);
	}
    }
#ifdef MOD_PRINTCLIP
 	if ( conf_get_int(term->conf,CONF_printclip) )
 		clipboard_copy();
 	else
#endif
    printer_finish_job(term->print_job);
    term->print_job = NULL;
    term->printing = term->only_printing = false;
}

static void term_display_graphic_char(Terminal *term, unsigned long c)
{
    termline *cline = scrlineptr(term->curs.y);
    int width = 0;
    if (DIRECT_CHAR(c))
        width = 1;
    if (!width)
        width = term_char_width(term, c);

    if (term->wrapnext && term->wrap && width > 0) {
        cline->lattr |= LATTR_WRAPPED;
        if (term->curs.y == term->marg_b)
            scroll(term, term->marg_t, term->marg_b, 1, true);
        else if (term->curs.y < term->rows - 1)
            term->curs.y++;
        term->curs.x = 0;
        term->wrapnext = false;
        cline = scrlineptr(term->curs.y);
    }
    if (term->insert && width > 0)
        insch(term, width);
    if (term->selstate != NO_SELECTION) {
        pos cursplus = term->curs;
        incpos(cursplus);
        check_selection(term, term->curs, cursplus);
    }
    if (((c & CSET_MASK) == CSET_ASCII ||
         (c & CSET_MASK) == 0) && term->logctx)
        logtraffic(term->logctx, (unsigned char) c, LGTYP_ASCII);

    check_trust_status(term, cline);

    int linecols = term->cols;
    if (cline->trusted)
        linecols -= TRUST_SIGIL_WIDTH;

    /*
     * Preliminary check: if the terminal is only one character cell
     * wide, then we cannot display any double-width character at all.
     * Substitute single-width REPLACEMENT CHARACTER instead.
     */
    if (width == 2 && linecols < 2) {
        width = 1;
        c = 0xFFFD;
    }

    switch (width) {
      case 2:
        /*
         * If we're about to display a double-width character starting
         * in the rightmost column, then we do something special
         * instead. We must print a space in the last column of the
         * screen, then wrap; and we also set LATTR_WRAPPED2 which
         * instructs subsequent cut-and-pasting not only to splice
         * this line to the one after it, but to ignore the space in
         * the last character position as well. (Because what was
         * actually output to the terminal was presumably just a
         * sequence of CJK characters, and we don't want a space to be
         * pasted in the middle of those just because they had the
         * misfortune to start in the wrong parity column. xterm
         * concurs.)
         */
        check_boundary(term, term->curs.x, term->curs.y);
        check_boundary(term, term->curs.x+2, term->curs.y);
        if (term->curs.x >= linecols-1) {
            copy_termchar(cline, term->curs.x,
                          &term->erase_char);
            cline->lattr |= LATTR_WRAPPED | LATTR_WRAPPED2;
            if (term->curs.y == term->marg_b)
                scroll(term, term->marg_t, term->marg_b,
                       1, true);
            else if (term->curs.y < term->rows - 1)
                term->curs.y++;
            term->curs.x = 0;
            cline = scrlineptr(term->curs.y);
            /* Now we must check_boundary again, of course. */
            check_boundary(term, term->curs.x, term->curs.y);
            check_boundary(term, term->curs.x+2, term->curs.y);
        }

        /* FULL-TERMCHAR */
        clear_cc(cline, term->curs.x);
        cline->chars[term->curs.x].chr = c;
        cline->chars[term->curs.x].attr = term->curr_attr;
        cline->chars[term->curs.x].truecolour =
            term->curr_truecolour;

        term->curs.x++;

        /* FULL-TERMCHAR */
        clear_cc(cline, term->curs.x);
        cline->chars[term->curs.x].chr = UCSWIDE;
        cline->chars[term->curs.x].attr = term->curr_attr;
        cline->chars[term->curs.x].truecolour =
            term->curr_truecolour;

        break;
      case 1:
        check_boundary(term, term->curs.x, term->curs.y);
        check_boundary(term, term->curs.x+1, term->curs.y);

        /* FULL-TERMCHAR */
        clear_cc(cline, term->curs.x);
        cline->chars[term->curs.x].chr = c;
        cline->chars[term->curs.x].attr = term->curr_attr;
        cline->chars[term->curs.x].truecolour =
            term->curr_truecolour;

        break;
      case 0:
        if (term->curs.x > 0) {
            int x = term->curs.x - 1;

            /* If we're in wrapnext state, the character to combine
             * with is _here_, not to our left. */
            if (term->wrapnext)
                x++;

            /*
             * If the previous character is UCSWIDE, back up another
             * one.
             */
            if (cline->chars[x].chr == UCSWIDE) {
                assert(x > 0);
                x--;
            }

            add_cc(cline, x, c);
            seen_disp_event(term);
        }
        return;
      default:
        return;
    }
    term->curs.x++;
    if (term->curs.x >= linecols) {
        term->curs.x = linecols - 1;
        term->wrapnext = true;
        if (term->wrap && term->vt52_mode) {
            cline->lattr |= LATTR_WRAPPED;
            if (term->curs.y == term->marg_b)
                scroll(term, term->marg_t, term->marg_b, 1, true);
            else if (term->curs.y < term->rows - 1)
                term->curs.y++;
            term->curs.x = 0;
            term->wrapnext = false;
        }
    }
    seen_disp_event(term);
}

static strbuf *term_input_data_from_unicode(
    Terminal *term, const wchar_t *widebuf, int len)
{
    strbuf *buf = strbuf_new();

    if (in_utf(term)) {
        /*
         * Translate input wide characters into UTF-8 to go in the
         * terminal's input data queue.
         */
        for (int i = 0; i < len; i++) {
            unsigned long ch = widebuf[i];

            if (IS_SURROGATE(ch)) {
#ifdef PLATFORM_IS_UTF16
                if (i+1 < len) {
                    unsigned long ch2 = widebuf[i+1];
                    if (IS_SURROGATE_PAIR(ch, ch2)) {
                        ch = FROM_SURROGATES(ch, ch2);
                        i++;
                    }
                } else
#endif
                {
                    /* Unrecognised UTF-16 sequence */
                    ch = '.';
                }
            }

            char utf8_chr[6];
            put_data(buf, utf8_chr, encode_utf8(utf8_chr, ch));
        }
    } else {
        /*
         * Call to the character-set subsystem to translate into
         * whatever charset the terminal is currently configured in.
         *
         * Since the terminal doesn't currently support any multibyte
         * character set other than UTF-8, we can assume here that
         * there will be at most one output byte per input wchar_t.
         * (But also we must allow space for the trailing NUL that
         * wc_to_mb will write.)
         */
        char *bufptr = strbuf_append(buf, len + 1);
        int rv;
        rv = wc_to_mb(term->ucsdata->line_codepage, 0, widebuf, len,
                      bufptr, len + 1, NULL, term->ucsdata);
        strbuf_shrink_to(buf, rv < 0 ? 0 : rv);
    }

    return buf;
}

static strbuf *term_input_data_from_charset(
    Terminal *term, int codepage, const char *str, int len)
{
    strbuf *buf;

    if (codepage < 0) {
        buf = strbuf_new();
        put_data(buf, str, len);
    } else {
        int widesize = len * 2;        /* allow for UTF-16 surrogates */
        wchar_t *widebuf = snewn(widesize, wchar_t);
        int widelen = mb_to_wc(codepage, 0, str, len, widebuf, widesize);
        buf = term_input_data_from_unicode(term, widebuf, widelen);
        sfree(widebuf);
    }

    return buf;
}

static inline void term_bracketed_paste_start(Terminal *term)
{
    ptrlen seq = PTRLEN_LITERAL("\033[200~");
    if (term->ldisc)
        ldisc_send(term->ldisc, seq.ptr, seq.len, false);
    term->bracketed_paste_active = true;
}

static inline void term_bracketed_paste_stop(Terminal *term)
{
    if (!term->bracketed_paste_active)
        return;

    ptrlen seq = PTRLEN_LITERAL("\033[201~");
    if (term->ldisc)
        ldisc_send(term->ldisc, seq.ptr, seq.len, false);
    term->bracketed_paste_active = false;
}

static inline void term_keyinput_internal(
    Terminal *term, const void *buf, int len, bool interactive)
{
    if (term->srm_echo) {
        /*
         * Implement the terminal-level local echo behaviour that
         * ECMA-48 specifies when terminal mode 12 is configured off
         * (ESC[12l). In this mode, data input to the terminal via the
         * keyboard is also added to the output buffer. But this
         * doesn't apply to escape sequences generated as session
         * input _within_ the terminal, e.g. in response to terminal
         * query sequences, or the bracketing sequences of bracketed
         * paste mode. Those will be sent directly via
         * ldisc_send(term->ldisc, ...) and won't go through this
         * function.
         */

        /* Mimic the special case of negative length in ldisc_send */
        int true_len = len >= 0 ? len : strlen(buf);

        bufchain_add(&term->inbuf, buf, true_len);
        term_added_data(term);
    }
    if (interactive)
        term_bracketed_paste_stop(term);
    if (term->ldisc)
        ldisc_send(term->ldisc, buf, len, interactive);
    term_seen_key_event(term);
}

unsigned long term_translate(
    Terminal *term, struct term_utf8_decode *utf8, unsigned char c)
{
    if (in_utf(term)) {
        switch (utf8->state) {
          case 0:
            if (c < 0x80) {
                /* UTF-8 must be stateless so we ignore iso2022. */
                if (term->ucsdata->unitab_ctrl[c] != 0xFF)  {
                    return term->ucsdata->unitab_ctrl[c];
                } else if ((term->utf8linedraw) &&
                           (term->cset_attr[term->cset] == CSET_LINEDRW)) {
                    /* Linedraw characters are explicitly enabled */
                    return c | CSET_LINEDRW;
                } else {
#ifdef ASCPORT
			/* unless we don't */
			if (!conf_get_bool(term->conf,CONF_acs_in_utf))
				return c | CSET_ASCII;
#else
                    return c | CSET_ASCII;
#endif
                }
            } else if ((c & 0xe0) == 0xc0) {
                utf8->size = utf8->state = 1;
                utf8->chr = (c & 0x1f);
            } else if ((c & 0xf0) == 0xe0) {
                utf8->size = utf8->state = 2;
                utf8->chr = (c & 0x0f);
            } else if ((c & 0xf8) == 0xf0) {
                utf8->size = utf8->state = 3;
                utf8->chr = (c & 0x07);
            } else if ((c & 0xfc) == 0xf8) {
                utf8->size = utf8->state = 4;
                utf8->chr = (c & 0x03);
            } else if ((c & 0xfe) == 0xfc) {
                utf8->size = utf8->state = 5;
                utf8->chr = (c & 0x01);
            } else {
                return UCSINVALID;
            }
            return UCSINCOMPLETE;
          case 1:
          case 2:
          case 3:
          case 4:
          case 5:
            if ((c & 0xC0) != 0x80) {
                utf8->state = 0;
                return UCSTRUNCATED;   /* caller will then give us the
                                        * same byte again */
            }
            utf8->chr = (utf8->chr << 6) | (c & 0x3f);
            if (--utf8->state)
                return UCSINCOMPLETE;

            unsigned long t = utf8->chr;

            /* Is somebody trying to be evil! */
            if (t < 0x80 ||
                (t < 0x800 && utf8->size >= 2) ||
                (t < 0x10000 && utf8->size >= 3) ||
                (t < 0x200000 && utf8->size >= 4) ||
                (t < 0x4000000 && utf8->size >= 5))
                return UCSINVALID;

            /* Unicode line separator and paragraph separator are CR-LF */
            if (t == 0x2028 || t == 0x2029)
                return 0x85;

            /* High controls are probably a Baaad idea too. */
            if (t < 0xA0)
                return 0xFFFD;

            /* The UTF-16 surrogates are not nice either. */
            /*       The standard give the option of decoding these: 
             *       I don't want to! */
            if (t >= 0xD800 && t < 0xE000)
                return UCSINVALID;

            /* ISO 10646 characters now limited to UTF-16 range. */
            if (t > 0x10FFFF)
                return UCSINVALID;

            /* This is currently a TagPhobic application.. */
            if (t >= 0xE0000 && t <= 0xE007F)
                return UCSINCOMPLETE;

            /* U+FEFF is best seen as a null. */
            if (t == 0xFEFF)
                return UCSINCOMPLETE;
            /* But U+FFFE is an error. */
            if (t == 0xFFFE || t == 0xFFFF)
                return UCSINVALID;

            return t;
        }
    } else if (term->sco_acs && 
               (c!='\033' && c!='\012' && c!='\015' && c!='\b')) {
        /* Are we in the nasty ACS mode? Note: no sco in utf mode. */
        if (term->sco_acs == 2)
            c |= 0x80;

        return c | CSET_SCOACS;
    } else {
        switch (term->cset_attr[term->cset]) {
            /* 
             * Linedraw characters are different from 'ESC ( B'
             * only for a small range. For ones outside that
             * range, make sure we use the same font as well as
             * the same encoding.
             */
          case CSET_LINEDRW:
            if (term->ucsdata->unitab_ctrl[c] != 0xFF)
                return term->ucsdata->unitab_ctrl[c];
            else
                return c | CSET_LINEDRW;
            break;

          case CSET_GBCHR:
            /* If UK-ASCII, make the '#' a LineDraw Pound */
            if (c == '#')
                return '}' | CSET_LINEDRW;
            /* fall through */

          case CSET_ASCII:
            if (term->ucsdata->unitab_ctrl[c] != 0xFF)
                return term->ucsdata->unitab_ctrl[c];
            else
                return c | CSET_ASCII;
            break;
          case CSET_SCOACS:
            if (c >= ' ')
                return c | CSET_SCOACS;
            break;
        }
    }
    return c;
}

/*
 * Remove everything currently in `inbuf' and stick it up on the
 * in-memory display. There's a big state machine in here to
 * process escape sequences...
 */
static void term_out(Terminal *term)
{
    unsigned long c;
    int unget;
    unsigned char localbuf[256], *chars;
    size_t nchars = 0;

    unget = -1;

    chars = NULL;		       /* placate compiler warnings */
    while (nchars > 0 || unget != -1 || bufchain_size(&term->inbuf) > 0) {
	if (unget == -1) {
	    if (nchars == 0) {
                ptrlen data = bufchain_prefix(&term->inbuf);
		if (data.len > sizeof(localbuf))
		    data.len = sizeof(localbuf);
		memcpy(localbuf, data.ptr, data.len);
		bufchain_consume(&term->inbuf, data.len);
                nchars = data.len;
		chars = localbuf;
		assert(chars != NULL);
		assert(nchars > 0);
	    }
	    c = *chars++;
	    nchars--;

	    /*
	     * Optionally log the session traffic to a file. Useful for
	     * debugging and possibly also useful for actual logging.
	     */
	    if (term->logtype == LGTYP_DEBUG && term->logctx)
		logtraffic(term->logctx, (unsigned char) c, LGTYP_DEBUG);
	} else {
	    c = unget;
	    unget = -1;
	}

	/* Note only VT220+ are 8-bit VT102 is seven bit, it shouldn't even
	 * be able to display 8-bit characters, but I'll let that go 'cause
	 * of i18n.
	 */

	/*
	 * If we're printing, add the character to the printer
	 * buffer.
	 */
	if (term->printing) {
	    bufchain_add(&term->printer_buf, &c, 1);

	    /*
	     * If we're in print-only mode, we use a much simpler
	     * state machine designed only to recognise the ESC[4i
	     * termination sequence.
	     */
	    if (term->only_printing) {
		if (c == '\033')
		    term->print_state = 1;
		else if (c == (unsigned char)'\233')
		    term->print_state = 2;
		else if (c == '[' && term->print_state == 1)
		    term->print_state = 2;
		else if (c == '4' && term->print_state == 2)
		    term->print_state = 3;
		else if (c == 'i' && term->print_state == 3)
		    term->print_state = 4;
		else
		    term->print_state = 0;
		if (term->print_state == 4) {
		    term_print_finish(term);
		}
		continue;
	    }
	}

	/* Do character-set translation. */
	if (term->termstate == TOPLEVEL) {
            unsigned long t = term_translate(term, &term->utf8, c);
            switch (t) {
              case UCSINCOMPLETE:
                continue;       /* didn't complete a multibyte char */
              case UCSTRUNCATED:
                unget = c;
                /* fall through */
              case UCSINVALID:
                c = UCSERR;
                break;
              default:
                c = t;
                break;
            }
	}

	/*
	 * How about C1 controls? 
	 * Explicitly ignore SCI (0x9a), which we don't translate to DECID.
	 */
	if ((c & -32) == 0x80 && term->termstate < DO_CTRLS &&
	    !term->vt52_mode && has_compat(VT220)) {
	    if (c == 0x9a)
		c = 0;
	    else {
		term->termstate = SEEN_ESC;
		term->esc_query = 0;
		c = '@' + (c & 0x1F);
	    }
	}

	/* Or the GL control. */
	if (c == '\177' && term->termstate < DO_CTRLS && has_compat(OTHER)) {
	    if (term->curs.x && !term->wrapnext)
		term->curs.x--;
	    term->wrapnext = false;
	    /* destructive backspace might be disabled */
	    if (!term->no_dbackspace) {
		check_boundary(term, term->curs.x, term->curs.y);
		check_boundary(term, term->curs.x+1, term->curs.y);
#ifdef MOD_HYPERLINK
			assert(term->erase_char.cc_next == 0);
#endif
		copy_termchar(scrlineptr(term->curs.y),
			      term->curs.x, &term->erase_char);
	    }
	} else
	    /* Or normal C0 controls. */
	if ((c & ~0x1F) == 0 && term->termstate < DO_CTRLS) {
	    switch (c) {
	      case '\005':	       /* ENQ: terminal type query */
		/* 
		 * Strictly speaking this is VT100 but a VT100 defaults to
		 * no response. Other terminals respond at their option.
		 *
		 * Don't put a CR in the default string as this tends to
		 * upset some weird software.
		 */
		compatibility(ANSIMIN);
		if (term->ldisc) {
                    strbuf *buf = term_input_data_from_charset(
                        term, DEFAULT_CODEPAGE,
                        term->answerback, term->answerbacklen);
                    ldisc_send(term->ldisc, buf->s, buf->len, false);
                    strbuf_free(buf);
		}
		break;
              case '\007': {            /* BEL: Bell */
		    struct beeptime *newbeep;
		    unsigned long ticks;

		    ticks = GETTICKCOUNT();

		    if (!term->beep_overloaded) {
			newbeep = snew(struct beeptime);
			newbeep->ticks = ticks;
			newbeep->next = NULL;
			if (!term->beephead)
			    term->beephead = newbeep;
			else
			    term->beeptail->next = newbeep;
			term->beeptail = newbeep;
			term->nbeeps++;
		    }

		    /*
		     * Throw out any beeps that happened more than
		     * t seconds ago.
		     */
		    while (term->beephead &&
			   term->beephead->ticks < ticks - term->bellovl_t) {
			struct beeptime *tmp = term->beephead;
			term->beephead = tmp->next;
			sfree(tmp);
			if (!term->beephead)
			    term->beeptail = NULL;
			term->nbeeps--;
		    }

		    if (term->bellovl && term->beep_overloaded &&
			ticks - term->lastbeep >= (unsigned)term->bellovl_s) {
			/*
			 * If we're currently overloaded and the
			 * last beep was more than s seconds ago,
			 * leave overload mode.
			 */
			term->beep_overloaded = false;
		    } else if (term->bellovl && !term->beep_overloaded &&
			       term->nbeeps >= term->bellovl_n) {
			/*
			 * Now, if we have n or more beeps
			 * remaining in the queue, go into overload
			 * mode.
			 */
			term->beep_overloaded = true;
		    }
		    term->lastbeep = ticks;

		    /*
		     * Perform an actual beep if we're not overloaded.
		     */
		    if (!term->bellovl || !term->beep_overloaded) {
			win_bell(term->win, term->beep);

			if (term->beep == BELL_VISUAL) {
			    term_schedule_vbell(term, false, 0);
			}
		    }
		    seen_disp_event(term);
		break;
              }
	      case '\b':	      /* BS: Back space */
		if (term->curs.x == 0 && (term->curs.y == 0 || !term->wrap))
		    /* do nothing */ ;
		else if (term->curs.x == 0 && term->curs.y > 0)
		    term->curs.x = term->cols - 1, term->curs.y--;
		else if (term->wrapnext)
		    term->wrapnext = false;
		else
		    term->curs.x--;
		seen_disp_event(term);
		break;
	      case '\016':	      /* LS1: Locking-shift one */
		compatibility(VT100);
		term->cset = 1;
		break;
	      case '\017':	      /* LS0: Locking-shift zero */
		compatibility(VT100);
		term->cset = 0;
		break;
	      case '\033':	      /* ESC: Escape */
		if (term->vt52_mode)
		    term->termstate = VT52_ESC;
		else {
		    compatibility(ANSIMIN);
		    term->termstate = SEEN_ESC;
		    term->esc_query = 0;
		}
		break;
	      case '\015':	      /* CR: Carriage return */
		term->curs.x = 0;
		term->wrapnext = false;
		seen_disp_event(term);

		if (term->crhaslf) {
		    if (term->curs.y == term->marg_b)
			scroll(term, term->marg_t, term->marg_b, 1, true);
		    else if (term->curs.y < term->rows - 1)
			term->curs.y++;
		}
		if (term->logctx)
		    logtraffic(term->logctx, (unsigned char) c, LGTYP_ASCII);
		break;
	      case '\014':	      /* FF: Form feed */
		if (has_compat(SCOANSI)) {
		    move(term, 0, 0, 0);
		    erase_lots(term, false, false, true);
                    if (term->scroll_on_disp)
                        term->disptop = 0;
		    term->wrapnext = false;
		    seen_disp_event(term);
		    break;
		}
	      case '\013':	      /* VT: Line tabulation */
		compatibility(VT100);
	      case '\012':	      /* LF: Line feed */
		if (term->curs.y == term->marg_b)
		    scroll(term, term->marg_t, term->marg_b, 1, true);
		else if (term->curs.y < term->rows - 1)
		    term->curs.y++;
		if (term->lfhascr)
		    term->curs.x = 0;
		term->wrapnext = false;
		seen_disp_event(term);
		if (term->logctx)
		    logtraffic(term->logctx, (unsigned char) c, LGTYP_ASCII);
		break;
              case '\t': {              /* HT: Character tabulation */
		    pos old_curs = term->curs;
		    termline *ldata = scrlineptr(term->curs.y);

		    do {
			term->curs.x++;
		    } while (term->curs.x < term->cols - 1 &&
			     !term->tabs[term->curs.x]);

		    if ((ldata->lattr & LATTR_MODE) != LATTR_NORM) {
			if (term->curs.x >= term->cols / 2)
			    term->curs.x = term->cols / 2 - 1;
		    } else {
			if (term->curs.x >= term->cols)
			    term->curs.x = term->cols - 1;
		    }

		    check_selection(term, old_curs, term->curs);
		seen_disp_event(term);
		break;
              }
	    }
	} else
	    switch (term->termstate) {
	      case TOPLEVEL:
		/* Only graphic characters get this far;
		 * ctrls are stripped above */
		term_display_graphic_char(term, c);
                term->last_graphic_char = c;
		break;

	      case OSC_MAYBE_ST:
		/*
		 * This state is virtually identical to SEEN_ESC, with the
		 * exception that we have an OSC sequence in the pipeline,
		 * and _if_ we see a backslash, we process it.
		 */
		if (c == '\\') {
		    do_osc(term);
		    term->termstate = TOPLEVEL;
		    break;
		}
		/* else fall through */
	      case SEEN_ESC:
		if (c >= ' ' && c <= '/') {
		    if (term->esc_query)
			term->esc_query = -1;
		    else
			term->esc_query = c;
		    break;
		}
		term->termstate = TOPLEVEL;
		switch (ANSI(c, term->esc_query)) {
		  case '[':		/* enter CSI mode */
		    term->termstate = SEEN_CSI;
		    term->esc_nargs = 1;
		    term->esc_args[0] = ARG_DEFAULT;
		    term->esc_query = 0;
		    break;
#ifdef MOD_FAR2L
		case '_': /* far2l: processing APC is almost the same as processing OSC */
             term->is_apc = 1;
             //break;
          //case SEEN_APC:
             /* todo */
             // if (!WriteStr2TC(fdout, enable ? "\x1b_far2l1\x1b\\\x1b[5n" : "\x1b_far2l0\x07\x1b[5n"))
             // if (!WriteStr2TC(fdout, enable ? "\x1b_far2l1\x1b\\\x1b[5n" : "\x1b_far2l0\x07\x1b[5n"))
             //break;
          case ']':		/* OSC: xterm escape sequences */
#else
		  case ']':		/* OSC: xterm escape sequences */
#endif
		    /* Compatibility is nasty here, xterm, linux, decterm yuk! */
		    compatibility(OTHER);
		    term->termstate = SEEN_OSC;
		    term->esc_args[0] = 0;
                    term->esc_nargs = 1;
		    break;
		  case '7':		/* DECSC: save cursor */
		    compatibility(VT100);
		    save_cursor(term, true);
		    break;
		  case '8':	 	/* DECRC: restore cursor */
		    compatibility(VT100);
		    save_cursor(term, false);
		    seen_disp_event(term);
		    break;
		  case '=':		/* DECKPAM: Keypad application mode */
		    compatibility(VT100);
		    term->app_keypad_keys = true;
		    break;
		  case '>':		/* DECKPNM: Keypad numeric mode */
		    compatibility(VT100);
		    term->app_keypad_keys = false;
		    break;
		  case 'D':	       /* IND: exactly equivalent to LF */
		    compatibility(VT100);
		    if (term->curs.y == term->marg_b)
			scroll(term, term->marg_t, term->marg_b, 1, true);
		    else if (term->curs.y < term->rows - 1)
			term->curs.y++;
		    term->wrapnext = false;
		    seen_disp_event(term);
		    break;
		  case 'E':	       /* NEL: exactly equivalent to CR-LF */
		    compatibility(VT100);
		    term->curs.x = 0;
		    if (term->curs.y == term->marg_b)
			scroll(term, term->marg_t, term->marg_b, 1, true);
		    else if (term->curs.y < term->rows - 1)
			term->curs.y++;
		    term->wrapnext = false;
		    seen_disp_event(term);
		    break;
		  case 'M':	       /* RI: reverse index - backwards LF */
		    compatibility(VT100);
		    if (term->curs.y == term->marg_t)
			scroll(term, term->marg_t, term->marg_b, -1, true);
		    else if (term->curs.y > 0)
			term->curs.y--;
		    term->wrapnext = false;
		    seen_disp_event(term);
		    break;
		  case 'Z':	       /* DECID: terminal type query */
		    compatibility(VT100);
                    if (term->ldisc)
			ldisc_send(term->ldisc, term->id_string,
				   strlen(term->id_string), false);
		    break;
		  case 'c':	       /* RIS: restore power-on settings */
		    compatibility(VT100);
		    power_on(term, true);
		    if (term->ldisc)   /* cause ldisc to notice changes */
			ldisc_echoedit_update(term->ldisc);
		    if (term->reset_132) {
                        if (!term->no_remote_resize) {
                            term->win_resize_pending = true;
                            term->win_resize_pending_w = 80;
                            term->win_resize_pending_h = term->rows;
                            term_schedule_update(term);
                        }
			term->reset_132 = false;
		    }
                    if (term->scroll_on_disp)
                        term->disptop = 0;
		    seen_disp_event(term);
		    break;
		  case 'H':	       /* HTS: set a tab */
		    compatibility(VT100);
		    term->tabs[term->curs.x] = true;
		    break;

                  case ANSI('8', '#'): { /* DECALN: fills screen with Es :-) */
		    compatibility(VT100);
			termline *ldata;
			int i, j;
			pos scrtop, scrbot;

			for (i = 0; i < term->rows; i++) {
			    ldata = scrlineptr(i);
                            check_line_size(term, ldata);
			    for (j = 0; j < term->cols; j++) {
				copy_termchar(ldata, j,
					      &term->basic_erase_char);
				ldata->chars[j].chr = 'E';
			    }
			    ldata->lattr = LATTR_NORM;
			}
                        if (term->scroll_on_disp)
                            term->disptop = 0;
			seen_disp_event(term);
			scrtop.x = scrtop.y = 0;
			scrbot.x = 0;
			scrbot.y = term->rows;
			check_selection(term, scrtop, scrbot);
		    break;
                  }

		  case ANSI('3', '#'):
		  case ANSI('4', '#'):
		  case ANSI('5', '#'):
                  case ANSI('6', '#'): {
		    compatibility(VT100);
			int nlattr;
			termline *ldata;

			switch (ANSI(c, term->esc_query)) {
			  case ANSI('3', '#'): /* DECDHL: 2*height, top */
			    nlattr = LATTR_TOP;
			    break;
			  case ANSI('4', '#'): /* DECDHL: 2*height, bottom */
			    nlattr = LATTR_BOT;
			    break;
			  case ANSI('5', '#'): /* DECSWL: normal */
			    nlattr = LATTR_NORM;
			    break;
			  default: /* case ANSI('6', '#'): DECDWL: 2*width */
			    nlattr = LATTR_WIDE;
			    break;
			}
			ldata = scrlineptr(term->curs.y);
                        check_line_size(term, ldata);
                        check_trust_status(term, ldata);
                        ldata->lattr = nlattr;
		    break;
                  }
		  /* GZD4: G0 designate 94-set */
		  case ANSI('A', '('):
		    compatibility(VT100);
		    if (!term->no_remote_charset)
			term->cset_attr[0] = CSET_GBCHR;
		    break;
		  case ANSI('B', '('):
		    compatibility(VT100);
		    if (!term->no_remote_charset)
			term->cset_attr[0] = CSET_ASCII;
		    break;
		  case ANSI('0', '('):
		    compatibility(VT100);
		    if (!term->no_remote_charset)
			term->cset_attr[0] = CSET_LINEDRW;
		    break;
		  case ANSI('U', '('): 
		    compatibility(OTHER);
		    if (!term->no_remote_charset)
			term->cset_attr[0] = CSET_SCOACS; 
		    break;
		  /* G1D4: G1-designate 94-set */
		  case ANSI('A', ')'):
		    compatibility(VT100);
		    if (!term->no_remote_charset)
			term->cset_attr[1] = CSET_GBCHR;
		    break;
		  case ANSI('B', ')'):
		    compatibility(VT100);
		    if (!term->no_remote_charset)
			term->cset_attr[1] = CSET_ASCII;
		    break;
		  case ANSI('0', ')'):
		    compatibility(VT100);
		    if (!term->no_remote_charset)
			term->cset_attr[1] = CSET_LINEDRW;
		    break;
		  case ANSI('U', ')'): 
		    compatibility(OTHER);
		    if (!term->no_remote_charset)
			term->cset_attr[1] = CSET_SCOACS; 
		    break;
		  /* DOCS: Designate other coding system */
		  case ANSI('8', '%'):	/* Old Linux code */
		  case ANSI('G', '%'):
		    compatibility(OTHER);
		    if (!term->no_remote_charset)
			term->utf = true;
		    break;
		  case ANSI('@', '%'):
		    compatibility(OTHER);
		    if (!term->no_remote_charset)
			term->utf = false;
		    break;
		}
		break;
	      case SEEN_CSI:
		term->termstate = TOPLEVEL;  /* default */
		if (isdigit(c)) {
		    if (term->esc_nargs <= ARGS_MAX) {
			if (term->esc_args[term->esc_nargs - 1] == ARG_DEFAULT)
			    term->esc_args[term->esc_nargs - 1] = 0;
			if (term->esc_args[term->esc_nargs - 1] <=
			    UINT_MAX / 10 &&
			    term->esc_args[term->esc_nargs - 1] * 10 <=
			    UINT_MAX - c - '0')
			    term->esc_args[term->esc_nargs - 1] =
			        10 * term->esc_args[term->esc_nargs - 1] +
			        c - '0';
			else
			    term->esc_args[term->esc_nargs - 1] = UINT_MAX;
		    }
		    term->termstate = SEEN_CSI;
		} else if (c == ';') {
		    if (term->esc_nargs < ARGS_MAX)
			term->esc_args[term->esc_nargs++] = ARG_DEFAULT;
		    term->termstate = SEEN_CSI;
#ifdef MOD_PERSO
		} else if (c == 'q') {
		    // cursor shape type (DECSCUSR)
		    int cursor_shape = def(term->esc_args[0], 1);
		    // set cursor_type from shape (cursor_type: 0=block, 1=underline, 2=beam)
		    SetCursorType( (cursor_shape - 1) >> 1 );
		    // set term->blink_cur from shape (odd numbers indicate blinking in DECSCUSR)
		    if (term->blink_cur != (cursor_shape & 1))
		    {
			term->blink_cur = cursor_shape & 1;
			term_reset_cblink(term);
		    }
		    term->termstate = TOPLEVEL;
#endif
		} else if (c < '@') {
		    if (term->esc_query)
			term->esc_query = -1;
		    else if (c == '?')
			term->esc_query = 1;
		    else
			term->esc_query = c;
		    term->termstate = SEEN_CSI;
		} else
#define CLAMP(arg, lim) ((arg) = ((arg) > (lim)) ? (lim) : (arg))
		    switch (ANSI(c, term->esc_query)) {
		      case 'A':       /* CUU: move up N lines */
			CLAMP(term->esc_args[0], term->rows);
			move(term, term->curs.x,
			     term->curs.y - def(term->esc_args[0], 1), 1);
			seen_disp_event(term);
			break;
		      case 'e':		/* VPR: move down N lines */
			compatibility(ANSI);
			/* FALLTHROUGH */
		      case 'B':		/* CUD: Cursor down */
			CLAMP(term->esc_args[0], term->rows);
			move(term, term->curs.x,
			     term->curs.y + def(term->esc_args[0], 1), 1);
			seen_disp_event(term);
			break;
                      case 'b':        /* REP: repeat previous grap */
                        CLAMP(term->esc_args[0], term->rows * term->cols);
                        if (term->last_graphic_char) {
                            unsigned i;
                            for (i = 0; i < term->esc_args[0]; i++)
                                term_display_graphic_char(
                                    term, term->last_graphic_char);
                        }
                        break;
		      case ANSI('c', '>'):	/* DA: report xterm version */
			compatibility(OTHER);
			/* this reports xterm version 136 so that VIM can
			   use the drag messages from the mouse reporting */
			if (term->ldisc)
			    ldisc_send(term->ldisc, "\033[>0;136;0c", 11,
                                       false);
			break;
		      case 'a':		/* HPR: move right N cols */
			compatibility(ANSI);
			/* FALLTHROUGH */
		      case 'C':		/* CUF: Cursor right */ 
			CLAMP(term->esc_args[0], term->cols);
			move(term, term->curs.x + def(term->esc_args[0], 1),
			     term->curs.y, 1);
			seen_disp_event(term);
			break;
		      case 'D':       /* CUB: move left N cols */
			CLAMP(term->esc_args[0], term->cols);
			move(term, term->curs.x - def(term->esc_args[0], 1),
			     term->curs.y, 1);
			seen_disp_event(term);
			break;
		      case 'E':       /* CNL: move down N lines and CR */
			compatibility(ANSI);
			CLAMP(term->esc_args[0], term->rows);
			move(term, 0,
			     term->curs.y + def(term->esc_args[0], 1), 1);
			seen_disp_event(term);
			break;
		      case 'F':       /* CPL: move up N lines and CR */
			compatibility(ANSI);
			CLAMP(term->esc_args[0], term->rows);
			move(term, 0,
			     term->curs.y - def(term->esc_args[0], 1), 1);
			seen_disp_event(term);
			break;
		      case 'G':	      /* CHA */
		      case '`':       /* HPA: set horizontal posn */
			compatibility(ANSI);
			CLAMP(term->esc_args[0], term->cols);
			move(term, def(term->esc_args[0], 1) - 1,
			     term->curs.y, 0);
			seen_disp_event(term);
			break;
		      case 'd':       /* VPA: set vertical posn */
			compatibility(ANSI);
			CLAMP(term->esc_args[0], term->rows);
			move(term, term->curs.x,
			     ((term->dec_om ? term->marg_t : 0) +
			      def(term->esc_args[0], 1) - 1),
			     (term->dec_om ? 2 : 0));
			seen_disp_event(term);
			break;
		      case 'H':	     /* CUP */
		      case 'f':      /* HVP: set horz and vert posns at once */
			if (term->esc_nargs < 2)
			    term->esc_args[1] = ARG_DEFAULT;
			CLAMP(term->esc_args[0], term->rows);
			CLAMP(term->esc_args[1], term->cols);
			move(term, def(term->esc_args[1], 1) - 1,
			     ((term->dec_om ? term->marg_t : 0) +
			      def(term->esc_args[0], 1) - 1),
			     (term->dec_om ? 2 : 0));
			seen_disp_event(term);
			break;
                      case 'J': {       /* ED: erase screen or parts of it */
			    unsigned int i = def(term->esc_args[0], 0);
			    if (i == 3) {
				/* Erase Saved Lines (xterm)
				 * This follows Thomas Dickey's xterm. */
                                if (!term->no_remote_clearscroll)
                                    term_clrsb(term);
			    } else {
				i++;
				if (i > 3)
				    i = 0;
				erase_lots(term, false, !!(i & 2), !!(i & 1));
			    }
			if (term->scroll_on_disp)
                            term->disptop = 0;
			seen_disp_event(term);
			break;
                      }
                      case 'K': {       /* EL: erase line or parts of it */
			    unsigned int i = def(term->esc_args[0], 0) + 1;
			    if (i > 3)
				i = 0;
			    erase_lots(term, true, !!(i & 2), !!(i & 1));
			seen_disp_event(term);
			break;
                      }
		      case 'L':       /* IL: insert lines */
			compatibility(VT102);
			CLAMP(term->esc_args[0], term->rows);
			if (term->curs.y <= term->marg_b)
			    scroll(term, term->curs.y, term->marg_b,
				   -def(term->esc_args[0], 1), false);
			seen_disp_event(term);
			break;
		      case 'M':       /* DL: delete lines */
			compatibility(VT102);
			CLAMP(term->esc_args[0], term->rows);
			if (term->curs.y <= term->marg_b)
			    scroll(term, term->curs.y, term->marg_b,
				   def(term->esc_args[0], 1),
				   true);
			seen_disp_event(term);
			break;
		      case '@':       /* ICH: insert chars */
			/* XXX VTTEST says this is vt220, vt510 manual says vt102 */
			compatibility(VT102);
			CLAMP(term->esc_args[0], term->cols);
			insch(term, def(term->esc_args[0], 1));
			seen_disp_event(term);
			break;
		      case 'P':       /* DCH: delete chars */
			compatibility(VT102);
			CLAMP(term->esc_args[0], term->cols);
			insch(term, -def(term->esc_args[0], 1));
			seen_disp_event(term);
			break;
		      case 'c':       /* DA: terminal type query */
			compatibility(VT100);
			/* This is the response for a VT102 */
                        if (term->ldisc)
			    ldisc_send(term->ldisc, term->id_string,
                                       strlen(term->id_string), false);
			break;
		      case 'n':       /* DSR: cursor position query */
			if (term->ldisc) {
			    if (term->esc_args[0] == 6) {
				char buf[32];
				sprintf(buf, "\033[%d;%dR", term->curs.y + 1,
					term->curs.x + 1);
				ldisc_send(term->ldisc, buf, strlen(buf),
                                           false);
			    } else if (term->esc_args[0] == 5) {
				ldisc_send(term->ldisc, "\033[0n", 4, false);
			    }
			}
			break;
		      case 'h':       /* SM: toggle modes to high */
		      case ANSI_QUE('h'):
			compatibility(VT100);
                        for (int i = 0; i < term->esc_nargs; i++)
				toggle_mode(term, term->esc_args[i],
					    term->esc_query, true);
			break;
		      case 'i':		/* MC: Media copy */
                      case ANSI_QUE('i'): {
			compatibility(VT100);
			    char *printer;
			    if (term->esc_nargs != 1) break;
			    if (term->esc_args[0] == 5 && 
#ifdef MOD_PRINTCLIP
				((printer = conf_get_str(term->conf,
							CONF_printer))[0] || conf_get_int(term->conf,CONF_printclip)) ) {
#else
				(printer = conf_get_str(term->conf,
							CONF_printer))[0]) {
#endif
				term->printing = true;
				term->only_printing = !term->esc_query;
				term->print_state = 0;
				term_print_setup(term, printer);
			    } else if (term->esc_args[0] == 4 &&
				       term->printing) {
				term_print_finish(term);
			    }
			break;			
                      }
		      case 'l':       /* RM: toggle modes to low */
		      case ANSI_QUE('l'):
			compatibility(VT100);
                        for (int i = 0; i < term->esc_nargs; i++)
				toggle_mode(term, term->esc_args[i],
					    term->esc_query, false);
			break;
		      case 'g':       /* TBC: clear tabs */
			compatibility(VT100);
			if (term->esc_nargs == 1) {
			    if (term->esc_args[0] == 0) {
				term->tabs[term->curs.x] = false;
			    } else if (term->esc_args[0] == 3) {
				int i;
				for (i = 0; i < term->cols; i++)
				    term->tabs[i] = false;
			    }
			}
			break;
		      case 'r':       /* DECSTBM: set scroll margins */
			compatibility(VT100);
			if (term->esc_nargs <= 2) {
			    int top, bot;
			    CLAMP(term->esc_args[0], term->rows);
			    CLAMP(term->esc_args[1], term->rows);
			    top = def(term->esc_args[0], 1) - 1;
			    bot = (term->esc_nargs <= 1
				   || term->esc_args[1] == 0 ?
				   term->rows :
				   def(term->esc_args[1], term->rows)) - 1;
			    if (bot >= term->rows)
				bot = term->rows - 1;
			    /* VTTEST Bug 9 - if region is less than 2 lines
			     * don't change region.
			     */
			    if (bot - top > 0) {
				term->marg_t = top;
				term->marg_b = bot;
				term->curs.x = 0;
				/*
				 * I used to think the cursor should be
				 * placed at the top of the newly marginned
				 * area. Apparently not: VMS TPU falls over
				 * if so.
				 *
				 * Well actually it should for
				 * Origin mode - RDB
				 */
				term->curs.y = (term->dec_om ?
						term->marg_t : 0);
				seen_disp_event(term);
			    }
			}
			break;
		      case 'm':       /* SGR: set graphics rendition */
			/* 
			 * A VT100 without the AVO only had one
                         * attribute, either underline or reverse
                         * video depending on the cursor type, this
                         * was selected by CSI 7m.
			 *
			 * case 2:
                         *  This is sometimes DIM, eg on the GIGI and
                         *  Linux
			 * case 8:
			 *  This is sometimes INVIS various ANSI.
			 * case 21:
			 *  This like 22 disables BOLD, DIM and INVIS
			 *
                         * The ANSI colours appear on any terminal
                         * that has colour (obviously) but the
                         * interaction between sgr0 and the colours
                         * varies but is usually related to the
                         * background colour erase item. The
                         * interaction between colour attributes and
                         * the mono ones is also very implementation
			 * dependent.
			 *
                         * The 39 and 49 attributes are likely to be
                         * unimplemented.
			 */
                        for (int i = 0; i < term->esc_nargs; i++)
				switch (def(term->esc_args[i], 0)) {
				  case 0:	/* restore defaults */
				    term->curr_attr = term->default_attr;
				    term->curr_truecolour =
                                        term->basic_erase_char.truecolour;
				    break;
				  case 1:	/* enable bold */
				    compatibility(VT100AVO);
				    term->curr_attr |= ATTR_BOLD;
				    break;
				  case 2:	/* enable dim */
				    compatibility(OTHER);
				    term->curr_attr |= ATTR_DIM;
				    break;
#ifdef MOD_PERSO
				  case 3:	/* enable italics */
				    compatibility(ANSI);
				    term->curr_attr |= ATTR_ITALIC;
				    break;
#endif
				  case 21:	/* (enable double underline) */
				    compatibility(OTHER);
				  case 4:	/* enable underline */
				    compatibility(VT100AVO);
				    term->curr_attr |= ATTR_UNDER;
				    break;
				  case 5:	/* enable blink */
				    compatibility(VT100AVO);
				    term->curr_attr |= ATTR_BLINK;
				    break;
				  case 6:	/* SCO light bkgrd */
				    compatibility(SCOANSI);
				    term->blink_is_real = false;
				    term->curr_attr |= ATTR_BLINK;
				    term_schedule_tblink(term);
				    break;
				  case 7:	/* enable reverse video */
				    term->curr_attr |= ATTR_REVERSE;
				    break;
                                  case 9:       /* enable strikethrough */
                                    term->curr_attr |= ATTR_STRIKE;
                                    break;
				  case 10:      /* SCO acs off */
				    compatibility(SCOANSI);
				    if (term->no_remote_charset) break;
				    term->sco_acs = 0; break;
				  case 11:      /* SCO acs on */
				    compatibility(SCOANSI);
				    if (term->no_remote_charset) break;
				    term->sco_acs = 1; break;
				  case 12:      /* SCO acs on, |0x80 */
				    compatibility(SCOANSI);
				    if (term->no_remote_charset) break;
				    term->sco_acs = 2; break;
				  case 22:	/* disable bold and dim */
				    compatibility2(OTHER, VT220);
				    term->curr_attr &= ~(ATTR_BOLD | ATTR_DIM);
				    break;
#ifdef MOD_PERSO
				  case 23:	/* disable italics */
				    compatibility(ANSI);
				    term->curr_attr &= ~ATTR_ITALIC;
				    break;
#endif
				  case 24:	/* disable underline */
				    compatibility2(OTHER, VT220);
				    term->curr_attr &= ~ATTR_UNDER;
				    break;
				  case 25:	/* disable blink */
				    compatibility2(OTHER, VT220);
				    term->curr_attr &= ~ATTR_BLINK;
				    break;
				  case 27:	/* disable reverse video */
				    compatibility2(OTHER, VT220);
				    term->curr_attr &= ~ATTR_REVERSE;
				    break;
                                  case 29:      /* disable strikethrough */
                                    term->curr_attr &= ~ATTR_STRIKE;
                                    break;
				  case 30:
				  case 31:
				  case 32:
				  case 33:
				  case 34:
				  case 35:
				  case 36:
				  case 37:
				    /* foreground */
				    term->curr_truecolour.fg.enabled = false;
				    term->curr_attr &= ~ATTR_FGMASK;
				    term->curr_attr |=
					(term->esc_args[i] - 30)<<ATTR_FGSHIFT;
				    break;
				  case 90:
				  case 91:
				  case 92:
				  case 93:
				  case 94:
				  case 95:
				  case 96:
				  case 97:
				    /* aixterm-style bright foreground */
				    term->curr_truecolour.fg.enabled = false;
				    term->curr_attr &= ~ATTR_FGMASK;
				    term->curr_attr |=
					((term->esc_args[i] - 90 + 8)
                                         << ATTR_FGSHIFT);
				    break;
				  case 39:	/* default-foreground */
				    term->curr_truecolour.fg.enabled = false;
				    term->curr_attr &= ~ATTR_FGMASK;
				    term->curr_attr |= ATTR_DEFFG;
				    break;
				  case 40:
				  case 41:
				  case 42:
				  case 43:
				  case 44:
				  case 45:
				  case 46:
				  case 47:
				    /* background */
				    term->curr_truecolour.bg.enabled = false;
				    term->curr_attr &= ~ATTR_BGMASK;
				    term->curr_attr |=
					(term->esc_args[i] - 40)<<ATTR_BGSHIFT;
				    break;
				  case 100:
				  case 101:
				  case 102:
				  case 103:
				  case 104:
				  case 105:
				  case 106:
				  case 107:
				    /* aixterm-style bright background */
				    term->curr_truecolour.bg.enabled = false;
				    term->curr_attr &= ~ATTR_BGMASK;
				    term->curr_attr |=
					((term->esc_args[i] - 100 + 8)
                                         << ATTR_BGSHIFT);
				    break;
				  case 49:	/* default-background */
				    term->curr_truecolour.bg.enabled = false;
				    term->curr_attr &= ~ATTR_BGMASK;
				    term->curr_attr |= ATTR_DEFBG;
				    break;

                                    /*
                                     * 256-colour and true-colour
                                     * sequences. A 256-colour
                                     * foreground is selected by a
                                     * sequence of 3 arguments in the
                                     * form 38;5;n, where n is in the
                                     * range 0-255. A true-colour RGB
                                     * triple is selected by 5 args of
                                     * the form 38;2;r;g;b. Replacing
                                     * the initial 38 with 48 in both
                                     * cases selects the same colour
                                     * as the background.
                                     */
				  case 38:
				    if (i+2 < term->esc_nargs &&
					term->esc_args[i+1] == 5) {
					term->curr_attr &= ~ATTR_FGMASK;
					term->curr_attr |=
					    ((term->esc_args[i+2] & 0xFF)
					     << ATTR_FGSHIFT);
                                        term->curr_truecolour.fg =
                                            optionalrgb_none;
					i += 2;
					}
				    if (i + 4 < term->esc_nargs &&
					term->esc_args[i + 1] == 2) {
					parse_optionalrgb(
                                            &term->curr_truecolour.fg,
                                            term->esc_args + (i+2));
					i += 4;
				    }
				    break;
				  case 48:
				    if (i+2 < term->esc_nargs &&
					term->esc_args[i+1] == 5) {
					term->curr_attr &= ~ATTR_BGMASK;
					term->curr_attr |=
					    ((term->esc_args[i+2] & 0xFF)
					     << ATTR_BGSHIFT);
                                        term->curr_truecolour.bg =
                                            optionalrgb_none;
					i += 2;
				    }
				    if (i + 4 < term->esc_nargs &&
					term->esc_args[i+1] == 2) {
					parse_optionalrgb(
                                            &term->curr_truecolour.bg,
                                            term->esc_args + (i+2));
					i += 4;
				    }
				    break;
				}
			    set_erase_char(term);
			break;
		      case 's':       /* save cursor */
			save_cursor(term, true);
			break;
		      case 'u':       /* restore cursor */
			save_cursor(term, false);
			seen_disp_event(term);
			break;
		      case 't': /* DECSLPP: set page size - ie window height */
			/*
			 * VT340/VT420 sequence DECSLPP, DEC only allows values
			 *  24/25/36/48/72/144 other emulators (eg dtterm) use
			 * illegal values (eg first arg 1..9) for window changing 
			 * and reports.
			 */
			if (term->esc_nargs <= 1
			    && (term->esc_args[0] < 1 ||
				term->esc_args[0] >= 24)) {
			    compatibility(VT340TEXT);
                            if (!term->no_remote_resize) {
                                term->win_resize_pending = true;
                                term->win_resize_pending_w = term->cols;
                                term->win_resize_pending_h =
                                    def(term->esc_args[0], 24);
                                term_schedule_update(term);
                            }
			    deselect(term);
			} else if (term->esc_nargs >= 1 &&
				   term->esc_args[0] >= 1 &&
				   term->esc_args[0] < 24) {
			    compatibility(OTHER);

			    switch (term->esc_args[0]) {
                                int len;
				char buf[80];
                                const char *p;
			      case 1:
                                term->win_minimise_pending = true;
                                term->win_minimise_enable = false;
                                term_schedule_update(term);
				break;
			      case 2:
                                term->win_minimise_pending = true;
                                term->win_minimise_enable = true;
                                term_schedule_update(term);
				break;
			      case 3:
				if (term->esc_nargs >= 3) {
                                    if (!term->no_remote_resize) {
                                        term->win_move_pending = true;
                                        term->win_move_pending_x =
                                            def(term->esc_args[1], 0);
                                        term->win_move_pending_y =
                                            def(term->esc_args[2], 0);
                                        term_schedule_update(term);
                                    }
				}
				break;
			      case 4:
				/* We should resize the window to a given
				 * size in pixels here, but currently our
				 * resizing code isn't healthy enough to
				 * manage it. */
				break;
			      case 5:
				/* move to top */
                                term->win_zorder_pending = true;
                                term->win_zorder_top = true;
                                term_schedule_update(term);
				break;
			      case 6:
				/* move to bottom */
                                term->win_zorder_pending = true;
                                term->win_zorder_top = false;
                                term_schedule_update(term);
				break;
			      case 7:
                                term->win_refresh_pending = true;
                                term_schedule_update(term);
				break;
			      case 8:
                                if (term->esc_nargs >= 3 &&
                                    !term->no_remote_resize) {
                                    term->win_resize_pending = true;
                                    term->win_resize_pending_w =
                                            def(term->esc_args[2],
                                            term->conf_width);
                                    term->win_resize_pending_h =
                                            def(term->esc_args[1],
                                            term->conf_height);
                                    term_schedule_update(term);
				}
				break;
			      case 9:
                                if (term->esc_nargs >= 2) {
                                    term->win_maximise_pending = true;
                                    term->win_maximise_enable =
                                        term->esc_args[1];
                                    term_schedule_update(term);
                                }
				break;
			      case 11:
				if (term->ldisc)
                                    ldisc_send(term->ldisc, term->minimised ?
					       "\033[2t" : "\033[1t", 4,
                                               false);
				break;
			      case 13:
				if (term->ldisc) {
				    len = sprintf(buf, "\033[3;%u;%ut",
                                                  term->winpos_x,
                                                  term->winpos_y);
				    ldisc_send(term->ldisc, buf, len, false);
				}
				break;
			      case 14:
				if (term->ldisc) {
                                    len = sprintf(buf, "\033[4;%u;%ut",
                                                  term->winpixsize_y,
                                                  term->winpixsize_x);
				    ldisc_send(term->ldisc, buf, len, false);
				}
				break;
			      case 18:
				if (term->ldisc) {
				    len = sprintf(buf, "\033[8;%d;%dt",
						  term->rows, term->cols);
				    ldisc_send(term->ldisc, buf, len, false);
				}
				break;
			      case 19:
				/*
				 * Hmmm. Strictly speaking we
				 * should return `the size of the
				 * screen in characters', but
				 * that's not easy: (a) window
				 * furniture being what it is it's
				 * hard to compute, and (b) in
				 * resize-font mode maximising the
				 * window wouldn't change the
				 * number of characters. *shrug*. I
				 * think we'll ignore it for the
				 * moment and see if anyone
				 * complains, and then ask them
				 * what they would like it to do.
				 */
				break;
			      case 20:
				if (term->ldisc &&
				    term->remote_qtitle_action != TITLE_NONE) {
				    if(term->remote_qtitle_action == TITLE_REAL)
                                        p = term->icon_title;
				    else
					p = EMPTY_WINDOW_TITLE;
				    len = strlen(p);
				    ldisc_send(term->ldisc, "\033]L", 3,
                                               false);
                                        ldisc_send(term->ldisc, p, len, false);
				    ldisc_send(term->ldisc, "\033\\", 2,
                                               false);
				}
				break;
			      case 21:
				if (term->ldisc &&
				    term->remote_qtitle_action != TITLE_NONE) {
				    if(term->remote_qtitle_action == TITLE_REAL)
                                        p = term->window_title;
				    else
					p = EMPTY_WINDOW_TITLE;
				    len = strlen(p);
				    ldisc_send(term->ldisc, "\033]l", 3,
                                               false);
                                        ldisc_send(term->ldisc, p, len, false);
				    ldisc_send(term->ldisc, "\033\\", 2,
                                               false);
				}
				break;
			    }
			}
			break;
		      case 'S':		/* SU: Scroll up */
			CLAMP(term->esc_args[0], term->rows);
			compatibility(SCOANSI);
			scroll(term, term->marg_t, term->marg_b,
			       def(term->esc_args[0], 1), true);
			term->wrapnext = false;
			seen_disp_event(term);
			break;
		      case 'T':		/* SD: Scroll down */
			CLAMP(term->esc_args[0], term->rows);
			compatibility(SCOANSI);
			scroll(term, term->marg_t, term->marg_b,
			       -def(term->esc_args[0], 1), true);
			term->wrapnext = false;
			seen_disp_event(term);
			break;
		      case ANSI('|', '*'): /* DECSNLS */
			/* 
			 * Set number of lines on screen
			 * VT420 uses VGA like hardware and can
			 * support any size in reasonable range
			 * (24..49 AIUI) with no default specified.
			 */
			compatibility(VT420);
			if (term->esc_nargs == 1 && term->esc_args[0] > 0) {
                            if (!term->no_remote_resize) {
                                term->win_resize_pending = true;
                                term->win_resize_pending_w = term->cols;
                                term->win_resize_pending_h =
                                    def(term->esc_args[0], term->conf_height);
                                term_schedule_update(term);
                            }
			    deselect(term);
			}
			break;
		      case ANSI('|', '$'): /* DECSCPP */
			/*
			 * Set number of columns per page
			 * Docs imply range is only 80 or 132, but
			 * I'll allow any.
			 */
			compatibility(VT340TEXT);
			if (term->esc_nargs <= 1) {
                            if (!term->no_remote_resize) {
                                term->win_resize_pending = true;
                                term->win_resize_pending_w =
                                    def(term->esc_args[0], term->conf_width);
                                term->win_resize_pending_h = term->rows;
                                term_schedule_update(term);
                            }
			    deselect(term);
			}
			break;
                      case 'X': {   /* ECH: write N spaces w/o moving cursor */
			/* XXX VTTEST says this is vt220, vt510 manual
			 * says vt100 */
			compatibility(ANSIMIN);
			CLAMP(term->esc_args[0], term->cols);
			    int n = def(term->esc_args[0], 1);
			    pos cursplus;
			    int p = term->curs.x;
			    termline *cline = scrlineptr(term->curs.y);

                            check_trust_status(term, cline);
			    if (n > term->cols - term->curs.x)
				n = term->cols - term->curs.x;
			    cursplus = term->curs;
			    cursplus.x += n;
			    check_boundary(term, term->curs.x, term->curs.y);
			    check_boundary(term, term->curs.x+n, term->curs.y);
			    check_selection(term, term->curs, cursplus);
			    while (n--)
				copy_termchar(cline, p++,
					      &term->erase_char);
			    seen_disp_event(term);
			break;
                      }
		      case 'x':       /* DECREQTPARM: report terminal characteristics */
			compatibility(VT100);
			if (term->ldisc) {
			    char buf[32];
			    int i = def(term->esc_args[0], 0);
			    if (i == 0 || i == 1) {
				strcpy(buf, "\033[2;1;1;112;112;1;0x");
				buf[2] += i;
				ldisc_send(term->ldisc, buf, 20, false);
			    }
			}
			break;
                      case 'Z': {         /* CBT */
			compatibility(OTHER);
			CLAMP(term->esc_args[0], term->cols);
			    int i = def(term->esc_args[0], 1);
			    pos old_curs = term->curs;

			    for(;i>0 && term->curs.x>0; i--) {
				do {
				    term->curs.x--;
				} while (term->curs.x >0 &&
					 !term->tabs[term->curs.x]);
			    }
			    check_selection(term, old_curs, term->curs);
			break;
                      }
		      case ANSI('c', '='):      /* Hide or Show Cursor */
			compatibility(SCOANSI);
			switch(term->esc_args[0]) {
			  case 0:  /* hide cursor */
			    term->cursor_on = false;
			    break;
			  case 1:  /* restore cursor */
			    term->big_cursor = false;
			    term->cursor_on = true;
			    break;
			  case 2:  /* block cursor */
			    term->big_cursor = true;
			    term->cursor_on = true;
			    break;
			}
			break;
		      case ANSI('C', '='):
			/*
			 * set cursor start on scanline esc_args[0] and
			 * end on scanline esc_args[1].If you set
			 * the bottom scan line to a value less than
			 * the top scan line, the cursor will disappear.
			 */
			compatibility(SCOANSI);
			if (term->esc_nargs >= 2) {
			    if (term->esc_args[0] > term->esc_args[1])
				term->cursor_on = false;
			    else
				term->cursor_on = true;
			}
			break;
		      case ANSI('D', '='):
			compatibility(SCOANSI);
			term->blink_is_real = false;
			term_schedule_tblink(term);
			if (term->esc_args[0]>=1)
			    term->curr_attr |= ATTR_BLINK;
			else
			    term->curr_attr &= ~ATTR_BLINK;
			break;
		      case ANSI('E', '='):
			compatibility(SCOANSI);
			term->blink_is_real = (term->esc_args[0] >= 1);
			term_schedule_tblink(term);
			break;
		      case ANSI('F', '='):      /* set normal foreground */
			compatibility(SCOANSI);
			if (term->esc_args[0] < 16) {
			    long colour =
 				(sco2ansicolour[term->esc_args[0] & 0x7] |
				 (term->esc_args[0] & 0x8)) <<
				ATTR_FGSHIFT;
			    term->curr_attr &= ~ATTR_FGMASK;
			    term->curr_attr |= colour;
                            term->curr_truecolour.fg = optionalrgb_none;
			    term->default_attr &= ~ATTR_FGMASK;
			    term->default_attr |= colour;
			    set_erase_char(term);
			}
			break;
		      case ANSI('G', '='):      /* set normal background */
			compatibility(SCOANSI);
			if (term->esc_args[0] < 16) {
			    long colour =
 				(sco2ansicolour[term->esc_args[0] & 0x7] |
				 (term->esc_args[0] & 0x8)) <<
				ATTR_BGSHIFT;
			    term->curr_attr &= ~ATTR_BGMASK;
			    term->curr_attr |= colour;
                            term->curr_truecolour.bg = optionalrgb_none;
			    term->default_attr &= ~ATTR_BGMASK;
			    term->default_attr |= colour;
			    set_erase_char(term);
			}
			break;
		      case ANSI('L', '='):
			compatibility(SCOANSI);
			term->use_bce = (term->esc_args[0] <= 0);
			set_erase_char(term);
			break;
		      case ANSI('p', '"'): /* DECSCL: set compat level */
			/*
			 * Allow the host to make this emulator a
			 * 'perfect' VT102. This first appeared in
			 * the VT220, but we do need to get back to
			 * PuTTY mode so I won't check it.
			 *
			 * The arg in 40..42,50 are a PuTTY extension.
			 * The 2nd arg, 8bit vs 7bit is not checked.
			 *
			 * Setting VT102 mode should also change
			 * the Fkeys to generate PF* codes as a
			 * real VT102 has no Fkeys. The VT220 does
			 * this, F11..F13 become ESC,BS,LF other
			 * Fkeys send nothing.
			 *
			 * Note ESC c will NOT change this!
			 */

			switch (term->esc_args[0]) {
			  case 61:
			    term->compatibility_level &= ~TM_VTXXX;
			    term->compatibility_level |= TM_VT102;
			    break;
			  case 62:
			    term->compatibility_level &= ~TM_VTXXX;
			    term->compatibility_level |= TM_VT220;
			    break;

			  default:
			    if (term->esc_args[0] > 60 &&
				term->esc_args[0] < 70)
				term->compatibility_level |= TM_VTXXX;
			    break;

			  case 40:
			    term->compatibility_level &= TM_VTXXX;
			    break;
			  case 41:
			    term->compatibility_level = TM_PUTTY;
			    break;
			  case 42:
			    term->compatibility_level = TM_SCOANSI;
			    break;

			  case ARG_DEFAULT:
			    term->compatibility_level = TM_PUTTY;
			    break;
			  case 50:
			    break;
			}

			/* Change the response to CSI c */
			if (term->esc_args[0] == 50) {
			    int i;
			    char lbuf[64];
			    strcpy(term->id_string, "\033[?");
			    for (i = 1; i < term->esc_nargs; i++) {
				if (i != 1)
				    strcat(term->id_string, ";");
				sprintf(lbuf, "%u", term->esc_args[i]);
				strcat(term->id_string, lbuf);
			    }
			    strcat(term->id_string, "c");
			}
#if 0
			/* Is this a good idea ? 
			 * Well we should do a soft reset at this point ...
			 */
			if (!has_compat(VT420) && has_compat(VT100)) {
			    if (!term->no_remote_resize) {
                                term->win_resize_pending = true;
                                term->win_resize_pending_w =
                                    term->reset_132 ? 132 : 80;
                                term->win_resize_pending_h = 24;
                                term_schedule_update(term);
			    }
			}
#endif
			break;
		    }
		break;
	      case SEEN_OSC:
		term->osc_w = false;
		switch (c) {
		  case 'P':	       /* Linux palette sequence */
		    term->termstate = SEEN_OSC_P;
		    term->osc_strlen = 0;
		    break;
		  case 'R':	       /* Linux palette reset */
                    palette_reset(term, false);
		    term_invalidate(term);
		    term->termstate = TOPLEVEL;
		    break;
		  case 'W':	       /* word-set */
		    term->termstate = SEEN_OSC_W;
		    term->osc_w = true;
		    break;
		  case '0':
		  case '1':
		  case '2':
		  case '3':
		  case '4':
		  case '5':
		  case '6':
		  case '7':
		  case '8':
		  case '9':
		    if (term->esc_args[term->esc_nargs-1] <= UINT_MAX / 10 &&
			term->esc_args[term->esc_nargs-1] * 10 <= UINT_MAX - c - '0')
			term->esc_args[term->esc_nargs-1] =
                            10 * term->esc_args[term->esc_nargs-1] + c - '0';
		    else
			term->esc_args[term->esc_nargs-1] = UINT_MAX;
		    break;
                  default:
                    /*
                     * _Most_ other characters here terminate the
                     * immediate parsing of the OSC sequence and go
                     * into OSC_STRING state, but we deal with a
                     * couple of exceptions first.
                     */
                    if (c == 'L' && term->esc_args[0] == 2) {
                        /*
                         * Grotty hack to support xterm and DECterm title
                         * sequences concurrently.
                         */
                        term->esc_args[0] = 1;
                    } else if (c == ';' && term->esc_nargs == 1 &&
                               term->esc_args[0] == 4) {
                        /*
                         * xterm's OSC 4 sequence to query the current
                         * RGB value of a colour takes a second
                         * numeric argument which is easiest to parse
                         * using the existing system rather than in
                         * do_osc.
                         */
                        term->esc_args[term->esc_nargs++] = 0;
                    } else {
                        term->termstate = OSC_STRING;
                        term->osc_strlen = 0;
#ifdef MOD_FAR2L
                        /* far2l */
                        if (term->is_apc) {
                            term->osc_string[term->osc_strlen++] = (char)c;
                        }
#endif
                    }
		}
		break;
	      case OSC_STRING:
		/*
		 * This OSC stuff is EVIL. It takes just one character to get into
		 * sysline mode and it's not initially obvious how to get out.
		 * So I've added CR and LF as string aborts.
		 * This shouldn't effect compatibility as I believe embedded 
		 * control characters are supposed to be interpreted (maybe?) 
		 * and they don't display anything useful anyway.
		 *
		 * -- RDB
		 */
		if (c == '\012' || c == '\015') {
		    term->termstate = TOPLEVEL;
		} else if (c == 0234 || c == '\007') {
		    /*
		     * These characters terminate the string; ST and BEL
		     * terminate the sequence and trigger instant
		     * processing of it, whereas ESC goes back to SEEN_ESC
		     * mode unless it is followed by \, in which case it is
		     * synonymous with ST in the first place.
		     */
		    do_osc(term);
		    term->termstate = TOPLEVEL;
		} else if (c == '\033')
		    term->termstate = OSC_MAYBE_ST;
		else if (term->osc_strlen < OSC_STR_MAX)
		    term->osc_string[term->osc_strlen++] = (char)c;
		break;
              case SEEN_OSC_P: {
		    int max = (term->osc_strlen == 0 ? 21 : 15);
		    int val;
		    if ((int)c >= '0' && (int)c <= '9')
			val = c - '0';
		    else if ((int)c >= 'A' && (int)c <= 'A' + max - 10)
			val = c - 'A' + 10;
		    else if ((int)c >= 'a' && (int)c <= 'a' + max - 10)
			val = c - 'a' + 10;
		    else {
			term->termstate = TOPLEVEL;
			break;
		    }
		    term->osc_string[term->osc_strlen++] = val;
		    if (term->osc_strlen >= 7) {
                    unsigned oscp_index = term->osc_string[0];
                    assert(oscp_index < OSCP_NCOLOURS);
                    unsigned osc4_index =
                        colour_indices_oscp_to_osc4[oscp_index];

                    rgb *value = &term->subpalettes[SUBPAL_SESSION].values[
                        osc4_index];
                    value->r = term->osc_string[1] * 16 + term->osc_string[2];
                    value->g = term->osc_string[3] * 16 + term->osc_string[4];
                    value->b = term->osc_string[5] * 16 + term->osc_string[6];
                    term->subpalettes[SUBPAL_SESSION].present[
                        osc4_index] = true;

                    palette_rebuild(term);

			term->termstate = TOPLEVEL;
		    }
		break;
              }
	      case SEEN_OSC_W:
		switch (c) {
		  case '0':
		  case '1':
		  case '2':
		  case '3':
		  case '4':
		  case '5':
		  case '6':
		  case '7':
		  case '8':
		  case '9':
		    if (term->esc_args[0] <= UINT_MAX / 10 &&
			term->esc_args[0] * 10 <= UINT_MAX - c - '0')
			term->esc_args[0] = 10 * term->esc_args[0] + c - '0';
		    else
			term->esc_args[0] = UINT_MAX;
		    break;
		  default:
		    term->termstate = OSC_STRING;
		    term->osc_strlen = 0;
		}
		break;
	      case VT52_ESC:
		term->termstate = TOPLEVEL;
		seen_disp_event(term);
		switch (c) {
		  case 'A':
		    move(term, term->curs.x, term->curs.y - 1, 1);
		    break;
		  case 'B':
		    move(term, term->curs.x, term->curs.y + 1, 1);
		    break;
		  case 'C':
		    move(term, term->curs.x + 1, term->curs.y, 1);
		    break;
		  case 'D':
		    move(term, term->curs.x - 1, term->curs.y, 1);
		    break;
		    /*
		     * From the VT100 Manual
		     * NOTE: The special graphics characters in the VT100
		     *       are different from those in the VT52
		     *
		     * From VT102 manual:
		     *       137 _  Blank             - Same
		     *       140 `  Reserved          - Humm.
		     *       141 a  Solid rectangle   - Similar
		     *       142 b  1/                - Top half of fraction for the
		     *       143 c  3/                - subscript numbers below.
		     *       144 d  5/
		     *       145 e  7/
		     *       146 f  Degrees           - Same
		     *       147 g  Plus or minus     - Same
		     *       150 h  Right arrow
		     *       151 i  Ellipsis (dots)
		     *       152 j  Divide by
		     *       153 k  Down arrow
		     *       154 l  Bar at scan 0
		     *       155 m  Bar at scan 1
		     *       156 n  Bar at scan 2
		     *       157 o  Bar at scan 3     - Similar
		     *       160 p  Bar at scan 4     - Similar
		     *       161 q  Bar at scan 5     - Similar
		     *       162 r  Bar at scan 6     - Same
		     *       163 s  Bar at scan 7     - Similar
		     *       164 t  Subscript 0
		     *       165 u  Subscript 1
		     *       166 v  Subscript 2
		     *       167 w  Subscript 3
		     *       170 x  Subscript 4
		     *       171 y  Subscript 5
		     *       172 z  Subscript 6
		     *       173 {  Subscript 7
		     *       174 |  Subscript 8
		     *       175 }  Subscript 9
		     *       176 ~  Paragraph
		     *
		     */
		  case 'F':
		    term->cset_attr[term->cset = 0] = CSET_LINEDRW;
		    break;
		  case 'G':
		    term->cset_attr[term->cset = 0] = CSET_ASCII;
		    break;
		  case 'H':
		    move(term, 0, 0, 0);
		    break;
		  case 'I':
		    if (term->curs.y == 0)
			scroll(term, 0, term->rows - 1, -1, true);
		    else if (term->curs.y > 0)
			term->curs.y--;
		    term->wrapnext = false;
		    break;
		  case 'J':
		    erase_lots(term, false, false, true);
                    if (term->scroll_on_disp)
                        term->disptop = 0;
		    break;
		  case 'K':
		    erase_lots(term, true, false, true);
		    break;
#if 0
		  case 'V':
		    /* XXX Print cursor line */
		    break;
		  case 'W':
		    /* XXX Start controller mode */
		    break;
		  case 'X':
		    /* XXX Stop controller mode */
		    break;
#endif
		  case 'Y':
		    term->termstate = VT52_Y1;
		    break;
		  case 'Z':
		    if (term->ldisc)
			ldisc_send(term->ldisc, "\033/Z", 3, false);
		    break;
		  case '=':
		    term->app_keypad_keys = true;
		    break;
		  case '>':
		    term->app_keypad_keys = false;
		    break;
		  case '<':
		    /* XXX This should switch to VT100 mode not current or default
		     *     VT mode. But this will only have effect in a VT220+
		     *     emulation.
		     */
		    term->vt52_mode = false;
		    term->blink_is_real = term->blinktext;
		    term_schedule_tblink(term);
		    break;
#if 0
		  case '^':
		    /* XXX Enter auto print mode */
		    break;
		  case '_':
		    /* XXX Exit auto print mode */
		    break;
		  case ']':
		    /* XXX Print screen */
		    break;
#endif

#ifdef VT52_PLUS
		  case 'E':
		    /* compatibility(ATARI) */
		    move(term, 0, 0, 0);
		    erase_lots(term, false, false, true);
                    if (term->scroll_on_disp)
                        term->disptop = 0;
		    break;
		  case 'L':
		    /* compatibility(ATARI) */
		    if (term->curs.y <= term->marg_b)
			scroll(term, term->curs.y, term->marg_b, -1, false);
		    break;
		  case 'M':
		    /* compatibility(ATARI) */
		    if (term->curs.y <= term->marg_b)
			scroll(term, term->curs.y, term->marg_b, 1, true);
		    break;
		  case 'b':
		    /* compatibility(ATARI) */
		    term->termstate = VT52_FG;
		    break;
		  case 'c':
		    /* compatibility(ATARI) */
		    term->termstate = VT52_BG;
		    break;
		  case 'd':
		    /* compatibility(ATARI) */
		    erase_lots(term, false, true, false);
                    if (term->scroll_on_disp)
                        term->disptop = 0;
		    break;
		  case 'e':
		    /* compatibility(ATARI) */
		    term->cursor_on = true;
		    break;
		  case 'f':
		    /* compatibility(ATARI) */
		    term->cursor_on = false;
		    break;
		    /* case 'j': Save cursor position - broken on ST */
		    /* case 'k': Restore cursor position */
		  case 'l':
		    /* compatibility(ATARI) */
		    erase_lots(term, true, true, true);
		    term->curs.x = 0;
		    term->wrapnext = false;
		    break;
		  case 'o':
		    /* compatibility(ATARI) */
		    erase_lots(term, true, true, false);
		    break;
		  case 'p':
		    /* compatibility(ATARI) */
		    term->curr_attr |= ATTR_REVERSE;
		    break;
		  case 'q':
		    /* compatibility(ATARI) */
		    term->curr_attr &= ~ATTR_REVERSE;
		    break;
		  case 'v':	       /* wrap Autowrap on - Wyse style */
		    /* compatibility(ATARI) */
		    term->wrap = true;
		    break;
		  case 'w':	       /* Autowrap off */
		    /* compatibility(ATARI) */
		    term->wrap = false;
		    break;

		  case 'R':
		    /* compatibility(OTHER) */
		    term->vt52_bold = false;
		    term->curr_attr = ATTR_DEFAULT;
                    term->curr_truecolour.fg = optionalrgb_none;
                    term->curr_truecolour.bg = optionalrgb_none;
		    set_erase_char(term);
		    break;
		  case 'S':
		    /* compatibility(VI50) */
		    term->curr_attr |= ATTR_UNDER;
		    break;
		  case 'W':
		    /* compatibility(VI50) */
		    term->curr_attr &= ~ATTR_UNDER;
		    break;
		  case 'U':
		    /* compatibility(VI50) */
		    term->vt52_bold = true;
		    term->curr_attr |= ATTR_BOLD;
		    break;
		  case 'T':
		    /* compatibility(VI50) */
		    term->vt52_bold = false;
		    term->curr_attr &= ~ATTR_BOLD;
		    break;
#endif
		}
		break;
	      case VT52_Y1:
		term->termstate = VT52_Y2;
		move(term, term->curs.x, c - ' ', 0);
		break;
	      case VT52_Y2:
		term->termstate = TOPLEVEL;
		move(term, c - ' ', term->curs.y, 0);
		break;

#ifdef VT52_PLUS
	      case VT52_FG:
		term->termstate = TOPLEVEL;
		term->curr_attr &= ~ATTR_FGMASK;
		term->curr_attr &= ~ATTR_BOLD;
		term->curr_attr |= (c & 0xF) << ATTR_FGSHIFT;
		set_erase_char(term);
		break;
	      case VT52_BG:
		term->termstate = TOPLEVEL;
		term->curr_attr &= ~ATTR_BGMASK;
		term->curr_attr &= ~ATTR_BLINK;
		term->curr_attr |= (c & 0xF) << ATTR_BGSHIFT;
		set_erase_char(term);
		break;
#endif
	      default: break;	       /* placate gcc warning about enum use */
	    }
	if (term->selstate != NO_SELECTION) {
	    pos cursplus = term->curs;
	    incpos(cursplus);
	    check_selection(term, term->curs, cursplus);
	}
    }

    term_print_flush(term);
    if (term->logflush && term->logctx)
	logflush(term->logctx);
}

/*
 * Small subroutine to parse three consecutive escape-sequence
 * arguments representing a true-colour RGB triple into an
 * optionalrgb.
 */
static void parse_optionalrgb(optionalrgb *out, unsigned *values)
{
    out->enabled = true;
    out->r = values[0] < 256 ? values[0] : 0;
    out->g = values[1] < 256 ? values[1] : 0;
    out->b = values[2] < 256 ? values[2] : 0;
}

/*
 * To prevent having to run the reasonably tricky bidi algorithm
 * too many times, we maintain a cache of the last lineful of data
 * fed to the algorithm on each line of the display.
 */
static bool term_bidi_cache_hit(Terminal *term, int line,
                                termchar *lbefore, int width, bool trusted)
{
    int i;

    if (!term->pre_bidi_cache)
	return false;		       /* cache doesn't even exist yet! */

    if (line >= term->bidi_cache_size)
	return false;		       /* cache doesn't have this many lines */

    if (!term->pre_bidi_cache[line].chars)
	return false;		       /* cache doesn't contain _this_ line */

    if (term->pre_bidi_cache[line].width != width)
	return false;		       /* line is wrong width */

    if (term->pre_bidi_cache[line].trusted != trusted)
	return false;		       /* line has wrong trust state */

    for (i = 0; i < width; i++)
	if (!termchars_equal(term->pre_bidi_cache[line].chars+i, lbefore+i))
	    return false;	       /* line doesn't match cache */

    return true;		       /* it didn't match. */
}

static void term_bidi_cache_store(Terminal *term, int line, termchar *lbefore,
				  termchar *lafter, bidi_char *wcTo,
				  int width, int size, bool trusted)
{
    size_t i, j;

    if (!term->pre_bidi_cache || term->bidi_cache_size <= line) {
        j = term->bidi_cache_size;
        sgrowarray(term->pre_bidi_cache, term->bidi_cache_size, line);
	term->post_bidi_cache = sresize(term->post_bidi_cache,
					term->bidi_cache_size,
					struct bidi_cache_entry);
	while (j < term->bidi_cache_size) {
	    term->pre_bidi_cache[j].chars =
		term->post_bidi_cache[j].chars = NULL;
	    term->pre_bidi_cache[j].width =
		term->post_bidi_cache[j].width = -1;
	    term->pre_bidi_cache[j].trusted = false;
            term->post_bidi_cache[j].trusted = false;
	    term->pre_bidi_cache[j].forward =
		term->post_bidi_cache[j].forward = NULL;
	    term->pre_bidi_cache[j].backward =
		term->post_bidi_cache[j].backward = NULL;
	    j++;
	}
    }

    sfree(term->pre_bidi_cache[line].chars);
    sfree(term->post_bidi_cache[line].chars);
    sfree(term->post_bidi_cache[line].forward);
    sfree(term->post_bidi_cache[line].backward);

    term->pre_bidi_cache[line].width = width;
    term->pre_bidi_cache[line].trusted = trusted;
    term->pre_bidi_cache[line].chars = snewn(size, termchar);
    term->post_bidi_cache[line].width = width;
    term->post_bidi_cache[line].trusted = trusted;
    term->post_bidi_cache[line].chars = snewn(size, termchar);
    term->post_bidi_cache[line].forward = snewn(width, int);
    term->post_bidi_cache[line].backward = snewn(width, int);

    memcpy(term->pre_bidi_cache[line].chars, lbefore, size * TSIZE);
    memcpy(term->post_bidi_cache[line].chars, lafter, size * TSIZE);
    memset(term->post_bidi_cache[line].forward, 0, width * sizeof(int));
    memset(term->post_bidi_cache[line].backward, 0, width * sizeof(int));

    for (i = j = 0; j < width; j += wcTo[i].nchars, i++) {
	int p = wcTo[i].index;

        if (p != BIDI_CHAR_INDEX_NONE) {
            assert(0 <= p && p < width);

            for (int x = 0; x < wcTo[i].nchars; x++) {
                term->post_bidi_cache[line].backward[j+x] = p+x;
                term->post_bidi_cache[line].forward[p+x] = j+x;
            }
        }
    }
}

/*
 * Prepare the bidi information for a screen line. Returns the
 * transformed list of termchars, or NULL if no transformation at
 * all took place (because bidi is disabled). If return was
 * non-NULL, auxiliary information such as the forward and reverse
 * mappings of permutation position are available in
 * term->post_bidi_cache[scr_y].*.
 */
static termchar *term_bidi_line(Terminal *term, struct termline *ldata,
				int scr_y)
{
    termchar *lchars;
    int it;

    /* Do Arabic shaping and bidi. */
    if (!term->no_bidi || !term->no_arabicshaping ||
        (ldata->trusted && term->cols > TRUST_SIGIL_WIDTH)) {

	if (!term_bidi_cache_hit(term, scr_y, ldata->chars, term->cols,
                                 ldata->trusted)) {

	    if (term->wcFromTo_size < term->cols) {
		term->wcFromTo_size = term->cols;
		term->wcFrom = sresize(term->wcFrom, term->wcFromTo_size,
				       bidi_char);
		term->wcTo = sresize(term->wcTo, term->wcFromTo_size,
				     bidi_char);
	    }

	    for(it=0; it<term->cols ; it++)
	    {
		unsigned long uc = (ldata->chars[it].chr);

		switch (uc & CSET_MASK) {
		  case CSET_LINEDRW:
		    if (!term->rawcnp) {
			uc = term->ucsdata->unitab_xterm[uc & 0xFF];
			break;
		    }
		  case CSET_ASCII:
		    uc = term->ucsdata->unitab_line[uc & 0xFF];
		    break;
		  case CSET_SCOACS:
		    uc = term->ucsdata->unitab_scoacs[uc&0xFF];
		    break;
		}
		switch (uc & CSET_MASK) {
		  case CSET_ACP:
		    uc = term->ucsdata->unitab_font[uc & 0xFF];
		    break;
		  case CSET_OEMCP:
		    uc = term->ucsdata->unitab_oemcp[uc & 0xFF];
		    break;
		}

		term->wcFrom[it].origwc = term->wcFrom[it].wc =
		    (unsigned int)uc;
		term->wcFrom[it].index = it;
		term->wcFrom[it].nchars = 1;
	    }

            if (ldata->trusted && term->cols > TRUST_SIGIL_WIDTH) {
                memmove(
                    term->wcFrom + TRUST_SIGIL_WIDTH, term->wcFrom,
                    (term->cols - TRUST_SIGIL_WIDTH) * sizeof(*term->wcFrom));
                for (it = 0; it < TRUST_SIGIL_WIDTH; it++) {
                    term->wcFrom[it].origwc = term->wcFrom[it].wc =
                        (it == 0 ? TRUST_SIGIL_CHAR :
                         it == 1 ? UCSWIDE : ' ');
                    term->wcFrom[it].index = BIDI_CHAR_INDEX_NONE;
                    term->wcFrom[it].nchars = 1;
                }
            }

            int nbc = 0;
            for (it = 0; it < term->cols; it++) {
                term->wcFrom[nbc] = term->wcFrom[it];
                if (it+1 < term->cols && term->wcFrom[it+1].wc == UCSWIDE) {
                    term->wcFrom[nbc].nchars++;
                    it++;
                }
                nbc++;
            }

	    if(!term->no_bidi)
		do_bidi(term->wcFrom, nbc);

	    if(!term->no_arabicshaping) {
		do_shape(term->wcFrom, term->wcTo, nbc);
            } else {
                /* If we're not calling do_shape, we must copy the
                 * data into wcTo anyway, unchanged */
                memcpy(term->wcTo, term->wcFrom, nbc * sizeof(*term->wcTo));
            }

	    if (term->ltemp_size < ldata->size) {
		term->ltemp_size = ldata->size;
		term->ltemp = sresize(term->ltemp, term->ltemp_size,
				      termchar);
	    }

	    memcpy(term->ltemp, ldata->chars, ldata->size * TSIZE);

            int opos = 0;
	    for (it=0; it<nbc; it++) {
                int ipos = term->wcTo[it].index;
                for (int j = 0; j < term->wcTo[it].nchars; j++) {
                    if (ipos != BIDI_CHAR_INDEX_NONE) {
                        term->ltemp[opos] = ldata->chars[ipos];
                        if (term->ltemp[opos].cc_next)
                            term->ltemp[opos].cc_next -= opos - ipos;

                        if (j > 0)
                            term->ltemp[opos].chr = UCSWIDE;
                        else if (term->wcTo[it].origwc != term->wcTo[it].wc)
                            term->ltemp[opos].chr = term->wcTo[it].wc;
                    } else {
                        term->ltemp[opos] = term->basic_erase_char;
                        term->ltemp[opos].chr =
                            j > 0 ? UCSWIDE : term->wcTo[it].origwc;
                    }
                    opos++;
                }
	    }
            assert(opos == term->cols);
	    term_bidi_cache_store(term, scr_y, ldata->chars,
				  term->ltemp, term->wcTo,
                                  term->cols, ldata->size, ldata->trusted);

	    lchars = term->ltemp;
	} else {
	    lchars = term->post_bidi_cache[scr_y].chars;
	}
    } else {
	lchars = NULL;
    }

    return lchars;
}

static void do_paint_draw(Terminal *term, termline *ldata, int x, int y,
                          wchar_t *ch, int ccount,
                          unsigned long attr, truecolour tc)
{
    if (ch[0] == TRUST_SIGIL_CHAR) {
        assert(ldata->trusted);
        assert(ccount == 1);
        assert(attr & ATTR_WIDE);
        wchar_t tch[2];
        tch[0] = tch[1] = L' ';
        win_draw_text(term->win, x, y, tch, 2, term->basic_erase_char.attr,
                      ldata->lattr, term->basic_erase_char.truecolour);
        win_draw_trust_sigil(term->win, x, y);
    } else {
        win_draw_text(term->win, x, y, ch, ccount, attr, ldata->lattr, tc);
        if (attr & (TATTR_ACTCURS | TATTR_PASCURS))
            win_draw_cursor(term->win, x, y, ch, ccount,
                            attr, ldata->lattr, tc);
    }
}

/*
 * Given a context, update the window.
 */
static void do_paint(Terminal *term)
{
    int i, j, our_curs_y, our_curs_x;
    int rv, cursor;
    pos scrpos;
    wchar_t *ch;
    size_t chlen;
    termchar *newline;
#ifdef MOD_HYPERLINK
	/*
	 * HACK: PuttyTray / Nutty
	 * Hyperlink stuff: Find visible hyperlinks
	 *
	 * TODO: We should find out somehow that the stuff on screen has changed since last
	 *       paint. How to do it?
	 */
	int urlhack_underline_always = conf_get_int(term->conf, CONF_url_underline) == URLHACK_UNDERLINE_ALWAYS;

	int urlhack_underline =
		conf_get_int(term->conf, CONF_url_underline) == URLHACK_UNDERLINE_ALWAYS ||
		(conf_get_int(term->conf, CONF_url_underline) == URLHACK_UNDERLINE_HOVER && (!conf_get_int(term->conf, CONF_url_ctrl_click) || urlhack_is_ctrl_pressed())) ? 1 : 0;

	int urlhack_is_link = 0, urlhack_hover_current = 0;
	int urlhack_toggle_x = term->cols, urlhack_toggle_y = term->rows;
	int urlhack_region_index = 0;
	text_region urlhack_region;

	if( !GetPuttyFlag() && GetHyperlinkFlag() ) {
		if (term->url_update) {
			urlhack_reset();
			for (i = 0; i < term->rows; i++) {
				termline *lp = lineptr(term->disptop + i);
			
				for (j = 0; j < term->cols; j++) {
					unsigned long tchar = lp->chars[j].chr;
					urlhack_putchar(tchar & CHAR_MASK ? (char)(tchar & CHAR_MASK) : ' ');
					//urlhack_putchar((char)(lp->chars[j].chr & CHAR_MASK));
				}
				unlineptr(lp);
			}
			urlhack_go_find_me_some_hyperlinks(term->cols);
		}
		urlhack_region = urlhack_get_link_region(urlhack_region_index);
		urlhack_toggle_x = urlhack_region.x0;
		urlhack_toggle_y = urlhack_region.y0;
		if (urlhack_underline_always)
			urlhack_hover_current = 1;
		else
			urlhack_hover_current = urlhack_is_in_this_link_region(urlhack_region, urlhack_mouse_old_x, urlhack_mouse_old_y);
	}
	/* HACK: PuttyTray / Nutty : END */
#endif

    chlen = 1024;
    ch = snewn(chlen, wchar_t);

    newline = snewn(term->cols, termchar);

    rv = (!term->rvideo ^ !term->in_vbell ? ATTR_REVERSE : 0);

    /* Depends on:
     * screen array, disptop, scrtop,
     * selection, rv, 
     * blinkpc, blink_is_real, tblinker, 
     * curs.y, curs.x, cblinker, blink_cur, cursor_on, has_focus, wrapnext
     */

    /* Has the cursor position or type changed ? */
    if (term->cursor_on) {
	if (term->has_focus) {
	    if (term->cblinker || !term->blink_cur)
		cursor = TATTR_ACTCURS;
	    else
		cursor = 0;
	} else
	    cursor = TATTR_PASCURS;
	if (term->wrapnext)
	    cursor |= TATTR_RIGHTCURS;
    } else
	cursor = 0;
    our_curs_y = term->curs.y - term->disptop;
    {
	/*
	 * Adjust the cursor position:
	 *  - for bidi
	 *  - in the case where it's resting on the right-hand half
	 *    of a CJK wide character. xterm's behaviour here,
	 *    which seems adequate to me, is to display the cursor
	 *    covering the _whole_ character, exactly as if it were
	 *    one space to the left.
	 */
	termline *ldata = lineptr(term->curs.y);
	termchar *lchars;

	our_curs_x = term->curs.x;

	if ( (lchars = term_bidi_line(term, ldata, our_curs_y)) != NULL) {
	    our_curs_x = term->post_bidi_cache[our_curs_y].forward[our_curs_x];
	} else
	    lchars = ldata->chars;

	if (our_curs_x > 0 &&
	    lchars[our_curs_x].chr == UCSWIDE)
	    our_curs_x--;

	unlineptr(ldata);
    }

    /*
     * If the cursor is not where it was last time we painted, and
     * its previous position is visible on screen, invalidate its
     * previous position.
     */
    if (term->dispcursy >= 0 &&
	(term->curstype != cursor ||
	 term->dispcursy != our_curs_y ||
	 term->dispcursx != our_curs_x)) {
	termchar *dispcurs = term->disptext[term->dispcursy]->chars +
	    term->dispcursx;

	if (term->dispcursx > 0 && dispcurs->chr == UCSWIDE)
	    dispcurs[-1].attr |= ATTR_INVALID;
	if (term->dispcursx < term->cols-1 && dispcurs[1].chr == UCSWIDE)
	    dispcurs[1].attr |= ATTR_INVALID;
	dispcurs->attr |= ATTR_INVALID;

	term->curstype = 0;
    }
    term->dispcursx = term->dispcursy = -1;

    /* The normal screen data */
    for (i = 0; i < term->rows; i++) {
	termline *ldata;
	termchar *lchars;
	bool dirty_line, dirty_run, selected;
	unsigned long attr = 0, cset = 0;
	int start = 0;
	int ccount = 0;
	bool last_run_dirty = false;
	int laststart;
        bool dirtyrect;
	int *backward;
        truecolour tc;

	scrpos.y = i + term->disptop;
	ldata = lineptr(scrpos.y);

	/* Do Arabic shaping and bidi. */
	lchars = term_bidi_line(term, ldata, i);
	if (lchars) {
	    backward = term->post_bidi_cache[i].backward;
	} else {
	    lchars = ldata->chars;
	    backward = NULL;
	}

	/*
	 * First loop: work along the line deciding what we want
	 * each character cell to look like.
	 */
	for (j = 0; j < term->cols; j++) {
	    unsigned long tattr, tchar;
	    termchar *d = lchars + j;
	    scrpos.x = backward ? backward[j] : j;

	    tchar = d->chr;
	    tattr = d->attr;

            if (!term->ansi_colour)
                tattr = (tattr & ~(ATTR_FGMASK | ATTR_BGMASK)) | 
                ATTR_DEFFG | ATTR_DEFBG;

	    if (!term->xterm_256_colour) {
		int colour;
		colour = (tattr & ATTR_FGMASK) >> ATTR_FGSHIFT;
		if (colour >= 16 && colour < 256)
		    tattr = (tattr &~ ATTR_FGMASK) | ATTR_DEFFG;
		colour = (tattr & ATTR_BGMASK) >> ATTR_BGSHIFT;
		if (colour >= 16 && colour < 256)
		    tattr = (tattr &~ ATTR_BGMASK) | ATTR_DEFBG;
	    }

            if (term->true_colour) {
                tc = d->truecolour;
            } else {
                tc.fg = tc.bg = optionalrgb_none;
            }

	    switch (tchar & CSET_MASK) {
	      case CSET_ASCII:
		tchar = term->ucsdata->unitab_line[tchar & 0xFF];
		break;
	      case CSET_LINEDRW:
		tchar = term->ucsdata->unitab_xterm[tchar & 0xFF];
		break;
	      case CSET_SCOACS:  
		tchar = term->ucsdata->unitab_scoacs[tchar&0xFF]; 
		break;
	    }
	    if (j < term->cols-1 && d[1].chr == UCSWIDE)
		tattr |= ATTR_WIDE;
#ifdef MOD_HYPERLINK
 		/*
 		 * HACK: PuttyTray / Nutty
 		 * Hyperlink stuff: Underline link regions if user has configured us so
 		 */
		if( !GetPuttyFlag() && GetHyperlinkFlag() ) {
		if (urlhack_underline) {
			if (j == urlhack_toggle_x && i == urlhack_toggle_y) {
				urlhack_is_link = urlhack_is_link == 1 ? 0 : 1;

				// Find next bound for the toggle
				
				if (urlhack_is_link == 1) {
					urlhack_toggle_x = urlhack_region.x1;
					urlhack_toggle_y = urlhack_region.y1;

					if (urlhack_toggle_x == term->cols - 1) {
						// Handle special case where link ends at the last char of the row
						urlhack_toggle_y++;
						urlhack_toggle_x = 0;
					}
				}
				else {
					urlhack_region = urlhack_get_link_region(++urlhack_region_index);

					if (urlhack_underline_always)
						urlhack_hover_current = 1;
					else
						urlhack_hover_current = urlhack_is_in_this_link_region(urlhack_region, urlhack_mouse_old_x, urlhack_mouse_old_y);

					urlhack_toggle_x = urlhack_region.x0;
					urlhack_toggle_y = urlhack_region.y0;
				}
			}
			if (urlhack_is_link == 1 && urlhack_hover_current == 1) {	
				tattr |= ATTR_UNDER;
			}

			term->url_update = 0;
		}
		}
 		/* HACK: PuttyTray / Nutty : END */
#endif

	    /* Video reversing things */
	    if (term->selstate == DRAGGING || term->selstate == SELECTED) {
		if (term->seltype == LEXICOGRAPHIC)
		    selected = (posle(term->selstart, scrpos) &&
				poslt(scrpos, term->selend));
		else
		    selected = (posPle(term->selstart, scrpos) &&
                                posPle_left(scrpos, term->selend));
	    } else
		selected = false;
#ifdef MOD_TUTTYCOLOR
	    tattr = (tattr ^ rv ^ (selected ? ATTR_SELECTED : 0));
#else
	    tattr = (tattr ^ rv
		     ^ (selected ? ATTR_REVERSE : 0));
#endif

	    /* 'Real' blinking ? */
	    if (term->blink_is_real && (tattr & ATTR_BLINK)) {
		if (term->has_focus && term->tblinker) {
		    tchar = term->ucsdata->unitab_line[(unsigned char)' '];
		}
		tattr &= ~ATTR_BLINK;
	    }

	    /*
	     * Check the font we'll _probably_ be using to see if 
	     * the character is wide when we don't want it to be.
	     */
	    if (tchar != term->disptext[i]->chars[j].chr ||
		tattr != (term->disptext[i]->chars[j].attr &~
			  (ATTR_NARROW | DATTR_MASK))) {
		if ((tattr & ATTR_WIDE) == 0 &&
                    win_char_width(term->win, tchar) == 2)
		    tattr |= ATTR_NARROW;
	    } else if (term->disptext[i]->chars[j].attr & ATTR_NARROW)
		tattr |= ATTR_NARROW;

	    if (i == our_curs_y && j == our_curs_x) {
		tattr |= cursor;
		term->curstype = cursor;
		term->dispcursx = j;
		term->dispcursy = i;
	    }

	    /* FULL-TERMCHAR */
	    newline[j].attr = tattr;
	    newline[j].chr = tchar;
	    newline[j].truecolour = tc;
	    /* Combining characters are still read from lchars */
	    newline[j].cc_next = 0;
	}

	/*
	 * Now loop over the line again, noting where things have
	 * changed.
	 * 
	 * During this loop, we keep track of where we last saw
	 * DATTR_STARTRUN. Any mismatch automatically invalidates
	 * _all_ of the containing run that was last printed: that
	 * is, any rectangle that was drawn in one go in the
	 * previous update should be either left completely alone
	 * or overwritten in its entirety. This, along with the
	 * expectation that front ends clip all text runs to their
	 * bounding rectangle, should solve any possible problems
	 * with fonts that overflow their character cells.
	 */
	laststart = 0;
	dirtyrect = false;
	for (j = 0; j < term->cols; j++) {
	    if (term->disptext[i]->chars[j].attr & DATTR_STARTRUN) {
		laststart = j;
		dirtyrect = false;
	    }

	    if (term->disptext[i]->chars[j].chr != newline[j].chr ||
		(term->disptext[i]->chars[j].attr &~ DATTR_MASK)
		!= newline[j].attr) {
		int k;

		if (!dirtyrect) {
		    for (k = laststart; k < j; k++)
			term->disptext[i]->chars[k].attr |= ATTR_INVALID;

		    dirtyrect = true;
		}
	    }

	    if (dirtyrect)
		term->disptext[i]->chars[j].attr |= ATTR_INVALID;
	}

	/*
	 * Finally, loop once more and actually do the drawing.
	 */
	dirty_run = dirty_line = (ldata->lattr !=
				  term->disptext[i]->lattr);
	term->disptext[i]->lattr = ldata->lattr;

	tc = term->erase_char.truecolour;
	for (j = 0; j < term->cols; j++) {
	    unsigned long tattr, tchar;
	    bool break_run, do_copy;
	    termchar *d = lchars + j;

	    tattr = newline[j].attr;
	    tchar = newline[j].chr;

	    if ((term->disptext[i]->chars[j].attr ^ tattr) & ATTR_WIDE)
		dirty_line = true;

	    break_run = ((tattr ^ attr) & term->attr_mask) != 0;

            if (!truecolour_equal(newline[j].truecolour, tc))
                break_run = true;

#ifdef USES_VTLINE_HACK
	    /* Special hack for VT100 Linedraw glyphs */
	    if ((tchar >= 0x23BA && tchar <= 0x23BD) ||
                (j > 0 && (newline[j-1].chr >= 0x23BA &&
                           newline[j-1].chr <= 0x23BD)))
		break_run = true;
#endif

	    /*
	     * Separate out sequences of characters that have the
	     * same CSET, if that CSET is a magic one.
	     */
	    if (CSET_OF(tchar) != cset)
		break_run = true;

	    /*
	     * Break on both sides of any combined-character cell.
	     */
	    if (d->cc_next != 0 ||
		(j > 0 && d[-1].cc_next != 0))
		break_run = true;

            /*
             * Break on both sides of a trust sigil.
             */
            if (d->chr == TRUST_SIGIL_CHAR ||
                (j >= 2 && d[-1].chr == UCSWIDE &&
                 d[-2].chr == TRUST_SIGIL_CHAR))
                break_run = true;

	    if (!term->ucsdata->dbcs_screenfont && !dirty_line) {
		if (term->disptext[i]->chars[j].chr == tchar &&
		    (term->disptext[i]->chars[j].attr &~ DATTR_MASK) == tattr)
		    break_run = true;
		else if (!dirty_run && ccount == 1)
		    break_run = true;
	    }

	    if (break_run) {
		if ((dirty_run || last_run_dirty) && ccount > 0)
                    do_paint_draw(term, ldata, start, i, ch, ccount, attr, tc);
		start = j;
		ccount = 0;
		attr = tattr;
		tc = newline[j].truecolour;
		cset = CSET_OF(tchar);
		if (term->ucsdata->dbcs_screenfont)
		    last_run_dirty = dirty_run;
		dirty_run = dirty_line;
	    }

	    do_copy = false;
	    if (!termchars_equal_override(&term->disptext[i]->chars[j],
					  d, tchar, tattr)) {
		do_copy = true;
		dirty_run = true;
	    }

            sgrowarrayn(ch, chlen, ccount, 2);

#ifdef PLATFORM_IS_UTF16
	    if (tchar > 0x10000 && tchar < 0x110000) {
		ch[ccount++] = (wchar_t) HIGH_SURROGATE_OF(tchar);
		ch[ccount++] = (wchar_t) LOW_SURROGATE_OF(tchar);
	    } else
#endif /* PLATFORM_IS_UTF16 */
	    ch[ccount++] = (wchar_t) tchar;

	    if (d->cc_next) {
		termchar *dd = d;

		while (dd->cc_next) {
		    unsigned long schar;

		    dd += dd->cc_next;

		    schar = dd->chr;
		    switch (schar & CSET_MASK) {
		      case CSET_ASCII:
			schar = term->ucsdata->unitab_line[schar & 0xFF];
			break;
		      case CSET_LINEDRW:
			schar = term->ucsdata->unitab_xterm[schar & 0xFF];
			break;
		      case CSET_SCOACS:
			schar = term->ucsdata->unitab_scoacs[schar&0xFF];
			break;
		    }

                    sgrowarrayn(ch, chlen, ccount, 2);

#ifdef PLATFORM_IS_UTF16
		    if (schar > 0x10000 && schar < 0x110000) {
			ch[ccount++] = (wchar_t) HIGH_SURROGATE_OF(schar);
			ch[ccount++] = (wchar_t) LOW_SURROGATE_OF(schar);
		    } else
#endif /* PLATFORM_IS_UTF16 */
		    ch[ccount++] = (wchar_t) schar;
		}

		attr |= TATTR_COMBINING;
	    }

	    if (do_copy) {
		copy_termchar(term->disptext[i], j, d);
		term->disptext[i]->chars[j].chr = tchar;
		term->disptext[i]->chars[j].attr = tattr;
		term->disptext[i]->chars[j].truecolour = tc;
		if (start == j)
		    term->disptext[i]->chars[j].attr |= DATTR_STARTRUN;
	    }

	    /* If it's a wide char step along to the next one. */
	    if (tattr & ATTR_WIDE) {
		if (++j < term->cols) {
		    d++;
		    /*
		     * By construction above, the cursor should not
		     * be on the right-hand half of this character.
		     * Ever.
		     */
		    assert(!(i == our_curs_y && j == our_curs_x));
		    if (!termchars_equal(&term->disptext[i]->chars[j], d))
			dirty_run = true;
		    copy_termchar(term->disptext[i], j, d);
		}
	    }
	}
	if (dirty_run && ccount > 0)
            do_paint_draw(term, ldata, start, i, ch, ccount, attr, tc);

	unlineptr(ldata);
    }

    sfree(newline);
    sfree(ch);
}

/*
 * Invalidate the whole screen so it will be repainted in full.
 */
void term_invalidate(Terminal *term)
{
    int i, j;

    for (i = 0; i < term->rows; i++)
	for (j = 0; j < term->cols; j++)
	    term->disptext[i]->chars[j].attr |= ATTR_INVALID;

    term_schedule_update(term);
}

/*
 * Paint the window in response to a WM_PAINT message.
 */
void term_paint(Terminal *term,
		int left, int top, int right, int bottom, bool immediately)
{
    int i, j;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right >= term->cols) right = term->cols-1;
    if (bottom >= term->rows) bottom = term->rows-1;

    for (i = top; i <= bottom && i < term->rows; i++) {
	if ((term->disptext[i]->lattr & LATTR_MODE) == LATTR_NORM)
	    for (j = left; j <= right && j < term->cols; j++)
		term->disptext[i]->chars[j].attr |= ATTR_INVALID;
	else
	    for (j = left / 2; j <= right / 2 + 1 && j < term->cols; j++)
		term->disptext[i]->chars[j].attr |= ATTR_INVALID;
    }

    if (immediately) {
        do_paint(term);
    } else {
	term_schedule_update(term);
    }
}

/*
 * Attempt to scroll the scrollback. The second parameter gives the
 * position we want to scroll to; the first is +1 to denote that
 * this position is relative to the beginning of the scrollback, -1
 * to denote it is relative to the end, and 0 to denote that it is
 * relative to the current position.
 */
void term_scroll(Terminal *term, int rel, int where)
{
    int sbtop = -sblines(term);
#ifdef MOD_HYPERLINK
	/*
	 * HACK: PuttyTray / Nutty
	 */
		if( !GetPuttyFlag() && GetHyperlinkFlag() ) term->url_update = TRUE;	
#endif

    term->disptop = (rel < 0 ? 0 : rel > 0 ? sbtop : term->disptop) + where;
    if (term->disptop < sbtop)
	term->disptop = sbtop;
    if (term->disptop > 0)
	term->disptop = 0;
    term->win_scrollbar_update_pending = true;
    term_schedule_update(term);
}

/*
 * Scroll the scrollback to centre it on the beginning or end of the
 * current selection, if any.
 */
void term_scroll_to_selection(Terminal *term, int which_end)
{
    pos target;
    int y;
    int sbtop = -sblines(term);

    if (term->selstate != SELECTED)
	return;
    if (which_end)
	target = term->selend;
    else
	target = term->selstart;

    y = target.y - term->rows/2;
    if (y < sbtop)
	y = sbtop;
    else if (y > 0)
	y = 0;
    term_scroll(term, -1, y);
}

/*
 * Helper routine for clipme(): growing buffer.
 */
typedef struct {
    size_t bufsize;         /* amount of allocated space in textbuf/attrbuf */
    size_t bufpos;          /* amount of actual data */
    wchar_t *textbuf;	    /* buffer for copied text */
    wchar_t *textptr;	    /* = textbuf + bufpos (current insertion point) */
    int *attrbuf;	    /* buffer for copied attributes */
    int *attrptr;	    /* = attrbuf + bufpos */
    truecolour *tcbuf;	    /* buffer for copied colours */
    truecolour *tcptr;	    /* = tcbuf + bufpos */
} clip_workbuf;

static void clip_addchar(clip_workbuf *b, wchar_t chr, int attr, truecolour tc)
{
    if (b->bufpos >= b->bufsize) {
        sgrowarray(b->textbuf, b->bufsize, b->bufpos);
	b->textptr = b->textbuf + b->bufpos;
	b->attrbuf = sresize(b->attrbuf, b->bufsize, int);
	b->attrptr = b->attrbuf + b->bufpos;
	b->tcbuf = sresize(b->tcbuf, b->bufsize, truecolour);
	b->tcptr = b->tcbuf + b->bufpos;
    }
    *b->textptr++ = chr;
    *b->attrptr++ = attr;
    *b->tcptr++ = tc;
    b->bufpos++;
}

#ifdef MOD_HYPERLINK
static void clipme(Terminal *term, pos top, pos bottom, bool rect, bool desel,
	const int *clipboards, int n_clipboards ,
	void (*output)(TermWin *, int,  wchar_t *, int *, truecolour *, int,  bool))
#else
static void clipme(Terminal *term, pos top, pos bottom, bool rect, bool desel,
                   const int *clipboards, int n_clipboards)
#endif
{
    clip_workbuf buf;
    int old_top_x;
    int attr;
    truecolour tc;

    buf.bufsize = 5120;
    buf.bufpos = 0;
    buf.textptr = buf.textbuf = snewn(buf.bufsize, wchar_t);
    buf.attrptr = buf.attrbuf = snewn(buf.bufsize, int);
    buf.tcptr = buf.tcbuf = snewn(buf.bufsize, truecolour);

    old_top_x = top.x;		       /* needed for rect==1 */

    while (poslt(top, bottom)) {
	bool nl = false;
	termline *ldata = lineptr(top.y);
	pos nlpos;

	/*
	 * nlpos will point at the maximum position on this line we
	 * should copy up to. So we start it at the end of the
	 * line...
	 */
	nlpos.y = top.y;
	nlpos.x = term->cols;

	/*
	 * ... move it backwards if there's unused space at the end
	 * of the line (and also set `nl' if this is the case,
	 * because in normal selection mode this means we need a
	 * newline at the end)...
	 */
	if (!(ldata->lattr & LATTR_WRAPPED)) {
	    while (nlpos.x &&
		   IS_SPACE_CHR(ldata->chars[nlpos.x - 1].chr) &&
		   !ldata->chars[nlpos.x - 1].cc_next &&
		   poslt(top, nlpos))
		decpos(nlpos);
	    if (poslt(nlpos, bottom))
		nl = true;
	} else {
            if (ldata->trusted) {
                /* A wrapped line with a trust sigil on it terminates
                 * a few characters earlier. */
                nlpos.x = (nlpos.x < TRUST_SIGIL_WIDTH ? 0 :
                           nlpos.x - TRUST_SIGIL_WIDTH);
            }
            if (ldata->lattr & LATTR_WRAPPED2) {
                /* Ignore the last char on the line in a WRAPPED2 line. */
                decpos(nlpos);
            }
	}

	/*
	 * ... and then clip it to the terminal x coordinate if
	 * we're doing rectangular selection. (In this case we
	 * still did the above, so that copying e.g. the right-hand
	 * column from a table doesn't fill with spaces on the
	 * right.)
	 */
	if (rect) {
	    if (nlpos.x > bottom.x)
		nlpos.x = bottom.x;
	    nl = (top.y < bottom.y);
	}

	while (poslt(top, bottom) && poslt(top, nlpos)) {
#if 0
	    char cbuf[16], *p;
	    sprintf(cbuf, "<U+%04x>", (ldata[top.x] & 0xFFFF));
#else
	    wchar_t cbuf[16], *p;
	    int c;
	    int x = top.x;

	    if (ldata->chars[x].chr == UCSWIDE) {
		top.x++;
		continue;
	    }

	    while (1) {
		int uc = ldata->chars[x].chr;
                attr = ldata->chars[x].attr;
		tc = ldata->chars[x].truecolour;

		switch (uc & CSET_MASK) {
		  case CSET_LINEDRW:
		    if (!term->rawcnp) {
			uc = term->ucsdata->unitab_xterm[uc & 0xFF];
			break;
		    }
		  case CSET_ASCII:
		    uc = term->ucsdata->unitab_line[uc & 0xFF];
		    break;
		  case CSET_SCOACS:
		    uc = term->ucsdata->unitab_scoacs[uc&0xFF];
		    break;
		}
		switch (uc & CSET_MASK) {
		  case CSET_ACP:
		    uc = term->ucsdata->unitab_font[uc & 0xFF];
		    break;
		  case CSET_OEMCP:
		    uc = term->ucsdata->unitab_oemcp[uc & 0xFF];
		    break;
		}

		c = (uc & ~CSET_MASK);
#ifdef PLATFORM_IS_UTF16
		if (uc > 0x10000 && uc < 0x110000) {
		    cbuf[0] = 0xD800 | ((uc - 0x10000) >> 10);
		    cbuf[1] = 0xDC00 | ((uc - 0x10000) & 0x3FF);
		    cbuf[2] = 0;
		} else
#endif
		{
		    cbuf[0] = uc;
		    cbuf[1] = 0;
		}

		if (DIRECT_FONT(uc)) {
		    if (c >= ' ' && c != 0x7F) {
			char buf[4];
			WCHAR wbuf[4];
			int rv;
			if (is_dbcs_leadbyte(term->ucsdata->font_codepage, (BYTE) c)) {
			    buf[0] = c;
			    buf[1] = (char) (0xFF & ldata->chars[top.x + 1].chr);
			    rv = mb_to_wc(term->ucsdata->font_codepage, 0, buf, 2, wbuf, 4);
			    top.x++;
			} else {
			    buf[0] = c;
			    rv = mb_to_wc(term->ucsdata->font_codepage, 0, buf, 1, wbuf, 4);
			}

			if (rv > 0) {
			    memcpy(cbuf, wbuf, rv * sizeof(wchar_t));
			    cbuf[rv] = 0;
			}
		    }
		}
#endif

		for (p = cbuf; *p; p++)
		    clip_addchar(&buf, *p, attr, tc);

		if (ldata->chars[x].cc_next)
		    x += ldata->chars[x].cc_next;
		else
		    break;
	    }
	    top.x++;
	}
	if (nl) {
	    int i;
	    for (i = 0; i < sel_nl_sz; i++)
		clip_addchar(&buf, sel_nl[i], 0, term->basic_erase_char.truecolour);
	}
	top.y++;
	top.x = rect ? old_top_x : 0;

	unlineptr(ldata);
    }
#if SELECTION_NUL_TERMINATED
    clip_addchar(&buf, 0, 0, term->basic_erase_char.truecolour);
#endif
    /* Finally, transfer all that to the clipboard(s). */
    {
        int i;
        bool clip_local = false;
        for (i = 0; i < n_clipboards; i++) {
            if (clipboards[i] == CLIP_LOCAL) {
                clip_local = true;
            } else if (clipboards[i] != CLIP_NULL) {
                win_clip_write(
                    term->win, clipboards[i], buf.textbuf, buf.attrbuf,
                    buf.tcbuf, buf.bufpos, desel);
            }
        }
        if (clip_local) {
            sfree(term->last_selected_text);
            sfree(term->last_selected_attr);
            sfree(term->last_selected_tc);
            term->last_selected_text = buf.textbuf;
            term->last_selected_attr = buf.attrbuf;
            term->last_selected_tc = buf.tcbuf;
            term->last_selected_len = buf.bufpos;
        } else {
            sfree(buf.textbuf);
            sfree(buf.attrbuf);
            sfree(buf.tcbuf);
        }
    }
}

void term_copyall(Terminal *term, const int *clipboards, int n_clipboards)
{
    pos top;
    pos bottom;
    tree234 *screen = term->screen;
    top.y = -sblines(term);
    top.x = 0;
    bottom.y = find_last_nonempty_line(term, screen);
    bottom.x = term->cols;
#ifdef MOD_HYPERLINK
    clipme(term, top, bottom, false, true, clipboards, n_clipboards, wintw_clip_write);
#else
    clipme(term, top, bottom, false, true, clipboards, n_clipboards);
#endif
}

static void paste_from_clip_local(void *vterm)
{
    Terminal *term = (Terminal *)vterm;
    term_do_paste(term, term->last_selected_text, term->last_selected_len);
}

void term_request_copy(Terminal *term, const int *clipboards, int n_clipboards)
{
    int i;
    for (i = 0; i < n_clipboards; i++) {
        assert(clipboards[i] != CLIP_LOCAL);
        if (clipboards[i] != CLIP_NULL) {
            win_clip_write(term->win, clipboards[i],
                           term->last_selected_text, term->last_selected_attr,
                           term->last_selected_tc, term->last_selected_len,
                           false);
        }
    }
}

void term_request_paste(Terminal *term, int clipboard)
{
    switch (clipboard) {
      case CLIP_NULL:
        /* Do nothing: CLIP_NULL never has data in it. */
        break;
      case CLIP_LOCAL:
        queue_toplevel_callback(paste_from_clip_local, term);
        break;
      default:
        win_clip_request_paste(term->win, clipboard);
        break;
    }
}

/*
 * The wordness array is mainly for deciding the disposition of the
 * US-ASCII characters.
 */
static int wordtype(Terminal *term, int uc)
{
    struct ucsword {
	int start, end, ctype;
    };
    static const struct ucsword ucs_words[] = {
	{
	128, 160, 0}, {
	161, 191, 1}, {
	215, 215, 1}, {
	247, 247, 1}, {
	0x037e, 0x037e, 1},	       /* Greek question mark */
	{
	0x0387, 0x0387, 1},	       /* Greek ano teleia */
	{
	0x055a, 0x055f, 1},	       /* Armenian punctuation */
	{
	0x0589, 0x0589, 1},	       /* Armenian full stop */
	{
	0x0700, 0x070d, 1},	       /* Syriac punctuation */
	{
	0x104a, 0x104f, 1},	       /* Myanmar punctuation */
	{
	0x10fb, 0x10fb, 1},	       /* Georgian punctuation */
	{
	0x1361, 0x1368, 1},	       /* Ethiopic punctuation */
	{
	0x166d, 0x166e, 1},	       /* Canadian Syl. punctuation */
	{
	0x17d4, 0x17dc, 1},	       /* Khmer punctuation */
	{
	0x1800, 0x180a, 1},	       /* Mongolian punctuation */
	{
	0x2000, 0x200a, 0},	       /* Various spaces */
	{
	0x2070, 0x207f, 2},	       /* superscript */
	{
	0x2080, 0x208f, 2},	       /* subscript */
	{
	0x200b, 0x27ff, 1},	       /* punctuation and symbols */
	{
	0x3000, 0x3000, 0},	       /* ideographic space */
	{
	0x3001, 0x3020, 1},	       /* ideographic punctuation */
	{
	0x303f, 0x309f, 3},	       /* Hiragana */
	{
	0x30a0, 0x30ff, 3},	       /* Katakana */
	{
	0x3300, 0x9fff, 3},	       /* CJK Ideographs */
	{
	0xac00, 0xd7a3, 3},	       /* Hangul Syllables */
	{
	0xf900, 0xfaff, 3},	       /* CJK Ideographs */
	{
	0xfe30, 0xfe6b, 1},	       /* punctuation forms */
	{
	0xff00, 0xff0f, 1},	       /* half/fullwidth ASCII */
	{
	0xff1a, 0xff20, 1},	       /* half/fullwidth ASCII */
	{
	0xff3b, 0xff40, 1},	       /* half/fullwidth ASCII */
	{
	0xff5b, 0xff64, 1},	       /* half/fullwidth ASCII */
	{
	0xfff0, 0xffff, 0},	       /* half/fullwidth ASCII */
	{
	0, 0, 0}
    };
    const struct ucsword *wptr;

    switch (uc & CSET_MASK) {
      case CSET_LINEDRW:
	uc = term->ucsdata->unitab_xterm[uc & 0xFF];
	break;
      case CSET_ASCII:
	uc = term->ucsdata->unitab_line[uc & 0xFF];
	break;
      case CSET_SCOACS:  
	uc = term->ucsdata->unitab_scoacs[uc&0xFF]; 
	break;
    }
    switch (uc & CSET_MASK) {
      case CSET_ACP:
	uc = term->ucsdata->unitab_font[uc & 0xFF];
	break;
      case CSET_OEMCP:
	uc = term->ucsdata->unitab_oemcp[uc & 0xFF];
	break;
    }

    /* For DBCS fonts I can't do anything useful. Even this will sometimes
     * fail as there's such a thing as a double width space. :-(
     */
    if (term->ucsdata->dbcs_screenfont &&
	term->ucsdata->font_codepage == term->ucsdata->line_codepage)
	return (uc != ' ');

    if (uc < 0x80)
	return term->wordness[uc];

    for (wptr = ucs_words; wptr->start; wptr++) {
	if (uc >= wptr->start && uc <= wptr->end)
	    return wptr->ctype;
    }

    return 2;
}

static int line_cols(Terminal *term, termline *ldata)
{
    int cols = term->cols;
    if (ldata->trusted) {
        cols -= TRUST_SIGIL_WIDTH;
    }
    if (ldata->lattr & LATTR_WRAPPED2)
        cols--;
    if (cols < 0)
        cols = 0;
    return cols;
}

/*
 * Spread the selection outwards according to the selection mode.
 */
static pos sel_spread_half(Terminal *term, pos p, int dir)
{
    termline *ldata;
    short wvalue;
    int topy = -sblines(term);

    ldata = lineptr(p.y);

    switch (term->selmode) {
      case SM_CHAR:
	/*
	 * In this mode, every character is a separate unit, except
	 * for runs of spaces at the end of a non-wrapping line.
	 */
	if (!(ldata->lattr & LATTR_WRAPPED)) {
	    termchar *q = ldata->chars + line_cols(term, ldata);
	    while (q > ldata->chars &&
		   IS_SPACE_CHR(q[-1].chr) && !q[-1].cc_next)
		q--;
	    if (q == ldata->chars + term->cols)
		q--;
	    if (p.x >= q - ldata->chars)
		p.x = (dir == -1 ? q - ldata->chars : term->cols - 1);
	}
	break;
      case SM_WORD:
	/*
	 * In this mode, the units are maximal runs of characters
	 * whose `wordness' has the same value.
	 */
	wvalue = wordtype(term, UCSGET(ldata->chars, p.x));
	if (dir == +1) {
	    while (1) {
		int maxcols = line_cols(term, ldata);
		if (p.x < maxcols-1) {
		    if (wordtype(term, UCSGET(ldata->chars, p.x+1)) == wvalue)
			p.x++;
		    else
			break;
		} else {
		    if (p.y+1 < term->rows && 
                        (ldata->lattr & LATTR_WRAPPED)) {
			termline *ldata2;
			ldata2 = lineptr(p.y+1);
			if (wordtype(term, UCSGET(ldata2->chars, 0))
			    == wvalue) {
			    p.x = 0;
			    p.y++;
			    unlineptr(ldata);
			    ldata = ldata2;
			} else {
			    unlineptr(ldata2);
			    break;
			}
		    } else
			break;
		}
	    }
	} else {
	    while (1) {
		if (p.x > 0) {
		    if (wordtype(term, UCSGET(ldata->chars, p.x-1)) == wvalue)
			p.x--;
		    else
			break;
		} else {
		    termline *ldata2;
		    int maxcols;
		    if (p.y <= topy)
			break;
		    ldata2 = lineptr(p.y-1);
		    maxcols = line_cols(term, ldata2);
		    if (ldata2->lattr & LATTR_WRAPPED) {
			if (wordtype(term, UCSGET(ldata2->chars, maxcols-1))
			    == wvalue) {
			    p.x = maxcols-1;
			    p.y--;
			    unlineptr(ldata);
			    ldata = ldata2;
			} else {
			    unlineptr(ldata2);
			    break;
			}
		    } else
			break;
		}
	    }
	}
	break;
      case SM_LINE:
	/*
	 * In this mode, every line is a unit.
	 */
	p.x = (dir == -1 ? 0 : term->cols - 1);
	break;
    }

    unlineptr(ldata);
    return p;
}

static void sel_spread(Terminal *term)
{
    if (term->seltype == LEXICOGRAPHIC) {
	term->selstart = sel_spread_half(term, term->selstart, -1);
	decpos(term->selend);
	term->selend = sel_spread_half(term, term->selend, +1);
	incpos(term->selend);
    }
}

static void term_paste_callback(void *vterm)
{
    Terminal *term = (Terminal *)vterm;

    if (term->paste_len == 0)
	return;

    while (term->paste_pos < term->paste_len) {
	int n = 0;
	while (n + term->paste_pos < term->paste_len) {
	    if (term->paste_buffer[term->paste_pos + n++] == '\015')
		break;
	}
	if (term->ldisc) {
            strbuf *buf = term_input_data_from_unicode(
                term, term->paste_buffer + term->paste_pos, n);
            term_keyinput_internal(term, buf->s, buf->len, false);
            strbuf_free(buf);
        }
	term->paste_pos += n;

	if (term->paste_pos < term->paste_len) {
            queue_toplevel_callback(term_paste_callback, term);
	    return;
	}
    }
    term_bracketed_paste_stop(term);
    sfree(term->paste_buffer);
    term->paste_buffer = NULL;
    term->paste_len = 0;
}

/*
 * Specialist string compare function. Returns true if the buffer of
 * alen wide characters starting at a has as a prefix the buffer of
 * blen characters starting at b.
 */
static bool wstartswith(const wchar_t *a, size_t alen,
                        const wchar_t *b, size_t blen)
{
    return alen >= blen && !wcsncmp(a, b, blen);
}

void term_do_paste(Terminal *term, const wchar_t *data, int len)
{
    const wchar_t *p;
    bool paste_controls = conf_get_bool(term->conf, CONF_paste_controls);

    /*
     * Pasting data into the terminal counts as a keyboard event (for
     * purposes of the 'Reset scrollback on keypress' config option),
     * unless the paste is zero-length.
     */
    if (len == 0)
        return;
    term_seen_key_event(term);

    if (term->paste_buffer)
        sfree(term->paste_buffer);
    term->paste_pos = term->paste_len = 0;
    term->paste_buffer = snewn(len + 12, wchar_t);

    if (term->bracketed_paste)
        term_bracketed_paste_start(term);

    p = data;
    while (p < data + len) {
        wchar_t wc = *p++;

        if (wc == sel_nl[0] &&
            wstartswith(p-1, data+len-(p-1), sel_nl, sel_nl_sz)) {
            /*
             * This is the (platform-dependent) sequence that the host
             * OS uses to represent newlines in clipboard data.
             * Normalise it to a press of CR.
             */
            p += sel_nl_sz - 1;
            wc = '\015';
        }

        if ((wc & ~(wint_t)0x9F) == 0) {
            /*
             * This is a control code, either in the range 0x00-0x1F
             * or 0x80-0x9F. We reject all of these in pastecontrols
             * mode, except for a small set of permitted ones.
             */
            if (!paste_controls) {
                /* In line with xterm 292, accepted control chars are:
                 * CR, LF, tab, backspace. (And DEL, i.e. 0x7F, but
                 * that's permitted by virtue of not matching the bit
                 * mask that got us into this if statement, so we
                 * don't have to permit it here. */
                static const unsigned mask =
                    (1<<13) | (1<<10) | (1<<9) | (1<<8);

                if (wc > 15 || !((mask >> wc) & 1))
                    continue;
            }

            if (wc == '\033' && term->bracketed_paste &&
                wstartswith(p-1, data+len-(p-1), L"\033[201~", 6)) {
                /*
                 * Also, in bracketed-paste mode, reject the ESC
                 * character that begins the end-of-paste sequence.
                 */
                continue;
            }
        }

        term->paste_buffer[term->paste_len++] = wc;
    }

    /* Assume a small paste will be OK in one go. */
    if (term->paste_len < 256) {
        if (term->ldisc) {
            strbuf *buf = term_input_data_from_unicode(
                term, term->paste_buffer, term->paste_len);
            term_keyinput_internal(term, buf->s, buf->len, false);
            strbuf_free(buf);
        }
        if (term->paste_buffer)
            sfree(term->paste_buffer);
        term_bracketed_paste_stop(term);
        term->paste_buffer = NULL;
        term->paste_pos = term->paste_len = 0;
    }

    queue_toplevel_callback(term_paste_callback, term);
}

void term_mouse(Terminal *term, Mouse_Button braw, Mouse_Button bcooked,
		Mouse_Action a, int x, int y, bool shift, bool ctrl, bool alt)
{
    pos selpoint;
    termline *ldata;
#ifdef MOD_PERSO
    bool raw_mouse = (term->xterm_mouse_mode &&
#else
    bool raw_mouse = (term->xterm_mouse &&
#endif
                      !term->no_mouse_rep &&
                      !(term->mouse_override && shift));
    int default_seltype;

    if (y < 0) {
	y = 0;
	if (a == MA_DRAG && !raw_mouse)
	    term_scroll(term, 0, -1);
    }
    if (y >= term->rows) {
	y = term->rows - 1;
	if (a == MA_DRAG && !raw_mouse)
	    term_scroll(term, 0, +1);
    }
    if (x < 0) {
	if (y > 0 && !raw_mouse && term->seltype != RECTANGULAR) {
            /*
             * When we're using the mouse for normal raster-based
             * selection, dragging off the left edge of a terminal row
             * is treated the same as the right-hand end of the
             * previous row, in that it's considered to identify a
             * point _before_ the first character on row y.
             *
             * But if the mouse action is going to be used for
             * anything else - rectangular selection, or xterm mouse
             * tracking - then we disable this special treatment.
             */
	    x = term->cols - 1;
	    y--;
	} else
	    x = 0;
    }
    if (x >= term->cols)
	x = term->cols - 1;

    selpoint.y = y + term->disptop;
    ldata = lineptr(selpoint.y);

    if ((ldata->lattr & LATTR_MODE) != LATTR_NORM)
	x /= 2;

    /*
     * Transform x through the bidi algorithm to find the _logical_
     * click point from the physical one.
     */
    if (term_bidi_line(term, ldata, y) != NULL) {
	x = term->post_bidi_cache[y].backward[x];
    }

    selpoint.x = x;
#ifdef MOD_HYPERLINK
    if( !GetPuttyFlag() && GetHyperlinkFlag() ) unlineptr(ldata);
#else
    unlineptr(ldata);
#endif

    /*
     * If we're in the middle of a selection operation, we ignore raw
     * mouse mode until it's done (we must have been not in raw mouse
     * mode when it started).
     * This makes use of Shift for selection reliable, and avoids the
     * host seeing mouse releases for which they never saw corresponding
     * presses.
     */
    if (raw_mouse &&
	(term->selstate != ABOUT_TO) && (term->selstate != DRAGGING)) {
	int encstate = 0, r, c;
        bool wheel;
#ifdef MOD_PERSO
        char abuf[64];
        char m; /* SGR 1006's postfix character ('M' or 'm') */
        int l = 0;
#else
	char abuf[32];
	int len = 0;
#endif

	if (term->ldisc) {

	    switch (braw) {
	      case MBT_LEFT:
		encstate = 0x00;	       /* left button down */
                wheel = false;
		break;
	      case MBT_MIDDLE:
		encstate = 0x01;
                wheel = false;
		break;
	      case MBT_RIGHT:
		encstate = 0x02;
                wheel = false;
		break;
	      case MBT_WHEEL_UP:
		encstate = 0x40;
                wheel = true;
		break;
	      case MBT_WHEEL_DOWN:
		encstate = 0x41;
                wheel = true;
		break;
#ifdef MOD_PERSO
	      case MBT_NOTHING:        /* for any event tracking */
                encstate = 0x03;
                wheel = false;
                break;
#endif
	      default:
                return;
	    }
            if (wheel) {
                /* For mouse wheel buttons, we only ever expect to see
                 * MA_CLICK actions, and we don't try to keep track of
                 * the buttons being 'pressed' (since without matching
                 * click/release pairs that's pointless). */
                if (a != MA_CLICK)
                    return;
            } else switch (a) {
	      case MA_DRAG:
#ifdef MOD_HYPERLINK
	      	if( !GetPuttyFlag() && GetHyperlinkFlag() ) {
				if (term->xterm_mouse_mode == MM_NORMAL) {// HACK: ADDED FOR hyperlink stuff
			unlineptr(ldata); 
			return;
			}
		}
	        else {
		if (term->xterm_mouse_mode == MM_NORMAL)
		    return;
		}
#else
#ifdef MOD_PERSO
		if (term->xterm_mouse_mode == MM_NORMAL)
#else
		if (term->xterm_mouse == 1)
#endif
		    return;
#endif
		encstate += 0x20;
		break;
#ifdef MOD_PERSO
              case MA_MOVE:
                if (term->xterm_mouse_mode != MM_ANY_EVENT)
                    return;
                encstate += 0x20;      /* add motion indicator */
                break;
#endif
	      case MA_RELEASE:
		/* If multiple extensions are enabled, the xterm 1006 is used, so it's okay to check for only that */
#ifdef MOD_PERSO
		if (term->xterm_mouse_protocol != MP_SGR)
#else
		if (!term->xterm_extended_mouse)
#endif
		    encstate = 0x03;
		term->mouse_is_down = 0;
		break;
	      case MA_CLICK:
#ifdef MOD_HYPERLINK
	        if( !GetPuttyFlag() && GetHyperlinkFlag() ) {
#ifdef MOD_PUTTYX
		if (term->mouse_is_down == braw && braw != MBT_WHEEL_UP && braw != MBT_WHEEL_DOWN) {// HACK: ADDED FOR hyperlink stuff  // MORE HACKING (@unphased: allow sequences of mouse wheel up and mouse wheel down to pass through)
#else
		if (term->mouse_is_down == braw) {// HACK: ADDED FOR hyperlink stuff
#endif
			unlineptr(ldata); 
			return;
			}	      
		}
		else {
#ifdef MOD_PUTTYX
		if (term->mouse_is_down == braw && braw != MBT_WHEEL_UP && braw != MBT_WHEEL_DOWN) // MORE HACKING (@unphased: allow sequences of mouse wheel up and mouse wheel down to pass through)
#else
	        if (term->mouse_is_down == braw)
#endif
		    return;
		}
#else
#ifdef MOD_PUTTYX
		if (term->mouse_is_down == braw && braw != MBT_WHEEL_UP && braw != MBT_WHEEL_DOWN) // MORE HACKING (@unphased: allow sequences of mouse wheel up and mouse wheel down to pass through)
#else
		if (term->mouse_is_down == braw)
#endif
		    return;
#endif
		term->mouse_is_down = braw;
		break;
              default:
                return;
	    }
	    if (shift)
		encstate += 0x04;
	    if (ctrl)
		encstate += 0x10;

#ifdef MOD_PERSO
             switch (term->xterm_mouse_protocol) {

              case MP_NORMAL:
               /* add safety to avoid sending garbage sequences */
                if (x < 223 && y < 223) {
                  encstate += 0x20;

                  r = y + 33;
                  c = x + 33;
                  sprintf(abuf, "\033[M%c%c%c", encstate, c, r);
                  ldisc_send(term->ldisc, abuf, 6, 0);
                }
                break;
             case MP_URXVT:
                encstate += 0x20;
                r = y + 1;
                c = x + 1;
                sprintf(abuf, "\033[%d;%d;%dM", encstate, c, r);
                l = strlen(abuf);
                ldisc_send(term->ldisc, abuf, l, 0);
                break;
              case MP_SGR:
                r = y + 1;
                c = x + 1;
                m = a == MA_RELEASE ? 'm': 'M';
                sprintf(abuf, "\033[<%d;%d;%d%c", encstate, c, r, m);
                l = strlen(abuf);
                ldisc_send(term->ldisc, abuf, l, 0);
                // debug("SGR: %s\n", abuf);  // recompile with -DDEBUG
                break;
              case MP_XTERM:
                /* add safety to avoid sending garbage sequences */
                if (x < 2015 && y < 2015) {
                  encstate += 0x20;
                  wchar_t input[2];
                  wchar_t *inputp = input;
                  int inputlen = 2;
                  input[0] = x + 33;
                  input[1] = y + 33;

                  l = sprintf(abuf, "\033[M%c", encstate);
                  l += charset_from_unicode((const wchar_t **)&inputp, &inputlen,
                                            abuf + l, 4,
                                            CS_UTF8, NULL, NULL, 0);
                  ldisc_send(term->ldisc, abuf, l, 0);
                }
                break;
              default: break;          /* placate gcc warning about enum use */
            }
        }
#else
	    r = y + 1;
	    c = x + 1;

	    /* Check the extensions in decreasing order of preference. Encoding the release event above assumes that 1006 comes first. */
	    if (term->xterm_extended_mouse) {
		len = sprintf(abuf, "\033[<%d;%d;%d%c", encstate, c, r, a == MA_RELEASE ? 'm' : 'M');
	    } else if (term->urxvt_extended_mouse) {
		len = sprintf(abuf, "\033[%d;%d;%dM", encstate + 32, c, r);
	    } else if (c <= 223 && r <= 223) {
		len = sprintf(abuf, "\033[M%c%c%c", encstate + 32, c + 32, r + 32);
	    }
            if (len > 0)
                ldisc_send(term->ldisc, abuf, len, false);
	}
#endif

#ifdef MOD_HYPERLINK
	if( !GetPuttyFlag() && GetHyperlinkFlag() ) {
		unlineptr(ldata); // HACK: ADDED FOR hyperlink stuff
		}
#endif
	return;
    }

    /*
     * Set the selection type (rectangular or normal) at the start
     * of a selection attempt, from the state of Alt.
     */
    if (!alt ^ !term->rect_select)
	default_seltype = RECTANGULAR;
    else
	default_seltype = LEXICOGRAPHIC;
	
    if (term->selstate == NO_SELECTION) {
	term->seltype = default_seltype;
    }

    if (bcooked == MBT_SELECT && a == MA_CLICK) {
	deselect(term);
	term->selstate = ABOUT_TO;
	term->seltype = default_seltype;
	term->selanchor = selpoint;
	term->selmode = SM_CHAR;
#ifdef MOD_HYPERLINK
	/*
	 * HACK: PuttyTray / Nutty
	 * Hyperlink stuff: Check whether the click coordinates are inside link
	 * region, if so -> copy url to temporary buffer and launch it. Delete
	 * the temporary buffer.
	 */
	} else if( !GetPuttyFlag() && GetHyperlinkFlag() && bcooked == MBT_SELECT && a == MA_RELEASE && term->selstate == ABOUT_TO) {
	deselect(term);
	term->selstate = NO_SELECTION;

	if ((!conf_get_int(term->conf, CONF_url_ctrl_click) || (conf_get_int(term->conf, CONF_url_ctrl_click) && urlhack_is_ctrl_pressed())) && urlhack_is_in_link_region(x, y)) {
		int i;
		char *linkbuf = NULL;
		text_region region = urlhack_get_link_bounds(x, y);

		if (region.y0 == region.y1) {
			linkbuf = snewn(region.x1 - region.x0 + 2, char);
			
			for (i = region.x0; i < region.x1; i++) {
				linkbuf[i - region.x0] = (char)(ldata->chars[i].chr);
			}

			linkbuf[i - region.x0] = '\0';
		}
		else {
			termline *urldata = lineptr(region.y0 + term->disptop);
			int linklen, /*pos = region.x0,*/ row = region.y0 + term->disptop;

			linklen = (term->cols - region.x0) +
				((region.y1 - region.y0 - 1) * term->cols) + region.x1 + 1;

			linkbuf = snewn(linklen, char);

			for (i = region.x0; i < linklen + region.x0; i++) {
				linkbuf[i - region.x0] = (char)(urldata->chars[i % term->cols].chr);
				
				// Jump to next line?
				if (((i + 1) % term->cols) == 0) {
					row++;
					urldata = lineptr(row);
				}
			}

			linkbuf[linklen - 1] = '\0';
			unlineptr(urldata);
		}
		
		urlhack_launch_url(!conf_get_int(term->conf, CONF_url_defbrowser) ? conf_get_filename(term->conf, CONF_url_browser)->path : NULL, linkbuf);
		
		sfree(linkbuf);
	}
	/* HACK: PuttyTray / Nutty : END */
#endif
    } else if (bcooked == MBT_SELECT && (a == MA_2CLK || a == MA_3CLK)) {
	deselect(term);
	term->selmode = (a == MA_2CLK ? SM_WORD : SM_LINE);
	term->selstate = DRAGGING;
	term->selstart = term->selanchor = selpoint;
	term->selend = term->selstart;
	incpos(term->selend);
	sel_spread(term);
    } else if ((bcooked == MBT_SELECT && a == MA_DRAG) ||
	       (bcooked == MBT_EXTEND && a != MA_RELEASE)) {
        if (a == MA_DRAG &&
            (term->selstate == NO_SELECTION || term->selstate == SELECTED)) {
            /*
             * This can happen if a front end has passed us a MA_DRAG
             * without a prior MA_CLICK. OS X GTK does so, for
             * example, if the initial button press was eaten by the
             * WM when it activated the window in the first place. The
             * nicest thing to do in this situation is to ignore
             * further drags, and wait for the user to click in the
             * window again properly if they want to select.
             */
            return;
        }
#ifdef MOD_HYPERLINK
	if( !GetPuttyFlag() && GetHyperlinkFlag() ) {
	if (term->selstate == ABOUT_TO && poseq(term->selanchor, selpoint)) { // HACK: ADDED FOR HYPERLINK STUFF
		unlineptr(ldata);
		return;
	}
	}
	else {
	if (term->selstate == ABOUT_TO && poseq(term->selanchor, selpoint))
	    return;
	       }
#else
	if (term->selstate == ABOUT_TO && poseq(term->selanchor, selpoint))
	    return;
#endif
	if (bcooked == MBT_EXTEND && a != MA_DRAG &&
	    term->selstate == SELECTED) {
	    if (term->seltype == LEXICOGRAPHIC) {
		/*
		 * For normal selection, we extend by moving
		 * whichever end of the current selection is closer
		 * to the mouse.
		 */
		if (posdiff(selpoint, term->selstart) <
		    posdiff(term->selend, term->selstart) / 2) {
		    term->selanchor = term->selend;
		    decpos(term->selanchor);
		} else {
		    term->selanchor = term->selstart;
		}
	    } else {
		/*
		 * For rectangular selection, we have a choice of
		 * _four_ places to put selanchor and selpoint: the
		 * four corners of the selection.
		 */
		if (2*selpoint.x < term->selstart.x + term->selend.x)
		    term->selanchor.x = term->selend.x-1;
		else
		    term->selanchor.x = term->selstart.x;

		if (2*selpoint.y < term->selstart.y + term->selend.y)
		    term->selanchor.y = term->selend.y;
		else
		    term->selanchor.y = term->selstart.y;
	    }
	    term->selstate = DRAGGING;
	}
	if (term->selstate != ABOUT_TO && term->selstate != DRAGGING)
	    term->selanchor = selpoint;
	term->selstate = DRAGGING;
	if (term->seltype == LEXICOGRAPHIC) {
	    /*
	     * For normal selection, we set (selstart,selend) to
	     * (selpoint,selanchor) in some order.
	     */
	    if (poslt(selpoint, term->selanchor)) {
		term->selstart = selpoint;
		term->selend = term->selanchor;
		incpos(term->selend);
	    } else {
		term->selstart = term->selanchor;
		term->selend = selpoint;
		incpos(term->selend);
	    }
	} else {
	    /*
	     * For rectangular selection, we may need to
	     * interchange x and y coordinates (if the user has
	     * dragged in the -x and +y directions, or vice versa).
	     */
	    term->selstart.x = min(term->selanchor.x, selpoint.x);
	    term->selend.x = 1+max(term->selanchor.x, selpoint.x);
	    term->selstart.y = min(term->selanchor.y, selpoint.y);
	    term->selend.y =   max(term->selanchor.y, selpoint.y);
	}
	sel_spread(term);
    } else if ((bcooked == MBT_SELECT || bcooked == MBT_EXTEND) &&
	       a == MA_RELEASE) {
	if (term->selstate == DRAGGING) {
	    /*
	     * We've completed a selection. We now transfer the
	     * data to the clipboard.
	     */
	    clipme(term, term->selstart, term->selend,
		   (term->seltype == RECTANGULAR), false,
                   term->mouse_select_clipboards,
#ifdef MOD_HYPERLINK
                   term->n_mouse_select_clipboards,wintw_clip_write);
#else
                   term->n_mouse_select_clipboards);
#endif
	    term->selstate = SELECTED;
	} else
	    term->selstate = NO_SELECTION;
    } else if (bcooked == MBT_PASTE
	       && (a == MA_CLICK
#if MULTICLICK_ONLY_EVENT
		   || a == MA_2CLK || a == MA_3CLK
#endif
		   )) {
	term_request_paste(term, term->mouse_paste_clipboard);
    }

    /*
     * Since terminal output is suppressed during drag-selects, we
     * should make sure to write any pending output if one has just
     * finished.
     */
    if (term->selstate != DRAGGING)
        term_out(term);
    term_schedule_update(term);
}

#ifdef MOD_KEYMAPPING
int get_xterm_modifier(bool shift, bool ctrl, bool alt)
{
    int xterm_modifier = 1;  // no modifier key was pressed
    if (shift)
        xterm_modifier += 1;
    if (alt)
        xterm_modifier += 2;
    if (ctrl)
        xterm_modifier += 4;
    return xterm_modifier;
}
int format_arrow_key(char *buf, Terminal *term, int xkey, int modifier, bool alt)
#else
int format_arrow_key(char *buf, Terminal *term, int xkey, bool ctrl)
#endif
{
    char *p = buf;

    if (term->vt52_mode)
	p += sprintf(p, "\x1B%c", xkey);
    else {
	bool app_flg = (term->app_cursor_keys && !term->no_applic_c);
#if 0
	/*
	 * RDB: VT100 & VT102 manuals both state the app cursor
	 * keys only work if the app keypad is on.
	 *
	 * SGT: That may well be true, but xterm disagrees and so
	 * does at least one application, so I've #if'ed this out
	 * and the behaviour is back to PuTTY's original: app
	 * cursor and app keypad are independently switchable
	 * modes. If anyone complains about _this_ I'll have to
	 * put in a configurable option.
	 */
	if (!term->app_keypad_keys)
	    app_flg = 0;
#endif
#ifdef MOD_KEYMAPPING
	if( !GetPuttyFlag() ) {
               if (modifier == 3 && alt == 1)
                       p += sprintf((char *) p, "\x1B[1;8%c", xkey); /*Control+Alt-Shift */
               else if (modifier == 2 && alt == 1)
                       p += sprintf((char *) p, "\x1B[1;7%c", xkey); /* Control+Alt */
               else if (modifier == 3)
                       p += sprintf((char *) p, "\x1B[1;6%c", xkey); /* Control+Shift*/
               else if (modifier == 2)
                       p += sprintf((char *) p, "\x1B[1;5%c", xkey); /* Control */
               else if (modifier == 1 && alt == 1)
                       p += sprintf((char *) p, "\x1B[1;4%c", xkey); /* Alt+Shift */
		else if (alt == 1)
                       p += sprintf((char *) p, "\x1B[1;3%c", xkey); /* Alt */
		else if (modifier == 1)
                       p += sprintf((char *) p, "\x1B[1;2%c", xkey); /* Shift */
		else if (app_flg)
                       p += sprintf((char *) p, "\x1BO%c", xkey); /* Application mode*/
		else
                       p += sprintf((char *) p, "\x1B[%c", xkey); /* Normal */
		}
	else {
	/* Useful mapping of Ctrl-arrows */
	if (modifier)
	    app_flg = !app_flg;

	if (app_flg)
	    p += sprintf((char *) p, "\x1BO%c", xkey);
	else
	    p += sprintf((char *) p, "\x1B[%c", xkey);
		}
#else
	/* Useful mapping of Ctrl-arrows */
	if (ctrl)
	    app_flg = !app_flg;

	if (app_flg)
	    p += sprintf(p, "\x1BO%c", xkey);
	else
	    p += sprintf(p, "\x1B[%c", xkey);
#endif
    }

    return p - buf;
}

#ifdef MOD_KEYMAPPING
int format_function_key(char *buf, Terminal *term, int key_number, int modifier, bool alt)
#else
int format_function_key(char *buf, Terminal *term, int key_number,
                        bool shift, bool ctrl)
#endif
{
    char *p = buf;

    static const int key_number_to_tilde_code[] = {
        -1,                 /* no such key as F0 */
        11, 12, 13, 14, 15, /*gap*/ 17, 18, 19, 20, 21, /*gap*/
        23, 24, 25, 26, /*gap*/ 28, 29, /*gap*/ 31, 32, 33, 34,
    };

    assert(key_number > 0);
    assert(key_number < lenof(key_number_to_tilde_code));
#ifdef MOD_KEYMAPPING
    bool shift = modifier & 1;
    bool ctrl = modifier & 2;
    if (term->funky_type == FUNKY_XTERM && !term->vt52_mode)
    {
        // XTerm mode
        char prefix[20];
        char suffix[20];
        int xterm_modifier = get_xterm_modifier(shift, ctrl, alt);
        if (xterm_modifier > 1)
        {
            sprintf(prefix, "[1;%d", xterm_modifier);
            sprintf(suffix, ";%d", xterm_modifier);
        }
        else
        {
            sprintf(prefix, "O");
            strcpy(suffix, "");//sprintf(suffix, "");
        }

        int code = key_number_to_tilde_code[key_number];
        if (code >= 11 && code <= 14)
        {
            p += sprintf(p, "\x1B%s%c", prefix, code + 'P' - 11);
        }
        else
        {
            p += sprintf(p, "\x1B[%d%s~", code, suffix);
        }
        return p - buf;
    }
#endif

    int index = (shift && key_number <= 10) ? key_number + 10 : key_number;
    int code = key_number_to_tilde_code[index];

    if (term->funky_type == FUNKY_SCO) {
        /* SCO function keys */
        static const char sco_codes[] =
            "MNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@[\\]^_`{";
        index = (key_number >= 1 && key_number <= 12) ? key_number - 1 : 0;
        if (shift) index += 12;
        if (ctrl) index += 24;
        p += sprintf(p, "\x1B[%c", sco_codes[index]);
    } else if ((term->vt52_mode || term->funky_type == FUNKY_VT100P) &&
               code >= 11 && code <= 24) {
        int offt = 0;
        if (code > 15)
            offt++;
        if (code > 21)
            offt++;
        if (term->vt52_mode)
            p += sprintf(p, "\x1B%c", code + 'P' - 11 - offt);
        else
            p += sprintf(p, "\x1BO%c", code + 'P' - 11 - offt);
    } else if (term->funky_type == FUNKY_LINUX && code >= 11 && code <= 15) {
        p += sprintf(p, "\x1B[[%c", code + 'A' - 11);
    } else if (term->funky_type == FUNKY_XTERM && code >= 11 && code <= 14) {
        if (term->vt52_mode)
            p += sprintf(p, "\x1B%c", code + 'P' - 11);
        else
            p += sprintf(p, "\x1BO%c", code + 'P' - 11);
    } else {
        p += sprintf(p, "\x1B[%d~", code);
    }

    return p - buf;
}

#ifdef MOD_KEYMAPPING
int format_small_keypad_key(char *buf, Terminal *term, SmallKeypadKey key, int modifier, bool alt)
#else
int format_small_keypad_key(char *buf, Terminal *term, SmallKeypadKey key)
#endif
{
    char *p = buf;

    int code;
    switch (key) {
      case SKK_HOME: code = 1; break;
      case SKK_INSERT: code = 2; break;
      case SKK_DELETE: code = 3; break;
      case SKK_END: code = 4; break;
      case SKK_PGUP: code = 5; break;
      case SKK_PGDN: code = 6; break;
      default: unreachable("bad small keypad key enum value");
    }
#ifdef MOD_KEYMAPPING
    int home_end_type = conf_get_int(term->conf, CONF_rxvt_homeend);  // standard(0), rxvt, urxvt, xterm(3), bsd1, bsd2
    bool shift = modifier & 1;
    bool ctrl = modifier & 2;
    if (term->funky_type == FUNKY_XTERM && !term->vt52_mode && (home_end_type == 0 || home_end_type == 3))
    {
        // XTerm mode
        char prefix[20];
        char suffix[20];
        bool app_flg = (term->app_cursor_keys && !term->no_applic_c);
        int xterm_modifier = get_xterm_modifier(shift, ctrl, alt);
        if (xterm_modifier > 1)
        {
            sprintf(prefix, "[1;%d", xterm_modifier);
            sprintf(suffix, ";%d", xterm_modifier);
        }
        else
        {
            if (!app_flg)
                sprintf(prefix, "[");
            else
                sprintf(prefix, "O");
            strcpy(suffix, "");//sprintf(suffix, "");
        }

        if ((xterm_modifier > 1 || app_flg || home_end_type == 3) && (code == 1 || code == 4))
        {
            p += sprintf(p, "\x1B%s%c", prefix, code == 1 ? 'H' : 'F');
        }
        else
        {
            p += sprintf(p, "\x1B[%d%s~", code, suffix);
        }

        return p - buf;
    }
#endif

    /* Reorder edit keys to physical order */
    if (term->funky_type == FUNKY_VT400 && code <= 6)
        code = "\0\2\1\4\5\3\6"[code];

    if (term->vt52_mode && code > 0 && code <= 6) {
        p += sprintf(p, "\x1B%c", " HLMEIG"[code]);
    } else if (term->funky_type == FUNKY_SCO) {
        static const char codes[] = "HL.FIG";
        if (code == 3) {
            *p++ = '\x7F';
        } else {
            p += sprintf(p, "\x1B[%c", codes[code-1]);
        }
#ifdef MOD_PERSO
    } else if ((code == 1 || code == 4) && (conf_get_int(term->conf, CONF_rxvt_homeend) == 1) ) {
#else
    } else if ((code == 1 || code == 4) && term->rxvt_homeend) {
#endif
        p += sprintf(p, code == 1 ? "\x1B[H" : "\x1BOw");
    } else {
#ifdef MOD_PERSO
	if ((code == 1 || code == 4) &&
	    conf_get_int(term->conf, CONF_rxvt_homeend) == 2) {
	    // urxvt
	    p += sprintf((char *) p, code == 1 ? "\x1B[7~" : "\x1B[8~");
	} else if ((code == 1 || code == 4) &&
	    conf_get_int(term->conf, CONF_rxvt_homeend) == 3) {
	    // xterm
	    p += sprintf((char *) p, code == 1 ? "\x1BOH" : "\x1BOF");
	} else if ((code == 1 || code == 4) &&
	    conf_get_int(term->conf, CONF_rxvt_homeend) == 4) {
	    // FreeBSD1
	    p += sprintf((char *) p, code == 1 ? "\x1B[H" : "\x1B[H");
	} else if ((code == 1 || code == 4) &&
	    conf_get_int(term->conf, CONF_rxvt_homeend) == 5) {
	    // FreeBSD2
	    p += sprintf((char *) p, code == 1 ? "\x1BOH" : "\x1B[?1l\x1B>");
	} else
#endif
	    
        p += sprintf(p, "\x1B[%d~", code);
    }

    return p - buf;
}

int format_numeric_keypad_key(char *buf, Terminal *term, char key,
                              bool shift, bool ctrl)
{
    char *p = buf;
    bool app_keypad = (term->app_keypad_keys && !term->no_applic_k);

    if (term->nethack_keypad && (key >= '1' && key <= '9')) {
        static const char nh_base[] = "bjnh.lyku";
        char c = nh_base[key - '1'];
        if (ctrl && c != '.')
            c &= 0x1F;
        else if (shift && c != '.')
            c += 'A'-'a';
        *p++ = c;
    } else {
        int xkey = 0;

        if (term->funky_type == FUNKY_VT400 ||
            (term->funky_type <= FUNKY_LINUX && app_keypad)) {
            switch (key) {
              case 'G': xkey = 'P'; break;
              case '/': xkey = 'Q'; break;
              case '*': xkey = 'R'; break;
              case '-': xkey = 'S'; break;
            }
        }

        if (app_keypad) {
            switch (key) {
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
              case '\r': xkey = 'M'; break;

              case '+':
		/*
		 * Keypad + is tricky. It covers a space that would
		 * be taken up on the VT100 by _two_ keys; so we
		 * let Shift select between the two. Worse still,
		 * in xterm function key mode we change which two...
		 */
                if (term->funky_type == FUNKY_XTERM)
                    xkey = shift ? 'l' : 'k';
                else
                    xkey = shift ? 'm' : 'l';
                break;

              case '/':
                if (term->funky_type == FUNKY_XTERM)
                    xkey = 'o';
                break;
              case '*':
                if (term->funky_type == FUNKY_XTERM)
                    xkey = 'j';
                break;
              case '-':
                if (term->funky_type == FUNKY_XTERM)
                    xkey = 'm';
                break;
            }
        }

        if (xkey) {
            if (term->vt52_mode) {
                if (xkey >= 'P' && xkey <= 'S')
                    p += sprintf(p, "\x1B%c", xkey);
                else
                    p += sprintf(p, "\x1B?%c", xkey);
            } else
                p += sprintf(p, "\x1BO%c", xkey);
        }
    }

    return p - buf;
}

void term_keyinputw(Terminal *term, const wchar_t *widebuf, int len)
{
    strbuf *buf = term_input_data_from_unicode(term, widebuf, len);
    if (buf->len)
        term_keyinput_internal(term, buf->s, buf->len, true);
    strbuf_free(buf);
}

void term_keyinput(Terminal *term, int codepage, const void *str, int len)
{
    if (codepage < 0 || codepage == term->ucsdata->line_codepage) {
        /*
         * This text needs no translation, either because it's already
         * in the right character set, or because we got the special
         * codepage value -1 from our caller which means 'this data
         * should be charset-agnostic, just send it raw' (for really
         * simple things like control characters).
         */
        term_keyinput_internal(term, str, len, true);
    } else {
        strbuf *buf = term_input_data_from_charset(term, codepage, str, len);
        if (buf->len)
            term_keyinput_internal(term, buf->s, buf->len, true);
        strbuf_free(buf);
    }
}

void term_nopaste(Terminal *term)
{
    if (term->paste_len == 0)
	return;
    sfree(term->paste_buffer);
    term_bracketed_paste_stop(term);
    term->paste_buffer = NULL;
    term->paste_len = 0;
}

static void deselect(Terminal *term)
{
    term->selstate = NO_SELECTION;
    term->selstart.x = term->selstart.y = term->selend.x = term->selend.y = 0;
}

void term_lost_clipboard_ownership(Terminal *term, int clipboard)
{
    if (!(term->n_mouse_select_clipboards > 1 &&
          clipboard == term->mouse_select_clipboards[1]))
        return;

    deselect(term);
    term_update(term);

    /*
     * Since terminal output is suppressed during drag-selects, we
     * should make sure to write any pending output if one has just
     * finished.
     */
    if (term->selstate != DRAGGING)
        term_out(term);
}

static void term_added_data(Terminal *term)
{
    if (!term->in_term_out) {
	term->in_term_out = true;
	term_reset_cblink(term);
	/*
	 * During drag-selects, we do not process terminal input,
	 * because the user will want the screen to hold still to
	 * be selected.
	 */
	if (term->selstate != DRAGGING)
	    term_out(term);
	term->in_term_out = false;
    }
}

size_t term_data(Terminal *term, bool is_stderr, const void *data, size_t len)
{
#ifdef MOD_ZMODEM
	if ( GetZModemFlag() && term->xyz_transfering && !is_stderr)
		{ return xyz_ReceiveData(term, (const u_char *) data, len) ; }
	else
	{
#endif
    bufchain_add(&term->inbuf, data, len);
    term_added_data(term);

#ifdef MOD_HYPERLINK
	/*
	 * HACK: PuttyTray / Nutty
	 */
	if( !GetPuttyFlag() && GetHyperlinkFlag() ) term->url_update = TRUE;
#endif
#ifdef MOD_ZMODEM
    }
#endif
    /*
     * term_out() always completely empties inbuf. Therefore,
     * there's no reason at all to return anything other than zero
     * from this function, because there _can't_ be a question of
     * the remote side needing to wait until term_out() has cleared
     * a backlog.
     *
     * This is a slightly suboptimal way to deal with SSH-2 - in
     * principle, the window mechanism would allow us to continue
     * to accept data on forwarded ports and X connections even
     * while the terminal processing was going slowly - but we
     * can't do the 100% right thing without moving the terminal
     * processing into a separate thread, and that might hurt
     * portability. So we manage stdout buffering the old SSH-1 way:
     * if the terminal processing goes slowly, the whole SSH
     * connection stops accepting data until it's ready.
     *
     * In practice, I can't imagine this causing serious trouble.
     */
    return 0;
}

void term_provide_logctx(Terminal *term, LogContext *logctx)
{
    term->logctx = logctx;
}

void term_set_focus(Terminal *term, bool has_focus)
{
#ifdef MOD_PERSO
    if( term->has_focus != has_focus) {
        term->has_focus = has_focus;

        if(term->report_focus && term->ldisc) {
            if (has_focus)
                ldisc_send(term->ldisc, "\033[I", 3,false);
            else
                ldisc_send(term->ldisc, "\033[O", 3,false);
        }
    }
#else
    term->has_focus = has_focus;
#endif
    term_schedule_cblink(term);
}

/*
 * Provide "auto" settings for remote tty modes, suitable for an
 * application with a terminal window.
 */
char *term_get_ttymode(Terminal *term, const char *mode)
{
    const char *val = NULL;
    if (strcmp(mode, "ERASE") == 0) {
	val = term->bksp_is_delete ? "^?" : "^H";
    } else if (strcmp(mode, "IUTF8") == 0) {
        val = (term->ucsdata->line_codepage == CP_UTF8) ? "yes" : "no";
    }
    /* FIXME: perhaps we should set ONLCR based on lfhascr as well? */
    /* FIXME: or ECHO and friends based on local echo state? */
    return dupstr(val);
}

struct term_userpass_state {
    size_t curr_prompt;
    bool done_prompt;   /* printed out prompt yet? */
};

/* Tiny wrapper to make it easier to write lots of little strings */
static inline void term_write(Terminal *term, ptrlen data)
{
    term_data(term, false, data.ptr, data.len);
}

/*
 * Process some terminal data in the course of username/password
 * input.
 */
int term_get_userpass_input(Terminal *term, prompts_t *p, bufchain *input)
{
    struct term_userpass_state *s = (struct term_userpass_state *)p->data;
    if (!s) {
	/*
	 * First call. Set some stuff up.
	 */
	p->data = s = snew(struct term_userpass_state);
	s->curr_prompt = 0;
	s->done_prompt = false;
	/* We only print the `name' caption if we have to... */
	if (p->name_reqd && p->name) {
            ptrlen plname = ptrlen_from_asciz(p->name);
            term_write(term, plname);
            if (!ptrlen_endswith(plname, PTRLEN_LITERAL("\n"), NULL))
                term_write(term, PTRLEN_LITERAL("\r\n"));
	}
	/* ...but we always print any `instruction'. */
	if (p->instruction) {
            ptrlen plinst = ptrlen_from_asciz(p->instruction);
            term_write(term, plinst);
            if (!ptrlen_endswith(plinst, PTRLEN_LITERAL("\n"), NULL))
                term_write(term, PTRLEN_LITERAL("\r\n"));
	}
	/*
	 * Zero all the results, in case we abort half-way through.
	 */
	{
	    int i;
	    for (i = 0; i < (int)p->n_prompts; i++)
                prompt_set_result(p->prompts[i], "");
	}
    }

    while (s->curr_prompt < p->n_prompts) {

	prompt_t *pr = p->prompts[s->curr_prompt];
	bool finished_prompt = false;

	if (!s->done_prompt) {
	    term_write(term, ptrlen_from_asciz(pr->prompt));
	    s->done_prompt = true;
	}

	/* Breaking out here ensures that the prompt is printed even
	 * if we're now waiting for user data. */
	if (!input || !bufchain_size(input)) break;

	/* FIXME: should we be using local-line-editing code instead? */
	while (!finished_prompt && bufchain_size(input) > 0) {
	    char c;
            bufchain_fetch_consume(input, &c, 1);
	    switch (c) {
	      case 10:
	      case 13:
		term_write(term, PTRLEN_LITERAL("\r\n"));
		/* go to next prompt, if any */
		s->curr_prompt++;
		s->done_prompt = false;
		finished_prompt = true; /* break out */
		break;
	      case 8:
	      case 127:
                if (pr->result->len > 0) {
		    if (pr->echo)
			term_write(term, PTRLEN_LITERAL("\b \b"));
                    strbuf_shrink_by(pr->result, 1);
		}
		break;
	      case 21:
	      case 27:
                while (pr->result->len > 0) {
		    if (pr->echo)
			term_write(term, PTRLEN_LITERAL("\b \b"));
                    strbuf_shrink_by(pr->result, 1);
		}
		break;
	      case 3:
	      case 4:
		/* Immediate abort. */
		term_write(term, PTRLEN_LITERAL("\r\n"));
		sfree(s);
		p->data = NULL;
		return 0; /* user abort */
	      default:
		/*
		 * This simplistic check for printability is disabled
		 * when we're doing password input, because some people
		 * have control characters in their passwords.
		 */
		if (!pr->echo || (c >= ' ' && c <= '~') ||
		     ((unsigned char) c >= 160)) {
                    put_byte(pr->result, c);
		    if (pr->echo)
			term_write(term, make_ptrlen(&c, 1));
		}
		break;
	    }
	}
	
    }

    if (s->curr_prompt < p->n_prompts) {
	return -1; /* more data required */
    } else {
	sfree(s);
	p->data = NULL;
	return +1; /* all done */
    }
}

void term_notify_minimised(Terminal *term, bool minimised)
{
    term->minimised = minimised;
}

void term_notify_palette_changed(Terminal *term)
{
    palette_reset(term, true);
}

void term_notify_window_pos(Terminal *term, int x, int y)
{
    term->winpos_x = x;
    term->winpos_y = y;
}

void term_notify_window_size_pixels(Terminal *term, int x, int y)
{
    term->winpixsize_x = x;
    term->winpixsize_y = y;
}
