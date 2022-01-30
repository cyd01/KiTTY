/*
 * DSS key generation.
 */

#include "misc.h"
#include "ssh.h"
#include "sshkeygen.h"
#include "mpint.h"

int dsa_generate(struct dss_key *key, int bits, PrimeGenerationContext *pgc,
                 ProgressReceiver *prog)
{
    /*
     * Progress-reporting setup.
     *
     * DSA generation involves three potentially long jobs: inventing
     * the small prime q, the large prime p, and finding an order-q
     * element of the multiplicative group of p.
     *
     * The latter is done by finding an element whose order is
     * _divisible_ by q and raising it to the power of (p-1)/q. Every
     * element whose order is not divisible by q is a qth power of q
     * distinct elements whose order _is_ divisible by q, so the
     * probability of not finding a suitable element on the first try
     * is in the region of 1/q, i.e. at most 2^-159.
     *
     * (So the probability of success will end up indistinguishable
     * from 1 in IEEE standard floating point! But what can you do.)
     */
    ProgressPhase phase_q = primegen_add_progress_phase(pgc, prog, 160);
    ProgressPhase phase_p = primegen_add_progress_phase(pgc, prog, bits);
    double g_failure_probability = 1.0
        / (double)(1ULL << 53)
        / (double)(1ULL << 53)
        / (double)(1ULL << 53);
    ProgressPhase phase_g = progress_add_probabilistic(
        prog, estimate_modexp_cost(bits), 1.0 - g_failure_probability);
    progress_ready(prog);

    PrimeCandidateSource *pcs;

    /*
     * Generate q: a prime of length 160.
     */
    progress_start_phase(prog, phase_q);
    pcs = pcs_new(160);
    mp_int *q = primegen_generate(pgc, pcs, prog);
    progress_report_phase_complete(prog);

    /*
     * Now generate p: a prime of length `bits', such that p-1 is
     * divisible by q.
     */
    progress_start_phase(prog, phase_p);
    pcs = pcs_new(bits);
    pcs_require_residue_1_mod_prime(pcs, q);
    mp_int *p = primegen_generate(pgc, pcs, prog);
    progress_report_phase_complete(prog);

    /*
     * Next we need g. Raise 2 to the power (p-1)/q modulo p, and
     * if that comes out to one then try 3, then 4 and so on. As
     * soon as we hit a non-unit (and non-zero!) one, that'll do
     * for g.
     */
    progress_start_phase(prog, phase_g);
    mp_int *power = mp_div(p, q); /* this is floor(p/q) == (p-1)/q */
    mp_int *h = mp_from_integer(2);
    mp_int *g;
    while (1) {
        progress_report_attempt(prog);
        g = mp_modpow(h, power, p);
        if (mp_hs_integer(g, 2))
            break;                     /* got one */
        mp_free(g);
        mp_add_integer_into(h, h, 1);
    }
    mp_free(h);
    mp_free(power);
    progress_report_phase_complete(prog);

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
