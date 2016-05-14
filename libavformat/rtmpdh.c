/*
 * RTMP Diffie-Hellmann utilities
 * Copyright (c) 2009 Andrej Stepanchuk
 * Copyright (c) 2009-2010 Howard Chu
 * Copyright (c) 2012 Samuel Pitoiset
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * RTMP Diffie-Hellmann utilities
 */

#include <stdint.h>
#include <string.h>

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"
#include "libavutil/random_seed.h"

#include "rtmpdh.h"

#define P1024                                          \
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1" \
    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245" \
    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED" \
    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381" \
    "FFFFFFFFFFFFFFFF"

#define Q1024                                          \
    "7FFFFFFFFFFFFFFFE487ED5110B4611A62633145C06E0E68" \
    "948127044533E63A0105DF531D89CD9128A5043CC71A026E" \
    "F7CA8CD9E69D218D98158536F92F8A1BA7F09AB6B6A8E122" \
    "F242DABB312F3F637A262174D31BF6B585FFAE5B7A035BF6" \
    "F71C35FDAD44CFD2D74F9208BE258FF324943328F67329C0" \
    "FFFFFFFFFFFFFFFF"

#if CONFIG_GMP || CONFIG_GCRYPT
#if CONFIG_GMP
#define bn_new(bn)                      \
    do {                                \
        bn = av_malloc(sizeof(*bn));    \
        if (bn)                         \
            mpz_init2(bn, 1);           \
    } while (0)
#define bn_free(bn)     \
    do {                \
        mpz_clear(bn);  \
        av_free(bn);    \
    } while (0)
#define bn_set_word(bn, w)          mpz_set_ui(bn, w)
#define bn_cmp(a, b)                mpz_cmp(a, b)
#define bn_copy(to, from)           mpz_set(to, from)
#define bn_sub_word(bn, w)          mpz_sub_ui(bn, bn, w)
#define bn_cmp_1(bn)                mpz_cmp_ui(bn, 1)
#define bn_num_bytes(bn)            (mpz_sizeinbase(bn, 2) + 7) / 8
#define bn_bn2bin(bn, buf, len)                     \
    do {                                            \
        memset(buf, 0, len);                        \
        if (bn_num_bytes(bn) <= len)                \
            mpz_export(buf, NULL, 1, 1, 0, 0, bn);  \
    } while (0)
#define bn_bin2bn(bn, buf, len)                     \
    do {                                            \
        bn_new(bn);                                 \
        if (bn)                                     \
            mpz_import(bn, len, 1, 1, 0, 0, buf);   \
    } while (0)
#define bn_hex2bn(bn, buf, ret)                     \
    do {                                            \
        bn_new(bn);                                 \
        if (bn)                                     \
            ret = (mpz_set_str(bn, buf, 16) == 0);  \
        else                                        \
            ret = 1;                                \
    } while (0)
#define bn_modexp(bn, y, q, p)      mpz_powm(bn, y, q, p)
#define bn_random(bn, num_bits)                       \
    do {                                              \
        int bits = num_bits;                          \
        mpz_set_ui(bn, 0);                            \
        for (bits = num_bits; bits > 0; bits -= 32) { \
            mpz_mul_2exp(bn, bn, 32);                 \
            mpz_add_ui(bn, bn, av_get_random_seed()); \
        }                                             \
        mpz_fdiv_r_2exp(bn, bn, num_bits);            \
    } while (0)
#elif CONFIG_GCRYPT
#define bn_new(bn)                                              \
    do {                                                        \
        if (!gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) { \
            if (!gcry_check_version("1.5.4"))                   \
                return AVERROR(EINVAL);                         \
            gcry_control(GCRYCTL_DISABLE_SECMEM, 0);            \
            gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);   \
        }                                                       \
        bn = gcry_mpi_new(1);                                   \
    } while (0)
#define bn_free(bn)                 gcry_mpi_release(bn)
#define bn_set_word(bn, w)          gcry_mpi_set_ui(bn, w)
#define bn_cmp(a, b)                gcry_mpi_cmp(a, b)
#define bn_copy(to, from)           gcry_mpi_set(to, from)
#define bn_sub_word(bn, w)          gcry_mpi_sub_ui(bn, bn, w)
#define bn_cmp_1(bn)                gcry_mpi_cmp_ui(bn, 1)
#define bn_num_bytes(bn)            (gcry_mpi_get_nbits(bn) + 7) / 8
#define bn_bn2bin(bn, buf, len)     gcry_mpi_print(GCRYMPI_FMT_USG, buf, len, NULL, bn)
#define bn_bin2bn(bn, buf, len)     gcry_mpi_scan(&bn, GCRYMPI_FMT_USG, buf, len, NULL)
#define bn_hex2bn(bn, buf, ret)     ret = (gcry_mpi_scan(&bn, GCRYMPI_FMT_HEX, buf, 0, 0) == 0)
#define bn_modexp(bn, y, q, p)      gcry_mpi_powm(bn, y, q, p)
#define bn_random(bn, num_bits)     gcry_mpi_randomize(bn, num_bits, GCRY_WEAK_RANDOM)
#endif

#define MAX_BYTES 18000

#define dh_new()                    av_malloc(sizeof(FF_DH))

static FFBigNum dh_generate_key(FF_DH *dh)
{
    int num_bytes;

    num_bytes = bn_num_bytes(dh->p) - 1;
    if (num_bytes <= 0 || num_bytes > MAX_BYTES)
        return NULL;

    bn_new(dh->priv_key);
    if (!dh->priv_key)
        return NULL;
    bn_random(dh->priv_key, 8 * num_bytes);

    bn_new(dh->pub_key);
    if (!dh->pub_key) {
        bn_free(dh->priv_key);
        return NULL;
    }

    bn_modexp(dh->pub_key, dh->g, dh->priv_key, dh->p);

    return dh->pub_key;
}

static int dh_compute_key(FF_DH *dh, FFBigNum pub_key_bn,
                          uint32_t secret_key_len, uint8_t *secret_key)
{
    FFBigNum k;

    bn_new(k);
    if (!k)
        return -1;

    bn_modexp(k, pub_key_bn, dh->priv_key, dh->p);
    bn_bn2bin(k, secret_key, secret_key_len);
    bn_free(k);

    /* return the length of the shared secret key like DH_compute_key */
    return secret_key_len;
}

void ff_dh_free(FF_DH *dh)
{
    if (!dh)
        return;
    bn_free(dh->p);
    bn_free(dh->g);
    bn_free(dh->pub_key);
    bn_free(dh->priv_key);
    av_free(dh);
}
#elif CONFIG_OPENSSL
#define bn_new(bn)                  bn = BN_new()
#define bn_free(bn)                 BN_free(bn)
#define bn_set_word(bn, w)          BN_set_word(bn, w)
#define bn_cmp(a, b)                BN_cmp(a, b)
#define bn_copy(to, from)           BN_copy(to, from)
#define bn_sub_word(bn, w)          BN_sub_word(bn, w)
#define bn_cmp_1(bn)                BN_cmp(bn, BN_value_one())
#define bn_num_bytes(bn)            BN_num_bytes(bn)
#define bn_bn2bin(bn, buf, len)     BN_bn2bin(bn, buf)
#define bn_bin2bn(bn, buf, len)     bn = BN_bin2bn(buf, len, 0)
#define bn_hex2bn(bn, buf, ret)     ret = BN_hex2bn(&bn, buf)
#define bn_modexp(bn, y, q, p)               \
    do {                                     \
        BN_CTX *ctx = BN_CTX_new();          \
        if (!ctx)                            \
            return AVERROR(ENOMEM);          \
        if (!BN_mod_exp(bn, y, q, p, ctx)) { \
            BN_CTX_free(ctx);                \
            return AVERROR(EINVAL);          \
        }                                    \
        BN_CTX_free(ctx);                    \
    } while (0)

#define dh_new()                                DH_new()
#define dh_generate_key(dh)                     DH_generate_key(dh)

static int dh_compute_key(FF_DH *dh, FFBigNum pub_key_bn,
                          uint32_t secret_key_len, uint8_t *secret_key)
{
    if (secret_key_len < DH_size(dh))
        return AVERROR(EINVAL);
    return DH_compute_key(secret_key, pub_key_bn, dh);
}

void ff_dh_free(FF_DH *dh)
{
    if (!dh)
        return;
    DH_free(dh);
}
#endif

static int dh_is_valid_public_key(FFBigNum y, FFBigNum p, FFBigNum q)
{
    FFBigNum bn = NULL;
    int ret = AVERROR(EINVAL);

    bn_new(bn);
    if (!bn)
        return AVERROR(ENOMEM);

    /* y must lie in [2, p - 1] */
    bn_set_word(bn, 1);
    if (!bn_cmp(y, bn))
        goto fail;

    /* bn = p - 2 */
    bn_copy(bn, p);
    bn_sub_word(bn, 1);
    if (!bn_cmp(y, bn))
        goto fail;

    /* Verify with Sophie-Germain prime
     *
     * This is a nice test to make sure the public key position is calculated
     * correctly. This test will fail in about 50% of the cases if applied to
     * random data.
     */
    /* y must fulfill y^q mod p = 1 */
    bn_modexp(bn, y, q, p);

    if (bn_cmp_1(bn))
        goto fail;

    ret = 0;
fail:
    bn_free(bn);

    return ret;
}

av_cold FF_DH *ff_dh_init(int key_len)
{
    FF_DH *dh;
    int ret;

    if (!(dh = dh_new()))
        return NULL;

    bn_new(dh->g);
    if (!dh->g)
        goto fail;

    bn_hex2bn(dh->p, P1024, ret);
    if (!ret)
        goto fail;

    bn_set_word(dh->g, 2);
    dh->length = key_len;

    return dh;

fail:
    ff_dh_free(dh);

    return NULL;
}

int ff_dh_generate_public_key(FF_DH *dh)
{
    int ret = 0;

    while (!ret) {
        FFBigNum q1 = NULL;

        if (!dh_generate_key(dh))
            return AVERROR(EINVAL);

        bn_hex2bn(q1, Q1024, ret);
        if (!ret)
            return AVERROR(ENOMEM);

        ret = dh_is_valid_public_key(dh->pub_key, dh->p, q1);
        bn_free(q1);

        if (!ret) {
            /* the public key is valid */
            break;
        }
    }

    return ret;
}

int ff_dh_write_public_key(FF_DH *dh, uint8_t *pub_key, int pub_key_len)
{
    int len;

    /* compute the length of the public key */
    len = bn_num_bytes(dh->pub_key);
    if (len <= 0 || len > pub_key_len)
        return AVERROR(EINVAL);

    /* convert the public key value into big-endian form */
    memset(pub_key, 0, pub_key_len);
    bn_bn2bin(dh->pub_key, pub_key + pub_key_len - len, len);

    return 0;
}

int ff_dh_compute_shared_secret_key(FF_DH *dh, const uint8_t *pub_key,
                                    int pub_key_len, uint8_t *secret_key,
                                    int secret_key_len)
{
    FFBigNum q1 = NULL, pub_key_bn = NULL;
    int ret;

    /* convert the big-endian form of the public key into a bignum */
    bn_bin2bn(pub_key_bn, pub_key, pub_key_len);
    if (!pub_key_bn)
        return AVERROR(ENOMEM);

    /* convert the string containing a hexadecimal number into a bignum */
    bn_hex2bn(q1, Q1024, ret);
    if (!ret) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* when the public key is valid we have to compute the shared secret key */
    if ((ret = dh_is_valid_public_key(pub_key_bn, dh->p, q1)) < 0) {
        goto fail;
    } else if ((ret = dh_compute_key(dh, pub_key_bn, secret_key_len,
                                     secret_key)) < 0) {
        ret = AVERROR(EINVAL);
        goto fail;
    }

fail:
    bn_free(pub_key_bn);
    bn_free(q1);

    return ret;
}
