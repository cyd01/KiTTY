/*
 * mpunsafe.h: functions that deal with mp_ints in ways that are *not*
 * expected to be constant-time. Used during key generation, in which
 * constant run time is a lost cause anyway.
 *
 * These functions are in a separate header, so that you can easily
 * check that you're not calling them in the wrong context. They're
 * also defined in a separate source file, which is only linked in to
 * the key generation tools. Furthermore, that source file also
 * defines a global symbol that intentionally conflicts with one
 * defined in the SSH client code, so that any attempt to put these
 * functions into the same binary as the live SSH client
 * implementation will cause a link-time failure. They should only be
 * linked into PuTTYgen and auxiliary test programs.
 *
 * Also, just in case those precautions aren't enough, all the unsafe
 * functions have 'unsafe' in the name.
 */

#ifndef PUTTY_MPINT_UNSAFE_H
#define PUTTY_MPINT_UNSAFE_H

/*
 * The most obvious unsafe thing you want to do with an mp_int is to
 * get rid of leading zero words in its representation, so that its
 * nominal size is as close as possible to its true size, and you
 * don't waste any time processing it.
 *
 * mp_unsafe_shrink performs this operation in place, mutating the
 * size field of the mp_int it's given. It returns the same pointer it
 * was given.
 *
 * mp_unsafe_copy leaves the original mp_int alone and makes a new one
 * with the minimal size.
 */
mp_int *mp_unsafe_shrink(mp_int *m);
mp_int *mp_unsafe_copy(mp_int *m);

/*
 * Compute the residue of x mod m. This is implemented in the most
 * obvious way using the C % operator, which won't be constant-time on
 * many C implementations.
 */
uint32_t mp_unsafe_mod_integer(mp_int *x, uint32_t m);

#endif /* PUTTY_MPINT_UNSAFE_H */
