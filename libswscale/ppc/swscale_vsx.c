/*
 * AltiVec-enhanced yuv2yuvX
 *
 * Copyright (C) 2004 Romain Dolbeau <romain@dolbeau.org>
 * based on the equivalent C code in swscale.c
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

#include <inttypes.h>

#include "config.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "yuv2rgb_altivec.h"
#include "libavutil/ppc/util_altivec.h"

#if HAVE_VSX
#define vzero vec_splat_s32(0)

#if !HAVE_BIGENDIAN
#define  GET_LS(a,b,c,s) {\
        ls  = a;\
        a = vec_vsx_ld(((b) << 1)  + 16, s);\
    }

#define yuv2planeX_8(d1, d2, l1, src, x, perm, filter) do {\
        vector signed short ls;\
        GET_LS(l1, x, perm, src);\
        vector signed int   i1  = vec_mule(filter, ls);\
        vector signed int   i2  = vec_mulo(filter, ls);\
        vector signed int   vf1, vf2;\
        vf1 = vec_mergeh(i1, i2);\
        vf2 = vec_mergel(i1, i2);\
        d1 = vec_add(d1, vf1);\
        d2 = vec_add(d2, vf2);\
    } while (0)

#define LOAD_FILTER(vf,f) {\
        vf = vec_vsx_ld(joffset, f);\
}
#define LOAD_L1(ll1,s,p){\
        ll1  = vec_vsx_ld(xoffset, s);\
}

// The 3 above is 2 (filterSize == 4) + 1 (sizeof(short) == 2).

// The neat trick: We only care for half the elements,
// high or low depending on (i<<3)%16 (it's 0 or 8 here),
// and we're going to use vec_mule, so we choose
// carefully how to "unpack" the elements into the even slots.
#define GET_VF4(a, vf, f) {\
    vf = (vector signed short)vec_vsx_ld(a << 3, f);\
    vf = vec_mergeh(vf, (vector signed short)vzero);\
}
#define FIRST_LOAD(sv, pos, s, per) {}
#define UPDATE_PTR(s0, d0, s1, d1) {}
#define LOAD_SRCV(pos, a, s, per, v0, v1, vf) {\
    vf = vec_vsx_ld(pos + a, s);\
}
#define LOAD_SRCV8(pos, a, s, per, v0, v1, vf) LOAD_SRCV(pos, a, s, per, v0, v1, vf)
#define GET_VFD(a, b, f, vf0, vf1, per, vf, off) {\
    vf  = vec_vsx_ld((a * 2 * filterSize) + (b * 2) + off, f);\
}

#define FUNC(name) name ## _vsx
#include "swscale_ppc_template.c"
#undef FUNC

#endif /* !HAVE_BIGENDIAN */

static void yuv2plane1_8_u(const int16_t *src, uint8_t *dest, int dstW,
                           const uint8_t *dither, int offset, int start)
{
    int i;
    for (i = start; i < dstW; i++) {
        int val = (src[i] + dither[(i + offset) & 7]) >> 7;
        dest[i] = av_clip_uint8(val);
    }
}

static void yuv2plane1_8_vsx(const int16_t *src, uint8_t *dest, int dstW,
                           const uint8_t *dither, int offset)
{
    const int dst_u = -(uintptr_t)dest & 15;
    int i, j;
    LOCAL_ALIGNED(16, int16_t, val, [16]);
    const vector uint16_t shifts = (vector uint16_t) {7, 7, 7, 7, 7, 7, 7, 7};
    vector int16_t vi, vileft, ditherleft, ditherright;
    vector uint8_t vd;

    for (j = 0; j < 16; j++) {
        val[j] = dither[(dst_u + offset + j) & 7];
    }

    ditherleft = vec_ld(0, val);
    ditherright = vec_ld(0, &val[8]);

    yuv2plane1_8_u(src, dest, dst_u, dither, offset, 0);

    for (i = dst_u; i < dstW - 15; i += 16) {

        vi = vec_vsx_ld(0, &src[i]);
        vi = vec_adds(ditherleft, vi);
        vileft = vec_sra(vi, shifts);

        vi = vec_vsx_ld(0, &src[i + 8]);
        vi = vec_adds(ditherright, vi);
        vi = vec_sra(vi, shifts);

        vd = vec_packsu(vileft, vi);
        vec_st(vd, 0, &dest[i]);
    }

    yuv2plane1_8_u(src, dest, dstW, dither, offset, i);
}

#endif /* HAVE_VSX */

av_cold void ff_sws_init_swscale_vsx(SwsContext *c)
{
#if HAVE_VSX
    enum AVPixelFormat dstFormat = c->dstFormat;

    if (!(av_get_cpu_flags() & AV_CPU_FLAG_VSX))
        return;

#if !HAVE_BIGENDIAN
    if (c->srcBpc == 8 && c->dstBpc <= 14) {
        c->hyScale = c->hcScale = hScale_real_vsx;
    }
    if (!is16BPS(dstFormat) && !isNBPS(dstFormat) &&
        dstFormat != AV_PIX_FMT_NV12 && dstFormat != AV_PIX_FMT_NV21 &&
        dstFormat != AV_PIX_FMT_GRAYF32BE && dstFormat != AV_PIX_FMT_GRAYF32LE &&
        !c->needAlpha) {
        c->yuv2planeX = yuv2planeX_vsx;
    }
#endif

    if (!(c->flags & (SWS_BITEXACT | SWS_FULL_CHR_H_INT)) && !c->needAlpha) {
        switch (c->dstBpc) {
        case 8:
            c->yuv2plane1 = yuv2plane1_8_vsx;
            break;
        }
    }
#endif /* HAVE_VSX */
}
