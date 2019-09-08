/*
 * Packet protocol layer for the server side of the SSH-2 userauth
 * protocol (RFC 4252).
 */

#include <assert.h>

#include "putty.h"
#include "ssh.h"
#include "sshbpp.h"
#include "sshppl.h"
#include "sshcr.h"
#include "sshserver.h"

#ifndef NO_GSSAPI
#include "sshgssc.h"
#include "sshgss.h"
#endif

struct ssh2_userauth_server_state {
    int crState;

    PacketProtocolLayer *transport_layer, *successor_layer;
    ptrlen session_id;

    AuthPolicy *authpolicy;

    ptrlen username, service, method;
    unsigned methods, this_method;
    bool partial_success;

    AuthKbdInt *aki;

    PacketProtocolLayer ppl;
};

static void ssh2_userauth_server_free(PacketProtocolLayer *);
static void ssh2_userauth_server_process_queue(PacketProtocolLayer *);

static const struct PacketProtocolLayerVtable ssh2_userauth_server_vtable = {
    ssh2_userauth_server_free,
    ssh2_userauth_server_process_queue,
    NULL /* get_specials */,
    NULL /* special_cmd */,
    NULL /* want_user_input */,
    NULL /* got_user_input */,
    NULL /* reconfigure */,
    "ssh-userauth",
};

static void free_auth_kbdint(AuthKbdInt *aki)
{
    int i;

    if (!aki)
        return;

    sfree(aki->title);
    sfree(aki->instruction);
    for (i = 0; i < aki->nprompts; i++)
        sfree(aki->prompts[i].prompt);
    sfree(aki->prompts);
    sfree(aki);
}

PacketProtocolLayer *ssh2_userauth_server_new(
    PacketProtocolLayer *successor_layer, AuthPolicy *authpolicy)
{
    struct ssh2_userauth_server_state *s =
        snew(struct ssh2_userauth_server_state);
    memset(s, 0, sizeof(*s));
    s->ppl.vt = &ssh2_userauth_server_vtable;

    s->successor_layer = successor_layer;
    s->authpolicy = authpolicy;

    return &s->ppl;
}

void ssh2_userauth_server_set_transport_layer(PacketProtocolLayer *userauth,
                                              PacketProtocolLayer *transport)
{
    struct ssh2_userauth_server_state *s =
        container_of(userauth, struct ssh2_userauth_server_state, ppl);
    s->transport_layer = transport;
}

static void ssh2_userauth_server_free(PacketProtocolLayer *ppl)
{
    struct ssh2_userauth_server_state *s =
        container_of(ppl, struct ssh2_userauth_server_state, ppl);

    if (s->successor_layer)
        ssh_ppl_free(s->successor_layer);

    free_auth_kbdint(s->aki);

    sfree(s);
}

static PktIn *ssh2_userauth_server_pop(struct ssh2_userauth_server_state *s)
{
    return pq_pop(s->ppl.in_pq);
}

static void ssh2_userauth_server_add_session_id(
    struct ssh2_userauth_server_state *s, strbuf *sigdata)
{
    if (s->ppl.remote_bugs & BUG_SSH2_PK_SESSIONID) {
        put_datapl(sigdata, s->session_id);
    } else {
        put_stringpl(sigdata, s->session_id);
    }
}

static void ssh2_userauth_server_process_queue(PacketProtocolLayer *ppl)
{
    struct ssh2_userauth_server_state *s =
        container_of(ppl, struct ssh2_userauth_server_state, ppl);
    PktIn *pktin;
    PktOut *pktout;

    crBegin(s->crState);

    s->session_id = ssh2_transport_get_session_id(s->transport_layer);

    while (1) {
        crMaybeWaitUntilV((pktin = ssh2_userauth_server_pop(s)) != NULL);
        if (pktin->type != SSH2_MSG_USERAUTH_REQUEST) {
            ssh_proto_error(s->ppl.ssh, "Received unexpected packet when "
                            "expecting USERAUTH_REQUEST, type %d (%s)",
                            pktin->type,
                            ssh2_pkt_type(s->ppl.bpp->pls->kctx,
                                          s->ppl.bpp->pls->actx, pktin->type));
            return;
        }

        s->username = get_string(pktin);
        s->service = get_string(pktin);
        s->method = get_string(pktin);

        if (!ptrlen_eq_string(s->service, s->successor_layer->vt->name)) {
            /*
             * Unconditionally reject authentication for any service
             * other than the one we're going to hand over to.
             */
            pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_USERAUTH_FAILURE);
            put_stringz(pktout, "");
            put_bool(pktout, false);
            pq_push(s->ppl.out_pq, pktout);
            continue;
        }

        s->methods = auth_methods(s->authpolicy);
        s->partial_success = false;

        if (ptrlen_eq_string(s->method, "none")) {
            s->this_method = AUTHMETHOD_NONE;
            if (!(s->methods & s->this_method))
                goto failure;

            if (!auth_none(s->authpolicy, s->username))
                goto failure;
        } else if (ptrlen_eq_string(s->method, "password")) {
            bool changing;
            ptrlen password, new_password, *new_password_ptr;

            s->this_method = AUTHMETHOD_PASSWORD;
            if (!(s->methods & s->this_method))
                goto failure;

            changing = get_bool(pktin);
            password = get_string(pktin);

            if (changing) {
                new_password = get_string(pktin);
                new_password_ptr = &new_password;
            } else {
                new_password_ptr = NULL;
            }

            int result = auth_password(s->authpolicy, s->username,
                                       password, new_password_ptr);
            if (result == 2) {
                pktout = ssh_bpp_new_pktout(
                    s->ppl.bpp, SSH2_MSG_USERAUTH_PASSWD_CHANGEREQ);
                put_stringz(pktout, "Please change your password");
                put_stringz(pktout, ""); /* language tag */
                pq_push(s->ppl.out_pq, pktout);
                continue; /* skip USERAUTH_{SUCCESS,FAILURE} epilogue */
            } else if (result != 1) {
                goto failure;
            }
        } else if (ptrlen_eq_string(s->method, "publickey")) {
            bool has_signature, success;
            ptrlen algorithm, blob, signature;
            const ssh_keyalg *keyalg;
            ssh_key *key;
            strbuf *sigdata;

            s->this_method = AUTHMETHOD_PUBLICKEY;
            if (!(s->methods & s->this_method))
                goto failure;

            has_signature = get_bool(pktin);
            algorithm = get_string(pktin);
            blob = get_string(pktin);

            if (!auth_publickey(s->authpolicy, s->username, blob))
                goto failure;

            keyalg = find_pubkey_alg_len(algorithm);
            if (!keyalg)
                goto failure;
            key = ssh_key_new_pub(keyalg, blob);
            if (!key)
                goto failure;

            if (!has_signature) {
                ssh_key_free(key);
                pktout = ssh_bpp_new_pktout(
                    s->ppl.bpp, SSH2_MSG_USERAUTH_PK_OK);
                put_stringpl(pktout, algorithm);
                put_stringpl(pktout, blob);
                pq_push(s->ppl.out_pq, pktout);
                continue; /* skip USERAUTH_{SUCCESS,FAILURE} epilogue */
            }

            sigdata = strbuf_new();
            ssh2_userauth_server_add_session_id(s, sigdata);
            put_byte(sigdata, SSH2_MSG_USERAUTH_REQUEST);
            put_stringpl(sigdata, s->username);
            put_stringpl(sigdata, s->service);
            put_stringpl(sigdata, s->method);
            put_bool(sigdata, has_signature);
            put_stringpl(sigdata, algorithm);
            put_stringpl(sigdata, blob);

            signature = get_string(pktin);
            success = ssh_key_verify(key, signature,
                                     ptrlen_from_strbuf(sigdata));
            ssh_key_free(key);
            strbuf_free(sigdata);

            if (!success)
                goto failure;
        } else if (ptrlen_eq_string(s->method, "keyboard-interactive")) {
            int i, ok;
            unsigned n;

            s->this_method = AUTHMETHOD_KBDINT;
            if (!(s->methods & s->this_method))
                goto failure;

            do {
                s->aki = auth_kbdint_prompts(s->authpolicy, s->username);
                if (!s->aki)
                    goto failure;

                pktout = ssh_bpp_new_pktout(
                    s->ppl.bpp, SSH2_MSG_USERAUTH_INFO_REQUEST);
                put_stringz(pktout, s->aki->title);
                put_stringz(pktout, s->aki->instruction);
                put_stringz(pktout, ""); /* language tag */
                put_uint32(pktout, s->aki->nprompts);
                for (i = 0; i < s->aki->nprompts; i++) {
                    put_stringz(pktout, s->aki->prompts[i].prompt);
                    put_bool(pktout, s->aki->prompts[i].echo);
                }
                pq_push(s->ppl.out_pq, pktout);

                crMaybeWaitUntilV(
                    (pktin = ssh2_userauth_server_pop(s)) != NULL);
                if (pktin->type != SSH2_MSG_USERAUTH_INFO_RESPONSE) {
                    ssh_proto_error(
                        s->ppl.ssh, "Received unexpected packet when "
                        "expecting USERAUTH_INFO_RESPONSE, type %d (%s)",
                        pktin->type,
                        ssh2_pkt_type(s->ppl.bpp->pls->kctx,
                                      s->ppl.bpp->pls->actx, pktin->type));
                    return;
                }

                n = get_uint32(pktin);
                if (n != s->aki->nprompts) {
                    ssh_proto_error(
                        s->ppl.ssh, "Received %u keyboard-interactive "
                        "responses after sending %u prompts",
                        n, s->aki->nprompts);
                    return;
                }

                {
                    ptrlen *responses = snewn(s->aki->nprompts, ptrlen);
                    for (i = 0; i < s->aki->nprompts; i++)
                        responses[i] = get_string(pktin);
                    ok = auth_kbdint_responses(s->authpolicy, responses);
                    sfree(responses);
                }

                free_auth_kbdint(s->aki);
                s->aki = NULL;
            } while (ok == 0);

            if (ok <= 0)
                goto failure;
        } else {
            goto failure;
        }

        /*
         * If we get here, we've successfully completed this
         * authentication step.
         */
        if (auth_successful(s->authpolicy, s->username, s->this_method)) {
            /*
             * ... and it was the last one, so we're completely done.
             */
            pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_USERAUTH_SUCCESS);
            pq_push(s->ppl.out_pq, pktout);
            break;
        } else {
            /*
             * ... but another is required, so fall through to
             * generation of USERAUTH_FAILURE, having first refreshed
             * the bit mask of available methods.
             */
            s->methods = auth_methods(s->authpolicy);
        }
        s->partial_success = true;

      failure:
        pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_USERAUTH_FAILURE);
        {
            strbuf *list = strbuf_new();
            if (s->methods & AUTHMETHOD_NONE)
                add_to_commasep(list, "none");
            if (s->methods & AUTHMETHOD_PASSWORD)
                add_to_commasep(list, "password");
            if (s->methods & AUTHMETHOD_PUBLICKEY)
                add_to_commasep(list, "publickey");
            if (s->methods & AUTHMETHOD_KBDINT)
                add_to_commasep(list, "keyboard-interactive");
            put_stringsb(pktout, list);
        }
        put_bool(pktout, s->partial_success);
        pq_push(s->ppl.out_pq, pktout);
    }

    /*
     * Finally, hand over to our successor layer, and return
     * immediately without reaching the crFinishV: ssh_ppl_replace
     * will have freed us, so crFinishV's zeroing-out of crState would
     * be a use-after-free bug.
     */
    {
        PacketProtocolLayer *successor = s->successor_layer;
        s->successor_layer = NULL;     /* avoid freeing it ourself */
        ssh_ppl_replace(&s->ppl, successor);
        return;   /* we've just freed s, so avoid even touching s->crState */
    }

    crFinishV;
}
