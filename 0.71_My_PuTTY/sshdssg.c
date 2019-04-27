/*
 * DSS key generation.
 */

#include "misc.h"
#include "ssh.h"
#include "mpint.h"

int dsa_generate(struct dss_key *key, int bits, progfn_t pfn,
		 void *pfnparam)
{
    /*
     * Set up the phase limits for the progress report. We do this
     * by passing minus the phase number.
     *
     * For prime generation: our initial filter finds things
     * coprime to everything below 2^16. Computing the product of
     * (p-1)/p for all prime p below 2^16 gives about 20.33; so
     * among B-bit integers, one in every 20.33 will get through
     * the initial filter to be a candidate prime.
     *
     * Meanwhile, we are searching for primes in the region of 2^B;
     * since pi(x) ~ x/log(x), when x is in the region of 2^B, the
     * prime density will be d/dx pi(x) ~ 1/log(B), i.e. about
     * 1/0.6931B. So the chance of any given candidate being prime
     * is 20.33/0.6931B, which is roughly 29.34 divided by B.
     *
     * So now we have this probability P, we're looking at an
     * exponential distribution with parameter P: we will manage in
     * one attempt with probability P, in two with probability
     * P(1-P), in three with probability P(1-P)^2, etc. The
     * probability that we have still not managed to find a prime
     * after N attempts is (1-P)^N.
     * 
     * We therefore inform the progress indicator of the number B
     * (29.34/B), so that it knows how much to increment by each
     * time. We do this in 16-bit fixed point, so 29.34 becomes
     * 0x1D.57C4.
     */
    pfn(pfnparam, PROGFN_PHASE_EXTENT, 1, 0x2800);
    pfn(pfnparam, PROGFN_EXP_PHASE, 1, -0x1D57C4 / 160);
    pfn(pfnparam, PROGFN_PHASE_EXTENT, 2, 0x40 * bits);
    pfn(pfnparam, PROGFN_EXP_PHASE, 2, -0x1D57C4 / bits);

    /*
     * In phase three we are finding an order-q element of the
     * multiplicative group of p, by finding an element whose order
     * is _divisible_ by q and raising it to the power of (p-1)/q.
     * _Most_ elements will have order divisible by q, since for a
     * start phi(p) of them will be primitive roots. So
     * realistically we don't need to set this much below 1 (64K).
     * Still, we'll set it to 1/2 (32K) to be on the safe side.
     */
    pfn(pfnparam, PROGFN_PHASE_EXTENT, 3, 0x2000);
    pfn(pfnparam, PROGFN_EXP_PHASE, 3, -32768);

    pfn(pfnparam, PROGFN_READY, 0, 0);

    unsigned pfirst, qfirst;
    invent_firstbits(&pfirst, &qfirst, 0);
    /*
     * Generate q: a prime of length 160.
     */
    mp_int *q = primegen(160, 2, 2, NULL, 1, pfn, pfnparam, qfirst);
    /*
     * Now generate p: a prime of length `bits', such that p-1 is
     * divisible by q.
     */
    mp_int *p = primegen(bits-160, 2, 2, q, 2, pfn, pfnparam, pfirst);

    /*
     * Next we need g. Raise 2 to the power (p-1)/q modulo p, and
     * if that comes out to one then try 3, then 4 and so on. As
     * soon as we hit a non-unit (and non-zero!) one, that'll do
     * for g.
     */
    mp_int *power = mp_div(p, q); /* this is floor(p/q) == (p-1)/q */
    mp_int *h = mp_from_integer(1);
    int progress = 0;
    mp_int *g;
    while (1) {
	pfn(pfnparam, PROGFN_PROGRESS, 3, ++progress);
	g = mp_modpow(h, power, p);
	if (mp_hs_integer(g, 2))
	    break;		       /* got one */
        mp_free(g);
        mp_add_integer_into(h, h, 1);
    }
    mp_free(h);
    mp_free(power);

    /*
     * Now we're nearly done. All we need now is our private key x,
     * which should be a number between 1 and q-1 exclusive, and
     * our public key y = g^x mod p.
     */
    mp_int *two = mp_from_integer(2);
    mp_int *qm1 = mp_copy(q);
    mp_sub_integer_into(qm1, qm1, 1);
    mp_int *x = mp_random_in_range(two, qm1);
    mp_free(two);
    mp_free(qm1);

    key->sshk.vt = &ssh_dss;

    key->p = p;
    key->q = q;
    key->g = g;
    key->x = x;
    key->y = mp_modpow(key->g, key->x, key->p);

    return 1;
}
