/*
 * Copyright (c) 2018 Yingming Fan <yingmingfan@gmail.com>
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

#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/avcodec.h"

#include "libavcodec/hevcdsp.h"

#include "checkasm.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff };
static const uint32_t sao_size[5] = {8, 16, 32, 48, 64};

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define PIXEL_STRIDE (2*MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE) //same with sao_edge src_stride
#define BUF_SIZE (PIXEL_STRIDE * (64+2) * 2) //+2 for top and bottom row, *2 for high bit depth
#define OFFSET_THRESH (1 << (bit_depth - 5))
#define OFFSET_LENGTH 5

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

#define randomize_buffers2(buf, size)                       \
    do {                                                    \
        uint32_t max_offset = OFFSET_THRESH;                \
        int k;                                              \
        if (bit_depth == 8) {                               \
            for (k = 0; k < size; k++) {                    \
                uint8_t r = rnd() % max_offset;             \
                buf[k] = r;                                 \
            }                                               \
        } else {                                            \
            for (k = 0; k < size; k++) {                    \
                uint16_t r = rnd() % max_offset;            \
                buf[k] = r;                                 \
            }                                               \
        }                                                   \
    } while (0)

static void check_sao_band(HEVCDSPContext h, int bit_depth)
{
    int i;
    LOCAL_ALIGNED_32(uint8_t, dst0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, src0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, src1, [BUF_SIZE]);
    int16_t offset_val[OFFSET_LENGTH];
    int left_class = rnd()%32;

    for (i = 0; i <= 4; i++) {
        int block_size = sao_size[i];
        int prev_size = i > 0 ? sao_size[i - 1] : 0;
        ptrdiff_t stride = PIXEL_STRIDE*SIZEOF_PIXEL;
        declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *dst, uint8_t *src, ptrdiff_t dst_stride, ptrdiff_t src_stride,
                          int16_t *sao_offset_val, int sao_left_class, int width, int height);

        if (check_func(h.sao_band_filter[i], "hevc_sao_band_%d_%d", block_size, bit_depth)) {

            for (int w = prev_size + 4; w <= block_size; w += 4) {
                randomize_buffers(src0, src1, BUF_SIZE);
                randomize_buffers2(offset_val, OFFSET_LENGTH);
                memset(dst0, 0, BUF_SIZE);
                memset(dst1, 0, BUF_SIZE);

                call_ref(dst0, src0, stride, stride, offset_val, left_class, w, block_size);
                call_new(dst1, src1, stride, stride, offset_val, left_class, w, block_size);
                for (int j = 0; j < block_size; j++) {
                    if (memcmp(dst0 + j*stride, dst1 + j*stride, w*SIZEOF_PIXEL))
                        fail();
                }
            }
            bench_new(dst1, src1, stride, stride, offset_val, left_class, block_size, block_size);
        }
    }
}

static void check_sao_edge(HEVCDSPContext h, int bit_depth)
{
    int i;
    LOCAL_ALIGNED_32(uint8_t, dst0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, src0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, src1, [BUF_SIZE]);
    int16_t offset_val[OFFSET_LENGTH];
    int eo = rnd()%4;

    for (i = 0; i <= 4; i++) {
        int block_size = sao_size[i];
        int prev_size = i > 0 ? sao_size[i - 1] : 0;
        ptrdiff_t stride = PIXEL_STRIDE*SIZEOF_PIXEL;
        int offset = (AV_INPUT_BUFFER_PADDING_SIZE + PIXEL_STRIDE)*SIZEOF_PIXEL;
        declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *dst, uint8_t *src, ptrdiff_t stride_dst,
                          int16_t *sao_offset_val, int eo, int width, int height);

        for (int w = prev_size + 4; w <= block_size; w += 4) {
            randomize_buffers(src0, src1, BUF_SIZE);
            randomize_buffers2(offset_val, OFFSET_LENGTH);
            memset(dst0, 0, BUF_SIZE);
            memset(dst1, 0, BUF_SIZE);

            if (check_func(h.sao_edge_filter[i], "hevc_sao_edge_%d_%d", block_size, bit_depth)) {
                call_ref(dst0, src0 + offset, stride, offset_val, eo, w, block_size);
                call_new(dst1, src1 + offset, stride, offset_val, eo, w, block_size);
                for (int j = 0; j < block_size; j++) {
                    if (memcmp(dst0 + j*stride, dst1 + j*stride, w*SIZEOF_PIXEL))
                        fail();
                }
            }
            bench_new(dst1, src1 + offset, stride, offset_val, eo, block_size, block_size);
        }
    }
}

void checkasm_check_hevc_sao(void)
{
    int bit_depth;

    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        HEVCDSPContext h;

        ff_hevc_dsp_init(&h, bit_depth);
        check_sao_band(h, bit_depth);
    }
    report("sao_band");

    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        HEVCDSPContext h;

        ff_hevc_dsp_init(&h, bit_depth);
        check_sao_edge(h, bit_depth);
    }
    report("sao_edge");
}
