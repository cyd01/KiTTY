#include <string.h>

#include "message.h"
#include "debug.h"

int
message_get(Message *m, const unsigned char *data, size_t len)
{
  int size;

  DBUG_ENTER("message_get");

  if (len < MESSAGE_MIN)
    /* partial message */
    DBUG_RETURN(0);

  /* we at least have the message header */
  size = data[0];

  if (MESSAGE_MIN > size || size > MESSAGE_MAX)
    /* bad message */
    DBUG_RETURN(-1);

  if (size > len)
    /* partial message */
    DBUG_RETURN(0);

  memcpy(m, data, size);

  DBUG_RETURN(1);
}
