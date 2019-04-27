/*
 * cmdgen.c - command-line form of PuTTYgen
 */

#define PUTTY_DO_GLOBALS

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

#ifdef TEST_CMDGEN
/*
 * This section overrides some definitions below for test purposes.
 * When compiled with -DTEST_CMDGEN (as cgtest.c will do):
 * 
 *  - Calls to get_random_data() are replaced with the diagnostic
 *    function below (I #define the name so that I can still link
 *    with the original set of modules without symbol clash), in
 *    order to avoid depleting the test system's /dev/random
 *    unnecessarily.
 * 
 *  - Calls to console_get_userpass_input() are replaced with the
 *    diagnostic function below, so that I can run tests in an
 *    automated manner and provide their interactive passphrase
 *    inputs.
 * 
 *  - main() is renamed to cmdgen_main(); at the bottom of the file
 *    I define another main() which calls the former repeatedly to
 *    run tests.
 */
#define get_random_data get_random_data_diagnostic
char *get_random_data(int len, const char *device)
{
    char *buf = snewn(len, char);
    memset(buf, 'x', len);
    return buf;
}
#define console_get_userpass_input console_get_userpass_input_diagnostic
int nprompts, promptsgot;
const char *prompts[3];
int console_get_userpass_input(prompts_t *p)
{
    size_t i;
    int ret = 1;
    for (i = 0; i < p->n_prompts; i++) {
	if (promptsgot < nprompts) {
	    p->prompts[i]->result = dupstr(prompts[promptsgot++]);
	} else {
	    promptsgot++;	    /* track number of requests anyway */
	    ret = 0;
	}
    }
    return ret;
}
#define main cmdgen_main
#endif

struct progress {
    int phase, current;
};

static void progress_update(void *param, int action, int phase, int iprogress)
{
    struct progress *p = (struct progress *)param;
    if (action != PROGFN_PROGRESS)
	return;
    if (phase > p->phase) {
	if (p->phase >= 0)
	    fputc('\n', stderr);
	p->phase = phase;
	if (iprogress >= 0)
	    p->current = iprogress - 1;
	else
	    p->current = iprogress;
    }
    while (p->current < iprogress) {
	fputc('+', stdout);
	p->current++;
    }
    fflush(stdout);
}

static void no_progress(void *param, int action, int phase, int iprogress)
{
}

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
    printf("  -t    specify key type when generating (ed25519, ecdsa, rsa, "
							"dsa, rsa1)\n"
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
	   "  -o    specify output file\n"
	   "  -l    equivalent to `-O fingerprint'\n"
	   "  -L    equivalent to `-O public-openssh'\n"
	   "  -p    equivalent to `-O public'\n"
	   "  --old-passphrase file\n"
	   "        specify file containing old key passphrase\n"
	   "  --new-passphrase file\n"
	   "        specify file containing new key passphrase\n"
	   "  --random-device device\n"
	   "        specify device to read entropy from (e.g. /dev/urandom)\n"
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
    else	/* empty file */
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
    enum { NOKEYGEN, RSA1, RSA2, DSA, ECDSA, ED25519 } keytype = NOKEYGEN;
    char *outfile = NULL, *outfiletmp = NULL;
    enum { PRIVATE, PUBLIC, PUBLICO, FP, OPENSSH_AUTO,
           OPENSSH_NEW, SSHCOM } outtype = PRIVATE;
    int bits = -1;
    char *comment = NULL, *origcomment = NULL;
    bool change_passphrase = false;
    bool errs = false, nogo = false;
    int intype = SSH_KEYTYPE_UNOPENABLE;
    int sshver = 0;
    ssh2_userkey *ssh2key = NULL;
    RSAKey *ssh1key = NULL;
    strbuf *ssh2blob = NULL;
    char *ssh2alg = NULL;
    char *old_passphrase = NULL, *new_passphrase = NULL;
    bool load_encrypted;
    progfn_t progressfn = is_interactive() ? progress_update : no_progress;
    const char *random_device = NULL;

    /* ------------------------------------------------------------------
     * Parse the command line to figure out what we've been asked to do.
     */

    /*
     * If run with no arguments at all, print the usage message and
     * return success.
     */
    if (argc <= 1) {
	usage(true);
	return 0;
    }

    /*
     * Parse command line arguments.
     */
    while (--argc) {
	char *p = *++argv;
	if (*p == '-') {
	    /*
	     * An option.
	     */
	    while (p && *++p) {
		char c = *p;
		switch (c) {
		  case '-':
		    /*
		     * Long option.
		     */
		    {
			char *opt, *val;
			opt = p++;     /* opt will have _one_ leading - */
			while (*p && *p != '=')
			    p++;	       /* find end of option */
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
			} else {
			    errs = true;
			    fprintf(stderr,
				    "puttygen: no such option `-%s'\n", opt);
			}
		    }
		    p = NULL;
		    break;
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
			progressfn = no_progress;
			break;
		    }
		    break;
		  case 't':
		  case 'b':
		  case 'C':
		  case 'O':
		  case 'o':
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
                        else if (!strcmp(p, "ed25519"))
                            keytype = ED25519, sshver = 2;
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
			else {
			    fprintf(stderr,
				    "puttygen: unknown output type `%s'\n", p);
			    errs = true;
			}
                        break;
		      case 'o':
			outfile = p;
                        break;
		    }
		    p = NULL;	       /* prevent continued processing */
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
          case ED25519:
            bits = 256;
            break;
          default:
            bits = DEFAULT_RSADSA_BITS;
            break;
        }
    }

    if (keytype == ECDSA && (bits != 256 && bits != 384 && bits != 521)) {
        fprintf(stderr, "puttygen: invalid bits for ECDSA, choose 256, 384 or 521\n");
        errs = true;
    }

    if (keytype == ED25519 && (bits != 256)) {
        fprintf(stderr, "puttygen: invalid bits for ED25519, choose 256\n");
        errs = true;
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
	return 1;

    if (nogo)
	return 0;

    /*
     * If run with at least one argument _but_ not the required
     * ones, print the usage message and return failure.
     */
    if (!infile && keytype == NOKEYGEN) {
	usage(true);
	return 1;
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
	return 1;
    }

    /* 
     * We must save the private part when generating a new key.
     */
    if (keytype != NOKEYGEN &&
	(outtype != PRIVATE && outtype != OPENSSH_AUTO &&
         outtype != OPENSSH_NEW && outtype != SSHCOM)) {
	fprintf(stderr, "puttygen: this would generate a new key but "
		"discard the private part\n");
	return 1;
    }

    /*
     * Analyse the type of the input file, in case this affects our
     * course of action.
     */
    if (infile) {
	infilename = filename_from_str(infile);

	intype = key_type(infilename);

	switch (intype) {
	  case SSH_KEYTYPE_UNOPENABLE:
	  case SSH_KEYTYPE_UNKNOWN:
	    fprintf(stderr, "puttygen: unable to load file `%s': %s\n",
		    infile, key_type_to_str(intype));
	    return 1;

	  case SSH_KEYTYPE_SSH1:
          case SSH_KEYTYPE_SSH1_PUBLIC:
	    if (sshver == 2) {
		fprintf(stderr, "puttygen: conversion from SSH-1 to SSH-2 keys"
			" not supported\n");
		return 1;
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
		return 1;
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
	    outfiletmp = dupcat(outfile, ".tmp", NULL);
	}

	if (!change_passphrase && !comment) {
	    fprintf(stderr, "puttygen: this command would perform no useful"
		    " action\n");
	    return 1;
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
		return 1;
	    }
	}
    }

    /*
     * Figure out whether we need to load the encrypted part of the
     * key. This will be the case if either (a) we need to write
     * out a private key format, or (b) the entire input key file
     * is encrypted.
     */
    if (outtype == PRIVATE || outtype == OPENSSH_AUTO ||
        outtype == OPENSSH_NEW || outtype == SSHCOM ||
	intype == SSH_KEYTYPE_OPENSSH_PEM ||
	intype == SSH_KEYTYPE_OPENSSH_NEW ||
        intype == SSH_KEYTYPE_SSHCOM)
	load_encrypted = true;
    else
	load_encrypted = false;

    if (load_encrypted && (intype == SSH_KEYTYPE_SSH1_PUBLIC ||
                           intype == SSH_KEYTYPE_SSH2_PUBLIC_RFC4716 ||
                           intype == SSH_KEYTYPE_SSH2_PUBLIC_OPENSSH)) {
        fprintf(stderr, "puttygen: cannot perform this action on a "
                "public-key-only input file\n");
        return 1;
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
	struct progress prog;

	prog.phase = -1;
	prog.current = -1;

	tm = ltime();
	if (keytype == DSA)
	    strftime(default_comment, 30, "dsa-key-%Y%m%d", &tm);
        else if (keytype == ECDSA)
            strftime(default_comment, 30, "ecdsa-key-%Y%m%d", &tm);
        else if (keytype == ED25519)
            strftime(default_comment, 30, "ed25519-key-%Y%m%d", &tm);
	else
	    strftime(default_comment, 30, "rsa-key-%Y%m%d", &tm);

	entropy = get_random_data(bits / 8, random_device);
	if (!entropy) {
	    fprintf(stderr, "puttygen: failed to collect entropy, "
		    "could not generate key\n");
	    return 1;
	}
	random_setup_special();
        random_reseed(make_ptrlen(entropy, bits / 8));
	smemclr(entropy, bits/8);
	sfree(entropy);

	if (keytype == DSA) {
	    struct dss_key *dsskey = snew(struct dss_key);
	    dsa_generate(dsskey, bits, progressfn, &prog);
	    ssh2key = snew(ssh2_userkey);
	    ssh2key->key = &dsskey->sshk;
	    ssh1key = NULL;
        } else if (keytype == ECDSA) {
            struct ecdsa_key *ek = snew(struct ecdsa_key);
            ecdsa_generate(ek, bits, progressfn, &prog);
            ssh2key = snew(ssh2_userkey);
            ssh2key->key = &ek->sshk;
            ssh1key = NULL;
        } else if (keytype == ED25519) {
            struct eddsa_key *ek = snew(struct eddsa_key);
            eddsa_generate(ek, bits, progressfn, &prog);
            ssh2key = snew(ssh2_userkey);
            ssh2key->key = &ek->sshk;
            ssh1key = NULL;
	} else {
	    RSAKey *rsakey = snew(RSAKey);
	    rsa_generate(rsakey, bits, progressfn, &prog);
	    rsakey->comment = NULL;
	    if (keytype == RSA1) {
		ssh1key = rsakey;
	    } else {
		ssh2key = snew(ssh2_userkey);
		ssh2key->key = &rsakey->sshk;
	    }
	}
	progressfn(&prog, PROGFN_PROGRESS, INT_MAX, -1);

	if (ssh2key)
	    ssh2key->comment = dupstr(default_comment);
	if (ssh1key)
	    ssh1key->comment = dupstr(default_comment);

    } else {
	const char *error = NULL;
	bool encrypted;

	assert(infile != NULL);

	/*
	 * Find out whether the input key is encrypted.
	 */
	if (intype == SSH_KEYTYPE_SSH1)
	    encrypted = rsa_ssh1_encrypted(infilename, &origcomment);
	else if (intype == SSH_KEYTYPE_SSH2)
	    encrypted = ssh2_userkey_encrypted(infilename, &origcomment);
	else
	    encrypted = import_encrypted(infilename, intype, &origcomment);

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
		    return 1;
		} else {
		    old_passphrase = dupstr(p->prompts[0]->result);
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
	    if (!load_encrypted) {
		strbuf *blob;
                BinarySource src[1];

                blob = strbuf_new();
		ret = rsa_ssh1_loadpub(infilename, BinarySink_UPCAST(blob),
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
		ret = rsa_ssh1_loadkey(
                    infilename, ssh1key, old_passphrase, &error);
	    }
	    if (ret > 0)
		error = NULL;
	    else if (!error)
		error = "unknown error";
	    break;

	  case SSH_KEYTYPE_SSH2:
          case SSH_KEYTYPE_SSH2_PUBLIC_RFC4716:
          case SSH_KEYTYPE_SSH2_PUBLIC_OPENSSH:
	    if (!load_encrypted) {
                ssh2blob = strbuf_new();
		if (ssh2_userkey_loadpub(infilename, &ssh2alg, BinarySink_UPCAST(ssh2blob),
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
		ssh2key = ssh2_load_userkey(infilename, old_passphrase,
					    &error);
	    }
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
	    ssh2key = import_ssh2(infilename, intype, old_passphrase, &error);
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
	    return 1;
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
     */
    if (!new_passphrase && (change_passphrase || keytype != NOKEYGEN)) {
	prompts_t *p = new_prompts(NULL);
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
	    return 1;
	} else {
	    if (strcmp(p->prompts[0]->result, p->prompts[1]->result)) {
		free_prompts(p);
		fprintf(stderr, "puttygen: passphrases do not match\n");
		return 1;
	    }
	    new_passphrase = dupstr(p->prompts[0]->result);
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
	if (sshver == 1) {
	    assert(ssh1key);
	    ret = rsa_ssh1_savekey(outfilename, ssh1key, new_passphrase);
	    if (!ret) {
		fprintf(stderr, "puttygen: unable to save SSH-1 private key\n");
		return 1;
	    }
	} else {
	    assert(ssh2key);
	    ret = ssh2_save_userkey(outfilename, ssh2key, new_passphrase);
 	    if (!ret) {
		fprintf(stderr, "puttygen: unable to save SSH-2 private key\n");
		return 1;
	    }
	}
	if (outfiletmp) {
	    if (!move(outfiletmp, outfile))
		return 1;	       /* rename failed */
	}
	break;

      case PUBLIC:
      case PUBLICO:
        {
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
        }
	break;

      case FP:
	{
	    FILE *fp;
	    char *fingerprint;

	    if (sshver == 1) {
		assert(ssh1key);
		fingerprint = rsa_ssh1_fingerprint(ssh1key);
	    } else {
		if (ssh2key) {
		    fingerprint = ssh2_fingerprint(ssh2key->key);
		} else {
		    assert(ssh2blob);
		    fingerprint = ssh2_fingerprint_blob(
                        ptrlen_from_strbuf(ssh2blob));
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
	}
	break;
	
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
	    return 1;
	}
	if (outfiletmp) {
	    if (!move(outfiletmp, outfile))
		return 1;	       /* rename failed */
	}
	break;
    }

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
    }
    if (ssh2key) {
	sfree(ssh2key->comment);
	ssh_key_free(ssh2key->key);
	sfree(ssh2key);
    }
    sfree(origcomment);
    if (infilename)
        filename_free(infilename);
    filename_free(outfilename);

    return 0;
}

#ifdef TEST_CMDGEN

#undef main

#include <stdarg.h>

int passes, fails;

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
    ret = cmdgen_main(argc, argv);

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

void filecmp(char *file1, char *file2, char *fmt, ...)
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

char *cleanup_fp(char *s)
{
    char *p;

    if (!strncmp(s, "ssh-", 4)) {
	s += strcspn(s, " \n\t");
	s += strspn(s, " \n\t");
    }

    p = s;
    s += strcspn(s, " \n\t");
    s += strspn(s, " \n\t");
    s += strcspn(s, " \n\t");

    return dupprintf("%.*s", (int)(s - p), p);
}

char *get_fp(char *filename)
{
    FILE *fp;
    char buf[256], *ret;

    fp = fopen(filename, "r");
    if (!fp)
	return NULL;
    ret = fgets(buf, sizeof(buf), fp);
    fclose(fp);
    if (!ret)
	return NULL;
    return cleanup_fp(buf);
}

void check_fp(char *filename, char *fp, char *fmt, ...)
{
    char *newfp;

    if (!fp)
	return;

    newfp = get_fp(filename);

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

int main(int argc, char **argv)
{
    int i;
    static char *const keytypes[] = { "rsa1", "dsa", "rsa" };

    /*
     * Even when this thing is compiled for automatic test mode,
     * it's helpful to be able to invoke it with command-line
     * options for _manual_ tests.
     */
    if (argc > 1)
	return cmdgen_main(argc, argv);

    passes = fails = 0;

    for (i = 0; i < lenof(keytypes); i++) {
	char filename[128], osfilename[128], scfilename[128];
	char pubfilename[128], tmpfilename1[128], tmpfilename2[128];
	char *fp;

	sprintf(filename, "test-%s.ppk", keytypes[i]);
	sprintf(pubfilename, "test-%s.pub", keytypes[i]);
	sprintf(osfilename, "test-%s.os", keytypes[i]);
	sprintf(scfilename, "test-%s.sc", keytypes[i]);
	sprintf(tmpfilename1, "test-%s.tmp1", keytypes[i]);
	sprintf(tmpfilename2, "test-%s.tmp2", keytypes[i]);

	/*
	 * Create an encrypted key.
	 */
	setup_passphrases("sponge", "sponge", NULL);
	test(0, "puttygen", "-t", keytypes[i], "-o", filename, NULL);

	/*
	 * List the public key in OpenSSH format.
	 */
	setup_passphrases(NULL);
	test(0, "puttygen", "-L", filename, "-o", pubfilename, NULL);
	{
	    char *cmdbuf;
	    fp = NULL;
	    cmdbuf = dupprintf("ssh-keygen -l -f '%s' > '%s'",
		    pubfilename, tmpfilename1);
	    if (system(cmdbuf) ||
		(fp = get_fp(tmpfilename1)) == NULL) {
		printf("UNABLE to test fingerprint matching against OpenSSH");
	    }
	    sfree(cmdbuf);
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
	test(0, "puttygen", "-l", filename, "-o", tmpfilename1, NULL);
	if (!fp) {
	    /*
	     * If we can't test fingerprints against OpenSSH, we
	     * can at the very least test equality of all the
	     * fingerprints we generate of this key throughout
	     * testing.
	     */
	    fp = get_fp(tmpfilename1);
	} else {
	    check_fp(tmpfilename1, fp, "%s initial fp", keytypes[i]);
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
	 * For RSA1 keys, this should give an error.
	 */
	setup_passphrases(NULL);
	test((i==0), "puttygen", "-O", "private-openssh", "-o", osfilename,
	     filename, NULL);

	if (i) {
	    /*
	     * List the fingerprint of the OpenSSH-formatted key.
	     */
	    setup_passphrases(NULL);
	    test(0, "puttygen", "-l", osfilename, "-o", tmpfilename1, NULL);
	    check_fp(tmpfilename1, fp, "%s openssh clear fp", keytypes[i]);

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
	 * For RSA1 keys, this should give an error.
	 */
	setup_passphrases(NULL);
	test((i==0), "puttygen", "-O", "private-sshcom", "-o", scfilename,
	     filename, NULL);

	if (i) {
	    /*
	     * List the fingerprint of the ssh.com-formatted key.
	     */
	    setup_passphrases(NULL);
	    test(0, "puttygen", "-l", scfilename, "-o", tmpfilename1, NULL);
	    check_fp(tmpfilename1, fp, "%s ssh.com clear fp", keytypes[i]);

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

	if (i) {
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
		    "p->o->s->p clear %s", keytypes[i]);

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
		    "p->s->o->p clear %s", keytypes[i]);

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
		    "p->s->p clear %s", keytypes[i]);
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
	 * while encrypted. For RSA1 keys, this should give an
	 * error.
	 */
	if (i == 0)
	    setup_passphrases(NULL);   /* error, hence no passphrase read */
	else
	    setup_passphrases("sponge2", NULL);
	test((i==0), "puttygen", "-O", "private-openssh", "-o", osfilename,
	     filename, NULL);

	if (i) {
	    /*
	     * List the fingerprint of the OpenSSH-formatted key.
	     */
	    setup_passphrases("sponge2", NULL);
	    test(0, "puttygen", "-l", osfilename, "-o", tmpfilename1, NULL);
	    check_fp(tmpfilename1, fp, "%s openssh encrypted fp", keytypes[i]);

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
	if (i == 0)
	    setup_passphrases(NULL);   /* error, hence no passphrase read */
	else
	    setup_passphrases("sponge2", NULL);
	test((i==0), "puttygen", "-O", "private-sshcom", "-o", scfilename,
	     filename, NULL);

	if (i) {
	    /*
	     * List the fingerprint of the ssh.com-formatted key.
	     */
	    setup_passphrases("sponge2", NULL);
	    test(0, "puttygen", "-l", scfilename, "-o", tmpfilename1, NULL);
	    check_fp(tmpfilename1, fp, "%s ssh.com encrypted fp", keytypes[i]);

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

	if (i) {
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
		    "p->o->s->p encrypted %s", keytypes[i]);

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
		    "p->s->o->p encrypted %s", keytypes[i]);

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
		    "p->s->p encrypted %s", keytypes[i]);
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
    }
    printf("%d passes, %d fails\n", passes, fails);
    return 0;
}

#endif
