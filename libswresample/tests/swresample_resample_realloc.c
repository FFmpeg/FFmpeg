/*
 * Exercise the swr_convert(N) -> swr_convert(2N) edge case where the
 * second call reuses the internal preout buffer at full capacity, with
 * no trailing slack from swri_realloc_audio()'s amortized doubling.
 * Forces internal_sample_fmt=S16P to reach the int16 SIMD resample path.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "libswresample/swresample.h"

int main(void)
{
    const int IN_RATE  = 48000;
    const int OUT_RATE = 16000;
    /* First call asks for N out frames, second call asks for 2N. */
    const int N1_OUT = 160;
    const int N2_OUT = 320;
    const int N1_IN  = N1_OUT * IN_RATE / OUT_RATE; /* 480 */
    const int N2_IN  = N2_OUT * IN_RATE / OUT_RATE; /* 960 */

    SwrContext *swr = swr_alloc();
    AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
    int ret = 0;

    if (!swr) {
        fprintf(stderr, "swr_alloc failed\n");
        return 1;
    }

    av_opt_set_chlayout   (swr, "in_chlayout",          &mono,                 0);
    av_opt_set_chlayout   (swr, "out_chlayout",         &mono,                 0);
    av_opt_set_int        (swr, "in_sample_rate",       IN_RATE,               0);
    av_opt_set_int        (swr, "out_sample_rate",      OUT_RATE,              0);
    av_opt_set_sample_fmt (swr, "in_sample_fmt",        AV_SAMPLE_FMT_S16,     0);
    av_opt_set_sample_fmt (swr, "out_sample_fmt",       AV_SAMPLE_FMT_S16,     0);
    /* Force the int16 SIMD resample path. */
    av_opt_set_sample_fmt (swr, "internal_sample_fmt",  AV_SAMPLE_FMT_S16P,    0);

    if ((ret = swr_init(swr)) < 0) {
        fprintf(stderr, "swr_init failed: %d\n", ret);
        ret = 1;
        goto end;
    }

    {
        int16_t *input = av_calloc(N2_IN,  sizeof(int16_t));
        int16_t *out   = av_calloc(N2_OUT, sizeof(int16_t));
        const uint8_t *in_planes[1];
        uint8_t       *out_planes[1];
        int i, n;

        if (!input || !out) {
            fprintf(stderr, "alloc failed\n");
            av_free(input);
            av_free(out);
            ret = 1;
            goto end;
        }

        /* Non-zero samples so the SIMD inner loop produces real data. */
        for (i = 0; i < N2_IN; ++i)
            input[i] = (int16_t)((i * 7) & 0x3fff);

        /* Call #1: out_count = N. swri_realloc_audio() doubles count and
         * grows s->preout to capacity 2N (e.g. 640 bytes for N=160). */
        in_planes[0]  = (const uint8_t *)input;
        out_planes[0] = (uint8_t *)out;
        n = swr_convert(swr, out_planes, N1_OUT, in_planes, N1_IN);
        if (n < 0) {
            fprintf(stderr, "swr_convert call#1 failed: %d\n", n);
            av_free(input);
            av_free(out);
            ret = 1;
            goto end;
        }

        /* Call #2: out_count = 2N. a->count == 2N, so swri_realloc_audio()
         * skips realloc and reuses the existing buffer at full capacity. */
        in_planes[0]  = (const uint8_t *)input;
        out_planes[0] = (uint8_t *)out;
        n = swr_convert(swr, out_planes, N2_OUT, in_planes, N2_IN);
        if (n < 0) {
            fprintf(stderr, "swr_convert call#2 failed: %d\n", n);
            av_free(input);
            av_free(out);
            ret = 1;
            goto end;
        }

        av_free(input);
        av_free(out);
    }

end:
    swr_free(&swr);
    return ret;
}
