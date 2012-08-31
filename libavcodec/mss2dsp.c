/*
 * Microsoft Screen 2 (aka Windows Media Video V9 Screen) decoder
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

/**
 * @file
 * Microsoft Screen 2 (aka Windows Media Video V9 Screen) decoder DSP routines
 */

#include "mss2dsp.h"
#include "libavutil/common.h"

static av_always_inline void mss2_blit_wmv9_template(uint8_t *dst,
                                                     int dst_stride,
                                                     int gray,
                                                     int use_mask,
                                                     int maskcolor,
                                                     const uint8_t *mask,
                                                     int mask_stride,
                                                     const uint8_t *srcy,
                                                     int srcy_stride,
                                                     const uint8_t *srcu,
                                                     const uint8_t *srcv,
                                                     int srcuv_stride,
                                                     int w, int h)
{
    int i, j, k, r = -1;
    while (++r < h) {
        for (i = 0, j = 0, k = 0; i < w; j += (i & 1), i++, k += 3) {
            if (!use_mask || mask[i] == maskcolor) {
                if (gray) {
                    dst[k] = dst[k + 1] = dst[k + 2] = 0x80;
                } else {
                    int y = srcy[i];
                    int u = srcu[j] - 128;
                    int v = srcv[j] - 128;
                    dst[k]     = av_clip_uint8(y + (             91881 * v + 32768 >> 16));
                    dst[k + 1] = av_clip_uint8(y + (-22554 * u - 46802 * v + 32768 >> 16));
                    dst[k + 2] = av_clip_uint8(y + (116130 * u             + 32768 >> 16));
                }
            }
        }
        mask +=  mask_stride;
        dst  +=   dst_stride;
        srcy +=  srcy_stride;
        srcu += srcuv_stride * (r & 1);
        srcv += srcuv_stride * (r & 1);
    }
}

static void mss2_blit_wmv9_c(uint8_t *dst, int dst_stride,
                             const uint8_t *srcy, int srcy_stride,
                             const uint8_t *srcu, const uint8_t *srcv,
                             int srcuv_stride, int w, int h)
{
    mss2_blit_wmv9_template(dst, dst_stride, 0, 0,
                            0, NULL, 0,
                            srcy, srcy_stride,
                            srcu, srcv, srcuv_stride,
                            w, h);
}

static void mss2_blit_wmv9_masked_c(uint8_t *dst, int dst_stride,
                                    int maskcolor, const uint8_t *mask,
                                    int mask_stride,
                                    const uint8_t *srcy, int srcy_stride,
                                    const uint8_t *srcu, const uint8_t *srcv,
                                    int srcuv_stride, int w, int h)
{
    mss2_blit_wmv9_template(dst, dst_stride, 0, 1,
                            maskcolor, mask, mask_stride,
                            srcy, srcy_stride,
                            srcu, srcv, srcuv_stride,
                            w, h);
}

static void mss2_gray_fill_masked_c(uint8_t *dst, int dst_stride,
                                    int maskcolor, const uint8_t *mask,
                                    int mask_stride, int w, int h)
{
    mss2_blit_wmv9_template(dst, dst_stride, 1, 1,
                            maskcolor, mask, mask_stride,
                            NULL, 0,
                            NULL, NULL, 0,
                            w, h);
}

static void upsample_plane_c(uint8_t *plane, int plane_stride, int w, int h)
{
    uint8_t *src1, *src2, *dst1, *dst2, *p, a, b;
    int i, j;

    w += (w & 1);
    h += (h & 1);

    j = h - 1;

    memcpy(plane + plane_stride *  j,
           plane + plane_stride * (j >> 1),
           w);

    while ((j -= 2) > 0) {
        dst1 = plane + plane_stride *  (j + 1);
        dst2 = plane + plane_stride *   j;
        src1 = plane + plane_stride * ((j + 1) >> 1);
        src2 = plane + plane_stride * ( j      >> 1);

        for (i = (w - 1) >> 1; i >= 0; i--) {
            a = src1[i];
            b = src2[i];
            dst1[i] = (3 * a + b + 2) >> 2;
            dst2[i] = (a + 3 * b + 2) >> 2;
        }
    }

    for (j = h - 1; j >= 0; j--) {
        p = plane + plane_stride * j;
        i = w - 1;

        p[i] = p[i >> 1];

        while ((i -= 2) > 0) {
            a = p[ i      >> 1];
            b = p[(i + 1) >> 1];
            p[i]     = (3 * a + b + 1) >> 2;
            p[i + 1] = (a + 3 * b + 1) >> 2;
        }
    }
}

av_cold void ff_mss2dsp_init(MSS2DSPContext* dsp)
{
    dsp->mss2_blit_wmv9        = mss2_blit_wmv9_c;
    dsp->mss2_blit_wmv9_masked = mss2_blit_wmv9_masked_c;
    dsp->mss2_gray_fill_masked = mss2_gray_fill_masked_c;
    dsp->upsample_plane        = upsample_plane_c;
}
