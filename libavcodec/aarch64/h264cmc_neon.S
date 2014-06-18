/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2013 Janne Grunau <janne-libav@jannau.net>
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

#include "libavutil/aarch64/asm.S"

/* chroma_mc8(uint8_t *dst, uint8_t *src, int stride, int h, int x, int y) */
.macro  h264_chroma_mc8 type, codec=h264
function ff_\type\()_\codec\()_chroma_mc8_neon, export=1
        sxtw            x2,  w2
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
        add             x6,  x6,  w9, UXTW
        ld1r            {v22.8H}, [x6]
  .endif
  .ifc \codec,vc1
        movi            v22.8H,   #28
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

        dup             v0.8B,  w4
        dup             v1.8B,  w12
        ld1             {v4.8B, v5.8B}, [x1], x2
        dup             v2.8B,  w6
        dup             v3.8B,  w7
        ext             v5.8B,  v4.8B,  v5.8B,  #1
1:      ld1             {v6.8B, v7.8B}, [x1], x2
        umull           v16.8H, v4.8B,  v0.8B
        umlal           v16.8H, v5.8B,  v1.8B
        ext             v7.8B,  v6.8B,  v7.8B,  #1
        ld1             {v4.8B, v5.8B}, [x1], x2
        umlal           v16.8H, v6.8B,  v2.8B
        prfm            pldl1strm, [x1]
        ext             v5.8B,  v4.8B,  v5.8B,  #1
        umlal           v16.8H, v7.8B,  v3.8B
        umull           v17.8H, v6.8B,  v0.8B
        subs            w3,  w3,  #2
        umlal           v17.8H, v7.8B, v1.8B
        umlal           v17.8H, v4.8B, v2.8B
        umlal           v17.8H, v5.8B, v3.8B
        prfm            pldl1strm, [x1, x2]
  .ifc \codec,h264
        rshrn           v16.8B, v16.8H, #6
        rshrn           v17.8B, v17.8H, #6
  .else
        add             v16.8H, v16.8H, v22.8H
        add             v17.8H, v17.8H, v22.8H
        shrn            v16.8B, v16.8H, #6
        shrn            v17.8B, v17.8H, #6
  .endif
  .ifc \type,avg
        ld1             {v20.8B}, [x8], x2
        ld1             {v21.8B}, [x8], x2
        urhadd          v16.8B, v16.8B, v20.8B
        urhadd          v17.8B, v17.8B, v21.8B
  .endif
        st1             {v16.8B}, [x0], x2
        st1             {v17.8B}, [x0], x2
        b.gt            1b
        ret

2:      adds            w12, w12, w6
        dup             v0.8B, w4
        b.eq            5f
        tst             w6,  w6
        dup             v1.8B, w12
        b.eq            4f

        ld1             {v4.8B}, [x1], x2
3:      ld1             {v6.8B}, [x1], x2
        umull           v16.8H, v4.8B,  v0.8B
        umlal           v16.8H, v6.8B,  v1.8B
        ld1             {v4.8B}, [x1], x2
        umull           v17.8H, v6.8B,  v0.8B
        umlal           v17.8H, v4.8B,  v1.8B
        prfm            pldl1strm, [x1]
  .ifc \codec,h264
        rshrn           v16.8B, v16.8H, #6
        rshrn           v17.8B, v17.8H, #6
  .else
        add             v16.8H, v16.8H, v22.8H
        add             v17.8H, v17.8H, v22.8H
        shrn            v16.8B, v16.8H, #6
        shrn            v17.8B, v17.8H, #6
  .endif
        prfm            pldl1strm, [x1, x2]
  .ifc \type,avg
        ld1             {v20.8B}, [x8], x2
        ld1             {v21.8B}, [x8], x2
        urhadd          v16.8B, v16.8B, v20.8B
        urhadd          v17.8B, v17.8B, v21.8B
  .endif
        subs            w3,  w3,  #2
        st1             {v16.8B}, [x0], x2
        st1             {v17.8B}, [x0], x2
        b.gt            3b
        ret

4:      ld1             {v4.8B, v5.8B}, [x1], x2
        ld1             {v6.8B, v7.8B}, [x1], x2
        ext             v5.8B,  v4.8B,  v5.8B,  #1
        ext             v7.8B,  v6.8B,  v7.8B,  #1
        prfm            pldl1strm, [x1]
        subs            w3,  w3,  #2
        umull           v16.8H, v4.8B, v0.8B
        umlal           v16.8H, v5.8B, v1.8B
        umull           v17.8H, v6.8B, v0.8B
        umlal           v17.8H, v7.8B, v1.8B
        prfm            pldl1strm, [x1, x2]
  .ifc \codec,h264
        rshrn           v16.8B, v16.8H, #6
        rshrn           v17.8B, v17.8H, #6
  .else
        add             v16.8H, v16.8H, v22.8H
        add             v17.8H, v17.8H, v22.8H
        shrn            v16.8B, v16.8H, #6
        shrn            v17.8B, v17.8H, #6
  .endif
  .ifc \type,avg
        ld1             {v20.8B}, [x8], x2
        ld1             {v21.8B}, [x8], x2
        urhadd          v16.8B, v16.8B, v20.8B
        urhadd          v17.8B, v17.8B, v21.8B
  .endif
        st1             {v16.8B}, [x0], x2
        st1             {v17.8B}, [x0], x2
        b.gt            4b
        ret

5:      ld1             {v4.8B}, [x1], x2
        ld1             {v5.8B}, [x1], x2
        prfm            pldl1strm, [x1]
        subs            w3,  w3,  #2
        umull           v16.8H, v4.8B, v0.8B
        umull           v17.8H, v5.8B, v0.8B
        prfm            pldl1strm, [x1, x2]
  .ifc \codec,h264
        rshrn           v16.8B, v16.8H, #6
        rshrn           v17.8B, v17.8H, #6
  .else
        add             v16.8H, v16.8H, v22.8H
        add             v17.8H, v17.8H, v22.8H
        shrn            v16.8B, v16.8H, #6
        shrn            v17.8B, v17.8H, #6
  .endif
  .ifc \type,avg
        ld1             {v20.8B}, [x8], x2
        ld1             {v21.8B}, [x8], x2
        urhadd          v16.8B, v16.8B, v20.8B
        urhadd          v17.8B, v17.8B, v21.8B
  .endif
        st1             {v16.8B}, [x0], x2
        st1             {v17.8B}, [x0], x2
        b.gt            5b
        ret
endfunc
.endm

/* chroma_mc4(uint8_t *dst, uint8_t *src, int stride, int h, int x, int y) */
.macro  h264_chroma_mc4 type, codec=h264
function ff_\type\()_\codec\()_chroma_mc4_neon, export=1
        sxtw            x2,  w2
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
        add             x6,  x6,  w9, UXTW
        ld1r            {v22.8H}, [x6]
  .endif
  .ifc \codec,vc1
        movi            v22.8H,   #28
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

        dup             v24.8B,  w4
        dup             v25.8B,  w12
        ld1             {v4.8B}, [x1], x2
        dup             v26.8B,  w6
        dup             v27.8B,  w7
        ext             v5.8B,  v4.8B,  v5.8B, #1
        trn1            v0.2S,  v24.2S, v25.2S
        trn1            v2.2S,  v26.2S, v27.2S
        trn1            v4.2S,  v4.2S,  v5.2S
1:      ld1             {v6.8B}, [x1], x2
        ext             v7.8B,  v6.8B,  v7.8B, #1
        trn1            v6.2S,  v6.2S,  v7.2S
        umull           v18.8H, v4.8B,  v0.8B
        umlal           v18.8H, v6.8B,  v2.8B
        ld1             {v4.8B}, [x1], x2
        ext             v5.8B,  v4.8B,  v5.8B, #1
        trn1            v4.2S,  v4.2S,  v5.2S
        prfm            pldl1strm, [x1]
        umull           v19.8H, v6.8B,  v0.8B
        umlal           v19.8H, v4.8B,  v2.8B
        trn1            v30.2D, v18.2D, v19.2D
        trn2            v31.2D, v18.2D, v19.2D
        add             v18.8H, v30.8H, v31.8H
  .ifc \codec,h264
        rshrn           v16.8B, v18.8H, #6
  .else
        add             v18.8H, v18.8H, v22.8H
        shrn            v16.8B, v18.8H, #6
  .endif
        subs            w3,  w3,  #2
        prfm            pldl1strm, [x1, x2]
  .ifc \type,avg
        ld1             {v20.S}[0], [x8], x2
        ld1             {v20.S}[1], [x8], x2
        urhadd          v16.8B, v16.8B, v20.8B
  .endif
        st1             {v16.S}[0], [x0], x2
        st1             {v16.S}[1], [x0], x2
        b.gt            1b
        ret

2:      adds            w12, w12, w6
        dup             v30.8B, w4
        b.eq            5f
        tst             w6,  w6
        dup             v31.8B, w12
        trn1            v0.2S,  v30.2S, v31.2S
        trn2            v1.2S,  v30.2S, v31.2S
        b.eq            4f

        ext             v1.8B,  v0.8B,  v1.8B, #4
        ld1             {v4.S}[0], [x1], x2
3:      ld1             {v4.S}[1], [x1], x2
        umull           v18.8H, v4.8B,  v0.8B
        ld1             {v4.S}[0], [x1], x2
        umull           v19.8H, v4.8B,  v1.8B
        trn1            v30.2D, v18.2D, v19.2D
        trn2            v31.2D, v18.2D, v19.2D
        add             v18.8H, v30.8H, v31.8H
        prfm            pldl1strm, [x1]
  .ifc \codec,h264
        rshrn           v16.8B, v18.8H, #6
  .else
        add             v18.8H, v18.8H, v22.8H
        shrn            v16.8B, v18.8H, #6
  .endif
  .ifc \type,avg
        ld1             {v20.S}[0], [x8], x2
        ld1             {v20.S}[1], [x8], x2
        urhadd          v16.8B, v16.8B, v20.8B
  .endif
        subs            w3,  w3,  #2
        prfm            pldl1strm, [x1, x2]
        st1             {v16.S}[0], [x0], x2
        st1             {v16.S}[1], [x0], x2
        b.gt            3b
        ret

4:      ld1             {v4.8B}, [x1], x2
        ld1             {v6.8B}, [x1], x2
        ext             v5.8B,  v4.8B,  v5.8B, #1
        ext             v7.8B,  v6.8B,  v7.8B, #1
        trn1            v4.2S,  v4.2S,  v5.2S
        trn1            v6.2S,  v6.2S,  v7.2S
        umull           v18.8H, v4.8B,  v0.8B
        umull           v19.8H, v6.8B,  v0.8B
        subs            w3,  w3,  #2
        trn1            v30.2D, v18.2D, v19.2D
        trn2            v31.2D, v18.2D, v19.2D
        add             v18.8H, v30.8H, v31.8H
        prfm            pldl1strm, [x1]
  .ifc \codec,h264
        rshrn           v16.8B, v18.8H, #6
  .else
        add             v18.8H, v18.8H, v22.8H
        shrn            v16.8B, v18.8H, #6
  .endif
  .ifc \type,avg
        ld1             {v20.S}[0], [x8], x2
        ld1             {v20.S}[1], [x8], x2
        urhadd          v16.8B, v16.8B, v20.8B
  .endif
        prfm            pldl1strm, [x1]
        st1             {v16.S}[0], [x0], x2
        st1             {v16.S}[1], [x0], x2
        b.gt            4b
        ret

5:      ld1             {v4.S}[0], [x1], x2
        ld1             {v4.S}[1], [x1], x2
        umull           v18.8H, v4.8B,  v30.8B
        subs            w3,  w3,  #2
        prfm            pldl1strm, [x1]
  .ifc \codec,h264
        rshrn           v16.8B, v18.8H, #6
  .else
        add             v18.8H, v18.8H, v22.8H
        shrn            v16.8B, v18.8H, #6
  .endif
  .ifc \type,avg
        ld1             {v20.S}[0], [x8], x2
        ld1             {v20.S}[1], [x8], x2
        urhadd          v16.8B, v16.8B, v20.8B
  .endif
        prfm            pldl1strm, [x1]
        st1             {v16.S}[0], [x0], x2
        st1             {v16.S}[1], [x0], x2
        b.gt            5b
        ret
endfunc
.endm

.macro  h264_chroma_mc2 type
function ff_\type\()_h264_chroma_mc2_neon, export=1
        sxtw            x2,  w2
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
        dup             v0.8B,  w4
        dup             v2.8B,  w12
        dup             v1.8B,  w6
        dup             v3.8B,  w7
        trn1            v0.4H,  v0.4H,  v2.4H
        trn1            v1.4H,  v1.4H,  v3.4H
1:
        ld1             {v4.S}[0],  [x1], x2
        ld1             {v4.S}[1],  [x1], x2
        rev64           v5.2S,  v4.2S
        ld1             {v5.S}[1],  [x1]
        ext             v6.8B,  v4.8B,  v5.8B,  #1
        ext             v7.8B,  v5.8B,  v4.8B,  #1
        trn1            v4.4H,  v4.4H,  v6.4H
        trn1            v5.4H,  v5.4H,  v7.4H
        umull           v16.8H, v4.8B,  v0.8B
        umlal           v16.8H, v5.8B,  v1.8B
  .ifc \type,avg
        ld1             {v18.H}[0], [x0], x2
        ld1             {v18.H}[2], [x0]
        sub             x0,  x0,  x2
  .endif
        rev64           v17.4S, v16.4S
        add             v16.8H, v16.8H, v17.8H
        rshrn           v16.8B, v16.8H, #6
  .ifc \type,avg
        urhadd          v16.8B, v16.8B, v18.8B
  .endif
        st1             {v16.H}[0], [x0], x2
        st1             {v16.H}[2], [x0], x2
        subs            w3,  w3,  #2
        b.gt            1b
        ret

2:
        ld1             {v16.H}[0], [x1], x2
        ld1             {v16.H}[1], [x1], x2
  .ifc \type,avg
        ld1             {v18.H}[0], [x0], x2
        ld1             {v18.H}[1], [x0]
        sub             x0,  x0,  x2
        urhadd          v16.8B, v16.8B, v18.8B
  .endif
        st1             {v16.H}[0], [x0], x2
        st1             {v16.H}[1], [x0], x2
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

#if CONFIG_VC1_DECODER
        h264_chroma_mc8 put, vc1
        h264_chroma_mc8 avg, vc1
        h264_chroma_mc4 put, vc1
        h264_chroma_mc4 avg, vc1
#endif
