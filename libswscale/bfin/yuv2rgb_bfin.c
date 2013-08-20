/*
 * Copyright (C) 2007 Marc Hoffman <marc.hoffman@analog.com>
 *
 * Blackfin video color space converter operations
 * convert I420 YV12 to RGB in various formats
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libswscale/swscale_internal.h"

#if defined(__FDPIC__) && CONFIG_SRAM
#define L1CODE __attribute__((l1_text))
#else
#define L1CODE
#endif

void ff_bfin_yuv2rgb555_line(const uint8_t *Y, const uint8_t *U,
                             const uint8_t *V, uint8_t *out,
                             int w, uint32_t *coeffs) L1CODE;

void ff_bfin_yuv2rgb565_line(const uint8_t *Y, const uint8_t *U,
                             const uint8_t *V, uint8_t *out,
                             int w, uint32_t *coeffs) L1CODE;

void ff_bfin_yuv2rgb24_line(const uint8_t *Y, const uint8_t *U,
                            const uint8_t *V, uint8_t *out,
                            int w, uint32_t *coeffs) L1CODE;

typedef void (*ltransform)(const uint8_t *Y, const uint8_t *U, const uint8_t *V,
                           uint8_t *out, int w, uint32_t *coeffs);

static void bfin_prepare_coefficients(SwsContext *c, int rgb, int masks)
{
    int oy;
    oy = c->yOffset & 0xffff;
    oy = oy >> 3;      // keep everything U8.0 for offset calculation

    c->oc = 128 * 0x01010101U;
    c->oy = oy * 0x01010101U;

    /* copy 64bit vector coeffs down to 32bit vector coeffs */
    c->cy   = c->yCoeff;
    c->zero = 0;

    if (rgb) {
        c->crv = c->vrCoeff;
        c->cbu = c->ubCoeff;
        c->cgu = c->ugCoeff;
        c->cgv = c->vgCoeff;
    } else {
        c->crv = c->ubCoeff;
        c->cbu = c->vrCoeff;
        c->cgu = c->vgCoeff;
        c->cgv = c->ugCoeff;
    }

    if (masks == 555) {
        c->rmask = 0x001f * 0x00010001U;
        c->gmask = 0x03e0 * 0x00010001U;
        c->bmask = 0x7c00 * 0x00010001U;
    } else if (masks == 565) {
        c->rmask = 0x001f * 0x00010001U;
        c->gmask = 0x07e0 * 0x00010001U;
        c->bmask = 0xf800 * 0x00010001U;
    }
}

static int core_yuv420_rgb(SwsContext *c, const uint8_t **in, int *instrides,
                           int srcSliceY, int srcSliceH, uint8_t **oplanes,
                           int *outstrides, ltransform lcscf,
                           int rgb, int masks)
{
    const uint8_t *py, *pu, *pv;
    uint8_t *op;
    int w  = instrides[0];
    int h2 = srcSliceH >> 1;
    int i;

    bfin_prepare_coefficients(c, rgb, masks);

    py = in[0];
    pu = in[1 + (1 ^ rgb)];
    pv = in[1 + (0 ^ rgb)];

    op = oplanes[0] + srcSliceY * outstrides[0];

    for (i = 0; i < h2; i++) {
        lcscf(py, pu, pv, op, w, &c->oy);

        py += instrides[0];
        op += outstrides[0];

        lcscf(py, pu, pv, op, w, &c->oy);

        py += instrides[0];
        pu += instrides[1];
        pv += instrides[2];
        op += outstrides[0];
    }

    return srcSliceH;
}

static int bfin_yuv420_rgb555(SwsContext *c, const uint8_t **in, int *instrides,
                              int srcSliceY, int srcSliceH,
                              uint8_t **oplanes, int *outstrides)
{
    return core_yuv420_rgb(c, in, instrides, srcSliceY, srcSliceH, oplanes,
                           outstrides, ff_bfin_yuv2rgb555_line, 1, 555);
}

static int bfin_yuv420_bgr555(SwsContext *c, const uint8_t **in, int *instrides,
                              int srcSliceY, int srcSliceH,
                              uint8_t **oplanes, int *outstrides)
{
    return core_yuv420_rgb(c, in, instrides, srcSliceY, srcSliceH, oplanes,
                           outstrides, ff_bfin_yuv2rgb555_line, 0, 555);
}

static int bfin_yuv420_rgb24(SwsContext *c, const uint8_t **in, int *instrides,
                             int srcSliceY, int srcSliceH,
                             uint8_t **oplanes, int *outstrides)
{
    return core_yuv420_rgb(c, in, instrides, srcSliceY, srcSliceH, oplanes,
                           outstrides, ff_bfin_yuv2rgb24_line, 1, 888);
}

static int bfin_yuv420_bgr24(SwsContext *c, const uint8_t **in, int *instrides,
                             int srcSliceY, int srcSliceH,
                             uint8_t **oplanes, int *outstrides)
{
    return core_yuv420_rgb(c, in, instrides, srcSliceY, srcSliceH, oplanes,
                           outstrides, ff_bfin_yuv2rgb24_line, 0, 888);
}

static int bfin_yuv420_rgb565(SwsContext *c, const uint8_t **in, int *instrides,
                              int srcSliceY, int srcSliceH,
                              uint8_t **oplanes, int *outstrides)
{
    return core_yuv420_rgb(c, in, instrides, srcSliceY, srcSliceH, oplanes,
                           outstrides, ff_bfin_yuv2rgb565_line, 1, 565);
}

static int bfin_yuv420_bgr565(SwsContext *c, const uint8_t **in, int *instrides,
                              int srcSliceY, int srcSliceH,
                              uint8_t **oplanes, int *outstrides)
{
    return core_yuv420_rgb(c, in, instrides, srcSliceY, srcSliceH, oplanes,
                           outstrides, ff_bfin_yuv2rgb565_line, 0, 565);
}

av_cold SwsFunc ff_yuv2rgb_init_bfin(SwsContext *c)
{
    SwsFunc f;

    switch (c->dstFormat) {
    case AV_PIX_FMT_RGB555:
        f = bfin_yuv420_rgb555;
        break;
    case AV_PIX_FMT_BGR555:
        f = bfin_yuv420_bgr555;
        break;
    case AV_PIX_FMT_RGB565:
        f = bfin_yuv420_rgb565;
        break;
    case AV_PIX_FMT_BGR565:
        f = bfin_yuv420_bgr565;
        break;
    case AV_PIX_FMT_RGB24:
        f = bfin_yuv420_rgb24;
        break;
    case AV_PIX_FMT_BGR24:
        f = bfin_yuv420_bgr24;
        break;
    default:
        return 0;
    }

    av_log(c, AV_LOG_INFO, "BlackFin accelerated color space converter %s\n",
           sws_format_name(c->dstFormat));

    return f;
}
