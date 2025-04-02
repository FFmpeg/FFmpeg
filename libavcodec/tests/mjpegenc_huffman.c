/*
 * Copyright (c) 2016 William Ma, Sofia Kim, Dustin Woo
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
 * Optimal Huffman Encoding tests.
 */

#include <stdio.h>

#include "libavutil/avassert.h"
#include "libavutil/macros.h"

#include "libavcodec/mjpegenc_huffman.c"

// Validate the computed lengths satisfy the JPEG restrictions and is optimal.
static int check_lengths(int L, const int *probs, int nprobs,
                         int expected_length, const uint8_t expected_len_counts[/* L + 1 */])
{
    PTable val_counts[256];
    uint8_t len_counts[17];
    int actual_length = 0, i;
    int ret = 0;
    av_assert0(nprobs <= 256);
    av_assert0(L < FF_ARRAY_ELEMS(len_counts));

    for (i = 0; i < nprobs; i++) {
        val_counts[i] = (PTable){.value = i, .prob = probs[i]};
    }

    mjpegenc_huffman_compute_bits(val_counts, len_counts, nprobs, L);

    // Test that the lengths can be made part of a complete, prefix-free tree:
    unsigned code = 0, count = 0;
    for (int i = 1; i <= L; ++i) {
        count += len_counts[i];
        code <<= 1;
        code += len_counts[i];
    }
    if (code > 1U << L) {
        fprintf(stderr, "Huffman tree overdetermined/invalid\n");
        ret = 1;
    }
    if (count != nprobs) {
        fprintf(stderr, "Total count %u does not match expected value %d\n",
                count, nprobs);
        ret = 1;
    }
    // Test that the input values have been properly ordered.
    for (unsigned i = 0; i < count; ++i) {
        if (val_counts[i].prob != probs[val_counts[i].value]) {
            fprintf(stderr, "PTable not properly reordered\n");
            ret = 1;
        }
        if (i && val_counts[i - 1].prob > val_counts[i].prob) {
            fprintf(stderr, "PTable not order ascendingly: [%u] = %d > [%u] = %d\n",
                    i - 1, val_counts[i - 1].prob, i, val_counts[i].prob);
            ret = 1;
        }
        unsigned j;
        for (j = 0; j < count; ++j)
            if (val_counts[j].value == i)
                break;
        if (j >= count) {
            fprintf(stderr, "Element %u missing after sorting\n", i);
            ret = 1;
        }
    }
    for (int len = L, j = 0; len; --len) {
        int prob = 0;
        for (int end = j + len_counts[len]; j < end; ++j)
            prob += val_counts[j].prob;
        actual_length += prob * len;
    }
    // Check that the total length is optimal
    if (actual_length != expected_length) ret = 1;

    if (ret == 1) {
      fprintf(stderr,
              "Actual length: %d\n"
              "Expected length: %d\n",
              actual_length, expected_length);
    }

    return ret;
}

static const int probs_zeroes[] = {
    6, 6, 0, 0, 0
};
static const uint8_t len_counts_zeroes[] = {
    0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2,
};

static const int probs_skewed[] = {
    2, 0, 0, 0, 0, 1, 0, 0, 20, 0, 2, 0, 10, 5, 1, 1, 9, 1, 1, 6, 0, 5, 0, 1, 0, 7, 6,
    1, 1, 5, 0, 0, 0, 0, 11, 0, 0, 0, 51, 1, 0, 20, 0, 1, 0, 0, 0, 0, 6, 106, 1, 0, 1,
    0, 2, 1, 16, 0, 0, 5, 0, 0, 0, 4, 3, 15, 4, 4, 0, 0, 0, 3, 0, 0, 1, 0, 3, 0, 3, 2,
    2, 0, 0, 4, 3, 40, 1, 2, 0, 22, 0, 0, 0, 9, 0, 0, 0, 0, 1, 1, 0, 1, 6, 11, 4, 10,
    28, 6, 1, 0, 0, 9, 9, 4, 0, 0, 0, 0, 8, 33844, 2, 0, 2, 1, 1, 5, 0, 0, 1, 9, 1, 0,
    4, 14, 4, 0, 0, 3, 8, 0, 51, 9, 6, 1, 1, 2, 2, 3, 1, 5, 5, 29, 0, 0, 0, 0, 14, 29,
    6, 4, 13, 12, 2, 3, 1, 0, 5, 4, 1, 1, 0, 0, 29, 1, 0, 0, 0, 0, 4, 0, 0, 1, 0, 1,
    7, 0, 42, 0, 0, 0, 0, 0, 2, 0, 3, 9, 0, 0, 0, 2, 1, 0, 0, 6, 5, 6, 1, 2, 3, 0, 0,
    0, 3, 0, 0, 28, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 23, 0, 0, 0, 0,
    0, 21, 1, 0, 3, 24, 2, 0, 0, 7, 0, 0, 1, 5, 1, 2, 0, 5
};
static const uint8_t len_counts_skewed[] = {
    0, 1, 0, 0, 1, 2, 7, 11, 18, 31, 28, 40, 0, 1, 0, 0, 116,
};

static const int probs_sat[] = {
    74, 8, 14, 7, 9345, 40, 0, 2014, 2, 1, 115, 0, 2, 1, 194, 388, 20, 0, 0, 2, 1, 121,
    1, 1583, 0, 16, 21, 2, 132, 2, 15, 9, 13, 1, 0, 2293, 2, 8, 5, 2, 30, 0, 0, 4, 54,
    783, 4, 1, 2, 4, 0, 22, 93, 1, 143, 19, 0, 36, 32, 4, 6, 33, 3, 45, 0, 8, 1, 0, 18,
    17, 1, 0, 1, 0, 0, 1, 1004, 38, 3, 8, 90, 23, 0, 2819, 3, 0, 970, 158, 9, 6, 4, 48,
    4, 0, 1, 0, 0, 60, 3, 62, 0, 2, 2, 2, 279, 66, 16, 1, 20, 0, 7, 9, 32, 1411, 6, 3,
    27, 1, 5, 49, 0, 0, 0, 0, 0, 2, 10, 1, 1, 2, 3, 801, 3, 25, 5, 1, 1, 0, 632, 0, 14,
    18, 5, 8, 200, 4, 4, 22, 12, 0, 4, 1, 0, 2, 4, 9, 3, 16, 7, 2, 2, 213, 0, 2, 620,
    39303, 0, 1, 0, 2, 1, 183781, 1, 0, 0, 0, 94, 7, 3, 4, 0, 4, 306, 43, 352, 76, 34,
    13, 11, 0, 51, 1, 13, 19, 0, 26, 0, 7276, 4, 207, 31, 1, 2, 4, 6, 19, 8, 17, 4, 6,
    0, 1085, 0, 0, 0, 3, 489, 36, 1, 0, 1, 9420, 294, 28, 0, 57, 5, 0, 9, 2, 0, 1, 2,
    2, 0, 0, 9, 2, 29, 2, 2, 7, 0, 5, 490, 0, 7, 5, 0, 1, 8, 0, 0, 23255, 0, 1
};
static const uint8_t len_counts_sat[] = {
    0, 1, 0, 2, 1, 2, 2, 5, 5, 7, 7, 8, 17, 23, 16, 24, 136,
};

// Test the example given on @see
// http://guru.multimedia.cx/small-tasks-for-ffmpeg/
int main(int argc, char **argv)
{
    enum {
        MAX_LEN = 3,
    };
    int ret = 0;
    // Probabilities of symbols 0..4
    PTable val_counts[] = {
        {.value = 0, .prob = 1},
        {.value = 1, .prob = 2},
        {.value = 2, .prob = 5},
        {.value = 3, .prob = 10},
        {.value = 4, .prob = 21},
    };
    // Expected code lengths for each symbol
    static const uint8_t expected[MAX_LEN + 1] = {
        [1] = 1, [3] = 4,
    };
    // Actual code lengths
    uint8_t len_counts[MAX_LEN + 1];

    // Build optimal huffman tree using an internal function, to allow for
    // smaller-than-normal test cases. This mutates val_counts by sorting.
    mjpegenc_huffman_compute_bits(val_counts, len_counts,
                                  FF_ARRAY_ELEMS(val_counts), MAX_LEN);

    for (unsigned i = 1; i < FF_ARRAY_ELEMS(len_counts); i++) {
        if (len_counts[i] != expected[i]) {
            fprintf(stderr,
                    "Built huffman does not equal expectations. "
                    "Expected: %d codes of length %u, "
                    "Actual: %d codes of length %u\n",
                    (int)expected[i], i,
                    (int)len_counts[i], i);
            ret = 1;
        }
    }
    for (unsigned i = 1; i < FF_ARRAY_ELEMS(val_counts); ++i) {
        if (val_counts[i - 1].prob > val_counts[i].prob) {
            fprintf(stderr, "Probability table not ordered ascendingly. "
                    "val_counts[%u] == %d, val_counts[%u] == %d\n",
                    i - 1, val_counts[i - 1].prob, i, val_counts[i].prob);
            ret = 1;
        }
    }

    // Check handling of zero probabilities
    if (check_lengths(16, probs_zeroes, FF_ARRAY_ELEMS(probs_zeroes), 18, len_counts_zeroes))
        ret = 1;
    // Check skewed distribution over 256 without saturated lengths
    if (check_lengths(16, probs_skewed, FF_ARRAY_ELEMS(probs_skewed), 41282, len_counts_skewed))
        ret = 1;
    // Check skewed distribution over 256 with saturated lengths
    if (check_lengths(16, probs_sat, FF_ARRAY_ELEMS(probs_sat), 669904, len_counts_sat))
        ret = 1;

    return ret;
}
