/*
 * DSP utils
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file dsputil.h
 * DSP utils.
 * note, many functions in here may use MMX which trashes the FPU state, it is
 * absolutely necessary to call emms_c() between dsp & float/double code
 */

#ifndef DSPUTIL_H
#define DSPUTIL_H

#include "common.h"
#include "avcodec.h"


//#define DEBUG
/* dct code */
typedef short DCTELEM;

void fdct_ifast (DCTELEM *data);
void fdct_ifast248 (DCTELEM *data);
void ff_jpeg_fdct_islow (DCTELEM *data);
void ff_fdct248_islow (DCTELEM *data);

void j_rev_dct (DCTELEM *data);
void j_rev_dct4 (DCTELEM *data);
void j_rev_dct2 (DCTELEM *data);
void j_rev_dct1 (DCTELEM *data);

void ff_fdct_mmx(DCTELEM *block);
void ff_fdct_mmx2(DCTELEM *block);
void ff_fdct_sse2(DCTELEM *block);

void ff_h264_idct_add_c(uint8_t *dst, DCTELEM *block, int stride);
void ff_h264_lowres_idct_add_c(uint8_t *dst, int stride, DCTELEM *block);
void ff_h264_lowres_idct_put_c(uint8_t *dst, int stride, DCTELEM *block);

/* encoding scans */
extern const uint8_t ff_alternate_horizontal_scan[64];
extern const uint8_t ff_alternate_vertical_scan[64];
extern const uint8_t ff_zigzag_direct[64];
extern const uint8_t ff_zigzag248_direct[64];

/* pixel operations */
#define MAX_NEG_CROP 1024

/* temporary */
extern uint32_t squareTbl[512];
extern uint8_t cropTbl[256 + 2 * MAX_NEG_CROP];

/* VP3 DSP functions */
void vp3_dsp_init_c(void);
void vp3_idct_c(int16_t *input_data, int16_t *dequant_matrix,
    int coeff_count, DCTELEM *output_data);

void vp3_dsp_init_mmx(void);
void vp3_idct_mmx(int16_t *input_data, int16_t *dequant_matrix,
    int coeff_count, DCTELEM *output_data);

void vp3_dsp_init_sse2(void);
void vp3_idct_sse2(int16_t *input_data, int16_t *dequant_matrix,
    int coeff_count, DCTELEM *output_data);

/* minimum alignment rules ;)
if u notice errors in the align stuff, need more alignment for some asm code for some cpu
or need to use a function with less aligned data then send a mail to the ffmpeg-dev list, ...

!warning these alignments might not match reallity, (missing attribute((align)) stuff somewhere possible)
i (michael) didnt check them, these are just the alignents which i think could be reached easily ...

!future video codecs might need functions with less strict alignment
*/

/*
void get_pixels_c(DCTELEM *block, const uint8_t *pixels, int line_size);
void diff_pixels_c(DCTELEM *block, const uint8_t *s1, const uint8_t *s2, int stride);
void put_pixels_clamped_c(const DCTELEM *block, uint8_t *pixels, int line_size);
void add_pixels_clamped_c(const DCTELEM *block, uint8_t *pixels, int line_size);
void clear_blocks_c(DCTELEM *blocks);
*/

/* add and put pixel (decoding) */
// blocksizes for op_pixels_func are 8x4,8x8 16x8 16x16
//h for op_pixels_func is limited to {width/2, width} but never larger than 16 and never smaller then 4
typedef void (*op_pixels_func)(uint8_t *block/*align width (8 or 16)*/, const uint8_t *pixels/*align 1*/, int line_size, int h);
typedef void (*tpel_mc_func)(uint8_t *block/*align width (8 or 16)*/, const uint8_t *pixels/*align 1*/, int line_size, int w, int h);
typedef void (*qpel_mc_func)(uint8_t *dst/*align width (8 or 16)*/, uint8_t *src/*align 1*/, int stride);
typedef void (*h264_chroma_mc_func)(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int srcStride, int h, int x, int y);
typedef void (*h264_weight_func)(uint8_t *block, int stride, int log2_denom, int weight, int offset);
typedef void (*h264_biweight_func)(uint8_t *dst, uint8_t *src, int stride, int log2_denom, int weightd, int weights, int offsetd, int offsets);

#define DEF_OLD_QPEL(name)\
void ff_put_        ## name (uint8_t *dst/*align width (8 or 16)*/, uint8_t *src/*align 1*/, int stride);\
void ff_put_no_rnd_ ## name (uint8_t *dst/*align width (8 or 16)*/, uint8_t *src/*align 1*/, int stride);\
void ff_avg_        ## name (uint8_t *dst/*align width (8 or 16)*/, uint8_t *src/*align 1*/, int stride);

DEF_OLD_QPEL(qpel16_mc11_old_c)
DEF_OLD_QPEL(qpel16_mc31_old_c)
DEF_OLD_QPEL(qpel16_mc12_old_c)
DEF_OLD_QPEL(qpel16_mc32_old_c)
DEF_OLD_QPEL(qpel16_mc13_old_c)
DEF_OLD_QPEL(qpel16_mc33_old_c)
DEF_OLD_QPEL(qpel8_mc11_old_c)
DEF_OLD_QPEL(qpel8_mc31_old_c)
DEF_OLD_QPEL(qpel8_mc12_old_c)
DEF_OLD_QPEL(qpel8_mc32_old_c)
DEF_OLD_QPEL(qpel8_mc13_old_c)
DEF_OLD_QPEL(qpel8_mc33_old_c)

#define CALL_2X_PIXELS(a, b, n)\
static void a(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    b(block  , pixels  , line_size, h);\
    b(block+n, pixels+n, line_size, h);\
}

/* motion estimation */
// h is limited to {width/2, width, 2*width} but never larger than 16 and never smaller then 2
// allthough currently h<4 is not used as functions with width <8 are not used and neither implemented
typedef int (*me_cmp_func)(void /*MpegEncContext*/ *s, uint8_t *blk1/*align width (8 or 16)*/, uint8_t *blk2/*align 1*/, int line_size, int h)/* __attribute__ ((const))*/;


/**
 * DSPContext.
 */
typedef struct DSPContext {
    /* pixel ops : interface with DCT */
    void (*get_pixels)(DCTELEM *block/*align 16*/, const uint8_t *pixels/*align 8*/, int line_size);
    void (*diff_pixels)(DCTELEM *block/*align 16*/, const uint8_t *s1/*align 8*/, const uint8_t *s2/*align 8*/, int stride);
    void (*put_pixels_clamped)(const DCTELEM *block/*align 16*/, uint8_t *pixels/*align 8*/, int line_size);
    void (*put_signed_pixels_clamped)(const DCTELEM *block/*align 16*/, uint8_t *pixels/*align 8*/, int line_size);
    void (*add_pixels_clamped)(const DCTELEM *block/*align 16*/, uint8_t *pixels/*align 8*/, int line_size);
    /**
     * translational global motion compensation.
     */
    void (*gmc1)(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int srcStride, int h, int x16, int y16, int rounder);
    /**
     * global motion compensation.
     */
    void (*gmc )(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int ox, int oy,
		    int dxx, int dxy, int dyx, int dyy, int shift, int r, int width, int height);
    void (*clear_blocks)(DCTELEM *blocks/*align 16*/);
    int (*pix_sum)(uint8_t * pix, int line_size);
    int (*pix_norm1)(uint8_t * pix, int line_size);
// 16x16 8x8 4x4 2x2 16x8 8x4 4x2 8x16 4x8 2x4
    
    me_cmp_func sad[5]; /* identical to pix_absAxA except additional void * */
    me_cmp_func sse[5];
    me_cmp_func hadamard8_diff[5];
    me_cmp_func dct_sad[5];
    me_cmp_func quant_psnr[5];
    me_cmp_func bit[5];
    me_cmp_func rd[5];
    me_cmp_func vsad[5];
    me_cmp_func vsse[5];
    me_cmp_func nsse[5];
    me_cmp_func w53[5];
    me_cmp_func w97[5];
    me_cmp_func dct_max[5];

    me_cmp_func me_pre_cmp[5];
    me_cmp_func me_cmp[5];
    me_cmp_func me_sub_cmp[5];
    me_cmp_func mb_cmp[5];
    me_cmp_func ildct_cmp[5]; //only width 16 used
    me_cmp_func frame_skip_cmp[5]; //only width 8 used

    /**
     * Halfpel motion compensation with rounding (a+b+1)>>1.
     * this is an array[4][4] of motion compensation funcions for 4 
     * horizontal blocksizes (8,16) and the 4 halfpel positions<br>
     * *pixels_tab[ 0->16xH 1->8xH ][ xhalfpel + 2*yhalfpel ]
     * @param block destination where the result is stored
     * @param pixels source
     * @param line_size number of bytes in a horizontal line of block
     * @param h height
     */
    op_pixels_func put_pixels_tab[4][4];

    /**
     * Halfpel motion compensation with rounding (a+b+1)>>1.
     * This is an array[4][4] of motion compensation functions for 4 
     * horizontal blocksizes (8,16) and the 4 halfpel positions<br>
     * *pixels_tab[ 0->16xH 1->8xH ][ xhalfpel + 2*yhalfpel ]
     * @param block destination into which the result is averaged (a+b+1)>>1
     * @param pixels source
     * @param line_size number of bytes in a horizontal line of block
     * @param h height
     */
    op_pixels_func avg_pixels_tab[4][4];

    /**
     * Halfpel motion compensation with no rounding (a+b)>>1.
     * this is an array[2][4] of motion compensation funcions for 2 
     * horizontal blocksizes (8,16) and the 4 halfpel positions<br>
     * *pixels_tab[ 0->16xH 1->8xH ][ xhalfpel + 2*yhalfpel ]
     * @param block destination where the result is stored
     * @param pixels source
     * @param line_size number of bytes in a horizontal line of block
     * @param h height
     */
    op_pixels_func put_no_rnd_pixels_tab[4][4];

    /**
     * Halfpel motion compensation with no rounding (a+b)>>1.
     * this is an array[2][4] of motion compensation funcions for 2 
     * horizontal blocksizes (8,16) and the 4 halfpel positions<br>
     * *pixels_tab[ 0->16xH 1->8xH ][ xhalfpel + 2*yhalfpel ]
     * @param block destination into which the result is averaged (a+b)>>1
     * @param pixels source
     * @param line_size number of bytes in a horizontal line of block
     * @param h height
     */
    op_pixels_func avg_no_rnd_pixels_tab[4][4];
    
    void (*put_no_rnd_pixels_l2[2])(uint8_t *block/*align width (8 or 16)*/, const uint8_t *a/*align 1*/, const uint8_t *b/*align 1*/, int line_size, int h);
    
    /**
     * Thirdpel motion compensation with rounding (a+b+1)>>1.
     * this is an array[12] of motion compensation funcions for the 9 thirdpel positions<br>
     * *pixels_tab[ xthirdpel + 4*ythirdpel ]
     * @param block destination where the result is stored
     * @param pixels source
     * @param line_size number of bytes in a horizontal line of block
     * @param h height
     */
    tpel_mc_func put_tpel_pixels_tab[11]; //FIXME individual func ptr per width?
    tpel_mc_func avg_tpel_pixels_tab[11]; //FIXME individual func ptr per width?

    qpel_mc_func put_qpel_pixels_tab[2][16];
    qpel_mc_func avg_qpel_pixels_tab[2][16];
    qpel_mc_func put_no_rnd_qpel_pixels_tab[2][16];
    qpel_mc_func avg_no_rnd_qpel_pixels_tab[2][16];
    qpel_mc_func put_mspel_pixels_tab[8];
    
    /**
     * h264 Chram MC
     */
    h264_chroma_mc_func put_h264_chroma_pixels_tab[3];
    h264_chroma_mc_func avg_h264_chroma_pixels_tab[3];

    qpel_mc_func put_h264_qpel_pixels_tab[3][16];
    qpel_mc_func avg_h264_qpel_pixels_tab[3][16];
    
    h264_weight_func weight_h264_pixels_tab[10];
    h264_biweight_func biweight_h264_pixels_tab[10];
    
    me_cmp_func pix_abs[2][4];
    
    /* huffyuv specific */
    void (*add_bytes)(uint8_t *dst/*align 16*/, uint8_t *src/*align 16*/, int w);
    void (*diff_bytes)(uint8_t *dst/*align 16*/, uint8_t *src1/*align 16*/, uint8_t *src2/*align 1*/,int w);
    /**
     * subtract huffyuv's variant of median prediction
     * note, this might read from src1[-1], src2[-1]
     */
    void (*sub_hfyu_median_prediction)(uint8_t *dst, uint8_t *src1, uint8_t *src2, int w, int *left, int *left_top);
    void (*bswap_buf)(uint32_t *dst, uint32_t *src, int w);
    
    void (*h263_v_loop_filter)(uint8_t *src, int stride, int qscale);
    void (*h263_h_loop_filter)(uint8_t *src, int stride, int qscale);

    void (*h261_loop_filter)(uint8_t *src, int stride);

    /* (I)DCT */
    void (*fdct)(DCTELEM *block/* align 16*/);
    void (*fdct248)(DCTELEM *block/* align 16*/);
    
    /* IDCT really*/
    void (*idct)(DCTELEM *block/* align 16*/);
    
    /**
     * block -> idct -> clip to unsigned 8 bit -> dest.
     * (-1392, 0, 0, ...) -> idct -> (-174, -174, ...) -> put -> (0, 0, ...)
     * @param line_size size in bytes of a horizotal line of dest
     */
    void (*idct_put)(uint8_t *dest/*align 8*/, int line_size, DCTELEM *block/*align 16*/);
    
    /**
     * block -> idct -> add dest -> clip to unsigned 8 bit -> dest.
     * @param line_size size in bytes of a horizotal line of dest
     */
    void (*idct_add)(uint8_t *dest/*align 8*/, int line_size, DCTELEM *block/*align 16*/);
    
    /**
     * idct input permutation.
     * several optimized IDCTs need a permutated input (relative to the normal order of the reference
     * IDCT)
     * this permutation must be performed before the idct_put/add, note, normally this can be merged
     * with the zigzag/alternate scan<br>
     * an example to avoid confusion:
     * - (->decode coeffs -> zigzag reorder -> dequant -> reference idct ->...)
     * - (x -> referece dct -> reference idct -> x)
     * - (x -> referece dct -> simple_mmx_perm = idct_permutation -> simple_idct_mmx -> x)
     * - (->decode coeffs -> zigzag reorder -> simple_mmx_perm -> dequant -> simple_idct_mmx ->...)
     */
    uint8_t idct_permutation[64];
    int idct_permutation_type;
#define FF_NO_IDCT_PERM 1
#define FF_LIBMPEG2_IDCT_PERM 2
#define FF_SIMPLE_IDCT_PERM 3
#define FF_TRANSPOSE_IDCT_PERM 4

    int (*try_8x8basis)(int16_t rem[64], int16_t weight[64], int16_t basis[64], int scale);
    void (*add_8x8basis)(int16_t rem[64], int16_t basis[64], int scale);
#define BASIS_SHIFT 16
#define RECON_SHIFT 6

    /**
     * This function handles any initialization for the VP3 DSP functions.
     */
    void (*vp3_dsp_init)(void);

    /** 
     * This function is responsible for taking a block of zigzag'd,
     * quantized DCT coefficients and reconstructing the original block of
     * samples.
     * @param input_data 64 zigzag'd, quantized DCT coefficients
     * @param dequant_matrix 64 zigzag'd quantizer coefficients
     * @param coeff_count index of the last coefficient
     * @param output_samples space for 64 DCTELEMs where the transformed
     * samples will be stored
     */
    void (*vp3_idct)(int16_t *input_data, int16_t *dequant_matrix,
        int coeff_count, DCTELEM *output_samples);
 
    void (*h264_idct_add)(uint8_t *dst, DCTELEM *block, int stride);
} DSPContext;

void dsputil_static_init(void);
void dsputil_init(DSPContext* p, AVCodecContext *avctx);

/**
 * permute block according to permuatation.
 * @param last last non zero element in scantable order
 */
void ff_block_permute(DCTELEM *block, uint8_t *permutation, const uint8_t *scantable, int last);

void ff_set_cmp(DSPContext* c, me_cmp_func *cmp, int type);

#define	BYTE_VEC32(c)	((c)*0x01010101UL)

static inline uint32_t rnd_avg32(uint32_t a, uint32_t b)
{
    return (a | b) - (((a ^ b) & ~BYTE_VEC32(0x01)) >> 1);
}

static inline uint32_t no_rnd_avg32(uint32_t a, uint32_t b)
{
    return (a & b) + (((a ^ b) & ~BYTE_VEC32(0x01)) >> 1);
}

static inline int get_penalty_factor(int lambda, int lambda2, int type){
    switch(type&0xFF){
    default:
    case FF_CMP_SAD:
        return lambda>>FF_LAMBDA_SHIFT;
    case FF_CMP_DCT:
        return (3*lambda)>>(FF_LAMBDA_SHIFT+1);
    case FF_CMP_W53:
        return (4*lambda)>>(FF_LAMBDA_SHIFT);
    case FF_CMP_W97:
        return (2*lambda)>>(FF_LAMBDA_SHIFT);
    case FF_CMP_SATD:
        return (2*lambda)>>FF_LAMBDA_SHIFT;
    case FF_CMP_RD:
    case FF_CMP_PSNR:
    case FF_CMP_SSE:
    case FF_CMP_NSSE:
        return lambda2>>FF_LAMBDA_SHIFT;
    case FF_CMP_BIT:
        return 1;
    }
}

/**
 * Empty mmx state.
 * this must be called between any dsp function and float/double code.
 * for example sin(); dsp->idct_put(); emms_c(); cos()
 */
#define emms_c()

/* should be defined by architectures supporting
   one or more MultiMedia extension */
int mm_support(void);

#define __align16 __attribute__ ((aligned (16)))

#if defined(HAVE_MMX)

#undef emms_c

#define MM_MMX    0x0001 /* standard MMX */
#define MM_3DNOW  0x0004 /* AMD 3DNOW */
#define MM_MMXEXT 0x0002 /* SSE integer functions or AMD MMX ext */
#define MM_SSE    0x0008 /* SSE functions */
#define MM_SSE2   0x0010 /* PIV SSE2 functions */
#define MM_3DNOWEXT  0x0020 /* AMD 3DNowExt */

extern int mm_flags;

void add_pixels_clamped_mmx(const DCTELEM *block, uint8_t *pixels, int line_size);
void put_pixels_clamped_mmx(const DCTELEM *block, uint8_t *pixels, int line_size);
void put_signed_pixels_clamped_mmx(const DCTELEM *block, uint8_t *pixels, int line_size);

static inline void emms(void)
{
    __asm __volatile ("emms;":::"memory");
}


#define emms_c() \
{\
    if (mm_flags & MM_MMX)\
        emms();\
}

#define __align8 __attribute__ ((aligned (8)))
#define STRIDE_ALIGN 8

void dsputil_init_mmx(DSPContext* c, AVCodecContext *avctx);
void dsputil_init_pix_mmx(DSPContext* c, AVCodecContext *avctx);

#elif defined(ARCH_ARMV4L)

/* This is to use 4 bytes read to the IDCT pointers for some 'zero'
   line optimizations */
#define __align8 __attribute__ ((aligned (4)))
#define STRIDE_ALIGN 4

void dsputil_init_armv4l(DSPContext* c, AVCodecContext *avctx);

#elif defined(HAVE_MLIB)

/* SPARC/VIS IDCT needs 8-byte aligned DCT blocks */
#define __align8 __attribute__ ((aligned (8)))
#define STRIDE_ALIGN 8

void dsputil_init_mlib(DSPContext* c, AVCodecContext *avctx);

#elif defined(ARCH_SPARC)

/* SPARC/VIS IDCT needs 8-byte aligned DCT blocks */
#define __align8 __attribute__ ((aligned (8)))
#define STRIDE_ALIGN 8
void dsputil_init_vis(DSPContext* c, AVCodecContext *avctx);

#elif defined(ARCH_ALPHA)

#define __align8 __attribute__ ((aligned (8)))
#define STRIDE_ALIGN 8

void dsputil_init_alpha(DSPContext* c, AVCodecContext *avctx);

#elif defined(ARCH_POWERPC)

#define MM_ALTIVEC    0x0001 /* standard AltiVec */

extern int mm_flags;

#if defined(HAVE_ALTIVEC) && !defined(CONFIG_DARWIN)
#define pixel altivec_pixel
#include <altivec.h>
#undef pixel
#endif

#define __align8 __attribute__ ((aligned (16)))
#define STRIDE_ALIGN 16

void dsputil_init_ppc(DSPContext* c, AVCodecContext *avctx);

#elif defined(HAVE_MMI)

#define __align8 __attribute__ ((aligned (16)))
#define STRIDE_ALIGN 16

void dsputil_init_mmi(DSPContext* c, AVCodecContext *avctx);

#elif defined(ARCH_SH4)

#define __align8 __attribute__ ((aligned (8)))
#define STRIDE_ALIGN 8

void dsputil_init_sh4(DSPContext* c, AVCodecContext *avctx);

#else

#define __align8 __attribute__ ((aligned (8)))
#define STRIDE_ALIGN 8

#endif

#ifdef __GNUC__

struct unaligned_64 { uint64_t l; } __attribute__((packed));
struct unaligned_32 { uint32_t l; } __attribute__((packed));
struct unaligned_16 { uint16_t l; } __attribute__((packed));

#define LD16(a) (((const struct unaligned_16 *) (a))->l)
#define LD32(a) (((const struct unaligned_32 *) (a))->l)
#define LD64(a) (((const struct unaligned_64 *) (a))->l)

#define ST32(a, b) (((struct unaligned_32 *) (a))->l) = (b)

#else /* __GNUC__ */

#define LD16(a) (*((uint16_t*)(a)))
#define LD32(a) (*((uint32_t*)(a)))
#define LD64(a) (*((uint64_t*)(a)))

#define ST32(a, b) *((uint32_t*)(a)) = (b)

#endif /* !__GNUC__ */

/* PSNR */
void get_psnr(uint8_t *orig_image[3], uint8_t *coded_image[3],
              int orig_linesize[3], int coded_linesize,
              AVCodecContext *avctx);

/* FFT computation */

/* NOTE: soon integer code will be added, so you must use the
   FFTSample type */
typedef float FFTSample;

typedef struct FFTComplex {
    FFTSample re, im;
} FFTComplex;

typedef struct FFTContext {
    int nbits;
    int inverse;
    uint16_t *revtab;
    FFTComplex *exptab;
    FFTComplex *exptab1; /* only used by SSE code */
    void (*fft_calc)(struct FFTContext *s, FFTComplex *z);
} FFTContext;

int ff_fft_init(FFTContext *s, int nbits, int inverse);
void ff_fft_permute(FFTContext *s, FFTComplex *z);
void ff_fft_calc_c(FFTContext *s, FFTComplex *z);
void ff_fft_calc_sse(FFTContext *s, FFTComplex *z);
void ff_fft_calc_altivec(FFTContext *s, FFTComplex *z);

static inline void ff_fft_calc(FFTContext *s, FFTComplex *z)
{
    s->fft_calc(s, z);
}
void ff_fft_end(FFTContext *s);

/* MDCT computation */

typedef struct MDCTContext {
    int n;  /* size of MDCT (i.e. number of input data * 2) */
    int nbits; /* n = 2^nbits */
    /* pre/post rotation tables */
    FFTSample *tcos;
    FFTSample *tsin;
    FFTContext fft;
} MDCTContext;

int ff_mdct_init(MDCTContext *s, int nbits, int inverse);
void ff_imdct_calc(MDCTContext *s, FFTSample *output,
                const FFTSample *input, FFTSample *tmp);
void ff_mdct_calc(MDCTContext *s, FFTSample *out,
               const FFTSample *input, FFTSample *tmp);
void ff_mdct_end(MDCTContext *s);

#define WARPER8_16(name8, name16)\
static int name16(void /*MpegEncContext*/ *s, uint8_t *dst, uint8_t *src, int stride, int h){\
    return name8(s, dst           , src           , stride, h)\
          +name8(s, dst+8         , src+8         , stride, h);\
}

#define WARPER8_16_SQ(name8, name16)\
static int name16(void /*MpegEncContext*/ *s, uint8_t *dst, uint8_t *src, int stride, int h){\
    int score=0;\
    score +=name8(s, dst           , src           , stride, 8);\
    score +=name8(s, dst+8         , src+8         , stride, 8);\
    if(h==16){\
        dst += 8*stride;\
        src += 8*stride;\
        score +=name8(s, dst           , src           , stride, 8);\
        score +=name8(s, dst+8         , src+8         , stride, 8);\
    }\
    return score;\
}

#ifndef HAVE_LRINTF
/* XXX: add ISOC specific test to avoid specific BSD testing. */
/* better than nothing implementation. */
/* btw, rintf() is existing on fbsd too -- alex */
static always_inline long int lrintf(float x)
{
#ifdef CONFIG_WIN32
#  ifdef ARCH_X86
    int32_t i;
    asm volatile(
        "fistpl %0\n\t"
        : "=m" (i) : "t" (x) : "st"
    );
    return i;
#  else
    /* XXX: incorrect, but make it compile */
    return (int)(x + (x < 0 ? -0.5 : 0.5));
#  endif
#else
    return (int)(rint(x));
#endif
}
#else
#ifndef _ISOC9X_SOURCE
#define _ISOC9X_SOURCE
#endif
#include <math.h>
#endif

#endif
