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

struct PageantClientDialogId {
    int dummy;
};

typedef struct PageantKeySort PageantKeySort;
typedef struct PageantKey PageantKey;
typedef struct PageantAsyncOp PageantAsyncOp;
typedef struct PageantAsyncOpVtable PageantAsyncOpVtable;
typedef struct PageantClientRequestNode PageantClientRequestNode;
typedef struct PageantKeyRequestNode PageantKeyRequestNode;

struct PageantClientRequestNode {
    PageantClientRequestNode *prev, *next;
};
struct PageantKeyRequestNode {
    PageantKeyRequestNode *prev, *next;
};

struct PageantClientInfo {
    PageantClient *pc; /* goes to NULL when client is unregistered */
    PageantClientRequestNode head;
};

struct PageantAsyncOp {
    const PageantAsyncOpVtable *vt;
    PageantClientInfo *info;
    PageantClientRequestNode cr;
    PageantClientRequestId *reqid;
};
struct PageantAsyncOpVtable {
    void (*coroutine)(PageantAsyncOp *pao);
    void (*free)(PageantAsyncOp *pao);
};
static inline void pageant_async_op_coroutine(PageantAsyncOp *pao)
{ pao->vt->coroutine(pao); }
static inline void pageant_async_op_free(PageantAsyncOp *pao)
{
    delete_callbacks_for_context(pao);
    pao->vt->free(pao);
}
static inline void pageant_async_op_unlink(PageantAsyncOp *pao)
{
    pao->cr.prev->next = pao->cr.next;
    pao->cr.next->prev = pao->cr.prev;
}
static inline void pageant_async_op_unlink_and_free(PageantAsyncOp *pao)
{
    pageant_async_op_unlink(pao);
    pageant_async_op_free(pao);
}
static void pageant_async_op_callback(void *vctx)
{
    pageant_async_op_coroutine((PageantAsyncOp *)vctx);
}

/*
 * Master list of all the keys we have stored, in any form at all.
 */
static tree234 *keytree;
struct PageantKeySort {
    /* Prefix of the main PageantKey structure which contains all the
     * data that the sorting order depends on. Also simple enough that
     * you can construct one for lookup purposes. */
    int ssh_version; /* 1 or 2; primary sort key */
    ptrlen public_blob; /* secondary sort key */
};
struct PageantKey {
    PageantKeySort sort;
    strbuf *public_blob; /* the true owner of sort.public_blob */
    char *comment;       /* stored separately, whether or not in rkey/skey */
    union {
        RSAKey *rkey;       /* if ssh_version == 1 */
        ssh2_userkey *skey; /* if ssh_version == 2 */
    };
    strbuf *encrypted_key_file;
    bool decryption_prompt_active;
    PageantKeyRequestNode blocked_requests;
    PageantClientDialogId dlgid;
};

typedef struct PageantSignOp PageantSignOp;
struct PageantSignOp {
    PageantKey *pk;
    strbuf *data_to_sign;
    unsigned flags;
    int crLine;
    unsigned char failure_type;

    PageantKeyRequestNode pkr;
    PageantAsyncOp pao;
};

/* Master lock that indicates whether a GUI request is currently in
 * progress */
static bool gui_request_in_progress = false;

static void failure(PageantClient *pc, PageantClientRequestId *reqid,
                    strbuf *sb, unsigned char type, const char *fmt, ...);
static void fail_requests_for_key(PageantKey *pk, const char *reason);
static PageantKey *pageant_nth_key(int ssh_version, int i);

static void pk_free(PageantKey *pk)
{
    if (pk->public_blob) strbuf_free(pk->public_blob);
    sfree(pk->comment);
    if (pk->sort.ssh_version == 1 && pk->rkey) {
        freersakey(pk->rkey);
        sfree(pk->rkey);
    }
    if (pk->sort.ssh_version == 2 && pk->skey) {
        sfree(pk->skey->comment);
        ssh_key_free(pk->skey->key);
        sfree(pk->skey);
    }
    if (pk->encrypted_key_file) strbuf_free(pk->encrypted_key_file);
    fail_requests_for_key(pk, "key deleted from Pageant while signing "
                          "request was pending");
    sfree(pk);
}

static int cmpkeys(void *av, void *bv)
{
    PageantKeySort *a = (PageantKeySort *)av, *b = (PageantKeySort *)bv;

    if (a->ssh_version != b->ssh_version)
        return a->ssh_version < b->ssh_version ? -1 : +1;
    else
        return ptrlen_strcmp(a->public_blob, b->public_blob);
}

static inline PageantKeySort keysort(int version, ptrlen blob)
{
    PageantKeySort sort;
    sort.ssh_version = version;
    sort.public_blob = blob;
    return sort;
}

static strbuf *makeblob1(RSAKey *rkey)
{
    strbuf *blob = strbuf_new();
    rsa_ssh1_public_blob(BinarySink_UPCAST(blob), rkey,
                         RSA_SSH1_EXPONENT_FIRST);
    return blob;
}

static strbuf *makeblob2(ssh2_userkey *skey)
{
    strbuf *blob = strbuf_new();
    ssh_key_public_blob(skey->key, BinarySink_UPCAST(blob));
    return blob;
}

static PageantKey *findkey1(RSAKey *reqkey)
{
    strbuf *blob = makeblob1(reqkey);
    PageantKeySort sort = keysort(1, ptrlen_from_strbuf(blob));
    PageantKey *toret = find234(keytree, &sort, NULL);
    strbuf_free(blob);
    return toret;
}

static PageantKey *findkey2(ptrlen blob)
{
    PageantKeySort sort = keysort(2, blob);
    return find234(keytree, &sort, NULL);
}

static int find_first_key_for_version(int ssh_version)
{
    PageantKeySort sort = keysort(ssh_version, PTRLEN_LITERAL(""));
    int pos;
    if (findrelpos234(keytree, &sort, NULL, REL234_GE, &pos))
        return pos;
    return count234(keytree);
}

static int count_keys(int ssh_version)
{
    return (find_first_key_for_version(ssh_version + 1) -
            find_first_key_for_version(ssh_version));
}
int pageant_count_ssh1_keys(void) { return count_keys(1); }
int pageant_count_ssh2_keys(void) { return count_keys(2); }

static bool pageant_add_ssh1_key(RSAKey *rkey)
{
    PageantKey *pk = snew(PageantKey);
    memset(pk, 0, sizeof(PageantKey));
    pk->sort.ssh_version = 1;
    pk->public_blob = makeblob1(rkey);
    pk->sort.public_blob = ptrlen_from_strbuf(pk->public_blob);
    pk->blocked_requests.next = pk->blocked_requests.prev =
        &pk->blocked_requests;

    if (add234(keytree, pk) == pk) {
        pk->rkey = rkey;
        if (rkey->comment)
            pk->comment = dupstr(rkey->comment);
        return true;
    } else {
        pk_free(pk);
        return false;
    }
}

static bool pageant_add_ssh2_key(ssh2_userkey *skey)
{
    PageantKey *pk = snew(PageantKey);
    memset(pk, 0, sizeof(PageantKey));
    pk->sort.ssh_version = 2;
    pk->public_blob = makeblob2(skey);
    pk->sort.public_blob = ptrlen_from_strbuf(pk->public_blob);
    pk->blocked_requests.next = pk->blocked_requests.prev =
        &pk->blocked_requests;

    PageantKey *pk_in_tree = add234(keytree, pk);
    if (pk_in_tree == pk) {
        /* The key wasn't in the tree at all, and we've just added it. */
        pk->skey = skey;
        if (skey->comment)
            pk->comment = dupstr(skey->comment);
        return true;
    } else if (!pk_in_tree->skey) {
        /* The key was only stored encrypted, and now we have an
         * unencrypted version to add to the existing record. */
        pk_in_tree->skey = skey;
        pk_free(pk);
        return true;
    } else {
        /* The key was already in the tree in full. */
        pk_free(pk);
        return false;
    }
}

static void remove_all_keys(int ssh_version)
{
    int start = find_first_key_for_version(ssh_version);
    int end = find_first_key_for_version(ssh_version + 1);
    while (end > start) {
        PageantKey *pk = delpos234(keytree, --end);
        assert(pk->sort.ssh_version == ssh_version);
        pk_free(pk);
    }
}

static void list_keys(BinarySink *bs, int ssh_version, bool extended)
{
    int i;
    PageantKey *pk;

    put_uint32(bs, count_keys(ssh_version));
    for (i = find_first_key_for_version(ssh_version);
         NULL != (pk = index234(keytree, i)); i++) {
        if (pk->sort.ssh_version != ssh_version)
            break;

        if (ssh_version > 1)
            put_stringpl(bs, pk->sort.public_blob);
        else
            put_datapl(bs, pk->sort.public_blob); /* no header */

        put_stringpl(bs, ptrlen_from_asciz(pk->comment));

        if (extended) {
            /*
             * Append to each key entry a string containing extension
             * data. This string begins with a flags word, and may in
             * future contain further data if flag bits are set saying
             * that it does. Hence, it's wrapped in a containing
             * string, so that clients that only partially understand
             * it can still find the parts they do understand.
             */
            strbuf *sb = strbuf_new();

            uint32_t flags = 0;
            if (!pk->skey)
                flags |= LIST_EXTENDED_FLAG_HAS_NO_CLEARTEXT_KEY;
            if (pk->encrypted_key_file)
                flags |= LIST_EXTENDED_FLAG_HAS_ENCRYPTED_KEY_FILE;
            put_uint32(sb, flags);

            put_stringsb(bs, sb);
        }
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
	//return i ;  // Ne marche pas
	if( (tab_int == NULL) || !GetScrumbleKeyFlag() ) { return i ; }
	else { return tab_int[i] ; }
}
static int confirm_key_usage(char* fingerprint, char* comment) {
	const char* title = "Confirm SSH Key usage";
	char* message = NULL;
	int result = IDYES; // successful result is the default
	if(comment!=NULL) {
		if( ( GetAskConfirmationFlag()==1 ) 
			||
			( (GetAskConfirmationFlag()==2) && (
			(NULL != strstr(comment, "needs confirm"))||(NULL != strstr(comment, "need confirm"))||(NULL != strstr(comment, "confirmation"))
			)
			) ) {
			message = dupprintf("Allow authentication with key with fingerprint\n%s\ncomment: %s", fingerprint, comment);
			result = MessageBox(NULL, message, title, MB_ICONQUESTION | MB_YESNO | MB_SYSTEMMODAL);
		}
	}
	
	if (result != IDYES) {
		if(message!=NULL) sfree(message);
		return 0;
	} else {
		message = dupprintf("Allow authentication with key with fingerprint\n%s\ncomment: %s", fingerprint, comment);
		if( GetShowBalloonOnKeyUsage()==1 ) ShowBalloonTip( trayIcone, "SSH private key usage", message ) ;
		if(message!=NULL) sfree(message);
		return 1;
	}
}
#endif

void pageant_make_keylist1(BinarySink *bs) { list_keys(bs, 1, false); }
void pageant_make_keylist2(BinarySink *bs) { list_keys(bs, 2, false); }
void pageant_make_keylist_extended(BinarySink *bs) { list_keys(bs, 2, true); }

void pageant_register_client(PageantClient *pc)
{
    pc->info = snew(PageantClientInfo);
    pc->info->pc = pc;
    pc->info->head.prev = pc->info->head.next = &pc->info->head;
}

void pageant_unregister_client(PageantClient *pc)
{
    PageantClientInfo *info = pc->info;
    assert(info);
    assert(info->pc == pc);

    while (pc->info->head.next != &pc->info->head) {
        PageantAsyncOp *pao = container_of(pc->info->head.next,
                                           PageantAsyncOp, cr);
        pageant_async_op_unlink_and_free(pao);
    }

    sfree(pc->info);
}

static PRINTF_LIKE(5, 6) void failure(
    PageantClient *pc, PageantClientRequestId *reqid, strbuf *sb,
    unsigned char type, const char *fmt, ...)
{
    strbuf_clear(sb);
    put_byte(sb, type);
    if (!pc->suppress_logging) {
        va_list ap;
        va_start(ap, fmt);
        char *msg = dupvprintf(fmt, ap);
        va_end(ap);
        pageant_client_log(pc, reqid, "reply: SSH_AGENT_FAILURE (%s)", msg);
        sfree(msg);
    }
}

static void signop_link(PageantSignOp *so)
{
    assert(!so->pkr.prev);
    assert(!so->pkr.next);

    so->pkr.prev = so->pk->blocked_requests.prev;
    so->pkr.next = &so->pk->blocked_requests;
    so->pkr.prev->next = &so->pkr;
    so->pkr.next->prev = &so->pkr;
}

static void signop_unlink(PageantSignOp *so)
{
    if (so->pkr.next) {
        assert(so->pkr.prev);
        so->pkr.next->prev = so->pkr.prev;
        so->pkr.prev->next = so->pkr.next;
    } else {
        assert(!so->pkr.prev);
    }
}

static void signop_free(PageantAsyncOp *pao)
{
    PageantSignOp *so = container_of(pao, PageantSignOp, pao);
    strbuf_free(so->data_to_sign);
    sfree(so);
}

static bool request_passphrase(PageantClient *pc, PageantKey *pk)
{
    if (!pk->decryption_prompt_active) {
        assert(!gui_request_in_progress);

        bool created_dlg = pageant_client_ask_passphrase(
            pc, &pk->dlgid, pk->comment);

        if (!created_dlg)
            return false;

        gui_request_in_progress = true;
        pk->decryption_prompt_active = true;
    }

    return true;
}

static void signop_coroutine(PageantAsyncOp *pao)
{
    PageantSignOp *so = container_of(pao, PageantSignOp, pao);
    strbuf *response;

    crBegin(so->crLine);

    while (!so->pk->skey && gui_request_in_progress)
        crReturnV;

    if (!so->pk->skey) {
        assert(so->pk->encrypted_key_file);

        if (!request_passphrase(so->pao.info->pc, so->pk)) {
            response = strbuf_new();
            failure(so->pao.info->pc, so->pao.reqid, response,
                    so->failure_type, "on-demand decryption could not "
                    "prompt for a passphrase");
            goto respond;
        }

        signop_link(so);
        crReturnV;
        signop_unlink(so);
    }

    uint32_t supported_flags = ssh_key_alg(so->pk->skey->key)->supported_flags;
    if (so->flags & ~supported_flags) {
        /*
         * We MUST reject any message containing flags we don't
         * understand.
         */
        response = strbuf_new();
        failure(so->pao.info->pc, so->pao.reqid, response, so->failure_type,
                "unsupported flag bits 0x%08"PRIx32,
                so->flags & ~supported_flags);
        goto respond;
    }

    char *invalid = ssh_key_invalid(so->pk->skey->key, so->flags);
    if (invalid) {
        response = strbuf_new();
        failure(so->pao.info->pc, so->pao.reqid, response, so->failure_type,
                "key invalid: %s", invalid);
        sfree(invalid);
        goto respond;
    }

    strbuf *signature = strbuf_new();
    ssh_key_sign(so->pk->skey->key, ptrlen_from_strbuf(so->data_to_sign),
                 so->flags, BinarySink_UPCAST(signature));

    response = strbuf_new();
    put_byte(response, SSH2_AGENT_SIGN_RESPONSE);
    put_stringsb(response, signature);

  respond:
    pageant_client_got_response(so->pao.info->pc, so->pao.reqid,
                                ptrlen_from_strbuf(response));
    strbuf_free(response);

    pageant_async_op_unlink_and_free(&so->pao);
    crFinishFreedV;
}

static const PageantAsyncOpVtable signop_vtable = {
    .coroutine = signop_coroutine,
    .free = signop_free,
};

static void fail_requests_for_key(PageantKey *pk, const char *reason)
{
    while (pk->blocked_requests.next != &pk->blocked_requests) {
        PageantSignOp *so = container_of(pk->blocked_requests.next,
                                         PageantSignOp, pkr);
        signop_unlink(so);
        strbuf *sb = strbuf_new();
        failure(so->pao.info->pc, so->pao.reqid, sb, so->failure_type,
                "%s", reason);
        pageant_client_got_response(so->pao.info->pc, so->pao.reqid,
                                    ptrlen_from_strbuf(sb));
        strbuf_free(sb);
        pageant_async_op_unlink_and_free(&so->pao);
    }
}

static void unblock_requests_for_key(PageantKey *pk)
{
    for (PageantKeyRequestNode *pkr = pk->blocked_requests.next;
         pkr != &pk->blocked_requests; pkr = pkr->next) {
        PageantSignOp *so = container_of(pk->blocked_requests.next,
                                         PageantSignOp, pkr);
        queue_toplevel_callback(pageant_async_op_callback, &so->pao);
    }
}

void pageant_passphrase_request_success(PageantClientDialogId *dlgid,
                                        ptrlen passphrase)
{
    PageantKey *pk = container_of(dlgid, PageantKey, dlgid);

    assert(gui_request_in_progress);
    gui_request_in_progress = false;
    pk->decryption_prompt_active = false;

    if (!pk->skey) {
        const char *error;

        BinarySource src[1];
        BinarySource_BARE_INIT_PL(src, ptrlen_from_strbuf(
                                      pk->encrypted_key_file));

        strbuf *ppsb = strbuf_new_nm();
        put_datapl(ppsb, passphrase);

        pk->skey = ppk_load_s(src, ppsb->s, &error);

        strbuf_free(ppsb);

        if (!pk->skey) {
            fail_requests_for_key(pk, "unable to decrypt key");
            return;
        } else if (pk->skey == SSH2_WRONG_PASSPHRASE) {
            pk->skey = NULL;

            /*
             * Find a PageantClient to use for another attempt at
             * request_passphrase.
             */
            PageantKeyRequestNode *pkr = pk->blocked_requests.next;
            if (pkr == &pk->blocked_requests) {
                /*
                 * Special case: if all the requests have gone away at
                 * this point, we need not bother putting up a request
                 * at all any more.
                 */
                return;
            }

            PageantSignOp *so = container_of(pk->blocked_requests.next,
                                             PageantSignOp, pkr);

            pk->decryption_prompt_active = false;
            if (!request_passphrase(so->pao.info->pc, pk)) {
                fail_requests_for_key(pk, "unable to continue creating "
                                      "passphrase prompts");
            }
            return;
        } else {
            keylist_update();
        }
    }

    unblock_requests_for_key(pk);
}

void pageant_passphrase_request_refused(PageantClientDialogId *dlgid)
{
    PageantKey *pk = container_of(dlgid, PageantKey, dlgid);

    assert(gui_request_in_progress);
    gui_request_in_progress = false;
    pk->decryption_prompt_active = false;

    fail_requests_for_key(pk, "user refused to supply passphrase");
}

typedef struct PageantImmOp PageantImmOp;
struct PageantImmOp {
    int crLine;
    strbuf *response;

    PageantAsyncOp pao;
};

static void immop_free(PageantAsyncOp *pao)
{
    PageantImmOp *io = container_of(pao, PageantImmOp, pao);
    if (io->response)
        strbuf_free(io->response);
    sfree(io);
}

static void immop_coroutine(PageantAsyncOp *pao)
{
    PageantImmOp *io = container_of(pao, PageantImmOp, pao);

    crBegin(io->crLine);

    if (0) crReturnV;

    pageant_client_got_response(io->pao.info->pc, io->pao.reqid,
                                ptrlen_from_strbuf(io->response));
    pageant_async_op_unlink_and_free(&io->pao);
    crFinishFreedV;
}

static const PageantAsyncOpVtable immop_vtable = {
    .coroutine = immop_coroutine,
    .free = immop_free,
};

static bool reencrypt_key(PageantKey *pk)
{
    if (pk->sort.ssh_version != 2) {
        /*
         * We don't support storing SSH-1 keys in encrypted form at
         * all.
         */
        return false;
    }

    if (!pk->encrypted_key_file) {
        /*
         * We can't re-encrypt a key if it doesn't have an encrypted
         * form. (We could make one up, of course - but with what
         * passphrase that we could expect the user to know later?)
         */
        return false;
    }

    /* Only actually free pk->skey if it exists. But we return success
     * regardless, so that 'please ensure this key isn't stored
     * decrypted' is idempotent. */
    if (pk->skey) {
        sfree(pk->skey->comment);
        ssh_key_free(pk->skey->key);
        sfree(pk->skey);
        pk->skey = NULL;
    }

    return true;
}

#define DECL_EXT_ENUM(id, name) id,
enum Extension { KNOWN_EXTENSIONS(DECL_EXT_ENUM) EXT_UNKNOWN };
#define DEF_EXT_NAMES(id, name) PTRLEN_DECL_LITERAL(name),
static const ptrlen extension_names[] = { KNOWN_EXTENSIONS(DEF_EXT_NAMES) };

static PageantAsyncOp *pageant_make_op(
    PageantClient *pc, PageantClientRequestId *reqid, ptrlen msgpl)
{
    BinarySource msg[1];
    strbuf *sb = strbuf_new_nm();
    unsigned char failure_type = SSH_AGENT_FAILURE;
    int type;

#define fail(...) failure(pc, reqid, sb, failure_type, __VA_ARGS__)

    BinarySource_BARE_INIT_PL(msg, msgpl);

    type = get_byte(msg);
    if (get_err(msg)) {
        fail("message contained no type code");
        goto responded;
    }

    switch (type) {
      case SSH1_AGENTC_REQUEST_RSA_IDENTITIES: {
        /*
         * Reply with SSH1_AGENT_RSA_IDENTITIES_ANSWER.
         */
        pageant_client_log(pc, reqid,
                           "request: SSH1_AGENTC_REQUEST_RSA_IDENTITIES");

        put_byte(sb, SSH1_AGENT_RSA_IDENTITIES_ANSWER);
        pageant_make_keylist1(BinarySink_UPCAST(sb));

        pageant_client_log(pc, reqid,
                           "reply: SSH1_AGENT_RSA_IDENTITIES_ANSWER");
        if (!pc->suppress_logging) {
            int i;
            PageantKey *pk;
            for (i = 0; NULL != (pk = pageant_nth_key(1, i)); i++) {
                char *fingerprint = rsa_ssh1_fingerprint(pk->rkey);
                pageant_client_log(pc, reqid, "returned key: %s",
                                   fingerprint);
                sfree(fingerprint);
            }
        }
        break;
      }
      case SSH2_AGENTC_REQUEST_IDENTITIES: {
        /*
         * Reply with SSH2_AGENT_IDENTITIES_ANSWER.
         */
        pageant_client_log(pc, reqid,
                           "request: SSH2_AGENTC_REQUEST_IDENTITIES");

        put_byte(sb, SSH2_AGENT_IDENTITIES_ANSWER);
        pageant_make_keylist2(BinarySink_UPCAST(sb));

        pageant_client_log(pc, reqid, "reply: SSH2_AGENT_IDENTITIES_ANSWER");
        if (!pc->suppress_logging) {
            int i;
            PageantKey *pk;
            for (i = 0; NULL != (pk = pageant_nth_key(2, i)); i++) {
                char *fingerprint = ssh2_fingerprint_blob(
                    ptrlen_from_strbuf(pk->public_blob), SSH_FPTYPE_DEFAULT);
                pageant_client_log(pc, reqid, "returned key: %s %s",
                                   fingerprint, pk->comment);
                sfree(fingerprint);
            }
        }
        break;
      }
      case SSH1_AGENTC_RSA_CHALLENGE: {
        /*
         * Reply with either SSH1_AGENT_RSA_RESPONSE or
         * SSH_AGENT_FAILURE, depending on whether we have that key
         * or not.
         */
        RSAKey reqkey;
        PageantKey *pk;
        mp_int *challenge, *response;
        ptrlen session_id;
        unsigned response_type;
        unsigned char response_md5[16];
        int i;
	
        pageant_client_log(pc, reqid, "request: SSH1_AGENTC_RSA_CHALLENGE");

        response = NULL;
        memset(&reqkey, 0, sizeof(reqkey));

        get_rsa_ssh1_pub(msg, &reqkey, RSA_SSH1_EXPONENT_FIRST);
        challenge = get_mp_ssh1(msg);
        session_id = get_data(msg, 16);
        response_type = get_uint32(msg);

        if (get_err(msg)) {
            fail("unable to decode request");
            goto challenge1_cleanup;
        }
        if (response_type != 1) {
            fail("response type other than 1 not supported");
            goto challenge1_cleanup;
        }

        if (!pc->suppress_logging) {
            char *fingerprint;
            reqkey.comment = NULL;
            fingerprint = rsa_ssh1_fingerprint(&reqkey);
            pageant_client_log(pc, reqid, "requested key: %s", fingerprint);
            sfree(fingerprint);
        }

        if ((pk = findkey1(&reqkey)) == NULL) {
            fail("key not found");
            goto challenge1_cleanup;
        }
#ifdef MOD_PERSO
	char *fingerprint;
		fingerprint = rsa_ssh1_fingerprint(pk->rkey);
		if (! confirm_key_usage(fingerprint, pk->rkey->comment)) {
	      goto challenge1_cleanup;
	    }
#endif
        response = rsa_ssh1_decrypt(challenge, pk->rkey);

        {
            ssh_hash *h = ssh_hash_new(&ssh_md5);
            for (i = 0; i < 32; i++)
                put_byte(h, mp_get_byte(response, 31 - i));
            put_datapl(h, session_id);
            ssh_hash_final(h, response_md5);
        }

        put_byte(sb, SSH1_AGENT_RSA_RESPONSE);
        put_data(sb, response_md5, 16);

        pageant_client_log(pc, reqid, "reply: SSH1_AGENT_RSA_RESPONSE");

          challenge1_cleanup:
        if (response)
            mp_free(response);
        mp_free(challenge);
        freersakey(&reqkey);
        break;
      }
      case SSH2_AGENTC_SIGN_REQUEST: {
        /*
         * Reply with either SSH2_AGENT_SIGN_RESPONSE or
         * SSH_AGENT_FAILURE, depending on whether we have that key
         * or not.
         */
        PageantKey *pk;
        ptrlen keyblob, sigdata;
        uint32_t flags;
        pageant_client_log(pc, reqid, "request: SSH2_AGENTC_SIGN_REQUEST");

        keyblob = get_string(msg);
        sigdata = get_string(msg);

        if (get_err(msg)) {
            fail("unable to decode request");
            goto responded;
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

        if (!pc->suppress_logging) {
            char *fingerprint = ssh2_fingerprint_blob(
                keyblob, SSH_FPTYPE_DEFAULT);
            pageant_client_log(pc, reqid, "requested key: %s", fingerprint);
            sfree(fingerprint);
        }
        if ((pk = findkey2(keyblob)) == NULL) {
            fail("key not found");
            goto responded;
        }
#ifdef MOD_PERSO
void debug_log( const char *fmt, ... ) {
	char filename[4096]="" ;
	va_list ap;
	FILE *fp ;
	strcpy(filename,"kitty.log");

	va_start( ap, fmt ) ;
	//vfprintf( stdout, fmt, ap ) ; // Ecriture a l'ecran
	if( ( fp = fopen( filename, "ab" ) ) != NULL ) {
		vfprintf( fp, fmt, ap ) ; // ecriture dans un fichier
		fclose( fp ) ;
	}
 
	va_end( ap ) ;
}

	char* confirm_fingerprint;
	confirm_fingerprint = ssh2_fingerprint_blob(keyblob, SSH_FPTYPE_DEFAULT);
	if( confirm_fingerprint!=NULL ) {
		if( pk->comment != NULL ) {
			if (! confirm_key_usage( confirm_fingerprint, pk->comment)) {
				if( confirm_fingerprint!=NULL ) sfree(confirm_fingerprint);
				fail("unconfirmed key");
				pk=NULL;
				goto responded;
				//return NULL;
			}
		} /*else if( pk->rkey->comment != NULL ) {
			if (! confirm_key_usage( confirm_fingerprint, pk->rkey->comment)) {
				if( confirm_fingerprint!=NULL ) sfree(confirm_fingerprint);
				fail("unconfirmed key");
				pk=NULL;
				goto responded;
				//return NULL;
			}
		}*/
	}
	if( confirm_fingerprint!=NULL ) sfree(confirm_fingerprint);
#endif
	
        if (have_flags)
            pageant_client_log(pc, reqid, "signature flags = 0x%08"PRIx32,
                               flags);
        else
            pageant_client_log(pc, reqid, "no signature flags");

        strbuf_free(sb); /* no immediate response */

        PageantSignOp *so = snew(PageantSignOp);
        so->pao.vt = &signop_vtable;
        so->pao.info = pc->info;
        so->pao.cr.prev = pc->info->head.prev;
        so->pao.cr.next = &pc->info->head;
        so->pao.reqid = reqid;
        so->pk = pk;
        so->pkr.prev = so->pkr.next = NULL;
        so->data_to_sign = strbuf_new();
        put_datapl(so->data_to_sign, sigdata);
        so->flags = flags;
        so->failure_type = failure_type;
        so->crLine = 0;
        return &so->pao;
        break;
      }
      case SSH1_AGENTC_ADD_RSA_IDENTITY: {
        /*
         * Add to the list and return SSH_AGENT_SUCCESS, or
         * SSH_AGENT_FAILURE if the key was malformed.
         */
        RSAKey *key;

        pageant_client_log(pc, reqid, "request: SSH1_AGENTC_ADD_RSA_IDENTITY");

        key = get_rsa_ssh1_priv_agent(msg);
        key->comment = mkstr(get_string(msg));

        if (get_err(msg)) {
            fail("unable to decode request");
            goto add1_cleanup;
        }

        if (!rsa_verify(key)) {
            fail("key is invalid");
            goto add1_cleanup;
        }

        if (!pc->suppress_logging) {
            char *fingerprint = rsa_ssh1_fingerprint(key);
            pageant_client_log(pc, reqid,
                               "submitted key: %s", fingerprint);
            sfree(fingerprint);
        }

        if (pageant_add_ssh1_key(key)) {
            keylist_update();
            put_byte(sb, SSH_AGENT_SUCCESS);
            pageant_client_log(pc, reqid, "reply: SSH_AGENT_SUCCESS");
            key = NULL;            /* don't free it in cleanup */
        } else {
            fail("key already present");
        }

          add1_cleanup:
        if (key) {
            freersakey(key);
            sfree(key);
        }
        break;
      }
      case SSH2_AGENTC_ADD_IDENTITY: {
        /*
         * Add to the list and return SSH_AGENT_SUCCESS, or
         * SSH_AGENT_FAILURE if the key was malformed.
         */
        ssh2_userkey *key = NULL;
        ptrlen algpl;
        const ssh_keyalg *alg;

        pageant_client_log(pc, reqid, "request: SSH2_AGENTC_ADD_IDENTITY");

        algpl = get_string(msg);

        key = snew(ssh2_userkey);
        key->key = NULL;
        key->comment = NULL;
        alg = find_pubkey_alg_len(algpl);
        if (!alg) {
            fail("algorithm unknown");
            goto add2_cleanup;
        }

        key->key = ssh_key_new_priv_openssh(alg, msg);

        if (!key->key) {
            fail("key setup failed");
            goto add2_cleanup;
        }

        key->comment = mkstr(get_string(msg));

        if (get_err(msg)) {
            fail("unable to decode request");
            goto add2_cleanup;
        }

        if (!pc->suppress_logging) {
            char *fingerprint = ssh2_fingerprint(key->key, SSH_FPTYPE_DEFAULT);
            pageant_client_log(pc, reqid, "submitted key: %s %s",
                               fingerprint, key->comment);
            sfree(fingerprint);
        }

        if (pageant_add_ssh2_key(key)) {
            keylist_update();
            put_byte(sb, SSH_AGENT_SUCCESS);

            pageant_client_log(pc, reqid, "reply: SSH_AGENT_SUCCESS");

            key = NULL;            /* don't clean it up */
        } else {
            fail("key already present");
        }

          add2_cleanup:
        if (key) {
            if (key->key)
                ssh_key_free(key->key);
            if (key->comment)
                sfree(key->comment);
            sfree(key);
        }
        break;
      }
      case SSH1_AGENTC_REMOVE_RSA_IDENTITY: {
        /*
         * Remove from the list and return SSH_AGENT_SUCCESS, or
         * perhaps SSH_AGENT_FAILURE if it wasn't in the list to
         * start with.
         */
        RSAKey reqkey;
        PageantKey *pk;

        pageant_client_log(pc, reqid,
                           "request: SSH1_AGENTC_REMOVE_RSA_IDENTITY");

        memset(&reqkey, 0, sizeof(reqkey));
        get_rsa_ssh1_pub(msg, &reqkey, RSA_SSH1_EXPONENT_FIRST);

        if (get_err(msg)) {
            fail("unable to decode request");
            freersakey(&reqkey);
            goto responded;
        }

        if (!pc->suppress_logging) {
            char *fingerprint;
            reqkey.comment = NULL;
            fingerprint = rsa_ssh1_fingerprint(&reqkey);
            pageant_client_log(pc, reqid, "unwanted key: %s", fingerprint);
            sfree(fingerprint);
        }

        pk = findkey1(&reqkey);
        freersakey(&reqkey);
        if (pk) {
            pageant_client_log(pc, reqid, "found with comment: %s",
                               pk->rkey->comment);

            del234(keytree, pk);
            keylist_update();
            pk_free(pk);
            put_byte(sb, SSH_AGENT_SUCCESS);

            pageant_client_log(pc, reqid, "reply: SSH_AGENT_SUCCESS");
        } else {
            fail("key not found");
        }
        break;
      }
      case SSH2_AGENTC_REMOVE_IDENTITY: {
        /*
         * Remove from the list and return SSH_AGENT_SUCCESS, or
         * perhaps SSH_AGENT_FAILURE if it wasn't in the list to
         * start with.
         */
        PageantKey *pk;
        ptrlen blob;

        pageant_client_log(pc, reqid, "request: SSH2_AGENTC_REMOVE_IDENTITY");

        blob = get_string(msg);

        if (get_err(msg)) {
            fail("unable to decode request");
            goto responded;
        }

        if (!pc->suppress_logging) {
            char *fingerprint = ssh2_fingerprint_blob(
                blob, SSH_FPTYPE_DEFAULT);
            pageant_client_log(pc, reqid, "unwanted key: %s", fingerprint);
            sfree(fingerprint);
        }

        pk = findkey2(blob);
        if (!pk) {
            fail("key not found");
            goto responded;
        }

        pageant_client_log(pc, reqid, "found with comment: %s", pk->comment);

        del234(keytree, pk);
        keylist_update();
        pk_free(pk);
        put_byte(sb, SSH_AGENT_SUCCESS);

        pageant_client_log(pc, reqid, "reply: SSH_AGENT_SUCCESS");
        break;
      }
      case SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES: {
        /*
         * Remove all SSH-1 keys. Always returns success.
         */
        pageant_client_log(pc, reqid,
                           "request: SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES");

        remove_all_keys(1);
        keylist_update();

        put_byte(sb, SSH_AGENT_SUCCESS);

        pageant_client_log(pc, reqid, "reply: SSH_AGENT_SUCCESS");
        break;
      }
      case SSH2_AGENTC_REMOVE_ALL_IDENTITIES: {
        /*
         * Remove all SSH-2 keys. Always returns success.
         */
        pageant_client_log(pc, reqid,
                           "request: SSH2_AGENTC_REMOVE_ALL_IDENTITIES");

        remove_all_keys(2);
        keylist_update();

        put_byte(sb, SSH_AGENT_SUCCESS);

        pageant_client_log(pc, reqid, "reply: SSH_AGENT_SUCCESS");
        break;
      }
      case SSH2_AGENTC_EXTENSION: {
        enum Extension exttype = EXT_UNKNOWN;
        ptrlen extname = get_string(msg);
        pageant_client_log(pc, reqid,
                           "request: SSH2_AGENTC_EXTENSION \"%.*s\"",
                           PTRLEN_PRINTF(extname));

        for (size_t i = 0; i < lenof(extension_names); i++)
            if (ptrlen_eq_ptrlen(extname, extension_names[i])) {
                exttype = i;

                /*
                 * For SSH_AGENTC_EXTENSION requests, the message
                 * code SSH_AGENT_FAILURE is reserved for "I don't
                 * recognise this extension name at all". For any
                 * other kind of failure while processing an
                 * extension we _do_ recognise, we must switch to
                 * returning a different failure code, with
                 * semantics "I understood the extension name, but
                 * something else went wrong".
                 */
                failure_type = SSH_AGENT_EXTENSION_FAILURE;
                break;
            }

        switch (exttype) {
          case EXT_UNKNOWN:
            fail("unrecognised extension name '%.*s'",
                 PTRLEN_PRINTF(extname));
            break;

          case EXT_QUERY:
            /* Standard request to list the supported extensions. */
            put_byte(sb, SSH_AGENT_SUCCESS);
            for (size_t i = 0; i < lenof(extension_names); i++)
                put_stringpl(sb, extension_names[i]);
            pageant_client_log(pc, reqid, "reply: SSH_AGENT_SUCCESS + names");
            break;

          case EXT_ADD_PPK: {
            ptrlen keyfile = get_string(msg);

            if (get_err(msg)) {
                fail("unable to decode request");
                goto responded;
            }

            BinarySource src[1];
            const char *error;

            strbuf *public_blob = strbuf_new();
            char *comment;

            BinarySource_BARE_INIT_PL(src, keyfile);
            if (!ppk_loadpub_s(src, NULL, BinarySink_UPCAST(public_blob),
                               &comment, &error)) {
                fail("failed to extract public key blob: %s", error);
                goto add_ppk_cleanup;
            }

            if (!pc->suppress_logging) {
                char *fingerprint = ssh2_fingerprint_blob(
                    ptrlen_from_strbuf(public_blob), SSH_FPTYPE_DEFAULT);
                pageant_client_log(pc, reqid, "add-ppk: %s %s",
                                   fingerprint, comment);
                sfree(fingerprint);
            }

            BinarySource_BARE_INIT_PL(src, keyfile);
            bool encrypted = ppk_encrypted_s(src, NULL);

            if (!encrypted) {
                /* If the key isn't encrypted, then we should just
                 * load and add it in the obvious way. */
                BinarySource_BARE_INIT_PL(src, keyfile);
                ssh2_userkey *skey = ppk_load_s(src, NULL, &error);
                if (!skey) {
                    fail("failed to decode private key: %s", error);
                } else if (pageant_add_ssh2_key(skey)) {
                    keylist_update();
                    put_byte(sb, SSH_AGENT_SUCCESS);

                    pageant_client_log(pc, reqid, "reply: SSH_AGENT_SUCCESS"
                                       " (loaded unencrypted PPK)");
                } else {
                    fail("key already present");
                    if (skey->key)
                        ssh_key_free(skey->key);
                    if (skey->comment)
                        sfree(skey->comment);
                    sfree(skey);
                }
                goto add_ppk_cleanup;
            }

            PageantKeySort sort =
                keysort(2, ptrlen_from_strbuf(public_blob));

            PageantKey *pk = find234(keytree, &sort, NULL);
            if (pk) {
                /*
                 * This public key blob already exists in the
                 * keytree. Add the encrypted key file to the
                 * existing record, if it doesn't have one already.
                 */
                if (!pk->encrypted_key_file) {
                    pk->encrypted_key_file = strbuf_new_nm();
                    put_datapl(pk->encrypted_key_file, keyfile);

                    keylist_update();
                    put_byte(sb, SSH_AGENT_SUCCESS);
                    pageant_client_log(
                        pc, reqid, "reply: SSH_AGENT_SUCCESS (added encrypted"
                        " PPK to existing key record)");
                } else {
                    fail("key already present");
                }
            } else {
                /*
                 * We're adding a new key record containing only
                 * an encrypted key file.
                 */
                PageantKey *pk = snew(PageantKey);
                memset(pk, 0, sizeof(PageantKey));
                pk->blocked_requests.next = pk->blocked_requests.prev =
                    &pk->blocked_requests;
                pk->sort.ssh_version = 2;
                pk->public_blob = public_blob;
                public_blob = NULL;
                pk->sort.public_blob = ptrlen_from_strbuf(pk->public_blob);
                pk->comment = dupstr(comment);
                pk->encrypted_key_file = strbuf_new_nm();
                put_datapl(pk->encrypted_key_file, keyfile);

                PageantKey *added = add234(keytree, pk);
                assert(added == pk); (void)added;

                keylist_update();
                put_byte(sb, SSH_AGENT_SUCCESS);
                pageant_client_log(pc, reqid, "reply: SSH_AGENT_SUCCESS (made"
                                   " new encrypted-only key record)");
            }

              add_ppk_cleanup:
            if (public_blob)
                strbuf_free(public_blob);
            sfree(comment);
            break;
          }

          case EXT_REENCRYPT: {
            /*
             * Re-encrypt a single key, in the sense of deleting
             * its unencrypted copy, returning it to the state of
             * only having the encrypted PPK form stored, so that
             * the next attempt to use it will have to re-prompt
             * for the passphrase.
             */
            ptrlen blob = get_string(msg);

            if (get_err(msg)) {
                fail("unable to decode request");
                goto responded;
            }

            if (!pc->suppress_logging) {
                char *fingerprint = ssh2_fingerprint_blob(
                    blob, SSH_FPTYPE_DEFAULT);
                pageant_client_log(pc, reqid, "key to re-encrypt: %s",
                                   fingerprint);
                sfree(fingerprint);
            }

            PageantKey *pk = findkey2(blob);
            if (!pk) {
                fail("key not found");
                goto responded;
            }

            pageant_client_log(pc, reqid,
                               "found with comment: %s", pk->comment);

            if (!reencrypt_key(pk)) {
                fail("this key couldn't be re-encrypted");
                goto responded;
            }

            keylist_update();
            put_byte(sb, SSH_AGENT_SUCCESS);
            pageant_client_log(pc, reqid, "reply: SSH_AGENT_SUCCESS");
            break;
          }

          case EXT_REENCRYPT_ALL: {
            /*
             * Re-encrypt all keys that have an encrypted form
             * stored. Usually, returns success, but with a uint32
             * appended indicating how many keys remain
             * unencrypted. The exception is if there is at least
             * one key in the agent and _no_ key was successfully
             * re-encrypted; in that situation we've done nothing,
             * and the client didn't _want_ us to do nothing, so
             * we return failure.
             *
             * (Rationale: the 'failure' message ought to be
             * atomic, that is, you shouldn't return failure
             * having made a state change.)
             */
            unsigned nfailures = 0, nsuccesses = 0;
            PageantKey *pk;

            for (int i = 0; (pk = index234(keytree, i)) != NULL; i++) {
                if (reencrypt_key(pk))
                    nsuccesses++;
                else
                    nfailures++;
            }

            if (nsuccesses == 0 && nfailures > 0) {
                fail("no key could be re-encrypted");
            } else {
                keylist_update();
                put_byte(sb, SSH_AGENT_SUCCESS);
                put_uint32(sb, nfailures);
                pageant_client_log(pc, reqid, "reply: SSH_AGENT_SUCCESS "
                                   "(%u keys re-encrypted, %u failures)",
                                   nsuccesses, nfailures);
            }
            break;
          }

          case EXT_LIST_EXTENDED: {
            /*
             * Return a key list like SSH2_AGENTC_REQUEST_IDENTITIES,
             * except that each key is annotated with extra
             * information such as whether it's currently encrypted.
             *
             * The return message type is AGENT_SUCCESS with auxiliary
             * data, which is more like other extension messages. I
             * think it would be confusing to reuse IDENTITIES_ANSWER
             * for a reply message with an incompatible format.
             */
            put_byte(sb, SSH_AGENT_SUCCESS);
            pageant_make_keylist_extended(BinarySink_UPCAST(sb));

            pageant_client_log(pc, reqid,
                               "reply: SSH2_AGENT_SUCCESS + key list");
            if (!pc->suppress_logging) {
                int i;
                PageantKey *pk;
                for (i = 0; NULL != (pk = pageant_nth_key(2, i)); i++) {
                    char *fingerprint = ssh2_fingerprint_blob(
                        ptrlen_from_strbuf(pk->public_blob),
                        SSH_FPTYPE_DEFAULT);
                    pageant_client_log(pc, reqid, "returned key: %s %s",
                                       fingerprint, pk->comment);
                    sfree(fingerprint);
                }
            }
            break;
          }
        }
        break;
      }
      default:
        pageant_client_log(pc, reqid, "request: unknown message type %d",
                           type);
        fail("unrecognised message");
        break;
    }

#undef fail

  responded:;

    PageantImmOp *io = snew(PageantImmOp);
    io->pao.vt = &immop_vtable;
    io->pao.info = pc->info;
    io->pao.cr.prev = pc->info->head.prev;
    io->pao.cr.next = &pc->info->head;
    io->pao.reqid = reqid;
    io->response = sb;
    io->crLine = 0;
    return &io->pao;
}

void pageant_handle_msg(PageantClient *pc, PageantClientRequestId *reqid,
                        ptrlen msgpl)
{
    PageantAsyncOp *pao = pageant_make_op(pc, reqid, msgpl);
    queue_toplevel_callback(pageant_async_op_callback, pao);
}

void pageant_init(void)
{
    pageant_local = true;
    keytree = newtree234(cmpkeys);
}

static PageantKey *pageant_nth_key(int ssh_version, int i)
{
#ifdef MOD_PERSO
    PageantKey *pk = index234(
        keytree, find_first_key_for_version(ssh_version) + get_scrumble_int(i));
#else
    PageantKey *pk = index234(
        keytree, find_first_key_for_version(ssh_version) + i);
#endif
    if (pk && pk->sort.ssh_version == ssh_version)
        return pk;
    else
        return NULL;
}

bool pageant_delete_nth_ssh1_key(int i)
{
    PageantKey *pk = delpos234(keytree, find_first_key_for_version(1) + i);
    if (!pk)
        return false;
    pk_free(pk);
    return true;
}

bool pageant_delete_nth_ssh2_key(int i)
{
    PageantKey *pk = delpos234(keytree, find_first_key_for_version(2) + i);
    if (!pk)
        return false;
    pk_free(pk);
    return true;
}

bool pageant_reencrypt_nth_ssh2_key(int i)
{
    PageantKey *pk = index234(keytree, find_first_key_for_version(2) + i);
    if (!pk)
        return false;
    return reencrypt_key(pk);
}

void pageant_delete_all(void)
{
    remove_all_keys(1);
    remove_all_keys(2);
}

void pageant_reencrypt_all(void)
{
    PageantKey *pk;
    for (int i = 0; (pk = index234(keytree, i)) != NULL; i++)
        reencrypt_key(pk);
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

struct pageant_conn_queued_response {
    struct pageant_conn_queued_response *next, *prev;
    size_t req_index;      /* for indexing requests in log messages */
    strbuf *sb;
    PageantClientRequestId reqid;
};

struct pageant_conn_state {
    Socket *connsock;
    PageantListenerClient *plc;
    unsigned char lenbuf[4], pktbuf[AGENT_MAX_MSGLEN];
    unsigned len, got;
    bool real_packet;
    size_t conn_index;     /* for indexing connections in log messages */
    size_t req_index;      /* for indexing requests in log messages */
    int crLine;            /* for coroutine in pageant_conn_receive */

    struct pageant_conn_queued_response response_queue;

    PageantClient pc;
    Plug plug;
};

static void pageant_conn_closing(Plug *plug, const char *error_msg,
                                 int error_code, bool calling_back)
{
    struct pageant_conn_state *pc = container_of(
        plug, struct pageant_conn_state, plug);
    if (error_msg)
        pageant_listener_client_log(pc->plc, "c#%"SIZEu": error: %s",
                                    pc->conn_index, error_msg);
    else
        pageant_listener_client_log(pc->plc, "c#%"SIZEu": connection closed",
                                    pc->conn_index);
    sk_close(pc->connsock);
    pageant_unregister_client(&pc->pc);
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

static void pageant_conn_log(PageantClient *pc, PageantClientRequestId *reqid,
                             const char *fmt, va_list ap)
{
    struct pageant_conn_state *pcs =
        container_of(pc, struct pageant_conn_state, pc);
    struct pageant_conn_queued_response *qr =
        container_of(reqid, struct pageant_conn_queued_response, reqid);

    char *formatted = dupvprintf(fmt, ap);
    pageant_listener_client_log(pcs->plc, "c#%"SIZEu",r#%"SIZEu": %s",
                                pcs->conn_index, qr->req_index, formatted);
    sfree(formatted);
}

static void pageant_conn_got_response(
    PageantClient *pc, PageantClientRequestId *reqid, ptrlen response)
{
    struct pageant_conn_state *pcs =
        container_of(pc, struct pageant_conn_state, pc);
    struct pageant_conn_queued_response *qr =
        container_of(reqid, struct pageant_conn_queued_response, reqid);

    qr->sb = strbuf_new_nm();
    put_stringpl(qr->sb, response);

    while (pcs->response_queue.next != &pcs->response_queue &&
           pcs->response_queue.next->sb) {
        qr = pcs->response_queue.next;
        sk_write(pcs->connsock, qr->sb->u, qr->sb->len);
        qr->next->prev = qr->prev;
        qr->prev->next = qr->next;
        strbuf_free(qr->sb);
        sfree(qr);
    }
}

static bool pageant_conn_ask_passphrase(
    PageantClient *pc, PageantClientDialogId *dlgid, const char *comment)
{
    struct pageant_conn_state *pcs =
        container_of(pc, struct pageant_conn_state, pc);
    return pageant_listener_client_ask_passphrase(pcs->plc, dlgid, comment);
}

static const PageantClientVtable pageant_connection_clientvt = {
    .log = pageant_conn_log,
    .got_response = pageant_conn_got_response,
    .ask_passphrase = pageant_conn_ask_passphrase,
};

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

        {
            struct pageant_conn_queued_response *qr =
                snew(struct pageant_conn_queued_response);
            qr->prev = pc->response_queue.prev;
            qr->next = &pc->response_queue;
            qr->prev->next = qr->next->prev = qr;
            qr->sb = NULL;
            qr->req_index = pc->req_index++;
        }

        if (!pc->real_packet) {
            /*
             * Send failure immediately, before consuming the packet
             * data. That way we notify the client reasonably early
             * even if the data channel has just started spewing
             * nonsense.
             */
            pageant_client_log(&pc->pc, &pc->response_queue.prev->reqid,
                               "early reply: SSH_AGENT_FAILURE "
                               "(overlong message, length %u)", pc->len);
            static const unsigned char failure[] = { SSH_AGENT_FAILURE };
            pageant_conn_got_response(&pc->pc, &pc->response_queue.prev->reqid,
                                      make_ptrlen(failure, lenof(failure)));
        }

        while (pc->got < pc->len) {
            crGetChar(c);
            if (pc->real_packet)
                pc->pktbuf[pc->got] = c;
            pc->got++;
        }

        if (pc->real_packet)
            pageant_handle_msg(&pc->pc, &pc->response_queue.prev->reqid,
                               make_ptrlen(pc->pktbuf, pc->len));
    }

    crFinishV;
}

struct pageant_listen_state {
    Socket *listensock;
    PageantListenerClient *plc;
    size_t conn_index;     /* for indexing connections in log messages */

    Plug plug;
};

static void pageant_listen_closing(Plug *plug, const char *error_msg,
                                   int error_code, bool calling_back)
{
    struct pageant_listen_state *pl = container_of(
        plug, struct pageant_listen_state, plug);
    if (error_msg)
        pageant_listener_client_log(pl->plc, "listening socket: error: %s",
                                    error_msg);
    sk_close(pl->listensock);
    pl->listensock = NULL;
}

static const PlugVtable pageant_connection_plugvt = {
    .closing = pageant_conn_closing,
    .receive = pageant_conn_receive,
    .sent = pageant_conn_sent,
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
    pc->pc.vt = &pageant_connection_clientvt;
    pc->plc = pl->plc;
    pc->response_queue.next = pc->response_queue.prev = &pc->response_queue;
    pc->conn_index = pl->conn_index++;
    pc->req_index = 0;
    pc->crLine = 0;

    pc->connsock = constructor(ctx, &pc->plug);
    if ((err = sk_socket_error(pc->connsock)) != NULL) {
        sk_close(pc->connsock);
        sfree(pc);
        return 1;
    }

    sk_set_frozen(pc->connsock, false);

    peerinfo = sk_peer_info(pc->connsock);
    if (peerinfo && peerinfo->log_text) {
        pageant_listener_client_log(pl->plc,
                                    "c#%"SIZEu": new connection from %s",
                                    pc->conn_index, peerinfo->log_text);
    } else {
        pageant_listener_client_log(pl->plc, "c#%"SIZEu": new connection",
                                    pc->conn_index);
    }
    sk_free_peer_info(peerinfo);

    pageant_register_client(&pc->pc);

    return 0;
}

static const PlugVtable pageant_listener_plugvt = {
    .closing = pageant_listen_closing,
    .accepting = pageant_listen_accepting,
};

struct pageant_listen_state *pageant_listener_new(
    Plug **plug, PageantListenerClient *plc)
{
    struct pageant_listen_state *pl = snew(struct pageant_listen_state);
    pl->plug.vt = &pageant_listener_plugvt;
    pl->plc = plc;
    pl->listensock = NULL;
    pl->conn_index = 0;
    *plug = &pl->plug;
    return pl;
}

void pageant_listener_got_socket(struct pageant_listen_state *pl, Socket *sock)
{
    pl->listensock = sock;
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

typedef struct PageantInternalClient {
    strbuf *response;
    bool got_response;
    PageantClient pc;
} PageantInternalClient;

static void internal_client_got_response(
    PageantClient *pc, PageantClientRequestId *reqid, ptrlen response)
{
    PageantInternalClient *pic = container_of(pc, PageantInternalClient, pc);
    strbuf_clear(pic->response);
    put_datapl(pic->response, response);
    pic->got_response = true;
}

static bool internal_client_ask_passphrase(
    PageantClient *pc, PageantClientDialogId *dlgid, const char *comment)
{
    /* No delaying operations are permitted in this mode */
    return false;
}

static const PageantClientVtable internal_clientvt = {
    .log = NULL,
    .got_response = internal_client_got_response,
    .ask_passphrase = internal_client_ask_passphrase,
};

typedef struct PageantClientOp {
    strbuf *buf;
    bool request_made;
    BinarySink_DELEGATE_IMPLEMENTATION;
    BinarySource_IMPLEMENTATION;
} PageantClientOp;

static PageantClientOp *pageant_client_op_new(void)
{
    PageantClientOp *pco = snew(PageantClientOp);
    pco->buf = strbuf_new_for_agent_query();
    pco->request_made = false;
    BinarySink_DELEGATE_INIT(pco, pco->buf);
    BinarySource_INIT(pco, "", 0);
    return pco;
}

static void pageant_client_op_free(PageantClientOp *pco)
{
    if (pco->buf)
        strbuf_free(pco->buf);
    sfree(pco);
}

static unsigned pageant_client_op_query(PageantClientOp *pco)
{
    /* Since we use the same strbuf for the request and the response,
     * check by assertion that we aren't embarrassingly sending a
     * previous response back to the agent */
    assert(!pco->request_made);
    pco->request_made = true;

    if (!pageant_local) {
        void *response_raw;
        int resplen_raw;
        agent_query_synchronous(pco->buf, &response_raw, &resplen_raw);
        strbuf_clear(pco->buf);
        put_data(pco->buf, response_raw, resplen_raw);
        sfree(response_raw);

        /* The data coming back from agent_query_synchronous will have
         * its length field prepended. So we start by parsing it as an
         * SSH-formatted string, and then reinitialise our
         * BinarySource with the interior of that string. */
        BinarySource_INIT_PL(pco, ptrlen_from_strbuf(pco->buf));
        BinarySource_INIT_PL(pco, get_string(pco));
    } else {
        PageantInternalClient pic;
        PageantClientRequestId reqid;

        pic.pc.vt = &internal_clientvt;
        pic.pc.suppress_logging = true;
        pic.response = pco->buf;
        pic.got_response = false;
        pageant_register_client(&pic.pc);

        assert(pco->buf->len > 4);
        PageantAsyncOp *pao = pageant_make_op(
            &pic.pc, &reqid, make_ptrlen(pco->buf->s + 4, pco->buf->len - 4));
        while (!pic.got_response)
            pageant_async_op_coroutine(pao);

        pageant_unregister_client(&pic.pc);

        BinarySource_INIT_PL(pco, ptrlen_from_strbuf(pco->buf));
    }

    /* Strip off and directly return the type byte, which every client
     * will need, to save a boilerplate get_byte at each call site */
    unsigned reply_type = get_byte(pco);
    if (get_err(pco))
        reply_type = 256;              /* out-of-range code */
    return reply_type;
}

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

typedef struct KeyListEntry {
    ptrlen blob, comment;
    uint32_t flags;
} KeyListEntry;
typedef struct KeyList {
    strbuf *raw_data;
    KeyListEntry *keys;
    size_t nkeys;
    bool broken;
} KeyList;

static void keylist_free(KeyList *kl)
{
    sfree(kl->keys);
    strbuf_free(kl->raw_data);
    sfree(kl);
}

static PageantClientOp *pageant_request_keylist_1(void)
{
    PageantClientOp *pco = pageant_client_op_new();
    put_byte(pco, SSH1_AGENTC_REQUEST_RSA_IDENTITIES);
    if (pageant_client_op_query(pco) == SSH1_AGENT_RSA_IDENTITIES_ANSWER)
        return pco;
    pageant_client_op_free(pco);
    return NULL;
}

static PageantClientOp *pageant_request_keylist_2(void)
{
    PageantClientOp *pco = pageant_client_op_new();
    put_byte(pco, SSH2_AGENTC_REQUEST_IDENTITIES);
    if (pageant_client_op_query(pco) == SSH2_AGENT_IDENTITIES_ANSWER)
        return pco;
    pageant_client_op_free(pco);
    return NULL;
}

static PageantClientOp *pageant_request_keylist_extended(void)
{
    PageantClientOp *pco = pageant_client_op_new();
    put_byte(pco, SSH2_AGENTC_EXTENSION);
    put_stringpl(pco, extension_names[EXT_LIST_EXTENDED]);
    if (pageant_client_op_query(pco) == SSH_AGENT_SUCCESS)
        return pco;
    pageant_client_op_free(pco);
    return NULL;
}

static KeyList *pageant_get_keylist(unsigned ssh_version)
{
    PageantClientOp *pco;
    bool list_is_extended = false;

    if (ssh_version == 1) {
        pco = pageant_request_keylist_1();
    } else {
        if ((pco = pageant_request_keylist_extended()) != NULL)
            list_is_extended = true;
        else
            pco = pageant_request_keylist_2();
    }

    if (!pco)
        return NULL;

    KeyList *kl = snew(KeyList);
    kl->nkeys = get_uint32(pco);
    kl->keys = snewn(kl->nkeys, struct KeyListEntry);
    kl->broken = false;

    for (size_t i = 0; i < kl->nkeys && !get_err(pco); i++) {
        if (ssh_version == 1) {
            int bloblen = rsa_ssh1_public_blob_len(
                make_ptrlen(get_ptr(pco), get_avail(pco)));
            if (bloblen < 0) {
                kl->broken = true;
                bloblen = 0;
            }
            kl->keys[i].blob = get_data(pco, bloblen);
        } else {
            kl->keys[i].blob = get_string(pco);
        }
        kl->keys[i].comment = get_string(pco);

        if (list_is_extended) {
            ptrlen key_ext_info = get_string(pco);
            BinarySource src[1];
            BinarySource_BARE_INIT_PL(src, key_ext_info);

            kl->keys[i].flags = get_uint32(src);
        } else {
            kl->keys[i].flags = 0;
        }
    }

    if (get_err(pco))
        kl->broken = true;
    kl->raw_data = pco->buf;
    pco->buf = NULL;
    pageant_client_op_free(pco);
    return kl;
}

int pageant_add_keyfile(Filename *filename, const char *passphrase,
                        char **retstr, bool add_encrypted)
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

    if (add_encrypted && type == SSH_KEYTYPE_SSH1) {
        *retstr = dupprintf("Can't add SSH-1 keys in encrypted form");
        return PAGEANT_ACTION_FAILURE;
    }

    /*
     * See if the key is already loaded (in the primary Pageant,
     * which may or may not be us).
     */
    {
        strbuf *blob = strbuf_new();
        KeyList *kl;

        if (type == SSH_KEYTYPE_SSH1) {
            if (!rsa1_loadpub_f(filename, BinarySink_UPCAST(blob),
                                NULL, &error)) {
                *retstr = dupprintf("Couldn't load private key (%s)", error);
                strbuf_free(blob);
                return PAGEANT_ACTION_FAILURE;
            }
            kl = pageant_get_keylist(1);
        } else {
            if (!ppk_loadpub_f(filename, NULL, BinarySink_UPCAST(blob),
                               NULL, &error)) {
                *retstr = dupprintf("Couldn't load private key (%s)", error);
                strbuf_free(blob);
                return PAGEANT_ACTION_FAILURE;
            }
            kl = pageant_get_keylist(2);
        }

        if (kl) {
            if (kl->broken) {
                *retstr = dupstr("Received broken key list from agent");
                keylist_free(kl);
                strbuf_free(blob);
                return PAGEANT_ACTION_FAILURE;
            }

            for (size_t i = 0; i < kl->nkeys; i++) {
                /*
                 * If the key already exists in the agent, we're done,
                 * except in the following special cases:
                 *
                 * It's encrypted in the agent, and we're being asked
                 * to add it unencrypted, in which case we still want
                 * to upload the unencrypted version to cause the key
                 * to become decrypted.
                 * (Rationale: if you know in advance you're going to
                 * want it, and don't want to be interrupted at an
                 * unpredictable moment to be asked for the
                 * passphrase.)
                 *
                 * The agent only has cleartext, and we're being asked
                 * to add it encrypted, in which case we'll add the
                 * encrypted form.
                 * (Rationale: if you might want to re-encrypt the key
                 * at some future point, but it happened to have been
                 * initially added in cleartext, perhaps by something
                 * other than Pageant.)
                 */
                if (ptrlen_eq_ptrlen(ptrlen_from_strbuf(blob),
                                     kl->keys[i].blob)) {
                    bool have_unencrypted =
                        !(kl->keys[i].flags &
                          LIST_EXTENDED_FLAG_HAS_NO_CLEARTEXT_KEY);
                    bool have_encrypted =
                        (kl->keys[i].flags &
                         LIST_EXTENDED_FLAG_HAS_ENCRYPTED_KEY_FILE);
                    if ((have_unencrypted && !add_encrypted)
                        || (have_encrypted && add_encrypted)) {
                        /* Key is already present in the desired form;
                         * we can now leave. */
                        keylist_free(kl);
                        strbuf_free(blob);
                        return PAGEANT_ACTION_OK;
                    }
                }
            }

            keylist_free(kl);
        }

        strbuf_free(blob);
    }

    if (add_encrypted) {
        const char *load_error;
        LoadedFile *lf = lf_load_keyfile(filename, &load_error);
        if (!lf) {
            *retstr = dupstr(load_error);
            return PAGEANT_ACTION_FAILURE;
        }

        PageantClientOp *pco = pageant_client_op_new();
        put_byte(pco, SSH2_AGENTC_EXTENSION);
        put_stringpl(pco, extension_names[EXT_ADD_PPK]);
        put_string(pco, lf->data, lf->len);

        lf_free(lf);

        unsigned reply = pageant_client_op_query(pco);
        pageant_client_op_free(pco);

        if (reply != SSH_AGENT_SUCCESS) {
            if (reply == SSH_AGENT_FAILURE) {
                /* The agent didn't understand the protocol extension
                 * at all. */
                *retstr = dupstr("Agent doesn't support adding "
                                 "encrypted keys");
            } else {
                *retstr = dupstr("The already running agent "
                                 "refused to add the key.");
            }
            return PAGEANT_ACTION_FAILURE;
        }

        return PAGEANT_ACTION_OK;
    }

    error = NULL;
    if (type == SSH_KEYTYPE_SSH1)
        needs_pass = rsa1_encrypted_f(filename, &comment);
    else
        needs_pass = ppk_encrypted_f(filename, &comment);
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
            ret = rsa1_load_f(filename, rkey, this_passphrase, &error);
        else {
            skey = ppk_load_f(filename, this_passphrase, &error);
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
        PageantClientOp *pco = pageant_client_op_new();
        put_byte(pco, SSH1_AGENTC_ADD_RSA_IDENTITY);
        rsa_ssh1_private_blob_agent(BinarySink_UPCAST(pco), rkey);
        put_stringz(pco, rkey->comment);
        unsigned reply = pageant_client_op_query(pco);
        pageant_client_op_free(pco);

        freersakey(rkey);
        sfree(rkey);

        if (reply != SSH_AGENT_SUCCESS) {
            *retstr = dupstr("The already running agent "
                             "refused to add the key.");
            return PAGEANT_ACTION_FAILURE;
        }
    } else {
        PageantClientOp *pco = pageant_client_op_new();
        put_byte(pco, SSH2_AGENTC_ADD_IDENTITY);
        put_stringz(pco, ssh_key_ssh_id(skey->key));
        ssh_key_openssh_blob(skey->key, BinarySink_UPCAST(pco));
        put_stringz(pco, skey->comment);
        unsigned reply = pageant_client_op_query(pco);
        pageant_client_op_free(pco);

        sfree(skey->comment);
        ssh_key_free(skey->key);
        sfree(skey);

        if (reply != SSH_AGENT_SUCCESS) {
            *retstr = dupstr("The already running agent "
                             "refused to add the key.");
            return PAGEANT_ACTION_FAILURE;
        }
    }
    return PAGEANT_ACTION_OK;
}

int pageant_enum_keys(pageant_key_enum_fn_t callback, void *callback_ctx,
                      char **retstr)
{
    KeyList *kl1 = NULL, *kl2 = NULL;
    struct pageant_pubkey cbkey;
    int toret = PAGEANT_ACTION_FAILURE;

    kl1 = pageant_get_keylist(1);
    if (kl1 && kl1->broken) {
        *retstr = dupstr("Received broken SSH-1 key list from agent");
        goto out;
    }

    kl2 = pageant_get_keylist(2);
    if (kl2 && kl2->broken) {
        *retstr = dupstr("Received broken SSH-2 key list from agent");
        goto out;
    }

    if (kl1) {
        for (size_t i = 0; i < kl1->nkeys; i++) {
            cbkey.blob = strbuf_new();
            put_datapl(cbkey.blob, kl1->keys[i].blob);
            cbkey.comment = mkstr(kl1->keys[i].comment);
            cbkey.ssh_version = 1;

            /* Decode public blob into a key in order to fingerprint it */
            RSAKey rkey;
            memset(&rkey, 0, sizeof(rkey));
            {
                BinarySource src[1];
                BinarySource_BARE_INIT_PL(src, kl1->keys[i].blob);
                get_rsa_ssh1_pub(src, &rkey, RSA_SSH1_EXPONENT_FIRST);
                if (get_err(src)) {
                    *retstr = dupstr(
                        "Received an invalid SSH-1 key from agent");
                    goto out;
                }
            }
            char **fingerprints = rsa_ssh1_fake_all_fingerprints(&rkey);
            freersakey(&rkey);

            callback(callback_ctx, fingerprints, cbkey.comment,
                     kl1->keys[i].flags, &cbkey);

            strbuf_free(cbkey.blob);
            sfree(cbkey.comment);
            ssh2_free_all_fingerprints(fingerprints);
        }
    }

    if (kl2) {
        for (size_t i = 0; i < kl2->nkeys; i++) {
            cbkey.blob = strbuf_new();
            put_datapl(cbkey.blob, kl2->keys[i].blob);
            cbkey.comment = mkstr(kl2->keys[i].comment);
            cbkey.ssh_version = 2;

            char **fingerprints =
                ssh2_all_fingerprints_for_blob(kl2->keys[i].blob);

            callback(callback_ctx, fingerprints, cbkey.comment,
                     kl2->keys[i].flags, &cbkey);

            ssh2_free_all_fingerprints(fingerprints);
            sfree(cbkey.comment);
            strbuf_free(cbkey.blob);
        }
    }

    *retstr = NULL;
    toret = PAGEANT_ACTION_OK;
  out:
    if (kl1)
        keylist_free(kl1);
    if (kl2)
        keylist_free(kl2);
    return toret;
}

int pageant_delete_key(struct pageant_pubkey *key, char **retstr)
{
    PageantClientOp *pco = pageant_client_op_new();

    if (key->ssh_version == 1) {
        put_byte(pco, SSH1_AGENTC_REMOVE_RSA_IDENTITY);
        put_data(pco, key->blob->s, key->blob->len);
    } else {
        put_byte(pco, SSH2_AGENTC_REMOVE_IDENTITY);
        put_string(pco, key->blob->s, key->blob->len);
    }

    unsigned reply = pageant_client_op_query(pco);
    pageant_client_op_free(pco);

    if (reply != SSH_AGENT_SUCCESS) {
        *retstr = dupstr("Agent failed to delete key");
        return PAGEANT_ACTION_FAILURE;
    } else {
        *retstr = NULL;
        return PAGEANT_ACTION_OK;
    }
}

int pageant_delete_all_keys(char **retstr)
{
    PageantClientOp *pco;
    unsigned reply;

    pco = pageant_client_op_new();
    put_byte(pco, SSH2_AGENTC_REMOVE_ALL_IDENTITIES);
    reply = pageant_client_op_query(pco);
    pageant_client_op_free(pco);
    if (reply != SSH_AGENT_SUCCESS) {
        *retstr = dupstr("Agent failed to delete SSH-2 keys");
        return PAGEANT_ACTION_FAILURE;
    }

    pco = pageant_client_op_new();
    put_byte(pco, SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES);
    reply = pageant_client_op_query(pco);
    pageant_client_op_free(pco);
    if (reply != SSH_AGENT_SUCCESS) {
        *retstr = dupstr("Agent failed to delete SSH-1 keys");
        return PAGEANT_ACTION_FAILURE;
    }

    *retstr = NULL;
    return PAGEANT_ACTION_OK;
}

int pageant_reencrypt_key(struct pageant_pubkey *key, char **retstr)
{
    PageantClientOp *pco = pageant_client_op_new();

    if (key->ssh_version == 1) {
        *retstr = dupstr("Can't re-encrypt an SSH-1 key");
        pageant_client_op_free(pco);
        return PAGEANT_ACTION_FAILURE;
    } else {
        put_byte(pco, SSH2_AGENTC_EXTENSION);
        put_stringpl(pco, extension_names[EXT_REENCRYPT]);
        put_string(pco, key->blob->s, key->blob->len);
    }

    unsigned reply = pageant_client_op_query(pco);
    pageant_client_op_free(pco);

    if (reply != SSH_AGENT_SUCCESS) {
        if (reply == SSH_AGENT_FAILURE) {
            /* The agent didn't understand the protocol extension at all. */
            *retstr = dupstr("Agent doesn't support encrypted keys");
        } else {
            *retstr = dupstr("Agent failed to re-encrypt key");
        }
        return PAGEANT_ACTION_FAILURE;
    } else {
        *retstr = NULL;
        return PAGEANT_ACTION_OK;
    }
}

int pageant_reencrypt_all_keys(char **retstr)
{
    PageantClientOp *pco = pageant_client_op_new();
    put_byte(pco, SSH2_AGENTC_EXTENSION);
    put_stringpl(pco, extension_names[EXT_REENCRYPT_ALL]);
    unsigned reply = pageant_client_op_query(pco);
    uint32_t failures = get_uint32(pco);
    pageant_client_op_free(pco);
    if (reply != SSH_AGENT_SUCCESS) {
        if (reply == SSH_AGENT_FAILURE) {
            /* The agent didn't understand the protocol extension at all. */
            *retstr = dupstr("Agent doesn't support encrypted keys");
        } else {
            *retstr = dupstr("Agent failed to re-encrypt any keys");
        }
        return PAGEANT_ACTION_FAILURE;
    } else if (failures == 1) {
        /* special case for English grammar */
        *retstr = dupstr("1 key remains unencrypted");
        return PAGEANT_ACTION_WARNING;
    } else if (failures > 0) {
        *retstr = dupprintf("%"PRIu32" keys remain unencrypted", failures);
        return PAGEANT_ACTION_WARNING;
    } else {
        *retstr = NULL;
        return PAGEANT_ACTION_OK;
    }
}

int pageant_sign(struct pageant_pubkey *key, ptrlen message, strbuf *out,
                 uint32_t flags, char **retstr)
{
    PageantClientOp *pco = pageant_client_op_new();
    put_byte(pco, SSH2_AGENTC_SIGN_REQUEST);
    put_string(pco, key->blob->s, key->blob->len);
    put_stringpl(pco, message);
    put_uint32(pco, flags);
    unsigned reply = pageant_client_op_query(pco);
    ptrlen signature = get_string(pco);

    if (reply == SSH2_AGENT_SIGN_RESPONSE && !get_err(pco)) {
        *retstr = NULL;
        put_datapl(out, signature);
        pageant_client_op_free(pco);
        return PAGEANT_ACTION_OK;
    } else {
        *retstr = dupstr("Agent failed to create signature");
        pageant_client_op_free(pco);
        return PAGEANT_ACTION_FAILURE;
    }
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
