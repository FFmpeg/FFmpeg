/*
 * Discrete wavelet transform
 * Copyright (c) 2007 Kamil Nowosad
 * Copyright (c) 2013 Nicolas Bertrand <nicoinattendu@gmail.com>
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

#include "libavcodec/jpeg2000dwt.c"

#include "libavutil/lfg.h"

#define MAX_W 256

static int test_dwt(int *array, int *ref, int border[2][2], int decomp_levels, int type, int max_diff) {
    int ret, j;
    DWTContext s1={{{0}}}, *s= &s1;
    int64_t err2 = 0;

    ret = ff_jpeg2000_dwt_init(s,  border, decomp_levels, type);
    if (ret < 0) {
        fprintf(stderr, "ff_jpeg2000_dwt_init failed\n");
        return 1;
    }
    ret = ff_dwt_encode(s, array);
    if (ret < 0) {
        fprintf(stderr, "ff_dwt_encode failed\n");
        return 1;
    }
    ret = ff_dwt_decode(s, array);
    if (ret < 0) {
        fprintf(stderr, "ff_dwt_encode failed\n");
        return 1;
    }
    for (j = 0; j<MAX_W * MAX_W; j++) {
        if (FFABS(array[j] - (int64_t)ref[j]) > max_diff) {
            fprintf(stderr, "missmatch at %d (%d != %d) decomp:%d border %d %d %d %d\n",
                    j, array[j], ref[j],decomp_levels, border[0][0], border[0][1], border[1][0], border[1][1]);
            return 2;
        }
        err2 += (array[j] - ref[j]) * (int64_t)(array[j] - ref[j]);
        array[j] = ref[j];
    }
    ff_dwt_destroy(s);

    printf("%s, decomp:%2d border %3d %3d %3d %3d milli-err2:%9"PRId64"\n",
           type == FF_DWT53 ? "5/3i" : "9/7i",
           decomp_levels, border[0][0], border[0][1], border[1][0], border[1][1],
           1000*err2 / ((border[0][1] - border[0][0])*(border[1][1] - border[1][0])));

    return 0;
}

static int test_dwtf(float *array, float *ref, int border[2][2], int decomp_levels, float max_diff) {
    int ret, j;
    DWTContext s1={{{0}}}, *s= &s1;
    double err2 = 0;

    ret = ff_jpeg2000_dwt_init(s,  border, decomp_levels, FF_DWT97);
    if (ret < 0) {
        fprintf(stderr, "ff_jpeg2000_dwt_init failed\n");
        return 1;
    }
    ret = ff_dwt_encode(s, array);
    if (ret < 0) {
        fprintf(stderr, "ff_dwt_encode failed\n");
        return 1;
    }
    ret = ff_dwt_decode(s, array);
    if (ret < 0) {
        fprintf(stderr, "ff_dwt_encode failed\n");
        return 1;
    }
    for (j = 0; j<MAX_W * MAX_W; j++) {
        if (FFABS(array[j] - ref[j]) > max_diff) {
            fprintf(stderr, "missmatch at %d (%f != %f) decomp:%d border %d %d %d %d\n",
                    j, array[j], ref[j],decomp_levels, border[0][0], border[0][1], border[1][0], border[1][1]);
            return 2;
        }
        err2 += (array[j] - ref[j]) * (array[j] - ref[j]);
        array[j] = ref[j];
    }
    ff_dwt_destroy(s);

    printf("9/7f, decomp:%2d border %3d %3d %3d %3d err2:%20.3f\n",
           decomp_levels, border[0][0], border[0][1], border[1][0], border[1][1],
           err2 / ((border[0][1] - border[0][0])*(border[1][1] - border[1][0])));

    return 0;
}

static int array[MAX_W * MAX_W];
static int ref  [MAX_W * MAX_W];
static float arrayf[MAX_W * MAX_W];
static float reff  [MAX_W * MAX_W];

int main(void) {
    AVLFG prng;
    int i,j;
    int border[2][2];
    int ret, decomp_levels;

    av_lfg_init(&prng, 1);

    for (i = 0; i<MAX_W * MAX_W; i++)
        arrayf[i] = reff[i] = array[i] = ref[i] =  av_lfg_get(&prng) % 2048;

    for (i = 0; i < 100; i++) {
        for (j=0; j<4; j++)
            border[j>>1][j&1] = av_lfg_get(&prng) % MAX_W;
        if (border[0][0] >= border[0][1] || border[1][0] >= border[1][1])
            continue;
        decomp_levels = av_lfg_get(&prng) % FF_DWT_MAX_DECLVLS;

        ret = test_dwt(array, ref, border, decomp_levels, FF_DWT53, 0);
        if (ret)
            return ret;
        ret = test_dwt(array, ref, border, decomp_levels, FF_DWT97_INT, FFMIN(7+5*decomp_levels, 15+3*decomp_levels));
        if (ret)
            return ret;
        ret = test_dwtf(arrayf, reff, border, decomp_levels, 0.05);
        if (ret)
            return ret;
    }

    return 0;
}
