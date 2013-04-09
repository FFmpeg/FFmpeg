/*
 * SRTP encryption/decryption
 * Copyright (c) 2012 Martin Storsjo
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

#include "libavutil/base64.h"
#include "libavutil/aes.h"
#include "libavutil/hmac.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "rtp.h"
#include "rtpdec.h"
#include "srtp.h"

void ff_srtp_free(struct SRTPContext *s)
{
    if (!s)
        return;
    av_freep(&s->aes);
    if (s->hmac)
        av_hmac_free(s->hmac);
    s->hmac = NULL;
}

static void encrypt_counter(struct AVAES *aes, uint8_t *iv, uint8_t *outbuf,
                            int outlen)
{
    int i, j, outpos;
    for (i = 0, outpos = 0; outpos < outlen; i++) {
        uint8_t keystream[16];
        AV_WB16(&iv[14], i);
        av_aes_crypt(aes, keystream, iv, 1, NULL, 0);
        for (j = 0; j < 16 && outpos < outlen; j++, outpos++)
            outbuf[outpos] ^= keystream[j];
    }
}

static void derive_key(struct AVAES *aes, const uint8_t *salt, int label,
                       uint8_t *out, int outlen)
{
    uint8_t input[16] = { 0 };
    memcpy(input, salt, 14);
    // Key derivation rate assumed to be zero
    input[14 - 7] ^= label;
    memset(out, 0, outlen);
    encrypt_counter(aes, input, out, outlen);
}

int ff_srtp_set_crypto(struct SRTPContext *s, const char *suite,
                       const char *params)
{
    uint8_t buf[30];

    ff_srtp_free(s);

    // RFC 4568
    if (!strcmp(suite, "AES_CM_128_HMAC_SHA1_80") ||
        !strcmp(suite, "SRTP_AES128_CM_HMAC_SHA1_80")) {
        s->rtp_hmac_size = s->rtcp_hmac_size = 10;
    } else if (!strcmp(suite, "AES_CM_128_HMAC_SHA1_32")) {
        s->rtp_hmac_size = s->rtcp_hmac_size = 4;
    } else if (!strcmp(suite, "SRTP_AES128_CM_HMAC_SHA1_32")) {
        // RFC 5764 section 4.1.2
        s->rtp_hmac_size  = 4;
        s->rtcp_hmac_size = 10;
    } else {
        av_log(NULL, AV_LOG_WARNING, "SRTP Crypto suite %s not supported\n",
                                     suite);
        return AVERROR(EINVAL);
    }
    if (av_base64_decode(buf, params, sizeof(buf)) != sizeof(buf)) {
        av_log(NULL, AV_LOG_WARNING, "Incorrect amount of SRTP params\n");
        return AVERROR(EINVAL);
    }
    // MKI and lifetime not handled yet
    s->aes  = av_aes_alloc();
    s->hmac = av_hmac_alloc(AV_HMAC_SHA1);
    if (!s->aes || !s->hmac)
        return AVERROR(ENOMEM);
    memcpy(s->master_key, buf, 16);
    memcpy(s->master_salt, buf + 16, 14);

    // RFC 3711
    av_aes_init(s->aes, s->master_key, 128, 0);

    derive_key(s->aes, s->master_salt, 0x00, s->rtp_key, sizeof(s->rtp_key));
    derive_key(s->aes, s->master_salt, 0x02, s->rtp_salt, sizeof(s->rtp_salt));
    derive_key(s->aes, s->master_salt, 0x01, s->rtp_auth, sizeof(s->rtp_auth));

    derive_key(s->aes, s->master_salt, 0x03, s->rtcp_key, sizeof(s->rtcp_key));
    derive_key(s->aes, s->master_salt, 0x05, s->rtcp_salt, sizeof(s->rtcp_salt));
    derive_key(s->aes, s->master_salt, 0x04, s->rtcp_auth, sizeof(s->rtcp_auth));
    return 0;
}

static void create_iv(uint8_t *iv, const uint8_t *salt, uint64_t index,
                      uint32_t ssrc)
{
    uint8_t indexbuf[8];
    int i;
    memset(iv, 0, 16);
    AV_WB32(&iv[4], ssrc);
    AV_WB64(indexbuf, index);
    for (i = 0; i < 8; i++) // index << 16
        iv[6 + i] ^= indexbuf[i];
    for (i = 0; i < 14; i++)
        iv[i] ^= salt[i];
}

int ff_srtp_decrypt(struct SRTPContext *s, uint8_t *buf, int *lenptr)
{
    uint8_t iv[16] = { 0 }, hmac[20];
    int len = *lenptr;
    int av_uninit(seq_largest);
    uint32_t ssrc, av_uninit(roc);
    uint64_t index;
    int rtcp, hmac_size;

    // TODO: Missing replay protection

    if (len < 2)
        return AVERROR_INVALIDDATA;

    rtcp = RTP_PT_IS_RTCP(buf[1]);
    hmac_size = rtcp ? s->rtcp_hmac_size : s->rtp_hmac_size;

    if (len < hmac_size)
        return AVERROR_INVALIDDATA;

    // Authentication HMAC
    av_hmac_init(s->hmac, rtcp ? s->rtcp_auth : s->rtp_auth, sizeof(s->rtp_auth));
    // If MKI is used, this should exclude the MKI as well
    av_hmac_update(s->hmac, buf, len - hmac_size);

    if (!rtcp) {
        int seq = AV_RB16(buf + 2);
        uint32_t v;
        uint8_t rocbuf[4];

        // RFC 3711 section 3.3.1, appendix A
        seq_largest = s->seq_initialized ? s->seq_largest : seq;
        v = roc = s->roc;
        if (seq_largest < 32768) {
            if (seq - seq_largest > 32768)
                v = roc - 1;
        } else {
            if (seq_largest - 32768 > seq)
                v = roc + 1;
        }
        if (v == roc) {
            seq_largest = FFMAX(seq_largest, seq);
        } else if (v == roc + 1) {
            seq_largest = seq;
            roc = v;
        }
        index = seq + (((uint64_t)v) << 16);

        AV_WB32(rocbuf, roc);
        av_hmac_update(s->hmac, rocbuf, 4);
    }

    av_hmac_final(s->hmac, hmac, sizeof(hmac));
    if (memcmp(hmac, buf + len - hmac_size, hmac_size)) {
        av_log(NULL, AV_LOG_WARNING, "HMAC mismatch\n");
        return AVERROR_INVALIDDATA;
    }

    len -= hmac_size;
    *lenptr = len;

    if (len < 12)
        return AVERROR_INVALIDDATA;

    if (rtcp) {
        uint32_t srtcp_index = AV_RB32(buf + len - 4);
        len -= 4;
        *lenptr = len;

        ssrc = AV_RB32(buf + 4);
        index = srtcp_index & 0x7fffffff;

        buf += 8;
        len -= 8;
        if (!(srtcp_index & 0x80000000))
            return 0;
    } else {
        int ext, csrc;
        s->seq_initialized = 1;
        s->seq_largest     = seq_largest;
        s->roc             = roc;

        csrc = buf[0] & 0x0f;
        ext  = buf[0] & 0x10;
        ssrc = AV_RB32(buf + 8);

        buf += 12;
        len -= 12;

        buf += 4 * csrc;
        len -= 4 * csrc;
        if (len < 0)
            return AVERROR_INVALIDDATA;

        if (ext) {
            if (len < 4)
                return AVERROR_INVALIDDATA;
            ext = (AV_RB16(buf + 2) + 1) * 4;
            if (len < ext)
                return AVERROR_INVALIDDATA;
            len -= ext;
            buf += ext;
        }
    }

    create_iv(iv, rtcp ? s->rtcp_salt : s->rtp_salt, index, ssrc);
    av_aes_init(s->aes, rtcp ? s->rtcp_key : s->rtp_key, 128, 0);
    encrypt_counter(s->aes, iv, buf, len);

    return 0;
}

int ff_srtp_encrypt(struct SRTPContext *s, const uint8_t *in, int len,
                    uint8_t *out, int outlen)
{
    uint8_t iv[16] = { 0 }, hmac[20];
    uint64_t index;
    uint32_t ssrc;
    int rtcp, hmac_size, padding;
    uint8_t *buf;

    if (len < 8)
        return AVERROR_INVALIDDATA;

    rtcp = RTP_PT_IS_RTCP(in[1]);
    hmac_size = rtcp ? s->rtcp_hmac_size : s->rtp_hmac_size;
    padding = hmac_size;
    if (rtcp)
        padding += 4; // For the RTCP index

    if (len + padding > outlen)
        return 0;

    memcpy(out, in, len);
    buf = out;

    if (rtcp) {
        ssrc = AV_RB32(buf + 4);
        index = s->rtcp_index++;

        buf += 8;
        len -= 8;
    } else {
        int ext, csrc;
        int seq = AV_RB16(buf + 2);

        if (len < 12)
            return AVERROR_INVALIDDATA;

        ssrc = AV_RB32(buf + 8);

        if (seq < s->seq_largest)
            s->roc++;
        s->seq_largest = seq;
        index = seq + (((uint64_t)s->roc) << 16);

        csrc = buf[0] & 0x0f;
        ext = buf[0] & 0x10;

        buf += 12;
        len -= 12;

        buf += 4 * csrc;
        len -= 4 * csrc;
        if (len < 0)
            return AVERROR_INVALIDDATA;

        if (ext) {
            if (len < 4)
                return AVERROR_INVALIDDATA;
            ext = (AV_RB16(buf + 2) + 1) * 4;
            if (len < ext)
                return AVERROR_INVALIDDATA;
            len -= ext;
            buf += ext;
        }
    }

    create_iv(iv, rtcp ? s->rtcp_salt : s->rtp_salt, index, ssrc);
    av_aes_init(s->aes, rtcp ? s->rtcp_key : s->rtp_key, 128, 0);
    encrypt_counter(s->aes, iv, buf, len);

    if (rtcp) {
        AV_WB32(buf + len, 0x80000000 | index);
        len += 4;
    }

    av_hmac_init(s->hmac, rtcp ? s->rtcp_auth : s->rtp_auth, sizeof(s->rtp_auth));
    av_hmac_update(s->hmac, out, buf + len - out);
    if (!rtcp) {
        uint8_t rocbuf[4];
        AV_WB32(rocbuf, s->roc);
        av_hmac_update(s->hmac, rocbuf, 4);
    }
    av_hmac_final(s->hmac, hmac, sizeof(hmac));

    memcpy(buf + len, hmac, hmac_size);
    len += hmac_size;
    return buf + len - out;
}

#ifdef TEST
#include <stdio.h>

static const char *aes128_80_key = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmn";

static const uint8_t rtp_aes128_80[] = {
    // RTP header
    0x80, 0xe0, 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78,
    // encrypted payload
    0x62, 0x69, 0x76, 0xca, 0xc5,
    // HMAC
    0xa1, 0xac, 0x1b, 0xb4, 0xa0, 0x1c, 0xd5, 0x49, 0x28, 0x99,
};

static const uint8_t rtcp_aes128_80[] = {
    // RTCP header
    0x81, 0xc9, 0x00, 0x07, 0x12, 0x34, 0x56, 0x78,
    // encrypted payload
    0x8a, 0xac, 0xdc, 0xa5, 0x4c, 0xf6, 0x78, 0xa6, 0x62, 0x8f, 0x24, 0xda,
    0x6c, 0x09, 0x3f, 0xa9, 0x28, 0x7a, 0xb5, 0x7f, 0x1f, 0x0f, 0xc9, 0x35,
    // RTCP index
    0x80, 0x00, 0x00, 0x03,
    // HMAC
    0xe9, 0x3b, 0xc0, 0x5c, 0x0c, 0x06, 0x9f, 0xab, 0xc0, 0xde,
};

static const char *aes128_32_key = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmn";

static const uint8_t rtp_aes128_32[] = {
    // RTP header
    0x80, 0xe0, 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78,
    // encrypted payload
    0x62, 0x69, 0x76, 0xca, 0xc5,
    // HMAC
    0xa1, 0xac, 0x1b, 0xb4,
};

static const uint8_t rtcp_aes128_32[] = {
    // RTCP header
    0x81, 0xc9, 0x00, 0x07, 0x12, 0x34, 0x56, 0x78,
    // encrypted payload
    0x35, 0xe9, 0xb5, 0xff, 0x0d, 0xd1, 0xde, 0x70, 0x74, 0x10, 0xaa, 0x1b,
    0xb2, 0x8d, 0xf0, 0x20, 0x02, 0x99, 0x6b, 0x1b, 0x0b, 0xd0, 0x47, 0x34,
    // RTCP index
    0x80, 0x00, 0x00, 0x04,
    // HMAC
    0x5b, 0xd2, 0xa9, 0x9d,
};

static const char *aes128_80_32_key = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmn";

static const uint8_t rtp_aes128_80_32[] = {
    // RTP header
    0x80, 0xe0, 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78,
    // encrypted payload
    0x62, 0x69, 0x76, 0xca, 0xc5,
    // HMAC
    0xa1, 0xac, 0x1b, 0xb4,
};

static const uint8_t rtcp_aes128_80_32[] = {
    // RTCP header
    0x81, 0xc9, 0x00, 0x07, 0x12, 0x34, 0x56, 0x78,
    // encrypted payload
    0xd6, 0xae, 0xc1, 0x58, 0x63, 0x70, 0xc9, 0x88, 0x66, 0x26, 0x1c, 0x53,
    0xff, 0x5d, 0x5d, 0x2b, 0x0f, 0x8c, 0x72, 0x3e, 0xc9, 0x1d, 0x43, 0xf9,
    // RTCP index
    0x80, 0x00, 0x00, 0x05,
    // HMAC
    0x09, 0x16, 0xb4, 0x27, 0x9a, 0xe9, 0x92, 0x26, 0x4e, 0x10,
};

static void print_data(const uint8_t *buf, int len)
{
    int i;
    for (i = 0; i < len; i++)
        printf("%02x", buf[i]);
    printf("\n");
}

static int test_decrypt(struct SRTPContext *srtp, const uint8_t *in, int len,
                        uint8_t *out)
{
    memcpy(out, in, len);
    if (!ff_srtp_decrypt(srtp, out, &len)) {
        print_data(out, len);
        return len;
    } else
        return -1;
}

static void test_encrypt(const uint8_t *data, int in_len, const char *suite,
                         const char *key)
{
    struct SRTPContext enc = { 0 }, dec = { 0 };
    int len;
    char buf[RTP_MAX_PACKET_LENGTH];
    ff_srtp_set_crypto(&enc, suite, key);
    ff_srtp_set_crypto(&dec, suite, key);
    len = ff_srtp_encrypt(&enc, data, in_len, buf, sizeof(buf));
    if (!ff_srtp_decrypt(&dec, buf, &len)) {
        if (len == in_len && !memcmp(buf, data, len))
            printf("Decrypted content matches input\n");
        else
            printf("Decrypted content doesn't match input\n");
    } else {
        printf("Decryption failed\n");
    }
    ff_srtp_free(&enc);
    ff_srtp_free(&dec);
}

int main(void)
{
    static const char *aes128_80_suite = "AES_CM_128_HMAC_SHA1_80";
    static const char *aes128_32_suite = "AES_CM_128_HMAC_SHA1_32";
    static const char *aes128_80_32_suite = "SRTP_AES128_CM_HMAC_SHA1_32";
    static const char *test_key = "abcdefghijklmnopqrstuvwxyz1234567890ABCD";
    uint8_t buf[RTP_MAX_PACKET_LENGTH];
    struct SRTPContext srtp = { 0 };
    int len;
    ff_srtp_set_crypto(&srtp, aes128_80_suite, aes128_80_key);
    len = test_decrypt(&srtp, rtp_aes128_80, sizeof(rtp_aes128_80), buf);
    test_encrypt(buf, len, aes128_80_suite, test_key);
    test_encrypt(buf, len, aes128_32_suite, test_key);
    test_encrypt(buf, len, aes128_80_32_suite, test_key);
    test_decrypt(&srtp, rtcp_aes128_80, sizeof(rtcp_aes128_80), buf);
    test_encrypt(buf, len, aes128_80_suite, test_key);
    test_encrypt(buf, len, aes128_32_suite, test_key);
    test_encrypt(buf, len, aes128_80_32_suite, test_key);
    ff_srtp_free(&srtp);

    memset(&srtp, 0, sizeof(srtp)); // Clear the context
    ff_srtp_set_crypto(&srtp, aes128_32_suite, aes128_32_key);
    test_decrypt(&srtp, rtp_aes128_32, sizeof(rtp_aes128_32), buf);
    test_decrypt(&srtp, rtcp_aes128_32, sizeof(rtcp_aes128_32), buf);
    ff_srtp_free(&srtp);

    memset(&srtp, 0, sizeof(srtp)); // Clear the context
    ff_srtp_set_crypto(&srtp, aes128_80_32_suite, aes128_80_32_key);
    test_decrypt(&srtp, rtp_aes128_80_32, sizeof(rtp_aes128_80_32), buf);
    test_decrypt(&srtp, rtcp_aes128_80_32, sizeof(rtcp_aes128_80_32), buf);
    ff_srtp_free(&srtp);
    return 0;
}
#endif /* TEST */
