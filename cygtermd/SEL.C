/*
 * sel.c: implementation of sel.h.
 */

#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>

#include "sel.h"
#include "malloc.h"

/* ----------------------------------------------------------------------
 * Chunk of code lifted from PuTTY's misc.c to manage buffers of
 * data to be written to an fd.
 */

#define BUFFER_GRANULE  512

typedef struct bufchain_tag {
    struct bufchain_granule *head, *tail;
    size_t buffersize;		       /* current amount of buffered data */
} bufchain;
struct bufchain_granule {
    struct bufchain_granule *next;
    size_t buflen, bufpos;
    char buf[BUFFER_GRANULE];
};

static void bufchain_init(bufchain *ch)
{
    ch->head = ch->tail = NULL;
    ch->buffersize = 0;
}

static void bufchain_clear(bufchain *ch)
{
    struct bufchain_granule *b;
    while (ch->head) {
	b = ch->head;
	ch->head = ch->head->next;
	sfree(b);
    }
    ch->tail = NULL;
    ch->buffersize = 0;
}

static size_t bufchain_size(bufchain *ch)
{
    return ch->buffersize;
}

static void bufchain_add(bufchain *ch, const void *data, size_t len)
{
    const char *buf = (const char *)data;

    if (len == 0) return;

    ch->buffersize += len;

    if (ch->tail && ch->tail->buflen < BUFFER_GRANULE) {
	size_t copylen = BUFFER_GRANULE - ch->tail->buflen;
	if (copylen > len)
	    copylen = len;
	memcpy(ch->tail->buf + ch->tail->buflen, buf, copylen);
	buf += copylen;
	len -= copylen;
	ch->tail->buflen += copylen;
    }
    while (len > 0) {
	struct bufchain_granule *newbuf;
	size_t grainlen = BUFFER_GRANULE;
	if (grainlen > len)
	    grainlen = len;
	newbuf = snew(struct bufchain_granule);
	newbuf->bufpos = 0;
	newbuf->buflen = grainlen;
	memcpy(newbuf->buf, buf, grainlen);
	buf += grainlen;
	len -= grainlen;
	if (ch->tail)
	    ch->tail->next = newbuf;
	else
	    ch->head = ch->tail = newbuf;
	newbuf->next = NULL;
	ch->tail = newbuf;
    }
}

static void bufchain_consume(bufchain *ch, size_t len)
{
    struct bufchain_granule *tmp;

    assert(ch->buffersize >= len);
    while (len > 0) {
	size_t remlen = len;
	assert(ch->head != NULL);
	if (remlen >= ch->head->buflen - ch->head->bufpos) {
	    remlen = ch->head->buflen - ch->head->bufpos;
	    tmp = ch->head;
	    ch->head = tmp->next;
	    sfree(tmp);
	    if (!ch->head)
		ch->tail = NULL;
	} else
	    ch->head->bufpos += remlen;
	ch->buffersize -= remlen;
	len -= remlen;
    }
}

static void bufchain_prefix(bufchain *ch, void **data, size_t *len)
{
    *len = ch->head->buflen - ch->head->bufpos;
    *data = ch->head->buf + ch->head->bufpos;
}

/* ----------------------------------------------------------------------
 * The actual implementation of the sel interface.
 */

struct sel {
    void *ctx;
    sel_rfd *rhead, *rtail;
    sel_wfd *whead, *wtail;
};

struct sel_rfd {
    sel *parent;
    sel_rfd *prev, *next;
    sel_readdata_fn_t readdata;
    sel_readerr_fn_t readerr;
    void *ctx;
    int fd;
    int frozen;
};

struct sel_wfd {
    sel *parent;
    sel_wfd *prev, *next;
    sel_written_fn_t written;
    sel_writeerr_fn_t writeerr;
    void *ctx;
    int fd;
    bufchain buf;
};

sel *sel_new(void *ctx)
{
    sel *sel = snew(struct sel);

    sel->ctx = ctx;
    sel->rhead = sel->rtail = NULL;
    sel->whead = sel->wtail = NULL;

    return sel;
}

sel_wfd *sel_wfd_add(sel *sel, int fd,
		     sel_written_fn_t written, sel_writeerr_fn_t writeerr,
		     void *ctx)
{
    sel_wfd *wfd = snew(sel_wfd);

    wfd->written = written;
    wfd->writeerr = writeerr;
    wfd->ctx = ctx;
    wfd->fd = fd;
    bufchain_init(&wfd->buf);

    wfd->next = NULL;
    wfd->prev = sel->wtail;
    if (sel->wtail)
	sel->wtail->next = wfd;
    else
	sel->whead = wfd;
    sel->wtail = wfd;
    wfd->parent = sel;

    return wfd;
}

sel_rfd *sel_rfd_add(sel *sel, int fd,
		     sel_readdata_fn_t readdata, sel_readerr_fn_t readerr,
		     void *ctx)
{
    sel_rfd *rfd = snew(sel_rfd);

    rfd->readdata = readdata;
    rfd->readerr = readerr;
    rfd->ctx = ctx;
    rfd->fd = fd;
    rfd->frozen = 0;

    rfd->next = NULL;
    rfd->prev = sel->rtail;
    if (sel->rtail)
	sel->rtail->next = rfd;
    else
	sel->rhead = rfd;
    sel->rtail = rfd;
    rfd->parent = sel;

    return rfd;
}

size_t sel_write(sel_wfd *wfd, const void *data, size_t len)
{
    bufchain_add(&wfd->buf, data, len);
    return bufchain_size(&wfd->buf);
}

void sel_wfd_setfd(sel_wfd *wfd, int fd)
{
    wfd->fd = fd;
}

void sel_rfd_setfd(sel_rfd *rfd, int fd)
{
    rfd->fd = fd;
}

void sel_rfd_freeze(sel_rfd *rfd)
{
    rfd->frozen = 1;
}

void sel_rfd_unfreeze(sel_rfd *rfd)
{
    rfd->frozen = 0;
}

int sel_wfd_delete(sel_wfd *wfd)
{
    sel *sel = wfd->parent;
    int ret;

    if (wfd->prev)
	wfd->prev->next = wfd->next;
    else
	sel->whead = wfd->next;
    if (wfd->next)
	wfd->next->prev = wfd->prev;
    else
	sel->wtail = wfd->prev;

    bufchain_clear(&wfd->buf);

    ret = wfd->fd;
    sfree(wfd);
    return ret;
}

int sel_rfd_delete(sel_rfd *rfd)
{
    sel *sel = rfd->parent;
    int ret;

    if (rfd->prev)
	rfd->prev->next = rfd->next;
    else
	sel->rhead = rfd->next;
    if (rfd->next)
	rfd->next->prev = rfd->prev;
    else
	sel->rtail = rfd->prev;

    ret = rfd->fd;
    sfree(rfd);
    return ret;
}

void sel_free(sel *sel)
{
    while (sel->whead)
	sel_wfd_delete(sel->whead);
    while (sel->rhead)
	sel_rfd_delete(sel->rhead);
    sfree(sel);
}

void *sel_get_ctx(sel *sel) { return sel->ctx; }
void sel_set_ctx(sel *sel, void *ctx) { sel->ctx = ctx; }
void *sel_wfd_get_ctx(sel_wfd *wfd) { return wfd->ctx; }
void sel_wfd_set_ctx(sel_wfd *wfd, void *ctx) { wfd->ctx = ctx; }
void *sel_rfd_get_ctx(sel_rfd *rfd) { return rfd->ctx; }
void sel_rfd_set_ctx(sel_rfd *rfd, void *ctx) { rfd->ctx = ctx; }

int sel_iterate(sel *sel, long timeout)
{
    sel_rfd *rfd;
    sel_wfd *wfd;
    fd_set rset, wset;
    int maxfd = 0;
    struct timeval tv, *ptv;
    char buf[65536];
    int ret;

    FD_ZERO(&rset);
    FD_ZERO(&wset);

    for (rfd = sel->rhead; rfd; rfd = rfd->next) {
	if (rfd->fd >= 0 && !rfd->frozen) {
	    FD_SET(rfd->fd, &rset);
	    if (maxfd < rfd->fd + 1)
		maxfd = rfd->fd + 1;
	}
    }

    for (wfd = sel->whead; wfd; wfd = wfd->next) {
	if (wfd->fd >= 0 && bufchain_size(&wfd->buf)) {
	    FD_SET(wfd->fd, &wset);
	    if (maxfd < wfd->fd + 1)
		maxfd = wfd->fd + 1;
	}
    }

    if (timeout < 0) {
	ptv = NULL;
    } else {
	ptv = &tv;
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = 1000 * (timeout % 1000);
    }

    do {
	ret = select(maxfd, &rset, &wset, NULL, ptv);
    } while (ret < 0 && (errno == EINTR || errno == EAGAIN));

    if (ret < 0)
	return errno;

    /*
     * Just in case one of the callbacks destroys an rfd or wfd we
     * had yet to get round to, we must loop from the start every
     * single time. Algorithmically irritating, but necessary
     * unless we want to store the rfd structures in a heavyweight
     * tree sorted by fd. And let's face it, if we cared about
     * good algorithmic complexity it's not at all clear we'd be
     * using select in the first place.
     */
    do {
	for (wfd = sel->whead; wfd; wfd = wfd->next)
	    if (wfd->fd >= 0 && FD_ISSET(wfd->fd, &wset)) {
		void *data;
		size_t len;

		FD_CLR(wfd->fd, &wset);
		bufchain_prefix(&wfd->buf, &data, &len);
		ret = write(wfd->fd, data, len);
		assert(ret != 0);
		if (ret < 0) {
		    if (wfd->writeerr)
			wfd->writeerr(wfd, errno);
		} else {
		    bufchain_consume(&wfd->buf, len);
		    if (wfd->written)
			wfd->written(wfd, bufchain_size(&wfd->buf));
		}
		break;
	    }
    } while (wfd);
    do {
	for (rfd = sel->rhead; rfd; rfd = rfd->next) 
	    if (rfd->fd >= 0 && !rfd->frozen && FD_ISSET(rfd->fd, &rset)) {
		FD_CLR(rfd->fd, &rset);
		ret = read(rfd->fd, buf, sizeof(buf));
		if (ret < 0) {
		    if (rfd->readerr)
			rfd->readerr(rfd, errno);
		} else {
		    if (rfd->readdata)
			rfd->readdata(rfd, buf, ret);
		}
		break;
	    }
    } while (rfd);

    return 0;
}
