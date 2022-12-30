/*
 * wincons.c - various interactive-prompt routines shared between
 * the Windows console PuTTY tools
 */

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"
#include "storage.h"
#include "ssh.h"

#ifdef MOD_PERSO
int GetAutoStoreSSHKeyFlag(void) ;
#endif

#include "console.h"

void cleanup_exit(int code)
{
    /*
     * Clean up.
     */
    sk_cleanup();

    random_save_seed();

    exit(code);
}

void console_print_error_msg(const char *prefix, const char *msg)
{
    fputs(prefix, stderr);
    fputs(": ", stderr);
    fputs(msg, stderr);
    fputc('\n', stderr);
    fflush(stderr);
}

int console_verify_ssh_host_key(
    Seat *seat, const char *host, int port, const char *keytype,
    char *keystr, const char *keydisp, char **fingerprints,
    void (*callback)(void *ctx, int result), void *ctx)
{
    int ret;
    HANDLE hin;
    DWORD savemode, i;

    const char *common_fmt, *intro, *prompt;

    char line[32];

    /*
     * Verify the key against the registry.
     */
    ret = verify_host_key(host, port, keytype, keystr);

    if (ret == 0)		       /* success - key matched OK */
	return 1;

    if (ret == 2) {		       /* key was different */
        common_fmt = hk_wrongmsg_common_fmt;
        intro = hk_wrongmsg_interactive_intro;
        prompt = hk_wrongmsg_interactive_prompt;
    } else {                           /* key was absent */
        common_fmt = hk_absentmsg_common_fmt;
        intro = hk_absentmsg_interactive_intro;
        prompt = hk_absentmsg_interactive_prompt;
    }

    FingerprintType fptype_default =
        ssh2_pick_default_fingerprint(fingerprints);

    fprintf(stderr, common_fmt, keytype, fingerprints[fptype_default]);
 #ifdef MOD_PERSO
	if( !GetAutoStoreSSHKeyFlag() ) 
#endif
    if (console_batch_mode) {
        fputs(console_abandoned_msg, stderr);
        return 0;
    }
    fputs(intro, stderr);
    fflush(stderr);
    while (true) {
        fputs(prompt, stderr);
	fflush(stderr);

    line[0] = '\0';         /* fail safe if ReadFile returns no data */

#ifdef MOD_PERSO
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

#ifdef MOD_PERSO
    }
#endif

        if (line[0] == 'i' || line[0] == 'I') {
            fprintf(stderr, "Full public key:\n%s\n", keydisp);
            if (fingerprints[SSH_FPTYPE_SHA256])
                fprintf(stderr, "SHA256 key fingerprint:\n%s\n",
                        fingerprints[SSH_FPTYPE_SHA256]);
            if (fingerprints[SSH_FPTYPE_MD5])
                fprintf(stderr, "MD5 key fingerprint:\n%s\n",
                        fingerprints[SSH_FPTYPE_MD5]);
        } else {
            break;
        }
    }

    /* In case of misplaced reflexes from another program, also recognise 'q'
     * as 'abandon connection rather than trust this key' */
    if (line[0] != '\0' && line[0] != '\r' && line[0] != '\n' &&
        line[0] != 'q' && line[0] != 'Q') {
	if (line[0] == 'y' || line[0] == 'Y')
	    store_host_key(host, port, keytype, keystr);
        return 1;
    } else {
        fputs(console_abandoned_msg, stderr);
        return 0;
    }
}

int console_confirm_weak_crypto_primitive(
    Seat *seat, const char *algtype, const char *algname,
    void (*callback)(void *ctx, int result), void *ctx)
{
    HANDLE hin;
    DWORD savemode, i;

    char line[32];

    fprintf(stderr, weakcrypto_msg_common_fmt, algtype, algname);

    if (console_batch_mode) {
        fputs(console_abandoned_msg, stderr);
	return 0;
    }

    fputs(console_continue_prompt, stderr);
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
        fputs(console_abandoned_msg, stderr);
	return 0;
    }
}

int console_confirm_weak_cached_hostkey(
    Seat *seat, const char *algname, const char *betteralgs,
    void (*callback)(void *ctx, int result), void *ctx)
{
    HANDLE hin;
    DWORD savemode, i;

    char line[32];

    fprintf(stderr, weakhk_msg_common_fmt, algname, betteralgs);

    if (console_batch_mode) {
        fputs(console_abandoned_msg, stderr);
	return 0;
    }

    fputs(console_continue_prompt, stderr);
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
        fputs(console_abandoned_msg, stderr);
	return 0;
    }
}

bool is_interactive(void)
{
    return is_console_handle(GetStdHandle(STD_INPUT_HANDLE));
}

bool console_antispoof_prompt = true;
bool console_set_trust_status(Seat *seat, bool trusted)
{
    if (console_batch_mode || !is_interactive() || !console_antispoof_prompt) {
        /*
         * In batch mode, we don't need to worry about the server
         * mimicking our interactive authentication, because the user
         * already knows not to expect any.
         *
         * If standard input isn't connected to a terminal, likewise,
         * because even if the server did send a spoof authentication
         * prompt, the user couldn't respond to it via the terminal
         * anyway.
         *
         * We also vacuously return success if the user has purposely
         * disabled the antispoof prompt.
         */
        return true;
    }

    return false;
}

/*
 * Ask whether to wipe a session log file before writing to it.
 * Returns 2 for wipe, 1 for append, 0 for cancel (don't log).
 */
int console_askappend(LogPolicy *lp, Filename *filename,
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
	  "PuTTY Master Key as of " PGP_MASTER_KEY_YEAR
          " (" PGP_MASTER_KEY_DETAILS "):\n"
	  "  " PGP_MASTER_KEY_FP "\n\n"
	  "Previous Master Key (" PGP_PREV_MASTER_KEY_YEAR
          ", " PGP_PREV_MASTER_KEY_DETAILS "):\n"
	  "  " PGP_PREV_MASTER_KEY_FP "\n", stdout);
}

void console_logging_error(LogPolicy *lp, const char *string)
{
    /* Ordinary Event Log entries are displayed in the same way as
     * logging errors, but only in verbose mode */
    fprintf(stderr, "%s\n", string);
    fflush(stderr);
}

void console_eventlog(LogPolicy *lp, const char *string)
{
    /* Ordinary Event Log entries are displayed in the same way as
     * logging errors, but only in verbose mode */
    if (lp_verbose(lp))
        console_logging_error(lp, string);
}

StripCtrlChars *console_stripctrl_new(
    Seat *seat, BinarySink *bs_out, SeatInteractionContext sic)
{
    return stripctrl_new(bs_out, false, 0);
}

static void console_write(HANDLE hout, ptrlen data)
{
    DWORD dummy;
    WriteFile(hout, data.ptr, data.len, &dummy, NULL);
}

int console_get_userpass_input(prompts_t *p)
{
    HANDLE hin = INVALID_HANDLE_VALUE, hout = INVALID_HANDLE_VALUE;
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
	ptrlen plname = ptrlen_from_asciz(p->name);
	console_write(hout, plname);
        if (!ptrlen_endswith(plname, PTRLEN_LITERAL("\n"), NULL))
	    console_write(hout, PTRLEN_LITERAL("\n"));
    }
    /* ...but we always print any `instruction'. */
    if (p->instruction) {
	ptrlen plinst = ptrlen_from_asciz(p->instruction);
	console_write(hout, plinst);
        if (!ptrlen_endswith(plinst, PTRLEN_LITERAL("\n"), NULL))
	    console_write(hout, PTRLEN_LITERAL("\n"));
    }

    for (curr_prompt = 0; curr_prompt < p->n_prompts; curr_prompt++) {

	DWORD savemode, newmode;
	prompt_t *pr = p->prompts[curr_prompt];

	GetConsoleMode(hin, &savemode);
	newmode = savemode | ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT;
	if (!pr->echo)
	    newmode &= ~ENABLE_ECHO_INPUT;
	else
	    newmode |= ENABLE_ECHO_INPUT;
	SetConsoleMode(hin, newmode);

	console_write(hout, ptrlen_from_asciz(pr->prompt));

        bool failed = false;
        while (1) {
            /*
             * Amount of data to try to read from the console in one
             * go. This isn't completely arbitrary: a user reported
             * that trying to read more than 31366 bytes at a time
             * would fail with ERROR_NOT_ENOUGH_MEMORY on Windows 7,
             * and Ruby's Win32 support module has evidence of a
             * similar workaround:
             *
             * https://github.com/ruby/ruby/blob/0aa5195262d4193d3accf3e6b9bad236238b816b/win32/win32.c#L6842
             *
             * To keep things simple, I stick with a nice round power
             * of 2 rather than trying to go to the very limit of that
             * bug. (We're typically reading user passphrases and the
             * like here, so even this much is overkill really.)
             */
            DWORD toread = 16384;

            size_t prev_result_len = pr->result->len;
            void *ptr = strbuf_append(pr->result, toread);

            DWORD ret = 0;

            if (!ReadFile(hin, ptr, toread, &ret, NULL) || ret == 0) {
                failed = true;
                break;
            }

            strbuf_shrink_to(pr->result, prev_result_len + ret);
            if (strbuf_chomp(pr->result, '\n')) {
                strbuf_chomp(pr->result, '\r');
                break;
            }
        }

	SetConsoleMode(hin, savemode);

	if (!pr->echo)
            console_write(hout, PTRLEN_LITERAL("\r\n"));

        if (failed) {
            return 0;                  /* failure due to read error */
        }

    }

    return 1; /* success */
}
