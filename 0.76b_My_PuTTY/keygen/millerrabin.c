/*
 * millerrabin.c: Miller-Rabin probabilistic primality testing, as
 * declared in sshkeygen.h.
 */

#include <assert.h>
#include "ssh.h"
#include "sshkeygen.h"
#include "mpint.h"
#include "mpunsafe.h"

/*
 * The Miller-Rabin primality test is an extension to the Fermat
 * test. The Fermat test just checks that a^(p-1) == 1 mod p; this
 * is vulnerable to Carmichael numbers. Miller-Rabin considers how
 * that 1 is derived as well.
 *
 * Lemma: if a^2 == 1 (mod p), and p is prime, then either a == 1
 * or a == -1 (mod p).
 *
 *   Proof: p divides a^2-1, i.e. p divides (a+1)(a-1). Hence,
 *   since p is prime, either p divides (a+1) or p divides (a-1).
 *   But this is the same as saying that either a is congruent to
 *   -1 mod p or a is congruent to +1 mod p. []
 *
 *   Comment: This fails when p is not prime. Consider p=mn, so
 *   that mn divides (a+1)(a-1). Now we could have m dividing (a+1)
 *   and n dividing (a-1), without the whole of mn dividing either.
 *   For example, consider a=10 and p=99. 99 = 9 * 11; 9 divides
 *   10-1 and 11 divides 10+1, so a^2 is congruent to 1 mod p
 *   without a having to be congruent to either 1 or -1.
 *
 * So the Miller-Rabin test, as well as considering a^(p-1),
 * considers a^((p-1)/2), a^((p-1)/4), and so on as far as it can
 * go. In other words. we write p-1 as q * 2^k, with k as large as
 * possible (i.e. q must be odd), and we consider the powers
 *
 *       a^(q*2^0)      a^(q*2^1)          ...  a^(q*2^(k-1))  a^(q*2^k)
 * i.e.  a^((n-1)/2^k)  a^((n-1)/2^(k-1))  ...  a^((n-1)/2)    a^(n-1)
 *
 * If p is to be prime, the last of these must be 1. Therefore, by
 * the above lemma, the one before it must be either 1 or -1. And
 * _if_ it's 1, then the one before that must be either 1 or -1,
 * and so on ... In other words, we expect to see a trailing chain
 * of 1s preceded by a -1. (If we're unlucky, our trailing chain of
 * 1s will be as long as the list so we'll never get to see what
 * lies before it. This doesn't count as a test failure because it
 * hasn't _proved_ that p is not prime.)
 *
 * For example, consider a=2 and p=1729. 1729 is a Carmichael
 * number: although it's not prime, it satisfies a^(p-1) == 1 mod p
 * for any a coprime to it. So the Fermat test wouldn't have a
 * problem with it at all, unless we happened to stumble on an a
 * which had a common factor.
 *
 * So. 1729 - 1 equals 27 * 2^6. So we look at
 *
 *     2^27 mod 1729 == 645
 *    2^108 mod 1729 == 1065
 *    2^216 mod 1729 == 1
 *    2^432 mod 1729 == 1
 *    2^864 mod 1729 == 1
 *   2^1728 mod 1729 == 1
 *
 * We do have a trailing string of 1s, so the Fermat test would
 * have been happy. But this trailing string of 1s is preceded by
 * 1065; whereas if 1729 were prime, we'd expect to see it preceded
 * by -1 (i.e. 1728.). Guards! Seize this impostor.
 *
 * (If we were unlucky, we might have tried a=16 instead of a=2;
 * now 16^27 mod 1729 == 1, so we would have seen a long string of
 * 1s and wouldn't have seen the thing _before_ the 1s. So, just
 * like the Fermat test, for a given p there may well exist values
 * of a which fail to show up its compositeness. So we try several,
 * just like the Fermat test. The difference is that Miller-Rabin
 * is not _in general_ fooled by Carmichael numbers.)
 *
 * Put simply, then, the Miller-Rabin test requires us to:
 *
 *  1. write p-1 as q * 2^k, with q odd
 *  2. compute z = (a^q) mod p.
 *  3. report success if z == 1 or z == -1.
 *  4. square z at most k-1 times, and report success if it becomes
 *     -1 at any point.
 *  5. report failure otherwise.
 *
 * (We expect z to become -1 after at most k-1 squarings, because
 * if it became -1 after k squarings then a^(p-1) would fail to be
 * 1. And we don't need to investigate what happens after we see a
 * -1, because we _know_ that -1 squared is 1 modulo anything at
 * all, so after we've seen a -1 we can be sure of seeing nothing
 * but 1s.)
 */

struct MillerRabin {
    MontyContext *mc;

    size_t k;
    mp_int *q;

    mp_int *two, *pm1, *m_pm1;
};

MillerRabin *miller_rabin_new(mp_int *p)
{
    MillerRabin *mr = snew(MillerRabin);

    assert(mp_hs_integer(p, 2));
    assert(mp_get_bit(p, 0) == 1);

    mr->k = 1;
    while (!mp_get_bit(p, mr->k))
        mr->k++;
    mr->q = mp_rshift_safe(p, mr->k);

    mr->two = mp_from_integer(2);

    mr->pm1 = mp_unsafe_copy(p);
    mp_sub_integer_into(mr->pm1, mr->pm1, 1);

    mr->mc = monty_new(p);
    mr->m_pm1 = monty_import(mr->mc, mr->pm1);

    return mr;
}

void miller_rabin_free(MillerRabin *mr)
{
    mp_free(mr->q);
    mp_free(mr->two);
    mp_free(mr->pm1);
    mp_free(mr->m_pm1);
    monty_free(mr->mc);
    smemclr(mr, sizeof(*mr));
    sfree(mr);
}

struct mr_result {
    bool passed;
    bool potential_primitive_root;
};

static struct mr_result miller_rabin_test_inner(MillerRabin *mr, mp_int *w)
{
    /*
     * Compute w^q mod p.
     */
    mp_int *wqp = monty_pow(mr->mc, w, mr->q);

    /*
     * See if this is 1, or if it is -1, or if it becomes -1
     * when squared at most k-1 times.
     */
    struct mr_result result;
    result.passed = false;
    result.potential_primitive_root = false;

    if (mp_cmp_eq(wqp, monty_identity(mr->mc))) {
        result.passed = true;
    } else {
        for (size_t i = 0; i < mr->k; i++) {
            if (mp_cmp_eq(wqp, mr->m_pm1)) {
                result.passed = true;
                result.potential_primitive_root = (i == mr->k - 1);
                break;
            }
            if (i == mr->k - 1)
                break;
            monty_mul_into(mr->mc, wqp, wqp, wqp);
        }
    }

    mp_free(wqp);

    return result;
}

bool miller_rabin_test_random(MillerRabin *mr)
{
    mp_int *mw = mp_random_in_range(mr->two, mr->pm1);
    struct mr_result result = miller_rabin_test_inner(mr, mw);
    mp_free(mw);
    return result.passed;
}

mp_int *miller_rabin_find_potential_primitive_root(MillerRabin *mr)
{
    while (true) {
        mp_int *mw = mp_unsafe_shrink(mp_random_in_range(mr->two, mr->pm1));
        struct mr_result result = miller_rabin_test_inner(mr, mw);

        if (result.passed && result.potential_primitive_root) {
            mp_int *pr = monty_export(mr->mc, mw);
            mp_free(mw);
            return pr;
        }

        mp_free(mw);

        if (!result.passed) {
            return NULL;
        }
    }
}

unsigned miller_rabin_checks_needed(unsigned bits)
{
    /* Table 4.4 from Handbook of Applied Cryptography */
    return (bits >= 1300 ?  2 : bits >= 850 ?  3 : bits >= 650 ?  4 :
            bits >=  550 ?  5 : bits >= 450 ?  6 : bits >= 400 ?  7 :
            bits >=  350 ?  8 : bits >= 300 ?  9 : bits >= 250 ? 12 :
            bits >=  200 ? 15 : bits >= 150 ? 18 : 27);
}

