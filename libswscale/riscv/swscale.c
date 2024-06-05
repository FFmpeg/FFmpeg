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
#include "libavutil/riscv/cpu.h"
#include "libswscale/swscale_internal.h"

void ff_bgr24ToY_rvv(uint8_t *dst, const uint8_t *src, const uint8_t *,
                     const uint8_t *, int width, uint32_t *coeffs, void *);
void ff_bgr24ToUV_rvv(uint8_t *, uint8_t *, const uint8_t *, const uint8_t *,
                      const uint8_t *, int width, uint32_t *coeffs, void *);
void ff_bgr24ToUV_half_rvv(uint8_t *, uint8_t *, const uint8_t *,
                           const uint8_t *, const uint8_t *, int width,
                           uint32_t *coeffs, void *);
void ff_rgb24ToY_rvv(uint8_t *dst, const uint8_t *src, const uint8_t *,
                     const uint8_t *, int width, uint32_t *coeffs, void *);
void ff_rgb24ToUV_rvv(uint8_t *, uint8_t *, const uint8_t *, const uint8_t *,
                      const uint8_t *, int width, uint32_t *coeffs, void *);
void ff_rgb24ToUV_half_rvv(uint8_t *, uint8_t *, const uint8_t *,
                           const uint8_t *, const uint8_t *, int width,
                           uint32_t *coeffs, void *);

av_cold void ff_sws_init_swscale_riscv(SwsContext *c)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_I32) && (flags & AV_CPU_FLAG_RVB_ADDR)) {
        switch (c->srcFormat) {
            case AV_PIX_FMT_BGR24:
                c->lumToYV12 = ff_bgr24ToY_rvv;
                if (c->chrSrcHSubSample)
                    c->chrToYV12 = ff_bgr24ToUV_half_rvv;
                else
                    c->chrToYV12 = ff_bgr24ToUV_rvv;
                break;

            case AV_PIX_FMT_RGB24:
                c->lumToYV12 = ff_rgb24ToY_rvv;
                if (c->chrSrcHSubSample)
                    c->chrToYV12 = ff_rgb24ToUV_half_rvv;
                else
                    c->chrToYV12 = ff_rgb24ToUV_rvv;
                break;
        }
    }
#endif
}
