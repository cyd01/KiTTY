/*
 * pageant.c: cross-platform code to implement Pageant.
 */

#include <stddef.h>
#include <stdlib.h>
#include <assert.h>

#include "putty.h"
#include "ssh.h"
#include "pageant.h"

/*
 * We need this to link with the RSA code, because rsaencrypt()
 * pads its data with random bytes. Since we only use rsadecrypt()
 * and the signing functions, which are deterministic, this should
 * never be called.
 *
 * If it _is_ called, there is a _serious_ problem, because it
 * won't generate true random numbers. So we must scream, panic,
 * and exit immediately if that should happen.
 */
#ifndef INTEGRATED_AGENT
int random_byte(void)
{
    modalfatalbox("Internal error: attempt to use random numbers in Pageant");
    exit(0);
    return 0;                 /* unreachable, but placate optimiser */
}
#endif

static int pageant_local = FALSE;

/*
 * rsakeys stores SSH-1 RSA keys. ssh2keys stores all SSH-2 keys.
 */
static tree234 *rsakeys, *ssh2keys;

/*
 * Blob structure for passing to the asymmetric SSH-2 key compare
 * function, prototyped here.
 */
struct blob {
    const unsigned char *blob;
    int len;
};
static int cmpkeys_ssh2_asymm(void *av, void *bv);

/*
 * Key comparison function for the 2-3-4 tree of RSA keys.
 */
static int cmpkeys_rsa(void *av, void *bv)
{
    struct RSAKey *a = (struct RSAKey *) av;
    struct RSAKey *b = (struct RSAKey *) bv;
    Bignum am, bm;
    int alen, blen;

    am = a->modulus;
    bm = b->modulus;
    /*
     * Compare by length of moduli.
     */
    alen = bignum_bitcount(am);
    blen = bignum_bitcount(bm);
    if (alen > blen)
	return +1;
    else if (alen < blen)
	return -1;
    /*
     * Now compare by moduli themselves.
     */
    alen = (alen + 7) / 8;	       /* byte count */
    while (alen-- > 0) {
	int abyte, bbyte;
	abyte = bignum_byte(am, alen);
	bbyte = bignum_byte(bm, alen);
	if (abyte > bbyte)
	    return +1;
	else if (abyte < bbyte)
	    return -1;
    }
    /*
     * Give up.
     */
    return 0;
}

/*
 * Key comparison function for the 2-3-4 tree of SSH-2 keys.
 */
static int cmpkeys_ssh2(void *av, void *bv)
{
    struct ssh2_userkey *a = (struct ssh2_userkey *) av;
    struct ssh2_userkey *b = (struct ssh2_userkey *) bv;
    int i;
    int alen, blen;
    unsigned char *ablob, *bblob;
    int c;

    /*
     * Compare purely by public blob.
     */
    ablob = a->alg->public_blob(a->data, &alen);
    bblob = b->alg->public_blob(b->data, &blen);

    c = 0;
    for (i = 0; i < alen && i < blen; i++) {
	if (ablob[i] < bblob[i]) {
	    c = -1;
	    break;
	} else if (ablob[i] > bblob[i]) {
	    c = +1;
	    break;
	}
    }
    if (c == 0 && i < alen)
	c = +1;			       /* a is longer */
    if (c == 0 && i < blen)
	c = -1;			       /* a is longer */

    sfree(ablob);
    sfree(bblob);

    return c;
}

/*
 * Key comparison function for looking up a blob in the 2-3-4 tree
 * of SSH-2 keys.
 */
static int cmpkeys_ssh2_asymm(void *av, void *bv)
{
    struct blob *a = (struct blob *) av;
    struct ssh2_userkey *b = (struct ssh2_userkey *) bv;
    int i;
    int alen, blen;
    const unsigned char *ablob;
    unsigned char *bblob;
    int c;

    /*
     * Compare purely by public blob.
     */
    ablob = a->blob;
    alen = a->len;
    bblob = b->alg->public_blob(b->data, &blen);

    c = 0;
    for (i = 0; i < alen && i < blen; i++) {
	if (ablob[i] < bblob[i]) {
	    c = -1;
	    break;
	} else if (ablob[i] > bblob[i]) {
	    c = +1;
	    break;
	}
    }
    if (c == 0 && i < alen)
	c = +1;			       /* a is longer */
    if (c == 0 && i < blen)
	c = -1;			       /* a is longer */

    sfree(bblob);

    return c;
}

/*
 * Create an SSH-1 key list in a malloc'ed buffer; return its
 * length.
 */
void *pageant_make_keylist1(int *length)
{
    int i, nkeys, len;
    struct RSAKey *key;
    unsigned char *blob, *p, *ret;
    int bloblen;

    /*
     * Count up the number and length of keys we hold.
     */
    len = 4;
    nkeys = 0;
    for (i = 0; NULL != (key = index234(rsakeys, i)); i++) {
	nkeys++;
	blob = rsa_public_blob(key, &bloblen);
	len += bloblen;
	sfree(blob);
	len += 4 + strlen(key->comment);
    }

    /* Allocate the buffer. */
    p = ret = snewn(len, unsigned char);
    if (length) *length = len;

    PUT_32BIT(p, nkeys);
    p += 4;
    for (i = 0; NULL != (key = index234(rsakeys, i)); i++) {
	blob = rsa_public_blob(key, &bloblen);
	memcpy(p, blob, bloblen);
	p += bloblen;
	sfree(blob);
	PUT_32BIT(p, strlen(key->comment));
	memcpy(p + 4, key->comment, strlen(key->comment));
	p += 4 + strlen(key->comment);
    }

    assert(p - ret == len);
    return ret;
}

/*
 * Create an SSH-2 key list in a malloc'ed buffer; return its
 * length.
 */
void *pageant_make_keylist2(int *length)
{
    struct ssh2_userkey *key;
    int i, len, nkeys;
    unsigned char *blob, *p, *ret;
    int bloblen;

    /*
     * Count up the number and length of keys we hold.
     */
    len = 4;
    nkeys = 0;
    for (i = 0; NULL != (key = index234(ssh2keys, i)); i++) {
	nkeys++;
	len += 4;	       /* length field */
	blob = key->alg->public_blob(key->data, &bloblen);
	len += bloblen;
	sfree(blob);
	len += 4 + strlen(key->comment);
    }

    /* Allocate the buffer. */
    p = ret = snewn(len, unsigned char);
    if (length) *length = len;

    /*
     * Packet header is the obvious five bytes, plus four
     * bytes for the key count.
     */
    PUT_32BIT(p, nkeys);
    p += 4;
    for (i = 0; NULL != (key = index234(ssh2keys, i)); i++) {
	blob = key->alg->public_blob(key->data, &bloblen);
	PUT_32BIT(p, bloblen);
	p += 4;
	memcpy(p, blob, bloblen);
	p += bloblen;
	sfree(blob);
	PUT_32BIT(p, strlen(key->comment));
	memcpy(p + 4, key->comment, strlen(key->comment));
	p += 4 + strlen(key->comment);
    }

    assert(p - ret == len);
    return ret;
}

static void plog(void *logctx, pageant_logfn_t logfn, const char *fmt, ...)
#ifdef __GNUC__
__attribute__ ((format (printf, 3, 4)))
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

#ifdef PERSOPORT
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

void *pageant_handle_msg(const void *msg, int msglen, int *outlen,
                         void *logctx, pageant_logfn_t logfn)
{
    const unsigned char *p = msg;
    const unsigned char *msgend;
    unsigned char *ret = snewn(AGENT_MAX_MSGLEN, unsigned char);
    int type;
    const char *fail_reason;

    msgend = p + msglen;

    /*
     * Get the message type.
     */
    if (msgend < p+1) {
        fail_reason = "message contained no type code";
	goto failure;
    }
    type = *p++;

    switch (type) {
      case SSH1_AGENTC_REQUEST_RSA_IDENTITIES:
	/*
	 * Reply with SSH1_AGENT_RSA_IDENTITIES_ANSWER.
	 */
	{
	    int len;
	    void *keylist;

            plog(logctx, logfn, "request: SSH1_AGENTC_REQUEST_RSA_IDENTITIES");

	    ret[4] = SSH1_AGENT_RSA_IDENTITIES_ANSWER;
	    keylist = pageant_make_keylist1(&len);
	    if (len + 5 > AGENT_MAX_MSGLEN) {
		sfree(keylist);
                fail_reason = "output would exceed max msglen";
		goto failure;
	    }
	    PUT_32BIT(ret, len + 1);
	    memcpy(ret + 5, keylist, len);

            plog(logctx, logfn, "reply: SSH1_AGENT_RSA_IDENTITIES_ANSWER");
            if (logfn) {               /* skip this loop if not logging */
                int i;
                struct RSAKey *rkey;
                for (i = 0; NULL != (rkey = pageant_nth_ssh1_key(i)); i++) {
                    char fingerprint[128];
                    rsa_fingerprint(fingerprint, sizeof(fingerprint), rkey);
                    plog(logctx, logfn, "returned key: %s", fingerprint);
                }
            }
	    sfree(keylist);
	}
	break;
      case SSH2_AGENTC_REQUEST_IDENTITIES:
	/*
	 * Reply with SSH2_AGENT_IDENTITIES_ANSWER.
	 */
	{
	    int len;
	    void *keylist;

            plog(logctx, logfn, "request: SSH2_AGENTC_REQUEST_IDENTITIES");

	    ret[4] = SSH2_AGENT_IDENTITIES_ANSWER;
	    keylist = pageant_make_keylist2(&len);
	    if (len + 5 > AGENT_MAX_MSGLEN) {
		sfree(keylist);
                fail_reason = "output would exceed max msglen";
		goto failure;
	    }
	    PUT_32BIT(ret, len + 1);
	    memcpy(ret + 5, keylist, len);

            plog(logctx, logfn, "reply: SSH2_AGENT_IDENTITIES_ANSWER");
            if (logfn) {               /* skip this loop if not logging */
                int i;
                struct ssh2_userkey *skey;
                for (i = 0; NULL != (skey = pageant_nth_ssh2_key(i)); i++) {
                    char *fingerprint = ssh2_fingerprint(skey->alg,
                                                         skey->data);
                    plog(logctx, logfn, "returned key: %s %s",
                         fingerprint, skey->comment);
                    sfree(fingerprint);
                }
            }

	    sfree(keylist);
	}
	break;
      case SSH1_AGENTC_RSA_CHALLENGE:
	/*
	 * Reply with either SSH1_AGENT_RSA_RESPONSE or
	 * SSH_AGENT_FAILURE, depending on whether we have that key
	 * or not.
	 */
	{
	    struct RSAKey reqkey, *key;
	    Bignum challenge, response;
	    unsigned char response_source[48], response_md5[16];
	    struct MD5Context md5c;
	    int i, len;
#ifdef PERSOPORT
		char fingerprint[100];
#endif

            plog(logctx, logfn, "request: SSH1_AGENTC_RSA_CHALLENGE");

	    p += 4;
	    i = ssh1_read_bignum(p, msgend - p, &reqkey.exponent);
	    if (i < 0) {
                fail_reason = "request truncated before key exponent";
		goto failure;
            }
	    p += i;
	    i = ssh1_read_bignum(p, msgend - p, &reqkey.modulus);
	    if (i < 0) {
                freebn(reqkey.exponent);
                fail_reason = "request truncated before key modulus";
		goto failure;
            }
	    p += i;
	    i = ssh1_read_bignum(p, msgend - p, &challenge);
	    if (i < 0) {
                freebn(reqkey.exponent);
                freebn(reqkey.modulus);
		freebn(challenge);
                fail_reason = "request truncated before challenge";
		goto failure;
            }
	    p += i;
	    if (msgend < p+16) {
		freebn(reqkey.exponent);
		freebn(reqkey.modulus);
		freebn(challenge);
                fail_reason = "request truncated before session id";
		goto failure;
	    }
	    memcpy(response_source + 32, p, 16);
	    p += 16;
	    if (msgend < p+4) {
		freebn(reqkey.exponent);
		freebn(reqkey.modulus);
		freebn(challenge);
                fail_reason = "request truncated before response type";
		goto failure;
            }
            if (GET_32BIT(p) != 1) {
		freebn(reqkey.exponent);
		freebn(reqkey.modulus);
		freebn(challenge);
                fail_reason = "response type other than 1 not supported";
		goto failure;
            }
            if (logfn) {
                char fingerprint[128];
                reqkey.comment = NULL;
                rsa_fingerprint(fingerprint, sizeof(fingerprint), &reqkey);
                plog(logctx, logfn, "requested key: %s", fingerprint);
            }
            if ((key = find234(rsakeys, &reqkey, NULL)) == NULL) {
		freebn(reqkey.exponent);
		freebn(reqkey.modulus);
		freebn(challenge);
                fail_reason = "key not found";
		goto failure;
	    }
#ifdef PERSOPORT
		rsa_fingerprint(fingerprint, sizeof(fingerprint), key);
		if (! confirm_key_usage(fingerprint, key->comment)) {
	      goto failure;
	    }
#endif
	    response = rsadecrypt(challenge, key);
	    for (i = 0; i < 32; i++)
		response_source[i] = bignum_byte(response, 31 - i);

	    MD5Init(&md5c);
	    MD5Update(&md5c, response_source, 48);
	    MD5Final(response_md5, &md5c);
	    smemclr(response_source, 48);	/* burn the evidence */
	    freebn(response);	       /* and that evidence */
	    freebn(challenge);	       /* yes, and that evidence */
	    freebn(reqkey.exponent);   /* and free some memory ... */
	    freebn(reqkey.modulus);    /* ... while we're at it. */

	    /*
	     * Packet is the obvious five byte header, plus sixteen
	     * bytes of MD5.
	     */
	    len = 5 + 16;
	    PUT_32BIT(ret, len - 4);
	    ret[4] = SSH1_AGENT_RSA_RESPONSE;
	    memcpy(ret + 5, response_md5, 16);

            plog(logctx, logfn, "reply: SSH1_AGENT_RSA_RESPONSE");
	}
	break;
      case SSH2_AGENTC_SIGN_REQUEST:
	/*
	 * Reply with either SSH2_AGENT_SIGN_RESPONSE or
	 * SSH_AGENT_FAILURE, depending on whether we have that key
	 * or not.
	 */
	{
	    struct ssh2_userkey *key;
	    struct blob b;
	    const unsigned char *data;
            unsigned char *signature;
	    int datalen, siglen, len;
#ifdef PERSOPORT
		char* confirm_fingerprint;
#endif
            plog(logctx, logfn, "request: SSH2_AGENTC_SIGN_REQUEST");

	    if (msgend < p+4) {
                fail_reason = "request truncated before public key";
		goto failure;
            }
	    b.len = toint(GET_32BIT(p));
            if (b.len < 0 || b.len > msgend - (p+4)) {
                fail_reason = "request truncated before public key";
                goto failure;
            }
	    p += 4;
	    b.blob = p;
	    p += b.len;
	    if (msgend < p+4) {
                fail_reason = "request truncated before string to sign";
		goto failure;
            }
	    datalen = toint(GET_32BIT(p));
	    p += 4;
	    if (datalen < 0 || datalen > msgend - p) {
                fail_reason = "request truncated before string to sign";
		goto failure;
            }
	    data = p;
            if (logfn) {
                char *fingerprint = ssh2_fingerprint_blob(b.blob, b.len);
                plog(logctx, logfn, "requested key: %s", fingerprint);
                sfree(fingerprint);
            }
	    key = find234(ssh2keys, &b, cmpkeys_ssh2_asymm);
	    if (!key) {
                fail_reason = "key not found";
		goto failure;
            }
#ifdef PERSOPORT
		confirm_fingerprint = ssh2_fingerprint_blob(b.blob, b.len);
		if (! confirm_key_usage( confirm_fingerprint , key->comment)) {
			sfree(confirm_fingerprint);
			goto failure;
	    }
		sfree(confirm_fingerprint);
#endif
	    signature = key->alg->sign(key->data, (const char *)data,
                                       datalen, &siglen);
	    len = 5 + 4 + siglen;
	    PUT_32BIT(ret, len - 4);
	    ret[4] = SSH2_AGENT_SIGN_RESPONSE;
	    PUT_32BIT(ret + 5, siglen);
	    memcpy(ret + 5 + 4, signature, siglen);
	    sfree(signature);

            plog(logctx, logfn, "reply: SSH2_AGENT_SIGN_RESPONSE");
	}
	break;
      case SSH1_AGENTC_ADD_RSA_IDENTITY:
	/*
	 * Add to the list and return SSH_AGENT_SUCCESS, or
	 * SSH_AGENT_FAILURE if the key was malformed.
	 */
	{
	    struct RSAKey *key;
	    char *comment;
            int n, commentlen;

            plog(logctx, logfn, "request: SSH1_AGENTC_ADD_RSA_IDENTITY");

	    key = snew(struct RSAKey);
	    memset(key, 0, sizeof(struct RSAKey));

	    n = makekey(p, msgend - p, key, NULL, 1);
	    if (n < 0) {
		freersakey(key);
		sfree(key);
                fail_reason = "request truncated before public key";
		goto failure;
	    }
	    p += n;

	    n = makeprivate(p, msgend - p, key);
	    if (n < 0) {
		freersakey(key);
		sfree(key);
                fail_reason = "request truncated before private key";
		goto failure;
	    }
	    p += n;

            /* SSH-1 names p and q the other way round, i.e. we have
             * the inverse of p mod q and not of q mod p. We swap the
             * names, because our internal RSA wants iqmp. */

	    n = ssh1_read_bignum(p, msgend - p, &key->iqmp);  /* p^-1 mod q */
	    if (n < 0) {
		freersakey(key);
		sfree(key);
                fail_reason = "request truncated before iqmp";
		goto failure;
	    }
	    p += n;

	    n = ssh1_read_bignum(p, msgend - p, &key->q);  /* p */
	    if (n < 0) {
		freersakey(key);
		sfree(key);
                fail_reason = "request truncated before p";
		goto failure;
	    }
	    p += n;

	    n = ssh1_read_bignum(p, msgend - p, &key->p);  /* q */
	    if (n < 0) {
		freersakey(key);
		sfree(key);
                fail_reason = "request truncated before q";
		goto failure;
	    }
	    p += n;

	    if (msgend < p+4) {
		freersakey(key);
		sfree(key);
                fail_reason = "request truncated before key comment";
		goto failure;
	    }
            commentlen = toint(GET_32BIT(p));

	    if (commentlen < 0 || commentlen > msgend - p) {
		freersakey(key);
		sfree(key);
                fail_reason = "request truncated before key comment";
		goto failure;
	    }

	    comment = snewn(commentlen+1, char);
	    if (comment) {
		memcpy(comment, p + 4, commentlen);
                comment[commentlen] = '\0';
		key->comment = comment;
	    }

            if (logfn) {
                char fingerprint[128];
                rsa_fingerprint(fingerprint, sizeof(fingerprint), key);
                plog(logctx, logfn, "submitted key: %s", fingerprint);
            }

	    if (add234(rsakeys, key) == key) {
		keylist_update();
                PUT_32BIT(ret, 1);
		ret[4] = SSH_AGENT_SUCCESS;

                plog(logctx, logfn, "reply: SSH_AGENT_SUCCESS");
	    } else {
		freersakey(key);
		sfree(key);

                fail_reason = "key already present";
                goto failure;
	    }
	}
	break;
      case SSH2_AGENTC_ADD_IDENTITY:
	/*
	 * Add to the list and return SSH_AGENT_SUCCESS, or
	 * SSH_AGENT_FAILURE if the key was malformed.
	 */
	{
	    struct ssh2_userkey *key;
	    char *comment;
            const char *alg;
	    int alglen, commlen;
	    int bloblen;

            plog(logctx, logfn, "request: SSH2_AGENTC_ADD_IDENTITY");

	    if (msgend < p+4) {
                fail_reason = "request truncated before key algorithm";
		goto failure;
            }
	    alglen = toint(GET_32BIT(p));
	    p += 4;
	    if (alglen < 0 || alglen > msgend - p) {
                fail_reason = "request truncated before key algorithm";
		goto failure;
            }
	    alg = (const char *)p;
	    p += alglen;

	    key = snew(struct ssh2_userkey);
            key->alg = find_pubkey_alg_len(alglen, alg);
	    if (!key->alg) {
		sfree(key);
                fail_reason = "algorithm unknown";
		goto failure;
	    }

	    bloblen = msgend - p;
	    key->data = key->alg->openssh_createkey(key->alg, &p, &bloblen);
	    if (!key->data) {
		sfree(key);
                fail_reason = "key setup failed";
		goto failure;
	    }

	    /*
	     * p has been advanced by openssh_createkey, but
	     * certainly not _beyond_ the end of the buffer.
	     */
	    assert(p <= msgend);

	    if (msgend < p+4) {
		key->alg->freekey(key->data);
		sfree(key);
                fail_reason = "request truncated before key comment";
		goto failure;
	    }
	    commlen = toint(GET_32BIT(p));
	    p += 4;

	    if (commlen < 0 || commlen > msgend - p) {
		key->alg->freekey(key->data);
		sfree(key);
                fail_reason = "request truncated before key comment";
		goto failure;
	    }
	    comment = snewn(commlen + 1, char);
	    if (comment) {
		memcpy(comment, p, commlen);
		comment[commlen] = '\0';
	    }
	    key->comment = comment;

            if (logfn) {
                char *fingerprint = ssh2_fingerprint(key->alg, key->data);
                plog(logctx, logfn, "submitted key: %s %s",
                     fingerprint, key->comment);
                sfree(fingerprint);
            }

	    if (add234(ssh2keys, key) == key) {
		keylist_update();
                PUT_32BIT(ret, 1);
		ret[4] = SSH_AGENT_SUCCESS;

                plog(logctx, logfn, "reply: SSH_AGENT_SUCCESS");
	    } else {
		key->alg->freekey(key->data);
		sfree(key->comment);
		sfree(key);

                fail_reason = "key already present";
                goto failure;
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
	    struct RSAKey reqkey, *key;
	    int n;

            plog(logctx, logfn, "request: SSH1_AGENTC_REMOVE_RSA_IDENTITY");

	    n = makekey(p, msgend - p, &reqkey, NULL, 0);
	    if (n < 0) {
                fail_reason = "request truncated before public key";
		goto failure;
            }

            if (logfn) {
                char fingerprint[128];
                reqkey.comment = NULL;
                rsa_fingerprint(fingerprint, sizeof(fingerprint), &reqkey);
                plog(logctx, logfn, "unwanted key: %s", fingerprint);
            }

	    key = find234(rsakeys, &reqkey, NULL);
	    freebn(reqkey.exponent);
	    freebn(reqkey.modulus);
	    PUT_32BIT(ret, 1);
	    if (key) {
                plog(logctx, logfn, "found with comment: %s", key->comment);

		del234(rsakeys, key);
		keylist_update();
		freersakey(key);
		sfree(key);
		ret[4] = SSH_AGENT_SUCCESS;

                plog(logctx, logfn, "reply: SSH_AGENT_SUCCESS");
	    } else {
                fail_reason = "key not found";
                goto failure;
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
	    struct ssh2_userkey *key;
	    struct blob b;

            plog(logctx, logfn, "request: SSH2_AGENTC_REMOVE_IDENTITY");

	    if (msgend < p+4) {
                fail_reason = "request truncated before public key";
		goto failure;
            }
	    b.len = toint(GET_32BIT(p));
	    p += 4;

	    if (b.len < 0 || b.len > msgend - p) {
                fail_reason = "request truncated before public key";
		goto failure;
            }
	    b.blob = p;
	    p += b.len;

            if (logfn) {
                char *fingerprint = ssh2_fingerprint_blob(b.blob, b.len);
                plog(logctx, logfn, "unwanted key: %s", fingerprint);
                sfree(fingerprint);
            }

	    key = find234(ssh2keys, &b, cmpkeys_ssh2_asymm);
	    if (!key) {
                fail_reason = "key not found";
		goto failure;
            }

            plog(logctx, logfn, "found with comment: %s", key->comment);

            del234(ssh2keys, key);
            keylist_update();
            key->alg->freekey(key->data);
            sfree(key);
	    PUT_32BIT(ret, 1);
            ret[4] = SSH_AGENT_SUCCESS;

            plog(logctx, logfn, "reply: SSH_AGENT_SUCCESS");
	}
	break;
      case SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES:
	/*
	 * Remove all SSH-1 keys. Always returns success.
	 */
	{
	    struct RSAKey *rkey;

            plog(logctx, logfn, "request:"
                " SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES");

	    while ((rkey = index234(rsakeys, 0)) != NULL) {
		del234(rsakeys, rkey);
		freersakey(rkey);
		sfree(rkey);
	    }
	    keylist_update();

	    PUT_32BIT(ret, 1);
	    ret[4] = SSH_AGENT_SUCCESS;

            plog(logctx, logfn, "reply: SSH_AGENT_SUCCESS");
	}
	break;
      case SSH2_AGENTC_REMOVE_ALL_IDENTITIES:
	/*
	 * Remove all SSH-2 keys. Always returns success.
	 */
	{
	    struct ssh2_userkey *skey;

            plog(logctx, logfn, "request: SSH2_AGENTC_REMOVE_ALL_IDENTITIES");

	    while ((skey = index234(ssh2keys, 0)) != NULL) {
		del234(ssh2keys, skey);
		skey->alg->freekey(skey->data);
		sfree(skey);
	    }
	    keylist_update();

	    PUT_32BIT(ret, 1);
	    ret[4] = SSH_AGENT_SUCCESS;

            plog(logctx, logfn, "reply: SSH_AGENT_SUCCESS");
	}
	break;
      default:
        plog(logctx, logfn, "request: unknown message type %d", type);

        fail_reason = "unrecognised message";
        /* fall through */
      failure:
	/*
	 * Unrecognised message. Return SSH_AGENT_FAILURE.
	 */
	PUT_32BIT(ret, 1);
	ret[4] = SSH_AGENT_FAILURE;
        plog(logctx, logfn, "reply: SSH_AGENT_FAILURE (%s)", fail_reason);
	break;
    }

    *outlen = 4 + GET_32BIT(ret);
    return ret;
}

void *pageant_failure_msg(int *outlen)
{
    unsigned char *ret = snewn(5, unsigned char);
    PUT_32BIT(ret, 1);
    ret[4] = SSH_AGENT_FAILURE;
    *outlen = 5;
    return ret;
}

void pageant_init(void)
{
    pageant_local = TRUE;
    rsakeys = newtree234(cmpkeys_rsa);
    ssh2keys = newtree234(cmpkeys_ssh2);
}

struct RSAKey *pageant_nth_ssh1_key(int i)
{
    return index234(rsakeys, i);
}

struct ssh2_userkey *pageant_nth_ssh2_key(int i)
{
    return index234(ssh2keys, i);
}

int pageant_count_ssh1_keys(void)
{
    return count234(rsakeys);
}

int pageant_count_ssh2_keys(void)
{
    return count234(ssh2keys);
}

int pageant_add_ssh1_key(struct RSAKey *rkey)
{
    return add234(rsakeys, rkey) == rkey;
}

int pageant_add_ssh2_key(struct ssh2_userkey *skey)
{
    return add234(ssh2keys, skey) == skey;
}

int pageant_delete_ssh1_key(struct RSAKey *rkey)
{
    struct RSAKey *deleted = del234(rsakeys, rkey);
    if (!deleted)
        return FALSE;
    assert(deleted == rkey);
    return TRUE;
}

int pageant_delete_ssh2_key(struct ssh2_userkey *skey)
{
    struct ssh2_userkey *deleted = del234(ssh2keys, skey);
    if (!deleted)
        return FALSE;
    assert(deleted == skey);
    return TRUE;
}

/* ----------------------------------------------------------------------
 * The agent plug.
 */

/*
 * Coroutine macros similar to, but simplified from, those in ssh.c.
 */
#define crBegin(v)	{ int *crLine = &v; switch(v) { case 0:;
#define crFinishV	} *crLine = 0; return; }
#define crGetChar(c) do                                         \
    {                                                           \
        while (len == 0) {                                      \
            *crLine =__LINE__; return; case __LINE__:;          \
        }                                                       \
        len--;                                                  \
        (c) = (unsigned char)*data++;                           \
    } while (0)

struct pageant_conn_state {
    const struct plug_function_table *fn;
    /* the above variable absolutely *must* be the first in this structure */

    Socket connsock;
    void *logctx;
    pageant_logfn_t logfn;
    unsigned char lenbuf[4], pktbuf[AGENT_MAX_MSGLEN];
    unsigned len, got;
    int real_packet;
    int crLine;            /* for coroutine in pageant_conn_receive */
};

static void pageant_conn_closing(Plug plug, const char *error_msg,
                                int error_code, int calling_back)
{
    struct pageant_conn_state *pc = (struct pageant_conn_state *)plug;
    if (error_msg)
        plog(pc->logctx, pc->logfn, "%p: error: %s", pc, error_msg);
    else
        plog(pc->logctx, pc->logfn, "%p: connection closed", pc);
    sk_close(pc->connsock);
    sfree(pc);
}

static void pageant_conn_sent(Plug plug, int bufsize)
{
    /* struct pageant_conn_state *pc = (struct pageant_conn_state *)plug; */

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

static void pageant_conn_receive(Plug plug, int urgent, char *data, int len)
{
    struct pageant_conn_state *pc = (struct pageant_conn_state *)plug;
    char c;

    crBegin(pc->crLine);

    while (len > 0) {
        pc->got = 0;
        while (pc->got < 4) {
            crGetChar(c);
            pc->lenbuf[pc->got++] = c;
        }

        pc->len = GET_32BIT(pc->lenbuf);
        pc->got = 0;
        pc->real_packet = (pc->len < AGENT_MAX_MSGLEN-4);

        while (pc->got < pc->len) {
            crGetChar(c);
            if (pc->real_packet)
                pc->pktbuf[pc->got] = c;
            pc->got++;
        }

        {
            void *reply;
            int replylen;

            if (pc->real_packet) {
                reply = pageant_handle_msg(pc->pktbuf, pc->len, &replylen, pc,
                                           pc->logfn?pageant_conn_log:NULL);
            } else {
                plog(pc->logctx, pc->logfn, "%p: overlong message (%u)",
                     pc, pc->len);
                plog(pc->logctx, pc->logfn, "%p: reply: SSH_AGENT_FAILURE "
                     "(message too long)", pc);
                reply = pageant_failure_msg(&replylen);
            }
            sk_write(pc->connsock, reply, replylen);
            smemclr(reply, replylen);
        }
    }

    crFinishV;
}

struct pageant_listen_state {
    const struct plug_function_table *fn;
    /* the above variable absolutely *must* be the first in this structure */

    Socket listensock;
    void *logctx;
    pageant_logfn_t logfn;
};

static void pageant_listen_closing(Plug plug, const char *error_msg,
                                  int error_code, int calling_back)
{
    struct pageant_listen_state *pl = (struct pageant_listen_state *)plug;
    if (error_msg)
        plog(pl->logctx, pl->logfn, "listening socket: error: %s", error_msg);
    sk_close(pl->listensock);
    pl->listensock = NULL;
}

static int pageant_listen_accepting(Plug plug,
                                    accept_fn_t constructor, accept_ctx_t ctx)
{
    static const struct plug_function_table connection_fn_table = {
	NULL, /* no log function, because that's for outgoing connections */
	pageant_conn_closing,
        pageant_conn_receive,
        pageant_conn_sent,
	NULL /* no accepting function, because we've already done it */
    };
    struct pageant_listen_state *pl = (struct pageant_listen_state *)plug;
    struct pageant_conn_state *pc;
    const char *err;
    char *peerinfo;

    pc = snew(struct pageant_conn_state);
    pc->fn = &connection_fn_table;
    pc->logfn = pl->logfn;
    pc->logctx = pl->logctx;
    pc->crLine = 0;

    pc->connsock = constructor(ctx, (Plug) pc);
    if ((err = sk_socket_error(pc->connsock)) != NULL) {
        sk_close(pc->connsock);
        sfree(pc);
	return TRUE;
    }

    sk_set_frozen(pc->connsock, 0);

    peerinfo = sk_peer_info(pc->connsock);
    if (peerinfo) {
        plog(pl->logctx, pl->logfn, "%p: new connection from %s",
             pc, peerinfo);
    } else {
        plog(pl->logctx, pl->logfn, "%p: new connection", pc);
    }

    return 0;
}

struct pageant_listen_state *pageant_listener_new(void)
{
    static const struct plug_function_table listener_fn_table = {
        NULL, /* no log function, because that's for outgoing connections */
        pageant_listen_closing,
        NULL, /* no receive function on a listening socket */
        NULL, /* no sent function on a listening socket */
        pageant_listen_accepting
    };

    struct pageant_listen_state *pl = snew(struct pageant_listen_state);
    pl->fn = &listener_fn_table;
    pl->logctx = NULL;
    pl->logfn = NULL;
    pl->listensock = NULL;
    return pl;
}

void pageant_listener_got_socket(struct pageant_listen_state *pl, Socket sock)
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
	free(pp);
    }
}

void *pageant_get_keylist1(int *length)
{
    void *ret;

    if (!pageant_local) {
	unsigned char request[5], *response;
	void *vresponse;
	int resplen;

	request[4] = SSH1_AGENTC_REQUEST_RSA_IDENTITIES;
	PUT_32BIT(request, 1);

        agent_query_synchronous(request, 5, &vresponse, &resplen);
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
	ret = pageant_make_keylist1(length);
    }
    return ret;
}

void *pageant_get_keylist2(int *length)
{
    void *ret;

    if (!pageant_local) {
	unsigned char request[5], *response;
	void *vresponse;
	int resplen;

	request[4] = SSH2_AGENTC_REQUEST_IDENTITIES;
	PUT_32BIT(request, 1);

	agent_query_synchronous(request, 5, &vresponse, &resplen);
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
	ret = pageant_make_keylist2(length);
    }
    return ret;
}

int pageant_add_keyfile(Filename *filename, const char *passphrase,
                        char **retstr)
{
    struct RSAKey *rkey = NULL;
    struct ssh2_userkey *skey = NULL;
    int needs_pass;
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
	void *blob;
	unsigned char *keylist, *p;
	int i, nkeys, bloblen, keylistlen;

	if (type == SSH_KEYTYPE_SSH1) {
	    if (!rsakey_pubblob(filename, &blob, &bloblen, NULL, &error)) {
                *retstr = dupprintf("Couldn't load private key (%s)", error);
                return PAGEANT_ACTION_FAILURE;
	    }
	    keylist = pageant_get_keylist1(&keylistlen);
	} else {
	    unsigned char *blob2;
#ifdef WINCRYPTPORT
#ifdef USE_CAPI
		if(0 == strncmp("cert://", filename->path, 7)) {
			blob = ssh2_userkey_loadpub(filename, NULL, &bloblen,
				&comment, &error);
			if(blob) {
				sfree(filename->path);
				filename->path = comment;
				comment = NULL;
			}
		}
		else {
#endif /* USE_CAPI */
#endif
	    blob = ssh2_userkey_loadpub(filename, NULL, &bloblen,
					NULL, &error);
#ifdef WINCRYPTPORT
#ifdef USE_CAPI
		}
#endif /* USE_CAPI */
#endif
	    if (!blob) {
                *retstr = dupprintf("Couldn't load private key (%s)", error);
		return PAGEANT_ACTION_FAILURE;
	    }
	    /* For our purposes we want the blob prefixed with its length */
	    blob2 = snewn(bloblen+4, unsigned char);
	    PUT_32BIT(blob2, bloblen);
	    memcpy(blob2 + 4, blob, bloblen);
	    sfree(blob);
	    blob = blob2;

	    keylist = pageant_get_keylist2(&keylistlen);
	}
	if (keylist) {
	    if (keylistlen < 4) {
		*retstr = dupstr("Received broken key list from agent");
                sfree(keylist);
                sfree(blob);
		return PAGEANT_ACTION_FAILURE;
	    }
	    nkeys = toint(GET_32BIT(keylist));
	    if (nkeys < 0) {
		*retstr = dupstr("Received broken key list from agent");
                sfree(keylist);
                sfree(blob);
		return PAGEANT_ACTION_FAILURE;
	    }
	    p = keylist + 4;
	    keylistlen -= 4;

	    for (i = 0; i < nkeys; i++) {
		if (!memcmp(blob, p, bloblen)) {
		    /* Key is already present; we can now leave. */
		    sfree(keylist);
		    sfree(blob);
                    return PAGEANT_ACTION_OK;
		}
		/* Now skip over public blob */
		if (type == SSH_KEYTYPE_SSH1) {
		    int n = rsa_public_blob_len(p, keylistlen);
		    if (n < 0) {
                        *retstr = dupstr("Received broken key list from agent");
                        sfree(keylist);
                        sfree(blob);
                        return PAGEANT_ACTION_FAILURE;
		    }
		    p += n;
		    keylistlen -= n;
		} else {
		    int n;
		    if (keylistlen < 4) {
                        *retstr = dupstr("Received broken key list from agent");
                        sfree(keylist);
                        sfree(blob);
                        return PAGEANT_ACTION_FAILURE;
		    }
		    n = GET_32BIT(p);
                    p += 4;
                    keylistlen -= 4;

		    if (n < 0 || n > keylistlen) {
                        *retstr = dupstr("Received broken key list from agent");
                        sfree(keylist);
                        sfree(blob);
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
                        sfree(blob);
                        return PAGEANT_ACTION_FAILURE;
		    }
		    n = GET_32BIT(p);
                    p += 4;
                    keylistlen -= 4;

		    if (n < 0 || n > keylistlen) {
                        *retstr = dupstr("Received broken key list from agent");
                        sfree(keylist);
                        sfree(blob);
                        return PAGEANT_ACTION_FAILURE;
		    }
		    p += n;
		    keylistlen -= n;
		}
	    }

	    sfree(keylist);
	}

	sfree(blob);
    }

    error = NULL;
    if (type == SSH_KEYTYPE_SSH1)
	needs_pass = rsakey_encrypted(filename, &comment);
    else
	needs_pass = ssh2_userkey_encrypted(filename, &comment);
    attempts = 0;
    if (type == SSH_KEYTYPE_SSH1)
	rkey = snew(struct RSAKey);

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
	    ret = loadrsakey(filename, rkey, this_passphrase, &error);
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
	    unsigned char *request, *response;
	    void *vresponse;
	    int reqlen, clen, resplen;

	    clen = strlen(rkey->comment);

	    reqlen = 4 + 1 +	       /* length, message type */
		4 +		       /* bit count */
		ssh1_bignum_length(rkey->modulus) +
		ssh1_bignum_length(rkey->exponent) +
		ssh1_bignum_length(rkey->private_exponent) +
		ssh1_bignum_length(rkey->iqmp) +
		ssh1_bignum_length(rkey->p) +
		ssh1_bignum_length(rkey->q) + 4 + clen	/* comment */
		;

	    request = snewn(reqlen, unsigned char);

	    request[4] = SSH1_AGENTC_ADD_RSA_IDENTITY;
	    reqlen = 5;
	    PUT_32BIT(request + reqlen, bignum_bitcount(rkey->modulus));
	    reqlen += 4;
	    reqlen += ssh1_write_bignum(request + reqlen, rkey->modulus);
	    reqlen += ssh1_write_bignum(request + reqlen, rkey->exponent);
	    reqlen +=
		ssh1_write_bignum(request + reqlen,
				  rkey->private_exponent);
	    reqlen += ssh1_write_bignum(request + reqlen, rkey->iqmp);
	    reqlen += ssh1_write_bignum(request + reqlen, rkey->p);
	    reqlen += ssh1_write_bignum(request + reqlen, rkey->q);
	    PUT_32BIT(request + reqlen, clen);
	    memcpy(request + reqlen + 4, rkey->comment, clen);
	    reqlen += 4 + clen;
	    PUT_32BIT(request, reqlen - 4);

	    agent_query_synchronous(request, reqlen, &vresponse, &resplen);
	    response = vresponse;
	    if (resplen < 5 || response[4] != SSH_AGENT_SUCCESS) {
		*retstr = dupstr("The already running Pageant "
                                 "refused to add the key.");
                freersakey(rkey);
                sfree(rkey);
                sfree(request);
                sfree(response);
                return PAGEANT_ACTION_FAILURE;
            }
            freersakey(rkey);
            sfree(rkey);
	    sfree(request);
	    sfree(response);
	} else {
	    if (!pageant_add_ssh1_key(rkey)) {
                freersakey(rkey);
		sfree(rkey);	       /* already present, don't waste RAM */
            }
	}
    } else {
	if (!pageant_local) {
	    unsigned char *request, *response;
	    void *vresponse;
	    int reqlen, alglen, clen, keybloblen, resplen;
	    alglen = strlen(skey->alg->name);
	    clen = strlen(skey->comment);

	    keybloblen = skey->alg->openssh_fmtkey(skey->data, NULL, 0);

	    reqlen = 4 + 1 +	       /* length, message type */
		4 + alglen +	       /* algorithm name */
		keybloblen +	       /* key data */
		4 + clen	       /* comment */
		;

	    request = snewn(reqlen, unsigned char);

	    request[4] = SSH2_AGENTC_ADD_IDENTITY;
	    reqlen = 5;
	    PUT_32BIT(request + reqlen, alglen);
	    reqlen += 4;
	    memcpy(request + reqlen, skey->alg->name, alglen);
	    reqlen += alglen;
	    reqlen += skey->alg->openssh_fmtkey(skey->data,
						request + reqlen,
						keybloblen);
	    PUT_32BIT(request + reqlen, clen);
	    memcpy(request + reqlen + 4, skey->comment, clen);
	    reqlen += clen + 4;
	    PUT_32BIT(request, reqlen - 4);

	    agent_query_synchronous(request, reqlen, &vresponse, &resplen);
	    response = vresponse;
	    if (resplen < 5 || response[4] != SSH_AGENT_SUCCESS) {
		*retstr = dupstr("The already running Pageant "
                                 "refused to add the key.");
                sfree(request);
                sfree(response);
                return PAGEANT_ACTION_FAILURE;
            }

	    sfree(request);
	    sfree(response);
	} else {
	    if (!pageant_add_ssh2_key(skey)) {
		skey->alg->freekey(skey->data);
		sfree(skey);	       /* already present, don't waste RAM */
	    }
	}
    }
    return PAGEANT_ACTION_OK;
}

int pageant_enum_keys(pageant_key_enum_fn_t callback, void *callback_ctx,
                      char **retstr)
{
    unsigned char *keylist, *p;
    int i, nkeys, keylistlen;
    char *comment;
    struct pageant_pubkey cbkey;

    keylist = pageant_get_keylist1(&keylistlen);
    if (keylistlen < 4) {
        *retstr = dupstr("Received broken SSH-1 key list from agent");
        sfree(keylist);
        return PAGEANT_ACTION_FAILURE;
    }
    nkeys = toint(GET_32BIT(keylist));
    if (nkeys < 0) {
        *retstr = dupstr("Received broken SSH-1 key list from agent");
        sfree(keylist);
        return PAGEANT_ACTION_FAILURE;
    }
    p = keylist + 4;
    keylistlen -= 4;

    for (i = 0; i < nkeys; i++) {
        struct RSAKey rkey;
        char fingerprint[128];
        int n;

        /* public blob and fingerprint */
        memset(&rkey, 0, sizeof(rkey));
        n = makekey(p, keylistlen, &rkey, NULL, 0);
        if (n < 0 || n > keylistlen) {
            freersakey(&rkey);
            *retstr = dupstr("Received broken SSH-1 key list from agent");
            sfree(keylist);
            return PAGEANT_ACTION_FAILURE;
        }
        p += n, keylistlen -= n;
        rsa_fingerprint(fingerprint, sizeof(fingerprint), &rkey);

        /* comment */
        if (keylistlen < 4) {
            *retstr = dupstr("Received broken SSH-1 key list from agent");
            freersakey(&rkey);
            sfree(keylist);
            return PAGEANT_ACTION_FAILURE;
        }
        n = toint(GET_32BIT(p));
        p += 4, keylistlen -= 4;
        if (n < 0 || keylistlen < n) {
            *retstr = dupstr("Received broken SSH-1 key list from agent");
            freersakey(&rkey);
            sfree(keylist);
            return PAGEANT_ACTION_FAILURE;
        }
        comment = dupprintf("%.*s", (int)n, (const char *)p);
        p += n, keylistlen -= n;

        cbkey.blob = rsa_public_blob(&rkey, &cbkey.bloblen);
        cbkey.comment = comment;
        cbkey.ssh_version = 1;
        callback(callback_ctx, fingerprint, comment, &cbkey);
        sfree(cbkey.blob);
        freersakey(&rkey);
        sfree(comment);
    }

    sfree(keylist);

    if (keylistlen != 0) {
        *retstr = dupstr("Received broken SSH-1 key list from agent");
        return PAGEANT_ACTION_FAILURE;
    }

    keylist = pageant_get_keylist2(&keylistlen);
    if (keylistlen < 4) {
        *retstr = dupstr("Received broken SSH-2 key list from agent");
        sfree(keylist);
        return PAGEANT_ACTION_FAILURE;
    }
    nkeys = toint(GET_32BIT(keylist));
    if (nkeys < 0) {
        *retstr = dupstr("Received broken SSH-2 key list from agent");
        sfree(keylist);
        return PAGEANT_ACTION_FAILURE;
    }
    p = keylist + 4;
    keylistlen -= 4;

    for (i = 0; i < nkeys; i++) {
        char *fingerprint;
        int n;

        /* public blob */
        if (keylistlen < 4) {
            *retstr = dupstr("Received broken SSH-2 key list from agent");
            sfree(keylist);
            return PAGEANT_ACTION_FAILURE;
        }
        n = toint(GET_32BIT(p));
        p += 4, keylistlen -= 4;
        if (n < 0 || keylistlen < n) {
            *retstr = dupstr("Received broken SSH-2 key list from agent");
            sfree(keylist);
            return PAGEANT_ACTION_FAILURE;
        }
        fingerprint = ssh2_fingerprint_blob(p, n);
        cbkey.blob = p;
        cbkey.bloblen = n;
        p += n, keylistlen -= n;

        /* comment */
        if (keylistlen < 4) {
            *retstr = dupstr("Received broken SSH-2 key list from agent");
            sfree(fingerprint);
            sfree(keylist);
            return PAGEANT_ACTION_FAILURE;
        }
        n = toint(GET_32BIT(p));
        p += 4, keylistlen -= 4;
        if (n < 0 || keylistlen < n) {
            *retstr = dupstr("Received broken SSH-2 key list from agent");
            sfree(fingerprint);
            sfree(keylist);
            return PAGEANT_ACTION_FAILURE;
        }
        comment = dupprintf("%.*s", (int)n, (const char *)p);
        p += n, keylistlen -= n;

        cbkey.ssh_version = 2;
        cbkey.comment = comment;
        callback(callback_ctx, fingerprint, comment, &cbkey);
        sfree(fingerprint);
        sfree(comment);
    }

    sfree(keylist);

    if (keylistlen != 0) {
        *retstr = dupstr("Received broken SSH-1 key list from agent");
        return PAGEANT_ACTION_FAILURE;
    }

    return PAGEANT_ACTION_OK;
}

int pageant_delete_key(struct pageant_pubkey *key, char **retstr)
{
    unsigned char *request, *response;
    int reqlen, resplen, ret;
    void *vresponse;

    if (key->ssh_version == 1) {
        reqlen = 5 + key->bloblen;
        request = snewn(reqlen, unsigned char);
        PUT_32BIT(request, reqlen - 4);
        request[4] = SSH1_AGENTC_REMOVE_RSA_IDENTITY;
        memcpy(request + 5, key->blob, key->bloblen);
    } else {
        reqlen = 9 + key->bloblen;
        request = snewn(reqlen, unsigned char);
        PUT_32BIT(request, reqlen - 4);
        request[4] = SSH2_AGENTC_REMOVE_IDENTITY;
        PUT_32BIT(request + 5, key->bloblen);
        memcpy(request + 9, key->blob, key->bloblen);
    }

    agent_query_synchronous(request, reqlen, &vresponse, &resplen);
    response = vresponse;
    if (resplen < 5 || response[4] != SSH_AGENT_SUCCESS) {
        *retstr = dupstr("Agent failed to delete key");
        ret = PAGEANT_ACTION_FAILURE;
    } else {
        *retstr = NULL;
        ret = PAGEANT_ACTION_OK;
    }
    sfree(request);
    sfree(response);
    return ret;
}

int pageant_delete_all_keys(char **retstr)
{
    unsigned char request[5], *response;
    int reqlen, resplen, success;
    void *vresponse;

    PUT_32BIT(request, 1);
    request[4] = SSH2_AGENTC_REMOVE_ALL_IDENTITIES;
    reqlen = 5;
    agent_query_synchronous(request, reqlen, &vresponse, &resplen);
    response = vresponse;
    success = (resplen >= 4 && response[4] == SSH_AGENT_SUCCESS);
    sfree(response);
    if (!success) {
        *retstr = dupstr("Agent failed to delete SSH-2 keys");
        return PAGEANT_ACTION_FAILURE;
    }

    PUT_32BIT(request, 1);
    request[4] = SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES;
    reqlen = 5;
    agent_query_synchronous(request, reqlen, &vresponse, &resplen);
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
    ret->blob = snewn(key->bloblen, unsigned char);
    memcpy(ret->blob, key->blob, key->bloblen);
    ret->bloblen = key->bloblen;
    ret->comment = key->comment ? dupstr(key->comment) : NULL;
    ret->ssh_version = key->ssh_version;
    return ret;
}

void pageant_pubkey_free(struct pageant_pubkey *key)
{
    sfree(key->comment);
    sfree(key->blob);
    sfree(key);
}
