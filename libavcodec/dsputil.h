/*
 * DSP utils
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
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
#ifndef DSPUTIL_H
#define DSPUTIL_H

#include "common.h"
#include "avcodec.h"

//#define DEBUG
/* dct code */
typedef short DCTELEM;

void fdct_ifast (DCTELEM *data);
void ff_jpeg_fdct_islow (DCTELEM *data);

void j_rev_dct (DCTELEM *data);

void ff_fdct_mmx(DCTELEM *block);

/* encoding scans */
extern const UINT8 ff_alternate_horizontal_scan[64];
extern const UINT8 ff_alternate_vertical_scan[64];
extern const UINT8 ff_zigzag_direct[64];

/* pixel operations */
#define MAX_NEG_CROP 384

/* temporary */
extern UINT32 squareTbl[512];
extern UINT8 cropTbl[256 + 2 * MAX_NEG_CROP];


/* minimum alignment rules ;)
if u notice errors in the align stuff, need more alignment for some asm code for some cpu
or need to use a function with less aligned data then send a mail to the ffmpeg-dev list, ...

!warning these alignments might not match reallity, (missing attribute((align)) stuff somewhere possible)
i (michael) didnt check them, these are just the alignents which i think could be reached easily ...

!future video codecs might need functions with less strict alignment
*/

/*
void get_pixels_c(DCTELEM *block, const UINT8 *pixels, int line_size);
void diff_pixels_c(DCTELEM *block, const UINT8 *s1, const UINT8 *s2, int stride);
void put_pixels_clamped_c(const DCTELEM *block, UINT8 *pixels, int line_size);
void add_pixels_clamped_c(const DCTELEM *block, UINT8 *pixels, int line_size);
void clear_blocks_c(DCTELEM *blocks);
*/

/* add and put pixel (decoding) */
// blocksizes for op_pixels_func are 8x4,8x8 16x8 16x16
typedef void (*op_pixels_func)(UINT8 *block/*align width (8 or 16)*/, const UINT8 *pixels/*align 1*/, int line_size, int h);
typedef void (*qpel_mc_func)(UINT8 *dst/*align width (8 or 16)*/, UINT8 *src/*align 1*/, int stride);

#define DEF_OLD_QPEL(name)\
void ff_put_        ## name (UINT8 *dst/*align width (8 or 16)*/, UINT8 *src/*align 1*/, int stride);\
void ff_put_no_rnd_ ## name (UINT8 *dst/*align width (8 or 16)*/, UINT8 *src/*align 1*/, int stride);\
void ff_avg_        ## name (UINT8 *dst/*align width (8 or 16)*/, UINT8 *src/*align 1*/, int stride);

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

typedef int (*op_pixels_abs_func)(UINT8 *blk1/*align width (8 or 16)*/, UINT8 *blk2/*align 1*/, int line_size)/* __attribute__ ((const))*/;

typedef int (*me_cmp_func)(void /*MpegEncContext*/ *s, UINT8 *blk1/*align width (8 or 16)*/, UINT8 *blk2/*align 1*/, int line_size)/* __attribute__ ((const))*/;

typedef struct DSPContext {
    /* pixel ops : interface with DCT */
    void (*get_pixels)(DCTELEM *block/*align 16*/, const UINT8 *pixels/*align 8*/, int line_size);
    void (*diff_pixels)(DCTELEM *block/*align 16*/, const UINT8 *s1/*align 8*/, const UINT8 *s2/*align 8*/, int stride);
    void (*put_pixels_clamped)(const DCTELEM *block/*align 16*/, UINT8 *pixels/*align 8*/, int line_size);
    void (*add_pixels_clamped)(const DCTELEM *block/*align 16*/, UINT8 *pixels/*align 8*/, int line_size);
    void (*gmc1)(UINT8 *dst/*align 8*/, UINT8 *src/*align 1*/, int srcStride, int h, int x16, int y16, int rounder);
    void (*gmc )(UINT8 *dst/*align 8*/, UINT8 *src/*align 1*/, int stride, int h, int ox, int oy,
		    int dxx, int dxy, int dyx, int dyy, int shift, int r, int width, int height);
    void (*clear_blocks)(DCTELEM *blocks/*align 16*/);
    int (*pix_sum)(UINT8 * pix, int line_size);
    int (*pix_norm1)(UINT8 * pix, int line_size);
    me_cmp_func sad[2]; /* identical to pix_absAxA except additional void * */
    me_cmp_func sse[2];
    me_cmp_func hadamard8_diff[2];
    me_cmp_func dct_sad[2];
    me_cmp_func quant_psnr[2];
    me_cmp_func bit[2];
    me_cmp_func rd[2];
    int (*hadamard8_abs )(uint8_t *src, int stride, int mean);

    me_cmp_func me_pre_cmp[11];
    me_cmp_func me_cmp[11];
    me_cmp_func me_sub_cmp[11];
    me_cmp_func mb_cmp[11];

    /* maybe create an array for 16/8 functions */
    op_pixels_func put_pixels_tab[2][4];
    op_pixels_func avg_pixels_tab[2][4];
    op_pixels_func put_no_rnd_pixels_tab[2][4];
    op_pixels_func avg_no_rnd_pixels_tab[2][4];
    qpel_mc_func put_qpel_pixels_tab[2][16];
    qpel_mc_func avg_qpel_pixels_tab[2][16];
    qpel_mc_func put_no_rnd_qpel_pixels_tab[2][16];
    qpel_mc_func avg_no_rnd_qpel_pixels_tab[2][16];
    qpel_mc_func put_mspel_pixels_tab[8];

    op_pixels_abs_func pix_abs16x16;
    op_pixels_abs_func pix_abs16x16_x2;
    op_pixels_abs_func pix_abs16x16_y2;
    op_pixels_abs_func pix_abs16x16_xy2;
    op_pixels_abs_func pix_abs8x8;
    op_pixels_abs_func pix_abs8x8_x2;
    op_pixels_abs_func pix_abs8x8_y2;
    op_pixels_abs_func pix_abs8x8_xy2;
    
    /* huffyuv specific */
    void (*add_bytes)(uint8_t *dst/*align 16*/, uint8_t *src/*align 16*/, int w);
    void (*diff_bytes)(uint8_t *dst/*align 16*/, uint8_t *src1/*align 16*/, uint8_t *src2/*align 1*/,int w);
} DSPContext;

void dsputil_init(DSPContext* p, unsigned mask);

/**
 * permute block according to permuatation.
 * @param last last non zero element in scantable order
 */
void ff_block_permute(INT16 *block, UINT8 *permutation, const UINT8 *scantable, int last);

#define emms_c()

/* should be defined by architectures supporting
   one or more MultiMedia extension */
int mm_support(void);

#if defined(HAVE_MMX)

#undef emms_c

#define MM_MMX    0x0001 /* standard MMX */
#define MM_3DNOW  0x0004 /* AMD 3DNOW */
#define MM_MMXEXT 0x0002 /* SSE integer functions or AMD MMX ext */
#define MM_SSE    0x0008 /* SSE functions */
#define MM_SSE2   0x0010 /* PIV SSE2 functions */

extern int mm_flags;

void add_pixels_clamped_mmx(const DCTELEM *block, UINT8 *pixels, int line_size);
void put_pixels_clamped_mmx(const DCTELEM *block, UINT8 *pixels, int line_size);

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

void dsputil_init_mmx(DSPContext* c, unsigned mask);
void dsputil_set_bit_exact_mmx(DSPContext* c, unsigned mask);

#elif defined(ARCH_ARMV4L)

/* This is to use 4 bytes read to the IDCT pointers for some 'zero'
   line ptimizations */
#define __align8 __attribute__ ((aligned (4)))

void dsputil_init_armv4l(DSPContext* c, unsigned mask);

#elif defined(HAVE_MLIB)

/* SPARC/VIS IDCT needs 8-byte aligned DCT blocks */
#define __align8 __attribute__ ((aligned (8)))

void dsputil_init_mlib(DSPContext* c, unsigned mask);

#elif defined(ARCH_ALPHA)

#define __align8 __attribute__ ((aligned (8)))

void dsputil_init_alpha(DSPContext* c, unsigned mask);

#elif defined(ARCH_POWERPC)

#define MM_ALTIVEC    0x0001 /* standard AltiVec */

extern int mm_flags;

#define __align8 __attribute__ ((aligned (16)))

void dsputil_init_ppc(DSPContext* c, unsigned mask);

#elif defined(HAVE_MMI)

#define __align8 __attribute__ ((aligned (16)))

void dsputil_init_mmi(DSPContext* c, unsigned mask);

#else

#define __align8

#endif

#ifdef __GNUC__

struct unaligned_64 { uint64_t l; } __attribute__((packed));
struct unaligned_32 { uint32_t l; } __attribute__((packed));

#define LD32(a) (((const struct unaligned_32 *) (a))->l)
#define LD64(a) (((const struct unaligned_64 *) (a))->l)

#define ST32(a, b) (((struct unaligned_32 *) (a))->l) = (b)

#else /* __GNUC__ */

#define LD32(a) (*((uint32_t*)(a)))
#define LD64(a) (*((uint64_t*)(a)))

#define ST32(a, b) *((uint32_t*)(a)) = (b)

#endif /* !__GNUC__ */

/* PSNR */
void get_psnr(UINT8 *orig_image[3], UINT8 *coded_image[3],
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

int fft_init(FFTContext *s, int nbits, int inverse);
void fft_permute(FFTContext *s, FFTComplex *z);
void fft_calc_c(FFTContext *s, FFTComplex *z);
void fft_calc_sse(FFTContext *s, FFTComplex *z);
void fft_calc_altivec(FFTContext *s, FFTComplex *z);

static inline void fft_calc(FFTContext *s, FFTComplex *z)
{
    s->fft_calc(s, z);
}
void fft_end(FFTContext *s);

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

#define WARPER88_1616(name8, name16)\
static int name16(void /*MpegEncContext*/ *s, uint8_t *dst, uint8_t *src, int stride){\
    return name8(s, dst           , src           , stride)\
          +name8(s, dst+8         , src+8         , stride)\
          +name8(s, dst  +8*stride, src  +8*stride, stride)\
          +name8(s, dst+8+8*stride, src+8+8*stride, stride);\
}

#ifndef HAVE_LRINTF
/* XXX: add ISOC specific test to avoid specific BSD testing. */
/* better than nothing implementation. */
/* btw, rintf() is existing on fbsd too -- alex */
static inline long int lrintf(float x)
{
    return (int)(rint(x));
}
#endif

#endif
