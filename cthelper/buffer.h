#ifndef BUFFER_H
#define BUFFER_H

#include <sys/types.h>
#include <alloca.h>

typedef struct buffer_tag *Buffer;
struct buffer_tag {
  size_t avail, len;
  unsigned char data[1];
};

/* Initialize a Buffer */
Buffer buffer_init(size_t size);

/* Free a buffer; sets *pb to NULL */
void buffer_free(Buffer *pb);

/* Initialize a Buffer with alloca() */
#define BUFFER_ALLOCA(b,s) do{\
    b = alloca(sizeof(b)+(s)); b->avail = s; b->len = 0;\
  }while(0)

/* Returns true if buffer is full */
#define buffer_isfull(b) ((b)->avail == 0)

/* Returns true if buffer is empty */
#define buffer_isempty(b) ((b)->len == 0)

/* Reads up to buffer_avail(b) bytes from descriptor `des' */
ssize_t buffer_read(Buffer b, int des);

/* Writes as many bytes in the buffer as possible to descriptor `des' */
ssize_t buffer_write(Buffer b, int des);

/* Appends data from data to end of buffer returning number of bytes
 * appended (which may be less than `len') */
size_t buffer_append(Buffer b, const char *data, size_t len);

/* Clears len bytes from start of buffer, returning number of bytes removed */
size_t buffer_consumed(Buffer b, size_t len);

#endif /* BUFFER_H */
