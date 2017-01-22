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

#include "checkasm.h"
#include "libavcodec/jpeg2000dsp.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

#define BUF_SIZE 512

#define randomize_buffers()                 \
    do {                                    \
        int i;                              \
        for (i = 0; i < BUF_SIZE; i += 4) { \
            uint32_t r = rnd();             \
            AV_WN32A(ref0 + i, r);          \
            AV_WN32A(new0 + i, r);          \
            r = rnd();                      \
            AV_WN32A(ref1 + i, r);          \
            AV_WN32A(new1 + i, r);          \
            r = rnd();                      \
            AV_WN32A(ref2 + i, r);          \
            AV_WN32A(new2 + i, r);          \
        }                                   \
    } while (0)

static void check_mct(uint8_t *ref0, uint8_t *ref1, uint8_t *ref2,
                      uint8_t *new0, uint8_t *new1, uint8_t *new2) {
    declare_func(void, void *src0, void *src1, void *src2, int csize);

    randomize_buffers();
    call_ref(ref0, ref1, ref2, BUF_SIZE / sizeof(int32_t));
    call_new(new0, new1, new2, BUF_SIZE / sizeof(int32_t));
    if (memcmp(ref0, new0, BUF_SIZE) || memcmp(ref1, new1, BUF_SIZE) ||
        memcmp(ref2, new2, BUF_SIZE))
        fail();
    bench_new(new0, new1, new2, BUF_SIZE / sizeof(int32_t));
}

void checkasm_check_jpeg2000dsp(void)
{
    LOCAL_ALIGNED_32(uint8_t, ref, [BUF_SIZE*3]);
    LOCAL_ALIGNED_32(uint8_t, new, [BUF_SIZE*3]);
    Jpeg2000DSPContext h;

    ff_jpeg2000dsp_init(&h);

    if (check_func(h.mct_decode[FF_DWT53], "jpeg2000_rct_int"))
        check_mct(&ref[BUF_SIZE*0], &ref[BUF_SIZE*1], &ref[BUF_SIZE*2],
                  &new[BUF_SIZE*0], &new[BUF_SIZE*1], &new[BUF_SIZE*2]);

    report("mct_decode");
}
