/*
 * psftpcommon.c: front-end functionality shared between both file
 * transfer tools across platforms. (As opposed to sftpcommon.c, which
 * has *protocol*-level common code.)
 */

#include <stdlib.h>
#include <string.h>

#include "putty.h"
#include "sftp.h"
#include "psftp.h"

#define MAX_NAMES_MEMORY ((size_t)8 << 20)

/*
 * qsort comparison routine for fxp_name structures. Sorts by real
 * file name.
 */
int sftp_name_compare(const void *av, const void *bv)
{
    const struct fxp_name *const *a = (const struct fxp_name *const *) av;
    const struct fxp_name *const *b = (const struct fxp_name *const *) bv;
    return strcmp((*a)->filename, (*b)->filename);
}

struct list_directory_from_sftp_ctx {
    size_t nnames, namesize, total_memory;
    struct fxp_name **names;
    bool sorting;
};

struct list_directory_from_sftp_ctx *list_directory_from_sftp_new(void)
{
    struct list_directory_from_sftp_ctx *ctx =
        snew(struct list_directory_from_sftp_ctx);
    memset(ctx, 0, sizeof(*ctx));
    ctx->sorting = true;
    return ctx;
}

void list_directory_from_sftp_free(struct list_directory_from_sftp_ctx *ctx)
{
    for (size_t i = 0; i < ctx->nnames; i++)
        fxp_free_name(ctx->names[i]);
    sfree(ctx->names);
    sfree(ctx);
}

void list_directory_from_sftp_feed(struct list_directory_from_sftp_ctx *ctx,
                                   struct fxp_name *name)
{
    if (ctx->sorting) {
        /*
         * Accumulate these filenames into an array that we'll sort -
         * unless the array gets _really_ big, in which case, to avoid
         * consuming all the client's memory, we fall back to
         * outputting the directory listing unsorted.
         */
        size_t this_name_memory =
            sizeof(*ctx->names) + sizeof(**ctx->names) +
            strlen(name->filename) +
            strlen(name->longname);

        if (MAX_NAMES_MEMORY - ctx->total_memory < this_name_memory) {
            list_directory_from_sftp_warn_unsorted();

            /* Output all the previously stored names. */
            for (size_t i = 0; i < ctx->nnames; i++) {
                list_directory_from_sftp_print(ctx->names[i]);
                fxp_free_name(ctx->names[i]);
            }

            /* Don't store further names in that array. */
            sfree(ctx->names);
            ctx->names = NULL;
            ctx->nnames = 0;
            ctx->namesize = 0;
            ctx->sorting = false;

            /* And don't forget to output the name passed in this
             * actual function call. */
            list_directory_from_sftp_print(name);
        } else {
            sgrowarray(ctx->names, ctx->namesize, ctx->nnames);
            ctx->names[ctx->nnames++] = fxp_dup_name(name);
            ctx->total_memory += this_name_memory;
        }
    } else {
        list_directory_from_sftp_print(name);
    }
}

void list_directory_from_sftp_finish(struct list_directory_from_sftp_ctx *ctx)
{
    if (ctx->nnames > 0) {
        assert(ctx->sorting);
        qsort(ctx->names, ctx->nnames, sizeof(*ctx->names), sftp_name_compare);
        for (size_t i = 0; i < ctx->nnames; i++)
            list_directory_from_sftp_print(ctx->names[i]);
    }
}
