/*
 * Implement the centralised parts of the server side of SFTP.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "putty.h"
#include "ssh.h"
#include "sftp.h"

struct sftp_packet *sftp_handle_request(
    SftpServer *srv, struct sftp_packet *req)
{
    struct sftp_packet *reply;
    unsigned id;
    ptrlen path, dstpath, handle, data;
    uint64_t offset;
    unsigned length;
    struct fxp_attrs attrs;
    DefaultSftpReplyBuilder dsrb;
    SftpReplyBuilder *rb;

    if (req->type == SSH_FXP_INIT) {
        /*
         * Special case which doesn't have a request id at the start.
         */
        reply = sftp_pkt_init(SSH_FXP_VERSION);
        /*
         * Since we support only the lowest protocol version, we don't
         * need to take the min of this and the client's version, or
         * even to bother reading the client version number out of the
         * input packet.
         */
        put_uint32(reply, SFTP_PROTO_VERSION);
        return reply;
    }

    /*
     * Centralise the request id handling. We'll overwrite the type
     * code of the output packet later.
     */
    id = get_uint32(req);
    reply = sftp_pkt_init(0);
    put_uint32(reply, id);

    dsrb.rb.vt = &DefaultSftpReplyBuilder_vt;
    dsrb.pkt = reply;
    rb = &dsrb.rb;

    switch (req->type) {
      case SSH_FXP_REALPATH:
        path = get_string(req);
        if (get_err(req))
            goto decode_error;
        sftpsrv_realpath(srv, rb, path);
        break;

      case SSH_FXP_OPEN:
        path = get_string(req);
        flags = get_uint32(req);
        get_fxp_attrs(req, &attrs);
        if (get_err(req))
            goto decode_error;
        if ((flags & (SSH_FXF_READ|SSH_FXF_WRITE)) == 0) {
            fxp_reply_error(rb, SSH_FX_BAD_MESSAGE,
                            "open without READ or WRITE flag");
        } else if ((flags & (SSH_FXF_CREAT|SSH_FXF_TRUNC)) == SSH_FXF_TRUNC) {
            fxp_reply_error(rb, SSH_FX_BAD_MESSAGE,
                            "open with TRUNC but not CREAT");
        } else if ((flags & (SSH_FXF_CREAT|SSH_FXF_EXCL)) == SSH_FXF_EXCL) {
            fxp_reply_error(rb, SSH_FX_BAD_MESSAGE,
                            "open with EXCL but not CREAT");
        } else {
            sftpsrv_open(srv, rb, path, flags, attrs);
        }
        break;

      case SSH_FXP_OPENDIR:
        path = get_string(req);
        if (get_err(req))
            goto decode_error;
        sftpsrv_opendir(srv, rb, path);
        break;

      case SSH_FXP_CLOSE:
        handle = get_string(req);
        if (get_err(req))
            goto decode_error;
        sftpsrv_close(srv, rb, handle);
        break;

      case SSH_FXP_MKDIR:
        path = get_string(req);
        get_fxp_attrs(req, &attrs);
        if (get_err(req))
            goto decode_error;
        sftpsrv_mkdir(srv, rb, path, attrs);
        break;

      case SSH_FXP_RMDIR:
        path = get_string(req);
        if (get_err(req))
            goto decode_error;
        sftpsrv_rmdir(srv, rb, path);
        break;

      case SSH_FXP_REMOVE:
        path = get_string(req);
        if (get_err(req))
            goto decode_error;
        sftpsrv_remove(srv, rb, path);
        break;

      case SSH_FXP_RENAME:
        path = get_string(req);
        dstpath = get_string(req);
        if (get_err(req))
            goto decode_error;
        sftpsrv_rename(srv, rb, path, dstpath);
        break;

      case SSH_FXP_STAT:
        path = get_string(req);
        if (get_err(req))
            goto decode_error;
        sftpsrv_stat(srv, rb, path, true);
        break;

      case SSH_FXP_LSTAT:
        path = get_string(req);
        if (get_err(req))
            goto decode_error;
        sftpsrv_stat(srv, rb, path, false);
        break;

      case SSH_FXP_FSTAT:
        handle = get_string(req);
        if (get_err(req))
            goto decode_error;
        sftpsrv_fstat(srv, rb, handle);
        break;

      case SSH_FXP_SETSTAT:
        path = get_string(req);
        get_fxp_attrs(req, &attrs);
        if (get_err(req))
            goto decode_error;
        sftpsrv_setstat(srv, rb, path, attrs);
        break;

      case SSH_FXP_FSETSTAT:
        handle = get_string(req);
        get_fxp_attrs(req, &attrs);
        if (get_err(req))
            goto decode_error;
        sftpsrv_fsetstat(srv, rb, handle, attrs);
        break;

      case SSH_FXP_READ:
        handle = get_string(req);
        offset = get_uint64(req);
        length = get_uint32(req);
        if (get_err(req))
            goto decode_error;
        sftpsrv_read(srv, rb, handle, offset, length);
        break;

      case SSH_FXP_READDIR:
        handle = get_string(req);
        if (get_err(req))
            goto decode_error;
        sftpsrv_readdir(srv, rb, handle, INT_MAX, false);
        break;

      case SSH_FXP_WRITE:
        handle = get_string(req);
        offset = get_uint64(req);
        data = get_string(req);
        if (get_err(req))
            goto decode_error;
        sftpsrv_write(srv, rb, handle, offset, data);
        break;

      default:
        if (get_err(req))
            goto decode_error;
        fxp_reply_error(rb, SSH_FX_OP_UNSUPPORTED,
                        "Unrecognised request type");
        break;

      decode_error:
        fxp_reply_error(rb, SSH_FX_BAD_MESSAGE, "Unable to decode request");
    }

    return reply;
}

static void default_reply_ok(SftpReplyBuilder *reply)
{
    DefaultSftpReplyBuilder *d =
        container_of(reply, DefaultSftpReplyBuilder, rb);
    d->pkt->type = SSH_FXP_STATUS;
    put_uint32(d->pkt, SSH_FX_OK);
    put_stringz(d->pkt, "");
}

static void default_reply_error(
    SftpReplyBuilder *reply, unsigned code, const char *msg)
{
    DefaultSftpReplyBuilder *d =
        container_of(reply, DefaultSftpReplyBuilder, rb);
    d->pkt->type = SSH_FXP_STATUS;
    put_uint32(d->pkt, code);
    put_stringz(d->pkt, msg);
}

static void default_reply_name_count(SftpReplyBuilder *reply, unsigned count)
{
    DefaultSftpReplyBuilder *d =
        container_of(reply, DefaultSftpReplyBuilder, rb);
    d->pkt->type = SSH_FXP_NAME;
    put_uint32(d->pkt, count);
}

static void default_reply_full_name(SftpReplyBuilder *reply, ptrlen name,
                                    ptrlen longname, struct fxp_attrs attrs)
{
    DefaultSftpReplyBuilder *d =
        container_of(reply, DefaultSftpReplyBuilder, rb);
    d->pkt->type = SSH_FXP_NAME;
    put_stringpl(d->pkt, name);
    put_stringpl(d->pkt, longname);
    put_fxp_attrs(d->pkt, attrs);
}

static void default_reply_simple_name(SftpReplyBuilder *reply, ptrlen name)
{
    fxp_reply_name_count(reply, 1);
    fxp_reply_full_name(reply, name, PTRLEN_LITERAL(""), no_attrs);
}

static void default_reply_handle(SftpReplyBuilder *reply, ptrlen handle)
{
    DefaultSftpReplyBuilder *d =
        container_of(reply, DefaultSftpReplyBuilder, rb);
    d->pkt->type = SSH_FXP_HANDLE;
    put_stringpl(d->pkt, handle);
}

static void default_reply_data(SftpReplyBuilder *reply, ptrlen data)
{
    DefaultSftpReplyBuilder *d =
        container_of(reply, DefaultSftpReplyBuilder, rb);
    d->pkt->type = SSH_FXP_DATA;
    put_stringpl(d->pkt, data);
}

static void default_reply_attrs(
    SftpReplyBuilder *reply, struct fxp_attrs attrs)
{
    DefaultSftpReplyBuilder *d =
        container_of(reply, DefaultSftpReplyBuilder, rb);
    d->pkt->type = SSH_FXP_ATTRS;
    put_fxp_attrs(d->pkt, attrs);
}

const struct SftpReplyBuilderVtable DefaultSftpReplyBuilder_vt = {
    default_reply_ok,
    default_reply_error,
    default_reply_simple_name,
    default_reply_name_count,
    default_reply_full_name,
    default_reply_handle,
    default_reply_data,
    default_reply_attrs,
};
