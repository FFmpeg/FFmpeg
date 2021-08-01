/*
 * MJPEG encoder
 * Copyright (c) 2016 William Ma, Ted Ying, Jerry Jiang
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

#include <string.h>
#include <stdint.h>
#include "libavutil/avassert.h"
#include "libavutil/qsort.h"
#include "mjpegenc_huffman.h"

/**
 * Comparison function for two PTables by prob
 *
 * @param a First PTable to compare
 * @param b Second PTable to compare
 * @return < 0 for less than, 0 for equals, > 0 for greater than
 */
static int compare_by_prob(const void *a, const void *b)
{
    PTable a_val = *(PTable *) a;
    PTable b_val = *(PTable *) b;
    return a_val.prob - b_val.prob;
}

/**
 * Comparison function for two HuffTables by length
 *
 * @param a First HuffTable to compare
 * @param b Second HuffTable to compare
 * @return < 0 for less than, 0 for equals, > 0 for greater than
 */
static int compare_by_length(const void *a, const void *b)
{
    HuffTable a_val = *(HuffTable *) a;
    HuffTable b_val = *(HuffTable *) b;
    return a_val.length - b_val.length;
}

/**
 * Computes the length of the Huffman encoding for each distinct input value.
 * Uses package merge algorithm as follows:
 * 1. start with an empty list, lets call it list(0), set i = 0
 * 2. add 1 entry to list(i) for each symbol we have and give each a score equal to the probability of the respective symbol
 * 3. merge the 2 symbols of least score and put them in list(i+1), and remove them from list(i). The new score will be the sum of the 2 scores
 * 4. if there is more than 1 symbol left in the current list(i), then goto 3
 * 5. i++
 * 6. if i < 16 goto 2
 * 7. select the n-1 elements in the last list with the lowest score (n = the number of symbols)
 * 8. the length of the huffman code for symbol s will be equal to the number of times the symbol occurs in the select elements
 * Go to guru.multimedia.cx/small-tasks-for-ffmpeg/ for more details
 *
 * All probabilities should be positive integers. The output is sorted by code,
 * not by length.
 *
 * @param prob_table input array of a PTable for each distinct input value
 * @param distincts  output array of a HuffTable that will be populated by this function
 * @param size       size of the prob_table array
 * @param max_length max length of an encoding
 */
void ff_mjpegenc_huffman_compute_bits(PTable *prob_table, HuffTable *distincts, int size, int max_length)
{
    PackageMergerList list_a, list_b, *to = &list_a, *from = &list_b, *temp;

    int times, i, j, k;

    int nbits[257] = {0};

    int min;

    av_assert0(max_length > 0);

    to->nitems = 0;
    from->nitems = 0;
    to->item_idx[0] = 0;
    from->item_idx[0] = 0;
    AV_QSORT(prob_table, size, PTable, compare_by_prob);

    for (times = 0; times <= max_length; times++) {
        to->nitems = 0;
        to->item_idx[0] = 0;

        j = 0;
        k = 0;

        if (times < max_length) {
            i = 0;
        }
        while (i < size || j + 1 < from->nitems) {
            to->nitems++;
            to->item_idx[to->nitems] = to->item_idx[to->nitems - 1];
            if (i < size &&
                (j + 1 >= from->nitems ||
                 prob_table[i].prob <
                     from->probability[j] + from->probability[j + 1])) {
                to->items[to->item_idx[to->nitems]++] = prob_table[i].value;
                to->probability[to->nitems - 1] = prob_table[i].prob;
                i++;
            } else {
                for (k = from->item_idx[j]; k < from->item_idx[j + 2]; k++) {
                    to->items[to->item_idx[to->nitems]++] = from->items[k];
                }
                to->probability[to->nitems - 1] =
                    from->probability[j] + from->probability[j + 1];
                j += 2;
            }
        }
        temp = to;
        to = from;
        from = temp;
    }

    min = (size - 1 < from->nitems) ? size - 1 : from->nitems;
    for (i = 0; i < from->item_idx[min]; i++) {
        nbits[from->items[i]]++;
    }
    // we don't want to return the 256 bit count (it was just in here to prevent
    // all 1s encoding)
    j = 0;
    for (i = 0; i < 256; i++) {
        if (nbits[i] > 0) {
            distincts[j].code = i;
            distincts[j].length = nbits[i];
            j++;
        }
    }
}

void ff_mjpeg_encode_huffman_init(MJpegEncHuffmanContext *s)
{
    memset(s->val_count, 0, sizeof(s->val_count));
}

/**
 * Produces a Huffman encoding with a given input
 *
 * @param s         input to encode
 * @param bits      output array where the ith character represents how many input values have i length encoding
 * @param val       output array of input values sorted by their encoded length
 * @param max_nval  maximum number of distinct input values
 */
void ff_mjpeg_encode_huffman_close(MJpegEncHuffmanContext *s, uint8_t bits[17],
                                   uint8_t val[], int max_nval)
{
    int i, j;
    int nval = 0;
    PTable val_counts[257];
    HuffTable distincts[256];

    for (i = 0; i < 256; i++) {
        if (s->val_count[i]) nval++;
    }
    av_assert0 (nval <= max_nval);

    j = 0;
    for (i = 0; i < 256; i++) {
        if (s->val_count[i]) {
            val_counts[j].value = i;
            val_counts[j].prob = s->val_count[i];
            j++;
        }
    }
    val_counts[j].value = 256;
    val_counts[j].prob = 0;
    ff_mjpegenc_huffman_compute_bits(val_counts, distincts, nval + 1, 16);
    AV_QSORT(distincts, nval, HuffTable, compare_by_length);

    memset(bits, 0, sizeof(bits[0]) * 17);
    for (i = 0; i < nval; i++) {
        val[i] = distincts[i].code;
        bits[distincts[i].length]++;
    }
}
