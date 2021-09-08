/*
 * Prime generation.
 */

#include <assert.h>
#include <math.h>

#include "ssh.h"
#include "mpint.h"
#include "mpunsafe.h"
#include "sshkeygen.h"

/* ----------------------------------------------------------------------
 * Standard probabilistic prime-generation algorithm:
 *
 *  - get a number from our PrimeCandidateSource which will at least
 *    avoid being divisible by any prime under 2^16
 *
 *  - perform the Miller-Rabin primality test enough times to
 *    ensure the probability of it being composite is 2^-80 or
 *    less
 *
 *  - go back to square one if any M-R test fails.
 */

static PrimeGenerationContext *probprime_new_context(
    const PrimeGenerationPolicy *policy)
{
    PrimeGenerationContext *ctx = snew(PrimeGenerationContext);
    ctx->vt = policy;
    return ctx;
}

static void probprime_free_context(PrimeGenerationContext *ctx)
{
    sfree(ctx);
}

static ProgressPhase probprime_add_progress_phase(
    const PrimeGenerationPolicy *policy,
    ProgressReceiver *prog, unsigned bits)
{
    /*
     * The density of primes near x is 1/(log x). When x is about 2^b,
     * that's 1/(b log 2).
     *
     * But we're only doing the expensive part of the process (the M-R
     * checks) for a number that passes the initial winnowing test of
     * having no factor less than 2^16 (at least, unless the prime is
     * so small that PrimeCandidateSource gives up on that winnowing).
     * The density of _those_ numbers is about 1/19.76. So the odds of
     * hitting a prime per expensive attempt are boosted by a factor
     * of 19.76.
     */
    const double log_2 = 0.693147180559945309417232121458;
    double winnow_factor = (bits < 32 ? 1.0 : 19.76);
    double prob = winnow_factor / (bits * log_2);

    /*
     * Estimate the cost of prime generation as the cost of the M-R
     * modexps.
     */
    double cost = (miller_rabin_checks_needed(bits) *
                   estimate_modexp_cost(bits));
    return progress_add_probabilistic(prog, cost, prob);
}

static mp_int *probprime_generate(
    PrimeGenerationContext *ctx,
    PrimeCandidateSource *pcs, ProgressReceiver *prog)
{
    pcs_ready(pcs);

    while (true) {
        progress_report_attempt(prog);

        mp_int *p = pcs_generate(pcs);
        if (!p) {
            pcs_free(pcs);
            return NULL;
        }

        MillerRabin *mr = miller_rabin_new(p);
        bool known_bad = false;
        unsigned nchecks = miller_rabin_checks_needed(mp_get_nbits(p));
        for (unsigned check = 0; check < nchecks; check++) {
            if (!miller_rabin_test_random(mr)) {
                known_bad = true;
                break;
            }
        }
        miller_rabin_free(mr);

        if (!known_bad) {
            /*
             * We have a prime!
             */
            pcs_free(pcs);
            return p;
        }

        mp_free(p);
    }
}

static strbuf *null_mpu_certificate(PrimeGenerationContext *ctx, mp_int *p)
{
    return NULL;
}

const PrimeGenerationPolicy primegen_probabilistic = {
    probprime_add_progress_phase,
    probprime_new_context,
    probprime_free_context,
    probprime_generate,
    null_mpu_certificate,
};

/* ----------------------------------------------------------------------
 * Alternative provable-prime algorithm, based on the following paper:
 *
 * [MAURER] Maurer, U.M. Fast generation of prime numbers and secure
 * public-key cryptographic parameters. J. Cryptology 8, 123–155
 * (1995). https://doi.org/10.1007/BF00202269
 */

typedef enum SubprimePolicy {
    SPP_FAST,
    SPP_MAURER_SIMPLE,
    SPP_MAURER_COMPLEX,
} SubprimePolicy;

typedef struct ProvablePrimePolicyExtra {
    SubprimePolicy spp;
} ProvablePrimePolicyExtra;

typedef struct ProvablePrimeContext ProvablePrimeContext;
struct ProvablePrimeContext {
    Pockle *pockle;
    PrimeGenerationContext pgc;
    const ProvablePrimePolicyExtra *extra;
};

static PrimeGenerationContext *provableprime_new_context(
    const PrimeGenerationPolicy *policy)
{
    ProvablePrimeContext *ppc = snew(ProvablePrimeContext);
    ppc->pgc.vt = policy;
    ppc->pockle = pockle_new();
    ppc->extra = policy->extra;
    return &ppc->pgc;
}

static void provableprime_free_context(PrimeGenerationContext *ctx)
{
    ProvablePrimeContext *ppc = container_of(ctx, ProvablePrimeContext, pgc);
    pockle_free(ppc->pockle);
    sfree(ppc);
}

static ProgressPhase provableprime_add_progress_phase(
    const PrimeGenerationPolicy *policy,
    ProgressReceiver *prog, unsigned bits)
{
    /*
     * Estimating the cost of making a _provable_ prime is difficult
     * because of all the recursions to smaller sizes.
     *
     * Once you have enough factors of p-1 to certify primality of p,
     * the remaining work in provable prime generation is not very
     * different from probabilistic: you generate a random candidate,
     * test its primality probabilistically, and use the witness value
     * generated as a byproduct of that test for the full Pocklington
     * verification. The expensive part, as usual, is made of modpows.
     *
     * The Pocklington test needs at least two modpows (one for the
     * Fermat check, and one per known factor of p-1).
     *
     * The prior M-R step needs an unknown number, because we iterate
     * until we find a value whose order is divisible by the largest
     * power of 2 that divides p-1, say 2^j. That excludes half the
     * possible witness values (specifically, the quadratic residues),
     * so we expect to need on average two M-R operations to find one.
     * But that's only if the number _is_ prime - as usual, it's also
     * possible that we hit a non-prime and have to try again.
     *
     * So, if we were only estimating the cost of that final step, it
     * would look a lot like the probabilistic version: we'd have to
     * estimate the expected total number of modexps by knowing
     * something about the density of primes among our candidate
     * integers, and then multiply that by estimate_modexp_cost(bits).
     * But the problem is that we also have to _find_ a smaller prime,
     * so we have to recurse.
     *
     * In the MAURER_SIMPLE version of the algorithm, you recurse to
     * any one of a range of possible smaller sizes i, each with
     * probability proportional to 1/i. So your expected time to
     * generate an n-bit prime is given by a horrible recurrence of
     * the form E_n = S_n + (sum E_i/i) / (sum 1/i), in which S_n is
     * the expected cost of the final step once you have your smaller
     * primes, and both sums are over ceil(n/2) <= i <= n-20.
     *
     * At this point I ran out of effort to actually do the maths
     * rigorously, so instead I did the empirical experiment of
     * generating that sequence in Python and plotting it on a graph.
     * My Python code is here, in case I need it again:

from math import log

alpha = log(3)/log(2) + 1 # exponent for modexp using Karatsuba mult

E = [1] * 16 # assume generating tiny primes is trivial

for n in range(len(E), 4096):

    # Expected time for sub-generations, as a weighted mean of prior
    # values of the same sequence.
    lo = (n+1)//2
    hi = n-20
    if lo <= hi:
        subrange = range(lo, hi+1)
        num = sum(E[i]/i for i in subrange)
        den = sum(1/i for i in subrange)
    else:
        num, den = 0, 1

    # Constant term (cost of final step).
    # Similar to probprime_add_progress_phase.
    winnow_factor = 1 if n < 32 else 19.76
    prob = winnow_factor / (n * log(2))
    cost = 4 * n**alpha / prob

    E.append(cost + num / den)

for i, p in enumerate(E):
    try:
        print(log(i), log(p))
    except ValueError:
        continue

     * The output loop prints the logs of both i and E_i, so that when
     * I plot the resulting data file in gnuplot I get a log-log
     * diagram. That showed me some early noise and then a very
     * straight-looking line; feeding the straight part of the graph
     * to linear-regression analysis reported that it fits the line
     *
     *     log E_n = -1.7901825337965498 + 3.6199197179662517 * log(n)
     * =>      E_n = 0.16692969657466802 * n^3.6199197179662517
     *
     * So my somewhat empirical estimate is that Maurer prime
     * generation costs about 0.167 * bits^3.62, in the same arbitrary
     * time units used by estimate_modexp_cost.
     */

    return progress_add_linear(prog, 0.167 * pow(bits, 3.62));
}

static mp_int *primegen_small(Pockle *pockle, PrimeCandidateSource *pcs)
{
    assert(pcs_get_bits(pcs) <= 32);

    pcs_ready(pcs);

    while (true) {
        mp_int *p = pcs_generate(pcs);
        if (!p) {
            pcs_free(pcs);
            return NULL;
        }
        if (pockle_add_small_prime(pockle, p) == POCKLE_OK) {
            pcs_free(pcs);
            return p;
        }
        mp_free(p);
    }
}

#ifdef DEBUG_PRIMEGEN
static void timestamp(FILE *fp)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(fp, "%lu.%09lu: ", (unsigned long)ts.tv_sec,
            (unsigned long)ts.tv_nsec);
}
static PRINTF_LIKE(1, 2) void debug_f(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    timestamp(stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}
static void debug_f_mp(const char *fmt, mp_int *x, ...)
{
    va_list ap;
    va_start(ap, x);
    timestamp(stderr);
    vfprintf(stderr, fmt, ap);
    mp_dump(stderr, "", x, "\n");
    va_end(ap);
}
#else
#define debug_f(...) ((void)0)
#define debug_f_mp(...) ((void)0)
#endif

static double uniform_random_double(void)
{
    unsigned char randbuf[8];
    random_read(randbuf, 8);
    return GET_64BIT_MSB_FIRST(randbuf) * 0x1.0p-64;
}

static mp_int *mp_ceil_div(mp_int *n, mp_int *d)
{
    mp_int *nplus = mp_add(n, d);
    mp_sub_integer_into(nplus, nplus, 1);
    mp_int *toret = mp_div(nplus, d);
    mp_free(nplus);
    return toret;
}

static mp_int *provableprime_generate_inner(
    ProvablePrimeContext *ppc, PrimeCandidateSource *pcs,
    ProgressReceiver *prog, double progress_origin, double progress_scale)
{
    unsigned bits = pcs_get_bits(pcs);
    assert(bits > 1);

    if (bits <= 32) {
        debug_f("ppgi(%u) -> small", bits);
        return primegen_small(ppc->pockle, pcs);
    }

    unsigned min_bits_needed, max_bits_needed;
    {
        /*
         * Find the product of all the prime factors we already know
         * about.
         */
        mp_int *size_got = mp_from_integer(1);
        size_t nfactors;
        mp_int **factors = pcs_get_known_prime_factors(pcs, &nfactors);
        for (size_t i = 0; i < nfactors; i++) {
            mp_int *to_free = size_got;
            size_got = mp_unsafe_shrink(mp_mul(size_got, factors[i]));
            mp_free(to_free);
        }

        /*
         * Find the largest cofactor we might be able to use, and the
         * smallest one we can get away with.
         */
        mp_int *upperbound = pcs_get_upper_bound(pcs);
        mp_int *size_needed = mp_nthroot(upperbound, 3, NULL);
        debug_f_mp("upperbound = ", upperbound);
        {
            mp_int *to_free = upperbound;
            upperbound = mp_unsafe_shrink(mp_div(upperbound, size_got));
            mp_free(to_free);
        }
        debug_f_mp("size_needed = ", size_needed);
        {
            mp_int *to_free = size_needed;
            size_needed = mp_unsafe_shrink(mp_ceil_div(size_needed, size_got));
            mp_free(to_free);
        }

        max_bits_needed = pcs_get_bits_remaining(pcs);

        /*
         * We need a prime that is greater than or equal to
         * 'size_needed' in order for the product of all our known
         * factors of p-1 to exceed the cube root of the largest value
         * p might take.
         *
         * Since pcs_new wants a size specified in bits, we must count
         * the bits in size_needed and then add 1. Otherwise we might
         * get a value with the same bit count as size_needed but
         * slightly smaller than it.
         *
         * An exception is if size_needed = 1. In that case the
         * product of existing known factors is _already_ enough, so
         * we don't need to generate an extra factor at all.
         */
        if (mp_hs_integer(size_needed, 2)) {
            min_bits_needed = mp_get_nbits(size_needed) + 1;
        } else {
            min_bits_needed = 0;
        }

        mp_free(upperbound);
        mp_free(size_needed);
        mp_free(size_got);
    }

    double progress = 0.0;

    if (min_bits_needed) {
        debug_f("ppgi(%u) recursing, need [%u,%u] more bits",
                bits, min_bits_needed, max_bits_needed);

        unsigned *sizes = NULL;
        size_t nsizes = 0, sizesize = 0;

        unsigned real_min = max_bits_needed / 2;
        unsigned real_max = (max_bits_needed >= 20 ?
                             max_bits_needed - 20 : 0);
        if (real_min < min_bits_needed)
            real_min = min_bits_needed;
        if (real_max < real_min)
            real_max = real_min;
        debug_f("ppgi(%u) revised bits interval = [%u,%u]",
                bits, real_min, real_max);

        switch (ppc->extra->spp) {
          case SPP_FAST:
            /*
             * Always pick the smallest subsidiary prime we can get
             * away with: just over n/3 bits.
             *
             * This is not a good mode for cryptographic prime
             * generation, because it skews the distribution of primes
             * greatly, and worse, it skews them in a direction that
             * heads away from the properties crypto algorithms tend
             * to like.
             *
             * (For both discrete-log systems and RSA, people have
             * tended to recommend in the past that p-1 should have a
             * _large_ factor if possible. There's some disagreement
             * on which algorithms this is really necessary for, but
             * certainly I've never seen anyone recommend arranging a
             * _small_ factor on purpose.)
             *
             * I originally implemented this mode because it was
             * convenient for debugging - it wastes as little time as
             * possible on finding a sub-prime and lets you get to the
             * interesting part! And I leave it in the code because it
             * might still be useful for _something_. Because it's
             * cryptographically questionable, it's not selectable in
             * the UI of either version of PuTTYgen proper; but it can
             * be accessed through testcrypt, and if for some reason a
             * definite prime is needed for non-crypto purposes, it
             * may still be the fastest way to put your hands on one.
             */
            debug_f("ppgi(%u) fast mode, just ask for %u bits",
                    bits, min_bits_needed);
            sgrowarray(sizes, sizesize, nsizes);
            sizes[nsizes++] = min_bits_needed;
            break;
          case SPP_MAURER_SIMPLE: {
            /*
             * Select the size of the subsidiary prime at random from
             * sqrt(outputprime) up to outputprime/2^20, in such a way
             * that the probability distribution matches that of the
             * largest prime factor of a random n-bit number.
             *
             * Per [MAURER] section 3.4, the cumulative distribution
             * function of this relative size is 1+log2(x), for x in
             * [1/2,1]. You can generate a value from the distribution
             * given by a cdf by applying the inverse cdf to a uniform
             * value in [0,1]. Simplifying that in this case, what we
             * have to do is raise 2 to the power of a random real
             * number between -1 and 0. (And that gives you the number
             * of _bits_ in the sub-prime, as a factor of the desired
             * output number of bits.)
             *
             * We also require that the subsidiary prime q is at least
             * 20 bits smaller than the output one, to give us a
             * fighting chance of there being _any_ prime we can find
             * such that q | p-1.
             *
             * (But these rules have to be applied in an order that
             * still leaves us _some_ interval of possible sizes we
             * can pick!)
             */
          maurer_simple:
            debug_f("ppgi(%u) Maurer simple mode", bits);

            unsigned sub_bits;
            do {
                double uniform = uniform_random_double();
                sub_bits = real_max * pow(2.0, uniform - 1) + 0.5;
                debug_f(" ... %.6f -> %u?", uniform, sub_bits);
            } while (!(real_min <= sub_bits && sub_bits <= real_max));

            debug_f("ppgi(%u) asking for %u bits", bits, sub_bits);
            sgrowarray(sizes, sizesize, nsizes);
            sizes[nsizes++] = sub_bits;

            break;
          }
          case SPP_MAURER_COMPLEX: {
            /*
             * In this mode, we may generate multiple factors of p-1
             * which between them add up to at least n/2 bits, in such
             * a way that those are guaranteed to be the largest
             * factors of p-1 and that they have the same probability
             * distribution as the largest k factors would have in a
             * random integer. The idea is that this more elaborate
             * procedure gets as close as possible to the same
             * probability distribution you'd get by selecting a
             * completely random prime (if you feasibly could).
             *
             * Algorithm from Appendix 1 of [MAURER]: we generate
             * random real numbers that sum to at most 1, by choosing
             * each one uniformly from the range [0, 1 - sum of all
             * the previous ones]. We maintain them in a list in
             * decreasing order, and we stop as soon as we find an
             * initial subsequence of the list s_1,...,s_r such that
             * s_1 + ... + s_{r-1} + 2 s_r > 1. In particular, this
             * guarantees that the sum of that initial subsequence is
             * at least 1/2, so we end up with enough factors to
             * satisfy Pocklington.
             */

            if (max_bits_needed / 2 + 1 > real_max) {
                /* Early exit path in the case where this algorithm
                 * can't possibly generate a value in the range we
                 * need. In that situation, fall back to Maurer
                 * simple. */
                debug_f("ppgi(%u) skipping GenerateSizeList, "
                        "real_max too small", bits);
                goto maurer_simple;    /* sorry! */
            }

            double *s = NULL;
            size_t ns, ssize = 0;

            while (true) {
                debug_f("ppgi(%u) starting GenerateSizeList", bits);
                ns = 0;
                double range = 1.0;
                while (true) {
                    /* Generate the next number */
                    double u = uniform_random_double() * range;
                    range -= u;
                    debug_f("  u_%"SIZEu" = %g", ns, u);

                    /* Insert it in the list */
                    sgrowarray(s, ssize, ns);
                    size_t i;
                    for (i = ns; i > 0 && s[i-1] < u; i--)
                        s[i] = s[i-1];
                    s[i] = u;
                    ns++;
                    debug_f("    inserting as s[%"SIZEu"]", i);

                    /* Look for a suitable initial subsequence */
                    double sum = 0;
                    for (i = 0; i < ns; i++) {
                        sum += s[i];
                        if (sum + s[i] > 1.0) {
                            debug_f("  s[0..%"SIZEu"] works!", i);

                            /* Truncate the sequence here, and stop
                             * generating random real numbers. */
                            ns = i+1;
                            goto got_list;
                        }
                    }
                }

              got_list:;
                /*
                 * Now translate those real numbers into actual bit
                 * counts, and do a last-minute check to make sure
                 * their product is going to be in range.
                 *
                 * We have to check both the min and max sizes of the
                 * total. A b-bit number is in [2^{b-1},2^b). So the
                 * product of numbers of sizes b_1,...,b_k is at least
                 * 2^{\sum (b_i-1)}, and less than 2^{\sum b_i}.
                 */
                nsizes = 0;

                unsigned min_total = 0, max_total = 0;

                for (size_t i = 0; i < ns; i++) {
                    /* These sizes are measured in actual entropy, so
                     * add 1 bit each time to account for the
                     * zero-information leading 1 */
                    unsigned this_size = max_bits_needed * s[i] + 1;
                    debug_f("  bits[%"SIZEu"] = %u", i, this_size);
                    sgrowarray(sizes, sizesize, nsizes);
                    sizes[nsizes++] = this_size;

                    min_total += this_size - 1;
                    max_total += this_size;
                }

                debug_f("  total bits = [%u,%u)", min_total, max_total);
                if (min_total < real_min || max_total > real_max+1) {
                    debug_f("  total out of range, try again");
                } else {
                    debug_f("  success! %"SIZEu" sub-primes totalling [%u,%u) "
                            "bits", nsizes, min_total, max_total);
                    break;
                }
            }

            smemclr(s, ssize * sizeof(*s));
            sfree(s);
            break;
          }
          default:
            unreachable("bad subprime policy");
        }

        for (size_t i = 0; i < nsizes; i++) {
            unsigned sub_bits = sizes[i];
            double progress_in_this_prime = (double)sub_bits / bits;
            mp_int *q = provableprime_generate_inner(
                ppc, pcs_new(sub_bits),
                prog, progress_origin + progress_scale * progress,
                progress_scale * progress_in_this_prime);
            progress += progress_in_this_prime;
            assert(q);
            debug_f_mp("ppgi(%u) got factor ", q, bits);
            pcs_require_residue_1_mod_prime(pcs, q);
            mp_free(q);
        }

        smemclr(sizes, sizesize * sizeof(*sizes));
        sfree(sizes);
    } else {
        debug_f("ppgi(%u) no need to recurse", bits);
    }

    debug_f("ppgi(%u) ready, %u bits remaining",
            bits, pcs_get_bits_remaining(pcs));
    pcs_ready(pcs);

    while (true) {
        mp_int *p = pcs_generate(pcs);
        if (!p) {
            pcs_free(pcs);
            return NULL;
        }

        debug_f_mp("provable_step p=", p);

        MillerRabin *mr = miller_rabin_new(p);
        debug_f("provable_step mr setup done");
        mp_int *witness = miller_rabin_find_potential_primitive_root(mr);
        miller_rabin_free(mr);

        if (!witness) {
            debug_f("provable_step mr failed");
            mp_free(p);
            continue;
        }

        size_t nfactors;
        mp_int **factors = pcs_get_known_prime_factors(pcs, &nfactors);
        PockleStatus st = pockle_add_prime(
            ppc->pockle, p, factors, nfactors, witness);

        if (st != POCKLE_OK) {
            debug_f("provable_step proof failed %d", (int)st);

            /*
             * Check by assertion that the error status is not one of
             * the ones we ought to have ruled out already by
             * construction. If there's a bug in this code that means
             * we can _never_ pass this test (e.g. picking products of
             * factors that never quite reach cbrt(n)), we'd rather
             * fail an assertion than loop forever.
             */
            assert(st == POCKLE_DISCRIMINANT_IS_SQUARE ||
                   st == POCKLE_WITNESS_POWER_IS_1 ||
                   st == POCKLE_WITNESS_POWER_NOT_COPRIME);

            mp_free(p);
            if (witness)
                mp_free(witness);
            continue;
        }

        mp_free(witness);
        pcs_free(pcs);
        debug_f_mp("ppgi(%u) done, got ", p, bits);
        progress_report(prog, progress_origin + progress_scale);
        return p;
    }
}

static mp_int *provableprime_generate(
    PrimeGenerationContext *ctx,
    PrimeCandidateSource *pcs, ProgressReceiver *prog)
{
    ProvablePrimeContext *ppc = container_of(ctx, ProvablePrimeContext, pgc);
    mp_int *p = provableprime_generate_inner(ppc, pcs, prog, 0.0, 1.0);

    return p;
}

static inline strbuf *provableprime_mpu_certificate(
    PrimeGenerationContext *ctx, mp_int *p)
{
    ProvablePrimeContext *ppc = container_of(ctx, ProvablePrimeContext, pgc);
    return pockle_mpu(ppc->pockle, p);
}

#define DECLARE_POLICY(name, policy)                                    \
    static const struct ProvablePrimePolicyExtra                        \
        pppextra_##name = {policy};                                     \
    const PrimeGenerationPolicy name = {                                \
        provableprime_add_progress_phase,                               \
        provableprime_new_context,                                      \
        provableprime_free_context,                                     \
        provableprime_generate,                                         \
        provableprime_mpu_certificate,                                  \
        &pppextra_##name,                                               \
    }

DECLARE_POLICY(primegen_provable_fast, SPP_FAST);
DECLARE_POLICY(primegen_provable_maurer_simple, SPP_MAURER_SIMPLE);
DECLARE_POLICY(primegen_provable_maurer_complex, SPP_MAURER_COMPLEX);

/* ----------------------------------------------------------------------
 * Reusable null implementation of the progress-reporting API.
 */

static inline ProgressPhase null_progress_add(void) {
    ProgressPhase ph = { .n = 0 };
    return ph;
}
ProgressPhase null_progress_add_linear(
    ProgressReceiver *prog, double c) { return null_progress_add(); }
ProgressPhase null_progress_add_probabilistic(
    ProgressReceiver *prog, double c, double p) { return null_progress_add(); }
void null_progress_ready(ProgressReceiver *prog) {}
void null_progress_start_phase(ProgressReceiver *prog, ProgressPhase phase) {}
void null_progress_report(ProgressReceiver *prog, double progress) {}
void null_progress_report_attempt(ProgressReceiver *prog) {}
void null_progress_report_phase_complete(ProgressReceiver *prog) {}
const ProgressReceiverVtable null_progress_vt = {
    .add_linear = null_progress_add_linear,
    .add_probabilistic = null_progress_add_probabilistic,
    .ready = null_progress_ready,
    .start_phase = null_progress_start_phase,
    .report = null_progress_report,
    .report_attempt = null_progress_report_attempt,
    .report_phase_complete = null_progress_report_phase_complete,
};

/* ----------------------------------------------------------------------
 * Helper function for progress estimation.
 */

double estimate_modexp_cost(unsigned bits)
{
    /*
     * A modexp of n bits goes roughly like O(n^2.58), on the grounds
     * that our modmul is O(n^1.58) (Karatsuba) and you need O(n) of
     * them in a modexp.
     */
    return pow(bits, 2.58);
}
