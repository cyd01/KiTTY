/*
 * cmdgen.c - command-line form of PuTTYgen
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#include "putty.h"
#include "ssh.h"
#include "sshkeygen.h"
#include "mpint.h"

static FILE *progress_fp = NULL;
static bool linear_progress_phase;
static unsigned last_progress_col;

static ProgressPhase cmdgen_progress_add_linear(
    ProgressReceiver *prog, double c)
{
    ProgressPhase ph = { .n = 0 };
    return ph;
}

static ProgressPhase cmdgen_progress_add_probabilistic(
    ProgressReceiver *prog, double c, double p)
{
    ProgressPhase ph = { .n = 1 };
    return ph;
}

static void cmdgen_progress_start_phase(ProgressReceiver *prog,
                                        ProgressPhase p)
{
    linear_progress_phase = (p.n == 0);
    last_progress_col = 0;
}
static void cmdgen_progress_report(ProgressReceiver *prog, double p)
{
    unsigned new_col = p * 64 + 0.5;
    for (; last_progress_col < new_col; last_progress_col++)
        fputc('+', progress_fp);
}
static void cmdgen_progress_report_attempt(ProgressReceiver *prog)
{
    if (progress_fp) {
        fputc('+', progress_fp);
        fflush(progress_fp);
    }
}
static void cmdgen_progress_report_phase_complete(ProgressReceiver *prog)
{
    if (linear_progress_phase)
        cmdgen_progress_report(prog, 1.0);
    if (progress_fp) {
        fputc('\n', progress_fp);
        fflush(progress_fp);
    }
}

static const ProgressReceiverVtable cmdgen_progress_vt = {
    .add_linear = cmdgen_progress_add_linear,
    .add_probabilistic = cmdgen_progress_add_probabilistic,
    .ready = null_progress_ready,
    .start_phase = cmdgen_progress_start_phase,
    .report = cmdgen_progress_report,
    .report_attempt = cmdgen_progress_report_attempt,
    .report_phase_complete = cmdgen_progress_report_phase_complete,
};

static ProgressReceiver cmdgen_progress = { .vt = &cmdgen_progress_vt };

/*
 * Stubs to let everything else link sensibly.
 */
char *x_get_default(const char *key)
{
    return NULL;
}
void sk_cleanup(void)
{
}

void showversion(void)
{
    char *buildinfo_text = buildinfo("\n");
    printf("puttygen: %s\n%s\n", ver, buildinfo_text);
    sfree(buildinfo_text);
}

void usage(bool standalone)
{
    fprintf(standalone ? stderr : stdout,
            "Usage: puttygen ( keyfile | -t type [ -b bits ] )\n"
            "                [ -C comment ] [ -P ] [ -q ]\n"
            "                [ -o output-keyfile ] [ -O type | -l | -L"
            " | -p ]\n");
    if (standalone)
        fprintf(stderr,
                "Use \"puttygen --help\" for more detail.\n");
}

void help(void)
{
    /*
     * Help message is an extended version of the usage message. So
     * start with that, plus a version heading.
     */
    printf("PuTTYgen: key generator and converter for the PuTTY tools\n"
           "%s\n", ver);
    usage(false);
    printf("  -t    specify key type when generating:\n"
           "           eddsa, ecdsa, rsa, dsa, rsa1   use with -b\n"
           "           ed25519, ed448                 special cases of eddsa\n"
           "  -b    specify number of bits when generating key\n"
           "  -C    change or specify key comment\n"
           "  -P    change key passphrase\n"
           "  -q    quiet: do not display progress bar\n"
           "  -O    specify output type:\n"
           "           private             output PuTTY private key format\n"
           "           private-openssh     export OpenSSH private key\n"
           "           private-openssh-new export OpenSSH private key "
                                             "(force new format)\n"
           "           private-sshcom      export ssh.com private key\n"
           "           public              RFC 4716 / ssh.com public key\n"
           "           public-openssh      OpenSSH public key\n"
           "           fingerprint         output the key fingerprint\n"
           "           text                output the key components as "
           "'name=0x####'\n"
           "  -o    specify output file\n"
           "  -l    equivalent to `-O fingerprint'\n"
           "  -L    equivalent to `-O public-openssh'\n"
           "  -p    equivalent to `-O public'\n"
           "  --dump   equivalent to `-O text'\n"
           "  --reencrypt          load a key and save it with fresh "
           "encryption\n"
           "  --old-passphrase file\n"
           "        specify file containing old key passphrase\n"
           "  --new-passphrase file\n"
           "        specify file containing new key passphrase\n"
           "  --random-device device\n"
           "        specify device to read entropy from (e.g. /dev/urandom)\n"
           "  --primes <type>      select prime-generation method:\n"
           "        probable       conventional probabilistic prime finding\n"
           "        proven         numbers that have been proven to be prime\n"
           "        proven-even    also try harder for an even distribution\n"
           "  --strong-rsa         use \"strong\" primes as RSA key factors\n"
           "  --ppk-param <key>=<value>[,<key>=<value>,...]\n"
           "        specify parameters when writing PuTTY private key file "
           "format:\n"
           "            version       PPK format version (min 2, max 3, "
           "default 3)\n"
           "            kdf           key derivation function (argon2id, "
           "argon2i, argon2d)\n"
           "            memory        Kbyte of memory to use in passphrase "
           "hash\n"
           "                             (default 8192)\n"
           "            time          approx milliseconds to hash for "
           "(default 100)\n"
           "            passes        number of hash passes to run "
           "(alternative to 'time')\n"
           "            parallelism   number of parallelisable threads in the "
           "hash function\n"
           "                             (default 1)\n"
           );
}

static bool move(char *from, char *to)
{
    int ret;

    ret = rename(from, to);
    if (ret) {
        /*
         * This OS may require us to remove the original file first.
         */
        remove(to);
        ret = rename(from, to);
    }
    if (ret) {
        perror("puttygen: cannot move new file on to old one");
        return false;
    }
    return true;
}

static char *readpassphrase(const char *filename)
{
    FILE *fp;
    char *line;

    fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "puttygen: cannot open %s: %s\n",
                filename, strerror(errno));
        return NULL;
    }
    line = fgetline(fp);
    if (line)
        line[strcspn(line, "\r\n")] = '\0';
    else if (ferror(fp))
        fprintf(stderr, "puttygen: error reading from %s: %s\n",
                filename, strerror(errno));
    else        /* empty file */
        line = dupstr("");
    fclose(fp);
    return line;
}

#define DEFAULT_RSADSA_BITS 2048

/* For Unix in particular, but harmless if this main() is reused elsewhere */
const bool buildinfo_gtk_relevant = false;

int main(int argc, char **argv)
{
    char *infile = NULL;
    Filename *infilename = NULL, *outfilename = NULL;
    LoadedFile *infile_lf = NULL;
    BinarySource *infile_bs = NULL;
    enum { NOKEYGEN, RSA1, RSA2, DSA, ECDSA, EDDSA } keytype = NOKEYGEN;
    char *outfile = NULL, *outfiletmp = NULL;
    enum { PRIVATE, PUBLIC, PUBLICO, FP, OPENSSH_AUTO,
           OPENSSH_NEW, SSHCOM, TEXT } outtype = PRIVATE;
    int bits = -1;
    const char *comment = NULL;
    char *origcomment = NULL;
    bool change_passphrase = false, reencrypt = false;
    bool errs = false, nogo = false;
    int intype = SSH_KEYTYPE_UNOPENABLE;
    int sshver = 0;
    ssh2_userkey *ssh2key = NULL;
    RSAKey *ssh1key = NULL;
    strbuf *ssh2blob = NULL;
    char *ssh2alg = NULL;
    char *old_passphrase = NULL, *new_passphrase = NULL;
    bool load_encrypted;
    const char *random_device = NULL;
    int exit_status = 0;
    const PrimeGenerationPolicy *primegen = &primegen_probabilistic;
    bool strong_rsa = false;
    ppk_save_parameters params = ppk_save_default_parameters;
    FingerprintType fptype = SSH_FPTYPE_DEFAULT;

    if (is_interactive())
        progress_fp = stderr;

    #define RETURN(status) do { exit_status = (status); goto out; } while (0)

    /* ------------------------------------------------------------------
     * Parse the command line to figure out what we've been asked to do.
     */

    /*
     * If run with no arguments at all, print the usage message and
     * return success.
     */
    if (argc <= 1) {
        usage(true);
        RETURN(0);
    }

    /*
     * Parse command line arguments.
     */
    while (--argc) {
        char *p = *++argv;
        if (p[0] == '-' && p[1]) {
            /*
             * An option.
             */
            while (p && *++p) {
                char c = *p;
                switch (c) {
                  case '-': {
                    /*
                     * Long option.
                     */
                    char *opt, *val;
                    opt = p++;     /* opt will have _one_ leading - */
                    while (*p && *p != '=')
                        p++;               /* find end of option */
                    if (*p == '=') {
                      *p++ = '\0';
                      val = p;
                    } else
                        val = NULL;

                    if (!strcmp(opt, "-help")) {
                      if (val) {
                        errs = true;
                        fprintf(stderr, "puttygen: option `-%s'"
                                " expects no argument\n", opt);
                      } else {
                        help();
                        nogo = true;
                      }
                    } else if (!strcmp(opt, "-version")) {
                      if (val) {
                        errs = true;
                        fprintf(stderr, "puttygen: option `-%s'"
                                " expects no argument\n", opt);
                      } else {
                        showversion();
                        nogo = true;
                      }
                    } else if (!strcmp(opt, "-pgpfp")) {
                      if (val) {
                        errs = true;
                        fprintf(stderr, "puttygen: option `-%s'"
                                " expects no argument\n", opt);
                      } else {
                        /* support --pgpfp for consistency */
                        pgp_fingerprints();
                        nogo = true;
                      }
                    } else if (!strcmp(opt, "-old-passphrase")) {
                      if (!val && argc > 1)
                          --argc, val = *++argv;
                      if (!val) {
                        errs = true;
                        fprintf(stderr, "puttygen: option `-%s'"
                                " expects an argument\n", opt);
                      } else {
                        old_passphrase = readpassphrase(val);
                        if (!old_passphrase)
                            errs = true;
                      }
                    } else if (!strcmp(opt, "-new-passphrase")) {
                      if (!val && argc > 1)
                          --argc, val = *++argv;
                      if (!val) {
                        errs = true;
                        fprintf(stderr, "puttygen: option `-%s'"
                                " expects an argument\n", opt);
                      } else {
                        new_passphrase = readpassphrase(val);
                        if (!new_passphrase)
                            errs = true;
                      }
                    } else if (!strcmp(opt, "-random-device")) {
                      if (!val && argc > 1)
                          --argc, val = *++argv;
                      if (!val) {
                        errs = true;
                        fprintf(stderr, "puttygen: option `-%s'"
                                " expects an argument\n", opt);
                      } else {
                        random_device = val;
                      }
                    } else if (!strcmp(opt, "-dump")) {
                        outtype = TEXT;
                    } else if (!strcmp(opt, "-primes")) {
                        if (!val && argc > 1)
                            --argc, val = *++argv;
                        if (!val) {
                            errs = true;
                            fprintf(stderr, "puttygen: option `-%s'"
                                    " expects an argument\n", opt);
                        } else if (!strcmp(val, "probable") ||
                                   !strcmp(val, "probabilistic")) {
                            primegen = &primegen_probabilistic;
                        } else if (!strcmp(val, "provable") ||
                                   !strcmp(val, "proven") ||
                                   !strcmp(val, "simple") ||
                                   !strcmp(val, "maurer-simple")) {
                            primegen = &primegen_provable_maurer_simple;
                        } else if (!strcmp(val, "provable-even") ||
                                   !strcmp(val, "proven-even") ||
                                   !strcmp(val, "even") ||
                                   !strcmp(val, "complex") ||
                                   !strcmp(val, "maurer-complex")) {
                            primegen = &primegen_provable_maurer_complex;
                        } else {
                            errs = true;
                            fprintf(stderr, "puttygen: unrecognised prime-"
                                    "generation mode `%s'\n", val);
                        }
                    } else if (!strcmp(opt, "-strong-rsa")) {
                        strong_rsa = true;
                    } else if (!strcmp(opt, "-reencrypt")) {
                        reencrypt = true;
                    } else if (!strcmp(opt, "-ppk-param") ||
                               !strcmp(opt, "-ppk-params")) {
                        if (!val && argc > 1)
                            --argc, val = *++argv;
                        if (!val) {
                            errs = true;
                            fprintf(stderr, "puttygen: option `-%s'"
                                    " expects an argument\n", opt);
                        } else {
                            char *nextval;
                            for (; val; val = nextval) {
                                nextval = strchr(val, ',');
                                if (nextval)
                                    *nextval++ = '\0';

                                char *optvalue = strchr(val, '=');
                                if (!optvalue) {
                                    errs = true;
                                    fprintf(stderr, "puttygen: PPK parameter "
                                            "'%s' expected a value\n", val);
                                    continue;
                                }
                                *optvalue++ = '\0';

                                /* Non-numeric options */
                                if (!strcmp(val, "kdf")) {
                                    if (!strcmp(optvalue, "Argon2id") ||
                                        !strcmp(optvalue, "argon2id")) {
                                        params.argon2_flavour = Argon2id;
                                    } else if (!strcmp(optvalue, "Argon2i") ||
                                               !strcmp(optvalue, "argon2i")) {
                                        params.argon2_flavour = Argon2i;
                                    } else if (!strcmp(optvalue, "Argon2d") ||
                                               !strcmp(optvalue, "argon2d")) {
                                        params.argon2_flavour = Argon2d;
                                    } else {
                                        errs = true;
                                        fprintf(stderr, "puttygen: unrecognise"
                                                "d kdf '%s'\n", optvalue);
                                    }
                                    continue;
                                }

                                char *end;
                                unsigned long n = strtoul(optvalue, &end, 0);
                                if (!*optvalue || *end) {
                                    errs = true;
                                    fprintf(stderr, "puttygen: value '%s' for "
                                            "PPK parameter '%s': expected a "
                                            "number\n", optvalue, val);
                                    continue;
                                }

                                if (!strcmp(val, "version")) {
                                    params.fmt_version = n;
                                } else if (!strcmp(val, "memory") ||
                                           !strcmp(val, "mem")) {
                                    params.argon2_mem = n;
                                } else if (!strcmp(val, "time")) {
                                    params.argon2_passes_auto = true;
                                    params.argon2_milliseconds = n;
                                } else if (!strcmp(val, "passes")) {
                                    params.argon2_passes_auto = false;
                                    params.argon2_passes = n;
                                } else if (!strcmp(val, "parallelism") ||
                                           !strcmp(val, "parallel")) {
                                    params.argon2_parallelism = n;
                                } else {
                                    errs = true;
                                    fprintf(stderr, "puttygen: unrecognised "
                                            "PPK parameter '%s'\n", val);
                                    continue;
                                }
                            }
                        }
                    } else {
                      errs = true;
                      fprintf(stderr,
                              "puttygen: no such option `-%s'\n", opt);
                    }
                    p = NULL;
                    break;
                  }
                  case 'h':
                  case 'V':
                  case 'P':
                  case 'l':
                  case 'L':
                  case 'p':
                  case 'q':
                    /*
                     * Option requiring no parameter.
                     */
                    switch (c) {
                      case 'h':
                        help();
                        nogo = true;
                        break;
                      case 'V':
                        showversion();
                        nogo = true;
                        break;
                      case 'P':
                        change_passphrase = true;
                        break;
                      case 'l':
                        outtype = FP;
                        break;
                      case 'L':
                        outtype = PUBLICO;
                        break;
                      case 'p':
                        outtype = PUBLIC;
                        break;
                      case 'q':
                        progress_fp = NULL;
                        break;
                    }
                    break;
                  case 't':
                  case 'b':
                  case 'C':
                  case 'O':
                  case 'o':
                  case 'E':
                    /*
                     * Option requiring parameter.
                     */
                    p++;
                    if (!*p && argc > 1)
                        --argc, p = *++argv;
                    else if (!*p) {
                        fprintf(stderr, "puttygen: option `-%c' expects a"
                                " parameter\n", c);
                        errs = true;
                    }
                    /*
                     * Now c is the option and p is the parameter.
                     */
                    switch (c) {
                      case 't':
                        if (!strcmp(p, "rsa") || !strcmp(p, "rsa2"))
                            keytype = RSA2, sshver = 2;
                        else if (!strcmp(p, "rsa1"))
                            keytype = RSA1, sshver = 1;
                        else if (!strcmp(p, "dsa") || !strcmp(p, "dss"))
                            keytype = DSA, sshver = 2;
                        else if (!strcmp(p, "ecdsa"))
                            keytype = ECDSA, sshver = 2;
                        else if (!strcmp(p, "eddsa"))
                            keytype = EDDSA, sshver = 2;
                        else if (!strcmp(p, "ed25519"))
                            keytype = EDDSA, bits = 255, sshver = 2;
                        else if (!strcmp(p, "ed448"))
                            keytype = EDDSA, bits = 448, sshver = 2;
                        else {
                            fprintf(stderr,
                                    "puttygen: unknown key type `%s'\n", p);
                            errs = true;
                        }
                        break;
                      case 'b':
                        bits = atoi(p);
                        break;
                      case 'C':
                        comment = p;
                        break;
                      case 'O':
                        if (!strcmp(p, "public"))
                            outtype = PUBLIC;
                        else if (!strcmp(p, "public-openssh"))
                            outtype = PUBLICO;
                        else if (!strcmp(p, "private"))
                            outtype = PRIVATE;
                        else if (!strcmp(p, "fingerprint"))
                            outtype = FP;
                        else if (!strcmp(p, "private-openssh"))
                            outtype = OPENSSH_AUTO, sshver = 2;
                        else if (!strcmp(p, "private-openssh-new"))
                            outtype = OPENSSH_NEW, sshver = 2;
                        else if (!strcmp(p, "private-sshcom"))
                            outtype = SSHCOM, sshver = 2;
                        else if (!strcmp(p, "text"))
                            outtype = TEXT;
                        else {
                            fprintf(stderr,
                                    "puttygen: unknown output type `%s'\n", p);
                            errs = true;
                        }
                        break;
                      case 'o':
                        outfile = p;
                        break;
                      case 'E':
                        if (!strcmp(p, "md5"))
                            fptype = SSH_FPTYPE_MD5;
                        else if (!strcmp(p, "sha256"))
                            fptype = SSH_FPTYPE_SHA256;
                        else {
                            fprintf(stderr, "puttygen: unknown fingerprint "
                                    "type `%s'\n", p);
                            errs = true;
                        }
                        break;
                    }
                    p = NULL;          /* prevent continued processing */
                    break;
                  default:
                    /*
                     * Unrecognised option.
                     */
                    errs = true;
                    fprintf(stderr, "puttygen: no such option `-%c'\n", c);
                    break;
                }
            }
        } else {
            /*
             * A non-option argument.
             */
            if (!infile)
                infile = p;
            else {
                errs = true;
                fprintf(stderr, "puttygen: cannot handle more than one"
                        " input file\n");
            }
        }
    }

    if (bits == -1) {
        /*
         * No explicit key size was specified. Default varies
         * depending on key type.
         */
        switch (keytype) {
          case ECDSA:
            bits = 384;
            break;
          case EDDSA:
            bits = 255;
            break;
          default:
            bits = DEFAULT_RSADSA_BITS;
            break;
        }
    }

    if (keytype == ECDSA || keytype == EDDSA) {
        const char *name = (keytype == ECDSA ? "ECDSA" : "EdDSA");
        const int *valid_lengths = (keytype == ECDSA ? ec_nist_curve_lengths :
                                    ec_ed_curve_lengths);
        size_t n_lengths = (keytype == ECDSA ? n_ec_nist_curve_lengths :
                            n_ec_ed_curve_lengths);
        bool (*alg_and_curve_by_bits)(int, const struct ec_curve **,
                                      const ssh_keyalg **) =
            (keytype == ECDSA ? ec_nist_alg_and_curve_by_bits :
             ec_ed_alg_and_curve_by_bits);

        const struct ec_curve *curve;
        const ssh_keyalg *alg;

        if (!alg_and_curve_by_bits(bits, &curve, &alg)) {
            fprintf(stderr, "puttygen: invalid bits for %s, choose", name);
            for (size_t i = 0; i < n_lengths; i++)
                fprintf(stderr, "%s%d", (i == 0 ? " " :
                                         i == n_lengths-1 ? " or " : ", "),
                        valid_lengths[i]);
            fputc('\n', stderr);
            errs = true;
        }
    }

    if (keytype == RSA2 || keytype == RSA1 || keytype == DSA) {
        if (bits < 256) {
            fprintf(stderr, "puttygen: cannot generate %s keys shorter than"
                    " 256 bits\n", (keytype == DSA ? "DSA" : "RSA"));
            errs = true;
        } else if (bits < DEFAULT_RSADSA_BITS) {
            fprintf(stderr, "puttygen: warning: %s keys shorter than"
                    " %d bits are probably not secure\n",
                    (keytype == DSA ? "DSA" : "RSA"), DEFAULT_RSADSA_BITS);
            /* but this is just a warning, so proceed anyway */
        }
    }

    if (errs)
        RETURN(1);

    if (nogo)
        RETURN(0);

    /*
     * If run with at least one argument _but_ not the required
     * ones, print the usage message and return failure.
     */
    if (!infile && keytype == NOKEYGEN) {
        usage(true);
        RETURN(1);
    }

    /* ------------------------------------------------------------------
     * Figure out further details of exactly what we're going to do.
     */

    /*
     * Bomb out if we've been asked to both load and generate a
     * key.
     */
    if (keytype != NOKEYGEN && infile) {
        fprintf(stderr, "puttygen: cannot both load and generate a key\n");
        RETURN(1);
    }

    /*
     * We must save the private part when generating a new key.
     */
    if (keytype != NOKEYGEN &&
        (outtype != PRIVATE && outtype != OPENSSH_AUTO &&
         outtype != OPENSSH_NEW && outtype != SSHCOM && outtype != TEXT)) {
        fprintf(stderr, "puttygen: this would generate a new key but "
                "discard the private part\n");
        RETURN(1);
    }

    /*
     * Analyse the type of the input file, in case this affects our
     * course of action.
     */
    if (infile) {
        const char *load_error;

        infilename = filename_from_str(infile);
        if (!strcmp(infile, "-"))
            infile_lf = lf_load_keyfile_fp(stdin, &load_error);
        else
            infile_lf = lf_load_keyfile(infilename, &load_error);

        if (!infile_lf) {
            fprintf(stderr, "puttygen: unable to load file `%s': %s\n",
                    infile, load_error);
            RETURN(1);
        }

        infile_bs = BinarySource_UPCAST(infile_lf);
        intype = key_type_s(infile_bs);
        BinarySource_REWIND(infile_bs);

        switch (intype) {
          case SSH_KEYTYPE_UNOPENABLE:
          case SSH_KEYTYPE_UNKNOWN:
            fprintf(stderr, "puttygen: unable to load file `%s': %s\n",
                    infile, key_type_to_str(intype));
            RETURN(1);

          case SSH_KEYTYPE_SSH1:
          case SSH_KEYTYPE_SSH1_PUBLIC:
            if (sshver == 2) {
                fprintf(stderr, "puttygen: conversion from SSH-1 to SSH-2 keys"
                        " not supported\n");
                RETURN(1);
            }
            sshver = 1;
            break;

          case SSH_KEYTYPE_SSH2:
          case SSH_KEYTYPE_SSH2_PUBLIC_RFC4716:
          case SSH_KEYTYPE_SSH2_PUBLIC_OPENSSH:
          case SSH_KEYTYPE_OPENSSH_PEM:
          case SSH_KEYTYPE_OPENSSH_NEW:
          case SSH_KEYTYPE_SSHCOM:
            if (sshver == 1) {
                fprintf(stderr, "puttygen: conversion from SSH-2 to SSH-1 keys"
                        " not supported\n");
                RETURN(1);
            }
            sshver = 2;
            break;

          case SSH_KEYTYPE_OPENSSH_AUTO:
          default:
            unreachable("Should never see these types on an input file");
        }
    }

    /*
     * Determine the default output file, if none is provided.
     *
     * This will usually be equal to stdout, except that if the
     * input and output file formats are the same then the default
     * output is to overwrite the input.
     *
     * Also in this code, we bomb out if the input and output file
     * formats are the same and no other action is performed.
     */
    if ((intype == SSH_KEYTYPE_SSH1 && outtype == PRIVATE) ||
        (intype == SSH_KEYTYPE_SSH2 && outtype == PRIVATE) ||
        (intype == SSH_KEYTYPE_OPENSSH_PEM && outtype == OPENSSH_AUTO) ||
        (intype == SSH_KEYTYPE_OPENSSH_NEW && outtype == OPENSSH_NEW) ||
        (intype == SSH_KEYTYPE_SSHCOM && outtype == SSHCOM)) {
        if (!outfile) {
            outfile = infile;
            outfiletmp = dupcat(outfile, ".tmp");
        }

        if (!change_passphrase && !comment && !reencrypt) {
            fprintf(stderr, "puttygen: this command would perform no useful"
                    " action\n");
            RETURN(1);
        }
    } else {
        if (!outfile) {
            /*
             * Bomb out rather than automatically choosing to write
             * a private key file to stdout.
             */
            if (outtype == PRIVATE || outtype == OPENSSH_AUTO ||
                outtype == OPENSSH_NEW || outtype == SSHCOM) {
                fprintf(stderr, "puttygen: need to specify an output file\n");
                RETURN(1);
            }
        }
    }

    /*
     * Figure out whether we need to load the encrypted part of the
     * key. This will be the case if (a) we need to write out
     * a private key format, (b) the entire input key file is
     * encrypted, or (c) we're outputting TEXT, in which case we
     * want all of the input file including private material if it
     * exists.
     */
    bool intype_entirely_encrypted =
        intype == SSH_KEYTYPE_OPENSSH_PEM ||
        intype == SSH_KEYTYPE_OPENSSH_NEW ||
        intype == SSH_KEYTYPE_SSHCOM;
    bool intype_has_private =
        !(intype == SSH_KEYTYPE_SSH1_PUBLIC ||
          intype == SSH_KEYTYPE_SSH2_PUBLIC_RFC4716 ||
          intype == SSH_KEYTYPE_SSH2_PUBLIC_OPENSSH);
    bool outtype_has_private =
        outtype == PRIVATE || outtype == OPENSSH_AUTO ||
        outtype == OPENSSH_NEW || outtype == SSHCOM;
    if (outtype_has_private || intype_entirely_encrypted ||
        (outtype == TEXT && intype_has_private))
        load_encrypted = true;
    else
        load_encrypted = false;

    if (load_encrypted && !intype_has_private) {
        fprintf(stderr, "puttygen: cannot perform this action on a "
                "public-key-only input file\n");
        RETURN(1);
    }

    /* ------------------------------------------------------------------
     * Now we're ready to actually do some stuff.
     */

    /*
     * Either load or generate a key.
     */
    if (keytype != NOKEYGEN) {
        char *entropy;
        char default_comment[80];
        struct tm tm;

        tm = ltime();
        if (keytype == DSA)
            strftime(default_comment, 30, "dsa-key-%Y%m%d", &tm);
        else if (keytype == ECDSA)
            strftime(default_comment, 30, "ecdsa-key-%Y%m%d", &tm);
        else if (keytype == EDDSA && bits == 255)
            strftime(default_comment, 30, "ed25519-key-%Y%m%d", &tm);
        else if (keytype == EDDSA)
            strftime(default_comment, 30, "eddsa-key-%Y%m%d", &tm);
        else
            strftime(default_comment, 30, "rsa-key-%Y%m%d", &tm);

        entropy = get_random_data(bits / 8, random_device);
        if (!entropy) {
            fprintf(stderr, "puttygen: failed to collect entropy, "
                    "could not generate key\n");
            RETURN(1);
        }
        random_setup_special();
        random_reseed(make_ptrlen(entropy, bits / 8));
        smemclr(entropy, bits/8);
        sfree(entropy);

        PrimeGenerationContext *pgc = primegen_new_context(primegen);

        if (keytype == DSA) {
            struct dss_key *dsskey = snew(struct dss_key);
            dsa_generate(dsskey, bits, pgc, &cmdgen_progress);
            ssh2key = snew(ssh2_userkey);
            ssh2key->key = &dsskey->sshk;
            ssh1key = NULL;
        } else if (keytype == ECDSA) {
            struct ecdsa_key *ek = snew(struct ecdsa_key);
            ecdsa_generate(ek, bits);
            ssh2key = snew(ssh2_userkey);
            ssh2key->key = &ek->sshk;
            ssh1key = NULL;
        } else if (keytype == EDDSA) {
            struct eddsa_key *ek = snew(struct eddsa_key);
            eddsa_generate(ek, bits);
            ssh2key = snew(ssh2_userkey);
            ssh2key->key = &ek->sshk;
            ssh1key = NULL;
        } else {
            RSAKey *rsakey = snew(RSAKey);
            rsa_generate(rsakey, bits, strong_rsa, pgc, &cmdgen_progress);
            rsakey->comment = NULL;
            if (keytype == RSA1) {
                ssh1key = rsakey;
            } else {
                ssh2key = snew(ssh2_userkey);
                ssh2key->key = &rsakey->sshk;
            }
        }

        primegen_free_context(pgc);

        if (ssh2key)
            ssh2key->comment = dupstr(default_comment);
        if (ssh1key)
            ssh1key->comment = dupstr(default_comment);

    } else {
        const char *error = NULL;
        bool encrypted;

        assert(infile != NULL);

        sfree(origcomment);
        origcomment = NULL;

        /*
         * Find out whether the input key is encrypted.
         */
        if (intype == SSH_KEYTYPE_SSH1)
            encrypted = rsa1_encrypted_s(infile_bs, &origcomment);
        else if (intype == SSH_KEYTYPE_SSH2)
            encrypted = ppk_encrypted_s(infile_bs, &origcomment);
        else
            encrypted = import_encrypted_s(infilename, infile_bs,
                                           intype, &origcomment);
        BinarySource_REWIND(infile_bs);

        /*
         * If so, ask for a passphrase.
         */
        if (encrypted && load_encrypted) {
            if (!old_passphrase) {
                prompts_t *p = new_prompts();
                int ret;
                p->to_server = false;
                p->from_server = false;
                p->name = dupstr("SSH key passphrase");
                add_prompt(p, dupstr("Enter passphrase to load key: "), false);
                ret = console_get_userpass_input(p);
                assert(ret >= 0);
                if (!ret) {
                    free_prompts(p);
                    perror("puttygen: unable to read passphrase");
                    RETURN(1);
                } else {
                    old_passphrase = prompt_get_result(p->prompts[0]);
                    free_prompts(p);
                }
            }
        } else {
            old_passphrase = NULL;
        }

        switch (intype) {
            int ret;

          case SSH_KEYTYPE_SSH1:
          case SSH_KEYTYPE_SSH1_PUBLIC:
            ssh1key = snew(RSAKey);
            memset(ssh1key, 0, sizeof(RSAKey));
            if (!load_encrypted) {
                strbuf *blob;
                BinarySource src[1];

                sfree(origcomment);
                origcomment = NULL;

                blob = strbuf_new();

                ret = rsa1_loadpub_s(infile_bs, BinarySink_UPCAST(blob),
                                     &origcomment, &error);
                BinarySource_BARE_INIT(src, blob->u, blob->len);
                get_rsa_ssh1_pub(src, ssh1key, RSA_SSH1_EXPONENT_FIRST);
                strbuf_free(blob);

                ssh1key->comment = dupstr(origcomment);
                ssh1key->private_exponent = NULL;
                ssh1key->p = NULL;
                ssh1key->q = NULL;
                ssh1key->iqmp = NULL;
            } else {
                ret = rsa1_load_s(infile_bs, ssh1key, old_passphrase, &error);
            }
            BinarySource_REWIND(infile_bs);
            if (ret > 0)
                error = NULL;
            else if (!error)
                error = "unknown error";
            break;

          case SSH_KEYTYPE_SSH2:
          case SSH_KEYTYPE_SSH2_PUBLIC_RFC4716:
          case SSH_KEYTYPE_SSH2_PUBLIC_OPENSSH:
            if (!load_encrypted) {
                sfree(origcomment);
                origcomment = NULL;
                ssh2blob = strbuf_new();
                if (ppk_loadpub_s(infile_bs, &ssh2alg,
                                  BinarySink_UPCAST(ssh2blob),
                                  &origcomment, &error)) {
                    const ssh_keyalg *alg = find_pubkey_alg(ssh2alg);
                    if (alg)
                        bits = ssh_key_public_bits(
                            alg, ptrlen_from_strbuf(ssh2blob));
                    else
                        bits = -1;
                } else {
                    strbuf_free(ssh2blob);
                    ssh2blob = NULL;
                }
                sfree(ssh2alg);
            } else {
                ssh2key = ppk_load_s(infile_bs, old_passphrase, &error);
            }
            BinarySource_REWIND(infile_bs);
            if ((ssh2key && ssh2key != SSH2_WRONG_PASSPHRASE) || ssh2blob)
                error = NULL;
            else if (!error) {
                if (ssh2key == SSH2_WRONG_PASSPHRASE)
                    error = "wrong passphrase";
                else
                    error = "unknown error";
            }
            break;

          case SSH_KEYTYPE_OPENSSH_PEM:
          case SSH_KEYTYPE_OPENSSH_NEW:
          case SSH_KEYTYPE_SSHCOM:
            ssh2key = import_ssh2_s(infile_bs, intype, old_passphrase, &error);
            if (ssh2key) {
                if (ssh2key != SSH2_WRONG_PASSPHRASE)
                    error = NULL;
                else
                    error = "wrong passphrase";
            } else if (!error)
                error = "unknown error";
            break;

          default:
            unreachable("bad input key type");
        }

        if (error) {
            fprintf(stderr, "puttygen: error loading `%s': %s\n",
                    infile, error);
            RETURN(1);
        }
    }

    /*
     * Change the comment if asked to.
     */
    if (comment) {
        if (sshver == 1) {
            assert(ssh1key);
            sfree(ssh1key->comment);
            ssh1key->comment = dupstr(comment);
        } else {
            assert(ssh2key);
            sfree(ssh2key->comment);
            ssh2key->comment = dupstr(comment);
        }
    }

    /*
     * Unless we're changing the passphrase, the old one (if any) is a
     * reasonable default.
     */
    if (!change_passphrase && old_passphrase && !new_passphrase)
        new_passphrase = dupstr(old_passphrase);

    /*
     * Prompt for a new passphrase if we have been asked to, or if
     * we have just generated a key.
     *
     * In the latter case, an exception is if we're producing text
     * output, because that output format doesn't support encryption
     * in any case.
     */
    if (!new_passphrase && (change_passphrase ||
                            (keytype != NOKEYGEN && outtype != TEXT))) {
        prompts_t *p = new_prompts();
        int ret;

        p->to_server = false;
        p->from_server = false;
        p->name = dupstr("New SSH key passphrase");
        add_prompt(p, dupstr("Enter passphrase to save key: "), false);
        add_prompt(p, dupstr("Re-enter passphrase to verify: "), false);
        ret = console_get_userpass_input(p);
        assert(ret >= 0);
        if (!ret) {
            free_prompts(p);
            perror("puttygen: unable to read new passphrase");
            RETURN(1);
        } else {
            if (strcmp(prompt_get_result_ref(p->prompts[0]),
                       prompt_get_result_ref(p->prompts[1]))) {
                free_prompts(p);
                fprintf(stderr, "puttygen: passphrases do not match\n");
                RETURN(1);
            }
            new_passphrase = prompt_get_result(p->prompts[0]);
            free_prompts(p);
        }
    }
    if (new_passphrase && !*new_passphrase) {
        sfree(new_passphrase);
        new_passphrase = NULL;
    }

    /*
     * Write output.
     *
     * (In the case where outfile and outfiletmp are both NULL,
     * there is no semantic reason to initialise outfilename at
     * all; but we have to write _something_ to it or some compiler
     * will probably complain that it might be used uninitialised.)
     */
    if (outfiletmp)
        outfilename = filename_from_str(outfiletmp);
    else
        outfilename = filename_from_str(outfile ? outfile : "");

    switch (outtype) {
        bool ret;
        int real_outtype;

      case PRIVATE:
        random_ref(); /* we'll need a few random bytes in the save file */
        if (sshver == 1) {
            assert(ssh1key);
            ret = rsa1_save_f(outfilename, ssh1key, new_passphrase);
            if (!ret) {
                fprintf(stderr, "puttygen: unable to save SSH-1 private key\n");
                RETURN(1);
            }
        } else {
            assert(ssh2key);
            ret = ppk_save_f(outfilename, ssh2key, new_passphrase, &params);
            if (!ret) {
                fprintf(stderr, "puttygen: unable to save SSH-2 private key\n");
                RETURN(1);
            }
        }
        if (outfiletmp) {
            if (!move(outfiletmp, outfile))
                RETURN(1);              /* rename failed */
        }
        break;

      case PUBLIC:
      case PUBLICO: {
        FILE *fp;

        if (outfile) {
          fp = f_open(outfilename, "w", false);
          if (!fp) {
            fprintf(stderr, "unable to open output file\n");
            exit(1);
          }
        } else {
          fp = stdout;
        }

        if (sshver == 1) {
          ssh1_write_pubkey(fp, ssh1key);
        } else {
          if (!ssh2blob) {
            assert(ssh2key);
            ssh2blob = strbuf_new();
            ssh_key_public_blob(ssh2key->key, BinarySink_UPCAST(ssh2blob));
          }

          ssh2_write_pubkey(fp, ssh2key ? ssh2key->comment : origcomment,
                            ssh2blob->s, ssh2blob->len,
                            (outtype == PUBLIC ?
                             SSH_KEYTYPE_SSH2_PUBLIC_RFC4716 :
                             SSH_KEYTYPE_SSH2_PUBLIC_OPENSSH));
        }

        if (outfile)
            fclose(fp);

        break;
      }

      case FP: {
        FILE *fp;
        char *fingerprint;

        if (sshver == 1) {
          assert(ssh1key);
          fingerprint = rsa_ssh1_fingerprint(ssh1key);
        } else {
          if (ssh2key) {
            fingerprint = ssh2_fingerprint(ssh2key->key, fptype);
          } else {
            assert(ssh2blob);
            fingerprint = ssh2_fingerprint_blob(
                ptrlen_from_strbuf(ssh2blob), fptype);
          }
        }

        if (outfile) {
          fp = f_open(outfilename, "w", false);
          if (!fp) {
            fprintf(stderr, "unable to open output file\n");
            exit(1);
          }
        } else {
          fp = stdout;
        }
        fprintf(fp, "%s\n", fingerprint);
        if (outfile)
            fclose(fp);

        sfree(fingerprint);
        break;
      }

      case OPENSSH_AUTO:
      case OPENSSH_NEW:
      case SSHCOM:
        assert(sshver == 2);
        assert(ssh2key);
        random_ref(); /* both foreign key types require randomness,
                       * for IV or padding */
        switch (outtype) {
          case OPENSSH_AUTO:
            real_outtype = SSH_KEYTYPE_OPENSSH_AUTO;
            break;
          case OPENSSH_NEW:
            real_outtype = SSH_KEYTYPE_OPENSSH_NEW;
            break;
          case SSHCOM:
            real_outtype = SSH_KEYTYPE_SSHCOM;
            break;
          default:
            unreachable("control flow goof");
        }
        ret = export_ssh2(outfilename, real_outtype, ssh2key, new_passphrase);
        if (!ret) {
            fprintf(stderr, "puttygen: unable to export key\n");
            RETURN(1);
        }
        if (outfiletmp) {
            if (!move(outfiletmp, outfile))
                RETURN(1);              /* rename failed */
        }
        break;

      case TEXT: {
        key_components *kc;
        if (sshver == 1) {
            assert(ssh1key);
            kc = rsa_components(ssh1key);
        } else {
            if (ssh2key) {
                kc = ssh_key_components(ssh2key->key);
            } else {
                assert(ssh2blob);

                BinarySource src[1];
                BinarySource_BARE_INIT_PL(src, ptrlen_from_strbuf(ssh2blob));
                ptrlen algname = get_string(src);
                const ssh_keyalg *alg = find_pubkey_alg_len(algname);
                if (!alg) {
                    fprintf(stderr, "puttygen: cannot extract key components "
                            "from public key of unknown type '%.*s'\n",
                            PTRLEN_PRINTF(algname));
                    RETURN(1);
                }
                ssh_key *sk = ssh_key_new_pub(
                    alg, ptrlen_from_strbuf(ssh2blob));
                if (!sk) {
                    fprintf(stderr, "puttygen: unable to decode public key\n");
                    RETURN(1);
                }
                kc = ssh_key_components(sk);
                ssh_key_free(sk);
            }
        }

        FILE *fp;
        if (outfile) {
            fp = f_open(outfilename, "w", false);
            if (!fp) {
                fprintf(stderr, "unable to open output file\n");
                exit(1);
            }
        } else {
            fp = stdout;
        }

        for (size_t i = 0; i < kc->ncomponents; i++) {
            if (kc->components[i].is_mp_int) {
                char *hex = mp_get_hex(kc->components[i].mp);
                fprintf(fp, "%s=0x%s\n", kc->components[i].name, hex);
                smemclr(hex, strlen(hex));
                sfree(hex);
            } else {
                fprintf(fp, "%s=\"", kc->components[i].name);
                write_c_string_literal(fp, ptrlen_from_asciz(
                                           kc->components[i].text));
                fputs("\"\n", fp);
            }
        }

        if (outfile)
            fclose(fp);
        key_components_free(kc);
        break;
      }
    }

  out:

    #undef RETURN

    if (old_passphrase) {
        smemclr(old_passphrase, strlen(old_passphrase));
        sfree(old_passphrase);
    }
    if (new_passphrase) {
        smemclr(new_passphrase, strlen(new_passphrase));
        sfree(new_passphrase);
    }

    if (ssh1key) {
        freersakey(ssh1key);
        sfree(ssh1key);
    }
    if (ssh2key && ssh2key != SSH2_WRONG_PASSPHRASE) {
        sfree(ssh2key->comment);
        if (ssh2key->key)
            ssh_key_free(ssh2key->key);
        sfree(ssh2key);
    }
    if (ssh2blob)
        strbuf_free(ssh2blob);
    sfree(origcomment);
    if (infilename)
        filename_free(infilename);
    if (infile_lf)
        lf_free(infile_lf);
    if (outfilename)
        filename_free(outfilename);
    sfree(outfiletmp);

    return exit_status;
}
