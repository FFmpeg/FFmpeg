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

static int check_coefficient_with_slev(const AVChannelLayout *in_layout,
                                       const AVChannelLayout *out_layout,
                                       enum AVChannel in_channel,
                                       enum AVChannel out_channel,
                                       double surround_mix_level,
                                       double expected)
{
    double matrix[MATRIX_STRIDE * MATRIX_STRIDE] = { 0 };
    char in_name[16], out_name[16];
    int in, out, ret;

    av_channel_name(in_name, sizeof(in_name), in_channel);
    av_channel_name(out_name, sizeof(out_name), out_channel);

    if (in_layout->nb_channels > MATRIX_STRIDE ||
        out_layout->nb_channels > MATRIX_STRIDE) {
        fprintf(stderr, "channel layout exceeds matrix capacity\n");
        return 1;
    }

    in  = av_channel_layout_index_from_channel(in_layout,  in_channel);
    out = av_channel_layout_index_from_channel(out_layout, out_channel);
    if (in < 0) {
        fprintf(stderr, "input channel %s is not in the input layout\n", in_name);
        return 1;
    }
    if (out < 0) {
        fprintf(stderr, "output channel %s is not in the output layout\n", out_name);
        return 1;
    }

    /* Disable normalization so the raw downmix gains can be checked. */
    ret = swr_build_matrix2(in_layout, out_layout, M_SQRT1_2,
                            surround_mix_level,
                            0.0, INT_MAX, 1.0, matrix, MATRIX_STRIDE,
                            AV_MATRIX_ENCODING_NONE, NULL);
    if (ret < 0) {
        fprintf(stderr, "swr_build_matrix2 failed with error %d\n", ret);
        return 1;
    }

    if (fabs(matrix[out * MATRIX_STRIDE + in] - expected) > 1e-12) {
        fprintf(stderr, "%s -> %s: expected %.12f, got %.12f\n",
                in_name, out_name, expected,
                matrix[out * MATRIX_STRIDE + in]);
        return 1;
    }

    return 0;
}

static int check_coefficient(const AVChannelLayout *in_layout,
                             const AVChannelLayout *out_layout,
                             enum AVChannel in_channel,
                             enum AVChannel out_channel, double expected)
{
    return check_coefficient_with_slev(in_layout, out_layout, in_channel,
                                       out_channel, M_SQRT1_2, expected);
}

int main(void)
{
    const AVChannelLayout mono          = AV_CHANNEL_LAYOUT_MONO;
    const AVChannelLayout stereo        = AV_CHANNEL_LAYOUT_STEREO;
    const AVChannelLayout surround      = AV_CHANNEL_LAYOUT_5POINT1;
    const AVChannelLayout surround_back = AV_CHANNEL_LAYOUT_5POINT1_BACK;
    const AVChannelLayout surround_2    = AV_CHANNEL_LAYOUT_5POINT1POINT2;
    const AVChannelLayout surround_4    = AV_CHANNEL_LAYOUT_5POINT1POINT4_BACK;
    const AVChannelLayout surround_tbc  = AV_CHANNEL_LAYOUT_7POINT2POINT3;
    int ret = 0;

    ret |= check_coefficient(&surround_2, &stereo,
                             AV_CHAN_TOP_FRONT_LEFT, AV_CHAN_FRONT_LEFT, 1.0);
    ret |= check_coefficient(&surround_2, &stereo,
                             AV_CHAN_TOP_FRONT_RIGHT, AV_CHAN_FRONT_RIGHT, 1.0);
    ret |= check_coefficient(&surround_4, &surround_2,
                             AV_CHAN_TOP_BACK_LEFT, AV_CHAN_TOP_FRONT_LEFT,
                             M_SQRT1_2);
    ret |= check_coefficient(&surround_4, &surround_2,
                             AV_CHAN_TOP_BACK_RIGHT, AV_CHAN_TOP_FRONT_RIGHT,
                             M_SQRT1_2);
    ret |= check_coefficient(&surround_4, &surround_tbc,
                             AV_CHAN_TOP_BACK_LEFT, AV_CHAN_TOP_BACK_CENTER,
                             M_SQRT1_2);
    ret |= check_coefficient(&surround_4, &surround_tbc,
                             AV_CHAN_TOP_BACK_RIGHT, AV_CHAN_TOP_BACK_CENTER,
                             M_SQRT1_2);
    ret |= check_coefficient(&surround_4, &surround,
                             AV_CHAN_TOP_BACK_LEFT, AV_CHAN_SIDE_LEFT, 1.0);
    ret |= check_coefficient(&surround_4, &surround,
                             AV_CHAN_TOP_BACK_RIGHT, AV_CHAN_SIDE_RIGHT, 1.0);
    ret |= check_coefficient(&surround_4, &surround_back,
                             AV_CHAN_TOP_BACK_LEFT, AV_CHAN_BACK_LEFT, 1.0);
    ret |= check_coefficient(&surround_4, &surround_back,
                             AV_CHAN_TOP_BACK_RIGHT, AV_CHAN_BACK_RIGHT, 1.0);
    ret |= check_coefficient(&surround_4, &stereo,
                             AV_CHAN_TOP_BACK_LEFT, AV_CHAN_FRONT_LEFT,
                             M_SQRT1_2);
    ret |= check_coefficient(&surround_4, &stereo,
                             AV_CHAN_TOP_BACK_RIGHT, AV_CHAN_FRONT_RIGHT,
                             M_SQRT1_2);
    ret |= check_coefficient_with_slev(&surround_4, &stereo,
                                       AV_CHAN_TOP_BACK_LEFT,
                                       AV_CHAN_FRONT_LEFT, 0.5, 0.5);
    ret |= check_coefficient_with_slev(&surround_4, &stereo,
                                       AV_CHAN_TOP_BACK_RIGHT,
                                       AV_CHAN_FRONT_RIGHT, 0.5, 0.5);
    ret |= check_coefficient(&surround_4, &mono,
                             AV_CHAN_TOP_BACK_LEFT, AV_CHAN_FRONT_CENTER,
                             M_SQRT1_2);
    ret |= check_coefficient(&surround_4, &mono,
                             AV_CHAN_TOP_BACK_RIGHT, AV_CHAN_FRONT_CENTER,
                             M_SQRT1_2);
    ret |= check_coefficient(&surround_tbc, &surround,
                             AV_CHAN_TOP_BACK_CENTER, AV_CHAN_SIDE_LEFT,
                             M_SQRT1_2);
    ret |= check_coefficient(&surround_tbc, &surround,
                             AV_CHAN_TOP_BACK_CENTER, AV_CHAN_SIDE_RIGHT,
                             M_SQRT1_2);
    ret |= check_coefficient(&surround_tbc, &surround_4,
                             AV_CHAN_TOP_BACK_CENTER, AV_CHAN_TOP_BACK_LEFT,
                             M_SQRT1_2);
    ret |= check_coefficient(&surround_tbc, &surround_4,
                             AV_CHAN_TOP_BACK_CENTER, AV_CHAN_TOP_BACK_RIGHT,
                             M_SQRT1_2);
    ret |= check_coefficient(&surround_tbc, &surround_back,
                             AV_CHAN_TOP_BACK_CENTER, AV_CHAN_BACK_LEFT,
                             M_SQRT1_2);
    ret |= check_coefficient(&surround_tbc, &surround_back,
                             AV_CHAN_TOP_BACK_CENTER, AV_CHAN_BACK_RIGHT,
                             M_SQRT1_2);
    ret |= check_coefficient(&surround_tbc, &stereo,
                             AV_CHAN_TOP_BACK_CENTER, AV_CHAN_FRONT_LEFT, 0.5);
    ret |= check_coefficient(&surround_tbc, &stereo,
                             AV_CHAN_TOP_BACK_CENTER, AV_CHAN_FRONT_RIGHT, 0.5);
    ret |= check_coefficient(&surround_tbc, &mono,
                             AV_CHAN_TOP_BACK_CENTER, AV_CHAN_FRONT_CENTER, 0.5);

    return ret;
}
