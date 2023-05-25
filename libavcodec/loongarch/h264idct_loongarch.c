/*
 * Loongson LSX/LASX optimized h264idct
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 * Contributed by Shiyou Yin <yinshiyou-hf@loongson.cn>
 *                Xiwei  Gu  <guxiwei-hf@loongson.cn>
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

#include "h264dsp_loongarch.h"
#include "libavcodec/bit_depth_template.c"

void ff_h264_idct_add16_8_lsx(uint8_t *dst, const int32_t *blk_offset,
                              int16_t *block, int32_t dst_stride,
                              const uint8_t nzc[15 * 8])
{
    int32_t i;

    for (i = 0; i < 16; i++) {
        int32_t nnz = nzc[scan8[i]];

        if (nnz == 1 && ((dctcoef *) block)[i * 16]) {
            ff_h264_idct_dc_add_8_lsx(dst + blk_offset[i],
                                      block + i * 16 * sizeof(pixel),
                                      dst_stride);
    } else if (nnz) {
            ff_h264_idct_add_8_lsx(dst + blk_offset[i],
                                   block + i * 16 * sizeof(pixel),
                                   dst_stride);
        }
    }
}

void ff_h264_idct8_add4_8_lsx(uint8_t *dst, const int32_t *blk_offset,
                              int16_t *block, int32_t dst_stride,
                              const uint8_t nzc[15 * 8])
{
    int32_t cnt;

    for (cnt = 0; cnt < 16; cnt += 4) {
        int32_t nnz = nzc[scan8[cnt]];

        if (nnz == 1 && ((dctcoef *) block)[cnt * 16]) {
            ff_h264_idct8_dc_add_8_lsx(dst + blk_offset[cnt],
                                        block + cnt * 16 * sizeof(pixel),
                                        dst_stride);
        } else if (nnz) {
            ff_h264_idct8_add_8_lsx(dst + blk_offset[cnt],
                                     block + cnt * 16 * sizeof(pixel),
                                     dst_stride);
        }
    }
}

#if HAVE_LASX
void ff_h264_idct8_add4_8_lasx(uint8_t *dst, const int32_t *blk_offset,
                               int16_t *block, int32_t dst_stride,
                               const uint8_t nzc[15 * 8])
{
    int32_t cnt;

    for (cnt = 0; cnt < 16; cnt += 4) {
        int32_t nnz = nzc[scan8[cnt]];

        if (nnz == 1 && ((dctcoef *) block)[cnt * 16]) {
            ff_h264_idct8_dc_add_8_lasx(dst + blk_offset[cnt],
                                        block + cnt * 16 * sizeof(pixel),
                                        dst_stride);
        } else if (nnz) {
            ff_h264_idct8_add_8_lasx(dst + blk_offset[cnt],
                                     block + cnt * 16 * sizeof(pixel),
                                     dst_stride);
        }
    }
}
#endif // #if HAVE_LASX

void ff_h264_idct_add8_8_lsx(uint8_t **dst, const int32_t *blk_offset,
                             int16_t *block, int32_t dst_stride,
                             const uint8_t nzc[15 * 8])
{
    int32_t i;

    for (i = 16; i < 20; i++) {
        if (nzc[scan8[i]])
            ff_h264_idct_add_8_lsx(dst[0] + blk_offset[i],
                                   block + i * 16 * sizeof(pixel),
                                   dst_stride);
        else if (((dctcoef *) block)[i * 16])
            ff_h264_idct_dc_add_8_lsx(dst[0] + blk_offset[i],
                                      block + i * 16 * sizeof(pixel),
                                      dst_stride);
    }
    for (i = 32; i < 36; i++) {
        if (nzc[scan8[i]])
            ff_h264_idct_add_8_lsx(dst[1] + blk_offset[i],
                                   block + i * 16 * sizeof(pixel),
                                   dst_stride);
        else if (((dctcoef *) block)[i * 16])
            ff_h264_idct_dc_add_8_lsx(dst[1] + blk_offset[i],
                                      block + i * 16 * sizeof(pixel),
                                      dst_stride);
    }
}

void ff_h264_idct_add8_422_8_lsx(uint8_t **dst, const int32_t *blk_offset,
                                 int16_t *block, int32_t dst_stride,
                                 const uint8_t nzc[15 * 8])
{
    int32_t i;

    for (i = 16; i < 20; i++) {
        if (nzc[scan8[i]])
            ff_h264_idct_add_8_lsx(dst[0] + blk_offset[i],
                                   block + i * 16 * sizeof(pixel),
                                   dst_stride);
        else if (((dctcoef *) block)[i * 16])
            ff_h264_idct_dc_add_8_lsx(dst[0] + blk_offset[i],
                                      block + i * 16 * sizeof(pixel),
                                      dst_stride);
    }
    for (i = 20; i < 24; i++) {
        if (nzc[scan8[i + 4]])
            ff_h264_idct_add_8_lsx(dst[0] + blk_offset[i + 4],
                                   block + i * 16 * sizeof(pixel),
                                   dst_stride);
        else if (((dctcoef *) block)[i * 16])
            ff_h264_idct_dc_add_8_lsx(dst[0] + blk_offset[i + 4],
                                      block + i * 16 * sizeof(pixel),
                                      dst_stride);
    }
    for (i = 32; i < 36; i++) {
        if (nzc[scan8[i]])
            ff_h264_idct_add_8_lsx(dst[1] + blk_offset[i],
                                   block + i * 16 * sizeof(pixel),
                                   dst_stride);
        else if (((dctcoef *) block)[i * 16])
            ff_h264_idct_dc_add_8_lsx(dst[1] + blk_offset[i],
                                      block + i * 16 * sizeof(pixel),
                                      dst_stride);
    }
    for (i = 36; i < 40; i++) {
        if (nzc[scan8[i + 4]])
            ff_h264_idct_add_8_lsx(dst[1] + blk_offset[i + 4],
                                   block + i * 16 * sizeof(pixel),
                                   dst_stride);
        else if (((dctcoef *) block)[i * 16])
            ff_h264_idct_dc_add_8_lsx(dst[1] + blk_offset[i + 4],
                                      block + i * 16 * sizeof(pixel),
                                      dst_stride);
    }
}

void ff_h264_idct_add16_intra_8_lsx(uint8_t *dst, const int32_t *blk_offset,
                                    int16_t *block, int32_t dst_stride,
                                    const uint8_t nzc[15 * 8])
{
    int32_t i;

    for (i = 0; i < 16; i++) {
        if (nzc[scan8[i]])
            ff_h264_idct_add_8_lsx(dst + blk_offset[i],
                                   block + i * 16 * sizeof(pixel), dst_stride);
        else if (((dctcoef *) block)[i * 16])
            ff_h264_idct_dc_add_8_lsx(dst + blk_offset[i],
                                      block + i * 16 * sizeof(pixel),
                                      dst_stride);
    }
}
