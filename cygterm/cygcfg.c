/*
-- putty-0.59/windows/cygcfg.c	1969-12-31 17:00:00.000000000 -0700
++ putty-0.59-cygterm/windows/cygcfg.c	2007-02-06 16:16:15.000000000 -0700
@ -0,0 +1,33 @@
*/

#ifdef CYGTERMPORT

#include "putty.h"
#include "dialog.h"

static int CygTermFlag = 1 ;

void cygterm_set_flag( int flag ) {
	if( flag >= 1 ) CygTermFlag = 1 ;
	else CygTermFlag = 0 ;
	}

int cygterm_get_flag( void ) {
	return CygTermFlag ;
	}

extern void config_protocolbuttons_handler(union control *, void *, void *, int);

static int is64Bits() {
	return (NULL != GetProcAddress(GetModuleHandle("kernel32"), "IsWow64Process")) ? 1 : 0;
	}

void cygterm_setup_config_box(struct controlbox *b, int midsession)
{
    union control *c;
    int i;
	
	if( !CygTermFlag ) return ;
	
    struct controlset *s;
    s = ctrl_getset(b, "Session", "hostport",
                    "Specify the destination you want to connect to");
    for (i = 0; i < s->ncontrols; i++) {
	c = s->ctrls[i];
	if (c->generic.type == CTRL_RADIO &&
	    c->generic.handler == config_protocolbuttons_handler) {
	    c->radio.nbuttons++;
	    /* c->radio.ncolumns++; */
	    c->radio.buttons =
		sresize(c->radio.buttons, c->radio.nbuttons, char *);
	    c->radio.buttons[c->radio.nbuttons-1] = dupstr("Cygterm");
	    c->radio.buttondata =
		sresize(c->radio.buttondata, c->radio.nbuttons, intorptr);
	    c->radio.buttondata[c->radio.nbuttons-1] = I(PROT_CYGTERM);
	    if (c->radio.shortcuts) {
		c->radio.shortcuts =
		    sresize(c->radio.shortcuts, c->radio.nbuttons, char);
		c->radio.shortcuts[c->radio.nbuttons-1] = NO_SHORTCUT;
	    }
	}
    }
    if (!midsession) {
	ctrl_settitle(b, "Connection/Cygterm",
	              "Options controlling Cygterm sessions");
	s = ctrl_getset(b, "Connection/Cygterm", "cygterm",
	                "Configure Cygwin paths");
	ctrl_checkbox(s, "Autodetect Cygwin installation", 'd',
	              HELPCTX(no_help),
	              conf_checkbox_handler/*dlg_stdcheckbox_handler*/,
	              I(CONF_cygautopath)
		      );
		if( is64Bits() )
			{
			ctrl_checkbox(s, "Use Cygwin64", 'u',
			HELPCTX(no_help),
			conf_checkbox_handler,
			I(CONF_cygterm64));
			}
    }
}

#endif
