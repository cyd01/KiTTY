/*
 * sshkeygen.h: routines used internally to key generation.
 */

/* ----------------------------------------------------------------------
 * A table of all the primes that fit in a 16-bit integer. Call
 * init_primes_array to make sure it's been initialised.
 */

#define NSMALLPRIMES 6542 /* number of primes < 65536 */
extern const unsigned short *const smallprimes;
void init_smallprimes(void);

/* ----------------------------------------------------------------------
 * A system for making up random candidate integers during prime
 * generation. This unconditionally ensures that the numbers have the
 * right number of bits and are not divisible by any prime in the
 * smallprimes[] array above. It can also impose further constraints,
 * as documented below.
 */
typedef struct PrimeCandidateSource PrimeCandidateSource;

/*
 * pcs_new: you say how many bits you want the prime to have (with the
 * usual semantics that an n-bit number is in the range [2^{n-1},2^n))
 * and also optionally specify what you want its topmost 'nfirst' bits
 * to be.
 *
 * (The 'first' system is used for RSA keys, where you need to arrange
 * that the product of your two primes is in a more tightly
 * constrained range than the factor of 4 you'd get by just generating
 * two (n/2)-bit primes and multiplying them.)
 */
PrimeCandidateSource *pcs_new(unsigned bits);
PrimeCandidateSource *pcs_new_with_firstbits(unsigned bits,
                                             unsigned first, unsigned nfirst);

/* Insist that generated numbers must be congruent to 'res' mod 'mod' */
void pcs_require_residue(PrimeCandidateSource *s, mp_int *mod, mp_int *res);

/* Convenience wrapper for the common case where res = 1 */
void pcs_require_residue_1(PrimeCandidateSource *s, mp_int *mod);

/* Same as pcs_require_residue_1, but also records that the modulus is
 * known to be prime */
void pcs_require_residue_1_mod_prime(PrimeCandidateSource *s, mp_int *mod);

/* Insist that generated numbers must _not_ be congruent to 'res' mod
 * 'mod'. This is used to avoid being 1 mod the RSA public exponent,
 * which is small, so it only needs ordinary integer parameters. */
void pcs_avoid_residue_small(PrimeCandidateSource *s,
                             unsigned mod, unsigned res);

/* Exclude any prime that has no chance of being a Sophie Germain prime. */
void pcs_try_sophie_germain(PrimeCandidateSource *s);

/* Mark a PrimeCandidateSource as one-shot, so that the prime generation
 * function will return NULL if an attempt fails, rather than looping. */
void pcs_set_oneshot(PrimeCandidateSource *s);

/* Prepare a PrimeCandidateSource to actually generate numbers. This
 * function does last-minute computation that has to be delayed until
 * all constraints have been input. */
void pcs_ready(PrimeCandidateSource *s);

/* Actually generate a candidate integer. You must free the result, of
 * course. */
mp_int *pcs_generate(PrimeCandidateSource *s);

/* Free a PrimeCandidateSource. */
void pcs_free(PrimeCandidateSource *s);

/* Return some internal fields of the PCS. Used by testcrypt for
 * unit-testing this system. */
void pcs_inspect(PrimeCandidateSource *pcs, mp_int **limit_out,
                 mp_int **factor_out, mp_int **addend_out);

/* Query functions for primegen to use */
unsigned pcs_get_bits(PrimeCandidateSource *pcs);
unsigned pcs_get_bits_remaining(PrimeCandidateSource *pcs);
mp_int *pcs_get_upper_bound(PrimeCandidateSource *pcs);
mp_int **pcs_get_known_prime_factors(PrimeCandidateSource *pcs, size_t *nout);

/* ----------------------------------------------------------------------
 * A system for doing Miller-Rabin probabilistic primality tests.
 * These benefit from having set up some context beforehand, if you're
 * going to do more than one of them on the same candidate prime, so
 * we declare an object type here to store that context.
 */

typedef struct MillerRabin MillerRabin;

/* Make and free a Miller-Rabin context. */
MillerRabin *miller_rabin_new(mp_int *p);
void miller_rabin_free(MillerRabin *mr);

/* Perform a single Miller-Rabin test, using a random witness value. */
bool miller_rabin_test_random(MillerRabin *mr);

/* Suggest how many tests are needed to make it sufficiently unlikely
 * that a composite number will pass them all */
unsigned miller_rabin_checks_needed(unsigned bits);

/* An extension to the M-R test, which iterates until it either finds
 * a witness value that is potentially a primitive root, or one
 * that proves the number to be composite. */
mp_int *miller_rabin_find_potential_primitive_root(MillerRabin *mr);

/* ----------------------------------------------------------------------
 * A system for proving numbers to be prime, using the Pocklington
 * test, which requires knowing a partial factorisation of p-1
 * (specifically, factors whose product is at least cbrt(p)) and a
 * primitive root.
 *
 * The API consists of instantiating a 'Pockle' object, which
 * internally stores a list of numbers you've already convinced it is
 * prime, and can accept further primes if you give a satisfactory
 * certificate of their primality based on primes it already knows
 * about.
 */

typedef struct Pockle Pockle;

/* In real use, you only really need to know whether the Pockle
 * successfully accepted your prime. But for testcrypt, it's useful to
 * expose many different failure modes so we can try to provoke them
 * all in unit tests and check they're working. */
#define POCKLE_STATUSES(X)                      \
    X(POCKLE_OK)                                \
    X(POCKLE_SMALL_PRIME_NOT_SMALL)             \
    X(POCKLE_SMALL_PRIME_NOT_PRIME)             \
    X(POCKLE_PRIME_SMALLER_THAN_2)              \
    X(POCKLE_FACTOR_NOT_KNOWN_PRIME)            \
    X(POCKLE_FACTOR_NOT_A_FACTOR)               \
    X(POCKLE_PRODUCT_OF_FACTORS_TOO_SMALL)      \
    X(POCKLE_FERMAT_TEST_FAILED)                \
    X(POCKLE_DISCRIMINANT_IS_SQUARE)            \
    X(POCKLE_WITNESS_POWER_IS_1)                \
    X(POCKLE_WITNESS_POWER_NOT_COPRIME)         \
    /* end of list */

#define DEFINE_ENUM(id) id,
typedef enum PockleStatus { POCKLE_STATUSES(DEFINE_ENUM) } PockleStatus;
#undef DEFINE_ENUM

/* Make a new empty Pockle, containing no primes. */
Pockle *pockle_new(void);

/* Insert a prime below 2^32 into the Pockle. No evidence is required:
 * Pockle will check it itself. */
PockleStatus pockle_add_small_prime(Pockle *pockle, mp_int *p);

/* Insert a general prime into the Pockle. You must provide a list of
 * prime factors of p-1, whose product exceeds the cube root of p, and
 * also a primitive root mod p. */
PockleStatus pockle_add_prime(Pockle *pockle, mp_int *p,
                              mp_int **factors, size_t nfactors,
                              mp_int *primitive_root);

/* If you call pockle_mark, and later pass the returned value to
 * pockle_release, it will free all the primes that were added to the
 * Pockle between those two calls. Useful in recursive algorithms, to
 * stop the Pockle growing unboundedly if the recursion keeps having
 * to backtrack. */
size_t pockle_mark(Pockle *pockle);
void pockle_release(Pockle *pockle, size_t mark);

/* Free a Pockle. */
void pockle_free(Pockle *pockle);

/* Generate a certificate of primality for a prime already known to
 * the Pockle, in a format acceptable to Math::Prime::Util. */
strbuf *pockle_mpu(Pockle *pockle, mp_int *p);

/* ----------------------------------------------------------------------
 * Callback API that allows key generation to report progress to its
 * caller.
 */

typedef struct ProgressReceiverVtable ProgressReceiverVtable;
typedef struct ProgressReceiver ProgressReceiver;
typedef union ProgressPhase ProgressPhase;

union ProgressPhase {
    int n;
    void *p;
};

struct ProgressReceiver {
    const ProgressReceiverVtable *vt;
};

struct ProgressReceiverVtable {
    ProgressPhase (*add_linear)(ProgressReceiver *prog, double overall_cost);
    ProgressPhase (*add_probabilistic)(ProgressReceiver *prog,
                                       double cost_per_attempt,
                                       double attempt_probability);
    void (*ready)(ProgressReceiver *prog);
    void (*start_phase)(ProgressReceiver *prog, ProgressPhase phase);
    void (*report)(ProgressReceiver *prog, double progress);
    void (*report_attempt)(ProgressReceiver *prog);
    void (*report_phase_complete)(ProgressReceiver *prog);
};

static inline ProgressPhase progress_add_linear(ProgressReceiver *prog,
                                                double c)
{ return prog->vt->add_linear(prog, c); }
static inline ProgressPhase progress_add_probabilistic(ProgressReceiver *prog,
                                                       double c, double p)
{ return prog->vt->add_probabilistic(prog, c, p); }
static inline void progress_ready(ProgressReceiver *prog)
{ prog->vt->ready(prog); }
static inline void progress_start_phase(
    ProgressReceiver *prog, ProgressPhase phase)
{ prog->vt->start_phase(prog, phase); }
static inline void progress_report(ProgressReceiver *prog, double progress)
{ prog->vt->report(prog, progress); }
static inline void progress_report_attempt(ProgressReceiver *prog)
{ prog->vt->report_attempt(prog); }
static inline void progress_report_phase_complete(ProgressReceiver *prog)
{ prog->vt->report_phase_complete(prog); }

ProgressPhase null_progress_add_linear(
    ProgressReceiver *prog, double c);
ProgressPhase null_progress_add_probabilistic(
    ProgressReceiver *prog, double c, double p);
void null_progress_ready(ProgressReceiver *prog);
void null_progress_start_phase(ProgressReceiver *prog, ProgressPhase phase);
void null_progress_report(ProgressReceiver *prog, double progress);
void null_progress_report_attempt(ProgressReceiver *prog);
void null_progress_report_phase_complete(ProgressReceiver *prog);
extern const ProgressReceiverVtable null_progress_vt;

/* A helper function for dreaming up progress cost estimates. */
double estimate_modexp_cost(unsigned bits);

/* ----------------------------------------------------------------------
 * The top-level API for generating primes.
 */

typedef struct PrimeGenerationPolicy PrimeGenerationPolicy;
typedef struct PrimeGenerationContext PrimeGenerationContext;

struct PrimeGenerationContext {
    const PrimeGenerationPolicy *vt;
};

struct PrimeGenerationPolicy {
    ProgressPhase (*add_progress_phase)(const PrimeGenerationPolicy *policy,
                                        ProgressReceiver *prog, unsigned bits);
    PrimeGenerationContext *(*new_context)(
        const PrimeGenerationPolicy *policy);
    void (*free_context)(PrimeGenerationContext *ctx);
    mp_int *(*generate)(
        PrimeGenerationContext *ctx,
        PrimeCandidateSource *pcs, ProgressReceiver *prog);
    strbuf *(*mpu_certificate)(PrimeGenerationContext *ctx, mp_int *p);

    const void *extra; /* additional data a particular impl might need */
};

static inline ProgressPhase primegen_add_progress_phase(
    PrimeGenerationContext *ctx, ProgressReceiver *prog, unsigned bits)
{ return ctx->vt->add_progress_phase(ctx->vt, prog, bits); }
static inline PrimeGenerationContext *primegen_new_context(
    const PrimeGenerationPolicy *policy)
{ return policy->new_context(policy); }
static inline void primegen_free_context(PrimeGenerationContext *ctx)
{ ctx->vt->free_context(ctx); }
static inline mp_int *primegen_generate(
    PrimeGenerationContext *ctx,
    PrimeCandidateSource *pcs, ProgressReceiver *prog)
{ return ctx->vt->generate(ctx, pcs, prog); }
static inline strbuf *primegen_mpu_certificate(
    PrimeGenerationContext *ctx, mp_int *p)
{ return ctx->vt->mpu_certificate(ctx, p); }

extern const PrimeGenerationPolicy primegen_probabilistic;
extern const PrimeGenerationPolicy primegen_provable_fast;
extern const PrimeGenerationPolicy primegen_provable_maurer_simple;
extern const PrimeGenerationPolicy primegen_provable_maurer_complex;

/* ----------------------------------------------------------------------
 * The overall top-level API for generating entire key pairs.
 */

int rsa_generate(RSAKey *key, int bits, bool strong,
                 PrimeGenerationContext *pgc, ProgressReceiver *prog);
int dsa_generate(struct dss_key *key, int bits, PrimeGenerationContext *pgc,
                 ProgressReceiver *prog);
int ecdsa_generate(struct ecdsa_key *key, int bits);
int eddsa_generate(struct eddsa_key *key, int bits);
