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

static int channel_is_unused(const AVChannelLayout *layout, int index)
{
    return av_channel_layout_channel_from_index(layout, index) == AV_CHAN_UNUSED;
}

static void print_matrix_row(const double *matrix,
                             const AVChannelLayout *in_layout,
                             int out, const char *out_name)
{
    char in_name[16];

    printf("[%s] = { ", out_name);
    for (int i = 0; i < 64; i++) {
        enum AVChannel in_ch = av_channel_layout_channel_from_index(in_layout, i);
        if (in_ch == AV_CHAN_NONE)
            continue;
        av_channel_name(in_name, sizeof(in_name), in_ch);
        if (in_ch == AV_CHAN_UNUSED)
            printf(".UNSD%d = %f, ", i,
                   matrix[out * MATRIX_STRIDE + i]);
        else
            printf(".%s = %f, ", in_name, matrix[out * MATRIX_STRIDE + i]);
    }
    printf("},\n");
}

static int print_matrix(const AVChannelLayout *in_layout,
                        const AVChannelLayout *out_layout)
{
    double matrix[MATRIX_STRIDE * MATRIX_STRIDE] = { 0 };
    char out_name[16];
    int ret;

    /* ensure swr_build_matrix2() overwrites unused columns and rows with zero. */
    for (int out = 0; out < out_layout->nb_channels; out++)
        for (int in = 0; in < in_layout->nb_channels; in++)
            if (channel_is_unused(in_layout, in) ||
                channel_is_unused(out_layout, out))
                matrix[out * MATRIX_STRIDE + in] = 1.0;

    /* Disable normalization so the raw downmix gains can be checked. */
    ret = swr_build_matrix2(in_layout, out_layout, M_SQRT1_2,
                            M_SQRT1_2,
                            0.0, INT_MAX, 1.0, matrix, MATRIX_STRIDE,
                            AV_MATRIX_ENCODING_NONE, NULL);
    if (ret < 0) {
        fprintf(stderr, "swr_build_matrix2 failed with error %d\n", ret);
        return 1;
    }

    for (int out = 0; out < out_layout->nb_channels; out++) {
        for (int in = 0; in < in_layout->nb_channels; in++) {
            if ((channel_is_unused(in_layout, in) ||
                 channel_is_unused(out_layout, out)) &&
                matrix[out * MATRIX_STRIDE + in] != 0.0) {
                fprintf(stderr, "unused matrix position %d:%d is non-zero\n",
                        out, in);
                return 1;
            }
        }
    }

    for (int i = 0; i < out_layout->nb_channels; i++) {
        enum AVChannel out_ch = av_channel_layout_channel_from_index(out_layout, i);
        if (out_ch == AV_CHAN_NONE)
            continue;
        if (out_ch == AV_CHAN_UNUSED)
            snprintf(out_name, sizeof(out_name), "UNSD%d", i);
        else
            av_channel_name(out_name, sizeof(out_name), out_ch);
        print_matrix_row(matrix, in_layout, i, out_name);
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
        ret = 1;
        goto end;
    }

    ret = av_channel_layout_from_string(&out_layout, out);
    if (ret < 0) {
        if (ret == AVERROR(EINVAL))
            fprintf(stderr, "Invalid output layout %s\n", out);
        ret = 1;
        goto end;
    }

    if (in_layout.nb_channels > MATRIX_STRIDE ||
        out_layout.nb_channels > MATRIX_STRIDE) {
        fprintf(stderr, "channel layout exceeds matrix capacity\n");
        ret = 1;
        goto end;
    }

    ret = print_matrix(&in_layout, &out_layout);

end:
    av_channel_layout_uninit(&in_layout);
    av_channel_layout_uninit(&out_layout);

    return ret;
}
