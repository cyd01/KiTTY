#include "putty.h"
#include "dialog.h"

extern void config_protocolbuttons_handler(union control *, void *, void *, int);

void cygterm_setup_config_box(struct controlbox *b, int midsession)
{
    union control *c;
    int i;
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
	              dlg_stdcheckbox_handler,
	              I(offsetof(Config,cygautopath)));
    }
}

/* ex:set ts=8 sw=4: */
