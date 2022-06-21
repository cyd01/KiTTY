/*
 * sftpcommon.c: SFTP code shared between client and server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#include "misc.h"
#include "sftp.h"

static void sftp_pkt_BinarySink_write(
    BinarySink *bs, const void *data, size_t length)
{
    struct sftp_packet *pkt = BinarySink_DOWNCAST(bs, struct sftp_packet);

    assert(length <= 0xFFFFFFFFU - pkt->length);

    sgrowarrayn_nm(pkt->data, pkt->maxlen, pkt->length, length);
    memcpy(pkt->data + pkt->length, data, length);
    pkt->length += length;
}

struct sftp_packet *sftp_pkt_init(int type)
{
    struct sftp_packet *pkt;
    pkt = snew(struct sftp_packet);
    pkt->data = NULL;
    pkt->savedpos = -1;
    pkt->length = 0;
    pkt->maxlen = 0;
    pkt->type = type;
    BinarySink_INIT(pkt, sftp_pkt_BinarySink_write);
    put_uint32(pkt, 0); /* length field will be filled in later */
    put_byte(pkt, 0); /* so will the type field */
    return pkt;
}

void BinarySink_put_fxp_attrs(BinarySink *bs, struct fxp_attrs attrs)
{
    put_uint32(bs, attrs.flags);
    if (attrs.flags & SSH_FILEXFER_ATTR_SIZE)
	put_uint64(bs, attrs.size);
    if (attrs.flags & SSH_FILEXFER_ATTR_UIDGID) {
	put_uint32(bs, attrs.uid);
	put_uint32(bs, attrs.gid);
    }
    if (attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS) {
	put_uint32(bs, attrs.permissions);
    }
    if (attrs.flags & SSH_FILEXFER_ATTR_ACMODTIME) {
	put_uint32(bs, attrs.atime);
	put_uint32(bs, attrs.mtime);
    }
    if (attrs.flags & SSH_FILEXFER_ATTR_EXTENDED) {
	/*
	 * We currently don't support sending any extended
	 * attributes.
	 */
    }
}

const struct fxp_attrs no_attrs = { 0 };

#define put_fxp_attrs(bs, attrs) \
    BinarySink_put_fxp_attrs(BinarySink_UPCAST(bs), attrs)

bool BinarySource_get_fxp_attrs(BinarySource *src, struct fxp_attrs *attrs)
{
    attrs->flags = get_uint32(src);
    if (attrs->flags & SSH_FILEXFER_ATTR_SIZE)
        attrs->size = get_uint64(src);
    if (attrs->flags & SSH_FILEXFER_ATTR_UIDGID) {
	attrs->uid = get_uint32(src);
	attrs->gid = get_uint32(src);
    }
    if (attrs->flags & SSH_FILEXFER_ATTR_PERMISSIONS)
	attrs->permissions = get_uint32(src);
    if (attrs->flags & SSH_FILEXFER_ATTR_ACMODTIME) {
	attrs->atime = get_uint32(src);
	attrs->mtime = get_uint32(src);
    }
    if (attrs->flags & SSH_FILEXFER_ATTR_EXTENDED) {
	unsigned long count = get_uint32(src);
	while (count--) {
	    if (get_err(src)) {
		/* Truncated packet. Don't waste time looking for
		 * attributes that aren't there. Caller should spot
		 * the truncation. */
		break;
	    }
	    /*
	     * We should try to analyse these, if we ever find one
	     * we recognise.
	     */
	    get_string(src);
	    get_string(src);
	}
    }
    return true;
}

void sftp_pkt_free(struct sftp_packet *pkt)
{
    if (pkt->data)
	sfree(pkt->data);
    sfree(pkt);
}

void sftp_send_prepare(struct sftp_packet *pkt)
{
    PUT_32BIT_MSB_FIRST(pkt->data, pkt->length - 4);
    if (pkt->length >= 5) {
        /* Rewrite the type code, in case the caller changed its mind
         * about pkt->type since calling sftp_pkt_init */
        pkt->data[4] = pkt->type;
    }
}

struct sftp_packet *sftp_recv_prepare(unsigned length)
{
    struct sftp_packet *pkt;

    pkt = snew(struct sftp_packet);
    pkt->savedpos = 0;
    pkt->length = pkt->maxlen = length;
    pkt->data = snewn(pkt->length, char);

    return pkt;
}

bool sftp_recv_finish(struct sftp_packet *pkt)
{
    BinarySource_INIT(pkt, pkt->data, pkt->length);
    pkt->type = get_byte(pkt);
    return !get_err(pkt);
}
