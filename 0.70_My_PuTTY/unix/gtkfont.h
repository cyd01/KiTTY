/*
 * Header file for gtkfont.c. Has to be separate from unix.h
 * because it depends on GTK data types, hence can't be included
 * from cross-platform code (which doesn't go near GTK).
 */

#ifndef PUTTY_GTKFONT_H
#define PUTTY_GTKFONT_H

/*
 * We support two entirely different drawing systems: the old
 * GDK1/GDK2 one which works on server-side X drawables, and the
 * new-style Cairo one. GTK1 only supports GDK drawing; GTK3 only
 * supports Cairo; GTK2 supports both, but deprecates GTK, so we only
 * enable it if we aren't trying on purpose to compile without the
 * deprecated functions.
 *
 * Our different font classes may prefer different drawing systems: X
 * server-side fonts are a lot faster to draw with GDK, but for
 * everything else we prefer Cairo, on general grounds of modernness
 * and also in particular because its matrix-based scaling system
 * gives much nicer results for double-width and double-height text
 * when a scalable font is in use.
 */
#if !GTK_CHECK_VERSION(3,0,0) && !defined GDK_DISABLE_DEPRECATED
#define DRAW_TEXT_GDK
#endif
#if GTK_CHECK_VERSION(2,8,0)
#define DRAW_TEXT_CAIRO
#endif

#if GTK_CHECK_VERSION(3,0,0) || defined GDK_DISABLE_DEPRECATED
/*
 * Where the facility is available, we prefer to render text on to a
 * persistent server-side pixmap, and redraw windows by simply
 * blitting rectangles of that pixmap into them as needed. This is
 * better for performance since we avoid expensive font rendering
 * calls where possible, and it's particularly good over a non-local X
 * connection because the response to an expose event can now be a
 * very simple rectangle-copy operation rather than a lot of fiddly
 * drawing or bitmap transfer.
 *
 * However, GTK is deprecating the use of server-side pixmaps, so we
 * have to disable this mode under some circumstances.
 */
#define NO_BACKING_PIXMAPS
#endif

/*
 * Exports from gtkfont.c.
 */
struct unifont_vtable;		       /* contents internal to gtkfont.c */
typedef struct unifont {
    const struct unifont_vtable *vt;
    /*
     * `Non-static data members' of the `class', accessible to
     * external code.
     */

    /*
     * public_charset is the charset used when the user asks for
     * `Use font encoding'.
     */
    int public_charset;

    /*
     * Font dimensions needed by clients.
     */
    int width, height, ascent, descent;

    /*
     * Indicates whether this font is capable of handling all glyphs
     * (Pango fonts can do this because Pango automatically supplies
     * missing glyphs from other fonts), or whether it would like a
     * fallback font to cope with missing glyphs.
     */
    int want_fallback;

    /*
     * Preferred drawing API to use when this class of font is active.
     * (See the enum below, in unifont_drawctx.)
     */
    int preferred_drawtype;
} unifont;

/* A default drawtype, for the case where no font exists to make the
 * decision with. */
#ifdef DRAW_TEXT_CAIRO
#define DRAW_DEFAULT_CAIRO
#define DRAWTYPE_DEFAULT DRAWTYPE_CAIRO
#elif defined DRAW_TEXT_GDK
#define DRAW_DEFAULT_GDK
#define DRAWTYPE_DEFAULT DRAWTYPE_GDK
#else
#error No drawtype available at all
#endif

/*
 * Drawing context passed in to unifont_draw_text, which contains
 * everything required to know where and how to draw the requested
 * text.
 */
typedef struct unifont_drawctx {
    enum {
#ifdef DRAW_TEXT_GDK
        DRAWTYPE_GDK,
#endif
#ifdef DRAW_TEXT_CAIRO
        DRAWTYPE_CAIRO,
#endif
        DRAWTYPE_NTYPES
    } type;
    union {
#ifdef DRAW_TEXT_GDK
        struct {
            GdkDrawable *target;
            GdkGC *gc;
        } gdk;
#endif
#ifdef DRAW_TEXT_CAIRO
        struct {
            /* Need an actual widget, in order to backtrack to its X
             * screen number when creating server-side pixmaps */
            GtkWidget *widget;
            cairo_t *cr;
            cairo_matrix_t origmatrix;
#if GTK_CHECK_VERSION(3,22,0)
            GdkWindow *gdkwin;
            GdkDrawingContext *drawctx;
#endif
        } cairo;
#endif
    } u;
} unifont_drawctx;

unifont *unifont_create(GtkWidget *widget, const char *name,
			int wide, int bold,
			int shadowoffset, int shadowalways);
void unifont_destroy(unifont *font);
void unifont_draw_text(unifont_drawctx *ctx, unifont *font,
                       int x, int y, const wchar_t *string, int len,
                       int wide, int bold, int cellwidth);
/* Same as unifont_draw_text, but expects 'string' to contain one
 * normal char plus combining chars, and overdraws them all in the
 * same character cell. */
void unifont_draw_combining(unifont_drawctx *ctx, unifont *font,
                            int x, int y, const wchar_t *string, int len,
                            int wide, int bold, int cellwidth);
/* Return a name that will select a bigger/smaller font than this one,
 * or NULL if no such name is available. */
char *unifont_size_increment(unifont *font, int increment);

/*
 * This function behaves exactly like the low-level unifont_create,
 * except that as well as the requested font it also allocates (if
 * necessary) a fallback font for filling in replacement glyphs.
 *
 * Return value is usable with unifont_destroy and unifont_draw_text
 * as if it were an ordinary unifont.
 */
unifont *multifont_create(GtkWidget *widget, const char *name,
                          int wide, int bold,
                          int shadowoffset, int shadowalways);

/*
 * Unified font selector dialog. I can't be bothered to do a
 * proper GTK subclassing today, so this will just be an ordinary
 * data structure with some useful members.
 * 
 * (Of course, these aren't the only members; this structure is
 * contained within a bigger one which holds data visible only to
 * the implementation.)
 */
typedef struct unifontsel {
    void *user_data;		       /* settable by the user */
    GtkWindow *window;
    GtkWidget *ok_button, *cancel_button;
} unifontsel;

unifontsel *unifontsel_new(const char *wintitle);
void unifontsel_destroy(unifontsel *fontsel);
void unifontsel_set_name(unifontsel *fontsel, const char *fontname);
char *unifontsel_get_name(unifontsel *fontsel);

#endif /* PUTTY_GTKFONT_H */
