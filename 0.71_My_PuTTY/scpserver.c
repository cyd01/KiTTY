/*
 * Server side of the old-school SCP protocol.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "putty.h"
#include "ssh.h"
#include "sshcr.h"
#include "sshchan.h"
#include "sftp.h"

/*
 * I think it's worth actually documenting my understanding of what
 * this protocol _is_, since I don't know of any other documentation
 * of it anywhere.
 *
 * Format of data stream
 * ---------------------
 *
 * The sending side of an SCP connection - the client, if you're
 * uploading files, or the server if you're downloading - sends a data
 * stream consisting of a sequence of 'commands', or header records,
 * or whatever you want to call them, interleaved with file data.
 *
 * Each command starts with a letter indicating what type it is, and
 * ends with a \n.
 *
 * The 'C' command introduces an actual file. It is followed by an
 * octal file-permissions mask, then a space, then a decimal file
 * size, then a space, then the file name up to the termating newline.
 * For example, "C0644 12345 filename.txt\n" would be a plausible C
 * command.
 *
 * After the 'C' command, the sending side will transmit exactly as
 * many bytes of file data as specified by the size field in the
 * header line, followed by a single zero byte.
 *
 * The 'D' command introduces a subdirectory. Its format is identical
 * to 'C', including the size field, but the size field is sent as
 * zero.
 *
 * After the 'D' command, all subsequent C and D commands are taken to
 * indicate files that should be placed inside that subdirectory,
 * until a terminating 'E' command.
 *
 * The 'E' command indicates the end of a subdirectory. It has no
 * arguments at all (its format is always just "E\n"). After the E
 * command, the receiver should revert to placing further downloaded
 * files in whatever directory it was placing them before the
 * subdirectory opened by the just-closed D.
 *
 * D and E commands match like parentheses: if you send, say,
 *
 *    C0644 123 foo.txt       ( followed by data )
 *    D0755 0 subdir
 *    C0644 123 bar.txt       ( followed by data )
 *    D0755 0 subsubdir
 *    C0644 123 baz.txt       ( followed by data )
 *    E
 *    C0644 123 quux.txt      ( followed by data )
 *    E
 *    C0644 123 wibble.txt    ( followed by data )
 *
 * then foo.txt, subdir and wibble.txt go in the top-level destination
 * directory; bar.txt, subsubdir and quux.txt go in 'subdir'; and
 * baz.txt goes in 'subdir/subsubdir'.
 *
 * The sender terminates the data stream with EOF when it has no more
 * files to send. I believe it is not _required_ for all D to be
 * closed by an E before this happens - you can elide a trailing
 * sequence of E commands without provoking an error message from the
 * receiver.
 *
 * Finally, the 'T' command is sent immediately before a C or D. It is
 * followed by four space-separated decimal integers giving an mtime
 * and atime to be applied to the file or directory created by the
 * following C or D command. The first two integers give the mtime,
 * encoded as seconds and microseconds (respectively) since the Unix
 * epoch; the next two give the atime, encoded similarly. So
 * "T1540373455 0 1540373457 0\n" is an example of a valid T command.
 *
 * Acknowledgments
 * ---------------
 *
 * The sending side waits for an ack from the receiving side before
 * sending each command; before beginning to send the file data
 * following a C command; and before sending the final EOF.
 *
 * (In particular, the receiving side is expected to send an initial
 * ack before _anything_ is sent.)
 *
 * Normally an ack consists of a single zero byte. It's also allowable
 * to send a byte with value 1 or 2 followed by a \n-terminated error
 * message (where 1 means a non-fatal error and 2 means a fatal one).
 * I have to suppose that sending an error message from client to
 * server is of limited use, but apparently it's allowed.
 *
 * Initiation
 * ----------
 *
 * The protocol is begun by the client sending a command string to the
 * server via the SSH-2 "exec" request (or the analogous
 * SSH1_CMSG_EXEC_CMD), which indicates that this is an scp session
 * rather than any other thing; specifies the direction of transfer;
 * says what file(s) are to be sent by the server, or where the server
 * should put files that the client is about to send; and a couple of
 * other options.
 *
 * The command string takes the following form:
 *
 * Start with prefix "scp ", indicating that this is an SCP command at
 * all. Otherwise it's a request to run some completely different
 * command in the SSH session.
 *
 * Next the command can contain zero or more of the following options,
 * each followed by a space:
 *
 * "-v" turns on verbose server diagnostics. Of course a server is not
 * required to actually produce any, but this is an invitation for it
 * to send any it might have available. Diagnostics are free-form, and
 * sent as SSH standard-error extended data, so that they are separate
 * from the actual data stream as described above.
 *
 * (Servers can send standard-error output anyway if they like, and in
 * case of an actual error, they probably will with or without -v.)
 *
 * "-r" indicates recursive file transfer, i.e. potentially including
 * subdirectories. For a download, this indicates that the client is
 * willing to receive subdirectories (a D/E command pair bracketing
 * further files and subdirs); without it, the server should only send
 * C commands for individual files, followed by EOF.
 *
 * This flag must also be specified for a recursive upload, because I
 * believe at least one server will reject D/E pairs sent by the
 * client if the command didn't have -r in it. (Probably a consequence
 * of sharing code between download and upload.)
 *
 * "-p" means preserve file times. In a download, this requests the
 * server to send a T command before each C or D. I don't know whether
 * any server will insist on having seen this option from the client
 * before accepting T commands in an upload, but it is probably
 * sensible to send it anyway.
 *
 * "-d", in an upload, means that the destination pathname (see below)
 * is expected to be a directory, and that uploaded files (and
 * subdirs) should be put inside it. Without -d, the semantics are
 * that _if_ the destination exists and is a directory, then files
 * will be put in it, whereas if it is not, then just a single file
 * (or subdir) upload is expected, which will be placed at that exact
 * pathname.
 *
 * In a download, I observe that clients tend to send -d if they are
 * requesting multiple files or a wildcard, but as far as I know,
 * servers ignore it.
 *
 * After all those optional options, there is a single mandatory
 * option indicating the direction of transfer, which is either "-f"
 * or "-t". "-f" indicates a download; "-t" indicates an upload.
 *
 * After that mandatory option, there is a single space, followed by
 * the name(s) of files to transfer.
 *
 * This file name field is transmitted with NO QUOTING, in spite of
 * the fact that a server will typically interpret it as a shell
 * command. You'd think this couldn't possibly work, in the face of
 * almost any filename with an interesting character in it - and you'd
 * be right. Or rather (you might argue), it works 'as designed', but
 * it's designed in a weird way, in that it's the user's
 * responsibility to apply quoting on the client command line to get
 * the filename through the shell that will decode things on the
 * server side.
 *
 * But one effect of this is that if you issue a download command
 * including a wildcard, say "scp -f somedir/foo*.txt", then the shell
 * will expand the wildcard, and actually run the server-side scp
 * program with multiple arguments, say "somedir/foo.txt
 * somedir/quux.txt", leading to the download sending multiple C
 * commands. This clearly _is_ intended: it's how a command such as
 * 'scp server:somedir/foo*.txt destdir' can work at all.
 *
 * (You would think, given that, that it might also be legal to send
 * multiple space-separated filenames in order to trigger a download
 * of exactly those files. Given how scp is invoked in practice on a
 * typical server, this would surely actually work, but my observation
 * is that scp clients don't in fact try this - if you run OpenSSH's
 * scp by saying 'scp server:foo server:bar destdir' then it will make
 * two separate connections to the server for the two files, rather
 * than sending a single space-separated remote command. PSCP won't
 * even do that, and will make you do it in two separate runs.)
 *
 * So, some examples:
 *
 *  - "scp -f filename.txt"
 *
 *    Server should send a single C command (plus data) for that file.
 *    Client ought to ignore the filename in the C command, in favour
 *    of saving the file under the name implied by the user's command
 *    line.
 *
 *  - "scp -f file*.txt"
 *
 *    Server sends zero or more C commands, then EOF. Client will have
 *    been given a target directory to put them all in, and will name
 *    each one according to the name in the C command.
 *
 *    (You'd like the client to validate the filenames against the
 *    wildcard it sent, to ensure a malicious server didn't try to
 *    overwrite some path like ".bashrc" when you thought you were
 *    downloading only normal text files. But wildcard semantics are
 *    chosen by the server, so this is essentially hopeless to do
 *    rigorously.)
 *
 *  - "scp -f -r somedir"
 *
 *    Assuming somedir is actually a directory, server sends a D/E
 *    pair, in between which are the contents of the directory
 *    (perhaps including further nested D/E pairs). Client probably
 *    ignores the name field of the outermost D
 *
 *  - "scp -f -r some*wild*card*"
 *
 *    Server sends multiple C or D-stuff-E, one for each top-level
 *    thing matching the wildcard, whether it's a file or a directory.
 *
 *  - "scp -t -d some_dir"
 *
 *    Client sends stuff, and server deposits each file at
 *    some_dir/<name from the C command>.
 *
 *  - "scp -t some_path_name"
 *
 *    Client sends one C command, and server deposits it at
 *    some_path_name itself, or in some_path_name/<name from C
 *    command>, depending whether some_path_name was already a
 *    directory or not.
 */

/*
 * Here's a useful debugging aid: run over a binary file containing
 * the complete contents of the sender's data stream (e.g. extracted
 * by contrib/logparse.pl -d), it removes the file contents, leaving
 * only the list of commands, so you can see what the server sent.
 *
 *   perl -pe 'read ARGV,$x,1+$1 if/^C\S+ (\d+)/'
 */

/* ----------------------------------------------------------------------
 * Shared system for receiving replies from the SftpServer, and
 * putting them into a set of ordinary variables rather than
 * marshalling them into actual SFTP reply packets that we'd only have
 * to unmarshal again.
 */

typedef struct ScpReplyReceiver ScpReplyReceiver;
struct ScpReplyReceiver {
    bool err;
    unsigned code;
    char *errmsg;
    struct fxp_attrs attrs;
    ptrlen name, handle, data;

    SftpReplyBuilder srb;
};

static void scp_reply_ok(SftpReplyBuilder *srb)
{
    ScpReplyReceiver *reply = container_of(srb, ScpReplyReceiver, srb);
    reply->err = false;
}

static void scp_reply_error(
    SftpReplyBuilder *srb, unsigned code, const char *msg)
{
    ScpReplyReceiver *reply = container_of(srb, ScpReplyReceiver, srb);
    reply->err = true;
    reply->code = code;
    sfree(reply->errmsg);
    reply->errmsg = dupstr(msg);
}

static void scp_reply_name_count(SftpReplyBuilder *srb, unsigned count)
{
    ScpReplyReceiver *reply = container_of(srb, ScpReplyReceiver, srb);
    reply->err = false;
}

static void scp_reply_full_name(
    SftpReplyBuilder *srb, ptrlen name,
    ptrlen longname, struct fxp_attrs attrs)
{
    ScpReplyReceiver *reply = container_of(srb, ScpReplyReceiver, srb);
    char *p;
    reply->err = false;
    sfree((void *)reply->name.ptr);
    reply->name.ptr = p = mkstr(name);
    reply->name.len = name.len;
    reply->attrs = attrs;
}

static void scp_reply_simple_name(SftpReplyBuilder *srb, ptrlen name)
{
    ScpReplyReceiver *reply = container_of(srb, ScpReplyReceiver, srb);
    reply->err = false;
}

static void scp_reply_handle(SftpReplyBuilder *srb, ptrlen handle)
{
    ScpReplyReceiver *reply = container_of(srb, ScpReplyReceiver, srb);
    char *p;
    reply->err = false;
    sfree((void *)reply->handle.ptr);
    reply->handle.ptr = p = mkstr(handle);
    reply->handle.len = handle.len;
}

static void scp_reply_data(SftpReplyBuilder *srb, ptrlen data)
{
    ScpReplyReceiver *reply = container_of(srb, ScpReplyReceiver, srb);
    char *p;
    reply->err = false;
    sfree((void *)reply->data.ptr);
    reply->data.ptr = p = mkstr(data);
    reply->data.len = data.len;
}

static void scp_reply_attrs(
    SftpReplyBuilder *srb, struct fxp_attrs attrs)
{
    ScpReplyReceiver *reply = container_of(srb, ScpReplyReceiver, srb);
    reply->err = false;
    reply->attrs = attrs;
}

static const struct SftpReplyBuilderVtable ScpReplyReceiver_vt = {
    scp_reply_ok,
    scp_reply_error,
    scp_reply_simple_name,
    scp_reply_name_count,
    scp_reply_full_name,
    scp_reply_handle,
    scp_reply_data,
    scp_reply_attrs,
};

static void scp_reply_setup(ScpReplyReceiver *reply)
{
    memset(reply, 0, sizeof(*reply));
    reply->srb.vt = &ScpReplyReceiver_vt;
}

static void scp_reply_cleanup(ScpReplyReceiver *reply)
{
    sfree(reply->errmsg);
    sfree((void *)reply->name.ptr);
    sfree((void *)reply->handle.ptr);
    sfree((void *)reply->data.ptr);
}

/* ----------------------------------------------------------------------
 * Source end of the SCP protocol.
 */

#define SCP_MAX_BACKLOG 65536

typedef struct ScpSource ScpSource;
typedef struct ScpSourceStackEntry ScpSourceStackEntry;

struct ScpSource {
    SftpServer *sf;

    int acks;
    bool expect_newline, eof, throttled, finished;

    SshChannel *sc;
    ScpSourceStackEntry *head;
    bool recursive;
    bool send_file_times;

    strbuf *pending_commands[3];
    int n_pending_commands;

    uint64_t file_offset, file_size;

    ScpReplyReceiver reply;

    ScpServer scpserver;
};

typedef enum ScpSourceNodeType ScpSourceNodeType;
enum ScpSourceNodeType { SCP_ROOTPATH, SCP_NAME, SCP_READDIR, SCP_READFILE };

struct ScpSourceStackEntry {
    ScpSourceStackEntry *next;
    ScpSourceNodeType type;
    ptrlen pathname, handle;
    const char *wildcard;
    struct fxp_attrs attrs;
};

static void scp_source_push(ScpSource *scp, ScpSourceNodeType type,
                            ptrlen pathname, ptrlen handle,
                            const struct fxp_attrs *attrs, const char *wc)
{
    size_t wc_len = wc ? strlen(wc)+1 : 0;
    ScpSourceStackEntry *node = snew_plus(
        ScpSourceStackEntry, pathname.len + handle.len + wc_len);
    char *namebuf = snew_plus_get_aux(node);
    memcpy(namebuf, pathname.ptr, pathname.len);
    node->pathname = make_ptrlen(namebuf, pathname.len);
    memcpy(namebuf + pathname.len, handle.ptr, handle.len);
    node->handle = make_ptrlen(namebuf + pathname.len, handle.len);
    if (wc) {
        strcpy(namebuf + pathname.len + handle.len, wc);
        node->wildcard = namebuf + pathname.len + handle.len;
    } else {
        node->wildcard = NULL;
    }
    node->attrs = attrs ? *attrs : no_attrs;
    node->type = type;
    node->next = scp->head;
    scp->head = node;
}

static char *scp_source_err_base(ScpSource *scp, const char *fmt, va_list ap)
{
    char *msg = dupvprintf(fmt, ap);
    sshfwd_write_ext(scp->sc, true, msg, strlen(msg));
    sshfwd_write_ext(scp->sc, true, "\012", 1);
    return msg;
}
static void scp_source_err(ScpSource *scp, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    sfree(scp_source_err_base(scp, fmt, ap));
    va_end(ap);
}
static void scp_source_abort(ScpSource *scp, const char *fmt, ...)
{
    va_list ap;
    char *msg;

    va_start(ap, fmt);
    msg = scp_source_err_base(scp, fmt, ap);
    va_end(ap);

    sshfwd_send_exit_status(scp->sc, 1);
    sshfwd_write_eof(scp->sc);
    sshfwd_initiate_close(scp->sc, msg);

    scp->finished = true;
}

static void scp_source_push_name(
    ScpSource *scp, ptrlen pathname, struct fxp_attrs attrs, const char *wc)
{
    if (!(attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS)) {        
        scp_source_err(scp, "unable to read file permissions for %.*s",
                       PTRLEN_PRINTF(pathname));
        return;
    }
    if (attrs.permissions & PERMS_DIRECTORY) {
        if (!scp->recursive && !wc) {
            scp_source_err(scp, "%.*s: is a directory",
                           PTRLEN_PRINTF(pathname));
            return;
        }
    } else {
        if (!(attrs.flags & SSH_FILEXFER_ATTR_SIZE)) {
            scp_source_err(scp, "unable to read file size for %.*s",
                           PTRLEN_PRINTF(pathname));
            return;
        }
    }

    scp_source_push(scp, SCP_NAME, pathname, PTRLEN_LITERAL(""), &attrs, wc);
}

static void scp_source_free(ScpServer *s);
static size_t scp_source_send(ScpServer *s, const void *data, size_t length);
static void scp_source_eof(ScpServer *s);
static void scp_source_throttle(ScpServer *s, bool throttled);

static struct ScpServerVtable ScpSource_ScpServer_vt = {
    scp_source_free,
    scp_source_send,
    scp_source_throttle,
    scp_source_eof,
};

static ScpSource *scp_source_new(
    SshChannel *sc, const SftpServerVtable *sftpserver_vt, ptrlen pathname)
{
    ScpSource *scp = snew(ScpSource);
    memset(scp, 0, sizeof(*scp));

    scp->scpserver.vt = &ScpSource_ScpServer_vt;
    scp_reply_setup(&scp->reply);
    scp->sc = sc;
    scp->sf = sftpsrv_new(sftpserver_vt);
    scp->n_pending_commands = 0;

    scp_source_push(scp, SCP_ROOTPATH, pathname, PTRLEN_LITERAL(""),
                    NULL, NULL);

    return scp;
}

static void scp_source_free(ScpServer *s)
{
    ScpSource *scp = container_of(s, ScpSource, scpserver);
    scp_reply_cleanup(&scp->reply);
    while (scp->n_pending_commands > 0)
        strbuf_free(scp->pending_commands[--scp->n_pending_commands]);
    while (scp->head) {
        ScpSourceStackEntry *node = scp->head;
        scp->head = node->next;
        sfree(node);
    }
    sfree(scp);
}

static void scp_source_send_E(ScpSource *scp)
{
    strbuf *cmd;

    assert(scp->n_pending_commands == 0);

    scp->pending_commands[scp->n_pending_commands++] = cmd = strbuf_new();
    strbuf_catf(cmd, "E\012");
}

static void scp_source_send_CD(
    ScpSource *scp, char cmdchar,
    struct fxp_attrs attrs, uint64_t size, ptrlen name)
{
    strbuf *cmd;

    assert(scp->n_pending_commands == 0);

    if (scp->send_file_times && (attrs.flags & SSH_FILEXFER_ATTR_ACMODTIME)) {
        scp->pending_commands[scp->n_pending_commands++] = cmd = strbuf_new();
        /* Our SFTP-based filesystem API doesn't support microsecond times */
        strbuf_catf(cmd, "T%lu 0 %lu 0\012", attrs.mtime, attrs.atime);
    }

    const char *slash;
    while ((slash = memchr(name.ptr, '/', name.len)) != NULL)
        name = make_ptrlen(
            slash+1, name.len - (slash+1 - (const char *)name.ptr));
 
    scp->pending_commands[scp->n_pending_commands++] = cmd = strbuf_new();
    strbuf_catf(cmd, "%c%04o %"PRIu64" %.*s\012", cmdchar,
                (unsigned)(attrs.permissions & 07777),
                size, PTRLEN_PRINTF(name));

    if (cmdchar == 'C') {
        /* We'll also wait for an ack before sending the file data,
         * which we record by saving a zero-length 'command' to be
         * sent after the C. */
        scp->pending_commands[scp->n_pending_commands++] = cmd = strbuf_new();
    }
}

static void scp_source_process_stack(ScpSource *scp);
static void scp_source_process_stack_cb(void *vscp)
{
    ScpSource *scp = (ScpSource *)vscp;
    if (scp->finished)
        return;                        /* this callback is out of date */
    scp_source_process_stack(scp);
}
static void scp_requeue(ScpSource *scp)
{
    queue_toplevel_callback(scp_source_process_stack_cb, scp);
}

static void scp_source_process_stack(ScpSource *scp)
{
    if (scp->throttled)
        return;

    while (scp->n_pending_commands > 0) {
        /* Expect an ack, and consume it */
        if (scp->eof) {
            scp_source_abort(
                scp, "scp: received client EOF, abandoning transfer");
            return;
        }
        if (scp->acks == 0)
            return;
        scp->acks--;

        /*
         * Now send the actual command (unless it was the phony
         * zero-length one that indicates our need for an ack before
         * beginning to send file data).
         */

        if (scp->pending_commands[0]->len)
            sshfwd_write(scp->sc, scp->pending_commands[0]->s,
                         scp->pending_commands[0]->len);

        strbuf_free(scp->pending_commands[0]);
        scp->n_pending_commands--;
        if (scp->n_pending_commands > 0) {
            /*
             * We still have at least one pending command to send, so
             * move up the queue.
             *
             * (We do that with a bodgy memmove, because there are at
             * most a bounded number of commands ever pending at once,
             * so no need to worry about quadratic time.)
             */
            memmove(scp->pending_commands, scp->pending_commands+1,
                    scp->n_pending_commands * sizeof(*scp->pending_commands));
        }
    }

    /*
     * Mostly, we start by waiting for an ack byte from the receiver.
     */
    if (scp->head && scp->head->type == SCP_READFILE && scp->file_offset) {
        /*
         * Exception: if we're already in the middle of transferring a
         * file, we'll be called back here because the channel backlog
         * has cleared; we don't need to wait for an ack.
         */
    } else if (scp->head && scp->head->type == SCP_ROOTPATH) {
        /*
         * Another exception: the initial action node that makes us
         * stat the root path. We'll translate it into an SCP_NAME,
         * and _that_ will require an ack.
         */
        ScpSourceStackEntry *node = scp->head;
        scp->head = node->next;

        /*
         * Start by checking if there's a wildcard involved in the
         * root path.
         */
        char *rootpath_str = mkstr(node->pathname);
        char *rootpath_unesc = snewn(1+node->pathname.len, char);
        ptrlen pathname;
        const char *wildcard;

        if (wc_unescape(rootpath_unesc, rootpath_str)) {
            /*
             * We successfully removed instances of the escape
             * character used in our wildcard syntax, without
             * encountering any actual wildcard chars - i.e. this is
             * not a wildcard, just a single file. The simple case.
             */
            pathname = ptrlen_from_asciz(rootpath_str);
            wildcard = NULL;
        } else {
            /*
             * This is a wildcard. Separate it into a directory name
             * (which we enforce mustn't contain wc characters, for
             * simplicity) and a wildcard to match leaf names.
             */
            char *last_slash = strrchr(rootpath_str, '/');

            if (last_slash) {
                wildcard = last_slash + 1;
                *last_slash = '\0';
                if (!wc_unescape(rootpath_unesc, rootpath_str)) {
                    scp_source_abort(scp, "scp: wildcards in path components "
                                     "before the file name not supported");
                    sfree(rootpath_str);
                    sfree(rootpath_unesc);
                    return;
                }

                pathname = ptrlen_from_asciz(rootpath_unesc);
            } else {
                pathname = PTRLEN_LITERAL(".");
                wildcard = rootpath_str;
            }
        }

        /*
         * Now we know what directory we're scanning, and what
         * wildcard (if any) we're using to match the filenames we get
         * back.
         */
        sftpsrv_stat(scp->sf, &scp->reply.srb, pathname, true);
        if (scp->reply.err) {
            scp_source_abort(
                scp, "%.*s: unable to access: %s",
                PTRLEN_PRINTF(pathname), scp->reply.errmsg);
            sfree(rootpath_str);
            sfree(rootpath_unesc);
            sfree(node);
            return;
        }

        scp_source_push_name(scp, pathname, scp->reply.attrs, wildcard);

        sfree(rootpath_str);
        sfree(rootpath_unesc);
        sfree(node);
        scp_requeue(scp);
        return;
    } else {
    }

    if (scp->head && scp->head->type == SCP_READFILE) {
        /*
         * Transfer file data if our backlog hasn't filled up.
         */
        int backlog;
        uint64_t limit = scp->file_size - scp->file_offset;
        if (limit > 4096)
            limit = 4096;
        if (limit > 0) {
            sftpsrv_read(scp->sf, &scp->reply.srb, scp->head->handle,
                         scp->file_offset, limit);
            if (scp->reply.err) {
                scp_source_abort(
                    scp, "%.*s: unable to read: %s",
                    PTRLEN_PRINTF(scp->head->pathname), scp->reply.errmsg);
                return;
            }

            backlog = sshfwd_write(
                scp->sc, scp->reply.data.ptr, scp->reply.data.len);
            scp->file_offset += scp->reply.data.len;

            if (backlog < SCP_MAX_BACKLOG)
                scp_requeue(scp);
            return;
        }

        /*
         * If we're done, send a terminating zero byte, close our file
         * handle, and pop the stack.
         */
        sshfwd_write(scp->sc, "\0", 1);
        sftpsrv_close(scp->sf, &scp->reply.srb, scp->head->handle);
        ScpSourceStackEntry *node = scp->head;
        scp->head = node->next;
        sfree(node);
        scp_requeue(scp);
        return;
    }

    /*
     * If our queue is actually empty, send outgoing EOF.
     */
    if (!scp->head) {
        sshfwd_send_exit_status(scp->sc, 0);
        sshfwd_write_eof(scp->sc);
        sshfwd_initiate_close(scp->sc, NULL);
        scp->finished = true;
        return;
    }

    /*
     * Otherwise, handle a command.
     */
    ScpSourceStackEntry *node = scp->head;
    scp->head = node->next;

    if (node->type == SCP_READDIR) {
        sftpsrv_readdir(scp->sf, &scp->reply.srb, node->handle, 1, true);
        if (scp->reply.err) {
            if (scp->reply.code != SSH_FX_EOF)
                scp_source_err(scp, "%.*s: unable to list directory: %s",
                               PTRLEN_PRINTF(node->pathname),
                               scp->reply.errmsg);
            sftpsrv_close(scp->sf, &scp->reply.srb, node->handle);

            if (!node->wildcard) {
                /*
                 * Send 'pop stack' or 'end of directory' command,
                 * unless this was the topmost READDIR in a
                 * wildcard-based retrieval (in which case we didn't
                 * send a D command to start, so an E now would have
                 * no stack entry to pop).
                 */
                scp_source_send_E(scp);
            }
        } else if (ptrlen_eq_string(scp->reply.name, ".") ||
                   ptrlen_eq_string(scp->reply.name, "..") ||
                   (node->wildcard &&
                    !wc_match_pl(node->wildcard, scp->reply.name))) {
            /* Skip special directory names . and .., and anything
             * that doesn't match our wildcard (if we have one). */
            scp->head = node;     /* put back the unfinished READDIR */
            node = NULL;          /* and prevent it being freed */
        } else {
            ptrlen subpath;
            subpath.len = node->pathname.len + 1 + scp->reply.name.len;
            char *subpath_space = snewn(subpath.len, char);
            subpath.ptr = subpath_space;
            memcpy(subpath_space, node->pathname.ptr, node->pathname.len);
            subpath_space[node->pathname.len] = '/';
            memcpy(subpath_space + node->pathname.len + 1,
                   scp->reply.name.ptr, scp->reply.name.len);

            scp->head = node;     /* put back the unfinished READDIR */
            node = NULL;          /* and prevent it being freed */
            scp_source_push_name(scp, subpath, scp->reply.attrs, NULL);

            sfree(subpath_space);
        }
    } else if (node->attrs.permissions & PERMS_DIRECTORY) {
        assert(scp->recursive || node->wildcard);

        if (!node->wildcard)
            scp_source_send_CD(scp, 'D', node->attrs, 0, node->pathname);
        sftpsrv_opendir(scp->sf, &scp->reply.srb, node->pathname);
        if (scp->reply.err) {
            scp_source_err(
                scp, "%.*s: unable to access: %s",
                PTRLEN_PRINTF(node->pathname), scp->reply.errmsg);

            if (!node->wildcard) {
                /* Send 'pop stack' or 'end of directory' command. */
                scp_source_send_E(scp);
            }
        } else {
            scp_source_push(
                scp, SCP_READDIR, node->pathname,
                scp->reply.handle, NULL, node->wildcard);
        }
    } else {
        sftpsrv_open(scp->sf, &scp->reply.srb,
                     node->pathname, SSH_FXF_READ, no_attrs);
        if (scp->reply.err) {
            scp_source_err(
                scp, "%.*s: unable to open: %s",
                PTRLEN_PRINTF(node->pathname), scp->reply.errmsg);
            scp_requeue(scp);
            return;
        }
        sftpsrv_fstat(scp->sf, &scp->reply.srb, scp->reply.handle);
        if (scp->reply.err) {
            scp_source_err(
                scp, "%.*s: unable to stat: %s",
                PTRLEN_PRINTF(node->pathname), scp->reply.errmsg);
            sftpsrv_close(scp->sf, &scp->reply.srb, scp->reply.handle);
            scp_requeue(scp);
            return;
        }
        scp->file_offset = 0;
        scp->file_size = scp->reply.attrs.size;
        scp_source_send_CD(scp, 'C', node->attrs,
                           scp->file_size, node->pathname);
        scp_source_push(
            scp, SCP_READFILE, node->pathname, scp->reply.handle, NULL, NULL);
    }
    sfree(node);
    scp_requeue(scp);
}

static size_t scp_source_send(ScpServer *s, const void *vdata, size_t length)
{
    ScpSource *scp = container_of(s, ScpSource, scpserver);
    const char *data = (const char *)vdata;
    size_t i;

    if (scp->finished)
        return 0;

    for (i = 0; i < length; i++) {
        if (scp->expect_newline) {
            if (data[i] == '\012') {
                /* End of an error message following a 1 byte */
                scp->expect_newline = false;
                scp->acks++;
            }
        } else {
            switch (data[i]) {
              case 0:                  /* ordinary ack */
                scp->acks++;
                break;
              case 1:                  /* non-fatal error; consume it */
                scp->expect_newline = true;
                break;
              case 2:
                scp_source_abort(
                    scp, "terminating on fatal error from client");
                return 0;
              default:
                scp_source_abort(
                    scp, "unrecognised response code from client");
                return 0;
            }
        }
    }

    scp_source_process_stack(scp);

    return 0;
}

static void scp_source_throttle(ScpServer *s, bool throttled)
{
    ScpSource *scp = container_of(s, ScpSource, scpserver);

    if (scp->finished)
        return;

    scp->throttled = throttled;
    if (!throttled)
        scp_source_process_stack(scp);
}

static void scp_source_eof(ScpServer *s)
{
    ScpSource *scp = container_of(s, ScpSource, scpserver);

    if (scp->finished)
        return;

    scp->eof = true;
    scp_source_process_stack(scp);
}

/* ----------------------------------------------------------------------
 * Sink end of the SCP protocol.
 */

typedef struct ScpSink ScpSink;
typedef struct ScpSinkStackEntry ScpSinkStackEntry;

struct ScpSink {
    SftpServer *sf;

    SshChannel *sc;
    ScpSinkStackEntry *head;

    uint64_t file_offset, file_size;
    unsigned long atime, mtime;
    bool got_file_times;

    bufchain data;
    bool input_eof;
    strbuf *command;
    char command_chr;

    strbuf *filename_sb;
    ptrlen filename;
    struct fxp_attrs attrs;

    char *errmsg;

    int crState;

    ScpReplyReceiver reply;

    ScpServer scpserver;
};

struct ScpSinkStackEntry {
    ScpSinkStackEntry *next;
    ptrlen destpath;

    /*
     * If isdir is true, then destpath identifies a directory that the
     * files we receive should be created inside. If it's false, then
     * it identifies the exact pathname the next file we receive
     * should be created _as_ - regardless of the filename in the 'C'
     * command.
     */
    bool isdir;
};

static void scp_sink_push(ScpSink *scp, ptrlen pathname, bool isdir)
{
    ScpSinkStackEntry *node = snew_plus(ScpSinkStackEntry, pathname.len);
    char *p = snew_plus_get_aux(node);

    node->destpath.ptr = p;
    node->destpath.len = pathname.len;
    memcpy(p, pathname.ptr, pathname.len);
    node->isdir = isdir;

    node->next = scp->head;
    scp->head = node;
}

static void scp_sink_pop(ScpSink *scp)
{
    ScpSinkStackEntry *node = scp->head;
    scp->head = node->next;
    sfree(node);
}

static void scp_sink_free(ScpServer *s);
static size_t scp_sink_send(ScpServer *s, const void *data, size_t length);
static void scp_sink_eof(ScpServer *s);
static void scp_sink_throttle(ScpServer *s, bool throttled) {}

static struct ScpServerVtable ScpSink_ScpServer_vt = {
    scp_sink_free,
    scp_sink_send,
    scp_sink_throttle,
    scp_sink_eof,
};

static void scp_sink_coroutine(ScpSink *scp);
static void scp_sink_start_callback(void *vscp)
{
    scp_sink_coroutine((ScpSink *)vscp);
}

static ScpSink *scp_sink_new(
    SshChannel *sc, const SftpServerVtable *sftpserver_vt, ptrlen pathname,
    bool pathname_is_definitely_dir)
{
    ScpSink *scp = snew(ScpSink);
    memset(scp, 0, sizeof(*scp));

    scp->scpserver.vt = &ScpSink_ScpServer_vt;
    scp_reply_setup(&scp->reply);
    scp->sc = sc;
    scp->sf = sftpsrv_new(sftpserver_vt);
    bufchain_init(&scp->data);
    scp->command = strbuf_new();
    scp->filename_sb = strbuf_new();

    if (!pathname_is_definitely_dir) {
        /*
         * If our root pathname is not already expected to be a
         * directory because of the -d option in the command line,
         * test it ourself to see whether it is or not.
         */
        sftpsrv_stat(scp->sf, &scp->reply.srb, pathname, true);
        if (!scp->reply.err &&
            (scp->reply.attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS) &&
            (scp->reply.attrs.permissions & PERMS_DIRECTORY))
            pathname_is_definitely_dir = true;
    }
    scp_sink_push(scp, pathname, pathname_is_definitely_dir);

    queue_toplevel_callback(scp_sink_start_callback, scp);

    return scp;
}

static void scp_sink_free(ScpServer *s)
{
    ScpSink *scp = container_of(s, ScpSink, scpserver);

    scp_reply_cleanup(&scp->reply);
    bufchain_clear(&scp->data);
    strbuf_free(scp->command);
    strbuf_free(scp->filename_sb);
    while (scp->head)
        scp_sink_pop(scp);
    sfree(scp->errmsg);

    delete_callbacks_for_context(scp);

    sfree(scp);
}

static void scp_sink_coroutine(ScpSink *scp)
{
    crBegin(scp->crState);

    while (1) {
        /*
         * Send an ack, and read a command.
         */
        sshfwd_write(scp->sc, "\0", 1);
        scp->command->len = 0;
        while (1) {
            crMaybeWaitUntilV(scp->input_eof || bufchain_size(&scp->data) > 0);
            if (scp->input_eof)
                goto done;

            ptrlen data = bufchain_prefix(&scp->data);
            const char *cdata = data.ptr;
            const char *newline = memchr(cdata, '\012', data.len);
            if (newline)
                data.len = (int)(newline+1 - cdata);
            put_data(scp->command, cdata, data.len);
            bufchain_consume(&scp->data, data.len);

            if (newline)
                break;
        }

        /*
         * Parse the command.
         */
        scp->command->len--;           /* chomp the newline */
        scp->command_chr = scp->command->len > 0 ? scp->command->s[0] : '\0';
        if (scp->command_chr == 'T') {
            unsigned long dummy1, dummy2;
            if (sscanf(scp->command->s, "T%lu %lu %lu %lu",
                       &scp->mtime, &dummy1, &scp->atime, &dummy2) != 4)
                goto parse_error;
            scp->got_file_times = true;
        } else if (scp->command_chr == 'C' || scp->command_chr == 'D') {
            /*
             * Common handling of the start of this case, because the
             * messages are parsed similarly. We diverge later.
             */
            const char *q, *p = scp->command->s + 1; /* skip the 'C' */

            scp->attrs.flags = SSH_FILEXFER_ATTR_PERMISSIONS;
            scp->attrs.permissions = 0;
            while (*p >= '0' && *p <= '7') {
                scp->attrs.permissions =
                    scp->attrs.permissions * 8 + (*p - '0');
                p++;
            }
            if (*p != ' ')
                goto parse_error;
            p++;

            q = p;
            while (*p >= '0' && *p <= '9')
                p++;
            if (*p != ' ')
                goto parse_error;
            p++;
            scp->file_size = strtoull(q, NULL, 10);

            ptrlen leafname = make_ptrlen(
                p, scp->command->len - (p - scp->command->s));
            scp->filename_sb->len = 0;
            put_datapl(scp->filename_sb, scp->head->destpath);
            if (scp->head->isdir) {
                if (scp->filename_sb->len > 0 &&
                    scp->filename_sb->s[scp->filename_sb->len-1]
                    != '/')
                    put_byte(scp->filename_sb, '/');
                put_datapl(scp->filename_sb, leafname);
            }
            scp->filename = ptrlen_from_strbuf(scp->filename_sb);

            if (scp->got_file_times) {
                scp->attrs.mtime = scp->mtime;
                scp->attrs.atime = scp->atime;
                scp->attrs.flags |= SSH_FILEXFER_ATTR_ACMODTIME;
            }
            scp->got_file_times = false;

            if (scp->command_chr == 'D') {
                sftpsrv_mkdir(scp->sf, &scp->reply.srb,
                              scp->filename, scp->attrs);

                if (scp->reply.err) {
                    scp->errmsg = dupprintf(
                        "'%.*s': unable to create directory: %s",
                        PTRLEN_PRINTF(scp->filename), scp->reply.errmsg);
                    goto done;
                }

                scp_sink_push(scp, scp->filename, true);
            } else {
                sftpsrv_open(scp->sf, &scp->reply.srb, scp->filename,
                             SSH_FXF_WRITE | SSH_FXF_CREAT | SSH_FXF_TRUNC,
                             scp->attrs);
                if (scp->reply.err) {
                    scp->errmsg = dupprintf(
                        "'%.*s': unable to open file: %s",
                        PTRLEN_PRINTF(scp->filename), scp->reply.errmsg);
                    goto done;
                }

                /*
                 * Now send an ack, and read the file data.
                 */
                sshfwd_write(scp->sc, "\0", 1);
                scp->file_offset = 0;
                while (scp->file_offset < scp->file_size) {
                    ptrlen data;
                    uint64_t this_len, remaining;

                    crMaybeWaitUntilV(
                        scp->input_eof || bufchain_size(&scp->data) > 0);
                    if (scp->input_eof) {
                        sftpsrv_close(scp->sf, &scp->reply.srb,
                                      scp->reply.handle);
                        goto done;
                    }

                    data = bufchain_prefix(&scp->data);
                    this_len = data.len;
                    remaining = scp->file_size - scp->file_offset;
                    if (this_len > remaining)
                        this_len = remaining;
                    sftpsrv_write(scp->sf, &scp->reply.srb,
                                  scp->reply.handle, scp->file_offset,
                                  make_ptrlen(data.ptr, this_len));
                    if (scp->reply.err) {
                        scp->errmsg = dupprintf(
                            "'%.*s': unable to write to file: %s",
                            PTRLEN_PRINTF(scp->filename), scp->reply.errmsg);
                        goto done;
                    }
                    bufchain_consume(&scp->data, this_len);
                    scp->file_offset += this_len;
                }

                /*
                 * Wait for the trailing NUL byte.
                 */
                crMaybeWaitUntilV(
                    scp->input_eof || bufchain_size(&scp->data) > 0);
                if (scp->input_eof) {
                    sftpsrv_close(scp->sf, &scp->reply.srb,
                                  scp->reply.handle);
                    goto done;
                }
                bufchain_consume(&scp->data, 1);
            }
        } else if (scp->command_chr == 'E') {
            if (!scp->head) {
                scp->errmsg = dupstr("received E command without matching D");
                goto done;
            }
            scp_sink_pop(scp);
            scp->got_file_times = false;
        } else {
            ptrlen cmd_pl;

            /*
             * Also come here if any of the above cases run into
             * parsing difficulties.
             */
          parse_error:
            cmd_pl = ptrlen_from_strbuf(scp->command);
            scp->errmsg = dupprintf("unrecognised scp command '%.*s'",
                                    PTRLEN_PRINTF(cmd_pl));
            goto done;
        }
    }

  done:
    if (scp->errmsg) {
        sshfwd_write_ext(scp->sc, true, scp->errmsg, strlen(scp->errmsg));
        sshfwd_write_ext(scp->sc, true, "\012", 1);
        sshfwd_send_exit_status(scp->sc, 1);
    } else {
        sshfwd_send_exit_status(scp->sc, 0);
    }
    sshfwd_write_eof(scp->sc);
    sshfwd_initiate_close(scp->sc, scp->errmsg);
    while (1) crReturnV;

    crFinishV;
}

static size_t scp_sink_send(ScpServer *s, const void *data, size_t length)
{
    ScpSink *scp = container_of(s, ScpSink, scpserver);

    if (!scp->input_eof) {
        bufchain_add(&scp->data, data, length);
        scp_sink_coroutine(scp);
    }
    return 0;
}

static void scp_sink_eof(ScpServer *s)
{
    ScpSink *scp = container_of(s, ScpSink, scpserver);

    scp->input_eof = true;
    scp_sink_coroutine(scp);
}

/* ----------------------------------------------------------------------
 * Top-level error handler, instantiated in the case where the user
 * sent a command starting with "scp " that we couldn't make sense of.
 */

typedef struct ScpError ScpError;

struct ScpError {
    SshChannel *sc;
    char *message;
    ScpServer scpserver;
};

static void scp_error_free(ScpServer *s);

static size_t scp_error_send(ScpServer *s, const void *data, size_t length)
{ return 0; }
static void scp_error_eof(ScpServer *s) {}
static void scp_error_throttle(ScpServer *s, bool throttled) {}

static struct ScpServerVtable ScpError_ScpServer_vt = {
    scp_error_free,
    scp_error_send,
    scp_error_throttle,
    scp_error_eof,
};

static void scp_error_send_message_cb(void *vscp)
{
    ScpError *scp = (ScpError *)vscp;
    sshfwd_write_ext(scp->sc, true, scp->message, strlen(scp->message));
    sshfwd_write_ext(scp->sc, true, "\n", 1);
    sshfwd_send_exit_status(scp->sc, 1);
    sshfwd_write_eof(scp->sc);
    sshfwd_initiate_close(scp->sc, scp->message);
}

static ScpError *scp_error_new(SshChannel *sc, const char *fmt, ...)
{
    va_list ap;
    ScpError *scp = snew(ScpError);

    memset(scp, 0, sizeof(*scp));

    scp->scpserver.vt = &ScpError_ScpServer_vt;
    scp->sc = sc;

    va_start(ap, fmt);
    scp->message = dupvprintf(fmt, ap);
    va_end(ap);

    queue_toplevel_callback(scp_error_send_message_cb, scp);

    return scp;
}

static void scp_error_free(ScpServer *s)
{
    ScpError *scp = container_of(s, ScpError, scpserver);

    sfree(scp->message);

    delete_callbacks_for_context(scp);

    sfree(scp);
}

/* ----------------------------------------------------------------------
 * Top-level entry point, which parses a command sent from the SSH
 * client, and if it recognises it as an scp command, instantiates an
 * appropriate ScpServer implementation and returns it.
 */

ScpServer *scp_recognise_exec(
    SshChannel *sc, const SftpServerVtable *sftpserver_vt, ptrlen command)
{
    bool recursive = false, preserve = false;
    bool targetshouldbedirectory = false;
    ptrlen command_orig = command;

    if (!ptrlen_startswith(command, PTRLEN_LITERAL("scp "), &command))
        return NULL;

    while (1) {
        if (ptrlen_startswith(command, PTRLEN_LITERAL("-v "), &command)) {
            /* Enable verbose mode in the server, which we ignore */
            continue;
        }
        if (ptrlen_startswith(command, PTRLEN_LITERAL("-r "), &command)) {
            recursive = true;
            continue;
        }
        if (ptrlen_startswith(command, PTRLEN_LITERAL("-p "), &command)) {
            preserve = true;
            continue;
        }
        if (ptrlen_startswith(command, PTRLEN_LITERAL("-d "), &command)) {
            targetshouldbedirectory = true;
            continue;
        }
        break;
    }

    if (ptrlen_startswith(command, PTRLEN_LITERAL("-t "), &command)) {
        ScpSink *scp = scp_sink_new(sc, sftpserver_vt, command,
                                    targetshouldbedirectory);
        return &scp->scpserver;
    } else if (ptrlen_startswith(command, PTRLEN_LITERAL("-f "), &command)) {
        ScpSource *scp = scp_source_new(sc, sftpserver_vt, command);
        scp->recursive = recursive;
        scp->send_file_times = preserve;
        return &scp->scpserver;
    } else {
        ScpError *scp = scp_error_new(
            sc, "Unable to parse scp command: '%.*s'",
            PTRLEN_PRINTF(command_orig));
        return &scp->scpserver;
    }
}
