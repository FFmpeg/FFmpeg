/*
 * Copyright (C) 2026 Loongson Technology Co. Ltd.
 * Contributed by Bo Jin(jinbo@loongson.cn)
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

#include "swscale_loongarch.h"
#include "libavutil/loongarch/loongson_intrinsics.h"

/* Each vector iteration scales 8 destination pixels. Their source
 * position offsets ((xpos & 0xFFFF) + j*xInc) >> 16, j in [0, 7], must
 * stay within the 32-byte gather window, which holds while
 * xInc <= (1 << 18); otherwise fall back to the scalar loop. */
#define LSX_HSCALE_FAST_MAX_XINC (1 << 18)

void ff_hyscale_fast_lsx(SwsInternal *c, int16_t *dst, int dstWidth,
                         const uint8_t *src, int srcW, int xInc)
{
    int i = 0;
    unsigned int xpos = 0;

    if (xInc <= LSX_HSCALE_FAST_MAX_XINC) {
        static const int32_t idx32[4]  = {0, 1, 2, 3};
        static const int16_t idx16[8]  = {0, 1, 2, 3, 4, 5, 6, 7};
        static const uint8_t shuf8[16] = {
            0, 4, 8, 12, 16, 20, 24, 28,
            0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30
        };

        /* [0, xInc, 2*xInc, 3*xInc] as 32-bit lanes */
        __m128i vadd_w = __lsx_vmul_w(__lsx_vreplgr2vr_w(xInc),
                                      __lsx_vld(idx32, 0));
        /* [0, xInc, ..., 7*xInc] modulo 2^16 as 16-bit lanes */
        __m128i vadd16 = __lsx_vmul_h(__lsx_vreplgr2vr_h(xInc),
                                      __lsx_vld(idx16, 0));
        __m128i vx4    = __lsx_vreplgr2vr_w(4 * xInc);
        __m128i vshuf8 = __lsx_vld(shuf8, 0);
        __m128i v128   = __lsx_vreplgr2vr_h(128);

        for (; i + 8 <= dstWidth && (xpos >> 16) + 32 < srcW; i += 8, xpos += 8 * xInc) {
            unsigned int lo = xpos & 0xFFFF;
            unsigned int xx = xpos >> 16;

            /* full 32-bit positions of the 8 pixels */
            __m128i vc0 = __lsx_vadd_w(__lsx_vreplgr2vr_w(lo), vadd_w);
            __m128i vc1 = __lsx_vadd_w(vc0, vx4);

            /* source offsets (j = 0..7), each in [0, 28] */
            __m128i vperm = __lsx_vshuf_b(__lsx_vsrli_w(vc1, 16),
                                          __lsx_vsrli_w(vc0, 16), vshuf8);

            /* xalpha = (xpos & 0xFFFF) >> 9, 16-bit lanes */
            __m128i valpha = __lsx_vsrli_h(__lsx_vadd_h(__lsx_vreplgr2vr_h(lo),
                                                        vadd16), 9);

            __m128i v0 = __lsx_vsllwil_hu_bu(__lsx_vshuf_b(__lsx_vld(src + xx + 16, 0),
                                                           __lsx_vld(src + xx, 0),
                                                           vperm), 0);
            __m128i v1 = __lsx_vsllwil_hu_bu(__lsx_vshuf_b(__lsx_vld(src + xx + 17, 0),
                                                           __lsx_vld(src + xx + 1, 0),
                                                           vperm), 0);

            __m128i w0 = __lsx_vsub_h(v128, valpha);
            __lsx_vst(__lsx_vadd_h(__lsx_vmul_h(v0, w0), __lsx_vmul_h(v1, valpha)),
                      dst + i, 0);
        }
    }

    for (; i < dstWidth; i++) {
        unsigned int xx     = xpos >> 16;
        unsigned int xalpha = (xpos & 0xFFFF) >> 9;
        dst[i] = (src[xx] << 7) + (src[xx + 1] - src[xx]) * xalpha;
        xpos  += xInc;
    }
    for (i = dstWidth - 1; (i * (int64_t)xInc) >> 16 >= srcW - 1; i--)
        dst[i] = src[srcW - 1] * 128;
}

void ff_hcscale_fast_lsx(SwsInternal *c, int16_t *dst1, int16_t *dst2,
                         int dstWidth, const uint8_t *src1,
                         const uint8_t *src2, int srcW, int xInc)
{
    int i = 0;
    unsigned int xpos = 0;

    if (xInc <= LSX_HSCALE_FAST_MAX_XINC) {
        static const int32_t idx32[4]  = {0, 1, 2, 3};
        static const int16_t idx16[8]  = {0, 1, 2, 3, 4, 5, 6, 7};
        static const uint8_t shuf8[16] = {
            0, 4, 8, 12, 16, 20, 24, 28,
            0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30
        };

        __m128i vadd_w = __lsx_vmul_w(__lsx_vreplgr2vr_w(xInc),
                                      __lsx_vld(idx32, 0));
        __m128i vadd16 = __lsx_vmul_h(__lsx_vreplgr2vr_h(xInc),
                                      __lsx_vld(idx16, 0));
        __m128i vx4    = __lsx_vreplgr2vr_w(4 * xInc);
        __m128i vshuf8 = __lsx_vld(shuf8, 0);
        __m128i v127   = __lsx_vreplgr2vr_h(127);

        for (; i + 8 <= dstWidth && (xpos >> 16) + 32 < srcW; i += 8, xpos += 8 * xInc) {
            unsigned int lo = xpos & 0xFFFF;
            unsigned int xx = xpos >> 16;

            __m128i vc0 = __lsx_vadd_w(__lsx_vreplgr2vr_w(lo), vadd_w);
            __m128i vc1 = __lsx_vadd_w(vc0, vx4);

            __m128i vperm = __lsx_vshuf_b(__lsx_vsrli_w(vc1, 16),
                                          __lsx_vsrli_w(vc0, 16), vshuf8);

            __m128i valpha = __lsx_vsrli_h(__lsx_vadd_h(__lsx_vreplgr2vr_h(lo),
                                                        vadd16), 9);

            __m128i v10 = __lsx_vsllwil_hu_bu(__lsx_vshuf_b(__lsx_vld(src1 + xx + 16, 0),
                                                            __lsx_vld(src1 + xx, 0),
                                                            vperm), 0);
            __m128i v11 = __lsx_vsllwil_hu_bu(__lsx_vshuf_b(__lsx_vld(src1 + xx + 17, 0),
                                                            __lsx_vld(src1 + xx + 1, 0),
                                                            vperm), 0);
            __m128i v20 = __lsx_vsllwil_hu_bu(__lsx_vshuf_b(__lsx_vld(src2 + xx + 16, 0),
                                                            __lsx_vld(src2 + xx, 0),
                                                            vperm), 0);
            __m128i v21 = __lsx_vsllwil_hu_bu(__lsx_vshuf_b(__lsx_vld(src2 + xx + 17, 0),
                                                            __lsx_vld(src2 + xx + 1, 0),
                                                            vperm), 0);

            __m128i w0 = __lsx_vsub_h(v127, valpha);
            __lsx_vst(__lsx_vadd_h(__lsx_vmul_h(v10, w0), __lsx_vmul_h(v11, valpha)),
                      dst1 + i, 0);
            __lsx_vst(__lsx_vadd_h(__lsx_vmul_h(v20, w0), __lsx_vmul_h(v21, valpha)),
                      dst2 + i, 0);
        }
    }

    for (; i < dstWidth; i++) {
        unsigned int xx     = xpos >> 16;
        unsigned int xalpha = (xpos & 0xFFFF) >> 9;
        dst1[i] = (src1[xx] * (xalpha ^ 127) + src1[xx + 1] * xalpha);
        dst2[i] = (src2[xx] * (xalpha ^ 127) + src2[xx + 1] * xalpha);
        xpos   += xInc;
    }
    for (i = dstWidth - 1; (i * (int64_t)xInc) >> 16 >= srcW - 1; i--) {
        dst1[i] = src1[srcW - 1] * 128;
        dst2[i] = src2[srcW - 1] * 128;
    }
}
