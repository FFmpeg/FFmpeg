/*
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "mpegvideodsp.h"

static void gmc1_c(uint8_t *dst, uint8_t *src, int stride, int h,
                   int x16, int y16, int rounder)
{
    const int A = (16 - x16) * (16 - y16);
    const int B = (x16)      * (16 - y16);
    const int C = (16 - x16) * (y16);
    const int D = (x16)      * (y16);
    int i;

    for (i = 0; i < h; i++) {
        dst[0] = (A * src[0] + B * src[1] + C * src[stride + 0] + D * src[stride + 1] + rounder) >> 8;
        dst[1] = (A * src[1] + B * src[2] + C * src[stride + 1] + D * src[stride + 2] + rounder) >> 8;
        dst[2] = (A * src[2] + B * src[3] + C * src[stride + 2] + D * src[stride + 3] + rounder) >> 8;
        dst[3] = (A * src[3] + B * src[4] + C * src[stride + 3] + D * src[stride + 4] + rounder) >> 8;
        dst[4] = (A * src[4] + B * src[5] + C * src[stride + 4] + D * src[stride + 5] + rounder) >> 8;
        dst[5] = (A * src[5] + B * src[6] + C * src[stride + 5] + D * src[stride + 6] + rounder) >> 8;
        dst[6] = (A * src[6] + B * src[7] + C * src[stride + 6] + D * src[stride + 7] + rounder) >> 8;
        dst[7] = (A * src[7] + B * src[8] + C * src[stride + 7] + D * src[stride + 8] + rounder) >> 8;
        dst   += stride;
        src   += stride;
    }
}

void ff_gmc_c(uint8_t *dst, uint8_t *src, int stride, int h, int ox, int oy,
              int dxx, int dxy, int dyx, int dyy, int shift, int r,
              int width, int height)
{
    int y, vx, vy;
    const int s = 1 << shift;

    width--;
    height--;

    for (y = 0; y < h; y++) {
        int x;

        vx = ox;
        vy = oy;
        for (x = 0; x < 8; x++) { // FIXME: optimize
            int index;
            int src_x  = vx >> 16;
            int src_y  = vy >> 16;
            int frac_x = src_x & (s - 1);
            int frac_y = src_y & (s - 1);

            src_x >>= shift;
            src_y >>= shift;

            if ((unsigned) src_x < width) {
                if ((unsigned) src_y < height) {
                    index = src_x + src_y * stride;
                    dst[y * stride + x] =
                        ((src[index]                        * (s - frac_x) +
                          src[index + 1]          * frac_x) * (s - frac_y) +
                         (src[index + stride]               * (s - frac_x) +
                          src[index + stride + 1] * frac_x) *      frac_y  +
                         r) >> (shift * 2);
                } else {
                    index = src_x + av_clip(src_y, 0, height) * stride;
                    dst[y * stride + x] =
                        ((src[index]               * (s - frac_x) +
                          src[index + 1] * frac_x) *  s           +
                         r) >> (shift * 2);
                }
            } else {
                if ((unsigned) src_y < height) {
                    index = av_clip(src_x, 0, width) + src_y * stride;
                    dst[y * stride + x] =
                        ((src[index]                    * (s - frac_y) +
                          src[index + stride] * frac_y) *  s           +
                         r) >> (shift * 2);
                } else {
                    index = av_clip(src_x, 0, width) +
                            av_clip(src_y, 0, height) * stride;
                    dst[y * stride + x] = src[index];
                }
            }

            vx += dxx;
            vy += dyx;
        }
        ox += dxy;
        oy += dyy;
    }
}

av_cold void ff_mpegvideodsp_init(MpegVideoDSPContext *c)
{
    c->gmc1 = gmc1_c;
    c->gmc  = ff_gmc_c;

    if (ARCH_PPC)
        ff_mpegvideodsp_init_ppc(c);
    if (ARCH_X86)
        ff_mpegvideodsp_init_x86(c);
}
