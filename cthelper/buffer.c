#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>

#include "buffer.h"
#include "debug.h"

Buffer
buffer_init(size_t size)
{
  Buffer b;

  DBUG_ENTER("buffer_init");
  assert((b = (Buffer)malloc(sizeof(struct buffer_tag) + size)));
  b->len = 0;
  b->avail = size;
  DBUG_RETURN(b);
}

void
buffer_free(Buffer *pb)
{
  DBUG_ENTER("buffer_free");
  assert(pb);
  if (*pb) free(*pb);
  *pb = 0;
  DBUG_VOID_RETURN;
}

ssize_t
buffer_read(Buffer b, int d)
{
  ssize_t n, total;
  DBUG_ENTER("buffer_read");
  total = 0;
  n = -1;
  DBUG_PRINT("buffer", ("reading %d: %p:%u", d, &b->data[b->len], b->avail));
  if (b->avail > 0 && (n = read(d, &b->data[b->len], b->avail)) > 0) {
    DBUG_PRINT("io", (" read %4d", n));
    b->len += n;
    b->avail -= n;
    total += n;
  }
  DBUG_PRINT("buffer", ("total %4d", total));
  DBUG_RETURN(total ? total : n);
}

ssize_t
buffer_write(Buffer b, int d)
{
  ssize_t n, total;
  DBUG_ENTER("buffer_write");
  total = 0;
  n = -1;
  DBUG_PRINT("buffer", ("writing %d: %p:%u", d, b->data, b->len));
  if (b->len > 0 && (n = write(d, b->data, b->len)) > 0) {
    DBUG_PRINT("io", ("wrote %4d", n));
    buffer_consumed(b, n);
    total += n;
  }
  DBUG_PRINT("buffer", ("total %4d", total));
  DBUG_RETURN(total ? total : n);
}

size_t
buffer_consumed(Buffer b, size_t n)
{
  DBUG_ENTER("buffer_consumed");
  assert(0 < n && n <= b->len);
  memmove(b->data, &b->data[n], b->len - n);
  b->len -= n;
  b->avail += n;
  DBUG_RETURN(n);
}
