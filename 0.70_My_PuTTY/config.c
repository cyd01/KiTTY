/*
 * config.c - the platform-independent parts of the PuTTY
 * configuration box.
 */
#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "dialog.h"
#include "storage.h"


#ifdef RUTTYPORT
#include "script.h"
#endif  /* rutty */
#ifdef CYGTERMPORT
int cygterm_get_flag( void ) ;
#endif
#ifdef PERSOPORT
#include "kitty.h"
union control * ctrlHostnameEdit = NULL ;
void MASKPASS( char * password ) ;
int stricmp(const char *s1, const char *s2) ;
int GetReadOnlyFlag(void) ;
#endif

#define PRINTER_DISABLED_STRING "None (printing disabled)"

#define HOST_BOX_TITLE "Host Name (or IP address)"
#define PORT_BOX_TITLE "Port"

void conf_radiobutton_handler(union control *ctrl, void *dlg,
			      void *data, int event)
{
    int button;
    Conf *conf = (Conf *)data;

    /*
     * For a standard radio button set, the context parameter gives
     * the primary key (CONF_foo), and the extra data per button
     * gives the value the target field should take if that button
     * is the one selected.
     */
    if (event == EVENT_REFRESH) {
	int val = conf_get_int(conf, ctrl->radio.context.i);
	for (button = 0; button < ctrl->radio.nbuttons; button++)
	    if (val == ctrl->radio.buttondata[button].i)
		break;
	/* We expected that `break' to happen, in all circumstances. */
	assert(button < ctrl->radio.nbuttons);
	dlg_radiobutton_set(ctrl, dlg, button);
    } else if (event == EVENT_VALCHANGE) {
	button = dlg_radiobutton_get(ctrl, dlg);
	assert(button >= 0 && button < ctrl->radio.nbuttons);
	conf_set_int(conf, ctrl->radio.context.i,
		     ctrl->radio.buttondata[button].i);
    }
}

#define CHECKBOX_INVERT (1<<30)
void conf_checkbox_handler(union control *ctrl, void *dlg,
			   void *data, int event)
{
    int key, invert;
    Conf *conf = (Conf *)data;

    /*
     * For a standard checkbox, the context parameter gives the
     * primary key (CONF_foo), optionally ORed with CHECKBOX_INVERT.
     */
    key = ctrl->checkbox.context.i;
    if (key & CHECKBOX_INVERT) {
	key &= ~CHECKBOX_INVERT;
	invert = 1;
    } else
	invert = 0;

    /*
     * C lacks a logical XOR, so the following code uses the idiom
     * (!a ^ !b) to obtain the logical XOR of a and b. (That is, 1
     * iff exactly one of a and b is nonzero, otherwise 0.)
     */

    if (event == EVENT_REFRESH) {
	int val = conf_get_int(conf, key);
	dlg_checkbox_set(ctrl, dlg, (!val ^ !invert));
    } else if (event == EVENT_VALCHANGE) {
	conf_set_int(conf, key, !dlg_checkbox_get(ctrl,dlg) ^ !invert);
    }
}

void conf_editbox_handler(union control *ctrl, void *dlg,
			  void *data, int event)
{
    /*
     * The standard edit-box handler expects the main `context'
     * field to contain the primary key. The secondary `context2'
     * field indicates the type of this field:
     *
     *  - if context2 > 0, the field is a string.
     *  - if context2 == -1, the field is an int and the edit box
     *    is numeric.
     *  - if context2 < -1, the field is an int and the edit box is
     *    _floating_, and (-context2) gives the scale. (E.g. if
     *    context2 == -1000, then typing 1.2 into the box will set
     *    the field to 1200.)
     */
    int key = ctrl->editbox.context.i;
    int length = ctrl->editbox.context2.i;
    Conf *conf = (Conf *)data;

    if (length > 0) {
	if (event == EVENT_REFRESH) {
	    char *field = conf_get_str(conf, key);
	    dlg_editbox_set(ctrl, dlg, field);
	} else if (event == EVENT_VALCHANGE) {
	    char *field = dlg_editbox_get(ctrl, dlg);
	    conf_set_str(conf, key, field);
	    sfree(field);
	}
    } else if (length < 0) {
	if (event == EVENT_REFRESH) {
	    char str[80];
	    int value = conf_get_int(conf, key);
	    if (length == -1)
		sprintf(str, "%d", value);
	    else
		sprintf(str, "%g", (double)value / (double)(-length));
	    dlg_editbox_set(ctrl, dlg, str);
	} else if (event == EVENT_VALCHANGE) {
	    char *str = dlg_editbox_get(ctrl, dlg);
	    if (length == -1)
		conf_set_int(conf, key, atoi(str));
	    else
		conf_set_int(conf, key, (int)((-length) * atof(str)));
	    sfree(str);
	}
    }
}

void conf_filesel_handler(union control *ctrl, void *dlg,
			  void *data, int event)
{
    int key = ctrl->fileselect.context.i;
    Conf *conf = (Conf *)data;

    if (event == EVENT_REFRESH) {
	dlg_filesel_set(ctrl, dlg, conf_get_filename(conf, key));
    } else if (event == EVENT_VALCHANGE) {
	Filename *filename = dlg_filesel_get(ctrl, dlg);
	conf_set_filename(conf, key, filename);
        filename_free(filename);
    }
}

#ifdef PERSOPORT
void ReadInitScript( const char * filename ) ;
int existfile( const char * filename );
union control * ctrlScriptFileContentEdit = NULL ;
void conf_scriptfilesel_handler(union control *ctrl, void *dlg,
			  void *data, int event)
{
    int key = ctrl->fileselect.context.i;
    Conf *conf = (Conf *)data;

    if (event == EVENT_REFRESH) {
	dlg_filesel_set(ctrl, dlg, conf_get_filename(conf, key));
    } else if (event == EVENT_VALCHANGE) {
	Filename *filename = dlg_filesel_get(ctrl, dlg);
	if( filename && existfile(filename->path) ) {
		//conf_set_filename(conf, key, filename);
		ReadInitScript(filename->path);
		if( ctrlScriptFileContentEdit ) dlg_editbox_set(ctrlScriptFileContentEdit, dlg, conf_get_str(conf,CONF_scriptfilecontent));
		Filename * fn = filename_from_str( "" ) ;
		conf_set_filename(conf,CONF_scriptfile,fn);
		dlg_filesel_set(ctrl, dlg,  fn ) ;
		filename_free(fn);
		}
        filename_free(filename);
    }
}
#endif

void conf_fontsel_handler(union control *ctrl, void *dlg,
			  void *data, int event)
{
    int key = ctrl->fontselect.context.i;
    Conf *conf = (Conf *)data;

    if (event == EVENT_REFRESH) {
	dlg_fontsel_set(ctrl, dlg, conf_get_fontspec(conf, key));
    } else if (event == EVENT_VALCHANGE) {
	FontSpec *fontspec = dlg_fontsel_get(ctrl, dlg);
	conf_set_fontspec(conf, key, fontspec);
        fontspec_free(fontspec);
    }
}

static void config_host_handler(union control *ctrl, void *dlg,
				void *data, int event)
{
    Conf *conf = (Conf *)data;
#ifdef PERSOPORT
	ctrlHostnameEdit = ctrl ;
#endif

    /*
     * This function works just like the standard edit box handler,
     * only it has to choose the control's label and text from two
     * different places depending on the protocol.
     */
    if (event == EVENT_REFRESH) {
	if (conf_get_int(conf, CONF_protocol) == PROT_SERIAL) {
	    /*
	     * This label text is carefully chosen to contain an n,
	     * since that's the shortcut for the host name control.
	     */
	    dlg_label_change(ctrl, dlg, "Serial line");
	    dlg_editbox_set(ctrl, dlg, conf_get_str(conf, CONF_serline));
#ifdef CYGTERMPORT
	} else if ( conf_get_int(conf, CONF_protocol) /*cfg->protocol*/ == PROT_CYGTERM) {
	    dlg_label_change(ctrl, dlg, "Command (use - for login shell, ? for exe)");
	    dlg_editbox_set(ctrl, dlg, conf_get_str(conf, CONF_cygcmd) /*cfg->cygcmd*/);
#endif
#ifdef ADBPORT
        } else if (conf_get_int(conf, CONF_protocol) == PROT_ADB) {
            char *saved_host = conf_get_str(conf, CONF_host);
            dlg_label_change(ctrl, dlg, "-a: any, -d: usb, -e: emulator, or :serial");
            if (!saved_host || !*saved_host)
                saved_host = "-a";
            dlg_editbox_set(ctrl, dlg, saved_host);
#endif
	} else {
	    dlg_label_change(ctrl, dlg, HOST_BOX_TITLE);
	    dlg_editbox_set(ctrl, dlg, conf_get_str(conf, CONF_host));
	}
    } else if (event == EVENT_VALCHANGE) {
	char *s = dlg_editbox_get(ctrl, dlg);
	if (conf_get_int(conf, CONF_protocol) == PROT_SERIAL)
	    conf_set_str(conf, CONF_serline, s);
#ifdef CYGTERMPORT
	else if ( conf_get_int(conf, CONF_protocol) /*cfg->protocol*/ == PROT_CYGTERM) {
	char *s = dlg_editbox_get(ctrl, dlg);
	    conf_set_str(conf, CONF_cygcmd, s);
	}
#endif
	else
	    conf_set_str(conf, CONF_host, s);
	sfree(s);
    }
}

static void config_port_handler(union control *ctrl, void *dlg,
				void *data, int event)
{
    Conf *conf = (Conf *)data;
    char buf[80];

    /*
     * This function works similarly to the standard edit box handler,
     * only it has to choose the control's label and text from two
     * different places depending on the protocol.
     */
    if (event == EVENT_REFRESH) {
	if (conf_get_int(conf, CONF_protocol) == PROT_SERIAL) {
	    /*
	     * This label text is carefully chosen to contain a p,
	     * since that's the shortcut for the port control.
	     */
	    dlg_label_change(ctrl, dlg, "Speed");
	    sprintf(buf, "%d", conf_get_int(conf, CONF_serspeed));
#ifdef CYGTERMPORT
	} else if ( conf_get_int(conf, CONF_protocol) /*cfg->protocol*/ == PROT_CYGTERM) {
	    dlg_label_change(ctrl, dlg, "Port (ignored)");
	    strcpy(buf, "-");
#endif
	} else {
	    dlg_label_change(ctrl, dlg, PORT_BOX_TITLE);
	    if (conf_get_int(conf, CONF_port) != 0)
		sprintf(buf, "%d", conf_get_int(conf, CONF_port));
	    else
		/* Display an (invalid) port of 0 as blank */
		buf[0] = '\0';
	}
	dlg_editbox_set(ctrl, dlg, buf);
    } else if (event == EVENT_VALCHANGE) {
	char *s = dlg_editbox_get(ctrl, dlg);
	int i = atoi(s);
	sfree(s);

	if (conf_get_int(conf, CONF_protocol) == PROT_SERIAL)
	    conf_set_int(conf, CONF_serspeed, i);
#ifdef CYGTERMPORT
	else if ( conf_get_int(conf, CONF_protocol) == PROT_CYGTERM) ;
#endif
	else
	    conf_set_int(conf, CONF_port, i);
    }
}

struct hostport {
    union control *host, *port;
};

/*
 * We export this function so that platform-specific config
 * routines can use it to conveniently identify the protocol radio
 * buttons in order to add to them.
 */
void config_protocolbuttons_handler(union control *ctrl, void *dlg,
				    void *data, int event)
{
    int button;
    Conf *conf = (Conf *)data;
    struct hostport *hp = (struct hostport *)ctrl->radio.context.p;

    /*
     * This function works just like the standard radio-button
     * handler, except that it also has to change the setting of
     * the port box, and refresh both host and port boxes when. We
     * expect the context parameter to point at a hostport
     * structure giving the `union control's for both.
     */
    if (event == EVENT_REFRESH) {
	int protocol = conf_get_int(conf, CONF_protocol);
	for (button = 0; button < ctrl->radio.nbuttons; button++)
	    if (protocol == ctrl->radio.buttondata[button].i)
		break;
	/* We expected that `break' to happen, in all circumstances. */
	assert(button < ctrl->radio.nbuttons);
	dlg_radiobutton_set(ctrl, dlg, button);
    } else if (event == EVENT_VALCHANGE) {
	int oldproto = conf_get_int(conf, CONF_protocol);
	int newproto, port;

	button = dlg_radiobutton_get(ctrl, dlg);
	assert(button >= 0 && button < ctrl->radio.nbuttons);
	newproto = ctrl->radio.buttondata[button].i;
	conf_set_int(conf, CONF_protocol, newproto);

	if (oldproto != newproto) {
	    Backend *ob = backend_from_proto(oldproto);
	    Backend *nb = backend_from_proto(newproto);
	    assert(ob);
	    assert(nb);
	    /* Iff the user hasn't changed the port from the old protocol's
	     * default, update it with the new protocol's default.
	     * (This includes a "default" of 0, implying that there is no
	     * sensible default for that protocol; in this case it's
	     * displayed as a blank.)
	     * This helps with the common case of tabbing through the
	     * controls in order and setting a non-default port before
	     * getting to the protocol; we want that non-default port
	     * to be preserved. */
	    port = conf_get_int(conf, CONF_port);
	    if (port == ob->default_port)
		conf_set_int(conf, CONF_port, nb->default_port);
	}
	dlg_refresh(hp->host, dlg);
	dlg_refresh(hp->port, dlg);
    }
}

static void loggingbuttons_handler(union control *ctrl, void *dlg,
				   void *data, int event)
{
    int button;
    Conf *conf = (Conf *)data;
    /* This function works just like the standard radio-button handler,
     * but it has to fall back to "no logging" in situations where the
     * configured logging type isn't applicable.
     */
    if (event == EVENT_REFRESH) {
	int logtype = conf_get_int(conf, CONF_logtype);

        for (button = 0; button < ctrl->radio.nbuttons; button++)
            if (logtype == ctrl->radio.buttondata[button].i)
	        break;

	/* We fell off the end, so we lack the configured logging type */
	if (button == ctrl->radio.nbuttons) {
	    button = 0;
	    conf_set_int(conf, CONF_logtype, LGTYP_NONE);
	}
	dlg_radiobutton_set(ctrl, dlg, button);
    } else if (event == EVENT_VALCHANGE) {
        button = dlg_radiobutton_get(ctrl, dlg);
        assert(button >= 0 && button < ctrl->radio.nbuttons);
        conf_set_int(conf, CONF_logtype, ctrl->radio.buttondata[button].i);
    }
}

static void numeric_keypad_handler(union control *ctrl, void *dlg,
				   void *data, int event)
{
    int button;
    Conf *conf = (Conf *)data;
    /*
     * This function works much like the standard radio button
     * handler, but it has to handle two fields in Conf.
     */
    if (event == EVENT_REFRESH) {
	if (conf_get_int(conf, CONF_nethack_keypad))
	    button = 2;
	else if (conf_get_int(conf, CONF_app_keypad))
	    button = 1;
	else
	    button = 0;
	assert(button < ctrl->radio.nbuttons);
	dlg_radiobutton_set(ctrl, dlg, button);
    } else if (event == EVENT_VALCHANGE) {
	button = dlg_radiobutton_get(ctrl, dlg);
	assert(button >= 0 && button < ctrl->radio.nbuttons);
	if (button == 2) {
	    conf_set_int(conf, CONF_app_keypad, FALSE);
	    conf_set_int(conf, CONF_nethack_keypad, TRUE);
	} else {
	    conf_set_int(conf, CONF_app_keypad, (button != 0));
	    conf_set_int(conf, CONF_nethack_keypad, FALSE);
	}
    }
}

static void cipherlist_handler(union control *ctrl, void *dlg,
			       void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
	int i;

	static const struct { const char *s; int c; } ciphers[] = {
            { "ChaCha20 (SSH-2 only)",  CIPHER_CHACHA20 },
	    { "3DES",			CIPHER_3DES },
	    { "Blowfish",		CIPHER_BLOWFISH },
	    { "DES",			CIPHER_DES },
	    { "AES (SSH-2 only)",	CIPHER_AES },
	    { "Arcfour (SSH-2 only)",	CIPHER_ARCFOUR },
	    { "-- warn below here --",	CIPHER_WARN }
	};

	/* Set up the "selected ciphers" box. */
	/* (cipherlist assumed to contain all ciphers) */
	dlg_update_start(ctrl, dlg);
	dlg_listbox_clear(ctrl, dlg);
	for (i = 0; i < CIPHER_MAX; i++) {
	    int c = conf_get_int_int(conf, CONF_ssh_cipherlist, i);
	    int j;
	    const char *cstr = NULL;
	    for (j = 0; j < (sizeof ciphers) / (sizeof ciphers[0]); j++) {
		if (ciphers[j].c == c) {
		    cstr = ciphers[j].s;
		    break;
		}
	    }
	    dlg_listbox_addwithid(ctrl, dlg, cstr, c);
	}
	dlg_update_done(ctrl, dlg);

    } else if (event == EVENT_VALCHANGE) {
	int i;

	/* Update array to match the list box. */
	for (i=0; i < CIPHER_MAX; i++)
	    conf_set_int_int(conf, CONF_ssh_cipherlist, i,
			     dlg_listbox_getid(ctrl, dlg, i));
    }
}

#ifndef NO_GSSAPI
static void gsslist_handler(union control *ctrl, void *dlg,
			    void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
	int i;

	dlg_update_start(ctrl, dlg);
	dlg_listbox_clear(ctrl, dlg);
	for (i = 0; i < ngsslibs; i++) {
	    int id = conf_get_int_int(conf, CONF_ssh_gsslist, i);
	    assert(id >= 0 && id < ngsslibs);
	    dlg_listbox_addwithid(ctrl, dlg, gsslibnames[id], id);
	}
	dlg_update_done(ctrl, dlg);

    } else if (event == EVENT_VALCHANGE) {
	int i;

	/* Update array to match the list box. */
	for (i=0; i < ngsslibs; i++)
	    conf_set_int_int(conf, CONF_ssh_gsslist, i,
			     dlg_listbox_getid(ctrl, dlg, i));
    }
}
#endif

static void kexlist_handler(union control *ctrl, void *dlg,
			    void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
	int i;

	static const struct { const char *s; int k; } kexes[] = {
	    { "Diffie-Hellman group 1",		KEX_DHGROUP1 },
	    { "Diffie-Hellman group 14",	KEX_DHGROUP14 },
	    { "Diffie-Hellman group exchange",	KEX_DHGEX },
	    { "RSA-based key exchange", 	KEX_RSA },
            { "ECDH key exchange",              KEX_ECDH },
	    { "-- warn below here --",		KEX_WARN }
	};

	/* Set up the "kex preference" box. */
	/* (kexlist assumed to contain all algorithms) */
	dlg_update_start(ctrl, dlg);
	dlg_listbox_clear(ctrl, dlg);
	for (i = 0; i < KEX_MAX; i++) {
	    int k = conf_get_int_int(conf, CONF_ssh_kexlist, i);
	    int j;
	    const char *kstr = NULL;
	    for (j = 0; j < (sizeof kexes) / (sizeof kexes[0]); j++) {
		if (kexes[j].k == k) {
		    kstr = kexes[j].s;
		    break;
		}
	    }
	    dlg_listbox_addwithid(ctrl, dlg, kstr, k);
	}
	dlg_update_done(ctrl, dlg);

    } else if (event == EVENT_VALCHANGE) {
	int i;

	/* Update array to match the list box. */
	for (i=0; i < KEX_MAX; i++)
	    conf_set_int_int(conf, CONF_ssh_kexlist, i,
			     dlg_listbox_getid(ctrl, dlg, i));
    }
}

static void hklist_handler(union control *ctrl, void *dlg,
                            void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
        int i;

        static const struct { const char *s; int k; } hks[] = {
            { "Ed25519",               HK_ED25519 },
            { "ECDSA",                 HK_ECDSA },
            { "DSA",                   HK_DSA },
            { "RSA",                   HK_RSA },
            { "-- warn below here --", HK_WARN }
        };

        /* Set up the "host key preference" box. */
        /* (hklist assumed to contain all algorithms) */
        dlg_update_start(ctrl, dlg);
        dlg_listbox_clear(ctrl, dlg);
        for (i = 0; i < HK_MAX; i++) {
            int k = conf_get_int_int(conf, CONF_ssh_hklist, i);
            int j;
            const char *kstr = NULL;
            for (j = 0; j < lenof(hks); j++) {
                if (hks[j].k == k) {
                    kstr = hks[j].s;
                    break;
                }
            }
            dlg_listbox_addwithid(ctrl, dlg, kstr, k);
        }
        dlg_update_done(ctrl, dlg);

    } else if (event == EVENT_VALCHANGE) {
        int i;

        /* Update array to match the list box. */
        for (i=0; i < HK_MAX; i++)
            conf_set_int_int(conf, CONF_ssh_hklist, i,
                             dlg_listbox_getid(ctrl, dlg, i));
    }
}

static void printerbox_handler(union control *ctrl, void *dlg,
			       void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
	int nprinters, i;
	printer_enum *pe;
	const char *printer;

	dlg_update_start(ctrl, dlg);
	/*
	 * Some backends may wish to disable the drop-down list on
	 * this edit box. Be prepared for this.
	 */
	if (ctrl->editbox.has_list) {
	    dlg_listbox_clear(ctrl, dlg);
	    dlg_listbox_add(ctrl, dlg, PRINTER_DISABLED_STRING);
#ifdef PRINTCLIPPORT
	dlg_listbox_add(ctrl, dlg, PRINT_TO_CLIPBOARD_STRING);
#endif
	    pe = printer_start_enum(&nprinters);
	    for (i = 0; i < nprinters; i++)
		dlg_listbox_add(ctrl, dlg, printer_get_name(pe, i));
	    printer_finish_enum(pe);
	}
#ifdef PRINTCLIPPORT
	printer = conf_get_str(conf, CONF_printer);
 	if ( printer )
		dlg_editbox_set(ctrl, dlg, conf_get_str(conf,CONF_printer) ) ;
 	else if ( conf_get_int(conf,CONF_printclip) )
 		dlg_editbox_set(ctrl, dlg, PRINT_TO_CLIPBOARD_STRING);
	else
 		dlg_editbox_set(ctrl, dlg, PRINTER_DISABLED_STRING);
	dlg_update_done(ctrl, dlg);
#else
	printer = conf_get_str(conf, CONF_printer);
	if (!printer)
	    printer = PRINTER_DISABLED_STRING;
	dlg_editbox_set(ctrl, dlg, printer);
	dlg_update_done(ctrl, dlg);
#endif
    } else if (event == EVENT_VALCHANGE) {
	char *printer = dlg_editbox_get(ctrl, dlg);
#ifdef PRINTCLIPPORT
	if (!strcmp(printer, PRINTER_DISABLED_STRING)) {
		//printer[0] = '\0';
 		conf_set_int(conf,CONF_printclip,0);
 	} else if (!strcmp( printer, PRINT_TO_CLIPBOARD_STRING)) {
		//printer[0] = '\0';
		conf_set_int(conf,CONF_printclip,1);
 	}
	conf_set_str(conf, CONF_printer, printer);
	sfree(printer);
#else
	if (!strcmp(printer, PRINTER_DISABLED_STRING))
	    printer[0] = '\0';
	conf_set_str(conf, CONF_printer, printer);
	sfree(printer);
#endif
    }
}

static void codepage_handler(union control *ctrl, void *dlg,
			     void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
	int i;
	const char *cp, *thiscp;
	dlg_update_start(ctrl, dlg);
	thiscp = cp_name(decode_codepage(conf_get_str(conf,
						      CONF_line_codepage)));
	dlg_listbox_clear(ctrl, dlg);
	for (i = 0; (cp = cp_enumerate(i)) != NULL; i++)
	    dlg_listbox_add(ctrl, dlg, cp);
	dlg_editbox_set(ctrl, dlg, thiscp);
	conf_set_str(conf, CONF_line_codepage, thiscp);
	dlg_update_done(ctrl, dlg);
    } else if (event == EVENT_VALCHANGE) {
	char *codepage = dlg_editbox_get(ctrl, dlg);
	conf_set_str(conf, CONF_line_codepage,
		     cp_name(decode_codepage(codepage)));
	sfree(codepage);
    }
}

static void sshbug_handler(union control *ctrl, void *dlg,
			   void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
        /*
         * We must fetch the previously configured value from the Conf
         * before we start modifying the drop-down list, otherwise the
         * spurious SELCHANGE we trigger in the process will overwrite
         * the value we wanted to keep.
         */
        int oldconf = conf_get_int(conf, ctrl->listbox.context.i);
	dlg_update_start(ctrl, dlg);
	dlg_listbox_clear(ctrl, dlg);
	dlg_listbox_addwithid(ctrl, dlg, "Auto", AUTO);
	dlg_listbox_addwithid(ctrl, dlg, "Off", FORCE_OFF);
	dlg_listbox_addwithid(ctrl, dlg, "On", FORCE_ON);
	switch (oldconf) {
	  case AUTO:      dlg_listbox_select(ctrl, dlg, 0); break;
	  case FORCE_OFF: dlg_listbox_select(ctrl, dlg, 1); break;
	  case FORCE_ON:  dlg_listbox_select(ctrl, dlg, 2); break;
	}
	dlg_update_done(ctrl, dlg);
    } else if (event == EVENT_SELCHANGE) {
	int i = dlg_listbox_index(ctrl, dlg);
	if (i < 0)
	    i = AUTO;
	else
	    i = dlg_listbox_getid(ctrl, dlg, i);
	conf_set_int(conf, ctrl->listbox.context.i, i);
    }
}

#ifdef PERSOPORT
#include "kitty.h"
int dlg_listbox_get(union control *ctrl, void *dlg, int index, char * pstr, int maxcount) ;
int dlg_listbox_gettext(union control *ctrl, void *dlg, int index, char * pstr, int maxcount) ;

int StringList_Add( char **list, char *str ) ;
int StringList_Exist( const char **list, const char * name ) ;
void StringList_Del( char **list, const char * name ) ;
void StringList_Up( char **list, const char * name ) ;

void UpFolderInList( char * name ) ;

int CreateFolderInPath( const char * d ) ;
static void sessionsaver_handler(union control *ctrl, void *dlg, void *data, int event) ;
char *stristr (const char *meule_de_foin, const char *aiguille) ;
void RunPuTTY( HWND hwnd, char * param ) ;
void RunConfig( Conf * conf /*Config *cfg*/ ) ;
int RunSession( HWND hwnd, const char * folder_in, char * session_in ) ;

extern char ** FolderList ;

static char CurrentFolder[1024]="Default" ;
union control * ctrlSessionList = NULL ;
union control * ctrlSessionEdit = NULL ;
union control * ctrlFolderList = NULL ;

int get_param( const char * val ) ;
#ifndef SAVEMODE_REG
#define SAVEMODE_REG 0
#endif
#ifndef SAVEMODE_DIR
#define SAVEMODE_DIR 2
#endif

void SetInitCurrentFolder( const char * name ) { if( StringList_Exist((const char **)FolderList, name) ) strcpy( CurrentFolder, name ) ; }

static void folder_handler(union control *ctrl, void *dlg,
			   void *data, int event)
{
	int inc = 1 ;
	int pfold = 0 ;
	char buffer[1024] ;
	
	//Config *cfg = (Config *)data ;
	Conf * conf = conf_copy( data ) ; 
	ctrlFolderList = ctrl ;
	
	if( FolderList== NULL ) InitFolderList() ;

    if (event == EVENT_REFRESH) {
		dlg_update_start(ctrl, dlg);
		dlg_listbox_clear(ctrl, dlg);
		
		dlg_listbox_addwithid(ctrl, dlg, "Default", 0 ) ;
		while( FolderList[inc] != NULL ) {
			if( !strcmp( CurrentFolder, FolderList[inc] ) ) pfold=inc ;
			if( strlen(FolderList[inc]) > 0 )
				dlg_listbox_addwithid(ctrl, dlg, FolderList[inc], inc ) ;
			inc++;
			}	
		if( inc == 1 ) {
			dlg_listbox_select(ctrl, dlg, 0) ;
			}
		else {
			dlg_listbox_select(ctrl, dlg, pfold) ;
			}
		dlg_update_done(ctrl, dlg);
    } 
	else if (event == EVENT_SELCHANGE) {
		int i = dlg_listbox_index(ctrl, dlg);
		dlg_listbox_get(ctrl, dlg, i, buffer, 1024) ;
		conf_set_str( conf, CONF_folder, buffer ) ; //strcpy( cfg->folder, buffer ) ;
		strcpy( CurrentFolder, buffer ) ;
		
		if (i < 0)
			i = AUTO;
		else
			i = dlg_listbox_getid(ctrl, dlg, i);
		//*(int *)ATOFFSET(data, ctrl->listbox.context.i) = i;    // Code supprimé en 0.65.0.2 car problèmes de crash dans la selection d'un folder ... mais peut-être aur-t-on un pb par la suite.

		//dlg_editbox_get( ctrlSessionEdit, dlg, buffer, 1024 ) ;
		strcpy( buffer, dlg_editbox_get( ctrlSessionEdit, dlg ) ) ;
		
		//if( strcmp(buffer, "" ) ) { dlg_editbox_set(ctrlSessionEdit, dlg,"" ) ; strcpy(buffer,"") ; }
		// Impossible de reinitialiser le champ "Saved Sessions" sur un changement de folder
		// sinon on n'est pas capable de modifier le folder associee a une session
		
		if( !strcmp( buffer, "" ) ) 
			sessionsaver_handler( ctrlSessionList, dlg, data, EVENT_REFRESH ) ;
		}
	else if (event == EVENT_VALCHANGE) {
		int i = dlg_listbox_index( ctrl, dlg ) ;
		dlg_listbox_get(ctrl, dlg, i, buffer, 1024) ;
		
		conf_set_str( conf, CONF_folder, buffer ) ; //strcpy( cfg->folder, buffer ) ;
		strcpy( CurrentFolder, buffer ) ;
		
/*
else if (event == EVENT_VALCHANGE) {
	dlg_editbox_get(ctrl, dlg, cfg->line_codepage,
			sizeof(cfg->line_codepage));
	strcpy(cfg->line_codepage,
	       cp_name(decode_codepage(cfg->line_codepage)));
    }
*/
		}
	conf_free( conf ) ;
	}
#endif

struct sessionsaver_data {
#ifdef PERSOPORT
    union control *editbox, *listbox, *clearbutton, *loadbutton, *savebutton, *delbutton, *createbutton, *delfolderbutton, *arrangebutton
#if (defined PERSOPORT) && (!defined FDJ) && (defined STARTBUTTON)
	, *startbutton 
#endif
	;
	union control *folderlist ;
#else
    union control *editbox, *listbox, *loadbutton, *savebutton, *delbutton;
#endif
    union control *okbutton, *cancelbutton;
    struct sesslist sesslist;
    int midsession;
    char *savedsession;     /* the current contents of ssd->editbox */
};

static void sessionsaver_data_free(void *ssdv)
{
    struct sessionsaver_data *ssd = (struct sessionsaver_data *)ssdv;
    get_sesslist(&ssd->sesslist, FALSE);
    sfree(ssd->savedsession);
    sfree(ssd);
}

/* 
 * Helper function to load the session selected in the list box, if
 * any, as this is done in more than one place below. Returns 0 for
 * failure.
 */
static int load_selected_session(struct sessionsaver_data *ssd,
				 void *dlg, Conf *conf, int *maybe_launch)
{
    int i = dlg_listbox_index(ssd->listbox, dlg);
#ifdef PERSOPORT
	{
	char sessionname[1024] ;
	int j;
	dlg_listbox_gettext(ctrlSessionList, dlg, i, sessionname, 1024 ) ;
	for( j=0; j<ssd->sesslist.nsessions; j++ ){
		if( !strcmp( ssd->sesslist.sessions[j], sessionname ) ) i = j ;
		}
	if( (get_param("INIFILE")==2/*SAVEMODE_DIR*/) && get_param("DIRECTORYBROWSE")
		&& (sessionname[0]==' ')&&(sessionname[1]=='[')&&(sessionname[strlen(sessionname)-1]==']') ) {
		sessionname[strlen(sessionname)-1]='\0';

		strcpy( CurrentFolder, SetSessPath( sessionname+2 ) ) ;
		if( strlen(CurrentFolder) == 0 ) strcpy( CurrentFolder, "Default" ) ;
		conf_set_str(conf,CONF_folder,CurrentFolder);//strcpy( cfg->folder, CurrentFolder ) ;

		ssd->savedsession[0]='\0';
		get_sesslist(&ssd->sesslist, FALSE);
		get_sesslist(&ssd->sesslist, TRUE);
		dlg_refresh(ssd->listbox, dlg);
		sessionsaver_handler( ctrlSessionList, dlg, conf/*cfg*/, EVENT_REFRESH ) ;
		return 0 ;
		}
	}
#endif
    int isdef;
    if (i < 0) {
	dlg_beep(dlg);
	return 0;
    }
    isdef = !strcmp(ssd->sesslist.sessions[i], "Default Settings");
    load_settings(ssd->sesslist.sessions[i], conf);
#ifdef PERSOPORT
	if( (get_param("INIFILE")==SAVEMODE_DIR) && get_param("DIRECTORYBROWSE") ) {
		conf_set_str(conf,CONF_folder,CurrentFolder) /*strcpy( cfg->folder, CurrentFolder )*/ ;
		}
	else {
		strcpy( CurrentFolder, "Default" ) ;
		if( conf_get_str(conf,CONF_folder) /*cfg->folder*/ )
		if( strlen( conf_get_str(conf,CONF_folder) /*cfg->folder*/)>0 ) {
			strcpy( CurrentFolder, conf_get_str(conf,CONF_folder) /*cfg->folder*/ ) ;
			StringList_Add( FolderList, conf_get_str(conf,CONF_folder) /*cfg->folder*/ ) ;
			}
		}
#endif
    sfree(ssd->savedsession);
    ssd->savedsession = dupstr(isdef ? "" : ssd->sesslist.sessions[i]);
    if (maybe_launch)
        *maybe_launch = !isdef;
    dlg_refresh(NULL, dlg);
    /* Restore the selection, which might have been clobbered by
     * changing the value of the edit box. */
    dlg_listbox_select(ssd->listbox, dlg, i);
    return 1;
}

#ifdef PERSOPORT
int filter_sessionname( const char * pattern, const char * sessionname, const char * folder, const char * savedsession ) {
	char buffer[4092] ;
	int lenpattern = strlen(pattern) ;
	const char * searchpattern = savedsession+lenpattern ;
	while( ((searchpattern[0]==' ')||(searchpattern[0]=='	'))&&(searchpattern[0]!='\0') ) searchpattern++ ;
	if( stristr( savedsession, pattern ) != savedsession ) return 0 ;
	if( strlen(searchpattern)==0 ) return 0 ;

	if( stristr( savedsession, "host:" ) == savedsession ) {
		GetSessionField( sessionname, "", "HostName", buffer ) ; if( stristr( buffer, searchpattern )!=NULL ) return 1 ;
		GetSessionField( sessionname, "", "SFTPConnect", buffer ) ; if( stristr( buffer, searchpattern )!=NULL ) return 1 ;
		GetSessionField( sessionname, "", "RemoteCommand", buffer ) ; if( stristr( buffer, searchpattern )!=NULL ) return 1 ;
		GetSessionField( sessionname, "", "PortForwardings", buffer ) ; if( stristr( buffer, searchpattern )!=NULL ) return 1 ;
		GetSessionField( sessionname, "", "WinTitle", buffer ) ; if( stristr( buffer, searchpattern )!=NULL ) return 1 ;
	}
	if( stristr( savedsession, "user:" ) == savedsession ) {
		GetSessionField( sessionname, "", "HostName", buffer ) ; if( stristr( buffer, searchpattern )!=NULL ) return 1 ;
		GetSessionField( sessionname, "", "UserName", buffer ) ; if( stristr( buffer, searchpattern )!=NULL ) return 1 ;
		GetSessionField( sessionname, "", "LocalUserName", buffer ) ; if( stristr( buffer, searchpattern )!=NULL ) return 1 ;
		GetSessionField( sessionname, "", "SFTPConnect", buffer ) ; if( stristr( buffer, searchpattern )!=NULL ) return 1 ;
	}
	if( stristr( savedsession, "comment:" ) == savedsession ) {
		GetSessionField( sessionname, "", "Comment", buffer ) ; if( stristr( buffer, searchpattern )!=NULL ) return 1 ;
		
	}
	if( stristr( savedsession, "title:" ) == savedsession ) {
		GetSessionField( sessionname, "", "WinTitle", buffer ) ; if( stristr( buffer, searchpattern )!=NULL ) return 1 ;
		
	}
	if( stristr( savedsession, "class:" ) == savedsession ) {
		GetSessionField( sessionname, "", "WindowClass", buffer ) ; if( stristr( buffer, searchpattern )!=NULL ) return 1 ;
		
	}
	return 0 ;
}

int isSessionExist( struct sessionsaver_data *ssd, char * name ) {
	int res = 0 ;
	int j;
	for( j=0; j<ssd->sesslist.nsessions; j++ ){
		if( !strcmp( ssd->sesslist.sessions[j], name ) ) { res = 1 ; break ; }
	}
	return res ;
	}
#endif

static void sessionsaver_handler(union control *ctrl, void *dlg,
				 void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct sessionsaver_data *ssd =
	(struct sessionsaver_data *)ctrl->generic.context.p;

    if (event == EVENT_REFRESH) {
	if (ctrl == ssd->editbox) {
#ifdef PERSOPORT
	    ctrlSessionEdit = ctrl ;
#endif
	    dlg_editbox_set(ctrl, dlg, ssd->savedsession);

	} else if (ctrl == ssd->listbox) {
	    int i;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
#ifdef PERSOPORT
		/*if( ssd->savedsession!=NULL ) strcpy( ssd->savedsession, dlg_editbox_get( ssd->editbox, dlg ) ) ; // ajout 0.63 pour que le filtre fonctionne
		
		ctrlSessionList = ctrl ;
		if(get_param("INIFILE")==SAVEMODE_DIR) CleanFolderName( CurrentFolder ) ;
		for (i = 0; i < ssd->sesslist.nsessions; i++) {
			char folder[1024] ;
			char host[1024] ;
			strcpy( folder, "" ) ;
			GetSessionFolderName( ssd->sesslist.sessions[i], folder ) ;
			if( (get_param("INIFILE")==SAVEMODE_DIR) && get_param("DIRECTORYBROWSE") ) CleanFolderName( folder ) ;
			strcpy( host, "" ) ;
			GetSessionField( ssd->sesslist.sessions[i], folder, "HostName", host ) ;
			if( get_param("PUTTY") )
				dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]);
			else if( (get_param("INIFILE")==SAVEMODE_DIR) && get_param("DIRECTORYBROWSE") )	{
				if (!strcmp( ssd->savedsession, "" )) {
					if( (!strcmp(CurrentFolder,folder))
						|| (!strcmp("",folder))
						|| ( strstr(ssd->sesslist.sessions[i]," [")==ssd->sesslist.sessions[i] )
						)
						dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]);
					}
				else {
					if( (!strcmp(CurrentFolder,folder))||(!strcmp("",folder)) ) {
						if( !GetSessionFilterFlag() ) // filtre desactive
							dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]);
						else if( (stristr(ssd->sesslist.sessions[i],ssd->savedsession)!=NULL) 
							|| ( strstr(ssd->sesslist.sessions[i]," [")==ssd->sesslist.sessions[i] ) )
							dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]);
						}
					}
				}
			else if( !GetSessionFilterFlag() ) // filtre desactive
				dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]);
			else 
			if( (!strcmp( CurrentFolder, "Default" )) || (!strcmp(CurrentFolder,folder)) ) {
				if( (!strcmp( ssd->savedsession, "" )) 
				|| ( strlen(ssd->savedsession)<=1 )
				|| (stristr(host,ssd->savedsession)!=NULL)
				|| (stristr(ssd->sesslist.sessions[i],ssd->savedsession)!=NULL) )
					dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]) ;
				}
				
			}*/
		if( ssd->savedsession!=NULL ) strcpy( ssd->savedsession, dlg_editbox_get( ssd->editbox, dlg ) ) ; // ajout 0.63 pour que le filtre fonctionne
		
		ctrlSessionList = ctrl ;
		if(get_param("INIFILE")==SAVEMODE_DIR) CleanFolderName( CurrentFolder ) ;

		for (i = 0; i < ssd->sesslist.nsessions; i++) {
			char folder[1024] ;
//			char host[1024] ;
			strcpy( folder, "" ) ;
			if( (get_param("INIFILE")==SAVEMODE_REG) || ((get_param("INIFILE")==SAVEMODE_DIR) && !get_param("DIRECTORYBROWSE")) ) {
				GetSessionFolderName( ssd->sesslist.sessions[i], folder ) ;
				}
//			strcpy( host, "" ) ;
//			if(get_param("INIFILE")==SAVEMODE_REG) {
//				GetSessionField( ssd->sesslist.sessions[i], folder, "HostName", host ) ;
//				}
			if( get_param("PUTTY") )
				dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]);
			else if( (get_param("INIFILE")==SAVEMODE_DIR) && get_param("DIRECTORYBROWSE") )	{
				CleanFolderName( folder ) ;
				if (!strcmp( ssd->savedsession, "" )) {
					if( (!strcmp(CurrentFolder,folder))
						|| (!strcmp("",folder))
						|| ( strstr(ssd->sesslist.sessions[i]," [")==ssd->sesslist.sessions[i] )
						)
						dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]);
					}
				else {
					if( (!strcmp(CurrentFolder,folder))||(!strcmp("",folder)) ) {
						if( !GetSessionFilterFlag() ) // filtre desactive
							dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]);
						else if( (stristr(ssd->sesslist.sessions[i],ssd->savedsession)!=NULL) 
							|| ( strstr(ssd->sesslist.sessions[i]," [")==ssd->sesslist.sessions[i] ) )
							dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]);
						}
					}
				}
			else if( !GetSessionFilterFlag() ) // filtre desactive
				dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]);
			else {
				if( (!strcmp(CurrentFolder, "Default" )&&GetSessionsInDefaultFlag()) 
				    || (!strcmp(CurrentFolder,folder)) ) {
					if( (!strcmp( ssd->savedsession, "" )) 
					|| ( strlen(ssd->savedsession)<=1 )
					|| ( filter_sessionname( "host:", ssd->sesslist.sessions[i], folder, ssd->savedsession ) )	/* Filtre sur le nom du host */
					|| ( filter_sessionname( "user:", ssd->sesslist.sessions[i], folder, ssd->savedsession ) )
					|| ( filter_sessionname( "comment:", ssd->sesslist.sessions[i], folder, ssd->savedsession ) )
					|| ( filter_sessionname( "title:", ssd->sesslist.sessions[i], folder, ssd->savedsession ) )
					|| ( filter_sessionname( "class:", ssd->sesslist.sessions[i], folder, ssd->savedsession ) )
					//|| (stristr(host,ssd->savedsession)!=NULL)			/* Filtre sur le nom du host */
					|| (stristr(ssd->sesslist.sessions[i],ssd->savedsession)!=NULL) )
						dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]) ;
					}
				}
				
			}
#else 
	    for (i = 0; i < ssd->sesslist.nsessions; i++)
		dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]);
#endif
	    dlg_update_done(ctrl, dlg);
	}
    } else if (event == EVENT_VALCHANGE) {
        int top, bottom, halfway, i;
	if (ctrl == ssd->editbox) {
            sfree(ssd->savedsession);
            ssd->savedsession = dlg_editbox_get(ctrl, dlg);
#ifdef PERSOPORT
	sessionsaver_handler( ctrlSessionList, dlg, data, EVENT_REFRESH ) ;
#endif
	    top = ssd->sesslist.nsessions;
	    bottom = -1;
	    while (top-bottom > 1) {
	        halfway = (top+bottom)/2;
	        i = strcmp(ssd->savedsession, ssd->sesslist.sessions[halfway]);
	        if (i <= 0 ) {
		    top = halfway;
	        } else {
		    bottom = halfway;
	        }
	    }
	    if (top == ssd->sesslist.nsessions) {
	        top -= 1;
	    }
	    dlg_listbox_select(ssd->listbox, dlg, top);
	}
    } else if (event == EVENT_ACTION) {
	int mbl = FALSE;
	if (!ssd->midsession &&
	    (ctrl == ssd->listbox ||
	     (ssd->loadbutton && ctrl == ssd->loadbutton))) {
	    /*
	     * The user has double-clicked a session, or hit Load.
	     * We must load the selected session, and then
	     * terminate the configuration dialog _if_ there was a
	     * double-click on the list box _and_ that session
	     * contains a hostname.
	     */
#ifdef PERSOPORT
	     if (load_selected_session(ssd, dlg, conf, &mbl) &&
		(mbl && ctrl == ssd->listbox && conf_launchable(conf))) {
		if( conf_get_int(conf, CONF_protocol) != PROT_SERIAL ) {
		     char buffer[1024] ;
		     strcpy( buffer, conf_get_str( conf, CONF_password) ) ;
		     MASKPASS( buffer ) ;
		     conf_set_str( conf, CONF_password, buffer ) ;
		     memset( buffer, 0, strlen(buffer) ) ;
		     }	
		dlg_end(dlg, 1);       /* it's all over, and succeeded */
	    }
	if( conf_get_int(conf, CONF_protocol) != PROT_SERIAL ) { 
		     char buffer[1024] ;
		     strcpy( buffer, conf_get_str( conf, CONF_password) ) ;
		     MASKPASS( buffer ) ;
		     conf_set_str( conf, CONF_password, buffer ) ;
		     memset( buffer, 0, strlen(buffer) ) ;
		}
#else
	     if (load_selected_session(ssd, dlg, conf, &mbl) &&
		(mbl && ctrl == ssd->listbox && conf_launchable(conf))) {
		dlg_end(dlg, 1);       /* it's all over, and succeeded */
	    }
#endif

	} else if (ctrl == ssd->savebutton) {
	    int isdef = !strcmp(ssd->savedsession, "Default Settings");
	    if (!ssd->savedsession[0]) {
		int i = dlg_listbox_index(ssd->listbox, dlg);
		if (i < 0) {
		    dlg_beep(dlg);
		    return;
		}
		isdef = !strcmp(ssd->sesslist.sessions[i], "Default Settings");
                sfree(ssd->savedsession);
                ssd->savedsession = dupstr(isdef ? "" :
                                           ssd->sesslist.sessions[i]);
	    }
            {
#ifdef PERSOPORT
		conf_set_str( conf, CONF_folder, CurrentFolder ) ;
		if( conf_get_int(conf, CONF_protocol) != PROT_SERIAL ) {
		     char buffer[1024] ;
		     strcpy( buffer, conf_get_str( conf, CONF_password) ) ;
		     MASKPASS( buffer ) ;
		     conf_set_str( conf, CONF_password, buffer ) ;
		     memset( buffer, 0, strlen(buffer) ) ;
		     }
		char *errmsg = save_settings(ssd->savedsession, conf);
		if( conf_get_int(conf, CONF_protocol) != PROT_SERIAL ) {
		     char buffer[1024] ;
		     strcpy( buffer, conf_get_str( conf, CONF_password) ) ;
		     MASKPASS( buffer ) ;
		     conf_set_str( conf, CONF_password, buffer ) ;
		     memset( buffer, 0, strlen(buffer) ) ;
	            }
#else
                char *errmsg = save_settings(ssd->savedsession, conf);
#endif
                if (errmsg) {
                    dlg_error_msg(dlg, errmsg);
                    sfree(errmsg);
                }
            }
	    get_sesslist(&ssd->sesslist, FALSE);
	    get_sesslist(&ssd->sesslist, TRUE);
	    dlg_refresh(ssd->editbox, dlg);
	    dlg_refresh(ssd->listbox, dlg);
	} else if (!ssd->midsession &&
		   ssd->delbutton && ctrl == ssd->delbutton) {
	    int i = dlg_listbox_index(ssd->listbox, dlg);
#ifdef PERSOPORT
		{ // Pour recuperer le bon index de la session a supprimer quand il y le filtre des sessions
		char sessionname[1024] ;
		int j;
		dlg_listbox_gettext(ctrlSessionList, dlg, i, sessionname, 1024 ) ;
		for( j=0; j<ssd->sesslist.nsessions; j++ ){
			if( !strcmp( ssd->sesslist.sessions[j], sessionname ) ) i = j ;
			}
		}
#endif
	    if (i <= 0) {
		dlg_beep(dlg);
	    } else {
		del_settings(ssd->sesslist.sessions[i]);
		get_sesslist(&ssd->sesslist, FALSE);
		get_sesslist(&ssd->sesslist, TRUE);
		dlg_refresh(ssd->listbox, dlg);
	    }
#ifdef PERSOPORT
	ssd->savedsession[0]='\0' ;
	dlg_refresh(ssd->editbox, dlg) ;
	sessionsaver_handler( ctrlSessionList, dlg, data, EVENT_REFRESH ) ;
#endif
	} else if (ctrl == ssd->okbutton) {
            if (ssd->midsession) {
                /* In a mid-session Change Settings, Apply is always OK. */
		dlg_end(dlg, 1);
                return;
            }
#ifdef PERSOPORT
	if( conf_get_int(conf, CONF_protocol) != PROT_SERIAL ) {
		     char buffer[1024] ;
		     strcpy( buffer, conf_get_str( conf, CONF_password) ) ;
		     MASKPASS( buffer ) ;
		     conf_set_str( conf, CONF_password, buffer ) ;
		     memset( buffer, 0, strlen(buffer) ) ;
		}
#endif
	    /*
	     * Annoying special case. If the `Open' button is
	     * pressed while no host name is currently set, _and_
	     * the session list previously had the focus, _and_
	     * there was a session selected in that which had a
	     * valid host name in it, then load it and go.
	     */
	    if (dlg_last_focused(ctrl, dlg) == ssd->listbox &&
		!conf_launchable(conf)) {
		Conf *conf2 = conf_new();
		int mbl = FALSE;
		if (!load_selected_session(ssd, dlg, conf2, &mbl)) {
		    dlg_beep(dlg);
		    conf_free(conf2);
		    return;
		}
		/* If at this point we have a valid session, go! */
		if (mbl && conf_launchable(conf2)) {
		    conf_copy_into(conf, conf2);
		    dlg_end(dlg, 1);
		} else
		    dlg_beep(dlg);

		conf_free(conf2);
                return;
	    }

	    /*
	     * Otherwise, do the normal thing: if we have a valid
	     * session, get going.
	     */
	    if (conf_launchable(conf)) {
		dlg_end(dlg, 1);
	    } else
		dlg_beep(dlg);
	} else if (ctrl == ssd->cancelbutton) {
	    dlg_end(dlg, 0);
	}
#ifdef PERSOPORT
#if (defined IMAGEPORT) && (!defined FDJ) && (defined STARTBUTTON)
	else if (ctrl == ssd->startbutton) {
	
	/*
	   int already_run = 0 ;
	
	   if( dlg_last_focused(ctrl, dlg) == ssd->listbox ) {
		Conf *conf2 = conf_new() ; 
		int mbl = FALSE;
		char *oldsavedsession ;
		oldsavedsession=(char*)malloc(strlen(ssd->savedsession)+1);strcpy(oldsavedsession,ssd->savedsession);
		if (!load_selected_session(ssd, dlg, conf2, &mbl)) { dlg_beep(dlg); }
		// If at this point we have a valid session, go!
		if (mbl && conf_launchable(conf2)) {
			conf_copy_into(conf,conf2); // structure copy
			conf_set_str(conf,CONF_remote_cmd,"");
			} else
			dlg_beep(dlg);
	    
		if (conf_launchable(conf)) { RunSession( hwnd, CurrentFolder, ssd->savedsession ) ; already_run=1 ; }
		strcpy(ssd->savedsession,oldsavedsession); free(oldsavedsession);
		//dlg_refresh(ssd->editbox, dlg) ;
		//sessionsaver_handler( ctrlSessionList, dlg, data, EVENT_REFRESH ) ;
		conf_free(conf2);
		}
	   if( !already_run) {
		if( strlen(ssd->savedsession)>0 ) {
			if( conf_launchable(conf) ) { if( RunSession( hwnd, CurrentFolder, ssd->savedsession ) ) { already_run=1 ; } }
			}
		if( !already_run && strlen(ssd->savedsession)==0 ) {
			if(conf_launchable(conf)) { RunConfig(conf) ; already_run=1; }
			else { dlg_beep(dlg); }
			}
		}
	*/
	
	if( conf_launchable(conf) ) {
		RunConfig(conf) ;	
	} else if( (strlen(ssd->savedsession)>0) && isSessionExist(ssd,ssd->savedsession) ) {
		if( !RunSession( hwnd, CurrentFolder, ssd->savedsession ) ) {  dlg_beep(dlg) ; } 
	} else if( dlg_last_focused(ctrl, dlg) == ssd->listbox ) { 
		Conf *conf2 = conf_new() ; 
		int mbl = FALSE;
		char *oldsavedsession ;
		oldsavedsession=(char*)malloc(strlen(ssd->savedsession)+1);strcpy(oldsavedsession,ssd->savedsession);
		if (!load_selected_session(ssd, dlg, conf2, &mbl)) { dlg_beep(dlg); }
		// If at this point we have a valid session, go!
		if (mbl && conf_launchable(conf2)) {
			conf_copy_into(conf,conf2); // structure copy
			conf_set_str(conf,CONF_remote_cmd,"");
			} else
			dlg_beep(dlg);
	    
		if (conf_launchable(conf)) { RunSession( hwnd, CurrentFolder, ssd->savedsession ) ; }
		strcpy(ssd->savedsession,oldsavedsession); free(oldsavedsession);
		//dlg_refresh(ssd->editbox, dlg) ;
		//sessionsaver_handler( ctrlSessionList, dlg, data, EVENT_REFRESH ) ;
		conf_free(conf2);	
	} else { dlg_beep(dlg) ; 
	}
	
	}
#endif
	else if (!ssd->midsession && // vide le nom de la session
		   ssd->clearbutton && ctrl == ssd->clearbutton) {
			SetInitCurrentFolder( "Default" );
			folder_handler( ctrlFolderList, dlg, data, EVENT_REFRESH ) ;
			do_defaults("Default Settings", conf) ;
			conf_set_str(conf,CONF_host,"");
			dlg_editbox_set(ctrlHostnameEdit, dlg,"");
			if( strlen(ssd->savedsession) > 0 ) {
				ssd->savedsession[0]='\0' ;
				dlg_refresh(ssd->editbox, dlg) ;
				}
			sessionsaver_handler( ctrlSessionList, dlg, data, EVENT_REFRESH ) ;
			}
	else if (!ssd->midsession && // creer un nouveau folder
		   ssd->createbutton && ctrl == ssd->createbutton) {
			if( strlen(ssd->savedsession) > 0 ) {
				if( !stricmp(ssd->savedsession,"Default") ) {
					MessageBox( NULL, "Your are not allowed to create a folder called Default !", "Error", MB_OK|MB_ICONERROR ) ;
				} else
				if( !get_param("DIRECTORYBROWSE") ) {
					InitFolderList() ;
					StringList_Add( FolderList, ssd->savedsession ) ;
					SaveFolderList();
					folder_handler( ctrlFolderList, dlg, data, EVENT_REFRESH ) ;
					ssd->savedsession[0] = '\0';
					dlg_editbox_set( ctrlSessionEdit, dlg, "" ) ;
					}
				else {
					if( !CreateFolderInPath( ssd->savedsession ) ) {
						MessageBox( NULL, "Your are not allowed to create sessions folder directory", "Error", MB_OK|MB_ICONERROR ) ;
					}
					ssd->savedsession[0] = '\0';
					dlg_editbox_set( ctrlSessionEdit, dlg, "" ) ;
					get_sesslist(&ssd->sesslist, FALSE);
					get_sesslist(&ssd->sesslist, TRUE);
					dlg_refresh(ssd->listbox, dlg);
					//sessionsaver_handler( ctrlSessionList, dlg, cfg, EVENT_REFRESH ) ;
					}
				}
		   }
	else if (!ssd->midsession && // supprimer un folder
		   ssd->delfolderbutton && ctrl == ssd->delfolderbutton) {
		   if( strlen(CurrentFolder) > 0 ) {
				if( !strcmp( CurrentFolder, "Default" ) )
					MessageBox( NULL, "To delete Default folder is not allowed", "Deleting error", MB_OK|MB_ICONERROR );
				else {
					StringList_Del( FolderList, CurrentFolder ) ;
					SaveFolderList() ;
					InitFolderList() ;
					strcpy( CurrentFolder,"Default" ) ;
					folder_handler( ctrlFolderList, dlg, data, EVENT_REFRESH ) ;
					ssd->savedsession[0] = '\0';
					dlg_editbox_set( ctrlSessionEdit, dlg, "" ) ;
					//sessionsaver_handler( ctrlSessionList, dlg, data, EVENT_REFRESH ) ;
					}
				}
		   }
	else if (!ssd->midsession && // Reordonner les folders
		   ssd->arrangebutton && ctrl == ssd->arrangebutton) {
		   if( strlen(CurrentFolder) > 0 ) 
			if( strcmp( CurrentFolder, "Default" ) ) {
				StringList_Up( FolderList, CurrentFolder ) ;
				SaveFolderList() ;
				InitFolderList() ;
				folder_handler( ctrlFolderList, dlg, data, EVENT_REFRESH ) ;
				ssd->savedsession[0] = '\0';
				dlg_editbox_set( ctrlSessionEdit, dlg, "" ) ;
				}
		   }
#endif
    }
}

struct charclass_data {
    union control *listbox, *editbox, *button;
};

static void charclass_handler(union control *ctrl, void *dlg,
			      void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct charclass_data *ccd =
	(struct charclass_data *)ctrl->generic.context.p;

    if (event == EVENT_REFRESH) {
	if (ctrl == ccd->listbox) {
	    int i;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
	    for (i = 0; i < 128; i++) {
		char str[100];
		sprintf(str, "%d\t(0x%02X)\t%c\t%d", i, i,
			(i >= 0x21 && i != 0x7F) ? i : ' ',
			conf_get_int_int(conf, CONF_wordness, i));
		dlg_listbox_add(ctrl, dlg, str);
	    }
	    dlg_update_done(ctrl, dlg);
	}
    } else if (event == EVENT_ACTION) {
	if (ctrl == ccd->button) {
	    char *str;
	    int i, n;
	    str = dlg_editbox_get(ccd->editbox, dlg);
	    n = atoi(str);
	    sfree(str);
	    for (i = 0; i < 128; i++) {
		if (dlg_listbox_issel(ccd->listbox, dlg, i))
		    conf_set_int_int(conf, CONF_wordness, i, n);
	    }
	    dlg_refresh(ccd->listbox, dlg);
	}
    }
}

struct colour_data {
#ifdef TUTTYPORT
	    union control *listbox, *redit, *gedit, *bedit, *button,
	*bold_checkbox, *underline_checkbox, *selected_checkbox;
#else
    union control *listbox, *redit, *gedit, *bedit, *button;
#endif
};

static const char *const colours[] = {
#ifdef TUTTYPORT
    "Default Foreground", "Default Bold Foreground",
    "Default Underlined Foreground",
    "Default Background", "Default Bold Background",
    "Default Underlined Background",
    "Cursor Text", "Cursor Colour",
    "Selected Text Foreground", "Selected Text Background",
    "ANSI Black", "ANSI Black Bold", "ANSI Black Underlined",
    "ANSI Red", "ANSI Red Bold", "ANSI Red Underlined",
    "ANSI Green", "ANSI Green Bold", "ANSI Green Underlined",
    "ANSI Yellow", "ANSI Yellow Bold", "ANSI Yellow Underlined",
    "ANSI Blue", "ANSI Blue Bold", "ANSI Blue Underlined",
    "ANSI Magenta", "ANSI Magenta Bold", "ANSI Magenta Underlined",
    "ANSI Cyan", "ANSI Cyan Bold", "ANSI Cyan Underlined",
    "ANSI White", "ANSI White Bold", "ANSI White Underlined"
#else
    "Default Foreground", "Default Bold Foreground",
    "Default Background", "Default Bold Background",
    "Cursor Text", "Cursor Colour",
    "ANSI Black", "ANSI Black Bold",
    "ANSI Red", "ANSI Red Bold",
    "ANSI Green", "ANSI Green Bold",
    "ANSI Yellow", "ANSI Yellow Bold",
    "ANSI Blue", "ANSI Blue Bold",
    "ANSI Magenta", "ANSI Magenta Bold",
    "ANSI Cyan", "ANSI Cyan Bold",
    "ANSI White", "ANSI White Bold"
#endif
};

#ifdef TUTTYPORT
void dlg_control_enable(union control *ctrl, void *dlg, int enable);
static const int itemcolour[] = {
    0, 1, 2, 0, 1, 2, 0, 0, 3, 3, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2,
    0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2
};

static const int idxcolour[2][2][2][34] = {
    {
     {
      {0, 2, 4, 5, 6, 8, 10, 12, 14, 16,
       18, 20, -1, -1, -1, -1, -1, -1, -1, -1,
       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
       -1, -1, -1, -1},		/* 0, 0, 0: !bold && !under && !selected */
      {0, 2, 4, 5, 32, 33, 6, 8, 10, 12,
       18, 20, -1, -1, -1, -1, -1, -1, -1, -1,
       -1, -1, -1, -1}		/* 0, 0, 1: !bold && !under && selected */
      },
     {
      {0, 22, 2, 23, 4, 5, 6, 24, 8, 25,
       10, 26, 12, 27, 14, 28, 16, 29, 18, 30,
       20, 31, -1, -1, -1, -1, -1, -1, -1, -1,
       -1, -1, -1, -1},		/* 0, 1, 0: !bold && under && !selected */
      {0, 22, 2, 23, 4, 5, 32, 33, 6, 24,
       8, 25, 10, 26, 12, 27, 14, 28, 16, 29,
       18, 30, 20, 31, -1, -1, -1, -1, -1, -1,
       -1, -1, -1, -1}		/* 0, 1, 1: !bold && under && selected */
      },
     },
    {
     {
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
       10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
       20, 21, -1, -1, -1, -1, -1, -1, -1, -1,
       -1, -1, -1, -1},		/* 1, 0, 0: bold && !under && !selected */
      {0, 1, 2, 3, 4, 5, 32, 33, 6, 7,
       8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
       18, 19, 20, 21, -1, -1, -1, -1, -1, -1,
       -1, -1, -1, -1}		/* 1, 0, 1: bold && !under && selected */
      },
     {
      {0, 1, 22, 2, 3, 23, 4, 5, 6, 7,
       24, 8, 9, 25, 10, 11, 26, 12, 13, 27,
       14, 15, 28, 16, 17, 29, 18, 19, 30, 20,
       21, 31, -1, -1},		/* 1, 1, 0: bold && under && !selected */
      {0, 1, 22, 2, 3, 23, 4, 5, 32, 33,
       6, 7, 24, 8, 9, 25, 10, 11, 26, 12,
       13, 27, 14, 15, 28, 16, 17, 29, 18, 19,
       30, 20, 21, 31}		/* 1, 1, 1: bold && under && selected */
      },
     }
};
#endif

static void colour_handler(union control *ctrl, void *dlg,
			    void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct colour_data *cd =
	(struct colour_data *)ctrl->generic.context.p;
    int update = FALSE, clear = FALSE, r, g, b;

#ifdef TUTTYPORT
    if (ctrl == cd->bold_checkbox) {
	    	conf_set_int(conf,CONF_bold_colour,1);
		dlg_control_enable(cd->bold_checkbox, dlg,0);
	switch (event) {
	case EVENT_REFRESH:
	    dlg_checkbox_set(cd->bold_checkbox, dlg, conf_get_int(conf,CONF_bold_colour) );
	    break;
	case EVENT_VALCHANGE:
	    conf_set_int(conf,CONF_bold_colour, dlg_checkbox_get(cd->bold_checkbox, dlg));
	    dlg_refresh(cd->listbox, dlg);
	    break;
	};
    } else if (ctrl == cd->underline_checkbox) {
	switch (event) {
	case EVENT_REFRESH:
	    dlg_checkbox_set(cd->underline_checkbox, dlg,
			     conf_get_int(conf,CONF_under_colour) );
	    break;
	case EVENT_VALCHANGE:
	    conf_set_int( conf, CONF_under_colour, dlg_checkbox_get(cd->underline_checkbox, dlg) ) ;
	    dlg_refresh(cd->listbox, dlg);
	    break;
	};
    } else if (ctrl == cd->selected_checkbox) {
	switch (event) {
	case EVENT_REFRESH:
	    dlg_checkbox_set(cd->selected_checkbox, dlg, conf_get_int(conf,CONF_sel_colour) );
	    break;
	case EVENT_VALCHANGE:
	    conf_set_int( conf, CONF_sel_colour, dlg_checkbox_get(cd->selected_checkbox, dlg) ) ;
	    dlg_refresh(cd->listbox, dlg);
	    break;
	};
    };
#endif
    if (event == EVENT_REFRESH) {
	if (ctrl == cd->listbox) {
	    int i;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
	    for (i = 0; i < lenof(colours); i++)
#ifdef TUTTYPORT
		/* This allows us to hide list items we don't need to
		 * see: if bold-as-colour (or underline) turned off, we just hide those bold
		 * choices to decrease user confusion. And, of course, it looks
		 * lots cooler to have "jumping" controls. Feels more interactive. :)
		 */
	    {
		switch (itemcolour[i]) {
		case 0:
		    dlg_listbox_add(ctrl, dlg, colours[i]);
		    break;
		case 1:
		    if (conf_get_int(conf,CONF_bold_colour))
			dlg_listbox_add(ctrl, dlg, colours[i]);
		    break;
		case 2:
		    if (conf_get_int(conf,CONF_under_colour))
			dlg_listbox_add(ctrl, dlg, colours[i]);
		    break;
		case 3:
		    if (conf_get_int(conf,CONF_sel_colour))
			dlg_listbox_add(ctrl, dlg, colours[i]);
		};
	    };
#else
		dlg_listbox_add(ctrl, dlg, colours[i]);
#endif
	    dlg_update_done(ctrl, dlg);
	    clear = TRUE;
	    update = TRUE;
	}
    } else if (event == EVENT_SELCHANGE) {
	if (ctrl == cd->listbox) {
	    /* The user has selected a colour. Update the RGB text. */
	    int i = dlg_listbox_index(ctrl, dlg);
	    if (i < 0) {
		clear = TRUE;
	    } else {
		clear = FALSE;
#ifdef TUTTYPORT
	    /* I know this looks a bit weird, but I just had no
	     * other choice. Other way it would break existing code and
	     * worse yet, existing saved session structure.
	     * This way, underline colours are stored in last 10
	     * positions of the colours array, not breaking anything.
	     * But the price is some weird transformations based on
	     * predefined index array.
	     */
	    i = idxcolour[conf_get_int(conf,CONF_bold_colour)][conf_get_int(conf,CONF_under_colour)][conf_get_int(conf,CONF_sel_colour)][i];
#endif
		r = conf_get_int_int(conf, CONF_colours, i*3+0);
		g = conf_get_int_int(conf, CONF_colours, i*3+1);
		b = conf_get_int_int(conf, CONF_colours, i*3+2);
	    }
	    update = TRUE;
	}
    } else if (event == EVENT_VALCHANGE) {
	if (ctrl == cd->redit || ctrl == cd->gedit || ctrl == cd->bedit) {
	    /* The user has changed the colour using the edit boxes. */
	    char *str;
	    int i, cval;

	    str = dlg_editbox_get(ctrl, dlg);
	    cval = atoi(str);
	    sfree(str);
	    if (cval > 255) cval = 255;
	    if (cval < 0)   cval = 0;

	    i = dlg_listbox_index(cd->listbox, dlg);
	    if (i >= 0) {
#ifdef TUTTYPORT
    		i = idxcolour[conf_get_int(conf,CONF_bold_colour)][conf_get_int(conf,CONF_under_colour)][conf_get_int(conf,CONF_sel_colour)][i];

#endif
		if (ctrl == cd->redit)
		    conf_set_int_int(conf, CONF_colours, i*3+0, cval);
		else if (ctrl == cd->gedit)
		    conf_set_int_int(conf, CONF_colours, i*3+1, cval);
		else if (ctrl == cd->bedit)
		    conf_set_int_int(conf, CONF_colours, i*3+2, cval);
	    }
	}
    } else if (event == EVENT_ACTION) {
	if (ctrl == cd->button) {
	    int i = dlg_listbox_index(cd->listbox, dlg);
	    if (i < 0) {
		dlg_beep(dlg);
		return;
	    }
	    /*
	     * Start a colour selector, which will send us an
	     * EVENT_CALLBACK when it's finished and allow us to
	     * pick up the results.
	     */
#ifdef TUTTYPORT
 i = idxcolour[conf_get_int(conf,CONF_bold_colour)][conf_get_int(conf,CONF_under_colour)][conf_get_int(conf,CONF_sel_colour)][i];
#endif
	    dlg_coloursel_start(ctrl, dlg,
				conf_get_int_int(conf, CONF_colours, i*3+0),
				conf_get_int_int(conf, CONF_colours, i*3+1),
				conf_get_int_int(conf, CONF_colours, i*3+2));
	}
    } else if (event == EVENT_CALLBACK) {
	if (ctrl == cd->button) {
	    int i = dlg_listbox_index(cd->listbox, dlg);
	    /*
	     * Collect the results of the colour selector. Will
	     * return nonzero on success, or zero if the colour
	     * selector did nothing (user hit Cancel, for example).
	     */
#ifdef TUTTYPORT
 i = idxcolour[conf_get_int(conf,CONF_bold_colour)][conf_get_int(conf,CONF_under_colour)][conf_get_int(conf,CONF_sel_colour)][i];
#endif
	    if (dlg_coloursel_results(ctrl, dlg, &r, &g, &b)) {
		conf_set_int_int(conf, CONF_colours, i*3+0, r);
		conf_set_int_int(conf, CONF_colours, i*3+1, g);
		conf_set_int_int(conf, CONF_colours, i*3+2, b);
		clear = FALSE;
		update = TRUE;
	    }
	}
    }

    if (update) {
	if (clear) {
	    dlg_editbox_set(cd->redit, dlg, "");
	    dlg_editbox_set(cd->gedit, dlg, "");
	    dlg_editbox_set(cd->bedit, dlg, "");
	} else {
	    char buf[40];
	    sprintf(buf, "%d", r); dlg_editbox_set(cd->redit, dlg, buf);
	    sprintf(buf, "%d", g); dlg_editbox_set(cd->gedit, dlg, buf);
	    sprintf(buf, "%d", b); dlg_editbox_set(cd->bedit, dlg, buf);
	}
    }
}

struct ttymodes_data {
    union control *valradio, *valbox, *setbutton, *listbox;
};

static void ttymodes_handler(union control *ctrl, void *dlg,
			     void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct ttymodes_data *td =
	(struct ttymodes_data *)ctrl->generic.context.p;

    if (event == EVENT_REFRESH) {
	if (ctrl == td->listbox) {
	    char *key, *val;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
	    for (val = conf_get_str_strs(conf, CONF_ttymodes, NULL, &key);
		 val != NULL;
		 val = conf_get_str_strs(conf, CONF_ttymodes, key, &key)) {
		char *disp = dupprintf("%s\t%s", key,
				       (val[0] == 'A') ? "(auto)" :
				       ((val[0] == 'N') ? "(don't send)"
							: val+1));
		dlg_listbox_add(ctrl, dlg, disp);
		sfree(disp);
	    }
	    dlg_update_done(ctrl, dlg);
	} else if (ctrl == td->valradio) {
	    dlg_radiobutton_set(ctrl, dlg, 0);
	}
    } else if (event == EVENT_SELCHANGE) {
	if (ctrl == td->listbox) {
	    int ind = dlg_listbox_index(td->listbox, dlg);
	    char *val;
	    if (ind < 0) {
		return; /* no item selected */
	    }
	    val = conf_get_str_str(conf, CONF_ttymodes,
				   conf_get_str_nthstrkey(conf, CONF_ttymodes,
							  ind));
	    assert(val != NULL);
	    /* Do this first to defuse side-effects on radio buttons: */
	    dlg_editbox_set(td->valbox, dlg, val+1);
	    dlg_radiobutton_set(td->valradio, dlg,
				val[0] == 'A' ? 0 : (val[0] == 'N' ? 1 : 2));
	}
    } else if (event == EVENT_VALCHANGE) {
	if (ctrl == td->valbox) {
	    /* If they're editing the text box, we assume they want its
	     * value to be used. */
	    dlg_radiobutton_set(td->valradio, dlg, 2);
	}
    } else if (event == EVENT_ACTION) {
	if (ctrl == td->setbutton) {
	    int ind = dlg_listbox_index(td->listbox, dlg);
		const char *key;
		char *str, *val;
	    char type;

	    {
                const char types[] = {'A', 'N', 'V'};
		int button = dlg_radiobutton_get(td->valradio, dlg);
		assert(button >= 0 && button < lenof(types));
		type = types[button];
	    }

		/* Construct new entry */
	    if (ind >= 0) {
		key = conf_get_str_nthstrkey(conf, CONF_ttymodes, ind);
		str = (type == 'V' ? dlg_editbox_get(td->valbox, dlg)
				   : dupstr(""));
		val = dupprintf("%c%s", type, str);
		sfree(str);
		conf_set_str_str(conf, CONF_ttymodes, key, val);
		sfree(val);
		dlg_refresh(td->listbox, dlg);
		dlg_listbox_select(td->listbox, dlg, ind);
	    } else {
		/* Not a multisel listbox, so this means nothing selected */
		dlg_beep(dlg);
	    }
	}
    }
}

struct environ_data {
    union control *varbox, *valbox, *addbutton, *rembutton, *listbox;
};

static void environ_handler(union control *ctrl, void *dlg,
			    void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct environ_data *ed =
	(struct environ_data *)ctrl->generic.context.p;

    if (event == EVENT_REFRESH) {
	if (ctrl == ed->listbox) {
	    char *key, *val;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
	    for (val = conf_get_str_strs(conf, CONF_environmt, NULL, &key);
		 val != NULL;
		 val = conf_get_str_strs(conf, CONF_environmt, key, &key)) {
		char *p = dupprintf("%s\t%s", key, val);
		dlg_listbox_add(ctrl, dlg, p);
		sfree(p);
	    }
	    dlg_update_done(ctrl, dlg);
	}
    } else if (event == EVENT_ACTION) {
	if (ctrl == ed->addbutton) {
	    char *key, *val, *str;
	    key = dlg_editbox_get(ed->varbox, dlg);
	    if (!*key) {
		sfree(key);
		dlg_beep(dlg);
		return;
	    }
	    val = dlg_editbox_get(ed->valbox, dlg);
	    if (!*val) {
		sfree(key);
		sfree(val);
		dlg_beep(dlg);
		return;
	    }
	    conf_set_str_str(conf, CONF_environmt, key, val);
	    str = dupcat(key, "\t", val, NULL);
	    dlg_editbox_set(ed->varbox, dlg, "");
	    dlg_editbox_set(ed->valbox, dlg, "");
	    sfree(str);
	    sfree(key);
	    sfree(val);
	    dlg_refresh(ed->listbox, dlg);
	} else if (ctrl == ed->rembutton) {
	    int i = dlg_listbox_index(ed->listbox, dlg);
	    if (i < 0) {
		dlg_beep(dlg);
	    } else {
		char *key, *val;

		key = conf_get_str_nthstrkey(conf, CONF_environmt, i);
		if (key) {
		    /* Populate controls with the entry we're about to delete
		     * for ease of editing */
		    val = conf_get_str_str(conf, CONF_environmt, key);
		    dlg_editbox_set(ed->varbox, dlg, key);
		    dlg_editbox_set(ed->valbox, dlg, val);
		    /* And delete it */
		    conf_del_str_str(conf, CONF_environmt, key);
		}
	    }
	    dlg_refresh(ed->listbox, dlg);
	}
    }
}

struct portfwd_data {
    union control *addbutton, *rembutton, *listbox;
    union control *sourcebox, *destbox, *direction;
#ifndef NO_IPV6
    union control *addressfamily;
#endif
};

static void portfwd_handler(union control *ctrl, void *dlg,
			    void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct portfwd_data *pfd =
	(struct portfwd_data *)ctrl->generic.context.p;

    if (event == EVENT_REFRESH) {
	if (ctrl == pfd->listbox) {
	    char *key, *val;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
	    for (val = conf_get_str_strs(conf, CONF_portfwd, NULL, &key);
		 val != NULL;
		 val = conf_get_str_strs(conf, CONF_portfwd, key, &key)) {
		char *p;
                if (!strcmp(val, "D")) {
                    char *L;
                    /*
                     * A dynamic forwarding is stored as L12345=D or
                     * 6L12345=D (since it's mutually exclusive with
                     * L12345=anything else), but displayed as D12345
                     * to match the fiction that 'Local', 'Remote' and
                     * 'Dynamic' are three distinct modes and also to
                     * align with OpenSSH's command line option syntax
                     * that people will already be used to. So, for
                     * display purposes, find the L in the key string
                     * and turn it into a D.
                     */
                    p = dupprintf("%s\t", key);
                    L = strchr(p, 'L');
                    if (L) *L = 'D';
                } else
                    p = dupprintf("%s\t%s", key, val);
		dlg_listbox_add(ctrl, dlg, p);
		sfree(p);
	    }
	    dlg_update_done(ctrl, dlg);
	} else if (ctrl == pfd->direction) {

	    /*
	     * Default is Local.
	     */
	    dlg_radiobutton_set(ctrl, dlg, 0);
#ifndef NO_IPV6
	} else if (ctrl == pfd->addressfamily) {
	    dlg_radiobutton_set(ctrl, dlg, 0);
#endif
	}
    } else if (event == EVENT_ACTION) {
	if (ctrl == pfd->addbutton) {
	    const char *family, *type;
            char *src, *key, *val;
	    int whichbutton;

#ifndef NO_IPV6
	    whichbutton = dlg_radiobutton_get(pfd->addressfamily, dlg);
	    if (whichbutton == 1)
		family = "4";
	    else if (whichbutton == 2)
		family = "6";
	    else
#endif
		family = "";

	    whichbutton = dlg_radiobutton_get(pfd->direction, dlg);
	    if (whichbutton == 0)
		type = "L";
	    else if (whichbutton == 1)
		type = "R";
	    else
		type = "D";

	    src = dlg_editbox_get(pfd->sourcebox, dlg);
	    if (!*src) {
		dlg_error_msg(dlg, "You need to specify a source port number");
		sfree(src);
		return;
	    }
	    if (*type != 'D') {
		val = dlg_editbox_get(pfd->destbox, dlg);
		if (!*val || !host_strchr(val, ':')) {
		    dlg_error_msg(dlg,
				  "You need to specify a destination address\n"
				  "in the form \"host.name:port\"");
		    sfree(src);
		    sfree(val);
		    return;
		}
	    } else {
                type = "L";
		val = dupstr("D");     /* special case */
            }

	    key = dupcat(family, type, src, NULL);
	    sfree(src);

	    if (conf_get_str_str_opt(conf, CONF_portfwd, key)) {
		dlg_error_msg(dlg, "Specified forwarding already exists");
	    } else {
		conf_set_str_str(conf, CONF_portfwd, key, val);
	    }

	    sfree(key);
	    sfree(val);
	    dlg_refresh(pfd->listbox, dlg);
	} else if (ctrl == pfd->rembutton) {
	    int i = dlg_listbox_index(pfd->listbox, dlg);
	    if (i < 0) {
		dlg_beep(dlg);
	    } else {
		char *key, *p;
                const char *val;

		key = conf_get_str_nthstrkey(conf, CONF_portfwd, i);
		if (key) {
		    static const char *const afs = "A46";
		    static const char *const dirs = "LRD";
		    char *afp;
		    int dir;
#ifndef NO_IPV6
		    int idx;
#endif

		    /* Populate controls with the entry we're about to delete
		     * for ease of editing */
		    p = key;

		    afp = strchr(afs, *p);
#ifndef NO_IPV6
		    idx = afp ? afp-afs : 0;
#endif
		    if (afp)
			p++;
#ifndef NO_IPV6
		    dlg_radiobutton_set(pfd->addressfamily, dlg, idx);
#endif

		    dir = *p;

                    val = conf_get_str_str(conf, CONF_portfwd, key);
		    if (!strcmp(val, "D")) {
                        dir = 'D';
			val = "";
		    }

		    dlg_radiobutton_set(pfd->direction, dlg,
					strchr(dirs, dir) - dirs);
		    p++;

		    dlg_editbox_set(pfd->sourcebox, dlg, p);
		    dlg_editbox_set(pfd->destbox, dlg, val);
		    /* And delete it */
		    conf_del_str_str(conf, CONF_portfwd, key);
		}
	    }
	    dlg_refresh(pfd->listbox, dlg);
	}
    }
}

struct manual_hostkey_data {
    union control *addbutton, *rembutton, *listbox, *keybox;
};

static void manual_hostkey_handler(union control *ctrl, void *dlg,
                                   void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct manual_hostkey_data *mh =
	(struct manual_hostkey_data *)ctrl->generic.context.p;

    if (event == EVENT_REFRESH) {
	if (ctrl == mh->listbox) {
	    char *key, *val;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
	    for (val = conf_get_str_strs(conf, CONF_ssh_manual_hostkeys,
                                         NULL, &key);
		 val != NULL;
		 val = conf_get_str_strs(conf, CONF_ssh_manual_hostkeys,
                                         key, &key)) {
		dlg_listbox_add(ctrl, dlg, key);
	    }
	    dlg_update_done(ctrl, dlg);
	}
    } else if (event == EVENT_ACTION) {
	if (ctrl == mh->addbutton) {
	    char *key;

	    key = dlg_editbox_get(mh->keybox, dlg);
	    if (!*key) {
		dlg_error_msg(dlg, "You need to specify a host key or "
                              "fingerprint");
		sfree(key);
		return;
	    }

            if (!validate_manual_hostkey(key)) {
		dlg_error_msg(dlg, "Host key is not in a valid format");
            } else if (conf_get_str_str_opt(conf, CONF_ssh_manual_hostkeys,
                                            key)) {
		dlg_error_msg(dlg, "Specified host key is already listed");
	    } else {
		conf_set_str_str(conf, CONF_ssh_manual_hostkeys, key, "");
	    }

	    sfree(key);
	    dlg_refresh(mh->listbox, dlg);
	} else if (ctrl == mh->rembutton) {
	    int i = dlg_listbox_index(mh->listbox, dlg);
	    if (i < 0) {
		dlg_beep(dlg);
	    } else {
		char *key;

		key = conf_get_str_nthstrkey(conf, CONF_ssh_manual_hostkeys, i);
		if (key) {
		    dlg_editbox_set(mh->keybox, dlg, key);
		    /* And delete it */
		    conf_del_str_str(conf, CONF_ssh_manual_hostkeys, key);
		}
	    }
	    dlg_refresh(mh->listbox, dlg);
	}
    }
}

void setup_config_box(struct controlbox *b, int midsession,
		      int protocol, int protcfginfo)
{
    struct controlset *s;
    struct sessionsaver_data *ssd;
    struct charclass_data *ccd;
    struct colour_data *cd;
    struct ttymodes_data *td;
    struct environ_data *ed;
    struct portfwd_data *pfd;
    struct manual_hostkey_data *mh;
    union control *c;
    char *str;

    ssd = (struct sessionsaver_data *)
	ctrl_alloc_with_free(b, sizeof(struct sessionsaver_data),
                             sessionsaver_data_free);
    memset(ssd, 0, sizeof(*ssd));
    ssd->savedsession = dupstr("");
    ssd->midsession = midsession;

    /*
     * The standard panel that appears at the bottom of all panels:
     * Open, Cancel, Apply etc.
     */
    s = ctrl_getset(b, "", "", "");
    ctrl_columns(s, 5, 20, 20, 20, 20, 20);
    ssd->okbutton = ctrl_pushbutton(s,
				    (midsession ? "Apply" : "Open"),
				    (char)(midsession ? 'a' : 'o'),
				    HELPCTX(no_help),
				    sessionsaver_handler, P(ssd));
    ssd->okbutton->button.isdefault = TRUE;
#ifdef PERSOPORT
    if( GetConfigBoxHeight() > 7 ) ssd->okbutton->generic.column = 0 ; else 
#endif
    ssd->okbutton->generic.column = 3;
#if (defined IMAGEPORT) && (!defined FDJ) && (defined STARTBUTTON)
	if( (!midsession)&&(!get_param("PUTTY")) ) {
		ssd->startbutton = ctrl_pushbutton(s, "Start", NO_SHORTCUT, HELPCTX(no_help), sessionsaver_handler, P(ssd));
		if( GetConfigBoxHeight() > 7 ) ssd->startbutton->generic.column = 0 ; else ssd->startbutton->generic.column = 3;
	}
#endif
    ssd->cancelbutton = ctrl_pushbutton(s, "Cancel", 'c', HELPCTX(no_help),
					sessionsaver_handler, P(ssd));
    ssd->cancelbutton->button.iscancel = TRUE;
#ifdef PERSOPORT
    if( GetConfigBoxHeight() > 7 ) ssd->cancelbutton->generic.column = 0 ; else
#endif
    ssd->cancelbutton->generic.column = 4;
    /* We carefully don't close the 5-column part, so that platform-
     * specific add-ons can put extra buttons alongside Open and Cancel. */

    /*
     * The Session panel.
     */
    str = dupprintf("Basic options for your %s session", appname);
    ctrl_settitle(b, "Session", str);
    sfree(str);

    if (!midsession) {
	struct hostport *hp = (struct hostport *)
	    ctrl_alloc(b, sizeof(struct hostport));

	s = ctrl_getset(b, "Session", "hostport",
			"Specify the destination you want to connect to");
	ctrl_columns(s, 2, 75, 25);
	c = ctrl_editbox(s, HOST_BOX_TITLE, 'n', 100,
			 HELPCTX(session_hostname),
			 config_host_handler, I(0), I(0));
	c->generic.column = 0;
	hp->host = c;
	c = ctrl_editbox(s, PORT_BOX_TITLE, 'p', 100,
			 HELPCTX(session_hostname),
			 config_port_handler, I(0), I(0));
	c->generic.column = 1;
	hp->port = c;
	ctrl_columns(s, 1, 100);

	if (!backend_from_proto(PROT_SSH)) {
	    ctrl_radiobuttons(s, "Connection type:", NO_SHORTCUT, 3,
			      HELPCTX(session_hostname),
			      config_protocolbuttons_handler, P(hp),
			      "Raw", 'w', I(PROT_RAW),
			      "Telnet", 't', I(PROT_TELNET),
			      "Rlogin", 'i', I(PROT_RLOGIN),
#ifdef ADBPORT
			      "ADB", 'b', I(PROT_ADB),
#endif
			      NULL);
	} else {
#ifdef ADBPORT
	    if( GetADBFlag() )
	    ctrl_radiobuttons(s, "Connection type:", NO_SHORTCUT, 4,
			      HELPCTX(session_hostname),
			      config_protocolbuttons_handler, P(hp),
			      "Raw", 'w', I(PROT_RAW),
			      "Telnet", 't', I(PROT_TELNET),
			      "Rlogin", 'i', I(PROT_RLOGIN),
			      "SSH", 's', I(PROT_SSH),
			      "ADB", 'b', I(PROT_ADB),
			      NULL);
	    else
#endif
	    ctrl_radiobuttons(s, "Connection type:", NO_SHORTCUT, 4,
			      HELPCTX(session_hostname),
			      config_protocolbuttons_handler, P(hp),
			      "Raw", 'w', I(PROT_RAW),
			      "Telnet", 't', I(PROT_TELNET),
			      "Rlogin", 'i', I(PROT_RLOGIN),
			      "SSH", 's', I(PROT_SSH),
			      NULL);
	}
    }

    /*
     * The Load/Save panel is available even in mid-session.
     */
    s = ctrl_getset(b, "Session", "savedsessions",
		    midsession ? "Save the current session settings" :
		    "Load, save or delete a stored session");
    ctrl_columns(s, 2, 75, 25);
    get_sesslist(&ssd->sesslist, TRUE);
#ifdef PERSOPORT
    ssd->editbox = ctrl_editbox(s, "Saved Sessions/New Folder", 'e', 100,
				HELPCTX(session_saved),
				sessionsaver_handler, P(ssd), P(NULL));
    ssd->editbox->generic.column = 0;
    if( !midsession && ( (get_param("INIFILE")!=2)||(!get_param("DIRECTORYBROWSE")) ) ) {
	ssd->clearbutton = ctrl_pushbutton(s, "Clear", NO_SHORTCUT,
					  HELPCTX(session_saved),
					  sessionsaver_handler, P(ssd));
  
	ssd->clearbutton->generic.column = 1;
	} else {
		ssd->clearbutton = NULL ;
	}
#else
    ssd->editbox = ctrl_editbox(s, "Saved Sessions", 'e', 100,
				HELPCTX(session_saved),
				sessionsaver_handler, P(ssd), P(NULL));
    ssd->editbox->generic.column = 0;
#endif
    /* Reset columns so that the buttons are alongside the list, rather
     * than alongside that edit box. */
    ctrl_columns(s, 1, 100);
    ctrl_columns(s, 2, 75, 25);
    ssd->listbox = ctrl_listbox(s, NULL, NO_SHORTCUT,
				HELPCTX(session_saved),
				sessionsaver_handler, P(ssd));
    ssd->listbox->generic.column = 0;
#ifdef PERSOPORT
	ssd->listbox->listbox.height = GetConfigBoxHeight() ;
#else
    ssd->listbox->listbox.height = 7;
#endif
#ifdef CYGTERMPORT
	if( cygterm_get_flag() ) ssd->listbox->listbox.height-- ;
#endif
    if (!midsession) {
	ssd->loadbutton = ctrl_pushbutton(s, "Load", 'l',
					  HELPCTX(session_saved),
					  sessionsaver_handler, P(ssd));
	ssd->loadbutton->generic.column = 1;
    } else {
	/* We can't offer the Load button mid-session, as it would allow the
	 * user to load and subsequently save settings they can't see. (And
	 * also change otherwise immutable settings underfoot; that probably
	 * shouldn't be a problem, but.) */
	ssd->loadbutton = NULL;
    }
    /* "Save" button is permitted mid-session. */
#ifdef PERSOPORT
    if( !GetReadOnlyFlag() ) {
	if( get_param("INIFILE") == 1 )
		ssd->savebutton = ctrl_pushbutton(s, "Save (f)", 'v', HELPCTX(session_saved), sessionsaver_handler, P(ssd));
	else if( get_param("INIFILE") == 2 )
		ssd->savebutton = ctrl_pushbutton(s, "Save (d)", 'v', HELPCTX(session_saved), sessionsaver_handler, P(ssd));
	else
		ssd->savebutton = ctrl_pushbutton(s, "Save", 'v',
				      HELPCTX(session_saved),
				      sessionsaver_handler, P(ssd));
	ssd->savebutton->generic.column = 1;
    } else { ssd->savebutton = NULL ; }
    
    if( !midsession && !GetReadOnlyFlag() ) {
	ssd->delbutton = ctrl_pushbutton(s, "Delete", 'd',
					 HELPCTX(session_saved),
					 sessionsaver_handler, P(ssd));
	ssd->delbutton->generic.column = 1;
    } else {
	/* Disable the Delete button mid-session too, for UI consistency. */
	ssd->delbutton = NULL;
    }
    
    if( GetConfigBoxHeight() > 7 ) { // On n'affiche les boutons KiTTY que si la taille de la config box le permet
	if (!midsession && !GetReadOnlyFlag()) { // Bouton de creation d'un folder
	ssd->createbutton = ctrl_pushbutton(s, "New folder", NO_SHORTCUT,
					  HELPCTX(session_saved),
					  sessionsaver_handler, P(ssd));
	ssd->createbutton->generic.column = 1;
	}
	else {
	ssd->createbutton = NULL ;
	}

	if( !get_param("DIRECTORYBROWSE" ) ) {
		if (!midsession && !GetReadOnlyFlag()) { // Bouton de suppression d'un folder
		ssd->delfolderbutton = ctrl_pushbutton(s, "Del folder", NO_SHORTCUT,
					  HELPCTX(session_saved),
					  sessionsaver_handler, P(ssd));
		ssd->delfolderbutton->generic.column = 1;	
		}
		else {
		ssd->delfolderbutton = NULL ;
		}
	}
	
	if( !get_param("DIRECTORYBROWSE" ) ) {
		if ( !midsession && !GetReadOnlyFlag() ) { // Bouton d'arrange de l'ordre de la liste des folders
		ssd->arrangebutton = ctrl_pushbutton(s, "Up folder", NO_SHORTCUT,
					  HELPCTX(session_saved),
					  sessionsaver_handler, P(ssd));
		ssd->arrangebutton->generic.column = 1;	
		}
		else {
		ssd->arrangebutton = NULL ;
		}
	}
	
	if( !get_param("DIRECTORYBROWSE" ) )
	ctrl_droplist(s, "Folder", NO_SHORTCUT, 80,
			  HELPCTX(no_help),
			  folder_handler, I(CONF_folder)) ; // folder_handler, I(offsetof(Config,folder)));
	}
#else
    ssd->savebutton = ctrl_pushbutton(s, "Save", 'v',
				      HELPCTX(session_saved),
				      sessionsaver_handler, P(ssd));
    ssd->savebutton->generic.column = 1;
    if (!midsession) {
	ssd->delbutton = ctrl_pushbutton(s, "Delete", 'd',
					 HELPCTX(session_saved),
					 sessionsaver_handler, P(ssd));
	ssd->delbutton->generic.column = 1;
    } else {
	/* Disable the Delete button mid-session too, for UI consistency. */
	ssd->delbutton = NULL;
    }
#endif
    ctrl_columns(s, 1, 100);

    s = ctrl_getset(b, "Session", "otheropts", NULL);
    ctrl_radiobuttons(s, "Close window on exit:", 'x', 4,
			  HELPCTX(session_coe),
			  conf_radiobutton_handler,
			  I(CONF_close_on_exit),
			  "Always", I(FORCE_ON),
			  "Never", I(FORCE_OFF),
			  "Only on clean exit", I(AUTO), NULL);

    /*
     * The Session/Logging panel.
     */
    ctrl_settitle(b, "Session/Logging", "Options controlling session logging");

    s = ctrl_getset(b, "Session/Logging", "main", NULL);
    /*
     * The logging buttons change depending on whether SSH packet
     * logging can sensibly be available.
     */
    {
	const char *sshlogname, *sshrawlogname;
	if ((midsession && protocol == PROT_SSH) ||
	    (!midsession && backend_from_proto(PROT_SSH))) {
	    sshlogname = "SSH packets";
	    sshrawlogname = "SSH packets and raw data";
        } else {
	    sshlogname = NULL;	       /* this will disable both buttons */
	    sshrawlogname = NULL;      /* this will just placate optimisers */
        }
	ctrl_radiobuttons(s, "Session logging:", NO_SHORTCUT, 2,
			  HELPCTX(logging_main),
			  loggingbuttons_handler,
			  I(CONF_logtype),
			  "None", 't', I(LGTYP_NONE),
			  "Printable output", 'p', I(LGTYP_ASCII),
			  "All session output", 'l', I(LGTYP_DEBUG),
			  sshlogname, 's', I(LGTYP_PACKETS),
			  sshrawlogname, 'r', I(LGTYP_SSHRAW),
			  NULL);
    }
    ctrl_filesel(s, "Log file name:", 'f',
		 NULL, TRUE, "Select session log file name",
		 HELPCTX(logging_filename),
		 conf_filesel_handler, I(CONF_logfilename));
    ctrl_text(s, "(Log file name can contain &Y, &M, &D for date,"
	      " &T for time, &H for host name, and &P for port number)",
	      HELPCTX(logging_filename));
#ifdef PERSOPORT
    ctrl_editbox(s, "Log rotation delay (sec)", NO_SHORTCUT, 50,
		 HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_logtimerotation), I(-1) ) ; // dlg_stdeditbox_handler, I(offsetof(Config,logtimerotation)), I(-1));
    ctrl_editbox(s, "Timestamp", NO_SHORTCUT, 100,
		 HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_logtimestamp), I(1) ) ; //  dlg_stdeditbox_handler, I(offsetof(Config,logtimestamp)),I(sizeof(((Config *)0)->logtimestamp)));
#endif
    ctrl_radiobuttons(s, "What to do if the log file already exists:", 'e', 1,
		      HELPCTX(logging_exists),
		      conf_radiobutton_handler, I(CONF_logxfovr),
		      "Always overwrite it", I(LGXF_OVR),
		      "Always append to the end of it", I(LGXF_APN),
		      "Ask the user every time", I(LGXF_ASK), NULL);
    ctrl_checkbox(s, "Flush log file frequently", 'u',
		 HELPCTX(logging_flush),
		 conf_checkbox_handler, I(CONF_logflush));

    if ((midsession && protocol == PROT_SSH) ||
	(!midsession && backend_from_proto(PROT_SSH))) {
	s = ctrl_getset(b, "Session/Logging", "ssh",
			"Options specific to SSH packet logging");
	ctrl_checkbox(s, "Omit known password fields", 'k',
		      HELPCTX(logging_ssh_omit_password),
		      conf_checkbox_handler, I(CONF_logomitpass));
	ctrl_checkbox(s, "Omit session data", 'd',
		      HELPCTX(logging_ssh_omit_data),
		      conf_checkbox_handler, I(CONF_logomitdata));
    }

/* rutty: Session/Scripting panel */
#ifdef RUTTYPORT
    if( !get_param("PUTTY") && (GetRuTTYFlag()>0) ) {
	ctrl_settitle(b, "Session/Scripting", "Scripting (RuTTY patch)");
	s = ctrl_getset(b, "Session/Scripting", "Start", NULL);
	 ctrl_filesel(s, "Script filename:", 'f',
		"Scr Files (*.scr, *.txt)\0*.scr;*.txt\0All Files (*.*)\0*\0\0\0"
		, TRUE, "Select filename for script replay", HELPCTX(no_help),
		conf_filesel_handler, I(CONF_script_filename));
	 ctrl_radiobuttons(s, NULL, 'm', 4, HELPCTX(no_help),
          conf_radiobutton_handler, I(CONF_script_mode),
          "Off", I(SCRIPT_STOP),
          "Replay", I(SCRIPT_PLAY),
          "Record", I(SCRIPT_RECORD),
          NULL);

   	s = ctrl_getset(b, "Session/Scripting", "Scripting", NULL);
    
    ctrl_editbox(s, "line delay (ms)", 'd', 40, HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_script_line_delay), I(-1));

	ctrl_editbox(s, "character delay (ms)", 'b', 40, HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_script_char_delay), I(-1));
	
	ctrl_editbox(s, "start of condition/comment line", 'l', 40, HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_script_cond_line), I(1));

    ctrl_radiobuttons(s, "CR/LF translation:", 't', 4, HELPCTX(no_help),
          conf_radiobutton_handler,I(CONF_script_crlf),
          "Off", I(SCRIPT_OFF),
          "no LF", I(SCRIPT_NOLF),
          "CR", I(SCRIPT_CR),
          "Rec", I(SCRIPT_REC),
          NULL);

	ctrl_editbox(s, "halt on", 'x', 80, HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_script_halton), I(1));
	ctrl_checkbox(s, "wait for response from host", 'r', HELPCTX(no_help),
		  conf_checkbox_handler, I(CONF_script_enable));
  ctrl_checkbox(s, "except for first command", 's', HELPCTX(no_help),
		  conf_checkbox_handler, I(CONF_script_except));
	ctrl_checkbox(s, "use conditions from file", 'v', HELPCTX(no_help),
		  conf_checkbox_handler, I(CONF_script_cond_use));
      
	ctrl_editbox(s, "timeout (sec)", 'u', 40, HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_script_timeout),I(-1));
  
	ctrl_editbox(s, "wait for", 'k', 80, HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_script_waitfor),I(1));
	}
#endif  /* rutty */

    /*
     * The Terminal panel.
     */
    ctrl_settitle(b, "Terminal", "Options controlling the terminal emulation");

    s = ctrl_getset(b, "Terminal", "general", "Set various terminal options");
    ctrl_checkbox(s, "Auto wrap mode initially on", 'w',
		  HELPCTX(terminal_autowrap),
		  conf_checkbox_handler, I(CONF_wrap_mode));
    ctrl_checkbox(s, "DEC Origin Mode initially on", 'd',
		  HELPCTX(terminal_decom),
		  conf_checkbox_handler, I(CONF_dec_om));
    ctrl_checkbox(s, "Implicit CR in every LF", 'r',
		  HELPCTX(terminal_lfhascr),
		  conf_checkbox_handler, I(CONF_lfhascr));
    ctrl_checkbox(s, "Implicit LF in every CR", 'f',
		  HELPCTX(terminal_crhaslf),
		  conf_checkbox_handler, I(CONF_crhaslf));
    ctrl_checkbox(s, "Use background colour to erase screen", 'e',
		  HELPCTX(terminal_bce),
		  conf_checkbox_handler, I(CONF_bce));
    ctrl_checkbox(s, "Enable blinking text", 'n',
		  HELPCTX(terminal_blink),
		  conf_checkbox_handler, I(CONF_blinktext));
    ctrl_editbox(s, "Answerback to ^E:", 's', 100,
		 HELPCTX(terminal_answerback),
		 conf_editbox_handler, I(CONF_answerback), I(1));

    s = ctrl_getset(b, "Terminal", "ldisc", "Line discipline options");
    ctrl_radiobuttons(s, "Local echo:", 'l', 3,
		      HELPCTX(terminal_localecho),
		      conf_radiobutton_handler,I(CONF_localecho),
		      "Auto", I(AUTO),
		      "Force on", I(FORCE_ON),
		      "Force off", I(FORCE_OFF), NULL);
    ctrl_radiobuttons(s, "Local line editing:", 't', 3,
		      HELPCTX(terminal_localedit),
		      conf_radiobutton_handler,I(CONF_localedit),
		      "Auto", I(AUTO),
		      "Force on", I(FORCE_ON),
		      "Force off", I(FORCE_OFF), NULL);

    s = ctrl_getset(b, "Terminal", "printing", "Remote-controlled printing");
    ctrl_combobox(s, "Printer to send ANSI printer output to:", 'p', 100,
		  HELPCTX(terminal_printing),
		  printerbox_handler, P(NULL), P(NULL));

    /*
     * The Terminal/Keyboard panel.
     */
    ctrl_settitle(b, "Terminal/Keyboard",
		  "Options controlling the effects of keys");

    s = ctrl_getset(b, "Terminal/Keyboard", "mappings",
		    "Change the sequences sent by:");
    ctrl_radiobuttons(s, "The Backspace key", 'b', 2,
		      HELPCTX(keyboard_backspace),
		      conf_radiobutton_handler,
		      I(CONF_bksp_is_delete),
		      "Control-H", I(0), "Control-? (127)", I(1), NULL);
#ifdef PERSOPORT
    ctrl_radiobuttons(s, "The Home and End keys", 'e', 3,
		      HELPCTX(keyboard_homeend),
		      conf_radiobutton_handler,
		      I(CONF_rxvt_homeend),
		      "Standard", I(0), "rxvt", I(1), "urxvt", I(2),
		      "xterm", I(3), "FreeBSD1", I(4), "FreeBSD2", I(5),
		      NULL);
#else
    ctrl_radiobuttons(s, "The Home and End keys", 'e', 2,
		      HELPCTX(keyboard_homeend),
		      conf_radiobutton_handler,
		      I(CONF_rxvt_homeend),
		      "Standard", I(0), "rxvt", I(1), NULL);
#endif
    ctrl_radiobuttons(s, "The Function keys and keypad", 'f', 3,
		      HELPCTX(keyboard_funkeys),
		      conf_radiobutton_handler,
		      I(CONF_funky_type),
		      "ESC[n~", I(0), "Linux", I(1), "Xterm R6", I(2),
		      "VT400", I(3), "VT100+", I(4), "SCO", I(5), 
#ifdef TUTTYPORT
//		      "AT&T 513", I(6), 
//		      "Sun Xterm", I(8),
#endif
		      NULL);

    s = ctrl_getset(b, "Terminal/Keyboard", "appkeypad",
		    "Application keypad settings:");
    ctrl_radiobuttons(s, "Initial state of cursor keys:", 'r', 3,
		      HELPCTX(keyboard_appcursor),
		      conf_radiobutton_handler,
		      I(CONF_app_cursor),
		      "Normal", I(0), "Application", I(1), NULL);
    ctrl_radiobuttons(s, "Initial state of numeric keypad:", 'n', 3,
		      HELPCTX(keyboard_appkeypad),
		      numeric_keypad_handler, P(NULL),
		      "Normal", I(0), "Application", I(1), "NetHack", I(2),
		      NULL);

    /*
     * The Terminal/Bell panel.
     */
    ctrl_settitle(b, "Terminal/Bell",
		  "Options controlling the terminal bell");

    s = ctrl_getset(b, "Terminal/Bell", "style", "Set the style of bell");
    ctrl_radiobuttons(s, "Action to happen when a bell occurs:", 'b', 1,
		      HELPCTX(bell_style),
		      conf_radiobutton_handler, I(CONF_beep),
		      "None (bell disabled)", I(BELL_DISABLED),
		      "Make default system alert sound", I(BELL_DEFAULT),
		      "Visual bell (flash window)", I(BELL_VISUAL), NULL);

#ifdef PERSOPORT
   ctrl_checkbox(s, "Put window on foreground on bell", NO_SHORTCUT,
		  HELPCTX(no_help),
		  conf_checkbox_handler, I(CONF_foreground_on_bell) ) ; //dlg_stdcheckbox_handler, I(offsetof(Config,foreground_on_bell)));
#endif

    s = ctrl_getset(b, "Terminal/Bell", "overload",
		    "Control the bell overload behaviour");
    ctrl_checkbox(s, "Bell is temporarily disabled when over-used", 'd',
		  HELPCTX(bell_overload),
		  conf_checkbox_handler, I(CONF_bellovl));
    ctrl_editbox(s, "Over-use means this many bells...", 'm', 20,
		 HELPCTX(bell_overload),
		 conf_editbox_handler, I(CONF_bellovl_n), I(-1));
    ctrl_editbox(s, "... in this many seconds", 't', 20,
		 HELPCTX(bell_overload),
		 conf_editbox_handler, I(CONF_bellovl_t),
		 I(-TICKSPERSEC));
    ctrl_text(s, "The bell is re-enabled after a few seconds of silence.",
	      HELPCTX(bell_overload));
    ctrl_editbox(s, "Seconds of silence required", 's', 20,
		 HELPCTX(bell_overload),
		 conf_editbox_handler, I(CONF_bellovl_s),
		 I(-TICKSPERSEC));

    /*
     * The Terminal/Features panel.
     */
    ctrl_settitle(b, "Terminal/Features",
		  "Enabling and disabling advanced terminal features");

    s = ctrl_getset(b, "Terminal/Features", "main", NULL);
    ctrl_checkbox(s, "Disable application cursor keys mode", 'u',
		  HELPCTX(features_application),
		  conf_checkbox_handler, I(CONF_no_applic_c));
    ctrl_checkbox(s, "Disable application keypad mode", 'k',
		  HELPCTX(features_application),
		  conf_checkbox_handler, I(CONF_no_applic_k));
    ctrl_checkbox(s, "Disable xterm-style mouse reporting", 'x',
		  HELPCTX(features_mouse),
		  conf_checkbox_handler, I(CONF_no_mouse_rep));
    ctrl_checkbox(s, "Disable remote-controlled terminal resizing", 's',
		  HELPCTX(features_resize),
		  conf_checkbox_handler,
		  I(CONF_no_remote_resize));
    ctrl_checkbox(s, "Disable switching to alternate terminal screen", 'w',
		  HELPCTX(features_altscreen),
		  conf_checkbox_handler, I(CONF_no_alt_screen));
    ctrl_checkbox(s, "Disable remote-controlled window title changing", 't',
		  HELPCTX(features_retitle),
		  conf_checkbox_handler,
		  I(CONF_no_remote_wintitle));
    ctrl_checkbox(s, "Disable remote-controlled clearing of scrollback", 'e',
		  HELPCTX(features_clearscroll),
		  conf_checkbox_handler,
		  I(CONF_no_remote_clearscroll));
    ctrl_radiobuttons(s, "Response to remote title query (SECURITY):", 'q', 3,
		      HELPCTX(features_qtitle),
		      conf_radiobutton_handler,
		      I(CONF_remote_qtitle_action),
		      "None", I(TITLE_NONE),
		      "Empty string", I(TITLE_EMPTY),
		      "Window title", I(TITLE_REAL), NULL);
    ctrl_checkbox(s, "Disable destructive backspace on server sending ^?",'b',
		  HELPCTX(features_dbackspace),
		  conf_checkbox_handler, I(CONF_no_dbackspace));
    ctrl_checkbox(s, "Disable remote-controlled character set configuration",
		  'r', HELPCTX(features_charset), conf_checkbox_handler,
		  I(CONF_no_remote_charset));
    ctrl_checkbox(s, "Disable Arabic text shaping",
		  'l', HELPCTX(features_arabicshaping), conf_checkbox_handler,
		  I(CONF_arabicshaping));
    ctrl_checkbox(s, "Disable bidirectional text display",
		  'd', HELPCTX(features_bidi), conf_checkbox_handler,
		  I(CONF_bidi));

    /*
     * The Window panel.
     */
    str = dupprintf("Options controlling %s's window", appname);
    ctrl_settitle(b, "Window", str);
    sfree(str);

    s = ctrl_getset(b, "Window", "size", "Set the size of the window");
    ctrl_columns(s, 2, 50, 50);
    c = ctrl_editbox(s, "Columns", 'm', 100,
		     HELPCTX(window_size),
		     conf_editbox_handler, I(CONF_width), I(-1));
    c->generic.column = 0;
    c = ctrl_editbox(s, "Rows", 'r', 100,
		     HELPCTX(window_size),
		     conf_editbox_handler, I(CONF_height),I(-1));
    c->generic.column = 1;
    ctrl_columns(s, 1, 100);

    s = ctrl_getset(b, "Window", "scrollback",
		    "Control the scrollback in the window");
    ctrl_editbox(s, "Lines of scrollback", 's', 50,
		 HELPCTX(window_scrollback),
		 conf_editbox_handler, I(CONF_savelines), I(-1));
    ctrl_checkbox(s, "Display scrollbar", 'd',
		  HELPCTX(window_scrollback),
		  conf_checkbox_handler, I(CONF_scrollbar));
    ctrl_checkbox(s, "Reset scrollback on keypress", 'k',
		  HELPCTX(window_scrollback),
		  conf_checkbox_handler, I(CONF_scroll_on_key));
    ctrl_checkbox(s, "Reset scrollback on display activity", 'p',
		  HELPCTX(window_scrollback),
		  conf_checkbox_handler, I(CONF_scroll_on_disp));
    ctrl_checkbox(s, "Push erased text into scrollback", 'e',
		  HELPCTX(window_erased),
		  conf_checkbox_handler,
		  I(CONF_erase_to_scrollback));

    /*
     * The Window/Appearance panel.
     */
    str = dupprintf("Configure the appearance of %s's window", appname);
    ctrl_settitle(b, "Window/Appearance", str);
    sfree(str);

    s = ctrl_getset(b, "Window/Appearance", "cursor",
		    "Adjust the use of the cursor");
    ctrl_radiobuttons(s, "Cursor appearance:", NO_SHORTCUT, 3,
		      HELPCTX(appearance_cursor),
		      conf_radiobutton_handler,
		      I(CONF_cursor_type),
		      "Block", 'l', I(0),
		      "Underline", 'u', I(1),
		      "Vertical line", 'v', I(2), NULL);
    ctrl_checkbox(s, "Cursor blinks", 'b',
		  HELPCTX(appearance_cursor),
		  conf_checkbox_handler, I(CONF_blink_cur));

    s = ctrl_getset(b, "Window/Appearance", "font",
		    "Font settings");
    ctrl_fontsel(s, "Font used in the terminal window", 'n',
		 HELPCTX(appearance_font),
		 conf_fontsel_handler, I(CONF_font));

    s = ctrl_getset(b, "Window/Appearance", "mouse",
		    "Adjust the use of the mouse pointer");
    ctrl_checkbox(s, "Hide mouse pointer when typing in window", 'p',
		  HELPCTX(appearance_hidemouse),
		  conf_checkbox_handler, I(CONF_hide_mouseptr));

    s = ctrl_getset(b, "Window/Appearance", "border",
		    "Adjust the window border");
    ctrl_editbox(s, "Gap between text and window edge:", 'e', 20,
		 HELPCTX(appearance_border),
		 conf_editbox_handler,
		 I(CONF_window_border), I(-1));
		 
#ifdef PERSOPORT
	if( !get_param("PUTTY") ) {
    /*
     * Brad's The Window/Position & Icon panel.
     */
	/* BKG */
    s = ctrl_getset(b, "Window/Appearance", "position",
		    "Remember Window Position");
    /**/
    ctrl_checkbox(s, "Remember Window Positions", NO_SHORTCUT,
		  HELPCTX(no_help),
		  conf_checkbox_handler, I(CONF_save_windowpos) ) ; //dlg_stdcheckbox_handler, I(offsetof(Config,save_windowpos)));
    ctrl_editbox(s, "Top:", NO_SHORTCUT, 20,
		 HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_ypos), I(-1) ) ; // dlg_stdeditbox_handler, I(offsetof(Config,ypos)), I(-1));
    ctrl_editbox(s, "Left:", NO_SHORTCUT, 20,
		 HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_xpos), I(-1) ) ; // dlg_stdeditbox_handler, I(offsetof(Config,xpos)), I(-1));
	}

    if( !get_param("PUTTY") && (GetIconeFlag()>0) ) {
    s = ctrl_getset(b, "Window/Appearance", "icon",
		    "Define the window icon");
    c = ctrl_editbox(s, "Icon (from internal resources)", NO_SHORTCUT, 40,
			 HELPCTX(no_help),
			 conf_editbox_handler, I(CONF_icone), I(-1) ) ; // dlg_stdeditbox_handler, I(offsetof(Config,icone)), I(-1));
    ctrl_filesel(s, "External icon file:", NO_SHORTCUT,
		     FILTER_ICON_FILES, FALSE, "Select icon file",
		     HELPCTX(no_help),
		     conf_filesel_handler, I(CONF_iconefile) ) ; // dlg_stdfilesel_handler, I(offsetof(Config, iconefile)));

    }
#endif
#if (defined IMAGEPORT) && (!defined FDJ)
	if( !get_param("PUTTY") && get_param("BACKGROUNDIMAGE") ) {
    /*
     * The Window/Background panel.
     * (This would really belong in Appearance but we overflowed -- to much
     * stuff on one page otherwise).
     */
    str = dupprintf("Configure the background of %s's window", appname);
    ctrl_settitle(b, "Window/Back.&Image", str);
    sfree(str);

    s = ctrl_getset(b, "Window/Back.&Image", "bg_style",
            "Background settings");
    ctrl_radiobuttons(s, "Background Style:", NO_SHORTCUT, 3,
              HELPCTX(no_help),
              conf_radiobutton_handler, //dlg_stdradiobutton_handler,
              I(CONF_bg_type), //I(offsetof(Config, bg_type)),
              "Solid", NO_SHORTCUT, I(0),  // TODO: Define shortcuts for these.
              "Desktop", NO_SHORTCUT, I(1),
              "Image", NO_SHORTCUT, I(2),
              NULL);

    s = ctrl_getset(b, "Window/Back.&Image", "bg_wp_img_settings",
            "Desktop and image settings");
    ctrl_editbox(s, "Opacity:", NO_SHORTCUT, 20,
		 HELPCTX(no_help),
		 conf_editbox_handler, // dlg_stdeditbox_handler,
		 I(CONF_bg_opacity), I(-1) ) ; // I(offsetof(Config,bg_opacity)), I(-1));
    ctrl_editbox(s, "Slideshow:", NO_SHORTCUT, 20,
		 HELPCTX(no_help),
		 conf_editbox_handler, // dlg_stdeditbox_handler,
		 I(CONF_bg_slideshow), I(-1) ) ; //I(offsetof(Config,bg_slideshow)), I(-1));

    s = ctrl_getset(b, "Window/Back.&Image", "bg_img_settings",
            "Image settings");
    ctrl_filesel(s, "Image file:", NO_SHORTCUT,
		     FILTER_IMAGE_FILES, FALSE, "Select background image file",
		     HELPCTX(no_help),
		     conf_filesel_handler, I(CONF_bg_image_filename)  ); //dlg_stdfilesel_handler, I(offsetof(Config, bg_image_filename)));
    ctrl_radiobuttons(s, "Image placement:", NO_SHORTCUT, 3,
              HELPCTX(no_help),
              conf_radiobutton_handler, // dlg_stdradiobutton_handler,
              I(CONF_bg_image_style), //I(offsetof(Config, bg_image_style)),
              "Tile", NO_SHORTCUT, I(0),  // TODO: Define shortcuts for these.
              "Center", NO_SHORTCUT, I(1),
              "Stretch", NO_SHORTCUT, I(2),
              "Absolute (X,Y)", NO_SHORTCUT, I(3),
	      "Blank back.", NO_SHORTCUT, I(4),
	      "Stretch+", NO_SHORTCUT, I(5),
              NULL);

    ctrl_editbox(s, "Absolute Left (X):", NO_SHORTCUT, 20,
		 HELPCTX(no_help),
		 conf_editbox_handler, //dlg_stdeditbox_handler,
		 I(CONF_bg_image_abs_x), I(-1) ) ; //I(offsetof(Config,bg_image_abs_x)), I(-1));
    ctrl_editbox(s, "Absolute Top (Y):", NO_SHORTCUT, 20,
		 HELPCTX(no_help),
		 conf_editbox_handler, //dlg_stdeditbox_handler,
		 I(CONF_bg_image_abs_y), I(-1) ) ; //I(offsetof(Config,bg_image_abs_y)), I(-1));
    ctrl_radiobuttons(s, "Image placement is relative to:", NO_SHORTCUT, 2,
              HELPCTX(no_help),
              conf_radiobutton_handler, // dlg_stdradiobutton_handler,
              I(CONF_bg_image_abs_fixed), //I(offsetof(Config, bg_image_abs_fixed)),
              "Desktop", NO_SHORTCUT, I(0),  // TODO: Define shortcuts for these.
              "Terminal Window", NO_SHORTCUT, I(1),
              NULL);

      }
#endif 
#ifdef PERSOPORT
        static char transTitle[256]="Window/Transparency" ;
        if( !get_param("PUTTY") ) {
#ifdef IVPORT
		if( get_param("TRANSPARENCY") && get_param("BACKGROUNDIMAGEIV") ) {
			strcpy( transTitle, "Window/Back.&Trans" );
			ctrl_settitle(b, transTitle, "Options controlling transparency and background");
		}
		else if( get_param("BACKGROUNDIMAGEIV") ) {
			strcpy( transTitle, "Window/Background" );
			ctrl_settitle(b, transTitle, "Options controlling background");
			}
		else 
#endif
			if( get_param("TRANSPARENCY") ) ctrl_settitle(b, transTitle, "Options controlling transparency");

	}

	if( !get_param("PUTTY") && get_param("TRANSPARENCY") ) {
    s = ctrl_getset(b, transTitle, "bg_transparency",
            "Transparency setting");
    ctrl_editbox(s, "Transparency:", NO_SHORTCUT, 20,
		 HELPCTX(no_help),
		 conf_editbox_handler, // dlg_stdeditbox_handler,
		 I(CONF_transparencynumber), I(-1) ) ; // I(offsetof(Config,bg_opacity)), I(-1));
    ctrl_text(s, "	from 0 (visible) to 255 (transparent)", HELPCTX(no_help));
    ctrl_text(s, "	-1 to disable completely", HELPCTX(no_help));
	 }
#endif

    /*
     * The Window/Behaviour panel.
     */
    str = dupprintf("Configure the behaviour of %s's window", appname);
    ctrl_settitle(b, "Window/Behaviour", str);
    sfree(str);

    s = ctrl_getset(b, "Window/Behaviour", "title",
		    "Adjust the behaviour of the window title");
    ctrl_editbox(s, "Window title:", 't', 100,
		 HELPCTX(appearance_title),
		 conf_editbox_handler, I(CONF_wintitle), I(1));
#ifdef PERSOPORT
if( !get_param("PUTTY") ) {
      ctrl_text(s, "     %%f: folder name", HELPCTX(appearance_title));
      ctrl_text(s, "     %%h: hostname", HELPCTX(appearance_title));
      ctrl_text(s, "     %%p: port number", HELPCTX(appearance_title));
      ctrl_text(s, "     %%P: protocol name", HELPCTX(appearance_title));
      ctrl_text(s, "     %%s: session name", HELPCTX(appearance_title));
      ctrl_text(s, "     %%u: username", HELPCTX(appearance_title));
      ctrl_text(s, "     %%w: forwarded ports list", HELPCTX(appearance_title));
}
#endif
    ctrl_checkbox(s, "Separate window and icon titles", 'i',
		  HELPCTX(appearance_title),
		  conf_checkbox_handler,
		  I(CHECKBOX_INVERT | CONF_win_name_always));

    s = ctrl_getset(b, "Window/Behaviour", "main", NULL);
    ctrl_checkbox(s, "Warn before closing window", 'w',
		  HELPCTX(behaviour_closewarn),
		  conf_checkbox_handler, I(CONF_warn_on_close));
#ifdef DISABLEALTGRPORT
if( !get_param("PUTTY") ) {
    ctrl_checkbox(s, "Disable AltGr menu", NO_SHORTCUT,
		  HELPCTX(no_help),
		  conf_checkbox_handler, I(CONF_disablealtgr));
}
#endif

    /*
     * The Window/Translation panel.
     */
    ctrl_settitle(b, "Window/Translation",
		  "Options controlling character set translation");

    s = ctrl_getset(b, "Window/Translation", "trans",
		    "Character set translation");
    ctrl_combobox(s, "Remote character set:",
		  'r', 100, HELPCTX(translation_codepage),
		  codepage_handler, P(NULL), P(NULL));

    s = ctrl_getset(b, "Window/Translation", "tweaks", NULL);
    ctrl_checkbox(s, "Treat CJK ambiguous characters as wide", 'w',
		  HELPCTX(translation_cjk_ambig_wide),
		  conf_checkbox_handler, I(CONF_cjk_ambig_wide));

    str = dupprintf("Adjust how %s handles line drawing characters", appname);
    s = ctrl_getset(b, "Window/Translation", "linedraw", str);
    sfree(str);
    ctrl_radiobuttons(s, "Handling of line drawing characters:", NO_SHORTCUT,1,
		      HELPCTX(translation_linedraw),
		      conf_radiobutton_handler,
		      I(CONF_vtmode),
		      "Use Unicode line drawing code points",'u',I(VT_UNICODE),
		      "Poor man's line drawing (+, - and |)",'p',I(VT_POORMAN),
		      NULL);
    ctrl_checkbox(s, "Copy and paste line drawing characters as lqqqk",'d',
		  HELPCTX(selection_linedraw),
		  conf_checkbox_handler, I(CONF_rawcnp));
#ifdef PERSOPORT
    ctrl_checkbox(s, "Allow ACS line drawing in UTF", NO_SHORTCUT,
		  HELPCTX(no_help),
		  conf_checkbox_handler, I(CONF_acs_in_utf));
#endif

    /*
     * The Window/Selection panel.
     */
    ctrl_settitle(b, "Window/Selection", "Options controlling copy and paste");
	
    s = ctrl_getset(b, "Window/Selection", "mouse",
		    "Control use of mouse");
    ctrl_checkbox(s, "Shift overrides application's use of mouse", 'p',
		  HELPCTX(selection_shiftdrag),
		  conf_checkbox_handler, I(CONF_mouse_override));
#ifdef URLPORT
    ctrl_checkbox(s, "Detect URLs on selection and launch in browser", NO_SHORTCUT,
		  HELPCTX(no_help),
		  conf_checkbox_handler, I(CONF_copy_clipbd_url_reg) ) ; //dlg_stdcheckbox_handler, I(offsetof(Config,copy_clipbd_url_reg)));
#endif
    ctrl_radiobuttons(s,
		      "Default selection mode (Alt+drag does the other one):",
		      NO_SHORTCUT, 2,
		      HELPCTX(selection_rect),
		      conf_radiobutton_handler,
		      I(CONF_rect_select),
		      "Normal", 'n', I(0),
		      "Rectangular block", 'r', I(1), NULL);

    s = ctrl_getset(b, "Window/Selection", "charclass",
		    "Control the select-one-word-at-a-time mode");
    ccd = (struct charclass_data *)
	ctrl_alloc(b, sizeof(struct charclass_data));
    ccd->listbox = ctrl_listbox(s, "Character classes:", 'e',
				HELPCTX(selection_charclasses),
				charclass_handler, P(ccd));
    ccd->listbox->listbox.multisel = 1;
    ccd->listbox->listbox.ncols = 4;
    ccd->listbox->listbox.percentages = snewn(4, int);
    ccd->listbox->listbox.percentages[0] = 15;
    ccd->listbox->listbox.percentages[1] = 25;
    ccd->listbox->listbox.percentages[2] = 20;
    ccd->listbox->listbox.percentages[3] = 40;
    ctrl_columns(s, 2, 67, 33);
    ccd->editbox = ctrl_editbox(s, "Set to class", 't', 50,
				HELPCTX(selection_charclasses),
				charclass_handler, P(ccd), P(NULL));
    ccd->editbox->generic.column = 0;
    ccd->button = ctrl_pushbutton(s, "Set", 's',
				  HELPCTX(selection_charclasses),
				  charclass_handler, P(ccd));
    ccd->button->generic.column = 1;
    ctrl_columns(s, 1, 100);

    /*
     * The Window/Colours panel.
     */
    ctrl_settitle(b, "Window/Colours", "Options controlling use of colours");

    s = ctrl_getset(b, "Window/Colours", "general",
		    "General options for colour usage");
    ctrl_checkbox(s, "Allow terminal to specify ANSI colours", 'i',
		  HELPCTX(colours_ansi),
		  conf_checkbox_handler, I(CONF_ansi_colour));
    ctrl_checkbox(s, "Allow terminal to use xterm 256-colour mode", '2',
		  HELPCTX(colours_xterm256), conf_checkbox_handler,
		  I(CONF_xterm_256_colour));
#ifdef PERSOPORT
/*  --- Inutile > 2013/06/27
	if( !get_param("PUTTY" ) ) {
	ctrl_radiobuttons(s,
		      "Displaying of bolded text",
		      'b', 1,
		      HELPCTX(colours_bold),
		      dlg_stdradiobutton_handler,
		      I(offsetof(Config, bold_colour)),
		      "Bold font only", I(0),
		      "Bold colours only", I(1),
		      "Both bold font and bold colours", I(2),
                      NULL);
		}
	else
*/
#endif
    ctrl_radiobuttons(s, "Indicate bolded text by changing:", 'b', 3,
                      HELPCTX(colours_bold),
                      conf_radiobutton_handler, I(CONF_bold_style),
                      "The font", I(1),
                      "The colour", I(2),
                      "Both", I(3),
                      NULL);
#ifdef TUTTYPORT
    cd = (struct colour_data *) ctrl_alloc(b, sizeof(struct colour_data));
    memset(cd , 0, sizeof(*cd ));
    cd->bold_checkbox =
	ctrl_checkbox(s, "Bolded text is a different colour", NO_SHORTCUT,
		      HELPCTX(no_help), colour_handler, P(cd));
    cd->underline_checkbox =
	ctrl_checkbox(s, "Underlined text is a different colour", NO_SHORTCUT,
		      HELPCTX(no_help), colour_handler, P(cd));
    cd->selected_checkbox =
	ctrl_checkbox(s, "Selected text is a different colour", NO_SHORTCUT,
		      HELPCTX(no_help), colour_handler, P(cd));
#endif

    str = dupprintf("Adjust the precise colours %s displays", appname);
    s = ctrl_getset(b, "Window/Colours", "adjust", str);
    sfree(str);
    ctrl_text(s, "Select a colour from the list, and then click the"
	      " Modify button to change its appearance.",
	      HELPCTX(colours_config));
    ctrl_columns(s, 2, 67, 33);
#ifndef TUTTYPORT
    cd = (struct colour_data *)ctrl_alloc(b, sizeof(struct colour_data));
#endif
    cd->listbox = ctrl_listbox(s, "Select a colour to adjust:", 'u',
			       HELPCTX(colours_config), colour_handler, P(cd));
    cd->listbox->generic.column = 0;
    cd->listbox->listbox.height = 7;
    c = ctrl_text(s, "RGB value:", HELPCTX(colours_config));
    c->generic.column = 1;
    cd->redit = ctrl_editbox(s, "Red", 'r', 50, HELPCTX(colours_config),
			     colour_handler, P(cd), P(NULL));
    cd->redit->generic.column = 1;
    cd->gedit = ctrl_editbox(s, "Green", 'n', 50, HELPCTX(colours_config),
			     colour_handler, P(cd), P(NULL));
    cd->gedit->generic.column = 1;
    cd->bedit = ctrl_editbox(s, "Blue", 'e', 50, HELPCTX(colours_config),
			     colour_handler, P(cd), P(NULL));
    cd->bedit->generic.column = 1;
    cd->button = ctrl_pushbutton(s, "Modify", 'm', HELPCTX(colours_config),
				 colour_handler, P(cd));
    cd->button->generic.column = 1;
    ctrl_columns(s, 1, 100);

    /*
     * The Connection panel. This doesn't show up if we're in a
     * non-network utility such as pterm. We tell this by being
     * passed a protocol < 0.
     */
    if (protocol >= 0) {
	ctrl_settitle(b, "Connection", "Options controlling the connection");

	s = ctrl_getset(b, "Connection", "keepalive",
			"Sending of null packets to keep session active");
	ctrl_editbox(s, "Seconds between keepalives (0 to turn off)", 'k', 20,
		     HELPCTX(connection_keepalive),
		     conf_editbox_handler, I(CONF_ping_interval),
		     I(-1));
#ifdef PERSOPORT
    	ctrl_editbox(s, "Anti-idle string", NO_SHORTCUT, 50,
		HELPCTX(no_help),
		//dlg_stdeditbox_handler, I(offsetof(Config,antiidle)),I(sizeof(((Config *)0)->antiidle)));
	        conf_editbox_handler, I(CONF_antiidle), I(1) ) ; 
#endif

	if (!midsession) {
	    s = ctrl_getset(b, "Connection", "tcp",
			    "Low-level TCP connection options");
	    ctrl_checkbox(s, "Disable Nagle's algorithm (TCP_NODELAY option)",
			  'n', HELPCTX(connection_nodelay),
			  conf_checkbox_handler,
			  I(CONF_tcp_nodelay));
	    ctrl_checkbox(s, "Enable TCP keepalives (SO_KEEPALIVE option)",
			  'p', HELPCTX(connection_tcpkeepalive),
			  conf_checkbox_handler,
			  I(CONF_tcp_keepalives));
#ifndef NO_IPV6
	    s = ctrl_getset(b, "Connection", "ipversion",
			  "Internet protocol version");
	    ctrl_radiobuttons(s, NULL, NO_SHORTCUT, 3,
			  HELPCTX(connection_ipversion),
			  conf_radiobutton_handler,
			  I(CONF_addressfamily),
			  "Auto", 'u', I(ADDRTYPE_UNSPEC),
			  "IPv4", '4', I(ADDRTYPE_IPV4),
			  "IPv6", '6', I(ADDRTYPE_IPV6),
			  NULL);
#endif

#ifdef RECONNECTPORT
	if( !get_param("PUTTY" ) && GetAutoreconnectFlag() ) {
		s = ctrl_getset(b, "Connection", "reconnect", "Reconnect options");
		ctrl_checkbox(s, "Attempt to reconnect on system wakeup", NO_SHORTCUT, HELPCTX(no_help), conf_checkbox_handler, I(CONF_wakeup_reconnect)) ;
		//dlg_stdcheckbox_handler, I(offsetof(Config,wakeup_reconnect)));
		ctrl_checkbox(s, "Attempt to reconnect on connection failure", NO_SHORTCUT, HELPCTX(no_help), conf_checkbox_handler, I(CONF_failure_reconnect)) ;
		//dlg_stdcheckbox_handler, I(offsetof(Config,failure_reconnect)));
	}
#endif

	    {
		const char *label = backend_from_proto(PROT_SSH) ?
		    "Logical name of remote host (e.g. for SSH key lookup):" :
		    "Logical name of remote host:";
		s = ctrl_getset(b, "Connection", "identity",
				"Logical name of remote host");
		ctrl_editbox(s, label, 'm', 100,
			     HELPCTX(connection_loghost),
			     conf_editbox_handler, I(CONF_loghost), I(1));
	    }
#ifdef PORTKNOCKINGPORT
    // port knocking panel
     if( !get_param("PUTTY") ) {
//    ctrl_settitle(b, "Connection/Port knocking", "Options controlling port knocking") ;
    s = ctrl_getset(b, "Connection", "PortKnocking",
			"Port knocking sequence");
    ctrl_editbox(s, "Sequence:",  NO_SHORTCUT, 100,
		 HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_portknockingoptions), I(1));
    ctrl_text(s, "The sequence is a list of port:protocol separated by comma. Valid protocols are tcp and udp.",HELPCTX(no_help));
    ctrl_text(s, "Special protocol 's' is used to include pause between knocks.",HELPCTX(no_help));
    ctrl_text(s, "Ex: 2001:tcp, 1:s, 2002:udp",HELPCTX(no_help));
	}
#endif
	}

	/*
	 * A sub-panel Connection/Data, containing options that
	 * decide on data to send to the server.
	 */
	if (!midsession) {
	    ctrl_settitle(b, "Connection/Data", "Data to send to the server");

	    s = ctrl_getset(b, "Connection/Data", "login",
			    "Login details");
	    ctrl_editbox(s, "Auto-login username", 'u', 50,
			 HELPCTX(connection_username),
			 conf_editbox_handler, I(CONF_username), I(1));
	    {
		/* We assume the local username is sufficiently stable
		 * to include on the dialog box. */
		char *user = get_username();
		char *userlabel = dupprintf("Use system username (%s)",
					    user ? user : "");
		sfree(user);
		ctrl_radiobuttons(s, "When username is not specified:", 'n', 4,
				  HELPCTX(connection_username_from_env),
				  conf_radiobutton_handler,
				  I(CONF_username_from_env),
				  "Prompt", I(FALSE),
				  userlabel, I(TRUE),
				  NULL);
		sfree(userlabel);
	    }
#ifdef PERSOPORT
	if( !get_param("PUTTY" ) ) {
#ifndef NO_PASSWORD
	c = ctrl_editbox(s, "Auto-login password", NO_SHORTCUT, 50,
		     HELPCTX(no_help),
		     conf_editbox_handler, I(CONF_password), I(1) ) ;
	c->editbox.password = 1;
#endif
	ctrl_editbox(s, "Command", NO_SHORTCUT, 74,
		     HELPCTX(no_help),
		     conf_editbox_handler, I(CONF_autocommand), I(1) ) ; 
	ctrl_filesel(s, "Login script file:", NO_SHORTCUT,
			"Scr Files (*.scr, *.txt)\0*.scr;*.txt\0All Files (*.*)\0*\0\0\0"
			 ,FALSE, "Select the login script file to load",
			 HELPCTX(no_help),
			 conf_scriptfilesel_handler, I(CONF_scriptfile) ) ; 
	ctrlScriptFileContentEdit = ctrl_editbox(s, "Login script content:", NO_SHORTCUT, 60,
		     HELPCTX(no_help),
		     conf_editbox_handler, I(CONF_scriptfilecontent), I(1) ) ;
	}
#endif

	    s = ctrl_getset(b, "Connection/Data", "term",
			    "Terminal details");
	    ctrl_editbox(s, "Terminal-type string", 't', 50,
			 HELPCTX(connection_termtype),
			 conf_editbox_handler, I(CONF_termtype), I(1));
	    ctrl_editbox(s, "Terminal speeds", 's', 50,
			 HELPCTX(connection_termspeed),
			 conf_editbox_handler, I(CONF_termspeed), I(1));

	    s = ctrl_getset(b, "Connection/Data", "env",
			    "Environment variables");
	    ctrl_columns(s, 2, 80, 20);
	    ed = (struct environ_data *)
		ctrl_alloc(b, sizeof(struct environ_data));
	    ed->varbox = ctrl_editbox(s, "Variable", 'v', 60,
				      HELPCTX(telnet_environ),
				      environ_handler, P(ed), P(NULL));
	    ed->varbox->generic.column = 0;
	    ed->valbox = ctrl_editbox(s, "Value", 'l', 60,
				      HELPCTX(telnet_environ),
				      environ_handler, P(ed), P(NULL));
	    ed->valbox->generic.column = 0;
	    ed->addbutton = ctrl_pushbutton(s, "Add", 'd',
					    HELPCTX(telnet_environ),
					    environ_handler, P(ed));
	    ed->addbutton->generic.column = 1;
	    ed->rembutton = ctrl_pushbutton(s, "Remove", 'r',
					    HELPCTX(telnet_environ),
					    environ_handler, P(ed));
	    ed->rembutton->generic.column = 1;
	    ctrl_columns(s, 1, 100);
	    ed->listbox = ctrl_listbox(s, NULL, NO_SHORTCUT,
				       HELPCTX(telnet_environ),
				       environ_handler, P(ed));
	    ed->listbox->listbox.height = 3;
	    ed->listbox->listbox.ncols = 2;
	    ed->listbox->listbox.percentages = snewn(2, int);
	    ed->listbox->listbox.percentages[0] = 30;
	    ed->listbox->listbox.percentages[1] = 70;
	}

    }

    if (!midsession) {
	/*
	 * The Connection/Proxy panel.
	 */
	ctrl_settitle(b, "Connection/Proxy",
		      "Options controlling proxy usage");

	s = ctrl_getset(b, "Connection/Proxy", "basics", NULL);
	ctrl_radiobuttons(s, "Proxy type:", 't', 3,
			  HELPCTX(proxy_type),
			  conf_radiobutton_handler,
			  I(CONF_proxy_type),
			  "None", I(PROXY_NONE),
			  "SOCKS 4", I(PROXY_SOCKS4),
			  "SOCKS 5", I(PROXY_SOCKS5),
			  "HTTP", I(PROXY_HTTP),
			  "Telnet", I(PROXY_TELNET),
			  NULL);
	ctrl_columns(s, 2, 80, 20);
	c = ctrl_editbox(s, "Proxy hostname", 'y', 100,
			 HELPCTX(proxy_main),
			 conf_editbox_handler,
			 I(CONF_proxy_host), I(1));
	c->generic.column = 0;
	c = ctrl_editbox(s, "Port", 'p', 100,
			 HELPCTX(proxy_main),
			 conf_editbox_handler,
			 I(CONF_proxy_port),
			 I(-1));
	c->generic.column = 1;
	ctrl_columns(s, 1, 100);
	ctrl_editbox(s, "Exclude Hosts/IPs", 'e', 100,
		     HELPCTX(proxy_exclude),
		     conf_editbox_handler,
		     I(CONF_proxy_exclude_list), I(1));
	ctrl_checkbox(s, "Consider proxying local host connections", 'x',
		      HELPCTX(proxy_exclude),
		      conf_checkbox_handler,
		      I(CONF_even_proxy_localhost));
	ctrl_radiobuttons(s, "Do DNS name lookup at proxy end:", 'd', 3,
			  HELPCTX(proxy_dns),
			  conf_radiobutton_handler,
			  I(CONF_proxy_dns),
			  "No", I(FORCE_OFF),
			  "Auto", I(AUTO),
			  "Yes", I(FORCE_ON), NULL);
	ctrl_editbox(s, "Username", 'u', 60,
		     HELPCTX(proxy_auth),
		     conf_editbox_handler,
		     I(CONF_proxy_username), I(1));
	c = ctrl_editbox(s, "Password", 'w', 60,
			 HELPCTX(proxy_auth),
			 conf_editbox_handler,
			 I(CONF_proxy_password), I(1));
	c->editbox.password = 1;
	ctrl_editbox(s, "Telnet command", 'm', 100,
		     HELPCTX(proxy_command),
		     conf_editbox_handler,
		     I(CONF_proxy_telnet_command), I(1));
		     
	ctrl_radiobuttons(s, "Print proxy diagnostics "
                          "in the terminal window", 'r', 5,
			  HELPCTX(proxy_logging),
			  conf_radiobutton_handler,
			  I(CONF_proxy_log_to_term),
			  "No", I(FORCE_OFF),
			  "Yes", I(FORCE_ON),
			  "Only until session starts", I(AUTO), NULL);
    }

    /*
     * The Telnet panel exists in the base config box, and in a
     * mid-session reconfig box _if_ we're using Telnet.
     */
    if (!midsession || protocol == PROT_TELNET) {
	/*
	 * The Connection/Telnet panel.
	 */
	ctrl_settitle(b, "Connection/Telnet",
		      "Options controlling Telnet connections");

	s = ctrl_getset(b, "Connection/Telnet", "protocol",
			"Telnet protocol adjustments");

	if (!midsession) {
	    ctrl_radiobuttons(s, "Handling of OLD_ENVIRON ambiguity:",
			      NO_SHORTCUT, 2,
			      HELPCTX(telnet_oldenviron),
			      conf_radiobutton_handler,
			      I(CONF_rfc_environ),
			      "BSD (commonplace)", 'b', I(0),
			      "RFC 1408 (unusual)", 'f', I(1), NULL);
	    ctrl_radiobuttons(s, "Telnet negotiation mode:", 't', 2,
			      HELPCTX(telnet_passive),
			      conf_radiobutton_handler,
			      I(CONF_passive_telnet),
			      "Passive", I(1), "Active", I(0), NULL);
	}
	ctrl_checkbox(s, "Keyboard sends Telnet special commands", 'k',
		      HELPCTX(telnet_specialkeys),
		      conf_checkbox_handler,
		      I(CONF_telnet_keyboard));
	ctrl_checkbox(s, "Return key sends Telnet New Line instead of ^M",
		      'm', HELPCTX(telnet_newline),
		      conf_checkbox_handler,
		      I(CONF_telnet_newline));
    }

    if (!midsession) {

	/*
	 * The Connection/Rlogin panel.
	 */
	ctrl_settitle(b, "Connection/Rlogin",
		      "Options controlling Rlogin connections");

	s = ctrl_getset(b, "Connection/Rlogin", "data",
			"Data to send to the server");
	ctrl_editbox(s, "Local username:", 'l', 50,
		     HELPCTX(rlogin_localuser),
		     conf_editbox_handler, I(CONF_localusername), I(1));

    }

    /*
     * All the SSH stuff is omitted in PuTTYtel, or in a reconfig
     * when we're not doing SSH.
     */

    if (backend_from_proto(PROT_SSH) && (!midsession || protocol == PROT_SSH)) {

	/*
	 * The Connection/SSH panel.
	 */
	ctrl_settitle(b, "Connection/SSH",
		      "Options controlling SSH connections");

	/* SSH-1 or connection-sharing downstream */
	if (midsession && (protcfginfo == 1 || protcfginfo == -1)) {
	    s = ctrl_getset(b, "Connection/SSH", "disclaimer", NULL);
	    ctrl_text(s, "Nothing on this panel may be reconfigured in mid-"
		      "session; it is only here so that sub-panels of it can "
		      "exist without looking strange.", HELPCTX(no_help));
	}

	if (!midsession) {

	    s = ctrl_getset(b, "Connection/SSH", "data",
			    "Data to send to the server");
	    ctrl_editbox(s, "Remote command:", 'r', 100,
			 HELPCTX(ssh_command),
			 conf_editbox_handler, I(CONF_remote_cmd), I(1));

	    s = ctrl_getset(b, "Connection/SSH", "protocol", "Protocol options");
	    ctrl_checkbox(s, "Don't start a shell or command at all", 'n',
			  HELPCTX(ssh_noshell),
			  conf_checkbox_handler,
			  I(CONF_ssh_no_shell));
	}

	if (!midsession || !(protcfginfo == 1 || protcfginfo == -1)) {
	    s = ctrl_getset(b, "Connection/SSH", "protocol", "Protocol options");

	    ctrl_checkbox(s, "Enable compression", 'e',
			  HELPCTX(ssh_compress),
			  conf_checkbox_handler,
			  I(CONF_compression));
	}

	if (!midsession) {
	    s = ctrl_getset(b, "Connection/SSH", "sharing", "Sharing an SSH connection between PuTTY tools");

	    ctrl_checkbox(s, "Share SSH connections if possible", 's',
			  HELPCTX(ssh_share),
			  conf_checkbox_handler,
			  I(CONF_ssh_connection_sharing));

            ctrl_text(s, "Permitted roles in a shared connection:",
                      HELPCTX(ssh_share));
	    ctrl_checkbox(s, "Upstream (connecting to the real server)", 'u',
			  HELPCTX(ssh_share),
			  conf_checkbox_handler,
			  I(CONF_ssh_connection_sharing_upstream));
	    ctrl_checkbox(s, "Downstream (connecting to the upstream PuTTY)", 'd',
			  HELPCTX(ssh_share),
			  conf_checkbox_handler,
			  I(CONF_ssh_connection_sharing_downstream));
	}
	
#ifdef PERSOPORT
	if (!midsession) {
	    s = ctrl_getset(b, "Connection/SSH", "pscp", "PSCP integration") ;
	   ctrl_checkbox(s, "Send file in current directory", NO_SHORTCUT,
			  HELPCTX(no_help),
			  conf_checkbox_handler,
			  I(CONF_scp_auto_pwd));	
	}
#endif
	
	if (!midsession) {
	    s = ctrl_getset(b, "Connection/SSH", "protocol", "Protocol options");

	    ctrl_radiobuttons(s, "SSH protocol version:", NO_SHORTCUT, 2,
			      HELPCTX(ssh_protocol),
			      conf_radiobutton_handler,
			      I(CONF_sshprot),
			      "2", '2', I(3),
			      "1 (INSECURE)", '1', I(0), NULL);
	}

	/*
	 * The Connection/SSH/Kex panel. (Owing to repeat key
	 * exchange, much of this is meaningful in mid-session _if_
	 * we're using SSH-2 and are not a connection-sharing
	 * downstream, or haven't decided yet.)
	 */
	if (protcfginfo != 1 && protcfginfo != -1) {
	    ctrl_settitle(b, "Connection/SSH/Kex",
			  "Options controlling SSH key exchange");

	    s = ctrl_getset(b, "Connection/SSH/Kex", "main",
			    "Key exchange algorithm options");
	    c = ctrl_draglist(s, "Algorithm selection policy:", 's',
			      HELPCTX(ssh_kexlist),
			      kexlist_handler, P(NULL));
	    c->listbox.height = 5;

	    s = ctrl_getset(b, "Connection/SSH/Kex", "repeat",
			    "Options controlling key re-exchange");

	    ctrl_editbox(s, "Max minutes before rekey (0 for no limit)", 't', 20,
			 HELPCTX(ssh_kex_repeat),
			 conf_editbox_handler,
			 I(CONF_ssh_rekey_time),
			 I(-1));
	    ctrl_editbox(s, "Max data before rekey (0 for no limit)", 'x', 20,
			 HELPCTX(ssh_kex_repeat),
			 conf_editbox_handler,
			 I(CONF_ssh_rekey_data),
			 I(16));
	    ctrl_text(s, "(Use 1M for 1 megabyte, 1G for 1 gigabyte etc)",
		      HELPCTX(ssh_kex_repeat));
	}

	/*
	 * The 'Connection/SSH/Host keys' panel.
	 */
	if (protcfginfo != 1 && protcfginfo != -1) {
	    ctrl_settitle(b, "Connection/SSH/Host keys",
			  "Options controlling SSH host keys");

	    s = ctrl_getset(b, "Connection/SSH/Host keys", "main",
			    "Host key algorithm preference");
	    c = ctrl_draglist(s, "Algorithm selection policy:", 's',
			      HELPCTX(ssh_hklist),
			      hklist_handler, P(NULL));
	    c->listbox.height = 5;
	}

	/*
	 * Manual host key configuration is irrelevant mid-session,
	 * as we enforce that the host key for rekeys is the
	 * same as that used at the start of the session.
	 */
	if (!midsession) {
	    s = ctrl_getset(b, "Connection/SSH/Host keys", "hostkeys",
			    "Manually configure host keys for this connection");

            ctrl_columns(s, 2, 75, 25);
            c = ctrl_text(s, "Host keys or fingerprints to accept:",
                          HELPCTX(ssh_kex_manual_hostkeys));
            c->generic.column = 0;
            /* You want to select from the list, _then_ hit Remove. So
             * tab order should be that way round. */
            mh = (struct manual_hostkey_data *)
                ctrl_alloc(b,sizeof(struct manual_hostkey_data));
            mh->rembutton = ctrl_pushbutton(s, "Remove", 'r',
                                            HELPCTX(ssh_kex_manual_hostkeys),
                                            manual_hostkey_handler, P(mh));
            mh->rembutton->generic.column = 1;
            mh->rembutton->generic.tabdelay = 1;
            mh->listbox = ctrl_listbox(s, NULL, NO_SHORTCUT,
                                       HELPCTX(ssh_kex_manual_hostkeys),
                                       manual_hostkey_handler, P(mh));
            /* This list box can't be very tall, because there's not
             * much room in the pane on Windows at least. This makes
             * it become really unhelpful if a horizontal scrollbar
             * appears, so we suppress that. */
            mh->listbox->listbox.height = 2;
            mh->listbox->listbox.hscroll = FALSE;
            ctrl_tabdelay(s, mh->rembutton);
	    mh->keybox = ctrl_editbox(s, "Key", 'k', 80,
                                      HELPCTX(ssh_kex_manual_hostkeys),
                                      manual_hostkey_handler, P(mh), P(NULL));
            mh->keybox->generic.column = 0;
            mh->addbutton = ctrl_pushbutton(s, "Add key", 'y',
                                            HELPCTX(ssh_kex_manual_hostkeys),
                                            manual_hostkey_handler, P(mh));
            mh->addbutton->generic.column = 1;
            ctrl_columns(s, 1, 100);
	}

	if (!midsession || !(protcfginfo == 1 || protcfginfo == -1)) {
	    /*
	     * The Connection/SSH/Cipher panel.
	     */
	    ctrl_settitle(b, "Connection/SSH/Cipher",
			  "Options controlling SSH encryption");

	    s = ctrl_getset(b, "Connection/SSH/Cipher",
                            "encryption", "Encryption options");
	    c = ctrl_draglist(s, "Encryption cipher selection policy:", 's',
			      HELPCTX(ssh_ciphers),
			      cipherlist_handler, P(NULL));
	    c->listbox.height = 6;

	    ctrl_checkbox(s, "Enable legacy use of single-DES in SSH-2", 'i',
			  HELPCTX(ssh_ciphers),
			  conf_checkbox_handler,
			  I(CONF_ssh2_des_cbc));
	}

	if (!midsession) {

	    /*
	     * The Connection/SSH/Auth panel.
	     */
	    ctrl_settitle(b, "Connection/SSH/Auth",
			  "Options controlling SSH authentication");

	    s = ctrl_getset(b, "Connection/SSH/Auth", "main", NULL);
	    ctrl_checkbox(s, "Display pre-authentication banner (SSH-2 only)",
			  'd', HELPCTX(ssh_auth_banner),
			  conf_checkbox_handler,
			  I(CONF_ssh_show_banner));
	    ctrl_checkbox(s, "Bypass authentication entirely (SSH-2 only)", 'b',
			  HELPCTX(ssh_auth_bypass),
			  conf_checkbox_handler,
			  I(CONF_ssh_no_userauth));

	    s = ctrl_getset(b, "Connection/SSH/Auth", "methods",
			    "Authentication methods");
	    ctrl_checkbox(s, "Attempt authentication using Pageant", 'p',
			  HELPCTX(ssh_auth_pageant),
			  conf_checkbox_handler,
			  I(CONF_tryagent));
	    ctrl_checkbox(s, "Attempt TIS or CryptoCard auth (SSH-1)", 'm',
			  HELPCTX(ssh_auth_tis),
			  conf_checkbox_handler,
			  I(CONF_try_tis_auth));
	    ctrl_checkbox(s, "Attempt \"keyboard-interactive\" auth (SSH-2)",
			  'i', HELPCTX(ssh_auth_ki),
			  conf_checkbox_handler,
			  I(CONF_try_ki_auth));

	    s = ctrl_getset(b, "Connection/SSH/Auth", "params",
			    "Authentication parameters");
	    ctrl_checkbox(s, "Allow agent forwarding", 'f',
			  HELPCTX(ssh_auth_agentfwd),
			  conf_checkbox_handler, I(CONF_agentfwd));
	    ctrl_checkbox(s, "Allow attempted changes of username in SSH-2", NO_SHORTCUT,
			  HELPCTX(ssh_auth_changeuser),
			  conf_checkbox_handler,
			  I(CONF_change_username));
	    ctrl_filesel(s, "Private key file for authentication:", 'k',
			 FILTER_KEY_FILES, FALSE, "Select private key file",
			 HELPCTX(ssh_auth_privkey),
			 conf_filesel_handler, I(CONF_keyfile));

#ifndef NO_GSSAPI
	    /*
	     * Connection/SSH/Auth/GSSAPI, which sadly won't fit on
	     * the main Auth panel.
	     */
	    ctrl_settitle(b, "Connection/SSH/Auth/GSSAPI",
			  "Options controlling GSSAPI authentication");
	    s = ctrl_getset(b, "Connection/SSH/Auth/GSSAPI", "gssapi", NULL);

	    ctrl_checkbox(s, "Attempt GSSAPI authentication (SSH-2 only)",
			  't', HELPCTX(ssh_gssapi),
			  conf_checkbox_handler,
			  I(CONF_try_gssapi_auth));

	    ctrl_checkbox(s, "Allow GSSAPI credential delegation", 'l',
			  HELPCTX(ssh_gssapi_delegation),
			  conf_checkbox_handler,
			  I(CONF_gssapifwd));

	    /*
	     * GSSAPI library selection.
	     */
	    if (ngsslibs > 1) {
		c = ctrl_draglist(s, "Preference order for GSSAPI libraries:",
				  'p', HELPCTX(ssh_gssapi_libraries),
				  gsslist_handler, P(NULL));
		c->listbox.height = ngsslibs;

		/*
		 * I currently assume that if more than one GSS
		 * library option is available, then one of them is
		 * 'user-supplied' and so we should present the
		 * following file selector. This is at least half-
		 * reasonable, because if we're using statically
		 * linked GSSAPI then there will only be one option
		 * and no way to load from a user-supplied library,
		 * whereas if we're using dynamic libraries then
		 * there will almost certainly be some default
		 * option in addition to a user-supplied path. If
		 * anyone ever ports PuTTY to a system on which
		 * dynamic-library GSSAPI is available but there is
		 * absolutely no consensus on where to keep the
		 * libraries, there'll need to be a flag alongside
		 * ngsslibs to control whether the file selector is
		 * displayed. 
		 */

		ctrl_filesel(s, "User-supplied GSSAPI library path:", 's',
			     FILTER_DYNLIB_FILES, FALSE, "Select library file",
			     HELPCTX(ssh_gssapi_libraries),
			     conf_filesel_handler,
			     I(CONF_ssh_gss_custom));
	    }
#endif
	}

	if (!midsession) {
	    /*
	     * The Connection/SSH/TTY panel.
	     */
	    ctrl_settitle(b, "Connection/SSH/TTY", "Remote terminal settings");

	    s = ctrl_getset(b, "Connection/SSH/TTY", "sshtty", NULL);
	    ctrl_checkbox(s, "Don't allocate a pseudo-terminal", 'p',
			  HELPCTX(ssh_nopty),
			  conf_checkbox_handler,
			  I(CONF_nopty));

	    s = ctrl_getset(b, "Connection/SSH/TTY", "ttymodes",
			    "Terminal modes");
	    td = (struct ttymodes_data *)
		ctrl_alloc(b, sizeof(struct ttymodes_data));
	    c = ctrl_text(s, "Terminal modes to send:", HELPCTX(ssh_ttymodes));
	    td->listbox = ctrl_listbox(s, NULL, NO_SHORTCUT,
				       HELPCTX(ssh_ttymodes),
				       ttymodes_handler, P(td));
	    td->listbox->listbox.height = 8;
	    td->listbox->listbox.ncols = 2;
	    td->listbox->listbox.percentages = snewn(2, int);
	    td->listbox->listbox.percentages[0] = 40;
	    td->listbox->listbox.percentages[1] = 60;
	    ctrl_columns(s, 2, 75, 25);
	    c = ctrl_text(s, "For selected mode, send:", HELPCTX(ssh_ttymodes));
	    c->generic.column = 0;
	    td->setbutton = ctrl_pushbutton(s, "Set", 's',
					    HELPCTX(ssh_ttymodes),
					    ttymodes_handler, P(td));
	    td->setbutton->generic.column = 1;
	    td->setbutton->generic.tabdelay = 1;
	    ctrl_columns(s, 1, 100);	    /* column break */
	    /* Bit of a hack to get the value radio buttons and
	     * edit-box on the same row. */
	    ctrl_columns(s, 2, 75, 25);
	    td->valradio = ctrl_radiobuttons(s, NULL, NO_SHORTCUT, 3,
					     HELPCTX(ssh_ttymodes),
					     ttymodes_handler, P(td),
					     "Auto", NO_SHORTCUT, P(NULL),
					     "Nothing", NO_SHORTCUT, P(NULL),
					     "This:", NO_SHORTCUT, P(NULL),
					     NULL);
	    td->valradio->generic.column = 0;
	    td->valbox = ctrl_editbox(s, NULL, NO_SHORTCUT, 100,
				      HELPCTX(ssh_ttymodes),
				      ttymodes_handler, P(td), P(NULL));
	    td->valbox->generic.column = 1;
	    ctrl_tabdelay(s, td->setbutton);

	}

	if (!midsession) {
	    /*
	     * The Connection/SSH/X11 panel.
	     */
	    ctrl_settitle(b, "Connection/SSH/X11",
			  "Options controlling SSH X11 forwarding");

	    s = ctrl_getset(b, "Connection/SSH/X11", "x11", "X11 forwarding");
	    ctrl_checkbox(s, "Enable X11 forwarding", 'e',
			  HELPCTX(ssh_tunnels_x11),
			  conf_checkbox_handler,I(CONF_x11_forward));
	    ctrl_editbox(s, "X display location", 'x', 50,
			 HELPCTX(ssh_tunnels_x11),
			 conf_editbox_handler, I(CONF_x11_display), I(1));
	    ctrl_radiobuttons(s, "Remote X11 authentication protocol", 'u', 2,
			      HELPCTX(ssh_tunnels_x11auth),
			      conf_radiobutton_handler,
			      I(CONF_x11_auth),
			      "MIT-Magic-Cookie-1", I(X11_MIT),
			      "XDM-Authorization-1", I(X11_XDM), NULL);
	}

	/*
	 * The Tunnels panel _is_ still available in mid-session.
	 */
	ctrl_settitle(b, "Connection/SSH/Tunnels",
		      "Options controlling SSH port forwarding");

	s = ctrl_getset(b, "Connection/SSH/Tunnels", "portfwd",
			"Port forwarding");
	ctrl_checkbox(s, "Local ports accept connections from other hosts",'t',
		      HELPCTX(ssh_tunnels_portfwd_localhost),
		      conf_checkbox_handler,
		      I(CONF_lport_acceptall));
	ctrl_checkbox(s, "Remote ports do the same (SSH-2 only)", 'p',
		      HELPCTX(ssh_tunnels_portfwd_localhost),
		      conf_checkbox_handler,
		      I(CONF_rport_acceptall));

	ctrl_columns(s, 3, 55, 20, 25);
	c = ctrl_text(s, "Forwarded ports:", HELPCTX(ssh_tunnels_portfwd));
	c->generic.column = COLUMN_FIELD(0,2);
	/* You want to select from the list, _then_ hit Remove. So tab order
	 * should be that way round. */
	pfd = (struct portfwd_data *)ctrl_alloc(b,sizeof(struct portfwd_data));
	pfd->rembutton = ctrl_pushbutton(s, "Remove", 'r',
					 HELPCTX(ssh_tunnels_portfwd),
					 portfwd_handler, P(pfd));
	pfd->rembutton->generic.column = 2;
	pfd->rembutton->generic.tabdelay = 1;
	pfd->listbox = ctrl_listbox(s, NULL, NO_SHORTCUT,
				    HELPCTX(ssh_tunnels_portfwd),
				    portfwd_handler, P(pfd));
#ifdef PERSOPORT
	pfd->listbox->listbox.height = 12 ;
#else
	pfd->listbox->listbox.height = 3;
#endif
	pfd->listbox->listbox.ncols = 2;
	pfd->listbox->listbox.percentages = snewn(2, int);
	pfd->listbox->listbox.percentages[0] = 20;
	pfd->listbox->listbox.percentages[1] = 80;
	ctrl_tabdelay(s, pfd->rembutton);
	ctrl_text(s, "Add new forwarded port:", HELPCTX(ssh_tunnels_portfwd));
	/* You want to enter source, destination and type, _then_ hit Add.
	 * Again, we adjust the tab order to reflect this. */
	pfd->addbutton = ctrl_pushbutton(s, "Add", 'd',
					 HELPCTX(ssh_tunnels_portfwd),
					 portfwd_handler, P(pfd));
	pfd->addbutton->generic.column = 2;
	pfd->addbutton->generic.tabdelay = 1;
	pfd->sourcebox = ctrl_editbox(s, "Source port", 's', 40,
				      HELPCTX(ssh_tunnels_portfwd),
				      portfwd_handler, P(pfd), P(NULL));
	pfd->sourcebox->generic.column = 0;
	pfd->destbox = ctrl_editbox(s, "Destination", 'i', 67,
				    HELPCTX(ssh_tunnels_portfwd),
				    portfwd_handler, P(pfd), P(NULL));
	pfd->direction = ctrl_radiobuttons(s, NULL, NO_SHORTCUT, 3,
					   HELPCTX(ssh_tunnels_portfwd),
					   portfwd_handler, P(pfd),
					   "Local", 'l', P(NULL),
					   "Remote", 'm', P(NULL),
					   "Dynamic", 'y', P(NULL),
					   NULL);
#ifndef NO_IPV6
	pfd->addressfamily =
	    ctrl_radiobuttons(s, NULL, NO_SHORTCUT, 3,
			      HELPCTX(ssh_tunnels_portfwd_ipversion),
			      portfwd_handler, P(pfd),
			      "Auto", 'u', I(ADDRTYPE_UNSPEC),
			      "IPv4", '4', I(ADDRTYPE_IPV4),
			      "IPv6", '6', I(ADDRTYPE_IPV6),
			      NULL);
#endif
	ctrl_tabdelay(s, pfd->addbutton);
	ctrl_columns(s, 1, 100);

	if (!midsession) {
	    /*
	     * The Connection/SSH/Bugs panels.
	     */
	    ctrl_settitle(b, "Connection/SSH/Bugs",
			  "Workarounds for SSH server bugs");

	    s = ctrl_getset(b, "Connection/SSH/Bugs", "main",
			    "Detection of known bugs in SSH servers");
	    ctrl_droplist(s, "Chokes on SSH-2 ignore messages", '2', 20,
			  HELPCTX(ssh_bugs_ignore2),
			  sshbug_handler, I(CONF_sshbug_ignore2));
	    ctrl_droplist(s, "Handles SSH-2 key re-exchange badly", 'k', 20,
			  HELPCTX(ssh_bugs_rekey2),
			  sshbug_handler, I(CONF_sshbug_rekey2));
	    ctrl_droplist(s, "Chokes on PuTTY's SSH-2 'winadj' requests", 'j',
                          20, HELPCTX(ssh_bugs_winadj),
			  sshbug_handler, I(CONF_sshbug_winadj));
	    ctrl_droplist(s, "Replies to requests on closed channels", 'q', 20,
			  HELPCTX(ssh_bugs_chanreq),
			  sshbug_handler, I(CONF_sshbug_chanreq));
	    ctrl_droplist(s, "Ignores SSH-2 maximum packet size", 'x', 20,
			  HELPCTX(ssh_bugs_maxpkt2),
			  sshbug_handler, I(CONF_sshbug_maxpkt2));
			  
	    ctrl_settitle(b, "Connection/SSH/More bugs",
			  "Further workarounds for SSH server bugs");

	    s = ctrl_getset(b, "Connection/SSH/More bugs", "main",
			    "Detection of known bugs in SSH servers");
	    ctrl_droplist(s, "Requires padding on SSH-2 RSA signatures", 'p', 20,
			  HELPCTX(ssh_bugs_rsapad2),
			  sshbug_handler, I(CONF_sshbug_rsapad2));
	    ctrl_droplist(s, "Only supports pre-RFC4419 SSH-2 DH GEX", 'd', 20,
			  HELPCTX(ssh_bugs_oldgex2),
			  sshbug_handler, I(CONF_sshbug_oldgex2));
	    ctrl_droplist(s, "Miscomputes SSH-2 HMAC keys", 'm', 20,
			  HELPCTX(ssh_bugs_hmac2),
			  sshbug_handler, I(CONF_sshbug_hmac2));
	    ctrl_droplist(s, "Misuses the session ID in SSH-2 PK auth", 'n', 20,
			  HELPCTX(ssh_bugs_pksessid2),
			  sshbug_handler, I(CONF_sshbug_pksessid2));
	    ctrl_droplist(s, "Miscomputes SSH-2 encryption keys", 'e', 20,
			  HELPCTX(ssh_bugs_derivekey2),
			  sshbug_handler, I(CONF_sshbug_derivekey2));
	    ctrl_droplist(s, "Chokes on SSH-1 ignore messages", 'i', 20,
			  HELPCTX(ssh_bugs_ignore1),
			  sshbug_handler, I(CONF_sshbug_ignore1));
	    ctrl_droplist(s, "Refuses all SSH-1 password camouflage", 's', 20,
			  HELPCTX(ssh_bugs_plainpw1),
			  sshbug_handler, I(CONF_sshbug_plainpw1));
	    ctrl_droplist(s, "Chokes on SSH-1 RSA authentication", 'r', 20,
			  HELPCTX(ssh_bugs_rsa1),
			  sshbug_handler, I(CONF_sshbug_rsa1));
	}
    }
#ifdef ZMODEMPORT
    // z-modem panel
     if( (!get_param("PUTTY"))&&get_param("ZMODEM") ) {
    ctrl_settitle(b, "Connection/ZModem",
		      "Options controlling Z Modem transfers");
   s = ctrl_getset(b, "Connection/ZModem", "download",
			"Download folder");
    ctrl_editbox(s, "Location:",  NO_SHORTCUT, 100,
		 HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_zdownloaddir), I(1));
 
    ctrl_settitle(b, "Connection/ZModem/rz",
		  "rz path and options");

    s = ctrl_getset(b, "Connection/ZModem/rz", "receive",
			"Receive command");

    ctrl_filesel(s, "Command rz:", NO_SHORTCUT,
		 FILTER_EXE_FILES, FALSE, "Select command to receive zmodem data",
		 HELPCTX(no_help),
		 conf_filesel_handler, I(CONF_rzcommand) ) ; 

    ctrl_editbox(s, "Options", NO_SHORTCUT, 
		     50,
		     HELPCTX(no_help),
		     conf_editbox_handler, I(CONF_rzoptions), I(1)); 

    ctrl_settitle(b, "Connection/ZModem/sz",
		  "sz path and options");

    s = ctrl_getset(b, "Connection/ZModem/sz", "send",
			"Send command");

    ctrl_filesel(s, "Command sz:", NO_SHORTCUT,
		 FILTER_EXE_FILES, FALSE, "Select command to send zmodem data",
		 HELPCTX(no_help),
		 conf_filesel_handler, I(CONF_szcommand) ) ; 

    ctrl_editbox(s, "Options", NO_SHORTCUT, 
		     50,
		     HELPCTX(no_help),
		     conf_editbox_handler, I(CONF_szoptions), I(1)); 
/**/
	}
#endif
#ifdef PERSOPORT
	if( !get_param("PUTTY") ) {
		ctrl_settitle(b, "Comment", "Options comments");
		s = ctrl_getset(b, "Comment", "freecomment",
			"Now you can add comments on your session");
		ctrl_editbox(s, "Comment", NO_SHORTCUT, 100,
		 HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_comment),
		 I(1));
		ctrl_editbox(s, "SFTP connect", NO_SHORTCUT, 100,
		 HELPCTX(no_help),
		 conf_editbox_handler, I(CONF_sftpconnect),
		 I(1));
		}
#endif
}

#ifdef ZMODEMPORT
/*
 * The standard directory-selector handler expects the main `context'
 * field to contain the `offsetof' a Filename field in the
 * structure pointed to by `data'.
 */
void conf_directorysel_handler(union control *ctrl, void *dlg, void *data, int event) {
    /*
     * The standard file-selector handler expects the `context'
     * field to contain the `offsetof' a Filename field in the
     * structure pointed to by `data'.
     */
    int offset = ctrl->directoryselect.context.i;

    if (event == EVENT_REFRESH) {
	dlg_directorysel_set(ctrl, dlg, *(Filename *)ATOFFSET(data, offset));
    } else if (event == EVENT_VALCHANGE) {
	dlg_directorysel_get(ctrl, dlg, (Filename *)ATOFFSET(data, offset));
    }
}
#endif

