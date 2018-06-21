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
#define USE_mbedcrypto       0x08    /* mbed TLS */

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
#include "libavutil/blowfish.h"
#include "libavutil/camellia.h"
#include "libavutil/cast5.h"
#include "libavutil/des.h"
#include "libavutil/twofish.h"
#include "libavutil/rc4.h"
#include "libavutil/xtea.h"

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
DEFINE_LAVU_MD(ripemd128, AVRIPEMD, ripemd, 128);
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

static void run_lavu_blowfish(uint8_t *output,
                              const uint8_t *input, unsigned size)
{
    static struct AVBlowfish *blowfish;
    if (!blowfish && !(blowfish = av_blowfish_alloc()))
        fatal_error("out of memory");
    av_blowfish_init(blowfish, hardcoded_key, 16);
    av_blowfish_crypt(blowfish, output, input, size >> 3, NULL, 0);
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

static void run_lavu_des(uint8_t *output,
                              const uint8_t *input, unsigned size)
{
    static struct AVDES *des;
    if (!des && !(des = av_des_alloc()))
        fatal_error("out of memory");
    av_des_init(des, hardcoded_key, 64, 0);
    av_des_crypt(des, output, input, size >> 3, NULL, 0);
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

static void run_lavu_rc4(uint8_t *output,
                              const uint8_t *input, unsigned size)
{
    static struct AVRC4 *rc4;
    if (!rc4 && !(rc4 = av_rc4_alloc()))
        fatal_error("out of memory");
    av_rc4_init(rc4, hardcoded_key, 128, 0);
    av_rc4_crypt(rc4, output, input, size, NULL, 0);
}

static void run_lavu_xtea(uint8_t *output,
                              const uint8_t *input, unsigned size)
{
    static struct AVXTEA *xtea;
    if (!xtea && !(xtea = av_xtea_alloc()))
        fatal_error("out of memory");
    av_xtea_init(xtea, hardcoded_key);
    av_xtea_crypt(xtea, output, input, size >> 3, NULL, 0);
}

/***************************************************************************
 * crypto: OpenSSL's libcrypto
 ***************************************************************************/

#if (USE_EXT_LIBS) & USE_crypto

#define OPENSSL_DISABLE_OLD_DES_SUPPORT
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <openssl/aes.h>
#include <openssl/blowfish.h>
#include <openssl/camellia.h>
#include <openssl/cast.h>
#include <openssl/des.h>
#include <openssl/rc4.h>

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

static void run_crypto_blowfish(uint8_t *output,
                                const uint8_t *input, unsigned size)
{
    BF_KEY blowfish;
    unsigned i;

    BF_set_key(&blowfish, 16, hardcoded_key);
    for (i = 0; i < size; i += 8)
        BF_ecb_encrypt(input + i, output + i, &blowfish, 1);
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

static void run_crypto_des(uint8_t *output,
                           const uint8_t *input, unsigned size)
{
    DES_key_schedule des;
    unsigned i;

    DES_set_key(hardcoded_key, &des);
    for (i = 0; i < size; i += 8)
        DES_ecb_encrypt(input + i, output + i, &des, 1);
}

static void run_crypto_rc4(uint8_t *output,
                                const uint8_t *input, unsigned size)
{
    RC4_KEY rc4;

    RC4_set_key(&rc4, 16, hardcoded_key);
    RC4(&rc4, size, input, output);
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

#define DEFINE_GCRYPT_CYPHER_WRAPPER(suffix, cypher, mode, sz)                      \
static void run_gcrypt_ ## suffix(uint8_t *output,                                  \
                              const uint8_t *input, unsigned size)                  \
{                                                                                   \
    static gcry_cipher_hd_t suffix;                                                 \
    if (!suffix)                                                                    \
        gcry_cipher_open(&suffix, GCRY_CIPHER_ ## cypher, GCRY_CIPHER_MODE_ ## mode, 0); \
    gcry_cipher_setkey(suffix, hardcoded_key, sz);                                  \
    gcry_cipher_encrypt(suffix, output, size, input, size);                         \
}

DEFINE_GCRYPT_CYPHER_WRAPPER(aes128,   AES128,      ECB,    16)
DEFINE_GCRYPT_CYPHER_WRAPPER(blowfish, BLOWFISH,    ECB,    16)
DEFINE_GCRYPT_CYPHER_WRAPPER(camellia, CAMELLIA128, ECB,    16)
DEFINE_GCRYPT_CYPHER_WRAPPER(cast128,  CAST5,       ECB,    16)
DEFINE_GCRYPT_CYPHER_WRAPPER(des,      DES,         ECB,    8)
DEFINE_GCRYPT_CYPHER_WRAPPER(twofish,  TWOFISH128,  ECB,    16)
DEFINE_GCRYPT_CYPHER_WRAPPER(rc4,      ARCFOUR,     STREAM, 16)

#define IMPL_USE_gcrypt(...) IMPL_USE(__VA_ARGS__)
#else
#define IMPL_USE_gcrypt(...) /* ignore */
#endif

/***************************************************************************
 * mbedcrypto: mbed TLS
 ***************************************************************************/

#if (USE_EXT_LIBS) & USE_mbedcrypto

#include <mbedtls/aes.h>
#include <mbedtls/arc4.h>
#include <mbedtls/blowfish.h>
#include <mbedtls/camellia.h>
#include <mbedtls/des.h>
#include <mbedtls/md5.h>
#include <mbedtls/ripemd160.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <mbedtls/xtea.h>

#define DEFINE_MBEDCRYPTO_WRAPPER(suffix)                                  \
static void run_mbedcrypto_ ## suffix(uint8_t *output,                     \
                                      const uint8_t *input, unsigned size) \
{                                                                          \
    mbedtls_ ## suffix ## _ret(input, size, output);                       \
}

#define DEFINE_MBEDCRYPTO_WRAPPER_SHA2(suffix)                             \
static void run_mbedcrypto_ ## suffix(uint8_t *output,                     \
                                      const uint8_t *input, unsigned size) \
{                                                                          \
    mbedtls_ ## suffix ## _ret(input, size, output, 0);                    \
}

DEFINE_MBEDCRYPTO_WRAPPER(md5)
DEFINE_MBEDCRYPTO_WRAPPER(ripemd160)
DEFINE_MBEDCRYPTO_WRAPPER(sha1)
DEFINE_MBEDCRYPTO_WRAPPER_SHA2(sha256)
DEFINE_MBEDCRYPTO_WRAPPER_SHA2(sha512)


#define DEFINE_MBEDCRYPTO_CYPHER_WRAPPER(suffix, cypher, algo)                  \
static void run_mbedcrypto_ ## suffix(uint8_t *output,                          \
                                      const uint8_t *input, unsigned size)      \
{                                                                               \
    mbedtls_ ## cypher ## _context cypher;                                      \
                                                                                \
    mbedtls_ ## cypher ## _init(&cypher);                                       \
    mbedtls_ ## cypher ## _setkey_enc(&cypher, hardcoded_key, 128);             \
    for (int i = 0; i < size; i += 16)                                          \
        mbedtls_ ## cypher ## _crypt_ecb(&cypher, MBEDTLS_ ## algo ## _ENCRYPT, \
                                         input + i, output + i);                \
    mbedtls_ ## cypher ## _free(&cypher);                                       \
}

DEFINE_MBEDCRYPTO_CYPHER_WRAPPER(aes128, aes, AES)
DEFINE_MBEDCRYPTO_CYPHER_WRAPPER(camellia, camellia, CAMELLIA)

static void run_mbedcrypto_blowfish(uint8_t *output,
                                    const uint8_t *input, unsigned size)
{
    mbedtls_blowfish_context blowfish;

    mbedtls_blowfish_init(&blowfish);
    mbedtls_blowfish_setkey(&blowfish, hardcoded_key, 128);
    for (int i = 0; i < size; i += 8)
        mbedtls_blowfish_crypt_ecb(&blowfish, MBEDTLS_BLOWFISH_ENCRYPT,
                                   input + i, output + i);
    mbedtls_blowfish_free(&blowfish);
}

static void run_mbedcrypto_des(uint8_t *output,
                               const uint8_t *input, unsigned size)
{
    mbedtls_des_context des;

    mbedtls_des_init(&des);
    mbedtls_des_setkey_enc(&des, hardcoded_key);
    for (int i = 0; i < size; i += 8)
        mbedtls_des_crypt_ecb(&des, input + i, output + i);
    mbedtls_des_free(&des);
}

static void run_mbedcrypto_rc4(uint8_t *output,
                               const uint8_t *input, unsigned size)
{
    mbedtls_arc4_context rc4;

    mbedtls_arc4_init(&rc4);
    mbedtls_arc4_setup(&rc4, hardcoded_key, 16);
    mbedtls_arc4_crypt(&rc4, size, input, output);
    mbedtls_arc4_free(&rc4);
}

static void run_mbedcrypto_xtea(uint8_t *output,
                                const uint8_t *input, unsigned size)
{
    mbedtls_xtea_context xtea;

    mbedtls_xtea_init(&xtea);
    mbedtls_xtea_setup(&xtea, hardcoded_key);
    for (int i = 0; i < size; i += 8)
        mbedtls_xtea_crypt_ecb(&xtea, MBEDTLS_XTEA_ENCRYPT,
                               input + i, output + i);
    mbedtls_xtea_free(&xtea);
}

#define IMPL_USE_mbedcrypto(...) IMPL_USE(__VA_ARGS__)
#else
#define IMPL_USE_mbedcrypto(...) /* ignore */
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
DEFINE_TOMCRYPT_WRAPPER(ripemd128, rmd128, RIPEMD128)
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

static void run_tomcrypt_blowfish(uint8_t *output,
                                  const uint8_t *input, unsigned size)
{
    symmetric_key blowfish;
    unsigned i;

    blowfish_setup(hardcoded_key, 16, 0, &blowfish);
    for (i = 0; i < size; i += 8)
        blowfish_ecb_encrypt(input + i, output + i, &blowfish);
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

static void run_tomcrypt_des(uint8_t *output,
                             const uint8_t *input, unsigned size)
{
    symmetric_key des;
    unsigned i;

    des_setup(hardcoded_key, 8, 0, &des);
    for (i = 0; i < size; i += 8)
        des_ecb_encrypt(input + i, output + i, &des);
}

static void run_tomcrypt_rc4(uint8_t *output,
                             const uint8_t *input, unsigned size)
{
    rc4_state rc4;

    rc4_stream_setup(&rc4, hardcoded_key, 16);
    rc4_stream_crypt(&rc4, input, size, output);
    rc4_stream_done(&rc4);
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

static void run_tomcrypt_xtea(uint8_t *output,
                              const uint8_t *input, unsigned size)
{
    symmetric_key xtea;
    unsigned i;

    xtea_setup(hardcoded_key, 16, 0, &xtea);
    for (i = 0; i < size; i += 8)
        xtea_ecb_encrypt(input + i, output + i, &xtea);
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
    IMPL(mbedcrypto, __VA_ARGS__) \
    IMPL(tomcrypt,   __VA_ARGS__)

struct hash_impl implementations[] = {
    IMPL_ALL("MD5",        md5,       "aa26ff5b895356bcffd9292ba9f89e66")
    IMPL_ALL("SHA-1",      sha1,      "1fd8bd1fa02f5b0fe916b0d71750726b096c5744")
    IMPL_ALL("SHA-256",    sha256,    "14028ac673b3087e51a1d407fbf0df4deeec8f217119e13b07bf2138f93db8c5")
    IMPL_ALL("SHA-512",    sha512,    "3afdd44a80d99af15c87bd724cb717243193767835ce866dd5d58c02d674bb57"
                                      "7c25b9e118c200a189fcd5a01ef106a4e200061f3e97dbf50ba065745fd46bef")
    IMPL(lavu,     "RIPEMD-128", ripemd128, "9ab8bfba2ddccc5d99c9d4cdfb844a5f")
    IMPL(tomcrypt, "RIPEMD-128", ripemd128, "9ab8bfba2ddccc5d99c9d4cdfb844a5f")
    IMPL_ALL("RIPEMD-160", ripemd160, "62a5321e4fc8784903bb43ab7752c75f8b25af00")
    IMPL_ALL("AES-128",    aes128,    "crc:ff6bc888")
    IMPL_ALL("CAMELLIA",   camellia,  "crc:7abb59a7")
    IMPL(lavu,     "CAST-128", cast128, "crc:456aa584")
    IMPL(crypto,   "CAST-128", cast128, "crc:456aa584")
    IMPL(gcrypt,   "CAST-128", cast128, "crc:456aa584")
    IMPL(tomcrypt, "CAST-128", cast128, "crc:456aa584")
    IMPL_ALL("BLOWFISH",   blowfish,  "crc:33e8aa74")
    IMPL_ALL("DES",        des,       "crc:31291e0b")
    IMPL(lavu,     "TWOFISH", twofish, "crc:9edbd5c1")
    IMPL(gcrypt,   "TWOFISH", twofish, "crc:9edbd5c1")
    IMPL(tomcrypt, "TWOFISH", twofish, "crc:9edbd5c1")
    IMPL_ALL("RC4",           rc4,     "crc:538d37b2")
    IMPL(lavu,     "XTEA",    xtea,    "crc:931fc270")
    IMPL(mbedcrypto, "XTEA",  xtea,    "crc:931fc270")
    IMPL(tomcrypt, "XTEA",    xtea,    "crc:931fc270")
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
                snprintf(buf, sizeof(buf), "%s%s%s%s",
                         ((USE_EXT_LIBS) & USE_crypto)   ? "+crypto"   : "",
                         ((USE_EXT_LIBS) & USE_gcrypt)   ? "+gcrypt"   : "",
                         ((USE_EXT_LIBS) & USE_mbedcrypto) ? "+mbedcrypto" : "",
                         ((USE_EXT_LIBS) & USE_tomcrypt) ? "+tomcrypt" : "");
                fprintf(stderr, "Built with the following external libraries:\n"
                        "make VERSUS=%s\n", buf + 1);
            } else {
                fprintf(stderr, "Built without external libraries; use\n"
                        "make VERSUS=crypto+gcrypt+mbedcrypto+tomcrypt tools/crypto_bench\n"
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
