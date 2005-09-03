/*
 * Copyright (c) 2004-2005 Michael Niedermayer, Loren Merritt
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


/***********************************/
/* IDCT */

/* in/out: mma=mma+mmb, mmb=mmb-mma */
#define SUMSUB_BA( a, b ) \
    "paddw "#b", "#a" \n\t"\
    "paddw "#b", "#b" \n\t"\
    "psubw "#a", "#b" \n\t"

#define SUMSUB_BADC( a, b, c, d ) \
    "paddw "#b", "#a" \n\t"\
    "paddw "#d", "#c" \n\t"\
    "paddw "#b", "#b" \n\t"\
    "paddw "#d", "#d" \n\t"\
    "psubw "#a", "#b" \n\t"\
    "psubw "#c", "#d" \n\t"

#define SUMSUBD2_AB( a, b, t ) \
    "movq  "#b", "#t" \n\t"\
    "psraw  $1 , "#b" \n\t"\
    "paddw "#a", "#b" \n\t"\
    "psraw  $1 , "#a" \n\t"\
    "psubw "#t", "#a" \n\t"

#define IDCT4_1D( s02, s13, d02, d13, t ) \
    SUMSUB_BA  ( s02, d02 )\
    SUMSUBD2_AB( s13, d13, t )\
    SUMSUB_BADC( d13, s02, s13, d02 )

#define SBUTTERFLY(a,b,t,n)\
    "movq " #a ", " #t "		\n\t" /* abcd */\
    "punpckl" #n " " #b ", " #a "	\n\t" /* aebf */\
    "punpckh" #n " " #b ", " #t "	\n\t" /* cgdh */\

#define TRANSPOSE4(a,b,c,d,t)\
    SBUTTERFLY(a,b,t,wd) /* a=aebf t=cgdh */\
    SBUTTERFLY(c,d,b,wd) /* c=imjn b=kolp */\
    SBUTTERFLY(a,c,d,dq) /* a=aeim d=bfjn */\
    SBUTTERFLY(t,b,c,dq) /* t=cgko c=dhlp */

#define STORE_DIFF_4P( p, t, z ) \
    "psraw      $6,     "#p" \n\t"\
    "movd       (%0),   "#t" \n\t"\
    "punpcklbw "#z",    "#t" \n\t"\
    "paddsw    "#t",    "#p" \n\t"\
    "packuswb  "#z",    "#p" \n\t"\
    "movd      "#p",    (%0) \n\t"

void ff_h264_idct_add_mmx2(uint8_t *dst, int16_t *block, int stride)
{
    /* Load dct coeffs */
    asm volatile(
        "movq   (%0), %%mm0 \n\t"
        "movq  8(%0), %%mm1 \n\t"
        "movq 16(%0), %%mm2 \n\t"
        "movq 24(%0), %%mm3 \n\t"
    :: "r"(block) );

    asm volatile(
        /* mm1=s02+s13  mm2=s02-s13  mm4=d02+d13  mm0=d02-d13 */
        IDCT4_1D( %%mm2, %%mm1, %%mm0, %%mm3, %%mm4 )

        "movq      %0,    %%mm6 \n\t"
        /* in: 1,4,0,2  out: 1,2,3,0 */
        TRANSPOSE4( %%mm3, %%mm1, %%mm0, %%mm2, %%mm4 )

        "paddw     %%mm6, %%mm3 \n\t"

        /* mm2=s02+s13  mm3=s02-s13  mm4=d02+d13  mm1=d02-d13 */
        IDCT4_1D( %%mm4, %%mm2, %%mm3, %%mm0, %%mm1 )

        "pxor %%mm7, %%mm7    \n\t"
    :: "m"(ff_pw_32));

    asm volatile(
    STORE_DIFF_4P( %%mm0, %%mm1, %%mm7)
        "add %1, %0             \n\t"
    STORE_DIFF_4P( %%mm2, %%mm1, %%mm7)
        "add %1, %0             \n\t"
    STORE_DIFF_4P( %%mm3, %%mm1, %%mm7)
        "add %1, %0             \n\t"
    STORE_DIFF_4P( %%mm4, %%mm1, %%mm7)
        : "+r"(dst)
        : "r" ((long)stride)
    );
}


/***********************************/
/* deblocking */

// out: o = |x-y|>a
// clobbers: t
#define DIFF_GT_MMX(x,y,a,o,t)\
    "movq     "#y", "#t"  \n\t"\
    "movq     "#x", "#o"  \n\t"\
    "psubusb  "#x", "#t"  \n\t"\
    "psubusb  "#y", "#o"  \n\t"\
    "por      "#t", "#o"  \n\t"\
    "psubusb  "#a", "#o"  \n\t"

// in: mm0=p1 mm1=p0 mm2=q0 mm3=q1
// out: mm5=beta-1, mm7=mask
// clobbers: mm4,mm6
#define H264_DEBLOCK_MASK(alpha1, beta1) \
    "pshufw $0, "#alpha1", %%mm4 \n\t"\
    "pshufw $0, "#beta1 ", %%mm5 \n\t"\
    "packuswb  %%mm4, %%mm4      \n\t"\
    "packuswb  %%mm5, %%mm5      \n\t"\
    DIFF_GT_MMX(%%mm1, %%mm2, %%mm4, %%mm7, %%mm6) /* |p0-q0| > alpha-1 */\
    DIFF_GT_MMX(%%mm0, %%mm1, %%mm5, %%mm4, %%mm6) /* |p1-p0| > beta-1 */\
    "por       %%mm4, %%mm7      \n\t"\
    DIFF_GT_MMX(%%mm3, %%mm2, %%mm5, %%mm4, %%mm6) /* |q1-q0| > beta-1 */\
    "por       %%mm4, %%mm7      \n\t"\
    "pxor      %%mm6, %%mm6      \n\t"\
    "pcmpeqb   %%mm6, %%mm7      \n\t"

// in: mm0=p1 mm1=p0 mm2=q0 mm3=q1 mm7=(tc&mask)
// out: mm1=p0' mm2=q0'
// clobbers: mm0,3-6
#define H264_DEBLOCK_P0_Q0(pb_01, pb_3f)\
        /* a = q0^p0^((p1-q1)>>2) */\
        "movq    %%mm0, %%mm4  \n\t"\
        "psubb   %%mm3, %%mm4  \n\t"\
        "psrlw   $2,    %%mm4  \n\t"\
        "pxor    %%mm1, %%mm4  \n\t"\
        "pxor    %%mm2, %%mm4  \n\t"\
        /* b = p0^(q1>>2) */\
        "psrlw   $2,    %%mm3  \n\t"\
        "pand "#pb_3f", %%mm3  \n\t"\
        "movq    %%mm1, %%mm5  \n\t"\
        "pxor    %%mm3, %%mm5  \n\t"\
        /* c = q0^(p1>>2) */\
        "psrlw   $2,    %%mm0  \n\t"\
        "pand "#pb_3f", %%mm0  \n\t"\
        "movq    %%mm2, %%mm6  \n\t"\
        "pxor    %%mm0, %%mm6  \n\t"\
        /* d = (c^b) & ~(b^a) & 1 */\
        "pxor    %%mm5, %%mm6  \n\t"\
        "pxor    %%mm4, %%mm5  \n\t"\
        "pandn   %%mm6, %%mm5  \n\t"\
        "pand "#pb_01", %%mm5  \n\t"\
        /* delta = (avg(q0, p1>>2) + (d&a))
         *       - (avg(p0, q1>>2) + (d&~a)) */\
        "pavgb   %%mm2, %%mm0  \n\t"\
        "pand    %%mm5, %%mm4  \n\t"\
        "paddusb %%mm4, %%mm0  \n\t"\
        "pavgb   %%mm1, %%mm3  \n\t"\
        "pxor    %%mm5, %%mm4  \n\t"\
        "paddusb %%mm4, %%mm3  \n\t"\
        /* p0 += clip(delta, -tc0, tc0)
         * q0 -= clip(delta, -tc0, tc0) */\
        "movq    %%mm0, %%mm4  \n\t"\
        "psubusb %%mm3, %%mm0  \n\t"\
        "psubusb %%mm4, %%mm3  \n\t"\
        "pminub  %%mm7, %%mm0  \n\t"\
        "pminub  %%mm7, %%mm3  \n\t"\
        "paddusb %%mm0, %%mm1  \n\t"\
        "paddusb %%mm3, %%mm2  \n\t"\
        "psubusb %%mm3, %%mm1  \n\t"\
        "psubusb %%mm0, %%mm2  \n\t"

// in: mm0=p1 mm1=p0 mm2=q0 mm3=q1 mm7=(tc&mask) %8=mm_bone
// out: (q1addr) = clip( (q2+((p0+q0+1)>>1))>>1, q1-tc0, q1+tc0 )
// clobbers: q2, tmp, tc0
#define H264_DEBLOCK_Q1(p1, q2, q2addr, q1addr, tc0, tmp)\
        "movq     %%mm1,  "#tmp"   \n\t"\
        "pavgb    %%mm2,  "#tmp"   \n\t"\
        "pavgb    "#tmp", "#q2"    \n\t" /* avg(p2,avg(p0,q0)) */\
        "pxor   "q2addr", "#tmp"   \n\t"\
        "pand     %8,     "#tmp"   \n\t" /* (p2^avg(p0,q0))&1 */\
        "psubusb  "#tmp", "#q2"    \n\t" /* (p2+((p0+q0+1)>>1))>>1 */\
        "movq     "#p1",  "#tmp"   \n\t"\
        "psubusb  "#tc0", "#tmp"   \n\t"\
        "paddusb  "#p1",  "#tc0"   \n\t"\
        "pmaxub   "#tmp", "#q2"    \n\t"\
        "pminub   "#tc0", "#q2"    \n\t"\
        "movq     "#q2",  "q1addr" \n\t"

static inline void h264_loop_filter_luma_mmx2(uint8_t *pix, int stride, int alpha1, int beta1, int8_t *tc0)
{
    uint64_t tmp0;
    uint64_t tc = (uint8_t)tc0[1]*0x01010000 | (uint8_t)tc0[0]*0x0101;
    // with luma, tc0=0 doesn't mean no filtering, so we need a separate input mask
    uint32_t mask[2] = { (tc0[0]>=0)*0xffffffff, (tc0[1]>=0)*0xffffffff };

    asm volatile(
        "movq    (%1,%3), %%mm0    \n\t" //p1
        "movq    (%1,%3,2), %%mm1  \n\t" //p0
        "movq    (%2),    %%mm2    \n\t" //q0
        "movq    (%2,%3), %%mm3    \n\t" //q1
        H264_DEBLOCK_MASK(%6, %7)
        "pand     %5,     %%mm7    \n\t"
        "movq     %%mm7,  %0       \n\t"

        /* filter p1 */
        "movq     (%1),   %%mm3    \n\t" //p2
        DIFF_GT_MMX(%%mm1, %%mm3, %%mm5, %%mm6, %%mm4) // |p2-p0|>beta-1
        "pandn    %%mm7,  %%mm6    \n\t"
        "pcmpeqb  %%mm7,  %%mm6    \n\t"
        "pand     %%mm7,  %%mm6    \n\t" // mask & |p2-p0|<beta
        "pshufw  $80, %4, %%mm4    \n\t"
        "pand     %%mm7,  %%mm4    \n\t" // mask & tc0
        "movq     %8,     %%mm7    \n\t"
        "pand     %%mm6,  %%mm7    \n\t" // mask & |p2-p0|<beta & 1
        "pand     %%mm4,  %%mm6    \n\t" // mask & |p2-p0|<beta & tc0
        "paddb    %%mm4,  %%mm7    \n\t" // tc++
        H264_DEBLOCK_Q1(%%mm0, %%mm3, "(%1)", "(%1,%3)", %%mm6, %%mm4)

        /* filter q1 */
        "movq    (%2,%3,2), %%mm4  \n\t" //q2
        DIFF_GT_MMX(%%mm2, %%mm4, %%mm5, %%mm6, %%mm3) // |q2-q0|>beta-1
        "pandn    %0,     %%mm6    \n\t"
        "pcmpeqb  %0,     %%mm6    \n\t"
        "pand     %0,     %%mm6    \n\t"
        "pshufw  $80, %4, %%mm5    \n\t"
        "pand     %%mm6,  %%mm5    \n\t"
        "pand     %8,     %%mm6    \n\t"
        "paddb    %%mm6,  %%mm7    \n\t"
        "movq    (%2,%3), %%mm3    \n\t"
        H264_DEBLOCK_Q1(%%mm3, %%mm4, "(%2,%3,2)", "(%2,%3)", %%mm5, %%mm6)

        /* filter p0, q0 */
        H264_DEBLOCK_P0_Q0(%8, %9)
        "movq      %%mm1, (%1,%3,2) \n\t"
        "movq      %%mm2, (%2)      \n\t"

        : "=m"(tmp0)
        : "r"(pix-3*stride), "r"(pix), "r"((long)stride),
          "m"(tc), "m"(*(uint64_t*)mask), "m"(alpha1), "m"(beta1),
          "m"(mm_bone), "m"(ff_pb_3F)
    );
}

static void h264_v_loop_filter_luma_mmx2(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    if((tc0[0] & tc0[1]) >= 0)
        h264_loop_filter_luma_mmx2(pix, stride, alpha-1, beta-1, tc0);
    if((tc0[2] & tc0[3]) >= 0)
        h264_loop_filter_luma_mmx2(pix+8, stride, alpha-1, beta-1, tc0+2);
}
static void h264_h_loop_filter_luma_mmx2(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    //FIXME: could cut some load/stores by merging transpose with filter
    // also, it only needs to transpose 6x8
    uint8_t trans[8*8];
    int i;
    for(i=0; i<2; i++, pix+=8*stride, tc0+=2) {
        if((tc0[0] & tc0[1]) < 0)
            continue;
        transpose4x4(trans,       pix-4,          8, stride);
        transpose4x4(trans  +4*8, pix,            8, stride);
        transpose4x4(trans+4,     pix-4+4*stride, 8, stride);
        transpose4x4(trans+4+4*8, pix  +4*stride, 8, stride);
        h264_loop_filter_luma_mmx2(trans+4*8, 8, alpha-1, beta-1, tc0);
        transpose4x4(pix-2,          trans  +2*8, stride, 8);
        transpose4x4(pix-2+4*stride, trans+4+2*8, stride, 8);
    }
}

static inline void h264_loop_filter_chroma_mmx2(uint8_t *pix, int stride, int alpha1, int beta1, int8_t *tc0)
{
    asm volatile(
        "movq    (%0),    %%mm0     \n\t" //p1
        "movq    (%0,%2), %%mm1     \n\t" //p0
        "movq    (%1),    %%mm2     \n\t" //q0
        "movq    (%1,%2), %%mm3     \n\t" //q1
        H264_DEBLOCK_MASK(%4, %5)
        "movd      %3,    %%mm6     \n\t"
        "punpcklbw %%mm6, %%mm6     \n\t"
        "pand      %%mm6, %%mm7     \n\t" // mm7 = tc&mask
        H264_DEBLOCK_P0_Q0(%6, %7)
        "movq      %%mm1, (%0,%2)   \n\t"
        "movq      %%mm2, (%1)      \n\t"

        :: "r"(pix-2*stride), "r"(pix), "r"((long)stride),
           "r"(*(uint32_t*)tc0),
           "m"(alpha1), "m"(beta1), "m"(mm_bone), "m"(ff_pb_3F)
    );
}

static void h264_v_loop_filter_chroma_mmx2(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    h264_loop_filter_chroma_mmx2(pix, stride, alpha-1, beta-1, tc0);
}

static void h264_h_loop_filter_chroma_mmx2(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    //FIXME: could cut some load/stores by merging transpose with filter
    uint8_t trans[8*4];
    transpose4x4(trans, pix-2, 8, stride);
    transpose4x4(trans+4, pix-2+4*stride, 8, stride);
    h264_loop_filter_chroma_mmx2(trans+2*8, 8, alpha-1, beta-1, tc0);
    transpose4x4(pix-2, trans, stride, 8);
    transpose4x4(pix-2+4*stride, trans+4, stride, 8);
}

// p0 = (p0 + q1 + 2*p1 + 2) >> 2
#define H264_FILTER_CHROMA4(p0, p1, q1, one) \
    "movq    "#p0", %%mm4  \n\t"\
    "pxor    "#q1", %%mm4  \n\t"\
    "pand   "#one", %%mm4  \n\t" /* mm4 = (p0^q1)&1 */\
    "pavgb   "#q1", "#p0"  \n\t"\
    "psubusb %%mm4, "#p0"  \n\t"\
    "pavgb   "#p1", "#p0"  \n\t" /* dst = avg(p1, avg(p0,q1) - ((p0^q1)&1)) */\

static inline void h264_loop_filter_chroma_intra_mmx2(uint8_t *pix, int stride, int alpha1, int beta1)
{
    asm volatile(
        "movq    (%0),    %%mm0     \n\t"
        "movq    (%0,%2), %%mm1     \n\t"
        "movq    (%1),    %%mm2     \n\t"
        "movq    (%1,%2), %%mm3     \n\t"
        H264_DEBLOCK_MASK(%3, %4)
        "movq    %%mm1,   %%mm5     \n\t"
        "movq    %%mm2,   %%mm6     \n\t"
        H264_FILTER_CHROMA4(%%mm1, %%mm0, %%mm3, %5) //p0'
        H264_FILTER_CHROMA4(%%mm2, %%mm3, %%mm0, %5) //q0'
        "psubb   %%mm5,   %%mm1     \n\t"
        "psubb   %%mm6,   %%mm2     \n\t"
        "pand    %%mm7,   %%mm1     \n\t"
        "pand    %%mm7,   %%mm2     \n\t"
        "paddb   %%mm5,   %%mm1     \n\t"
        "paddb   %%mm6,   %%mm2     \n\t"
        "movq    %%mm1,   (%0,%2)   \n\t"
        "movq    %%mm2,   (%1)      \n\t"
        :: "r"(pix-2*stride), "r"(pix), "r"((long)stride),
           "m"(alpha1), "m"(beta1), "m"(mm_bone)
    );
}

static void h264_v_loop_filter_chroma_intra_mmx2(uint8_t *pix, int stride, int alpha, int beta)
{
    h264_loop_filter_chroma_intra_mmx2(pix, stride, alpha-1, beta-1);
}

static void h264_h_loop_filter_chroma_intra_mmx2(uint8_t *pix, int stride, int alpha, int beta)
{
    //FIXME: could cut some load/stores by merging transpose with filter
    uint8_t trans[8*4];
    transpose4x4(trans, pix-2, 8, stride);
    transpose4x4(trans+4, pix-2+4*stride, 8, stride);
    h264_loop_filter_chroma_intra_mmx2(trans+2*8, 8, alpha-1, beta-1);
    transpose4x4(pix-2, trans, stride, 8);
    transpose4x4(pix-2+4*stride, trans+4, stride, 8);
}


/***********************************/
/* motion compensation */

#define QPEL_H264V(A,B,C,D,E,F,OP)\
        "movd (%0), "#F"		\n\t"\
        "movq "#C", %%mm6		\n\t"\
        "paddw "#D", %%mm6		\n\t"\
        "psllw $2, %%mm6		\n\t"\
        "psubw "#B", %%mm6		\n\t"\
        "psubw "#E", %%mm6		\n\t"\
        "pmullw %4, %%mm6		\n\t"\
        "add %2, %0			\n\t"\
        "punpcklbw %%mm7, "#F"		\n\t"\
        "paddw %5, "#A"			\n\t"\
        "paddw "#F", "#A"		\n\t"\
        "paddw "#A", %%mm6		\n\t"\
        "psraw $5, %%mm6		\n\t"\
        "packuswb %%mm6, %%mm6		\n\t"\
        OP(%%mm6, (%1), A, d)\
        "add %3, %1			\n\t"     

#define QPEL_H264HV(A,B,C,D,E,F,OF)\
        "movd (%0), "#F"		\n\t"\
        "movq "#C", %%mm6		\n\t"\
        "paddw "#D", %%mm6		\n\t"\
        "psllw $2, %%mm6		\n\t"\
        "psubw "#B", %%mm6		\n\t"\
        "psubw "#E", %%mm6		\n\t"\
        "pmullw %3, %%mm6		\n\t"\
        "add %2, %0			\n\t"\
        "punpcklbw %%mm7, "#F"		\n\t"\
        "paddw "#F", "#A"		\n\t"\
        "paddw "#A", %%mm6		\n\t"\
        "movq %%mm6, "#OF"(%1)		\n\t"
        
#define QPEL_H264(OPNAME, OP, MMX)\
static void OPNAME ## h264_qpel4_h_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    int h=4;\
\
    asm volatile(\
        "pxor %%mm7, %%mm7		\n\t"\
        "movq %5, %%mm4			\n\t"\
        "movq %6, %%mm5			\n\t"\
        "1:				\n\t"\
        "movd  -1(%0), %%mm1		\n\t"\
        "movd    (%0), %%mm2		\n\t"\
        "movd   1(%0), %%mm3		\n\t"\
        "movd   2(%0), %%mm0		\n\t"\
        "punpcklbw %%mm7, %%mm1		\n\t"\
        "punpcklbw %%mm7, %%mm2		\n\t"\
        "punpcklbw %%mm7, %%mm3		\n\t"\
        "punpcklbw %%mm7, %%mm0		\n\t"\
        "paddw %%mm0, %%mm1		\n\t"\
        "paddw %%mm3, %%mm2		\n\t"\
        "movd  -2(%0), %%mm0		\n\t"\
        "movd   3(%0), %%mm3		\n\t"\
        "punpcklbw %%mm7, %%mm0		\n\t"\
        "punpcklbw %%mm7, %%mm3		\n\t"\
        "paddw %%mm3, %%mm0		\n\t"\
        "psllw $2, %%mm2		\n\t"\
        "psubw %%mm1, %%mm2		\n\t"\
        "pmullw %%mm4, %%mm2		\n\t"\
        "paddw %%mm5, %%mm0		\n\t"\
        "paddw %%mm2, %%mm0		\n\t"\
        "psraw $5, %%mm0		\n\t"\
        "packuswb %%mm0, %%mm0		\n\t"\
        OP(%%mm0, (%1),%%mm6, d)\
        "add %3, %0			\n\t"\
        "add %4, %1			\n\t"\
        "decl %2			\n\t"\
        " jnz 1b			\n\t"\
        : "+a"(src), "+c"(dst), "+m"(h)\
        : "d"((long)srcStride), "S"((long)dstStride), "m"(ff_pw_5), "m"(ff_pw_16)\
        : "memory"\
    );\
}\
static void OPNAME ## h264_qpel4_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    src -= 2*srcStride;\
    asm volatile(\
        "pxor %%mm7, %%mm7		\n\t"\
        "movd (%0), %%mm0		\n\t"\
        "add %2, %0			\n\t"\
        "movd (%0), %%mm1		\n\t"\
        "add %2, %0			\n\t"\
        "movd (%0), %%mm2		\n\t"\
        "add %2, %0			\n\t"\
        "movd (%0), %%mm3		\n\t"\
        "add %2, %0			\n\t"\
        "movd (%0), %%mm4		\n\t"\
        "add %2, %0			\n\t"\
        "punpcklbw %%mm7, %%mm0		\n\t"\
        "punpcklbw %%mm7, %%mm1		\n\t"\
        "punpcklbw %%mm7, %%mm2		\n\t"\
        "punpcklbw %%mm7, %%mm3		\n\t"\
        "punpcklbw %%mm7, %%mm4		\n\t"\
        QPEL_H264V(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
        QPEL_H264V(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
        QPEL_H264V(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP)\
        QPEL_H264V(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP)\
         \
        : "+a"(src), "+c"(dst)\
        : "S"((long)srcStride), "D"((long)dstStride), "m"(ff_pw_5), "m"(ff_pw_16)\
        : "memory"\
    );\
}\
static void OPNAME ## h264_qpel4_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    int h=4;\
    int w=3;\
    src -= 2*srcStride+2;\
    while(w--){\
        asm volatile(\
            "pxor %%mm7, %%mm7			\n\t"\
            "movd (%0), %%mm0			\n\t"\
            "add %2, %0				\n\t"\
            "movd (%0), %%mm1			\n\t"\
            "add %2, %0				\n\t"\
            "movd (%0), %%mm2			\n\t"\
            "add %2, %0				\n\t"\
            "movd (%0), %%mm3			\n\t"\
            "add %2, %0				\n\t"\
            "movd (%0), %%mm4			\n\t"\
            "add %2, %0				\n\t"\
            "punpcklbw %%mm7, %%mm0		\n\t"\
            "punpcklbw %%mm7, %%mm1		\n\t"\
            "punpcklbw %%mm7, %%mm2		\n\t"\
            "punpcklbw %%mm7, %%mm3		\n\t"\
            "punpcklbw %%mm7, %%mm4		\n\t"\
            QPEL_H264HV(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, 0*8*3)\
            QPEL_H264HV(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, 1*8*3)\
            QPEL_H264HV(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, 2*8*3)\
            QPEL_H264HV(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, 3*8*3)\
             \
            : "+a"(src)\
            : "c"(tmp), "S"((long)srcStride), "m"(ff_pw_5)\
            : "memory"\
        );\
        tmp += 4;\
        src += 4 - 9*srcStride;\
    }\
    tmp -= 3*4;\
    asm volatile(\
        "movq %4, %%mm6			\n\t"\
        "1:				\n\t"\
        "movq     (%0), %%mm0		\n\t"\
        "paddw  10(%0), %%mm0		\n\t"\
        "movq    2(%0), %%mm1		\n\t"\
        "paddw   8(%0), %%mm1		\n\t"\
        "movq    4(%0), %%mm2		\n\t"\
        "paddw   6(%0), %%mm2		\n\t"\
        "psubw %%mm1, %%mm0		\n\t"/*a-b   (abccba)*/\
        "psraw $2, %%mm0		\n\t"/*(a-b)/4 */\
        "psubw %%mm1, %%mm0		\n\t"/*(a-b)/4-b */\
        "paddsw %%mm2, %%mm0		\n\t"\
        "psraw $2, %%mm0		\n\t"/*((a-b)/4-b)/4 */\
        "paddw %%mm6, %%mm2		\n\t"\
        "paddw %%mm2, %%mm0		\n\t"\
        "psraw $6, %%mm0		\n\t"\
        "packuswb %%mm0, %%mm0		\n\t"\
        OP(%%mm0, (%1),%%mm7, d)\
        "add $24, %0			\n\t"\
        "add %3, %1			\n\t"\
        "decl %2			\n\t"\
        " jnz 1b			\n\t"\
        : "+a"(tmp), "+c"(dst), "+m"(h)\
        : "S"((long)dstStride), "m"(ff_pw_32)\
        : "memory"\
    );\
}\
\
static void OPNAME ## h264_qpel8_h_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    int h=8;\
    asm volatile(\
        "pxor %%mm7, %%mm7		\n\t"\
        "movq %5, %%mm6			\n\t"\
        "1:				\n\t"\
        "movq    (%0), %%mm0		\n\t"\
        "movq   1(%0), %%mm2		\n\t"\
        "movq %%mm0, %%mm1		\n\t"\
        "movq %%mm2, %%mm3		\n\t"\
        "punpcklbw %%mm7, %%mm0		\n\t"\
        "punpckhbw %%mm7, %%mm1		\n\t"\
        "punpcklbw %%mm7, %%mm2		\n\t"\
        "punpckhbw %%mm7, %%mm3		\n\t"\
        "paddw %%mm2, %%mm0		\n\t"\
        "paddw %%mm3, %%mm1		\n\t"\
        "psllw $2, %%mm0		\n\t"\
        "psllw $2, %%mm1		\n\t"\
        "movq   -1(%0), %%mm2		\n\t"\
        "movq    2(%0), %%mm4		\n\t"\
        "movq %%mm2, %%mm3		\n\t"\
        "movq %%mm4, %%mm5		\n\t"\
        "punpcklbw %%mm7, %%mm2		\n\t"\
        "punpckhbw %%mm7, %%mm3		\n\t"\
        "punpcklbw %%mm7, %%mm4		\n\t"\
        "punpckhbw %%mm7, %%mm5		\n\t"\
        "paddw %%mm4, %%mm2		\n\t"\
        "paddw %%mm3, %%mm5		\n\t"\
        "psubw %%mm2, %%mm0		\n\t"\
        "psubw %%mm5, %%mm1		\n\t"\
        "pmullw %%mm6, %%mm0		\n\t"\
        "pmullw %%mm6, %%mm1		\n\t"\
        "movd   -2(%0), %%mm2		\n\t"\
        "movd    7(%0), %%mm5		\n\t"\
        "punpcklbw %%mm7, %%mm2		\n\t"\
        "punpcklbw %%mm7, %%mm5		\n\t"\
        "paddw %%mm3, %%mm2		\n\t"\
        "paddw %%mm5, %%mm4		\n\t"\
        "movq %6, %%mm5			\n\t"\
        "paddw %%mm5, %%mm2		\n\t"\
        "paddw %%mm5, %%mm4		\n\t"\
        "paddw %%mm2, %%mm0		\n\t"\
        "paddw %%mm4, %%mm1		\n\t"\
        "psraw $5, %%mm0		\n\t"\
        "psraw $5, %%mm1		\n\t"\
        "packuswb %%mm1, %%mm0		\n\t"\
        OP(%%mm0, (%1),%%mm5, q)\
        "add %3, %0			\n\t"\
        "add %4, %1			\n\t"\
        "decl %2			\n\t"\
        " jnz 1b			\n\t"\
        : "+a"(src), "+c"(dst), "+m"(h)\
        : "d"((long)srcStride), "S"((long)dstStride), "m"(ff_pw_5), "m"(ff_pw_16)\
        : "memory"\
    );\
}\
\
static void OPNAME ## h264_qpel8_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    int h= 2;\
    src -= 2*srcStride;\
    \
    while(h--){\
      asm volatile(\
        "pxor %%mm7, %%mm7		\n\t"\
        "movd (%0), %%mm0		\n\t"\
        "add %2, %0			\n\t"\
        "movd (%0), %%mm1		\n\t"\
        "add %2, %0			\n\t"\
        "movd (%0), %%mm2		\n\t"\
        "add %2, %0			\n\t"\
        "movd (%0), %%mm3		\n\t"\
        "add %2, %0			\n\t"\
        "movd (%0), %%mm4		\n\t"\
        "add %2, %0			\n\t"\
        "punpcklbw %%mm7, %%mm0		\n\t"\
        "punpcklbw %%mm7, %%mm1		\n\t"\
        "punpcklbw %%mm7, %%mm2		\n\t"\
        "punpcklbw %%mm7, %%mm3		\n\t"\
        "punpcklbw %%mm7, %%mm4		\n\t"\
        QPEL_H264V(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
        QPEL_H264V(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
        QPEL_H264V(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP)\
        QPEL_H264V(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP)\
        QPEL_H264V(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, OP)\
        QPEL_H264V(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, OP)\
        QPEL_H264V(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
        QPEL_H264V(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
         \
        : "+a"(src), "+c"(dst)\
        : "S"((long)srcStride), "D"((long)dstStride), "m"(ff_pw_5), "m"(ff_pw_16)\
        : "memory"\
     );\
     src += 4-13*srcStride;\
     dst +=  4-8*dstStride;\
   }\
}\
static void OPNAME ## h264_qpel8_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    int h=8;\
    int w=4;\
    src -= 2*srcStride+2;\
    while(w--){\
        asm volatile(\
            "pxor %%mm7, %%mm7			\n\t"\
            "movd (%0), %%mm0			\n\t"\
            "add %2, %0				\n\t"\
            "movd (%0), %%mm1			\n\t"\
            "add %2, %0				\n\t"\
            "movd (%0), %%mm2			\n\t"\
            "add %2, %0				\n\t"\
            "movd (%0), %%mm3			\n\t"\
            "add %2, %0				\n\t"\
            "movd (%0), %%mm4			\n\t"\
            "add %2, %0				\n\t"\
            "punpcklbw %%mm7, %%mm0		\n\t"\
            "punpcklbw %%mm7, %%mm1		\n\t"\
            "punpcklbw %%mm7, %%mm2		\n\t"\
            "punpcklbw %%mm7, %%mm3		\n\t"\
            "punpcklbw %%mm7, %%mm4		\n\t"\
            QPEL_H264HV(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, 0*8*4)\
            QPEL_H264HV(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, 1*8*4)\
            QPEL_H264HV(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, 2*8*4)\
            QPEL_H264HV(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, 3*8*4)\
            QPEL_H264HV(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, 4*8*4)\
            QPEL_H264HV(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, 5*8*4)\
            QPEL_H264HV(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, 6*8*4)\
            QPEL_H264HV(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, 7*8*4)\
             \
            : "+a"(src)\
            : "c"(tmp), "S"((long)srcStride), "m"(ff_pw_5)\
            : "memory"\
        );\
        tmp += 4;\
        src += 4 - 13*srcStride;\
    }\
    tmp -= 4*4;\
    asm volatile(\
        "movq %4, %%mm6			\n\t"\
        "1:				\n\t"\
        "movq     (%0), %%mm0		\n\t"\
        "movq    8(%0), %%mm3		\n\t"\
        "movq    2(%0), %%mm1		\n\t"\
        "movq   10(%0), %%mm4		\n\t"\
        "paddw   %%mm4, %%mm0		\n\t"\
        "paddw   %%mm3, %%mm1		\n\t"\
        "paddw  18(%0), %%mm3		\n\t"\
        "paddw  16(%0), %%mm4		\n\t"\
        "movq    4(%0), %%mm2		\n\t"\
        "movq   12(%0), %%mm5		\n\t"\
        "paddw   6(%0), %%mm2		\n\t"\
        "paddw  14(%0), %%mm5		\n\t"\
        "psubw %%mm1, %%mm0		\n\t"\
        "psubw %%mm4, %%mm3		\n\t"\
        "psraw $2, %%mm0		\n\t"\
        "psraw $2, %%mm3		\n\t"\
        "psubw %%mm1, %%mm0		\n\t"\
        "psubw %%mm4, %%mm3		\n\t"\
        "paddsw %%mm2, %%mm0		\n\t"\
        "paddsw %%mm5, %%mm3		\n\t"\
        "psraw $2, %%mm0		\n\t"\
        "psraw $2, %%mm3		\n\t"\
        "paddw %%mm6, %%mm2		\n\t"\
        "paddw %%mm6, %%mm5		\n\t"\
        "paddw %%mm2, %%mm0		\n\t"\
        "paddw %%mm5, %%mm3		\n\t"\
        "psraw $6, %%mm0		\n\t"\
        "psraw $6, %%mm3		\n\t"\
        "packuswb %%mm3, %%mm0		\n\t"\
        OP(%%mm0, (%1),%%mm7, q)\
        "add $32, %0			\n\t"\
        "add %3, %1			\n\t"\
        "decl %2			\n\t"\
        " jnz 1b			\n\t"\
        : "+a"(tmp), "+c"(dst), "+m"(h)\
        : "S"((long)dstStride), "m"(ff_pw_32)\
        : "memory"\
    );\
}\
static void OPNAME ## h264_qpel16_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## h264_qpel8_v_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## h264_qpel8_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## h264_qpel8_v_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
}\
\
static void OPNAME ## h264_qpel16_h_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
}\
\
static void OPNAME ## h264_qpel16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    OPNAME ## h264_qpel8_hv_lowpass_ ## MMX(dst  , tmp  , src  , dstStride, tmpStride, srcStride);\
    OPNAME ## h264_qpel8_hv_lowpass_ ## MMX(dst+8, tmp  , src+8, dstStride, tmpStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## h264_qpel8_hv_lowpass_ ## MMX(dst  , tmp  , src  , dstStride, tmpStride, srcStride);\
    OPNAME ## h264_qpel8_hv_lowpass_ ## MMX(dst+8, tmp  , src+8, dstStride, tmpStride, srcStride);\
}\

#define H264_MC(OPNAME, SIZE, MMX) \
static void OPNAME ## h264_qpel ## SIZE ## _mc00_ ## MMX (uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels ## SIZE ## _mmx(dst, src, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc10_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/8];\
    uint8_t * const half= (uint8_t*)temp;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## MMX(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, src, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc20_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc30_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/8];\
    uint8_t * const half= (uint8_t*)temp;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## MMX(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, src+1, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc01_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/8];\
    uint8_t * const half= (uint8_t*)temp;\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, src, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc02_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _v_lowpass_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc03_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/8];\
    uint8_t * const half= (uint8_t*)temp;\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, src+stride, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc11_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/4];\
    uint8_t * const halfH= (uint8_t*)temp;\
    uint8_t * const halfV= ((uint8_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## MMX(halfH, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(halfV, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc31_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/4];\
    uint8_t * const halfH= (uint8_t*)temp;\
    uint8_t * const halfV= ((uint8_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## MMX(halfH, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(halfV, src+1, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc13_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/4];\
    uint8_t * const halfH= (uint8_t*)temp;\
    uint8_t * const halfV= ((uint8_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## MMX(halfH, src + stride, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(halfV, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc33_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/4];\
    uint8_t * const halfH= (uint8_t*)temp;\
    uint8_t * const halfV= ((uint8_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## MMX(halfH, src + stride, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(halfV, src+1, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc22_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*(SIZE+8)/4];\
    int16_t * const tmp= (int16_t*)temp;\
    OPNAME ## h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(dst, tmp, src, stride, SIZE, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc21_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*(SIZE+8)/4 + SIZE*SIZE/4];\
    uint8_t * const halfH= (uint8_t*)temp;\
    uint8_t * const halfHV= ((uint8_t*)temp) + SIZE*SIZE;\
    int16_t * const tmp= ((int16_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## MMX(halfH, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, halfH, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc23_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*(SIZE+8)/4 + SIZE*SIZE/4];\
    uint8_t * const halfH= (uint8_t*)temp;\
    uint8_t * const halfHV= ((uint8_t*)temp) + SIZE*SIZE;\
    int16_t * const tmp= ((int16_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## MMX(halfH, src + stride, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, halfH, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc12_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*(SIZE+8)/4 + SIZE*SIZE/4];\
    uint8_t * const halfV= (uint8_t*)temp;\
    uint8_t * const halfHV= ((uint8_t*)temp) + SIZE*SIZE;\
    int16_t * const tmp= ((int16_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(halfV, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, halfV, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc32_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*(SIZE+8)/4 + SIZE*SIZE/4];\
    uint8_t * const halfV= (uint8_t*)temp;\
    uint8_t * const halfHV= ((uint8_t*)temp) + SIZE*SIZE;\
    int16_t * const tmp= ((int16_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(halfV, src+1, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, halfV, halfHV, stride, SIZE, SIZE);\
}\


#define PUT_OP(a,b,temp, size) "mov" #size " " #a ", " #b "	\n\t"
#define AVG_3DNOW_OP(a,b,temp, size) \
"mov" #size " " #b ", " #temp "	\n\t"\
"pavgusb " #temp ", " #a "	\n\t"\
"mov" #size " " #a ", " #b "	\n\t"
#define AVG_MMX2_OP(a,b,temp, size) \
"mov" #size " " #b ", " #temp "	\n\t"\
"pavgb " #temp ", " #a "	\n\t"\
"mov" #size " " #a ", " #b "	\n\t"

QPEL_H264(put_,       PUT_OP, 3dnow)
QPEL_H264(avg_, AVG_3DNOW_OP, 3dnow)
QPEL_H264(put_,       PUT_OP, mmx2)
QPEL_H264(avg_,  AVG_MMX2_OP, mmx2)

H264_MC(put_, 4, 3dnow)
H264_MC(put_, 8, 3dnow)
H264_MC(put_, 16,3dnow)
H264_MC(avg_, 4, 3dnow)
H264_MC(avg_, 8, 3dnow)
H264_MC(avg_, 16,3dnow)
H264_MC(put_, 4, mmx2)
H264_MC(put_, 8, mmx2)
H264_MC(put_, 16,mmx2)
H264_MC(avg_, 4, mmx2)
H264_MC(avg_, 8, mmx2)
H264_MC(avg_, 16,mmx2)


#define H264_CHROMA_OP(S,D)
#define H264_CHROMA_MC8_TMPL put_h264_chroma_mc8_mmx
#include "dsputil_h264_template_mmx.c"
#undef H264_CHROMA_OP
#undef H264_CHROMA_MC8_TMPL

#define H264_CHROMA_OP(S,D) "pavgb " #S ", " #D " \n\t"
#define H264_CHROMA_MC8_TMPL avg_h264_chroma_mc8_mmx2
#include "dsputil_h264_template_mmx.c"
#undef H264_CHROMA_OP
#undef H264_CHROMA_MC8_TMPL

#define H264_CHROMA_OP(S,D) "pavgusb " #S ", " #D " \n\t"
#define H264_CHROMA_MC8_TMPL avg_h264_chroma_mc8_3dnow
#include "dsputil_h264_template_mmx.c"
#undef H264_CHROMA_OP
#undef H264_CHROMA_MC8_TMPL

