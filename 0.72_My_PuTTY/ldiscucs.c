/*
 * ldisc.c: PuTTY line discipline. Sits between the input coming
 * from keypresses in the window, and the output channel leading to
 * the back end. Implements echo and/or local line editing,
 * depending on what's currently configured.
 */

#include <stdio.h>
#include <ctype.h>

#include "putty.h"
#include "terminal.h"
#include "ldisc.h"

void lpage_send(Ldisc *ldisc,
		int codepage, const char *buf, int len, bool interactive)
{
    wchar_t *widebuffer = 0;
    int widesize = 0;
    int wclen;

    if (codepage < 0) {
	ldisc_send(ldisc, buf, len, interactive);
	return;
    }

    widesize = len * 2;
    widebuffer = snewn(widesize, wchar_t);

    wclen = mb_to_wc(codepage, 0, buf, len, widebuffer, widesize);
    luni_send(ldisc, widebuffer, wclen, interactive);

    sfree(widebuffer);
}

void luni_send(Ldisc *ldisc, const wchar_t *widebuf, int len, bool interactive)
{
    int ratio = (in_utf(ldisc->term))?3:1;
    char *linebuffer;
    int linesize;
    int i;
    char *p;

    linesize = len * ratio * 2;
    linebuffer = snewn(linesize, char);

    if (in_utf(ldisc->term)) {
	/* UTF is a simple algorithm */
	for (p = linebuffer, i = 0; i < len; i++) {
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

            p += encode_utf8(p, ch);
	}
    } else {
	int rv;
	rv = wc_to_mb(ldisc->term->ucsdata->line_codepage, 0, widebuf, len,
		      linebuffer, linesize, NULL, ldisc->term->ucsdata);
	if (rv >= 0)
	    p = linebuffer + rv;
	else
	    p = linebuffer;
    }
    if (p > linebuffer)
	ldisc_send(ldisc, linebuffer, p - linebuffer, interactive);

    sfree(linebuffer);
}
