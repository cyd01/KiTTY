/*
 * Packet protocol layer for the SSH-1 login phase, from the server side.
 */

#include <assert.h>

#include "putty.h"
#include "mpint.h"
#include "ssh.h"
#include "sshbpp.h"
#include "sshppl.h"
#include "sshcr.h"
#include "sshserver.h"

struct ssh1_login_server_state {
    int crState;

    PacketProtocolLayer *successor_layer;

    int remote_protoflags;
    int local_protoflags;
    unsigned long supported_ciphers_mask, supported_auths_mask;
    unsigned cipher_type;

    unsigned char cookie[8];
    unsigned char session_key[32];
    unsigned char session_id[16];
    char *username_str;
    ptrlen username;

    RSAKey *servkey, *hostkey;
    bool servkey_generated_here;
    mp_int *sesskey;

    AuthPolicy *authpolicy;
    unsigned ap_methods, current_method;
    unsigned char auth_rsa_expected_response[16];
    RSAKey *authkey;
    bool auth_successful;

    PacketProtocolLayer ppl;
};

static void ssh1_login_server_free(PacketProtocolLayer *); 
static void ssh1_login_server_process_queue(PacketProtocolLayer *);

static bool ssh1_login_server_get_specials(
    PacketProtocolLayer *ppl, add_special_fn_t add_special,
    void *ctx) { return false; }
static void ssh1_login_server_special_cmd(PacketProtocolLayer *ppl,
                                   SessionSpecialCode code, int arg) {}
static bool ssh1_login_server_want_user_input(
    PacketProtocolLayer *ppl) { return false; }
static void ssh1_login_server_got_user_input(PacketProtocolLayer *ppl) {}
static void ssh1_login_server_reconfigure(
    PacketProtocolLayer *ppl, Conf *conf) {}

static const struct PacketProtocolLayerVtable ssh1_login_server_vtable = {
    ssh1_login_server_free,
    ssh1_login_server_process_queue,
    ssh1_login_server_get_specials,
    ssh1_login_server_special_cmd,
    ssh1_login_server_want_user_input,
    ssh1_login_server_got_user_input,
    ssh1_login_server_reconfigure,
    NULL /* no layer names in SSH-1 */,
};

static void no_progress(void *param, int action, int phase, int iprogress) {}

PacketProtocolLayer *ssh1_login_server_new(
    PacketProtocolLayer *successor_layer, RSAKey *hostkey,
    AuthPolicy *authpolicy)
{
    struct ssh1_login_server_state *s = snew(struct ssh1_login_server_state);
    memset(s, 0, sizeof(*s));
    s->ppl.vt = &ssh1_login_server_vtable;

    s->hostkey = hostkey;
    s->authpolicy = authpolicy;

    s->successor_layer = successor_layer;
    return &s->ppl;
}

static void ssh1_login_server_free(PacketProtocolLayer *ppl)
{
    struct ssh1_login_server_state *s =
        container_of(ppl, struct ssh1_login_server_state, ppl);

    if (s->successor_layer)
        ssh_ppl_free(s->successor_layer);

    if (s->servkey_generated_here && s->servkey) {
        freersakey(s->servkey);
        sfree(s->servkey);
    }

    smemclr(s->session_key, sizeof(s->session_key));
    sfree(s->username_str);

    sfree(s);
}

static bool ssh1_login_server_filter_queue(struct ssh1_login_server_state *s)
{
    return ssh1_common_filter_queue(&s->ppl);
}

static PktIn *ssh1_login_server_pop(struct ssh1_login_server_state *s)
{
    if (ssh1_login_server_filter_queue(s))
        return NULL;
    return pq_pop(s->ppl.in_pq);
}

static void ssh1_login_server_process_queue(PacketProtocolLayer *ppl)
{
    struct ssh1_login_server_state *s =
        container_of(ppl, struct ssh1_login_server_state, ppl);
    PktIn *pktin;
    PktOut *pktout;
    int i;

    /* Filter centrally handled messages off the front of the queue on
     * every entry to this coroutine, no matter where we're resuming
     * from, even if we're _not_ looping on pq_pop. That way we can
     * still proactively handle those messages even if we're waiting
     * for a user response. */
    if (ssh1_login_server_filter_queue(s))
        return;

    crBegin(s->crState);

    if (!s->servkey) {
        int server_key_bits = s->hostkey->bytes - 256;
        if (server_key_bits < 512)
            server_key_bits = s->hostkey->bytes + 256;
        s->servkey = snew(RSAKey);
        rsa_generate(s->servkey, server_key_bits, no_progress, NULL);
        s->servkey->comment = NULL;
        s->servkey_generated_here = true;
    }

    s->local_protoflags = SSH1_PROTOFLAGS_SUPPORTED;
    /* FIXME: ability to configure this to a subset */
    s->supported_ciphers_mask = ((1U << SSH_CIPHER_3DES) |
                                 (1U << SSH_CIPHER_BLOWFISH) |
                                 (1U << SSH_CIPHER_DES));
    s->supported_auths_mask = 0;
    s->ap_methods = auth_methods(s->authpolicy);
    if (s->ap_methods & AUTHMETHOD_PASSWORD)
        s->supported_auths_mask |= (1U << SSH1_AUTH_PASSWORD);
    if (s->ap_methods & AUTHMETHOD_PUBLICKEY)
        s->supported_auths_mask |= (1U << SSH1_AUTH_RSA);
    if (s->ap_methods & AUTHMETHOD_TIS)
        s->supported_auths_mask |= (1U << SSH1_AUTH_TIS);
    if (s->ap_methods & AUTHMETHOD_CRYPTOCARD)
        s->supported_auths_mask |= (1U << SSH1_AUTH_CCARD);

    random_read(s->cookie, 8);

    pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH1_SMSG_PUBLIC_KEY);
    put_data(pktout, s->cookie, 8);
    rsa_ssh1_public_blob(BinarySink_UPCAST(pktout),
                         s->servkey, RSA_SSH1_EXPONENT_FIRST);
    rsa_ssh1_public_blob(BinarySink_UPCAST(pktout),
                         s->hostkey, RSA_SSH1_EXPONENT_FIRST);
    put_uint32(pktout, s->local_protoflags);
    put_uint32(pktout, s->supported_ciphers_mask);
    put_uint32(pktout, s->supported_auths_mask);
    pq_push(s->ppl.out_pq, pktout);

    crMaybeWaitUntilV((pktin = ssh1_login_server_pop(s)) != NULL);
    if (pktin->type != SSH1_CMSG_SESSION_KEY) {
        ssh_proto_error(s->ppl.ssh, "Received unexpected packet in response"
                        " to initial public key packet, type %d (%s)",
                        pktin->type, ssh1_pkt_type(pktin->type));
        return;
    }

    {
        ptrlen client_cookie;
        s->cipher_type = get_byte(pktin);
        client_cookie = get_data(pktin, 8);
        s->sesskey = get_mp_ssh1(pktin);
        s->remote_protoflags = get_uint32(pktin);

        if (get_err(pktin)) {
            ssh_proto_error(s->ppl.ssh, "Unable to parse session key packet");
            return;
        }
        if (!ptrlen_eq_ptrlen(client_cookie, make_ptrlen(s->cookie, 8))) {
            ssh_proto_error(s->ppl.ssh,
                            "Client sent incorrect anti-spoofing cookie");
            return;
        }
    }
    if (s->cipher_type >= 32 ||
        !((s->supported_ciphers_mask >> s->cipher_type) & 1)) {
        ssh_proto_error(s->ppl.ssh, "Client selected an unsupported cipher");
        return;
    }

    {
        RSAKey *smaller, *larger;
        strbuf *data = strbuf_new_nm();

        if (mp_get_nbits(s->hostkey->modulus) >
            mp_get_nbits(s->servkey->modulus)) {
            larger = s->hostkey;
            smaller = s->servkey;
        } else {
            smaller = s->hostkey;
            larger = s->servkey;
        }

        if (rsa_ssh1_decrypt_pkcs1(s->sesskey, larger, data)) {
            mp_free(s->sesskey);
            s->sesskey = mp_from_bytes_be(ptrlen_from_strbuf(data));
            data->len = 0;
            if (rsa_ssh1_decrypt_pkcs1(s->sesskey, smaller, data) &&
                data->len == sizeof(s->session_key)) {
                memcpy(s->session_key, data->u, sizeof(s->session_key));
                mp_free(s->sesskey);
                s->sesskey = NULL;     /* indicates success */
            }
        }

        strbuf_free(data);
    }
    if (s->sesskey) {
        ssh_proto_error(s->ppl.ssh, "Failed to decrypt session key");
        return;
    }

    ssh1_compute_session_id(s->session_id, s->cookie, s->hostkey, s->servkey);

    for (i = 0; i < 16; i++)
        s->session_key[i] ^= s->session_id[i];

    {
        const ssh_cipheralg *cipher =
            (s->cipher_type == SSH_CIPHER_BLOWFISH ? &ssh_blowfish_ssh1 :
             s->cipher_type == SSH_CIPHER_DES ? &ssh_des : &ssh_3des_ssh1);
        ssh1_bpp_new_cipher(s->ppl.bpp, cipher, s->session_key);
    }

    pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH1_SMSG_SUCCESS);
    pq_push(s->ppl.out_pq, pktout);

    crMaybeWaitUntilV((pktin = ssh1_login_server_pop(s)) != NULL);
    if (pktin->type != SSH1_CMSG_USER) {
        ssh_proto_error(s->ppl.ssh, "Received unexpected packet while "
                        "expecting username, type %d (%s)",
                        pktin->type, ssh1_pkt_type(pktin->type));
        return;
    }
    s->username = get_string(pktin);
    s->username.ptr = s->username_str = mkstr(s->username);
    ppl_logevent("Received username '%.*s'", PTRLEN_PRINTF(s->username));

    s->auth_successful = auth_none(s->authpolicy, s->username);
    while (1) {
        /* Signal failed authentication */
        pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH1_SMSG_FAILURE);
        pq_push(s->ppl.out_pq, pktout);

        crMaybeWaitUntilV((pktin = ssh1_login_server_pop(s)) != NULL);
        if (pktin->type == SSH1_CMSG_AUTH_PASSWORD) {
            s->current_method = AUTHMETHOD_PASSWORD;
            if (!(s->ap_methods & s->current_method))
                continue;

            ptrlen password = get_string(pktin);

            /* Tolerate historic traffic-analysis defence of NUL +
             * garbage on the end of the binary password string */
            char *nul = memchr(password.ptr, '\0', password.len);
            if (nul)
                password.len = (const char *)nul - (const char *)password.ptr;

            if (auth_password(s->authpolicy, s->username, password, NULL))
                goto auth_success;
        } else if (pktin->type == SSH1_CMSG_AUTH_RSA) {
            s->current_method = AUTHMETHOD_PUBLICKEY;
            if (!(s->ap_methods & s->current_method))
                continue;

            {
                mp_int *modulus = get_mp_ssh1(pktin);
                s->authkey = auth_publickey_ssh1(
                    s->authpolicy, s->username, modulus);
                mp_free(modulus);
            }

            if (!s->authkey)
                continue;

            if (s->authkey->bytes < 32) {
                ppl_logevent("Auth key far too small");
                continue;
            }

            {
                unsigned char *rsabuf =
                    snewn(s->authkey->bytes, unsigned char);

                random_read(rsabuf, 32);

                {
                    ssh_hash *h = ssh_hash_new(&ssh_md5);
                    put_data(h, rsabuf, 32);
                    put_data(h, s->session_id, 16);
                    ssh_hash_final(h, s->auth_rsa_expected_response);
                }

                if (!rsa_ssh1_encrypt(rsabuf, 32, s->authkey)) {
                    sfree(rsabuf);
                    ppl_logevent("Failed to encrypt auth challenge");
                    continue;
                }

                mp_int *bn = mp_from_bytes_be(
                    make_ptrlen(rsabuf, s->authkey->bytes));
                smemclr(rsabuf, s->authkey->bytes);
                sfree(rsabuf);

                pktout = ssh_bpp_new_pktout(
                    s->ppl.bpp, SSH1_SMSG_AUTH_RSA_CHALLENGE);
                put_mp_ssh1(pktout, bn);
                pq_push(s->ppl.out_pq, pktout);

                mp_free(bn);
            }

            crMaybeWaitUntilV((pktin = ssh1_login_server_pop(s)) != NULL);
            if (pktin->type != SSH1_CMSG_AUTH_RSA_RESPONSE) {
                ssh_proto_error(s->ppl.ssh, "Received unexpected packet in "
                                "response to RSA auth challenge, type %d (%s)",
                                pktin->type, ssh1_pkt_type(pktin->type));
                return;
            }

            {
                ptrlen response = get_data(pktin, 16);
                ptrlen expected = make_ptrlen(
                    s->auth_rsa_expected_response, 16);
                if (!ptrlen_eq_ptrlen(response, expected)) {
                    ppl_logevent("Wrong response to auth challenge");
                    continue;
                }
            }

            goto auth_success;
        } else if (pktin->type == SSH1_CMSG_AUTH_TIS ||
                   pktin->type == SSH1_CMSG_AUTH_CCARD) {
            char *challenge;
            unsigned response_type;
            ptrlen response;

            s->current_method = (pktin->type == SSH1_CMSG_AUTH_TIS ?
                                 AUTHMETHOD_TIS : AUTHMETHOD_CRYPTOCARD);
            if (!(s->ap_methods & s->current_method))
                continue;

            challenge = auth_ssh1int_challenge(
                s->authpolicy, s->current_method, s->username);
            if (!challenge)
                continue;
            pktout = ssh_bpp_new_pktout(
                s->ppl.bpp,
                (s->current_method == AUTHMETHOD_TIS ?
                 SSH1_SMSG_AUTH_TIS_CHALLENGE :
                 SSH1_SMSG_AUTH_CCARD_CHALLENGE));
            put_stringz(pktout, challenge);
            pq_push(s->ppl.out_pq, pktout);
            sfree(challenge);

            crMaybeWaitUntilV((pktin = ssh1_login_server_pop(s)) != NULL);
            response_type = (s->current_method == AUTHMETHOD_TIS ?
                             SSH1_CMSG_AUTH_TIS_RESPONSE :
                             SSH1_CMSG_AUTH_CCARD_RESPONSE);
            if (pktin->type != response_type) {
                ssh_proto_error(s->ppl.ssh, "Received unexpected packet in "
                                "response to %s challenge, type %d (%s)",
                                (s->current_method == AUTHMETHOD_TIS ?
                                 "TIS" : "CryptoCard"),
                                pktin->type, ssh1_pkt_type(pktin->type));
                return;
            }

            response = get_string(pktin);

            if (auth_ssh1int_response(s->authpolicy, response))
                goto auth_success;
        }
    }

  auth_success:
    if (!auth_successful(s->authpolicy, s->username, s->current_method)) {
        ssh_sw_abort(s->ppl.ssh, "Multiple authentications required but SSH-1"
                     " cannot perform them");
        return;
    }

    /* Signal successful authentication */
    pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH1_SMSG_SUCCESS);
    pq_push(s->ppl.out_pq, pktout);

    ssh1_connection_set_protoflags(
        s->successor_layer, s->local_protoflags, s->remote_protoflags);
    {
        PacketProtocolLayer *successor = s->successor_layer;
        s->successor_layer = NULL;     /* avoid freeing it ourself */
        ssh_ppl_replace(&s->ppl, successor);
        return;   /* we've just freed s, so avoid even touching s->crState */
    }

    crFinishV;
}
