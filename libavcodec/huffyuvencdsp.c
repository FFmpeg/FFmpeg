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
#include "libavutil/intreadwrite.h"
#include "huffyuvencdsp.h"
#include "mathops.h"

#if HAVE_FAST_64BIT
#define BITS 64
typedef uint64_t uint_native;
#else
#define BITS 32
typedef uint32_t uint_native;
#endif
#define RN  AV_JOIN(AV_RN, BITS)
#define RNA AV_JOIN(AV_JOIN(AV_RN, BITS), A)
#define WNA AV_JOIN(AV_JOIN(AV_WN, BITS), A)

// 0x7f7f7f7f or 0x7f7f7f7f7f7f7f7f or whatever, depending on the cpu's native arithmetic size
#define pb_7f (~(uint_native)0 / 255 * 0x7f)
#define pb_80 (~(uint_native)0 / 255 * 0x80)

// 0x00010001 or 0x0001000100010001 or whatever, depending on the cpu's native arithmetic size
#define pw_1 ((uint_native)-1 / UINT16_MAX)

static void diff_int16_c(uint16_t *dst, const uint16_t *src1, const uint16_t *src2, unsigned mask, int w){
    long i;
#if !HAVE_FAST_UNALIGNED
    if ((uintptr_t)src2 & (sizeof(uint_native) - 1)) {
        for(i=0; i+3<w; i+=4){
            dst[i+0] = (src1[i+0]-src2[i+0]) & mask;
            dst[i+1] = (src1[i+1]-src2[i+1]) & mask;
            dst[i+2] = (src1[i+2]-src2[i+2]) & mask;
            dst[i+3] = (src1[i+3]-src2[i+3]) & mask;
        }
    }else
#endif
    {
        uint_native pw_lsb = (mask >> 1) * pw_1;
        uint_native pw_msb = pw_lsb +  pw_1;

        for (i = 0; i <= w - (int)sizeof(uint_native)/2; i += sizeof(uint_native)/2) {
            uint_native a = RNA(src1 + i);
            uint_native b = RN (src2 + i);
            WNA(dst + i, ((a | pw_msb) - (b & pw_lsb)) ^ ((a^b^pw_msb) & pw_msb));
        }
    }
    for (; i<w; i++)
        dst[i] = (src1[i] - src2[i]) & mask;
}

static void sub_hfyu_median_pred_int16_c(uint16_t *dst, const uint16_t *src1, const uint16_t *src2, unsigned mask, int w, int *left, int *left_top){
    int i;
    uint16_t l, lt;

    l  = *left;
    lt = *left_top;

    for(i=0; i<w; i++){
        const int pred = mid_pred(l, src1[i], (l + src1[i] - lt) & mask);
        lt = src1[i];
        l  = src2[i];
        dst[i] = (l - pred) & mask;
    }

    *left     = l;
    *left_top = lt;
}

av_cold void ff_huffyuvencdsp_init(HuffYUVEncDSPContext *c, enum AVPixelFormat pix_fmt)
{
    c->diff_int16           = diff_int16_c;
    c->sub_hfyu_median_pred_int16 = sub_hfyu_median_pred_int16_c;

#if ARCH_X86
    ff_huffyuvencdsp_init_x86(c, pix_fmt);
#endif
}
