/*
 * Simple Telnet server code, adapted from PuTTY's own Telnet
 * client code for use as a Cygwin local pty proxy.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sel.h"
#include "telnet.h"
#include "malloc.h"
#include "pty.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define	IAC	255		       /* interpret as command: */
#define	DONT	254		       /* you are not to use option */
#define	DO	253		       /* please, you use option */
#define	WONT	252		       /* I won't use option */
#define	WILL	251		       /* I will use option */
#define	SB	250		       /* interpret as subnegotiation */
#define	SE	240		       /* end sub negotiation */

#define GA      249		       /* you may reverse the line */
#define EL      248		       /* erase the current line */
#define EC      247		       /* erase the current character */
#define	AYT	246		       /* are you there */
#define	AO	245		       /* abort output--but let prog finish */
#define	IP	244		       /* interrupt process--permanently */
#define	BREAK	243		       /* break */
#define DM      242		       /* data mark--for connect. cleaning */
#define NOP     241		       /* nop */
#define EOR     239		       /* end of record (transparent mode) */
#define ABORT   238		       /* Abort process */
#define SUSP    237		       /* Suspend process */
#define xEOF    236		       /* End of file: EOF is already used... */

#define TELOPTS(X) \
    X(BINARY, 0)                       /* 8-bit data path */ \
    X(ECHO, 1)                         /* echo */ \
    X(RCP, 2)                          /* prepare to reconnect */ \
    X(SGA, 3)                          /* suppress go ahead */ \
    X(NAMS, 4)                         /* approximate message size */ \
    X(STATUS, 5)                       /* give status */ \
    X(TM, 6)                           /* timing mark */ \
    X(RCTE, 7)                         /* remote controlled transmission and echo */ \
    X(NAOL, 8)                         /* negotiate about output line width */ \
    X(NAOP, 9)                         /* negotiate about output page size */ \
    X(NAOCRD, 10)                      /* negotiate about CR disposition */ \
    X(NAOHTS, 11)                      /* negotiate about horizontal tabstops */ \
    X(NAOHTD, 12)                      /* negotiate about horizontal tab disposition */ \
    X(NAOFFD, 13)                      /* negotiate about formfeed disposition */ \
    X(NAOVTS, 14)                      /* negotiate about vertical tab stops */ \
    X(NAOVTD, 15)                      /* negotiate about vertical tab disposition */ \
    X(NAOLFD, 16)                      /* negotiate about output LF disposition */ \
    X(XASCII, 17)                      /* extended ascic character set */ \
    X(LOGOUT, 18)                      /* force logout */ \
    X(BM, 19)                          /* byte macro */ \
    X(DET, 20)                         /* data entry terminal */ \
    X(SUPDUP, 21)                      /* supdup protocol */ \
    X(SUPDUPOUTPUT, 22)                /* supdup output */ \
    X(SNDLOC, 23)                      /* send location */ \
    X(TTYPE, 24)                       /* terminal type */ \
    X(EOR, 25)                         /* end or record */ \
    X(TUID, 26)                        /* TACACS user identification */ \
    X(OUTMRK, 27)                      /* output marking */ \
    X(TTYLOC, 28)                      /* terminal location number */ \
    X(3270REGIME, 29)                  /* 3270 regime */ \
    X(X3PAD, 30)                       /* X.3 PAD */ \
    X(NAWS, 31)                        /* window size */ \
    X(TSPEED, 32)                      /* terminal speed */ \
    X(LFLOW, 33)                       /* remote flow control */ \
    X(LINEMODE, 34)                    /* Linemode option */ \
    X(XDISPLOC, 35)                    /* X Display Location */ \
    X(OLD_ENVIRON, 36)                 /* Old - Environment variables */ \
    X(AUTHENTICATION, 37)              /* Authenticate */ \
    X(ENCRYPT, 38)                     /* Encryption option */ \
    X(NEW_ENVIRON, 39)                 /* New - Environment variables */ \
    X(TN3270E, 40)                     /* TN3270 enhancements */ \
    X(XAUTH, 41)                       \
    X(CHARSET, 42)                     /* Character set */ \
    X(RSP, 43)                         /* Remote serial port */ \
    X(COM_PORT_OPTION, 44)             /* Com port control */ \
    X(SLE, 45)                         /* Suppress local echo */ \
    X(STARTTLS, 46)                    /* Start TLS */ \
    X(KERMIT, 47)                      /* Automatic Kermit file transfer */ \
    X(SEND_URL, 48)                    \
    X(FORWARD_X, 49)                   \
    X(PRAGMA_LOGON, 138)               \
    X(SSPI_LOGON, 139)                 \
    X(PRAGMA_HEARTBEAT, 140)           \
    X(EXOPL, 255)                      /* extended-options-list */

#define telnet_enum(x,y) TELOPT_##x = y,
enum { TELOPTS(telnet_enum) dummy=0 };
#undef telnet_enum

#define	TELQUAL_IS	0	       /* option is... */
#define	TELQUAL_SEND	1	       /* send option */
#define	TELQUAL_INFO	2	       /* ENVIRON: informational version of IS */
#define BSD_VAR 1
#define BSD_VALUE 0
#define RFC_VAR 0
#define RFC_VALUE 1

#define CR 13
#define LF 10
#define NUL 0

#define iswritable(x) ( (x) != IAC && (x) != CR )

static char *telopt(int opt)
{
#define telnet_str(x,y) case TELOPT_##x: return #x;
    switch (opt) {
	TELOPTS(telnet_str)
      default:
	return "<unknown>";
    }
#undef telnet_str
}

static void telnet_size(void *handle, int width, int height);

struct Opt {
    int send;			       /* what we initially send */
    int nsend;			       /* -ve send if requested to stop it */
    int ack, nak;		       /* +ve and -ve acknowledgements */
    int option;			       /* the option code */
    int index;			       /* index into telnet->opt_states[] */
    enum {
	REQUESTED, ACTIVE, INACTIVE, REALLY_INACTIVE
    } initial_state;
};

enum {
    OPTINDEX_NAWS,
    OPTINDEX_TSPEED,
    OPTINDEX_TTYPE,
    OPTINDEX_OENV,
    OPTINDEX_NENV,
    OPTINDEX_ECHO,
    OPTINDEX_WE_SGA,
    OPTINDEX_THEY_SGA,
    OPTINDEX_WE_BIN,
    OPTINDEX_THEY_BIN,
    NUM_OPTS
};

static const struct Opt o_naws =
    { DO, DONT, WILL, WONT, TELOPT_NAWS, OPTINDEX_NAWS, REQUESTED };
static const struct Opt o_ttype =
    { DO, DONT, WILL, WONT, TELOPT_TTYPE, OPTINDEX_TTYPE, REQUESTED };
static const struct Opt o_oenv =
    { DO, DONT, WILL, WONT, TELOPT_OLD_ENVIRON, OPTINDEX_OENV, INACTIVE };
static const struct Opt o_nenv =
    { DO, DONT, WILL, WONT, TELOPT_NEW_ENVIRON, OPTINDEX_NENV, REQUESTED };
static const struct Opt o_echo =
    { WILL, WONT, DO, DONT, TELOPT_ECHO, OPTINDEX_ECHO, REQUESTED };
static const struct Opt o_they_sga =
    { DO, DONT, WILL, WONT, TELOPT_SGA, OPTINDEX_WE_SGA, REQUESTED };
static const struct Opt o_we_sga =
    { WILL, WONT, DO, DONT, TELOPT_SGA, OPTINDEX_THEY_SGA, REQUESTED };

static const struct Opt *const opts[] = {
    &o_echo, &o_we_sga, &o_they_sga, &o_naws, &o_ttype, &o_oenv, &o_nenv, NULL
};

struct telnet_tag {
    int opt_states[NUM_OPTS];

    int sb_opt, sb_len;
    unsigned char *sb_buf;
    int sb_size;

    enum {
	TOP_LEVEL, SEENIAC, SEENWILL, SEENWONT, SEENDO, SEENDONT,
	    SEENSB, SUBNEGOT, SUBNEG_IAC, SEENCR
    } state;

    sel_wfd *net, *pty;

    /*
     * Options we must finish processing before launching the shell
     */
    int old_environ_done, new_environ_done, ttype_done;

    /*
     * Ready to start shell?
     */
    int shell_ok;
    int envvarsize;
    struct shell_data shdata;
};

#define TELNET_MAX_BACKLOG 4096

#define SB_DELTA 1024

static void send_opt(Telnet telnet, int cmd, int option)
{
    unsigned char b[3];

    b[0] = IAC;
    b[1] = cmd;
    b[2] = option;
    sel_write(telnet->net, (char *)b, 3);
}

static void deactivate_option(Telnet telnet, const struct Opt *o)
{
    if (telnet->opt_states[o->index] == REQUESTED ||
	telnet->opt_states[o->index] == ACTIVE)
	send_opt(telnet, o->nsend, o->option);
    telnet->opt_states[o->index] = REALLY_INACTIVE;
}

/*
 * Generate side effects of enabling or disabling an option.
 */
static void option_side_effects(Telnet telnet, const struct Opt *o, int enabled)
{
}

static void activate_option(Telnet telnet, const struct Opt *o)
{
    if (o->option == TELOPT_NEW_ENVIRON ||
	o->option == TELOPT_OLD_ENVIRON ||
	o->option == TELOPT_TTYPE) {
	char buf[6];
	buf[0] = IAC;
	buf[1] = SB;
	buf[2] = o->option;
	buf[3] = TELQUAL_SEND;
	buf[4] = IAC;
	buf[5] = SE;
	sel_write(telnet->net, buf, 6);
    }
    option_side_effects(telnet, o, 1);
}

static void done_option(Telnet telnet, int option)
{
    if (option == TELOPT_OLD_ENVIRON)
	telnet->old_environ_done = 1;
    else if (option == TELOPT_NEW_ENVIRON)
	telnet->new_environ_done = 1;
    else if (option == TELOPT_TTYPE)
	telnet->ttype_done = 1;

    if (telnet->old_environ_done && telnet->new_environ_done &&
	telnet->ttype_done) {
	telnet->shell_ok = 1;
    }
}

static void refused_option(Telnet telnet, const struct Opt *o)
{
    done_option(telnet, o->option);
    if (o->send == WILL && o->option == TELOPT_NEW_ENVIRON &&
	telnet->opt_states[o_oenv.index] == INACTIVE) {
	send_opt(telnet, WILL, TELOPT_OLD_ENVIRON);
	telnet->opt_states[o_oenv.index] = REQUESTED;
	telnet->old_environ_done = 0;
    }
    option_side_effects(telnet, o, 0);
}

static void proc_rec_opt(Telnet telnet, int cmd, int option)
{
    const struct Opt *const *o;

    for (o = opts; *o; o++) {
	if ((*o)->option == option && (*o)->ack == cmd) {
	    switch (telnet->opt_states[(*o)->index]) {
	      case REQUESTED:
		telnet->opt_states[(*o)->index] = ACTIVE;
		activate_option(telnet, *o);
		break;
	      case ACTIVE:
		break;
	      case INACTIVE:
		telnet->opt_states[(*o)->index] = ACTIVE;
		send_opt(telnet, (*o)->send, option);
		activate_option(telnet, *o);
		break;
	      case REALLY_INACTIVE:
		send_opt(telnet, (*o)->nsend, option);
		break;
	    }
	    return;
	} else if ((*o)->option == option && (*o)->nak == cmd) {
	    switch (telnet->opt_states[(*o)->index]) {
	      case REQUESTED:
		telnet->opt_states[(*o)->index] = INACTIVE;
		refused_option(telnet, *o);
		break;
	      case ACTIVE:
		telnet->opt_states[(*o)->index] = INACTIVE;
		send_opt(telnet, (*o)->nsend, option);
		option_side_effects(telnet, *o, 0);
		break;
	      case INACTIVE:
	      case REALLY_INACTIVE:
		break;
	    }
	    return;
	}
    }
    /*
     * If we reach here, the option was one we weren't prepared to
     * cope with. If the request was positive (WILL or DO), we send
     * a negative ack to indicate refusal. If the request was
     * negative (WONT / DONT), we must do nothing.
     */
    if (cmd == WILL || cmd == DO)
        send_opt(telnet, (cmd == WILL ? DONT : WONT), option);
}

static void process_subneg(Telnet telnet)
{
    int var, value, n;

    switch (telnet->sb_opt) {
      case TELOPT_OLD_ENVIRON:
      case TELOPT_NEW_ENVIRON:
	if (telnet->sb_buf[0] == TELQUAL_IS) {
	    if (telnet->sb_opt == TELOPT_NEW_ENVIRON) {
		var = RFC_VAR;
		value = RFC_VALUE;
	    } else {
		if (telnet->sb_len > 1 && !(telnet->sb_buf[0] &~ 1)) {
		    var = telnet->sb_buf[0];
		    value = BSD_VAR ^ BSD_VALUE ^ var;
		} else {
		    var = BSD_VAR;
		    value = BSD_VALUE;
		}
	    }
	}
	n = 1;
	while (n < telnet->sb_len && telnet->sb_buf[n] == var) {
	    int varpos, varlen, valpos, vallen;
	    char *result;

	    varpos = ++n;
	    while (n < telnet->sb_len && telnet->sb_buf[n] != value)
		n++;
	    if (n == telnet->sb_len)
		break;
	    varlen = n - varpos;
	    valpos = ++n;
	    while (n < telnet->sb_len && telnet->sb_buf[n] != var)
		n++;
	    vallen = n - valpos;

	    result = snewn(varlen + vallen + 2, char);
	    sprintf(result, "%.*s=%.*s",
		    varlen, telnet->sb_buf+varpos,
		    vallen, telnet->sb_buf+valpos);
	    if (telnet->shdata.nenvvars >= telnet->envvarsize) {
		telnet->envvarsize = telnet->shdata.nenvvars * 3 / 2 + 16;
		telnet->shdata.envvars = sresize(telnet->shdata.envvars,
						 telnet->envvarsize, char *);
	    }
	    telnet->shdata.envvars[telnet->shdata.nenvvars++] = result;
	}
	done_option(telnet, telnet->sb_opt);
	break;
      case TELOPT_TTYPE:
	if (telnet->sb_len >= 1 && telnet->sb_buf[0] == TELQUAL_IS) {
	    telnet->shdata.termtype = snewn(5 + telnet->sb_len, char);
            strcpy(telnet->shdata.termtype, "TERM=");
            for (n = 0; n < telnet->sb_len-1; n++) {
                char c = telnet->sb_buf[n+1];
                if (c >= 'A' && c <= 'Z')
                    c = c + 'a' - 'A';
                telnet->shdata.termtype[n+5] = c;
            }
	    telnet->shdata.termtype[telnet->sb_len+5-1] = '\0';
	}
	done_option(telnet, telnet->sb_opt);
	break;
      case TELOPT_NAWS:
	if (telnet->sb_len == 4) {
	    int w, h;
	    w = (unsigned char)telnet->sb_buf[0];
	    w = (w << 8) | (unsigned char)telnet->sb_buf[1];
	    h = (unsigned char)telnet->sb_buf[2];
	    h = (h << 8) | (unsigned char)telnet->sb_buf[3];
	    pty_resize(w, h);
	}
	break;
    }
}

void telnet_from_net(Telnet telnet, char *buf, int len)
{
    while (len--) {
	int c = (unsigned char) *buf++;

	switch (telnet->state) {
	  case TOP_LEVEL:
	  case SEENCR:
	    /*
	     * PuTTY sends Telnet's new line sequence (CR LF on
	     * the wire) in response to the return key. We must
	     * therefore treat that as equivalent to CR NUL, and
	     * send CR to the pty.
	     */
	    if ((c == NUL || c == '\n') && telnet->state == SEENCR)
		telnet->state = TOP_LEVEL;
	    else if (c == IAC)
		telnet->state = SEENIAC;
	    else {
		char cc = c;
		sel_write(telnet->pty, &cc, 1);

		if (c == CR)
		    telnet->state = SEENCR;
		else
		    telnet->state = TOP_LEVEL;
	    }
	    break;
	  case SEENIAC:
	    if (c == DO)
		telnet->state = SEENDO;
	    else if (c == DONT)
		telnet->state = SEENDONT;
	    else if (c == WILL)
		telnet->state = SEENWILL;
	    else if (c == WONT)
		telnet->state = SEENWONT;
	    else if (c == SB)
		telnet->state = SEENSB;
	    else if (c == DM)
		telnet->state = TOP_LEVEL;
	    else {
		/* ignore everything else; print it if it's IAC */
		if (c == IAC) {
		    char cc = c;
		    sel_write(telnet->pty, &cc, 1);
		}
		telnet->state = TOP_LEVEL;
	    }
	    break;
	  case SEENWILL:
	    proc_rec_opt(telnet, WILL, c);
	    telnet->state = TOP_LEVEL;
	    break;
	  case SEENWONT:
	    proc_rec_opt(telnet, WONT, c);
	    telnet->state = TOP_LEVEL;
	    break;
	  case SEENDO:
	    proc_rec_opt(telnet, DO, c);
	    telnet->state = TOP_LEVEL;
	    break;
	  case SEENDONT:
	    proc_rec_opt(telnet, DONT, c);
	    telnet->state = TOP_LEVEL;
	    break;
	  case SEENSB:
	    telnet->sb_opt = c;
	    telnet->sb_len = 0;
	    telnet->state = SUBNEGOT;
	    break;
	  case SUBNEGOT:
	    if (c == IAC)
		telnet->state = SUBNEG_IAC;
	    else {
	      subneg_addchar:
		if (telnet->sb_len >= telnet->sb_size) {
		    telnet->sb_size += SB_DELTA;
		    telnet->sb_buf = sresize(telnet->sb_buf, telnet->sb_size,
					     unsigned char);
		}
		telnet->sb_buf[telnet->sb_len++] = c;
		telnet->state = SUBNEGOT;	/* in case we came here by goto */
	    }
	    break;
	  case SUBNEG_IAC:
	    if (c != SE)
		goto subneg_addchar;   /* yes, it's a hack, I know, but... */
	    else {
		process_subneg(telnet);
		telnet->state = TOP_LEVEL;
	    }
	    break;
	}
    }
}

Telnet telnet_new(sel_wfd *net, sel_wfd *pty)
{
    Telnet telnet;

    telnet = snew(struct telnet_tag);
    telnet->sb_buf = NULL;
    telnet->sb_size = 0;
    telnet->state = TOP_LEVEL;
    telnet->net = net;
    telnet->pty = pty;
    telnet->shdata.envvars = NULL;
    telnet->shdata.nenvvars = telnet->envvarsize = 0;
    telnet->shdata.termtype = NULL;

    /*
     * Initialise option states.
     */
    {
	const struct Opt *const *o;

	for (o = opts; *o; o++) {
	    telnet->opt_states[(*o)->index] = (*o)->initial_state;
	    if (telnet->opt_states[(*o)->index] == REQUESTED)
		send_opt(telnet, (*o)->send, (*o)->option);
	}
    }

    telnet->old_environ_done = 1;      /* initially don't want to bother */
    telnet->new_environ_done = 0;
    telnet->ttype_done = 0;
    telnet->shell_ok = 0;

    return telnet;
}

void telnet_free(Telnet telnet)
{
    sfree(telnet->sb_buf);
    sfree(telnet);
}

void telnet_from_pty(Telnet telnet, char *buf, int len)
{
    unsigned char *p, *end;
    static const unsigned char iac[2] = { IAC, IAC };
    static const unsigned char cr[2] = { CR, NUL };
#if 0
    static const unsigned char nl[2] = { CR, LF };
#endif

    p = (unsigned char *)buf;
    end = (unsigned char *)(buf + len);
    while (p < end) {
	unsigned char *q = p;

	while (p < end && iswritable(*p))
	    p++;
	sel_write(telnet->net, (char *)q, p - q);

	while (p < end && !iswritable(*p)) {
	    sel_write(telnet->net, (char *)(*p == IAC ? iac : cr), 2);
	    p++;
	}
    }
}

int telnet_shell_ok(Telnet telnet, struct shell_data *shdata)
{
    if (telnet->shell_ok)
	*shdata = telnet->shdata;      /* structure copy */
    return telnet->shell_ok;
}
