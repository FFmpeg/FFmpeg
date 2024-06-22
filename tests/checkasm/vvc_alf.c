/*
 * Copyright (c) 2023-2024 Nuo Mi <nuomi2021@gmail.com>
 * Copyright (c) 2023-2024 Wu Jianhua <toqsxw@outlook.com>
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
#include "libavcodec/vvc/ctu.h"
#include "libavcodec/vvc/data.h"
#include "libavcodec/vvc/dsp.h"

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff };

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define SRC_PIXEL_STRIDE (MAX_CTU_SIZE + 2 * ALF_PADDING_SIZE)
#define DST_PIXEL_STRIDE (SRC_PIXEL_STRIDE + 4)
#define SRC_BUF_SIZE (SRC_PIXEL_STRIDE * (MAX_CTU_SIZE + 3 * 2) * 2) //+3 * 2 for top and bottom row, *2 for high bit depth
#define DST_BUF_SIZE (DST_PIXEL_STRIDE * (MAX_CTU_SIZE + 3 * 2) * 2)
#define LUMA_PARAMS_SIZE (MAX_CTU_SIZE * MAX_CTU_SIZE / ALF_BLOCK_SIZE / ALF_BLOCK_SIZE * ALF_NUM_COEFF_LUMA)

#define randomize_buffers(buf0, buf1, size)                 \
    do {                                                    \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1];   \
        int k;                                              \
        for (k = 0; k < size; k += 4) {                     \
            uint32_t r = rnd() & mask;                      \
            AV_WN32A(buf0 + k, r);                          \
            AV_WN32A(buf1 + k, r);                          \
        }                                                   \
    } while (0)

#define randomize_buffers2(buf, size, filter)               \
    do {                                                    \
        int k;                                              \
        if (filter) {                                       \
            for (k = 0; k < size; k++) {                    \
                int8_t r = rnd();                           \
                buf[k] = r;                                 \
            }                                               \
        } else {                                            \
            for (k = 0; k < size; k++) {                    \
                int r = rnd() % FF_ARRAY_ELEMS(clip_set);   \
                buf[k] = clip_set[r];                       \
            }                                               \
        }                                                   \
    } while (0)

static int get_alf_vb_pos(const int h, const int vb_pos_above)
{
    if (h == MAX_CTU_SIZE)
        return MAX_CTU_SIZE - vb_pos_above;
    // If h < MAX_CTU_SIZE and picture virtual boundaries are involved, ALF virtual boundaries can either be within or outside this ALF block.
    return ((rnd() & 1) ? h : MAX_CTU_SIZE) - vb_pos_above;
}

static void check_alf_filter(VVCDSPContext *c, const int bit_depth)
{
    LOCAL_ALIGNED_32(uint8_t, dst0, [DST_BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [DST_BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, src0, [SRC_BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, src1, [SRC_BUF_SIZE]);
    int16_t filter[LUMA_PARAMS_SIZE];
    int16_t clip[LUMA_PARAMS_SIZE];

    const int16_t clip_set[] = {
        1 << bit_depth, 1 << (bit_depth - 3), 1 << (bit_depth - 5), 1 << (bit_depth - 7)
    };

    ptrdiff_t src_stride = SRC_PIXEL_STRIDE * SIZEOF_PIXEL;
    ptrdiff_t dst_stride = DST_PIXEL_STRIDE * SIZEOF_PIXEL;
    int offset = (3 * SRC_PIXEL_STRIDE + 3) * SIZEOF_PIXEL;

    declare_func(void, uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
        int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos);

    randomize_buffers(src0, src1, SRC_BUF_SIZE);
    randomize_buffers2(filter, LUMA_PARAMS_SIZE, 1);
    randomize_buffers2(clip, LUMA_PARAMS_SIZE, 0);

    for (int h = 4; h <= MAX_CTU_SIZE; h += 4) {
        for (int w = 4; w <= MAX_CTU_SIZE; w += 4) {
            //Both picture size and virtual boundaries are 8-aligned. For luma, we only need to check 8-aligned sizes.
            if (!(w % 8) && !(h % 8)) {
                if (check_func(c->alf.filter[LUMA], "vvc_alf_filter_luma_%dx%d_%d", w, h, bit_depth)) {
                    const int vb_pos = get_alf_vb_pos(h, ALF_VB_POS_ABOVE_LUMA);
                    memset(dst0, 0, DST_BUF_SIZE);
                    memset(dst1, 0, DST_BUF_SIZE);
                    call_ref(dst0, dst_stride, src0 + offset, src_stride, w, h, filter, clip, vb_pos);
                    call_new(dst1, dst_stride, src1 + offset, src_stride, w, h, filter, clip, vb_pos);
                    for (int i = 0; i < (h + 1); i++) {
                        if (memcmp(dst0 + i * dst_stride, dst1 + i * dst_stride, (w + 1) * SIZEOF_PIXEL))
                            fail();
                    }
                    // Bench only square sizes, and ones with dimensions being a power of two.
                    if (w == h && (w & (w - 1)) == 0)
                        bench_new(dst1, dst_stride, src1 + offset, src_stride, w, h, filter, clip, vb_pos);
                }
            }
            //For chroma, once it exceeds 64, it's not a 4:2:0 format, so we only need to check 8-aligned sizes as well.
            if ((w <= 64 || !(w % 8)) && (h <= 64 || !(h % 8))) {
                if (check_func(c->alf.filter[CHROMA], "vvc_alf_filter_chroma_%dx%d_%d", w, h, bit_depth)) {
                    const int vb_pos = get_alf_vb_pos(h, ALF_VB_POS_ABOVE_CHROMA);
                    memset(dst0, 0, DST_BUF_SIZE);
                    memset(dst1, 0, DST_BUF_SIZE);
                    call_ref(dst0, dst_stride, src0 + offset, src_stride, w, h, filter, clip, vb_pos);
                    call_new(dst1, dst_stride, src1 + offset, src_stride, w, h, filter, clip, vb_pos);
                    for (int i = 0; i < (h + 1); i++) {
                        if (memcmp(dst0 + i * dst_stride, dst1 + i * dst_stride, (w + 1) * SIZEOF_PIXEL))
                            fail();
                    }
                    if (w == h && (w & (w - 1)) == 0)
                        bench_new(dst1, dst_stride, src1 + offset, src_stride, w, h, filter, clip, vb_pos);
                }
            }
        }
    }
}

static void check_alf_classify(VVCDSPContext *c, const int bit_depth)
{
    LOCAL_ALIGNED_32(int, class_idx0, [SRC_BUF_SIZE]);
    LOCAL_ALIGNED_32(int, transpose_idx0, [SRC_BUF_SIZE]);
    LOCAL_ALIGNED_32(int, class_idx1, [SRC_BUF_SIZE]);
    LOCAL_ALIGNED_32(int, transpose_idx1, [SRC_BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, src0, [SRC_BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, src1, [SRC_BUF_SIZE]);
    LOCAL_ALIGNED_32(int32_t, alf_gradient_tmp, [ALF_GRADIENT_SIZE * ALF_GRADIENT_SIZE * ALF_NUM_DIR]);

    ptrdiff_t stride = SRC_PIXEL_STRIDE * SIZEOF_PIXEL;
    int offset = (3 * SRC_PIXEL_STRIDE + 3) * SIZEOF_PIXEL;

    declare_func(void, int *class_idx, int *transpose_idx,
        const uint8_t *src, ptrdiff_t src_stride, int width, int height, int vb_pos, int *gradient_tmp);

    randomize_buffers(src0, src1, SRC_BUF_SIZE);

    //Both picture size and virtual boundaries are 8-aligned. Classify is luma only, we only need to check 8-aligned sizes.
    for (int h = 8; h <= MAX_CTU_SIZE; h += 8) {
        for (int w = 8; w <= MAX_CTU_SIZE; w += 8) {
            const int id_size = w * h / ALF_BLOCK_SIZE / ALF_BLOCK_SIZE * sizeof(int);
            const int vb_pos  = get_alf_vb_pos(h, ALF_VB_POS_ABOVE_LUMA);
            if (check_func(c->alf.classify, "vvc_alf_classify_%dx%d_%d", w, h, bit_depth)) {
                memset(class_idx0, 0, id_size);
                memset(class_idx1, 0, id_size);
                memset(transpose_idx0, 0, id_size);
                memset(transpose_idx1, 0, id_size);
                call_ref(class_idx0, transpose_idx0, src0 + offset, stride, w, h, vb_pos, alf_gradient_tmp);

                call_new(class_idx1, transpose_idx1, src1 + offset, stride, w, h, vb_pos, alf_gradient_tmp);

                if (memcmp(class_idx0, class_idx1, id_size))
                    fail();
                if (memcmp(transpose_idx0, transpose_idx1, id_size))
                    fail();
                // Bench only square sizes, and ones with dimensions being a power of two.
                if (w == h && (w & (w - 1)) == 0)
                    bench_new(class_idx1, transpose_idx1, src1 + offset, stride, w, h, vb_pos, alf_gradient_tmp);
            }
        }
    }
}

void checkasm_check_vvc_alf(void)
{
    int bit_depth;
    VVCDSPContext h;
    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_vvc_dsp_init(&h, bit_depth);
        check_alf_filter(&h, bit_depth);
    }
    report("alf_filter");

    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_vvc_dsp_init(&h, bit_depth);
        check_alf_classify(&h, bit_depth);
    }
    report("alf_classify");
}
