/*
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libavformat/rtpdec.h"
#include "libavformat/srtp.h"

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
