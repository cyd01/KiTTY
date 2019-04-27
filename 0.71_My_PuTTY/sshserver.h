typedef struct AuthPolicy AuthPolicy;

Plug *ssh_server_plug(
    Conf *conf, ssh_key *const *hostkeys, int nhostkeys,
    RSAKey *hostkey1, AuthPolicy *authpolicy, LogPolicy *logpolicy,
    const SftpServerVtable *sftpserver_vt);
void ssh_server_start(Plug *plug, Socket *socket);

void server_instance_terminated(void);
void platform_logevent(const char *msg);

#define AUTHMETHODS(X)                          \
    X(NONE)                                     \
    X(PASSWORD)                                 \
    X(PUBLICKEY)                                \
    X(KBDINT)                                   \
    X(TIS)                                      \
    X(CRYPTOCARD)                               \
    /* end of list */

#define AUTHMETHOD_BIT_INDEX(name) AUTHMETHOD_BIT_INDEX_##name,
enum { AUTHMETHODS(AUTHMETHOD_BIT_INDEX) AUTHMETHOD_BIT_INDEX_dummy };
#define AUTHMETHOD_BIT_VALUE(name) \
    AUTHMETHOD_##name = 1 << AUTHMETHOD_BIT_INDEX_##name,
enum { AUTHMETHODS(AUTHMETHOD_BIT_VALUE) AUTHMETHOD_BIT_VALUE_dummy };

typedef struct AuthKbdInt AuthKbdInt;
typedef struct AuthKbdIntPrompt AuthKbdIntPrompt;
struct AuthKbdInt {
    char *title, *instruction;         /* both need freeing */
    int nprompts;
    AuthKbdIntPrompt *prompts;         /* the array itself needs freeing */
};
struct AuthKbdIntPrompt {
    char *prompt;                      /* needs freeing */
    bool echo;
};

unsigned auth_methods(AuthPolicy *);
bool auth_none(AuthPolicy *, ptrlen username);

int auth_password(AuthPolicy *, ptrlen username, ptrlen password,
                  ptrlen *opt_new_password);
/* auth_password returns 1 for 'accepted', 0 for 'rejected', and 2 for
 * 'ok but now you need to change your password' */

bool auth_publickey(AuthPolicy *, ptrlen username, ptrlen public_blob);
/* auth_publickey_ssh1 must return the whole public key given the modulus,
 * because the SSH-1 client never transmits the exponent over the wire.
 * The key remains owned by the AuthPolicy. */

AuthKbdInt *auth_kbdint_prompts(AuthPolicy *, ptrlen username);
/* auth_kbdint_prompts returns NULL to trigger auth failure */
int auth_kbdint_responses(AuthPolicy *, const ptrlen *responses);
/* auth_kbdint_responses returns >0 for success, <0 for failure, and 0
 * to indicate that we haven't decided yet and further prompts are
 * coming */

/* The very similar SSH-1 TIS and CryptoCard methods are combined into
 * a single API for AuthPolicy, which takes a method argument */
char *auth_ssh1int_challenge(AuthPolicy *, unsigned method, ptrlen username);
bool auth_ssh1int_response(AuthPolicy *, ptrlen response);

RSAKey *auth_publickey_ssh1(
    AuthPolicy *ap, ptrlen username, mp_int *rsa_modulus);
/* auth_successful returns false if further authentication is needed */
bool auth_successful(AuthPolicy *, ptrlen username, unsigned method);

PacketProtocolLayer *ssh2_userauth_server_new(
    PacketProtocolLayer *successor_layer, AuthPolicy *authpolicy);
void ssh2_userauth_server_set_transport_layer(
    PacketProtocolLayer *userauth, PacketProtocolLayer *transport);

void ssh2connection_server_configure(
    PacketProtocolLayer *ppl, const SftpServerVtable *sftpserver_vt);

PacketProtocolLayer *ssh1_login_server_new(
    PacketProtocolLayer *successor_layer, RSAKey *hostkey,
    AuthPolicy *authpolicy);

Channel *sesschan_new(SshChannel *c, LogContext *logctx,
                      const SftpServerVtable *sftpserver_vt);

Backend *pty_backend_create(
    Seat *seat, LogContext *logctx, Conf *conf, char **argv, const char *cmd,
    struct ssh_ttymodes ttymodes, bool pipes_instead_of_pty);
ptrlen pty_backend_exit_signame(Backend *be, char **aux_msg);

/*
 * Establish a listening X server. Return value is the _number_ of
 * Sockets that it established pointing at the given Plug. (0
 * indicates complete failure.) The socket pointers themselves are
 * written into sockets[], up to a possible total of MAX_X11_SOCKETS.
 *
 * The supplied Conf has necessary environment variables written into
 * it. (And is also used to open the port listeners, though that
 * shouldn't affect anything.)
 */
#define MAX_X11_SOCKETS 2
int platform_make_x11_server(Plug *plug, const char *progname, int mindisp,
                             const char *screen_number_suffix,
                             ptrlen authproto, ptrlen authdata,
                             Socket **sockets, Conf *conf);
