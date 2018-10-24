/*
 * winhelp.c: centralised functions to launch Windows help files,
 * and to decide whether to use .HLP or .CHM help in any given
 * situation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "putty.h"

#ifndef NO_HTMLHELP
#include <htmlhelp.h>
#endif /* NO_HTMLHELP */

static int requested_help;
static char *help_path;
static int help_has_contents;
#ifndef NO_HTMLHELP
DECL_WINDOWS_FUNCTION(static, HWND, HtmlHelpA, (HWND, LPCSTR, UINT, DWORD));
static char *chm_path;
#endif /* NO_HTMLHELP */

void init_help(void)
{
    char b[2048], *p, *q, *r;
    FILE *fp;

    GetModuleFileName(NULL, b, sizeof(b) - 1);
    r = b;
    p = strrchr(b, '\\');
    if (p && p >= r) r = p+1;
    q = strrchr(b, ':');
    if (q && q >= r) r = q+1;
    strcpy(r, PUTTY_HELP_FILE);
    if ( (fp = fopen(b, "r")) != NULL) {
	help_path = dupstr(b);
	fclose(fp);
    } else
	help_path = NULL;
    strcpy(r, PUTTY_HELP_CONTENTS);
    if ( (fp = fopen(b, "r")) != NULL) {
	help_has_contents = TRUE;
	fclose(fp);
    } else
	help_has_contents = FALSE;

#ifndef NO_HTMLHELP
    strcpy(r, PUTTY_CHM_FILE);
    if ( (fp = fopen(b, "r")) != NULL) {
	chm_path = dupstr(b);
	fclose(fp);
    } else
	chm_path = NULL;
    if (chm_path) {
	HINSTANCE dllHH = load_system32_dll("hhctrl.ocx");
	GET_WINDOWS_FUNCTION(dllHH, HtmlHelpA);
	if (!p_HtmlHelpA) {
            sfree(chm_path);
	    chm_path = NULL;
	    if (dllHH)
		FreeLibrary(dllHH);
	}
    }
#endif /* NO_HTMLHELP */
}

void shutdown_help(void)
{
    /* Nothing to do currently.
     * (If we were running HTML Help single-threaded, this is where we'd
     * call HH_UNINITIALIZE.) */
}

int has_help(void)
{
    /*
     * FIXME: it would be nice here to disregard help_path on
     * platforms that didn't have WINHLP32. But that's probably
     * unrealistic, since even Vista will have it if the user
     * specifically downloads it.
     */
    return (help_path != NULL
#ifndef NO_HTMLHELP
	    || chm_path
#endif /* NO_HTMLHELP */
	   );
}

void launch_help(HWND hwnd, const char *topic)
{
    if (topic) {
	int colonpos = strcspn(topic, ":");

#ifndef NO_HTMLHELP
	if (chm_path) {
	    char *fname;
	    assert(topic[colonpos] != '\0');
	    fname = dupprintf("%s::/%s.html>main", chm_path,
			      topic + colonpos + 1);
	    p_HtmlHelpA(hwnd, fname, HH_DISPLAY_TOPIC, 0);
	    sfree(fname);
	} else
#endif /* NO_HTMLHELP */
	if (help_path) {
	    char *cmd = dupprintf("JI(`',`%.*s')", colonpos, topic);
	    WinHelp(hwnd, help_path, HELP_COMMAND, (ULONG_PTR)cmd);
	    sfree(cmd);
	}
    } else {
#ifndef NO_HTMLHELP
	if (chm_path) {
	    p_HtmlHelpA(hwnd, chm_path, HH_DISPLAY_TOPIC, 0);
	} else
#endif /* NO_HTMLHELP */
	if (help_path) {
	    WinHelp(hwnd, help_path,
		    help_has_contents ? HELP_FINDER : HELP_CONTENTS, 0);
	}
    }
    requested_help = TRUE;
}

void quit_help(HWND hwnd)
{
    if (requested_help) {
#ifndef NO_HTMLHELP
	if (chm_path) {
	    p_HtmlHelpA(NULL, NULL, HH_CLOSE_ALL, 0);
	} else
#endif /* NO_HTMLHELP */
	if (help_path) {
	    WinHelp(hwnd, help_path, HELP_QUIT, 0);
	}
	requested_help = FALSE;
    }
}
