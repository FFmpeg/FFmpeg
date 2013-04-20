/*
 * aligned/packed access motion
 *
 * Copyright (c) 2001-2003 BERO <bero@geocities.co.jp>
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

#include "libavutil/attributes.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/rnd_avg.h"
#include "dsputil_sh4.h"


#define         LP(p)           *(uint32_t*)(p)
#define         LPC(p)          *(const uint32_t*)(p)


#define         UNPACK(ph,pl,tt0,tt1) do { \
        uint32_t t0,t1; t0=tt0;t1=tt1; \
        ph = ( (t0 & ~BYTE_VEC32(0x03))>>2) + ( (t1 & ~BYTE_VEC32(0x03))>>2); \
        pl = (t0 & BYTE_VEC32(0x03)) + (t1 & BYTE_VEC32(0x03)); } while(0)

#define         rnd_PACK(ph,pl,nph,npl) ph + nph + (((pl + npl + BYTE_VEC32(0x02))>>2) & BYTE_VEC32(0x03))
#define         no_rnd_PACK(ph,pl,nph,npl)      ph + nph + (((pl + npl + BYTE_VEC32(0x01))>>2) & BYTE_VEC32(0x03))

/* little-endian */
#define         MERGE1(a,b,ofs) (ofs==0)?a:( ((a)>>(8*ofs))|((b)<<(32-8*ofs)) )
#define         MERGE2(a,b,ofs) (ofs==3)?b:( ((a)>>(8*(ofs+1)))|((b)<<(32-8*(ofs+1))) )
/* big
#define         MERGE1(a,b,ofs) (ofs==0)?a:( ((a)<<(8*ofs))|((b)>>(32-8*ofs)) )
#define         MERGE2(a,b,ofs) (ofs==3)?b:( ((a)<<(8+8*ofs))|((b)>>(32-8-8*ofs)) )
*/


#define         put(d,s)        d = s
#define         avg(d,s)        d = rnd_avg32(s,d)

#define         OP_C4(ofs) \
        ref-=ofs; \
        do { \
                OP(LP(dest),MERGE1(LPC(ref),LPC(ref+4),ofs)); \
                ref+=stride; \
                dest+=stride; \
        } while(--height)

#define        OP_C40() \
        do { \
                OP(LP(dest),LPC(ref)); \
                ref+=stride; \
                dest+=stride; \
        } while(--height)

#define         OP_C(ofs,sz,avg2) \
{ \
        ref-=ofs; \
        do { \
                uint32_t        t0,t1; \
                t0 = LPC(ref+0); \
                t1 = LPC(ref+4); \
                OP(LP(dest+0), MERGE1(t0,t1,ofs)); \
                t0 = LPC(ref+8); \
                OP(LP(dest+4), MERGE1(t1,t0,ofs)); \
if (sz==16) { \
                t1 = LPC(ref+12); \
                OP(LP(dest+8), MERGE1(t0,t1,ofs)); \
                t0 = LPC(ref+16); \
                OP(LP(dest+12), MERGE1(t1,t0,ofs)); \
} \
                ref+=stride; \
                dest+= stride; \
        } while(--height); \
}

/* aligned */
#define         OP_C0(sz,avg2) \
{ \
        do { \
                OP(LP(dest+0), LPC(ref+0)); \
                OP(LP(dest+4), LPC(ref+4)); \
if (sz==16) { \
                OP(LP(dest+8), LPC(ref+8)); \
                OP(LP(dest+12), LPC(ref+12)); \
} \
                ref+=stride; \
                dest+= stride; \
        } while(--height); \
}

#define         OP_X(ofs,sz,avg2) \
{ \
        ref-=ofs; \
        do { \
                uint32_t        t0,t1; \
                t0 = LPC(ref+0); \
                t1 = LPC(ref+4); \
                OP(LP(dest+0), avg2(MERGE1(t0,t1,ofs),MERGE2(t0,t1,ofs))); \
                t0 = LPC(ref+8); \
                OP(LP(dest+4), avg2(MERGE1(t1,t0,ofs),MERGE2(t1,t0,ofs))); \
if (sz==16) { \
                t1 = LPC(ref+12); \
                OP(LP(dest+8), avg2(MERGE1(t0,t1,ofs),MERGE2(t0,t1,ofs))); \
                t0 = LPC(ref+16); \
                OP(LP(dest+12), avg2(MERGE1(t1,t0,ofs),MERGE2(t1,t0,ofs))); \
} \
                ref+=stride; \
                dest+= stride; \
        } while(--height); \
}

/* aligned */
#define         OP_Y0(sz,avg2) \
{ \
        uint32_t t0,t1,t2,t3,t; \
\
        t0 = LPC(ref+0); \
        t1 = LPC(ref+4); \
if (sz==16) { \
        t2 = LPC(ref+8); \
        t3 = LPC(ref+12); \
} \
        do { \
                ref += stride; \
\
                t = LPC(ref+0); \
                OP(LP(dest+0), avg2(t0,t)); t0 = t; \
                t = LPC(ref+4); \
                OP(LP(dest+4), avg2(t1,t)); t1 = t; \
if (sz==16) { \
                t = LPC(ref+8); \
                OP(LP(dest+8), avg2(t2,t)); t2 = t; \
                t = LPC(ref+12); \
                OP(LP(dest+12), avg2(t3,t)); t3 = t; \
} \
                dest+= stride; \
        } while(--height); \
}

#define         OP_Y(ofs,sz,avg2) \
{ \
        uint32_t t0,t1,t2,t3,t,w0,w1; \
\
        ref-=ofs; \
        w0 = LPC(ref+0); \
        w1 = LPC(ref+4); \
        t0 = MERGE1(w0,w1,ofs); \
        w0 = LPC(ref+8); \
        t1 = MERGE1(w1,w0,ofs); \
if (sz==16) { \
        w1 = LPC(ref+12); \
        t2 = MERGE1(w0,w1,ofs); \
        w0 = LPC(ref+16); \
        t3 = MERGE1(w1,w0,ofs); \
} \
        do { \
                ref += stride; \
\
                w0 = LPC(ref+0); \
                w1 = LPC(ref+4); \
                t = MERGE1(w0,w1,ofs); \
                OP(LP(dest+0), avg2(t0,t)); t0 = t; \
                w0 = LPC(ref+8); \
                t = MERGE1(w1,w0,ofs); \
                OP(LP(dest+4), avg2(t1,t)); t1 = t; \
if (sz==16) { \
                w1 = LPC(ref+12); \
                t = MERGE1(w0,w1,ofs); \
                OP(LP(dest+8), avg2(t2,t)); t2 = t; \
                w0 = LPC(ref+16); \
                t = MERGE1(w1,w0,ofs); \
                OP(LP(dest+12), avg2(t3,t)); t3 = t; \
} \
                dest+=stride; \
        } while(--height); \
}

#define OP_X0(sz,avg2) OP_X(0,sz,avg2)
#define OP_XY0(sz,PACK) OP_XY(0,sz,PACK)
#define         OP_XY(ofs,sz,PACK) \
{ \
        uint32_t        t2,t3,w0,w1; \
        uint32_t        a0,a1,a2,a3,a4,a5,a6,a7; \
\
        ref -= ofs; \
        w0 = LPC(ref+0); \
        w1 = LPC(ref+4); \
        UNPACK(a0,a1,MERGE1(w0,w1,ofs),MERGE2(w0,w1,ofs)); \
        w0 = LPC(ref+8); \
        UNPACK(a2,a3,MERGE1(w1,w0,ofs),MERGE2(w1,w0,ofs)); \
if (sz==16) { \
        w1 = LPC(ref+12); \
        UNPACK(a4,a5,MERGE1(w0,w1,ofs),MERGE2(w0,w1,ofs)); \
        w0 = LPC(ref+16); \
        UNPACK(a6,a7,MERGE1(w1,w0,ofs),MERGE2(w1,w0,ofs)); \
} \
        do { \
                ref+=stride; \
                w0 = LPC(ref+0); \
                w1 = LPC(ref+4); \
                UNPACK(t2,t3,MERGE1(w0,w1,ofs),MERGE2(w0,w1,ofs)); \
                OP(LP(dest+0),PACK(a0,a1,t2,t3)); \
                a0 = t2; a1 = t3; \
                w0 = LPC(ref+8); \
                UNPACK(t2,t3,MERGE1(w1,w0,ofs),MERGE2(w1,w0,ofs)); \
                OP(LP(dest+4),PACK(a2,a3,t2,t3)); \
                a2 = t2; a3 = t3; \
if (sz==16) { \
                w1 = LPC(ref+12); \
                UNPACK(t2,t3,MERGE1(w0,w1,ofs),MERGE2(w0,w1,ofs)); \
                OP(LP(dest+8),PACK(a4,a5,t2,t3)); \
                a4 = t2; a5 = t3; \
                w0 = LPC(ref+16); \
                UNPACK(t2,t3,MERGE1(w1,w0,ofs),MERGE2(w1,w0,ofs)); \
                OP(LP(dest+12),PACK(a6,a7,t2,t3)); \
                a6 = t2; a7 = t3; \
} \
                dest+=stride; \
        } while(--height); \
}

#define         put_pixels8_c            ff_put_rnd_pixels8_o
#define         put_pixels16_c           ff_put_rnd_pixels16_o
#define         avg_pixels8_c            ff_avg_rnd_pixels8_o
#define         avg_pixels16_c           ff_avg_rnd_pixels16_o
#define         put_no_rnd_pixels8_c     ff_put_rnd_pixels8_o
#define         put_no_rnd_pixels16_c    ff_put_rnd_pixels16_o
#define         avg_no_rnd_pixels16_c    ff_avg_rnd_pixels16_o

#if CONFIG_HPELDSP

#include "qpel.c"

#endif

av_cold void ff_dsputil_init_align(DSPContext *c, AVCodecContext *avctx)
{
#if CONFIG_HPELDSP

#define dspfunc(PFX, IDX, NUM) \
    c->PFX ## _pixels_tab[IDX][ 0] = PFX ## NUM ## _mc00_sh4; \
    c->PFX ## _pixels_tab[IDX][ 1] = PFX ## NUM ## _mc10_sh4; \
    c->PFX ## _pixels_tab[IDX][ 2] = PFX ## NUM ## _mc20_sh4; \
    c->PFX ## _pixels_tab[IDX][ 3] = PFX ## NUM ## _mc30_sh4; \
    c->PFX ## _pixels_tab[IDX][ 4] = PFX ## NUM ## _mc01_sh4; \
    c->PFX ## _pixels_tab[IDX][ 5] = PFX ## NUM ## _mc11_sh4; \
    c->PFX ## _pixels_tab[IDX][ 6] = PFX ## NUM ## _mc21_sh4; \
    c->PFX ## _pixels_tab[IDX][ 7] = PFX ## NUM ## _mc31_sh4; \
    c->PFX ## _pixels_tab[IDX][ 8] = PFX ## NUM ## _mc02_sh4; \
    c->PFX ## _pixels_tab[IDX][ 9] = PFX ## NUM ## _mc12_sh4; \
    c->PFX ## _pixels_tab[IDX][10] = PFX ## NUM ## _mc22_sh4; \
    c->PFX ## _pixels_tab[IDX][11] = PFX ## NUM ## _mc32_sh4; \
    c->PFX ## _pixels_tab[IDX][12] = PFX ## NUM ## _mc03_sh4; \
    c->PFX ## _pixels_tab[IDX][13] = PFX ## NUM ## _mc13_sh4; \
    c->PFX ## _pixels_tab[IDX][14] = PFX ## NUM ## _mc23_sh4; \
    c->PFX ## _pixels_tab[IDX][15] = PFX ## NUM ## _mc33_sh4

    dspfunc(put_qpel, 0, 16);
    dspfunc(put_no_rnd_qpel, 0, 16);

    dspfunc(avg_qpel, 0, 16);
    /* dspfunc(avg_no_rnd_qpel, 0, 16); */

    dspfunc(put_qpel, 1, 8);
    dspfunc(put_no_rnd_qpel, 1, 8);

    dspfunc(avg_qpel, 1, 8);
    /* dspfunc(avg_no_rnd_qpel, 1, 8); */

#undef dspfunc

    c->put_mspel_pixels_tab[0]= put_mspel8_mc00_sh4;
    c->put_mspel_pixels_tab[1]= put_mspel8_mc10_sh4;
    c->put_mspel_pixels_tab[2]= put_mspel8_mc20_sh4;
    c->put_mspel_pixels_tab[3]= put_mspel8_mc30_sh4;
    c->put_mspel_pixels_tab[4]= put_mspel8_mc02_sh4;
    c->put_mspel_pixels_tab[5]= put_mspel8_mc12_sh4;
    c->put_mspel_pixels_tab[6]= put_mspel8_mc22_sh4;
    c->put_mspel_pixels_tab[7]= put_mspel8_mc32_sh4;

    c->gmc1 = gmc1_c;

#endif
}
