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

// LCOV_EXCL_START

#include "libavutil/timer.h"

#include <stdint.h>
#include <stdio.h>

#include "libavutil/common.h"
#include "libavutil/base64.h"

#define MAX_DATA_SIZE    1024
#define MAX_ENCODED_SIZE 2048

static int test_encode_decode(const uint8_t *data, unsigned int data_size,
                              const char *encoded_ref)
{
    char  encoded[MAX_ENCODED_SIZE];
    uint8_t data2[MAX_DATA_SIZE];
    int data2_size, max_data2_size = MAX_DATA_SIZE;

    if (!av_base64_encode(encoded, MAX_ENCODED_SIZE, data, data_size)) {
        printf("Failed: cannot encode the input data\n");
        return 1;
    }
    if (encoded_ref && strcmp(encoded, encoded_ref)) {
        printf("Failed: encoded string differs from reference\n"
               "Encoded:\n%s\nReference:\n%s\n", encoded, encoded_ref);
        return 1;
    }

    if ((data2_size = av_base64_decode(data2, encoded, max_data2_size)) != data_size) {
        printf("Failed: cannot decode the encoded string\n"
               "Encoded:\n%s\n", encoded);
        return 1;
    }
    if ((data2_size = av_base64_decode(data2, encoded, data_size)) != data_size) {
        printf("Failed: cannot decode with minimal buffer\n"
               "Encoded:\n%s\n", encoded);
        return 1;
    }
    if (memcmp(data2, data, data_size)) {
        printf("Failed: encoded/decoded data differs from original data\n");
        return 1;
    }
    if (av_base64_decode(NULL, encoded, 0) != 0) {
        printf("Failed: decode to NULL buffer\n");
        return 1;
    }
    if (strlen(encoded)) {
        char *end = strchr(encoded, '=');
        if (!end)
            end = encoded + strlen(encoded) - 1;
        *end = '%';
        if (av_base64_decode(NULL, encoded, 0) >= 0) {
            printf("Failed: error detection\n");
            return 1;
        }
    }

    printf("Passed!\n");
    return 0;
}

int main(int argc, char ** argv)
{
    int i, error_count = 0;
    struct test {
        const uint8_t *data;
        const char *encoded_ref;
    } tests[] = {
        { "",        ""},
        { "1",       "MQ=="},
        { "22",      "MjI="},
        { "333",     "MzMz"},
        { "4444",    "NDQ0NA=="},
        { "55555",   "NTU1NTU="},
        { "666666",  "NjY2NjY2"},
        { "abc:def", "YWJjOmRlZg=="},
    };
    char in[1024], out[2048];

    printf("Encoding/decoding tests\n");
    for (i = 0; i < FF_ARRAY_ELEMS(tests); i++)
        error_count += test_encode_decode(tests[i].data, strlen(tests[i].data), tests[i].encoded_ref);

    if (argc>1 && !strcmp(argv[1], "-t")) {
        memset(in, 123, sizeof(in));
        for(i=0; i<10000; i++){
            START_TIMER
            av_base64_encode(out, sizeof(out), in, sizeof(in));
            STOP_TIMER("encode")
        }
        for(i=0; i<10000; i++){
            START_TIMER
            av_base64_decode(in, out, sizeof(in));
            STOP_TIMER("decode")
        }

        for(i=0; i<10000; i++){
            START_TIMER
            av_base64_decode(NULL, out, 0);
            STOP_TIMER("syntax check")
        }
    }

    if (error_count)
        printf("Error Count: %d.\n", error_count);

    return !!error_count;
}

// LCOV_EXCL_STOP
