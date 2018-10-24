/*
 * Unified font management for GTK.
 * 
 * PuTTY is willing to use both old-style X server-side bitmap
 * fonts _and_ GTK2/Pango client-side fonts. This requires us to
 * do a bit of work to wrap the two wildly different APIs into
 * forms the rest of the code can switch between seamlessly, and
 * also requires a custom font selector capable of handling both
 * types of font.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#if !GTK_CHECK_VERSION(3,0,0)
#include <gdk/gdkkeysyms.h>
#endif

#define MAY_REFER_TO_GTK_IN_HEADERS

#include "putty.h"
#include "gtkfont.h"
#include "gtkcompat.h"
#include "gtkmisc.h"
#include "tree234.h"

#ifndef NOT_X_WINDOWS
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#endif

/*
 * Future work:
 * 
 *  - it would be nice to have a display of the current font name,
 *    and in particular whether it's client- or server-side,
 *    during the progress of the font selector.
 */

#if !GLIB_CHECK_VERSION(1,3,7)
#define g_ascii_strcasecmp g_strcasecmp
#define g_ascii_strncasecmp g_strncasecmp
#endif

/*
 * Ad-hoc vtable mechanism to allow font structures to be
 * polymorphic.
 * 
 * Any instance of `unifont' used in the vtable functions will
 * actually be the first element of a larger structure containing
 * data specific to the subtype. This is permitted by the ISO C
 * provision that one may safely cast between a pointer to a
 * structure and a pointer to its first element.
 */

#define FONTFLAG_CLIENTSIDE    0x0001
#define FONTFLAG_SERVERSIDE    0x0002
#define FONTFLAG_SERVERALIAS   0x0004
#define FONTFLAG_NONMONOSPACED 0x0008

#define FONTFLAG_SORT_MASK     0x0007 /* used to disambiguate font families */

typedef void (*fontsel_add_entry)(void *ctx, const char *realfontname,
				  const char *family, const char *charset,
				  const char *style, const char *stylekey,
				  int size, int flags,
				  const struct unifont_vtable *fontclass);

struct unifont_vtable {
    /*
     * `Methods' of the `class'.
     */
    unifont *(*create)(GtkWidget *widget, const char *name, int wide, int bold,
		       int shadowoffset, int shadowalways);
    unifont *(*create_fallback)(GtkWidget *widget, int height, int wide,
                                int bold, int shadowoffset, int shadowalways);
    void (*destroy)(unifont *font);
    int (*has_glyph)(unifont *font, wchar_t glyph);
    void (*draw_text)(unifont_drawctx *ctx, unifont *font,
                      int x, int y, const wchar_t *string, int len,
                      int wide, int bold, int cellwidth);
    void (*draw_combining)(unifont_drawctx *ctx, unifont *font,
                           int x, int y, const wchar_t *string, int len,
                           int wide, int bold, int cellwidth);
    void (*enum_fonts)(GtkWidget *widget,
		       fontsel_add_entry callback, void *callback_ctx);
    char *(*canonify_fontname)(GtkWidget *widget, const char *name, int *size,
			       int *flags, int resolve_aliases);
    char *(*scale_fontname)(GtkWidget *widget, const char *name, int size);
    char *(*size_increment)(unifont *font, int increment);

    /*
     * `Static data members' of the `class'.
     */
    const char *prefix;
};

#ifndef NOT_X_WINDOWS

/* ----------------------------------------------------------------------
 * X11 font implementation, directly using Xlib calls. Conditioned out
 * if X11 fonts aren't available at all (e.g. building with GTK3 for a
 * back end other than X).
 */

static int x11font_has_glyph(unifont *font, wchar_t glyph);
static void x11font_draw_text(unifont_drawctx *ctx, unifont *font,
                              int x, int y, const wchar_t *string, int len,
                              int wide, int bold, int cellwidth);
static void x11font_draw_combining(unifont_drawctx *ctx, unifont *font,
                                   int x, int y, const wchar_t *string,
                                   int len, int wide, int bold, int cellwidth);
static unifont *x11font_create(GtkWidget *widget, const char *name,
			       int wide, int bold,
			       int shadowoffset, int shadowalways);
static void x11font_destroy(unifont *font);
static void x11font_enum_fonts(GtkWidget *widget,
			       fontsel_add_entry callback, void *callback_ctx);
static char *x11font_canonify_fontname(GtkWidget *widget, const char *name,
				       int *size, int *flags,
				       int resolve_aliases);
static char *x11font_scale_fontname(GtkWidget *widget, const char *name,
				    int size);
static char *x11font_size_increment(unifont *font, int increment);

#ifdef DRAW_TEXT_CAIRO
struct cairo_cached_glyph {
    cairo_surface_t *surface;
    unsigned char *bitmap;
};
#endif

/*
 * Structure storing a single physical XFontStruct, plus associated
 * data.
 */
typedef struct x11font_individual {
    /* The XFontStruct itself. */
    XFontStruct *xfs;

    /*
     * The `allocated' flag indicates whether we've tried to fetch
     * this subfont already (thus distinguishing xfs==NULL because we
     * haven't tried yet from xfs==NULL because we tried and failed,
     * so that we don't keep trying and failing subsequently).
     */
    int allocated;

#ifdef DRAW_TEXT_CAIRO
    /*
     * A cache of glyph bitmaps downloaded from the X server when
     * we're in Cairo rendering mode. If glyphcache itself is
     * non-NULL, then entries in [0,nglyphs) are expected to be
     * initialised to either NULL or a bitmap pointer.
     */
    struct cairo_cached_glyph *glyphcache;
    int nglyphs;

    /*
     * X server paraphernalia for actually downloading the glyphs.
     */
    Pixmap pixmap;
    GC gc;
    int pixwidth, pixheight, pixoriginx, pixoriginy;

    /*
     * Paraphernalia for loading the resulting bitmaps into Cairo.
     */
    int rowsize, allsize, indexflip;
#endif

} x11font_individual;

struct x11font {
    struct unifont u;
    /*
     * Individual physical X fonts. We store a number of these, for
     * automatically guessed bold and wide variants.
     */
    x11font_individual fonts[4];
    /*
     * `sixteen_bit' is true iff the font object is indexed by
     * values larger than a byte. That is, this flag tells us
     * whether we use XDrawString or XDrawString16, etc.
     */
    int sixteen_bit;
    /*
     * `variable' is true iff the font is non-fixed-pitch. This
     * enables some code which takes greater care over character
     * positioning during text drawing.
     */
    int variable;
    /*
     * real_charset is the charset used when translating text into the
     * font's internal encoding inside draw_text(). This need not be
     * the same as the public_charset provided to the client; for
     * example, public_charset might be CS_ISO8859_1 while
     * real_charset is CS_ISO8859_1_X11.
     */
    int real_charset;
    /*
     * Data passed in to unifont_create().
     */
    int wide, bold, shadowoffset, shadowalways;
};

static const struct unifont_vtable x11font_vtable = {
    x11font_create,
    NULL,                              /* no fallback fonts in X11 */
    x11font_destroy,
    x11font_has_glyph,
    x11font_draw_text,
    x11font_draw_combining,
    x11font_enum_fonts,
    x11font_canonify_fontname,
    x11font_scale_fontname,
    x11font_size_increment,
    "server",
};

#define XLFD_STRING_PARTS_LIST(S,I)             \
    S(foundry)                                  \
    S(family_name)                              \
    S(weight_name)                              \
    S(slant)                                    \
    S(setwidth_name)                            \
    S(add_style_name)                           \
    I(pixel_size)                               \
    I(point_size)                               \
    I(resolution_x)                             \
    I(resolution_y)                             \
    S(spacing)                                  \
    I(average_width)                            \
    S(charset_registry)                         \
    S(charset_encoding)                         \
    /* end of list */

/* Special value for int fields that xlfd_recompose will render as "*" */
#define XLFD_INT_WILDCARD INT_MIN

struct xlfd_decomposed {
#define STR_FIELD(f) const char *f;
#define INT_FIELD(f) int f;
    XLFD_STRING_PARTS_LIST(STR_FIELD, INT_FIELD)
#undef STR_FIELD
#undef INT_FIELD
};

static struct xlfd_decomposed *xlfd_decompose(const char *xlfd)
{
    void *mem;
    char *p, *components[14];
    struct xlfd_decomposed *dec;
    int i;

    if (!xlfd)
        return NULL;

    mem = smalloc(sizeof(struct xlfd_decomposed) + strlen(xlfd) + 1);
    p = ((char *)mem) + sizeof(struct xlfd_decomposed);
    strcpy(p, xlfd);
    dec = (struct xlfd_decomposed *)mem;

    for (i = 0; i < 14; i++) {
        if (*p != '-') {
            /* Malformed XLFD: not enough '-' */
            sfree(mem);
            return NULL;
        }
        *p++ = '\0';
        components[i] = p;
        p += strcspn(p, "-");
    }
    if (*p) {
        /* Malformed XLFD: too many '-' */
        sfree(mem);
        return NULL;
    }

    i = 0;
#define STORE_STR(f) dec->f = components[i++];
#define STORE_INT(f) dec->f = atoi(components[i++]);
    XLFD_STRING_PARTS_LIST(STORE_STR, STORE_INT)
#undef STORE_STR
#undef STORE_INT

    return dec;
}

static char *xlfd_recompose(const struct xlfd_decomposed *dec)
{
#define FMT_STR(f) "-%s"
#define ARG_STR(f) , dec->f
#define FMT_INT(f) "%s%.*d"
#define ARG_INT(f)                                      \
        , dec->f == XLFD_INT_WILDCARD ? "-*" : "-"      \
        , dec->f == XLFD_INT_WILDCARD ? 0 : 1           \
        , dec->f == XLFD_INT_WILDCARD ? 0 : dec->f
    return dupprintf(XLFD_STRING_PARTS_LIST(FMT_STR, FMT_INT)
                     XLFD_STRING_PARTS_LIST(ARG_STR, ARG_INT));
#undef FMT_STR
#undef ARG_STR
#undef FMT_INT
#undef ARG_INT
}

static char *x11_guess_derived_font_name(XFontStruct *xfs, int bold, int wide)
{
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Atom fontprop = XInternAtom(disp, "FONT", False);
    unsigned long ret;
    if (XGetFontProperty(xfs, fontprop, &ret)) {
	char *name = XGetAtomName(disp, (Atom)ret);
        struct xlfd_decomposed *xlfd = xlfd_decompose(name);
        if (!xlfd)
            return NULL;

        if (bold)
            xlfd->weight_name = "bold";

        if (wide) {
            /* Width name obviously may have changed. */
            /* Additional style may now become e.g. `ja' or `ko'. */
            xlfd->setwidth_name = xlfd->add_style_name = "*";

            /* Expect to double the average width. */
            xlfd->average_width *= 2;
        }

        {
            char *ret = xlfd_recompose(xlfd);
            sfree(xlfd);
            return ret;
        }
    }
    return NULL;
}

static int x11_font_width(XFontStruct *xfs, int sixteen_bit)
{
    if (sixteen_bit) {
	XChar2b space;
	space.byte1 = 0;
	space.byte2 = '0';
	return XTextWidth16(xfs, &space, 1);
    } else {
	return XTextWidth(xfs, "0", 1);
    }
}

static const XCharStruct *x11_char_struct(
    XFontStruct *xfs, unsigned char byte1, unsigned char byte2)
{
    int index;

    /*
     * The man page for XQueryFont is rather confusing about how the
     * per_char array in the XFontStruct is laid out, because it gives
     * formulae for determining the two-byte X character code _from_
     * an index into the per_char array. Going the other way, it's
     * rather simpler:
     *
     * The valid character codes have byte1 between min_byte1 and
     * max_byte1 inclusive, and byte2 between min_char_or_byte2 and
     * max_char_or_byte2 inclusive. This gives a rectangle of size
     * (max_byte2-min_byte1+1) by
     * (max_char_or_byte2-min_char_or_byte2+1), which is precisely the
     * rectangle encoded in the per_char array. Hence, given a
     * character code which is valid in the sense that it falls
     * somewhere in that rectangle, its index in per_char is given by
     * setting
     *
     *   x = byte2 - min_char_or_byte2
     *   y = byte1 - min_byte1
     *   index = y * (max_char_or_byte2-min_char_or_byte2+1) + x
     *
     * If min_byte1 and min_byte2 are both zero, that's a special case
     * which can be treated as if min_byte2 was 1 instead, i.e. the
     * per_char array just runs from min_char_or_byte2 to
     * max_char_or_byte2 inclusive, and byte1 should always be zero.
     */

    if (byte2 < xfs->min_char_or_byte2 || byte2 > xfs->max_char_or_byte2)
        return FALSE;

    if (xfs->min_byte1 == 0 && xfs->max_byte1 == 0) {
        index = byte2 - xfs->min_char_or_byte2;
    } else {
        if (byte1 < xfs->min_byte1 || byte1 > xfs->max_byte1)
            return FALSE;
        index = ((byte2 - xfs->min_char_or_byte2) +
                 ((byte1 - xfs->min_byte1) *
                  (xfs->max_char_or_byte2 - xfs->min_char_or_byte2 + 1)));
    }

    if (!xfs->per_char)   /* per_char NULL => everything in range exists */
        return &xfs->max_bounds;

    return &xfs->per_char[index];
}

static int x11_font_has_glyph(
    XFontStruct *xfs, unsigned char byte1, unsigned char byte2)
{
    /*
     * Not to be confused with x11font_has_glyph, which is a method of
     * the x11font 'class' and hence takes a unifont as argument. This
     * is the low-level function which grubs about in an actual
     * XFontStruct to see if a given glyph exists.
     *
     * We must do this ourselves rather than letting Xlib's
     * XTextExtents16 do the job, because XTextExtents will helpfully
     * substitute the font's default_char for any missing glyph and
     * not tell us it did so, which precisely won't help us find out
     * which glyphs _are_ missing.
     */
    const XCharStruct *xcs = x11_char_struct(xfs, byte1, byte2);
    return xcs && (xcs->ascent + xcs->descent > 0 || xcs->width > 0);
}

static unifont *x11font_create(GtkWidget *widget, const char *name,
			       int wide, int bold,
			       int shadowoffset, int shadowalways)
{
    struct x11font *xfont;
    XFontStruct *xfs;
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Atom charset_registry, charset_encoding, spacing;
    unsigned long registry_ret, encoding_ret, spacing_ret;
    int pubcs, realcs, sixteen_bit, variable;
    int i;

    xfs = XLoadQueryFont(disp, name);
    if (!xfs)
	return NULL;

    charset_registry = XInternAtom(disp, "CHARSET_REGISTRY", False);
    charset_encoding = XInternAtom(disp, "CHARSET_ENCODING", False);

    pubcs = realcs = CS_NONE;
    sixteen_bit = FALSE;
    variable = TRUE;

    if (XGetFontProperty(xfs, charset_registry, &registry_ret) &&
	XGetFontProperty(xfs, charset_encoding, &encoding_ret)) {
	char *reg, *enc;
	reg = XGetAtomName(disp, (Atom)registry_ret);
	enc = XGetAtomName(disp, (Atom)encoding_ret);
	if (reg && enc) {
	    char *encoding = dupcat(reg, "-", enc, NULL);
	    pubcs = realcs = charset_from_xenc(encoding);

	    /*
	     * iso10646-1 is the only wide font encoding we
	     * support. In this case, we expect clients to give us
	     * UTF-8, which this module must internally convert
	     * into 16-bit Unicode.
	     */
	    if (!strcasecmp(encoding, "iso10646-1")) {
		sixteen_bit = TRUE;
		pubcs = realcs = CS_UTF8;
	    }

	    /*
	     * Hack for X line-drawing characters: if the primary font
	     * is encoded as ISO-8859-1, and has valid glyphs in the
	     * low character positions, it is assumed that those
	     * glyphs are the VT100 line-drawing character set.
	     */
	    if (pubcs == CS_ISO8859_1) {
                int ch;
                for (ch = 1; ch < 32; ch++)
                    if (!x11_font_has_glyph(xfs, 0, ch))
                        break;
                if (ch == 32)
                    realcs = CS_ISO8859_1_X11;
            }

	    sfree(encoding);
	}
    }

    spacing = XInternAtom(disp, "SPACING", False);
    if (XGetFontProperty(xfs, spacing, &spacing_ret)) {
	char *spc;
	spc = XGetAtomName(disp, (Atom)spacing_ret);

	if (spc && strchr("CcMm", spc[0]))
	    variable = FALSE;
    }

    xfont = snew(struct x11font);
    xfont->u.vt = &x11font_vtable;
    xfont->u.width = x11_font_width(xfs, sixteen_bit);
    xfont->u.ascent = xfs->ascent;
    xfont->u.descent = xfs->descent;
    xfont->u.height = xfont->u.ascent + xfont->u.descent;
    xfont->u.public_charset = pubcs;
    xfont->u.want_fallback = TRUE;
#ifdef DRAW_TEXT_GDK
    xfont->u.preferred_drawtype = DRAWTYPE_GDK;
#elif defined DRAW_TEXT_CAIRO
    xfont->u.preferred_drawtype = DRAWTYPE_CAIRO;
#else
#error No drawtype available at all
#endif
    xfont->real_charset = realcs;
    xfont->sixteen_bit = sixteen_bit;
    xfont->variable = variable;
    xfont->wide = wide;
    xfont->bold = bold;
    xfont->shadowoffset = shadowoffset;
    xfont->shadowalways = shadowalways;

    for (i = 0; i < lenof(xfont->fonts); i++) {
	xfont->fonts[i].xfs = NULL;
	xfont->fonts[i].allocated = FALSE;
#ifdef DRAW_TEXT_CAIRO
	xfont->fonts[i].glyphcache = NULL;
	xfont->fonts[i].nglyphs = 0;
	xfont->fonts[i].pixmap = None;
	xfont->fonts[i].gc = None;
#endif
    }
    xfont->fonts[0].xfs = xfs;
    xfont->fonts[0].allocated = TRUE;

    return (unifont *)xfont;
}

static void x11font_destroy(unifont *font)
{
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    struct x11font *xfont = (struct x11font *)font;
    int i;

    for (i = 0; i < lenof(xfont->fonts); i++) {
	if (xfont->fonts[i].xfs)
	    XFreeFont(disp, xfont->fonts[i].xfs);
#ifdef DRAW_TEXT_CAIRO
	if (xfont->fonts[i].gc != None)
	    XFreeGC(disp, xfont->fonts[i].gc);
	if (xfont->fonts[i].pixmap != None)
	    XFreePixmap(disp, xfont->fonts[i].pixmap);
	if (xfont->fonts[i].glyphcache) {
            int j;
            for (j = 0; j < xfont->fonts[i].nglyphs; j++) {
                cairo_surface_destroy(xfont->fonts[i].glyphcache[j].surface);
                sfree(xfont->fonts[i].glyphcache[j].bitmap);
            }
            sfree(xfont->fonts[i].glyphcache);
        }
#endif
    }
    sfree(font);
}

static void x11_alloc_subfont(struct x11font *xfont, int sfid)
{
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    char *derived_name = x11_guess_derived_font_name
	(xfont->fonts[0].xfs, sfid & 1, !!(sfid & 2));
    xfont->fonts[sfid].xfs = XLoadQueryFont(disp, derived_name);
    xfont->fonts[sfid].allocated = TRUE;
    sfree(derived_name);
    /* Note that xfont->fonts[sfid].xfs may still be NULL, if XLQF failed. */
}

static int x11font_has_glyph(unifont *font, wchar_t glyph)
{
    struct x11font *xfont = (struct x11font *)font;

    if (xfont->sixteen_bit) {
	/*
	 * This X font has 16-bit character indices, which means
	 * we can directly use our Unicode input value.
	 */
        return x11_font_has_glyph(xfont->fonts[0].xfs,
                                  glyph >> 8, glyph & 0xFF);
    } else {
        /*
         * This X font has 8-bit indices, so we must convert to the
         * appropriate character set.
         */
        char sbstring[2];
        int sblen = wc_to_mb(xfont->real_charset, 0, &glyph, 1,
                             sbstring, 2, "", NULL, NULL);
        if (sblen == 0 || !sbstring[0])
            return FALSE;              /* not even in the charset */

        return x11_font_has_glyph(xfont->fonts[0].xfs, 0,
                                  (unsigned char)sbstring[0]);
    }
}

#if !GTK_CHECK_VERSION(2,0,0)
#define GDK_DRAWABLE_XID(d) GDK_WINDOW_XWINDOW(d) /* GTK1's name for this */
#elif GTK_CHECK_VERSION(3,0,0)
#define GDK_DRAWABLE_XID(d) GDK_WINDOW_XID(d) /* GTK3's name for this */
#endif

static int x11font_width_16(unifont_drawctx *ctx, x11font_individual *xfi,
                            const void *vstring, int start, int length)
{
    const XChar2b *string = (const XChar2b *)vstring;
    return XTextWidth16(xfi->xfs, string+start, length);
}

static int x11font_width_8(unifont_drawctx *ctx, x11font_individual *xfi,
                           const void *vstring, int start, int length)
{
    const char *string = (const char *)vstring;
    return XTextWidth(xfi->xfs, string+start, length);
}

#ifdef DRAW_TEXT_GDK
static void x11font_gdk_setup(unifont_drawctx *ctx, x11font_individual *xfi)
{
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    XSetFont(disp, GDK_GC_XGC(ctx->u.gdk.gc), xfi->xfs->fid);
}

static void x11font_gdk_draw_16(unifont_drawctx *ctx,
                                x11font_individual *xfi, int x, int y,
                                const void *vstring, int start, int length)
{
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    const XChar2b *string = (const XChar2b *)vstring;
    XDrawString16(disp, GDK_DRAWABLE_XID(ctx->u.gdk.target),
                  GDK_GC_XGC(ctx->u.gdk.gc), x, y, string+start, length);
}

static void x11font_gdk_draw_8(unifont_drawctx *ctx,
                               x11font_individual *xfi, int x, int y,
                               const void *vstring, int start, int length)
{
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    const char *string = (const char *)vstring;
    XDrawString(disp, GDK_DRAWABLE_XID(ctx->u.gdk.target),
                GDK_GC_XGC(ctx->u.gdk.gc), x, y, string+start, length);
}
#endif

#ifdef DRAW_TEXT_CAIRO
static void x11font_cairo_setup(unifont_drawctx *ctx, x11font_individual *xfi)
{
    if (xfi->pixmap == None) {
        Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
        XGCValues gcvals;
        GdkWindow *widgetwin = gtk_widget_get_window(ctx->u.cairo.widget);
        int widgetscr = GDK_SCREEN_XNUMBER(gdk_window_get_screen(widgetwin));

        xfi->pixwidth =
            xfi->xfs->max_bounds.rbearing - xfi->xfs->min_bounds.lbearing;
        xfi->pixheight =
            xfi->xfs->max_bounds.ascent + xfi->xfs->max_bounds.descent;
        xfi->pixoriginx = -xfi->xfs->min_bounds.lbearing;
        xfi->pixoriginy = xfi->xfs->max_bounds.ascent;

        xfi->rowsize = cairo_format_stride_for_width(CAIRO_FORMAT_A1,
                                                     xfi->pixwidth);
        xfi->allsize = xfi->rowsize * xfi->pixheight;

        {
            /*
             * Test host endianness and use it to set xfi->indexflip,
             * which is XORed into our left-shift counts in order to
             * implement the CAIRO_FORMAT_A1 specification, in which
             * each bitmap byte is oriented LSB-first on little-endian
             * platforms and MSB-first on big-endian ones.
             *
             * This is the same technique Cairo itself uses to test
             * endianness, so hopefully it'll work in any situation
             * where Cairo is usable at all.
             */
            static const int endianness_test = 1;
            xfi->indexflip = (*((char *) &endianness_test) == 1) ? 0 : 7;
        }

        xfi->pixmap = XCreatePixmap
            (disp,
             GDK_DRAWABLE_XID(gtk_widget_get_window(ctx->u.cairo.widget)),
             xfi->pixwidth, xfi->pixheight, 1);
        gcvals.foreground = WhitePixel(disp, widgetscr);
        gcvals.background = BlackPixel(disp, widgetscr);
        gcvals.font = xfi->xfs->fid;
        xfi->gc = XCreateGC(disp, xfi->pixmap,
                            GCForeground | GCBackground | GCFont, &gcvals);
    }
}

static void x11font_cairo_cache_glyph(x11font_individual *xfi, int glyphindex)
{
    XImage *image;
    int x, y;
    unsigned char *bitmap;
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    const XCharStruct *xcs = x11_char_struct(xfi->xfs, glyphindex >> 8,
                                             glyphindex & 0xFF);

    bitmap = snewn(xfi->allsize, unsigned char);
    memset(bitmap, 0, xfi->allsize);

    image = XGetImage(disp, xfi->pixmap, 0, 0,
                      xfi->pixwidth, xfi->pixheight, AllPlanes, XYPixmap);
    for (y = xfi->pixoriginy - xcs->ascent;
         y < xfi->pixoriginy + xcs->descent; y++) {
        for (x = xfi->pixoriginx + xcs->lbearing;
             x < xfi->pixoriginx + xcs->rbearing; x++) {
            unsigned long pixel = XGetPixel(image, x, y);
            if (pixel) {
                int byteindex = y * xfi->rowsize + x/8;
                int bitindex = (x & 7) ^ xfi->indexflip;
                bitmap[byteindex] |= 1U << bitindex;
            }
        }
    }
    XDestroyImage(image);

    if (xfi->nglyphs <= glyphindex) {
        /* Round up to the next multiple of 256 on the general
         * principle that Unicode characters come in contiguous blocks
         * often used together */
        int old_nglyphs = xfi->nglyphs;
        xfi->nglyphs = (glyphindex + 0x100) & ~0xFF;
        xfi->glyphcache = sresize(xfi->glyphcache, xfi->nglyphs,
                                  struct cairo_cached_glyph);

        while (old_nglyphs < xfi->nglyphs) {
            xfi->glyphcache[old_nglyphs].surface = NULL;
            xfi->glyphcache[old_nglyphs].bitmap = NULL;
            old_nglyphs++;
        }
    }
    xfi->glyphcache[glyphindex].bitmap = bitmap;
    xfi->glyphcache[glyphindex].surface = cairo_image_surface_create_for_data
        (bitmap, CAIRO_FORMAT_A1, xfi->pixwidth, xfi->pixheight, xfi->rowsize);
}

static void x11font_cairo_draw_glyph(unifont_drawctx *ctx,
                                     x11font_individual *xfi, int x, int y,
                                     int glyphindex)
{
    if (xfi->glyphcache[glyphindex].surface) {
        cairo_mask_surface(ctx->u.cairo.cr,
                           xfi->glyphcache[glyphindex].surface,
                           x - xfi->pixoriginx, y - xfi->pixoriginy);
    }
}

static void x11font_cairo_draw_16(unifont_drawctx *ctx,
                                  x11font_individual *xfi, int x, int y,
                                  const void *vstring, int start, int length)
{
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    const XChar2b *string = (const XChar2b *)vstring + start;
    int i;
    for (i = 0; i < length; i++) {
        if (x11_font_has_glyph(xfi->xfs, string[i].byte1, string[i].byte2)) {
            int glyphindex = (256 * (unsigned char)string[i].byte1 +
                              (unsigned char)string[i].byte2);
            if (glyphindex >= xfi->nglyphs ||
                !xfi->glyphcache[glyphindex].surface) {
                XDrawImageString16(disp, xfi->pixmap, xfi->gc,
                                   xfi->pixoriginx, xfi->pixoriginy,
                                   string+i, 1);
                x11font_cairo_cache_glyph(xfi, glyphindex);
            }
            x11font_cairo_draw_glyph(ctx, xfi, x, y, glyphindex);
            x += XTextWidth16(xfi->xfs, string+i, 1);
        }
    }
}

static void x11font_cairo_draw_8(unifont_drawctx *ctx,
                                 x11font_individual *xfi, int x, int y,
                                 const void *vstring, int start, int length)
{
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    const char *string = (const char *)vstring + start;
    int i;
    for (i = 0; i < length; i++) {
        if (x11_font_has_glyph(xfi->xfs, 0, string[i])) {
            int glyphindex = (unsigned char)string[i];
            if (glyphindex >= xfi->nglyphs ||
                !xfi->glyphcache[glyphindex].surface) {
                XDrawImageString(disp, xfi->pixmap, xfi->gc,
                                 xfi->pixoriginx, xfi->pixoriginy,
                                 string+i, 1);
                x11font_cairo_cache_glyph(xfi, glyphindex);
            }
            x11font_cairo_draw_glyph(ctx, xfi, x, y, glyphindex);
            x += XTextWidth(xfi->xfs, string+i, 1);
        }
    }
}
#endif /* DRAW_TEXT_CAIRO */

struct x11font_drawfuncs {
    int (*width)(unifont_drawctx *ctx, x11font_individual *xfi,
                 const void *vstring, int start, int length);
    void (*setup)(unifont_drawctx *ctx, x11font_individual *xfi);
    void (*draw)(unifont_drawctx *ctx, x11font_individual *xfi, int x, int y,
                 const void *vstring, int start, int length);
};

/*
 * This array has two entries per compiled-in drawtype; of each pair,
 * the first is for an 8-bit font and the second for 16-bit.
 */
static const struct x11font_drawfuncs x11font_drawfuncs[2*DRAWTYPE_NTYPES] = {
#ifdef DRAW_TEXT_GDK
    /* gdk, 8-bit */
    {
        x11font_width_8,
        x11font_gdk_setup,
        x11font_gdk_draw_8,
    },
    /* gdk, 16-bit */
    {
        x11font_width_16,
        x11font_gdk_setup,
        x11font_gdk_draw_16,
    },
#endif
#ifdef DRAW_TEXT_CAIRO
    /* cairo, 8-bit */
    {
        x11font_width_8,
        x11font_cairo_setup,
        x11font_cairo_draw_8,
    },
    /* [3] cairo, 16-bit */
    {
        x11font_width_16,
        x11font_cairo_setup,
        x11font_cairo_draw_16,
    },
#endif
};

static void x11font_really_draw_text(const struct x11font_drawfuncs *dfns,
                                     unifont_drawctx *ctx,
                                     x11font_individual *xfi, int x, int y,
                                     const void *string, int nchars,
                                     int shadowoffset,
                                     int fontvariable, int cellwidth)
{
    int start = 0, step, nsteps, centre;

    if (fontvariable) {
	/*
	 * In a variable-pitch font, we draw one character at a
	 * time, and centre it in the character cell.
	 */
	step = 1;
	nsteps = nchars;
	centre = TRUE;
    } else {
        /*
         * In a fixed-pitch font, we can draw the whole lot in one go.
         */
        step = nchars;
        nsteps = 1;
        centre = FALSE;
    }

    dfns->setup(ctx, xfi);

    while (nsteps-- > 0) {
	int X = x;
	if (centre)
	    X += (cellwidth - dfns->width(ctx, xfi, string, start, step)) / 2;

        dfns->draw(ctx, xfi, X, y, string, start, step);
	if (shadowoffset)
            dfns->draw(ctx, xfi, X + shadowoffset, y, string, start, step);

	x += cellwidth;
        start += step;
    }
}

static void x11font_draw_text(unifont_drawctx *ctx, unifont *font,
			      int x, int y, const wchar_t *string, int len,
			      int wide, int bold, int cellwidth)
{
    struct x11font *xfont = (struct x11font *)font;
    int sfid;
    int shadowoffset = 0;
    int mult = (wide ? 2 : 1);
    int index = 2 * (int)ctx->type;

    wide -= xfont->wide;
    bold -= xfont->bold;

    /*
     * Decide which subfont we're using, and whether we have to
     * use shadow bold.
     */
    if (xfont->shadowalways && bold) {
	shadowoffset = xfont->shadowoffset;
	bold = 0;
    }
    sfid = 2 * wide + bold;
    if (!xfont->fonts[sfid].allocated)
	x11_alloc_subfont(xfont, sfid);
    if (bold && !xfont->fonts[sfid].xfs) {
	bold = 0;
	shadowoffset = xfont->shadowoffset;
	sfid = 2 * wide + bold;
	if (!xfont->fonts[sfid].allocated)
	    x11_alloc_subfont(xfont, sfid);
    }

    if (!xfont->fonts[sfid].xfs)
	return;			       /* we've tried our best, but no luck */

    if (xfont->sixteen_bit) {
	/*
	 * This X font has 16-bit character indices, which means
	 * we can directly use our Unicode input string.
	 */
	XChar2b *xcs;
	int i;

	xcs = snewn(len, XChar2b);
	for (i = 0; i < len; i++) {
	    xcs[i].byte1 = string[i] >> 8;
	    xcs[i].byte2 = string[i];
	}

	x11font_really_draw_text(x11font_drawfuncs + index + 1, ctx,
                                 &xfont->fonts[sfid], x, y,
                                 xcs, len, shadowoffset,
                                 xfont->variable, cellwidth * mult);
	sfree(xcs);
    } else {
        /*
         * This X font has 8-bit indices, so we must convert to the
         * appropriate character set.
         */
        char *sbstring = snewn(len+1, char);
        int sblen = wc_to_mb(xfont->real_charset, 0, string, len,
                             sbstring, len+1, ".", NULL, NULL);
	x11font_really_draw_text(x11font_drawfuncs + index + 0, ctx,
                                 &xfont->fonts[sfid], x, y,
				 sbstring, sblen, shadowoffset,
				 xfont->variable, cellwidth * mult);
        sfree(sbstring);
    }
}

static void x11font_draw_combining(unifont_drawctx *ctx, unifont *font,
                                   int x, int y, const wchar_t *string,
                                   int len, int wide, int bold, int cellwidth)
{
    /*
     * For server-side fonts, there's no sophisticated system for
     * combining characters intelligently, so the best we can do is to
     * overprint them on each other in the obvious way.
     */
    int i;
    for (i = 0; i < len; i++)
        x11font_draw_text(ctx, font, x, y, string+i, 1, wide, bold, cellwidth);
}

static void x11font_enum_fonts(GtkWidget *widget,
			       fontsel_add_entry callback, void *callback_ctx)
{
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    char **fontnames;
    char *tmp = NULL;
    int nnames, i, max, tmpsize;

    max = 32768;
    while (1) {
	fontnames = XListFonts(disp, "*", max, &nnames);
	if (nnames >= max) {
	    XFreeFontNames(fontnames);
	    max *= 2;
	} else
	    break;
    }

    tmpsize = 0;

    for (i = 0; i < nnames; i++) {
        struct xlfd_decomposed *xlfd = xlfd_decompose(fontnames[i]);
	if (xlfd) {
            char *p, *font, *style, *stylekey, *charset;
            int weightkey, slantkey, setwidthkey;
            int thistmpsize;

	    /*
	     * Convert a dismembered XLFD into the format we'll be
	     * using in the font selector.
	     */

            thistmpsize = 4 * strlen(fontnames[i]) + 256;
            if (tmpsize < thistmpsize) {
                tmpsize = thistmpsize;
                tmp = sresize(tmp, tmpsize, char);
            }
	    p = tmp;

	    /*
	     * Font name is in the form "family (foundry)". (This is
	     * what the GTK 1.2 X font selector does, and it seems to
	     * come out looking reasonably sensible.)
	     */
	    font = p;
	    p += 1 + sprintf(p, "%s (%s)", xlfd->family_name, xlfd->foundry);

	    /*
	     * Character set.
	     */
	    charset = p;
	    p += 1 + sprintf(p, "%s-%s", xlfd->charset_registry,
                             xlfd->charset_encoding);

	    /*
	     * Style is a mixture of quite a lot of the fields,
	     * with some strange formatting.
	     */
	    style = p;
	    p += sprintf(p, "%s", xlfd->weight_name[0] ? xlfd->weight_name :
			 "regular");
	    if (!g_ascii_strcasecmp(xlfd->slant, "i"))
		p += sprintf(p, " italic");
	    else if (!g_ascii_strcasecmp(xlfd->slant, "o"))
		p += sprintf(p, " oblique");
	    else if (!g_ascii_strcasecmp(xlfd->slant, "ri"))
		p += sprintf(p, " reverse italic");
	    else if (!g_ascii_strcasecmp(xlfd->slant, "ro"))
		p += sprintf(p, " reverse oblique");
	    else if (!g_ascii_strcasecmp(xlfd->slant, "ot"))
		p += sprintf(p, " other-slant");
	    if (xlfd->setwidth_name[0] &&
                g_ascii_strcasecmp(xlfd->setwidth_name, "normal"))
		p += sprintf(p, " %s", xlfd->setwidth_name);
	    if (!g_ascii_strcasecmp(xlfd->spacing, "m"))
		p += sprintf(p, " [M]");
	    if (!g_ascii_strcasecmp(xlfd->spacing, "c"))
		p += sprintf(p, " [C]");
	    if (xlfd->add_style_name[0])
		p += sprintf(p, " %s", xlfd->add_style_name);

	    /*
	     * Style key is the same stuff as above, but with a
	     * couple of transformations done on it to make it
	     * sort more sensibly.
	     */
	    p++;
	    stylekey = p;
	    if (!g_ascii_strcasecmp(xlfd->weight_name, "medium") ||
		!g_ascii_strcasecmp(xlfd->weight_name, "regular") ||
		!g_ascii_strcasecmp(xlfd->weight_name, "normal") ||
		!g_ascii_strcasecmp(xlfd->weight_name, "book"))
		weightkey = 0;
	    else if (!g_ascii_strncasecmp(xlfd->weight_name, "demi", 4) ||
		     !g_ascii_strncasecmp(xlfd->weight_name, "semi", 4))
		weightkey = 1;
	    else
		weightkey = 2;
	    if (!g_ascii_strcasecmp(xlfd->slant, "r"))
		slantkey = 0;
	    else if (!g_ascii_strncasecmp(xlfd->slant, "r", 1))
		slantkey = 2;
	    else
		slantkey = 1;
	    if (!g_ascii_strcasecmp(xlfd->setwidth_name, "normal"))
		setwidthkey = 0;
	    else
		setwidthkey = 1;

	    p += sprintf(
                p, "%04d%04d%s%04d%04d%s%04d%04d%s%04d%s%04d%s",
                weightkey, (int)strlen(xlfd->weight_name), xlfd->weight_name,
                slantkey, (int)strlen(xlfd->slant), xlfd->slant,
                setwidthkey,
                (int)strlen(xlfd->setwidth_name), xlfd->setwidth_name,
                (int)strlen(xlfd->spacing), xlfd->spacing,
                (int)strlen(xlfd->add_style_name), xlfd->add_style_name);

	    assert(p - tmp < thistmpsize);

	    /*
	     * Flags: we need to know whether this is a monospaced
	     * font, which we do by examining the spacing field
	     * again.
	     */
	    flags = FONTFLAG_SERVERSIDE;
	    if (!strchr("CcMm", xlfd->spacing[0]))
		flags |= FONTFLAG_NONMONOSPACED;

	    /*
	     * Some fonts have a pixel size of zero, meaning they're
	     * treated as scalable. For these purposes, we only want
	     * fonts whose pixel size we actually know, so filter
	     * those out.
	     */
	    if (xlfd->pixel_size)
                callback(callback_ctx, fontnames[i], font, charset,
                         style, stylekey, xlfd->pixel_size, flags,
                         &x11font_vtable);

            sfree(xlfd);
	} else {
	    /*
	     * This isn't an XLFD, so it must be an alias.
	     * Transmit it with mostly null data.
	     * 
	     * It would be nice to work out if it's monospaced
	     * here, but at the moment I can't see that being
	     * anything but computationally hideous. Ah well.
	     */
	    callback(callback_ctx, fontnames[i], fontnames[i], NULL,
		     NULL, NULL, 0, FONTFLAG_SERVERALIAS, &x11font_vtable);

            sfree(xlfd);
	}
    }
    XFreeFontNames(fontnames);
    sfree(tmp);
}

static char *x11font_canonify_fontname(GtkWidget *widget, const char *name,
				       int *size, int *flags,
				       int resolve_aliases)
{
    /*
     * When given an X11 font name to try to make sense of for a
     * font selector, we must attempt to load it (to see if it
     * exists), and then canonify it by extracting its FONT
     * property, which should give its full XLFD even if what we
     * originally had was a wildcard.
     * 
     * However, we must carefully avoid canonifying font
     * _aliases_, unless specifically asked to, because the font
     * selector treats them as worthwhile in their own right.
     */
    XFontStruct *xfs;
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Atom fontprop, fontprop2;
    unsigned long ret;

    xfs = XLoadQueryFont(disp, name);

    if (!xfs)
	return NULL;		       /* didn't make sense to us, sorry */

    fontprop = XInternAtom(disp, "FONT", False);

    if (XGetFontProperty(xfs, fontprop, &ret)) {
	char *newname = XGetAtomName(disp, (Atom)ret);
	if (newname) {
	    unsigned long fsize = 12;

	    fontprop2 = XInternAtom(disp, "PIXEL_SIZE", False);
	    if (XGetFontProperty(xfs, fontprop2, &fsize) && fsize > 0) {
		*size = fsize;
                XFreeFont(disp, xfs);
		if (flags) {
		    if (name[0] == '-' || resolve_aliases)
			*flags = FONTFLAG_SERVERSIDE;
		    else
			*flags = FONTFLAG_SERVERALIAS;
		}
		return dupstr(name[0] == '-' || resolve_aliases ?
			      newname : name);
	    }
	}
    }

    XFreeFont(disp, xfs);

    return NULL;		       /* something went wrong */
}

static char *x11font_scale_fontname(GtkWidget *widget, const char *name,
				    int size)
{
    return NULL;		       /* shan't */
}

static char *x11font_size_increment(unifont *font, int increment)
{
    struct x11font *xfont = (struct x11font *)font;
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Atom fontprop = XInternAtom(disp, "FONT", False);
    char *returned_name = NULL;
    unsigned long ret;
    if (XGetFontProperty(xfont->fonts[0].xfs, fontprop, &ret)) {
        struct xlfd_decomposed *xlfd;
        struct xlfd_decomposed *xlfd_best;
        char *wc;
        char **fontnames;
        int nnames, i, max;

        xlfd = xlfd_decompose(XGetAtomName(disp, (Atom)ret));
        if (!xlfd)
            return NULL;

        /*
         * Form a wildcard consisting of everything in the
         * original XLFD except for the size-related fields.
         */
        {
            struct xlfd_decomposed xlfd_wc = *xlfd; /* structure copy */
            xlfd_wc.pixel_size = XLFD_INT_WILDCARD;
            xlfd_wc.point_size = XLFD_INT_WILDCARD;
            xlfd_wc.average_width = XLFD_INT_WILDCARD;
            wc = xlfd_recompose(&xlfd_wc);
        }

        /*
         * Fetch all the font names matching that wildcard.
         */
        max = 32768;
        while (1) {
            fontnames = XListFonts(disp, wc, max, &nnames);
            if (nnames >= max) {
                XFreeFontNames(fontnames);
                max *= 2;
            } else
                break;
        }

        sfree(wc);

        /*
         * Iterate over those to find the one closest in size to the
         * original font, in the correct direction.
         */

#define FLIPPED_SIZE(xlfd) \
        (((xlfd)->pixel_size + (xlfd)->point_size) * \
         (increment < 0 ? -1 : +1))

        xlfd_best = NULL;
        for (i = 0; i < nnames; i++) {
            struct xlfd_decomposed *xlfd2 = xlfd_decompose(fontnames[i]);
            if (!xlfd2)
                continue;

            if (xlfd2->pixel_size != 0 &&
                FLIPPED_SIZE(xlfd2) > FLIPPED_SIZE(xlfd) &&
                (!xlfd_best || FLIPPED_SIZE(xlfd2)<FLIPPED_SIZE(xlfd_best))) {
                sfree(xlfd_best);
                xlfd_best = xlfd2;
                xlfd2 = NULL;
            }

            sfree(xlfd2);
        }

#undef FLIPPED_SIZE

        if (xlfd_best) {
            char *bare_returned_name = xlfd_recompose(xlfd_best);
            returned_name = dupcat(
                xfont->u.vt->prefix, ":", bare_returned_name,
                (const char *)NULL);
            sfree(bare_returned_name);
        }

        XFreeFontNames(fontnames);
        sfree(xlfd);
        sfree(xlfd_best);
    }
    return returned_name;
}

#endif /* NOT_X_WINDOWS */

#if GTK_CHECK_VERSION(2,0,0)

/* ----------------------------------------------------------------------
 * Pango font implementation (for GTK 2 only).
 */

#if defined PANGO_PRE_1POINT4 && !defined PANGO_PRE_1POINT6
#define PANGO_PRE_1POINT6	       /* make life easier for pre-1.4 folk */
#endif

static int pangofont_has_glyph(unifont *font, wchar_t glyph);
static void pangofont_draw_text(unifont_drawctx *ctx, unifont *font,
                                int x, int y, const wchar_t *string, int len,
                                int wide, int bold, int cellwidth);
static void pangofont_draw_combining(unifont_drawctx *ctx, unifont *font,
                                     int x, int y, const wchar_t *string,
                                     int len, int wide, int bold,
                                     int cellwidth);
static unifont *pangofont_create(GtkWidget *widget, const char *name,
				 int wide, int bold,
				 int shadowoffset, int shadowalways);
static unifont *pangofont_create_fallback(GtkWidget *widget, int height,
                                          int wide, int bold,
                                          int shadowoffset, int shadowalways);
static void pangofont_destroy(unifont *font);
static void pangofont_enum_fonts(GtkWidget *widget, fontsel_add_entry callback,
				 void *callback_ctx);
static char *pangofont_canonify_fontname(GtkWidget *widget, const char *name,
					 int *size, int *flags,
					 int resolve_aliases);
static char *pangofont_scale_fontname(GtkWidget *widget, const char *name,
				      int size);
static char *pangofont_size_increment(unifont *font, int increment);

struct pangofont {
    struct unifont u;
    /*
     * Pango objects.
     */
    PangoFontDescription *desc;
    PangoFontset *fset;
    /*
     * The containing widget.
     */
    GtkWidget *widget;
    /*
     * Data passed in to unifont_create().
     */
    int bold, shadowoffset, shadowalways;
    /*
     * Cache of character widths, indexed by Unicode code point. In
     * pixels; -1 means we haven't asked Pango about this character
     * before.
     */
    int *widthcache;
    unsigned nwidthcache;
};

static const struct unifont_vtable pangofont_vtable = {
    pangofont_create,
    pangofont_create_fallback,
    pangofont_destroy,
    pangofont_has_glyph,
    pangofont_draw_text,
    pangofont_draw_combining,
    pangofont_enum_fonts,
    pangofont_canonify_fontname,
    pangofont_scale_fontname,
    pangofont_size_increment,
    "client",
};

/*
 * This function is used to rigorously validate a
 * PangoFontDescription. Later versions of Pango have a nasty
 * habit of accepting _any_ old string as input to
 * pango_font_description_from_string and returning a font
 * description which can actually be used to display text, even if
 * they have to do it by falling back to their most default font.
 * This is doubtless helpful in some situations, but not here,
 * because we need to know if a Pango font string actually _makes
 * sense_ in order to fall back to treating it as an X font name
 * if it doesn't. So we check that the font family is actually one
 * supported by Pango.
 */
static int pangofont_check_desc_makes_sense(PangoContext *ctx,
					    PangoFontDescription *desc)
{
#ifndef PANGO_PRE_1POINT6
    PangoFontMap *map;
#endif
    PangoFontFamily **families;
    int i, nfamilies, matched;

    /*
     * Ask Pango for a list of font families, and iterate through
     * them to see if one of them matches the family in the
     * PangoFontDescription.
     */
#ifndef PANGO_PRE_1POINT6
    map = pango_context_get_font_map(ctx);
    if (!map)
	return FALSE;
    pango_font_map_list_families(map, &families, &nfamilies);
#else
    pango_context_list_families(ctx, &families, &nfamilies);
#endif

    matched = FALSE;
    for (i = 0; i < nfamilies; i++) {
	if (!g_ascii_strcasecmp(pango_font_family_get_name(families[i]),
				pango_font_description_get_family(desc))) {
	    matched = TRUE;
	    break;
	}
    }
    g_free(families);

    return matched;
}

static unifont *pangofont_create_internal(GtkWidget *widget,
                                          PangoContext *ctx,
                                          PangoFontDescription *desc,
                                          int wide, int bold,
                                          int shadowoffset, int shadowalways)
{
    struct pangofont *pfont;
#ifndef PANGO_PRE_1POINT6
    PangoFontMap *map;
#endif
    PangoFontset *fset;
    PangoFontMetrics *metrics;

#ifndef PANGO_PRE_1POINT6
    map = pango_context_get_font_map(ctx);
    if (!map) {
	pango_font_description_free(desc);
	return NULL;
    }
    fset = pango_font_map_load_fontset(map, ctx, desc,
				       pango_context_get_language(ctx));
#else
    fset = pango_context_load_fontset(ctx, desc,
                                      pango_context_get_language(ctx));
#endif
    if (!fset) {
	pango_font_description_free(desc);
	return NULL;
    }
    metrics = pango_fontset_get_metrics(fset);
    if (!metrics ||
	pango_font_metrics_get_approximate_digit_width(metrics) == 0) {
	pango_font_description_free(desc);
	g_object_unref(fset);
	return NULL;
    }

    pfont = snew(struct pangofont);
    pfont->u.vt = &pangofont_vtable;
    pfont->u.width =
	PANGO_PIXELS(pango_font_metrics_get_approximate_digit_width(metrics));
    pfont->u.ascent = PANGO_PIXELS(pango_font_metrics_get_ascent(metrics));
    pfont->u.descent = PANGO_PIXELS(pango_font_metrics_get_descent(metrics));
    pfont->u.height = pfont->u.ascent + pfont->u.descent;
    pfont->u.want_fallback = FALSE;
#ifdef DRAW_TEXT_CAIRO
    pfont->u.preferred_drawtype = DRAWTYPE_CAIRO;
#elif defined DRAW_TEXT_GDK
    pfont->u.preferred_drawtype = DRAWTYPE_GDK;
#else
#error No drawtype available at all
#endif
    /* The Pango API is hardwired to UTF-8 */
    pfont->u.public_charset = CS_UTF8;
    pfont->desc = desc;
    pfont->fset = fset;
    pfont->widget = widget;
    pfont->bold = bold;
    pfont->shadowoffset = shadowoffset;
    pfont->shadowalways = shadowalways;
    pfont->widthcache = NULL;
    pfont->nwidthcache = 0;

    pango_font_metrics_unref(metrics);

    return (unifont *)pfont;
}

static unifont *pangofont_create(GtkWidget *widget, const char *name,
				 int wide, int bold,
				 int shadowoffset, int shadowalways)
{
    PangoContext *ctx;
    PangoFontDescription *desc;

    desc = pango_font_description_from_string(name);
    if (!desc)
	return NULL;
    ctx = gtk_widget_get_pango_context(widget);
    if (!ctx) {
	pango_font_description_free(desc);
	return NULL;
    }
    if (!pangofont_check_desc_makes_sense(ctx, desc)) {
	pango_font_description_free(desc);
	return NULL;
    }
    return pangofont_create_internal(widget, ctx, desc, wide, bold,
                                     shadowoffset, shadowalways);
}

static unifont *pangofont_create_fallback(GtkWidget *widget, int height,
                                          int wide, int bold,
                                          int shadowoffset, int shadowalways)
{
    PangoContext *ctx;
    PangoFontDescription *desc;

    desc = pango_font_description_from_string("Monospace");
    if (!desc)
	return NULL;
    ctx = gtk_widget_get_pango_context(widget);
    if (!ctx) {
	pango_font_description_free(desc);
	return NULL;
    }
    pango_font_description_set_absolute_size(desc, height * PANGO_SCALE);
    return pangofont_create_internal(widget, ctx, desc, wide, bold,
                                     shadowoffset, shadowalways);
}

static void pangofont_destroy(unifont *font)
{
    struct pangofont *pfont = (struct pangofont *)font;
    pango_font_description_free(pfont->desc);
    sfree(pfont->widthcache);
    g_object_unref(pfont->fset);
    sfree(font);
}

static int pangofont_char_width(PangoLayout *layout, struct pangofont *pfont,
                                wchar_t uchr, const char *utfchr, int utflen)
{
    /*
     * Here we check whether a character has the same width as the
     * character cell it'll be drawn in. Because profiling showed that
     * asking Pango for text sizes was a huge bottleneck when we were
     * calling it every time we needed to know this, we instead call
     * it only on characters we don't already know about, and cache
     * the results.
     */

    if ((unsigned)uchr >= pfont->nwidthcache) {
        unsigned newsize = ((int)uchr + 0x100) & ~0xFF;
        pfont->widthcache = sresize(pfont->widthcache, newsize, int);
        while (pfont->nwidthcache < newsize)
            pfont->widthcache[pfont->nwidthcache++] = -1;
    }

    if (pfont->widthcache[uchr] < 0) {
        PangoRectangle rect;
        pango_layout_set_text(layout, utfchr, utflen);
        pango_layout_get_extents(layout, NULL, &rect);
        pfont->widthcache[uchr] = rect.width;
    }

    return pfont->widthcache[uchr];
}

static int pangofont_has_glyph(unifont *font, wchar_t glyph)
{
    /* Pango implements font fallback, so assume it has everything */
    return TRUE;
}

#ifdef DRAW_TEXT_GDK
static void pango_gdk_draw_layout(unifont_drawctx *ctx,
                                  gint x, gint y, PangoLayout *layout)
{
    gdk_draw_layout(ctx->u.gdk.target, ctx->u.gdk.gc, x, y, layout);
}
#endif

#ifdef DRAW_TEXT_CAIRO
static void pango_cairo_draw_layout(unifont_drawctx *ctx,
                                    gint x, gint y, PangoLayout *layout)
{
    cairo_move_to(ctx->u.cairo.cr, x, y);
    pango_cairo_show_layout(ctx->u.cairo.cr, layout);
}
#endif

static void pangofont_draw_internal(unifont_drawctx *ctx, unifont *font,
                                    int x, int y, const wchar_t *string,
                                    int len, int wide, int bold, int cellwidth,
                                    int combining)
{
    struct pangofont *pfont = (struct pangofont *)font;
    PangoLayout *layout;
    PangoRectangle rect;
    char *utfstring, *utfptr;
    int utflen;
    int shadowbold = FALSE;
    void (*draw_layout)(unifont_drawctx *ctx,
                        gint x, gint y, PangoLayout *layout) = NULL;

#ifdef DRAW_TEXT_GDK
    if (ctx->type == DRAWTYPE_GDK) {
        draw_layout = pango_gdk_draw_layout;
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (ctx->type == DRAWTYPE_CAIRO) {
        draw_layout = pango_cairo_draw_layout;
    }
#endif

    if (wide)
	cellwidth *= 2;

    y -= pfont->u.ascent;

    layout = pango_layout_new(gtk_widget_get_pango_context(pfont->widget));
    pango_layout_set_font_description(layout, pfont->desc);
    if (bold > pfont->bold) {
	if (pfont->shadowalways)
	    shadowbold = TRUE;
	else {
	    PangoFontDescription *desc2 =
		pango_font_description_copy_static(pfont->desc);
	    pango_font_description_set_weight(desc2, PANGO_WEIGHT_BOLD);
	    pango_layout_set_font_description(layout, desc2);
	}
    }

    /*
     * Pango always expects UTF-8, so convert the input wide character
     * string to UTF-8.
     */
    utfstring = snewn(len*6+1, char); /* UTF-8 has max 6 bytes/char */
    utflen = wc_to_mb(CS_UTF8, 0, string, len,
                      utfstring, len*6+1, ".", NULL, NULL);

    utfptr = utfstring;
    while (utflen > 0) {
	int clen, n;
        int desired = cellwidth * PANGO_SCALE;

	/*
	 * We want to display every character from this string in
	 * the centre of its own character cell. In the worst case,
	 * this requires a separate text-drawing call for each
	 * character; but in the common case where the font is
	 * properly fixed-width, we can draw many characters in one
	 * go which is much faster.
	 *
	 * This still isn't really ideal. If you look at what
	 * happens in the X protocol as a result of all of this, you
	 * find - naturally enough - that each call to
	 * gdk_draw_layout() generates a separate set of X RENDER
	 * operations involving creating a picture, setting a clip
	 * rectangle, doing some drawing and undoing the whole lot.
	 * In an ideal world, we should _always_ be able to turn the
	 * contents of this loop into a single RenderCompositeGlyphs
	 * operation which internally specifies inter-character
	 * deltas to get the spacing right, which would give us full
	 * speed _even_ in the worst case of a non-fixed-width font.
	 * However, Pango's architecture and documentation are so
	 * unhelpful that I have no idea how if at all to persuade
	 * them to do that.
	 */

        if (combining) {
            /*
             * For a character with combining stuff, we just dump the
             * whole lot in one go, and expect it to take up just one
             * character cell.
             */
            clen = utflen;
            n = 1;
        } else {
            /*
             * Start by extracting a single UTF-8 character from the
             * string.
             */
            clen = 1;
            while (clen < utflen &&
                   (unsigned char)utfptr[clen] >= 0x80 &&
                   (unsigned char)utfptr[clen] < 0xC0)
                clen++;
            n = 1;

            if (is_rtl(string[0]) ||
                pangofont_char_width(layout, pfont, string[n-1],
                                     utfptr, clen) != desired) {
                /*
                 * If this character is a right-to-left one, or has an
                 * unusual width, then we must display it on its own.
                 */
            } else {
                /*
                 * Try to amalgamate a contiguous string of characters
                 * with the expected sensible width, for the common case
                 * in which we're using a monospaced font and everything
                 * works as expected.
                 */
                while (clen < utflen) {
                    int oldclen = clen;
                    clen++;		       /* skip UTF-8 introducer byte */
                    while (clen < utflen &&
                           (unsigned char)utfptr[clen] >= 0x80 &&
                           (unsigned char)utfptr[clen] < 0xC0)
                        clen++;
                    n++;
                    if (is_rtl(string[n-1]) ||
                        pangofont_char_width(layout, pfont,
                                             string[n-1], utfptr + oldclen,
                                             clen - oldclen) != desired) {
                        clen = oldclen;
                        n--;
                        break;
                    }
                }
            }
        }

	pango_layout_set_text(layout, utfptr, clen);
	pango_layout_get_pixel_extents(layout, NULL, &rect);
        
	draw_layout(ctx,
                    x + (n*cellwidth - rect.width)/2,
                    y + (pfont->u.height - rect.height)/2, layout);
	if (shadowbold)
	    draw_layout(ctx,
                        x + (n*cellwidth - rect.width)/2 + pfont->shadowoffset,
                        y + (pfont->u.height - rect.height)/2, layout);

	utflen -= clen;
	utfptr += clen;
        string += n;
	x += n * cellwidth;
    }

    sfree(utfstring);

    g_object_unref(layout);
}

static void pangofont_draw_text(unifont_drawctx *ctx, unifont *font,
                                int x, int y, const wchar_t *string, int len,
                                int wide, int bold, int cellwidth)
{
    pangofont_draw_internal(ctx, font, x, y, string, len, wide, bold,
                            cellwidth, FALSE);
}

static void pangofont_draw_combining(unifont_drawctx *ctx, unifont *font,
                                     int x, int y, const wchar_t *string,
                                     int len, int wide, int bold,
                                     int cellwidth)
{
    wchar_t *tmpstring = NULL;
    if (mk_wcwidth(string[0]) == 0) {
        /*
         * If we've been told to draw a sequence of _only_ combining
         * characters, prefix a space so that they have something to
         * combine with.
         */
        tmpstring = snewn(len+1, wchar_t);
        memcpy(tmpstring+1, string, len * sizeof(wchar_t));
        tmpstring[0] = L' ';
        string = tmpstring;
        len++;
    }
    pangofont_draw_internal(ctx, font, x, y, string, len, wide, bold,
                            cellwidth, TRUE);
    sfree(tmpstring);
}

/*
 * Dummy size value to be used when converting a
 * PangoFontDescription of a scalable font to a string for
 * internal use.
 */
#define PANGO_DUMMY_SIZE 12

static void pangofont_enum_fonts(GtkWidget *widget, fontsel_add_entry callback,
				 void *callback_ctx)
{
    PangoContext *ctx;
#ifndef PANGO_PRE_1POINT6
    PangoFontMap *map;
#endif
    PangoFontFamily **families;
    int i, nfamilies;

    ctx = gtk_widget_get_pango_context(widget);
    if (!ctx)
	return;

    /*
     * Ask Pango for a list of font families, and iterate through
     * them.
     */
#ifndef PANGO_PRE_1POINT6
    map = pango_context_get_font_map(ctx);
    if (!map)
	return;
    pango_font_map_list_families(map, &families, &nfamilies);
#else
    pango_context_list_families(ctx, &families, &nfamilies);
#endif
    for (i = 0; i < nfamilies; i++) {
	PangoFontFamily *family = families[i];
	const char *familyname;
	int flags;
	PangoFontFace **faces;
	int j, nfaces;

	/*
	 * Set up our flags for this font family, and get the name
	 * string.
	 */
	flags = FONTFLAG_CLIENTSIDE;
#ifndef PANGO_PRE_1POINT4
        /*
         * In very early versions of Pango, we can't tell
         * monospaced fonts from non-monospaced.
         */
	if (!pango_font_family_is_monospace(family))
	    flags |= FONTFLAG_NONMONOSPACED;
#endif
	familyname = pango_font_family_get_name(family);

	/*
	 * Go through the available font faces in this family.
	 */
	pango_font_family_list_faces(family, &faces, &nfaces);
	for (j = 0; j < nfaces; j++) {
	    PangoFontFace *face = faces[j];
	    PangoFontDescription *desc;
	    const char *facename;
	    int *sizes;
	    int k, nsizes, dummysize;

	    /*
	     * Get the face name string.
	     */
	    facename = pango_font_face_get_face_name(face);

	    /*
	     * Set up a font description with what we've got so
	     * far. We'll fill in the size field manually and then
	     * call pango_font_description_to_string() to give the
	     * full real name of the specific font.
	     */
	    desc = pango_font_face_describe(face);

	    /*
	     * See if this font has a list of specific sizes.
	     */
#ifndef PANGO_PRE_1POINT4
	    pango_font_face_list_sizes(face, &sizes, &nsizes);
#else
            /*
             * In early versions of Pango, that call wasn't
             * supported; we just have to assume everything is
             * scalable.
             */
            sizes = NULL;
#endif
	    if (!sizes) {
		/*
		 * Write a single entry with a dummy size.
		 */
		dummysize = PANGO_DUMMY_SIZE * PANGO_SCALE;
		sizes = &dummysize;
		nsizes = 1;
	    }

	    /*
	     * If so, go through them one by one.
	     */
	    for (k = 0; k < nsizes; k++) {
		char *fullname;
		char stylekey[128];

		pango_font_description_set_size(desc, sizes[k]);

		fullname = pango_font_description_to_string(desc);

		/*
		 * Construct the sorting key for font styles.
		 */
		{
		    char *p = stylekey;
		    int n;

		    n = pango_font_description_get_weight(desc);
		    /* Weight: normal, then lighter, then bolder */
		    if (n <= PANGO_WEIGHT_NORMAL)
			n = PANGO_WEIGHT_NORMAL - n;
		    p += sprintf(p, "%4d", n);

		    n = pango_font_description_get_style(desc);
		    p += sprintf(p, " %2d", n);

		    n = pango_font_description_get_stretch(desc);
		    /* Stretch: closer to normal sorts earlier */
		    n = 2 * abs(PANGO_STRETCH_NORMAL - n) +
			(n < PANGO_STRETCH_NORMAL);
		    p += sprintf(p, " %2d", n);

		    n = pango_font_description_get_variant(desc);
		    p += sprintf(p, " %2d", n);
		    
		}

		/*
		 * Got everything. Hand off to the callback.
		 * (The charset string is NULL, because only
		 * server-side X fonts use it.)
		 */
		callback(callback_ctx, fullname, familyname, NULL, facename,
			 stylekey,
			 (sizes == &dummysize ? 0 : PANGO_PIXELS(sizes[k])),
			 flags, &pangofont_vtable);

		g_free(fullname);
	    }
	    if (sizes != &dummysize)
		g_free(sizes);

	    pango_font_description_free(desc);
	}
	g_free(faces);
    }
    g_free(families);
}

static char *pangofont_canonify_fontname(GtkWidget *widget, const char *name,
					 int *size, int *flags,
					 int resolve_aliases)
{
    /*
     * When given a Pango font name to try to make sense of for a
     * font selector, we must normalise it to PANGO_DUMMY_SIZE and
     * extract its original size (in pixels) into the `size' field.
     */
    PangoContext *ctx;
#ifndef PANGO_PRE_1POINT6
    PangoFontMap *map;
#endif
    PangoFontDescription *desc;
    PangoFontset *fset;
    PangoFontMetrics *metrics;
    char *newname, *retname;

    desc = pango_font_description_from_string(name);
    if (!desc)
	return NULL;
    ctx = gtk_widget_get_pango_context(widget);
    if (!ctx) {
	pango_font_description_free(desc);
	return NULL;
    }
    if (!pangofont_check_desc_makes_sense(ctx, desc)) {
	pango_font_description_free(desc);
	return NULL;
    }
#ifndef PANGO_PRE_1POINT6
    map = pango_context_get_font_map(ctx);
    if (!map) {
	pango_font_description_free(desc);
	return NULL;
    }
    fset = pango_font_map_load_fontset(map, ctx, desc,
				       pango_context_get_language(ctx));
#else
    fset = pango_context_load_fontset(ctx, desc,
                                      pango_context_get_language(ctx));
#endif
    if (!fset) {
	pango_font_description_free(desc);
	return NULL;
    }
    metrics = pango_fontset_get_metrics(fset);
    if (!metrics ||
	pango_font_metrics_get_approximate_digit_width(metrics) == 0) {
	pango_font_description_free(desc);
	g_object_unref(fset);
	return NULL;
    }

    *size = PANGO_PIXELS(pango_font_description_get_size(desc));
    *flags = FONTFLAG_CLIENTSIDE;
    pango_font_description_set_size(desc, PANGO_DUMMY_SIZE * PANGO_SCALE);
    newname = pango_font_description_to_string(desc);
    retname = dupstr(newname);
    g_free(newname);

    pango_font_metrics_unref(metrics);
    pango_font_description_free(desc);
    g_object_unref(fset);

    return retname;
}

static char *pangofont_scale_fontname(GtkWidget *widget, const char *name,
				      int size)
{
    PangoFontDescription *desc;
    char *newname, *retname;

    desc = pango_font_description_from_string(name);
    if (!desc)
	return NULL;
    pango_font_description_set_size(desc, size * PANGO_SCALE);
    newname = pango_font_description_to_string(desc);
    retname = dupstr(newname);
    g_free(newname);
    pango_font_description_free(desc);

    return retname;
}

static char *pangofont_size_increment(unifont *font, int increment)
{
    struct pangofont *pfont = (struct pangofont *)font;
    PangoFontDescription *desc;
    int size;
    char *newname, *retname;

    desc = pango_font_description_copy_static(pfont->desc);

    size = pango_font_description_get_size(desc);
    size += PANGO_SCALE * increment;

    if (size <= 0) {
        retname = NULL;
    } else {
        pango_font_description_set_size(desc, size);
        newname = pango_font_description_to_string(desc);
        retname = dupcat(pfont->u.vt->prefix, ":",
                         newname, (const char *)NULL);
        g_free(newname);
    }

    pango_font_description_free(desc);
    return retname;
}

#endif /* GTK_CHECK_VERSION(2,0,0) */

/* ----------------------------------------------------------------------
 * Outermost functions which do the vtable dispatch.
 */

/*
 * Complete list of font-type subclasses. Listed in preference
 * order for unifont_create(). (That is, in the extremely unlikely
 * event that the same font name is valid as both a Pango and an
 * X11 font, it will be interpreted as the former in the absence
 * of an explicit type-disambiguating prefix.)
 *
 * The 'multifont' subclass is omitted here, as discussed above.
 */
static const struct unifont_vtable *unifont_types[] = {
#if GTK_CHECK_VERSION(2,0,0)
    &pangofont_vtable,
#endif
#ifndef NOT_X_WINDOWS
    &x11font_vtable,
#endif
};

/*
 * Function which takes a font name and processes the optional
 * scheme prefix. Returns the tail of the font name suitable for
 * passing to individual font scheme functions, and also provides
 * a subrange of the unifont_types[] array above.
 * 
 * The return values `start' and `end' denote a half-open interval
 * in unifont_types[]; that is, the correct way to iterate over
 * them is
 * 
 *   for (i = start; i < end; i++) {...}
 */
static const char *unifont_do_prefix(const char *name, int *start, int *end)
{
    int colonpos = strcspn(name, ":");
    int i;

    if (name[colonpos]) {
	/*
	 * There's a colon prefix on the font name. Use it to work
	 * out which subclass to use.
	 */
	for (i = 0; i < lenof(unifont_types); i++) {
	    if (strlen(unifont_types[i]->prefix) == colonpos &&
		!strncmp(unifont_types[i]->prefix, name, colonpos)) {
		*start = i;
		*end = i+1;
		return name + colonpos + 1;
	    }
	}
	/*
	 * None matched, so return an empty scheme list to prevent
	 * any scheme from being called at all.
	 */
	*start = *end = 0;
	return name + colonpos + 1;
    } else {
	/*
	 * No colon prefix, so just use all the subclasses.
	 */
	*start = 0;
	*end = lenof(unifont_types);
	return name;
    }
}

unifont *unifont_create(GtkWidget *widget, const char *name, int wide,
			int bold, int shadowoffset, int shadowalways)
{
    int i, start, end;

    name = unifont_do_prefix(name, &start, &end);

    for (i = start; i < end; i++) {
	unifont *ret = unifont_types[i]->create(widget, name, wide, bold,
						shadowoffset, shadowalways);
	if (ret)
	    return ret;
    }
    return NULL;		       /* font not found in any scheme */
}

void unifont_destroy(unifont *font)
{
    font->vt->destroy(font);
}

void unifont_draw_text(unifont_drawctx *ctx, unifont *font,
		       int x, int y, const wchar_t *string, int len,
		       int wide, int bold, int cellwidth)
{
    font->vt->draw_text(ctx, font, x, y, string, len, wide, bold, cellwidth);
}

void unifont_draw_combining(unifont_drawctx *ctx, unifont *font,
                            int x, int y, const wchar_t *string, int len,
                            int wide, int bold, int cellwidth)
{
    font->vt->draw_combining(ctx, font, x, y, string, len, wide, bold,
                             cellwidth);
}

char *unifont_size_increment(unifont *font, int increment)
{
    return font->vt->size_increment(font, increment);
}

/* ----------------------------------------------------------------------
 * Multiple-font wrapper. This is a type of unifont which encapsulates
 * up to two other unifonts, permitting missing glyphs in the main
 * font to be filled in by a fallback font.
 *
 * This is a type of unifont just like the previous two, but it has a
 * separate constructor which is manually called by the client, so it
 * doesn't appear in the list of available font types enumerated by
 * unifont_create. This means it's not used by unifontsel either, so
 * it doesn't need to support any methods except draw_text and
 * destroy.
 */

static void multifont_draw_text(unifont_drawctx *ctx, unifont *font,
                                int x, int y, const wchar_t *string, int len,
                                int wide, int bold, int cellwidth);
static void multifont_draw_combining(unifont_drawctx *ctx, unifont *font,
                                     int x, int y, const wchar_t *string,
                                     int len, int wide, int bold,
                                     int cellwidth);
static void multifont_destroy(unifont *font);
static char *multifont_size_increment(unifont *font, int increment);

struct multifont {
    struct unifont u;
    unifont *main;
    unifont *fallback;
};

static const struct unifont_vtable multifont_vtable = {
    NULL,                             /* creation is done specially */
    NULL,
    multifont_destroy,
    NULL,
    multifont_draw_text,
    multifont_draw_combining,
    NULL,
    NULL,
    NULL,
    multifont_size_increment,
    "client",
};

unifont *multifont_create(GtkWidget *widget, const char *name,
                          int wide, int bold,
                          int shadowoffset, int shadowalways)
{
    int i;
    unifont *font, *fallback;
    struct multifont *mfont;

    font = unifont_create(widget, name, wide, bold,
                          shadowoffset, shadowalways);
    if (!font)
        return NULL;

    fallback = NULL;
    if (font->want_fallback) {
	for (i = 0; i < lenof(unifont_types); i++) {
            if (unifont_types[i]->create_fallback) {
                fallback = unifont_types[i]->create_fallback
                    (widget, font->height, wide, bold,
                     shadowoffset, shadowalways);
                if (fallback)
                    break;
            }
        }
    }

    /*
     * Construct our multifont. Public members are all copied from the
     * primary font we're wrapping.
     */
    mfont = snew(struct multifont);
    mfont->u.vt = &multifont_vtable;
    mfont->u.width = font->width;
    mfont->u.ascent = font->ascent;
    mfont->u.descent = font->descent;
    mfont->u.height = font->height;
    mfont->u.public_charset = font->public_charset;
    mfont->u.want_fallback = FALSE; /* shouldn't be needed, but just in case */
    mfont->u.preferred_drawtype = font->preferred_drawtype;
    mfont->main = font;
    mfont->fallback = fallback;

    return (unifont *)mfont;
}

static void multifont_destroy(unifont *font)
{
    struct multifont *mfont = (struct multifont *)font;
    unifont_destroy(mfont->main);
    if (mfont->fallback)
        unifont_destroy(mfont->fallback);
    sfree(font);
}

typedef void (*unifont_draw_func_t)(unifont_drawctx *ctx, unifont *font,
                                    int x, int y, const wchar_t *string,
                                    int len, int wide, int bold,
                                    int cellwidth);

static void multifont_draw_main(unifont_drawctx *ctx, unifont *font, int x,
                                int y, const wchar_t *string, int len,
                                int wide, int bold, int cellwidth,
                                int cellinc, unifont_draw_func_t draw)
{
    struct multifont *mfont = (struct multifont *)font;
    unifont *f;
    int ok, i;

    while (len > 0) {
        /*
         * Find a maximal sequence of characters which are, or are
         * not, supported by our main font.
         */
        ok = mfont->main->vt->has_glyph(mfont->main, string[0]);
        for (i = 1;
             i < len &&
             !mfont->main->vt->has_glyph(mfont->main, string[i]) == !ok;
             i++);

        /*
         * Now display it.
         */
        f = ok ? mfont->main : mfont->fallback;
        if (f)
            draw(ctx, f, x, y, string, i, wide, bold, cellwidth);
        string += i;
        len -= i;
        x += i * cellinc;
    }
}

static void multifont_draw_text(unifont_drawctx *ctx, unifont *font, int x,
                                int y, const wchar_t *string, int len,
                                int wide, int bold, int cellwidth)
{
    multifont_draw_main(ctx, font, x, y, string, len, wide, bold,
                        cellwidth, cellwidth, unifont_draw_text);
}

static void multifont_draw_combining(unifont_drawctx *ctx, unifont *font,
                                     int x, int y, const wchar_t *string,
                                     int len, int wide, int bold,
                                     int cellwidth)
{
    multifont_draw_main(ctx, font, x, y, string, len, wide, bold,
                        cellwidth, 0, unifont_draw_combining);
}

static char *multifont_size_increment(unifont *font, int increment)
{
    struct multifont *mfont = (struct multifont *)font;
    return unifont_size_increment(mfont->main, increment);
}

#if GTK_CHECK_VERSION(2,0,0)

/* ----------------------------------------------------------------------
 * Implementation of a unified font selector. Used on GTK 2 only;
 * for GTK 1 we still use the standard font selector.
 */

typedef struct fontinfo fontinfo;

typedef struct unifontsel_internal {
    /* This must be the structure's first element, for cross-casting */
    unifontsel u;
    GtkListStore *family_model, *style_model, *size_model;
    GtkWidget *family_list, *style_list, *size_entry, *size_list;
    GtkWidget *filter_buttons[4];
    int n_filter_buttons;
    GtkWidget *preview_area;
#ifndef NO_BACKING_PIXMAPS
    GdkPixmap *preview_pixmap;
#endif
    int preview_width, preview_height;
    GdkColor preview_fg, preview_bg;
    int filter_flags;
    tree234 *fonts_by_realname, *fonts_by_selorder;
    fontinfo *selected;
    int selsize, intendedsize;
    int inhibit_response;  /* inhibit callbacks when we change GUI controls */
} unifontsel_internal;

/*
 * The structure held in the tree234s. All the string members are
 * part of the same allocated area, so don't need freeing
 * separately.
 */
struct fontinfo {
    char *realname;
    char *family, *charset, *style, *stylekey;
    int size, flags;
    /*
     * Fallback sorting key, to permit multiple identical entries
     * to exist in the selorder tree.
     */
    int index;
    /*
     * Indices mapping fontinfo structures to indices in the list
     * boxes. sizeindex is irrelevant if the font is scalable
     * (size==0).
     */
    int familyindex, styleindex, sizeindex;
    /*
     * The class of font.
     */
    const struct unifont_vtable *fontclass;
};

struct fontinfo_realname_find {
    const char *realname;
    int flags;
};

static int strnullcasecmp(const char *a, const char *b)
{
    int i;

    /*
     * If exactly one of the inputs is NULL, it compares before
     * the other one.
     */
    if ((i = (!b) - (!a)) != 0)
	return i;

    /*
     * NULL compares equal.
     */
    if (!a)
	return 0;

    /*
     * Otherwise, ordinary strcasecmp.
     */
    return g_ascii_strcasecmp(a, b);
}

static int fontinfo_realname_compare(void *av, void *bv)
{
    fontinfo *a = (fontinfo *)av;
    fontinfo *b = (fontinfo *)bv;
    int i;

    if ((i = strnullcasecmp(a->realname, b->realname)) != 0)
	return i;
    if ((a->flags & FONTFLAG_SORT_MASK) != (b->flags & FONTFLAG_SORT_MASK))
	return ((a->flags & FONTFLAG_SORT_MASK) <
		(b->flags & FONTFLAG_SORT_MASK) ? -1 : +1);
    return 0;
}

static int fontinfo_realname_find(void *av, void *bv)
{
    struct fontinfo_realname_find *a = (struct fontinfo_realname_find *)av;
    fontinfo *b = (fontinfo *)bv;
    int i;

    if ((i = strnullcasecmp(a->realname, b->realname)) != 0)
	return i;
    if ((a->flags & FONTFLAG_SORT_MASK) != (b->flags & FONTFLAG_SORT_MASK))
	return ((a->flags & FONTFLAG_SORT_MASK) <
		(b->flags & FONTFLAG_SORT_MASK) ? -1 : +1);
    return 0;
}

static int fontinfo_selorder_compare(void *av, void *bv)
{
    fontinfo *a = (fontinfo *)av;
    fontinfo *b = (fontinfo *)bv;
    int i;
    if ((i = strnullcasecmp(a->family, b->family)) != 0)
	return i;
    /*
     * Font class comes immediately after family, so that fonts
     * from different classes with the same family
     */
    if ((a->flags & FONTFLAG_SORT_MASK) != (b->flags & FONTFLAG_SORT_MASK))
	return ((a->flags & FONTFLAG_SORT_MASK) <
		(b->flags & FONTFLAG_SORT_MASK) ? -1 : +1);
    if ((i = strnullcasecmp(a->charset, b->charset)) != 0)
	return i;
    if ((i = strnullcasecmp(a->stylekey, b->stylekey)) != 0)
	return i;
    if ((i = strnullcasecmp(a->style, b->style)) != 0)
	return i;
    if (a->size != b->size)
	return (a->size < b->size ? -1 : +1);
    if (a->index != b->index)
	return (a->index < b->index ? -1 : +1);
    return 0;
}

static void unifontsel_draw_preview_text(unifontsel_internal *fs);

static void unifontsel_deselect(unifontsel_internal *fs)
{
    fs->selected = NULL;
    gtk_list_store_clear(fs->style_model);
    gtk_list_store_clear(fs->size_model);
    gtk_widget_set_sensitive(fs->u.ok_button, FALSE);
    gtk_widget_set_sensitive(fs->size_entry, FALSE);
    unifontsel_draw_preview_text(fs);
}

static void unifontsel_setup_familylist(unifontsel_internal *fs)
{
    GtkTreeIter iter;
    int i, listindex, minpos = -1, maxpos = -1;
    char *currfamily = NULL;
    int currflags = -1;
    fontinfo *info;

    fs->inhibit_response = TRUE;

    gtk_list_store_clear(fs->family_model);
    listindex = 0;

    /*
     * Search through the font tree for anything matching our
     * current filter criteria. When we find one, add its font
     * name to the list box.
     */
    for (i = 0 ;; i++) {
	info = (fontinfo *)index234(fs->fonts_by_selorder, i);
	/*
	 * info may be NULL if we've just run off the end of the
	 * tree. We must still do a processing pass in that
	 * situation, in case we had an unfinished font record in
	 * progress.
	 */
	if (info && (info->flags &~ fs->filter_flags)) {
	    info->familyindex = -1;
	    continue;		       /* we're filtering out this font */
	}
	if (!info || strnullcasecmp(currfamily, info->family) ||
	    currflags != (info->flags & FONTFLAG_SORT_MASK)) {
	    /*
	     * We've either finished a family, or started a new
	     * one, or both.
	     */
	    if (currfamily) {
		gtk_list_store_append(fs->family_model, &iter);
		gtk_list_store_set(fs->family_model, &iter,
				   0, currfamily, 1, minpos, 2, maxpos+1, -1);
		listindex++;
	    }
	    if (info) {
		minpos = i;
		currfamily = info->family;
		currflags = info->flags & FONTFLAG_SORT_MASK;
	    }
	}
	if (!info)
	    break;		       /* now we're done */
	info->familyindex = listindex;
	maxpos = i;
    }

    /*
     * If we've just filtered out the previously selected font,
     * deselect it thoroughly.
     */
    if (fs->selected && fs->selected->familyindex < 0)
	unifontsel_deselect(fs);

    fs->inhibit_response = FALSE;
}

static void unifontsel_setup_stylelist(unifontsel_internal *fs,
				       int start, int end)
{
    GtkTreeIter iter;
    int i, listindex, minpos = -1, maxpos = -1, started = FALSE;
    char *currcs = NULL, *currstyle = NULL;
    fontinfo *info;

    gtk_list_store_clear(fs->style_model);
    listindex = 0;
    started = FALSE;

    /*
     * Search through the font tree for anything matching our
     * current filter criteria. When we find one, add its charset
     * and/or style name to the list box.
     */
    for (i = start; i <= end; i++) {
	if (i == end)
	    info = NULL;
	else
	    info = (fontinfo *)index234(fs->fonts_by_selorder, i);
	/*
	 * info may be NULL if we've just run off the end of the
	 * relevant data. We must still do a processing pass in
	 * that situation, in case we had an unfinished font
	 * record in progress.
	 */
	if (info && (info->flags &~ fs->filter_flags)) {
	    info->styleindex = -1;
	    continue;		       /* we're filtering out this font */
	}
	if (!info || !started || strnullcasecmp(currcs, info->charset) ||
	     strnullcasecmp(currstyle, info->style)) {
	    /*
	     * We've either finished a style/charset, or started a
	     * new one, or both.
	     */
	    started = TRUE;
	    if (currstyle) {
		gtk_list_store_append(fs->style_model, &iter);
		gtk_list_store_set(fs->style_model, &iter,
				   0, currstyle, 1, minpos, 2, maxpos+1,
				   3, TRUE, 4, PANGO_WEIGHT_NORMAL, -1);
		listindex++;
	    }
	    if (info) {
		minpos = i;
		if (info->charset && strnullcasecmp(currcs, info->charset)) {
		    gtk_list_store_append(fs->style_model, &iter);
		    gtk_list_store_set(fs->style_model, &iter,
				       0, info->charset, 1, -1, 2, -1,
				       3, FALSE, 4, PANGO_WEIGHT_BOLD, -1);
		    listindex++;
		}
		currcs = info->charset;
		currstyle = info->style;
	    }
	}
	if (!info)
	    break;		       /* now we're done */
	info->styleindex = listindex;
	maxpos = i;
    }
}

static const int unifontsel_default_sizes[] = { 10, 12, 14, 16, 20, 24, 32 };

static void unifontsel_setup_sizelist(unifontsel_internal *fs,
				      int start, int end)
{
    GtkTreeIter iter;
    int i, listindex;
    char sizetext[40];
    fontinfo *info;

    gtk_list_store_clear(fs->size_model);
    listindex = 0;

    /*
     * Search through the font tree for anything matching our
     * current filter criteria. When we find one, add its font
     * name to the list box.
     */
    for (i = start; i < end; i++) {
	info = (fontinfo *)index234(fs->fonts_by_selorder, i);
	if (info->flags &~ fs->filter_flags) {
	    info->sizeindex = -1;
	    continue;		       /* we're filtering out this font */
	}
	if (info->size) {
	    sprintf(sizetext, "%d", info->size);
	    info->sizeindex = listindex;
	    gtk_list_store_append(fs->size_model, &iter);
	    gtk_list_store_set(fs->size_model, &iter,
			       0, sizetext, 1, i, 2, info->size, -1);
	    listindex++;
	} else {
	    int j;

	    assert(i == start);
	    assert(i+1 == end);

	    for (j = 0; j < lenof(unifontsel_default_sizes); j++) {
		sprintf(sizetext, "%d", unifontsel_default_sizes[j]);
		gtk_list_store_append(fs->size_model, &iter);
		gtk_list_store_set(fs->size_model, &iter, 0, sizetext, 1, i,
				   2, unifontsel_default_sizes[j], -1);
		listindex++;
	    }
	}
    }
}

static void unifontsel_set_filter_buttons(unifontsel_internal *fs)
{
    int i;

    for (i = 0; i < fs->n_filter_buttons; i++) {
        int flagbit = GPOINTER_TO_INT(g_object_get_data
                                      (G_OBJECT(fs->filter_buttons[i]),
				       "user-data"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fs->filter_buttons[i]),
				     !!(fs->filter_flags & flagbit));
    }
}

static void unifontsel_draw_preview_text_inner(unifont_drawctx *dctx,
                                               unifontsel_internal *fs)
{
    unifont *font;
    char *sizename = NULL;
    fontinfo *info = fs->selected;

    if (info) {
	sizename = info->fontclass->scale_fontname
	    (GTK_WIDGET(fs->u.window), info->realname, fs->selsize);
	font = info->fontclass->create(GTK_WIDGET(fs->u.window),
				       sizename ? sizename : info->realname,
				       FALSE, FALSE, 0, 0);
    } else
	font = NULL;

#ifdef DRAW_TEXT_GDK
    if (dctx->type == DRAWTYPE_GDK) {
        gdk_gc_set_foreground(dctx->u.gdk.gc, &fs->preview_bg);
        gdk_draw_rectangle(dctx->u.gdk.target, dctx->u.gdk.gc, 1, 0, 0,
                           fs->preview_width, fs->preview_height);
        gdk_gc_set_foreground(dctx->u.gdk.gc, &fs->preview_fg);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (dctx->type == DRAWTYPE_CAIRO) {
        cairo_set_source_rgb(dctx->u.cairo.cr,
                             fs->preview_bg.red / 65535.0,
                             fs->preview_bg.green / 65535.0,
                             fs->preview_bg.blue / 65535.0);
        cairo_paint(dctx->u.cairo.cr);
        cairo_set_source_rgb(dctx->u.cairo.cr,
                             fs->preview_fg.red / 65535.0,
                             fs->preview_fg.green / 65535.0,
                             fs->preview_fg.blue / 65535.0);
    }
#endif

    if (font) {
        /*
         * The pangram used here is rather carefully
         * constructed: it contains a sequence of very narrow
         * letters (`jil') and a pair of adjacent very wide
         * letters (`wm').
         *
         * If the user selects a proportional font, it will be
         * coerced into fixed-width character cells when used
         * in the actual terminal window. We therefore display
         * it the same way in the preview pane, so as to show
         * it the way it will actually be displayed - and we
         * deliberately pick a pangram which will show the
         * resulting miskerning at its worst.
         *
         * We aren't trying to sell people these fonts; we're
         * trying to let them make an informed choice. Better
         * that they find out the problems with using
         * proportional fonts in terminal windows here than
         * that they go to the effort of selecting their font
         * and _then_ realise it was a mistake.
         */
        info->fontclass->draw_text(dctx, font,
                                   0, font->ascent,
                                   L"bankrupt jilted showmen quiz convex fogey",
                                   41, FALSE, FALSE, font->width);
        info->fontclass->draw_text(dctx, font,
                                   0, font->ascent + font->height,
                                   L"BANKRUPT JILTED SHOWMEN QUIZ CONVEX FOGEY",
                                   41, FALSE, FALSE, font->width);
        /*
         * The ordering of punctuation here is also selected
         * with some specific aims in mind. I put ` and '
         * together because some software (and people) still
         * use them as matched quotes no matter what Unicode
         * might say on the matter, so people can quickly
         * check whether they look silly in a candidate font.
         * The sequence #_@ is there to let people judge the
         * suitability of the underscore as an effectively
         * alphabetic character (since that's how it's often
         * used in practice, at least by programmers).
         */
        info->fontclass->draw_text(dctx, font,
                                   0, font->ascent + font->height * 2,
                                   L"0123456789!?,.:;<>()[]{}\\/`'\"+*-=~#_@|%&^$",
                                   42, FALSE, FALSE, font->width);

	info->fontclass->destroy(font);
    }

    sfree(sizename);
}

static void unifontsel_draw_preview_text(unifontsel_internal *fs)
{
    unifont_drawctx dctx;
    GdkWindow *target;

#ifndef NO_BACKING_PIXMAPS
    target = fs->preview_pixmap;
#else
    target = gtk_widget_get_window(fs->preview_area);
#endif
    if (!target) /* we may be called when we haven't created everything yet */
        return;

    dctx.type = DRAWTYPE_DEFAULT;
#ifdef DRAW_TEXT_GDK
    if (dctx.type == DRAWTYPE_GDK) {
        dctx.u.gdk.target = target;
        dctx.u.gdk.gc = gdk_gc_new(target);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (dctx.type == DRAWTYPE_CAIRO) {
#if GTK_CHECK_VERSION(3,22,0)
        cairo_region_t *region;
#endif

        dctx.u.cairo.widget = GTK_WIDGET(fs->preview_area);

#if GTK_CHECK_VERSION(3,22,0)
        dctx.u.cairo.gdkwin = gtk_widget_get_window(dctx.u.cairo.widget);
        region = gdk_window_get_clip_region(dctx.u.cairo.gdkwin);
        dctx.u.cairo.drawctx = gdk_window_begin_draw_frame(
            dctx.u.cairo.gdkwin, region);
        dctx.u.cairo.cr = gdk_drawing_context_get_cairo_context(
            dctx.u.cairo.drawctx);
        cairo_region_destroy(region);
#else
        dctx.u.cairo.cr = gdk_cairo_create(target);
#endif
    }
#endif

    unifontsel_draw_preview_text_inner(&dctx, fs);

#ifdef DRAW_TEXT_GDK
    if (dctx.type == DRAWTYPE_GDK) {
        gdk_gc_unref(dctx.u.gdk.gc);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (dctx.type == DRAWTYPE_CAIRO) {
#if GTK_CHECK_VERSION(3,22,0)
        gdk_window_end_draw_frame(dctx.u.cairo.gdkwin, dctx.u.cairo.drawctx);
#else
        cairo_destroy(dctx.u.cairo.cr);
#endif
    }
#endif

    gdk_window_invalidate_rect(gtk_widget_get_window(fs->preview_area),
                               NULL, FALSE);
}

static void unifontsel_select_font(unifontsel_internal *fs,
				   fontinfo *info, int size, int leftlist,
				   int size_is_explicit)
{
    int index;
    int minval, maxval;
    gboolean success;
    GtkTreePath *treepath;
    GtkTreeIter iter;

    fs->inhibit_response = TRUE;

    fs->selected = info;
    fs->selsize = size;
    if (size_is_explicit)
	fs->intendedsize = size;

    gtk_widget_set_sensitive(fs->u.ok_button, TRUE);

    /*
     * Find the index of this fontinfo in the selorder list. 
     */
    index = -1;
    findpos234(fs->fonts_by_selorder, info, NULL, &index);
    assert(index >= 0);

    /*
     * Adjust the font selector flags and redo the font family
     * list box, if necessary.
     */
    if (leftlist <= 0 &&
	(fs->filter_flags | info->flags) != fs->filter_flags) {
	fs->filter_flags |= info->flags;
	unifontsel_set_filter_buttons(fs);
	unifontsel_setup_familylist(fs);
    }

    /*
     * Find the appropriate family name and select it in the list.
     */
    assert(info->familyindex >= 0);
    treepath = gtk_tree_path_new_from_indices(info->familyindex, -1);
    gtk_tree_selection_select_path
	(gtk_tree_view_get_selection(GTK_TREE_VIEW(fs->family_list)),
	 treepath);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(fs->family_list),
				 treepath, NULL, FALSE, 0.0, 0.0);
    success = gtk_tree_model_get_iter(GTK_TREE_MODEL(fs->family_model),
                                      &iter, treepath);
    assert(success);
    gtk_tree_path_free(treepath);

    /*
     * Now set up the font style list.
     */
    gtk_tree_model_get(GTK_TREE_MODEL(fs->family_model), &iter,
		       1, &minval, 2, &maxval, -1);
    if (leftlist <= 1)
	unifontsel_setup_stylelist(fs, minval, maxval);

    /*
     * Find the appropriate style name and select it in the list.
     */
    if (info->style) {
	assert(info->styleindex >= 0);
	treepath = gtk_tree_path_new_from_indices(info->styleindex, -1);
	gtk_tree_selection_select_path
	    (gtk_tree_view_get_selection(GTK_TREE_VIEW(fs->style_list)),
	     treepath);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(fs->style_list),
				     treepath, NULL, FALSE, 0.0, 0.0);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(fs->style_model),
				&iter, treepath);
	gtk_tree_path_free(treepath);

	/*
	 * And set up the size list.
	 */
	gtk_tree_model_get(GTK_TREE_MODEL(fs->style_model), &iter,
			   1, &minval, 2, &maxval, -1);
	if (leftlist <= 2)
	    unifontsel_setup_sizelist(fs, minval, maxval);

	/*
	 * Find the appropriate size, and select it in the list.
	 */
	if (info->size) {
	    assert(info->sizeindex >= 0);
	    treepath = gtk_tree_path_new_from_indices(info->sizeindex, -1);
	    gtk_tree_selection_select_path
		(gtk_tree_view_get_selection(GTK_TREE_VIEW(fs->size_list)),
		 treepath);
	    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(fs->size_list),
					 treepath, NULL, FALSE, 0.0, 0.0);
	    gtk_tree_path_free(treepath);
	    size = info->size;
	} else {
	    int j;
	    for (j = 0; j < lenof(unifontsel_default_sizes); j++)
		if (unifontsel_default_sizes[j] == size) {
		    treepath = gtk_tree_path_new_from_indices(j, -1);
		    gtk_tree_view_set_cursor(GTK_TREE_VIEW(fs->size_list),
					     treepath, NULL, FALSE);
		    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(fs->size_list),
						 treepath, NULL, FALSE, 0.0,
						 0.0);
		    gtk_tree_path_free(treepath);
		}
	}

	/*
	 * And set up the font size text entry box.
	 */
	{
	    char sizetext[40];
	    sprintf(sizetext, "%d", size);
	    gtk_entry_set_text(GTK_ENTRY(fs->size_entry), sizetext);
	}
    } else {
	if (leftlist <= 2)
	    unifontsel_setup_sizelist(fs, 0, 0);
	gtk_entry_set_text(GTK_ENTRY(fs->size_entry), "");
    }

    /*
     * Grey out the font size edit box if we're not using a
     * scalable font.
     */
    gtk_editable_set_editable(GTK_EDITABLE(fs->size_entry),
                              fs->selected->size == 0);
    gtk_widget_set_sensitive(fs->size_entry, fs->selected->size == 0);

    unifontsel_draw_preview_text(fs);

    fs->inhibit_response = FALSE;
}

static void unifontsel_button_toggled(GtkToggleButton *tb, gpointer data)
{
    unifontsel_internal *fs = (unifontsel_internal *)data;
    int newstate = gtk_toggle_button_get_active(tb);
    int newflags;
    int flagbit = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tb),
                                                    "user-data"));

    if (newstate)
	newflags = fs->filter_flags | flagbit;
    else
	newflags = fs->filter_flags & ~flagbit;

    if (fs->filter_flags != newflags) {
	fs->filter_flags = newflags;
	unifontsel_setup_familylist(fs);
    }
}

static void unifontsel_add_entry(void *ctx, const char *realfontname,
				 const char *family, const char *charset,
				 const char *style, const char *stylekey,
				 int size, int flags,
				 const struct unifont_vtable *fontclass)
{
    unifontsel_internal *fs = (unifontsel_internal *)ctx;
    fontinfo *info;
    int totalsize;
    char *p;

    totalsize = sizeof(fontinfo) + strlen(realfontname) +
	(family ? strlen(family) : 0) + (charset ? strlen(charset) : 0) +
	(style ? strlen(style) : 0) + (stylekey ? strlen(stylekey) : 0) + 10;
    info = (fontinfo *)smalloc(totalsize);
    info->fontclass = fontclass;
    p = (char *)info + sizeof(fontinfo);
    info->realname = p;
    strcpy(p, realfontname);
    p += 1+strlen(p);
    if (family) {
	info->family = p;
	strcpy(p, family);
	p += 1+strlen(p);
    } else
	info->family = NULL;
    if (charset) {
	info->charset = p;
	strcpy(p, charset);
	p += 1+strlen(p);
    } else
	info->charset = NULL;
    if (style) {
	info->style = p;
	strcpy(p, style);
	p += 1+strlen(p);
    } else
	info->style = NULL;
    if (stylekey) {
	info->stylekey = p;
	strcpy(p, stylekey);
	p += 1+strlen(p);
    } else
	info->stylekey = NULL;
    assert(p - (char *)info <= totalsize);
    info->size = size;
    info->flags = flags;
    info->index = count234(fs->fonts_by_selorder);

    /*
     * It's just conceivable that a misbehaving font enumerator
     * might tell us about the same font real name more than once,
     * in which case we should silently drop the new one.
     */
    if (add234(fs->fonts_by_realname, info) != info) {
	sfree(info);
	return;
    }
    /*
     * However, we should never get a duplicate key in the
     * selorder tree, because the index field carefully
     * disambiguates otherwise identical records.
     */
    add234(fs->fonts_by_selorder, info);
}

static fontinfo *update_for_intended_size(unifontsel_internal *fs,
					  fontinfo *info)
{
    fontinfo info2, *below, *above;
    int pos;

    /*
     * Copy the info structure. This doesn't copy its dynamic
     * string fields, but that's unimportant because all we're
     * going to do is to adjust the size field and use it in one
     * tree search.
     */
    info2 = *info;
    info2.size = fs->intendedsize;

    /*
     * Search in the tree to find the fontinfo structure which
     * best approximates the size the user last requested.
     */
    below = findrelpos234(fs->fonts_by_selorder, &info2, NULL,
			  REL234_LE, &pos);
    if (!below)
        pos = -1;
    above = index234(fs->fonts_by_selorder, pos+1);

    /*
     * See if we've found it exactly, which is an easy special
     * case. If we have, it'll be in `below' and not `above',
     * because we did a REL234_LE rather than REL234_LT search.
     */
    if (below && !fontinfo_selorder_compare(&info2, below))
	return below;

    /*
     * Now we've either found two suitable fonts, one smaller and
     * one larger, or we're at one or other extreme end of the
     * scale. Find out which, by NULLing out either of below and
     * above if it differs from this one in any respect but size
     * (and the disambiguating index field). Bear in mind, also,
     * that either one might _already_ be NULL if we're at the
     * extreme ends of the font list.
     */
    if (below) {
	info2.size = below->size;
	info2.index = below->index;
	if (fontinfo_selorder_compare(&info2, below))
	    below = NULL;
    }
    if (above) {
	info2.size = above->size;
	info2.index = above->index;
	if (fontinfo_selorder_compare(&info2, above))
	    above = NULL;
    }

    /*
     * Now return whichever of above and below is non-NULL, if
     * that's unambiguous.
     */
    if (!above)
	return below;
    if (!below)
	return above;

    /*
     * And now we really do have to make a choice about whether to
     * round up or down. We'll do it by rounding to nearest,
     * breaking ties by rounding up.
     */
    if (above->size - fs->intendedsize <= fs->intendedsize - below->size)
	return above;
    else
	return below;
}

static void family_changed(GtkTreeSelection *treeselection, gpointer data)
{
    unifontsel_internal *fs = (unifontsel_internal *)data;
    GtkTreeModel *treemodel;
    GtkTreeIter treeiter;
    int minval;
    fontinfo *info;

    if (fs->inhibit_response)	       /* we made this change ourselves */
	return;

    if (!gtk_tree_selection_get_selected(treeselection, &treemodel, &treeiter))
	return;

    gtk_tree_model_get(treemodel, &treeiter, 1, &minval, -1);
    info = (fontinfo *)index234(fs->fonts_by_selorder, minval);
    info = update_for_intended_size(fs, info);
    if (!info)
	return; /* _shouldn't_ happen unless font list is completely funted */
    if (!info->size)
	fs->selsize = fs->intendedsize;   /* font is scalable */
    unifontsel_select_font(fs, info, info->size ? info->size : fs->selsize,
			   1, FALSE);
}

static void style_changed(GtkTreeSelection *treeselection, gpointer data)
{
    unifontsel_internal *fs = (unifontsel_internal *)data;
    GtkTreeModel *treemodel;
    GtkTreeIter treeiter;
    int minval;
    fontinfo *info;

    if (fs->inhibit_response)	       /* we made this change ourselves */
	return;

    if (!gtk_tree_selection_get_selected(treeselection, &treemodel, &treeiter))
	return;

    gtk_tree_model_get(treemodel, &treeiter, 1, &minval, -1);
    if (minval < 0)
        return;                    /* somehow a charset heading got clicked */
    info = (fontinfo *)index234(fs->fonts_by_selorder, minval);
    info = update_for_intended_size(fs, info);
    if (!info)
	return; /* _shouldn't_ happen unless font list is completely funted */
    if (!info->size)
	fs->selsize = fs->intendedsize;   /* font is scalable */
    unifontsel_select_font(fs, info, info->size ? info->size : fs->selsize,
			   2, FALSE);
}

static void size_changed(GtkTreeSelection *treeselection, gpointer data)
{
    unifontsel_internal *fs = (unifontsel_internal *)data;
    GtkTreeModel *treemodel;
    GtkTreeIter treeiter;
    int minval, size;
    fontinfo *info;

    if (fs->inhibit_response)	       /* we made this change ourselves */
	return;

    if (!gtk_tree_selection_get_selected(treeselection, &treemodel, &treeiter))
	return;

    gtk_tree_model_get(treemodel, &treeiter, 1, &minval, 2, &size, -1);
    info = (fontinfo *)index234(fs->fonts_by_selorder, minval);
    unifontsel_select_font(fs, info, info->size ? info->size : size, 3, TRUE);
}

static void size_entry_changed(GtkEditable *ed, gpointer data)
{
    unifontsel_internal *fs = (unifontsel_internal *)data;
    const char *text;
    int size;

    if (fs->inhibit_response)	       /* we made this change ourselves */
	return;

    text = gtk_entry_get_text(GTK_ENTRY(ed));
    size = atoi(text);

    if (size > 0) {
	assert(fs->selected->size == 0);
	unifontsel_select_font(fs, fs->selected, size, 3, TRUE);
    }
}

static void alias_resolve(GtkTreeView *treeview, GtkTreePath *path,
			  GtkTreeViewColumn *column, gpointer data)
{
    unifontsel_internal *fs = (unifontsel_internal *)data;
    GtkTreeIter iter;
    int minval, newsize;
    fontinfo *info, *newinfo;
    char *newname;

    if (fs->inhibit_response)	       /* we made this change ourselves */
	return;

    gtk_tree_model_get_iter(GTK_TREE_MODEL(fs->family_model), &iter, path);
    gtk_tree_model_get(GTK_TREE_MODEL(fs->family_model), &iter, 1,&minval, -1);
    info = (fontinfo *)index234(fs->fonts_by_selorder, minval);
    if (info) {
	int flags;
	struct fontinfo_realname_find f;

	newname = info->fontclass->canonify_fontname
	    (GTK_WIDGET(fs->u.window), info->realname, &newsize, &flags, TRUE);

	f.realname = newname;
	f.flags = flags;
	newinfo = find234(fs->fonts_by_realname, &f, fontinfo_realname_find);

	sfree(newname);
	if (!newinfo)
	    return;		       /* font name not in our index */
	if (newinfo == info)
	    return;   /* didn't change under canonification => not an alias */
	unifontsel_select_font(fs, newinfo,
			       newinfo->size ? newinfo->size : newsize,
			       1, TRUE);
    }
}

#if GTK_CHECK_VERSION(3,0,0)
static gint unifontsel_draw_area(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    unifontsel_internal *fs = (unifontsel_internal *)data;
    unifont_drawctx dctx;

    dctx.type = DRAWTYPE_CAIRO;
    dctx.u.cairo.widget = widget;
    dctx.u.cairo.cr = cr;
    unifontsel_draw_preview_text_inner(&dctx, fs);

    return TRUE;
}
#else
static gint unifontsel_expose_area(GtkWidget *widget, GdkEventExpose *event,
				   gpointer data)
{
    unifontsel_internal *fs = (unifontsel_internal *)data;

#ifndef NO_BACKING_PIXMAPS
    if (fs->preview_pixmap) {
        gdk_draw_pixmap(gtk_widget_get_window(widget),
			(gtk_widget_get_style(widget)->fg_gc
                         [gtk_widget_get_state(widget)]),
			fs->preview_pixmap,
			event->area.x, event->area.y,
			event->area.x, event->area.y,
			event->area.width, event->area.height);
    }
#else
    unifontsel_draw_preview_text(fs);
#endif

    return TRUE;
}
#endif

static gint unifontsel_configure_area(GtkWidget *widget,
				      GdkEventConfigure *event, gpointer data)
{
#ifndef NO_BACKING_PIXMAPS
    unifontsel_internal *fs = (unifontsel_internal *)data;
    int ox, oy, nx, ny, x, y;

    /*
     * Enlarge the pixmap, but never shrink it.
     */
    ox = fs->preview_width;
    oy = fs->preview_height;
    x = event->width;
    y = event->height;
    if (x > ox || y > oy) {
	if (fs->preview_pixmap)
	    gdk_pixmap_unref(fs->preview_pixmap);
	
	nx = (x > ox ? x : ox);
	ny = (y > oy ? y : oy);
	fs->preview_pixmap = gdk_pixmap_new(gtk_widget_get_window(widget),
                                            nx, ny, -1);
	fs->preview_width = nx;
	fs->preview_height = ny;

	unifontsel_draw_preview_text(fs);
    }
#endif

    gdk_window_invalidate_rect(gtk_widget_get_window(widget), NULL, FALSE);

    return TRUE;
}

unifontsel *unifontsel_new(const char *wintitle)
{
    unifontsel_internal *fs = snew(unifontsel_internal);
    GtkWidget *table, *label, *w, *ww, *scroll;
    GtkListStore *model;
    GtkTreeViewColumn *column;
    int lists_height, preview_height, font_width, style_width, size_width;
    int i;

    fs->inhibit_response = FALSE;
    fs->selected = NULL;

    {
        int width, height;

	/*
	 * Invent some magic size constants.
	 */
	get_label_text_dimensions("Quite Long Font Name (Foundry)",
                                  &width, &height);
	font_width = width;
	lists_height = 14 * height;
	preview_height = 5 * height;

	get_label_text_dimensions("Italic Extra Condensed", &width, &height);
	style_width = width;

	get_label_text_dimensions("48000", &width, &height);
	size_width = width;
    }

    /*
     * Create the dialog box and initialise the user-visible
     * fields in the returned structure.
     */
    fs->u.user_data = NULL;
    fs->u.window = GTK_WINDOW(gtk_dialog_new());
    gtk_window_set_title(fs->u.window, wintitle);
    fs->u.cancel_button = gtk_dialog_add_button
	(GTK_DIALOG(fs->u.window), STANDARD_CANCEL_LABEL, GTK_RESPONSE_CANCEL);
    fs->u.ok_button = gtk_dialog_add_button
	(GTK_DIALOG(fs->u.window), STANDARD_OK_LABEL, GTK_RESPONSE_OK);
    gtk_widget_grab_default(fs->u.ok_button);

    /*
     * Now set up the internal fields, including in particular all
     * the controls that actually allow the user to select fonts.
     */
#if GTK_CHECK_VERSION(3,0,0)
    table = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(table), 8);
#else
    table = gtk_table_new(8, 3, FALSE);
    gtk_table_set_col_spacings(GTK_TABLE(table), 8);
#endif
    gtk_widget_show(table);

#if GTK_CHECK_VERSION(3,0,0)
    /* GtkAlignment has become deprecated and we use the "margin"
     * property */
    w = table;
    g_object_set(G_OBJECT(w), "margin", 8, (const char *)NULL);
#elif GTK_CHECK_VERSION(2,4,0)
    /* GtkAlignment seems to be the simplest way to put padding round things */
    w = gtk_alignment_new(0, 0, 1, 1);
    gtk_alignment_set_padding(GTK_ALIGNMENT(w), 8, 8, 8, 8);
    gtk_container_add(GTK_CONTAINER(w), table);
    gtk_widget_show(w);
#else
    /* In GTK < 2.4, even that isn't available */
    w = table;
#endif

    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area
                               (GTK_DIALOG(fs->u.window))),
		       w, TRUE, TRUE, 0);

    label = gtk_label_new_with_mnemonic("_Font:");
    gtk_widget_show(label);
    align_label_left(GTK_LABEL(label));
#if GTK_CHECK_VERSION(3,0,0)
    gtk_grid_attach(GTK_GRID(table), label, 0, 0, 1, 1);
    g_object_set(G_OBJECT(label), "hexpand", TRUE, (const char *)NULL);
#else
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1, GTK_FILL, 0, 0, 0);
#endif

    /*
     * The Font list box displays only a string, but additionally
     * stores two integers which give the limits within the
     * tree234 of the font entries covered by this list entry.
     */
    model = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
    w = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(w), FALSE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), w);
    gtk_widget_show(w);
    column = gtk_tree_view_column_new_with_attributes
	("Font", gtk_cell_renderer_text_new(),
	 "text", 0, (char *)NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(w), column);
    g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(w))),
		     "changed", G_CALLBACK(family_changed), fs);
    g_signal_connect(G_OBJECT(w), "row-activated",
		     G_CALLBACK(alias_resolve), fs);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll),
					GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), w);
    gtk_widget_show(scroll);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_widget_set_size_request(scroll, font_width, lists_height);
#if GTK_CHECK_VERSION(3,0,0)
    gtk_grid_attach(GTK_GRID(table), scroll, 0, 1, 1, 2);
    g_object_set(G_OBJECT(scroll), "expand", TRUE, (const char *)NULL);
#else
    gtk_table_attach(GTK_TABLE(table), scroll, 0, 1, 1, 3, GTK_FILL,
		     GTK_EXPAND | GTK_FILL, 0, 0);
#endif
    fs->family_model = model;
    fs->family_list = w;

    label = gtk_label_new_with_mnemonic("_Style:");
    gtk_widget_show(label);
    align_label_left(GTK_LABEL(label));
#if GTK_CHECK_VERSION(3,0,0)
    gtk_grid_attach(GTK_GRID(table), label, 1, 0, 1, 1);
    g_object_set(G_OBJECT(label), "hexpand", TRUE, (const char *)NULL);
#else
    gtk_table_attach(GTK_TABLE(table), label, 1, 2, 0, 1, GTK_FILL, 0, 0, 0);
#endif

    /*
     * The Style list box can contain insensitive elements (character
     * set headings for server-side fonts), so we add an extra column
     * to the list store to hold that information. Also, since GTK3 at
     * least doesn't seem to display insensitive elements differently
     * by default, we add a further column to change their style.
     */
    model = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT,
			       G_TYPE_BOOLEAN, G_TYPE_INT);
    w = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(w), FALSE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), w);
    gtk_widget_show(w);
    column = gtk_tree_view_column_new_with_attributes
	("Style", gtk_cell_renderer_text_new(),
	 "text", 0, "sensitive", 3, "weight", 4, (char *)NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(w), column);
    g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(w))),
		     "changed", G_CALLBACK(style_changed), fs);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll),
					GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), w);
    gtk_widget_show(scroll);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_widget_set_size_request(scroll, style_width, lists_height);
#if GTK_CHECK_VERSION(3,0,0)
    gtk_grid_attach(GTK_GRID(table), scroll, 1, 1, 1, 2);
    g_object_set(G_OBJECT(scroll), "expand", TRUE, (const char *)NULL);
#else
    gtk_table_attach(GTK_TABLE(table), scroll, 1, 2, 1, 3, GTK_FILL,
		     GTK_EXPAND | GTK_FILL, 0, 0);
#endif
    fs->style_model = model;
    fs->style_list = w;

    label = gtk_label_new_with_mnemonic("Si_ze:");
    gtk_widget_show(label);
    align_label_left(GTK_LABEL(label));
#if GTK_CHECK_VERSION(3,0,0)
    gtk_grid_attach(GTK_GRID(table), label, 2, 0, 1, 1);
    g_object_set(G_OBJECT(label), "hexpand", TRUE, (const char *)NULL);
#else
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, 0, 1, GTK_FILL, 0, 0, 0);
#endif

    /*
     * The Size label attaches primarily to a text input box so
     * that the user can select a size of their choice. The list
     * of available sizes is secondary.
     */
    fs->size_entry = w = gtk_entry_new();
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), w);
    gtk_widget_set_size_request(w, size_width, -1);
    gtk_widget_show(w);
#if GTK_CHECK_VERSION(3,0,0)
    gtk_grid_attach(GTK_GRID(table), w, 2, 1, 1, 1);
    g_object_set(G_OBJECT(w), "hexpand", TRUE, (const char *)NULL);
#else
    gtk_table_attach(GTK_TABLE(table), w, 2, 3, 1, 2, GTK_FILL, 0, 0, 0);
#endif
    g_signal_connect(G_OBJECT(w), "changed", G_CALLBACK(size_entry_changed),
		     fs);

    model = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
    w = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(w), FALSE);
    gtk_widget_show(w);
    column = gtk_tree_view_column_new_with_attributes
	("Size", gtk_cell_renderer_text_new(),
	 "text", 0, (char *)NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(w), column);
    g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(w))),
		     "changed", G_CALLBACK(size_changed), fs);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll),
					GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), w);
    gtk_widget_show(scroll);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
#if GTK_CHECK_VERSION(3,0,0)
    gtk_grid_attach(GTK_GRID(table), scroll, 2, 2, 1, 1);
    g_object_set(G_OBJECT(scroll), "expand", TRUE, (const char *)NULL);
#else
    gtk_table_attach(GTK_TABLE(table), scroll, 2, 3, 2, 3, GTK_FILL,
		     GTK_EXPAND | GTK_FILL, 0, 0);
#endif
    fs->size_model = model;
    fs->size_list = w;

    /*
     * Preview widget.
     */
    fs->preview_area = gtk_drawing_area_new();
#ifndef NO_BACKING_PIXMAPS
    fs->preview_pixmap = NULL;
#endif
    fs->preview_width = 0;
    fs->preview_height = 0;
    fs->preview_fg.pixel = fs->preview_bg.pixel = 0;
    fs->preview_fg.red = fs->preview_fg.green = fs->preview_fg.blue = 0x0000;
    fs->preview_bg.red = fs->preview_bg.green = fs->preview_bg.blue = 0xFFFF;
#if !GTK_CHECK_VERSION(3,0,0)
    gdk_colormap_alloc_color(gdk_colormap_get_system(), &fs->preview_fg,
			     FALSE, FALSE);
    gdk_colormap_alloc_color(gdk_colormap_get_system(), &fs->preview_bg,
			     FALSE, FALSE);
#endif
#if GTK_CHECK_VERSION(3,0,0)
    g_signal_connect(G_OBJECT(fs->preview_area), "draw",
                     G_CALLBACK(unifontsel_draw_area), fs);
#else
    g_signal_connect(G_OBJECT(fs->preview_area), "expose_event",
                     G_CALLBACK(unifontsel_expose_area), fs);
#endif
    g_signal_connect(G_OBJECT(fs->preview_area), "configure_event",
                     G_CALLBACK(unifontsel_configure_area), fs);
    gtk_widget_set_size_request(fs->preview_area, 1, preview_height);
    gtk_widget_show(fs->preview_area);
    ww = fs->preview_area;
    w = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(w), ww);
    gtk_widget_show(w);

#if GTK_CHECK_VERSION(3,0,0)
    /* GtkAlignment has become deprecated and we use the "margin"
     * property */
    g_object_set(G_OBJECT(w), "margin", 8, (const char *)NULL);
#elif GTK_CHECK_VERSION(2,4,0)
    ww = w;
    /* GtkAlignment seems to be the simplest way to put padding round things */
    w = gtk_alignment_new(0, 0, 1, 1);
    gtk_alignment_set_padding(GTK_ALIGNMENT(w), 8, 8, 8, 8);
    gtk_container_add(GTK_CONTAINER(w), ww);
    gtk_widget_show(w);
#endif

    ww = w;
    w = gtk_frame_new("Preview of font");
    gtk_container_add(GTK_CONTAINER(w), ww);
    gtk_widget_show(w);
#if GTK_CHECK_VERSION(3,0,0)
    gtk_grid_attach(GTK_GRID(table), w, 0, 3, 3, 1);
    g_object_set(G_OBJECT(w), "expand", TRUE, (const char *)NULL);
#else
    gtk_table_attach(GTK_TABLE(table), w, 0, 3, 3, 4,
		     GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 8);
#endif

    /*
     * We only provide the checkboxes for client- and server-side
     * fonts if we have the X11 back end available, because that's the
     * only situation in which more than one class of font is
     * available anyway.
     */
    fs->n_filter_buttons = 0;
#ifndef NOT_X_WINDOWS
    w = gtk_check_button_new_with_label("Show client-side fonts");
    g_object_set_data(G_OBJECT(w), "user-data",
                      GINT_TO_POINTER(FONTFLAG_CLIENTSIDE));
    g_signal_connect(G_OBJECT(w), "toggled",
                     G_CALLBACK(unifontsel_button_toggled), fs);
    gtk_widget_show(w);
    fs->filter_buttons[fs->n_filter_buttons++] = w;
#if GTK_CHECK_VERSION(3,0,0)
    gtk_grid_attach(GTK_GRID(table), w, 0, 4, 3, 1);
    g_object_set(G_OBJECT(w), "hexpand", TRUE, (const char *)NULL);
#else
    gtk_table_attach(GTK_TABLE(table), w, 0, 3, 4, 5, GTK_FILL, 0, 0, 0);
#endif
    w = gtk_check_button_new_with_label("Show server-side fonts");
    g_object_set_data(G_OBJECT(w), "user-data",
                      GINT_TO_POINTER(FONTFLAG_SERVERSIDE));
    g_signal_connect(G_OBJECT(w), "toggled",
                     G_CALLBACK(unifontsel_button_toggled), fs);
    gtk_widget_show(w);
    fs->filter_buttons[fs->n_filter_buttons++] = w;
#if GTK_CHECK_VERSION(3,0,0)
    gtk_grid_attach(GTK_GRID(table), w, 0, 5, 3, 1);
    g_object_set(G_OBJECT(w), "hexpand", TRUE, (const char *)NULL);
#else
    gtk_table_attach(GTK_TABLE(table), w, 0, 3, 5, 6, GTK_FILL, 0, 0, 0);
#endif
    w = gtk_check_button_new_with_label("Show server-side font aliases");
    g_object_set_data(G_OBJECT(w), "user-data",
                      GINT_TO_POINTER(FONTFLAG_SERVERALIAS));
    g_signal_connect(G_OBJECT(w), "toggled",
                     G_CALLBACK(unifontsel_button_toggled), fs);
    gtk_widget_show(w);
    fs->filter_buttons[fs->n_filter_buttons++] = w;
#if GTK_CHECK_VERSION(3,0,0)
    gtk_grid_attach(GTK_GRID(table), w, 0, 6, 3, 1);
    g_object_set(G_OBJECT(w), "hexpand", TRUE, (const char *)NULL);
#else
    gtk_table_attach(GTK_TABLE(table), w, 0, 3, 6, 7, GTK_FILL, 0, 0, 0);
#endif
#endif /* NOT_X_WINDOWS */
    w = gtk_check_button_new_with_label("Show non-monospaced fonts");
    g_object_set_data(G_OBJECT(w), "user-data",
                      GINT_TO_POINTER(FONTFLAG_NONMONOSPACED));
    g_signal_connect(G_OBJECT(w), "toggled",
                     G_CALLBACK(unifontsel_button_toggled), fs);
    gtk_widget_show(w);
    fs->filter_buttons[fs->n_filter_buttons++] = w;
#if GTK_CHECK_VERSION(3,0,0)
    gtk_grid_attach(GTK_GRID(table), w, 0, 7, 3, 1);
    g_object_set(G_OBJECT(w), "hexpand", TRUE, (const char *)NULL);
#else
    gtk_table_attach(GTK_TABLE(table), w, 0, 3, 7, 8, GTK_FILL, 0, 0, 0);
#endif

    assert(fs->n_filter_buttons <= lenof(fs->filter_buttons));
    fs->filter_flags = FONTFLAG_CLIENTSIDE | FONTFLAG_SERVERSIDE |
	FONTFLAG_SERVERALIAS;
    unifontsel_set_filter_buttons(fs);

    /*
     * Go and find all the font names, and set up our master font
     * list.
     */
    fs->fonts_by_realname = newtree234(fontinfo_realname_compare);
    fs->fonts_by_selorder = newtree234(fontinfo_selorder_compare);
    for (i = 0; i < lenof(unifont_types); i++)
	unifont_types[i]->enum_fonts(GTK_WIDGET(fs->u.window),
				     unifontsel_add_entry, fs);

    /*
     * And set up the initial font names list.
     */
    unifontsel_setup_familylist(fs);

    fs->selsize = fs->intendedsize = 13;   /* random default */
    gtk_widget_set_sensitive(fs->u.ok_button, FALSE);

    return (unifontsel *)fs;
}

void unifontsel_destroy(unifontsel *fontsel)
{
    unifontsel_internal *fs = (unifontsel_internal *)fontsel;
    fontinfo *info;

#ifndef NO_BACKING_PIXMAPS
    if (fs->preview_pixmap)
	gdk_pixmap_unref(fs->preview_pixmap);
#endif

    freetree234(fs->fonts_by_selorder);
    while ((info = delpos234(fs->fonts_by_realname, 0)) != NULL)
	sfree(info);
    freetree234(fs->fonts_by_realname);

    gtk_widget_destroy(GTK_WIDGET(fs->u.window));
    sfree(fs);
}

void unifontsel_set_name(unifontsel *fontsel, const char *fontname)
{
    unifontsel_internal *fs = (unifontsel_internal *)fontsel;
    int i, start, end, size, flags;
    const char *fontname2 = NULL;
    fontinfo *info;

    /*
     * Provide a default if given an empty or null font name.
     */
    if (!fontname || !*fontname)
	fontname = DEFAULT_GTK_FONT;

    /*
     * Call the canonify_fontname function.
     */
    fontname = unifont_do_prefix(fontname, &start, &end);
    for (i = start; i < end; i++) {
	fontname2 = unifont_types[i]->canonify_fontname
	    (GTK_WIDGET(fs->u.window), fontname, &size, &flags, FALSE);
	if (fontname2)
	    break;
    }
    if (i == end)
	return;			       /* font name not recognised */

    /*
     * Now look up the canonified font name in our index.
     */
    {
	struct fontinfo_realname_find f;
	f.realname = fontname2;
	f.flags = flags;
	info = find234(fs->fonts_by_realname, &f, fontinfo_realname_find);
    }

    /*
     * If we've found the font, and its size field is either
     * correct or zero (the latter indicating a scalable font),
     * then we're done. Otherwise, try looking up the original
     * font name instead.
     */
    if (!info || (info->size != size && info->size != 0)) {
	struct fontinfo_realname_find f;
	f.realname = fontname;
	f.flags = flags;

	info = find234(fs->fonts_by_realname, &f, fontinfo_realname_find);
	if (!info || info->size != size)
	    return;		       /* font name not in our index */
    }

    /*
     * Now we've got a fontinfo structure and a font size, so we
     * know everything we need to fill in all the fields in the
     * dialog.
     */
    unifontsel_select_font(fs, info, size, 0, TRUE);
}

char *unifontsel_get_name(unifontsel *fontsel)
{
    unifontsel_internal *fs = (unifontsel_internal *)fontsel;
    char *name;

    if (!fs->selected)
	return NULL;

    if (fs->selected->size == 0) {
	name = fs->selected->fontclass->scale_fontname
	    (GTK_WIDGET(fs->u.window), fs->selected->realname, fs->selsize);
	if (name) {
	    char *ret = dupcat(fs->selected->fontclass->prefix, ":",
			       name, NULL);
	    sfree(name);
	    return ret;
	}
    }

    return dupcat(fs->selected->fontclass->prefix, ":",
		  fs->selected->realname, NULL);
}

#endif /* GTK_CHECK_VERSION(2,0,0) */
