/*
 * Copyright (c) 2015 James Almer
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>
#include "checkasm.h"
#include "libavcodec/flacdsp.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define BUF_SIZE 256
#define MAX_CHANNELS 8

#define randomize_buffers()                                 \
    do {                                                    \
        int i, j;                                           \
        for (i = 0; i < BUF_SIZE; i += 4) {                 \
            for (j = 0; j < channels; j++) {                \
                uint32_t r = rnd() & (1 << (bits - 2)) - 1; \
                AV_WN32A(ref_src[j] + i, r);                \
                AV_WN32A(new_src[j] + i, r);                \
            }                                               \
        }                                                   \
    } while (0)

static void check_decorrelate(uint8_t **ref_dst, uint8_t **ref_src, uint8_t **new_dst, uint8_t **new_src,
                              int channels, int bits) {
    declare_func(void, uint8_t **out, int32_t **in, int channels, int len, int shift);

    randomize_buffers();
    call_ref(ref_dst, (int32_t **)ref_src, channels, BUF_SIZE / sizeof(int32_t), 8);
    call_new(new_dst, (int32_t **)new_src, channels, BUF_SIZE / sizeof(int32_t), 8);
    if (memcmp(*ref_dst, *new_dst, bits == 16 ? BUF_SIZE * (channels/2) : BUF_SIZE * channels) ||
        memcmp(*ref_src, *new_src, BUF_SIZE * channels))
        fail();
    bench_new(new_dst, (int32_t **)new_src, channels, BUF_SIZE / sizeof(int32_t), 8);
}

void checkasm_check_flacdsp(void)
{
    LOCAL_ALIGNED_16(uint8_t, ref_dst, [BUF_SIZE*MAX_CHANNELS]);
    LOCAL_ALIGNED_16(uint8_t, ref_buf, [BUF_SIZE*MAX_CHANNELS]);
    LOCAL_ALIGNED_16(uint8_t, new_dst, [BUF_SIZE*MAX_CHANNELS]);
    LOCAL_ALIGNED_16(uint8_t, new_buf, [BUF_SIZE*MAX_CHANNELS]);
    uint8_t *ref_src[] = { &ref_buf[BUF_SIZE*0], &ref_buf[BUF_SIZE*1], &ref_buf[BUF_SIZE*2], &ref_buf[BUF_SIZE*3],
                           &ref_buf[BUF_SIZE*4], &ref_buf[BUF_SIZE*5], &ref_buf[BUF_SIZE*6], &ref_buf[BUF_SIZE*7] };
    uint8_t *new_src[] = { &new_buf[BUF_SIZE*0], &new_buf[BUF_SIZE*1], &new_buf[BUF_SIZE*2], &new_buf[BUF_SIZE*3],
                           &new_buf[BUF_SIZE*4], &new_buf[BUF_SIZE*5], &new_buf[BUF_SIZE*6], &new_buf[BUF_SIZE*7] };
    static const char * const names[3] = { "ls", "rs", "ms" };
    static const struct {
        enum AVSampleFormat fmt;
        int bits;
    } fmts[] = {
        { AV_SAMPLE_FMT_S16, 16 },
        { AV_SAMPLE_FMT_S32, 32 },
    };
    FLACDSPContext h;
    int i, j;

    for (i = 0; i < 2; i++) {
        ff_flacdsp_init(&h, fmts[i].fmt, 2, 0);
        for (j = 0; j < 3; j++)
            if (check_func(h.decorrelate[j], "flac_decorrelate_%s_%d", names[j], fmts[i].bits))
                check_decorrelate(&ref_dst, ref_src, &new_dst, new_src, 2, fmts[i].bits);
        for (j = 2; j <= MAX_CHANNELS; j += 2) {
            ff_flacdsp_init(&h, fmts[i].fmt, j, 0);
            if (check_func(h.decorrelate[0], "flac_decorrelate_indep%d_%d", j, fmts[i].bits))
                check_decorrelate(&ref_dst, ref_src, &new_dst, new_src, j, fmts[i].bits);
        }
    }

    report("decorrelate");
}
