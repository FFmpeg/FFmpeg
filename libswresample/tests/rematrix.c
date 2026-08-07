/*
 * Copyright (c) 2026 Ayoub Nabil Boubagrat
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

#include <limits.h>
#include <stdio.h>

#include "libavutil/channel_layout.h"
#include "libavutil/mathematics.h"
#include "libswresample/swresample.h"

/* swr_build_matrix2() accesses an internal SWR_CH_MAX by SWR_CH_MAX matrix. */
#define MATRIX_STRIDE 64

static int print_matrix(const AVChannelLayout *in_layout,
                        const AVChannelLayout *out_layout)
{
    double matrix[MATRIX_STRIDE * MATRIX_STRIDE] = { 0 };
    char in_name[16], out_name[16];
    int ret;

    /* Disable normalization so the raw downmix gains can be checked. */
    ret = swr_build_matrix2(in_layout, out_layout, M_SQRT1_2,
                            M_SQRT1_2,
                            0.0, INT_MAX, 1.0, matrix, MATRIX_STRIDE,
                            AV_MATRIX_ENCODING_NONE, NULL);
    if (ret < 0) {
        fprintf(stderr, "swr_build_matrix2 failed with error %d\n", ret);
        return 1;
    }

    for (int i = 0; i < 64; i++) {
        int out_i = av_channel_layout_index_from_channel(out_layout, i);
        if (out_i < 0)
            continue;
        av_channel_name(out_name, sizeof(out_name), i);
        printf("[%s] = { ", out_name);
        for (int j = 0; j < 64; j++) {
            int in_i = av_channel_layout_index_from_channel(in_layout, j);
            if (in_i < 0)
                continue;
            av_channel_name(in_name, sizeof(in_name), j);
            printf(".%s = %f, ", in_name, matrix[out_i * MATRIX_STRIDE + in_i]);
        }
        printf("},\n");
    }

    return 0;
}

int main(int argc, char **argv)
{
    AVChannelLayout in_layout = { 0 }, out_layout = { 0 };
    const char *in, *out;
    int ret = 0;

    if (argc != 3) {
        printf("usage: rematrix input_layout output_layout\n");
        return 0;
    }

    in  = argv[1];
    out = argv[2];

    ret = av_channel_layout_from_string(&in_layout, in);
    if (ret < 0) {
        if (ret == AVERROR(EINVAL))
            fprintf(stderr, "Invalid input layout %s\n", in);
        return 1;
    }

    ret = av_channel_layout_from_string(&out_layout, out);
    if (ret < 0) {
        if (ret == AVERROR(EINVAL))
            fprintf(stderr, "Invalid output layout %s\n", out);
        return 1;
    }

    if (in_layout.nb_channels > MATRIX_STRIDE ||
        out_layout.nb_channels > MATRIX_STRIDE) {
        fprintf(stderr, "channel layout exceeds matrix capacity\n");
        return 1;
    }

    ret = print_matrix(&in_layout, &out_layout);

    return ret;
}
