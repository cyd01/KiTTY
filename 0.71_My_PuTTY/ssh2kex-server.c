/*
 * Server side of key exchange for the SSH-2 transport protocol (RFC 4253).
 */

#include <assert.h>

#include "putty.h"
#include "ssh.h"
#include "sshbpp.h"
#include "sshppl.h"
#include "sshcr.h"
#include "storage.h"
#include "ssh2transport.h"
#include "mpint.h"

void ssh2_transport_provide_hostkeys(PacketProtocolLayer *ppl,
                                     ssh_key *const *hostkeys, int nhostkeys)
{
    struct ssh2_transport_state *s =
        container_of(ppl, struct ssh2_transport_state, ppl);

    s->hostkeys = hostkeys;
    s->nhostkeys = nhostkeys;
}

static strbuf *finalise_and_sign_exhash(struct ssh2_transport_state *s)
{
    strbuf *sb;
    ssh2transport_finalise_exhash(s);
    sb = strbuf_new();
    ssh_key_sign(
        s->hkey, make_ptrlen(s->exchange_hash, s->kex_alg->hash->hlen),
        0, BinarySink_UPCAST(sb));
    return sb;
}

static void no_progress(void *param, int action, int phase, int iprogress)
{
}

void ssh2kex_coroutine(struct ssh2_transport_state *s, bool *aborted)
{
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    PktIn *pktin;
    PktOut *pktout;

    crBegin(s->crStateKex);

    {
        int i;
        for (i = 0; i < s->nhostkeys; i++)
            if (ssh_key_alg(s->hostkeys[i]) == s->hostkey_alg) {
                s->hkey = s->hostkeys[i];
                break;
            }
        assert(s->hkey);
    }

    s->hostkeyblob->len = 0;
    ssh_key_public_blob(s->hkey, BinarySink_UPCAST(s->hostkeyblob));
    s->hostkeydata = ptrlen_from_strbuf(s->hostkeyblob);

    put_stringpl(s->exhash, s->hostkeydata);

    if (s->kex_alg->main_type == KEXTYPE_DH) {
        /*
         * If we're doing Diffie-Hellman group exchange, start by
         * waiting for the group request.
         */
        if (dh_is_gex(s->kex_alg)) {
            ppl_logevent("Doing Diffie-Hellman group exchange");
            s->ppl.bpp->pls->kctx = SSH2_PKTCTX_DHGEX;

            crMaybeWaitUntilV((pktin = ssh2_transport_pop(s)) != NULL);
            if (pktin->type != SSH2_MSG_KEX_DH_GEX_REQUEST &&
                pktin->type != SSH2_MSG_KEX_DH_GEX_REQUEST_OLD) {
                ssh_proto_error(s->ppl.ssh, "Received unexpected packet when "
                                "expecting Diffie-Hellman group exchange "
                                "request, type %d (%s)", pktin->type,
                                ssh2_pkt_type(s->ppl.bpp->pls->kctx,
                                              s->ppl.bpp->pls->actx,
                                              pktin->type));
                *aborted = true;
                return;
            }

            if (pktin->type != SSH2_MSG_KEX_DH_GEX_REQUEST_OLD) {
                s->dh_got_size_bounds = true;
                s->dh_min_size = get_uint32(pktin);
                s->pbits = get_uint32(pktin);
                s->dh_max_size = get_uint32(pktin);
            } else {
                s->dh_got_size_bounds = false;
                s->pbits = get_uint32(pktin);
            }

            /*
             * This is a hopeless strategy for making a secure DH
             * group! It's good enough for testing a client against,
             * but not for serious use.
             */
            s->p = primegen(s->pbits, 2, 2, NULL, 1, no_progress, NULL, 1);
            s->g = mp_from_integer(2);
            s->dh_ctx = dh_setup_gex(s->p, s->g);
            s->kex_init_value = SSH2_MSG_KEX_DH_GEX_INIT;
            s->kex_reply_value = SSH2_MSG_KEX_DH_GEX_REPLY;

            pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_KEX_DH_GEX_GROUP);
            put_mp_ssh2(pktout, s->p);
            put_mp_ssh2(pktout, s->g);
            pq_push(s->ppl.out_pq, pktout);
        } else {
            s->ppl.bpp->pls->kctx = SSH2_PKTCTX_DHGROUP;
            s->dh_ctx = dh_setup_group(s->kex_alg);
            s->kex_init_value = SSH2_MSG_KEXDH_INIT;
            s->kex_reply_value = SSH2_MSG_KEXDH_REPLY;
            ppl_logevent("Using Diffie-Hellman with standard group \"%s\"",
                         s->kex_alg->groupname);
        }

        ppl_logevent("Doing Diffie-Hellman key exchange with hash %s",
                     ssh_hash_alg(s->exhash)->text_name);

        /*
         * Generate e for Diffie-Hellman.
         */
        s->e = dh_create_e(s->dh_ctx, s->nbits * 2);

        /*
         * Wait to receive f.
         */
        crMaybeWaitUntilV((pktin = ssh2_transport_pop(s)) != NULL);
        if (pktin->type != s->kex_init_value) {
            ssh_proto_error(s->ppl.ssh, "Received unexpected packet when "
                            "expecting Diffie-Hellman initial packet, "
                            "type %d (%s)", pktin->type,
                            ssh2_pkt_type(s->ppl.bpp->pls->kctx,
                                          s->ppl.bpp->pls->actx,
                                          pktin->type));
            *aborted = true;
            return;
        }
        s->f = get_mp_ssh2(pktin);
        if (get_err(pktin)) {
            ssh_proto_error(s->ppl.ssh,
                            "Unable to parse Diffie-Hellman initial packet");
            *aborted = true;
            return;
        }

        {
            const char *err = dh_validate_f(s->dh_ctx, s->f);
            if (err) {
                ssh_proto_error(s->ppl.ssh, "Diffie-Hellman initial packet "
                                "failed validation: %s", err);
                *aborted = true;
                return;
            }
        }
        s->K = dh_find_K(s->dh_ctx, s->f);

        if (dh_is_gex(s->kex_alg)) {
            if (s->dh_got_size_bounds)
                put_uint32(s->exhash, s->dh_min_size);
            put_uint32(s->exhash, s->pbits);
            if (s->dh_got_size_bounds)
                put_uint32(s->exhash, s->dh_max_size);
            put_mp_ssh2(s->exhash, s->p);
            put_mp_ssh2(s->exhash, s->g);
        }
        put_mp_ssh2(s->exhash, s->f);
        put_mp_ssh2(s->exhash, s->e);

        pktout = ssh_bpp_new_pktout(s->ppl.bpp, s->kex_reply_value);
        put_stringpl(pktout, s->hostkeydata);
        put_mp_ssh2(pktout, s->e);
        put_stringsb(pktout, finalise_and_sign_exhash(s));
        pq_push(s->ppl.out_pq, pktout);

        dh_cleanup(s->dh_ctx);
        s->dh_ctx = NULL;
        mp_free(s->f); s->f = NULL;
        if (dh_is_gex(s->kex_alg)) {
            mp_free(s->g); s->g = NULL;
            mp_free(s->p); s->p = NULL;
        }
    } else if (s->kex_alg->main_type == KEXTYPE_ECDH) {
        ppl_logevent("Doing ECDH key exchange with curve %s and hash %s",
                     ssh_ecdhkex_curve_textname(s->kex_alg),
                     ssh_hash_alg(s->exhash)->text_name);
        s->ppl.bpp->pls->kctx = SSH2_PKTCTX_ECDHKEX;

        s->ecdh_key = ssh_ecdhkex_newkey(s->kex_alg);
        if (!s->ecdh_key) {
            ssh_sw_abort(s->ppl.ssh, "Unable to generate key for ECDH");
            *aborted = true;
            return;
        }

        crMaybeWaitUntilV((pktin = ssh2_transport_pop(s)) != NULL);
        if (pktin->type != SSH2_MSG_KEX_ECDH_INIT) {
            ssh_proto_error(s->ppl.ssh, "Received unexpected packet when "
                            "expecting ECDH initial packet, type %d (%s)",
                            pktin->type,
                            ssh2_pkt_type(s->ppl.bpp->pls->kctx,
                                          s->ppl.bpp->pls->actx,
                                          pktin->type));
            *aborted = true;
            return;
        }

        {
            ptrlen keydata = get_string(pktin);
            put_stringpl(s->exhash, keydata);

            s->K = ssh_ecdhkex_getkey(s->ecdh_key, keydata);
            if (!get_err(pktin) && !s->K) {
                ssh_proto_error(s->ppl.ssh, "Received invalid elliptic curve "
                                "point in ECDH initial packet");
                *aborted = true;
                return;
            }
        }

        pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_KEX_ECDH_REPLY);
        put_stringpl(pktout, s->hostkeydata);
        {
            strbuf *pubpoint = strbuf_new();
            ssh_ecdhkex_getpublic(s->ecdh_key, BinarySink_UPCAST(pubpoint));
            put_string(s->exhash, pubpoint->u, pubpoint->len);
            put_stringsb(pktout, pubpoint);
        }
        put_stringsb(pktout, finalise_and_sign_exhash(s));
        pq_push(s->ppl.out_pq, pktout);

        ssh_ecdhkex_freekey(s->ecdh_key);
        s->ecdh_key = NULL;
    } else if (s->kex_alg->main_type == KEXTYPE_GSS) {
        ssh_sw_abort(s->ppl.ssh, "GSS key exchange not supported in server");
    } else {
        assert(s->kex_alg->main_type == KEXTYPE_RSA);
        ppl_logevent("Doing RSA key exchange with hash %s",
                     ssh_hash_alg(s->exhash)->text_name);
        s->ppl.bpp->pls->kctx = SSH2_PKTCTX_RSAKEX;

        {
            const struct ssh_rsa_kex_extra *extra =
                (const struct ssh_rsa_kex_extra *)s->kex_alg->extra;

	    s->rsa_kex_key = snew(RSAKey);
	    rsa_generate(s->rsa_kex_key, extra->minklen, no_progress, NULL);
	    s->rsa_kex_key->comment = NULL;
        }

        pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_KEXRSA_PUBKEY);
        put_stringpl(pktout, s->hostkeydata);
        {
            strbuf *pubblob = strbuf_new();
            ssh_key_public_blob(&s->rsa_kex_key->sshk,
                                BinarySink_UPCAST(pubblob));
            put_string(s->exhash, pubblob->u, pubblob->len);
            put_stringsb(pktout, pubblob);
        }
        pq_push(s->ppl.out_pq, pktout);

        crMaybeWaitUntilV((pktin = ssh2_transport_pop(s)) != NULL);
        if (pktin->type != SSH2_MSG_KEXRSA_SECRET) {
            ssh_proto_error(s->ppl.ssh, "Received unexpected packet when "
                            "expecting RSA kex secret, type %d (%s)",
                            pktin->type,
                            ssh2_pkt_type(s->ppl.bpp->pls->kctx,
                                          s->ppl.bpp->pls->actx,
                                          pktin->type));
            *aborted = true;
            return;
        }

        {
            ptrlen encrypted_secret = get_string(pktin);
            put_stringpl(s->exhash, encrypted_secret);
            s->K = ssh_rsakex_decrypt(
                s->rsa_kex_key, s->kex_alg->hash, encrypted_secret);
        }

        if (!s->K) {
            ssh_proto_error(s->ppl.ssh, "Unable to decrypt RSA kex secret");
            *aborted = true;
            return;
        }

        ssh_rsakex_freekey(s->rsa_kex_key);
        s->rsa_kex_key = NULL;

        pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_KEXRSA_DONE);
        put_stringsb(pktout, finalise_and_sign_exhash(s));
        pq_push(s->ppl.out_pq, pktout);
    }

    crFinishV;
}
