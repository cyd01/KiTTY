#ifndef _mm_h_
#define _mm_H_

#include <stdlib.h>

#ifndef DBUG_OFF
void *dbug_malloc (ssize_t len, const char *, int);
char *dbug_strdup (const char *s, const char *, int);
void *dbug_calloc (ssize_t nmemb, ssize_t len, const char *, int);
void *dbug_realloc (void *ptr, ssize_t len);
void dbug_free (void *ptr);
void dbug_dump_mm (void);
#endif

#ifndef DBUG_OFF
#undef strdup
#define malloc(x) dbug_malloc(x,__FILE__,__LINE__)
#define realloc(x,y) dbug_realloc(x,y)
#define free(x) dbug_free(x)
#define strdup(x) dbug_strdup(x,__FILE__,__LINE__)
#define calloc(x,y) dbug_calloc(x,y,__FILE__,__LINE__)
/*#else
#define dbug_malloc(x) malloc(x)
#define dbug_strdup(x) strdup(x)
#define dbug_calloc(x,y) calloc(x,y)
#define dbug_free(x) free(x)*/
#endif /* DBUG_OFF */
#endif /* _mm_h_ */
