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


#include "../avcodec.h"
#include "../dsputil.h"


#define         LP(p)           *(uint32_t*)(p)


#define         UNPACK(ph,pl,tt0,tt1) do { \
        uint32_t t0,t1; t0=tt0;t1=tt1; \
        ph = ( (t0 & ~BYTE_VEC32(0x03))>>2) + ( (t1 & ~BYTE_VEC32(0x03))>>2); \
        pl = (t0 & BYTE_VEC32(0x03)) + (t1 & BYTE_VEC32(0x03)); } while(0)

#define         rnd_PACK(ph,pl,nph,npl) ph + nph + (((pl + npl + BYTE_VEC32(0x02))>>2) & BYTE_VEC32(0x03))
#define         no_rnd_PACK(ph,pl,nph,npl)      ph + nph + (((pl + npl + BYTE_VEC32(0x01))>>2) & BYTE_VEC32(0x03))

/* little endian */
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
                OP(LP(dest),MERGE1(LP(ref),LP(ref+4),ofs)); \
                ref+=stride; \
                dest+=stride; \
        } while(--height)

#define        OP_C40() \
        do { \
                OP(LP(dest),LP(ref)); \
                ref+=stride; \
                dest+=stride; \
        } while(--height)


#define         OP      put

static void put_pixels4_c(uint8_t *dest,const uint8_t *ref, const int stride,int height)
{
        switch((int)ref&3){
        case 0: OP_C40(); return;
        case 1: OP_C4(1); return;
        case 2: OP_C4(2); return;
        case 3: OP_C4(3); return;
        }
}

#undef          OP
#define         OP      avg

static void avg_pixels4_c(uint8_t *dest,const uint8_t *ref, const int stride,int height)
{
        switch((int)ref&3){
        case 0: OP_C40(); return;
        case 1: OP_C4(1); return;
        case 2: OP_C4(2); return;
        case 3: OP_C4(3); return;
        }
}

#undef          OP

#define         OP_C(ofs,sz,avg2) \
{ \
        ref-=ofs; \
        do { \
                uint32_t        t0,t1; \
                t0 = LP(ref+0); \
                t1 = LP(ref+4); \
                OP(LP(dest+0), MERGE1(t0,t1,ofs)); \
                t0 = LP(ref+8); \
                OP(LP(dest+4), MERGE1(t1,t0,ofs)); \
if (sz==16) { \
                t1 = LP(ref+12); \
                OP(LP(dest+8), MERGE1(t0,t1,ofs)); \
                t0 = LP(ref+16); \
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
                OP(LP(dest+0), LP(ref+0)); \
                OP(LP(dest+4), LP(ref+4)); \
if (sz==16) { \
                OP(LP(dest+8), LP(ref+8)); \
                OP(LP(dest+12), LP(ref+12)); \
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
                t0 = LP(ref+0); \
                t1 = LP(ref+4); \
                OP(LP(dest+0), avg2(MERGE1(t0,t1,ofs),MERGE2(t0,t1,ofs))); \
                t0 = LP(ref+8); \
                OP(LP(dest+4), avg2(MERGE1(t1,t0,ofs),MERGE2(t1,t0,ofs))); \
if (sz==16) { \
                t1 = LP(ref+12); \
                OP(LP(dest+8), avg2(MERGE1(t0,t1,ofs),MERGE2(t0,t1,ofs))); \
                t0 = LP(ref+16); \
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
        t0 = LP(ref+0); \
        t1 = LP(ref+4); \
if (sz==16) { \
        t2 = LP(ref+8); \
        t3 = LP(ref+12); \
} \
        do { \
                ref += stride; \
\
                t = LP(ref+0); \
                OP(LP(dest+0), avg2(t0,t)); t0 = t; \
                t = LP(ref+4); \
                OP(LP(dest+4), avg2(t1,t)); t1 = t; \
if (sz==16) { \
                t = LP(ref+8); \
                OP(LP(dest+8), avg2(t2,t)); t2 = t; \
                t = LP(ref+12); \
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
        w0 = LP(ref+0); \
        w1 = LP(ref+4); \
        t0 = MERGE1(w0,w1,ofs); \
        w0 = LP(ref+8); \
        t1 = MERGE1(w1,w0,ofs); \
if (sz==16) { \
        w1 = LP(ref+12); \
        t2 = MERGE1(w0,w1,ofs); \
        w0 = LP(ref+16); \
        t3 = MERGE1(w1,w0,ofs); \
} \
        do { \
                ref += stride; \
\
                w0 = LP(ref+0); \
                w1 = LP(ref+4); \
                t = MERGE1(w0,w1,ofs); \
                OP(LP(dest+0), avg2(t0,t)); t0 = t; \
                w0 = LP(ref+8); \
                t = MERGE1(w1,w0,ofs); \
                OP(LP(dest+4), avg2(t1,t)); t1 = t; \
if (sz==16) { \
                w1 = LP(ref+12); \
                t = MERGE1(w0,w1,ofs); \
                OP(LP(dest+8), avg2(t2,t)); t2 = t; \
                w0 = LP(ref+16); \
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
        w0 = LP(ref+0); \
        w1 = LP(ref+4); \
        UNPACK(a0,a1,MERGE1(w0,w1,ofs),MERGE2(w0,w1,ofs)); \
        w0 = LP(ref+8); \
        UNPACK(a2,a3,MERGE1(w1,w0,ofs),MERGE2(w1,w0,ofs)); \
if (sz==16) { \
        w1 = LP(ref+12); \
        UNPACK(a4,a5,MERGE1(w0,w1,ofs),MERGE2(w0,w1,ofs)); \
        w0 = LP(ref+16); \
        UNPACK(a6,a7,MERGE1(w1,w0,ofs),MERGE2(w1,w0,ofs)); \
} \
        do { \
                ref+=stride; \
                w0 = LP(ref+0); \
                w1 = LP(ref+4); \
                UNPACK(t2,t3,MERGE1(w0,w1,ofs),MERGE2(w0,w1,ofs)); \
                OP(LP(dest+0),PACK(a0,a1,t2,t3)); \
                a0 = t2; a1 = t3; \
                w0 = LP(ref+8); \
                UNPACK(t2,t3,MERGE1(w1,w0,ofs),MERGE2(w1,w0,ofs)); \
                OP(LP(dest+4),PACK(a2,a3,t2,t3)); \
                a2 = t2; a3 = t3; \
if (sz==16) { \
                w1 = LP(ref+12); \
                UNPACK(t2,t3,MERGE1(w0,w1,ofs),MERGE2(w0,w1,ofs)); \
                OP(LP(dest+8),PACK(a4,a5,t2,t3)); \
                a4 = t2; a5 = t3; \
                w0 = LP(ref+16); \
                UNPACK(t2,t3,MERGE1(w1,w0,ofs),MERGE2(w1,w0,ofs)); \
                OP(LP(dest+12),PACK(a6,a7,t2,t3)); \
                a6 = t2; a7 = t3; \
} \
                dest+=stride; \
        } while(--height); \
}

#define         DEFFUNC(op,rnd,xy,sz,OP_N,avgfunc) \
static void op##_##rnd##_pixels##sz##_##xy (uint8_t * dest, const uint8_t * ref, \
                                const int stride, int height) \
{ \
        switch((int)ref&3) { \
        case 0:OP_N##0(sz,rnd##_##avgfunc); return; \
        case 1:OP_N(1,sz,rnd##_##avgfunc); return; \
        case 2:OP_N(2,sz,rnd##_##avgfunc); return; \
        case 3:OP_N(3,sz,rnd##_##avgfunc); return; \
        } \
}

#define OP put

DEFFUNC(put,   rnd,o,8,OP_C,avg2)
DEFFUNC(put,   rnd,x,8,OP_X,avg2)
DEFFUNC(put,no_rnd,x,8,OP_X,avg2)
DEFFUNC(put,   rnd,y,8,OP_Y,avg2)
DEFFUNC(put,no_rnd,y,8,OP_Y,avg2)
DEFFUNC(put,   rnd,xy,8,OP_XY,PACK)
DEFFUNC(put,no_rnd,xy,8,OP_XY,PACK)
DEFFUNC(put,   rnd,o,16,OP_C,avg2)
DEFFUNC(put,   rnd,x,16,OP_X,avg2)
DEFFUNC(put,no_rnd,x,16,OP_X,avg2)
DEFFUNC(put,   rnd,y,16,OP_Y,avg2)
DEFFUNC(put,no_rnd,y,16,OP_Y,avg2)
DEFFUNC(put,   rnd,xy,16,OP_XY,PACK)
DEFFUNC(put,no_rnd,xy,16,OP_XY,PACK)

#undef OP
#define OP avg

DEFFUNC(avg,   rnd,o,8,OP_C,avg2)
DEFFUNC(avg,   rnd,x,8,OP_X,avg2)
DEFFUNC(avg,no_rnd,x,8,OP_X,avg2)
DEFFUNC(avg,   rnd,y,8,OP_Y,avg2)
DEFFUNC(avg,no_rnd,y,8,OP_Y,avg2)
DEFFUNC(avg,   rnd,xy,8,OP_XY,PACK)
DEFFUNC(avg,no_rnd,xy,8,OP_XY,PACK)
DEFFUNC(avg,   rnd,o,16,OP_C,avg2)
DEFFUNC(avg,   rnd,x,16,OP_X,avg2)
DEFFUNC(avg,no_rnd,x,16,OP_X,avg2)
DEFFUNC(avg,   rnd,y,16,OP_Y,avg2)
DEFFUNC(avg,no_rnd,y,16,OP_Y,avg2)
DEFFUNC(avg,   rnd,xy,16,OP_XY,PACK)
DEFFUNC(avg,no_rnd,xy,16,OP_XY,PACK)

#undef OP

#define         put_no_rnd_pixels8_o     put_rnd_pixels8_o
#define         put_no_rnd_pixels16_o    put_rnd_pixels16_o
#define         avg_no_rnd_pixels8_o     avg_rnd_pixels8_o
#define         avg_no_rnd_pixels16_o    avg_rnd_pixels16_o

#define         put_pixels8_c            put_rnd_pixels8_o
#define         put_pixels16_c           put_rnd_pixels16_o
#define         avg_pixels8_c            avg_rnd_pixels8_o
#define         avg_pixels16_c           avg_rnd_pixels16_o
#define         put_no_rnd_pixels8_c     put_rnd_pixels8_o
#define         put_no_rnd_pixels16_c    put_rnd_pixels16_o
#define         avg_no_rnd_pixels8_c     avg_rnd_pixels8_o
#define         avg_no_rnd_pixels16_c    avg_rnd_pixels16_o

#define         QPEL

#ifdef QPEL

#include "qpel.c"

#endif

void dsputil_init_align(DSPContext* c, AVCodecContext *avctx)
{
        c->put_pixels_tab[0][0] = put_rnd_pixels16_o;
        c->put_pixels_tab[0][1] = put_rnd_pixels16_x;
        c->put_pixels_tab[0][2] = put_rnd_pixels16_y;
        c->put_pixels_tab[0][3] = put_rnd_pixels16_xy;
        c->put_pixels_tab[1][0] = put_rnd_pixels8_o;
        c->put_pixels_tab[1][1] = put_rnd_pixels8_x;
        c->put_pixels_tab[1][2] = put_rnd_pixels8_y;
        c->put_pixels_tab[1][3] = put_rnd_pixels8_xy;

        c->put_no_rnd_pixels_tab[0][0] = put_no_rnd_pixels16_o;
        c->put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x;
        c->put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y;
        c->put_no_rnd_pixels_tab[0][3] = put_no_rnd_pixels16_xy;
        c->put_no_rnd_pixels_tab[1][0] = put_no_rnd_pixels8_o;
        c->put_no_rnd_pixels_tab[1][1] = put_no_rnd_pixels8_x;
        c->put_no_rnd_pixels_tab[1][2] = put_no_rnd_pixels8_y;
        c->put_no_rnd_pixels_tab[1][3] = put_no_rnd_pixels8_xy;

        c->avg_pixels_tab[0][0] = avg_rnd_pixels16_o;
        c->avg_pixels_tab[0][1] = avg_rnd_pixels16_x;
        c->avg_pixels_tab[0][2] = avg_rnd_pixels16_y;
        c->avg_pixels_tab[0][3] = avg_rnd_pixels16_xy;
        c->avg_pixels_tab[1][0] = avg_rnd_pixels8_o;
        c->avg_pixels_tab[1][1] = avg_rnd_pixels8_x;
        c->avg_pixels_tab[1][2] = avg_rnd_pixels8_y;
        c->avg_pixels_tab[1][3] = avg_rnd_pixels8_xy;

        c->avg_no_rnd_pixels_tab[0][0] = avg_no_rnd_pixels16_o;
        c->avg_no_rnd_pixels_tab[0][1] = avg_no_rnd_pixels16_x;
        c->avg_no_rnd_pixels_tab[0][2] = avg_no_rnd_pixels16_y;
        c->avg_no_rnd_pixels_tab[0][3] = avg_no_rnd_pixels16_xy;
        c->avg_no_rnd_pixels_tab[1][0] = avg_no_rnd_pixels8_o;
        c->avg_no_rnd_pixels_tab[1][1] = avg_no_rnd_pixels8_x;
        c->avg_no_rnd_pixels_tab[1][2] = avg_no_rnd_pixels8_y;
        c->avg_no_rnd_pixels_tab[1][3] = avg_no_rnd_pixels8_xy;

#ifdef QPEL

#define dspfunc(PFX, IDX, NUM) \
    c->PFX ## _pixels_tab[IDX][ 0] = PFX ## NUM ## _mc00_c; \
    c->PFX ## _pixels_tab[IDX][ 1] = PFX ## NUM ## _mc10_c; \
    c->PFX ## _pixels_tab[IDX][ 2] = PFX ## NUM ## _mc20_c; \
    c->PFX ## _pixels_tab[IDX][ 3] = PFX ## NUM ## _mc30_c; \
    c->PFX ## _pixels_tab[IDX][ 4] = PFX ## NUM ## _mc01_c; \
    c->PFX ## _pixels_tab[IDX][ 5] = PFX ## NUM ## _mc11_c; \
    c->PFX ## _pixels_tab[IDX][ 6] = PFX ## NUM ## _mc21_c; \
    c->PFX ## _pixels_tab[IDX][ 7] = PFX ## NUM ## _mc31_c; \
    c->PFX ## _pixels_tab[IDX][ 8] = PFX ## NUM ## _mc02_c; \
    c->PFX ## _pixels_tab[IDX][ 9] = PFX ## NUM ## _mc12_c; \
    c->PFX ## _pixels_tab[IDX][10] = PFX ## NUM ## _mc22_c; \
    c->PFX ## _pixels_tab[IDX][11] = PFX ## NUM ## _mc32_c; \
    c->PFX ## _pixels_tab[IDX][12] = PFX ## NUM ## _mc03_c; \
    c->PFX ## _pixels_tab[IDX][13] = PFX ## NUM ## _mc13_c; \
    c->PFX ## _pixels_tab[IDX][14] = PFX ## NUM ## _mc23_c; \
    c->PFX ## _pixels_tab[IDX][15] = PFX ## NUM ## _mc33_c

    dspfunc(put_qpel, 0, 16);
    dspfunc(put_no_rnd_qpel, 0, 16);

    dspfunc(avg_qpel, 0, 16);
    /* dspfunc(avg_no_rnd_qpel, 0, 16); */

    dspfunc(put_qpel, 1, 8);
    dspfunc(put_no_rnd_qpel, 1, 8);

    dspfunc(avg_qpel, 1, 8);
    /* dspfunc(avg_no_rnd_qpel, 1, 8); */

    dspfunc(put_h264_qpel, 0, 16);
    dspfunc(put_h264_qpel, 1, 8);
    dspfunc(put_h264_qpel, 2, 4);
    dspfunc(avg_h264_qpel, 0, 16);
    dspfunc(avg_h264_qpel, 1, 8);
    dspfunc(avg_h264_qpel, 2, 4);

#undef dspfunc
    c->put_h264_chroma_pixels_tab[0]= put_h264_chroma_mc8_c;
    c->put_h264_chroma_pixels_tab[1]= put_h264_chroma_mc4_c;
    c->put_h264_chroma_pixels_tab[2]= put_h264_chroma_mc2_c;
    c->avg_h264_chroma_pixels_tab[0]= avg_h264_chroma_mc8_c;
    c->avg_h264_chroma_pixels_tab[1]= avg_h264_chroma_mc4_c;
    c->avg_h264_chroma_pixels_tab[2]= avg_h264_chroma_mc2_c;

    c->put_mspel_pixels_tab[0]= put_mspel8_mc00_c;
    c->put_mspel_pixels_tab[1]= put_mspel8_mc10_c;
    c->put_mspel_pixels_tab[2]= put_mspel8_mc20_c;
    c->put_mspel_pixels_tab[3]= put_mspel8_mc30_c;
    c->put_mspel_pixels_tab[4]= put_mspel8_mc02_c;
    c->put_mspel_pixels_tab[5]= put_mspel8_mc12_c;
    c->put_mspel_pixels_tab[6]= put_mspel8_mc22_c;
    c->put_mspel_pixels_tab[7]= put_mspel8_mc32_c;

    c->gmc1 = gmc1_c;
    c->gmc = gmc_c;

#endif
}
