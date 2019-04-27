/*
 * EC key generation.
 */

#include "ssh.h"
#include "mpint.h"

int ecdsa_generate(struct ecdsa_key *ek, int bits,
                   progfn_t pfn, void *pfnparam)
{
    if (!ec_nist_alg_and_curve_by_bits(bits, &ek->curve, &ek->sshk.vt))
        return 0;

    mp_int *one = mp_from_integer(1);
    ek->privateKey = mp_random_in_range(one, ek->curve->w.G_order);
    mp_free(one);

    ek->publicKey = ecdsa_public(ek->privateKey, ek->sshk.vt);

    return 1;
}

int eddsa_generate(struct eddsa_key *ek, int bits,
                   progfn_t pfn, void *pfnparam)
{
    if (!ec_ed_alg_and_curve_by_bits(bits, &ek->curve, &ek->sshk.vt))
        return 0;

    /* EdDSA secret keys are just 32 bytes of hash preimage; the
     * 64-byte SHA-512 hash of that key will be used when signing,
     * but the form of the key stored on disk is the preimage
     * only. */
    ek->privateKey = mp_random_bits(bits);

    ek->publicKey = eddsa_public(ek->privateKey, ek->sshk.vt);

    return 1;
}
