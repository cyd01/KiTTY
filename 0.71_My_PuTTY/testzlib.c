/*
 * Main program to compile sshzlib.c into a zlib decoding tool.
 *
 * This is potentially a handy tool in its own right for picking apart
 * Zip files or PDFs or PNGs, because it accepts the bare Deflate
 * format and the zlib wrapper format, unlike 'zcat' which accepts
 * only the gzip wrapper format.
 *
 * It's also useful as a means for a fuzzer to get reasonably direct
 * access to PuTTY's zlib decompressor.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "defs.h"
#include "ssh.h"

void out_of_memory(void)
{
    fprintf(stderr, "Out of memory!\n");
    exit(1);
}

int main(int argc, char **argv)
{
    unsigned char buf[16], *outbuf;
    int ret, outlen;
    ssh_decompressor *handle;
    int noheader = false, opts = true;
    char *filename = NULL;
    FILE *fp;

    while (--argc) {
        char *p = *++argv;

        if (p[0] == '-' && opts) {
            if (!strcmp(p, "-d")) {
                noheader = true;
            } else if (!strcmp(p, "--")) {
                opts = false;          /* next thing is filename */
            } else if (!strcmp(p, "--help")) {
                printf("usage: testzlib          decode zlib (RFC1950) data"
                       " from standard input\n");
                printf("       testzlib -d       decode Deflate (RFC1951) data"
                       " from standard input\n");
                printf("       testzlib --help   display this text\n");
                return 0;
            } else {
                fprintf(stderr, "unknown command line option '%s'\n", p);
                return 1;
            }
        } else if (!filename) {
            filename = p;
        } else {
            fprintf(stderr, "can only handle one filename\n");
            return 1;
        }
    }

    handle = ssh_decompressor_new(&ssh_zlib);

    if (noheader) {
        /*
         * Provide missing zlib header if -d was specified.
         */
        static const unsigned char ersatz_zlib_header[] = { 0x78, 0x9C };
        ssh_decompressor_decompress(
            handle, ersatz_zlib_header, sizeof(ersatz_zlib_header),
            &outbuf, &outlen);
        assert(outlen == 0);
    }

    if (filename)
        fp = fopen(filename, "rb");
    else
        fp = stdin;

    if (!fp) {
        assert(filename);
        fprintf(stderr, "unable to open '%s'\n", filename);
        return 1;
    }

    while (1) {
	ret = fread(buf, 1, sizeof(buf), fp);
	if (ret <= 0)
	    break;
	ssh_decompressor_decompress(handle, buf, ret, &outbuf, &outlen);
        if (outbuf) {
            if (outlen)
                fwrite(outbuf, 1, outlen, stdout);
            sfree(outbuf);
        } else {
            fprintf(stderr, "decoding error\n");
            fclose(fp);
            return 1;
        }
    }

    ssh_decompressor_free(handle);

    if (filename)
        fclose(fp);

    return 0;
}
