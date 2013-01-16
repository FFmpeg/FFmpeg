/*
 * Copyright (C) 2003 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2005 Nikolaj Poroshin <porosh3@psu.ru>
 *
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * This implementation is based on an algorithm described in
 * "Aria Nosratinia Embedded Post-Processing for
 * Enhancement of Compressed Images (1999)"
 * (http://citeseer.nj.nec.com/nosratinia99embedded.html)
 * Futher, with splitting (i)dct into hor/ver passes, one of them can be
 * performed once per block, not pixel. This allows for much better speed.
 */

/*
  Heavily optimized version of SPP filter by Nikolaj
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "config.h"

#include "mp_msg.h"
#include "cpudetect.h"
#include "img_format.h"
#include "mp_image.h"
#include "vf.h"
#include "av_helpers.h"
#include "libvo/fastmemcpy.h"

#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"

#undef free
#undef malloc

//===========================================================================//
#define BLOCKSZ 12

static const short custom_threshold[64]=
// values (296) can't be too high
// -it causes too big quant dependence
// or maybe overflow(check), which results in some flashing
{ 71, 296, 295, 237,  71,  40,  38,  19,
  245, 193, 185, 121, 102,  73,  53,  27,
  158, 129, 141, 107,  97,  73,  50,  26,
  102, 116, 109,  98,  82,  66,  45,  23,
  71,  94,  95,  81,  70,  56,  38,  20,
  56,  77,  74,  66,  56,  44,  30,  15,
  38,  53,  50,  45,  38,  30,  21,  11,
  20,  27,  26,  23,  20,  15,  11,   5
};

static const uint8_t  __attribute__((aligned(32))) dither[8][8]={
    {  0,  48,  12,  60,   3,  51,  15,  63, },
    { 32,  16,  44,  28,  35,  19,  47,  31, },
    {  8,  56,   4,  52,  11,  59,   7,  55, },
    { 40,  24,  36,  20,  43,  27,  39,  23, },
    {  2,  50,  14,  62,   1,  49,  13,  61, },
    { 34,  18,  46,  30,  33,  17,  45,  29, },
    { 10,  58,   6,  54,   9,  57,   5,  53, },
    { 42,  26,  38,  22,  41,  25,  37,  21, },
};

struct vf_priv_s { //align 16 !
    uint64_t threshold_mtx_noq[8*2];
    uint64_t threshold_mtx[8*2];//used in both C & MMX (& later SSE2) versions

    int log2_count;
    int temp_stride;
    int qp;
    int mpeg2;
    int prev_q;
    uint8_t *src;
    int16_t *temp;
    int bframes;
    char *non_b_qp;
};


#if !HAVE_MMX

//This func reads from 1 slice, 1 and clears 0 & 1
static void store_slice_c(uint8_t *dst, int16_t *src, int dst_stride, int src_stride, int width, int height, int log2_scale)
{int y, x;
#define STORE(pos)                                                        \
    temp= (src[x + pos] + (d[pos]>>log2_scale))>>(6-log2_scale);        \
    src[x + pos]=src[x + pos - 8*src_stride]=0;                                \
    if(temp & 0x100) temp= ~(temp>>31);                                        \
    dst[x + pos]= temp;

    for(y=0; y<height; y++){
        const uint8_t *d= dither[y];
        for(x=0; x<width; x+=8){
            int temp;
            STORE(0);
            STORE(1);
            STORE(2);
            STORE(3);
            STORE(4);
            STORE(5);
            STORE(6);
            STORE(7);
        }
        src+=src_stride;
        dst+=dst_stride;
    }
}

//This func reads from 2 slices, 0 & 2  and clears 2-nd
static void store_slice2_c(uint8_t *dst, int16_t *src, int dst_stride, int src_stride, int width, int height, int log2_scale)
{int y, x;
#define STORE2(pos)                                                        \
    temp= (src[x + pos] + src[x + pos + 16*src_stride] + (d[pos]>>log2_scale))>>(6-log2_scale);        \
    src[x + pos + 16*src_stride]=0;                                        \
    if(temp & 0x100) temp= ~(temp>>31);                                        \
    dst[x + pos]= temp;

    for(y=0; y<height; y++){
        const uint8_t *d= dither[y];
        for(x=0; x<width; x+=8){
            int temp;
            STORE2(0);
            STORE2(1);
            STORE2(2);
            STORE2(3);
            STORE2(4);
            STORE2(5);
            STORE2(6);
            STORE2(7);
        }
        src+=src_stride;
        dst+=dst_stride;
    }
}

static void mul_thrmat_c(struct vf_priv_s *p,int q)
{
    int a;
    for(a=0;a<64;a++)
        ((short*)p->threshold_mtx)[a]=q * ((short*)p->threshold_mtx_noq)[a];//ints faster in C
}

static void column_fidct_c(int16_t* thr_adr, int16_t *data, int16_t *output, int cnt);
static void row_idct_c(int16_t* workspace,
                       int16_t* output_adr, int output_stride, int cnt);
static void row_fdct_c(int16_t *data, const uint8_t *pixels, int line_size, int cnt);

//this is rather ugly, but there is no need for function pointers
#define store_slice_s store_slice_c
#define store_slice2_s store_slice2_c
#define mul_thrmat_s mul_thrmat_c
#define column_fidct_s column_fidct_c
#define row_idct_s row_idct_c
#define row_fdct_s row_fdct_c

#else /* HAVE_MMX */

//This func reads from 1 slice, 1 and clears 0 & 1
static void store_slice_mmx(uint8_t *dst, int16_t *src, long dst_stride, long src_stride, long width, long height, long log2_scale)
{
    const uint8_t *od=&dither[0][0];
    const uint8_t *end=&dither[height][0];
    width = (width+7)&~7;
    dst_stride-=width;
    //src_stride=(src_stride-width)*2;
    __asm__ volatile(
        "mov %5, %%"REG_d"                \n\t"
        "mov %6, %%"REG_S"                \n\t"
        "mov %7, %%"REG_D"                \n\t"
        "mov %1, %%"REG_a"                \n\t"
        "movd %%"REG_d", %%mm5             \n\t"
        "xor $-1, %%"REG_d"              \n\t"
        "mov %%"REG_a", %%"REG_c"             \n\t"
        "add $7, %%"REG_d"               \n\t"
        "neg %%"REG_a"                   \n\t"
        "sub %0, %%"REG_c"            \n\t"
        "add %%"REG_c", %%"REG_c"             \n\t"
        "movd %%"REG_d", %%mm2             \n\t"
        "mov %%"REG_c", %1       \n\t"
        "mov %2, %%"REG_d"               \n\t"
        "shl $4, %%"REG_a"               \n\t"

        "2:                        \n\t"
        "movq (%%"REG_d"), %%mm3           \n\t"
        "movq %%mm3, %%mm4             \n\t"
        "pxor %%mm7, %%mm7             \n\t"
        "punpcklbw %%mm7, %%mm3        \n\t"
        "punpckhbw %%mm7, %%mm4        \n\t"
        "mov %0, %%"REG_c"            \n\t"
        "psraw %%mm5, %%mm3            \n\t"
        "psraw %%mm5, %%mm4            \n\t"
        "1:                        \n\t"
        "movq %%mm7, (%%"REG_S",%%"REG_a",)     \n\t"
        "movq (%%"REG_S"), %%mm0           \n\t"
        "movq 8(%%"REG_S"), %%mm1          \n\t"

        "movq %%mm7, 8(%%"REG_S",%%"REG_a",)    \n\t"
        "paddw %%mm3, %%mm0            \n\t"
        "paddw %%mm4, %%mm1            \n\t"

        "movq %%mm7, (%%"REG_S")           \n\t"
        "psraw %%mm2, %%mm0            \n\t"
        "psraw %%mm2, %%mm1            \n\t"

        "movq %%mm7, 8(%%"REG_S")          \n\t"
        "packuswb %%mm1, %%mm0         \n\t"
        "add $16, %%"REG_S"              \n\t"

        "movq %%mm0, (%%"REG_D")           \n\t"
        "add $8, %%"REG_D"               \n\t"
        "sub $8, %%"REG_c"               \n\t"
        "jg 1b                      \n\t"
        "add %1, %%"REG_S"       \n\t"
        "add $8, %%"REG_d"               \n\t"
        "add %3, %%"REG_D"       \n\t"
        "cmp %4, %%"REG_d"           \n\t"
        "jl 2b                      \n\t"

        :
        : "m" (width), "m" (src_stride), "erm" (od), "m" (dst_stride), "erm" (end),
          "m" (log2_scale), "m" (src), "m" (dst) //input
        : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_S, "%"REG_D
        );
}

//This func reads from 2 slices, 0 & 2  and clears 2-nd
static void store_slice2_mmx(uint8_t *dst, int16_t *src, long dst_stride, long src_stride, long width, long height, long log2_scale)
{
    const uint8_t *od=&dither[0][0];
    const uint8_t *end=&dither[height][0];
    width = (width+7)&~7;
    dst_stride-=width;
    //src_stride=(src_stride-width)*2;
    __asm__ volatile(
        "mov %5, %%"REG_d"                \n\t"
        "mov %6, %%"REG_S"                \n\t"
        "mov %7, %%"REG_D"                \n\t"
        "mov %1, %%"REG_a"            \n\t"
        "movd %%"REG_d", %%mm5             \n\t"
        "xor $-1, %%"REG_d"              \n\t"
        "mov %%"REG_a", %%"REG_c"             \n\t"
        "add $7, %%"REG_d"               \n\t"
        "sub %0, %%"REG_c"            \n\t"
        "add %%"REG_c", %%"REG_c"             \n\t"
        "movd %%"REG_d", %%mm2             \n\t"
        "mov %%"REG_c", %1       \n\t"
        "mov %2, %%"REG_d"               \n\t"
        "shl $5, %%"REG_a"               \n\t"

        "2:                        \n\t"
        "movq (%%"REG_d"), %%mm3           \n\t"
        "movq %%mm3, %%mm4             \n\t"
        "pxor %%mm7, %%mm7             \n\t"
        "punpcklbw %%mm7, %%mm3        \n\t"
        "punpckhbw %%mm7, %%mm4        \n\t"
        "mov %0, %%"REG_c"            \n\t"
        "psraw %%mm5, %%mm3            \n\t"
        "psraw %%mm5, %%mm4            \n\t"
        "1:                        \n\t"
        "movq (%%"REG_S"), %%mm0           \n\t"
        "movq 8(%%"REG_S"), %%mm1          \n\t"
        "paddw %%mm3, %%mm0            \n\t"

        "paddw (%%"REG_S",%%"REG_a",), %%mm0    \n\t"
        "paddw %%mm4, %%mm1            \n\t"
        "movq 8(%%"REG_S",%%"REG_a",), %%mm6    \n\t"

        "movq %%mm7, (%%"REG_S",%%"REG_a",)     \n\t"
        "psraw %%mm2, %%mm0            \n\t"
        "paddw %%mm6, %%mm1            \n\t"

        "movq %%mm7, 8(%%"REG_S",%%"REG_a",)    \n\t"
        "psraw %%mm2, %%mm1            \n\t"
        "packuswb %%mm1, %%mm0         \n\t"

        "movq %%mm0, (%%"REG_D")           \n\t"
        "add $16, %%"REG_S"              \n\t"
        "add $8, %%"REG_D"               \n\t"
        "sub $8, %%"REG_c"               \n\t"
        "jg 1b                      \n\t"
        "add %1, %%"REG_S"       \n\t"
        "add $8, %%"REG_d"               \n\t"
        "add %3, %%"REG_D"       \n\t"
        "cmp %4, %%"REG_d"           \n\t"
        "jl 2b                      \n\t"

        :
        : "m" (width), "m" (src_stride), "erm" (od), "m" (dst_stride), "erm" (end),
          "m" (log2_scale), "m" (src), "m" (dst) //input
        : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_D, "%"REG_S
        );
}

static void mul_thrmat_mmx(struct vf_priv_s *p, int q)
{
    uint64_t *adr=&p->threshold_mtx_noq[0];
    __asm__ volatile(
        "movd %0, %%mm7                \n\t"
        "add $8*8*2, %%"REG_D"            \n\t"
        "movq 0*8(%%"REG_S"), %%mm0        \n\t"
        "punpcklwd %%mm7, %%mm7        \n\t"
        "movq 1*8(%%"REG_S"), %%mm1        \n\t"
        "punpckldq %%mm7, %%mm7        \n\t"
        "pmullw %%mm7, %%mm0           \n\t"

        "movq 2*8(%%"REG_S"), %%mm2        \n\t"
        "pmullw %%mm7, %%mm1           \n\t"

        "movq 3*8(%%"REG_S"), %%mm3        \n\t"
        "pmullw %%mm7, %%mm2           \n\t"

        "movq %%mm0, 0*8(%%"REG_D")        \n\t"
        "movq 4*8(%%"REG_S"), %%mm4        \n\t"
        "pmullw %%mm7, %%mm3           \n\t"

        "movq %%mm1, 1*8(%%"REG_D")        \n\t"
        "movq 5*8(%%"REG_S"), %%mm5        \n\t"
        "pmullw %%mm7, %%mm4           \n\t"

        "movq %%mm2, 2*8(%%"REG_D")        \n\t"
        "movq 6*8(%%"REG_S"), %%mm6        \n\t"
        "pmullw %%mm7, %%mm5           \n\t"

        "movq %%mm3, 3*8(%%"REG_D")        \n\t"
        "movq 7*8+0*8(%%"REG_S"), %%mm0    \n\t"
        "pmullw %%mm7, %%mm6           \n\t"

        "movq %%mm4, 4*8(%%"REG_D")        \n\t"
        "movq 7*8+1*8(%%"REG_S"), %%mm1    \n\t"
        "pmullw %%mm7, %%mm0           \n\t"

        "movq %%mm5, 5*8(%%"REG_D")        \n\t"
        "movq 7*8+2*8(%%"REG_S"), %%mm2    \n\t"
        "pmullw %%mm7, %%mm1           \n\t"

        "movq %%mm6, 6*8(%%"REG_D")        \n\t"
        "movq 7*8+3*8(%%"REG_S"), %%mm3    \n\t"
        "pmullw %%mm7, %%mm2           \n\t"

        "movq %%mm0, 7*8+0*8(%%"REG_D")    \n\t"
        "movq 7*8+4*8(%%"REG_S"), %%mm4    \n\t"
        "pmullw %%mm7, %%mm3           \n\t"

        "movq %%mm1, 7*8+1*8(%%"REG_D")    \n\t"
        "movq 7*8+5*8(%%"REG_S"), %%mm5    \n\t"
        "pmullw %%mm7, %%mm4           \n\t"

        "movq %%mm2, 7*8+2*8(%%"REG_D")    \n\t"
        "movq 7*8+6*8(%%"REG_S"), %%mm6    \n\t"
        "pmullw %%mm7, %%mm5           \n\t"

        "movq %%mm3, 7*8+3*8(%%"REG_D")    \n\t"
        "movq 14*8+0*8(%%"REG_S"), %%mm0   \n\t"
        "pmullw %%mm7, %%mm6           \n\t"

        "movq %%mm4, 7*8+4*8(%%"REG_D")    \n\t"
        "movq 14*8+1*8(%%"REG_S"), %%mm1   \n\t"
        "pmullw %%mm7, %%mm0           \n\t"

        "movq %%mm5, 7*8+5*8(%%"REG_D")    \n\t"
        "pmullw %%mm7, %%mm1           \n\t"

        "movq %%mm6, 7*8+6*8(%%"REG_D")    \n\t"
        "movq %%mm0, 14*8+0*8(%%"REG_D")   \n\t"
        "movq %%mm1, 14*8+1*8(%%"REG_D")   \n\t"

        : "+g" (q), "+S" (adr), "+D" (adr)
        :
        );
}

static void column_fidct_mmx(int16_t* thr_adr,  int16_t *data,  int16_t *output,  int cnt);
static void row_idct_mmx(int16_t* workspace,
                         int16_t* output_adr,  int output_stride,  int cnt);
static void row_fdct_mmx(int16_t *data,  const uint8_t *pixels,  int line_size,  int cnt);

#define store_slice_s store_slice_mmx
#define store_slice2_s store_slice2_mmx
#define mul_thrmat_s mul_thrmat_mmx
#define column_fidct_s column_fidct_mmx
#define row_idct_s row_idct_mmx
#define row_fdct_s row_fdct_mmx
#endif // HAVE_MMX

static void filter(struct vf_priv_s *p, uint8_t *dst, uint8_t *src,
                   int dst_stride, int src_stride,
                   int width, int height,
                   uint8_t *qp_store, int qp_stride, int is_luma)
{
    int x, x0, y, es, qy, t;
    const int stride= is_luma ? p->temp_stride : (width+16);//((width+16+15)&(~15))
    const int step=6-p->log2_count;
    const int qps= 3 + is_luma;
    int32_t __attribute__((aligned(32))) block_align[4*8*BLOCKSZ+ 4*8*BLOCKSZ];
    int16_t *block= (int16_t *)block_align;
    int16_t *block3=(int16_t *)(block_align+4*8*BLOCKSZ);

    memset(block3, 0, 4*8*BLOCKSZ);

    //p->src=src-src_stride*8-8;//!
    if (!src || !dst) return; // HACK avoid crash for Y8 colourspace
    for(y=0; y<height; y++){
        int index= 8 + 8*stride + y*stride;
        fast_memcpy(p->src + index, src + y*src_stride, width);//this line can be avoided by using DR & user fr.buffers
        for(x=0; x<8; x++){
            p->src[index         - x - 1]= p->src[index +         x    ];
            p->src[index + width + x    ]= p->src[index + width - x - 1];
        }
    }
    for(y=0; y<8; y++){
        fast_memcpy(p->src + (      7-y)*stride, p->src + (      y+8)*stride, stride);
        fast_memcpy(p->src + (height+8+y)*stride, p->src + (height-y+7)*stride, stride);
    }
    //FIXME (try edge emu)

    for(y=8; y<24; y++)
        memset(p->temp+ 8 +y*stride, 0,width*sizeof(int16_t));

    for(y=step; y<height+8; y+=step){    //step= 1,2
        qy=y-4;
        if (qy>height-1) qy=height-1;
        if (qy<0) qy=0;
        qy=(qy>>qps)*qp_stride;
        row_fdct_s(block, p->src + y*stride +2-(y&1), stride, 2);
        for(x0=0; x0<width+8-8*(BLOCKSZ-1); x0+=8*(BLOCKSZ-1)){
            row_fdct_s(block+8*8, p->src + y*stride+8+x0 +2-(y&1), stride, 2*(BLOCKSZ-1));
            if(p->qp)
                column_fidct_s((int16_t*)(&p->threshold_mtx[0]), block+0*8, block3+0*8, 8*(BLOCKSZ-1)); //yes, this is a HOTSPOT
            else
                for (x=0; x<8*(BLOCKSZ-1); x+=8) {
                    t=x+x0-2; //correct t=x+x0-2-(y&1), but its the same
                    if (t<0) t=0;//t always < width-2
                    t=qp_store[qy+(t>>qps)];
                    t=norm_qscale(t, p->mpeg2);
                    if (t!=p->prev_q) p->prev_q=t, mul_thrmat_s(p, t);
                    column_fidct_s((int16_t*)(&p->threshold_mtx[0]), block+x*8, block3+x*8, 8); //yes, this is a HOTSPOT
                }
            row_idct_s(block3+0*8, p->temp + (y&15)*stride+x0+2-(y&1), stride, 2*(BLOCKSZ-1));
            memmove(block, block+(BLOCKSZ-1)*64, 8*8*sizeof(int16_t)); //cycling
            memmove(block3, block3+(BLOCKSZ-1)*64, 6*8*sizeof(int16_t));
        }
        //
        es=width+8-x0; //  8, ...
        if (es>8)
            row_fdct_s(block+8*8, p->src + y*stride+8+x0 +2-(y&1), stride, (es-4)>>2);
        column_fidct_s((int16_t*)(&p->threshold_mtx[0]), block, block3, es&(~1));
        row_idct_s(block3+0*8, p->temp + (y&15)*stride+x0+2-(y&1), stride, es>>2);
        {const int y1=y-8+step;//l5-7  l4-6
            if (!(y1&7) && y1) {
                if (y1&8) store_slice_s(dst + (y1-8)*dst_stride, p->temp+ 8 +8*stride,
                                        dst_stride, stride, width, 8, 5-p->log2_count);
                else store_slice2_s(dst + (y1-8)*dst_stride, p->temp+ 8 +0*stride,
                                    dst_stride, stride, width, 8, 5-p->log2_count);
            } }
    }

    if (y&7) {  // == height & 7
        if (y&8) store_slice_s(dst + ((y-8)&~7)*dst_stride, p->temp+ 8 +8*stride,
                               dst_stride, stride, width, y&7, 5-p->log2_count);
        else store_slice2_s(dst + ((y-8)&~7)*dst_stride, p->temp+ 8 +0*stride,
                            dst_stride, stride, width, y&7, 5-p->log2_count);
    }
}

static int config(struct vf_instance *vf,
                  int width, int height, int d_width, int d_height,
                  unsigned int flags, unsigned int outfmt)
{
    int h= (height+16+15)&(~15);

    vf->priv->temp_stride= (width+16+15)&(~15);
    vf->priv->temp= (int16_t*)av_mallocz(vf->priv->temp_stride*3*8*sizeof(int16_t));
    //this can also be avoided, see above
    vf->priv->src = (uint8_t*)av_malloc(vf->priv->temp_stride*h*sizeof(uint8_t));

    return ff_vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static void get_image(struct vf_instance *vf, mp_image_t *mpi)
{
    if(mpi->flags&MP_IMGFLAG_PRESERVE) return; // don't change
    // ok, we can do pp in-place (or pp disabled):
    vf->dmpi=ff_vf_get_image(vf->next,mpi->imgfmt,
                          mpi->type, mpi->flags, mpi->width, mpi->height);
    mpi->planes[0]=vf->dmpi->planes[0];
    mpi->stride[0]=vf->dmpi->stride[0];
    mpi->width=vf->dmpi->width;
    if(mpi->flags&MP_IMGFLAG_PLANAR){
        mpi->planes[1]=vf->dmpi->planes[1];
        mpi->planes[2]=vf->dmpi->planes[2];
        mpi->stride[1]=vf->dmpi->stride[1];
        mpi->stride[2]=vf->dmpi->stride[2];
    }
    mpi->flags|=MP_IMGFLAG_DIRECT;
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    mp_image_t *dmpi;
    if(!(mpi->flags&MP_IMGFLAG_DIRECT)){
        // no DR, so get a new image! hope we'll get DR buffer:
        dmpi=ff_vf_get_image(vf->next,mpi->imgfmt,
                          MP_IMGTYPE_TEMP,
                          MP_IMGFLAG_ACCEPT_STRIDE|MP_IMGFLAG_PREFER_ALIGNED_STRIDE,
                          mpi->width,mpi->height);
        ff_vf_clone_mpi_attributes(dmpi, mpi);
    }else{
        dmpi=vf->dmpi;
    }

    vf->priv->mpeg2= mpi->qscale_type;
    if(mpi->pict_type != 3 && mpi->qscale && !vf->priv->qp){
        int w = mpi->qstride;
        int h = (mpi->h + 15) >> 4;
        if (!w) {
            w = (mpi->w + 15) >> 4;
            h = 1;
        }
        if(!vf->priv->non_b_qp)
            vf->priv->non_b_qp= malloc(w*h);
        fast_memcpy(vf->priv->non_b_qp, mpi->qscale, w*h);
    }
    if(vf->priv->log2_count || !(mpi->flags&MP_IMGFLAG_DIRECT)){
        char *qp_tab= vf->priv->non_b_qp;
        if(vf->priv->bframes || !qp_tab)
            qp_tab= mpi->qscale;

        if(qp_tab || vf->priv->qp){
            filter(vf->priv, dmpi->planes[0], mpi->planes[0], dmpi->stride[0], mpi->stride[0],
                   mpi->w, mpi->h, qp_tab, mpi->qstride, 1);
            filter(vf->priv, dmpi->planes[1], mpi->planes[1], dmpi->stride[1], mpi->stride[1],
                   mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, qp_tab, mpi->qstride, 0);
            filter(vf->priv, dmpi->planes[2], mpi->planes[2], dmpi->stride[2], mpi->stride[2],
                   mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, qp_tab, mpi->qstride, 0);
        }else{
            memcpy_pic(dmpi->planes[0], mpi->planes[0], mpi->w, mpi->h, dmpi->stride[0], mpi->stride[0]);
            memcpy_pic(dmpi->planes[1], mpi->planes[1], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, dmpi->stride[1], mpi->stride[1]);
            memcpy_pic(dmpi->planes[2], mpi->planes[2], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, dmpi->stride[2], mpi->stride[2]);
        }
    }

#if HAVE_MMX
    if(ff_gCpuCaps.hasMMX) __asm__ volatile ("emms\n\t");
#endif
#if HAVE_MMX2
    if(ff_gCpuCaps.hasMMX2) __asm__ volatile ("sfence\n\t");
#endif
    return ff_vf_next_put_image(vf,dmpi, pts);
}

static void uninit(struct vf_instance *vf)
{
    if(!vf->priv) return;

    av_free(vf->priv->temp);
    vf->priv->temp= NULL;
    av_free(vf->priv->src);
    vf->priv->src= NULL;
    //free(vf->priv->avctx);
    //vf->priv->avctx= NULL;
    free(vf->priv->non_b_qp);
    vf->priv->non_b_qp= NULL;

    av_free(vf->priv);
    vf->priv=NULL;
}

//===========================================================================//

static int query_format(struct vf_instance *vf, unsigned int fmt)
{
    switch(fmt){
    case IMGFMT_YVU9:
    case IMGFMT_IF09:
    case IMGFMT_YV12:
    case IMGFMT_I420:
    case IMGFMT_IYUV:
    case IMGFMT_CLPL:
    case IMGFMT_Y800:
    case IMGFMT_Y8:
    case IMGFMT_444P:
    case IMGFMT_422P:
    case IMGFMT_411P:
        return ff_vf_next_query_format(vf,fmt);
    }
    return 0;
}

static int control(struct vf_instance *vf, int request, void* data)
{
    switch(request){
    case VFCTRL_QUERY_MAX_PP_LEVEL:
        return 5;
    case VFCTRL_SET_PP_LEVEL:
        vf->priv->log2_count= *((unsigned int*)data);
        if (vf->priv->log2_count < 4) vf->priv->log2_count=4;
        return CONTROL_TRUE;
    }
    return ff_vf_next_control(vf,request,data);
}

static int vf_open(vf_instance_t *vf, char *args)
{
    int i=0, bias;
    int custom_threshold_m[64];
    int log2c=-1;

    vf->config=config;
    vf->put_image=put_image;
    vf->get_image=get_image;
    vf->query_format=query_format;
    vf->uninit=uninit;
    vf->control= control;
    vf->priv=av_mallocz(sizeof(struct vf_priv_s));//assumes align 16 !

    ff_init_avcodec();

    //vf->priv->avctx= avcodec_alloc_context();
    //dsputil_init(&vf->priv->dsp, vf->priv->avctx);

    vf->priv->log2_count= 4;
    vf->priv->bframes = 0;

    if (args) sscanf(args, "%d:%d:%d:%d", &log2c, &vf->priv->qp, &i, &vf->priv->bframes);

    if( log2c >=4 && log2c <=5 )
        vf->priv->log2_count = log2c;
    else if( log2c >= 6 )
        vf->priv->log2_count = 5;

    if(vf->priv->qp < 0)
        vf->priv->qp = 0;

    if (i < -15) i = -15;
    if (i > 32) i = 32;

    bias= (1<<4)+i; //regulable
    vf->priv->prev_q=0;
    //
    for(i=0;i<64;i++) //FIXME: tune custom_threshold[] and remove this !
        custom_threshold_m[i]=(int)(custom_threshold[i]*(bias/71.)+ 0.5);
    for(i=0;i<8;i++){
        vf->priv->threshold_mtx_noq[2*i]=(uint64_t)custom_threshold_m[i*8+2]
            |(((uint64_t)custom_threshold_m[i*8+6])<<16)
            |(((uint64_t)custom_threshold_m[i*8+0])<<32)
            |(((uint64_t)custom_threshold_m[i*8+4])<<48);
        vf->priv->threshold_mtx_noq[2*i+1]=(uint64_t)custom_threshold_m[i*8+5]
            |(((uint64_t)custom_threshold_m[i*8+3])<<16)
            |(((uint64_t)custom_threshold_m[i*8+1])<<32)
            |(((uint64_t)custom_threshold_m[i*8+7])<<48);
    }

    if (vf->priv->qp) vf->priv->prev_q=vf->priv->qp, mul_thrmat_s(vf->priv, vf->priv->qp);

    return 1;
}

const vf_info_t ff_vf_info_fspp = {
    "fast simple postprocess",
    "fspp",
    "Michael Niedermayer, Nikolaj Poroshin",
    "",
    vf_open,
    NULL
};

//====================================================================
//Specific spp's dct, idct and threshold functions
//I'd prefer to have them in the separate file.

//#define MANGLE(a) #a

//typedef int16_t int16_t; //! only int16_t

#define DCTSIZE 8
#define DCTSIZE_S "8"

#define FIX(x,s)  ((int) ((x) * (1<<s) + 0.5)&0xffff)
#define C64(x)    ((uint64_t)((x)|(x)<<16))<<32 | (uint64_t)(x) | (uint64_t)(x)<<16
#define FIX64(x,s)  C64(FIX(x,s))

#define MULTIPLY16H(x,k)   (((x)*(k))>>16)
#define THRESHOLD(r,x,t) if(((unsigned)((x)+t))>t*2) r=(x);else r=0;
#define DESCALE(x,n)  (((x) + (1 << ((n)-1))) >> n)

#if HAVE_MMX

DECLARE_ASM_CONST(8, uint64_t, MM_FIX_0_382683433)=FIX64(0.382683433, 14);
DECLARE_ALIGNED(8, uint64_t, ff_MM_FIX_0_541196100)=FIX64(0.541196100, 14);
DECLARE_ALIGNED(8, uint64_t, ff_MM_FIX_0_707106781)=FIX64(0.707106781, 14);
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_1_306562965)=FIX64(1.306562965, 14);

DECLARE_ASM_CONST(8, uint64_t, MM_FIX_1_414213562_A)=FIX64(1.414213562, 14);

DECLARE_ASM_CONST(8, uint64_t, MM_FIX_1_847759065)=FIX64(1.847759065, 13);
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_2_613125930)=FIX64(-2.613125930, 13); //-
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_1_414213562)=FIX64(1.414213562, 13);
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_1_082392200)=FIX64(1.082392200, 13);
//for t3,t5,t7 == 0 shortcut
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_0_847759065)=FIX64(0.847759065, 14);
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_0_566454497)=FIX64(0.566454497, 14);
DECLARE_ASM_CONST(8, uint64_t, MM_FIX_0_198912367)=FIX64(0.198912367, 14);

DECLARE_ASM_CONST(8, uint64_t, MM_DESCALE_RND)=C64(4);
DECLARE_ASM_CONST(8, uint64_t, MM_2)=C64(2);

#else /* !HAVE_MMX */

typedef int32_t int_simd16_t;
static const int16_t FIX_0_382683433=FIX(0.382683433, 14);
static const int16_t FIX_0_541196100=FIX(0.541196100, 14);
static const int16_t FIX_0_707106781=FIX(0.707106781, 14);
static const int16_t FIX_1_306562965=FIX(1.306562965, 14);
static const int16_t FIX_1_414213562_A=FIX(1.414213562, 14);
static const int16_t FIX_1_847759065=FIX(1.847759065, 13);
static const int16_t FIX_2_613125930=FIX(-2.613125930, 13); //-
static const int16_t FIX_1_414213562=FIX(1.414213562, 13);
static const int16_t FIX_1_082392200=FIX(1.082392200, 13);

#endif

#if !HAVE_MMX

static void column_fidct_c(int16_t* thr_adr, int16_t *data, int16_t *output, int cnt)
{
    int_simd16_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    int_simd16_t tmp10, tmp11, tmp12, tmp13;
    int_simd16_t z1,z2,z3,z4,z5, z10, z11, z12, z13;
    int_simd16_t d0, d1, d2, d3, d4, d5, d6, d7;

    int16_t* dataptr;
    int16_t* wsptr;
    int16_t *threshold;
    int ctr;

    dataptr = data;
    wsptr = output;

    for (; cnt > 0; cnt-=2) { //start positions
        threshold=(int16_t*)thr_adr;//threshold_mtx
        for (ctr = DCTSIZE; ctr > 0; ctr--) {
            // Process columns from input, add to output.
            tmp0 = dataptr[DCTSIZE*0] + dataptr[DCTSIZE*7];
            tmp7 = dataptr[DCTSIZE*0] - dataptr[DCTSIZE*7];

            tmp1 = dataptr[DCTSIZE*1] + dataptr[DCTSIZE*6];
            tmp6 = dataptr[DCTSIZE*1] - dataptr[DCTSIZE*6];

            tmp2 = dataptr[DCTSIZE*2] + dataptr[DCTSIZE*5];
            tmp5 = dataptr[DCTSIZE*2] - dataptr[DCTSIZE*5];

            tmp3 = dataptr[DCTSIZE*3] + dataptr[DCTSIZE*4];
            tmp4 = dataptr[DCTSIZE*3] - dataptr[DCTSIZE*4];

            // Even part of FDCT

            tmp10 = tmp0 + tmp3;
            tmp13 = tmp0 - tmp3;
            tmp11 = tmp1 + tmp2;
            tmp12 = tmp1 - tmp2;

            d0 = tmp10 + tmp11;
            d4 = tmp10 - tmp11;

            z1 = MULTIPLY16H((tmp12 + tmp13) <<2, FIX_0_707106781);
            d2 = tmp13 + z1;
            d6 = tmp13 - z1;

            // Even part of IDCT

            THRESHOLD(tmp0, d0, threshold[0*8]);
            THRESHOLD(tmp1, d2, threshold[2*8]);
            THRESHOLD(tmp2, d4, threshold[4*8]);
            THRESHOLD(tmp3, d6, threshold[6*8]);
            tmp0+=2;
            tmp10 = (tmp0 + tmp2)>>2;
            tmp11 = (tmp0 - tmp2)>>2;

            tmp13 = (tmp1 + tmp3)>>2; //+2 !  (psnr decides)
            tmp12 = MULTIPLY16H((tmp1 - tmp3), FIX_1_414213562_A) - tmp13; //<<2

            tmp0 = tmp10 + tmp13; //->temps
            tmp3 = tmp10 - tmp13; //->temps
            tmp1 = tmp11 + tmp12; //->temps
            tmp2 = tmp11 - tmp12; //->temps

            // Odd part of FDCT

            tmp10 = tmp4 + tmp5;
            tmp11 = tmp5 + tmp6;
            tmp12 = tmp6 + tmp7;

            z5 = MULTIPLY16H((tmp10 - tmp12)<<2, FIX_0_382683433);
            z2 = MULTIPLY16H(tmp10 <<2, FIX_0_541196100) + z5;
            z4 = MULTIPLY16H(tmp12 <<2, FIX_1_306562965) + z5;
            z3 = MULTIPLY16H(tmp11 <<2, FIX_0_707106781);

            z11 = tmp7 + z3;
            z13 = tmp7 - z3;

            d5 = z13 + z2;
            d3 = z13 - z2;
            d1 = z11 + z4;
            d7 = z11 - z4;

            // Odd part of IDCT

            THRESHOLD(tmp4, d1, threshold[1*8]);
            THRESHOLD(tmp5, d3, threshold[3*8]);
            THRESHOLD(tmp6, d5, threshold[5*8]);
            THRESHOLD(tmp7, d7, threshold[7*8]);

            //Simd version uses here a shortcut for the tmp5,tmp6,tmp7 == 0
            z13 = tmp6 + tmp5;
            z10 = (tmp6 - tmp5)<<1;
            z11 = tmp4 + tmp7;
            z12 = (tmp4 - tmp7)<<1;

            tmp7 = (z11 + z13)>>2; //+2 !
            tmp11 = MULTIPLY16H((z11 - z13)<<1, FIX_1_414213562);
            z5 =    MULTIPLY16H(z10 + z12, FIX_1_847759065);
            tmp10 = MULTIPLY16H(z12, FIX_1_082392200) - z5;
            tmp12 = MULTIPLY16H(z10, FIX_2_613125930) + z5; // - !!

            tmp6 = tmp12 - tmp7;
            tmp5 = tmp11 - tmp6;
            tmp4 = tmp10 + tmp5;

            wsptr[DCTSIZE*0]+=  (tmp0 + tmp7);
            wsptr[DCTSIZE*1]+=  (tmp1 + tmp6);
            wsptr[DCTSIZE*2]+=  (tmp2 + tmp5);
            wsptr[DCTSIZE*3]+=  (tmp3 - tmp4);
            wsptr[DCTSIZE*4]+=  (tmp3 + tmp4);
            wsptr[DCTSIZE*5]+=  (tmp2 - tmp5);
            wsptr[DCTSIZE*6]=  (tmp1 - tmp6);
            wsptr[DCTSIZE*7]=  (tmp0 - tmp7);
            //
            dataptr++; //next column
            wsptr++;
            threshold++;
        }
        dataptr+=8; //skip each second start pos
        wsptr  +=8;
    }
}

#else /* HAVE_MMX */

static void column_fidct_mmx(int16_t* thr_adr,  int16_t *data,  int16_t *output,  int cnt)
{
    uint64_t __attribute__((aligned(8))) temps[4];
    __asm__ volatile(
        ASMALIGN(4)
        "1:                   \n\t"
        "movq "DCTSIZE_S"*0*2(%%"REG_S"), %%mm1 \n\t"
        //
        "movq "DCTSIZE_S"*3*2(%%"REG_S"), %%mm7 \n\t"
        "movq %%mm1, %%mm0             \n\t"

        "paddw "DCTSIZE_S"*7*2(%%"REG_S"), %%mm1 \n\t" //t0
        "movq %%mm7, %%mm3             \n\t"

        "paddw "DCTSIZE_S"*4*2(%%"REG_S"), %%mm7 \n\t" //t3
        "movq %%mm1, %%mm5             \n\t"

        "movq "DCTSIZE_S"*1*2(%%"REG_S"), %%mm6 \n\t"
        "psubw %%mm7, %%mm1            \n\t" //t13

        "movq "DCTSIZE_S"*2*2(%%"REG_S"), %%mm2 \n\t"
        "movq %%mm6, %%mm4             \n\t"

        "paddw "DCTSIZE_S"*6*2(%%"REG_S"), %%mm6 \n\t" //t1
        "paddw %%mm7, %%mm5            \n\t" //t10

        "paddw "DCTSIZE_S"*5*2(%%"REG_S"), %%mm2 \n\t" //t2
        "movq %%mm6, %%mm7             \n\t"

        "paddw %%mm2, %%mm6            \n\t" //t11
        "psubw %%mm2, %%mm7            \n\t" //t12

        "movq %%mm5, %%mm2             \n\t"
        "paddw %%mm6, %%mm5            \n\t" //d0
        // i0 t13 t12 i3 i1 d0 - d4
        "psubw %%mm6, %%mm2            \n\t" //d4
        "paddw %%mm1, %%mm7            \n\t"

        "movq  4*16(%%"REG_d"), %%mm6      \n\t"
        "psllw $2, %%mm7              \n\t"

        "psubw 0*16(%%"REG_d"), %%mm5      \n\t"
        "psubw %%mm6, %%mm2            \n\t"

        "paddusw 0*16(%%"REG_d"), %%mm5    \n\t"
        "paddusw %%mm6, %%mm2          \n\t"

        "pmulhw "MANGLE(ff_MM_FIX_0_707106781)", %%mm7 \n\t"
        //
        "paddw 0*16(%%"REG_d"), %%mm5      \n\t"
        "paddw %%mm6, %%mm2            \n\t"

        "psubusw 0*16(%%"REG_d"), %%mm5    \n\t"
        "psubusw %%mm6, %%mm2          \n\t"

//This func is totally compute-bound,  operates at huge speed. So,  DC shortcut
// at this place isn't worthwhile due to BTB miss penalty (checked on Pent. 3).
//However,  typical numbers: nondc - 29%%,  dc - 46%%,  zero - 25%%. All <> 0 case is very rare.
        "paddw "MANGLE(MM_2)", %%mm5            \n\t"
        "movq %%mm2, %%mm6             \n\t"

        "paddw %%mm5, %%mm2            \n\t"
        "psubw %%mm6, %%mm5            \n\t"

        "movq %%mm1, %%mm6             \n\t"
        "paddw %%mm7, %%mm1            \n\t" //d2

        "psubw 2*16(%%"REG_d"), %%mm1      \n\t"
        "psubw %%mm7, %%mm6            \n\t" //d6

        "movq 6*16(%%"REG_d"), %%mm7       \n\t"
        "psraw $2, %%mm5              \n\t"

        "paddusw 2*16(%%"REG_d"), %%mm1    \n\t"
        "psubw %%mm7, %%mm6            \n\t"
        // t7 d2 /t11 t4 t6 - d6 /t10

        "paddw 2*16(%%"REG_d"), %%mm1      \n\t"
        "paddusw %%mm7, %%mm6          \n\t"

        "psubusw 2*16(%%"REG_d"), %%mm1    \n\t"
        "paddw %%mm7, %%mm6            \n\t"

        "psubw "DCTSIZE_S"*4*2(%%"REG_S"), %%mm3 \n\t"
        "psubusw %%mm7, %%mm6          \n\t"

        //movq [edi+"DCTSIZE_S"*2*2], mm1
        //movq [edi+"DCTSIZE_S"*6*2], mm6
        "movq %%mm1, %%mm7             \n\t"
        "psraw $2, %%mm2              \n\t"

        "psubw "DCTSIZE_S"*6*2(%%"REG_S"), %%mm4 \n\t"
        "psubw %%mm6, %%mm1            \n\t"

        "psubw "DCTSIZE_S"*7*2(%%"REG_S"), %%mm0 \n\t"
        "paddw %%mm7, %%mm6            \n\t" //'t13

        "psraw $2, %%mm6              \n\t" //paddw mm6, MM_2 !!    ---
        "movq %%mm2, %%mm7             \n\t"

        "pmulhw "MANGLE(MM_FIX_1_414213562_A)", %%mm1 \n\t"
        "paddw %%mm6, %%mm2            \n\t" //'t0

        "movq %%mm2, 0*8+%3            \n\t" //!
        "psubw %%mm6, %%mm7            \n\t" //'t3

        "movq "DCTSIZE_S"*2*2(%%"REG_S"), %%mm2 \n\t"
        "psubw %%mm6, %%mm1            \n\t" //'t12

        "psubw "DCTSIZE_S"*5*2(%%"REG_S"), %%mm2 \n\t" //t5
        "movq %%mm5, %%mm6             \n\t"

        "movq %%mm7, 3*8+%3            \n\t"
        "paddw %%mm2, %%mm3            \n\t" //t10

        "paddw %%mm4, %%mm2            \n\t" //t11
        "paddw %%mm0, %%mm4            \n\t" //t12

        "movq %%mm3, %%mm7             \n\t"
        "psubw %%mm4, %%mm3            \n\t"

        "psllw $2, %%mm3              \n\t"
        "psllw $2, %%mm7              \n\t" //opt for P6

        "pmulhw "MANGLE(MM_FIX_0_382683433)", %%mm3 \n\t"
        "psllw $2, %%mm4              \n\t"

        "pmulhw "MANGLE(ff_MM_FIX_0_541196100)", %%mm7 \n\t"
        "psllw $2, %%mm2              \n\t"

        "pmulhw "MANGLE(MM_FIX_1_306562965)", %%mm4 \n\t"
        "paddw %%mm1, %%mm5            \n\t" //'t1

        "pmulhw "MANGLE(ff_MM_FIX_0_707106781)", %%mm2 \n\t"
        "psubw %%mm1, %%mm6            \n\t" //'t2
        // t7 't12 't11 t4 t6 - 't13 't10   ---

        "paddw %%mm3, %%mm7            \n\t" //z2

        "movq %%mm5, 1*8+%3            \n\t"
        "paddw %%mm3, %%mm4            \n\t" //z4

        "movq 3*16(%%"REG_d"), %%mm3       \n\t"
        "movq %%mm0, %%mm1             \n\t"

        "movq %%mm6, 2*8+%3            \n\t"
        "psubw %%mm2, %%mm1            \n\t" //z13

//===
        "paddw %%mm2, %%mm0            \n\t" //z11
        "movq %%mm1, %%mm5             \n\t"

        "movq 5*16(%%"REG_d"), %%mm2       \n\t"
        "psubw %%mm7, %%mm1            \n\t" //d3

        "paddw %%mm7, %%mm5            \n\t" //d5
        "psubw %%mm3, %%mm1            \n\t"

        "movq 1*16(%%"REG_d"), %%mm7       \n\t"
        "psubw %%mm2, %%mm5            \n\t"

        "movq %%mm0, %%mm6             \n\t"
        "paddw %%mm4, %%mm0            \n\t" //d1

        "paddusw %%mm3, %%mm1          \n\t"
        "psubw %%mm4, %%mm6            \n\t" //d7

        // d1 d3 - - - d5 d7 -
        "movq 7*16(%%"REG_d"), %%mm4       \n\t"
        "psubw %%mm7, %%mm0            \n\t"

        "psubw %%mm4, %%mm6            \n\t"
        "paddusw %%mm2, %%mm5          \n\t"

        "paddusw %%mm4, %%mm6          \n\t"
        "paddw %%mm3, %%mm1            \n\t"

        "paddw %%mm2, %%mm5            \n\t"
        "paddw %%mm4, %%mm6            \n\t"

        "psubusw %%mm3, %%mm1          \n\t"
        "psubusw %%mm2, %%mm5          \n\t"

        "psubusw %%mm4, %%mm6          \n\t"
        "movq %%mm1, %%mm4             \n\t"

        "por %%mm5, %%mm4              \n\t"
        "paddusw %%mm7, %%mm0          \n\t"

        "por %%mm6, %%mm4              \n\t"
        "paddw %%mm7, %%mm0            \n\t"

        "packssdw %%mm4, %%mm4         \n\t"
        "psubusw %%mm7, %%mm0          \n\t"

        "movd %%mm4, %%"REG_a"             \n\t"
        "or %%"REG_a", %%"REG_a"              \n\t"
        "jnz 2f                 \n\t"
        //movq [edi+"DCTSIZE_S"*3*2], mm1
        //movq [edi+"DCTSIZE_S"*5*2], mm5
        //movq [edi+"DCTSIZE_S"*1*2], mm0
        //movq [edi+"DCTSIZE_S"*7*2], mm6
        // t4 t5 - - - t6 t7 -
        //--- t4 (mm0) may be <>0; mm1, mm5, mm6 == 0
//Typical numbers: nondc - 19%%,  dc - 26%%,  zero - 55%%. zero case alone isn't worthwhile
        "movq 0*8+%3, %%mm4            \n\t"
        "movq %%mm0, %%mm1             \n\t"

        "pmulhw "MANGLE(MM_FIX_0_847759065)", %%mm0 \n\t" //tmp6
        "movq %%mm1, %%mm2             \n\t"

        "movq "DCTSIZE_S"*0*2(%%"REG_D"), %%mm5 \n\t"
        "movq %%mm2, %%mm3             \n\t"

        "pmulhw "MANGLE(MM_FIX_0_566454497)", %%mm1 \n\t" //tmp5
        "paddw %%mm4, %%mm5            \n\t"

        "movq 1*8+%3, %%mm6            \n\t"
        //paddw mm3, MM_2
        "psraw $2, %%mm3              \n\t" //tmp7

        "pmulhw "MANGLE(MM_FIX_0_198912367)", %%mm2 \n\t" //-tmp4
        "psubw %%mm3, %%mm4            \n\t"

        "movq "DCTSIZE_S"*1*2(%%"REG_D"), %%mm7 \n\t"
        "paddw %%mm3, %%mm5            \n\t"

        "movq %%mm4, "DCTSIZE_S"*7*2(%%"REG_D") \n\t"
        "paddw %%mm6, %%mm7            \n\t"

        "movq 2*8+%3, %%mm3            \n\t"
        "psubw %%mm0, %%mm6            \n\t"

        "movq "DCTSIZE_S"*2*2(%%"REG_D"), %%mm4 \n\t"
        "paddw %%mm0, %%mm7            \n\t"

        "movq %%mm5, "DCTSIZE_S"*0*2(%%"REG_D") \n\t"
        "paddw %%mm3, %%mm4            \n\t"

        "movq %%mm6, "DCTSIZE_S"*6*2(%%"REG_D") \n\t"
        "psubw %%mm1, %%mm3            \n\t"

        "movq "DCTSIZE_S"*5*2(%%"REG_D"), %%mm5 \n\t"
        "paddw %%mm1, %%mm4            \n\t"

        "movq "DCTSIZE_S"*3*2(%%"REG_D"), %%mm6 \n\t"
        "paddw %%mm3, %%mm5            \n\t"

        "movq 3*8+%3, %%mm0            \n\t"
        "add $8, %%"REG_S"               \n\t"

        "movq %%mm7, "DCTSIZE_S"*1*2(%%"REG_D") \n\t"
        "paddw %%mm0, %%mm6            \n\t"

        "movq %%mm4, "DCTSIZE_S"*2*2(%%"REG_D") \n\t"
        "psubw %%mm2, %%mm0            \n\t"

        "movq "DCTSIZE_S"*4*2(%%"REG_D"), %%mm7 \n\t"
        "paddw %%mm2, %%mm6            \n\t"

        "movq %%mm5, "DCTSIZE_S"*5*2(%%"REG_D") \n\t"
        "paddw %%mm0, %%mm7            \n\t"

        "movq %%mm6, "DCTSIZE_S"*3*2(%%"REG_D") \n\t"

        "movq %%mm7, "DCTSIZE_S"*4*2(%%"REG_D") \n\t"
        "add $8, %%"REG_D"               \n\t"
        "jmp 4f                  \n\t"

        "2:                    \n\t"
        //--- non DC2
        //psraw mm1, 2 w/o it -> offset. thr1, thr1, thr1  (actually thr1, thr1, thr1-1)
        //psraw mm5, 2
        //psraw mm0, 2
        //psraw mm6, 2
        "movq %%mm5, %%mm3             \n\t"
        "psubw %%mm1, %%mm5            \n\t"

        "psllw $1, %%mm5              \n\t" //'z10
        "paddw %%mm1, %%mm3            \n\t" //'z13

        "movq %%mm0, %%mm2             \n\t"
        "psubw %%mm6, %%mm0            \n\t"

        "movq %%mm5, %%mm1             \n\t"
        "psllw $1, %%mm0              \n\t" //'z12

        "pmulhw "MANGLE(MM_FIX_2_613125930)", %%mm1 \n\t" //-
        "paddw %%mm0, %%mm5            \n\t"

        "pmulhw "MANGLE(MM_FIX_1_847759065)", %%mm5 \n\t" //'z5
        "paddw %%mm6, %%mm2            \n\t" //'z11

        "pmulhw "MANGLE(MM_FIX_1_082392200)", %%mm0 \n\t"
        "movq %%mm2, %%mm7             \n\t"

        //---
        "movq 0*8+%3, %%mm4            \n\t"
        "psubw %%mm3, %%mm2            \n\t"

        "psllw $1, %%mm2              \n\t"
        "paddw %%mm3, %%mm7            \n\t" //'t7

        "pmulhw "MANGLE(MM_FIX_1_414213562)", %%mm2 \n\t" //'t11
        "movq %%mm4, %%mm6             \n\t"
        //paddw mm7, MM_2
        "psraw $2, %%mm7              \n\t"

        "paddw "DCTSIZE_S"*0*2(%%"REG_D"), %%mm4 \n\t"
        "psubw %%mm7, %%mm6            \n\t"

        "movq 1*8+%3, %%mm3            \n\t"
        "paddw %%mm7, %%mm4            \n\t"

        "movq %%mm6, "DCTSIZE_S"*7*2(%%"REG_D") \n\t"
        "paddw %%mm5, %%mm1            \n\t" //'t12

        "movq %%mm4, "DCTSIZE_S"*0*2(%%"REG_D") \n\t"
        "psubw %%mm7, %%mm1            \n\t" //'t6

        "movq 2*8+%3, %%mm7            \n\t"
        "psubw %%mm5, %%mm0            \n\t" //'t10

        "movq 3*8+%3, %%mm6            \n\t"
        "movq %%mm3, %%mm5             \n\t"

        "paddw "DCTSIZE_S"*1*2(%%"REG_D"), %%mm3 \n\t"
        "psubw %%mm1, %%mm5            \n\t"

        "psubw %%mm1, %%mm2            \n\t" //'t5
        "paddw %%mm1, %%mm3            \n\t"

        "movq %%mm5, "DCTSIZE_S"*6*2(%%"REG_D") \n\t"
        "movq %%mm7, %%mm4             \n\t"

        "paddw "DCTSIZE_S"*2*2(%%"REG_D"), %%mm7 \n\t"
        "psubw %%mm2, %%mm4            \n\t"

        "paddw "DCTSIZE_S"*5*2(%%"REG_D"), %%mm4 \n\t"
        "paddw %%mm2, %%mm7            \n\t"

        "movq %%mm3, "DCTSIZE_S"*1*2(%%"REG_D") \n\t"
        "paddw %%mm2, %%mm0            \n\t" //'t4

        // 't4 't6 't5 - - - - 't7
        "movq %%mm7, "DCTSIZE_S"*2*2(%%"REG_D") \n\t"
        "movq %%mm6, %%mm1             \n\t"

        "paddw "DCTSIZE_S"*4*2(%%"REG_D"), %%mm6 \n\t"
        "psubw %%mm0, %%mm1            \n\t"

        "paddw "DCTSIZE_S"*3*2(%%"REG_D"), %%mm1 \n\t"
        "paddw %%mm0, %%mm6            \n\t"

        "movq %%mm4, "DCTSIZE_S"*5*2(%%"REG_D") \n\t"
        "add $8, %%"REG_S"               \n\t"

        "movq %%mm6, "DCTSIZE_S"*4*2(%%"REG_D") \n\t"

        "movq %%mm1, "DCTSIZE_S"*3*2(%%"REG_D") \n\t"
        "add $8, %%"REG_D"               \n\t"

        "4:                     \n\t"
//=part 2 (the same)===========================================================
        "movq "DCTSIZE_S"*0*2(%%"REG_S"), %%mm1 \n\t"
        //
        "movq "DCTSIZE_S"*3*2(%%"REG_S"), %%mm7 \n\t"
        "movq %%mm1, %%mm0             \n\t"

        "paddw "DCTSIZE_S"*7*2(%%"REG_S"), %%mm1 \n\t" //t0
        "movq %%mm7, %%mm3             \n\t"

        "paddw "DCTSIZE_S"*4*2(%%"REG_S"), %%mm7 \n\t" //t3
        "movq %%mm1, %%mm5             \n\t"

        "movq "DCTSIZE_S"*1*2(%%"REG_S"), %%mm6 \n\t"
        "psubw %%mm7, %%mm1            \n\t" //t13

        "movq "DCTSIZE_S"*2*2(%%"REG_S"), %%mm2 \n\t"
        "movq %%mm6, %%mm4             \n\t"

        "paddw "DCTSIZE_S"*6*2(%%"REG_S"), %%mm6 \n\t" //t1
        "paddw %%mm7, %%mm5            \n\t" //t10

        "paddw "DCTSIZE_S"*5*2(%%"REG_S"), %%mm2 \n\t" //t2
        "movq %%mm6, %%mm7             \n\t"

        "paddw %%mm2, %%mm6            \n\t" //t11
        "psubw %%mm2, %%mm7            \n\t" //t12

        "movq %%mm5, %%mm2             \n\t"
        "paddw %%mm6, %%mm5            \n\t" //d0
        // i0 t13 t12 i3 i1 d0 - d4
        "psubw %%mm6, %%mm2            \n\t" //d4
        "paddw %%mm1, %%mm7            \n\t"

        "movq  1*8+4*16(%%"REG_d"), %%mm6  \n\t"
        "psllw $2, %%mm7              \n\t"

        "psubw 1*8+0*16(%%"REG_d"), %%mm5  \n\t"
        "psubw %%mm6, %%mm2            \n\t"

        "paddusw 1*8+0*16(%%"REG_d"), %%mm5 \n\t"
        "paddusw %%mm6, %%mm2          \n\t"

        "pmulhw "MANGLE(ff_MM_FIX_0_707106781)", %%mm7 \n\t"
        //
        "paddw 1*8+0*16(%%"REG_d"), %%mm5  \n\t"
        "paddw %%mm6, %%mm2            \n\t"

        "psubusw 1*8+0*16(%%"REG_d"), %%mm5 \n\t"
        "psubusw %%mm6, %%mm2          \n\t"

//This func is totally compute-bound,  operates at huge speed. So,  DC shortcut
// at this place isn't worthwhile due to BTB miss penalty (checked on Pent. 3).
//However,  typical numbers: nondc - 29%%,  dc - 46%%,  zero - 25%%. All <> 0 case is very rare.
        "paddw "MANGLE(MM_2)", %%mm5            \n\t"
        "movq %%mm2, %%mm6             \n\t"

        "paddw %%mm5, %%mm2            \n\t"
        "psubw %%mm6, %%mm5            \n\t"

        "movq %%mm1, %%mm6             \n\t"
        "paddw %%mm7, %%mm1            \n\t" //d2

        "psubw 1*8+2*16(%%"REG_d"), %%mm1  \n\t"
        "psubw %%mm7, %%mm6            \n\t" //d6

        "movq 1*8+6*16(%%"REG_d"), %%mm7   \n\t"
        "psraw $2, %%mm5              \n\t"

        "paddusw 1*8+2*16(%%"REG_d"), %%mm1 \n\t"
        "psubw %%mm7, %%mm6            \n\t"
        // t7 d2 /t11 t4 t6 - d6 /t10

        "paddw 1*8+2*16(%%"REG_d"), %%mm1  \n\t"
        "paddusw %%mm7, %%mm6          \n\t"

        "psubusw 1*8+2*16(%%"REG_d"), %%mm1 \n\t"
        "paddw %%mm7, %%mm6            \n\t"

        "psubw "DCTSIZE_S"*4*2(%%"REG_S"), %%mm3 \n\t"
        "psubusw %%mm7, %%mm6          \n\t"

        //movq [edi+"DCTSIZE_S"*2*2], mm1
        //movq [edi+"DCTSIZE_S"*6*2], mm6
        "movq %%mm1, %%mm7             \n\t"
        "psraw $2, %%mm2              \n\t"

        "psubw "DCTSIZE_S"*6*2(%%"REG_S"), %%mm4 \n\t"
        "psubw %%mm6, %%mm1            \n\t"

        "psubw "DCTSIZE_S"*7*2(%%"REG_S"), %%mm0 \n\t"
        "paddw %%mm7, %%mm6            \n\t" //'t13

        "psraw $2, %%mm6              \n\t" //paddw mm6, MM_2 !!    ---
        "movq %%mm2, %%mm7             \n\t"

        "pmulhw "MANGLE(MM_FIX_1_414213562_A)", %%mm1 \n\t"
        "paddw %%mm6, %%mm2            \n\t" //'t0

        "movq %%mm2, 0*8+%3            \n\t" //!
        "psubw %%mm6, %%mm7            \n\t" //'t3

        "movq "DCTSIZE_S"*2*2(%%"REG_S"), %%mm2 \n\t"
        "psubw %%mm6, %%mm1            \n\t" //'t12

        "psubw "DCTSIZE_S"*5*2(%%"REG_S"), %%mm2 \n\t" //t5
        "movq %%mm5, %%mm6             \n\t"

        "movq %%mm7, 3*8+%3            \n\t"
        "paddw %%mm2, %%mm3            \n\t" //t10

        "paddw %%mm4, %%mm2            \n\t" //t11
        "paddw %%mm0, %%mm4            \n\t" //t12

        "movq %%mm3, %%mm7             \n\t"
        "psubw %%mm4, %%mm3            \n\t"

        "psllw $2, %%mm3              \n\t"
        "psllw $2, %%mm7              \n\t" //opt for P6

        "pmulhw "MANGLE(MM_FIX_0_382683433)", %%mm3 \n\t"
        "psllw $2, %%mm4              \n\t"

        "pmulhw "MANGLE(ff_MM_FIX_0_541196100)", %%mm7 \n\t"
        "psllw $2, %%mm2              \n\t"

        "pmulhw "MANGLE(MM_FIX_1_306562965)", %%mm4 \n\t"
        "paddw %%mm1, %%mm5            \n\t" //'t1

        "pmulhw "MANGLE(ff_MM_FIX_0_707106781)", %%mm2 \n\t"
        "psubw %%mm1, %%mm6            \n\t" //'t2
        // t7 't12 't11 t4 t6 - 't13 't10   ---

        "paddw %%mm3, %%mm7            \n\t" //z2

        "movq %%mm5, 1*8+%3            \n\t"
        "paddw %%mm3, %%mm4            \n\t" //z4

        "movq 1*8+3*16(%%"REG_d"), %%mm3   \n\t"
        "movq %%mm0, %%mm1             \n\t"

        "movq %%mm6, 2*8+%3            \n\t"
        "psubw %%mm2, %%mm1            \n\t" //z13

//===
        "paddw %%mm2, %%mm0            \n\t" //z11
        "movq %%mm1, %%mm5             \n\t"

        "movq 1*8+5*16(%%"REG_d"), %%mm2   \n\t"
        "psubw %%mm7, %%mm1            \n\t" //d3

        "paddw %%mm7, %%mm5            \n\t" //d5
        "psubw %%mm3, %%mm1            \n\t"

        "movq 1*8+1*16(%%"REG_d"), %%mm7   \n\t"
        "psubw %%mm2, %%mm5            \n\t"

        "movq %%mm0, %%mm6             \n\t"
        "paddw %%mm4, %%mm0            \n\t" //d1

        "paddusw %%mm3, %%mm1          \n\t"
        "psubw %%mm4, %%mm6            \n\t" //d7

        // d1 d3 - - - d5 d7 -
        "movq 1*8+7*16(%%"REG_d"), %%mm4   \n\t"
        "psubw %%mm7, %%mm0            \n\t"

        "psubw %%mm4, %%mm6            \n\t"
        "paddusw %%mm2, %%mm5          \n\t"

        "paddusw %%mm4, %%mm6          \n\t"
        "paddw %%mm3, %%mm1            \n\t"

        "paddw %%mm2, %%mm5            \n\t"
        "paddw %%mm4, %%mm6            \n\t"

        "psubusw %%mm3, %%mm1          \n\t"
        "psubusw %%mm2, %%mm5          \n\t"

        "psubusw %%mm4, %%mm6          \n\t"
        "movq %%mm1, %%mm4             \n\t"

        "por %%mm5, %%mm4              \n\t"
        "paddusw %%mm7, %%mm0          \n\t"

        "por %%mm6, %%mm4              \n\t"
        "paddw %%mm7, %%mm0            \n\t"

        "packssdw %%mm4, %%mm4         \n\t"
        "psubusw %%mm7, %%mm0          \n\t"

        "movd %%mm4, %%"REG_a"             \n\t"
        "or %%"REG_a", %%"REG_a"              \n\t"
        "jnz 3f                 \n\t"
        //movq [edi+"DCTSIZE_S"*3*2], mm1
        //movq [edi+"DCTSIZE_S"*5*2], mm5
        //movq [edi+"DCTSIZE_S"*1*2], mm0
        //movq [edi+"DCTSIZE_S"*7*2], mm6
        // t4 t5 - - - t6 t7 -
        //--- t4 (mm0) may be <>0; mm1, mm5, mm6 == 0
//Typical numbers: nondc - 19%%,  dc - 26%%,  zero - 55%%. zero case alone isn't worthwhile
        "movq 0*8+%3, %%mm4            \n\t"
        "movq %%mm0, %%mm1             \n\t"

        "pmulhw "MANGLE(MM_FIX_0_847759065)", %%mm0 \n\t" //tmp6
        "movq %%mm1, %%mm2             \n\t"

        "movq "DCTSIZE_S"*0*2(%%"REG_D"), %%mm5 \n\t"
        "movq %%mm2, %%mm3             \n\t"

        "pmulhw "MANGLE(MM_FIX_0_566454497)", %%mm1 \n\t" //tmp5
        "paddw %%mm4, %%mm5            \n\t"

        "movq 1*8+%3, %%mm6            \n\t"
        //paddw mm3, MM_2
        "psraw $2, %%mm3              \n\t" //tmp7

        "pmulhw "MANGLE(MM_FIX_0_198912367)", %%mm2 \n\t" //-tmp4
        "psubw %%mm3, %%mm4            \n\t"

        "movq "DCTSIZE_S"*1*2(%%"REG_D"), %%mm7 \n\t"
        "paddw %%mm3, %%mm5            \n\t"

        "movq %%mm4, "DCTSIZE_S"*7*2(%%"REG_D") \n\t"
        "paddw %%mm6, %%mm7            \n\t"

        "movq 2*8+%3, %%mm3            \n\t"
        "psubw %%mm0, %%mm6            \n\t"

        "movq "DCTSIZE_S"*2*2(%%"REG_D"), %%mm4 \n\t"
        "paddw %%mm0, %%mm7            \n\t"

        "movq %%mm5, "DCTSIZE_S"*0*2(%%"REG_D") \n\t"
        "paddw %%mm3, %%mm4            \n\t"

        "movq %%mm6, "DCTSIZE_S"*6*2(%%"REG_D") \n\t"
        "psubw %%mm1, %%mm3            \n\t"

        "movq "DCTSIZE_S"*5*2(%%"REG_D"), %%mm5 \n\t"
        "paddw %%mm1, %%mm4            \n\t"

        "movq "DCTSIZE_S"*3*2(%%"REG_D"), %%mm6 \n\t"
        "paddw %%mm3, %%mm5            \n\t"

        "movq 3*8+%3, %%mm0            \n\t"
        "add $24, %%"REG_S"              \n\t"

        "movq %%mm7, "DCTSIZE_S"*1*2(%%"REG_D") \n\t"
        "paddw %%mm0, %%mm6            \n\t"

        "movq %%mm4, "DCTSIZE_S"*2*2(%%"REG_D") \n\t"
        "psubw %%mm2, %%mm0            \n\t"

        "movq "DCTSIZE_S"*4*2(%%"REG_D"), %%mm7 \n\t"
        "paddw %%mm2, %%mm6            \n\t"

        "movq %%mm5, "DCTSIZE_S"*5*2(%%"REG_D") \n\t"
        "paddw %%mm0, %%mm7            \n\t"

        "movq %%mm6, "DCTSIZE_S"*3*2(%%"REG_D") \n\t"

        "movq %%mm7, "DCTSIZE_S"*4*2(%%"REG_D") \n\t"
        "add $24, %%"REG_D"              \n\t"
        "sub $2, %%"REG_c"               \n\t"
        "jnz 1b                \n\t"
        "jmp 5f                   \n\t"

        "3:                    \n\t"
        //--- non DC2
        //psraw mm1, 2 w/o it -> offset. thr1, thr1, thr1  (actually thr1, thr1, thr1-1)
        //psraw mm5, 2
        //psraw mm0, 2
        //psraw mm6, 2
        "movq %%mm5, %%mm3             \n\t"
        "psubw %%mm1, %%mm5            \n\t"

        "psllw $1, %%mm5              \n\t" //'z10
        "paddw %%mm1, %%mm3            \n\t" //'z13

        "movq %%mm0, %%mm2             \n\t"
        "psubw %%mm6, %%mm0            \n\t"

        "movq %%mm5, %%mm1             \n\t"
        "psllw $1, %%mm0              \n\t" //'z12

        "pmulhw "MANGLE(MM_FIX_2_613125930)", %%mm1 \n\t" //-
        "paddw %%mm0, %%mm5            \n\t"

        "pmulhw "MANGLE(MM_FIX_1_847759065)", %%mm5 \n\t" //'z5
        "paddw %%mm6, %%mm2            \n\t" //'z11

        "pmulhw "MANGLE(MM_FIX_1_082392200)", %%mm0 \n\t"
        "movq %%mm2, %%mm7             \n\t"

        //---
        "movq 0*8+%3, %%mm4            \n\t"
        "psubw %%mm3, %%mm2            \n\t"

        "psllw $1, %%mm2              \n\t"
        "paddw %%mm3, %%mm7            \n\t" //'t7

        "pmulhw "MANGLE(MM_FIX_1_414213562)", %%mm2 \n\t" //'t11
        "movq %%mm4, %%mm6             \n\t"
        //paddw mm7, MM_2
        "psraw $2, %%mm7              \n\t"

        "paddw "DCTSIZE_S"*0*2(%%"REG_D"), %%mm4 \n\t"
        "psubw %%mm7, %%mm6            \n\t"

        "movq 1*8+%3, %%mm3            \n\t"
        "paddw %%mm7, %%mm4            \n\t"

        "movq %%mm6, "DCTSIZE_S"*7*2(%%"REG_D") \n\t"
        "paddw %%mm5, %%mm1            \n\t" //'t12

        "movq %%mm4, "DCTSIZE_S"*0*2(%%"REG_D") \n\t"
        "psubw %%mm7, %%mm1            \n\t" //'t6

        "movq 2*8+%3, %%mm7            \n\t"
        "psubw %%mm5, %%mm0            \n\t" //'t10

        "movq 3*8+%3, %%mm6            \n\t"
        "movq %%mm3, %%mm5             \n\t"

        "paddw "DCTSIZE_S"*1*2(%%"REG_D"), %%mm3 \n\t"
        "psubw %%mm1, %%mm5            \n\t"

        "psubw %%mm1, %%mm2            \n\t" //'t5
        "paddw %%mm1, %%mm3            \n\t"

        "movq %%mm5, "DCTSIZE_S"*6*2(%%"REG_D") \n\t"
        "movq %%mm7, %%mm4             \n\t"

        "paddw "DCTSIZE_S"*2*2(%%"REG_D"), %%mm7 \n\t"
        "psubw %%mm2, %%mm4            \n\t"

        "paddw "DCTSIZE_S"*5*2(%%"REG_D"), %%mm4 \n\t"
        "paddw %%mm2, %%mm7            \n\t"

        "movq %%mm3, "DCTSIZE_S"*1*2(%%"REG_D") \n\t"
        "paddw %%mm2, %%mm0            \n\t" //'t4

        // 't4 't6 't5 - - - - 't7
        "movq %%mm7, "DCTSIZE_S"*2*2(%%"REG_D") \n\t"
        "movq %%mm6, %%mm1             \n\t"

        "paddw "DCTSIZE_S"*4*2(%%"REG_D"), %%mm6 \n\t"
        "psubw %%mm0, %%mm1            \n\t"

        "paddw "DCTSIZE_S"*3*2(%%"REG_D"), %%mm1 \n\t"
        "paddw %%mm0, %%mm6            \n\t"

        "movq %%mm4, "DCTSIZE_S"*5*2(%%"REG_D") \n\t"
        "add $24, %%"REG_S"              \n\t"

        "movq %%mm6, "DCTSIZE_S"*4*2(%%"REG_D") \n\t"

        "movq %%mm1, "DCTSIZE_S"*3*2(%%"REG_D") \n\t"
        "add $24, %%"REG_D"              \n\t"
        "sub $2, %%"REG_c"               \n\t"
        "jnz 1b                \n\t"
        "5:                      \n\t"

        : "+S"(data), "+D"(output), "+c"(cnt), "=o"(temps)
        : "d"(thr_adr)
        : "%"REG_a
        );
}

#endif // HAVE_MMX

#if !HAVE_MMX

static void row_idct_c(int16_t* workspace,
                       int16_t* output_adr, int output_stride, int cnt)
{
    int_simd16_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    int_simd16_t tmp10, tmp11, tmp12, tmp13;
    int_simd16_t z5, z10, z11, z12, z13;
    int16_t* outptr;
    int16_t* wsptr;

    cnt*=4;
    wsptr = workspace;
    outptr = output_adr;
    for (; cnt > 0; cnt--) {
        // Even part
        //Simd version reads 4x4 block and transposes it
        tmp10 = ( wsptr[2] +  wsptr[3]);
        tmp11 = ( wsptr[2] -  wsptr[3]);

        tmp13 = ( wsptr[0] +  wsptr[1]);
        tmp12 = (MULTIPLY16H( wsptr[0] - wsptr[1], FIX_1_414213562_A)<<2) - tmp13;//this shift order to avoid overflow

        tmp0 = tmp10 + tmp13; //->temps
        tmp3 = tmp10 - tmp13; //->temps
        tmp1 = tmp11 + tmp12;
        tmp2 = tmp11 - tmp12;

        // Odd part
        //Also transpose, with previous:
        // ---- ----      ||||
        // ---- ---- idct ||||
        // ---- ---- ---> ||||
        // ---- ----      ||||
        z13 = wsptr[4] + wsptr[5];
        z10 = wsptr[4] - wsptr[5];
        z11 = wsptr[6] + wsptr[7];
        z12 = wsptr[6] - wsptr[7];

        tmp7 = z11 + z13;
        tmp11 = MULTIPLY16H(z11 - z13, FIX_1_414213562);

        z5 =    MULTIPLY16H(z10 + z12, FIX_1_847759065);
        tmp10 = MULTIPLY16H(z12, FIX_1_082392200) - z5;
        tmp12 = MULTIPLY16H(z10, FIX_2_613125930) + z5; // - FIX_

        tmp6 = (tmp12<<3) - tmp7;
        tmp5 = (tmp11<<3) - tmp6;
        tmp4 = (tmp10<<3) + tmp5;

        // Final output stage: descale and write column
        outptr[0*output_stride]+= DESCALE(tmp0 + tmp7, 3);
        outptr[1*output_stride]+= DESCALE(tmp1 + tmp6, 3);
        outptr[2*output_stride]+= DESCALE(tmp2 + tmp5, 3);
        outptr[3*output_stride]+= DESCALE(tmp3 - tmp4, 3);
        outptr[4*output_stride]+= DESCALE(tmp3 + tmp4, 3);
        outptr[5*output_stride]+= DESCALE(tmp2 - tmp5, 3);
        outptr[6*output_stride]+= DESCALE(tmp1 - tmp6, 3); //no += ?
        outptr[7*output_stride]+= DESCALE(tmp0 - tmp7, 3); //no += ?
        outptr++;

        wsptr += DCTSIZE;       // advance pointer to next row
    }
}

#else /* HAVE_MMX */

static void row_idct_mmx (int16_t* workspace,
                          int16_t* output_adr,  int output_stride,  int cnt)
{
    uint64_t __attribute__((aligned(8))) temps[4];
    __asm__ volatile(
        "lea (%%"REG_a",%%"REG_a",2), %%"REG_d"    \n\t"
        "1:                     \n\t"
        "movq "DCTSIZE_S"*0*2(%%"REG_S"), %%mm0 \n\t"
        //

        "movq "DCTSIZE_S"*1*2(%%"REG_S"), %%mm1 \n\t"
        "movq %%mm0, %%mm4             \n\t"

        "movq "DCTSIZE_S"*2*2(%%"REG_S"), %%mm2 \n\t"
        "punpcklwd %%mm1, %%mm0        \n\t"

        "movq "DCTSIZE_S"*3*2(%%"REG_S"), %%mm3 \n\t"
        "punpckhwd %%mm1, %%mm4        \n\t"

        //transpose 4x4
        "movq %%mm2, %%mm7             \n\t"
        "punpcklwd %%mm3, %%mm2        \n\t"

        "movq %%mm0, %%mm6             \n\t"
        "punpckldq %%mm2, %%mm0        \n\t" //0

        "punpckhdq %%mm2, %%mm6        \n\t" //1
        "movq %%mm0, %%mm5             \n\t"

        "punpckhwd %%mm3, %%mm7        \n\t"
        "psubw %%mm6, %%mm0            \n\t"

        "pmulhw "MANGLE(MM_FIX_1_414213562_A)", %%mm0 \n\t"
        "movq %%mm4, %%mm2             \n\t"

        "punpckldq %%mm7, %%mm4        \n\t" //2
        "paddw %%mm6, %%mm5            \n\t"

        "punpckhdq %%mm7, %%mm2        \n\t" //3
        "movq %%mm4, %%mm1             \n\t"

        "psllw $2, %%mm0              \n\t"
        "paddw %%mm2, %%mm4            \n\t" //t10

        "movq "DCTSIZE_S"*0*2+"DCTSIZE_S"(%%"REG_S"), %%mm3 \n\t"
        "psubw %%mm2, %%mm1            \n\t" //t11

        "movq "DCTSIZE_S"*1*2+"DCTSIZE_S"(%%"REG_S"), %%mm2 \n\t"
        "psubw %%mm5, %%mm0            \n\t"

        "movq %%mm4, %%mm6             \n\t"
        "paddw %%mm5, %%mm4            \n\t" //t0

        "psubw %%mm5, %%mm6            \n\t" //t3
        "movq %%mm1, %%mm7             \n\t"

        "movq "DCTSIZE_S"*2*2+"DCTSIZE_S"(%%"REG_S"), %%mm5 \n\t"
        "paddw %%mm0, %%mm1            \n\t" //t1

        "movq %%mm4, 0*8+%3            \n\t" //t0
        "movq %%mm3, %%mm4             \n\t"

        "movq %%mm6, 1*8+%3            \n\t" //t3
        "punpcklwd %%mm2, %%mm3        \n\t"

        //transpose 4x4
        "movq "DCTSIZE_S"*3*2+"DCTSIZE_S"(%%"REG_S"), %%mm6 \n\t"
        "punpckhwd %%mm2, %%mm4        \n\t"

        "movq %%mm5, %%mm2             \n\t"
        "punpcklwd %%mm6, %%mm5        \n\t"

        "psubw %%mm0, %%mm7            \n\t" //t2
        "punpckhwd %%mm6, %%mm2        \n\t"

        "movq %%mm3, %%mm0             \n\t"
        "punpckldq %%mm5, %%mm3        \n\t" //4

        "punpckhdq %%mm5, %%mm0        \n\t" //5
        "movq %%mm4, %%mm5             \n\t"

        //
        "movq %%mm3, %%mm6             \n\t"
        "punpckldq %%mm2, %%mm4        \n\t" //6

        "psubw %%mm0, %%mm3            \n\t" //z10
        "punpckhdq %%mm2, %%mm5        \n\t" //7

        "paddw %%mm0, %%mm6            \n\t" //z13
        "movq %%mm4, %%mm2             \n\t"

        "movq %%mm3, %%mm0             \n\t"
        "psubw %%mm5, %%mm4            \n\t" //z12

        "pmulhw "MANGLE(MM_FIX_2_613125930)", %%mm0 \n\t" //-
        "paddw %%mm4, %%mm3            \n\t"

        "pmulhw "MANGLE(MM_FIX_1_847759065)", %%mm3 \n\t" //z5
        "paddw %%mm5, %%mm2            \n\t" //z11  >

        "pmulhw "MANGLE(MM_FIX_1_082392200)", %%mm4 \n\t"
        "movq %%mm2, %%mm5             \n\t"

        "psubw %%mm6, %%mm2            \n\t"
        "paddw %%mm6, %%mm5            \n\t" //t7

        "pmulhw "MANGLE(MM_FIX_1_414213562)", %%mm2 \n\t" //t11
        "paddw %%mm3, %%mm0            \n\t" //t12

        "psllw $3, %%mm0              \n\t"
        "psubw %%mm3, %%mm4            \n\t" //t10

        "movq 0*8+%3, %%mm6            \n\t"
        "movq %%mm1, %%mm3             \n\t"

        "psllw $3, %%mm4              \n\t"
        "psubw %%mm5, %%mm0            \n\t" //t6

        "psllw $3, %%mm2              \n\t"
        "paddw %%mm0, %%mm1            \n\t" //d1

        "psubw %%mm0, %%mm2            \n\t" //t5
        "psubw %%mm0, %%mm3            \n\t" //d6

        "paddw %%mm2, %%mm4            \n\t" //t4
        "movq %%mm7, %%mm0             \n\t"

        "paddw %%mm2, %%mm7            \n\t" //d2
        "psubw %%mm2, %%mm0            \n\t" //d5

        "movq "MANGLE(MM_DESCALE_RND)", %%mm2   \n\t" //4
        "psubw %%mm5, %%mm6            \n\t" //d7

        "paddw 0*8+%3, %%mm5           \n\t" //d0
        "paddw %%mm2, %%mm1            \n\t"

        "paddw %%mm2, %%mm5            \n\t"
        "psraw $3, %%mm1              \n\t"

        "paddw %%mm2, %%mm7            \n\t"
        "psraw $3, %%mm5              \n\t"

        "paddw (%%"REG_D"), %%mm5          \n\t"
        "psraw $3, %%mm7              \n\t"

        "paddw (%%"REG_D",%%"REG_a",), %%mm1    \n\t"
        "paddw %%mm2, %%mm0            \n\t"

        "paddw (%%"REG_D",%%"REG_a",2), %%mm7   \n\t"
        "paddw %%mm2, %%mm3            \n\t"

        "movq %%mm5, (%%"REG_D")           \n\t"
        "paddw %%mm2, %%mm6            \n\t"

        "movq %%mm1, (%%"REG_D",%%"REG_a",)     \n\t"
        "psraw $3, %%mm0              \n\t"

        "movq %%mm7, (%%"REG_D",%%"REG_a",2)    \n\t"
        "add %%"REG_d", %%"REG_D"             \n\t" //3*ls

        "movq 1*8+%3, %%mm5           \n\t" //t3
        "psraw $3, %%mm3              \n\t"

        "paddw (%%"REG_D",%%"REG_a",2), %%mm0   \n\t"
        "psubw %%mm4, %%mm5            \n\t" //d3

        "paddw (%%"REG_D",%%"REG_d",), %%mm3    \n\t"
        "psraw $3, %%mm6              \n\t"

        "paddw 1*8+%3, %%mm4           \n\t" //d4
        "paddw %%mm2, %%mm5            \n\t"

        "paddw (%%"REG_D",%%"REG_a",4), %%mm6   \n\t"
        "paddw %%mm2, %%mm4            \n\t"

        "movq %%mm0, (%%"REG_D",%%"REG_a",2)    \n\t"
        "psraw $3, %%mm5              \n\t"

        "paddw (%%"REG_D"), %%mm5          \n\t"
        "psraw $3, %%mm4              \n\t"

        "paddw (%%"REG_D",%%"REG_a",), %%mm4    \n\t"
        "add $"DCTSIZE_S"*2*4, %%"REG_S"      \n\t" //4 rows

        "movq %%mm3, (%%"REG_D",%%"REG_d",)     \n\t"
        "movq %%mm6, (%%"REG_D",%%"REG_a",4)    \n\t"
        "movq %%mm5, (%%"REG_D")           \n\t"
        "movq %%mm4, (%%"REG_D",%%"REG_a",)     \n\t"

        "sub %%"REG_d", %%"REG_D"             \n\t"
        "add $8, %%"REG_D"               \n\t"
        "dec %%"REG_c"                   \n\t"
        "jnz 1b                  \n\t"

        : "+S"(workspace), "+D"(output_adr), "+c"(cnt), "=o"(temps)
        : "a"(output_stride*sizeof(short))
        : "%"REG_d
        );
}

#endif // HAVE_MMX

#if !HAVE_MMX

static void row_fdct_c(int16_t *data, const uint8_t *pixels, int line_size, int cnt)
{
    int_simd16_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    int_simd16_t tmp10, tmp11, tmp12, tmp13;
    int_simd16_t z1, z2, z3, z4, z5, z11, z13;
    int16_t *dataptr;

    cnt*=4;
    // Pass 1: process rows.

    dataptr = data;
    for (; cnt > 0; cnt--) {
        tmp0 = pixels[line_size*0] + pixels[line_size*7];
        tmp7 = pixels[line_size*0] - pixels[line_size*7];
        tmp1 = pixels[line_size*1] + pixels[line_size*6];
        tmp6 = pixels[line_size*1] - pixels[line_size*6];
        tmp2 = pixels[line_size*2] + pixels[line_size*5];
        tmp5 = pixels[line_size*2] - pixels[line_size*5];
        tmp3 = pixels[line_size*3] + pixels[line_size*4];
        tmp4 = pixels[line_size*3] - pixels[line_size*4];

        // Even part

        tmp10 = tmp0 + tmp3;
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;
        //Even columns are written first, this leads to different order of columns
        //in column_fidct(), but they are processed independently, so all ok.
        //Later in the row_idct() columns readed at the same order.
        dataptr[2] = tmp10 + tmp11;
        dataptr[3] = tmp10 - tmp11;

        z1 = MULTIPLY16H((tmp12 + tmp13)<<2, FIX_0_707106781);
        dataptr[0] = tmp13 + z1;
        dataptr[1] = tmp13 - z1;

        // Odd part

        tmp10 = (tmp4 + tmp5) <<2;
        tmp11 = (tmp5 + tmp6) <<2;
        tmp12 = (tmp6 + tmp7) <<2;

        z5 = MULTIPLY16H(tmp10 - tmp12, FIX_0_382683433);
        z2 = MULTIPLY16H(tmp10, FIX_0_541196100) + z5;
        z4 = MULTIPLY16H(tmp12, FIX_1_306562965) + z5;
        z3 = MULTIPLY16H(tmp11, FIX_0_707106781);

        z11 = tmp7 + z3;
        z13 = tmp7 - z3;

        dataptr[4] = z13 + z2;
        dataptr[5] = z13 - z2;
        dataptr[6] = z11 + z4;
        dataptr[7] = z11 - z4;

        pixels++;               // advance pointer to next column
        dataptr += DCTSIZE;
    }
}

#else /* HAVE_MMX */

static void row_fdct_mmx(int16_t *data,  const uint8_t *pixels,  int line_size,  int cnt)
{
    uint64_t __attribute__((aligned(8))) temps[4];
    __asm__ volatile(
        "lea (%%"REG_a",%%"REG_a",2), %%"REG_d"    \n\t"
        "6:                     \n\t"
        "movd (%%"REG_S"), %%mm0           \n\t"
        "pxor %%mm7, %%mm7             \n\t"

        "movd (%%"REG_S",%%"REG_a",), %%mm1     \n\t"
        "punpcklbw %%mm7, %%mm0        \n\t"

        "movd (%%"REG_S",%%"REG_a",2), %%mm2    \n\t"
        "punpcklbw %%mm7, %%mm1        \n\t"

        "punpcklbw %%mm7, %%mm2        \n\t"
        "add %%"REG_d", %%"REG_S"             \n\t"

        "movq %%mm0, %%mm5             \n\t"
        //

        "movd (%%"REG_S",%%"REG_a",4), %%mm3    \n\t" //7  ;prefetch!
        "movq %%mm1, %%mm6             \n\t"

        "movd (%%"REG_S",%%"REG_d",), %%mm4     \n\t" //6
        "punpcklbw %%mm7, %%mm3        \n\t"

        "psubw %%mm3, %%mm5            \n\t"
        "punpcklbw %%mm7, %%mm4        \n\t"

        "paddw %%mm3, %%mm0            \n\t"
        "psubw %%mm4, %%mm6            \n\t"

        "movd (%%"REG_S",%%"REG_a",2), %%mm3    \n\t" //5
        "paddw %%mm4, %%mm1            \n\t"

        "movq %%mm5, 0*8+%3            \n\t" //t7
        "punpcklbw %%mm7, %%mm3        \n\t"

        "movq %%mm6, 1*8+%3            \n\t" //t6
        "movq %%mm2, %%mm4             \n\t"

        "movd (%%"REG_S"), %%mm5           \n\t" //3
        "paddw %%mm3, %%mm2            \n\t"

        "movd (%%"REG_S",%%"REG_a",), %%mm6     \n\t" //4
        "punpcklbw %%mm7, %%mm5        \n\t"

        "psubw %%mm3, %%mm4            \n\t"
        "punpcklbw %%mm7, %%mm6        \n\t"

        "movq %%mm5, %%mm3             \n\t"
        "paddw %%mm6, %%mm5            \n\t" //t3

        "psubw %%mm6, %%mm3            \n\t" //t4  ; t0 t1 t2 t4 t5 t3 - -
        "movq %%mm0, %%mm6             \n\t"

        "movq %%mm1, %%mm7             \n\t"
        "psubw %%mm5, %%mm0            \n\t" //t13

        "psubw %%mm2, %%mm1            \n\t"
        "paddw %%mm2, %%mm7            \n\t" //t11

        "paddw %%mm0, %%mm1            \n\t"
        "movq %%mm7, %%mm2             \n\t"

        "psllw $2, %%mm1              \n\t"
        "paddw %%mm5, %%mm6            \n\t" //t10

        "pmulhw "MANGLE(ff_MM_FIX_0_707106781)", %%mm1 \n\t"
        "paddw %%mm6, %%mm7            \n\t" //d2

        "psubw %%mm2, %%mm6            \n\t" //d3
        "movq %%mm0, %%mm5             \n\t"

        //transpose 4x4
        "movq %%mm7, %%mm2             \n\t"
        "punpcklwd %%mm6, %%mm7        \n\t"

        "paddw %%mm1, %%mm0            \n\t" //d0
        "punpckhwd %%mm6, %%mm2        \n\t"

        "psubw %%mm1, %%mm5            \n\t" //d1
        "movq %%mm0, %%mm6             \n\t"

        "movq 1*8+%3, %%mm1            \n\t"
        "punpcklwd %%mm5, %%mm0        \n\t"

        "punpckhwd %%mm5, %%mm6        \n\t"
        "movq %%mm0, %%mm5             \n\t"

        "punpckldq %%mm7, %%mm0        \n\t" //0
        "paddw %%mm4, %%mm3            \n\t"

        "punpckhdq %%mm7, %%mm5        \n\t" //1
        "movq %%mm6, %%mm7             \n\t"

        "movq %%mm0, "DCTSIZE_S"*0*2(%%"REG_D") \n\t"
        "punpckldq %%mm2, %%mm6        \n\t" //2

        "movq %%mm5, "DCTSIZE_S"*1*2(%%"REG_D") \n\t"
        "punpckhdq %%mm2, %%mm7        \n\t" //3

        "movq %%mm6, "DCTSIZE_S"*2*2(%%"REG_D") \n\t"
        "paddw %%mm1, %%mm4            \n\t"

        "movq %%mm7, "DCTSIZE_S"*3*2(%%"REG_D") \n\t"
        "psllw $2, %%mm3              \n\t" //t10

        "movq 0*8+%3, %%mm2           \n\t"
        "psllw $2, %%mm4              \n\t" //t11

        "pmulhw "MANGLE(ff_MM_FIX_0_707106781)", %%mm4 \n\t" //z3
        "paddw %%mm2, %%mm1            \n\t"

        "psllw $2, %%mm1              \n\t" //t12
        "movq %%mm3, %%mm0             \n\t"

        "pmulhw "MANGLE(ff_MM_FIX_0_541196100)", %%mm0 \n\t"
        "psubw %%mm1, %%mm3            \n\t"

        "pmulhw "MANGLE(MM_FIX_0_382683433)", %%mm3 \n\t" //z5
        "movq %%mm2, %%mm5             \n\t"

        "pmulhw "MANGLE(MM_FIX_1_306562965)", %%mm1 \n\t"
        "psubw %%mm4, %%mm2            \n\t" //z13

        "paddw %%mm4, %%mm5            \n\t" //z11
        "movq %%mm2, %%mm6             \n\t"

        "paddw %%mm3, %%mm0            \n\t" //z2
        "movq %%mm5, %%mm7             \n\t"

        "paddw %%mm0, %%mm2            \n\t" //d4
        "psubw %%mm0, %%mm6            \n\t" //d5

        "movq %%mm2, %%mm4             \n\t"
        "paddw %%mm3, %%mm1            \n\t" //z4

        //transpose 4x4
        "punpcklwd %%mm6, %%mm2        \n\t"
        "paddw %%mm1, %%mm5            \n\t" //d6

        "punpckhwd %%mm6, %%mm4        \n\t"
        "psubw %%mm1, %%mm7            \n\t" //d7

        "movq %%mm5, %%mm6             \n\t"
        "punpcklwd %%mm7, %%mm5        \n\t"

        "punpckhwd %%mm7, %%mm6        \n\t"
        "movq %%mm2, %%mm7             \n\t"

        "punpckldq %%mm5, %%mm2        \n\t" //4
        "sub %%"REG_d", %%"REG_S"             \n\t"

        "punpckhdq %%mm5, %%mm7        \n\t" //5
        "movq %%mm4, %%mm5             \n\t"

        "movq %%mm2, "DCTSIZE_S"*0*2+"DCTSIZE_S"(%%"REG_D") \n\t"
        "punpckldq %%mm6, %%mm4        \n\t" //6

        "movq %%mm7, "DCTSIZE_S"*1*2+"DCTSIZE_S"(%%"REG_D") \n\t"
        "punpckhdq %%mm6, %%mm5        \n\t" //7

        "movq %%mm4, "DCTSIZE_S"*2*2+"DCTSIZE_S"(%%"REG_D") \n\t"
        "add $4, %%"REG_S"               \n\t"

        "movq %%mm5, "DCTSIZE_S"*3*2+"DCTSIZE_S"(%%"REG_D") \n\t"
        "add $"DCTSIZE_S"*2*4, %%"REG_D"      \n\t" //4 rows
        "dec %%"REG_c"                   \n\t"
        "jnz 6b                  \n\t"

        : "+S"(pixels), "+D"(data), "+c"(cnt), "=o"(temps)
        : "a"(line_size)
        : "%"REG_d);
}

#endif // HAVE_MMX
