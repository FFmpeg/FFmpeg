#ifndef DSPUTIL_H
#define DSPUTIL_H

#include "common.h"
#include <inttypes.h>

/* dct code */
typedef short DCTELEM;

void jpeg_fdct_ifast (DCTELEM *data);

void j_rev_dct (DCTELEM *data);

void fdct_mmx(DCTELEM *block);

void (*av_fdct)(DCTELEM *block);

/* pixel operations */
#define MAX_NEG_CROP 384

/* temporary */
extern UINT32 squareTbl[512];

void dsputil_init(void);

/* pixel ops : interface with DCT */

extern void (*ff_idct)(DCTELEM *block);
extern void (*get_pixels)(DCTELEM *block, const UINT8 *pixels, int line_size);
extern void (*put_pixels_clamped)(const DCTELEM *block, UINT8 *pixels, int line_size);
extern void (*add_pixels_clamped)(const DCTELEM *block, UINT8 *pixels, int line_size);

void get_pixels_c(DCTELEM *block, const UINT8 *pixels, int line_size);
void put_pixels_clamped_c(const DCTELEM *block, UINT8 *pixels, int line_size);
void add_pixels_clamped_c(const DCTELEM *block, UINT8 *pixels, int line_size);

/* add and put pixel (decoding) */
typedef void (*op_pixels_func)(UINT8 *block, const UINT8 *pixels, int line_size, int h);

extern op_pixels_func put_pixels_tab[4];
extern op_pixels_func avg_pixels_tab[4];
extern op_pixels_func put_no_rnd_pixels_tab[4];
extern op_pixels_func avg_no_rnd_pixels_tab[4];

/* sub pixel (encoding) */
extern void (*sub_pixels_tab[4])(DCTELEM *block, const UINT8 *pixels, int line_size, int h);

#define sub_pixels_2(block, pixels, line_size, dxy) \
   sub_pixels_tab[dxy](block, pixels, line_size, 8)

/* motion estimation */

typedef int (*op_pixels_abs_func)(UINT8 *blk1, UINT8 *blk2, int line_size, int h);

extern op_pixels_abs_func pix_abs16x16;
extern op_pixels_abs_func pix_abs16x16_x2;
extern op_pixels_abs_func pix_abs16x16_y2;
extern op_pixels_abs_func pix_abs16x16_xy2;

int pix_abs16x16_c(UINT8 *blk1, UINT8 *blk2, int lx, int h);
int pix_abs16x16_x2_c(UINT8 *blk1, UINT8 *blk2, int lx, int h);
int pix_abs16x16_y2_c(UINT8 *blk1, UINT8 *blk2, int lx, int h);
int pix_abs16x16_xy2_c(UINT8 *blk1, UINT8 *blk2, int lx, int h);

#ifdef HAVE_MMX

#define MM_MMX    0x0001 /* standard MMX */
#define MM_3DNOW  0x0004 /* AMD 3DNOW */
#define MM_MMXEXT 0x0002 /* SSE integer functions or AMD MMX ext */
#define MM_SSE    0x0008 /* SSE functions */
#define MM_SSE2   0x0010 /* PIV SSE2 functions */

extern int mm_flags;

int mm_support(void);

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

void dsputil_init_mmx(void);

#else

#define emms_c()

#define __align8

#endif

#endif
