/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2013 Janne Grunau <janne-libav@jannau.net>
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

#include "config_components.h"

#include "libavutil/aarch64/asm.S"

/* chroma_mc8(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h, int x, int y) */
.macro  h264_chroma_mc8 type, codec=h264
function ff_\type\()_\codec\()_chroma_mc8_neon, export=1
  .ifc \type,avg
        mov             x8,  x0
  .endif
        prfm            pldl1strm, [x1]
        prfm            pldl1strm, [x1, x2]
  .ifc \codec,rv40
        movrel          x6,  rv40bias
        lsr             w9,  w5,  #1
        lsr             w10, w4,  #1
        lsl             w9,  w9,  #3
        lsl             w10, w10, #1
        add             w9,  w9,  w10
        add             x6,  x6,  w9, uxtw
        ld1r            {v22.8h}, [x6]
  .endif
  .ifc \codec,vc1
        movi            v22.8h,   #28
  .endif
        mul             w7,  w4,  w5
        lsl             w14, w5,  #3
        lsl             w13, w4,  #3
        cmp             w7,  #0
        sub             w6,  w14, w7
        sub             w12, w13, w7
        sub             w4,  w7,  w13
        sub             w4,  w4,  w14
        add             w4,  w4,  #64
        b.eq            2f

        dup             v0.8b,  w4
        dup             v1.8b,  w12
        ld1             {v4.8b, v5.8b}, [x1], x2
        dup             v2.8b,  w6
        dup             v3.8b,  w7
        ext             v5.8b,  v4.8b,  v5.8b,  #1
1:      ld1             {v6.8b, v7.8b}, [x1], x2
        umull           v16.8h, v4.8b,  v0.8b
        umlal           v16.8h, v5.8b,  v1.8b
        ext             v7.8b,  v6.8b,  v7.8b,  #1
        ld1             {v4.8b, v5.8b}, [x1], x2
        umlal           v16.8h, v6.8b,  v2.8b
        prfm            pldl1strm, [x1]
        ext             v5.8b,  v4.8b,  v5.8b,  #1
        umlal           v16.8h, v7.8b,  v3.8b
        umull           v17.8h, v6.8b,  v0.8b
        subs            w3,  w3,  #2
        umlal           v17.8h, v7.8b, v1.8b
        umlal           v17.8h, v4.8b, v2.8b
        umlal           v17.8h, v5.8b, v3.8b
        prfm            pldl1strm, [x1, x2]
  .ifc \codec,h264
        rshrn           v16.8b, v16.8h, #6
        rshrn           v17.8b, v17.8h, #6
  .else
        add             v16.8h, v16.8h, v22.8h
        add             v17.8h, v17.8h, v22.8h
        shrn            v16.8b, v16.8h, #6
        shrn            v17.8b, v17.8h, #6
  .endif
  .ifc \type,avg
        ld1             {v20.8b}, [x8], x2
        ld1             {v21.8b}, [x8], x2
        urhadd          v16.8b, v16.8b, v20.8b
        urhadd          v17.8b, v17.8b, v21.8b
  .endif
        st1             {v16.8b}, [x0], x2
        st1             {v17.8b}, [x0], x2
        b.gt            1b
        ret

2:      adds            w12, w12, w6
        dup             v0.8b, w4
        b.eq            5f
        tst             w6,  w6
        dup             v1.8b, w12
        b.eq            4f

        ld1             {v4.8b}, [x1], x2
3:      ld1             {v6.8b}, [x1], x2
        umull           v16.8h, v4.8b,  v0.8b
        umlal           v16.8h, v6.8b,  v1.8b
        ld1             {v4.8b}, [x1], x2
        umull           v17.8h, v6.8b,  v0.8b
        umlal           v17.8h, v4.8b,  v1.8b
        prfm            pldl1strm, [x1]
  .ifc \codec,h264
        rshrn           v16.8b, v16.8h, #6
        rshrn           v17.8b, v17.8h, #6
  .else
        add             v16.8h, v16.8h, v22.8h
        add             v17.8h, v17.8h, v22.8h
        shrn            v16.8b, v16.8h, #6
        shrn            v17.8b, v17.8h, #6
  .endif
        prfm            pldl1strm, [x1, x2]
  .ifc \type,avg
        ld1             {v20.8b}, [x8], x2
        ld1             {v21.8b}, [x8], x2
        urhadd          v16.8b, v16.8b, v20.8b
        urhadd          v17.8b, v17.8b, v21.8b
  .endif
        subs            w3,  w3,  #2
        st1             {v16.8b}, [x0], x2
        st1             {v17.8b}, [x0], x2
        b.gt            3b
        ret

4:      ld1             {v4.8b, v5.8b}, [x1], x2
        ld1             {v6.8b, v7.8b}, [x1], x2
        ext             v5.8b,  v4.8b,  v5.8b,  #1
        ext             v7.8b,  v6.8b,  v7.8b,  #1
        prfm            pldl1strm, [x1]
        subs            w3,  w3,  #2
        umull           v16.8h, v4.8b, v0.8b
        umlal           v16.8h, v5.8b, v1.8b
        umull           v17.8h, v6.8b, v0.8b
        umlal           v17.8h, v7.8b, v1.8b
        prfm            pldl1strm, [x1, x2]
  .ifc \codec,h264
        rshrn           v16.8b, v16.8h, #6
        rshrn           v17.8b, v17.8h, #6
  .else
        add             v16.8h, v16.8h, v22.8h
        add             v17.8h, v17.8h, v22.8h
        shrn            v16.8b, v16.8h, #6
        shrn            v17.8b, v17.8h, #6
  .endif
  .ifc \type,avg
        ld1             {v20.8b}, [x8], x2
        ld1             {v21.8b}, [x8], x2
        urhadd          v16.8b, v16.8b, v20.8b
        urhadd          v17.8b, v17.8b, v21.8b
  .endif
        st1             {v16.8b}, [x0], x2
        st1             {v17.8b}, [x0], x2
        b.gt            4b
        ret

5:      ld1             {v4.8b}, [x1], x2
        ld1             {v5.8b}, [x1], x2
        prfm            pldl1strm, [x1]
        subs            w3,  w3,  #2
        umull           v16.8h, v4.8b, v0.8b
        umull           v17.8h, v5.8b, v0.8b
        prfm            pldl1strm, [x1, x2]
  .ifc \codec,h264
        rshrn           v16.8b, v16.8h, #6
        rshrn           v17.8b, v17.8h, #6
  .else
        add             v16.8h, v16.8h, v22.8h
        add             v17.8h, v17.8h, v22.8h
        shrn            v16.8b, v16.8h, #6
        shrn            v17.8b, v17.8h, #6
  .endif
  .ifc \type,avg
        ld1             {v20.8b}, [x8], x2
        ld1             {v21.8b}, [x8], x2
        urhadd          v16.8b, v16.8b, v20.8b
        urhadd          v17.8b, v17.8b, v21.8b
  .endif
        st1             {v16.8b}, [x0], x2
        st1             {v17.8b}, [x0], x2
        b.gt            5b
        ret
endfunc
.endm

/* chroma_mc4(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h, int x, int y) */
.macro  h264_chroma_mc4 type, codec=h264
function ff_\type\()_\codec\()_chroma_mc4_neon, export=1
  .ifc \type,avg
        mov             x8,  x0
  .endif
        prfm            pldl1strm, [x1]
        prfm            pldl1strm, [x1, x2]
  .ifc \codec,rv40
        movrel          x6,  rv40bias
        lsr             w9,  w5,  #1
        lsr             w10, w4,  #1
        lsl             w9,  w9,  #3
        lsl             w10, w10, #1
        add             w9,  w9,  w10
        add             x6,  x6,  w9, uxtw
        ld1r            {v22.8h}, [x6]
  .endif
  .ifc \codec,vc1
        movi            v22.8h,   #28
  .endif
        mul             w7,  w4,  w5
        lsl             w14, w5,  #3
        lsl             w13, w4,  #3
        cmp             w7,  #0
        sub             w6,  w14, w7
        sub             w12, w13, w7
        sub             w4,  w7,  w13
        sub             w4,  w4,  w14
        add             w4,  w4,  #64
        b.eq            2f

        dup             v24.8b,  w4
        dup             v25.8b,  w12
        ld1             {v4.8b}, [x1], x2
        dup             v26.8b,  w6
        dup             v27.8b,  w7
        ext             v5.8b,  v4.8b,  v5.8b, #1
        trn1            v0.2s,  v24.2s, v25.2s
        trn1            v2.2s,  v26.2s, v27.2s
        trn1            v4.2s,  v4.2s,  v5.2s
1:      ld1             {v6.8b}, [x1], x2
        ext             v7.8b,  v6.8b,  v7.8b, #1
        trn1            v6.2s,  v6.2s,  v7.2s
        umull           v18.8h, v4.8b,  v0.8b
        umlal           v18.8h, v6.8b,  v2.8b
        ld1             {v4.8b}, [x1], x2
        ext             v5.8b,  v4.8b,  v5.8b, #1
        trn1            v4.2s,  v4.2s,  v5.2s
        prfm            pldl1strm, [x1]
        umull           v19.8h, v6.8b,  v0.8b
        umlal           v19.8h, v4.8b,  v2.8b
        trn1            v30.2d, v18.2d, v19.2d
        trn2            v31.2d, v18.2d, v19.2d
        add             v18.8h, v30.8h, v31.8h
  .ifc \codec,h264
        rshrn           v16.8b, v18.8h, #6
  .else
        add             v18.8h, v18.8h, v22.8h
        shrn            v16.8b, v18.8h, #6
  .endif
        subs            w3,  w3,  #2
        prfm            pldl1strm, [x1, x2]
  .ifc \type,avg
        ld1             {v20.s}[0], [x8], x2
        ld1             {v20.s}[1], [x8], x2
        urhadd          v16.8b, v16.8b, v20.8b
  .endif
        st1             {v16.s}[0], [x0], x2
        st1             {v16.s}[1], [x0], x2
        b.gt            1b
        ret

2:      adds            w12, w12, w6
        dup             v30.8b, w4
        b.eq            5f
        tst             w6,  w6
        dup             v31.8b, w12
        trn1            v0.2s,  v30.2s, v31.2s
        trn2            v1.2s,  v30.2s, v31.2s
        b.eq            4f

        ext             v1.8b,  v0.8b,  v1.8b, #4
        ld1             {v4.s}[0], [x1], x2
3:      ld1             {v4.s}[1], [x1], x2
        umull           v18.8h, v4.8b,  v0.8b
        ld1             {v4.s}[0], [x1], x2
        umull           v19.8h, v4.8b,  v1.8b
        trn1            v30.2d, v18.2d, v19.2d
        trn2            v31.2d, v18.2d, v19.2d
        add             v18.8h, v30.8h, v31.8h
        prfm            pldl1strm, [x1]
  .ifc \codec,h264
        rshrn           v16.8b, v18.8h, #6
  .else
        add             v18.8h, v18.8h, v22.8h
        shrn            v16.8b, v18.8h, #6
  .endif
  .ifc \type,avg
        ld1             {v20.s}[0], [x8], x2
        ld1             {v20.s}[1], [x8], x2
        urhadd          v16.8b, v16.8b, v20.8b
  .endif
        subs            w3,  w3,  #2
        prfm            pldl1strm, [x1, x2]
        st1             {v16.s}[0], [x0], x2
        st1             {v16.s}[1], [x0], x2
        b.gt            3b
        ret

4:      ld1             {v4.8b}, [x1], x2
        ld1             {v6.8b}, [x1], x2
        ext             v5.8b,  v4.8b,  v5.8b, #1
        ext             v7.8b,  v6.8b,  v7.8b, #1
        trn1            v4.2s,  v4.2s,  v5.2s
        trn1            v6.2s,  v6.2s,  v7.2s
        umull           v18.8h, v4.8b,  v0.8b
        umull           v19.8h, v6.8b,  v0.8b
        subs            w3,  w3,  #2
        trn1            v30.2d, v18.2d, v19.2d
        trn2            v31.2d, v18.2d, v19.2d
        add             v18.8h, v30.8h, v31.8h
        prfm            pldl1strm, [x1]
  .ifc \codec,h264
        rshrn           v16.8b, v18.8h, #6
  .else
        add             v18.8h, v18.8h, v22.8h
        shrn            v16.8b, v18.8h, #6
  .endif
  .ifc \type,avg
        ld1             {v20.s}[0], [x8], x2
        ld1             {v20.s}[1], [x8], x2
        urhadd          v16.8b, v16.8b, v20.8b
  .endif
        prfm            pldl1strm, [x1]
        st1             {v16.s}[0], [x0], x2
        st1             {v16.s}[1], [x0], x2
        b.gt            4b
        ret

5:      ld1             {v4.s}[0], [x1], x2
        ld1             {v4.s}[1], [x1], x2
        umull           v18.8h, v4.8b,  v30.8b
        subs            w3,  w3,  #2
        prfm            pldl1strm, [x1]
  .ifc \codec,h264
        rshrn           v16.8b, v18.8h, #6
  .else
        add             v18.8h, v18.8h, v22.8h
        shrn            v16.8b, v18.8h, #6
  .endif
  .ifc \type,avg
        ld1             {v20.s}[0], [x8], x2
        ld1             {v20.s}[1], [x8], x2
        urhadd          v16.8b, v16.8b, v20.8b
  .endif
        prfm            pldl1strm, [x1]
        st1             {v16.s}[0], [x0], x2
        st1             {v16.s}[1], [x0], x2
        b.gt            5b
        ret
endfunc
.endm

.macro  h264_chroma_mc2 type
function ff_\type\()_h264_chroma_mc2_neon, export=1
        prfm            pldl1strm, [x1]
        prfm            pldl1strm, [x1, x2]
        orr             w7,  w4,  w5
        cbz             w7,  2f

        mul             w7,  w4,  w5
        lsl             w14, w5,  #3
        lsl             w13, w4,  #3
        sub             w6,  w14, w7
        sub             w12, w13, w7
        sub             w4,  w7,  w13
        sub             w4,  w4,  w14
        add             w4,  w4,  #64
        dup             v0.8b,  w4
        dup             v2.8b,  w12
        dup             v1.8b,  w6
        dup             v3.8b,  w7
        trn1            v0.4h,  v0.4h,  v2.4h
        trn1            v1.4h,  v1.4h,  v3.4h
1:
        ld1             {v4.s}[0],  [x1], x2
        ld1             {v4.s}[1],  [x1], x2
        rev64           v5.2s,  v4.2s
        ld1             {v5.s}[1],  [x1]
        ext             v6.8b,  v4.8b,  v5.8b,  #1
        ext             v7.8b,  v5.8b,  v4.8b,  #1
        trn1            v4.4h,  v4.4h,  v6.4h
        trn1            v5.4h,  v5.4h,  v7.4h
        umull           v16.8h, v4.8b,  v0.8b
        umlal           v16.8h, v5.8b,  v1.8b
  .ifc \type,avg
        ld1             {v18.h}[0], [x0], x2
        ld1             {v18.h}[2], [x0]
        sub             x0,  x0,  x2
  .endif
        rev64           v17.4s, v16.4s
        add             v16.8h, v16.8h, v17.8h
        rshrn           v16.8b, v16.8h, #6
  .ifc \type,avg
        urhadd          v16.8b, v16.8b, v18.8b
  .endif
        st1             {v16.h}[0], [x0], x2
        st1             {v16.h}[2], [x0], x2
        subs            w3,  w3,  #2
        b.gt            1b
        ret

2:
        ld1             {v16.h}[0], [x1], x2
        ld1             {v16.h}[1], [x1], x2
  .ifc \type,avg
        ld1             {v18.h}[0], [x0], x2
        ld1             {v18.h}[1], [x0]
        sub             x0,  x0,  x2
        urhadd          v16.8b, v16.8b, v18.8b
  .endif
        st1             {v16.h}[0], [x0], x2
        st1             {v16.h}[1], [x0], x2
        subs            w3,  w3,  #2
        b.gt            2b
        ret
endfunc
.endm

        h264_chroma_mc8 put
        h264_chroma_mc8 avg
        h264_chroma_mc4 put
        h264_chroma_mc4 avg
        h264_chroma_mc2 put
        h264_chroma_mc2 avg

#if CONFIG_RV40_DECODER
const   rv40bias
        .short           0, 16, 32, 16
        .short          32, 28, 32, 28
        .short           0, 32, 16, 32
        .short          32, 28, 32, 28
endconst

        h264_chroma_mc8 put, rv40
        h264_chroma_mc8 avg, rv40
        h264_chroma_mc4 put, rv40
        h264_chroma_mc4 avg, rv40
#endif

#if CONFIG_VC1DSP
        h264_chroma_mc8 put, vc1
        h264_chroma_mc8 avg, vc1
        h264_chroma_mc4 put, vc1
        h264_chroma_mc4 avg, vc1
#endif
