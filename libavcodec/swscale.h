/*
 * copyright (C) 2006 Luca Abeni
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef SWSCALE_EMU_H
#define SWSCALE_EMU_H

#ifdef __cplusplus
extern "C" {
#endif

/* Dummy, only useful for compilation! */
#define SWS_FAST_BILINEAR 1
#define SWS_BILINEAR 2
#define SWS_BICUBIC  4
#define SWS_X        8
#define SWS_POINT    0x10
#define SWS_AREA     0x20
#define SWS_BICUBLIN 0x40
#define SWS_GAUSS    0x80
#define SWS_SINC     0x100
#define SWS_LANCZOS  0x200
#define SWS_SPLINE   0x400

#define SwsFilter void
struct SwsContext {
    struct ImgReSampleContext *resampling_ctx;
    enum PixelFormat src_pix_fmt, dst_pix_fmt;
};

struct SwsContext *sws_getContext(int srcW, int srcH, int srcFormat,
                                  int dstW, int dstH, int dstFormat,
                                  int flags, SwsFilter *srcFilter,
                                  SwsFilter *dstFilter, double *param);

int sws_scale(struct SwsContext *ctx, uint8_t* src[], int srcStride[],
              int srcSliceY, int srcSliceH, uint8_t* dst[], int dstStride[]);

void sws_freeContext(struct SwsContext *swsContext);

#ifdef __cplusplus
}
#endif

#endif /* SWSCALE_EMU_H */
