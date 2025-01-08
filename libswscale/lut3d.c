/*
 * Copyright (C) 2024 Niklas Haas
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

#include <assert.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"

#include "cms.h"
#include "csputils.h"
#include "lut3d.h"

SwsLut3D *ff_sws_lut3d_alloc(void)
{
    SwsLut3D *lut3d = av_malloc(sizeof(*lut3d));
    if (!lut3d)
        return NULL;

    lut3d->dynamic = false;
    return lut3d;
}

void ff_sws_lut3d_free(SwsLut3D **plut3d)
{
    av_freep(plut3d);
}

bool ff_sws_lut3d_test_fmt(enum AVPixelFormat fmt, int output)
{
    return fmt == AV_PIX_FMT_RGBA64;
}

enum AVPixelFormat ff_sws_lut3d_pick_pixfmt(SwsFormat fmt, int output)
{
    return AV_PIX_FMT_RGBA64;
}

/**
 * v0 and v1 are 'black' and 'white'
 * v2 and v3 are closest RGB/CMY vertices
 * x >= y >= z are relative weights
 */
static av_always_inline
v3u16_t barycentric(int shift, int x, int y, int z,
                    v3u16_t v0, v3u16_t v1, v3u16_t v2, v3u16_t v3)
{
    const int a = (1 << shift) - x;
    const int b = x - y;
    const int c = y - z;
    const int d = z;
    av_assert2(x >= y);
    av_assert2(y >= z);

    return (v3u16_t) {
        (a * v0.x + b * v1.x + c * v2.x + d * v3.x) >> shift,
        (a * v0.y + b * v1.y + c * v2.y + d * v3.y) >> shift,
        (a * v0.z + b * v1.z + c * v2.z + d * v3.z) >> shift,
    };
}

static av_always_inline
v3u16_t tetrahedral(const SwsLut3D *lut3d, int Rx, int Gx, int Bx,
                    int Rf, int Gf, int Bf)
{
    const int shift = 16 - INPUT_LUT_BITS;
    const int Rn = FFMIN(Rx + 1, INPUT_LUT_SIZE - 1);
    const int Gn = FFMIN(Gx + 1, INPUT_LUT_SIZE - 1);
    const int Bn = FFMIN(Bx + 1, INPUT_LUT_SIZE - 1);

    const v3u16_t c000 = lut3d->input[Bx][Gx][Rx];
    const v3u16_t c111 = lut3d->input[Bn][Gn][Rn];
    if (Rf > Gf) {
        if (Gf > Bf) {
            const v3u16_t c100 = lut3d->input[Bx][Gx][Rn];
            const v3u16_t c110 = lut3d->input[Bx][Gn][Rn];
            return barycentric(shift, Rf, Gf, Bf, c000, c100, c110, c111);
        } else if (Rf > Bf) {
            const v3u16_t c100 = lut3d->input[Bx][Gx][Rn];
            const v3u16_t c101 = lut3d->input[Bn][Gx][Rn];
            return barycentric(shift, Rf, Bf, Gf, c000, c100, c101, c111);
        } else {
            const v3u16_t c001 = lut3d->input[Bn][Gx][Rx];
            const v3u16_t c101 = lut3d->input[Bn][Gx][Rn];
            return barycentric(shift, Bf, Rf, Gf, c000, c001, c101, c111);
        }
    } else {
        if (Bf > Gf) {
            const v3u16_t c001 = lut3d->input[Bn][Gx][Rx];
            const v3u16_t c011 = lut3d->input[Bn][Gn][Rx];
            return barycentric(shift, Bf, Gf, Rf, c000, c001, c011, c111);
        } else if (Bf > Rf) {
            const v3u16_t c010 = lut3d->input[Bx][Gn][Rx];
            const v3u16_t c011 = lut3d->input[Bn][Gn][Rx];
            return barycentric(shift, Gf, Bf, Rf, c000, c010, c011, c111);
        } else {
            const v3u16_t c010 = lut3d->input[Bx][Gn][Rx];
            const v3u16_t c110 = lut3d->input[Bx][Gn][Rn];
            return barycentric(shift, Gf, Rf, Bf, c000, c010, c110, c111);
        }
    }
}

static av_always_inline v3u16_t lookup_input16(const SwsLut3D *lut3d, v3u16_t rgb)
{
    const int shift = 16 - INPUT_LUT_BITS;
    const int Rx = rgb.x >> shift;
    const int Gx = rgb.y >> shift;
    const int Bx = rgb.z >> shift;
    const int Rf = rgb.x & ((1 << shift) - 1);
    const int Gf = rgb.y & ((1 << shift) - 1);
    const int Bf = rgb.z & ((1 << shift) - 1);
    return tetrahedral(lut3d, Rx, Gx, Bx, Rf, Gf, Bf);
}

static av_always_inline v3u16_t lookup_input8(const SwsLut3D *lut3d, v3u8_t rgb)
{
    static_assert(INPUT_LUT_BITS <= 8, "INPUT_LUT_BITS must be <= 8");
    const int shift = 8 - INPUT_LUT_BITS;
    const int Rx = rgb.x >> shift;
    const int Gx = rgb.y >> shift;
    const int Bx = rgb.z >> shift;
    const int Rf = rgb.x & ((1 << shift) - 1);
    const int Gf = rgb.y & ((1 << shift) - 1);
    const int Bf = rgb.z & ((1 << shift) - 1);
    return tetrahedral(lut3d, Rx, Gx, Bx, Rf, Gf, Bf);
}

/**
 * Note: These functions are scaled such that x == (1 << shift) corresponds to
 * a value of 1.0. This makes them suitable for use when interpolation LUT
 * entries with a fractional part that is just masked away from the index,
 * since a fractional coordinate of e.g. 0xFFFF corresponds to a mix weight of
 * just slightly *less* than 1.0.
 */
static av_always_inline v2u16_t lerp2u16(v2u16_t a, v2u16_t b, int x, int shift)
{
    const int xi = (1 << shift) - x;
    return (v2u16_t) {
        (a.x * xi + b.x * x) >> shift,
        (a.y * xi + b.y * x) >> shift,
    };
}

static av_always_inline v3u16_t lerp3u16(v3u16_t a, v3u16_t b, int x, int shift)
{
    const int xi = (1 << shift) - x;
    return (v3u16_t) {
        (a.x * xi + b.x * x) >> shift,
        (a.y * xi + b.y * x) >> shift,
        (a.z * xi + b.z * x) >> shift,
    };
}

static av_always_inline v3u16_t lookup_output(const SwsLut3D *lut3d, v3u16_t ipt)
{
    const int Ishift = 16 - OUTPUT_LUT_BITS_I;
    const int Cshift = 16 - OUTPUT_LUT_BITS_PT;
    const int Ix = ipt.x >> Ishift;
    const int Px = ipt.y >> Cshift;
    const int Tx = ipt.z >> Cshift;
    const int If = ipt.x & ((1 << Ishift) - 1);
    const int Pf = ipt.y & ((1 << Cshift) - 1);
    const int Tf = ipt.z & ((1 << Cshift) - 1);
    const int In = FFMIN(Ix + 1, OUTPUT_LUT_SIZE_I  - 1);
    const int Pn = FFMIN(Px + 1, OUTPUT_LUT_SIZE_PT - 1);
    const int Tn = FFMIN(Tx + 1, OUTPUT_LUT_SIZE_PT - 1);

    /* Trilinear interpolation */
    const v3u16_t c000 = lut3d->output[Tx][Px][Ix];
    const v3u16_t c001 = lut3d->output[Tx][Px][In];
    const v3u16_t c010 = lut3d->output[Tx][Pn][Ix];
    const v3u16_t c011 = lut3d->output[Tx][Pn][In];
    const v3u16_t c100 = lut3d->output[Tn][Px][Ix];
    const v3u16_t c101 = lut3d->output[Tn][Px][In];
    const v3u16_t c110 = lut3d->output[Tn][Pn][Ix];
    const v3u16_t c111 = lut3d->output[Tn][Pn][In];
    const v3u16_t c00  = lerp3u16(c000, c100, Tf, Cshift);
    const v3u16_t c10  = lerp3u16(c010, c110, Tf, Cshift);
    const v3u16_t c01  = lerp3u16(c001, c101, Tf, Cshift);
    const v3u16_t c11  = lerp3u16(c011, c111, Tf, Cshift);
    const v3u16_t c0   = lerp3u16(c00,  c10,  Pf, Cshift);
    const v3u16_t c1   = lerp3u16(c01,  c11,  Pf, Cshift);
    const v3u16_t c    = lerp3u16(c0,   c1,   If, Ishift);
    return c;
}

static av_always_inline v3u16_t apply_tone_map(const SwsLut3D *lut3d, v3u16_t ipt)
{
    const int shift = 16 - TONE_LUT_BITS;
    const int Ix = ipt.x >> shift;
    const int If = ipt.x & ((1 << shift) - 1);
    const int In = FFMIN(Ix + 1, TONE_LUT_SIZE - 1);

    const v2u16_t w0 = lut3d->tone_map[Ix];
    const v2u16_t w1 = lut3d->tone_map[In];
    const v2u16_t w  = lerp2u16(w0, w1, If, shift);
    const int base   = (1 << 15) - w.y;

    ipt.x = w.x;
    ipt.y = base + (ipt.y * w.y >> 15);
    ipt.z = base + (ipt.z * w.y >> 15);
    return ipt;
}

int ff_sws_lut3d_generate(SwsLut3D *lut3d, enum AVPixelFormat fmt_in,
                          enum AVPixelFormat fmt_out, const SwsColorMap *map)
{
    int ret;

    if (!ff_sws_lut3d_test_fmt(fmt_in, 0) || !ff_sws_lut3d_test_fmt(fmt_out, 1))
        return AVERROR(EINVAL);

    lut3d->dynamic = map->src.frame_peak.num > 0;
    lut3d->map = *map;

    if (lut3d->dynamic) {
        ret = ff_sws_color_map_generate_dynamic(&lut3d->input[0][0][0],
                                             &lut3d->output[0][0][0],
                                             INPUT_LUT_SIZE, OUTPUT_LUT_SIZE_I,
                                             OUTPUT_LUT_SIZE_PT, map);
        if (ret < 0)
            return ret;

        /* Make sure initial state is valid */
        ff_sws_lut3d_update(lut3d, &map->src);
        return 0;
    } else {
        return ff_sws_color_map_generate_static(&lut3d->input[0][0][0],
                                             INPUT_LUT_SIZE, map);
    }
}

void ff_sws_lut3d_update(SwsLut3D *lut3d, const SwsColor *new_src)
{
    if (!new_src || !lut3d->dynamic)
        return;

    lut3d->map.src.frame_peak = new_src->frame_peak;
    lut3d->map.src.frame_avg  = new_src->frame_avg;

    ff_sws_tone_map_generate(lut3d->tone_map, TONE_LUT_SIZE, &lut3d->map);
}

void ff_sws_lut3d_apply(const SwsLut3D *lut3d, const uint8_t *in, int in_stride,
                        uint8_t *out, int out_stride, int w, int h)
{
    while (h--) {
        const uint16_t *in16 = (const uint16_t *) in;
        uint16_t *out16 = (uint16_t *) out;

        for (int x = 0; x < w; x++) {
            v3u16_t c = { in16[0], in16[1], in16[2] };
            c = lookup_input16(lut3d, c);

            if (lut3d->dynamic) {
                c = apply_tone_map(lut3d, c);
                c = lookup_output(lut3d, c);
            }

            out16[0] = c.x;
            out16[1] = c.y;
            out16[2] = c.z;
            out16[3] = in16[3];
            in16  += 4;
            out16 += 4;
        }

        in  += in_stride;
        out += out_stride;
    }
}
