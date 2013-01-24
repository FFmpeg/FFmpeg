/*
 * SPARC VIS optimized inverse DCT
 * Copyright (c) 2007 Denes Balatoni < dbalatoni XatX interware XdotX hu >
 *
 * I did consult the following fine web page about dct
 * http://www.geocities.com/ssavekar/dct.htm
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

#include <stdint.h>

#include "libavcodec/dsputil.h"
#include "dsputil_vis.h"
#include "libavutil/mem.h"

static const DECLARE_ALIGNED(8, int16_t, coeffs)[28] = {
    - 1259,- 1259,- 1259,- 1259,
    - 4989,- 4989,- 4989,- 4989,
    -11045,-11045,-11045,-11045,
    -19195,-19195,-19195,-19195,
    -29126,-29126,-29126,-29126,
     25080, 25080, 25080, 25080,
     12785, 12785, 12785, 12785
};
static const DECLARE_ALIGNED(8, uint16_t, scale)[4] = {
    65536>>6, 65536>>6, 65536>>6, 65536>>6
};
static const DECLARE_ALIGNED(8, uint16_t, rounder)[4] = {
    1<<5, 1<<5, 1<<5, 1<<5
};
static const DECLARE_ALIGNED(8, uint16_t, expand)[4] = {
    1<<14, 1<<14, 1<<14, 1<<14
};

#define INIT_IDCT \
        "ldd [%1], %%f32         \n\t"\
        "ldd [%1+8], %%f34       \n\t"\
        "ldd [%1+16], %%f36      \n\t"\
        "ldd [%1+24], %%f38      \n\t"\
        "ldd [%1+32], %%f40      \n\t"\
        "ldd [%1+40], %%f42      \n\t"\
        "ldd [%1+48], %%f44      \n\t"\
        "ldd [%0], %%f46         \n\t"\
        "fzero %%f62             \n\t"\

#define LOADSCALE(in) \
        "ldd [" in "], %%f0          \n\t"\
        "ldd [" in "+16], %%f2       \n\t"\
        "ldd [" in "+32], %%f4       \n\t"\
        "ldd [" in "+48], %%f6       \n\t"\
        "ldd [" in "+64], %%f8       \n\t"\
        "ldd [" in "+80], %%f10      \n\t"\
        "ldd [" in "+96], %%f12      \n\t"\
        "ldd [" in "+112], %%f14     \n\t"\
        "fpadd16 %%f0, %%f0, %%f0    \n\t"\
        "fpadd16 %%f2, %%f2, %%f2    \n\t"\
        "fpadd16 %%f4, %%f4, %%f4    \n\t"\
        "fpadd16 %%f6, %%f6, %%f6    \n\t"\
        "fpadd16 %%f8, %%f8, %%f8    \n\t"\
        "fpadd16 %%f10, %%f10, %%f10 \n\t"\
        "fpadd16 %%f12, %%f12, %%f12 \n\t"\
        "fpadd16 %%f14, %%f14, %%f14 \n\t"\
\
        "fpadd16 %%f0, %%f0, %%f0    \n\t"\
        "fpadd16 %%f2, %%f2, %%f2    \n\t"\
        "fpadd16 %%f4, %%f4, %%f4    \n\t"\
        "fpadd16 %%f6, %%f6, %%f6    \n\t"\
        "fpadd16 %%f8, %%f8, %%f8    \n\t"\
        "fpadd16 %%f10, %%f10, %%f10 \n\t"\
        "fpadd16 %%f12, %%f12, %%f12 \n\t"\
        "fpadd16 %%f14, %%f14, %%f14 \n\t"\
\
        "fpadd16 %%f0, %%f0, %%f0    \n\t"\
        "fpadd16 %%f2, %%f2, %%f2    \n\t"\
        "fpadd16 %%f4, %%f4, %%f4    \n\t"\
        "fpadd16 %%f6, %%f6, %%f6    \n\t"\
        "fpadd16 %%f8, %%f8, %%f8    \n\t"\
        "fpadd16 %%f10, %%f10, %%f10 \n\t"\
        "fpadd16 %%f12, %%f12, %%f12 \n\t"\
        "fpadd16 %%f14, %%f14, %%f14 \n\t"\
\
        "fpadd16 %%f0, %%f0, %%f0    \n\t"\
        "fpadd16 %%f2, %%f2, %%f2    \n\t"\
        "fpadd16 %%f4, %%f4, %%f4    \n\t"\
        "fpadd16 %%f6, %%f6, %%f6    \n\t"\
        "fpadd16 %%f8, %%f8, %%f8    \n\t"\
        "fpadd16 %%f10, %%f10, %%f10 \n\t"\
        "fpadd16 %%f12, %%f12, %%f12 \n\t"\
        "fpadd16 %%f14, %%f14, %%f14 \n\t"\

#define LOAD(in) \
        "ldd [" in "], %%f16         \n\t"\
        "ldd [" in "+8], %%f18       \n\t"\
        "ldd [" in "+16], %%f20      \n\t"\
        "ldd [" in "+24], %%f22      \n\t"\
        "ldd [" in "+32], %%f24      \n\t"\
        "ldd [" in "+40], %%f26      \n\t"\
        "ldd [" in "+48], %%f28      \n\t"\
        "ldd [" in "+56], %%f30      \n\t"\

#define TRANSPOSE \
        "fpmerge %%f16, %%f24, %%f0  \n\t"\
        "fpmerge %%f20, %%f28, %%f2  \n\t"\
        "fpmerge %%f17, %%f25, %%f4  \n\t"\
        "fpmerge %%f21, %%f29, %%f6  \n\t"\
        "fpmerge %%f18, %%f26, %%f8  \n\t"\
        "fpmerge %%f22, %%f30, %%f10 \n\t"\
        "fpmerge %%f19, %%f27, %%f12 \n\t"\
        "fpmerge %%f23, %%f31, %%f14 \n\t"\
\
        "fpmerge %%f0, %%f2, %%f16   \n\t"\
        "fpmerge %%f1, %%f3, %%f18   \n\t"\
        "fpmerge %%f4, %%f6, %%f20   \n\t"\
        "fpmerge %%f5, %%f7, %%f22   \n\t"\
        "fpmerge %%f8, %%f10, %%f24  \n\t"\
        "fpmerge %%f9, %%f11, %%f26  \n\t"\
        "fpmerge %%f12, %%f14, %%f28 \n\t"\
        "fpmerge %%f13, %%f15, %%f30 \n\t"\
\
        "fpmerge %%f16, %%f17, %%f0  \n\t"\
        "fpmerge %%f18, %%f19, %%f2  \n\t"\
        "fpmerge %%f20, %%f21, %%f4  \n\t"\
        "fpmerge %%f22, %%f23, %%f6  \n\t"\
        "fpmerge %%f24, %%f25, %%f8  \n\t"\
        "fpmerge %%f26, %%f27, %%f10 \n\t"\
        "fpmerge %%f28, %%f29, %%f12 \n\t"\
        "fpmerge %%f30, %%f31, %%f14 \n\t"\

#define IDCT4ROWS \
    /* 1. column */\
        "fmul8ulx16 %%f0, %%f38, %%f28 \n\t"\
        "for %%f4, %%f6, %%f60         \n\t"\
        "fmul8ulx16 %%f2, %%f32, %%f18 \n\t"\
        "fmul8ulx16 %%f2, %%f36, %%f22 \n\t"\
        "fmul8ulx16 %%f2, %%f40, %%f26 \n\t"\
        "fmul8ulx16 %%f2, %%f44, %%f30 \n\t"\
\
        ADDROUNDER\
\
        "fmul8sux16 %%f0, %%f38, %%f48 \n\t"\
        "fcmpd %%fcc0, %%f62, %%f60    \n\t"\
        "for %%f8, %%f10, %%f60        \n\t"\
        "fmul8sux16 %%f2, %%f32, %%f50 \n\t"\
        "fmul8sux16 %%f2, %%f36, %%f52 \n\t"\
        "fmul8sux16 %%f2, %%f40, %%f54 \n\t"\
        "fmul8sux16 %%f2, %%f44, %%f56 \n\t"\
\
        "fpadd16 %%f48, %%f28, %%f28 \n\t"\
        "fcmpd %%fcc1, %%f62, %%f60  \n\t"\
        "for %%f12, %%f14, %%f60     \n\t"\
        "fpadd16 %%f50, %%f18, %%f18 \n\t"\
        "fpadd16 %%f52, %%f22, %%f22 \n\t"\
        "fpadd16 %%f54, %%f26, %%f26 \n\t"\
        "fpadd16 %%f56, %%f30, %%f30 \n\t"\
\
        "fpadd16 %%f28, %%f0, %%f16  \n\t"\
        "fcmpd %%fcc2, %%f62, %%f60  \n\t"\
        "fpadd16 %%f28, %%f0, %%f20  \n\t"\
        "fpadd16 %%f28, %%f0, %%f24  \n\t"\
        "fpadd16 %%f28, %%f0, %%f28  \n\t"\
        "fpadd16 %%f18, %%f2, %%f18  \n\t"\
        "fpadd16 %%f22, %%f2, %%f22  \n\t"\
    /* 2. column */\
        "fbe %%fcc0, 3f                \n\t"\
        "fpadd16 %%f26, %%f2, %%f26    \n\t"\
        "fmul8ulx16 %%f4, %%f34, %%f48 \n\t"\
        "fmul8ulx16 %%f4, %%f42, %%f50 \n\t"\
        "fmul8ulx16 %%f6, %%f36, %%f52 \n\t"\
        "fmul8ulx16 %%f6, %%f44, %%f54 \n\t"\
        "fmul8ulx16 %%f6, %%f32, %%f56 \n\t"\
        "fmul8ulx16 %%f6, %%f40, %%f58 \n\t"\
\
        "fpadd16 %%f16, %%f48, %%f16 \n\t"\
        "fpadd16 %%f20, %%f50, %%f20 \n\t"\
        "fpsub16 %%f24, %%f50, %%f24 \n\t"\
        "fpsub16 %%f28, %%f48, %%f28 \n\t"\
        "fpadd16 %%f18, %%f52, %%f18 \n\t"\
        "fpsub16 %%f22, %%f54, %%f22 \n\t"\
        "fpsub16 %%f26, %%f56, %%f26 \n\t"\
        "fpsub16 %%f30, %%f58, %%f30 \n\t"\
\
        "fmul8sux16 %%f4, %%f34, %%f48 \n\t"\
        "fmul8sux16 %%f4, %%f42, %%f50 \n\t"\
        "fmul8sux16 %%f6, %%f36, %%f52 \n\t"\
        "fmul8sux16 %%f6, %%f44, %%f54 \n\t"\
        "fmul8sux16 %%f6, %%f32, %%f56 \n\t"\
        "fmul8sux16 %%f6, %%f40, %%f58 \n\t"\
\
        "fpadd16 %%f16, %%f48, %%f16 \n\t"\
        "fpadd16 %%f20, %%f50, %%f20 \n\t"\
        "fpsub16 %%f24, %%f50, %%f24 \n\t"\
        "fpsub16 %%f28, %%f48, %%f28 \n\t"\
        "fpadd16 %%f18, %%f52, %%f18 \n\t"\
        "fpsub16 %%f22, %%f54, %%f22 \n\t"\
        "fpsub16 %%f26, %%f56, %%f26 \n\t"\
        "fpsub16 %%f30, %%f58, %%f30 \n\t"\
\
        "fpadd16 %%f16, %%f4, %%f16  \n\t"\
        "fpsub16 %%f28, %%f4, %%f28  \n\t"\
        "fpadd16 %%f18, %%f6, %%f18  \n\t"\
        "fpsub16 %%f26, %%f6, %%f26  \n\t"\
    /* 3. column */\
        "3:                             \n\t"\
        "fbe %%fcc1, 4f                 \n\t"\
        "fpsub16 %%f30, %%f6, %%f30     \n\t"\
        "fmul8ulx16 %%f8, %%f38, %%f48  \n\t"\
        "fmul8ulx16 %%f10, %%f40, %%f50 \n\t"\
        "fmul8ulx16 %%f10, %%f32, %%f52 \n\t"\
        "fmul8ulx16 %%f10, %%f44, %%f54 \n\t"\
        "fmul8ulx16 %%f10, %%f36, %%f56 \n\t"\
\
        "fpadd16 %%f16, %%f48, %%f16 \n\t"\
        "fpsub16 %%f20, %%f48, %%f20 \n\t"\
        "fpsub16 %%f24, %%f48, %%f24 \n\t"\
        "fpadd16 %%f28, %%f48, %%f28 \n\t"\
        "fpadd16 %%f18, %%f50, %%f18 \n\t"\
        "fpsub16 %%f22, %%f52, %%f22 \n\t"\
        "fpadd16 %%f26, %%f54, %%f26 \n\t"\
        "fpadd16 %%f30, %%f56, %%f30 \n\t"\
\
        "fmul8sux16 %%f8, %%f38, %%f48 \n\t"\
        "fmul8sux16 %%f10, %%f40, %%f50 \n\t"\
        "fmul8sux16 %%f10, %%f32, %%f52 \n\t"\
        "fmul8sux16 %%f10, %%f44, %%f54 \n\t"\
        "fmul8sux16 %%f10, %%f36, %%f56 \n\t"\
\
        "fpadd16 %%f16, %%f48, %%f16 \n\t"\
        "fpsub16 %%f20, %%f48, %%f20 \n\t"\
        "fpsub16 %%f24, %%f48, %%f24 \n\t"\
        "fpadd16 %%f28, %%f48, %%f28 \n\t"\
        "fpadd16 %%f18, %%f50, %%f18 \n\t"\
        "fpsub16 %%f22, %%f52, %%f22 \n\t"\
        "fpadd16 %%f26, %%f54, %%f26 \n\t"\
        "fpadd16 %%f30, %%f56, %%f30 \n\t"\
\
        "fpadd16 %%f16, %%f8, %%f16  \n\t"\
        "fpsub16 %%f20, %%f8, %%f20  \n\t"\
        "fpsub16 %%f24, %%f8, %%f24  \n\t"\
        "fpadd16 %%f28, %%f8, %%f28  \n\t"\
        "fpadd16 %%f18, %%f10, %%f18 \n\t"\
        "fpsub16 %%f22, %%f10, %%f22 \n\t"\
    /* 4. column */\
        "4:                             \n\t"\
        "fbe %%fcc2, 5f                 \n\t"\
        "fpadd16 %%f30, %%f10, %%f30    \n\t"\
        "fmul8ulx16 %%f12, %%f42, %%f48 \n\t"\
        "fmul8ulx16 %%f12, %%f34, %%f50 \n\t"\
        "fmul8ulx16 %%f14, %%f44, %%f52 \n\t"\
        "fmul8ulx16 %%f14, %%f40, %%f54 \n\t"\
        "fmul8ulx16 %%f14, %%f36, %%f56 \n\t"\
        "fmul8ulx16 %%f14, %%f32, %%f58 \n\t"\
\
        "fpadd16 %%f16, %%f48, %%f16 \n\t"\
        "fpsub16 %%f20, %%f50, %%f20 \n\t"\
        "fpadd16 %%f24, %%f50, %%f24 \n\t"\
        "fpsub16 %%f28, %%f48, %%f28 \n\t"\
        "fpadd16 %%f18, %%f52, %%f18 \n\t"\
        "fpsub16 %%f22, %%f54, %%f22 \n\t"\
        "fpadd16 %%f26, %%f56, %%f26 \n\t"\
        "fpsub16 %%f30, %%f58, %%f30 \n\t"\
\
        "fmul8sux16 %%f12, %%f42, %%f48 \n\t"\
        "fmul8sux16 %%f12, %%f34, %%f50 \n\t"\
        "fmul8sux16 %%f14, %%f44, %%f52 \n\t"\
        "fmul8sux16 %%f14, %%f40, %%f54 \n\t"\
        "fmul8sux16 %%f14, %%f36, %%f56 \n\t"\
        "fmul8sux16 %%f14, %%f32, %%f58 \n\t"\
\
        "fpadd16 %%f16, %%f48, %%f16 \n\t"\
        "fpsub16 %%f20, %%f50, %%f20 \n\t"\
        "fpadd16 %%f24, %%f50, %%f24 \n\t"\
        "fpsub16 %%f28, %%f48, %%f28 \n\t"\
        "fpadd16 %%f18, %%f52, %%f18 \n\t"\
        "fpsub16 %%f22, %%f54, %%f22 \n\t"\
        "fpadd16 %%f26, %%f56, %%f26 \n\t"\
        "fpsub16 %%f30, %%f58, %%f30 \n\t"\
\
        "fpsub16 %%f20, %%f12, %%f20 \n\t"\
        "fpadd16 %%f24, %%f12, %%f24 \n\t"\
        "fpsub16 %%f22, %%f14, %%f22 \n\t"\
        "fpadd16 %%f26, %%f14, %%f26 \n\t"\
        "fpsub16 %%f30, %%f14, %%f30 \n\t"\
    /* final butterfly */\
        "5:                          \n\t"\
        "fpsub16 %%f16, %%f18, %%f48 \n\t"\
        "fpsub16 %%f20, %%f22, %%f50 \n\t"\
        "fpsub16 %%f24, %%f26, %%f52 \n\t"\
        "fpsub16 %%f28, %%f30, %%f54 \n\t"\
        "fpadd16 %%f16, %%f18, %%f16 \n\t"\
        "fpadd16 %%f20, %%f22, %%f20 \n\t"\
        "fpadd16 %%f24, %%f26, %%f24 \n\t"\
        "fpadd16 %%f28, %%f30, %%f28 \n\t"\

#define STOREROWS(out) \
        "std %%f48, [" out "+112]          \n\t"\
        "std %%f50, [" out "+96]           \n\t"\
        "std %%f52, [" out "+80]           \n\t"\
        "std %%f54, [" out "+64]           \n\t"\
        "std %%f16, [" out "]              \n\t"\
        "std %%f20, [" out "+16]           \n\t"\
        "std %%f24, [" out "+32]           \n\t"\
        "std %%f28, [" out "+48]           \n\t"\

#define SCALEROWS \
        "fmul8sux16 %%f46, %%f48, %%f48 \n\t"\
        "fmul8sux16 %%f46, %%f50, %%f50 \n\t"\
        "fmul8sux16 %%f46, %%f52, %%f52 \n\t"\
        "fmul8sux16 %%f46, %%f54, %%f54 \n\t"\
        "fmul8sux16 %%f46, %%f16, %%f16 \n\t"\
        "fmul8sux16 %%f46, %%f20, %%f20 \n\t"\
        "fmul8sux16 %%f46, %%f24, %%f24 \n\t"\
        "fmul8sux16 %%f46, %%f28, %%f28 \n\t"\

#define PUTPIXELSCLAMPED(dest) \
        "fpack16 %%f48, %%f14 \n\t"\
        "fpack16 %%f50, %%f12 \n\t"\
        "fpack16 %%f16, %%f0  \n\t"\
        "fpack16 %%f20, %%f2  \n\t"\
        "fpack16 %%f24, %%f4  \n\t"\
        "fpack16 %%f28, %%f6  \n\t"\
        "fpack16 %%f54, %%f8  \n\t"\
        "fpack16 %%f52, %%f10 \n\t"\
        "st %%f0, [%3+" dest "]   \n\t"\
        "st %%f2, [%5+" dest "]   \n\t"\
        "st %%f4, [%6+" dest "]   \n\t"\
        "st %%f6, [%7+" dest "]   \n\t"\
        "st %%f8, [%8+" dest "]   \n\t"\
        "st %%f10, [%9+" dest "]  \n\t"\
        "st %%f12, [%10+" dest "] \n\t"\
        "st %%f14, [%11+" dest "] \n\t"\

#define ADDPIXELSCLAMPED(dest) \
        "ldd [%5], %%f18         \n\t"\
        "ld [%3+" dest"], %%f0   \n\t"\
        "ld [%6+" dest"], %%f2   \n\t"\
        "ld [%7+" dest"], %%f4   \n\t"\
        "ld [%8+" dest"], %%f6   \n\t"\
        "ld [%9+" dest"], %%f8   \n\t"\
        "ld [%10+" dest"], %%f10 \n\t"\
        "ld [%11+" dest"], %%f12 \n\t"\
        "ld [%12+" dest"], %%f14 \n\t"\
        "fmul8x16 %%f0, %%f18, %%f0   \n\t"\
        "fmul8x16 %%f2, %%f18, %%f2   \n\t"\
        "fmul8x16 %%f4, %%f18, %%f4   \n\t"\
        "fmul8x16 %%f6, %%f18, %%f6   \n\t"\
        "fmul8x16 %%f8, %%f18, %%f8   \n\t"\
        "fmul8x16 %%f10, %%f18, %%f10 \n\t"\
        "fmul8x16 %%f12, %%f18, %%f12 \n\t"\
        "fmul8x16 %%f14, %%f18, %%f14 \n\t"\
        "fpadd16 %%f0, %%f16, %%f0    \n\t"\
        "fpadd16 %%f2, %%f20, %%f2    \n\t"\
        "fpadd16 %%f4, %%f24, %%f4    \n\t"\
        "fpadd16 %%f6, %%f28, %%f6    \n\t"\
        "fpadd16 %%f8, %%f54, %%f8    \n\t"\
        "fpadd16 %%f10, %%f52, %%f10  \n\t"\
        "fpadd16 %%f12, %%f50, %%f12  \n\t"\
        "fpadd16 %%f14, %%f48, %%f14  \n\t"\
        "fpack16 %%f0, %%f0   \n\t"\
        "fpack16 %%f2, %%f2   \n\t"\
        "fpack16 %%f4, %%f4   \n\t"\
        "fpack16 %%f6, %%f6   \n\t"\
        "fpack16 %%f8, %%f8   \n\t"\
        "fpack16 %%f10, %%f10 \n\t"\
        "fpack16 %%f12, %%f12 \n\t"\
        "fpack16 %%f14, %%f14 \n\t"\
        "st %%f0, [%3+" dest "]   \n\t"\
        "st %%f2, [%6+" dest "]   \n\t"\
        "st %%f4, [%7+" dest "]   \n\t"\
        "st %%f6, [%8+" dest "]   \n\t"\
        "st %%f8, [%9+" dest "]   \n\t"\
        "st %%f10, [%10+" dest "] \n\t"\
        "st %%f12, [%11+" dest "] \n\t"\
        "st %%f14, [%12+" dest "] \n\t"\


void ff_simple_idct_vis(int16_t *data) {
    int out1, out2, out3, out4;
    DECLARE_ALIGNED(8, int16_t, temp)[8*8];

    __asm__ volatile(
        INIT_IDCT

#define ADDROUNDER

        // shift right 16-4=12
        LOADSCALE("%2+8")
        IDCT4ROWS
        STOREROWS("%3+8")
        LOADSCALE("%2+0")
        IDCT4ROWS
        "std %%f48, [%3+112] \n\t"
        "std %%f50, [%3+96]  \n\t"
        "std %%f52, [%3+80]  \n\t"
        "std %%f54, [%3+64]  \n\t"

        // shift right 16+4
        "ldd [%3+8], %%f18  \n\t"
        "ldd [%3+24], %%f22 \n\t"
        "ldd [%3+40], %%f26 \n\t"
        "ldd [%3+56], %%f30 \n\t"
        TRANSPOSE
        IDCT4ROWS
        SCALEROWS
        STOREROWS("%2+0")
        LOAD("%3+64")
        TRANSPOSE
        IDCT4ROWS
        SCALEROWS
        STOREROWS("%2+8")

        : "=r" (out1), "=r" (out2), "=r" (out3), "=r" (out4)
        : "0" (scale), "1" (coeffs), "2" (data), "3" (temp)
    );
}

void ff_simple_idct_put_vis(uint8_t *dest, int line_size, int16_t *data) {
    int out1, out2, out3, out4, out5;
    int r1, r2, r3, r4, r5, r6, r7;

    __asm__ volatile(
        "wr %%g0, 0x8, %%gsr \n\t"

        INIT_IDCT

        "add %3, %4, %5   \n\t"
        "add %5, %4, %6   \n\t"
        "add %6, %4, %7   \n\t"
        "add %7, %4, %8   \n\t"
        "add %8, %4, %9   \n\t"
        "add %9, %4, %10  \n\t"
        "add %10, %4, %11 \n\t"

        // shift right 16-4=12
        LOADSCALE("%2+8")
        IDCT4ROWS
        STOREROWS("%2+8")
        LOADSCALE("%2+0")
        IDCT4ROWS
        "std %%f48, [%2+112] \n\t"
        "std %%f50, [%2+96]  \n\t"
        "std %%f52, [%2+80]  \n\t"
        "std %%f54, [%2+64]  \n\t"

#undef ADDROUNDER
#define ADDROUNDER "fpadd16 %%f28, %%f46, %%f28 \n\t"

        // shift right 16+4
        "ldd [%2+8], %%f18  \n\t"
        "ldd [%2+24], %%f22 \n\t"
        "ldd [%2+40], %%f26 \n\t"
        "ldd [%2+56], %%f30 \n\t"
        TRANSPOSE
        IDCT4ROWS
        PUTPIXELSCLAMPED("0")
        LOAD("%2+64")
        TRANSPOSE
        IDCT4ROWS
        PUTPIXELSCLAMPED("4")

        : "=r" (out1), "=r" (out2), "=r" (out3), "=r" (out4), "=r" (out5),
          "=r" (r1), "=r" (r2), "=r" (r3), "=r" (r4), "=r" (r5), "=r" (r6), "=r" (r7)
        : "0" (rounder), "1" (coeffs), "2" (data), "3" (dest), "4" (line_size)
    );
}

void ff_simple_idct_add_vis(uint8_t *dest, int line_size, int16_t *data) {
    int out1, out2, out3, out4, out5, out6;
    int r1, r2, r3, r4, r5, r6, r7;

    __asm__ volatile(
        "wr %%g0, 0x8, %%gsr \n\t"

        INIT_IDCT

        "add %3, %4, %6   \n\t"
        "add %6, %4, %7   \n\t"
        "add %7, %4, %8   \n\t"
        "add %8, %4, %9   \n\t"
        "add %9, %4, %10  \n\t"
        "add %10, %4, %11 \n\t"
        "add %11, %4, %12 \n\t"

#undef ADDROUNDER
#define ADDROUNDER

        // shift right 16-4=12
        LOADSCALE("%2+8")
        IDCT4ROWS
        STOREROWS("%2+8")
        LOADSCALE("%2+0")
        IDCT4ROWS
        "std %%f48, [%2+112] \n\t"
        "std %%f50, [%2+96]  \n\t"
        "std %%f52, [%2+80]  \n\t"
        "std %%f54, [%2+64]  \n\t"

#undef ADDROUNDER
#define ADDROUNDER "fpadd16 %%f28, %%f46, %%f28 \n\t"

        // shift right 16+4
        "ldd [%2+8], %%f18  \n\t"
        "ldd [%2+24], %%f22 \n\t"
        "ldd [%2+40], %%f26 \n\t"
        "ldd [%2+56], %%f30 \n\t"
        TRANSPOSE
        IDCT4ROWS
        ADDPIXELSCLAMPED("0")
        LOAD("%2+64")
        TRANSPOSE
        IDCT4ROWS
        ADDPIXELSCLAMPED("4")

        : "=r" (out1), "=r" (out2), "=r" (out3), "=r" (out4), "=r" (out5), "=r" (out6),
          "=r" (r1), "=r" (r2), "=r" (r3), "=r" (r4), "=r" (r5), "=r" (r6), "=r" (r7)
        : "0" (rounder), "1" (coeffs), "2" (data), "3" (dest), "4" (line_size), "5" (expand)
    );
}
