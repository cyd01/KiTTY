/*
 * cgtest.c: stub file to compile cmdgen.c in self-test mode
 */

/*
 * Before we #include cmdgen.c, we override some function names for
 * test purposes. We do this via #define, so that when we link against
 * modules containing the original versions, we don't get a link-time
 * symbol clash:
 *
 *  - Calls to get_random_data() are replaced with the diagnostic
 *    function below, in order to avoid depleting the test system's
 *    /dev/random unnecessarily.
 *
 *  - Calls to console_get_userpass_input() are replaced with the
 *    diagnostic function below, so that I can run tests in an
 *    automated manner and provide their interactive passphrase
 *    inputs.
 *
 *  - The main() defined by cmdgen.c is renamed to cmdgen_main(); in
 *    this file I define another main() which calls the former
 *    repeatedly to run tests.
 */
#define get_random_data get_random_data_diagnostic
#define console_get_userpass_input console_get_userpass_input_diagnostic
#define main cmdgen_main
#define ppk_save_default_parameters ppk_save_cgtest_parameters

#include "cmdgen.c"

#undef get_random_data
#undef console_get_userpass_input
#undef main

static bool cgtest_verbose = false;

const struct ppk_save_parameters ppk_save_cgtest_parameters = {
    /* Replacement set of key derivation parameters that make this
     * test suite run a bit faster and also add determinism: we don't
     * try to auto-scale the number of passes (in case it gets
     * different answers twice in the test suite when we were
     * expecting two key files to compare equal), and we specify a
     * passphrase salt. */
    .fmt_version = 3,
    .argon2_flavour = Argon2id,
    .argon2_mem = 16,
    .argon2_passes_auto = false,
    .argon2_passes = 2,
    .argon2_parallelism = 1,
    .salt = (const uint8_t *)"SameSaltEachTime",
    .saltlen = 16,
};

/*
 * Define the special versions of get_random_data and
 * console_get_userpass_input that we need for this test rig.
 */

char *get_random_data_diagnostic(int len, const char *device)
{
    char *buf = snewn(len, char);
    memset(buf, 'x', len);
    return buf;
}

static int nprompts, promptsgot;
static const char *prompts[3];
int console_get_userpass_input_diagnostic(prompts_t *p)
{
    size_t i;
    int ret = 1;
    for (i = 0; i < p->n_prompts; i++) {
        if (promptsgot < nprompts) {
            prompt_set_result(p->prompts[i], prompts[promptsgot++]);
            if (cgtest_verbose)
                printf("  prompt \"%s\": response \"%s\"\n",
                       p->prompts[i]->prompt, p->prompts[i]->result->s);
        } else {
            promptsgot++;           /* track number of requests anyway */
            ret = 0;
            if (cgtest_verbose)
                printf("  prompt \"%s\": no response preloaded\n",
                       p->prompts[i]->prompt);
        }
    }
    return ret;
}

#include <stdarg.h>

static int passes, fails;

void setup_passphrases(char *first, ...)
{
    va_list ap;
    char *next;

    nprompts = 0;
    if (first) {
        prompts[nprompts++] = first;
        va_start(ap, first);
        while ((next = va_arg(ap, char *)) != NULL) {
            assert(nprompts < lenof(prompts));
            prompts[nprompts++] = next;
        }
        va_end(ap);
    }
}

void test(int retval, ...)
{
    va_list ap;
    int i, argc, ret;
    char **argv;

    argc = 0;
    va_start(ap, retval);
    while (va_arg(ap, char *) != NULL)
        argc++;
    va_end(ap);

    argv = snewn(argc+1, char *);
    va_start(ap, retval);
    for (i = 0; i <= argc; i++)
        argv[i] = va_arg(ap, char *);
    va_end(ap);

    promptsgot = 0;
    if (cgtest_verbose) {
        printf("run:");
        for (int i = 0; i < argc; i++) {
            static const char okchars[] =
                "0123456789abcdefghijklmnopqrstuvwxyz"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ%+,-./:=[]^_";
            const char *arg = argv[i];

            printf(" ");
            if (arg[strspn(arg, okchars)]) {
                printf("'");
                for (const char *c = argv[i]; *c; c++) {
                    if (*c == '\'') {
                        printf("'\\''");
                    } else {
                        putchar(*c);
                    }
                }
                printf("'");
            } else {
                fputs(arg, stdout);
            }
        }
        printf("\n");
    }
    ret = cmdgen_main(argc, argv);
    random_clear();

    if (ret != retval) {
        printf("FAILED retval (exp %d got %d):", retval, ret);
        for (i = 0; i < argc; i++)
            printf(" %s", argv[i]);
        printf("\n");
        fails++;
    } else if (promptsgot != nprompts) {
        printf("FAILED nprompts (exp %d got %d):", nprompts, promptsgot);
        for (i = 0; i < argc; i++)
            printf(" %s", argv[i]);
        printf("\n");
        fails++;
    } else {
        passes++;
    }

    sfree(argv);
}

PRINTF_LIKE(3, 4) void filecmp(char *file1, char *file2, char *fmt, ...)
{
    /*
     * Ideally I should do file comparison myself, to maximise the
     * portability of this test suite once this application begins
     * running on non-Unix platforms. For the moment, though,
     * calling Unix diff is perfectly adequate.
     */
    char *buf;
    int ret;

    buf = dupprintf("diff -q '%s' '%s'", file1, file2);
    ret = system(buf);
    sfree(buf);

    if (ret) {
        va_list ap;

        printf("FAILED diff (ret=%d): ", ret);

        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);

        printf("\n");

        fails++;
    } else
        passes++;
}

/*
 * General-purpose flags word
 */
#define CGT_FLAGS(X)                            \
    X(CGT_TYPE_KNOWN_EARLY)                     \
    X(CGT_OPENSSH)                              \
    X(CGT_SSHCOM)                               \
    X(CGT_SSH_KEYGEN)                           \
    X(CGT_ED25519)                              \
    /* end of list */

#define FLAG_SHIFTS(name) name ## _shift,
enum { CGT_FLAGS(FLAG_SHIFTS) CGT_dummy_shift };
#define FLAG_VALUES(name) name = 1 << name ## _shift,
enum { CGT_FLAGS(FLAG_VALUES) CGT_dummy_flag };

char *cleanup_fp(char *s, unsigned flags)
{
    ptrlen pl = ptrlen_from_asciz(s);
    static const char separators[] = " \n\t";

    /* Skip initial key type word if we find one */
    if (ptrlen_startswith(pl, PTRLEN_LITERAL("ssh-"), NULL) ||
        ptrlen_startswith(pl, PTRLEN_LITERAL("ecdsa-"), NULL))
        ptrlen_get_word(&pl, separators);

    /* Expect two words giving the key length and the hash */
    ptrlen bits = ptrlen_get_word(&pl, separators);
    ptrlen hash = ptrlen_get_word(&pl, separators);

    if (flags & CGT_SSH_KEYGEN) {
        /* Strip "MD5:" prefix if it's present, and do nothing if it isn't */
        ptrlen_startswith(hash, PTRLEN_LITERAL("MD5:"), &hash);

        if (flags & CGT_ED25519) {
            /* OpenSSH ssh-keygen lists ed25519 keys as 256 bits, not 255 */
            if (ptrlen_eq_string(bits, "256"))
                bits = PTRLEN_LITERAL("255");
        }
    }

    return dupprintf("%.*s %.*s", PTRLEN_PRINTF(bits), PTRLEN_PRINTF(hash));
}

char *get_line(char *filename)
{
    FILE *fp;
    char *line;

    fp = fopen(filename, "r");
    if (!fp)
        return NULL;
    line = fgetline(fp);
    fclose(fp);
    return line;
}

char *get_fp(char *filename, unsigned flags)
{
    char *orig = get_line(filename);
    if (!orig)
        return NULL;
    char *toret = cleanup_fp(orig, flags);
    sfree(orig);
    return toret;
}

PRINTF_LIKE(3, 4) void check_fp(char *filename, char *fp, char *fmt, ...)
{
    char *newfp;

    if (!fp)
        return;

    newfp = get_fp(filename, 0);

    if (!strcmp(fp, newfp)) {
        passes++;
    } else {
        va_list ap;

        printf("FAILED check_fp ['%s' != '%s']: ", newfp, fp);

        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);

        printf("\n");

        fails++;
    }

    sfree(newfp);
}

static const struct cgtest_keytype {
    const char *name;
    unsigned flags;
} cgtest_keytypes[] = {
    { "rsa1", CGT_TYPE_KNOWN_EARLY },
    { "dsa", CGT_OPENSSH | CGT_SSHCOM },
    { "rsa", CGT_OPENSSH | CGT_SSHCOM },
    { "ecdsa", CGT_OPENSSH },
    { "ed25519", CGT_OPENSSH | CGT_ED25519 },
};

int main(int argc, char **argv)
{
    int i;
    int active[lenof(cgtest_keytypes)], active_value;
    bool remove_files = true;

    active_value = 0;
    for (i = 0; i < lenof(cgtest_keytypes); i++)
        active[i] = active_value;

    while (--argc > 0) {
        ptrlen arg = ptrlen_from_asciz(*++argv);
        if (ptrlen_eq_string(arg, "-v") ||
            ptrlen_eq_string(arg, "--verbose")) {
            cgtest_verbose = true;
        } else if (ptrlen_eq_string(arg, "--keep")) {
            remove_files = false;
        } else if (ptrlen_eq_string(arg, "--help")) {
            printf("usage:     cgtest [options] [key types]\n");
            printf("options:   -v, --verbose         "
                   "print more output during tests\n");
            printf("           --keep                "
                   "do not delete the temporary output files\n");
            printf("           --help                "
                   "display this help text\n");
            printf("key types: ");
            for (i = 0; i < lenof(cgtest_keytypes); i++)
                printf("%s%s", i ? ", " : "", cgtest_keytypes[i].name);
            printf("\n");
            return 0;
        } else if (!ptrlen_startswith(arg, PTRLEN_LITERAL("-"), NULL)) {
            for (i = 0; i < lenof(cgtest_keytypes); i++)
                if (ptrlen_eq_string(arg, cgtest_keytypes[i].name))
                    break;
            if (i == lenof(cgtest_keytypes)) {
                fprintf(stderr, "cgtest: unrecognised key type '%.*s'\n",
                        PTRLEN_PRINTF(arg));
                return 1;
            }
            active_value = 1; /* disables all keys not explicitly enabled */
            active[i] = active_value;
        } else {
            fprintf(stderr, "cgtest: unrecognised option '%.*s'\n",
                    PTRLEN_PRINTF(arg));
            return 1;
        }
    }

    passes = fails = 0;

    for (i = 0; i < lenof(cgtest_keytypes); i++) {
        if (active[i] != active_value)
            continue;

        const struct cgtest_keytype *keytype = &cgtest_keytypes[i];
        bool supports_openssh = keytype->flags & CGT_OPENSSH;
        bool supports_sshcom = keytype->flags & CGT_SSHCOM;
        bool type_known_early = keytype->flags & CGT_TYPE_KNOWN_EARLY;

        char filename[128], osfilename[128], scfilename[128];
        char pubfilename[128], tmpfilename1[128], tmpfilename2[128];
        char *fps[SSH_N_FPTYPES];

        sprintf(filename, "test-%s.ppk", keytype->name);
        sprintf(pubfilename, "test-%s.pub", keytype->name);
        sprintf(osfilename, "test-%s.os", keytype->name);
        sprintf(scfilename, "test-%s.sc", keytype->name);
        sprintf(tmpfilename1, "test-%s.tmp1", keytype->name);
        sprintf(tmpfilename2, "test-%s.tmp2", keytype->name);

        /*
         * Create an encrypted key.
         */
        setup_passphrases("sponge", "sponge", NULL);
        test(0, "puttygen", "-t", keytype->name, "-o", filename, NULL);

        /*
         * List the public key in OpenSSH format.
         */
        setup_passphrases(NULL);
        test(0, "puttygen", "-L", filename, "-o", pubfilename, NULL);
        for (FingerprintType fptype = 0; fptype < SSH_N_FPTYPES; fptype++) {
            const char *fpname = (fptype == SSH_FPTYPE_MD5 ? "md5" : "sha256");
            char *cmdbuf;
            char *fp = NULL;
            cmdbuf = dupprintf("ssh-keygen -E %s -l -f '%s' > '%s'",
                               fpname, pubfilename, tmpfilename1);
            if (cgtest_verbose)
                printf("OpenSSH %s fp check: %s\n", fpname, cmdbuf);
            if (system(cmdbuf) ||
                (fp = get_fp(tmpfilename1,
                             CGT_SSH_KEYGEN | keytype->flags)) == NULL) {
                printf("UNABLE to test fingerprint matching against "
                       "OpenSSH\n");
            }
            sfree(cmdbuf);
            if (fp && cgtest_verbose) {
                char *line = get_line(tmpfilename1);
                printf("OpenSSH %s fp: %s\n", fpname, line);
                printf("Cleaned up: %s\n", fp);
                sfree(line);
            }
            fps[fptype] = fp;
        }

        /*
         * List the public key in IETF/ssh.com format.
         */
        setup_passphrases(NULL);
        test(0, "puttygen", "-p", filename, NULL);

        /*
         * List the fingerprint of the key.
         */
        setup_passphrases(NULL);
        for (FingerprintType fptype = 0; fptype < SSH_N_FPTYPES; fptype++) {
            const char *fpname = (fptype == SSH_FPTYPE_MD5 ? "md5" : "sha256");
            test(0, "puttygen", "-E", fpname, "-l", filename,
                 "-o", tmpfilename1, NULL);
            if (!fps[fptype]) {
                /*
                 * If we can't test fingerprints against OpenSSH, we
                 * can at the very least test equality of all the
                 * fingerprints we generate of this key throughout
                 * testing.
                 */
                fps[fptype] = get_fp(tmpfilename1, 0);
            } else {
                check_fp(tmpfilename1, fps[fptype], "%s initial %s fp",
                         keytype->name, fpname);
            }
        }

        /*
         * Change the comment of the key; this _does_ require a
         * passphrase owing to the tamperproofing.
         *
         * NOTE: In SSH-1, this only requires a passphrase because
         * of inadequacies of the loading and saving mechanisms. In
         * _principle_, it should be perfectly possible to modify
         * the comment on an SSH-1 key without requiring a
         * passphrase; the only reason I can't do it is because my
         * loading and saving mechanisms don't include a method of
         * loading all the key data without also trying to decrypt
         * the private section.
         *
         * I don't consider this to be a problem worth solving,
         * because (a) to fix it would probably end up bloating
         * PuTTY proper, and (b) SSH-1 is on the way out anyway so
         * it shouldn't be highly significant. If it seriously
         * bothers anyone then perhaps I _might_ be persuadable.
         */
        setup_passphrases("sponge", NULL);
        test(0, "puttygen", "-C", "new-comment", filename, NULL);

        /*
         * Change the passphrase to nothing.
         */
        setup_passphrases("sponge", "", "", NULL);
        test(0, "puttygen", "-P", filename, NULL);

        /*
         * Change the comment of the key again; this time we expect no
         * passphrase to be required.
         */
        setup_passphrases(NULL);
        test(0, "puttygen", "-C", "new-comment-2", filename, NULL);

        /*
         * Export the private key into OpenSSH format; no passphrase
         * should be required since the key is currently unencrypted.
         */
        setup_passphrases(NULL);
        test(supports_openssh ? 0 : 1,
             "puttygen", "-O", "private-openssh", "-o", osfilename,
             filename, NULL);

        if (supports_openssh) {
            /*
             * List the fingerprint of the OpenSSH-formatted key.
             */
            setup_passphrases(NULL);
            test(0, "puttygen", "-l", osfilename, "-o", tmpfilename1, NULL);
            check_fp(tmpfilename1, fps[SSH_FPTYPE_DEFAULT],
                     "%s openssh clear fp", keytype->name);

            /*
             * List the public half of the OpenSSH-formatted key in
             * OpenSSH format.
             */
            setup_passphrases(NULL);
            test(0, "puttygen", "-L", osfilename, NULL);

            /*
             * List the public half of the OpenSSH-formatted key in
             * IETF/ssh.com format.
             */
            setup_passphrases(NULL);
            test(0, "puttygen", "-p", osfilename, NULL);
        }

        /*
         * Export the private key into ssh.com format; no passphrase
         * should be required since the key is currently unencrypted.
         */
        setup_passphrases(NULL);
        test(supports_sshcom ? 0 : 1,
             "puttygen", "-O", "private-sshcom",
             "-o", scfilename, filename, NULL);

        if (supports_sshcom) {
            /*
             * List the fingerprint of the ssh.com-formatted key.
             */
            setup_passphrases(NULL);
            test(0, "puttygen", "-l", scfilename, "-o", tmpfilename1, NULL);
            check_fp(tmpfilename1, fps[SSH_FPTYPE_DEFAULT],
                     "%s ssh.com clear fp", keytype->name);

            /*
             * List the public half of the ssh.com-formatted key in
             * OpenSSH format.
             */
            setup_passphrases(NULL);
            test(0, "puttygen", "-L", scfilename, NULL);

            /*
             * List the public half of the ssh.com-formatted key in
             * IETF/ssh.com format.
             */
            setup_passphrases(NULL);
            test(0, "puttygen", "-p", scfilename, NULL);
        }

        if (supports_openssh && supports_sshcom) {
            /*
             * Convert from OpenSSH into ssh.com.
             */
            setup_passphrases(NULL);
            test(0, "puttygen", osfilename, "-o", tmpfilename1,
                 "-O", "private-sshcom", NULL);

            /*
             * Convert from ssh.com back into a PuTTY key,
             * supplying the same comment as we had before we
             * started to ensure the comparison works.
             */
            setup_passphrases(NULL);
            test(0, "puttygen", tmpfilename1, "-C", "new-comment-2",
                 "-o", tmpfilename2, NULL);

            /*
             * See if the PuTTY key thus generated is the same as
             * the original.
             */
            filecmp(filename, tmpfilename2,
                    "p->o->s->p clear %s", keytype->name);

            /*
             * Convert from ssh.com to OpenSSH.
             */
            setup_passphrases(NULL);
            test(0, "puttygen", scfilename, "-o", tmpfilename1,
                 "-O", "private-openssh", NULL);

            /*
             * Convert from OpenSSH back into a PuTTY key,
             * supplying the same comment as we had before we
             * started to ensure the comparison works.
             */
            setup_passphrases(NULL);
            test(0, "puttygen", tmpfilename1, "-C", "new-comment-2",
                 "-o", tmpfilename2, NULL);

            /*
             * See if the PuTTY key thus generated is the same as
             * the original.
             */
            filecmp(filename, tmpfilename2,
                    "p->s->o->p clear %s", keytype->name);

            /*
             * Finally, do a round-trip conversion between PuTTY
             * and ssh.com without involving OpenSSH, to test that
             * the key comment is preserved in that case.
             */
            setup_passphrases(NULL);
            test(0, "puttygen", "-O", "private-sshcom", "-o", tmpfilename1,
                 filename, NULL);
            setup_passphrases(NULL);
            test(0, "puttygen", tmpfilename1, "-o", tmpfilename2, NULL);
            filecmp(filename, tmpfilename2,
                    "p->s->p clear %s", keytype->name);
        }

        /*
         * Check that mismatched passphrases cause an error.
         */
        setup_passphrases("sponge2", "sponge3", NULL);
        test(1, "puttygen", "-P", filename, NULL);

        /*
         * Put a passphrase back on.
         */
        setup_passphrases("sponge2", "sponge2", NULL);
        test(0, "puttygen", "-P", filename, NULL);

        /*
         * Export the private key into OpenSSH format, this time
         * while encrypted.
         */
        if (!supports_openssh && type_known_early) {
            /* We'll know far enough in advance that this combination
             * is going to fail that we never ask for the passphrase */
            setup_passphrases(NULL);
        } else {
            setup_passphrases("sponge2", NULL);
        }

        test(supports_openssh ? 0 : 1,
             "puttygen", "-O", "private-openssh", "-o", osfilename,
             filename, NULL);

        if (supports_openssh) {
            /*
             * List the fingerprint of the OpenSSH-formatted key.
             */
            setup_passphrases("sponge2", NULL);
            test(0, "puttygen", "-l", osfilename, "-o", tmpfilename1, NULL);
            check_fp(tmpfilename1, fps[SSH_FPTYPE_DEFAULT],
                     "%s openssh encrypted fp", keytype->name);

            /*
             * List the public half of the OpenSSH-formatted key in
             * OpenSSH format.
             */
            setup_passphrases("sponge2", NULL);
            test(0, "puttygen", "-L", osfilename, NULL);

            /*
             * List the public half of the OpenSSH-formatted key in
             * IETF/ssh.com format.
             */
            setup_passphrases("sponge2", NULL);
            test(0, "puttygen", "-p", osfilename, NULL);
        }

        /*
         * Export the private key into ssh.com format, this time
         * while encrypted. For RSA1 keys, this should give an
         * error.
         */
        if (!supports_sshcom && type_known_early) {
            /* We'll know far enough in advance that this combination
             * is going to fail that we never ask for the passphrase */
            setup_passphrases(NULL);
        } else {
            setup_passphrases("sponge2", NULL);
        }

        test(supports_sshcom ? 0 : 1,
             "puttygen", "-O", "private-sshcom", "-o", scfilename,
             filename, NULL);

        if (supports_sshcom) {
            /*
             * List the fingerprint of the ssh.com-formatted key.
             */
            setup_passphrases("sponge2", NULL);
            test(0, "puttygen", "-l", scfilename, "-o", tmpfilename1, NULL);
            check_fp(tmpfilename1, fps[SSH_FPTYPE_DEFAULT],
                     "%s ssh.com encrypted fp", keytype->name);

            /*
             * List the public half of the ssh.com-formatted key in
             * OpenSSH format.
             */
            setup_passphrases("sponge2", NULL);
            test(0, "puttygen", "-L", scfilename, NULL);

            /*
             * List the public half of the ssh.com-formatted key in
             * IETF/ssh.com format.
             */
            setup_passphrases("sponge2", NULL);
            test(0, "puttygen", "-p", scfilename, NULL);
        }

        if (supports_openssh && supports_sshcom) {
            /*
             * Convert from OpenSSH into ssh.com.
             */
            setup_passphrases("sponge2", NULL);
            test(0, "puttygen", osfilename, "-o", tmpfilename1,
                 "-O", "private-sshcom", NULL);

            /*
             * Convert from ssh.com back into a PuTTY key,
             * supplying the same comment as we had before we
             * started to ensure the comparison works.
             */
            setup_passphrases("sponge2", NULL);
            test(0, "puttygen", tmpfilename1, "-C", "new-comment-2",
                 "-o", tmpfilename2, NULL);

            /*
             * See if the PuTTY key thus generated is the same as
             * the original.
             */
            filecmp(filename, tmpfilename2,
                    "p->o->s->p encrypted %s", keytype->name);

            /*
             * Convert from ssh.com to OpenSSH.
             */
            setup_passphrases("sponge2", NULL);
            test(0, "puttygen", scfilename, "-o", tmpfilename1,
                 "-O", "private-openssh", NULL);

            /*
             * Convert from OpenSSH back into a PuTTY key,
             * supplying the same comment as we had before we
             * started to ensure the comparison works.
             */
            setup_passphrases("sponge2", NULL);
            test(0, "puttygen", tmpfilename1, "-C", "new-comment-2",
                 "-o", tmpfilename2, NULL);

            /*
             * See if the PuTTY key thus generated is the same as
             * the original.
             */
            filecmp(filename, tmpfilename2,
                    "p->s->o->p encrypted %s", keytype->name);

            /*
             * Finally, do a round-trip conversion between PuTTY
             * and ssh.com without involving OpenSSH, to test that
             * the key comment is preserved in that case.
             */
            setup_passphrases("sponge2", NULL);
            test(0, "puttygen", "-O", "private-sshcom", "-o", tmpfilename1,
                 filename, NULL);
            setup_passphrases("sponge2", NULL);
            test(0, "puttygen", tmpfilename1, "-o", tmpfilename2, NULL);
            filecmp(filename, tmpfilename2,
                    "p->s->p encrypted %s", keytype->name);
        }

        /*
         * Load with the wrong passphrase.
         */
        setup_passphrases("sponge8", NULL);
        test(1, "puttygen", "-C", "spurious-new-comment", filename, NULL);

        /*
         * Load a totally bogus file.
         */
        setup_passphrases(NULL);
        test(1, "puttygen", "-C", "spurious-new-comment", pubfilename, NULL);

        for (FingerprintType fptype = 0; fptype < SSH_N_FPTYPES; fptype++)
            sfree(fps[fptype]);

        if (remove_files) {
            remove(filename);
            remove(pubfilename);
            remove(osfilename);
            remove(scfilename);
            remove(tmpfilename1);
            remove(tmpfilename2);
        }
    }
    printf("%d passes, %d fails\n", passes, fails);
    return fails == 0 ? 0 : 1;
}
