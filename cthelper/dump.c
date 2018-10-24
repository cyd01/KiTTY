/* Code stolen from putty/misc.c */

#include <string.h>
#include "dbug.h"

void
debug_memdump(const void *buf, int len, int L)
{
  int i;
  unsigned const char *p = (unsigned const char*)buf;
  char foo[17];
  if (L) {
    int delta;
    fprintf(DBUG_FILE,"\t%d (0x%x) bytes:\n", len, len);
    delta = 15 &(int)p;
    p -= delta;
    len += delta;
  }
  for (; 0 < len; p += 16, len -= 16) {
    fputs("  ", DBUG_FILE);
    if (L)
      fprintf(DBUG_FILE,"%p: ", p);
    strcpy(foo, "................"); /* sixteen dots */
    for (i = 0; i < 16 && i < len; ++i) {
      if (&p[i] < (unsigned char *) buf) {
        fputs("   ", DBUG_FILE); /* 3 spaces */
        foo[i] = ' ';
      }
      else {
        fprintf(DBUG_FILE,"%c%02x",
          &p[i] != (unsigned char *) buf
          && i % 4 ? '.' : ' ', p[i]);
        if (p[i] >= ' ' && p[i] <= '~')
          foo[i] = (char) p[i];
      }
    }
    foo[i] = '\0';
    fprintf(DBUG_FILE,"%*s%s\n", (16 - i) * 3 + 2, "", foo);
  }
}
