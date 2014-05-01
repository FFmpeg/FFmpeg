/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2014 Janne Grunau <janne-libav@jannau.net>
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

#include "config.h"
#include "libavutil/aarch64/asm.S"

function ff_conv_flt_to_s16_neon, export=1
        subs            x2,  x2,  #8
        ld1             {v0.4s}, [x1],  #16
        fcvtzs          v4.4s,  v0.4s,  #31
        ld1             {v1.4s}, [x1],  #16
        fcvtzs          v5.4s,  v1.4s,  #31
        b.eq            3f
        ands            x12, x2,  #~15
        b.eq            2f
1:      subs            x12, x12, #16
        sqrshrn         v4.4h,  v4.4s,  #16
        ld1             {v2.4s}, [x1],  #16
        fcvtzs          v6.4s,  v2.4s,  #31
        sqrshrn2        v4.8h,  v5.4s,  #16
        ld1             {v3.4s}, [x1],  #16
        fcvtzs          v7.4s,  v3.4s,  #31
        sqrshrn         v6.4h,  v6.4s,  #16
        st1             {v4.8h}, [x0],  #16
        sqrshrn2        v6.8h,  v7.4s,  #16
        ld1             {v0.4s}, [x1],  #16
        fcvtzs          v4.4s,  v0.4s,  #31
        ld1             {v1.4s}, [x1],  #16
        fcvtzs          v5.4s,  v1.4s,  #31
        st1             {v6.8h}, [x0],  #16
        b.ne            1b
        ands            x2,  x2,  #15
        b.eq            3f
2:      ld1             {v2.4s}, [x1],  #16
        sqrshrn         v4.4h,  v4.4s,  #16
        fcvtzs          v6.4s,  v2.4s,  #31
        ld1             {v3.4s}, [x1],  #16
        sqrshrn2        v4.8h,  v5.4s,  #16
        fcvtzs          v7.4s,  v3.4s,  #31
        sqrshrn         v6.4h,  v6.4s,  #16
        st1             {v4.8h}, [x0],  #16
        sqrshrn2        v6.8h,  v7.4s,  #16
        st1             {v6.8h}, [x0]
        ret
3:      sqrshrn         v4.4h,  v4.4s,  #16
        sqrshrn2        v4.8h,  v5.4s,  #16
        st1             {v4.8h}, [x0]
        ret
endfunc

function ff_conv_fltp_to_s16_2ch_neon, export=1
        ldp             x4,  x5,  [x1]
        subs            x2,  x2,  #8
        ld1             {v0.4s},  [x4], #16
        fcvtzs          v4.4s,  v0.4s,  #31
        ld1             {v1.4s},  [x4], #16
        fcvtzs          v5.4s,  v1.4s,  #31
        ld1             {v2.4s},  [x5], #16
        fcvtzs          v6.4s,  v2.4s,  #31
        ld1             {v3.4s},  [x5], #16
        fcvtzs          v7.4s,  v3.4s,  #31
        b.eq            3f
        ands            x12, x2,  #~15
        b.eq            2f
1:      subs            x12, x12, #16
        ld1             {v16.4s}, [x4], #16
        fcvtzs          v20.4s, v16.4s, #31
        sri             v6.4s,  v4.4s,  #16
        ld1             {v17.4s}, [x4], #16
        fcvtzs          v21.4s, v17.4s, #31
        ld1             {v18.4s}, [x5], #16
        fcvtzs          v22.4s, v18.4s, #31
        ld1             {v19.4s}, [x5], #16
        sri             v7.4s,  v5.4s,  #16
        st1             {v6.4s},  [x0], #16
        fcvtzs          v23.4s, v19.4s, #31
        st1             {v7.4s},  [x0], #16
        sri             v22.4s, v20.4s, #16
        ld1             {v0.4s},  [x4], #16
        sri             v23.4s, v21.4s, #16
        st1             {v22.4s}, [x0], #16
        fcvtzs          v4.4s,  v0.4s,  #31
        ld1             {v1.4s},  [x4], #16
        fcvtzs          v5.4s,  v1.4s,  #31
        ld1             {v2.4s},  [x5], #16
        fcvtzs          v6.4s,  v2.4s,  #31
        ld1             {v3.4s},  [x5], #16
        fcvtzs          v7.4s,  v3.4s,  #31
        st1             {v23.4s}, [x0], #16
        b.ne            1b
        ands            x2,  x2,  #15
        b.eq            3f
2:      sri             v6.4s,  v4.4s,  #16
        ld1             {v0.4s},  [x4], #16
        fcvtzs          v0.4s,  v0.4s,  #31
        ld1             {v1.4s},  [x4], #16
        fcvtzs          v1.4s,  v1.4s,  #31
        ld1             {v2.4s},  [x5], #16
        fcvtzs          v2.4s,  v2.4s,  #31
        sri             v7.4s,  v5.4s,  #16
        ld1             {v3.4s},  [x5], #16
        fcvtzs          v3.4s,  v3.4s,  #31
        sri             v2.4s,  v0.4s,  #16
        st1             {v6.4s,v7.4s},  [x0], #32
        sri             v3.4s,  v1.4s,  #16
        st1             {v2.4s,v3.4s},  [x0], #32
        ret
3:      sri             v6.4s,  v4.4s,  #16
        sri             v7.4s,  v5.4s,  #16
        st1             {v6.4s,v7.4s},  [x0]
        ret
endfunc

function ff_conv_fltp_to_s16_neon, export=1
        cmp             w3,  #2
        b.eq            X(ff_conv_fltp_to_s16_2ch_neon)
        b.gt            1f
        ldr             x1,  [x1]
        b               X(ff_conv_flt_to_s16_neon)
1:
        cmp             w3,  #4
        lsl             x12, x3,  #1
        b.lt            4f

5:      // 4 channels
        ldp             x4, x5, [x1], #16
        ldp             x6, x7, [x1], #16
        mov             w9,  w2
        mov             x8,  x0
        ld1             {v4.4s},        [x4], #16
        fcvtzs          v4.4s,  v4.4s,  #31
        ld1             {v5.4s},        [x5], #16
        fcvtzs          v5.4s,  v5.4s,  #31
        ld1             {v6.4s},        [x6], #16
        fcvtzs          v6.4s, v6.4s, #31
        ld1             {v7.4s},        [x7], #16
        fcvtzs          v7.4s, v7.4s, #31
6:
        subs            w9,  w9,  #8
        ld1             {v0.4s},        [x4], #16
        fcvtzs          v0.4s,  v0.4s,  #31
        sri             v5.4s,  v4.4s,  #16
        ld1             {v1.4s},        [x5], #16
        fcvtzs          v1.4s,  v1.4s,  #31
        sri             v7.4s,  v6.4s,  #16
        ld1             {v2.4s},        [x6], #16
        fcvtzs          v2.4s,  v2.4s,  #31
        zip1            v16.4s, v5.4s,  v7.4s
        ld1             {v3.4s},        [x7], #16
        fcvtzs          v3.4s,  v3.4s,  #31
        zip2            v17.4s, v5.4s,  v7.4s
        st1             {v16.d}[0],     [x8], x12
        sri             v1.4s,  v0.4s,  #16
        st1             {v16.d}[1],     [x8], x12
        sri             v3.4s,  v2.4s,  #16
        st1             {v17.d}[0],     [x8], x12
        zip1            v18.4s, v1.4s,  v3.4s
        st1             {v17.d}[1],     [x8], x12
        zip2            v19.4s, v1.4s,  v3.4s
        b.eq            7f
        ld1             {v4.4s},        [x4], #16
        fcvtzs          v4.4s,  v4.4s,  #31
        st1             {v18.d}[0],     [x8], x12
        ld1             {v5.4s},        [x5], #16
        fcvtzs          v5.4s,  v5.4s,  #31
        st1             {v18.d}[1],     [x8], x12
        ld1             {v6.4s},    [x6], #16
        fcvtzs          v6.4s, v6.4s, #31
        st1             {v19.d}[0],     [x8], x12
        ld1             {v7.4s},    [x7], #16
        fcvtzs          v7.4s, v7.4s, #31
        st1             {v19.d}[1],     [x8], x12
        b               6b
7:
        st1             {v18.d}[0],     [x8], x12
        st1             {v18.d}[1],     [x8], x12
        st1             {v19.d}[0],     [x8], x12
        st1             {v19.d}[1],     [x8], x12
        subs            w3,  w3,  #4
        b.eq            end
        cmp             w3,  #4
        add             x0,  x0,  #8
        b.ge            5b

4:      // 2 channels
        cmp             w3,  #2
        b.lt            4f
        ldp             x4,  x5,  [x1], #16
        mov             w9,  w2
        mov             x8,  x0
        tst             w9,  #8
        ld1             {v4.4s},        [x4], #16
        fcvtzs          v4.4s,  v4.4s,  #31
        ld1             {v5.4s},        [x5], #16
        fcvtzs          v5.4s,  v5.4s,  #31
        ld1             {v6.4s},        [x4], #16
        fcvtzs          v6.4s,  v6.4s,  #31
        ld1             {v7.4s},        [x5], #16
        fcvtzs          v7.4s,  v7.4s,  #31
        b.eq            6f
        subs            w9,  w9,  #8
        b.eq            7f
        sri             v5.4s,  v4.4s,  #16
        ld1             {v4.4s},        [x4], #16
        fcvtzs          v4.4s,  v4.4s,  #31
        st1             {v5.s}[0],      [x8], x12
        sri             v7.4s,  v6.4s,  #16
        st1             {v5.s}[1],      [x8], x12
        ld1             {v6.4s},        [x4], #16
        fcvtzs          v6.4s,  v6.4s, #31
        st1             {v5.s}[2],      [x8], x12
        st1             {v5.s}[3],      [x8], x12
        st1             {v7.s}[0],      [x8], x12
        st1             {v7.s}[1],      [x8], x12
        ld1             {v5.4s},        [x5], #16
        fcvtzs          v5.4s,  v5.4s,  #31
        st1             {v7.s}[2],      [x8], x12
        st1             {v7.s}[3],      [x8], x12
        ld1             {v7.4s},        [x5], #16
        fcvtzs          v7.4s,  v7.4s,  #31
6:
        subs            w9,  w9,  #16
        ld1             {v0.4s},        [x4], #16
        sri             v5.4s,  v4.4s,  #16
        fcvtzs          v0.4s,  v0.4s,  #31
        ld1             {v1.4s},        [x5], #16
        sri             v7.4s,  v6.4s,  #16
        st1             {v5.s}[0],      [x8], x12
        st1             {v5.s}[1],      [x8], x12
        fcvtzs          v1.4s,  v1.4s,  #31
        st1             {v5.s}[2],      [x8], x12
        st1             {v5.s}[3],      [x8], x12
        ld1             {v2.4s},        [x4], #16
        st1             {v7.s}[0],      [x8], x12
        fcvtzs          v2.4s,  v2.4s,  #31
        st1             {v7.s}[1],      [x8], x12
        ld1             {v3.4s},        [x5], #16
        st1             {v7.s}[2],      [x8], x12
        fcvtzs          v3.4s,  v3.4s,  #31
        st1             {v7.s}[3],      [x8], x12
        sri             v1.4s,  v0.4s,  #16
        sri             v3.4s,  v2.4s,  #16
        b.eq            6f
        ld1             {v4.4s},        [x4], #16
        st1             {v1.s}[0],      [x8], x12
        fcvtzs          v4.4s,  v4.4s,  #31
        st1             {v1.s}[1],      [x8], x12
        ld1             {v5.4s},        [x5], #16
        st1             {v1.s}[2],      [x8], x12
        fcvtzs          v5.4s,  v5.4s,  #31
        st1             {v1.s}[3],      [x8], x12
        ld1             {v6.4s},        [x4], #16
        st1             {v3.s}[0],      [x8], x12
        fcvtzs          v6.4s,  v6.4s,  #31
        st1             {v3.s}[1],      [x8], x12
        ld1             {v7.4s},        [x5], #16
        st1             {v3.s}[2],      [x8], x12
        fcvtzs          v7.4s,  v7.4s,  #31
        st1             {v3.s}[3],      [x8], x12
        b.gt            6b
6:
        st1             {v1.s}[0],      [x8], x12
        st1             {v1.s}[1],      [x8], x12
        st1             {v1.s}[2],      [x8], x12
        st1             {v1.s}[3],      [x8], x12
        st1             {v3.s}[0],      [x8], x12
        st1             {v3.s}[1],      [x8], x12
        st1             {v3.s}[2],      [x8], x12
        st1             {v3.s}[3],      [x8], x12
        b               8f
7:
        sri             v5.4s,  v4.4s,  #16
        sri             v7.4s,  v6.4s,  #16
        st1             {v5.s}[0],      [x8], x12
        st1             {v5.s}[1],      [x8], x12
        st1             {v5.s}[2],      [x8], x12
        st1             {v5.s}[3],      [x8], x12
        st1             {v7.s}[0],      [x8], x12
        st1             {v7.s}[1],      [x8], x12
        st1             {v7.s}[2],      [x8], x12
        st1             {v7.s}[3],      [x8], x12
8:
        subs            w3,  w3,  #2
        add             x0,  x0,  #4
        b.eq            end

4:      // 1 channel
        ldr             x4,  [x1]
        tst             w2,  #8
        mov             w9,  w2
        mov             x5,  x0
        ld1             {v0.4s},        [x4], #16
        fcvtzs          v0.4s,  v0.4s,  #31
        ld1             {v1.4s},        [x4], #16
        fcvtzs          v1.4s,  v1.4s,  #31
        b.ne            8f
6:
        subs            w9,  w9,  #16
        ld1             {v2.4s},        [x4], #16
        fcvtzs          v2.4s,  v2.4s,  #31
        ld1             {v3.4s},        [x4], #16
        fcvtzs          v3.4s,  v3.4s,  #31
        st1             {v0.h}[1],      [x5], x12
        st1             {v0.h}[3],      [x5], x12
        st1             {v0.h}[5],      [x5], x12
        st1             {v0.h}[7],      [x5], x12
        st1             {v1.h}[1],      [x5], x12
        st1             {v1.h}[3],      [x5], x12
        st1             {v1.h}[5],      [x5], x12
        st1             {v1.h}[7],      [x5], x12
        b.eq            7f
        ld1             {v0.4s},        [x4], #16
        fcvtzs          v0.4s,  v0.4s,  #31
        ld1             {v1.4s},        [x4], #16
        fcvtzs          v1.4s,  v1.4s,  #31
7:
        st1             {v2.h}[1],      [x5], x12
        st1             {v2.h}[3],      [x5], x12
        st1             {v2.h}[5],      [x5], x12
        st1             {v2.h}[7],      [x5], x12
        st1             {v3.h}[1],      [x5], x12
        st1             {v3.h}[3],      [x5], x12
        st1             {v3.h}[5],      [x5], x12
        st1             {v3.h}[7],      [x5], x12
        b.gt            6b
        ret
8:
        subs            w9,  w9,  #8
        st1             {v0.h}[1],      [x5], x12
        st1             {v0.h}[3],      [x5], x12
        st1             {v0.h}[5],      [x5], x12
        st1             {v0.h}[7],      [x5], x12
        st1             {v1.h}[1],      [x5], x12
        st1             {v1.h}[3],      [x5], x12
        st1             {v1.h}[5],      [x5], x12
        st1             {v1.h}[7],      [x5], x12
        b.eq            end
        ld1             {v0.4s},        [x4], #16
        fcvtzs          v0.4s,  v0.4s,  #31
        ld1             {v1.4s},        [x4], #16
        fcvtzs          v1.4s,  v1.4s,  #31
        b               6b
end:
        ret
endfunc
