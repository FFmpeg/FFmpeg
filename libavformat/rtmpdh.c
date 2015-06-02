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

#include "config.h"
#include "rtmpdh.h"
#include "libavutil/random_seed.h"

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
#define bn_new(bn)                  bn = gcry_mpi_new(1)
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

#ifdef TEST
static int test_random_shared_secret(void)
{
    FF_DH *peer1 = NULL, *peer2 = NULL;
    int ret;
    uint8_t pubkey1[128], pubkey2[128];
    uint8_t sharedkey1[128], sharedkey2[128];

    peer1 = ff_dh_init(1024);
    peer2 = ff_dh_init(1024);
    if (!peer1 || !peer2) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    if ((ret = ff_dh_generate_public_key(peer1)) < 0)
        goto fail;
    if ((ret = ff_dh_generate_public_key(peer2)) < 0)
        goto fail;
    if ((ret = ff_dh_write_public_key(peer1, pubkey1, sizeof(pubkey1))) < 0)
        goto fail;
    if ((ret = ff_dh_write_public_key(peer2, pubkey2, sizeof(pubkey2))) < 0)
        goto fail;
    if ((ret = ff_dh_compute_shared_secret_key(peer1, pubkey2, sizeof(pubkey2),
                                               sharedkey1, sizeof(sharedkey1))) < 0)
        goto fail;
    if ((ret = ff_dh_compute_shared_secret_key(peer2, pubkey1, sizeof(pubkey1),
                                               sharedkey2, sizeof(sharedkey2))) < 0)
        goto fail;
    if (memcmp(sharedkey1, sharedkey2, sizeof(sharedkey1))) {
        printf("Mismatched generated shared key\n");
        ret = AVERROR_INVALIDDATA;
    } else {
        printf("Generated shared key ok\n");
    }
fail:
    ff_dh_free(peer1);
    ff_dh_free(peer2);
    return ret;
}

static const char *private_key =
    "976C18FCADC255B456564F74F3EEDA59D28AF6B744D743F2357BFD2404797EF896EF1A"
    "7C1CBEAAA3AB60AF3192D189CFF3F991C9CBBFD78119FCA2181384B94011943B6D6F28"
    "9E1B708E2D1A0C7771169293F03DA27E561F15F16F0AC9BC858C77A80FA98FD088A232"
    "19D08BE6F165DE0B02034B18705829FAD0ACB26A5B75EF";
static const char *public_key =
    "F272ECF8362257C5D2C3CC2229CF9C0A03225BC109B1DBC76A68C394F256ACA3EF5F64"
    "FC270C26382BF315C19E97A76104A716FC998A651E8610A3AE6CF65D8FAE5D3F32EEA0"
    "0B32CB9609B494116A825D7142D17B88E3D20EDD98743DE29CF37A23A9F6A58B960591"
    "3157D5965FCB46DDA73A1F08DD897BAE88DFE6FC937CBA";
static const uint8_t public_key_bin[] = {
    0xf2, 0x72, 0xec, 0xf8, 0x36, 0x22, 0x57, 0xc5, 0xd2, 0xc3, 0xcc, 0x22,
    0x29, 0xcf, 0x9c, 0x0a, 0x03, 0x22, 0x5b, 0xc1, 0x09, 0xb1, 0xdb, 0xc7,
    0x6a, 0x68, 0xc3, 0x94, 0xf2, 0x56, 0xac, 0xa3, 0xef, 0x5f, 0x64, 0xfc,
    0x27, 0x0c, 0x26, 0x38, 0x2b, 0xf3, 0x15, 0xc1, 0x9e, 0x97, 0xa7, 0x61,
    0x04, 0xa7, 0x16, 0xfc, 0x99, 0x8a, 0x65, 0x1e, 0x86, 0x10, 0xa3, 0xae,
    0x6c, 0xf6, 0x5d, 0x8f, 0xae, 0x5d, 0x3f, 0x32, 0xee, 0xa0, 0x0b, 0x32,
    0xcb, 0x96, 0x09, 0xb4, 0x94, 0x11, 0x6a, 0x82, 0x5d, 0x71, 0x42, 0xd1,
    0x7b, 0x88, 0xe3, 0xd2, 0x0e, 0xdd, 0x98, 0x74, 0x3d, 0xe2, 0x9c, 0xf3,
    0x7a, 0x23, 0xa9, 0xf6, 0xa5, 0x8b, 0x96, 0x05, 0x91, 0x31, 0x57, 0xd5,
    0x96, 0x5f, 0xcb, 0x46, 0xdd, 0xa7, 0x3a, 0x1f, 0x08, 0xdd, 0x89, 0x7b,
    0xae, 0x88, 0xdf, 0xe6, 0xfc, 0x93, 0x7c, 0xba
};
static const uint8_t peer_public_key[] = {
    0x58, 0x66, 0x05, 0x49, 0x94, 0x23, 0x2b, 0x66, 0x52, 0x13, 0xff, 0x46,
    0xf2, 0xb3, 0x79, 0xa9, 0xee, 0xae, 0x1a, 0x13, 0xf0, 0x71, 0x52, 0xfb,
    0x93, 0x4e, 0xee, 0x97, 0x05, 0x73, 0x50, 0x7d, 0xaf, 0x02, 0x07, 0x72,
    0xac, 0xdc, 0xa3, 0x95, 0x78, 0xee, 0x9a, 0x19, 0x71, 0x7e, 0x99, 0x9f,
    0x2a, 0xd4, 0xb3, 0xe2, 0x0c, 0x1d, 0x1a, 0x78, 0x4c, 0xde, 0xf1, 0xad,
    0xb4, 0x60, 0xa8, 0x51, 0xac, 0x71, 0xec, 0x86, 0x70, 0xa2, 0x63, 0x36,
    0x92, 0x7c, 0xe3, 0x87, 0xee, 0xe4, 0xf1, 0x62, 0x24, 0x74, 0xb4, 0x04,
    0xfa, 0x5c, 0xdf, 0xba, 0xfa, 0xa3, 0xc2, 0xbb, 0x62, 0x27, 0xd0, 0xf4,
    0xe4, 0x43, 0xda, 0x8a, 0x88, 0x69, 0x60, 0xe2, 0xdb, 0x75, 0x2a, 0x98,
    0x9d, 0xb5, 0x50, 0xe3, 0x99, 0xda, 0xe0, 0xa6, 0x14, 0xc9, 0x80, 0x12,
    0xf9, 0x3c, 0xac, 0x06, 0x02, 0x7a, 0xde, 0x74
};
static const uint8_t shared_secret[] = {
    0xb2, 0xeb, 0xcb, 0x71, 0xf3, 0x61, 0xfb, 0x5b, 0x4e, 0x5c, 0x4c, 0xcf,
    0x5c, 0x08, 0x5f, 0x96, 0x26, 0x77, 0x1d, 0x31, 0xf1, 0xe1, 0xf7, 0x4b,
    0x92, 0xac, 0x82, 0x2a, 0x88, 0xc7, 0x83, 0xe1, 0xc7, 0xf3, 0xd3, 0x1a,
    0x7d, 0xc8, 0x31, 0xe3, 0x97, 0xe4, 0xec, 0x31, 0x0e, 0x8f, 0x73, 0x1a,
    0xe4, 0xf6, 0xd8, 0xc8, 0x94, 0xff, 0xa0, 0x03, 0x84, 0x03, 0x0f, 0xa5,
    0x30, 0x5d, 0x67, 0xe0, 0x7a, 0x3b, 0x5f, 0xed, 0x4c, 0xf5, 0xbc, 0x18,
    0xea, 0xd4, 0x77, 0xa9, 0x07, 0xb3, 0x54, 0x0b, 0x02, 0xd9, 0xc6, 0xb8,
    0x66, 0x5e, 0xec, 0xa4, 0xcd, 0x47, 0xed, 0xc9, 0x38, 0xc6, 0x91, 0x08,
    0xf3, 0x85, 0x9b, 0x69, 0x16, 0x78, 0x0d, 0xb7, 0x74, 0x51, 0xaa, 0x5b,
    0x4d, 0x74, 0xe4, 0x29, 0x2e, 0x9e, 0x8e, 0xf7, 0xe5, 0x42, 0x83, 0xb0,
    0x65, 0xb0, 0xce, 0xc6, 0xb2, 0x8f, 0x5b, 0xb0
};

static int test_ref_data(void)
{
    FF_DH *dh;
    int ret = AVERROR(ENOMEM);
    uint8_t pubkey_test[128];
    uint8_t sharedkey_test[128];

    dh = ff_dh_init(1024);
    if (!dh)
        goto fail;
    bn_hex2bn(dh->priv_key, private_key, ret);
    if (!ret)
        goto fail;
    bn_hex2bn(dh->pub_key, public_key, ret);
    if (!ret)
        goto fail;
    if ((ret = ff_dh_write_public_key(dh, pubkey_test, sizeof(pubkey_test))) < 0)
        goto fail;
    if (memcmp(pubkey_test, public_key_bin, sizeof(pubkey_test))) {
        printf("Mismatched generated public key\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
    } else {
        printf("Generated public key ok\n");
    }
    if ((ret = ff_dh_compute_shared_secret_key(dh, peer_public_key, sizeof(peer_public_key),
                                               sharedkey_test, sizeof(sharedkey_test))) < 0)
        goto fail;
    if (memcmp(shared_secret, sharedkey_test, sizeof(sharedkey_test))) {
        printf("Mismatched generated shared key\n");
        ret = AVERROR_INVALIDDATA;
    } else {
        printf("Generated shared key ok\n");
    }
fail:
    ff_dh_free(dh);
    return ret;
}

int main(void)
{
    if (test_random_shared_secret() < 0)
        return 1;
    if (test_ref_data() < 0)
        return 1;
    return 0;
}
#endif
