/*
 * uxgen.c: Unix implementation of get_heavy_noise() from cmdgen.c.
 */

#include <stdio.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>

#include "putty.h"

char *get_random_data(int len, const char *device)
{
    char *buf = snewn(len, char);
    int fd;
    int ngot, ret;

    if (!device)
        device = "/dev/random";

    fd = open(device, O_RDONLY);
    if (fd < 0) {
	sfree(buf);
	fprintf(stderr, "puttygen: %s: open: %s\n",
                device, strerror(errno));
	return NULL;
    }

    ngot = 0;
    while (ngot < len) {
	ret = read(fd, buf+ngot, len-ngot);
	if (ret < 0) {
	    close(fd);
            sfree(buf);
            fprintf(stderr, "puttygen: %s: read: %s\n",
                    device, strerror(errno));
	    return NULL;
	}
	ngot += ret;
    }

    close(fd);

    return buf;
}
