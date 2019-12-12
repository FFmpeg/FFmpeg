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
static const struct {uint8_t w, h, s;} planes[] = {
    {12,16,12}, {16,16,16}, {20,23,25}, {32,18,48}, {8,128,16}, {128,128,128}
};

#define MAX_STRIDE 128
#define MAX_HEIGHT 128

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

static void check_uyvy_to_422p(void)
{
    int i;

    LOCAL_ALIGNED_32(uint8_t, src0, [MAX_STRIDE * MAX_HEIGHT * 2]);
    LOCAL_ALIGNED_32(uint8_t, src1, [MAX_STRIDE * MAX_HEIGHT * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst_y_0, [MAX_STRIDE * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_y_1, [MAX_STRIDE * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_u_0, [(MAX_STRIDE/2) * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_u_1, [(MAX_STRIDE/2) * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_v_0, [(MAX_STRIDE/2) * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_v_1, [(MAX_STRIDE/2) * MAX_HEIGHT]);

    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                      const uint8_t *src, int width, int height,
                      int lumStride, int chromStride, int srcStride);

    randomize_buffers(src0, MAX_STRIDE * MAX_HEIGHT * 2);
    memcpy(src1, src0, MAX_STRIDE * MAX_HEIGHT * 2);

    if (check_func(uyvytoyuv422, "uyvytoyuv422")) {
        for (i = 0; i < 6; i ++) {
            memset(dst_y_0, 0, MAX_STRIDE * MAX_HEIGHT);
            memset(dst_y_1, 0, MAX_STRIDE * MAX_HEIGHT);
            memset(dst_u_0, 0, (MAX_STRIDE/2) * MAX_HEIGHT);
            memset(dst_u_1, 0, (MAX_STRIDE/2) * MAX_HEIGHT);
            memset(dst_v_0, 0, (MAX_STRIDE/2) * MAX_HEIGHT);
            memset(dst_v_1, 0, (MAX_STRIDE/2) * MAX_HEIGHT);

            call_ref(dst_y_0, dst_u_0, dst_v_0, src0, planes[i].w, planes[i].h,
                     MAX_STRIDE, MAX_STRIDE / 2, planes[i].s);
            call_new(dst_y_1, dst_u_1, dst_v_1, src1, planes[i].w, planes[i].h,
                     MAX_STRIDE, MAX_STRIDE / 2, planes[i].s);
            if (memcmp(dst_y_0, dst_y_1, MAX_STRIDE * MAX_HEIGHT) ||
                memcmp(dst_u_0, dst_u_1, (MAX_STRIDE/2) * MAX_HEIGHT) ||
                memcmp(dst_v_0, dst_v_1, (MAX_STRIDE/2) * MAX_HEIGHT))
                fail();
        }
        bench_new(dst_y_1, dst_u_1, dst_v_1, src1, planes[5].w, planes[5].h,
                  MAX_STRIDE, MAX_STRIDE / 2, planes[5].s);
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

    check_uyvy_to_422p();
    report("uyvytoyuv422");
}
