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

#include "libavutil/hmac.c"

#include <stdio.h>
#include <string.h>

static void test(AVHMAC *hmac, const uint8_t *key, int keylen,
                 const uint8_t *data, int datalen)
{
    uint8_t buf[MAX_HASHLEN];
    int out, i;
    // Some of the test vectors are strings, where sizeof() includes the
    // trailing null byte - remove that.
    if (!key[keylen - 1])
        keylen--;
    if (!data[datalen - 1])
        datalen--;
    out = av_hmac_calc(hmac, data, datalen, key, keylen, buf, sizeof(buf));
    for (i = 0; i < out; i++)
        printf("%02x", buf[i]);
    printf("\n");
}

int main(void)
{
    uint8_t key1[20], key3[131], data3[50];
    AVHMAC *hmac;
    enum AVHMACType i;
    static const uint8_t key2[]  = "Jefe";
    static const uint8_t data1[] = "Hi There";
    static const uint8_t data2[] = "what do ya want for nothing?";
    static const uint8_t data4[] = "Test Using Larger Than Block-Size Key - Hash Key First";
    static const uint8_t data5[] = "Test Using Larger Than Block-Size Key and Larger Than One Block-Size Data";
    static const uint8_t data6[] = "This is a test using a larger than block-size key and a larger "
                            "than block-size data. The key needs to be hashed before being used"
                            " by the HMAC algorithm.";
    memset(key1, 0x0b, sizeof(key1));
    memset(key3, 0xaa, sizeof(key3));
    memset(data3, 0xdd, sizeof(data3));

    /* MD5, SHA-1 */
    for (i = AV_HMAC_MD5; i <= AV_HMAC_SHA1; i++) {
        hmac = av_hmac_alloc(i);
        if (!hmac)
            return 1;
        // RFC 2202 test vectors
        test(hmac, key1, hmac->hashlen, data1, sizeof(data1));
        test(hmac, key2, sizeof(key2),  data2, sizeof(data2));
        test(hmac, key3, hmac->hashlen, data3, sizeof(data3));
        test(hmac, key3, 80,            data4, sizeof(data4));
        test(hmac, key3, 80,            data5, sizeof(data5));
        av_hmac_free(hmac);
    }

    /* SHA-2 */
    for (i = AV_HMAC_SHA224; i <= AV_HMAC_SHA512; i++) {
        hmac = av_hmac_alloc(i);
        if (!hmac)
            return 1;
        // RFC 4231 test vectors
        test(hmac, key1, sizeof(key1), data1, sizeof(data1));
        test(hmac, key2, sizeof(key2), data2, sizeof(data2));
        test(hmac, key3, 20,           data3, sizeof(data3));
        test(hmac, key3, sizeof(key3), data4, sizeof(data4));
        test(hmac, key3, sizeof(key3), data6, sizeof(data6));
        av_hmac_free(hmac);
    }

    return 0;
}
