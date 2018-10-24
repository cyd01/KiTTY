/*
 * uxproxy.c: Unix implementation of platform_new_connection(),
 * supporting an OpenSSH-like proxy command.
 */

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#define DEFINE_PLUG_METHOD_MACROS
#include "tree234.h"
#include "putty.h"
#include "network.h"
#include "proxy.h"

typedef struct Socket_localproxy_tag * Local_Proxy_Socket;

struct Socket_localproxy_tag {
    const struct socket_function_table *fn;
    /* the above variable absolutely *must* be the first in this structure */

    int to_cmd, from_cmd, cmd_err;     /* fds */

    char *error;

    Plug plug;

    bufchain pending_output_data;
    bufchain pending_input_data;
    bufchain pending_error_data;
    enum { EOF_NO, EOF_PENDING, EOF_SENT } outgoingeof;
};

static void localproxy_select_result(int fd, int event);

/*
 * Trees to look up the pipe fds in.
 */
static tree234 *localproxy_by_fromfd;
static tree234 *localproxy_by_tofd;
static tree234 *localproxy_by_errfd;
static int localproxy_fromfd_cmp(void *av, void *bv)
{
    Local_Proxy_Socket a = (Local_Proxy_Socket)av;
    Local_Proxy_Socket b = (Local_Proxy_Socket)bv;
    if (a->from_cmd < b->from_cmd)
	return -1;
    if (a->from_cmd > b->from_cmd)
	return +1;
    return 0;
}
static int localproxy_fromfd_find(void *av, void *bv)
{
    int a = *(int *)av;
    Local_Proxy_Socket b = (Local_Proxy_Socket)bv;
    if (a < b->from_cmd)
	return -1;
    if (a > b->from_cmd)
	return +1;
    return 0;
}
static int localproxy_tofd_cmp(void *av, void *bv)
{
    Local_Proxy_Socket a = (Local_Proxy_Socket)av;
    Local_Proxy_Socket b = (Local_Proxy_Socket)bv;
    if (a->to_cmd < b->to_cmd)
	return -1;
    if (a->to_cmd > b->to_cmd)
	return +1;
    return 0;
}
static int localproxy_tofd_find(void *av, void *bv)
{
    int a = *(int *)av;
    Local_Proxy_Socket b = (Local_Proxy_Socket)bv;
    if (a < b->to_cmd)
	return -1;
    if (a > b->to_cmd)
	return +1;
    return 0;
}
static int localproxy_errfd_cmp(void *av, void *bv)
{
    Local_Proxy_Socket a = (Local_Proxy_Socket)av;
    Local_Proxy_Socket b = (Local_Proxy_Socket)bv;
    if (a->cmd_err < b->cmd_err)
	return -1;
    if (a->cmd_err > b->cmd_err)
	return +1;
    return 0;
}
static int localproxy_errfd_find(void *av, void *bv)
{
    int a = *(int *)av;
    Local_Proxy_Socket b = (Local_Proxy_Socket)bv;
    if (a < b->cmd_err)
	return -1;
    if (a > b->cmd_err)
	return +1;
    return 0;
}

/* basic proxy socket functions */

static Plug sk_localproxy_plug (Socket s, Plug p)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;
    Plug ret = ps->plug;
    if (p)
	ps->plug = p;
    return ret;
}

static void sk_localproxy_close (Socket s)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;

    if (ps->to_cmd >= 0) {
        del234(localproxy_by_tofd, ps);
        uxsel_del(ps->to_cmd);
        close(ps->to_cmd);
    }

    del234(localproxy_by_fromfd, ps);
    uxsel_del(ps->from_cmd);
    close(ps->from_cmd);

    del234(localproxy_by_errfd, ps);
    uxsel_del(ps->cmd_err);
    close(ps->cmd_err);

    bufchain_clear(&ps->pending_input_data);
    bufchain_clear(&ps->pending_output_data);
    bufchain_clear(&ps->pending_error_data);

    sfree(ps);
}

static int localproxy_try_send(Local_Proxy_Socket ps)
{
    int sent = 0;

    while (bufchain_size(&ps->pending_output_data) > 0) {
	void *data;
	int len, ret;

	bufchain_prefix(&ps->pending_output_data, &data, &len);
	ret = write(ps->to_cmd, data, len);
	if (ret < 0 && errno != EWOULDBLOCK) {
            plug_closing(ps->plug, strerror(errno), errno, 0);
            return 0;
	} else if (ret <= 0) {
	    break;
	} else {
	    bufchain_consume(&ps->pending_output_data, ret);
	    sent += ret;
	}
    }

    if (ps->outgoingeof == EOF_PENDING) {
        del234(localproxy_by_tofd, ps);
        close(ps->to_cmd);
        uxsel_del(ps->to_cmd);
        ps->to_cmd = -1;
        ps->outgoingeof = EOF_SENT;
    }

    if (bufchain_size(&ps->pending_output_data) == 0)
	uxsel_del(ps->to_cmd);
    else
	uxsel_set(ps->to_cmd, 2, localproxy_select_result);

    return sent;
}

static int sk_localproxy_write (Socket s, const char *data, int len)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;

    assert(ps->outgoingeof == EOF_NO);

    bufchain_add(&ps->pending_output_data, data, len);

    localproxy_try_send(ps);

    return bufchain_size(&ps->pending_output_data);
}

static int sk_localproxy_write_oob (Socket s, const char *data, int len)
{
    /*
     * oob data is treated as inband; nasty, but nothing really
     * better we can do
     */
    return sk_localproxy_write(s, data, len);
}

static void sk_localproxy_write_eof (Socket s)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;

    assert(ps->outgoingeof == EOF_NO);
    ps->outgoingeof = EOF_PENDING;

    localproxy_try_send(ps);
}

static void sk_localproxy_flush (Socket s)
{
    /* Local_Proxy_Socket ps = (Local_Proxy_Socket) s; */
    /* do nothing */
}

static void sk_localproxy_set_frozen (Socket s, int is_frozen)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;

    if (is_frozen)
	uxsel_del(ps->from_cmd);
    else
	uxsel_set(ps->from_cmd, 1, localproxy_select_result);
}

static const char * sk_localproxy_socket_error (Socket s)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;
    return ps->error;
}

static void localproxy_select_result(int fd, int event)
{
    Local_Proxy_Socket s;
    char buf[20480];
    int ret;

    if (!(s = find234(localproxy_by_fromfd, &fd, localproxy_fromfd_find)) &&
	!(s = find234(localproxy_by_fromfd, &fd, localproxy_errfd_find)) &&
	!(s = find234(localproxy_by_tofd, &fd, localproxy_tofd_find)) )
	return;		       /* boggle */

    if (event == 1) {
        if (fd == s->cmd_err) {
            ret = read(fd, buf, sizeof(buf));
            if (ret > 0)
                log_proxy_stderr(s->plug, &s->pending_error_data, buf, ret);
        } else {
            assert(fd == s->from_cmd);
            ret = read(fd, buf, sizeof(buf));
            if (ret < 0) {
                plug_closing(s->plug, strerror(errno), errno, 0);
            } else if (ret == 0) {
                plug_closing(s->plug, NULL, 0, 0);
            } else {
                plug_receive(s->plug, 0, buf, ret);
            }
        }
    } else if (event == 2) {
	assert(fd == s->to_cmd);
	if (localproxy_try_send(s))
	    plug_sent(s->plug, bufchain_size(&s->pending_output_data));
    }
}

Socket platform_new_connection(SockAddr addr, const char *hostname,
			       int port, int privport,
			       int oobinline, int nodelay, int keepalive,
			       Plug plug, Conf *conf)
{
    char *cmd;

    static const struct socket_function_table socket_fn_table = {
	sk_localproxy_plug,
	sk_localproxy_close,
	sk_localproxy_write,
	sk_localproxy_write_oob,
	sk_localproxy_write_eof,
	sk_localproxy_flush,
	sk_localproxy_set_frozen,
	sk_localproxy_socket_error,
        NULL, /* peer_info */
    };

    Local_Proxy_Socket ret;
    int to_cmd_pipe[2], from_cmd_pipe[2], cmd_err_pipe[2], pid, proxytype;

    proxytype = conf_get_int(conf, CONF_proxy_type);
    if (proxytype != PROXY_CMD && proxytype != PROXY_FUZZ)
	return NULL;

    ret = snew(struct Socket_localproxy_tag);
    ret->fn = &socket_fn_table;
    ret->plug = plug;
    ret->error = NULL;
    ret->outgoingeof = EOF_NO;

    bufchain_init(&ret->pending_input_data);
    bufchain_init(&ret->pending_output_data);
    bufchain_init(&ret->pending_error_data);

    if (proxytype == PROXY_CMD) {
	cmd = format_telnet_command(addr, port, conf);

        if (flags & FLAG_STDERR) {
            /* If we have a sensible stderr, the proxy command can
             * send its own standard error there, so we won't
             * interfere. */
            cmd_err_pipe[0] = cmd_err_pipe[1] = -1;
        } else {
            /* If we don't have a sensible stderr, we should catch the
             * proxy command's standard error to put in our event
             * log. */
            cmd_err_pipe[0] = cmd_err_pipe[1] = 0;
        }

        {
            char *logmsg = dupprintf("Starting local proxy command: %s", cmd);
            plug_log(plug, 2, NULL, 0, logmsg, 0);
            sfree(logmsg);
        }

	/*
	 * Create the pipes to the proxy command, and spawn the proxy
	 * command process.
	 */
	if (pipe(to_cmd_pipe) < 0 ||
	    pipe(from_cmd_pipe) < 0 ||
            (cmd_err_pipe[0] == 0 && pipe(cmd_err_pipe) < 0)) {
	    ret->error = dupprintf("pipe: %s", strerror(errno));
	    sfree(cmd);
	    return (Socket)ret;
	}
	cloexec(to_cmd_pipe[1]);
	cloexec(from_cmd_pipe[0]);
	if (cmd_err_pipe[0] >= 0)
            cloexec(cmd_err_pipe[0]);

	pid = fork();

	if (pid < 0) {
	    ret->error = dupprintf("fork: %s", strerror(errno));
	    sfree(cmd);
	    return (Socket)ret;
	} else if (pid == 0) {
	    close(0);
	    close(1);
	    dup2(to_cmd_pipe[0], 0);
	    dup2(from_cmd_pipe[1], 1);
	    close(to_cmd_pipe[0]);
	    close(from_cmd_pipe[1]);
	    if (cmd_err_pipe[0] >= 0) {
                dup2(cmd_err_pipe[1], 2);
                close(cmd_err_pipe[1]);
            }
	    noncloexec(0);
	    noncloexec(1);
	    execl("/bin/sh", "sh", "-c", cmd, (void *)NULL);
	    _exit(255);
	}

	sfree(cmd);

	close(to_cmd_pipe[0]);
	close(from_cmd_pipe[1]);
        if (cmd_err_pipe[0] >= 0)
            close(cmd_err_pipe[1]);

	ret->to_cmd = to_cmd_pipe[1];
	ret->from_cmd = from_cmd_pipe[0];
	ret->cmd_err = cmd_err_pipe[0];
    } else {
	cmd = format_telnet_command(addr, port, conf);
	ret->to_cmd = open("/dev/null", O_WRONLY);
	if (ret->to_cmd == -1) {
	    ret->error = dupprintf("/dev/null: %s", strerror(errno));
	    sfree(cmd);
	    return (Socket)ret;
	}
	ret->from_cmd = open(cmd, O_RDONLY);
	if (ret->from_cmd == -1) {
	    ret->error = dupprintf("%s: %s", cmd, strerror(errno));
	    sfree(cmd);
	    return (Socket)ret;
	}
	sfree(cmd);
	ret->cmd_err = -1;
    }

    if (!localproxy_by_fromfd)
	localproxy_by_fromfd = newtree234(localproxy_fromfd_cmp);
    if (!localproxy_by_tofd)
	localproxy_by_tofd = newtree234(localproxy_tofd_cmp);
    if (!localproxy_by_errfd)
	localproxy_by_errfd = newtree234(localproxy_errfd_cmp);

    add234(localproxy_by_fromfd, ret);
    add234(localproxy_by_tofd, ret);
    if (ret->cmd_err >= 0)
        add234(localproxy_by_errfd, ret);

    uxsel_set(ret->from_cmd, 1, localproxy_select_result);
    if (ret->cmd_err >= 0)
        uxsel_set(ret->cmd_err, 1, localproxy_select_result);

    /* We are responsible for this and don't need it any more */
    sk_addr_free(addr);

    return (Socket) ret;
}
