/*
 * Copyright (c) 1999 Simon Tatham
 * Copyright (c) 1999 Ben Harris
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* PuTTY test backends */

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"

static char *null_init(const BackendVtable *, Seat *, Backend **, LogContext *,
                       Conf *, const char *, int, char **, bool, bool);
static char *loop_init(const BackendVtable *, Seat *, Backend **, LogContext *,
                       Conf *, const char *, int, char **, bool, bool);
static void null_free(Backend *);
static void loop_free(Backend *);
static void null_reconfig(Backend *, Conf *);
static size_t null_send(Backend *, const char *, size_t);
static size_t loop_send(Backend *, const char *, size_t);
static size_t null_sendbuffer(Backend *);
static void null_size(Backend *, int, int);
static void null_special(Backend *, SessionSpecialCode, int);
static const SessionSpecial *null_get_specials(Backend *);
static int null_connected(Backend *);
static int null_exitcode(Backend *);
static int null_sendok(Backend *);
static int null_ldisc(Backend *, int);
static void null_provide_ldisc(Backend *, Ldisc *);
static void null_unthrottle(Backend *, size_t);
static int null_cfg_info(Backend *);

const BackendVtable null_backend = {
    .init = null_init,
    .free = null_free,
    .reconfig = null_reconfig,
    .send = null_send,
    .sendbuffer = null_sendbuffer,
    .size = null_size,
    .special = null_special,
    .get_specials = null_get_specials,
    .connected = null_connected,
    .exitcode = null_exitcode,
    .sendok = null_sendok,
    .ldisc_option_state = null_ldisc,
    .provide_ldisc = null_provide_ldisc,
    .unthrottle = null_unthrottle,
    .cfg_info = null_cfg_info,
    .id = "null",
    .displayname = "null",
    .protocol = -1,
    .default_port = 0,
};

const BackendVtable loop_backend = {
    .init = loop_init,
    .free = loop_free,
    .reconfig = null_reconfig,
    .send = loop_send,
    .sendbuffer = null_sendbuffer,
    .size = null_size,
    .special = null_special,
    .get_specials = null_get_specials,
    .connected = null_connected,
    .exitcode = null_exitcode,
    .sendok = null_sendok,
    .ldisc_option_state = null_ldisc,
    .provide_ldisc = null_provide_ldisc,
    .unthrottle = null_unthrottle,
    .cfg_info = null_cfg_info,
    .id = "loop",
    .displayname = "loop",
    .protocol = -1,
    .default_port = 0,
};

struct loop_state {
    Seat *seat;
    Backend backend;
};

static char *null_init(const BackendVtable *vt, Seat *seat,
                       Backend **backend_handle, LogContext *logctx,
                       Conf *conf, const char *host, int port,
                       char **realhost, bool nodelay, bool keepalive) {
    /* No local authentication phase in this protocol */
    seat_set_trust_status(seat, false);

    *backend_handle = NULL;
    return NULL;
}

static char *loop_init(const BackendVtable *vt, Seat *seat,
                       Backend **backend_handle, LogContext *logctx,
                       Conf *conf, const char *host, int port,
                       char **realhost, bool nodelay, bool keepalive) {
    struct loop_state *st = snew(struct loop_state);

    /* No local authentication phase in this protocol */
    seat_set_trust_status(seat, false);

    st->seat = seat;
    *backend_handle = &st->backend;
    return NULL;
}

static void null_free(Backend *be)
{

}

static void loop_free(Backend *be)
{
    struct loop_state *st = container_of(be, struct loop_state, backend);

    sfree(st);
}

static void null_reconfig(Backend *be, Conf *conf) {

}

static size_t null_send(Backend *be, const char *buf, size_t len) {

    return 0;
}

static size_t loop_send(Backend *be, const char *buf, size_t len) {
    struct loop_state *st = container_of(be, struct loop_state, backend);

    return seat_output(st->seat, 0, buf, len);
}

static size_t null_sendbuffer(Backend *be) {

    return 0;
}

static void null_size(Backend *be, int width, int height) {

}

static void null_special(Backend *be, SessionSpecialCode code, int arg) {

}

static const SessionSpecial *null_get_specials (Backend *be) {

    return NULL;
}

static int null_connected(Backend *be) {

    return 0;
}

static int null_exitcode(Backend *be) {

    return 0;
}

static int null_sendok(Backend *be) {

    return 1;
}

static void null_unthrottle(Backend *be, size_t backlog) {

}

static int null_ldisc(Backend *be, int option) {

    return 0;
}

static void null_provide_ldisc (Backend *be, Ldisc *ldisc) {

}

static int null_cfg_info(Backend *be)
{
    return 0;
}


/*
 * Emacs magic:
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
