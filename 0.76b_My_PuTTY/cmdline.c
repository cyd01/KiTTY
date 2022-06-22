/*
 * cmdline.c - command-line parsing shared between many of the
 * PuTTY applications
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "putty.h"

/*
 * Some command-line parameters need to be saved up until after
 * we've loaded the saved session which will form the basis of our
 * eventual running configuration. For this we use the macro
 * SAVEABLE, which notices if the `need_save' parameter is set and
 * saves the parameter and value on a list.
 * 
 * We also assign priorities to saved parameters, just to slightly
 * ameliorate silly ordering problems. For example, if you specify
 * a saved session to load, it will be loaded _before_ all your
 * local modifications such as -L are evaluated; and if you specify
 * a protocol and a port, the protocol is set up first so that the
 * port can override its choice of port number.
 * 
 * (In fact -load is not saved at all, since in at least Plink the
 * processing of further command-line options depends on whether or
 * not the loaded session contained a hostname. So it must be
 * executed immediately.)
 */

#ifdef MOD_PERSO
#include "kitty_crypt.h"
#include "kitty.h"
int decode64(char *buffer) ;
void SetAutoStoreSSHKey( void ) ;
void load_open_settings_forced(char *filename, Conf *conf) ;
void SetPasswordInConfig( const char * password ) ;
void SetAutoStoreSSHKeyFlag( const int flag ) ;
int GetCryptSaltFlag() ;
extern char CurrentFolder[1024] ;
// Manage connect string as: user:pass@@@hostname:port/cmd
void ManageConnectString( Conf *cf, char * hostname ) {
	int i, j ;
	char *p, *us, *pw, *hn, *po, *cm ;
	us = (char*)malloc(strlen(hostname)+1); strcpy(us,"");
	pw = (char*)malloc(strlen(hostname)+1); strcpy(pw,"");
	hn = (char*)malloc(strlen(hostname)+1); strcpy(hn,hostname);
	po = (char*)malloc(strlen(hostname)+1); strcpy(po,"");
	cm = (char*)malloc(strlen(hostname)+1); strcpy(cm,"");
	for( i=0 ; i<strlen(hostname) ; i++ ) {
		if( hostname[i]=='@' ) {
			if( hostname[i+1]=='@' ) {
				i++ ; 
			} else {
				strcpy( us, hostname ) ;
				us[i] = '\0' ;
				j = 0 ;
				do { i++; hn[j]=hostname[i]; j++; } while( hostname[i]!='\0' ) ;
				break ;
			}
		}
	}
	if( hn[0]=='[' ) { // IPv6
		char *q ;
		if( (q = strstr( hn, "]" )) != NULL ) {
			q++ ;
			if( (p = strstr( q, "/"  )) != NULL ) {
				strcpy( cm, p+1 ) ;
				p[0] = '\0' ;
			}
			if( (p = strstr( q, ":"  )) != NULL ) {
				strcpy( po, p+1 ) ;
				p[0] = '\0' ;
			}
			q[0] = '\0' ;
		}
	} else { // IPv4
		if( (p = strstr( hn, "/"  )) != NULL ) {
			strcpy( cm, p+1 ) ;
			p[0] = '\0' ;
		}
		if( (p = strstr( hn, ":"  )) != NULL ) {
			strcpy( po, p+1 ) ;
			p[0] = '\0' ;
		}

	}
	if( strlen(us)>0 ) {
		if( (p = strstr( us, ":" )) != NULL ) {
			strcpy( pw, p+1 ) ;
			p[0] = '\0' ;
			while( (p = strstr( pw, "@@" )) != NULL ) { // Manage @ in password ( @ => double @@)
				do { p[0] = p[1] ; p++ ; } while( p[0] != '\0' ) ;
			}
		}
		if( strlen(pw)>0 ) {
			SetPasswordInConfig( pw ) ;
		}
		sprintf( hostname, "%s@%s", us, hn ) ;
	} else {
		strcpy( hostname, hn ) ;
	}
	if( strlen(po)>0 ) {
		conf_set_int( cf, CONF_port, atoi(po) ) ;
	}
	if( strlen(cm)>0 ) {
		if( cm[0] == '#' ) {
			decryptstring( GetCryptSaltFlag(), (char*)cm+1, MASTER_PASSWORD ) ;
			conf_set_str( cf, CONF_autocommand, cm+1 ) ;
		} else {
			char *s = (char*)malloc( strlen(cm)+1 ) ;
			strcpy( s, cm ) ;
			int i = decode64(s) ;
			s[i] = '\0' ;
			conf_set_str( cf,CONF_autocommand,s ) ;
			free(s) ;
		}
	}
	free(cm);free(po);free(hn);free(pw);free(us);
}
#endif
#ifdef MOD_ADB
int GetADBFlag(void) ;
#endif

#define NPRIORITIES 2

struct cmdline_saved_param {
    char *p, *value;
};
struct cmdline_saved_param_set {
    struct cmdline_saved_param *params;
    size_t nsaved, savesize;
};

/*
 * C guarantees this structure will be initialised to all zero at
 * program start, which is exactly what we want.
 */
static struct cmdline_saved_param_set saves[NPRIORITIES];

static void cmdline_save_param(const char *p, const char *value, int pri)
{
    sgrowarray(saves[pri].params, saves[pri].savesize, saves[pri].nsaved);
    saves[pri].params[saves[pri].nsaved].p = dupstr(p);
    saves[pri].params[saves[pri].nsaved].value = dupstr(value);
    saves[pri].nsaved++;
}

static char *cmdline_password = NULL;

void cmdline_cleanup(void)
{
    int pri;

    if (cmdline_password) {
	smemclr(cmdline_password, strlen(cmdline_password));
	sfree(cmdline_password);
	cmdline_password = NULL;
    }
    
    for (pri = 0; pri < NPRIORITIES; pri++) {
	sfree(saves[pri].params);
	saves[pri].params = NULL;
	saves[pri].savesize = 0;
	saves[pri].nsaved = 0;
    }
}

#define SAVEABLE(pri) do { \
    if (need_save) { cmdline_save_param(p, value, pri); return ret; } \
} while (0)

/*
 * Similar interface to seat_get_userpass_input(), except that here a
 * -1 return means that we aren't capable of processing the prompt and
 * someone else should do it.
 */
int cmdline_get_passwd_input(prompts_t *p)
{
    static bool tried_once = false;

    /*
     * We only handle prompts which don't echo (which we assume to be
     * passwords), and (currently) we only cope with a password prompt
     * that comes in a prompt-set on its own.
     */
    if (!cmdline_password || p->n_prompts != 1 || p->prompts[0]->echo) {
	return -1;
    }

    /*
     * If we've tried once, return utter failure (no more passwords left
     * to try).
     */
    if (tried_once)
	return 0;

    prompt_set_result(p->prompts[0], cmdline_password);
    smemclr(cmdline_password, strlen(cmdline_password));
    sfree(cmdline_password);
    cmdline_password = NULL;
    tried_once = true;
    return 1;
}

static bool cmdline_check_unavailable(int flag, const char *p)
{
    if (cmdline_tooltype & flag) {
	cmdline_error("option \"%s\" not available in this tool", p);
	return true;
    }
    return false;
}

#define UNAVAILABLE_IN(flag) do { \
    if (cmdline_check_unavailable(flag, p)) return ret; \
} while (0)

/*
 * Process a standard command-line parameter. `p' is the parameter
 * in question; `value' is the subsequent element of argv, which
 * may or may not be required as an operand to the parameter.
 * If `need_save' is 1, arguments which need to be saved as
 * described at this top of this file are, for later execution;
 * if 0, they are processed normally. (-1 is a special value used
 * by pterm to count arguments for a preliminary pass through the
 * argument list; it causes immediate return with an appropriate
 * value with no action taken.)
 * Return value is 2 if both arguments were used; 1 if only p was
 * used; 0 if the parameter wasn't one we recognised; -2 if it
 * should have been 2 but value was NULL.
 */

#define RETURN(x) do { \
    if ((x) == 2 && !value) return -2; \
    ret = x; \
    if (need_save < 0) return x; \
} while (0)

static bool seen_hostname_argument = false;
static bool seen_port_argument = false;
static bool seen_verbose_option = false;
static bool loaded_session = false;
bool cmdline_verbose(void) { return seen_verbose_option; }
bool cmdline_seat_verbose(Seat *seat) { return cmdline_verbose(); }
bool cmdline_lp_verbose(LogPolicy *lp) { return cmdline_verbose(); }
bool cmdline_loaded_session(void) { return loaded_session; }

static void set_protocol(Conf *conf, int protocol)
{
    settings_set_default_protocol(protocol);
    conf_set_int(conf, CONF_protocol, protocol);
}

static void set_port(Conf *conf, int port)
{
    settings_set_default_port(port);
    conf_set_int(conf, CONF_port, port);
}

int cmdline_process_param(const char *p, char *value,
                          int need_save, Conf *conf)
{
    int ret = 0;
    if (p[0] != '-') {
        if (need_save < 0)
            return 0;

        /*
         * Common handling for the tools whose initial command-line
         * arguments specify a hostname to connect to, i.e. PuTTY and
         * Plink. Doesn't count the file transfer tools, because their
         * hostname specification appears as part of a more
         * complicated scheme.
         */
        if ((cmdline_tooltype & TOOLTYPE_HOST_ARG) &&
            !seen_hostname_argument &&
            (!(cmdline_tooltype & TOOLTYPE_HOST_ARG_FROM_LAUNCHABLE_LOAD) ||
             !loaded_session || !conf_launchable(conf))) {
            /*
             * Treat this argument as a host name, if we have not yet
             * seen a host name argument or -load.
             *
             * Exception, in some tools (Plink): if we have seen -load
             * but it didn't create a launchable session, then we
             * still accept a hostname argument following that -load.
             * This allows you to make saved sessions that configure
             * lots of other stuff (colour schemes, terminal settings
             * etc) and then say 'putty -load sessionname hostname'.
             *
             * Also, we carefully _don't_ test conf for launchability
             * if we haven't been explicitly told to load a session
             * (otherwise saving a host name into Default Settings
             * would cause 'putty' on its own to immediately launch
             * the default session and never be able to do anything
             * else).
             */

            if (!strncmp(p, "telnet:", 7)) {
                /*
                 * If the argument starts with "telnet:", set the
                 * protocol to Telnet and process the string as a
                 * Telnet URL.
                 */

                /*
                 * Skip the "telnet:" or "telnet://" prefix.
                 */
                p += 7;
                if (p[0] == '/' && p[1] == '/')
                    p += 2;
                conf_set_int(conf, CONF_protocol, PROT_TELNET);

                /*
                 * The next thing we expect is a host name.
                 */
                {
                    const char *host = p;
                    char *buf;

                    p += host_strcspn(p, ":/");
                    buf = dupprintf("%.*s", (int)(p - host), host);
                    conf_set_str(conf, CONF_host, buf);
                    sfree(buf);
                    seen_hostname_argument = true;
                }

                /*
                 * If the host name is followed by a colon, then
                 * expect a port number after it.
                 */
                if (*p == ':') {
                    p++;

                    conf_set_int(conf, CONF_port, atoi(p));
                    /*
                     * Set the flag that will stop us from treating
                     * the next argument as a separate port; this one
                     * counts as explicitly provided.
                     */
                    seen_port_argument = true;
                } else {
                    conf_set_int(conf, CONF_port, -1);
                }
#ifdef MOD_PERSO
	    } else if (!strncmp(p, "ssh:", 4)) {
			char *pst = (char*)malloc(strlen(p)+1) ; strcpy(pst,p);
			char *q = (char*)pst ;
			/*
			* If the hostname starts with "ssh:",
			* set the protocol to SSH and process
			* the string as a SSH URL
			*/
			q += 4;
			if (q[0] == '/' && q[1] == '/')
			q += 2;
			//while (*q && *q != ':' && *q != '/') q++ ;
			conf_set_int( conf, CONF_protocol, PROT_SSH); 
			conf_set_int( conf, CONF_port, 22);
			ManageConnectString( conf, q ) ;

			conf_set_str( conf, CONF_host, q ) ;
			seen_hostname_argument = true ;
			free(pst);
	    } else if (!strncmp(p, "putty:", 4)) {
				char * q = (char*)p ;
				int ret = 0;
				q += 6;
				if (q[0] == '/' && q[1] == '/') q += 2;
				if (q[strlen(q) - 1] == '/') q[strlen(q) - 1] = '\0';
				p = q;
				ret = cmdline_process_param("-load", (char*)p, 1, conf);
				assert(ret == 2);
#endif
            } else {
                char *user = NULL, *hostname = NULL;
                const char *hostname_after_user;
                int port_override = -1;
                size_t len;

                /*
                 * Otherwise, treat it as a bare host name.
                 */

                if (cmdline_tooltype & TOOLTYPE_HOST_ARG_PROTOCOL_PREFIX) {
                    /*
                     * Here Plink checks for a comma-separated
                     * protocol prefix, e.g. 'ssh,hostname' or
                     * 'ssh,user@hostname'.
                     *
                     * I'm not entirely sure why; this behaviour dates
                     * from 2000 and isn't explained. But I _think_ it
                     * has to do with CVS transport or similar use
                     * cases, in which the end user invokes the SSH
                     * client indirectly, via some means that only
                     * lets them pass a single string argument, and it
                     * was occasionally useful to shoehorn the choice
                     * of protocol into that argument.
                     */
                    const char *comma = strchr(p, ',');
                    if (comma) {
                        char *prefix = dupprintf("%.*s", (int)(comma - p), p);
                        const struct BackendVtable *vt =
                            backend_vt_from_name(prefix);

                        if (vt) {
                            set_protocol(conf, vt->protocol);
                            port_override = vt->default_port;
                        } else {
                            cmdline_error("unrecognised protocol prefix '%s'",
                                          prefix);
                        }

                        sfree(prefix);
                        p = comma + 1;
                    }
                }

                hostname_after_user = p;
                if (cmdline_tooltype & TOOLTYPE_HOST_ARG_CAN_BE_SESSION) {
                    /*
                     * If the hostname argument can also be a saved
                     * session (see below), then here we also check
                     * for a user@ prefix, which will override the
                     * username from the saved session.
                     *
                     * (If the hostname argument _isn't_ a saved
                     * session, we don't do this.)
                     */
                    const char *at = strrchr(p, '@');
                    if (at) {
                        user = dupprintf("%.*s", (int)(at - p), p);
                        hostname_after_user = at + 1;
                    }
                }

                /*
                 * Write the whole hostname argument (minus only that
                 * optional protocol prefix) into the existing Conf,
                 * for tools that don't treat it as a saved session
                 * and as a fallback for those that do.
                 */
                hostname = dupstr(p + strspn(p, " \t"));
                len = strlen(hostname);
                while (len > 0 && (hostname[len-1] == ' ' ||
                                   hostname[len-1] == '\t'))
                    hostname[--len] = '\0';
                seen_hostname_argument = true;
#ifdef MOD_PERSO
		// Manage connect string user:pass@hostname
		ManageConnectString( conf, hostname ) ;
#endif
                conf_set_str(conf, CONF_host, hostname);

                if ((cmdline_tooltype & TOOLTYPE_HOST_ARG_CAN_BE_SESSION) &&
                    !loaded_session) {
                    /*
                     * For some tools, we equivocate between a
		     * hostname argument and an argument naming a
		     * saved session. Here we attempt to load a
		     * session with the specified name, and if that
		     * session exists and is launchable, we overwrite
		     * the entire Conf with it.
                     *
                     * We skip this check if a -load option has
                     * already happened, so that
                     *
                     *   plink -load non-launchable-session hostname
                     *
                     * will treat 'hostname' as a hostname _even_ if a
                     * saved session called 'hostname' exists. (This
                     * doesn't lose any functionality someone could
                     * have needed, because if 'hostname' did cause a
                     * session to be loaded, then it would overwrite
                     * everything from the previously loaded session.
                     * So if that was the behaviour someone wanted,
                     * then they could get it by leaving off the
                     * -load completely.)
		     */
                    Conf *conf2 = conf_new();
                    if (do_defaults(hostname_after_user, conf2) &&
                        conf_launchable(conf2)) {
                        conf_copy_into(conf, conf2);
                        loaded_session = true;
                        /* And override the username if one was given. */
                        if (user)
                            conf_set_str(conf, CONF_username, user);
                    }
                    conf_free(conf2);
                }

                sfree(hostname);
                sfree(user);

                if (port_override >= 0)
                    conf_set_int(conf, CONF_port, port_override);
            }

            return 1;
        } else if ((cmdline_tooltype & TOOLTYPE_PORT_ARG) &&
                   !seen_port_argument) {
            /*
             * If we've already got a host name from the command line
             * (either as a hostname argument or a qualifying -load),
             * but not a port number, then treat the next argument as
             * a port number.
             *
             * We handle this by calling ourself recursively to
             * pretend we received a -P argument, so that it will be
             * deferred until it's a good moment to run it.
             */
            char *dup = dupstr(p);     /* 'value' is not a const char * */
            int retd = cmdline_process_param("-P", dup, 1, conf);
            sfree(dup);
            assert(retd == 2);
            seen_port_argument = true;
            return 1;
        } else {
            /*
             * Refuse to recognise this argument, and give it back to
             * the tool's own command-line processing.
             */
            return 0;
        }
    }

    if (!strcmp(p, "-load")) {
	RETURN(2);
	/* This parameter must be processed immediately rather than being
	 * saved. */
#if MOD_PERSO
	if( IniFileFlag == SAVEMODE_REG ) {
	    do_defaults(value, conf);
	    loaded_session = true;
	} else {
	    char *pst = strstr( value, "/" ) ;
	    if( pst==NULL ) {
		do_defaults(value, conf);
		loaded_session = true;
	    } else {
		char *name = (char*)malloc( strlen(value)+1 ) ;
		strcpy(name,value) ;
	        pst = strstr( name, "/" ) ;
	        pst[0]='\0';
	        conf_set_str(conf,CONF_folder,name) ;
		SetSessPath( name ) ;
		strcpy( CurrentFolder, name ) ;
		do_defaults(pst+1, conf);
		loaded_session = true;
		free(value) ;
	    }
        }
#else
	do_defaults(value, conf);
	loaded_session = true;
#endif
	return 2;
    }

#ifdef MOD_PERSO
    if (!strcmp(p, "-kload")) {
	RETURN(2);
	/* This parameter must be processed immediately rather than being
	 * saved. */
	load_open_settings_forced( value, conf ) ;
	loaded_session = true;
	//cmdline_session_name = dupstr(value);
	return 2;
    }
    if (!strcmp(p, "-knock")) {
	RETURN(2);
	conf_set_str( conf, CONF_portknockingoptions, value ) ;
    }
    if ( !strcmp(p, "-auto_store_sshkey") || !strcmp(p, "-auto-store-sshkey") ) {
	RETURN(1);
	SetAutoStoreSSHKeyFlag( 1 ) ;
    }
#endif
#ifdef MOD_ADB
    if (!strcmp(p, "-adb") && GetADBFlag()) {
        RETURN(1);
        UNAVAILABLE_IN(TOOLTYPE_FILETRANSFER | TOOLTYPE_NONNETWORK);
        SAVEABLE(0);
        //default_protocol = PROT_ADB;
        conf_set_int(conf, CONF_protocol, PROT_ADB);
    }    
#endif
    for (size_t i = 0; backends[i]; i++) {
        if (p[0] == '-' && !strcmp(p+1, backends[i]->id)) {
            RETURN(1);
            UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
            SAVEABLE(0);
            set_protocol(conf, backends[i]->protocol);
            if (backends[i]->default_port)
                set_port(conf, backends[i]->default_port);
            if (backends[i]->protocol == PROT_SERIAL) {
                /* Special handling: the 'where to connect to' argument will
                 * have been placed into CONF_host, but for this protocol, it
                 * needs to be in CONF_serline */
                conf_set_str(conf, CONF_serline,
                             conf_get_str(conf, CONF_host));
            }
            return 1;
        }
    }
    if (!strcmp(p, "-v")) {
	RETURN(1);
        UNAVAILABLE_IN(TOOLTYPE_NO_VERBOSE_OPTION);
        seen_verbose_option = true;
    }
    if (!strcmp(p, "-l")) {
	RETURN(2);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_str(conf, CONF_username, value);
    }
    if (!strcmp(p, "-loghost")) {
	RETURN(2);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_str(conf, CONF_loghost, value);
    }
    if (!strcmp(p, "-hostkey")) {
        char *dup;
	RETURN(2);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
        dup = dupstr(value);
        if (!validate_manual_hostkey(dup)) {
            cmdline_error("'%s' is not a valid format for a manual host "
                          "key specification", value);
            sfree(dup);
            return ret;
        }
	conf_set_str_str(conf, CONF_ssh_manual_hostkeys, dup, "");
        sfree(dup);
    }
    if ((!strcmp(p, "-L") || !strcmp(p, "-R") || !strcmp(p, "-D"))) {
	char type, *q, *qq, *key, *val;
	RETURN(2);
	UNAVAILABLE_IN(TOOLTYPE_FILETRANSFER | TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	if (strcmp(p, "-D")) {
	    /*
             * For -L or -R forwarding types:
             *
	     * We expect _at least_ two colons in this string. The
	     * possible formats are `sourceport:desthost:destport',
	     * or `sourceip:sourceport:desthost:destport' if you're
	     * specifying a particular loopback address. We need to
	     * replace the one between source and dest with a \t;
	     * this means we must find the second-to-last colon in
	     * the string.
	     *
	     * (This looks like a foolish way of doing it given the
	     * existence of strrchr, but it's more efficient than
	     * two strrchrs - not to mention that the second strrchr
	     * would require us to modify the input string!)
	     */

            type = p[1];               /* 'L' or 'R' */

	    q = qq = host_strchr(value, ':');
	    while (qq) {
		char *qqq = host_strchr(qq+1, ':');
		if (qqq)
		    q = qq;
		qq = qqq;
	    }

	    if (!q) {
		cmdline_error("-%c expects at least two colons in its"
			      " argument", type);
		return ret;
	    }

	    key = dupprintf("%c%.*s", type, (int)(q - value), value);
	    val = dupstr(q+1);
	} else {
            /*
             * Dynamic port forwardings are entered under the same key
             * as if they were local (because they occupy the same
             * port space - a local and a dynamic forwarding on the
             * same local port are mutually exclusive), with the
             * special value "D" (which can be distinguished from
             * anything in the ordinary -L case by containing no
             * colon).
             */
	    key = dupprintf("L%s", value);
	    val = dupstr("D");
	}
	conf_set_str_str(conf, CONF_portfwd, key, val);
	sfree(key);
	sfree(val);
    }
    if ((!strcmp(p, "-nc"))) {
	char *host, *portp;

	RETURN(2);
	UNAVAILABLE_IN(TOOLTYPE_FILETRANSFER | TOOLTYPE_NONNETWORK);
	SAVEABLE(0);

	portp = host_strchr(value, ':');
	if (!portp) {
	    cmdline_error("-nc expects argument of form 'host:port'");
	    return ret;
	}

	host = dupprintf("%.*s", (int)(portp - value), value);
	conf_set_str(conf, CONF_ssh_nc_host, host);
	conf_set_int(conf, CONF_ssh_nc_port, atoi(portp + 1));
        sfree(host);
    }
    if (!strcmp(p, "-m")) {
	const char *filename;
	FILE *fp;

	RETURN(2);
	UNAVAILABLE_IN(TOOLTYPE_FILETRANSFER | TOOLTYPE_NONNETWORK);
	SAVEABLE(0);

	filename = value;

	fp = fopen(filename, "r");
	if (!fp) {
	    cmdline_error("unable to open command file \"%s\"", filename);
	    return ret;
	}
        strbuf *command = strbuf_new();
        char readbuf[4096];
	while (1) {
            size_t nread = fread(readbuf, 1, sizeof(readbuf), fp);
            if (nread == 0)
                break;
            put_data(command, readbuf, nread);
	}
	fclose(fp);
	conf_set_str(conf, CONF_remote_cmd, command->s);
	conf_set_str(conf, CONF_remote_cmd2, "");
	conf_set_bool(conf, CONF_nopty, true);   /* command => no terminal */
	strbuf_free(command);
    }
    if (!strcmp(p, "-P")) {
	RETURN(2);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
        SAVEABLE(1);            /* lower priority than -ssh, -telnet, etc */
	conf_set_int(conf, CONF_port, atoi(value));
    }
    if (!strcmp(p, "-pw")) {
	RETURN(2);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(1);
	/* We delay evaluating this until after the protocol is decided,
	 * so that we can warn if it's of no use with the selected protocol */
	if (conf_get_int(conf, CONF_protocol) != PROT_SSH)
	    cmdline_error("the -pw option can only be used with the "
			  "SSH protocol");
	else {
	    cmdline_password = dupstr(value);
	    /* Assuming that `value' is directly from argv, make a good faith
	     * attempt to trample it, to stop it showing up in `ps' output
	     * on Unix-like systems. Not guaranteed, of course. */
	    smemclr(value, strlen(value));
	}
    }

    if (!strcmp(p, "-agent") || !strcmp(p, "-pagent") ||
	!strcmp(p, "-pageant")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_bool(conf, CONF_tryagent, true);
    }
    if (!strcmp(p, "-noagent") || !strcmp(p, "-nopagent") ||
	!strcmp(p, "-nopageant")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_bool(conf, CONF_tryagent, false);
    }
    if (!strcmp(p, "-no-trivial-auth")) {
        RETURN(1);
        UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
        SAVEABLE(0);
        conf_set_bool(conf, CONF_ssh_no_trivial_userauth, true);
    }

    if (!strcmp(p, "-share")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_bool(conf, CONF_ssh_connection_sharing, true);
    }
    if (!strcmp(p, "-noshare")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_bool(conf, CONF_ssh_connection_sharing, false);
    }
    if (!strcmp(p, "-A")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_FILETRANSFER | TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_bool(conf, CONF_agentfwd, true);
    }
    if (!strcmp(p, "-a")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_FILETRANSFER | TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_bool(conf, CONF_agentfwd, false);
    }

    if (!strcmp(p, "-X")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_FILETRANSFER | TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_bool(conf, CONF_x11_forward, true);
    }
    if (!strcmp(p, "-x")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_FILETRANSFER | TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_bool(conf, CONF_x11_forward, false);
    }

    if (!strcmp(p, "-t")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_FILETRANSFER | TOOLTYPE_NONNETWORK);
	SAVEABLE(1);	/* lower priority than -m */
	conf_set_bool(conf, CONF_nopty, false);
    }
    if (!strcmp(p, "-T")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_FILETRANSFER | TOOLTYPE_NONNETWORK);
	SAVEABLE(1);
	conf_set_bool(conf, CONF_nopty, true);
    }

    if (!strcmp(p, "-N")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_FILETRANSFER | TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_bool(conf, CONF_ssh_no_shell, true);
    }

    if (!strcmp(p, "-C")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_bool(conf, CONF_compression, true);
    }

    if (!strcmp(p, "-1")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_int(conf, CONF_sshprot, 0);   /* ssh protocol 1 only */
    }
    if (!strcmp(p, "-2")) {
	RETURN(1);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	conf_set_int(conf, CONF_sshprot, 3);   /* ssh protocol 2 only */
    }

    if (!strcmp(p, "-i")) {
	Filename *fn;
	RETURN(2);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	fn = filename_from_str(value);
	conf_set_filename(conf, CONF_keyfile, fn);
        filename_free(fn);
    }

    if (!strcmp(p, "-4") || !strcmp(p, "-ipv4")) {
	RETURN(1);
	SAVEABLE(1);
	conf_set_int(conf, CONF_addressfamily, ADDRTYPE_IPV4);
    }
    if (!strcmp(p, "-6") || !strcmp(p, "-ipv6")) {
	RETURN(1);
	SAVEABLE(1);
	conf_set_int(conf, CONF_addressfamily, ADDRTYPE_IPV6);
    }
    if (!strcmp(p, "-sercfg")) {
	char* nextitem;
	RETURN(2);
	UNAVAILABLE_IN(TOOLTYPE_FILETRANSFER | TOOLTYPE_NONNETWORK);
	SAVEABLE(1);
	if (conf_get_int(conf, CONF_protocol) != PROT_SERIAL)
	    cmdline_error("the -sercfg option can only be used with the "
			  "serial protocol");
	/* Value[0] contains one or more , separated values, like 19200,8,n,1,X */
	nextitem = value;
	while (nextitem[0] != '\0') {
	    int length, skip;
	    char *end = strchr(nextitem, ',');
	    if (!end) {
		length = strlen(nextitem);
		skip = 0;
	    } else {
		length = end - nextitem;
		nextitem[length] = '\0';
		skip = 1;
	    }
	    if (length == 1) {
		switch (*nextitem) {
		  case '1':
		  case '2':
		    conf_set_int(conf, CONF_serstopbits, 2 * (*nextitem-'0'));
		    break;

		  case '5':
		  case '6':
		  case '7':
		  case '8':
		  case '9':
		    conf_set_int(conf, CONF_serdatabits, *nextitem-'0');
		    break;

		  case 'n':
		    conf_set_int(conf, CONF_serparity, SER_PAR_NONE);
		    break;
		  case 'o':
		    conf_set_int(conf, CONF_serparity, SER_PAR_ODD);
		    break;
		  case 'e':
		    conf_set_int(conf, CONF_serparity, SER_PAR_EVEN);
		    break;
		  case 'm':
		    conf_set_int(conf, CONF_serparity, SER_PAR_MARK);
		    break;
		  case 's':
		    conf_set_int(conf, CONF_serparity, SER_PAR_SPACE);
		    break;

		  case 'N':
		    conf_set_int(conf, CONF_serflow, SER_FLOW_NONE);
		    break;
		  case 'X':
		    conf_set_int(conf, CONF_serflow, SER_FLOW_XONXOFF);
		    break;
		  case 'R':
		    conf_set_int(conf, CONF_serflow, SER_FLOW_RTSCTS);
		    break;
		  case 'D':
		    conf_set_int(conf, CONF_serflow, SER_FLOW_DSRDTR);
		    break;

		  default:
		    cmdline_error("Unrecognised suboption \"-sercfg %c\"",
				  *nextitem);
		}
	    } else if (length == 3 && !strncmp(nextitem,"1.5",3)) {
		/* Messy special case */
		conf_set_int(conf, CONF_serstopbits, 3);
	    } else {
		int serspeed = atoi(nextitem);
		if (serspeed != 0) {
		    conf_set_int(conf, CONF_serspeed, serspeed);
		} else {
		    cmdline_error("Unrecognised suboption \"-sercfg %s\"",
				  nextitem);
		}
	    }
	    nextitem += length + skip;
	}
    }

    if (!strcmp(p, "-sessionlog")) {
	Filename *fn;
	RETURN(2);
	UNAVAILABLE_IN(TOOLTYPE_FILETRANSFER);
	/* but available even in TOOLTYPE_NONNETWORK, cf pterm "-log" */
	SAVEABLE(0);
	fn = filename_from_str(value);
	conf_set_filename(conf, CONF_logfilename, fn);
	conf_set_int(conf, CONF_logtype, LGTYP_DEBUG);
        filename_free(fn);
    }

    if (!strcmp(p, "-sshlog") ||
        !strcmp(p, "-sshrawlog")) {
	Filename *fn;
	RETURN(2);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
	fn = filename_from_str(value);
	conf_set_filename(conf, CONF_logfilename, fn);
	conf_set_int(conf, CONF_logtype,
                     !strcmp(p, "-sshlog") ? LGTYP_PACKETS :
                     /* !strcmp(p, "-sshrawlog") ? */ LGTYP_SSHRAW);
        filename_free(fn);
    }

    if (!strcmp(p, "-logoverwrite")) {
        RETURN(1);
        UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
        SAVEABLE(0);
        conf_set_int(conf, CONF_logxfovr, LGXF_OVR);
    }

    if (!strcmp(p, "-logappend")) {
        RETURN(1);
        UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
        SAVEABLE(0);
        conf_set_int(conf, CONF_logxfovr, LGXF_APN);
    }

    if (!strcmp(p, "-proxycmd")) {
	RETURN(2);
	UNAVAILABLE_IN(TOOLTYPE_NONNETWORK);
	SAVEABLE(0);
        conf_set_int(conf, CONF_proxy_type, PROXY_CMD);
	conf_set_str(conf, CONF_proxy_telnet_command, value);
    }

#ifdef _WINDOWS
    /*
     * Cross-tool options only available on Windows.
     */
    if (!strcmp(p, "-restrict-acl") || !strcmp(p, "-restrict_acl") ||
        !strcmp(p, "-restrictacl")) {
	RETURN(1);
        restrict_process_acl();
    }
#endif

    return ret;			       /* unrecognised */
}

void cmdline_run_saved(Conf *conf)
{
    for (size_t pri = 0; pri < NPRIORITIES; pri++) {
        for (size_t i = 0; i < saves[pri].nsaved; i++) {
	    cmdline_process_param(saves[pri].params[i].p,
				  saves[pri].params[i].value, 0, conf);
            sfree(saves[pri].params[i].p);
            sfree(saves[pri].params[i].value);
        }
        saves[pri].nsaved = 0;
    }
}

bool cmdline_host_ok(Conf *conf)
{
    /*
     * Return true if the command-line arguments we've processed in
     * TOOLTYPE_HOST_ARG mode are sufficient to justify launching a
     * session.
     */
    assert(cmdline_tooltype & TOOLTYPE_HOST_ARG);

    /*
     * Of course, if we _can't_ launch a session, the answer is
     * clearly no.
     */
    if (!conf_launchable(conf))
        return false;

    /*
     * But also, if we haven't seen either a -load option or a
     * hostname argument, i.e. the only saved settings we've loaded
     * are Default Settings plus any non-hostname-based stuff from the
     * command line, then the answer is still no, _even_ if this Conf
     * is launchable. Otherwise, if you saved your favourite hostname
     * into Default Settings, then just running 'putty' without
     * arguments would connect to it without ever offering you the
     * option to connect to something else or change the setting.
     */
    if (!seen_hostname_argument && !loaded_session)
        return false;

    return true;
}
