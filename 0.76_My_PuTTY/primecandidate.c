/*
 * primecandidate.c: implementation of the PrimeCandidateSource
 * abstraction declared in sshkeygen.h.
 */

#include <assert.h>
#include "ssh.h"
#include "mpint.h"
#include "mpunsafe.h"
#include "sshkeygen.h"

struct avoid {
    unsigned mod, res;
};

struct PrimeCandidateSource {
    unsigned bits;
    bool ready, try_sophie_germain;
    bool one_shot, thrown_away_my_shot;

    /* We'll start by making up a random number strictly less than this ... */
    mp_int *limit;

    /* ... then we'll multiply by 'factor', and add 'addend'. */
    mp_int *factor, *addend;

    /* Then we'll try to add a small multiple of 'factor' to it to
     * avoid it being a multiple of any small prime. Also, for RSA, we
     * may need to avoid it being _this_ multiple of _this_: */
    unsigned avoid_residue, avoid_modulus;

    /* Once we're actually running, this will be the complete list of
     * (modulus, residue) pairs we want to avoid. */
    struct avoid *avoids;
    size_t navoids, avoidsize;

    /* List of known primes that our number will be congruent to 1 modulo */
    mp_int **kps;
    size_t nkps, kpsize;
};

PrimeCandidateSource *pcs_new_with_firstbits(unsigned bits,
                                             unsigned first, unsigned nfirst)
{
    PrimeCandidateSource *s = snew(PrimeCandidateSource);

    assert(first >> (nfirst-1) == 1);

    s->bits = bits;
    s->ready = false;
    s->try_sophie_germain = false;
    s->one_shot = false;
    s->thrown_away_my_shot = false;

    s->kps = NULL;
    s->nkps = s->kpsize = 0;

    s->avoids = NULL;
    s->navoids = s->avoidsize = 0;

    /* Make the number that's the lower limit of our range */
    mp_int *firstmp = mp_from_integer(first);
    mp_int *base = mp_lshift_fixed(firstmp, bits - nfirst);
    mp_free(firstmp);

    /* Set the low bit of that, because all (nontrivial) primes are odd */
    mp_set_bit(base, 0, 1);

    /* That's our addend. Now initialise factor to 2, to ensure we
     * only generate odd numbers */
    s->factor = mp_from_integer(2);
    s->addend = base;

    /* And that means the limit of our random numbers must be one
     * factor of two _less_ than the position of the low bit of
     * 'first', because we'll be multiplying the random number by
     * 2 immediately afterwards. */
    s->limit = mp_power_2(bits - nfirst - 1);

    /* avoid_modulus == 0 signals that there's no extra residue to avoid */
    s->avoid_residue = 1;
    s->avoid_modulus = 0;

    return s;
}

PrimeCandidateSource *pcs_new(unsigned bits)
{
    return pcs_new_with_firstbits(bits, 1, 1);
}

void pcs_free(PrimeCandidateSource *s)
{
    mp_free(s->limit);
    mp_free(s->factor);
    mp_free(s->addend);
    for (size_t i = 0; i < s->nkps; i++)
        mp_free(s->kps[i]);
    sfree(s->avoids);
    sfree(s->kps);
    sfree(s);
}

void pcs_try_sophie_germain(PrimeCandidateSource *s)
{
    s->try_sophie_germain = true;
}

void pcs_set_oneshot(PrimeCandidateSource *s)
{
    s->one_shot = true;
}

static void pcs_require_residue_inner(PrimeCandidateSource *s,
                                      mp_int *mod, mp_int *res)
{
    /*
     * We already have a factor and addend. Ensure this one doesn't
     * contradict it.
     */
    mp_int *gcd = mp_gcd(mod, s->factor);
    mp_int *test1 = mp_mod(s->addend, gcd);
    mp_int *test2 = mp_mod(res, gcd);
    assert(mp_cmp_eq(test1, test2));
    mp_free(test1);
    mp_free(test2);

    /*
     * Reduce our input factor and addend, which are constraints on
     * the ultimate output number, so that they're constraints on the
     * initial cofactor we're going to make up.
     *
     * If we're generating x and we want to ensure ax+b == r (mod m),
     * how does that work? We've already checked that b == r modulo g
     * = gcd(a,m), i.e. r-b is a multiple of g, and so are a and m. So
     * let's write a=gA, m=gM, (r-b)=gR, and then we can start by
     * dividing that off:
     *
     *      ax == r-b (mod m )
     * =>  gAx == gR  (mod gM)
     * =>   Ax ==  R  (mod  M)
     *
     * Now the moduli A,M are coprime, which makes things easier.
     *
     * We're going to need to generate the x in this equation by
     * generating a new smaller value y, multiplying it by M, and
     * adding some constant K. So we have x = My + K, and we need to
     * work out what K will satisfy the above equation. In other
     * words, we need A(My+K) == R (mod M), and the AMy term vanishes,
     * so we just need AK == R (mod M). So our congruence is solved by
     * setting K to be R * A^{-1} mod M.
     */
    mp_int *A = mp_div(s->factor, gcd);
    mp_int *M = mp_div(mod, gcd);
    mp_int *Rpre = mp_modsub(res, s->addend, mod);
    mp_int *R = mp_div(Rpre, gcd);
    mp_int *Ainv = mp_invert(A, M);
    mp_int *K = mp_modmul(R, Ainv, M);

    mp_free(gcd);
    mp_free(Rpre);
    mp_free(Ainv);
    mp_free(A);
    mp_free(R);

    /*
     * So we know we have to transform our existing (factor, addend)
     * pair into (factor * M, addend * factor * K). Now we just need
     * to work out what the limit should be on the random value we're
     * generating.
     *
     * If we need My+K < old_limit, then y < (old_limit-K)/M. But the
     * RHS is a fraction, so in integers, we need y < ceil of it.
     */
    assert(!mp_cmp_hs(K, s->limit));
    mp_int *dividend = mp_add(s->limit, M);
    mp_sub_integer_into(dividend, dividend, 1);
    mp_sub_into(dividend, dividend, K);
    mp_free(s->limit);
    s->limit = mp_div(dividend, M);
    mp_free(dividend);

    /*
     * Now just update the real factor and addend, and we're done.
     */

    mp_int *addend_old = s->addend;
    mp_int *tmp = mp_mul(s->factor, K); /* use the _old_ value of factor */
    s->addend = mp_add(s->addend, tmp);
    mp_free(tmp);
    mp_free(addend_old);

    mp_int *factor_old = s->factor;
    s->factor = mp_mul(s->factor, M);
    mp_free(factor_old);

    mp_free(M);
    mp_free(K);
    s->factor = mp_unsafe_shrink(s->factor);
    s->addend = mp_unsafe_shrink(s->addend);
    s->limit = mp_unsafe_shrink(s->limit);
}

void pcs_require_residue(PrimeCandidateSource *s,
                         mp_int *mod, mp_int *res_orig)
{
    /*
     * Reduce the input residue to its least non-negative value, in
     * case it was given as a larger equivalent value.
     */
    mp_int *res_reduced = mp_mod(res_orig, mod);
    pcs_require_residue_inner(s, mod, res_reduced);
    mp_free(res_reduced);
}

void pcs_require_residue_1(PrimeCandidateSource *s, mp_int *mod)
{
    mp_int *res = mp_from_integer(1);
    pcs_require_residue(s, mod, res);
    mp_free(res);
}

void pcs_require_residue_1_mod_prime(PrimeCandidateSource *s, mp_int *mod)
{
    pcs_require_residue_1(s, mod);

    sgrowarray(s->kps, s->kpsize, s->nkps);
    s->kps[s->nkps++] = mp_copy(mod);
}

void pcs_avoid_residue_small(PrimeCandidateSource *s,
                             unsigned mod, unsigned res)
{
    assert(!s->avoid_modulus);         /* can't cope with more than one */
    s->avoid_modulus = mod;
    s->avoid_residue = res % mod;      /* reduce, just in case */
}

static int avoid_cmp(const void *av, const void *bv)
{
    const struct avoid *a = (const struct avoid *)av;
    const struct avoid *b = (const struct avoid *)bv;
    return a->mod < b->mod ? -1 : a->mod > b->mod ? +1 : 0;
}

static uint64_t invert(uint64_t a, uint64_t m)
{
    int64_t v0 = a, i0 = 1;
    int64_t v1 = m, i1 = 0;
    while (v0) {
        int64_t tmp, q = v1 / v0;
        tmp = v0; v0 = v1 - q*v0; v1 = tmp;
        tmp = i0; i0 = i1 - q*i0; i1 = tmp;
    }
    assert(v1 == 1 || v1 == -1);
    return i1 * v1;
}

void pcs_ready(PrimeCandidateSource *s)
{
    /*
     * List all the small (modulus, residue) pairs we want to avoid.
     */

    init_smallprimes();

#define ADD_AVOID(newmod, newres) do {                          \
        sgrowarray(s->avoids, s->avoidsize, s->navoids);        \
        s->avoids[s->navoids].mod = (newmod);                   \
        s->avoids[s->navoids].res = (newres);                   \
        s->navoids++;                                           \
    } while (0)

    unsigned limit = (mp_hs_integer(s->addend, 65536) ? 65536 :
                      mp_get_integer(s->addend));

    /*
     * Don't be divisible by any small prime, or at least, any prime
     * smaller than our output number might actually manage to be. (If
     * asked to generate a really small prime, it would be
     * embarrassing to rule out legitimate answers on the grounds that
     * they were divisible by themselves.)
     */
    for (size_t i = 0; i < NSMALLPRIMES && smallprimes[i] < limit; i++)
        ADD_AVOID(smallprimes[i], 0);

    if (s->try_sophie_germain) {
        /*
         * If we're aiming to generate a Sophie Germain prime (i.e. p
         * such that 2p+1 is also prime), then we also want to ensure
         * 2p+1 is not congruent to 0 mod any small prime, because if
         * it is, we'll waste a lot of time generating a p for which
         * 2p+1 can't possibly work. So we have to avoid an extra
         * residue mod each odd q.
         *
         * We can simplify:        2p+1 ==  0      (mod q)
         *                    =>     2p == -1      (mod q)
         *                    =>      p == -2^{-1} (mod q)
         *
         * There's no need to do Euclid's algorithm to compute those
         * inverses, because for any odd q, the modular inverse of -2
         * mod q is just (q-1)/2. (Proof: multiplying it by -2 gives
         * 1-q, which is congruent to 1 mod q.)
         */
        for (size_t i = 0; i < NSMALLPRIMES && smallprimes[i] < limit; i++)
            if (smallprimes[i] != 2)
                ADD_AVOID(smallprimes[i], (smallprimes[i] - 1) / 2);
    }

    /*
     * Finally, if there's a particular modulus and residue we've been
     * told to avoid, put it on the list.
     */
    if (s->avoid_modulus)
        ADD_AVOID(s->avoid_modulus, s->avoid_residue);

#undef ADD_AVOID

    /*
     * Sort our to-avoid list by modulus. Partly this is so that we'll
     * check the smaller moduli first during the live runs, which lets
     * us spot most failing cases earlier rather than later. Also, it
     * brings equal moduli together, so that we can reuse the residue
     * we computed from a previous one.
     */
    qsort(s->avoids, s->navoids, sizeof(*s->avoids), avoid_cmp);

    /*
     * Next, adjust each of these moduli to take account of our factor
     * and addend. If we want factor*x+addend to avoid being congruent
     * to 'res' modulo 'mod', then x itself must avoid being congruent
     * to (res - addend) * factor^{-1}.
     *
     * If factor == 0 modulo mod, then the answer will have a fixed
     * residue anyway, so we can discard it from our list to test.
     */
    int64_t factor_m = 0, addend_m = 0, last_mod = 0;

    size_t out = 0;
    for (size_t i = 0; i < s->navoids; i++) {
        int64_t mod = s->avoids[i].mod, res = s->avoids[i].res;
        if (mod != last_mod) {
            last_mod = mod;
            addend_m = mp_unsafe_mod_integer(s->addend, mod);
            factor_m = mp_unsafe_mod_integer(s->factor, mod);
        }

        if (factor_m == 0) {
            assert(res != addend_m);
            continue;
        }

        res = (res - addend_m) * invert(factor_m, mod);
        res %= mod;
        if (res < 0)
            res += mod;

        s->avoids[out].mod = mod;
        s->avoids[out].res = res;
        out++;
    }

    s->navoids = out;

    s->ready = true;
}

mp_int *pcs_generate(PrimeCandidateSource *s)
{
    assert(s->ready);
    if (s->one_shot) {
        if (s->thrown_away_my_shot)
            return NULL;
        s->thrown_away_my_shot = true;
    }

    while (true) {
        mp_int *x = mp_random_upto(s->limit);

        int64_t x_res = 0, last_mod = 0;
        bool ok = true;

        for (size_t i = 0; i < s->navoids; i++) {
            int64_t mod = s->avoids[i].mod, avoid_res = s->avoids[i].res;

            if (mod != last_mod) {
                last_mod = mod;
                x_res = mp_unsafe_mod_integer(x, mod);
            }

            if (x_res == avoid_res) {
                ok = false;
                break;
            }
        }

        if (!ok) {
            mp_free(x);
            continue; /* try a new x */
        }

        /*
         * We've found a viable x. Make the final output value.
         */
        mp_int *toret = mp_new(s->bits);
        mp_mul_into(toret, x, s->factor);
        mp_add_into(toret, toret, s->addend);
        mp_free(x);
        return toret;
    }
}

void pcs_inspect(PrimeCandidateSource *pcs, mp_int **limit_out,
                 mp_int **factor_out, mp_int **addend_out)
{
    *limit_out = mp_copy(pcs->limit);
    *factor_out = mp_copy(pcs->factor);
    *addend_out = mp_copy(pcs->addend);
}

unsigned pcs_get_bits(PrimeCandidateSource *pcs)
{
    return pcs->bits;
}

unsigned pcs_get_bits_remaining(PrimeCandidateSource *pcs)
{
    return mp_get_nbits(pcs->limit);
}

mp_int *pcs_get_upper_bound(PrimeCandidateSource *pcs)
{
    /* Compute (limit-1) * factor + addend */
    mp_int *tmp = mp_mul(pcs->limit, pcs->factor);
    mp_int *bound = mp_add(tmp, pcs->addend);
    mp_free(tmp);
    mp_sub_into(bound, bound, pcs->factor);
    return bound;
}

mp_int **pcs_get_known_prime_factors(PrimeCandidateSource *pcs, size_t *nout)
{
    *nout = pcs->nkps;
    return pcs->kps;
}
