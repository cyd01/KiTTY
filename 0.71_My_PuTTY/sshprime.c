/*
 * Prime generation.
 */

#include <assert.h>
#include "ssh.h"
#include "mpint.h"

/*
 * This prime generation algorithm is pretty much cribbed from
 * OpenSSL. The algorithm is:
 * 
 *  - invent a B-bit random number and ensure the top and bottom
 *    bits are set (so it's definitely B-bit, and it's definitely
 *    odd)
 * 
 *  - see if it's coprime to all primes below 2^16; increment it by
 *    two until it is (this shouldn't take long in general)
 * 
 *  - perform the Miller-Rabin primality test enough times to
 *    ensure the probability of it being composite is 2^-80 or
 *    less
 * 
 *  - go back to square one if any M-R test fails.
 */

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

static unsigned short primes[6542]; /* # primes < 65536 */
#define NPRIMES (lenof(primes))

static void init_primes_array(void)
{
    if (primes[0])
        return;                        /* already done */

    bool A[65536];

    for (size_t i = 2; i < lenof(A); i++)
        A[i] = true;

    for (size_t i = 2; i < lenof(A); i++) {
        if (!A[i])
            continue;
        for (size_t j = 2*i; j < lenof(A); j += i)
            A[j] = false;
    }

    size_t pos = 0;
    for (size_t i = 2; i < lenof(A); i++)
        if (A[i])
            primes[pos++] = i;

    assert(pos == NPRIMES);
}

static unsigned short mp_mod_short(mp_int *x, unsigned short modulus)
{
    /*
     * This function lives here rather than in mpint.c partly because
     * this is the only place it's needed, but mostly because it
     * doesn't pay careful attention to constant running time, since
     * as far as I can tell that's a lost cause for key generation
     * anyway.
     */
    unsigned accumulator = 0;
    for (size_t i = mp_max_bytes(x); i-- > 0 ;) {
        accumulator = 0x100 * accumulator + mp_get_byte(x, i);
        accumulator %= modulus;
    }
    return accumulator;
}

/*
 * Generate a prime. We can deal with various extra properties of
 * the prime:
 * 
 *  - to speed up use in RSA, we can arrange to select a prime with
 *    the property (prime % modulus) != residue.
 * 
 *  - for use in DSA, we can arrange to select a prime which is one
 *    more than a multiple of a dirty great bignum. In this case
 *    `bits' gives the size of the factor by which we _multiply_
 *    that bignum, rather than the size of the whole number.
 *
 *  - for the basically cosmetic purposes of generating keys of the
 *    length actually specified rather than off by one bit, we permit
 *    the caller to provide an unsigned integer 'firstbits' which will
 *    match the top few bits of the returned prime. (That is, there
 *    will exist some n such that (returnvalue >> n) == firstbits.) If
 *    'firstbits' is not needed, specifying it to either 0 or 1 is
 *    an adequate no-op.
 */
mp_int *primegen(
    int bits, int modulus, int residue, mp_int *factor,
    int phase, progfn_t pfn, void *pfnparam, unsigned firstbits)
{
    init_primes_array();

    int progress = 0;

    size_t fbsize = 0;
    while (firstbits >> fbsize)        /* work out how to align this */
        fbsize++;

  STARTOVER:

    pfn(pfnparam, PROGFN_PROGRESS, phase, ++progress);

    /*
     * Generate a k-bit random number with top and bottom bits set.
     * Alternatively, if `factor' is nonzero, generate a k-bit
     * random number with the top bit set and the bottom bit clear,
     * multiply it by `factor', and add one.
     */
    mp_int *p = mp_random_bits(bits - 1);

    mp_set_bit(p, 0, factor ? 0 : 1);  /* bottom bit */
    mp_set_bit(p, bits-1, 1);          /* top bit */
    for (size_t i = 0; i < fbsize; i++)
        mp_set_bit(p, bits-fbsize + i, 1 & (firstbits >> i));

    if (factor) {
	mp_int *tmp = p;
	p = mp_mul(tmp, factor);
	mp_free(tmp);
	assert(mp_get_bit(p, 0) == 0);
        mp_set_bit(p, 0, 1);
    }

    /*
     * We need to ensure this random number is coprime to the first
     * few primes, by repeatedly adding either 2 or 2*factor to it
     * until it is. To do this we make a list of (modulus, residue)
     * pairs to avoid, and we also add to that list the extra pair our
     * caller wants to avoid.
     */

    /* List the moduli */
    unsigned long moduli[NPRIMES + 1];
    for (size_t i = 0; i < NPRIMES; i++)
	moduli[i] = primes[i];
    moduli[NPRIMES] = modulus;

    /* Find the residue of our starting number mod each of them. Also
     * set up the multipliers array which tells us how each one will
     * change when we increment the number (which isn't just 1 if
     * we're incrementing by multiples of factor). */
    unsigned long residues[NPRIMES + 1], multipliers[NPRIMES + 1];
    for (size_t i = 0; i < lenof(moduli); i++) {
	residues[i] = mp_mod_short(p, moduli[i]);
	if (factor)
	    multipliers[i] = mp_mod_short(factor, moduli[i]);
	else
	    multipliers[i] = 1;
    }

    /* Adjust the last entry so that it avoids a residue other than zero */
    residues[NPRIMES] = (residues[NPRIMES] + modulus - residue) % modulus;

    /*
     * Now loop until no residue in that list is zero, to find a
     * sensible increment. We maintain the increment in an ordinary
     * integer, so if it gets too big, we'll have to give up and go
     * back to making up a fresh random large integer.
     */
    unsigned delta = 0;
    while (1) {
	for (size_t i = 0; i < lenof(moduli); i++)
	    if (!((residues[i] + delta * multipliers[i]) % moduli[i]))
		goto found_a_zero;

        /* If we didn't exit that loop by goto, we've got our candidate. */
        break;

      found_a_zero:
        delta += 2;
        if (delta > 65536) {
            mp_free(p);
            goto STARTOVER;
        }
    }

    /*
     * Having found a plausible increment, actually add it on.
     */
    if (factor) {
	mp_int *d = mp_from_integer(delta);
        mp_int *df = mp_mul(d, factor);
        mp_add_into(p, p, df);
        mp_free(d);
        mp_free(df);
    } else {
        mp_add_integer_into(p, p, delta);
    }

    /*
     * Now apply the Miller-Rabin primality test a few times. First
     * work out how many checks are needed.
     */
    unsigned checks =
        bits >= 1300 ?  2 : bits >= 850 ?  3 : bits >= 650 ?  4 :
        bits >=  550 ?  5 : bits >= 450 ?  6 : bits >= 400 ?  7 :
        bits >=  350 ?  8 : bits >= 300 ?  9 : bits >= 250 ? 12 :
        bits >=  200 ? 15 : bits >= 150 ? 18 : 27;

    /*
     * Next, write p-1 as q*2^k.
     */
    size_t k;
    for (k = 0; mp_get_bit(p, k) == !k; k++)
	continue;	/* find first 1 bit in p-1 */
    mp_int *q = mp_rshift_safe(p, k);

    /*
     * Set up stuff for the Miller-Rabin checks.
     */
    mp_int *two = mp_from_integer(2);
    mp_int *pm1 = mp_copy(p);
    mp_sub_integer_into(pm1, pm1, 1);
    MontyContext *mc = monty_new(p);
    mp_int *m_pm1 = monty_import(mc, pm1);

    bool known_bad = false;

    /*
     * Now, for each check ...
     */
    for (unsigned check = 0; check < checks && !known_bad; check++) {
	/*
	 * Invent a random number between 1 and p-1.
	 */
        mp_int *w = mp_random_in_range(two, pm1);
        monty_import_into(mc, w, w);

	pfn(pfnparam, PROGFN_PROGRESS, phase, ++progress);

	/*
	 * Compute w^q mod p.
	 */
	mp_int *wqp = monty_pow(mc, w, q);
	mp_free(w);

	/*
	 * See if this is 1, or if it is -1, or if it becomes -1
	 * when squared at most k-1 times.
	 */
        bool passed = false;

	if (mp_cmp_eq(wqp, monty_identity(mc)) || mp_cmp_eq(wqp, m_pm1)) {
            passed = true;
        } else {
            for (size_t i = 0; i < k - 1; i++) {
                monty_mul_into(mc, wqp, wqp, wqp);
                if (mp_cmp_eq(wqp, m_pm1)) {
                    passed = true;
                    break;
                }
            }
	}

        if (!passed)
            known_bad = true;

	mp_free(wqp);
    }

    mp_free(q);
    mp_free(two);
    mp_free(pm1);
    monty_free(mc);
    mp_free(m_pm1);

    if (known_bad) {
        mp_free(p);
        goto STARTOVER;
    }

    /*
     * We have a prime!
     */
    return p;
}

/*
 * Invent a pair of values suitable for use as 'firstbits' in the
 * above function, such that their product is at least 2, and such
 * that their difference is also at least min_separation.
 *
 * This is used for generating both RSA and DSA keys which have
 * exactly the specified number of bits rather than one fewer - if you
 * generate an a-bit and a b-bit number completely at random and
 * multiply them together, you could end up with either an (ab-1)-bit
 * number or an (ab)-bit number. The former happens log(2)*2-1 of the
 * time (about 39%) and, though actually harmless, every time it
 * occurs it has a non-zero probability of sparking a user email along
 * the lines of 'Hey, I asked PuTTYgen for a 2048-bit key and I only
 * got 2047 bits! Bug!'
 */
static inline unsigned firstbits_b_min(
    unsigned a, unsigned lo, unsigned hi, unsigned min_separation)
{
    /* To get a large enough product, b must be at least this much */
    unsigned b_min = (lo*lo + a - 1) / a;
    /* Now enforce a<b, optionally with minimum separation */
    if (b_min < a + min_separation)
        b_min = a + min_separation;
    /* And cap at the upper limit */
    if (b_min > hi)
        b_min = hi;
    return b_min;
}

void invent_firstbits(unsigned *one, unsigned *two, unsigned min_separation)
{
    /*
     * We'll pick 12 initial bits (number selected at random) for each
     * prime, not counting the leading 1. So we want to return two
     * values in the range [2^12,2^13) whose product is at least 2^24.
     *
     * Strategy: count up all the viable pairs, then select a random
     * number in that range and use it to pick a pair.
     *
     * To keep things simple, we'll ensure a < b, and randomly swap
     * them at the end.
     */
    const unsigned lo = 1<<12, hi = 1<<13, minproduct = lo*lo;
    unsigned a, b;

    /*
     * Count up the number of prefixes of b that would be valid for
     * each prefix of a.
     */
    mp_int *total = mp_new(32);
    for (a = lo; a < hi; a++) {
        unsigned b_min = firstbits_b_min(a, lo, hi, min_separation);
        mp_add_integer_into(total, total, hi - b_min);
    }

    /*
     * Make up a random number in the range [0,2*total).
     */
    mp_int *mlo = mp_from_integer(0), *mhi = mp_new(32);
    mp_lshift_fixed_into(mhi, total, 1);
    mp_int *randval = mp_random_in_range(mlo, mhi);
    mp_free(mlo);
    mp_free(mhi);

    /*
     * Use the low bit of randval as our swap indicator, leaving the
     * rest of it in the range [0,total).
     */
    unsigned swap = mp_get_bit(randval, 0);
    mp_rshift_fixed_into(randval, randval, 1);

    /*
     * Now do the same counting loop again to make the actual choice.
     */
    a = b = 0;
    for (unsigned a_candidate = lo; a_candidate < hi; a_candidate++) {
        unsigned b_min = firstbits_b_min(a_candidate, lo, hi, min_separation);
        unsigned limit = hi - b_min;

        unsigned b_candidate = b_min + mp_get_integer(randval);
        unsigned use_it = 1 ^ mp_hs_integer(randval, limit);
        a ^= (a ^ a_candidate) & -use_it;
        b ^= (b ^ b_candidate) & -use_it;

        mp_sub_integer_into(randval, randval, limit);
    }

    mp_free(randval);
    mp_free(total);

    /*
     * Check everything came out right.
     */
    assert(lo <= a);
    assert(a < hi);
    assert(lo <= b);
    assert(b < hi);
    assert(a * b >= minproduct);
    assert(b >= a + min_separation);

    /*
     * Last-minute optional swap of a and b.
     */
    unsigned diff = (a ^ b) & (-swap);
    a ^= diff;
    b ^= diff;

    *one = a;
    *two = b;
}
