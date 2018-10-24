/*
 * wincons.c - various interactive-prompt routines shared between
 * the Windows console PuTTY tools
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "putty.h"
#include "storage.h"
#include "ssh.h"

#ifdef PERSOPORT
int GetAutoStoreSSHKeyFlag(void) ;
#endif

int console_batch_mode = FALSE;

static void *console_logctx = NULL;

/*
 * Clean up and exit.
 */
void cleanup_exit(int code)
{
    /*
     * Clean up.
     */
    sk_cleanup();

    random_save_seed();
#ifdef MSCRYPTOAPI
    crypto_wrapup();
#endif

    exit(code);
}

void set_busy_status(void *frontend, int status)
{
}

void notify_remote_exit(void *frontend)
{
}

void timer_change_notify(unsigned long next)
{
}

int verify_ssh_host_key(void *frontend, char *host, int port,
                        const char *keytype, char *keystr, char *fingerprint,
                        void (*callback)(void *ctx, int result), void *ctx)
{
    int ret;
    HANDLE hin;
    DWORD savemode, i;

    static const char absentmsg_batch[] =
	"The server's host key is not cached in the registry. You\n"
	"have no guarantee that the server is the computer you\n"
	"think it is.\n"
	"The server's %s key fingerprint is:\n"
	"%s\n"
	"Connection abandoned.\n";
    static const char absentmsg[] =
	"The server's host key is not cached in the registry. You\n"
	"have no guarantee that the server is the computer you\n"
	"think it is.\n"
	"The server's %s key fingerprint is:\n"
	"%s\n"
	"If you trust this host, enter \"y\" to add the key to\n"
	"PuTTY's cache and carry on connecting.\n"
	"If you want to carry on connecting just once, without\n"
	"adding the key to the cache, enter \"n\".\n"
	"If you do not trust this host, press Return to abandon the\n"
	"connection.\n"
	"Store key in cache? (y/n) ";

    static const char wrongmsg_batch[] =
	"WARNING - POTENTIAL SECURITY BREACH!\n"
	"The server's host key does not match the one PuTTY has\n"
	"cached in the registry. This means that either the\n"
	"server administrator has changed the host key, or you\n"
	"have actually connected to another computer pretending\n"
	"to be the server.\n"
	"The new %s key fingerprint is:\n"
	"%s\n"
	"Connection abandoned.\n";
    static const char wrongmsg[] =
	"WARNING - POTENTIAL SECURITY BREACH!\n"
	"The server's host key does not match the one PuTTY has\n"
	"cached in the registry. This means that either the\n"
	"server administrator has changed the host key, or you\n"
	"have actually connected to another computer pretending\n"
	"to be the server.\n"
	"The new %s key fingerprint is:\n"
	"%s\n"
	"If you were expecting this change and trust the new key,\n"
	"enter \"y\" to update PuTTY's cache and continue connecting.\n"
	"If you want to carry on connecting but without updating\n"
	"the cache, enter \"n\".\n"
	"If you want to abandon the connection completely, press\n"
	"Return to cancel. Pressing Return is the ONLY guaranteed\n"
	"safe choice.\n"
	"Update cached key? (y/n, Return cancels connection) ";

    static const char abandoned[] = "Connection abandoned.\n";

    char line[32];

    /*
     * Verify the key against the registry.
     */
    ret = verify_host_key(host, port, keytype, keystr);

    if (ret == 0)		       /* success - key matched OK */
	return 1;

    if (ret == 2) {		       /* key was different */
	if (console_batch_mode) {
	    fprintf(stderr, wrongmsg_batch, keytype, fingerprint);
            return 0;
	}
	fprintf(stderr, wrongmsg, keytype, fingerprint);
	fflush(stderr);
    }
    if (ret == 1) {		       /* key was absent */
	if (console_batch_mode) {
	    fprintf(stderr, absentmsg_batch, keytype, fingerprint);
            return 0;
	}
	fprintf(stderr, absentmsg, keytype, fingerprint);
	fflush(stderr);
    }

    line[0] = '\0';         /* fail safe if ReadFile returns no data */

 #ifdef PERSOPORT
	if( GetAutoStoreSSHKeyFlag() ) { 
		fprintf( stderr, "\nAutostore key is on\n" );
		strcpy(line,"y\r\n"); 
	} else {
#endif
    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
			 ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
    SetConsoleMode(hin, savemode);

#ifdef PERSOPORT
    }
#endif
    if (line[0] != '\0' && line[0] != '\r' && line[0] != '\n') {
	if (line[0] == 'y' || line[0] == 'Y')
	    store_host_key(host, port, keytype, keystr);
        return 1;
    } else {
	fprintf(stderr, abandoned);
        return 0;
    }
}

void update_specials_menu(void *frontend)
{
}

/*
 * Ask whether the selected algorithm is acceptable (since it was
 * below the configured 'warn' threshold).
 */
int askalg(void *frontend, const char *algtype, const char *algname,
	   void (*callback)(void *ctx, int result), void *ctx)
{
    HANDLE hin;
    DWORD savemode, i;

    static const char msg[] =
	"The first %s supported by the server is\n"
	"%s, which is below the configured warning threshold.\n"
	"Continue with connection? (y/n) ";
    static const char msg_batch[] =
	"The first %s supported by the server is\n"
	"%s, which is below the configured warning threshold.\n"
	"Connection abandoned.\n";
    static const char abandoned[] = "Connection abandoned.\n";

    char line[32];

    if (console_batch_mode) {
	fprintf(stderr, msg_batch, algtype, algname);
	return 0;
    }

    fprintf(stderr, msg, algtype, algname);
    fflush(stderr);

    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
			 ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
    SetConsoleMode(hin, savemode);

    if (line[0] == 'y' || line[0] == 'Y') {
	return 1;
    } else {
	fprintf(stderr, abandoned);
	return 0;
    }
}

int askhk(void *frontend, const char *algname, const char *betteralgs,
          void (*callback)(void *ctx, int result), void *ctx)
{
    HANDLE hin;
    DWORD savemode, i;

    static const char msg[] =
	"The first host key type we have stored for this server\n"
	"is %s, which is below the configured warning threshold.\n"
	"The server also provides the following types of host key\n"
        "above the threshold, which we do not have stored:\n"
        "%s\n"
	"Continue with connection? (y/n) ";
    static const char msg_batch[] =
	"The first host key type we have stored for this server\n"
	"is %s, which is below the configured warning threshold.\n"
	"The server also provides the following types of host key\n"
        "above the threshold, which we do not have stored:\n"
        "%s\n"
	"Connection abandoned.\n";
    static const char abandoned[] = "Connection abandoned.\n";

    char line[32];

    if (console_batch_mode) {
	fprintf(stderr, msg_batch, algname, betteralgs);
	return 0;
    }

    fprintf(stderr, msg, algname, betteralgs);
    fflush(stderr);

    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
			 ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
    SetConsoleMode(hin, savemode);

    if (line[0] == 'y' || line[0] == 'Y') {
	return 1;
    } else {
	fprintf(stderr, abandoned);
	return 0;
    }
}

/*
 * Ask whether to wipe a session log file before writing to it.
 * Returns 2 for wipe, 1 for append, 0 for cancel (don't log).
 */
int askappend(void *frontend, Filename *filename,
	      void (*callback)(void *ctx, int result), void *ctx)
{
    HANDLE hin;
    DWORD savemode, i;

    static const char msgtemplate[] =
	"The session log file \"%.*s\" already exists.\n"
	"You can overwrite it with a new session log,\n"
	"append your session log to the end of it,\n"
	"or disable session logging for this session.\n"
	"Enter \"y\" to wipe the file, \"n\" to append to it,\n"
	"or just press Return to disable logging.\n"
	"Wipe the log file? (y/n, Return cancels logging) ";

    static const char msgtemplate_batch[] =
	"The session log file \"%.*s\" already exists.\n"
	"Logging will not be enabled.\n";

    char line[32];

    if (console_batch_mode) {
	fprintf(stderr, msgtemplate_batch, FILENAME_MAX, filename->path);
	fflush(stderr);
	return 0;
    }
    fprintf(stderr, msgtemplate, FILENAME_MAX, filename->path);
    fflush(stderr);

    hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode | ENABLE_ECHO_INPUT |
			 ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT));
    ReadFile(hin, line, sizeof(line) - 1, &i, NULL);
    SetConsoleMode(hin, savemode);

    if (line[0] == 'y' || line[0] == 'Y')
	return 2;
    else if (line[0] == 'n' || line[0] == 'N')
	return 1;
    else
	return 0;
}

/*
 * Warn about the obsolescent key file format.
 * 
 * Uniquely among these functions, this one does _not_ expect a
 * frontend handle. This means that if PuTTY is ported to a
 * platform which requires frontend handles, this function will be
 * an anomaly. Fortunately, the problem it addresses will not have
 * been present on that platform, so it can plausibly be
 * implemented as an empty function.
 */
void old_keyfile_warning(void)
{
    static const char message[] =
	"You are loading an SSH-2 private key which has an\n"
	"old version of the file format. This means your key\n"
	"file is not fully tamperproof. Future versions of\n"
	"PuTTY may stop supporting this private key format,\n"
	"so we recommend you convert your key to the new\n"
	"format.\n"
	"\n"
	"Once the key is loaded into PuTTYgen, you can perform\n"
	"this conversion simply by saving it again.\n";

    fputs(message, stderr);
}

/*
 * Display the fingerprints of the PGP Master Keys to the user.
 */
void pgp_fingerprints(void)
{
    fputs("These are the fingerprints of the PuTTY PGP Master Keys. They can\n"
	  "be used to establish a trust path from this executable to another\n"
	  "one. See the manual for more information.\n"
	  "(Note: these fingerprints have nothing to do with SSH!)\n"
	  "\n"
	  "PuTTY Master Key as of 2015 (RSA, 4096-bit):\n"
	  "  " PGP_MASTER_KEY_FP "\n\n"
	  "Original PuTTY Master Key (RSA, 1024-bit):\n"
	  "  " PGP_RSA_MASTER_KEY_FP "\n"
	  "Original PuTTY Master Key (DSA, 1024-bit):\n"
	  "  " PGP_DSA_MASTER_KEY_FP "\n", stdout);
}

void console_provide_logctx(void *logctx)
{
    console_logctx = logctx;
}

void logevent(void *frontend, const char *string)
{
    log_eventlog(console_logctx, string);
}

static void console_data_untrusted(HANDLE hout, const char *data, int len)
{
    DWORD dummy;
    /* FIXME: control-character filtering */
    WriteFile(hout, data, len, &dummy, NULL);
}

int console_get_userpass_input(prompts_t *p,
                               const unsigned char *in, int inlen)
{
    HANDLE hin, hout;
    size_t curr_prompt;

    /*
     * Zero all the results, in case we abort half-way through.
     */
    {
	int i;
	for (i = 0; i < (int)p->n_prompts; i++)
            prompt_set_result(p->prompts[i], "");
    }

    /*
     * The prompts_t might contain a message to be displayed but no
     * actual prompt. More usually, though, it will contain
     * questions that the user needs to answer, in which case we
     * need to ensure that we're able to get the answers.
     */
    if (p->n_prompts) {
	if (console_batch_mode)
	    return 0;
	hin = GetStdHandle(STD_INPUT_HANDLE);
	if (hin == INVALID_HANDLE_VALUE) {
	    fprintf(stderr, "Cannot get standard input handle\n");
	    cleanup_exit(1);
	}
    }

    /*
     * And if we have anything to print, we need standard output.
     */
    if ((p->name_reqd && p->name) || p->instruction || p->n_prompts) {
	hout = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hout == INVALID_HANDLE_VALUE) {
	    fprintf(stderr, "Cannot get standard output handle\n");
	    cleanup_exit(1);
	}
    }

    /*
     * Preamble.
     */
    /* We only print the `name' caption if we have to... */
    if (p->name_reqd && p->name) {
	size_t l = strlen(p->name);
	console_data_untrusted(hout, p->name, l);
	if (p->name[l-1] != '\n')
	    console_data_untrusted(hout, "\n", 1);
    }
    /* ...but we always print any `instruction'. */
    if (p->instruction) {
	size_t l = strlen(p->instruction);
	console_data_untrusted(hout, p->instruction, l);
	if (p->instruction[l-1] != '\n')
	    console_data_untrusted(hout, "\n", 1);
    }

    for (curr_prompt = 0; curr_prompt < p->n_prompts; curr_prompt++) {

	DWORD savemode, newmode;
        int len;
	prompt_t *pr = p->prompts[curr_prompt];

	GetConsoleMode(hin, &savemode);
	newmode = savemode | ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT;
	if (!pr->echo)
	    newmode &= ~ENABLE_ECHO_INPUT;
	else
	    newmode |= ENABLE_ECHO_INPUT;
	SetConsoleMode(hin, newmode);

	console_data_untrusted(hout, pr->prompt, strlen(pr->prompt));

        len = 0;
        while (1) {
            DWORD ret = 0;
            BOOL r;

            prompt_ensure_result_size(pr, len * 5 / 4 + 512);

            r = ReadFile(hin, pr->result + len, pr->resultsize - len - 1,
                         &ret, NULL);

            if (!r || ret == 0) {
                len = -1;
                break;
            }
            len += ret;
            if (pr->result[len - 1] == '\n') {
                len--;
                if (pr->result[len - 1] == '\r')
                    len--;
                break;
            }
        }

	SetConsoleMode(hin, savemode);

	if (!pr->echo) {
	    DWORD dummy;
	    WriteFile(hout, "\r\n", 2, &dummy, NULL);
	}

        if (len < 0) {
            return 0;                  /* failure due to read error */
        }

	pr->result[len] = '\0';
    }

    return 1; /* success */
}

void frontend_keypress(void *handle)
{
    /*
     * This is nothing but a stub, in console code.
     */
    return;
}
