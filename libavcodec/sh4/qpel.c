/*
 * This is optimized for sh, which have post increment addressing (*p++).
 * Some CPU may be index (p[n]) faster than post increment (*p++).
 *
 * copyright (c) 2001-2003 BERO <bero@geocities.co.jp>
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

#include "libavutil/common.h"
#include "libavcodec/copy_block.h"
#include "libavcodec/rnd_avg.h"

#define PIXOP2(OPNAME, OP) \
\
static inline void OPNAME ## _pixels4_l2_aligned(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(LPC(src1  ),LPC(src2  )) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _pixels4_l2_aligned2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(AV_RN32(src1  ),LPC(src2  )) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _no_rnd_pixels16_l2_aligned2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),no_rnd_avg32(AV_RN32(src1  ),LPC(src2  )) ); \
                OP(LP(dst+4),no_rnd_avg32(AV_RN32(src1+4),LPC(src2+4)) ); \
                OP(LP(dst+8),no_rnd_avg32(AV_RN32(src1+8),LPC(src2+8)) ); \
                OP(LP(dst+12),no_rnd_avg32(AV_RN32(src1+12),LPC(src2+12)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _pixels16_l2_aligned2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(AV_RN32(src1  ),LPC(src2  )) ); \
                OP(LP(dst+4),rnd_avg32(AV_RN32(src1+4),LPC(src2+4)) ); \
                OP(LP(dst+8),rnd_avg32(AV_RN32(src1+8),LPC(src2+8)) ); \
                OP(LP(dst+12),rnd_avg32(AV_RN32(src1+12),LPC(src2+12)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _no_rnd_pixels8_l2_aligned2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do { /* onlye src2 aligned */\
                OP(LP(dst  ),no_rnd_avg32(AV_RN32(src1  ),LPC(src2  )) ); \
                OP(LP(dst+4),no_rnd_avg32(AV_RN32(src1+4),LPC(src2+4)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _pixels8_l2_aligned2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(AV_RN32(src1  ),LPC(src2  )) ); \
                OP(LP(dst+4),rnd_avg32(AV_RN32(src1+4),LPC(src2+4)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _no_rnd_pixels8_l2_aligned(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),no_rnd_avg32(LPC(src1  ),LPC(src2  )) ); \
                OP(LP(dst+4),no_rnd_avg32(LPC(src1+4),LPC(src2+4)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _pixels8_l2_aligned(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(LPC(src1  ),LPC(src2  )) ); \
                OP(LP(dst+4),rnd_avg32(LPC(src1+4),LPC(src2+4)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _no_rnd_pixels16_l2_aligned(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),no_rnd_avg32(LPC(src1  ),LPC(src2  )) ); \
                OP(LP(dst+4),no_rnd_avg32(LPC(src1+4),LPC(src2+4)) ); \
                OP(LP(dst+8),no_rnd_avg32(LPC(src1+8),LPC(src2+8)) ); \
                OP(LP(dst+12),no_rnd_avg32(LPC(src1+12),LPC(src2+12)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _pixels16_l2_aligned(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(LPC(src1  ),LPC(src2  )) ); \
                OP(LP(dst+4),rnd_avg32(LPC(src1+4),LPC(src2+4)) ); \
                OP(LP(dst+8),rnd_avg32(LPC(src1+8),LPC(src2+8)) ); \
                OP(LP(dst+12),rnd_avg32(LPC(src1+12),LPC(src2+12)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _no_rnd_pixels16_l2_aligned1(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{ OPNAME ## _no_rnd_pixels16_l2_aligned2(dst,src2,src1,dst_stride,src_stride2,src_stride1,h); } \
\
static inline void OPNAME ## _pixels16_l2_aligned1(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{ OPNAME ## _pixels16_l2_aligned2(dst,src2,src1,dst_stride,src_stride2,src_stride1,h); } \
\
static inline void OPNAME ## _no_rnd_pixels8_l2_aligned1(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{ OPNAME ## _no_rnd_pixels8_l2_aligned2(dst,src2,src1,dst_stride,src_stride2,src_stride1,h); } \
\
static inline void OPNAME ## _pixels8_l2_aligned1(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{ OPNAME ## _pixels8_l2_aligned2(dst,src2,src1,dst_stride,src_stride2,src_stride1,h); } \
\
static inline void OPNAME ## _pixels8_l4_aligned(uint8_t *dst, const uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *src4,int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
        do { \
                uint32_t a0,a1,a2,a3; \
                UNPACK(a0,a1,LPC(src1),LPC(src2)); \
                UNPACK(a2,a3,LPC(src3),LPC(src4)); \
                OP(LP(dst),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LPC(src1+4),LPC(src2+4)); \
                UNPACK(a2,a3,LPC(src3+4),LPC(src4+4)); \
                OP(LP(dst+4),rnd_PACK(a0,a1,a2,a3)); \
                src1+=src_stride1;\
                src2+=src_stride2;\
                src3+=src_stride3;\
                src4+=src_stride4;\
                dst+=dst_stride;\
        } while(--h); \
} \
\
static inline void OPNAME ## _no_rnd_pixels8_l4_aligned(uint8_t *dst, const uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *src4,int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
        do { \
                uint32_t a0,a1,a2,a3; \
                UNPACK(a0,a1,LPC(src1),LPC(src2)); \
                UNPACK(a2,a3,LPC(src3),LPC(src4)); \
                OP(LP(dst),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LPC(src1+4),LPC(src2+4)); \
                UNPACK(a2,a3,LPC(src3+4),LPC(src4+4)); \
                OP(LP(dst+4),no_rnd_PACK(a0,a1,a2,a3)); \
                src1+=src_stride1;\
                src2+=src_stride2;\
                src3+=src_stride3;\
                src4+=src_stride4;\
                dst+=dst_stride;\
        } while(--h); \
} \
\
static inline void OPNAME ## _pixels8_l4_aligned0(uint8_t *dst, const uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *src4,int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
        do { \
                uint32_t a0,a1,a2,a3; /* src1 only not aligned */\
                UNPACK(a0,a1,AV_RN32(src1),LPC(src2)); \
                UNPACK(a2,a3,LPC(src3),LPC(src4)); \
                OP(LP(dst),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,AV_RN32(src1+4),LPC(src2+4)); \
                UNPACK(a2,a3,LPC(src3+4),LPC(src4+4)); \
                OP(LP(dst+4),rnd_PACK(a0,a1,a2,a3)); \
                src1+=src_stride1;\
                src2+=src_stride2;\
                src3+=src_stride3;\
                src4+=src_stride4;\
                dst+=dst_stride;\
        } while(--h); \
} \
\
static inline void OPNAME ## _no_rnd_pixels8_l4_aligned0(uint8_t *dst, const uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *src4,int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
        do { \
                uint32_t a0,a1,a2,a3; \
                UNPACK(a0,a1,AV_RN32(src1),LPC(src2)); \
                UNPACK(a2,a3,LPC(src3),LPC(src4)); \
                OP(LP(dst),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,AV_RN32(src1+4),LPC(src2+4)); \
                UNPACK(a2,a3,LPC(src3+4),LPC(src4+4)); \
                OP(LP(dst+4),no_rnd_PACK(a0,a1,a2,a3)); \
                src1+=src_stride1;\
                src2+=src_stride2;\
                src3+=src_stride3;\
                src4+=src_stride4;\
                dst+=dst_stride;\
        } while(--h); \
} \
\
static inline void OPNAME ## _pixels16_l4_aligned(uint8_t *dst, const uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *src4,int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
        do { \
                uint32_t a0,a1,a2,a3; \
                UNPACK(a0,a1,LPC(src1),LPC(src2)); \
                UNPACK(a2,a3,LPC(src3),LPC(src4)); \
                OP(LP(dst),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LPC(src1+4),LPC(src2+4)); \
                UNPACK(a2,a3,LPC(src3+4),LPC(src4+4)); \
                OP(LP(dst+8),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LPC(src1+8),LPC(src2+8)); \
                UNPACK(a2,a3,LPC(src3+8),LPC(src4+8)); \
                OP(LP(dst+8),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LPC(src1+12),LPC(src2+12)); \
                UNPACK(a2,a3,LPC(src3+12),LPC(src4+12)); \
                OP(LP(dst+12),rnd_PACK(a0,a1,a2,a3)); \
                src1+=src_stride1;\
                src2+=src_stride2;\
                src3+=src_stride3;\
                src4+=src_stride4;\
                dst+=dst_stride;\
        } while(--h); \
} \
\
static inline void OPNAME ## _no_rnd_pixels16_l4_aligned(uint8_t *dst, const uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *src4,int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
        do { \
                uint32_t a0,a1,a2,a3; \
                UNPACK(a0,a1,LPC(src1),LPC(src2)); \
                UNPACK(a2,a3,LPC(src3),LPC(src4)); \
                OP(LP(dst),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LPC(src1+4),LPC(src2+4)); \
                UNPACK(a2,a3,LPC(src3+4),LPC(src4+4)); \
                OP(LP(dst+4),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LPC(src1+8),LPC(src2+8)); \
                UNPACK(a2,a3,LPC(src3+8),LPC(src4+8)); \
                OP(LP(dst+8),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LPC(src1+12),LPC(src2+12)); \
                UNPACK(a2,a3,LPC(src3+12),LPC(src4+12)); \
                OP(LP(dst+12),no_rnd_PACK(a0,a1,a2,a3)); \
                src1+=src_stride1;\
                src2+=src_stride2;\
                src3+=src_stride3;\
                src4+=src_stride4;\
                dst+=dst_stride;\
        } while(--h); \
} \
\
static inline void OPNAME ## _pixels16_l4_aligned0(uint8_t *dst, const uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *src4,int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
        do { /* src1 is unaligned */\
                uint32_t a0,a1,a2,a3; \
                UNPACK(a0,a1,AV_RN32(src1),LPC(src2)); \
                UNPACK(a2,a3,LPC(src3),LPC(src4)); \
                OP(LP(dst),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,AV_RN32(src1+4),LPC(src2+4)); \
                UNPACK(a2,a3,LPC(src3+4),LPC(src4+4)); \
                OP(LP(dst+8),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,AV_RN32(src1+8),LPC(src2+8)); \
                UNPACK(a2,a3,LPC(src3+8),LPC(src4+8)); \
                OP(LP(dst+8),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,AV_RN32(src1+12),LPC(src2+12)); \
                UNPACK(a2,a3,LPC(src3+12),LPC(src4+12)); \
                OP(LP(dst+12),rnd_PACK(a0,a1,a2,a3)); \
                src1+=src_stride1;\
                src2+=src_stride2;\
                src3+=src_stride3;\
                src4+=src_stride4;\
                dst+=dst_stride;\
        } while(--h); \
} \
\
static inline void OPNAME ## _no_rnd_pixels16_l4_aligned0(uint8_t *dst, const uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *src4,int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
        do { \
                uint32_t a0,a1,a2,a3; \
                UNPACK(a0,a1,AV_RN32(src1),LPC(src2)); \
                UNPACK(a2,a3,LPC(src3),LPC(src4)); \
                OP(LP(dst),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,AV_RN32(src1+4),LPC(src2+4)); \
                UNPACK(a2,a3,LPC(src3+4),LPC(src4+4)); \
                OP(LP(dst+4),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,AV_RN32(src1+8),LPC(src2+8)); \
                UNPACK(a2,a3,LPC(src3+8),LPC(src4+8)); \
                OP(LP(dst+8),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,AV_RN32(src1+12),LPC(src2+12)); \
                UNPACK(a2,a3,LPC(src3+12),LPC(src4+12)); \
                OP(LP(dst+12),no_rnd_PACK(a0,a1,a2,a3)); \
                src1+=src_stride1;\
                src2+=src_stride2;\
                src3+=src_stride3;\
                src4+=src_stride4;\
                dst+=dst_stride;\
        } while(--h); \
} \
\

#define op_avg(a, b) a = rnd_avg32(a,b)
#define op_put(a, b) a = b

PIXOP2(avg, op_avg)
PIXOP2(put, op_put)
#undef op_avg
#undef op_put

#define avg2(a,b) ((a+b+1)>>1)
#define avg4(a,b,c,d) ((a+b+c+d+2)>>2)


static void gmc1_c(uint8_t *dst, uint8_t *src, int stride, int h, int x16, int y16, int rounder)
{
    const int A=(16-x16)*(16-y16);
    const int B=(   x16)*(16-y16);
    const int C=(16-x16)*(   y16);
    const int D=(   x16)*(   y16);

    do {
        int t0,t1,t2,t3;
        uint8_t *s0 = src;
        uint8_t *s1 = src+stride;
        t0 = *s0++; t2 = *s1++;
        t1 = *s0++; t3 = *s1++;
        dst[0]= (A*t0 + B*t1 + C*t2 + D*t3 + rounder)>>8;
        t0 = *s0++; t2 = *s1++;
        dst[1]= (A*t1 + B*t0 + C*t3 + D*t2 + rounder)>>8;
        t1 = *s0++; t3 = *s1++;
        dst[2]= (A*t0 + B*t1 + C*t2 + D*t3 + rounder)>>8;
        t0 = *s0++; t2 = *s1++;
        dst[3]= (A*t1 + B*t0 + C*t3 + D*t2 + rounder)>>8;
        t1 = *s0++; t3 = *s1++;
        dst[4]= (A*t0 + B*t1 + C*t2 + D*t3 + rounder)>>8;
        t0 = *s0++; t2 = *s1++;
        dst[5]= (A*t1 + B*t0 + C*t3 + D*t2 + rounder)>>8;
        t1 = *s0++; t3 = *s1++;
        dst[6]= (A*t0 + B*t1 + C*t2 + D*t3 + rounder)>>8;
        t0 = *s0++; t2 = *s1++;
        dst[7]= (A*t1 + B*t0 + C*t3 + D*t2 + rounder)>>8;
        dst+= stride;
        src+= stride;
    }while(--h);
}

#define QPEL_MC(r, OPNAME, RND, OP) \
static void OPNAME ## mpeg4_qpel8_h_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){\
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;\
    do {\
        uint8_t *s = src; \
        int src0,src1,src2,src3,src4,src5,src6,src7,src8;\
        src0= *s++;\
        src1= *s++;\
        src2= *s++;\
        src3= *s++;\
        src4= *s++;\
        OP(dst[0], (src0+src1)*20 - (src0+src2)*6 + (src1+src3)*3 - (src2+src4));\
        src5= *s++;\
        OP(dst[1], (src1+src2)*20 - (src0+src3)*6 + (src0+src4)*3 - (src1+src5));\
        src6= *s++;\
        OP(dst[2], (src2+src3)*20 - (src1+src4)*6 + (src0+src5)*3 - (src0+src6));\
        src7= *s++;\
        OP(dst[3], (src3+src4)*20 - (src2+src5)*6 + (src1+src6)*3 - (src0+src7));\
        src8= *s++;\
        OP(dst[4], (src4+src5)*20 - (src3+src6)*6 + (src2+src7)*3 - (src1+src8));\
        OP(dst[5], (src5+src6)*20 - (src4+src7)*6 + (src3+src8)*3 - (src2+src8));\
        OP(dst[6], (src6+src7)*20 - (src5+src8)*6 + (src4+src8)*3 - (src3+src7));\
        OP(dst[7], (src7+src8)*20 - (src6+src8)*6 + (src5+src7)*3 - (src4+src6));\
        dst+=dstStride;\
        src+=srcStride;\
    }while(--h);\
}\
\
static void OPNAME ## mpeg4_qpel8_v_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;\
    int w=8;\
    do{\
        uint8_t *s = src, *d=dst;\
        int src0,src1,src2,src3,src4,src5,src6,src7,src8;\
        src0 = *s; s+=srcStride; \
        src1 = *s; s+=srcStride; \
        src2 = *s; s+=srcStride; \
        src3 = *s; s+=srcStride; \
        src4 = *s; s+=srcStride; \
        OP(*d, (src0+src1)*20 - (src0+src2)*6 + (src1+src3)*3 - (src2+src4));d+=dstStride;\
        src5 = *s; s+=srcStride; \
        OP(*d, (src1+src2)*20 - (src0+src3)*6 + (src0+src4)*3 - (src1+src5));d+=dstStride;\
        src6 = *s; s+=srcStride; \
        OP(*d, (src2+src3)*20 - (src1+src4)*6 + (src0+src5)*3 - (src0+src6));d+=dstStride;\
        src7 = *s; s+=srcStride; \
        OP(*d, (src3+src4)*20 - (src2+src5)*6 + (src1+src6)*3 - (src0+src7));d+=dstStride;\
        src8 = *s; \
        OP(*d, (src4+src5)*20 - (src3+src6)*6 + (src2+src7)*3 - (src1+src8));d+=dstStride;\
        OP(*d, (src5+src6)*20 - (src4+src7)*6 + (src3+src8)*3 - (src2+src8));d+=dstStride;\
        OP(*d, (src6+src7)*20 - (src5+src8)*6 + (src4+src8)*3 - (src3+src7));d+=dstStride;\
        OP(*d, (src7+src8)*20 - (src6+src8)*6 + (src5+src7)*3 - (src4+src6));\
        dst++;\
        src++;\
    }while(--w);\
}\
\
static void OPNAME ## mpeg4_qpel16_h_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){\
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;\
    do {\
        uint8_t *s = src;\
        int src0,src1,src2,src3,src4,src5,src6,src7,src8;\
        int src9,src10,src11,src12,src13,src14,src15,src16;\
        src0= *s++;\
        src1= *s++;\
        src2= *s++;\
        src3= *s++;\
        src4= *s++;\
        OP(dst[ 0], (src0 +src1 )*20 - (src0 +src2 )*6 + (src1 +src3 )*3 - (src2 +src4 ));\
        src5= *s++;\
        OP(dst[ 1], (src1 +src2 )*20 - (src0 +src3 )*6 + (src0 +src4 )*3 - (src1 +src5 ));\
        src6= *s++;\
        OP(dst[ 2], (src2 +src3 )*20 - (src1 +src4 )*6 + (src0 +src5 )*3 - (src0 +src6 ));\
        src7= *s++;\
        OP(dst[ 3], (src3 +src4 )*20 - (src2 +src5 )*6 + (src1 +src6 )*3 - (src0 +src7 ));\
        src8= *s++;\
        OP(dst[ 4], (src4 +src5 )*20 - (src3 +src6 )*6 + (src2 +src7 )*3 - (src1 +src8 ));\
        src9= *s++;\
        OP(dst[ 5], (src5 +src6 )*20 - (src4 +src7 )*6 + (src3 +src8 )*3 - (src2 +src9 ));\
        src10= *s++;\
        OP(dst[ 6], (src6 +src7 )*20 - (src5 +src8 )*6 + (src4 +src9 )*3 - (src3 +src10));\
        src11= *s++;\
        OP(dst[ 7], (src7 +src8 )*20 - (src6 +src9 )*6 + (src5 +src10)*3 - (src4 +src11));\
        src12= *s++;\
        OP(dst[ 8], (src8 +src9 )*20 - (src7 +src10)*6 + (src6 +src11)*3 - (src5 +src12));\
        src13= *s++;\
        OP(dst[ 9], (src9 +src10)*20 - (src8 +src11)*6 + (src7 +src12)*3 - (src6 +src13));\
        src14= *s++;\
        OP(dst[10], (src10+src11)*20 - (src9 +src12)*6 + (src8 +src13)*3 - (src7 +src14));\
        src15= *s++;\
        OP(dst[11], (src11+src12)*20 - (src10+src13)*6 + (src9 +src14)*3 - (src8 +src15));\
        src16= *s++;\
        OP(dst[12], (src12+src13)*20 - (src11+src14)*6 + (src10+src15)*3 - (src9 +src16));\
        OP(dst[13], (src13+src14)*20 - (src12+src15)*6 + (src11+src16)*3 - (src10+src16));\
        OP(dst[14], (src14+src15)*20 - (src13+src16)*6 + (src12+src16)*3 - (src11+src15));\
        OP(dst[15], (src15+src16)*20 - (src14+src16)*6 + (src13+src15)*3 - (src12+src14));\
        dst+=dstStride;\
        src+=srcStride;\
    }while(--h);\
}\
\
static void OPNAME ## mpeg4_qpel16_v_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;\
    int w=16;\
    do {\
        uint8_t *s = src, *d=dst;\
        int src0,src1,src2,src3,src4,src5,src6,src7,src8;\
        int src9,src10,src11,src12,src13,src14,src15,src16;\
        src0 = *s; s+=srcStride; \
        src1 = *s; s+=srcStride; \
        src2 = *s; s+=srcStride; \
        src3 = *s; s+=srcStride; \
        src4 = *s; s+=srcStride; \
        OP(*d, (src0 +src1 )*20 - (src0 +src2 )*6 + (src1 +src3 )*3 - (src2 +src4 ));d+=dstStride;\
        src5 = *s; s+=srcStride; \
        OP(*d, (src1 +src2 )*20 - (src0 +src3 )*6 + (src0 +src4 )*3 - (src1 +src5 ));d+=dstStride;\
        src6 = *s; s+=srcStride; \
        OP(*d, (src2 +src3 )*20 - (src1 +src4 )*6 + (src0 +src5 )*3 - (src0 +src6 ));d+=dstStride;\
        src7 = *s; s+=srcStride; \
        OP(*d, (src3 +src4 )*20 - (src2 +src5 )*6 + (src1 +src6 )*3 - (src0 +src7 ));d+=dstStride;\
        src8 = *s; s+=srcStride; \
        OP(*d, (src4 +src5 )*20 - (src3 +src6 )*6 + (src2 +src7 )*3 - (src1 +src8 ));d+=dstStride;\
        src9 = *s; s+=srcStride; \
        OP(*d, (src5 +src6 )*20 - (src4 +src7 )*6 + (src3 +src8 )*3 - (src2 +src9 ));d+=dstStride;\
        src10 = *s; s+=srcStride; \
        OP(*d, (src6 +src7 )*20 - (src5 +src8 )*6 + (src4 +src9 )*3 - (src3 +src10));d+=dstStride;\
        src11 = *s; s+=srcStride; \
        OP(*d, (src7 +src8 )*20 - (src6 +src9 )*6 + (src5 +src10)*3 - (src4 +src11));d+=dstStride;\
        src12 = *s; s+=srcStride; \
        OP(*d, (src8 +src9 )*20 - (src7 +src10)*6 + (src6 +src11)*3 - (src5 +src12));d+=dstStride;\
        src13 = *s; s+=srcStride; \
        OP(*d, (src9 +src10)*20 - (src8 +src11)*6 + (src7 +src12)*3 - (src6 +src13));d+=dstStride;\
        src14 = *s; s+=srcStride; \
        OP(*d, (src10+src11)*20 - (src9 +src12)*6 + (src8 +src13)*3 - (src7 +src14));d+=dstStride;\
        src15 = *s; s+=srcStride; \
        OP(*d, (src11+src12)*20 - (src10+src13)*6 + (src9 +src14)*3 - (src8 +src15));d+=dstStride;\
        src16 = *s; \
        OP(*d, (src12+src13)*20 - (src11+src14)*6 + (src10+src15)*3 - (src9 +src16));d+=dstStride;\
        OP(*d, (src13+src14)*20 - (src12+src15)*6 + (src11+src16)*3 - (src10+src16));d+=dstStride;\
        OP(*d, (src14+src15)*20 - (src13+src16)*6 + (src12+src16)*3 - (src11+src15));d+=dstStride;\
        OP(*d, (src15+src16)*20 - (src14+src16)*6 + (src13+src15)*3 - (src12+src14));\
        dst++;\
        src++;\
    }while(--w);\
}\
\
static void OPNAME ## qpel8_mc00_sh4 (uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels8_c(dst, src, stride, 8);\
}\
\
static void OPNAME ## qpel8_mc10_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t half[64];\
    put ## RND ## mpeg4_qpel8_h_lowpass(half, src, 8, stride, 8);\
    OPNAME ## pixels8_l2_aligned2(dst, src, half, stride, stride, 8, 8);\
}\
\
static void OPNAME ## qpel8_mc20_sh4(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## mpeg4_qpel8_h_lowpass(dst, src, stride, stride, 8);\
}\
\
static void OPNAME ## qpel8_mc30_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t half[64];\
    put ## RND ## mpeg4_qpel8_h_lowpass(half, src, 8, stride, 8);\
    OPNAME ## pixels8_l2_aligned2(dst, src+1, half, stride, stride, 8, 8);\
}\
\
static void OPNAME ## qpel8_mc01_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t half[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(half, full, 8, 16);\
    OPNAME ## pixels8_l2_aligned(dst, full, half, stride, 16, 8, 8);\
}\
\
static void OPNAME ## qpel8_mc02_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    copy_block9(full, src, 16, stride, 9);\
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, full, stride, 16);\
}\
\
static void OPNAME ## qpel8_mc03_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t half[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(half, full, 8, 16);\
    OPNAME ## pixels8_l2_aligned(dst, full+16, half, stride, 16, 8, 8);\
}\
static void OPNAME ## qpel8_mc11_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## pixels8_l2_aligned(halfH, halfH, full, 8, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfH, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc31_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## pixels8_l2_aligned1(halfH, halfH, full+1, 8, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfH, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc13_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## pixels8_l2_aligned(halfH, halfH, full, 8, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfH+8, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc33_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## pixels8_l2_aligned1(halfH, halfH, full+1, 8, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfH+8, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc21_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t halfH[72];\
    uint8_t halfHV[64];\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfH, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc23_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t halfH[72];\
    uint8_t halfHV[64];\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfH+8, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc12_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## pixels8_l2_aligned(halfH, halfH, full, 8, 8, 16, 9);\
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, halfH, stride, 8);\
}\
static void OPNAME ## qpel8_mc32_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## pixels8_l2_aligned1(halfH, halfH, full+1, 8, 8, 16, 9);\
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, halfH, stride, 8);\
}\
static void OPNAME ## qpel8_mc22_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t halfH[72];\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);\
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, halfH, stride, 8);\
}\
static void OPNAME ## qpel16_mc00_sh4 (uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels16_c(dst, src, stride, 16);\
}\
\
static void OPNAME ## qpel16_mc10_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t half[256];\
    put ## RND ## mpeg4_qpel16_h_lowpass(half, src, 16, stride, 16);\
    OPNAME ## pixels16_l2_aligned2(dst, src, half, stride, stride, 16, 16);\
}\
\
static void OPNAME ## qpel16_mc20_sh4(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## mpeg4_qpel16_h_lowpass(dst, src, stride, stride, 16);\
}\
\
static void OPNAME ## qpel16_mc30_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t half[256];\
    put ## RND ## mpeg4_qpel16_h_lowpass(half, src, 16, stride, 16);\
    OPNAME ## pixels16_l2_aligned2(dst, src+1, half, stride, stride, 16, 16);\
}\
\
static void OPNAME ## qpel16_mc01_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t half[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(half, full, 16, 24);\
    OPNAME ## pixels16_l2_aligned(dst, full, half, stride, 24, 16, 16);\
}\
\
static void OPNAME ## qpel16_mc02_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    copy_block17(full, src, 24, stride, 17);\
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, full, stride, 24);\
}\
\
static void OPNAME ## qpel16_mc03_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t half[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(half, full, 16, 24);\
    OPNAME ## pixels16_l2_aligned(dst, full+24, half, stride, 24, 16, 16);\
}\
static void OPNAME ## qpel16_mc11_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## pixels16_l2_aligned(halfH, halfH, full, 16, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfH, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc31_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## pixels16_l2_aligned1(halfH, halfH, full+1, 16, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfH, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc13_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## pixels16_l2_aligned(halfH, halfH, full, 16, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfH+16, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc33_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## pixels16_l2_aligned1(halfH, halfH, full+1, 16, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfH+16, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc21_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t halfH[272];\
    uint8_t halfHV[256];\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfH, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc23_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t halfH[272];\
    uint8_t halfHV[256];\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfH+16, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc12_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## pixels16_l2_aligned(halfH, halfH, full, 16, 16, 24, 17);\
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, halfH, stride, 16);\
}\
static void OPNAME ## qpel16_mc32_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## pixels16_l2_aligned1(halfH, halfH, full+1, 16, 16, 24, 17);\
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, halfH, stride, 16);\
}\
static void OPNAME ## qpel16_mc22_sh4(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t halfH[272];\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);\
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, halfH, stride, 16);\
}

#define op_avg(a, b) a = (((a)+cm[((b) + 16)>>5]+1)>>1)
#define op_avg_no_rnd(a, b) a = (((a)+cm[((b) + 15)>>5])>>1)
#define op_put(a, b) a = cm[((b) + 16)>>5]
#define op_put_no_rnd(a, b) a = cm[((b) + 15)>>5]

QPEL_MC(0, put_       , _       , op_put)
QPEL_MC(1, put_no_rnd_, _no_rnd_, op_put_no_rnd)
QPEL_MC(0, avg_       , _       , op_avg)
//QPEL_MC(1, avg_no_rnd , _       , op_avg)
#undef op_avg
#undef op_avg_no_rnd
#undef op_put
#undef op_put_no_rnd

static void wmv2_mspel8_h_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    do{
        int src_1,src0,src1,src2,src3,src4,src5,src6,src7,src8,src9;
        uint8_t *s = src;
        src_1 = s[-1];
        src0 = *s++;
        src1 = *s++;
        src2 = *s++;
        dst[0]= cm[(9*(src0 + src1) - (src_1 + src2) + 8)>>4];
        src3 = *s++;
        dst[1]= cm[(9*(src1 + src2) - (src0 + src3) + 8)>>4];
        src4 = *s++;
        dst[2]= cm[(9*(src2 + src3) - (src1 + src4) + 8)>>4];
        src5 = *s++;
        dst[3]= cm[(9*(src3 + src4) - (src2 + src5) + 8)>>4];
        src6 = *s++;
        dst[4]= cm[(9*(src4 + src5) - (src3 + src6) + 8)>>4];
        src7 = *s++;
        dst[5]= cm[(9*(src5 + src6) - (src4 + src7) + 8)>>4];
        src8 = *s++;
        dst[6]= cm[(9*(src6 + src7) - (src5 + src8) + 8)>>4];
        src9 = *s++;
        dst[7]= cm[(9*(src7 + src8) - (src6 + src9) + 8)>>4];
        dst+=dstStride;
        src+=srcStride;
    }while(--h);
}

static void wmv2_mspel8_v_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int w){
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    do{
        int src_1,src0,src1,src2,src3,src4,src5,src6,src7,src8,src9;
        uint8_t *s = src,*d = dst;
        src_1 = *(s-srcStride);
        src0 = *s; s+=srcStride;
        src1 = *s; s+=srcStride;
        src2 = *s; s+=srcStride;
        *d= cm[(9*(src0 + src1) - (src_1 + src2) + 8)>>4]; d+=dstStride;
        src3 = *s; s+=srcStride;
        *d= cm[(9*(src1 + src2) - (src0  + src3) + 8)>>4]; d+=dstStride;
        src4 = *s; s+=srcStride;
        *d= cm[(9*(src2 + src3) - (src1  + src4) + 8)>>4]; d+=dstStride;
        src5 = *s; s+=srcStride;
        *d= cm[(9*(src3 + src4) - (src2  + src5) + 8)>>4]; d+=dstStride;
        src6 = *s; s+=srcStride;
        *d= cm[(9*(src4 + src5) - (src3  + src6) + 8)>>4]; d+=dstStride;
        src7 = *s; s+=srcStride;
        *d= cm[(9*(src5 + src6) - (src4  + src7) + 8)>>4]; d+=dstStride;
        src8 = *s; s+=srcStride;
        *d= cm[(9*(src6 + src7) - (src5  + src8) + 8)>>4]; d+=dstStride;
        src9 = *s;
        *d= cm[(9*(src7 + src8) - (src6  + src9) + 8)>>4]; d+=dstStride;
        src++;
        dst++;
    }while(--w);
}

static void put_mspel8_mc00_sh4 (uint8_t *dst, uint8_t *src, int stride){
    put_pixels8_c(dst, src, stride, 8);
}

static void put_mspel8_mc10_sh4(uint8_t *dst, uint8_t *src, int stride){
    uint8_t half[64];
    wmv2_mspel8_h_lowpass(half, src, 8, stride, 8);
    put_pixels8_l2_aligned2(dst, src, half, stride, stride, 8, 8);
}

static void put_mspel8_mc20_sh4(uint8_t *dst, uint8_t *src, int stride){
    wmv2_mspel8_h_lowpass(dst, src, stride, stride, 8);
}

static void put_mspel8_mc30_sh4(uint8_t *dst, uint8_t *src, int stride){
    uint8_t half[64];
    wmv2_mspel8_h_lowpass(half, src, 8, stride, 8);
    put_pixels8_l2_aligned2(dst, src+1, half, stride, stride, 8, 8);
}

static void put_mspel8_mc02_sh4(uint8_t *dst, uint8_t *src, int stride){
    wmv2_mspel8_v_lowpass(dst, src, stride, stride, 8);
}

static void put_mspel8_mc12_sh4(uint8_t *dst, uint8_t *src, int stride){
    uint8_t halfH[88];
    uint8_t halfV[64];
    uint8_t halfHV[64];
    wmv2_mspel8_h_lowpass(halfH, src-stride, 8, stride, 11);
    wmv2_mspel8_v_lowpass(halfV, src, 8, stride, 8);
    wmv2_mspel8_v_lowpass(halfHV, halfH+8, 8, 8, 8);
    put_pixels8_l2_aligned(dst, halfV, halfHV, stride, 8, 8, 8);
}
static void put_mspel8_mc32_sh4(uint8_t *dst, uint8_t *src, int stride){
    uint8_t halfH[88];
    uint8_t halfV[64];
    uint8_t halfHV[64];
    wmv2_mspel8_h_lowpass(halfH, src-stride, 8, stride, 11);
    wmv2_mspel8_v_lowpass(halfV, src+1, 8, stride, 8);
    wmv2_mspel8_v_lowpass(halfHV, halfH+8, 8, 8, 8);
    put_pixels8_l2_aligned(dst, halfV, halfHV, stride, 8, 8, 8);
}
static void put_mspel8_mc22_sh4(uint8_t *dst, uint8_t *src, int stride){
    uint8_t halfH[88];
    wmv2_mspel8_h_lowpass(halfH, src-stride, 8, stride, 11);
    wmv2_mspel8_v_lowpass(dst, halfH+8, stride, 8, 8);
}
