#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <assert.h>
#if __INTERIX
#include <rpc/types.h> /* for INADDR_LOOPBACK */
#else
#include <stdint.h>
#endif

#if __INTERIX
/* Under SFU, openpty(), and therefore forkpty(), require root privileges.
 * So instead, we write our own forkpty() which uses /dev/ptmx.  I have
 * no why the builtin openpty() cannot do this internally.
 */
#include "ptyfork.h"
#else
/* On Cygwin, forkpty() works without privileges */
#include <pty.h>
#define pty_fork forkpty
#endif

#include "cthelper.h"
#include "buffer.h"
#include "message.h"

#include "debug.h"

/* Buffer sizes */
enum {
  CTLBUF = 32,
  PTOBUF = 256,
  PTIBUF = 256,
};

static int
connect_cygterm(unsigned int port)
{
  int sock;
  struct sockaddr_in sa;

  DBUG_ENTER("connect_cygterm");
  if (0 <= (sock = socket(PF_INET, SOCK_STREAM, 0))) {
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    DBUG_PRINT("startup", ("connecting to cygterm on port %u", port));
    if (0 != connect(sock, (struct sockaddr *)&sa, sizeof(sa))) {
      close(sock);
      DBUG_PRINT("startup", ("connect failed: %s", strerror(errno)));
      DBUG_RETURN(-1);
    }
  }
  DBUG_RETURN(sock);
}

static int
resize(int pty, int h, int w)
{
  struct winsize ws;
  DBUG_ENTER("resize");
  if (0 != ioctl(pty, TIOCGWINSZ, (void *)&ws)) {
    DBUG_PRINT("msg", ("TIOCGWINSZ failed: %s", strerror(errno)));
    DBUG_RETURN(-1);
  }
  ws.ws_row = h;
  ws.ws_col = w;
  DBUG_PRINT("msg", ("resizing terminal: %d x %d", ws.ws_col, ws.ws_row));
  if (0 != ioctl(pty, TIOCSWINSZ, (void *)&ws))
    DBUG_PRINT("msg", ("TIOCSWINSZ failed: %s", strerror(errno)));
  DBUG_RETURN(0);
}

static int
parse_char(const char *desc)
{
  int ch;
  char *err;

  DBUG_ENTER("parse_char");
  if (desc[0] == '\0' || desc[0] == ':')
    ch = -1;
  else if (desc[1] == '\0' || desc[1] == ':')
    ch = desc[0];
  else if (desc[0] == '^') {
    ch = toupper(desc[1]);
    if ('?' <= ch && ch <= '_')
      ch ^= '@';
    else
      ch = -1;
  }
  else {
    ch = strtol(desc, &err, 0);
    if (!err || err == desc)
      ch = -1;
  }
  DBUG_RETURN(ch);
}

/* attr is a colon-separated sequence.  Each element of this sequence may be:
 *   size=width,height  to set the terminal size
 *   charname=char      to set a special character
 *   [-]flag            to enable or disable a terminal characteristic
 * Supported characters and characteristics are listed below.
 */
static void
init_pty(int pty, const char *attr)
{
  enum flag_type_t { T_SIZE, T_K, T_I, T_O, T_C, T_L };
  static const struct flag_s {
    const char *name;
    enum flag_type_t type;
    long v;
  } flag[] = {
    { "brkint",   T_I, BRKINT   }, /* SUSv2 */
#if _XOPEN_SOURCE >= 500
    { "bs0",      T_O, BS0      }, /* SUSv2 Ex */
    { "bs1",      T_O, BS1      }, /* SUSv2 Ex */
#endif
    { "clocal",   T_C, CLOCAL   }, /* SUSv2 */
#if _XOPEN_SOURCE >= 500
    { "cr0",      T_O, CR0      }, /* SUSv2 Ex */
    { "cr1",      T_O, CR1      }, /* SUSv2 Ex */
    { "cr2",      T_O, CR2      }, /* SUSv2 Ex */
    { "cr3",      T_O, CR3      }, /* SUSv2 Ex */
#endif
    { "cread",    T_C, CREAD    }, /* SUSv2 */
#if _XOPEN_SOURCE >= 600
    { "crterase", T_L, CRTERASE }, /* SUSv3 */
    { "crtkill",  T_L, CRTKILL  }, /* SUSv3 */
#endif
#if _XOPEN_SOURCE >= 600
    { "crtscts",  T_C, CRTSCTS  }, /* SUSv3 */
#endif
    { "cs5",      T_C, CS5      }, /* SUSv2 */
    { "cs6",      T_C, CS6      }, /* SUSv2 */
    { "cs7",      T_C, CS7      }, /* SUSv2 */
    { "cs8",      T_C, CS8      }, /* SUSv2 */
    { "cstopb",   T_C, CSTOPB   }, /* SUSv2 */
#if _XOPEN_SOURCE >= 600
    { "ctlecho",  T_L, CTLECHO  }, /* SUSv3 */
#endif
    { "echo",     T_L, ECHO     }, /* SUSv2 */
    { "echoctl",  T_L, ECHOCTL  }, /* SUSv3 */
    { "echoe",    T_L, ECHOE    }, /* SUSv2 */
    { "echok",    T_L, ECHOK    }, /* SUSv2 */
#if _XOPEN_SOURCE >= 600
    { "echoke",   T_L, ECHOKE   }, /* SUSv3 */
#endif
    { "echonl",   T_L, ECHONL   }, /* SUSv2 */
#if _XOPEN_SOURCE >= 600
    { "echoprt",  T_L, ECHOPRT  }, /* SUSv3 */
#endif
    { "eof",      T_K, VEOF     }, /* SUSv2 */
    { "eol",      T_K, VEOL     }, /* SUSv2 */
    { "erase",    T_K, VERASE   }, /* SUSv2 */
#if !__INTERIX
    { "ff0",      T_O, FF0      }, /* SUSv2 Ex */
    { "ff1",      T_O, FF1      }, /* SUSv2 Ex */
#endif
    { "hup",      T_C, HUPCL    }, /* SUSv2 */
    { "hupcl",    T_C, HUPCL    }, /* SUSv2 */
    { "icanon",   T_L, ICANON   }, /* SUSv2 */
    { "icrnl",    T_I, ICRNL    }, /* SUSv2 */
    { "iexten",   T_L, IEXTEN   }, /* SUSv2 */
    { "ignbrk",   T_I, IGNBRK   }, /* SUSv2 */
    { "igncr",    T_I, IGNCR    }, /* SUSv2 */
    { "ignpar",   T_I, IGNPAR   }, /* SUSv2 */
    { "imaxbel",  T_I, IMAXBEL  },
    { "inlcr",    T_I, INLCR    }, /* SUSv2 */
    { "inpck",    T_I, INPCK    },
    { "intr",     T_K, VINTR    }, /* SUSv2 */
    { "isig",     T_L, ISIG     }, /* SUSv2 */
    { "istrip",   T_I, ISTRIP   }, /* SUSv2 */
#if 0
    { "iuclc",    T_I, IUCLC    }, /* SUSv2 LEGACY */
#endif
    { "ixany",    T_I, IXANY    }, /* SUSv2 Ex */
    { "ixoff",    T_I, IXOFF    },
    { "ixon",     T_I, IXON     },
    { "kill",     T_K, VKILL    }, /* SUSv2 */
#if !__INTERIX
    { "nl0",      T_O, NL0      }, /* SUSv2 Ex */
    { "nl1",      T_O, NL1      }, /* SUSv2 Ex */
#endif
    { "noflsh",   T_L, NOFLSH   }, /* SUSv2 */
    { "ocrnl",    T_O, OCRNL    }, /* SUSv2 Ex */
#if _XOPEN_SOURCE >= 600
    { "ofdel",    T_O, OFDEL    }, /* SUSv3 */
#endif
#if !__INTERIX
    { "ofill",    T_O, OFILL    }, /* SUSv2 Ex */
#endif
#if 0
    { "olcuc",    T_O, OLCUC    }, /* SUSv2 LEGACY */
#endif
    { "onlcr",    T_O, ONLCR    }, /* SUSv2 Ex */
    { "onlret",   T_O, ONLRET   }, /* SUSv2 Ex */
    { "onocr",    T_O, ONOCR    }, /* SUSv2 Ex */
    { "opost",    T_O, OPOST    }, /* SUSv2 */
    { "parenb",   T_C, PARENB   }, /* SUSv2 */
    { "parmrk",   T_I, PARMRK   }, /* SUSv2 */
    { "parodd",   T_C, PARODD   }, /* SUSv2 */
#if _XOPEN_SOURCE >= 600
    { "prterase", T_L, PRTERASE }, /* SUSv3 */
#endif
    { "quit",     T_K, VQUIT    }, /* SUSv2 */
    { "size",     T_SIZE, 0     },
    { "start",    T_K, VSTART   }, /* SUSv2 */
    { "stop",     T_K, VSTOP    }, /* SUSv2 */
    { "susp",     T_K, VSUSP    }, /* SUSv2 */
#if !__INTERIX
    { "tab0",     T_O, TAB0     }, /* SUSv2 Ex */
    { "tab1",     T_O, TAB1     }, /* SUSv2 Ex */
    { "tab2",     T_O, TAB2     }, /* SUSv2 Ex */
    { "tab3",     T_O, TAB3     }, /* SUSv2 Ex */
#endif
    { "tostop",   T_L, TOSTOP   }, /* SUSv2 */
#if !__INTERIX
    { "vt0",      T_O, VT0      }, /* SUSv2 Ex */
    { "vt1",      T_O, VT1      }, /* SUSv2 Ex */
#endif
    { "werase",   T_K, VWERASE  },
#if 0
    { "xcase",    T_L, XCASE    }, /* SUSv2 LEGACY */
#endif
    { 0, 0, 0 },
  };
  struct termios ts;

  DBUG_ENTER("init_pty");

  /* get terminal attributes */
  tcgetattr(pty, &ts);

  /* parse attribute string */
  for (; attr && *attr; (attr = strchr(attr, ':')) && ++attr) {
    size_t attr_len;
    int negate;
    const struct flag_s *s;
    tcflag_t *which;
    int ch;
    int cmp;

    if (*attr == ':')
      continue;

    /* check for negated attributes */
    if ((negate = *attr == '-'))
      attr++;
    attr_len = strcspn(attr, "=");

    /* find attribute by name */
    for (s = flag; s->name; s++) {
      if ((cmp = s->name[0] - *attr) < 0)
        continue;
      if ((cmp = strncmp(s->name, attr, attr_len)) < 0)
        continue;
      break;
    }

    if (cmp != 0) {
      DBUG_PRINT("error", ("invalid attribute name %s", attr));
      continue;
    }

    DBUG_PRINT("info", ("attr=%s", s->name));

    /* attribute found */
    switch (s->type) {
    case T_SIZE: { /* set terminal size */
      int h, w;
      if (2 == sscanf(attr+attr_len, "=%d,%d", &h, &w))
        resize(pty, h, w);
      which = 0;
      break;
    }
    case T_K: /* set special character */
      if (attr[attr_len] == '=') {
        ch = parse_char(attr + attr_len + 1);
        DBUG_PRINT("info", ("parse_char() returns %d", ch));
        ts.c_cc[s->v] = (cc_t)ch;
      }
      which = 0;
      break;
    case T_I: which = &ts.c_iflag; break;
    case T_O: which = &ts.c_oflag; break;
    case T_C: which = &ts.c_cflag; break;
    case T_L: which = &ts.c_lflag; break;
    }
    if (which) {
      if (negate) /* negate terminal setting */
        *which &= ~(tcflag_t)s->v;
      else /* set terminal setting */
        *which |= (tcflag_t)s->v;
    }
  }

  tcsetattr(pty, TCSANOW, &ts);
  DBUG_VOID_RETURN;
}

static char *
str_dup(const char *src)
{
    size_t len = strlen(src) + 1;
    char *p;
    if ((p = malloc(len)))
        memcpy(p, src, len);
    return p;
}

static int
setup_child(pid_t *pid, const char *term, const char *attr, char *const *argv)
{
  int master;
  char ttyname[20];

  DBUG_ENTER("setup_child");
  switch ((*pid = pty_fork(&master, ttyname, 0, 0))) {
  case -1:
    DBUG_PRINT("startup", ("forkpty: failed"));
    DBUG_RETURN(-errno);
    /*NOTREACHED*/
  case 0: {
    const char *shell;
    DBUG_PROCESS("child");

    DBUG_PRINT("info", ("TERM=%s", term));
    if (term) setenv("TERM", term, 1);

    DBUG_PRINT("info", ("attributes=%s", attr));
    if (attr) init_pty(0, attr);

    if (!(shell = argv[0])) {
      char *s0;
      uid_t uid;
      struct passwd *pw;
      shell = "/bin/bash";
      s0 = "-bash";
      /* get user's login shell */
      if (!(pw = getpwuid(uid = getuid()))) {
        DBUG_PRINT("error", ("getpwuid(%ld) failed: %s",
          uid, strerror(errno)));
      }
      else if (!(shell = pw->pw_shell) || *shell != '/') {
        DBUG_PRINT("error", ("bad shell for user id %ld", uid));
      }
      else {
        DBUG_PRINT("info", ("got shell %s", shell));
        s0 = strrchr(shell, '/');
        s0 = str_dup(s0);
        assert(s0 != 0);
        s0[0] = '-';
      }
      DBUG_PRINT("info", ("startup %s (%s)", shell, s0));
      execl(shell, s0, (char *)0);
    }
    else {
      DBUG_PRINT("info", ("startup %s", *argv));
      execvp(*argv, argv);
    }

    DBUG_PRINT("error", ("exec* failed: %s", strerror(errno)));
    perror(shell);
    exit(111);
    /*NOTREACHED*/ }
  }
  DBUG_PRINT("startup", ("forkpty:pid=%ld:master=%d:ttyname=%s",
    (long int)*pid, master, ttyname));
  DBUG_RETURN(master);
}

static void
process_message(Buffer b, int pty)
{
  Message m;

  DBUG_ENTER("process_message");
  switch (message_get(&m, b->data, b->len)) {
  case -1:
    DBUG_PRINT("msg", ("invalid message"));
    buffer_consumed(b, b->len);
    break;
  case 0:
    DBUG_PRINT("msg", ("message too small"));
    break;
  default:
    switch (m.type) {
    /* the only message type yet supported */
    case MSG_RESIZE:
      if (pty > 0)
        resize(pty, m.msg.resize.height, m.msg.resize.width);
      else
        DBUG_PRINT("msg", ("ignoring RESIZE on closed pty"));
      break;
    default:
      DBUG_PRINT("msg", ("unknown message type: %d", m.type));
      break;
    }
    buffer_consumed(b, m.size);
    break;
  }
  DBUG_VOID_RETURN;
}


/* These need to be global so that the signal handler has access to them. */
static int t; /* pty descriptor */
static pid_t child;
static volatile sig_atomic_t child_signalled;
static int exit_status;

#define PID_NONE 1
/* child is set to 1 when it is dead.  Hopefully, an accidental kill(1,HUP)
 * will have no effect as pid 1 (init) does not exist under Cygwin.
 */
#define child_alive() (child != PID_NONE)

static void
handle_sigchld(int sig)
{
  child_signalled = sig;
}

static void
check_child(void)
{
  int status;
  DBUG_ENTER("check_child");
  DBUG_PRINT("sig", ("child signalled: %d", child_signalled));
  switch (waitpid(child, &status, WNOHANG)) {
  case -1:
  case 0: break;
  default:
    if (WIFEXITED(status))
      exit_status = WEXITSTATUS(status);
    else
      exit_status = 112;
  }
  DBUG_PRINT("sig", ("child is gone"));
  child = PID_NONE;
  child_signalled = 0;
  DBUG_VOID_RETURN;
}

static void
setnonblock(int d)
{
  fcntl(d, F_SETFL, O_NONBLOCK | fcntl(d, F_GETFL));
}


#ifdef DEBUG
static struct termios save_termios;
static void raw(void)
{
    struct termios termios;
    assert(tcgetattr(STDOUT_FILENO, &save_termios) == 0);
    termios = save_termios;
    termios.c_lflag &= ~(ECHO | ICANON);
    termios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    termios.c_oflag &= ~(OPOST);
    termios.c_cc[VMIN] = 1;
    termios.c_cc[VTIME] = 0;
    assert(tcsetattr(STDOUT_FILENO, TCSAFLUSH, &termios) == 0);
}
static void restore(void)
{
    assert(tcsetattr(STDOUT_FILENO, TCSAFLUSH, &save_termios) == 0);
}
#endif

#define obligatory_max(a,b) ((a)>(b)?(a):(b))

int
main(int argc, char *const *argv)
{
  int
    c,      /* control descriptor (stdin) */
    s;      /* socket descriptor (PuTTY) */
  Buffer
    cbuf,   /* control buffer */
    pbuf,   /* pty buffer */
    sbuf;   /* socket buffer */

  DBUG_INIT_ENV("main",argv[0],"DBUG_OPTS");

#ifndef DBUG_OFF
  setvbuf(DBUG_FILE, 0, _IONBF, 0);
#endif

  /* General steps:
    1. connect to cygterm backend
    2. create pty
    3. fork child process (/bin/bash)
    4. select on pty, cygterm backend forwarding pty data and messages
  */

  if (argc < 4) {
    DBUG_PRINT("error", ("Too few arguments"));
    DBUG_RETURN(CthelperInvalidUsage);
  }

  DBUG_PRINT("startup", ("isatty: (%d,%d,%d)",
    isatty(STDIN_FILENO), isatty(STDOUT_FILENO), isatty(STDERR_FILENO)));
  DBUG_PRINT("startup", (
    "cmdline: [%s] %s %s %s ...", argv[0], argv[1], argv[2], argv[3]));
  {
    extern char **environ;
    char **envp;
    for (envp = environ; *envp; envp++)
      DBUG_PRINT("startup", ("%s", *envp));
  }

  /* It is not necessary to close all open descriptors.  There are no
   * files inherited from the PuTTY process except standard input.
   */
#ifndef DEBUG
  close(STDERR_FILENO);
#endif

  /* Duplicate c and open /dev/null as 0 so that 0 can mean "closed". */
  c = dup(STDIN_FILENO); close(STDIN_FILENO);
  open("/dev/null", O_RDWR);

  /* Command line:
   * argv[1] =  port number
   * argv[2] =  terminal name
   * argv[3] =  terminal characteristics string
   * Any remaining arguments are the command to execute.  If there are no
   * other arguments, use the user's default login shell with a - prefix
   * for its argv[0].
   */
/*
cthelper command line parameters:

cthelper PORT TERM ATTRS [COMMAND [ARGS]]

    PORT
        port number for PuTTY pty input data socket
    TERM
        name of terminal (set TERM environment variable)
    ATTRS
    a colon-separated list of terminal attributes
    See init_pty() for details.
    COMMAND
        Runs COMMAND with ARGS as child process.  If COMMAND is not
        supplied, cthelper will run the user's login shell as specified in
        /etc/passwd specifying "-" for its argv[0] as typical.
*/


  /* connect to cygterm */
  {
    int ct_port = strtol(argv[1], 0, 0);
#ifdef DEBUG
    if (ct_port == 0) {
      /* For debugging purposes, make the tty we are started
       * in the "socket". This allows to test cthelper without
       * putty.exe */
      assert(isatty(STDOUT_FILENO));
      raw();
      atexit(restore);
      c = open("/dev/null", O_RDONLY);
      s = dup(STDOUT_FILENO);
    }
    else 
#endif
    if (ct_port <= 0) {
      DBUG_PRINT("startup", ("invalid port"));
      DBUG_RETURN(CthelperInvalidPort);
    }
    DBUG_PRINT("startup", ("connect cygterm"));
    if (0 > (s = connect_cygterm(ct_port))) {
      DBUG_PRINT("startup", ("connect_cygterm: bad"));
      DBUG_RETURN(CthelperConnectFailed);
    }
    DBUG_PRINT("startup", ("OK"));
  }

  /* initialize buffers */
  DBUG_PRINT("startup", ("initialize buffers"));
  BUFFER_ALLOCA(cbuf, CTLBUF);
  BUFFER_ALLOCA(pbuf, PTOBUF);
  BUFFER_ALLOCA(sbuf, PTIBUF);

  /* set up signal handling */
  signal(SIGCHLD, handle_sigchld);

  /* start child process */
  if (0 > (t = setup_child(&child, argv[2], argv[3], argv + 4))) {
    DBUG_PRINT("startup", ("setup_child failed: %s", strerror(-t)));
    DBUG_RETURN(CthelperPtyforkFailure);
  }

  /*  To explain what is happening here:
   *  's' is the socket between PuTTY and cthelper; it is read to get
   *  input for the tty and written to display output from the pty.
   *  't' is the pseudo terminal; it is read to get pty input which is sent to
   *  PuTTY and written to pass input from PuTTY to the pty.
   *  'c' is standard input, which is a one-way anonymous pipe from PuTTY.
   *  It is read to receive special messages from PuTTY such as
   *  terminal resize events.
   *
   *  This is the flow of data through the buffers:
   *      s => sbuf => t
   *      t => pbuf => s
   *      c => cbuf => process_message()
   *
   *  When 't' is closed, we close(s) to signal PuTTY we are done.
   *  When 's' is closed, we kill(child, HUP) to kill the child process.
   */

  setnonblock(c);
  setnonblock(s);
  setnonblock(t);

  DBUG_PRINT("info", ("c==%d, s==%d, t==%d", c, s, t));
  /* allow easy select() and FD_ISSET() stuff */
  assert(0 < c && c < s && s < t);
  DBUG_PRINT("startup", ("starting select loop"));
  while (s || t) {
    int n = 0;
    fd_set r, w;
    DBUG_ENTER("select");
    FD_ZERO(&r); FD_ZERO(&w);
    if (c && !buffer_isfull(cbuf)) { FD_SET(c, &r); n = c; }
    if (s && !buffer_isfull(sbuf)) { FD_SET(s, &r); n = s; }
    if (s && !buffer_isempty(pbuf)) { FD_SET(s, &w); n = s; }
    if (t && !buffer_isfull(pbuf)) { FD_SET(t, &r); n = t; }
    if (t && !buffer_isempty(sbuf)) { FD_SET(t, &w); n = t; }
    switch (n = select(n + 1, &r, &w, 0, 0)) {
    case -1:
      DBUG_PRINT("error", ("%s", strerror(errno)));
      if (errno != EINTR) {
        /* Something bad happened */
        close(c); c = 0;
        close(s); s = 0;
        close(t); t = 0;
      }
      break;
    case 0:
      DBUG_PRINT("info", ("select timeout"));
      break;
    default:
      DBUG_PRINT("info", ("%d ready descriptors [[r==%lx,w==%lx]]", n, *(unsigned long *)&r, *(unsigned long *)&w));
      if (FD_ISSET(c, &r)) {
        DBUG_ENTER("c=>cbuf");
        switch (buffer_read(cbuf, c)) {
        case -1:
          DBUG_PRINT("error", ("error reading c: %s", strerror(errno)));
          if (errno == EINTR) break;
          /*FALLTHRU*/
        case 0:
          /* PuTTY closed the message pipe */
          DBUG_PRINT("io", ("c closed"));
          close(c); c = 0;
          break;
        default:
          DBUG_PRINT("io", ("cbuf => process_message()"));
          process_message(cbuf, t);
          break;
        }
        DBUG_LEAVE;
        if (!--n) break;
      }
      if (FD_ISSET(s, &r)) {
        DBUG_ENTER("s=>sbuf");
        switch (buffer_read(sbuf, s)) {
        case -1:
          DBUG_PRINT("error", ("error reading s: %s", strerror(errno)));
          if (errno == EINTR) break;
          /*FALLTHRU*/
        case 0:
          /* PuTTY closed the socket */
          DBUG_PRINT("io", ("s closed"));
          close(s); s = 0;
          break;
        default:
          FD_SET(t, &w);
          break;
        }
        DBUG_LEAVE;
        if (!--n) break;
      }
      if (FD_ISSET(t, &r)) {
        DBUG_ENTER("t=>pbuf");
        switch (buffer_read(pbuf, t)) {
        case -1:
          DBUG_PRINT("error", ("error reading t: %s", strerror(errno)));
          if (errno == EINTR) break;
          /*FALLTHRU*/
        case 0:
          /* pty closed */
          DBUG_PRINT("io", ("t closed"));
          if (!FD_ISSET(t, &w)) {
            close(t); t = 0;
          }
          break;
        default:
          FD_SET(s, &w);
          break;
        }
        DBUG_LEAVE;
        if (!--n) break;
      }
      if (FD_ISSET(t, &w)) {
        DBUG_ENTER("sbuf=>t");
        switch (buffer_write(sbuf, t)) {
        case -1:
          DBUG_PRINT("error", ("error writing t: %s", strerror(errno)));
          if (errno == EINTR) break;
          /*FALLTHRU*/
        case 0:
          /* pty closed */
          DBUG_PRINT("io", ("t closed"));
          close(t); t = 0;
          break;
        }
        DBUG_LEAVE;
        if (!--n) break;
      }
      if (FD_ISSET(s, &w)) {
        DBUG_ENTER("pbuf=>s");
        switch (buffer_write(pbuf, s)) {
        case -1:
          DBUG_PRINT("error", ("error writing s: %s", strerror(errno)));
          if (errno == EINTR) break;
          /*FALLTHRU*/
        case 0:
          /* PuTTY closed the socket */
          DBUG_PRINT("io", ("s closed"));
          close(s); s = 0;
          break;
        }
        DBUG_LEAVE;
        if (!--n) break;
      }
      DBUG_PRINT("info", ("[[n==%d,r==%lx,w==%lx]]", n, *(unsigned long *)&r, *(unsigned long *)&w));
      assert(n == 0);
      break;
    }

    if (child_signalled) check_child();

    if (!t && buffer_isempty(pbuf)) {
      DBUG_PRINT("info", ("shutdown socket"));
      shutdown(s, SHUT_WR);
    }

    if (!s && buffer_isempty(sbuf) && child_alive()) {
      DBUG_PRINT("sig", ("kill child"));
      kill(child, SIGHUP);
      /* handle_sigchld() will close(t) */
    }
    DBUG_LEAVE;
  }
  DBUG_PRINT("info", ("end of select loop"));

  /* ensure child process killed */
  /* XXX I'm not sure if all of this is necessary, but it probably won't
   * hurt anything. */
  if (child_alive() && sleep(1) == 0) {
    DBUG_PRINT("sig", ("waiting for child"));
    waitpid(child, 0, WNOHANG);
  }

  DBUG_PRINT("info", ("goodbye"));
  if (exit_status == 111)
    DBUG_RETURN(CthelperExecFailure);
  DBUG_RETURN(EXIT_SUCCESS);
}

/* ex:set sw=4 smarttab: */
