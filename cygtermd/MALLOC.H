/*
 * malloc.h: safe wrappers around malloc, realloc, free, strdup
 */

#ifndef UMLWRAP_MALLOC_H
#define UMLWRAP_MALLOC_H

#include <stddef.h>

/*
 * smalloc should guarantee to return a useful pointer - Halibut
 * can do nothing except die when it's out of memory anyway.
 */
void *smalloc(size_t size);

/*
 * srealloc should guaranteeably be able to realloc NULL
 */
void *srealloc(void *p, size_t size);

/*
 * sfree should guaranteeably deal gracefully with freeing NULL
 */
void sfree(void *p);

/*
 * dupstr is like strdup, but with the never-return-NULL property
 * of smalloc (and also reliably defined in all environments :-)
 */
char *dupstr(const char *s);

/*
 * snew allocates one instance of a given type, and casts the
 * result so as to type-check that you're assigning it to the
 * right kind of pointer. Protects against allocation bugs
 * involving allocating the wrong size of thing.
 */
#define snew(type) \
    ( (type *) smalloc (sizeof (type)) )

/*
 * snewn allocates n instances of a given type, for arrays.
 */
#define snewn(number, type) \
    ( (type *) smalloc ((number) * sizeof (type)) )

/*
 * sresize wraps realloc so that you specify the new number of
 * elements and the type of the element, with the same type-
 * checking advantages. Also type-checks the input pointer.
 */
#define sresize(array, number, type) \
    ( (void)sizeof((array)-(type *)0), \
      (type *) srealloc ((array), (number) * sizeof (type)) )

#endif /* UMLWRAP_MALLOC_H */
