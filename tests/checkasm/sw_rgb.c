/*
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

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "libswscale/rgb2rgb.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)      \
    do {                                  \
        int j;                            \
        for (j = 0; j < size; j+=4)       \
            AV_WN32(buf + j, rnd());      \
    } while (0)

static const uint8_t width[] = {12, 16, 20, 32, 36, 128};

#define MAX_STRIDE 128

static void check_shuffle_bytes(void * func, const char * report)
{
    int i;
    LOCAL_ALIGNED_32(uint8_t, src0, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, src1, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [MAX_STRIDE]);

    declare_func_emms(AV_CPU_FLAG_MMX, void, const uint8_t *src, uint8_t *dst, int src_size);

    memset(dst0, 0, MAX_STRIDE);
    memset(dst1, 0, MAX_STRIDE);
    randomize_buffers(src0, MAX_STRIDE);
    memcpy(src1, src0, MAX_STRIDE);

    if (check_func(func, "%s", report)) {
        for (i = 0; i < 6; i ++) {
            call_ref(src0, dst0, width[i]);
            call_new(src1, dst1, width[i]);
            if (memcmp(dst0, dst1, MAX_STRIDE))
                fail();
        }
        bench_new(src0, dst0, width[5]);
    }
}

void checkasm_check_sw_rgb(void)
{
    ff_sws_rgb2rgb_init();

    check_shuffle_bytes(shuffle_bytes_2103, "shuffle_bytes_2103");
    report("shuffle_bytes_2103");

    check_shuffle_bytes(shuffle_bytes_0321, "shuffle_bytes_0321");
    report("shuffle_bytes_0321");

    check_shuffle_bytes(shuffle_bytes_1230, "shuffle_bytes_1230");
    report("shuffle_bytes_1230");

    check_shuffle_bytes(shuffle_bytes_3012, "shuffle_bytes_3012");
    report("shuffle_bytes_3012");

    check_shuffle_bytes(shuffle_bytes_3210, "shuffle_bytes_3210");
    report("shuffle_bytes_3210");
}
