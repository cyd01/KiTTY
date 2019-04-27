/*
 * testsc: run PuTTY's crypto primitives under instrumentation that
 * checks for cache and timing side channels.
 *
 * The idea is: cryptographic code should avoid leaking secret data
 * through timing information, or through traces of its activity left
 * in the caches.
 *
 * (This property is sometimes called 'constant-time', although really
 * that's a misnomer. It would be impossible to avoid the execution
 * time varying for any number of reasons outside the code's control,
 * such as the prior contents of caches and branch predictors,
 * temperature-based CPU throttling, system load, etc. And in any case
 * you don't _need_ the execution time to be literally constant: you
 * just need it to be independent of your secrets. It can vary as much
 * as it likes based on anything else.)
 *
 * To avoid this, you need to ensure that various aspects of the
 * code's behaviour do not depend on the secret data. The control
 * flow, for a start - no conditional branches based on secrets - and
 * also the memory access pattern (no using secret data as an index
 * into a lookup table). A couple of other kinds of CPU instruction
 * also can't be trusted to run in constant time: we check for
 * register-controlled shifts and hardware divisions. (But, again,
 * it's perfectly fine to _use_ those instructions in the course of
 * crypto code. You just can't use a secret as any time-affecting
 * operand.)
 *
 * This test program works by running the same crypto primitive
 * multiple times, with different secret input data. The relevant
 * details of each run is logged to a file via the DynamoRIO-based
 * instrumentation system living in the subdirectory test/sclog. Then
 * we check over all the files and ensure they're identical.
 *
 * This program itself (testsc) is built by the ordinary PuTTY
 * makefiles. But run by itself, it will do nothing useful: it needs
 * to be run under DynamoRIO, with the sclog instrumentation library.
 *
 * Here's an example of how I built it:
 *
 * Download the DynamoRIO source. I did this by cloning
 * https://github.com/DynamoRIO/dynamorio.git, and at the time of
 * writing this, 259c182a75ce80112bcad329c97ada8d56ba854d was the head
 * commit.
 *
 * In the DynamoRIO checkout:
 *
 *   mkdir build
 *   cd build
 *   cmake -G Ninja ..
 *   ninja
 *
 * Now set the shell variable DRBUILD to be the location of the build
 * directory you did that in. (Or not, if you prefer, but the example
 * build commands below will assume that that's where the DynamoRIO
 * libraries, headers and runtime can be found.)
 *
 * Then, in test/sclog:
 *
 *   cmake -G Ninja -DCMAKE_PREFIX_PATH=$DRBUILD/cmake .
 *   ninja
 *
 * Finally, to run the actual test, set SCTMP to some temp directory
 * you don't mind filling with large temp files (several GB at a
 * time), and in the main PuTTY source directory (assuming that's
 * where testsc has been built):
 *
 *   $DRBUILD/bin64/drrun -c test/sclog/libsclog.so -- ./testsc -O $SCTMP
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "defs.h"
#include "putty.h"
#include "ssh.h"
#include "misc.h"
#include "mpint.h"
#include "ecc.h"

static NORETURN void fatal_error(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "testsc: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void out_of_memory(void) { fatal_error("out of memory"); }

/*
 * A simple deterministic PRNG, without any of the Fortuna
 * complexities, for generating test inputs in a way that's repeatable
 * between runs of the program, even if only a subset of test cases is
 * run.
 */
static uint64_t random_counter = 0;
static const char *random_seedstr = NULL;
static uint8_t random_buf[MAX_HASH_LEN];
static size_t random_buf_limit = 0;

static void random_seed(const char *seedstr)
{
    random_seedstr = seedstr;
    random_counter = 0;
    random_buf_limit = 0;
}

void random_read(void *vbuf, size_t size)
{
    assert(random_seedstr);
    uint8_t *buf = (uint8_t *)vbuf;
    while (size-- > 0) {
        if (random_buf_limit == 0) {
            ssh_hash *h = ssh_hash_new(&ssh_sha256);
            put_asciz(h, random_seedstr);
            put_uint64(h, random_counter);
            random_counter++;
            random_buf_limit = ssh_hash_alg(h)->hlen;
            ssh_hash_final(h, random_buf);
        }
        *buf++ = random_buf[random_buf_limit--];
    }
}

/*
 * Macro that defines a function, and also a volatile function pointer
 * pointing to it. Callers indirect through the function pointer
 * instead of directly calling the function, to ensure that the
 * compiler doesn't try to get clever by eliminating the call
 * completely, or inlining it.
 *
 * This is used to mark functions that DynamoRIO will look for to
 * intercept, and also to inhibit inlining and unrolling where they'd
 * cause a failure of experimental control in the main test.
 */
#define VOLATILE_WRAPPED_DEFN(qualifier, rettype, fn, params)   \
    qualifier rettype fn##_real params;                         \
    qualifier rettype (*volatile fn) params = fn##_real;        \
    qualifier rettype fn##_real params

VOLATILE_WRAPPED_DEFN(, void, log_to_file, (const char *filename))
{
    /*
     * This function is intercepted by the DynamoRIO side of the
     * mechanism. We use it to send instructions to the DR wrapper,
     * namely, 'please start logging to this file' or 'please stop
     * logging' (if filename == NULL). But we don't have to actually
     * do anything in _this_ program - all the functionality is in the
     * DR wrapper.
     */
}

static const char *outdir = NULL;
char *log_filename(const char *basename, size_t index)
{
    return dupprintf("%s/%s.%04zu", outdir, basename, index);
}

static char *last_filename;
static const char *test_basename;
static size_t test_index = 0;
void log_start(void)
{
    last_filename = log_filename(test_basename, test_index++);
    log_to_file(last_filename);
}
void log_end(void)
{
    log_to_file(NULL);
    sfree(last_filename);
}

static bool test_skipped = false;

VOLATILE_WRAPPED_DEFN(, intptr_t, dry_run, (void))
{
    /*
     * This is another function intercepted by DynamoRIO. In this
     * case, DR overrides this function to return 0 rather than 1, so
     * we can use it as a check for whether we're running under
     * instrumentation, or whether this is just a dry run which goes
     * through the motions but doesn't expect to find any log files
     * created.
     */
    return 1;
}

static void mp_random_bits_into(mp_int *r, size_t bits)
{
    mp_int *x = mp_random_bits(bits);
    mp_copy_into(r, x);
    mp_free(x);
}

static void mp_random_fill(mp_int *r)
{
    mp_random_bits_into(r, mp_max_bits(r));
}

VOLATILE_WRAPPED_DEFN(static, size_t, looplimit, (size_t x))
{
    /*
     * looplimit() is the identity function on size_t, but the
     * compiler isn't allowed to rely on it being that. I use it to
     * make loops in the test functions look less attractive to
     * compilers' unrolling heuristics.
     */
    return x;
}

/* Ciphers that we expect to pass this test. Blowfish and Arcfour are
 * intentionally omitted, because we already know they don't. */
#define CIPHERS(X, Y)                           \
    X(Y, ssh_3des_ssh1)                         \
    X(Y, ssh_3des_ssh2_ctr)                     \
    X(Y, ssh_3des_ssh2)                         \
    X(Y, ssh_des)                               \
    X(Y, ssh_des_sshcom_ssh2)                   \
    X(Y, ssh_aes256_sdctr)                      \
    X(Y, ssh_aes256_sdctr_hw)                   \
    X(Y, ssh_aes256_sdctr_sw)                   \
    X(Y, ssh_aes256_cbc)                        \
    X(Y, ssh_aes256_cbc_hw)                     \
    X(Y, ssh_aes256_cbc_sw)                     \
    X(Y, ssh_aes192_sdctr)                      \
    X(Y, ssh_aes192_sdctr_hw)                   \
    X(Y, ssh_aes192_sdctr_sw)                   \
    X(Y, ssh_aes192_cbc)                        \
    X(Y, ssh_aes192_cbc_hw)                     \
    X(Y, ssh_aes192_cbc_sw)                     \
    X(Y, ssh_aes128_sdctr)                      \
    X(Y, ssh_aes128_sdctr_hw)                   \
    X(Y, ssh_aes128_sdctr_sw)                   \
    X(Y, ssh_aes128_cbc)                        \
    X(Y, ssh_aes128_cbc_hw)                     \
    X(Y, ssh_aes128_cbc_sw)                     \
    X(Y, ssh2_chacha20_poly1305)                \
    /* end of list */

#define CIPHER_TESTLIST(X, name) X(cipher_ ## name)

#define MACS(X, Y)                              \
    X(Y, ssh_hmac_md5)                          \
    X(Y, ssh_hmac_sha1)                         \
    X(Y, ssh_hmac_sha1_buggy)                   \
    X(Y, ssh_hmac_sha1_96)                      \
    X(Y, ssh_hmac_sha1_96_buggy)                \
    X(Y, ssh_hmac_sha256)                       \
    /* end of list */

#define MAC_TESTLIST(X, name) X(mac_ ## name)

#define HASHES(X, Y)                            \
    X(Y, ssh_md5)                               \
    X(Y, ssh_sha1)                              \
    X(Y, ssh_sha1_hw)                           \
    X(Y, ssh_sha1_sw)                           \
    X(Y, ssh_sha256)                            \
    X(Y, ssh_sha256_hw)                         \
    X(Y, ssh_sha256_sw)                         \
    X(Y, ssh_sha384)                            \
    X(Y, ssh_sha512)                            \
    /* end of list */

#define HASH_TESTLIST(X, name) X(hash_ ## name)

#define TESTLIST(X)                             \
    X(mp_get_nbits)                             \
    X(mp_from_decimal)                          \
    X(mp_from_hex)                              \
    X(mp_get_decimal)                           \
    X(mp_get_hex)                               \
    X(mp_cmp_hs)                                \
    X(mp_cmp_eq)                                \
    X(mp_min)                                   \
    X(mp_max)                                   \
    X(mp_select_into)                           \
    X(mp_cond_swap)                             \
    X(mp_cond_clear)                            \
    X(mp_add)                                   \
    X(mp_sub)                                   \
    X(mp_mul)                                   \
    X(mp_rshift_safe)                           \
    X(mp_divmod)                                \
    X(mp_modadd)                                \
    X(mp_modsub)                                \
    X(mp_modmul)                                \
    X(mp_modpow)                                \
    X(mp_invert_mod_2to)                        \
    X(mp_invert)                                \
    X(mp_modsqrt)                               \
    X(ecc_weierstrass_add)                      \
    X(ecc_weierstrass_double)                   \
    X(ecc_weierstrass_add_general)              \
    X(ecc_weierstrass_multiply)                 \
    X(ecc_weierstrass_is_identity)              \
    X(ecc_weierstrass_get_affine)               \
    X(ecc_weierstrass_decompress)               \
    X(ecc_montgomery_diff_add)                  \
    X(ecc_montgomery_double)                    \
    X(ecc_montgomery_multiply)                  \
    X(ecc_montgomery_get_affine)                \
    X(ecc_edwards_add)                          \
    X(ecc_edwards_multiply)                     \
    X(ecc_edwards_eq)                           \
    X(ecc_edwards_get_affine)                   \
    X(ecc_edwards_decompress)                   \
    CIPHERS(CIPHER_TESTLIST, X)                 \
    MACS(MAC_TESTLIST, X)                       \
    HASHES(HASH_TESTLIST, X)                    \
    /* end of list */

static void test_mp_get_nbits(void)
{
    mp_int *z = mp_new(512);
    static const size_t bitposns[] = {
        0, 1, 5, 16, 23, 32, 67, 123, 234, 511
    };
    mp_int *prev = mp_from_integer(0);
    for (size_t i = 0; i < looplimit(lenof(bitposns)); i++) {
        mp_int *x = mp_power_2(bitposns[i]);
        mp_add_into(z, x, prev);
        mp_free(prev);
        prev = x;
        log_start();
        mp_get_nbits(z);
        log_end();
    }
    mp_free(prev);
}

static void test_mp_from_decimal(void)
{
    char dec[64];
    static const size_t starts[] = { 0, 1, 5, 16, 23, 32, 63, 64 };
    for (size_t i = 0; i < looplimit(lenof(starts)); i++) {
        memset(dec, '0', lenof(dec));
        for (size_t j = starts[i]; j < lenof(dec); j++) {
            uint8_t r[4];
            random_read(r, 4);
            dec[j] = '0' + GET_32BIT_MSB_FIRST(r) % 10;
        }
        log_start();
        mp_int *x = mp_from_decimal_pl(make_ptrlen(dec, lenof(dec)));
        log_end();
        mp_free(x);
    }
}

static void test_mp_from_hex(void)
{
    char hex[64];
    static const size_t starts[] = { 0, 1, 5, 16, 23, 32, 63, 64 };
    static const char digits[] = "0123456789abcdefABCDEF";
    for (size_t i = 0; i < looplimit(lenof(starts)); i++) {
        memset(hex, '0', lenof(hex));
        for (size_t j = starts[i]; j < lenof(hex); j++) {
            uint8_t r[4];
            random_read(r, 4);
            hex[j] = digits[GET_32BIT_MSB_FIRST(r) % lenof(digits)];
        }
        log_start();
        mp_int *x = mp_from_hex_pl(make_ptrlen(hex, lenof(hex)));
        log_end();
        mp_free(x);
    }
}

static void test_mp_string_format(char *(*mp_format)(mp_int *x))
{
    mp_int *z = mp_new(512);
    static const size_t bitposns[] = {
        0, 1, 5, 16, 23, 32, 67, 123, 234, 511
    };
    for (size_t i = 0; i < looplimit(lenof(bitposns)); i++) {
        mp_random_bits_into(z, bitposns[i]);
        log_start();
        char *formatted = mp_format(z);
        log_end();
        sfree(formatted);
    }
}

static void test_mp_get_decimal(void)
{
    test_mp_string_format(mp_get_decimal);
}

static void test_mp_get_hex(void)
{
    test_mp_string_format(mp_get_hex);
}

static void test_mp_cmp(unsigned (*mp_cmp)(mp_int *a, mp_int *b))
{
    mp_int *a = mp_new(512), *b = mp_new(512);
    static const size_t bitposns[] = {
        0, 1, 5, 16, 23, 32, 67, 123, 234, 511
    };
    for (size_t i = 0; i < looplimit(lenof(bitposns)); i++) {
        mp_random_fill(b);
        mp_int *x = mp_random_bits(bitposns[i]);
        mp_xor_into(a, b, x);
        mp_free(x);
        log_start();
        mp_cmp(a, b);
        log_end();
    }
    mp_free(a);
    mp_free(b);
}

static void test_mp_cmp_hs(void)
{
    test_mp_cmp(mp_cmp_hs);
}

static void test_mp_cmp_eq(void)
{
    test_mp_cmp(mp_cmp_eq);
}

static void test_mp_minmax(
    void (*mp_minmax_into)(mp_int *r, mp_int *x, mp_int *y))
{
    mp_int *a = mp_new(256), *b = mp_new(256);
    for (size_t i = 0; i < looplimit(10); i++) {
        uint8_t lens[2];
        random_read(lens, 2);
        mp_int *x = mp_random_bits(lens[0]);
        mp_copy_into(a, x);
        mp_free(x);
        mp_int *y = mp_random_bits(lens[1]);
        mp_copy_into(a, y);
        mp_free(y);
        log_start();
        mp_minmax_into(a, a, b);
        log_end();
    }
    mp_free(a);
    mp_free(b);
}

static void test_mp_max(void)
{
    test_mp_minmax(mp_max_into);
}

static void test_mp_min(void)
{
    test_mp_minmax(mp_min_into);
}

static void test_mp_select_into(void)
{
    mp_int *a = mp_random_bits(256);
    mp_int *b = mp_random_bits(512);
    mp_int *r = mp_new(384);
    for (size_t i = 0; i < looplimit(16); i++) {
        log_start();
        mp_select_into(r, a, b, i & 1);
        log_end();
    }
    mp_free(a);
    mp_free(b);
    mp_free(r);
}

static void test_mp_cond_swap(void)
{
    mp_int *a = mp_random_bits(512);
    mp_int *b = mp_random_bits(512);
    for (size_t i = 0; i < looplimit(16); i++) {
        log_start();
        mp_cond_swap(a, b, i & 1);
        log_end();
    }
    mp_free(a);
    mp_free(b);
}

static void test_mp_cond_clear(void)
{
    mp_int *a = mp_random_bits(512);
    mp_int *x = mp_copy(a);
    for (size_t i = 0; i < looplimit(16); i++) {
        mp_copy_into(x, a);
        log_start();
        mp_cond_clear(a, i & 1);
        log_end();
    }
    mp_free(a);
    mp_free(x);
}

static void test_mp_arithmetic(mp_int *(*mp_arith)(mp_int *x, mp_int *y))
{
    mp_int *a = mp_new(256), *b = mp_new(512);
    for (size_t i = 0; i < looplimit(16); i++) {
        mp_random_fill(a);
        mp_random_fill(b);
        log_start();
        mp_int *r = mp_arith(a, b);
        log_end();
        mp_free(r);
    }
    mp_free(a);
    mp_free(b);
}

static void test_mp_add(void)
{
    test_mp_arithmetic(mp_add);
}

static void test_mp_sub(void)
{
    test_mp_arithmetic(mp_sub);
}

static void test_mp_mul(void)
{
    test_mp_arithmetic(mp_mul);
}

static void test_mp_invert(void)
{
    test_mp_arithmetic(mp_invert);
}

static void test_mp_rshift_safe(void)
{
    mp_int *x = mp_random_bits(256);

    for (size_t i = 0; i < looplimit(mp_max_bits(x)+1); i++) {
        log_start();
        mp_int *r = mp_rshift_safe(x, i);
        log_end();
        mp_free(r);
    }

    mp_free(x);
}

static void test_mp_divmod(void)
{
    mp_int *n = mp_new(256), *d = mp_new(256);
    mp_int *q = mp_new(256), *r = mp_new(256);

    for (size_t i = 0; i < looplimit(32); i++) {
        uint8_t sizes[2];
        random_read(sizes, 2);
        mp_random_bits_into(n, sizes[0]);
        mp_random_bits_into(d, sizes[1]);
        log_start();
        mp_divmod_into(n, d, q, r);
        log_end();
    }

    mp_free(n);
    mp_free(d);
    mp_free(q);
    mp_free(r);
}

static void test_mp_modarith(
    mp_int *(*mp_modarith)(mp_int *x, mp_int *y, mp_int *modulus))
{
    mp_int *base = mp_new(256);
    mp_int *exponent = mp_new(256);
    mp_int *modulus = mp_new(256);

    for (size_t i = 0; i < looplimit(8); i++) {
        mp_random_fill(base);
        mp_random_fill(exponent);
        mp_random_fill(modulus);
        mp_set_bit(modulus, 0, 1);    /* we only support odd moduli */

        log_start();
        mp_int *out = mp_modarith(base, exponent, modulus);
        log_end();

        mp_free(out);
    }
}

static void test_mp_modadd(void)
{
    test_mp_modarith(mp_modadd);
}

static void test_mp_modsub(void)
{
    test_mp_modarith(mp_modsub);
}

static void test_mp_modmul(void)
{
    test_mp_modarith(mp_modmul);
}

static void test_mp_modpow(void)
{
    test_mp_modarith(mp_modpow);
}

static void test_mp_invert_mod_2to(void)
{
    mp_int *x = mp_new(512);

    for (size_t i = 0; i < looplimit(32); i++) {
        mp_random_fill(x);
        mp_set_bit(x, 0, 1);           /* input should be odd */

        log_start();
        mp_int *out = mp_invert_mod_2to(x, 511);
        log_end();

        mp_free(out);
    }
}

static void test_mp_modsqrt(void)
{
    /* The prime isn't secret in this function (and in any case
     * finding a non-square on the fly would be prohibitively
     * annoying), so I hardcode a fixed one, selected to have a lot of
     * factors of two in p-1 so as to exercise lots of choices in the
     * algorithm. */
    mp_int *p =
        MP_LITERAL(0xb56a517b206a88c73cfa9ec6f704c7030d18212cace82401);
    mp_int *nonsquare = MP_LITERAL(0x5);
    size_t bits = mp_max_bits(p);
    ModsqrtContext *sc = modsqrt_new(p, nonsquare);
    mp_free(p);
    mp_free(nonsquare);

    mp_int *x = mp_new(bits);
    unsigned success;

    /* Do one initial call to cause the lazily initialised sub-context
     * to be set up. This will take a while, but it can't be helped. */
    mp_modsqrt(sc, x, &success);

    for (size_t i = 0; i < looplimit(8); i++) {
        mp_random_bits_into(x, bits - 1);
        log_start();
        mp_int *out = mp_modsqrt(sc, x, &success);
        log_end();
        mp_free(out);
    }

    mp_free(x);
}

static WeierstrassCurve *wcurve(void)
{
    mp_int *p = MP_LITERAL(0xc19337603dc856acf31e01375a696fdf5451);
    mp_int *a = MP_LITERAL(0x864946f50eecca4cde7abad4865e34be8f67);
    mp_int *b = MP_LITERAL(0x6a5bf56db3a03ba91cfbf3241916c90feeca);
    mp_int *nonsquare = mp_from_integer(3);
    WeierstrassCurve *wc = ecc_weierstrass_curve(p, a, b, nonsquare);
    mp_free(p);
    mp_free(a);
    mp_free(b);
    mp_free(nonsquare);
    return wc;
}

static WeierstrassPoint *wpoint(WeierstrassCurve *wc, size_t index)
{
    mp_int *x = NULL, *y = NULL;
    WeierstrassPoint *wp;
    switch (index) {
      case 0:
        break;
      case 1:
        x = MP_LITERAL(0x12345);
        y = MP_LITERAL(0x3c2c799a365b53d003ef37dab65860bf80ae);
        break;
      case 2:
        x = MP_LITERAL(0x4e1c77e3c00f7c3b15869e6a4e5f86b3ee53);
        y = MP_LITERAL(0x5bde01693130591400b5c9d257d8325a44a5);
        break;
      case 3:
        x = MP_LITERAL(0xb5f0e722b2f0f7e729f55ba9f15511e3b399);
        y = MP_LITERAL(0x033d636b855c931cfe679f0b18db164a0d64);
        break;
      case 4:
        x = MP_LITERAL(0xb5f0e722b2f0f7e729f55ba9f15511e3b399);
        y = MP_LITERAL(0xbe55d3f4b86bc38ff4b6622c418e599546ed);
        break;
      default:
        unreachable("only 5 example Weierstrass points defined");
    }
    if (x && y) {
        wp = ecc_weierstrass_point_new(wc, x, y);
    } else {
        wp = ecc_weierstrass_point_new_identity(wc);
    }
    if (x)
        mp_free(x);
    if (y)
        mp_free(y);
    return wp;
}

static void test_ecc_weierstrass_add(void)
{
    WeierstrassCurve *wc = wcurve();
    WeierstrassPoint *a = ecc_weierstrass_point_new_identity(wc);
    WeierstrassPoint *b = ecc_weierstrass_point_new_identity(wc);
    for (size_t i = 0; i < looplimit(5); i++) {
        for (size_t j = 0; j < looplimit(5); j++) {
            if (i == 0 || j == 0 || i == j ||
                (i==3 && j==4) || (i==4 && j==3))
                continue;              /* difficult cases */

            WeierstrassPoint *A = wpoint(wc, i), *B = wpoint(wc, j);
            ecc_weierstrass_point_copy_into(a, A);
            ecc_weierstrass_point_copy_into(b, B);
            ecc_weierstrass_point_free(A);
            ecc_weierstrass_point_free(B);

            log_start();
            WeierstrassPoint *r = ecc_weierstrass_add(a, b);
            log_end();
            ecc_weierstrass_point_free(r);
        }
    }
    ecc_weierstrass_point_free(a);
    ecc_weierstrass_point_free(b);
    ecc_weierstrass_curve_free(wc);
}

static void test_ecc_weierstrass_double(void)
{
    WeierstrassCurve *wc = wcurve();
    WeierstrassPoint *a = ecc_weierstrass_point_new_identity(wc);
    for (size_t i = 0; i < looplimit(5); i++) {
        WeierstrassPoint *A = wpoint(wc, i);
        ecc_weierstrass_point_copy_into(a, A);
        ecc_weierstrass_point_free(A);

        log_start();
        WeierstrassPoint *r = ecc_weierstrass_double(a);
        log_end();
        ecc_weierstrass_point_free(r);
    }
    ecc_weierstrass_point_free(a);
    ecc_weierstrass_curve_free(wc);
}

static void test_ecc_weierstrass_add_general(void)
{
    WeierstrassCurve *wc = wcurve();
    WeierstrassPoint *a = ecc_weierstrass_point_new_identity(wc);
    WeierstrassPoint *b = ecc_weierstrass_point_new_identity(wc);
    for (size_t i = 0; i < looplimit(5); i++) {
        for (size_t j = 0; j < looplimit(5); j++) {
            WeierstrassPoint *A = wpoint(wc, i), *B = wpoint(wc, j);
            ecc_weierstrass_point_copy_into(a, A);
            ecc_weierstrass_point_copy_into(b, B);
            ecc_weierstrass_point_free(A);
            ecc_weierstrass_point_free(B);

            log_start();
            WeierstrassPoint *r = ecc_weierstrass_add_general(a, b);
            log_end();
            ecc_weierstrass_point_free(r);
        }
    }
    ecc_weierstrass_point_free(a);
    ecc_weierstrass_point_free(b);
    ecc_weierstrass_curve_free(wc);
}

static void test_ecc_weierstrass_multiply(void)
{
    WeierstrassCurve *wc = wcurve();
    WeierstrassPoint *a = ecc_weierstrass_point_new_identity(wc);
    mp_int *exponent = mp_new(56);
    for (size_t i = 1; i < looplimit(5); i++) {
        WeierstrassPoint *A = wpoint(wc, i);
        ecc_weierstrass_point_copy_into(a, A);
        ecc_weierstrass_point_free(A);
        mp_random_fill(exponent);

        log_start();
        WeierstrassPoint *r = ecc_weierstrass_multiply(a, exponent);
        log_end();

        ecc_weierstrass_point_free(r);
    }
    ecc_weierstrass_point_free(a);
    ecc_weierstrass_curve_free(wc);
}

static void test_ecc_weierstrass_is_identity(void)
{
    WeierstrassCurve *wc = wcurve();
    WeierstrassPoint *a = ecc_weierstrass_point_new_identity(wc);
    for (size_t i = 1; i < looplimit(5); i++) {
        WeierstrassPoint *A = wpoint(wc, i);
        ecc_weierstrass_point_copy_into(a, A);
        ecc_weierstrass_point_free(A);

        log_start();
        ecc_weierstrass_is_identity(a);
        log_end();
    }
    ecc_weierstrass_point_free(a);
    ecc_weierstrass_curve_free(wc);
}

static void test_ecc_weierstrass_get_affine(void)
{
    WeierstrassCurve *wc = wcurve();
    WeierstrassPoint *r = ecc_weierstrass_point_new_identity(wc);
    for (size_t i = 0; i < looplimit(4); i++) {
        WeierstrassPoint *A = wpoint(wc, i), *B = wpoint(wc, i+1);
        WeierstrassPoint *R = ecc_weierstrass_add_general(A, B);
        ecc_weierstrass_point_copy_into(r, R);
        ecc_weierstrass_point_free(A);
        ecc_weierstrass_point_free(B);
        ecc_weierstrass_point_free(R);

        log_start();
        mp_int *x, *y;
        ecc_weierstrass_get_affine(r, &x, &y);
        log_end();
        mp_free(x);
        mp_free(y);
    }
    ecc_weierstrass_point_free(r);
    ecc_weierstrass_curve_free(wc);
}

static void test_ecc_weierstrass_decompress(void)
{
    WeierstrassCurve *wc = wcurve();

    /* As in the mp_modsqrt test, prime the lazy initialisation of the
     * ModsqrtContext */
    mp_int *x = mp_new(144);
    WeierstrassPoint *a = ecc_weierstrass_point_new_from_x(wc, x, 0);
    if (a)                 /* don't care whether this one succeeded */
        ecc_weierstrass_point_free(a);

    for (size_t p = 0; p < looplimit(2); p++) {
        for (size_t i = 1; i < looplimit(5); i++) {
            WeierstrassPoint *A = wpoint(wc, i);
            mp_int *X;
            ecc_weierstrass_get_affine(A, &X, NULL);
            mp_copy_into(x, X);
            mp_free(X);
            ecc_weierstrass_point_free(A);

            log_start();
            WeierstrassPoint *a = ecc_weierstrass_point_new_from_x(wc, x, p);
            log_end();

            ecc_weierstrass_point_free(a);
        }
    }
    mp_free(x);
    ecc_weierstrass_curve_free(wc);
}

static MontgomeryCurve *mcurve(void)
{
    mp_int *p = MP_LITERAL(0xde978eb1db35236a5792e9f0c04d86000659);
    mp_int *a = MP_LITERAL(0x799b62a612b1b30e1c23cea6d67b2e33c51a);
    mp_int *b = MP_LITERAL(0x944bf9042b56821a8c9e0b49b636c2502b2b);
    MontgomeryCurve *mc = ecc_montgomery_curve(p, a, b);
    mp_free(p);
    mp_free(a);
    mp_free(b);
    return mc;
}

static MontgomeryPoint *mpoint(MontgomeryCurve *wc, size_t index)
{
    mp_int *x = NULL;
    MontgomeryPoint *mp;
    switch (index) {
      case 0:
        x = MP_LITERAL(31415);
        break;
      case 1:
        x = MP_LITERAL(0x4d352c654c06eecfe19104118857b38398e8);
        break;
      case 2:
        x = MP_LITERAL(0x03fca2a73983bc3434caae3134599cd69cce);
        break;
      case 3:
        x = MP_LITERAL(0xa0fd735ce9b3406498b5f035ee655bda4e15);
        break;
      case 4:
        x = MP_LITERAL(0x7c7f46a00cc286dbe47db39b6d8f5efd920e);
        break;
      case 5:
        x = MP_LITERAL(0x07a6dc30d3b320448e6f8999be417e6b7c6b);
        break;
      case 6:
        x = MP_LITERAL(0x7832da5fc16dfbd358170b2b96896cd3cd06);
        break;
      default:
        unreachable("only 7 example Weierstrass points defined");
    }
    mp = ecc_montgomery_point_new(wc, x);
    mp_free(x);
    return mp;
}

static void test_ecc_montgomery_diff_add(void)
{
    MontgomeryCurve *wc = mcurve();
    MontgomeryPoint *a = NULL, *b = NULL, *c = NULL;
    for (size_t i = 0; i < looplimit(5); i++) {
        MontgomeryPoint *A = mpoint(wc, i);
        MontgomeryPoint *B = mpoint(wc, i);
        MontgomeryPoint *C = mpoint(wc, i);
        if (!a) {
            a = A;
            b = B;
            c = C;
        } else {
            ecc_montgomery_point_copy_into(a, A);
            ecc_montgomery_point_copy_into(b, B);
            ecc_montgomery_point_copy_into(c, C);
            ecc_montgomery_point_free(A);
            ecc_montgomery_point_free(B);
            ecc_montgomery_point_free(C);
        }

        log_start();
        MontgomeryPoint *r = ecc_montgomery_diff_add(b, c, a);
        log_end();

        ecc_montgomery_point_free(r);
    }
    ecc_montgomery_point_free(a);
    ecc_montgomery_point_free(b);
    ecc_montgomery_point_free(c);
    ecc_montgomery_curve_free(wc);
}

static void test_ecc_montgomery_double(void)
{
    MontgomeryCurve *wc = mcurve();
    MontgomeryPoint *a = NULL;
    for (size_t i = 0; i < looplimit(7); i++) {
        MontgomeryPoint *A = mpoint(wc, i);
        if (!a) {
            a = A;
        } else {
            ecc_montgomery_point_copy_into(a, A);
            ecc_montgomery_point_free(A);
        }

        log_start();
        MontgomeryPoint *r = ecc_montgomery_double(a);
        log_end();

        ecc_montgomery_point_free(r);
    }
    ecc_montgomery_point_free(a);
    ecc_montgomery_curve_free(wc);
}

static void test_ecc_montgomery_multiply(void)
{
    MontgomeryCurve *wc = mcurve();
    MontgomeryPoint *a = NULL;
    mp_int *exponent = mp_new(56);
    for (size_t i = 0; i < looplimit(7); i++) {
        MontgomeryPoint *A = mpoint(wc, i);
        if (!a) {
            a = A;
        } else {
            ecc_montgomery_point_copy_into(a, A);
            ecc_montgomery_point_free(A);
        }
        mp_random_fill(exponent);

        log_start();
        MontgomeryPoint *r = ecc_montgomery_multiply(a, exponent);
        log_end();

        ecc_montgomery_point_free(r);
    }
    ecc_montgomery_point_free(a);
    ecc_montgomery_curve_free(wc);
}

static void test_ecc_montgomery_get_affine(void)
{
    MontgomeryCurve *wc = mcurve();
    MontgomeryPoint *r = NULL;
    for (size_t i = 0; i < looplimit(5); i++) {
        MontgomeryPoint *A = mpoint(wc, i);
        MontgomeryPoint *B = mpoint(wc, i);
        MontgomeryPoint *C = mpoint(wc, i);
        MontgomeryPoint *R = ecc_montgomery_diff_add(B, C, A);
        ecc_montgomery_point_free(A);
        ecc_montgomery_point_free(B);
        ecc_montgomery_point_free(C);
        if (!r) {
            r = R;
        } else {
            ecc_montgomery_point_copy_into(r, R);
            ecc_montgomery_point_free(R);
        }

        log_start();
        mp_int *x;
        ecc_montgomery_get_affine(r, &x);
        log_end();

        mp_free(x);
    }
    ecc_montgomery_point_free(r);
    ecc_montgomery_curve_free(wc);
}

static EdwardsCurve *ecurve(void)
{
    mp_int *p = MP_LITERAL(0xfce2dac1704095de0b5c48876c45063cd475);
    mp_int *d = MP_LITERAL(0xbd4f77401c3b14ae1742a7d1d367adac8f3e);
    mp_int *a = MP_LITERAL(0x51d0845da3fa871aaac4341adea53b861919);
    mp_int *nonsquare = mp_from_integer(2);
    EdwardsCurve *ec = ecc_edwards_curve(p, d, a, nonsquare);
    mp_free(p);
    mp_free(d);
    mp_free(a);
    mp_free(nonsquare);
    return ec;
}

static EdwardsPoint *epoint(EdwardsCurve *wc, size_t index)
{
    mp_int *x, *y;
    EdwardsPoint *ep;
    switch (index) {
      case 0:
        x = MP_LITERAL(0x0);
        y = MP_LITERAL(0x1);
        break;
      case 1:
        x = MP_LITERAL(0x3d8aef0294a67c1c7e8e185d987716250d7c);
        y = MP_LITERAL(0x27184);
        break;
      case 2:
        x = MP_LITERAL(0xf44ed5b8a6debfd3ab24b7874cd2589fd672);
        y = MP_LITERAL(0xd635d8d15d367881c8a3af472c8fe487bf40);
        break;
      case 3:
        x = MP_LITERAL(0xde114ecc8b944684415ef81126a07269cd30);
        y = MP_LITERAL(0xbe0fd45ff67ebba047ed0ec5a85d22e688a1);
        break;
      case 4:
        x = MP_LITERAL(0x76bd2f90898d271b492c9c20dd7bbfe39fe5);
        y = MP_LITERAL(0xbf1c82698b4a5a12c1057631c1ebdc216ae2);
        break;
      default:
        unreachable("only 5 example Edwards points defined");
    }
    ep = ecc_edwards_point_new(wc, x, y);
    mp_free(x);
    mp_free(y);
    return ep;
}

static void test_ecc_edwards_add(void)
{
    EdwardsCurve *ec = ecurve();
    EdwardsPoint *a = NULL, *b = NULL;
    for (size_t i = 0; i < looplimit(5); i++) {
        for (size_t j = 0; j < looplimit(5); j++) {
            EdwardsPoint *A = epoint(ec, i), *B = epoint(ec, j);
            if (!a) {
                a = A;
                b = B;
            } else {
                ecc_edwards_point_copy_into(a, A);
                ecc_edwards_point_copy_into(b, B);
                ecc_edwards_point_free(A);
                ecc_edwards_point_free(B);
            }

            log_start();
            EdwardsPoint *r = ecc_edwards_add(a, b);
            log_end();

            ecc_edwards_point_free(r);
        }
    }
    ecc_edwards_point_free(a);
    ecc_edwards_point_free(b);
    ecc_edwards_curve_free(ec);
}

static void test_ecc_edwards_multiply(void)
{
    EdwardsCurve *ec = ecurve();
    EdwardsPoint *a = NULL;
    mp_int *exponent = mp_new(56);
    for (size_t i = 1; i < looplimit(5); i++) {
        EdwardsPoint *A = epoint(ec, i);
        if (!a) {
            a = A;
        } else {
            ecc_edwards_point_copy_into(a, A);
            ecc_edwards_point_free(A);
        }
        mp_random_fill(exponent);

        log_start();
        EdwardsPoint *r = ecc_edwards_multiply(a, exponent);
        log_end();

        ecc_edwards_point_free(r);
    }
    ecc_edwards_point_free(a);
    ecc_edwards_curve_free(ec);
}

static void test_ecc_edwards_eq(void)
{
    EdwardsCurve *ec = ecurve();
    EdwardsPoint *a = NULL, *b = NULL;
    for (size_t i = 0; i < looplimit(5); i++) {
        for (size_t j = 0; j < looplimit(5); j++) {
            EdwardsPoint *A = epoint(ec, i), *B = epoint(ec, j);
            if (!a) {
                a = A;
                b = B;
            } else {
                ecc_edwards_point_copy_into(a, A);
                ecc_edwards_point_copy_into(b, B);
                ecc_edwards_point_free(A);
                ecc_edwards_point_free(B);
            }

            log_start();
            ecc_edwards_eq(a, b);
            log_end();
        }
    }
    ecc_edwards_point_free(a);
    ecc_edwards_point_free(b);
    ecc_edwards_curve_free(ec);
}

static void test_ecc_edwards_get_affine(void)
{
    EdwardsCurve *ec = ecurve();
    EdwardsPoint *r = NULL;
    for (size_t i = 0; i < looplimit(4); i++) {
        EdwardsPoint *A = epoint(ec, i), *B = epoint(ec, i+1);
        EdwardsPoint *R = ecc_edwards_add(A, B);
        ecc_edwards_point_free(A);
        ecc_edwards_point_free(B);
        if (!r) {
            r = R;
        } else {
            ecc_edwards_point_copy_into(r, R);
            ecc_edwards_point_free(R);
        }

        log_start();
        mp_int *x, *y;
        ecc_edwards_get_affine(r, &x, &y);
        log_end();

        mp_free(x);
        mp_free(y);
    }
    ecc_edwards_point_free(r);
    ecc_edwards_curve_free(ec);
}

static void test_ecc_edwards_decompress(void)
{
    EdwardsCurve *ec = ecurve();

    /* As in the mp_modsqrt test, prime the lazy initialisation of the
     * ModsqrtContext */
    mp_int *y = mp_new(144);
    EdwardsPoint *a = ecc_edwards_point_new_from_y(ec, y, 0);
    if (a)                 /* don't care whether this one succeeded */
        ecc_edwards_point_free(a);

    for (size_t p = 0; p < looplimit(2); p++) {
        for (size_t i = 0; i < looplimit(5); i++) {
            EdwardsPoint *A = epoint(ec, i);
            mp_int *Y;
            ecc_edwards_get_affine(A, NULL, &Y);
            mp_copy_into(y, Y);
            mp_free(Y);
            ecc_edwards_point_free(A);

            log_start();
            EdwardsPoint *a = ecc_edwards_point_new_from_y(ec, y, p);
            log_end();

            ecc_edwards_point_free(a);
        }
    }
    mp_free(y);
    ecc_edwards_curve_free(ec);
}

static void test_cipher(const ssh_cipheralg *calg)
{
    ssh_cipher *c = ssh_cipher_new(calg);
    if (!c) {
        test_skipped = true;
        return;
    }
    const ssh2_macalg *malg = calg->required_mac;
    ssh2_mac *m = NULL;
    if (malg) {
        m = ssh2_mac_new(malg, c);
        if (!m) {
            ssh_cipher_free(c);
            test_skipped = true;
            return;
        }
    }

    uint8_t *ckey = snewn(calg->padded_keybytes, uint8_t);
    uint8_t *civ = snewn(calg->blksize, uint8_t);
    uint8_t *mkey = malg ? snewn(malg->keylen, uint8_t) : NULL;
    size_t datalen = calg->blksize * 8;
    size_t maclen = malg ? malg->len : 0;
    uint8_t *data = snewn(datalen + maclen, uint8_t);
    size_t lenlen = 4;
    uint8_t *lendata = snewn(lenlen, uint8_t);

    for (size_t i = 0; i < looplimit(16); i++) {
        random_read(ckey, calg->padded_keybytes);
        if (malg)
            random_read(mkey, malg->keylen);
        random_read(data, datalen);
        random_read(lendata, lenlen);
        if (i == 0) {
            /* Ensure one of our test IVs will cause SDCTR wraparound */
            memset(civ, 0xFF, calg->blksize);
        } else {
            random_read(civ, calg->blksize);
        }
        uint8_t seqbuf[4];
        random_read(seqbuf, 4);
        uint32_t seq = GET_32BIT_MSB_FIRST(seqbuf);

        log_start();
        ssh_cipher_setkey(c, ckey);
        ssh_cipher_setiv(c, civ);
        if (m)
            ssh2_mac_setkey(m, make_ptrlen(mkey, malg->keylen));
        if (calg->flags & SSH_CIPHER_SEPARATE_LENGTH)
            ssh_cipher_encrypt_length(c, data, datalen, seq);
        ssh_cipher_encrypt(c, data, datalen);
        if (m) {
            ssh2_mac_generate(m, data, datalen, seq);
            ssh2_mac_verify(m, data, datalen, seq);
        }
        if (calg->flags & SSH_CIPHER_SEPARATE_LENGTH)
            ssh_cipher_decrypt_length(c, data, datalen, seq);
        ssh_cipher_decrypt(c, data, datalen);
        log_end();
    }

    sfree(ckey);
    sfree(civ);
    sfree(mkey);
    sfree(data);
    sfree(lendata);
    if (m)
        ssh2_mac_free(m);
    ssh_cipher_free(c);
}

#define CIPHER_TESTFN(Y_unused, cipher)                                 \
    static void test_cipher_##cipher(void) { test_cipher(&cipher); }
CIPHERS(CIPHER_TESTFN, Y_unused)

static void test_mac(const ssh2_macalg *malg)
{
    ssh2_mac *m = ssh2_mac_new(malg, NULL);
    if (!m) {
        test_skipped = true;
        return;
    }

    uint8_t *mkey = malg ? snewn(malg->keylen, uint8_t) : NULL;
    size_t datalen = 256;
    size_t maclen = malg ? malg->len : 0;
    uint8_t *data = snewn(datalen + maclen, uint8_t);

    /* Preliminarily key the MAC, to avoid the divergence of control
     * flow in which hmac_key() avoids some free()s the first time
     * through */
    random_read(mkey, malg->keylen);
    ssh2_mac_setkey(m, make_ptrlen(mkey, malg->keylen));

    for (size_t i = 0; i < looplimit(16); i++) {
        random_read(mkey, malg->keylen);
        random_read(data, datalen);
        uint8_t seqbuf[4];
        random_read(seqbuf, 4);
        uint32_t seq = GET_32BIT_MSB_FIRST(seqbuf);

        log_start();
        ssh2_mac_setkey(m, make_ptrlen(mkey, malg->keylen));
        ssh2_mac_generate(m, data, datalen, seq);
        ssh2_mac_verify(m, data, datalen, seq);
        log_end();
    }

    sfree(mkey);
    sfree(data);
    ssh2_mac_free(m);
}

#define MAC_TESTFN(Y_unused, mac)                                 \
    static void test_mac_##mac(void) { test_mac(&mac); }
MACS(MAC_TESTFN, Y_unused)

static void test_hash(const ssh_hashalg *halg)
{
    ssh_hash *h = ssh_hash_new(halg);
    if (!h) {
        test_skipped = true;
        return;
    }

    size_t datalen = 256;
    uint8_t *data = snewn(datalen, uint8_t);
    uint8_t *hash = snewn(halg->hlen, uint8_t);

    for (size_t i = 0; i < looplimit(16); i++) {
        random_read(data, datalen);

        log_start();
        put_data(h, data, datalen);
        ssh_hash_final(h, hash);
        log_end();

        h = ssh_hash_new(halg);
    }

    sfree(data);
    sfree(hash);
    ssh_hash_free(h);
}

#define HASH_TESTFN(Y_unused, hash)                             \
    static void test_hash_##hash(void) { test_hash(&hash); }
HASHES(HASH_TESTFN, Y_unused)

struct test {
    const char *testname;
    void (*testfn)(void);
};

static const struct test tests[] = {
#define STRUCT_TEST(X) { #X, test_##X },
TESTLIST(STRUCT_TEST)
#undef STRUCT_TEST
};

int main(int argc, char **argv)
{
    bool doing_opts = true;
    const char *pname = argv[0];
    uint8_t tests_to_run[lenof(tests)];
    bool keep_outfiles = false;
    bool test_names_given = false;

    memset(tests_to_run, 1, sizeof(tests_to_run));

    while (--argc > 0) {
        char *p = *++argv;

        if (p[0] == '-' && doing_opts) {
            if (!strcmp(p, "-O")) {
                if (--argc <= 0) {
                    fprintf(stderr, "'-O' expects a directory name\n");
                    return 1;
                }
                outdir = *++argv;
            } else if (!strcmp(p, "-k") || !strcmp(p, "--keep")) {
                keep_outfiles = true;
            } else if (!strcmp(p, "--")) {
                doing_opts = false;
            } else if (!strcmp(p, "--help")) {
                printf("  usage: drrun -c test/sclog/libsclog.so -- "
                       "%s -O <outdir>\n", pname);
                printf("options: -O <outdir>           "
                       "put log files in the specified directory\n");
                printf("         -k, --keep            "
                       "do not delete log files for tests that passed\n");
                printf("   also: --help                "
                       "display this text\n");
                return 0;
            } else {
                fprintf(stderr, "unknown command line option '%s'\n", p);
                return 1;
            }
        } else {
            if (!test_names_given) {
                test_names_given = true;
                memset(tests_to_run, 0, sizeof(tests_to_run));
            }
            bool found_one = false;
            for (size_t i = 0; i < lenof(tests); i++) {
                if (wc_match(p, tests[i].testname)) {
                    tests_to_run[i] = 1;
                    found_one = true;
                }
            }
            if (!found_one) {
                fprintf(stderr, "no test name matched '%s'\n", p);
                return 1;
            }
        }
    }

    bool is_dry_run = dry_run();

    if (is_dry_run) {
        printf("Dry run (DynamoRIO instrumentation not detected)\n");
    } else {
        if (!outdir) {
            fprintf(stderr, "expected -O <outdir> option\n");
            return 1;
        }
        printf("Will write log files to %s\n", outdir);
    }

    size_t nrun = 0, npass = 0;

    for (size_t i = 0; i < lenof(tests); i++) {
        bool keep_these_outfiles = true;

        if (!tests_to_run[i])
            continue;
        const struct test *test = &tests[i];
        printf("Running test %s ... ", test->testname);
        fflush(stdout);

        test_skipped = false;
        random_seed(test->testname);
        test_basename = test->testname;
        test_index = 0;

        test->testfn();

        if (test_skipped) {
            /* Used for e.g. tests of hardware-accelerated crypto when
             * the hardware acceleration isn't available */
            printf("skipped\n");
            continue;
        }

        nrun++;

        if (is_dry_run) {
            printf("dry run done\n");
            continue;                  /* test files won't exist anyway */
        }

        if (test_index < 2) {
            printf("FAIL: test did not generate multiple output files\n");
            goto test_done;
        }

        char *firstfile = log_filename(test_basename, 0);
        FILE *firstfp = fopen(firstfile, "rb");
        if (!firstfp) {
            printf("ERR: %s: open: %s\n", firstfile, strerror(errno));
            goto test_done;
        }
        for (size_t i = 1; i < test_index; i++) {
            char *nextfile = log_filename(test_basename, i);
            FILE *nextfp = fopen(nextfile, "rb");
            if (!nextfp) {
                printf("ERR: %s: open: %s\n", nextfile, strerror(errno));
                goto test_done;
            }

            rewind(firstfp);
            char buf1[4096], bufn[4096];
            bool compare_ok = false;
            while (true) {
                size_t r1 = fread(buf1, 1, sizeof(buf1), firstfp);
                size_t rn = fread(bufn, 1, sizeof(bufn), nextfp);
                if (r1 != rn) {
                    printf("FAIL: %s %s: different lengths\n",
                           firstfile, nextfile);
                    break;
                }
                if (r1 == 0) {
                    if (feof(firstfp) && feof(nextfp)) {
                        compare_ok = true;
                    } else {
                        printf("FAIL: %s %s: error at end of file\n",
                               firstfile, nextfile);
                    }
                    break;
                }
                if (memcmp(buf1, bufn, r1) != 0) {
                    printf("FAIL: %s %s: different content\n",
                           firstfile, nextfile);
                    break;
                }
            }
            fclose(nextfp);
            sfree(nextfile);
            if (!compare_ok) {
                goto test_done;
            }
        }
        fclose(firstfp);
        sfree(firstfile);

        printf("pass\n");
        npass++;
        keep_these_outfiles = keep_outfiles;

      test_done:
        if (!keep_these_outfiles) {
            for (size_t i = 0; i < test_index; i++) {
                char *file = log_filename(test_basename, i);
                remove(file);
                sfree(file);
            }
        }
    }

    if (npass == nrun) {
        printf("All tests passed\n");
        return 0;
    } else {
        printf("%zu tests failed\n", nrun - npass);
        return 1;
    }
}
