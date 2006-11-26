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

#define         LD(adr) *(uint32_t*)(adr)

#define PIXOP2(OPNAME, OP) \
/*static inline void OPNAME ## _no_rnd_pixels8_l2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),no_rnd_avg32(LD32(src1  ),LD32(src2  )) ); \
                OP(LP(dst+4),no_rnd_avg32(LD32(src1+4),LD32(src2+4)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _pixels8_l2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(LD32(src1  ),LD32(src2  )) ); \
                OP(LP(dst+4),rnd_avg32(LD32(src1+4),LD32(src2+4)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _pixels4_l2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(LD32(src1  ),LD32(src2  )) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _no_rnd_pixels16_l2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),no_rnd_avg32(LD32(src1  ),LD32(src2  )) ); \
                OP(LP(dst+4),no_rnd_avg32(LD32(src1+4),LD32(src2+4)) ); \
                OP(LP(dst+8),no_rnd_avg32(LD32(src1+8),LD32(src2+8)) ); \
                OP(LP(dst+12),no_rnd_avg32(LD32(src1+12),LD32(src2+12)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _pixels16_l2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(LD32(src1  ),LD32(src2  )) ); \
                OP(LP(dst+4),rnd_avg32(LD32(src1+4),LD32(src2+4)) ); \
                OP(LP(dst+8),rnd_avg32(LD32(src1+8),LD32(src2+8)) ); \
                OP(LP(dst+12),rnd_avg32(LD32(src1+12),LD32(src2+12)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}*/\
\
static inline void OPNAME ## _pixels4_l2_aligned(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(LP(src1  ),LP(src2  )) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _pixels4_l2_aligned2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(LD32(src1  ),LP(src2  )) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _no_rnd_pixels16_l2_aligned2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),no_rnd_avg32(LD32(src1  ),LP(src2  )) ); \
                OP(LP(dst+4),no_rnd_avg32(LD32(src1+4),LP(src2+4)) ); \
                OP(LP(dst+8),no_rnd_avg32(LD32(src1+8),LP(src2+8)) ); \
                OP(LP(dst+12),no_rnd_avg32(LD32(src1+12),LP(src2+12)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _pixels16_l2_aligned2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(LD32(src1  ),LP(src2  )) ); \
                OP(LP(dst+4),rnd_avg32(LD32(src1+4),LP(src2+4)) ); \
                OP(LP(dst+8),rnd_avg32(LD32(src1+8),LP(src2+8)) ); \
                OP(LP(dst+12),rnd_avg32(LD32(src1+12),LP(src2+12)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _no_rnd_pixels8_l2_aligned2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do { /* onlye src2 aligned */\
                OP(LP(dst  ),no_rnd_avg32(LD32(src1  ),LP(src2  )) ); \
                OP(LP(dst+4),no_rnd_avg32(LD32(src1+4),LP(src2+4)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _pixels8_l2_aligned2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(LD32(src1  ),LP(src2  )) ); \
                OP(LP(dst+4),rnd_avg32(LD32(src1+4),LP(src2+4)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _no_rnd_pixels8_l2_aligned(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),no_rnd_avg32(LP(src1  ),LP(src2  )) ); \
                OP(LP(dst+4),no_rnd_avg32(LP(src1+4),LP(src2+4)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _pixels8_l2_aligned(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(LP(src1  ),LP(src2  )) ); \
                OP(LP(dst+4),rnd_avg32(LP(src1+4),LP(src2+4)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _no_rnd_pixels16_l2_aligned(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),no_rnd_avg32(LP(src1  ),LP(src2  )) ); \
                OP(LP(dst+4),no_rnd_avg32(LP(src1+4),LP(src2+4)) ); \
                OP(LP(dst+8),no_rnd_avg32(LP(src1+8),LP(src2+8)) ); \
                OP(LP(dst+12),no_rnd_avg32(LP(src1+12),LP(src2+12)) ); \
                src1+=src_stride1; \
                src2+=src_stride2; \
                dst+=dst_stride; \
        } while(--h); \
}\
\
static inline void OPNAME ## _pixels16_l2_aligned(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2, int h) \
{\
        do {\
                OP(LP(dst  ),rnd_avg32(LP(src1  ),LP(src2  )) ); \
                OP(LP(dst+4),rnd_avg32(LP(src1+4),LP(src2+4)) ); \
                OP(LP(dst+8),rnd_avg32(LP(src1+8),LP(src2+8)) ); \
                OP(LP(dst+12),rnd_avg32(LP(src1+12),LP(src2+12)) ); \
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
                UNPACK(a0,a1,LP(src1),LP(src2)); \
                UNPACK(a2,a3,LP(src3),LP(src4)); \
                OP(LP(dst),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LP(src1+4),LP(src2+4)); \
                UNPACK(a2,a3,LP(src3+4),LP(src4+4)); \
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
                UNPACK(a0,a1,LP(src1),LP(src2)); \
                UNPACK(a2,a3,LP(src3),LP(src4)); \
                OP(LP(dst),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LP(src1+4),LP(src2+4)); \
                UNPACK(a2,a3,LP(src3+4),LP(src4+4)); \
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
                UNPACK(a0,a1,LD32(src1),LP(src2)); \
                UNPACK(a2,a3,LP(src3),LP(src4)); \
                OP(LP(dst),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LD32(src1+4),LP(src2+4)); \
                UNPACK(a2,a3,LP(src3+4),LP(src4+4)); \
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
                UNPACK(a0,a1,LD32(src1),LP(src2)); \
                UNPACK(a2,a3,LP(src3),LP(src4)); \
                OP(LP(dst),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LD32(src1+4),LP(src2+4)); \
                UNPACK(a2,a3,LP(src3+4),LP(src4+4)); \
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
                UNPACK(a0,a1,LP(src1),LP(src2)); \
                UNPACK(a2,a3,LP(src3),LP(src4)); \
                OP(LP(dst),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LP(src1+4),LP(src2+4)); \
                UNPACK(a2,a3,LP(src3+4),LP(src4+4)); \
                OP(LP(dst+8),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LP(src1+8),LP(src2+8)); \
                UNPACK(a2,a3,LP(src3+8),LP(src4+8)); \
                OP(LP(dst+8),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LP(src1+12),LP(src2+12)); \
                UNPACK(a2,a3,LP(src3+12),LP(src4+12)); \
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
                UNPACK(a0,a1,LP(src1),LP(src2)); \
                UNPACK(a2,a3,LP(src3),LP(src4)); \
                OP(LP(dst),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LP(src1+4),LP(src2+4)); \
                UNPACK(a2,a3,LP(src3+4),LP(src4+4)); \
                OP(LP(dst+4),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LP(src1+8),LP(src2+8)); \
                UNPACK(a2,a3,LP(src3+8),LP(src4+8)); \
                OP(LP(dst+8),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LP(src1+12),LP(src2+12)); \
                UNPACK(a2,a3,LP(src3+12),LP(src4+12)); \
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
                UNPACK(a0,a1,LD32(src1),LP(src2)); \
                UNPACK(a2,a3,LP(src3),LP(src4)); \
                OP(LP(dst),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LD32(src1+4),LP(src2+4)); \
                UNPACK(a2,a3,LP(src3+4),LP(src4+4)); \
                OP(LP(dst+8),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LD32(src1+8),LP(src2+8)); \
                UNPACK(a2,a3,LP(src3+8),LP(src4+8)); \
                OP(LP(dst+8),rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LD32(src1+12),LP(src2+12)); \
                UNPACK(a2,a3,LP(src3+12),LP(src4+12)); \
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
                UNPACK(a0,a1,LD32(src1),LP(src2)); \
                UNPACK(a2,a3,LP(src3),LP(src4)); \
                OP(LP(dst),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LD32(src1+4),LP(src2+4)); \
                UNPACK(a2,a3,LP(src3+4),LP(src4+4)); \
                OP(LP(dst+4),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LD32(src1+8),LP(src2+8)); \
                UNPACK(a2,a3,LP(src3+8),LP(src4+8)); \
                OP(LP(dst+8),no_rnd_PACK(a0,a1,a2,a3)); \
                UNPACK(a0,a1,LD32(src1+12),LP(src2+12)); \
                UNPACK(a2,a3,LP(src3+12),LP(src4+12)); \
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

static void gmc_c(uint8_t *dst, uint8_t *src, int stride, int h, int ox, int oy,
                  int dxx, int dxy, int dyx, int dyy, int shift, int r, int width, int height)
{
    int y, vx, vy;
    const int s= 1<<shift;

    width--;
    height--;

    for(y=0; y<h; y++){
        int x;

        vx= ox;
        vy= oy;
        for(x=0; x<8; x++){ //XXX FIXME optimize
            int src_x, src_y, frac_x, frac_y, index;

            src_x= vx>>16;
            src_y= vy>>16;
            frac_x= src_x&(s-1);
            frac_y= src_y&(s-1);
            src_x>>=shift;
            src_y>>=shift;

            if((unsigned)src_x < width){
                if((unsigned)src_y < height){
                    index= src_x + src_y*stride;
                    dst[y*stride + x]= (  (  src[index         ]*(s-frac_x)
                                           + src[index       +1]*   frac_x )*(s-frac_y)
                                        + (  src[index+stride  ]*(s-frac_x)
                                           + src[index+stride+1]*   frac_x )*   frac_y
                                        + r)>>(shift*2);
                }else{
                    index= src_x + clip(src_y, 0, height)*stride;
                    dst[y*stride + x]= ( (  src[index         ]*(s-frac_x)
                                          + src[index       +1]*   frac_x )*s
                                        + r)>>(shift*2);
                }
            }else{
                if((unsigned)src_y < height){
                    index= clip(src_x, 0, width) + src_y*stride;
                    dst[y*stride + x]= (  (  src[index         ]*(s-frac_y)
                                           + src[index+stride  ]*   frac_y )*s
                                        + r)>>(shift*2);
                }else{
                    index= clip(src_x, 0, width) + clip(src_y, 0, height)*stride;
                    dst[y*stride + x]=    src[index         ];
                }
            }

            vx+= dxx;
            vy+= dyx;
        }
        ox += dxy;
        oy += dyy;
    }
}
#define H264_CHROMA_MC(OPNAME, OP)\
static void OPNAME ## h264_chroma_mc2_c(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y){\
    const int A=(8-x)*(8-y);\
    const int B=(  x)*(8-y);\
    const int C=(8-x)*(  y);\
    const int D=(  x)*(  y);\
    \
    assert(x<8 && y<8 && x>=0 && y>=0);\
\
    do {\
        int t0,t1,t2,t3; \
        uint8_t *s0 = src; \
        uint8_t *s1 = src+stride; \
        t0 = *s0++; t2 = *s1++; \
        t1 = *s0++; t3 = *s1++; \
        OP(dst[0], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[1], (A*t1 + B*t0 + C*t3 + D*t2));\
        dst+= stride;\
        src+= stride;\
    }while(--h);\
}\
\
static void OPNAME ## h264_chroma_mc4_c(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y){\
    const int A=(8-x)*(8-y);\
    const int B=(  x)*(8-y);\
    const int C=(8-x)*(  y);\
    const int D=(  x)*(  y);\
    \
    assert(x<8 && y<8 && x>=0 && y>=0);\
\
    do {\
        int t0,t1,t2,t3; \
        uint8_t *s0 = src; \
        uint8_t *s1 = src+stride; \
        t0 = *s0++; t2 = *s1++; \
        t1 = *s0++; t3 = *s1++; \
        OP(dst[0], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[1], (A*t1 + B*t0 + C*t3 + D*t2));\
        t1 = *s0++; t3 = *s1++; \
        OP(dst[2], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[3], (A*t1 + B*t0 + C*t3 + D*t2));\
        dst+= stride;\
        src+= stride;\
    }while(--h);\
}\
\
static void OPNAME ## h264_chroma_mc8_c(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y){\
    const int A=(8-x)*(8-y);\
    const int B=(  x)*(8-y);\
    const int C=(8-x)*(  y);\
    const int D=(  x)*(  y);\
    \
    assert(x<8 && y<8 && x>=0 && y>=0);\
\
    do {\
        int t0,t1,t2,t3; \
        uint8_t *s0 = src; \
        uint8_t *s1 = src+stride; \
        t0 = *s0++; t2 = *s1++; \
        t1 = *s0++; t3 = *s1++; \
        OP(dst[0], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[1], (A*t1 + B*t0 + C*t3 + D*t2));\
        t1 = *s0++; t3 = *s1++; \
        OP(dst[2], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[3], (A*t1 + B*t0 + C*t3 + D*t2));\
        t1 = *s0++; t3 = *s1++; \
        OP(dst[4], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[5], (A*t1 + B*t0 + C*t3 + D*t2));\
        t1 = *s0++; t3 = *s1++; \
        OP(dst[6], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[7], (A*t1 + B*t0 + C*t3 + D*t2));\
        dst+= stride;\
        src+= stride;\
    }while(--h);\
}

#define op_avg(a, b) a = (((a)+(((b) + 32)>>6)+1)>>1)
#define op_put(a, b) a = (((b) + 32)>>6)

H264_CHROMA_MC(put_       , op_put)
H264_CHROMA_MC(avg_       , op_avg)
#undef op_avg
#undef op_put

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
static void OPNAME ## qpel8_mc00_c (uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels8_c(dst, src, stride, 8);\
}\
\
static void OPNAME ## qpel8_mc10_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t half[64];\
    put ## RND ## mpeg4_qpel8_h_lowpass(half, src, 8, stride, 8);\
    OPNAME ## pixels8_l2_aligned2(dst, src, half, stride, stride, 8, 8);\
}\
\
static void OPNAME ## qpel8_mc20_c(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## mpeg4_qpel8_h_lowpass(dst, src, stride, stride, 8);\
}\
\
static void OPNAME ## qpel8_mc30_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t half[64];\
    put ## RND ## mpeg4_qpel8_h_lowpass(half, src, 8, stride, 8);\
    OPNAME ## pixels8_l2_aligned2(dst, src+1, half, stride, stride, 8, 8);\
}\
\
static void OPNAME ## qpel8_mc01_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t half[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(half, full, 8, 16);\
    OPNAME ## pixels8_l2_aligned(dst, full, half, stride, 16, 8, 8);\
}\
\
static void OPNAME ## qpel8_mc02_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    copy_block9(full, src, 16, stride, 9);\
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, full, stride, 16);\
}\
\
static void OPNAME ## qpel8_mc03_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t half[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(half, full, 8, 16);\
    OPNAME ## pixels8_l2_aligned(dst, full+16, half, stride, 16, 8, 8);\
}\
static void ff_ ## OPNAME ## qpel8_mc11_old_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfV[64];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full, 8, 16);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l4_aligned(dst, full, halfH, halfV, halfHV, stride, 16, 8, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc11_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## pixels8_l2_aligned(halfH, halfH, full, 8, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfH, halfHV, stride, 8, 8, 8);\
}\
static void ff_ ## OPNAME ## qpel8_mc31_old_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfV[64];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full+1, 8, 16);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l4_aligned0(dst, full+1, halfH, halfV, halfHV, stride, 16, 8, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc31_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## pixels8_l2_aligned1(halfH, halfH, full+1, 8, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfH, halfHV, stride, 8, 8, 8);\
}\
static void ff_ ## OPNAME ## qpel8_mc13_old_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfV[64];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full, 8, 16);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l4_aligned(dst, full+16, halfH+8, halfV, halfHV, stride, 16, 8, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc13_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## pixels8_l2_aligned(halfH, halfH, full, 8, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfH+8, halfHV, stride, 8, 8, 8);\
}\
static void ff_ ## OPNAME ## qpel8_mc33_old_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfV[64];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full  , 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full+1, 8, 16);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l4_aligned0(dst, full+17, halfH+8, halfV, halfHV, stride, 16, 8, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc33_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## pixels8_l2_aligned1(halfH, halfH, full+1, 8, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfH+8, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc21_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t halfH[72];\
    uint8_t halfHV[64];\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfH, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc23_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t halfH[72];\
    uint8_t halfHV[64];\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfH+8, halfHV, stride, 8, 8, 8);\
}\
static void ff_ ## OPNAME ## qpel8_mc12_old_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfV[64];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full, 8, 16);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfV, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc12_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## pixels8_l2_aligned(halfH, halfH, full, 8, 8, 16, 9);\
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, halfH, stride, 8);\
}\
static void ff_ ## OPNAME ## qpel8_mc32_old_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    uint8_t halfV[64];\
    uint8_t halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full+1, 8, 16);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_aligned(dst, halfV, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc32_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[16*9];\
    uint8_t halfH[72];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## pixels8_l2_aligned1(halfH, halfH, full+1, 8, 8, 16, 9);\
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, halfH, stride, 8);\
}\
static void OPNAME ## qpel8_mc22_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t halfH[72];\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);\
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, halfH, stride, 8);\
}\
static void OPNAME ## qpel16_mc00_c (uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels16_c(dst, src, stride, 16);\
}\
\
static void OPNAME ## qpel16_mc10_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t half[256];\
    put ## RND ## mpeg4_qpel16_h_lowpass(half, src, 16, stride, 16);\
    OPNAME ## pixels16_l2_aligned2(dst, src, half, stride, stride, 16, 16);\
}\
\
static void OPNAME ## qpel16_mc20_c(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## mpeg4_qpel16_h_lowpass(dst, src, stride, stride, 16);\
}\
\
static void OPNAME ## qpel16_mc30_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t half[256];\
    put ## RND ## mpeg4_qpel16_h_lowpass(half, src, 16, stride, 16);\
    OPNAME ## pixels16_l2_aligned2(dst, src+1, half, stride, stride, 16, 16);\
}\
\
static void OPNAME ## qpel16_mc01_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t half[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(half, full, 16, 24);\
    OPNAME ## pixels16_l2_aligned(dst, full, half, stride, 24, 16, 16);\
}\
\
static void OPNAME ## qpel16_mc02_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    copy_block17(full, src, 24, stride, 17);\
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, full, stride, 24);\
}\
\
static void OPNAME ## qpel16_mc03_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t half[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(half, full, 16, 24);\
    OPNAME ## pixels16_l2_aligned(dst, full+24, half, stride, 24, 16, 16);\
}\
static void ff_ ## OPNAME ## qpel16_mc11_old_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfV[256];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full, 16, 24);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l4_aligned(dst, full, halfH, halfV, halfHV, stride, 24, 16, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc11_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## pixels16_l2_aligned(halfH, halfH, full, 16, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfH, halfHV, stride, 16, 16, 16);\
}\
static void ff_ ## OPNAME ## qpel16_mc31_old_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfV[256];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full+1, 16, 24);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l4_aligned0(dst, full+1, halfH, halfV, halfHV, stride, 24, 16, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc31_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## pixels16_l2_aligned1(halfH, halfH, full+1, 16, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfH, halfHV, stride, 16, 16, 16);\
}\
static void ff_ ## OPNAME ## qpel16_mc13_old_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfV[256];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full, 16, 24);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l4_aligned(dst, full+24, halfH+16, halfV, halfHV, stride, 24, 16, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc13_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## pixels16_l2_aligned(halfH, halfH, full, 16, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfH+16, halfHV, stride, 16, 16, 16);\
}\
static void ff_ ## OPNAME ## qpel16_mc33_old_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfV[256];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full  , 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full+1, 16, 24);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l4_aligned0(dst, full+25, halfH+16, halfV, halfHV, stride, 24, 16, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc33_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## pixels16_l2_aligned1(halfH, halfH, full+1, 16, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfH+16, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc21_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t halfH[272];\
    uint8_t halfHV[256];\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfH, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc23_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t halfH[272];\
    uint8_t halfHV[256];\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfH+16, halfHV, stride, 16, 16, 16);\
}\
static void ff_ ## OPNAME ## qpel16_mc12_old_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfV[256];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full, 16, 24);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfV, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc12_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## pixels16_l2_aligned(halfH, halfH, full, 16, 16, 24, 17);\
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, halfH, stride, 16);\
}\
static void ff_ ## OPNAME ## qpel16_mc32_old_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    uint8_t halfV[256];\
    uint8_t halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full+1, 16, 24);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_aligned(dst, halfV, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc32_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[24*17];\
    uint8_t halfH[272];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## pixels16_l2_aligned1(halfH, halfH, full+1, 16, 16, 24, 17);\
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, halfH, stride, 16);\
}\
static void OPNAME ## qpel16_mc22_c(uint8_t *dst, uint8_t *src, int stride){\
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

#if 1
#define H264_LOWPASS(OPNAME, OP, OP2) \
static inline void OPNAME ## h264_qpel_h_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride,int w,int h){\
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;\
    do {\
        int srcB,srcA,src0,src1,src2,src3,src4,src5,src6;\
        uint8_t *s = src-2;\
        srcB = *s++;\
        srcA = *s++;\
        src0 = *s++;\
        src1 = *s++;\
        src2 = *s++;\
        src3 = *s++;\
        OP(dst[0], (src0+src1)*20 - (srcA+src2)*5 + (srcB+src3));\
        src4 = *s++;\
        OP(dst[1], (src1+src2)*20 - (src0+src3)*5 + (srcA+src4));\
        src5 = *s++;\
        OP(dst[2], (src2+src3)*20 - (src1+src4)*5 + (src0+src5));\
        src6 = *s++;\
        OP(dst[3], (src3+src4)*20 - (src2+src5)*5 + (src1+src6));\
      if (w>4) { /* it optimized */ \
        int src7,src8,src9,src10; \
        src7 = *s++;\
        OP(dst[4], (src4+src5)*20 - (src3+src6)*5 + (src2+src7));\
        src8 = *s++;\
        OP(dst[5], (src5+src6)*20 - (src4+src7)*5 + (src3+src8));\
        src9 = *s++;\
        OP(dst[6], (src6+src7)*20 - (src5+src8)*5 + (src4+src9));\
        src10 = *s++;\
        OP(dst[7], (src7+src8)*20 - (src6+src9)*5 + (src5+src10));\
       if (w>8) { \
        int src11,src12,src13,src14,src15,src16,src17,src18; \
        src11 = *s++;\
        OP(dst[8] , (src8 +src9 )*20 - (src7 +src10)*5 + (src6 +src11));\
        src12 = *s++;\
        OP(dst[9] , (src9 +src10)*20 - (src8 +src11)*5 + (src7 +src12));\
        src13 = *s++;\
        OP(dst[10], (src10+src11)*20 - (src9 +src12)*5 + (src8 +src13));\
        src14 = *s++;\
        OP(dst[11], (src11+src12)*20 - (src10+src13)*5 + (src9 +src14));\
        src15 = *s++;\
        OP(dst[12], (src12+src13)*20 - (src11+src14)*5 + (src10+src15));\
        src16 = *s++;\
        OP(dst[13], (src13+src14)*20 - (src12+src15)*5 + (src11+src16));\
        src17 = *s++;\
        OP(dst[14], (src14+src15)*20 - (src13+src16)*5 + (src12+src17));\
        src18 = *s++;\
        OP(dst[15], (src15+src16)*20 - (src14+src17)*5 + (src13+src18));\
       } \
      } \
        dst+=dstStride;\
        src+=srcStride;\
    }while(--h);\
}\
\
static inline void OPNAME ## h264_qpel_v_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride,int w,int h){\
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;\
    do{\
        int srcB,srcA,src0,src1,src2,src3,src4,src5,src6;\
        uint8_t *s = src-2*srcStride,*d=dst;\
        srcB = *s; s+=srcStride;\
        srcA = *s; s+=srcStride;\
        src0 = *s; s+=srcStride;\
        src1 = *s; s+=srcStride;\
        src2 = *s; s+=srcStride;\
        src3 = *s; s+=srcStride;\
        OP(*d, (src0+src1)*20 - (srcA+src2)*5 + (srcB+src3));d+=dstStride;\
        src4 = *s; s+=srcStride;\
        OP(*d, (src1+src2)*20 - (src0+src3)*5 + (srcA+src4));d+=dstStride;\
        src5 = *s; s+=srcStride;\
        OP(*d, (src2+src3)*20 - (src1+src4)*5 + (src0+src5));d+=dstStride;\
        src6 = *s; s+=srcStride;\
        OP(*d, (src3+src4)*20 - (src2+src5)*5 + (src1+src6));d+=dstStride;\
      if (h>4) { \
        int src7,src8,src9,src10; \
        src7 = *s; s+=srcStride;\
        OP(*d, (src4+src5)*20 - (src3+src6)*5 + (src2+src7));d+=dstStride;\
        src8 = *s; s+=srcStride;\
        OP(*d, (src5+src6)*20 - (src4+src7)*5 + (src3+src8));d+=dstStride;\
        src9 = *s; s+=srcStride;\
        OP(*d, (src6+src7)*20 - (src5+src8)*5 + (src4+src9));d+=dstStride;\
        src10 = *s; s+=srcStride;\
        OP(*d, (src7+src8)*20 - (src6+src9)*5 + (src5+src10));d+=dstStride;\
       if (h>8) { \
        int src11,src12,src13,src14,src15,src16,src17,src18; \
        src11 = *s; s+=srcStride;\
        OP(*d , (src8 +src9 )*20 - (src7 +src10)*5 + (src6 +src11));d+=dstStride;\
        src12 = *s; s+=srcStride;\
        OP(*d , (src9 +src10)*20 - (src8 +src11)*5 + (src7 +src12));d+=dstStride;\
        src13 = *s; s+=srcStride;\
        OP(*d, (src10+src11)*20 - (src9 +src12)*5 + (src8 +src13));d+=dstStride;\
        src14 = *s; s+=srcStride;\
        OP(*d, (src11+src12)*20 - (src10+src13)*5 + (src9 +src14));d+=dstStride;\
        src15 = *s; s+=srcStride;\
        OP(*d, (src12+src13)*20 - (src11+src14)*5 + (src10+src15));d+=dstStride;\
        src16 = *s; s+=srcStride;\
        OP(*d, (src13+src14)*20 - (src12+src15)*5 + (src11+src16));d+=dstStride;\
        src17 = *s; s+=srcStride;\
        OP(*d, (src14+src15)*20 - (src13+src16)*5 + (src12+src17));d+=dstStride;\
        src18 = *s; s+=srcStride;\
        OP(*d, (src15+src16)*20 - (src14+src17)*5 + (src13+src18));d+=dstStride;\
       } \
      } \
        dst++;\
        src++;\
    }while(--w);\
}\
\
static inline void OPNAME ## h264_qpel_hv_lowpass(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride,int w,int h){\
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;\
    int i;\
    src -= 2*srcStride;\
    i= h+5; \
    do {\
        int srcB,srcA,src0,src1,src2,src3,src4,src5,src6;\
        uint8_t *s = src-2;\
        srcB = *s++;\
        srcA = *s++;\
        src0 = *s++;\
        src1 = *s++;\
        src2 = *s++;\
        src3 = *s++;\
        tmp[0] = ((src0+src1)*20 - (srcA+src2)*5 + (srcB+src3));\
        src4 = *s++;\
        tmp[1] = ((src1+src2)*20 - (src0+src3)*5 + (srcA+src4));\
        src5 = *s++;\
        tmp[2] = ((src2+src3)*20 - (src1+src4)*5 + (src0+src5));\
        src6 = *s++;\
        tmp[3] = ((src3+src4)*20 - (src2+src5)*5 + (src1+src6));\
      if (w>4) { /* it optimized */ \
        int src7,src8,src9,src10; \
        src7 = *s++;\
        tmp[4] = ((src4+src5)*20 - (src3+src6)*5 + (src2+src7));\
        src8 = *s++;\
        tmp[5] = ((src5+src6)*20 - (src4+src7)*5 + (src3+src8));\
        src9 = *s++;\
        tmp[6] = ((src6+src7)*20 - (src5+src8)*5 + (src4+src9));\
        src10 = *s++;\
        tmp[7] = ((src7+src8)*20 - (src6+src9)*5 + (src5+src10));\
       if (w>8) { \
        int src11,src12,src13,src14,src15,src16,src17,src18; \
        src11 = *s++;\
        tmp[8] = ((src8 +src9 )*20 - (src7 +src10)*5 + (src6 +src11));\
        src12 = *s++;\
        tmp[9] = ((src9 +src10)*20 - (src8 +src11)*5 + (src7 +src12));\
        src13 = *s++;\
        tmp[10] = ((src10+src11)*20 - (src9 +src12)*5 + (src8 +src13));\
        src14 = *s++;\
        tmp[11] = ((src11+src12)*20 - (src10+src13)*5 + (src9 +src14));\
        src15 = *s++;\
        tmp[12] = ((src12+src13)*20 - (src11+src14)*5 + (src10+src15));\
        src16 = *s++;\
        tmp[13] = ((src13+src14)*20 - (src12+src15)*5 + (src11+src16));\
        src17 = *s++;\
        tmp[14] = ((src14+src15)*20 - (src13+src16)*5 + (src12+src17));\
        src18 = *s++;\
        tmp[15] = ((src15+src16)*20 - (src14+src17)*5 + (src13+src18));\
       } \
      } \
        tmp+=tmpStride;\
        src+=srcStride;\
    }while(--i);\
    tmp -= tmpStride*(h+5-2);\
    i = w; \
    do {\
        int tmpB,tmpA,tmp0,tmp1,tmp2,tmp3,tmp4,tmp5,tmp6;\
        int16_t *s = tmp-2*tmpStride; \
        uint8_t *d=dst;\
        tmpB = *s; s+=tmpStride;\
        tmpA = *s; s+=tmpStride;\
        tmp0 = *s; s+=tmpStride;\
        tmp1 = *s; s+=tmpStride;\
        tmp2 = *s; s+=tmpStride;\
        tmp3 = *s; s+=tmpStride;\
        OP2(*d, (tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3));d+=dstStride;\
        tmp4 = *s; s+=tmpStride;\
        OP2(*d, (tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4));d+=dstStride;\
        tmp5 = *s; s+=tmpStride;\
        OP2(*d, (tmp2+tmp3)*20 - (tmp1+tmp4)*5 + (tmp0+tmp5));d+=dstStride;\
        tmp6 = *s; s+=tmpStride;\
        OP2(*d, (tmp3+tmp4)*20 - (tmp2+tmp5)*5 + (tmp1+tmp6));d+=dstStride;\
      if (h>4) { \
        int tmp7,tmp8,tmp9,tmp10; \
        tmp7 = *s; s+=tmpStride;\
        OP2(*d, (tmp4+tmp5)*20 - (tmp3+tmp6)*5 + (tmp2+tmp7));d+=dstStride;\
        tmp8 = *s; s+=tmpStride;\
        OP2(*d, (tmp5+tmp6)*20 - (tmp4+tmp7)*5 + (tmp3+tmp8));d+=dstStride;\
        tmp9 = *s; s+=tmpStride;\
        OP2(*d, (tmp6+tmp7)*20 - (tmp5+tmp8)*5 + (tmp4+tmp9));d+=dstStride;\
        tmp10 = *s; s+=tmpStride;\
        OP2(*d, (tmp7+tmp8)*20 - (tmp6+tmp9)*5 + (tmp5+tmp10));d+=dstStride;\
       if (h>8) { \
        int tmp11,tmp12,tmp13,tmp14,tmp15,tmp16,tmp17,tmp18; \
        tmp11 = *s; s+=tmpStride;\
        OP2(*d , (tmp8 +tmp9 )*20 - (tmp7 +tmp10)*5 + (tmp6 +tmp11));d+=dstStride;\
        tmp12 = *s; s+=tmpStride;\
        OP2(*d , (tmp9 +tmp10)*20 - (tmp8 +tmp11)*5 + (tmp7 +tmp12));d+=dstStride;\
        tmp13 = *s; s+=tmpStride;\
        OP2(*d, (tmp10+tmp11)*20 - (tmp9 +tmp12)*5 + (tmp8 +tmp13));d+=dstStride;\
        tmp14 = *s; s+=tmpStride;\
        OP2(*d, (tmp11+tmp12)*20 - (tmp10+tmp13)*5 + (tmp9 +tmp14));d+=dstStride;\
        tmp15 = *s; s+=tmpStride;\
        OP2(*d, (tmp12+tmp13)*20 - (tmp11+tmp14)*5 + (tmp10+tmp15));d+=dstStride;\
        tmp16 = *s; s+=tmpStride;\
        OP2(*d, (tmp13+tmp14)*20 - (tmp12+tmp15)*5 + (tmp11+tmp16));d+=dstStride;\
        tmp17 = *s; s+=tmpStride;\
        OP2(*d, (tmp14+tmp15)*20 - (tmp13+tmp16)*5 + (tmp12+tmp17));d+=dstStride;\
        tmp18 = *s; s+=tmpStride;\
        OP2(*d, (tmp15+tmp16)*20 - (tmp14+tmp17)*5 + (tmp13+tmp18));d+=dstStride;\
       } \
      } \
        dst++;\
        tmp++;\
    }while(--i);\
}\
\
static void OPNAME ## h264_qpel4_h_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel_h_lowpass(dst,src,dstStride,srcStride,4,4); \
}\
static void OPNAME ## h264_qpel8_h_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
   OPNAME ## h264_qpel_h_lowpass(dst,src,dstStride,srcStride,8,8); \
}\
static void OPNAME ## h264_qpel16_h_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
   OPNAME ## h264_qpel_h_lowpass(dst,src,dstStride,srcStride,16,16); \
}\
\
static void OPNAME ## h264_qpel4_v_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
   OPNAME ## h264_qpel_v_lowpass(dst,src,dstStride,srcStride,4,4); \
}\
static void OPNAME ## h264_qpel8_v_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
   OPNAME ## h264_qpel_v_lowpass(dst,src,dstStride,srcStride,8,8); \
}\
static void OPNAME ## h264_qpel16_v_lowpass(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
   OPNAME ## h264_qpel_v_lowpass(dst,src,dstStride,srcStride,16,16); \
}\
static void OPNAME ## h264_qpel4_hv_lowpass(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
   OPNAME ## h264_qpel_hv_lowpass(dst,tmp,src,dstStride,tmpStride,srcStride,4,4); \
}\
static void OPNAME ## h264_qpel8_hv_lowpass(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
   OPNAME ## h264_qpel_hv_lowpass(dst,tmp,src,dstStride,tmpStride,srcStride,8,8); \
}\
static void OPNAME ## h264_qpel16_hv_lowpass(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
   OPNAME ## h264_qpel_hv_lowpass(dst,tmp,src,dstStride,tmpStride,srcStride,16,16); \
}\

#define H264_MC(OPNAME, SIZE) \
static void OPNAME ## h264_qpel ## SIZE ## _mc00_c (uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels ## SIZE ## _c(dst, src, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc10_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t half[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_aligned2(dst, src, half, stride, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc20_c(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc30_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t half[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_aligned2(dst, src+1, half, stride, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc01_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid= full + SIZE*2;\
    uint8_t half[SIZE*SIZE];\
    copy_block ## SIZE (full, src - stride*2, SIZE,  stride, SIZE + 5);\
    put_h264_qpel ## SIZE ## _v_lowpass(half, full_mid, SIZE, SIZE);\
    OPNAME ## pixels ## SIZE ## _l2_aligned(dst, full_mid, half, stride, SIZE, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc02_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid= full + SIZE*2;\
    copy_block ## SIZE (full, src - stride*2, SIZE,  stride, SIZE + 5);\
    OPNAME ## h264_qpel ## SIZE ## _v_lowpass(dst, full_mid, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc03_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid= full + SIZE*2;\
    uint8_t half[SIZE*SIZE];\
    copy_block ## SIZE (full, src - stride*2, SIZE,  stride, SIZE + 5);\
    put_h264_qpel ## SIZE ## _v_lowpass(half, full_mid, SIZE, SIZE);\
    OPNAME ## pixels ## SIZE ## _l2_aligned(dst, full_mid+SIZE, half, stride, SIZE, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc11_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid= full + SIZE*2;\
    uint8_t halfH[SIZE*SIZE];\
    uint8_t halfV[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass(halfH, src, SIZE, stride);\
    copy_block ## SIZE (full, src - stride*2, SIZE,  stride, SIZE + 5);\
    put_h264_qpel ## SIZE ## _v_lowpass(halfV, full_mid, SIZE, SIZE);\
    OPNAME ## pixels ## SIZE ## _l2_aligned(dst, halfH, halfV, stride, SIZE, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc31_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid= full + SIZE*2;\
    uint8_t halfH[SIZE*SIZE];\
    uint8_t halfV[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass(halfH, src, SIZE, stride);\
    copy_block ## SIZE (full, src - stride*2 + 1, SIZE,  stride, SIZE + 5);\
    put_h264_qpel ## SIZE ## _v_lowpass(halfV, full_mid, SIZE, SIZE);\
    OPNAME ## pixels ## SIZE ## _l2_aligned(dst, halfH, halfV, stride, SIZE, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc13_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid= full + SIZE*2;\
    uint8_t halfH[SIZE*SIZE];\
    uint8_t halfV[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass(halfH, src + stride, SIZE, stride);\
    copy_block ## SIZE (full, src - stride*2, SIZE,  stride, SIZE + 5);\
    put_h264_qpel ## SIZE ## _v_lowpass(halfV, full_mid, SIZE, SIZE);\
    OPNAME ## pixels ## SIZE ## _l2_aligned(dst, halfH, halfV, stride, SIZE, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc33_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid= full + SIZE*2;\
    uint8_t halfH[SIZE*SIZE];\
    uint8_t halfV[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass(halfH, src + stride, SIZE, stride);\
    copy_block ## SIZE (full, src - stride*2 + 1, SIZE,  stride, SIZE + 5);\
    put_h264_qpel ## SIZE ## _v_lowpass(halfV, full_mid, SIZE, SIZE);\
    OPNAME ## pixels ## SIZE ## _l2_aligned(dst, halfH, halfV, stride, SIZE, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc22_c(uint8_t *dst, uint8_t *src, int stride){\
    int16_t tmp[SIZE*(SIZE+5)];\
    OPNAME ## h264_qpel ## SIZE ## _hv_lowpass(dst, tmp, src, stride, SIZE, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc21_c(uint8_t *dst, uint8_t *src, int stride){\
    int16_t tmp[SIZE*(SIZE+5)];\
    uint8_t halfH[SIZE*SIZE];\
    uint8_t halfHV[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass(halfH, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_aligned(dst, halfH, halfHV, stride, SIZE, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc23_c(uint8_t *dst, uint8_t *src, int stride){\
    int16_t tmp[SIZE*(SIZE+5)];\
    uint8_t halfH[SIZE*SIZE];\
    uint8_t halfHV[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass(halfH, src + stride, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_aligned(dst, halfH, halfHV, stride, SIZE, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc12_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid= full + SIZE*2;\
    int16_t tmp[SIZE*(SIZE+5)];\
    uint8_t halfV[SIZE*SIZE];\
    uint8_t halfHV[SIZE*SIZE];\
    copy_block ## SIZE (full, src - stride*2, SIZE,  stride, SIZE + 5);\
    put_h264_qpel ## SIZE ## _v_lowpass(halfV, full_mid, SIZE, SIZE);\
    put_h264_qpel ## SIZE ## _hv_lowpass(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_aligned(dst, halfV, halfHV, stride, SIZE, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc32_c(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)];\
    uint8_t * const full_mid= full + SIZE*2;\
    int16_t tmp[SIZE*(SIZE+5)];\
    uint8_t halfV[SIZE*SIZE];\
    uint8_t halfHV[SIZE*SIZE];\
    copy_block ## SIZE (full, src - stride*2 + 1, SIZE,  stride, SIZE + 5);\
    put_h264_qpel ## SIZE ## _v_lowpass(halfV, full_mid, SIZE, SIZE);\
    put_h264_qpel ## SIZE ## _hv_lowpass(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_aligned(dst, halfV, halfHV, stride, SIZE, SIZE, SIZE);\
}\

#define op_avg(a, b)  a = (((a)+cm[((b) + 16)>>5]+1)>>1)
//#define op_avg2(a, b) a = (((a)*w1+cm[((b) + 16)>>5]*w2 + o + 64)>>7)
#define op_put(a, b)  a = cm[((b) + 16)>>5]
#define op2_avg(a, b)  a = (((a)+cm[((b) + 512)>>10]+1)>>1)
#define op2_put(a, b)  a = cm[((b) + 512)>>10]

H264_LOWPASS(put_       , op_put, op2_put)
H264_LOWPASS(avg_       , op_avg, op2_avg)
H264_MC(put_, 4)
H264_MC(put_, 8)
H264_MC(put_, 16)
H264_MC(avg_, 4)
H264_MC(avg_, 8)
H264_MC(avg_, 16)

#undef op_avg
#undef op_put
#undef op2_avg
#undef op2_put
#endif

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

static void put_mspel8_mc00_c (uint8_t *dst, uint8_t *src, int stride){
    put_pixels8_c(dst, src, stride, 8);
}

static void put_mspel8_mc10_c(uint8_t *dst, uint8_t *src, int stride){
    uint8_t half[64];
    wmv2_mspel8_h_lowpass(half, src, 8, stride, 8);
    put_pixels8_l2_aligned2(dst, src, half, stride, stride, 8, 8);
}

static void put_mspel8_mc20_c(uint8_t *dst, uint8_t *src, int stride){
    wmv2_mspel8_h_lowpass(dst, src, stride, stride, 8);
}

static void put_mspel8_mc30_c(uint8_t *dst, uint8_t *src, int stride){
    uint8_t half[64];
    wmv2_mspel8_h_lowpass(half, src, 8, stride, 8);
    put_pixels8_l2_aligned2(dst, src+1, half, stride, stride, 8, 8);
}

static void put_mspel8_mc02_c(uint8_t *dst, uint8_t *src, int stride){
    wmv2_mspel8_v_lowpass(dst, src, stride, stride, 8);
}

static void put_mspel8_mc12_c(uint8_t *dst, uint8_t *src, int stride){
    uint8_t halfH[88];
    uint8_t halfV[64];
    uint8_t halfHV[64];
    wmv2_mspel8_h_lowpass(halfH, src-stride, 8, stride, 11);
    wmv2_mspel8_v_lowpass(halfV, src, 8, stride, 8);
    wmv2_mspel8_v_lowpass(halfHV, halfH+8, 8, 8, 8);
    put_pixels8_l2_aligned(dst, halfV, halfHV, stride, 8, 8, 8);
}
static void put_mspel8_mc32_c(uint8_t *dst, uint8_t *src, int stride){
    uint8_t halfH[88];
    uint8_t halfV[64];
    uint8_t halfHV[64];
    wmv2_mspel8_h_lowpass(halfH, src-stride, 8, stride, 11);
    wmv2_mspel8_v_lowpass(halfV, src+1, 8, stride, 8);
    wmv2_mspel8_v_lowpass(halfHV, halfH+8, 8, 8, 8);
    put_pixels8_l2_aligned(dst, halfV, halfHV, stride, 8, 8, 8);
}
static void put_mspel8_mc22_c(uint8_t *dst, uint8_t *src, int stride){
    uint8_t halfH[88];
    wmv2_mspel8_h_lowpass(halfH, src-stride, 8, stride, 11);
    wmv2_mspel8_v_lowpass(dst, halfH+8, stride, 8, 8);
}
