/*
 * Copyright (c) 2013 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* Optional external libraries; can be enabled using:
 * make VERSUS=crypto+gcrypt+tomcrypt tools/crypto_bench */
#define USE_crypto           0x01    /* OpenSSL's libcrypto */
#define USE_gcrypt           0x02    /* GnuTLS's libgcrypt */
#define USE_tomcrypt         0x04    /* LibTomCrypt */

#include <stdlib.h>
#include <math.h>

#include "libavutil/avutil.h"
#include "libavutil/avstring.h"
#include "libavutil/crc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/timer.h"

#ifndef AV_READ_TIME
#define AV_READ_TIME(x) 0
#endif

#if HAVE_UNISTD_H
#include <unistd.h> /* for getopt */
#endif
#if !HAVE_GETOPT
#include "compat/getopt.c"
#endif

#define MAX_INPUT_SIZE 1048576
#define MAX_OUTPUT_SIZE 128

static const char *enabled_libs;
static const char *enabled_algos;
static unsigned specified_runs;

static const uint8_t *hardcoded_key = "FFmpeg is the best program ever.";

static void fatal_error(const char *tag)
{
    av_log(NULL, AV_LOG_ERROR, "Fatal error: %s\n", tag);
    exit(1);
}

struct hash_impl {
    const char *lib;
    const char *name;
    void (*run)(uint8_t *output, const uint8_t *input, unsigned size);
    const char *output;
};

/***************************************************************************
 * lavu: libavutil
 ***************************************************************************/

#include "libavutil/md5.h"
#include "libavutil/sha.h"
#include "libavutil/sha512.h"
#include "libavutil/ripemd.h"
#include "libavutil/aes.h"
#include "libavutil/camellia.h"
#include "libavutil/cast5.h"
#include "libavutil/twofish.h"

#define IMPL_USE_lavu IMPL_USE

static void run_lavu_md5(uint8_t *output,
                         const uint8_t *input, unsigned size)
{
    av_md5_sum(output, input, size);
}

#define DEFINE_LAVU_MD(suffix, type, namespace, hsize)                       \
static void run_lavu_ ## suffix(uint8_t *output,                             \
                                const uint8_t *input, unsigned size)         \
{                                                                            \
    static struct type *h;                                                   \
    if (!h && !(h = av_ ## namespace ## _alloc()))                           \
        fatal_error("out of memory");                                        \
    av_ ## namespace ## _init(h, hsize);                                     \
    av_ ## namespace ## _update(h, input, size);                             \
    av_ ## namespace ## _final(h, output);                                   \
}

DEFINE_LAVU_MD(sha1,      AVSHA,    sha, 160);
DEFINE_LAVU_MD(sha256,    AVSHA,    sha, 256);
DEFINE_LAVU_MD(sha512,    AVSHA512, sha512, 512);
DEFINE_LAVU_MD(ripemd160, AVRIPEMD, ripemd, 160);

static void run_lavu_aes128(uint8_t *output,
                            const uint8_t *input, unsigned size)
{
    static struct AVAES *aes;
    if (!aes && !(aes = av_aes_alloc()))
        fatal_error("out of memory");
    av_aes_init(aes, hardcoded_key, 128, 0);
    av_aes_crypt(aes, output, input, size >> 4, NULL, 0);
}

static void run_lavu_camellia(uint8_t *output,
                              const uint8_t *input, unsigned size)
{
    static struct AVCAMELLIA *camellia;
    if (!camellia && !(camellia = av_camellia_alloc()))
        fatal_error("out of memory");
    av_camellia_init(camellia, hardcoded_key, 128);
    av_camellia_crypt(camellia, output, input, size >> 4, NULL, 0);
}

static void run_lavu_cast128(uint8_t *output,
                             const uint8_t *input, unsigned size)
{
    static struct AVCAST5 *cast;
    if (!cast && !(cast = av_cast5_alloc()))
        fatal_error("out of memory");
    av_cast5_init(cast, hardcoded_key, 128);
    av_cast5_crypt(cast, output, input, size >> 3, 0);
}

static void run_lavu_twofish(uint8_t *output,
                              const uint8_t *input, unsigned size)
{
    static struct AVTWOFISH *twofish;
    if (!twofish && !(twofish = av_twofish_alloc()))
        fatal_error("out of memory");
    av_twofish_init(twofish, hardcoded_key, 128);
    av_twofish_crypt(twofish, output, input, size >> 4, NULL, 0);
}
/***************************************************************************
 * crypto: OpenSSL's libcrypto
 ***************************************************************************/

#if (USE_EXT_LIBS) & USE_crypto

#include <openssl/md5.h>
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <openssl/aes.h>
#include <openssl/camellia.h>
#include <openssl/cast.h>

#define DEFINE_CRYPTO_WRAPPER(suffix, function)                              \
static void run_crypto_ ## suffix(uint8_t *output,                           \
                                  const uint8_t *input, unsigned size)       \
{                                                                            \
    function(input, size, output);                                           \
}

DEFINE_CRYPTO_WRAPPER(md5,       MD5)
DEFINE_CRYPTO_WRAPPER(sha1,      SHA1)
DEFINE_CRYPTO_WRAPPER(sha256,    SHA256)
DEFINE_CRYPTO_WRAPPER(sha512,    SHA512)
DEFINE_CRYPTO_WRAPPER(ripemd160, RIPEMD160)

static void run_crypto_aes128(uint8_t *output,
                              const uint8_t *input, unsigned size)
{
    AES_KEY aes;
    unsigned i;

    AES_set_encrypt_key(hardcoded_key, 128, &aes);
    size -= 15;
    for (i = 0; i < size; i += 16)
        AES_encrypt(input + i, output + i, &aes);
}

static void run_crypto_camellia(uint8_t *output,
                                const uint8_t *input, unsigned size)
{
    CAMELLIA_KEY camellia;
    unsigned i;

    Camellia_set_key(hardcoded_key, 128, &camellia);
    size -= 15;
    for (i = 0; i < size; i += 16)
        Camellia_ecb_encrypt(input + i, output + i, &camellia, 1);
}

static void run_crypto_cast128(uint8_t *output,
                               const uint8_t *input, unsigned size)
{
    CAST_KEY cast;
    unsigned i;

    CAST_set_key(&cast, 16, hardcoded_key);
    for (i = 0; i < size; i += 8)
        CAST_ecb_encrypt(input + i, output + i, &cast, 1);
}

#define IMPL_USE_crypto(...) IMPL_USE(__VA_ARGS__)
#else
#define IMPL_USE_crypto(...) /* ignore */
#endif

/***************************************************************************
 * gcrypt: GnuTLS's libgcrypt
 ***************************************************************************/

#if (USE_EXT_LIBS) & USE_gcrypt

#include <gcrypt.h>

#define DEFINE_GCRYPT_WRAPPER(suffix, algo)                                  \
static void run_gcrypt_ ## suffix(uint8_t *output,                           \
                                  const uint8_t *input, unsigned size)       \
{                                                                            \
    gcry_md_hash_buffer(GCRY_MD_ ## algo, output, input, size);              \
}

DEFINE_GCRYPT_WRAPPER(md5,       MD5)
DEFINE_GCRYPT_WRAPPER(sha1,      SHA1)
DEFINE_GCRYPT_WRAPPER(sha256,    SHA256)
DEFINE_GCRYPT_WRAPPER(sha512,    SHA512)
DEFINE_GCRYPT_WRAPPER(ripemd160, RMD160)

static void run_gcrypt_aes128(uint8_t *output,
                              const uint8_t *input, unsigned size)
{
    static gcry_cipher_hd_t aes;
    if (!aes)
        gcry_cipher_open(&aes, GCRY_CIPHER_AES128, GCRY_CIPHER_MODE_ECB, 0);
    gcry_cipher_setkey(aes, hardcoded_key, 16);
    gcry_cipher_encrypt(aes, output, size, input, size);
}

static void run_gcrypt_camellia(uint8_t *output,
                                const uint8_t *input, unsigned size)
{
    static gcry_cipher_hd_t camellia;
    if (!camellia)
        gcry_cipher_open(&camellia, GCRY_CIPHER_CAMELLIA128, GCRY_CIPHER_MODE_ECB, 0);
    gcry_cipher_setkey(camellia, hardcoded_key, 16);
    gcry_cipher_encrypt(camellia, output, size, input, size);
}

static void run_gcrypt_cast128(uint8_t *output,
                              const uint8_t *input, unsigned size)
{
    static gcry_cipher_hd_t cast;
    if (!cast)
        gcry_cipher_open(&cast, GCRY_CIPHER_CAST5, GCRY_CIPHER_MODE_ECB, 0);
    gcry_cipher_setkey(cast, hardcoded_key, 16);
    gcry_cipher_encrypt(cast, output, size, input, size);
}

static void run_gcrypt_twofish(uint8_t *output,
                                const uint8_t *input, unsigned size)
{
    static gcry_cipher_hd_t twofish;
    if (!twofish)
        gcry_cipher_open(&twofish, GCRY_CIPHER_TWOFISH128, GCRY_CIPHER_MODE_ECB, 0);
    gcry_cipher_setkey(twofish, hardcoded_key, 16);
    gcry_cipher_encrypt(twofish, output, size, input, size);
}

#define IMPL_USE_gcrypt(...) IMPL_USE(__VA_ARGS__)
#else
#define IMPL_USE_gcrypt(...) /* ignore */
#endif

/***************************************************************************
 * tomcrypt: LibTomCrypt
 ***************************************************************************/

#if (USE_EXT_LIBS) & USE_tomcrypt

#include <tomcrypt.h>

#define DEFINE_TOMCRYPT_WRAPPER(suffix, namespace, algo)                     \
static void run_tomcrypt_ ## suffix(uint8_t *output,                         \
                                    const uint8_t *input, unsigned size)     \
{                                                                            \
    hash_state md;                                                           \
    namespace ## _init(&md);                                                 \
    namespace ## _process(&md, input, size);                                 \
    namespace ## _done(&md, output);                                         \
}

DEFINE_TOMCRYPT_WRAPPER(md5,       md5,    MD5)
DEFINE_TOMCRYPT_WRAPPER(sha1,      sha1,   SHA1)
DEFINE_TOMCRYPT_WRAPPER(sha256,    sha256, SHA256)
DEFINE_TOMCRYPT_WRAPPER(sha512,    sha512, SHA512)
DEFINE_TOMCRYPT_WRAPPER(ripemd160, rmd160, RIPEMD160)

static void run_tomcrypt_aes128(uint8_t *output,
                                const uint8_t *input, unsigned size)
{
    symmetric_key aes;
    unsigned i;

    aes_setup(hardcoded_key, 16, 0, &aes);
    size -= 15;
    for (i = 0; i < size; i += 16)
        aes_ecb_encrypt(input + i, output + i, &aes);
}

static void run_tomcrypt_camellia(uint8_t *output,
                                  const uint8_t *input, unsigned size)
{
    symmetric_key camellia;
    unsigned i;

    camellia_setup(hardcoded_key, 16, 0, &camellia);
    size -= 15;
    for (i = 0; i < size; i += 16)
        camellia_ecb_encrypt(input + i, output + i, &camellia);
}

static void run_tomcrypt_cast128(uint8_t *output,
                                const uint8_t *input, unsigned size)
{
    symmetric_key cast;
    unsigned i;

    cast5_setup(hardcoded_key, 16, 0, &cast);
    for (i = 0; i < size; i += 8)
        cast5_ecb_encrypt(input + i, output + i, &cast);
}

static void run_tomcrypt_twofish(uint8_t *output,
                                const uint8_t *input, unsigned size)
{
    symmetric_key twofish;
    unsigned i;

    twofish_setup(hardcoded_key, 16, 0, &twofish);
    size -= 15;
    for (i = 0; i < size; i += 16)
        twofish_ecb_encrypt(input + i, output + i, &twofish);
}


#define IMPL_USE_tomcrypt(...) IMPL_USE(__VA_ARGS__)
#else
#define IMPL_USE_tomcrypt(...) /* ignore */
#endif

/***************************************************************************
 * Driver code
 ***************************************************************************/

static unsigned crc32(const uint8_t *data, unsigned size)
{
    return av_crc(av_crc_get_table(AV_CRC_32_IEEE), 0, data, size);
}

static void run_implementation(const uint8_t *input, uint8_t *output,
                               struct hash_impl *impl, unsigned size)
{
    uint64_t t0, t1;
    unsigned nruns = specified_runs ? specified_runs : (1 << 30) / size;
    unsigned outlen = 0, outcrc = 0;
    unsigned i, j, val;
    double mtime, ttime = 0, ttime2 = 0, stime;
    uint8_t outref[MAX_OUTPUT_SIZE];

    if (enabled_libs  && !av_stristr(enabled_libs,  impl->lib) ||
        enabled_algos && !av_stristr(enabled_algos, impl->name))
        return;
    if (!sscanf(impl->output, "crc:%x", &outcrc)) {
        outlen = strlen(impl->output) / 2;
        for (i = 0; i < outlen; i++) {
            sscanf(impl->output + i * 2, "%02x", &val);
            outref[i] = val;
        }
    }
    for (i = 0; i < 8; i++) /* heat caches */
        impl->run(output, input, size);
    for (i = 0; i < nruns; i++) {
        memset(output, 0, size); /* avoid leftovers from previous runs */
        t0 = AV_READ_TIME();
        impl->run(output, input, size);
        t1 = AV_READ_TIME();
        if (outlen ? memcmp(output, outref, outlen) :
                     crc32(output, size) != outcrc) {
            fprintf(stderr, "Expected: ");
            if (outlen)
                for (j = 0; j < outlen; j++)
                    fprintf(stderr, "%02x", output[j]);
            else
                fprintf(stderr, "%08x", crc32(output, size));
            fprintf(stderr, "\n");
            fatal_error("output mismatch");
        }
        mtime = (double)(t1 - t0) / size;
        ttime  += mtime;
        ttime2 += mtime * mtime;
    }

    ttime  /= nruns;
    ttime2 /= nruns;
    stime = sqrt(ttime2 - ttime * ttime);
    printf("%-10s %-12s size: %7d  runs: %6d  time: %8.3f +- %.3f\n",
           impl->lib, impl->name, size, nruns, ttime, stime);
    fflush(stdout);
}

#define IMPL_USE(lib, name, symbol, output) \
    { #lib, name, run_ ## lib ## _ ## symbol, output },
#define IMPL(lib, ...) IMPL_USE_ ## lib(lib, __VA_ARGS__)
#define IMPL_ALL(...) \
    IMPL(lavu,       __VA_ARGS__) \
    IMPL(crypto,     __VA_ARGS__) \
    IMPL(gcrypt,     __VA_ARGS__) \
    IMPL(tomcrypt,   __VA_ARGS__)

struct hash_impl implementations[] = {
    IMPL_ALL("MD5",        md5,       "aa26ff5b895356bcffd9292ba9f89e66")
    IMPL_ALL("SHA-1",      sha1,      "1fd8bd1fa02f5b0fe916b0d71750726b096c5744")
    IMPL_ALL("SHA-256",    sha256,    "14028ac673b3087e51a1d407fbf0df4deeec8f217119e13b07bf2138f93db8c5")
    IMPL_ALL("SHA-512",    sha512,    "3afdd44a80d99af15c87bd724cb717243193767835ce866dd5d58c02d674bb57"
                                      "7c25b9e118c200a189fcd5a01ef106a4e200061f3e97dbf50ba065745fd46bef")
    IMPL_ALL("RIPEMD-160", ripemd160, "62a5321e4fc8784903bb43ab7752c75f8b25af00")
    IMPL_ALL("AES-128",    aes128,    "crc:ff6bc888")
    IMPL_ALL("CAMELLIA",   camellia,  "crc:7abb59a7")
    IMPL_ALL("CAST-128",   cast128,   "crc:456aa584")
    IMPL(lavu,     "TWOFISH", twofish, "crc:9edbd5c1")
    IMPL(gcrypt,   "TWOFISH", twofish, "crc:9edbd5c1")
    IMPL(tomcrypt, "TWOFISH", twofish, "crc:9edbd5c1")
};

int main(int argc, char **argv)
{
    uint8_t *input = av_malloc(MAX_INPUT_SIZE * 2);
    uint8_t *output = input + MAX_INPUT_SIZE;
    unsigned i, impl, size;
    int opt;

    while ((opt = getopt(argc, argv, "hl:a:r:")) != -1) {
        switch (opt) {
        case 'l':
            enabled_libs = optarg;
            break;
        case 'a':
            enabled_algos = optarg;
            break;
        case 'r':
            specified_runs = strtol(optarg, NULL, 0);
            break;
        case 'h':
        default:
            fprintf(stderr, "Usage: %s [-l libs] [-a algos] [-r runs]\n",
                    argv[0]);
            if ((USE_EXT_LIBS)) {
                char buf[1024];
                snprintf(buf, sizeof(buf), "%s%s%s",
                         ((USE_EXT_LIBS) & USE_crypto)   ? "+crypto"   : "",
                         ((USE_EXT_LIBS) & USE_gcrypt)   ? "+gcrypt"   : "",
                         ((USE_EXT_LIBS) & USE_tomcrypt) ? "+tomcrypt" : "");
                fprintf(stderr, "Built with the following external libraries:\n"
                        "make VERSUS=%s\n", buf + 1);
            } else {
                fprintf(stderr, "Built without external libraries; use\n"
                        "make VERSUS=crypto+gcrypt+tomcrypt tools/crypto_bench\n"
                        "to enable them.\n");
            }
            exit(opt != 'h');
        }
    }

    if (!input)
        fatal_error("out of memory");
    for (i = 0; i < MAX_INPUT_SIZE; i += 4)
        AV_WB32(input + i, i);

    size = MAX_INPUT_SIZE;
    for (impl = 0; impl < FF_ARRAY_ELEMS(implementations); impl++)
        run_implementation(input, output, &implementations[impl], size);

    av_free(input);

    return 0;
}
