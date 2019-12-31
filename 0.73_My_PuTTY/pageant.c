/*
 * pageant.c: cross-platform code to implement Pageant.
 */

#include <stddef.h>
#include <stdlib.h>
#include <assert.h>

#include "putty.h"
#include "mpint.h"
#include "ssh.h"
#include "sshcr.h"
#include "pageant.h"

/*
 * We need this to link with the RSA code, because rsa_ssh1_encrypt()
 * pads its data with random bytes. Since we only use rsa_ssh1_decrypt()
 * and the signing functions, which are deterministic, this should
 * never be called.
 *
 * If it _is_ called, there is a _serious_ problem, because it
 * won't generate true random numbers. So we must scream, panic,
 * and exit immediately if that should happen.
 */
 #ifndef MOD_INTEGRATED_AGENT
int random_byte(void)
{
    modalfatalbox("Internal error: attempt to use random numbers in Pageant");
    exit(0);
    return 0;                 /* unreachable, but placate optimiser */
}

void random_read(void *buf, size_t size)
{
    modalfatalbox("Internal error: attempt to use random numbers in Pageant");
}
#endif

static bool pageant_local = false;

/*
 * rsakeys stores SSH-1 RSA keys. ssh2keys stores all SSH-2 keys.
 */
static tree234 *rsakeys, *ssh2keys;

/*
 * Key comparison function for the 2-3-4 tree of RSA keys.
 */
static int cmpkeys_rsa(void *av, void *bv)
{
    RSAKey *a = (RSAKey *) av;
    RSAKey *b = (RSAKey *) bv;

    return ((int)mp_cmp_hs(a->modulus, b->modulus) -
            (int)mp_cmp_hs(b->modulus, a->modulus));
}

/*
 * Key comparison function for looking up a blob in the 2-3-4 tree
 * of SSH-2 keys.
 */
static int cmpkeys_ssh2_asymm(void *av, void *bv)
{
    ptrlen *ablob = (ptrlen *) av;
    ssh2_userkey *b = (ssh2_userkey *) bv;
    strbuf *bblob;
    int i, c;

    /*
     * Compare purely by public blob.
     */
    bblob = strbuf_new();
    ssh_key_public_blob(b->key, BinarySink_UPCAST(bblob));

    c = 0;
    for (i = 0; i < ablob->len && i < bblob->len; i++) {
        unsigned char abyte = ((unsigned char *)ablob->ptr)[i];
	if (abyte < bblob->u[i]) {
	    c = -1;
	    break;
	} else if (abyte > bblob->u[i]) {
	    c = +1;
	    break;
	}
    }
    if (c == 0 && i < ablob->len)
	c = +1;			       /* a is longer */
    if (c == 0 && i < bblob->len)
	c = -1;			       /* a is longer */

    strbuf_free(bblob);

    return c;
}

/*
 * Main key comparison function for the 2-3-4 tree of SSH-2 keys.
 */
static int cmpkeys_ssh2(void *av, void *bv)
{
    ssh2_userkey *a = (ssh2_userkey *) av;
    strbuf *ablob;
    ptrlen apl;
    int toret;

    ablob = strbuf_new();
    ssh_key_public_blob(a->key, BinarySink_UPCAST(ablob));
    apl.ptr = ablob->u;
    apl.len = ablob->len;
    toret = cmpkeys_ssh2_asymm(&apl, bv);
    strbuf_free(ablob);
    return toret;
}

void pageant_make_keylist1(BinarySink *bs)
{
    int i;
    RSAKey *key;

    put_uint32(bs, count234(rsakeys));
    for (i = 0; NULL != (key = index234(rsakeys, i)); i++) {
        rsa_ssh1_public_blob(bs, key, RSA_SSH1_EXPONENT_FIRST);
	put_stringz(bs, key->comment);
    }
}

#ifdef MOD_PERSO
int GetScrumbleKeyFlag(void) ;
static int * tab_int = NULL ;
void scrumble_int(void) {
	int count = pageant_count_ssh2_keys(), s, i, j ;
	bool test ; 
	if( count>0 ) {
		if( GetScrumbleKeyFlag() ) {
			if( tab_int != NULL ) { free(tab_int) ; tab_int = NULL ; }
			tab_int = (int*)malloc( count*sizeof(int) ) ;
			if( count==1 ) { tab_int[0] = 0 ; }
			else {
				tab_int[0] = rand() % count ;
				for( i=1 ; i<count ; i++ ) {
					do {
						test = false ;
						s = rand() % count ;
						for( j=0 ; j<i ; j++ ) {
							if( tab_int[j] == s ) { test = true ; }
						}
					} while( test ) ;
					tab_int[i] = s ;
				}
			}
		} else {
			if( tab_int != NULL ) { free(tab_int) ; tab_int = NULL ; }
			tab_int = (int*)malloc( count*sizeof(int) ) ;
			for( i=0 ; i<count ; i++ ) { tab_int[i] = i ; }
		}
		
		/*
		char b[1024]="",c[256];
		for( i=0 ;i<count ; i++ ) {
			sprintf(c,"%d\n",tab_int[i]);
			strcat( b, c);
		}
		MessageBox(NULL,b,"info",MB_OK);
		*/
	
	}
}
int get_scrumble_int( int i ) {
	if( (tab_int == NULL) || !GetScrumbleKeyFlag() ) { return i ; }
	else { return tab_int[i] ; }
}
static int confirm_key_usage(char* fingerprint, char* comment) {
	const char* title = "Confirm SSH Key usage";
	char* message = NULL;
	int result = IDYES; // successful result is the default

	message = dupprintf("Allow authentication with key with fingerprint\n%s\ncomment: %s", fingerprint, comment);
	
	if( (GetAskConfirmationFlag()==1) 
		||
		( (GetAskConfirmationFlag()==2) && (
		(NULL != strstr(comment, "needs confirm"))||(NULL != strstr(comment, "need confirm"))||(NULL != strstr(comment, "confirmation"))
		)
		) )
	{
		result = MessageBox(NULL, message, title, MB_ICONQUESTION | MB_YESNO);
	}

	if (result != IDYES) {
		sfree(message);
		return 0;
	} else {
		if( GetShowBalloonOnKeyUsage()==1 ) ShowBalloonTip( trayIcone, "SSH private key usage", message ) ;
		sfree(message);
		return 1;
	}
}
#endif

void pageant_make_keylist2(BinarySink *bs)
{
    int i;
    ssh2_userkey *key;

    put_uint32(bs, count234(ssh2keys));
    for (i = 0; NULL != (key = index234(ssh2keys, i)); i++) {
        strbuf *blob = strbuf_new();
        ssh_key_public_blob(key->key, BinarySink_UPCAST(blob));
        put_stringsb(bs, blob);
	put_stringz(bs, key->comment);
    }
}

static void plog(void *logctx, pageant_logfn_t logfn, const char *fmt, ...)
#ifdef __GNUC__
__attribute__ ((format (PUTTY_PRINTF_ARCHETYPE, 3, 4)))
#endif
    ;

static void plog(void *logctx, pageant_logfn_t logfn, const char *fmt, ...)
{
    /*
     * This is the wrapper that takes a variadic argument list and
     * turns it into the va_list that the log function really expects.
     * It's safe to call this with logfn==NULL, because we
     * double-check that below; but if you're going to do lots of work
     * before getting here (such as looping, or hashing things) then
     * you should probably check logfn manually before doing that.
     */
    if (logfn) {
        va_list ap;
        va_start(ap, fmt);
        logfn(logctx, fmt, ap);
        va_end(ap);
    }
}

void pageant_handle_msg(BinarySink *bs,
                        const void *msgdata, int msglen,
                        void *logctx, pageant_logfn_t logfn)
{
    BinarySource msg[1];
    int type;

    BinarySource_BARE_INIT(msg, msgdata, msglen);

    type = get_byte(msg);
    if (get_err(msg)) {
        pageant_failure_msg(bs, "message contained no type code",
                            logctx, logfn);
        return;
    }

    switch (type) {
      case SSH1_AGENTC_REQUEST_RSA_IDENTITIES:
	/*
	 * Reply with SSH1_AGENT_RSA_IDENTITIES_ANSWER.
	 */
	{
            plog(logctx, logfn, "request: SSH1_AGENTC_REQUEST_RSA_IDENTITIES");

	    put_byte(bs, SSH1_AGENT_RSA_IDENTITIES_ANSWER);
            pageant_make_keylist1(bs);

            plog(logctx, logfn, "reply: SSH1_AGENT_RSA_IDENTITIES_ANSWER");
            if (logfn) {               /* skip this loop if not logging */
                int i;
                RSAKey *rkey;
                for (i = 0; NULL != (rkey = pageant_nth_ssh1_key(i)); i++) {
                    char *fingerprint = rsa_ssh1_fingerprint(rkey);
                    plog(logctx, logfn, "returned key: %s", fingerprint);
                    sfree(fingerprint);
                }
            }
	}
	break;
      case SSH2_AGENTC_REQUEST_IDENTITIES:
	/*
	 * Reply with SSH2_AGENT_IDENTITIES_ANSWER.
	 */
	{
            plog(logctx, logfn, "request: SSH2_AGENTC_REQUEST_IDENTITIES");

	    put_byte(bs, SSH2_AGENT_IDENTITIES_ANSWER);
            pageant_make_keylist2(bs);

            plog(logctx, logfn, "reply: SSH2_AGENT_IDENTITIES_ANSWER");
            if (logfn) {               /* skip this loop if not logging */
                int i;
                ssh2_userkey *skey;
                for (i = 0; NULL != (skey = pageant_nth_ssh2_key(i)); i++) {
                    char *fingerprint = ssh2_fingerprint(skey->key);
                    plog(logctx, logfn, "returned key: %s %s",
                         fingerprint, skey->comment);
                    sfree(fingerprint);
                }
            }
	}
	break;
      case SSH1_AGENTC_RSA_CHALLENGE:
	/*
	 * Reply with either SSH1_AGENT_RSA_RESPONSE or
	 * SSH_AGENT_FAILURE, depending on whether we have that key
	 * or not.
	 */
	{
	    RSAKey reqkey, *key;
	    mp_int *challenge, *response;
            ptrlen session_id;
            unsigned response_type;
	    unsigned char response_md5[16];
	    int i;
#ifdef MOD_PERSO
		char *fingerprint;
#endif

            plog(logctx, logfn, "request: SSH1_AGENTC_RSA_CHALLENGE");

            response = NULL;
            memset(&reqkey, 0, sizeof(reqkey));

            get_rsa_ssh1_pub(msg, &reqkey, RSA_SSH1_EXPONENT_FIRST);
            challenge = get_mp_ssh1(msg);
            session_id = get_data(msg, 16);
	    response_type = get_uint32(msg);

            if (get_err(msg)) {
                pageant_failure_msg(bs, "unable to decode request",
                                    logctx, logfn);
                goto challenge1_cleanup;
            }
            if (response_type != 1) {
                pageant_failure_msg(
                    bs, "response type other than 1 not supported",
                    logctx, logfn);
                goto challenge1_cleanup;
            }

            if (logfn) {
                char *fingerprint;
                reqkey.comment = NULL;
                fingerprint = rsa_ssh1_fingerprint(&reqkey);
                plog(logctx, logfn, "requested key: %s", fingerprint);
                sfree(fingerprint);
            }
            if ((key = find234(rsakeys, &reqkey, NULL)) == NULL) {
                pageant_failure_msg(bs, "key not found", logctx, logfn);
                goto challenge1_cleanup;
	    }
#ifdef MOD_PERSO
		fingerprint = rsa_ssh1_fingerprint(key);
		if (! confirm_key_usage(fingerprint, key->comment)) {
	      goto challenge1_cleanup;
	    }
#endif
	    response = rsa_ssh1_decrypt(challenge, key);

            {
                ssh_hash *h = ssh_hash_new(&ssh_md5);
                for (i = 0; i < 32; i++)
                    put_byte(h, mp_get_byte(response, 31 - i));
                put_datapl(h, session_id);
                ssh_hash_final(h, response_md5);
            }

	    put_byte(bs, SSH1_AGENT_RSA_RESPONSE);
	    put_data(bs, response_md5, 16);

            plog(logctx, logfn, "reply: SSH1_AGENT_RSA_RESPONSE");

          challenge1_cleanup:
            if (response)
                mp_free(response);
            mp_free(challenge);
            freersakey(&reqkey);
	}
	break;
      case SSH2_AGENTC_SIGN_REQUEST:
	/*
	 * Reply with either SSH2_AGENT_SIGN_RESPONSE or
	 * SSH_AGENT_FAILURE, depending on whether we have that key
	 * or not.
	 */
	{
	    ssh2_userkey *key;
            ptrlen keyblob, sigdata;
            strbuf *signature;
            uint32_t flags, supported_flags;
#ifdef MOD_PERSO
		char* confirm_fingerprint;
#endif

            plog(logctx, logfn, "request: SSH2_AGENTC_SIGN_REQUEST");

            keyblob = get_string(msg);
            sigdata = get_string(msg);

            if (get_err(msg)) {
                pageant_failure_msg(bs, "unable to decode request",
                                    logctx, logfn);
                return;
            }

            /*
             * Later versions of the agent protocol added a flags word
             * on the end of the sign request. That hasn't always been
             * there, so we don't complain if we don't find it.
             *
             * get_uint32 will default to returning zero if no data is
             * available.
             */
            bool have_flags = false;
            flags = get_uint32(msg);
            if (!get_err(msg))
                have_flags = true;

            if (logfn) {
                char *fingerprint = ssh2_fingerprint_blob(keyblob);
                plog(logctx, logfn, "requested key: %s", fingerprint);
                sfree(fingerprint);
            }
            key = find234(ssh2keys, &keyblob, cmpkeys_ssh2_asymm);
	    if (!key) {
                pageant_failure_msg(bs, "key not found", logctx, logfn);
                return;
            }
#ifdef MOD_PERSO
		confirm_fingerprint = ssh2_fingerprint_blob(keyblob);
		if (! confirm_key_usage( confirm_fingerprint , key->comment)) {
			sfree(confirm_fingerprint);
			return;
	    }
		sfree(confirm_fingerprint);
#endif

            if (have_flags)
                plog(logctx, logfn, "signature flags = 0x%08"PRIx32, flags);
            else
                plog(logctx, logfn, "no signature flags");

            supported_flags = ssh_key_alg(key->key)->supported_flags;
            if (flags & ~supported_flags) {
                /*
                 * We MUST reject any message containing flags we
                 * don't understand.
                 */
                char *msg = dupprintf(
                    "unsupported flag bits 0x%08"PRIx32,
                    flags & ~supported_flags);
                pageant_failure_msg(bs, msg, logctx, logfn);
                sfree(msg);
                return;
            }

            char *invalid = ssh_key_invalid(key->key, flags);
            if (invalid) {
                char *msg = dupprintf("key invalid: %s", invalid);
                pageant_failure_msg(bs, msg, logctx, logfn);
                sfree(msg);
                sfree(invalid);
                return;
            }

            signature = strbuf_new();
            ssh_key_sign(key->key, sigdata, flags,
                         BinarySink_UPCAST(signature));

            put_byte(bs, SSH2_AGENT_SIGN_RESPONSE);
            put_stringsb(bs, signature);

            plog(logctx, logfn, "reply: SSH2_AGENT_SIGN_RESPONSE");
	}
	break;
      case SSH1_AGENTC_ADD_RSA_IDENTITY:
	/*
	 * Add to the list and return SSH_AGENT_SUCCESS, or
	 * SSH_AGENT_FAILURE if the key was malformed.
	 */
	{
	    RSAKey *key;

            plog(logctx, logfn, "request: SSH1_AGENTC_ADD_RSA_IDENTITY");

	    key = snew(RSAKey);
	    memset(key, 0, sizeof(RSAKey));

            get_rsa_ssh1_pub(msg, key, RSA_SSH1_MODULUS_FIRST);
            get_rsa_ssh1_priv(msg, key);

            /* SSH-1 names p and q the other way round, i.e. we have
             * the inverse of p mod q and not of q mod p. We swap the
             * names, because our internal RSA wants iqmp. */
	    key->iqmp = get_mp_ssh1(msg);
	    key->q = get_mp_ssh1(msg);
	    key->p = get_mp_ssh1(msg);

	    key->comment = mkstr(get_string(msg));

            if (get_err(msg)) {
                pageant_failure_msg(bs, "unable to decode request",
                                    logctx, logfn);
                goto add1_cleanup;
            }

            if (!rsa_verify(key)) {
                pageant_failure_msg(bs, "key is invalid", logctx, logfn);
		goto add1_cleanup;
            }

            if (logfn) {
                char *fingerprint = rsa_ssh1_fingerprint(key);
                plog(logctx, logfn, "submitted key: %s", fingerprint);
                sfree(fingerprint);
            }

	    if (add234(rsakeys, key) == key) {
		keylist_update();
		put_byte(bs, SSH_AGENT_SUCCESS);
                plog(logctx, logfn, "reply: SSH_AGENT_SUCCESS");
                key = NULL;            /* don't free it in cleanup */
	    } else {
                pageant_failure_msg(bs, "key already present",
                                    logctx, logfn);
	    }

          add1_cleanup:
            if (key) {
		freersakey(key);
		sfree(key);
            }
	}
	break;
      case SSH2_AGENTC_ADD_IDENTITY:
	/*
	 * Add to the list and return SSH_AGENT_SUCCESS, or
	 * SSH_AGENT_FAILURE if the key was malformed.
	 */
	{
	    ssh2_userkey *key = NULL;
            ptrlen algpl;
            const ssh_keyalg *alg;

            plog(logctx, logfn, "request: SSH2_AGENTC_ADD_IDENTITY");

            algpl = get_string(msg);

	    key = snew(ssh2_userkey);
            key->key = NULL;
            key->comment = NULL;
            alg = find_pubkey_alg_len(algpl);
	    if (!alg) {
                pageant_failure_msg(bs, "algorithm unknown", logctx, logfn);
		goto add2_cleanup;
	    }

            key->key = ssh_key_new_priv_openssh(alg, msg);

	    if (!key->key) {
                pageant_failure_msg(bs, "key setup failed", logctx, logfn);
		goto add2_cleanup;
	    }

	    key->comment = mkstr(get_string(msg));

            if (get_err(msg)) {
                pageant_failure_msg(bs, "unable to decode request",
                                    logctx, logfn);
                goto add2_cleanup;
            }

            if (logfn) {
                char *fingerprint = ssh2_fingerprint(key->key);
                plog(logctx, logfn, "submitted key: %s %s",
                     fingerprint, key->comment);
                sfree(fingerprint);
            }

	    if (add234(ssh2keys, key) == key) {
		keylist_update();
		put_byte(bs, SSH_AGENT_SUCCESS);

                plog(logctx, logfn, "reply: SSH_AGENT_SUCCESS");

                key = NULL;            /* don't clean it up */
	    } else {
                pageant_failure_msg(bs, "key already present",
                                    logctx, logfn);
	    }

          add2_cleanup:
            if (key) {
                if (key->key)
                    ssh_key_free(key->key);
                if (key->comment)
                    sfree(key->comment);
		sfree(key);
            }
	}
	break;
      case SSH1_AGENTC_REMOVE_RSA_IDENTITY:
	/*
	 * Remove from the list and return SSH_AGENT_SUCCESS, or
	 * perhaps SSH_AGENT_FAILURE if it wasn't in the list to
	 * start with.
	 */
	{
	    RSAKey reqkey, *key;

            plog(logctx, logfn, "request: SSH1_AGENTC_REMOVE_RSA_IDENTITY");

            memset(&reqkey, 0, sizeof(reqkey));
            get_rsa_ssh1_pub(msg, &reqkey, RSA_SSH1_EXPONENT_FIRST);

            if (get_err(msg)) {
                pageant_failure_msg(bs, "unable to decode request",
                                    logctx, logfn);
                freersakey(&reqkey);
                return;
            }

            if (logfn) {
                char *fingerprint;
                reqkey.comment = NULL;
                fingerprint = rsa_ssh1_fingerprint(&reqkey);
                plog(logctx, logfn, "unwanted key: %s", fingerprint);
                sfree(fingerprint);
            }

	    key = find234(rsakeys, &reqkey, NULL);
            freersakey(&reqkey);
	    if (key) {
                plog(logctx, logfn, "found with comment: %s", key->comment);

		del234(rsakeys, key);
		keylist_update();
		freersakey(key);
		sfree(key);
		put_byte(bs, SSH_AGENT_SUCCESS);

                plog(logctx, logfn, "reply: SSH_AGENT_SUCCESS");
	    } else {
                pageant_failure_msg(bs, "key not found", logctx, logfn);
            }
	}
	break;
      case SSH2_AGENTC_REMOVE_IDENTITY:
	/*
	 * Remove from the list and return SSH_AGENT_SUCCESS, or
	 * perhaps SSH_AGENT_FAILURE if it wasn't in the list to
	 * start with.
	 */
	{
	    ssh2_userkey *key;
            ptrlen blob;

            plog(logctx, logfn, "request: SSH2_AGENTC_REMOVE_IDENTITY");

            blob = get_string(msg);

            if (get_err(msg)) {
                pageant_failure_msg(bs, "unable to decode request",
                                    logctx, logfn);
                return;
            }

            if (logfn) {
                char *fingerprint = ssh2_fingerprint_blob(blob);
                plog(logctx, logfn, "unwanted key: %s", fingerprint);
                sfree(fingerprint);
            }

            key = find234(ssh2keys, &blob, cmpkeys_ssh2_asymm);
	    if (!key) {
                pageant_failure_msg(bs, "key not found", logctx, logfn);
                return;
            }

            plog(logctx, logfn, "found with comment: %s", key->comment);

            del234(ssh2keys, key);
            keylist_update();
            ssh_key_free(key->key);
            sfree(key->comment);
            sfree(key);
            put_byte(bs, SSH_AGENT_SUCCESS);

            plog(logctx, logfn, "reply: SSH_AGENT_SUCCESS");
	}
	break;
      case SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES:
	/*
	 * Remove all SSH-1 keys. Always returns success.
	 */
	{
	    RSAKey *rkey;

            plog(logctx, logfn, "request:"
                " SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES");

	    while ((rkey = index234(rsakeys, 0)) != NULL) {
		del234(rsakeys, rkey);
		freersakey(rkey);
		sfree(rkey);
	    }
	    keylist_update();

            put_byte(bs, SSH_AGENT_SUCCESS);

            plog(logctx, logfn, "reply: SSH_AGENT_SUCCESS");
	}
	break;
      case SSH2_AGENTC_REMOVE_ALL_IDENTITIES:
	/*
	 * Remove all SSH-2 keys. Always returns success.
	 */
	{
	    ssh2_userkey *skey;

            plog(logctx, logfn, "request: SSH2_AGENTC_REMOVE_ALL_IDENTITIES");

	    while ((skey = index234(ssh2keys, 0)) != NULL) {
		del234(ssh2keys, skey);
                ssh_key_free(skey->key);
                sfree(skey->comment);
		sfree(skey);
	    }
	    keylist_update();

            put_byte(bs, SSH_AGENT_SUCCESS);

            plog(logctx, logfn, "reply: SSH_AGENT_SUCCESS");
	}
	break;
      default:
        plog(logctx, logfn, "request: unknown message type %d", type);
        pageant_failure_msg(bs, "unrecognised message", logctx, logfn);
	break;
    }
}

void pageant_failure_msg(BinarySink *bs,
                         const char *log_reason,
                         void *logctx, pageant_logfn_t logfn)
{
    put_byte(bs, SSH_AGENT_FAILURE);
    plog(logctx, logfn, "reply: SSH_AGENT_FAILURE (%s)", log_reason);
}

void pageant_init(void)
{
    pageant_local = true;
    rsakeys = newtree234(cmpkeys_rsa);
    ssh2keys = newtree234(cmpkeys_ssh2);
}

RSAKey *pageant_nth_ssh1_key(int i)
{
    return index234(rsakeys, i);
}

ssh2_userkey *pageant_nth_ssh2_key(int i)
{
#ifdef MOD_PERSO
    return index234(ssh2keys, get_scrumble_int(i));
#else
    return index234(ssh2keys, i);
#endif
}


int pageant_count_ssh1_keys(void)
{
    return count234(rsakeys);
}

int pageant_count_ssh2_keys(void)
{
    return count234(ssh2keys);
}

bool pageant_add_ssh1_key(RSAKey *rkey)
{
    return add234(rsakeys, rkey) == rkey;
}

bool pageant_add_ssh2_key(ssh2_userkey *skey)
{
    return add234(ssh2keys, skey) == skey;
}

bool pageant_delete_ssh1_key(RSAKey *rkey)
{
    RSAKey *deleted = del234(rsakeys, rkey);
    if (!deleted)
        return false;
    assert(deleted == rkey);
    return true;
}

bool pageant_delete_ssh2_key(ssh2_userkey *skey)
{
    ssh2_userkey *deleted = del234(ssh2keys, skey);
    if (!deleted)
        return false;
    assert(deleted == skey);
    return true;
}

/* ----------------------------------------------------------------------
 * The agent plug.
 */

/*
 * An extra coroutine macro, specific to this code which is consuming
 * 'const char *data'.
 */
#define crGetChar(c) do                                         \
    {                                                           \
        while (len == 0) {                                      \
            *crLine =__LINE__; return; case __LINE__:;          \
        }                                                       \
        len--;                                                  \
        (c) = (unsigned char)*data++;                           \
    } while (0)

struct pageant_conn_state {
    Socket *connsock;
    void *logctx;
    pageant_logfn_t logfn;
    unsigned char lenbuf[4], pktbuf[AGENT_MAX_MSGLEN];
    unsigned len, got;
    bool real_packet;
    int crLine;            /* for coroutine in pageant_conn_receive */

    Plug plug;
};

static void pageant_conn_closing(Plug *plug, const char *error_msg,
				 int error_code, bool calling_back)
{
    struct pageant_conn_state *pc = container_of(
        plug, struct pageant_conn_state, plug);
    if (error_msg)
        plog(pc->logctx, pc->logfn, "%p: error: %s", pc, error_msg);
    else
        plog(pc->logctx, pc->logfn, "%p: connection closed", pc);
    sk_close(pc->connsock);
    sfree(pc);
}

static void pageant_conn_sent(Plug *plug, size_t bufsize)
{
    /* struct pageant_conn_state *pc = container_of(
        plug, struct pageant_conn_state, plug); */

    /*
     * We do nothing here, because we expect that there won't be a
     * need to throttle and unthrottle the connection to an agent -
     * clients will typically not send many requests, and will wait
     * until they receive each reply before sending a new request.
     */
}

static void pageant_conn_log(void *logctx, const char *fmt, va_list ap)
{
    /* Wrapper on pc->logfn that prefixes the connection identifier */
    struct pageant_conn_state *pc = (struct pageant_conn_state *)logctx;
    char *formatted = dupvprintf(fmt, ap);
    plog(pc->logctx, pc->logfn, "%p: %s", pc, formatted);
    sfree(formatted);
}

static void pageant_conn_receive(
    Plug *plug, int urgent, const char *data, size_t len)
{
    struct pageant_conn_state *pc = container_of(
        plug, struct pageant_conn_state, plug);
    char c;

    crBegin(pc->crLine);

    while (len > 0) {
        pc->got = 0;
        while (pc->got < 4) {
            crGetChar(c);
            pc->lenbuf[pc->got++] = c;
        }

        pc->len = GET_32BIT_MSB_FIRST(pc->lenbuf);
        pc->got = 0;
        pc->real_packet = (pc->len < AGENT_MAX_MSGLEN-4);

        while (pc->got < pc->len) {
            crGetChar(c);
            if (pc->real_packet)
                pc->pktbuf[pc->got] = c;
            pc->got++;
        }

        {
            strbuf *reply = strbuf_new();

            put_uint32(reply, 0);      /* length field to fill in later */

            if (pc->real_packet) {
                pageant_handle_msg(BinarySink_UPCAST(reply), pc->pktbuf, pc->len, pc,
                                   pc->logfn ? pageant_conn_log : NULL);
            } else {
                plog(pc->logctx, pc->logfn, "%p: overlong message (%u)",
                     pc, pc->len);
                pageant_failure_msg(BinarySink_UPCAST(reply), "message too long", pc,
                                    pc->logfn ? pageant_conn_log : NULL);
            }

            PUT_32BIT_MSB_FIRST(reply->s, reply->len - 4);
            sk_write(pc->connsock, reply->s, reply->len);

            strbuf_free(reply);
        }
    }

    crFinishV;
}

struct pageant_listen_state {
    Socket *listensock;
    void *logctx;
    pageant_logfn_t logfn;

    Plug plug;
};

static void pageant_listen_closing(Plug *plug, const char *error_msg,
				   int error_code, bool calling_back)
{
    struct pageant_listen_state *pl = container_of(
        plug, struct pageant_listen_state, plug);
    if (error_msg)
        plog(pl->logctx, pl->logfn, "listening socket: error: %s", error_msg);
    sk_close(pl->listensock);
    pl->listensock = NULL;
}

static const PlugVtable pageant_connection_plugvt = {
    NULL, /* no log function, because that's for outgoing connections */
    pageant_conn_closing,
    pageant_conn_receive,
    pageant_conn_sent,
    NULL /* no accepting function, because we've already done it */
};

static int pageant_listen_accepting(Plug *plug,
                                    accept_fn_t constructor, accept_ctx_t ctx)
{
    struct pageant_listen_state *pl = container_of(
        plug, struct pageant_listen_state, plug);
    struct pageant_conn_state *pc;
    const char *err;
    SocketPeerInfo *peerinfo;

    pc = snew(struct pageant_conn_state);
    pc->plug.vt = &pageant_connection_plugvt;
    pc->logfn = pl->logfn;
    pc->logctx = pl->logctx;
    pc->crLine = 0;

    pc->connsock = constructor(ctx, &pc->plug);
    if ((err = sk_socket_error(pc->connsock)) != NULL) {
        sk_close(pc->connsock);
        sfree(pc);
	return 1;
    }

    sk_set_frozen(pc->connsock, 0);

    peerinfo = sk_peer_info(pc->connsock);
    if (peerinfo && peerinfo->log_text) {
        plog(pl->logctx, pl->logfn, "%p: new connection from %s",
             pc, peerinfo->log_text);
    } else {
        plog(pl->logctx, pl->logfn, "%p: new connection", pc);
    }
    sk_free_peer_info(peerinfo);

    return 0;
}

static const PlugVtable pageant_listener_plugvt = {
    NULL, /* no log function, because that's for outgoing connections */
    pageant_listen_closing,
    NULL, /* no receive function on a listening socket */
    NULL, /* no sent function on a listening socket */
    pageant_listen_accepting
};

struct pageant_listen_state *pageant_listener_new(Plug **plug)
{
    struct pageant_listen_state *pl = snew(struct pageant_listen_state);
    pl->plug.vt = &pageant_listener_plugvt;
    pl->logctx = NULL;
    pl->logfn = NULL;
    pl->listensock = NULL;
    *plug = &pl->plug;
    return pl;
}

void pageant_listener_got_socket(struct pageant_listen_state *pl, Socket *sock)
{
    pl->listensock = sock;
}

void pageant_listener_set_logfn(struct pageant_listen_state *pl,
                                void *logctx, pageant_logfn_t logfn)
{
    pl->logctx = logctx;
    pl->logfn = logfn;
}

void pageant_listener_free(struct pageant_listen_state *pl)
{
    if (pl->listensock)
        sk_close(pl->listensock);
    sfree(pl);
}

/* ----------------------------------------------------------------------
 * Code to perform agent operations either as a client, or within the
 * same process as the running agent.
 */

static tree234 *passphrases = NULL;

/*
 * After processing a list of filenames, we want to forget the
 * passphrases.
 */
void pageant_forget_passphrases(void)
{
    if (!passphrases)                  /* in case we never set it up at all */
        return;

    while (count234(passphrases) > 0) {
	char *pp = index234(passphrases, 0);
	smemclr(pp, strlen(pp));
	delpos234(passphrases, 0);
	sfree(pp);
    }
}

void *pageant_get_keylist1(int *length)
{
    void *ret;

    if (!pageant_local) {
        strbuf *request;
	unsigned char *response;
	void *vresponse;
	int resplen;

        request = strbuf_new_for_agent_query();
	put_byte(request, SSH1_AGENTC_REQUEST_RSA_IDENTITIES);
        agent_query_synchronous(request, &vresponse, &resplen);
        strbuf_free(request);

	response = vresponse;
	if (resplen < 5 || response[4] != SSH1_AGENT_RSA_IDENTITIES_ANSWER) {
            sfree(response);
	    return NULL;
        }

	ret = snewn(resplen-5, unsigned char);
	memcpy(ret, response+5, resplen-5);
	sfree(response);

	if (length)
	    *length = resplen-5;
    } else {
        strbuf *buf = strbuf_new();
	pageant_make_keylist1(BinarySink_UPCAST(buf));
        *length = buf->len;
        ret = strbuf_to_str(buf);
    }
    return ret;
}

void *pageant_get_keylist2(int *length)
{
    void *ret;

    if (!pageant_local) {
        strbuf *request;
	unsigned char *response;
	void *vresponse;
	int resplen;

        request = strbuf_new_for_agent_query();
	put_byte(request, SSH2_AGENTC_REQUEST_IDENTITIES);
        agent_query_synchronous(request, &vresponse, &resplen);
        strbuf_free(request);

	response = vresponse;
	if (resplen < 5 || response[4] != SSH2_AGENT_IDENTITIES_ANSWER) {
            sfree(response);
	    return NULL;
        }

	ret = snewn(resplen-5, unsigned char);
	memcpy(ret, response+5, resplen-5);
	sfree(response);

	if (length)
	    *length = resplen-5;
    } else {
        strbuf *buf = strbuf_new();
	pageant_make_keylist2(BinarySink_UPCAST(buf));
        *length = buf->len;
        ret = strbuf_to_str(buf);
    }
    return ret;
}

int pageant_add_keyfile(Filename *filename, const char *passphrase,
                        char **retstr)
{
    RSAKey *rkey = NULL;
    ssh2_userkey *skey = NULL;
    bool needs_pass;
    int ret;
    int attempts;
    char *comment;
    const char *this_passphrase;
    const char *error = NULL;
    int type;

    if (!passphrases) {
        passphrases = newtree234(NULL);
    }

    *retstr = NULL;

    type = key_type(filename);
    if (type != SSH_KEYTYPE_SSH1 && type != SSH_KEYTYPE_SSH2) {
	*retstr = dupprintf("Couldn't load this key (%s)",
                            key_type_to_str(type));
	return PAGEANT_ACTION_FAILURE;
    }

    /*
     * See if the key is already loaded (in the primary Pageant,
     * which may or may not be us).
     */
    {
	strbuf *blob = strbuf_new();
	unsigned char *keylist, *p;
	int i, nkeys, keylistlen;

	if (type == SSH_KEYTYPE_SSH1) {
	    if (!rsa_ssh1_loadpub(filename, BinarySink_UPCAST(blob), NULL, &error)) {
                *retstr = dupprintf("Couldn't load private key (%s)", error);
                strbuf_free(blob);
                return PAGEANT_ACTION_FAILURE;
	    }
	    keylist = pageant_get_keylist1(&keylistlen);
	} else {
	    /* For our purposes we want the blob prefixed with its
             * length, so add a placeholder here to fill in
             * afterwards */
            put_uint32(blob, 0);
#ifdef MOD_WINCRYPT
	    if (!ssh2_userkey_loadpub((const Filename **)&filename, NULL, BinarySink_UPCAST(blob),
#else
	    if (!ssh2_userkey_loadpub(filename, NULL, BinarySink_UPCAST(blob),
#endif
                                      NULL, &error)) {
                *retstr = dupprintf("Couldn't load private key (%s)", error);
                strbuf_free(blob);
		return PAGEANT_ACTION_FAILURE;
	    }
	    PUT_32BIT_MSB_FIRST(blob->s, blob->len - 4);
	    keylist = pageant_get_keylist2(&keylistlen);
	}
	if (keylist) {
	    if (keylistlen < 4) {
		*retstr = dupstr("Received broken key list from agent");
                sfree(keylist);
                strbuf_free(blob);
		return PAGEANT_ACTION_FAILURE;
	    }
	    nkeys = toint(GET_32BIT_MSB_FIRST(keylist));
	    if (nkeys < 0) {
		*retstr = dupstr("Received broken key list from agent");
                sfree(keylist);
                strbuf_free(blob);
		return PAGEANT_ACTION_FAILURE;
	    }
	    p = keylist + 4;
	    keylistlen -= 4;

	    for (i = 0; i < nkeys; i++) {
		if (!memcmp(blob->s, p, blob->len)) {
		    /* Key is already present; we can now leave. */
		    sfree(keylist);
		    strbuf_free(blob);
                    return PAGEANT_ACTION_OK;
		}
		/* Now skip over public blob */
		if (type == SSH_KEYTYPE_SSH1) {
		    int n = rsa_ssh1_public_blob_len(
                        make_ptrlen(p, keylistlen));
		    if (n < 0) {
                        *retstr = dupstr("Received broken key list from agent");
                        sfree(keylist);
                        strbuf_free(blob);
                        return PAGEANT_ACTION_FAILURE;
		    }
		    p += n;
		    keylistlen -= n;
		} else {
		    int n;
		    if (keylistlen < 4) {
                        *retstr = dupstr("Received broken key list from agent");
                        sfree(keylist);
                        strbuf_free(blob);
                        return PAGEANT_ACTION_FAILURE;
		    }
		    n = GET_32BIT_MSB_FIRST(p);
                    p += 4;
                    keylistlen -= 4;

		    if (n < 0 || n > keylistlen) {
                        *retstr = dupstr("Received broken key list from agent");
                        sfree(keylist);
                        strbuf_free(blob);
                        return PAGEANT_ACTION_FAILURE;
		    }
		    p += n;
		    keylistlen -= n;
		}
		/* Now skip over comment field */
		{
		    int n;
		    if (keylistlen < 4) {
                        *retstr = dupstr("Received broken key list from agent");
                        sfree(keylist);
                        strbuf_free(blob);
                        return PAGEANT_ACTION_FAILURE;
		    }
		    n = GET_32BIT_MSB_FIRST(p);
                    p += 4;
                    keylistlen -= 4;

		    if (n < 0 || n > keylistlen) {
                        *retstr = dupstr("Received broken key list from agent");
                        sfree(keylist);
                        strbuf_free(blob);
                        return PAGEANT_ACTION_FAILURE;
		    }
		    p += n;
		    keylistlen -= n;
		}
	    }

	    sfree(keylist);
	}

	strbuf_free(blob);
    }

    error = NULL;
    if (type == SSH_KEYTYPE_SSH1)
	needs_pass = rsa_ssh1_encrypted(filename, &comment);
    else
	needs_pass = ssh2_userkey_encrypted(filename, &comment);
    attempts = 0;
    if (type == SSH_KEYTYPE_SSH1)
	rkey = snew(RSAKey);

    /*
     * Loop round repeatedly trying to load the key, until we either
     * succeed, fail for some serious reason, or run out of
     * passphrases to try.
     */
    while (1) {
	if (needs_pass) {

            /*
             * If we've been given a passphrase on input, try using
             * it. Otherwise, try one from our tree234 of previously
             * useful passphrases.
             */
            if (passphrase) {
                this_passphrase = (attempts == 0 ? passphrase : NULL);
            } else {
                this_passphrase = (const char *)index234(passphrases, attempts);
            }

            if (!this_passphrase) {
                /*
                 * Run out of passphrases to try.
                 */
                *retstr = comment;
                sfree(rkey);
                return PAGEANT_ACTION_NEED_PP;
            }
	} else
	    this_passphrase = "";

	if (type == SSH_KEYTYPE_SSH1)
	    ret = rsa_ssh1_loadkey(filename, rkey, this_passphrase, &error);
	else {
	    skey = ssh2_load_userkey(filename, this_passphrase, &error);
	    if (skey == SSH2_WRONG_PASSPHRASE)
		ret = -1;
	    else if (!skey)
		ret = 0;
	    else
		ret = 1;
	}

        if (ret == 0) {
            /*
             * Failed to load the key file, for some reason other than
             * a bad passphrase.
             */
            *retstr = dupstr(error);
            sfree(rkey);
            if (comment)
                sfree(comment);
            return PAGEANT_ACTION_FAILURE;
        } else if (ret == 1) {
            /*
             * Successfully loaded the key file.
             */
            break;
        } else {
            /*
             * Passphrase wasn't right; go round again.
             */
            attempts++;
        }
    }

    /*
     * If we get here, we've successfully loaded the key into
     * rkey/skey, but not yet added it to the agent.
     */

    /*
     * If the key was successfully decrypted, save the passphrase for
     * use with other keys we try to load.
     */
    {
        char *pp_copy = dupstr(this_passphrase);
	if (addpos234(passphrases, pp_copy, 0) != pp_copy) {
            /* No need; it was already there. */
            smemclr(pp_copy, strlen(pp_copy));
            sfree(pp_copy);
        }
    }

    if (comment)
	sfree(comment);

    if (type == SSH_KEYTYPE_SSH1) {
	if (!pageant_local) {
            strbuf *request;
	    unsigned char *response;
	    void *vresponse;
	    int resplen;

	    request = strbuf_new_for_agent_query();
	    put_byte(request, SSH1_AGENTC_ADD_RSA_IDENTITY);
	    put_uint32(request, mp_get_nbits(rkey->modulus));
	    put_mp_ssh1(request, rkey->modulus);
	    put_mp_ssh1(request, rkey->exponent);
	    put_mp_ssh1(request, rkey->private_exponent);
	    put_mp_ssh1(request, rkey->iqmp);
	    put_mp_ssh1(request, rkey->q);
	    put_mp_ssh1(request, rkey->p);
	    put_stringz(request, rkey->comment);
	    agent_query_synchronous(request, &vresponse, &resplen);
            strbuf_free(request);

	    response = vresponse;
	    if (resplen < 5 || response[4] != SSH_AGENT_SUCCESS) {
		*retstr = dupstr("The already running Pageant "
                                 "refused to add the key.");
                freersakey(rkey);
                sfree(rkey);
                sfree(response);
                return PAGEANT_ACTION_FAILURE;
            }
            freersakey(rkey);
            sfree(rkey);
	    sfree(response);
	} else {
	    if (!pageant_add_ssh1_key(rkey)) {
                freersakey(rkey);
		sfree(rkey);	       /* already present, don't waste RAM */
            }
	}
    } else {
	if (!pageant_local) {
	    strbuf *request;
            unsigned char *response;
	    void *vresponse;
	    int resplen;

	    request = strbuf_new_for_agent_query();
	    put_byte(request, SSH2_AGENTC_ADD_IDENTITY);
	    put_stringz(request, ssh_key_ssh_id(skey->key));
            ssh_key_openssh_blob(skey->key, BinarySink_UPCAST(request));
	    put_stringz(request, skey->comment);
	    agent_query_synchronous(request, &vresponse, &resplen);
            strbuf_free(request);

	    response = vresponse;
	    if (resplen < 5 || response[4] != SSH_AGENT_SUCCESS) {
		*retstr = dupstr("The already running Pageant "
                                 "refused to add the key.");
                sfree(response);
                return PAGEANT_ACTION_FAILURE;
            }

            ssh_key_free(skey->key);
            sfree(skey);
	    sfree(response);
	} else {
	    if (!pageant_add_ssh2_key(skey)) {
                ssh_key_free(skey->key);
		sfree(skey);	       /* already present, don't waste RAM */
	    }
	}
    }
    return PAGEANT_ACTION_OK;
}

int pageant_enum_keys(pageant_key_enum_fn_t callback, void *callback_ctx,
                      char **retstr)
{
    unsigned char *keylist;
    int i, nkeys, keylistlen;
    ptrlen comment;
    struct pageant_pubkey cbkey;
    BinarySource src[1];

    keylist = pageant_get_keylist1(&keylistlen);
    if (!keylist) {
        *retstr = dupstr("Did not receive an SSH-1 key list from agent");
        return PAGEANT_ACTION_FAILURE;
    }
    BinarySource_BARE_INIT(src, keylist, keylistlen);

    nkeys = toint(get_uint32(src));
    for (i = 0; i < nkeys; i++) {
        RSAKey rkey;
        char *fingerprint;

        /* public blob and fingerprint */
        memset(&rkey, 0, sizeof(rkey));
        get_rsa_ssh1_pub(src, &rkey, RSA_SSH1_EXPONENT_FIRST);
        comment = get_string(src);

        if (get_err(src)) {
            *retstr = dupstr("Received broken SSH-1 key list from agent");
            freersakey(&rkey);
            sfree(keylist);
            return PAGEANT_ACTION_FAILURE;
        }

        fingerprint = rsa_ssh1_fingerprint(&rkey);

        cbkey.blob = strbuf_new();
        rsa_ssh1_public_blob(BinarySink_UPCAST(cbkey.blob), &rkey,
                             RSA_SSH1_EXPONENT_FIRST);
        cbkey.comment = mkstr(comment);
        cbkey.ssh_version = 1;
        callback(callback_ctx, fingerprint, cbkey.comment, &cbkey);
        strbuf_free(cbkey.blob);
        freersakey(&rkey);
        sfree(cbkey.comment);
        sfree(fingerprint);
    }

    sfree(keylist);

    if (get_err(src) || get_avail(src) != 0) {
        *retstr = dupstr("Received broken SSH-1 key list from agent");
        return PAGEANT_ACTION_FAILURE;
    }

    keylist = pageant_get_keylist2(&keylistlen);
    if (!keylist) {
        *retstr = dupstr("Did not receive an SSH-2 key list from agent");
        return PAGEANT_ACTION_FAILURE;
    }
    BinarySource_BARE_INIT(src, keylist, keylistlen);

    nkeys = toint(get_uint32(src));
    for (i = 0; i < nkeys; i++) {
        ptrlen pubblob;
        char *fingerprint;

        pubblob = get_string(src);
        comment = get_string(src);

        if (get_err(src)) {
            *retstr = dupstr("Received broken SSH-2 key list from agent");
            sfree(keylist);
            return PAGEANT_ACTION_FAILURE;
        }

        fingerprint = ssh2_fingerprint_blob(pubblob);
        cbkey.blob = strbuf_new();
        put_datapl(cbkey.blob, pubblob);

        cbkey.ssh_version = 2;
        cbkey.comment = mkstr(comment);
        callback(callback_ctx, fingerprint, cbkey.comment, &cbkey);
        sfree(fingerprint);
        sfree(cbkey.comment);
    }

    sfree(keylist);

    if (get_err(src) || get_avail(src) != 0) {
        *retstr = dupstr("Received broken SSH-2 key list from agent");
        return PAGEANT_ACTION_FAILURE;
    }

    return PAGEANT_ACTION_OK;
}

int pageant_delete_key(struct pageant_pubkey *key, char **retstr)
{
    strbuf *request;
    unsigned char *response;
    int resplen, ret;
    void *vresponse;

    request = strbuf_new_for_agent_query();

    if (key->ssh_version == 1) {
        put_byte(request, SSH1_AGENTC_REMOVE_RSA_IDENTITY);
        put_data(request, key->blob->s, key->blob->len);
    } else {
        put_byte(request, SSH2_AGENTC_REMOVE_IDENTITY);
        put_string(request, key->blob->s, key->blob->len);
    }

    agent_query_synchronous(request, &vresponse, &resplen);
    strbuf_free(request);

    response = vresponse;
    if (resplen < 5 || response[4] != SSH_AGENT_SUCCESS) {
        *retstr = dupstr("Agent failed to delete key");
        ret = PAGEANT_ACTION_FAILURE;
    } else {
        *retstr = NULL;
        ret = PAGEANT_ACTION_OK;
    }
    sfree(response);
    return ret;
}

int pageant_delete_all_keys(char **retstr)
{
    strbuf *request;
    unsigned char *response;
    int resplen;
    bool success;
    void *vresponse;

    request = strbuf_new_for_agent_query();
    put_byte(request, SSH2_AGENTC_REMOVE_ALL_IDENTITIES);
    agent_query_synchronous(request, &vresponse, &resplen);
    strbuf_free(request);
    response = vresponse;
    success = (resplen >= 4 && response[4] == SSH_AGENT_SUCCESS);
    sfree(response);
    if (!success) {
        *retstr = dupstr("Agent failed to delete SSH-2 keys");
        return PAGEANT_ACTION_FAILURE;
    }

    request = strbuf_new_for_agent_query();
    put_byte(request, SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES);
    agent_query_synchronous(request, &vresponse, &resplen);
    strbuf_free(request);
    response = vresponse;
    success = (resplen >= 4 && response[4] == SSH_AGENT_SUCCESS);
    sfree(response);
    if (!success) {
        *retstr = dupstr("Agent failed to delete SSH-1 keys");
        return PAGEANT_ACTION_FAILURE;
    }

    *retstr = NULL;
    return PAGEANT_ACTION_OK;
}

struct pageant_pubkey *pageant_pubkey_copy(struct pageant_pubkey *key)
{
    struct pageant_pubkey *ret = snew(struct pageant_pubkey);
    ret->blob = strbuf_new();
    put_data(ret->blob, key->blob->s, key->blob->len);
    ret->comment = key->comment ? dupstr(key->comment) : NULL;
    ret->ssh_version = key->ssh_version;
    return ret;
}

void pageant_pubkey_free(struct pageant_pubkey *key)
{
    sfree(key->comment);
    strbuf_free(key->blob);
    sfree(key);
}
