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

static const char *null_init(Seat *, Backend **, LogContext *, Conf *,
                             const char *, int, char **, int, int);
static const char *loop_init(Seat *, Backend **, LogContext *, Conf *,
                             const char *, int, char **, int, int);
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

const struct BackendVtable null_backend = {
    null_init, null_free, null_reconfig, null_send, null_sendbuffer, null_size,
    null_special, null_get_specials, null_connected, null_exitcode, null_sendok,
    null_ldisc, null_provide_ldisc, null_unthrottle,
    null_cfg_info, NULL /* test_for_upstream */, "null", -1, 0
};

const struct BackendVtable loop_backend = {
    loop_init, loop_free, null_reconfig, loop_send, null_sendbuffer, null_size,
    null_special, null_get_specials, null_connected, null_exitcode, null_sendok,
    null_ldisc, null_provide_ldisc, null_unthrottle,
    null_cfg_info, NULL /* test_for_upstream */, "loop", -1, 0
};

struct loop_state {
    Seat *seat;
    Backend backend;
};

static const char *null_init(Seat *seat, Backend **backend_handle,
                               LogContext *logctx, Conf *conf,
			       const char *host, int port, char **realhost,
			       int nodelay, int keepalive) {
    /* No local authentication phase in this protocol */
    seat_set_trust_status(seat, false);

    *backend_handle = NULL;
    return NULL;
}

static const char *loop_init(Seat *seat, Backend **backend_handle,
                             LogContext *logctx, Conf *conf,
                             const char *host, int port, char **realhost,
                             int nodelay, int keepalive) {
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
