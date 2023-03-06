/*
 * Work around lack of strtoumax in older MSVC libraries.
 */

#include <stdlib.h>

#include "defs.h"

uintmax_t strtoumax(const char *nptr, char **endptr, int base)
{
    return _strtoui64(nptr, endptr, base);
}
