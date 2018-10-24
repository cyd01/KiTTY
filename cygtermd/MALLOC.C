/*
 * malloc.c: implementation of malloc.h
 */

#include <stdlib.h>
#include <string.h>

#include "malloc.h"

extern void fatal(const char *, ...);

void *smalloc(size_t size) {
    void *p;
    p = malloc(size);
    if (!p) {
	fatal("out of memory");
    }
    return p;
}

void sfree(void *p) {
    if (p) {
	free(p);
    }
}

void *srealloc(void *p, size_t size) {
    void *q;
    if (p) {
	q = realloc(p, size);
    } else {
	q = malloc(size);
    }
    if (!q)
	fatal("out of memory");
    return q;
}

char *dupstr(const char *s) {
    char *r = smalloc(1+strlen(s));
    strcpy(r,s);
    return r;
}
