#include <assert.h>
#include "ssh.h"
#include "sshkeygen.h"
#include "mpint.h"
#include "mpunsafe.h"
#include "tree234.h"

typedef struct PocklePrimeRecord PocklePrimeRecord;

struct Pockle {
    tree234 *tree;

    PocklePrimeRecord **list;
    size_t nlist, listsize;
};

struct PocklePrimeRecord {
    mp_int *prime;
    PocklePrimeRecord **factors;
    size_t nfactors;
    mp_int *witness;

    size_t index; /* index in pockle->list */
};

static int ppr_cmp(void *av, void *bv)
{
    PocklePrimeRecord *a = (PocklePrimeRecord *)av;
    PocklePrimeRecord *b = (PocklePrimeRecord *)bv;
    return mp_cmp_hs(a->prime, b->prime) - mp_cmp_hs(b->prime, a->prime);
}

static int ppr_find(void *av, void *bv)
{
    mp_int *a = (mp_int *)av;
    PocklePrimeRecord *b = (PocklePrimeRecord *)bv;
    return mp_cmp_hs(a, b->prime) - mp_cmp_hs(b->prime, a);
}

Pockle *pockle_new(void)
{
    Pockle *pockle = snew(Pockle);
    pockle->tree = newtree234(ppr_cmp);
    pockle->list = NULL;
    pockle->nlist = pockle->listsize = 0;
    return pockle;
}

void pockle_free(Pockle *pockle)
{
    pockle_release(pockle, 0);
    assert(count234(pockle->tree) == 0);
    freetree234(pockle->tree);
    sfree(pockle->list);
    sfree(pockle);
}

static PockleStatus pockle_insert(Pockle *pockle, mp_int *p, mp_int **factors,
                                  size_t nfactors, mp_int *w)
{
    PocklePrimeRecord *pr = snew(PocklePrimeRecord);
    pr->prime = mp_copy(p);

    PocklePrimeRecord *found = add234(pockle->tree, pr);
    if (pr != found) {
        /* it was already in there */
        mp_free(pr->prime);
        sfree(pr);
        return POCKLE_OK;
    }

    if (w) {
        pr->factors = snewn(nfactors, PocklePrimeRecord *);
        for (size_t i = 0; i < nfactors; i++) {
            pr->factors[i] = find234(pockle->tree, factors[i], ppr_find);
            assert(pr->factors[i]);
        }
        pr->nfactors = nfactors;
        pr->witness = mp_copy(w);
    } else {
        pr->factors = NULL;
        pr->nfactors = 0;
        pr->witness = NULL;
    }
    pr->index = pockle->nlist;

    sgrowarray(pockle->list, pockle->listsize, pockle->nlist);
    pockle->list[pockle->nlist++] = pr;
    return POCKLE_OK;
}

size_t pockle_mark(Pockle *pockle)
{
    return pockle->nlist;
}

void pockle_release(Pockle *pockle, size_t mark)
{
    while (pockle->nlist > mark) {
        PocklePrimeRecord *pr = pockle->list[--pockle->nlist];
        del234(pockle->tree, pr);
        mp_free(pr->prime);
        if (pr->witness)
            mp_free(pr->witness);
        sfree(pr->factors);
        sfree(pr);
    }
}

PockleStatus pockle_add_small_prime(Pockle *pockle, mp_int *p)
{
    if (mp_hs_integer(p, (1ULL << 32)))
        return POCKLE_SMALL_PRIME_NOT_SMALL;

    uint32_t val = mp_get_integer(p);

    if (val < 2)
        return POCKLE_PRIME_SMALLER_THAN_2;

    init_smallprimes();
    for (size_t i = 0; i < NSMALLPRIMES; i++) {
        if (val == smallprimes[i])
            break; /* success */
        if (val % smallprimes[i] == 0)
            return POCKLE_SMALL_PRIME_NOT_PRIME;
    }

    return pockle_insert(pockle, p, NULL, 0, NULL);
}

PockleStatus pockle_add_prime(Pockle *pockle, mp_int *p,
                              mp_int **factors, size_t nfactors,
                              mp_int *witness)
{
    MontyContext *mc = NULL;
    mp_int *x = NULL, *f = NULL, *w = NULL;
    PockleStatus status;

    /*
     * We're going to try to verify that p is prime by using
     * Pocklington's theorem. The idea is that we're given w such that
     *          w^{p-1} == 1 (mod p)             (1)
     * and for a collection of primes q | p-1,
     *       w^{(p-1)/q} - 1 is coprime to p.    (2)
     *
     * Suppose r is a prime factor of p itself. Consider the
     * multiplicative order of w mod r. By (1), r | w^{p-1}-1. But by
     * (2), r does not divide w^{(p-1)/q}-1. So the order of w mod r
     * is a factor of p-1, but not a factor of (p-1)/q. Hence, the
     * largest power of q that divides p-1 must also divide ord w.
     *
     * Repeating this reasoning for all q, we find that the product of
     * all the q (which we'll denote f) must divide ord w, which in
     * turn divides r-1. So f | r-1 for any r | p.
     *
     * In particular, this means f < r. That is, all primes r | p are
     * bigger than f. So if f > sqrt(p), then we've shown p is prime,
     * because otherwise it would have to be the product of at least
     * two factors bigger than its own square root.
     *
     * With an extra check, we can also show p to be prime even if
     * we're only given enough factors to make f > cbrt(p). See below
     * for that part, when we come to it.
     */

    /*
     * Start by checking p > 1. It certainly can't be prime otherwise!
     * (And since we're going to prove it prime by showing all its
     * prime factors are large, we do also have to know it _has_ at
     * least one prime factor for that to tell us anything.)
     */
    if (!mp_hs_integer(p, 2))
        return POCKLE_PRIME_SMALLER_THAN_2;

    /*
     * Check that all the factors we've been given really are primes
     * (in the sense that we already had them in our index). Make the
     * product f, and check it really does divide p-1.
     */
    x = mp_copy(p);
    mp_sub_integer_into(x, x, 1);
    f = mp_from_integer(1);
    for (size_t i = 0; i < nfactors; i++) {
        mp_int *q = factors[i];

        if (!find234(pockle->tree, q, ppr_find)) {
            status = POCKLE_FACTOR_NOT_KNOWN_PRIME;
            goto out;
        }

        mp_int *quotient = mp_new(mp_max_bits(x));
        mp_int *residue = mp_new(mp_max_bits(q));
        mp_divmod_into(x, q, quotient, residue);

        unsigned exact = mp_eq_integer(residue, 0);
        mp_free(residue);

        mp_free(x);
        x = quotient;

        if (!exact) {
            status = POCKLE_FACTOR_NOT_A_FACTOR;
            goto out;
        }

        mp_int *tmp = f;
        f = mp_unsafe_shrink(mp_mul(tmp, q));
        mp_free(tmp);
    }

    /*
     * Check that f > cbrt(p).
     */
    mp_int *f2 = mp_mul(f, f);
    mp_int *f3 = mp_mul(f2, f);
    bool too_big = mp_cmp_hs(p, f3);
    mp_free(f3);
    mp_free(f2);
    if (too_big) {
        status = POCKLE_PRODUCT_OF_FACTORS_TOO_SMALL;
        goto out;
    }

    /*
     * Now do the extra check that allows us to get away with only
     * having f > cbrt(p) instead of f > sqrt(p).
     *
     * If we can show that f | r-1 for any r | p, then we've ruled out
     * p being a product of _more_ than two primes (because then it
     * would be the product of at least three things bigger than its
     * own cube root). But we still have to rule out it being a
     * product of exactly two.
     *
     * Suppose for the sake of contradiction that p is the product of
     * two prime factors. We know both of those factors would have to
     * be congruent to 1 mod f. So we'd have to have
     *
     *    p = (uf+1)(vf+1) = (uv)f^2 + (u+v)f + 1        (3)
     *
     * We can't have uv >= f, or else that expression would come to at
     * least f^3, i.e. it would exceed p. So uv < f. Hence, u,v < f as
     * well.
     *
     * Can we have u+v >= f? If we did, then we could write v >= f-u,
     * and hence f > uv >= u(f-u). That can be rearranged to show that
     * u^2 > (u-1)f; decrementing the LHS makes the inequality no
     * longer necessarily strict, so we have u^2-1 >= (u-1)f, and
     * dividing off u-1 gives u+1 >= f. But we know u < f, so the only
     * way this could happen would be if u=f-1, which makes v=1. But
     * _then_ (3) gives us p = (f-1)f^2 + f^2 + 1 = f^3+1. But that
     * can't be true if f^3 > p. So we can't have u+v >= f either, by
     * contradiction.
     *
     * After all that, what have we shown? We've shown that we can
     * write p = (uv)f^2 + (u+v)f + 1, with both uv and u+v strictly
     * less than f. In other words, if you write down p in base f, it
     * has exactly three digits, and they are uv, u+v and 1.
     *
     * But that means we can _find_ u and v: we know p and f, so we
     * can just extract those digits of p's base-f representation.
     * Once we've done so, they give the sum and product of the
     * potential u,v. And given the sum and product of two numbers,
     * you can make a quadratic which has those numbers as roots.
     *
     * We don't actually have to _solve_ the quadratic: all we have to
     * do is check if its discriminant is a perfect square. If not,
     * we'll know that no integers u,v can match this description.
     */
    {
        /* We already have x = (p-1)/f. So we just need to write x in
         * the form aF + b, and then we have a=uv and b=u+v. */
        mp_int *a = mp_new(mp_max_bits(x));
        mp_int *b = mp_new(mp_max_bits(f));
        mp_divmod_into(x, f, a, b);
        assert(!mp_cmp_hs(a, f));
        assert(!mp_cmp_hs(b, f));

        /* If a=0, then that means p < f^2, so we don't need to do
         * this check at all: the straightforward Pocklington theorem
         * is all we need. */
        if (!mp_eq_integer(a, 0)) {
            unsigned perfect_square = 0;

            mp_int *bsq = mp_mul(b, b);
            mp_lshift_fixed_into(a, a, 2);

            if (mp_cmp_hs(bsq, a)) {
                /* b^2-4a is non-negative, so it might be a square.
                 * Check it. */
                mp_int *discriminant = mp_sub(bsq, a);
                mp_int *remainder = mp_new(mp_max_bits(discriminant));
                mp_int *root = mp_nthroot(discriminant, 2, remainder);
                perfect_square = mp_eq_integer(remainder, 0);
                mp_free(discriminant);
                mp_free(root);
                mp_free(remainder);
            }

            mp_free(bsq);

            if (perfect_square) {
                mp_free(b);
                mp_free(a);
                status = POCKLE_DISCRIMINANT_IS_SQUARE;
                goto out;
            }
        }
        mp_free(b);
        mp_free(a);
    }

    /*
     * Now we've done all the checks that are cheaper than a modpow,
     * so we've ruled out as many things as possible before having to
     * do any hard work. But there's nothing for it now: make a
     * MontyContext.
     */
    mc = monty_new(p);
    w = monty_import(mc, witness);

    /*
     * The initial Fermat check: is w^{p-1} itself congruent to 1 mod
     * p?
     */
    {
        mp_int *pm1 = mp_copy(p);
        mp_sub_integer_into(pm1, pm1, 1);
        mp_int *power = monty_pow(mc, w, pm1);
        unsigned fermat_pass = mp_cmp_eq(power, monty_identity(mc));
        mp_free(power);
        mp_free(pm1);

        if (!fermat_pass) {
            status = POCKLE_FERMAT_TEST_FAILED;
            goto out;
        }
    }

    /*
     * And now, for each factor q, is w^{(p-1)/q}-1 coprime to p?
     */
    for (size_t i = 0; i < nfactors; i++) {
        mp_int *q = factors[i];
        mp_int *exponent = mp_unsafe_shrink(mp_div(p, q));
        mp_int *power = monty_pow(mc, w, exponent);
        mp_int *power_extracted = monty_export(mc, power);
        mp_sub_integer_into(power_extracted, power_extracted, 1);

        unsigned coprime = mp_coprime(power_extracted, p);
        if (!coprime) {
            /*
             * If w^{(p-1)/q}-1 is not coprime to p, the test has
             * failed. But it makes a difference why. If the power of
             * w turned out to be 1, so that we took gcd(1-1,p) =
             * gcd(0,p) = p, that's like an inconclusive Fermat or M-R
             * test: it might just mean you picked a witness integer
             * that wasn't a primitive root. But if the power is any
             * _other_ value mod p that is not coprime to p, it means
             * we've detected that the number is *actually not prime*!
             */
            if (mp_eq_integer(power_extracted, 0))
                status = POCKLE_WITNESS_POWER_IS_1;
            else
                status = POCKLE_WITNESS_POWER_NOT_COPRIME;
        }

        mp_free(exponent);
        mp_free(power);
        mp_free(power_extracted);

        if (!coprime)
            goto out; /* with the status we set up above */
    }

    /*
     * Success! p is prime. Insert it into our tree234 of known
     * primes, so that future calls to this function can cite it in
     * evidence of larger numbers' primality.
     */
    status = pockle_insert(pockle, p, factors, nfactors, witness);

  out:
    if (x)
        mp_free(x);
    if (f)
        mp_free(f);
    if (w)
        mp_free(w);
    if (mc)
        monty_free(mc);
    return status;
}

static void mp_write_decimal(strbuf *sb, mp_int *x)
{
    char *s = mp_get_decimal(x);
    ptrlen pl = ptrlen_from_asciz(s);
    put_datapl(sb, pl);
    smemclr(s, pl.len);
    sfree(s);
}

strbuf *pockle_mpu(Pockle *pockle, mp_int *p)
{
    strbuf *sb = strbuf_new_nm();
    PocklePrimeRecord *pr = find234(pockle->tree, p, ppr_find);
    assert(pr);

    bool *needed = snewn(pockle->nlist, bool);
    memset(needed, 0, pockle->nlist * sizeof(bool));
    needed[pr->index] = true;

    strbuf_catf(sb, "[MPU - Primality Certificate]\nVersion 1.0\nBase 10\n\n"
                "Proof for:\nN  ");
    mp_write_decimal(sb, p);
    strbuf_catf(sb, "\n");

    for (size_t index = pockle->nlist; index-- > 0 ;) {
        if (!needed[index])
            continue;
        pr = pockle->list[index];

        if (mp_get_nbits(pr->prime) <= 64) {
            strbuf_catf(sb, "\nType Small\nN  ");
            mp_write_decimal(sb, pr->prime);
            strbuf_catf(sb, "\n");
        } else {
            assert(pr->witness);
            strbuf_catf(sb, "\nType BLS5\nN  ");
            mp_write_decimal(sb, pr->prime);
            strbuf_catf(sb, "\n");
            for (size_t i = 0; i < pr->nfactors; i++) {
                strbuf_catf(sb, "Q[%"SIZEu"]  ", i+1);
                mp_write_decimal(sb, pr->factors[i]->prime);
                assert(pr->factors[i]->index < index);
                needed[pr->factors[i]->index] = true;
                strbuf_catf(sb, "\n");
            }
            for (size_t i = 0; i < pr->nfactors + 1; i++) {
                strbuf_catf(sb, "A[%"SIZEu"]  ", i);
                mp_write_decimal(sb, pr->witness);
                strbuf_catf(sb, "\n");
            }
            strbuf_catf(sb, "----\n");
        }
    }
    sfree(needed);

    return sb;
}
