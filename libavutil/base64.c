/*
 * Copyright (c) 2006 Ryan Martell. (rdm4@martellventures.com)
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
 * @file libavutil/base64.c
 * @brief Base64 encode/decode
 * @author Ryan Martell <rdm4@martellventures.com> (with lots of Michael)
 */

#include "common.h"
#include "base64.h"

/* ---------------- private code */
static const uint8_t map2[] =
{
    0x3e, 0xff, 0xff, 0xff, 0x3f, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1a, 0x1b,
    0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
    0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33
};

int av_base64_decode(uint8_t * out, const char *in, int out_length)
{
    int i, v;
    uint8_t *dst = out;

    v = 0;
    for (i = 0; in[i] && in[i] != '='; i++) {
        unsigned int index= in[i]-43;
        if (index>=FF_ARRAY_ELEMS(map2) || map2[index] == 0xff)
            return -1;
        v = (v << 6) + map2[index];
        if (i & 3) {
            if (dst - out < out_length) {
                *dst++ = v >> (6 - 2 * (i & 3));
            }
        }
    }

    return dst - out;
}

/*****************************************************************************
* b64_encode: Stolen from VLC's http.c.
* Simplified by Michael.
* Fixed edge cases and made it work from data (vs. strings) by Ryan.
*****************************************************************************/

char *av_base64_encode(char * buf, int buf_len, const uint8_t * src, int len)
{
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *ret, *dst;
    unsigned i_bits = 0;
    int i_shift = 0;
    int bytes_remaining = len;

    if (len >= UINT_MAX / 4 ||
        buf_len < len * 4 / 3 + 12)
        return NULL;
    ret = dst = buf;
    while (bytes_remaining) {
        i_bits = (i_bits << 8) + *src++;
        bytes_remaining--;
        i_shift += 8;

        do {
            *dst++ = b64[(i_bits << 6 >> i_shift) & 0x3f];
            i_shift -= 6;
        } while (i_shift > 6 || (bytes_remaining == 0 && i_shift > 0));
    }
    while ((dst - ret) & 3)
        *dst++ = '=';
    *dst = '\0';

    return ret;
}

#ifdef TEST
#include "log.h"
#include "mem.h"

int main(void)
{
    int numerr = 0;
    int len;
    int numtest = 1;
    uint8_t decode[1000];
    struct test {
        void *data;
        int len;
        const char *result;
    } *t, tests[] = {
        {
        "", 0, ""}, {
        "1", 1, "MQ=="}, {
        "22", 2, "MjI="}, {
        "333", 3, "MzMz"}, {
        "4444", 4, "NDQ0NA=="}, {
        "55555", 5, "NTU1NTU="}, {
        "abc:def", 7, "YWJjOmRlZg=="}, {
        NULL}
    };
    for (t = tests; t->data; t++) {
        char *str;

        av_log(NULL, AV_LOG_ERROR, "Encoding %s...\n", (char *) t->data);
        str = av_base64_encode(t->data, t->len);
        if (str) {
            av_log(NULL, AV_LOG_ERROR, "Encoded to %s...\n", str);
            if (strcmp(str, t->result) != 0) {
                av_log(NULL, AV_LOG_ERROR, "failed test %d: %s != %s\n",
                       numtest, str, t->result);
                numerr++;
            }
            av_free(str);
        }

        av_log(NULL, AV_LOG_ERROR, "Done encoding, about to decode...\n");
        len = av_base64_decode(decode, t->result, sizeof(decode));
        if (len != t->len) {
            av_log(NULL, AV_LOG_ERROR, "failed test %d: len %d != %d\n",
                   numtest, len, t->len);
            numerr++;
        } else if (memcmp(decode, t->data, t->len) != 0) {
            av_log(NULL, AV_LOG_ERROR, "failed test %d: data\n", numtest);
            numerr++;
        } else {
            av_log(NULL, AV_LOG_ERROR, "Decoded to %s\n",
                   (char *) t->data);
        }
        numtest++;
    }

#undef srand
#undef rand

    {
        int test_count;
        srand(123141);          // time(NULL));
        for (test_count = 0; test_count < 100; test_count++) {
            int size = rand() % 1024;
            int ii;
            uint8_t *data;
            char *encoded_result;

            av_log(NULL, AV_LOG_ERROR, "Test %d: Size %d bytes...",
                   test_count, size);
            data = (uint8_t *) av_malloc(size);
            for (ii = 0; ii < size; ii++) {
                data[ii] = rand() % 255;
            }

            encoded_result = av_base64_encode(data, size);
            if (encoded_result) {
                int decode_buffer_size = size + 10;     // try without 10 as well
                uint8_t *decode_buffer = av_malloc(decode_buffer_size);
                if (decode_buffer) {
                    int decoded_size =
                        av_base64_decode(decode_buffer, encoded_result,
                                   decode_buffer_size);

                    if (decoded_size != size) {
                        av_log(NULL, AV_LOG_ERROR,
                               "Decoded/Encoded size mismatch (%d != %d)\n",
                               decoded_size, size);
                    } else {
                        if (memcmp(decode_buffer, data, decoded_size) == 0) {
                            av_log(NULL, AV_LOG_ERROR, "Passed!\n");
                        } else {
                            av_log(NULL, AV_LOG_ERROR,
                                   "Failed (Data differs)!\n");
                        }
                    }
                    av_free(decode_buffer);
                }

                av_free(encoded_result);
            }
        }
    }

    // these are invalid strings, that it currently decodes (which it probably shouldn't?)
    {
        uint8_t str[32];
        if (av_base64_decode(str, "M=M=", sizeof(str)) != -1) {
            av_log(NULL, AV_LOG_ERROR,
                   "failed test %d: successful decode of `M=M='\n",
                   numtest++);
            numerr++;
        }
        if (av_base64_decode(str, "MQ===", sizeof(str)) != -1) {
            av_log(NULL, AV_LOG_ERROR,
                   "failed test %d: successful decode of `MQ==='\n",
                   numtest++);
            numerr++;
        }
    }

    return numerr;
}
#endif

