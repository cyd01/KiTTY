#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#define PUTTY_DO_GLOBALS
#include "putty.h"
#include "terminal.h"

/* For Unix in particular, but harmless if this main() is reused elsewhere */
const int buildinfo_gtk_relevant = FALSE;

int main(int argc, char **argv)
{
	char blk[512];
	size_t len;
	Terminal *term;
	Conf *conf;
	struct unicode_data ucsdata;

	conf = conf_new();
	do_defaults(NULL, conf);
	init_ucs(&ucsdata, conf_get_str(conf, CONF_line_codepage),
		 conf_get_int(conf, CONF_utf8_override),
		 CS_NONE, conf_get_int(conf, CONF_vtmode));

	term = term_init(conf, &ucsdata, NULL);
	term_size(term, 24, 80, 10000);
	term->ldisc = NULL;
	/* Tell american fuzzy lop that this is a good place to fork. */
#ifdef __AFL_HAVE_MANUAL_CONTROL
	__AFL_INIT();
#endif
	while (!feof(stdin)) {
		len = fread(blk, 1, sizeof(blk), stdin);
		term_data(term, 0, blk, len);
	}
	term_update(term);
	return 0;
}

int from_backend(void *frontend, int is_stderr, const char *data, int len)
{ return 0; }

/* functions required by terminal.c */

void request_resize(void *frontend, int x, int y) { }
void do_text(Context ctx, int x, int y, wchar_t * text, int len,
#ifdef TRUECOLORPORT
	     unsigned long attr, int lattr, truecolour tc)
#else
	     unsigned long attr, int lattr)
#endif
{
    int i;

    printf("TEXT[attr=%08lx,lattr=%02x]@(%d,%d):", attr, lattr, x, y);
    for (i = 0; i < len; i++) {
	printf(" %x", (unsigned)text[i]);
    }
    printf("\n");
}
void do_cursor(Context ctx, int x, int y, wchar_t * text, int len,
#ifdef TRUECOLORPORT
            unsigned long attr, int lattr, truecolour tc)
#else
	     unsigned long attr, int lattr)
#endif
{
    int i;

    printf("CURS[attr=%08lx,lattr=%02x]@(%d,%d):", attr, lattr, x, y);
    for (i = 0; i < len; i++) {
	printf(" %x", (unsigned)text[i]);
    }
    printf("\n");
}
int char_width(Context ctx, int uc) { return 1; }
void set_title(void *frontend, char *t) { }
void set_icon(void *frontend, char *t) { }
void set_sbar(void *frontend, int a, int b, int c) { }

void ldisc_send(void *handle, const char *buf, int len, int interactive) {}
void ldisc_echoedit_update(void *handle) {}
Context get_ctx(void *frontend) { 
    static char x;

    return &x;
}
void free_ctx(Context ctx) { }
void palette_set(void *frontend, int a, int b, int c, int d) { }
void palette_reset(void *frontend) { }
void write_clip(void *frontend, wchar_t *a, int *b, int c, int d) { }
void get_clip(void *frontend, wchar_t **w, int *i) { }
void set_raw_mouse_mode(void *frontend, int m) { }
void request_paste(void *frontend) { }
void do_beep(void *frontend, int a) { }
void sys_cursor(void *frontend, int x, int y) { }
void fatalbox(const char *fmt, ...) { exit(0); }
void modalfatalbox(const char *fmt, ...) { exit(0); }
void nonfatal(const char *fmt, ...) { }

void set_iconic(void *frontend, int iconic) { }
void move_window(void *frontend, int x, int y) { }
void set_zorder(void *frontend, int top) { }
void refresh_window(void *frontend) { }
void set_zoomed(void *frontend, int zoomed) { }
int is_iconic(void *frontend) { return 0; }
void get_window_pos(void *frontend, int *x, int *y) { *x = 0; *y = 0; }
void get_window_pixels(void *frontend, int *x, int *y) { *x = 0; *y = 0; }
char *get_window_title(void *frontend, int icon) { return "moo"; }
int frontend_is_utf8(void *frontend) { return TRUE; }

/* needed by timing.c */
void timer_change_notify(unsigned long next) { }

/* needed by config.c and sercfg.c */

void dlg_radiobutton_set(union control *ctrl, void *dlg, int whichbutton) { }
int dlg_radiobutton_get(union control *ctrl, void *dlg) { return 0; }
void dlg_checkbox_set(union control *ctrl, void *dlg, int checked) { }
int dlg_checkbox_get(union control *ctrl, void *dlg) { return 0; }
void dlg_editbox_set(union control *ctrl, void *dlg, char const *text) { }
char *dlg_editbox_get(union control *ctrl, void *dlg) { return dupstr("moo"); }
void dlg_listbox_clear(union control *ctrl, void *dlg) { }
void dlg_listbox_del(union control *ctrl, void *dlg, int index) { }
void dlg_listbox_add(union control *ctrl, void *dlg, char const *text) { }
void dlg_listbox_addwithid(union control *ctrl, void *dlg,
			   char const *text, int id) { }
int dlg_listbox_getid(union control *ctrl, void *dlg, int index) { return 0; }
int dlg_listbox_index(union control *ctrl, void *dlg) { return -1; }
int dlg_listbox_issel(union control *ctrl, void *dlg, int index) { return 0; }
void dlg_listbox_select(union control *ctrl, void *dlg, int index) { }
void dlg_text_set(union control *ctrl, void *dlg, char const *text) { }
void dlg_filesel_set(union control *ctrl, void *dlg, Filename *fn) { }
Filename *dlg_filesel_get(union control *ctrl, void *dlg) { return NULL; }
void dlg_fontsel_set(union control *ctrl, void *dlg, FontSpec *fn) { }
FontSpec *dlg_fontsel_get(union control *ctrl, void *dlg) { return NULL; }
void dlg_update_start(union control *ctrl, void *dlg) { }
void dlg_update_done(union control *ctrl, void *dlg) { }
void dlg_set_focus(union control *ctrl, void *dlg) { }
void dlg_label_change(union control *ctrl, void *dlg, char const *text) { }
union control *dlg_last_focused(union control *ctrl, void *dlg) { return NULL; }
void dlg_beep(void *dlg) { }
void dlg_error_msg(void *dlg, const char *msg) { }
void dlg_end(void *dlg, int value) { }
void dlg_coloursel_start(union control *ctrl, void *dlg,
			 int r, int g, int b) { }
int dlg_coloursel_results(union control *ctrl, void *dlg,
			  int *r, int *g, int *b) { return 0; }
void dlg_refresh(union control *ctrl, void *dlg) { }

/* miscellany */
void logevent(void *frontend, const char *msg) { }
int askappend(void *frontend, Filename *filename,
	      void (*callback)(void *ctx, int result), void *ctx) { return 0; }

const char *const appname = "FuZZterm";
const int ngsslibs = 0;
const char *const gsslibnames[0] = { };
const struct keyvalwhere gsslibkeywords[0] = { };

/*
 * Default settings that are specific to Unix plink.
 */
char *platform_default_s(const char *name)
{
    if (!strcmp(name, "TermType"))
	return dupstr(getenv("TERM"));
    if (!strcmp(name, "SerialLine"))
	return dupstr("/dev/ttyS0");
    return NULL;
}

int platform_default_i(const char *name, int def)
{
    return def;
}

FontSpec *platform_default_fontspec(const char *name)
{
    return fontspec_new("");
}

Filename *platform_default_filename(const char *name)
{
    if (!strcmp(name, "LogFileName"))
	return filename_from_str("putty.log");
    else
	return filename_from_str("");
}

char *x_get_default(const char *key)
{
    return NULL;		       /* this is a stub */
}


