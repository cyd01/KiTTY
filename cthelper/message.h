#ifndef MESSAGE_H
#define MESSAGE_H

#if __STDC__VERSION >= 199901L
#include <stdint.h>
#elif defined(__INTERIX)
#include <sys/types.h>
#else
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
#endif

typedef struct message_tag Message;
struct message_tag {
  uint8_t size;
  uint8_t type;
  union {
    struct {
      uint16_t width, height;
    } resize;
  } msg;
};

/* message types */
#define MSG_RESIZE (21)

#define MESSAGE_MIN (2)
#define MESSAGE_MAX (sizeof(struct message_tag))

/* Fills in a Message structure from `data'.  Does not attempt to validate
 * the message type, but returns 0 if a partial message is received.
 * Returns -1 if the size is impossible (bigger than largest possible
 * message or smaller than smallest possible message).
 * Returns 1 if the message structure is filled in properly.
 */
int message_get(Message *m, const unsigned char *data, size_t len);

#endif /* MESSAGE_H */
