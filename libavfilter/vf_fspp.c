/*
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2005 Nikolaj Poroshin <porosh3@psu.ru>
 * Copyright (c) 2014 Arwa Arif <arwaarif1994@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * Fast Simple Post-processing filter
 * This implementation is based on an algorithm described in
 * "Aria Nosratinia Embedded Post-Processing for
 * Enhancement of Compressed Images (1999)"
 * (http://www.utdallas.edu/~aria/papers/vlsisp99.pdf)
 * Further, with splitting (I)DCT into horizontal/vertical passes, one of
 * them can be performed once per block, not per pixel. This allows for much
 * higher speed.
 *
 * Originally written by Michael Niedermayer and Nikolaj for the MPlayer
 * project, and ported by Arwa Arif for FFmpeg.
 */

#include "libavutil/imgutils.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "internal.h"
#include "qp_table.h"
#include "vf_fspp.h"

#define OFFSET(x) offsetof(FSPPContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption fspp_options[] = {
    { "quality",       "set quality",                          OFFSET(log2_count),    AV_OPT_TYPE_INT, {.i64 = 4},   4, MAX_LEVEL, FLAGS },
    { "qp",            "force a constant quantizer parameter", OFFSET(qp),            AV_OPT_TYPE_INT, {.i64 = 0},   0, 64,        FLAGS },
    { "strength",      "set filter strength",                  OFFSET(strength),      AV_OPT_TYPE_INT, {.i64 = 0}, -15, 32,        FLAGS },
    { "use_bframe_qp", "use B-frames' QP",                     OFFSET(use_bframe_qp), AV_OPT_TYPE_BOOL,{.i64 = 0},   0, 1,         FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(fspp);

DECLARE_ALIGNED(32, static const uint8_t, dither)[8][8] = {
    {  0,  48,  12,  60,   3,  51,  15,  63, },
    { 32,  16,  44,  28,  35,  19,  47,  31, },
    {  8,  56,   4,  52,  11,  59,   7,  55, },
    { 40,  24,  36,  20,  43,  27,  39,  23, },
    {  2,  50,  14,  62,   1,  49,  13,  61, },
    { 34,  18,  46,  30,  33,  17,  45,  29, },
    { 10,  58,   6,  54,   9,  57,   5,  53, },
    { 42,  26,  38,  22,  41,  25,  37,  21, },
};

static const short custom_threshold[64] = {
// values (296) can't be too high
// -it causes too big quant dependence
// or maybe overflow(check), which results in some flashing
     71, 296, 295, 237,  71,  40,  38,  19,
    245, 193, 185, 121, 102,  73,  53,  27,
    158, 129, 141, 107,  97,  73,  50,  26,
    102, 116, 109,  98,  82,  66,  45,  23,
     71,  94,  95,  81,  70,  56,  38,  20,
     56,  77,  74,  66,  56,  44,  30,  15,
     38,  53,  50,  45,  38,  30,  21,  11,
     20,  27,  26,  23,  20,  15,  11,   5
};

//This func reads from 1 slice, 1 and clears 0 & 1
static void store_slice_c(uint8_t *dst, int16_t *src,
                          ptrdiff_t dst_stride, ptrdiff_t src_stride,
                          ptrdiff_t width, ptrdiff_t height, ptrdiff_t log2_scale)
{
    int y, x;
#define STORE(pos)                                                             \
    temp = (src[x + pos] + (d[pos] >> log2_scale)) >> (6 - log2_scale);        \
    src[x + pos] = src[x + pos - 8 * src_stride] = 0;                          \
    if (temp & 0x100) temp = ~(temp >> 31);                                    \
    dst[x + pos] = temp;

    for (y = 0; y < height; y++) {
        const uint8_t *d = dither[y];
        for (x = 0; x < width; x += 8) {
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
        src += src_stride;
        dst += dst_stride;
    }
}

//This func reads from 2 slices, 0 & 2  and clears 2-nd
static void store_slice2_c(uint8_t *dst, int16_t *src,
                           ptrdiff_t dst_stride, ptrdiff_t src_stride,
                           ptrdiff_t width, ptrdiff_t height, ptrdiff_t log2_scale)
{
    int y, x;
#define STORE2(pos)                                                                                       \
    temp = (src[x + pos] + src[x + pos + 16 * src_stride] + (d[pos] >> log2_scale)) >> (6 - log2_scale);  \
    src[x + pos + 16 * src_stride] = 0;                                                                   \
    if (temp & 0x100) temp = ~(temp >> 31);                                                               \
    dst[x + pos] = temp;

    for (y = 0; y < height; y++) {
        const uint8_t *d = dither[y];
        for (x = 0; x < width; x += 8) {
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
        src += src_stride;
        dst += dst_stride;
    }
}

static void mul_thrmat_c(int16_t *thr_adr_noq, int16_t *thr_adr, int q)
{
    int a;
    for (a = 0; a < 64; a++)
        thr_adr[a] = q * thr_adr_noq[a];
}

static void filter(FSPPContext *p, uint8_t *dst, uint8_t *src,
                   int dst_stride, int src_stride,
                   int width, int height,
                   uint8_t *qp_store, int qp_stride, int is_luma)
{
    int x, x0, y, es, qy, t;

    const int stride = is_luma ? p->temp_stride : (width + 16);
    const int step = 6 - p->log2_count;
    const int qpsh = 4 - p->hsub * !is_luma;
    const int qpsv = 4 - p->vsub * !is_luma;

    DECLARE_ALIGNED(32, int32_t, block_align)[4 * 8 * BLOCKSZ + 4 * 8 * BLOCKSZ];
    int16_t *block  = (int16_t *)block_align;
    int16_t *block3 = (int16_t *)(block_align + 4 * 8 * BLOCKSZ);

    memset(block3, 0, 4 * 8 * BLOCKSZ);

    if (!src || !dst) return;

    for (y = 0; y < height; y++) {
        int index = 8 + 8 * stride + y * stride;
        memcpy(p->src + index, src + y * src_stride, width);
        for (x = 0; x < 8; x++) {
            p->src[index         - x - 1] = p->src[index +         x    ];
            p->src[index + width + x    ] = p->src[index + width - x - 1];
        }
    }

    for (y = 0; y < 8; y++) {
        memcpy(p->src + (     7 - y    ) * stride, p->src + (     y + 8    ) * stride, stride);
        memcpy(p->src + (height + 8 + y) * stride, p->src + (height - y + 7) * stride, stride);
    }
    //FIXME (try edge emu)

    for (y = 8; y < 24; y++)
        memset(p->temp + 8 + y * stride, 0, width * sizeof(int16_t));

    for (y = step; y < height + 8; y += step) {    //step= 1,2
        const int y1 = y - 8 + step;                 //l5-7  l4-6;
        qy = y - 4;

        if (qy > height - 1) qy = height - 1;
        if (qy < 0) qy = 0;

        qy = (qy >> qpsv) * qp_stride;
        p->row_fdct(block, p->src + y * stride + 2 - (y&1), stride, 2);

        for (x0 = 0; x0 < width + 8 - 8 * (BLOCKSZ - 1); x0 += 8 * (BLOCKSZ - 1)) {
            p->row_fdct(block + 8 * 8, p->src + y * stride + 8 + x0 + 2 - (y&1), stride, 2 * (BLOCKSZ - 1));

            if (p->qp)
                p->column_fidct((int16_t *)(&p->threshold_mtx[0]), block + 0 * 8, block3 + 0 * 8, 8 * (BLOCKSZ - 1)); //yes, this is a HOTSPOT
            else
                for (x = 0; x < 8 * (BLOCKSZ - 1); x += 8) {
                    t = x + x0 - 2;                    //correct t=x+x0-2-(y&1), but its the same

                    if (t < 0) t = 0;                   //t always < width-2

                    t = qp_store[qy + (t >> qpsh)];
                    t = ff_norm_qscale(t, p->qscale_type);

                    if (t != p->prev_q) p->prev_q = t, p->mul_thrmat((int16_t *)(&p->threshold_mtx_noq[0]), (int16_t *)(&p->threshold_mtx[0]), t);
                    p->column_fidct((int16_t *)(&p->threshold_mtx[0]), block + x * 8, block3 + x * 8, 8); //yes, this is a HOTSPOT
                }
            p->row_idct(block3 + 0 * 8, p->temp + (y & 15) * stride + x0 + 2 - (y & 1), stride, 2 * (BLOCKSZ - 1));
            memmove(block,  block  + (BLOCKSZ - 1) * 64, 8 * 8 * sizeof(int16_t)); //cycling
            memmove(block3, block3 + (BLOCKSZ - 1) * 64, 6 * 8 * sizeof(int16_t));
        }

        es = width + 8 - x0; //  8, ...
        if (es > 8)
            p->row_fdct(block + 8 * 8, p->src + y * stride + 8 + x0 + 2 - (y & 1), stride, (es - 4) >> 2);

        p->column_fidct((int16_t *)(&p->threshold_mtx[0]), block, block3, es&(~1));
        if (es > 3)
            p->row_idct(block3 + 0 * 8, p->temp + (y & 15) * stride + x0 + 2 - (y & 1), stride, es >> 2);

        if (!(y1 & 7) && y1) {
            if (y1 & 8)
                p->store_slice(dst + (y1 - 8) * dst_stride, p->temp + 8 + 8 * stride,
                               dst_stride, stride, width, 8, 5 - p->log2_count);
            else
                p->store_slice2(dst + (y1 - 8) * dst_stride, p->temp + 8 + 0 * stride,
                                dst_stride, stride, width, 8, 5 - p->log2_count);
        }
    }

    if (y & 7) {  // height % 8 != 0
        if (y & 8)
            p->store_slice(dst + ((y - 8) & ~7) * dst_stride, p->temp + 8 + 8 * stride,
                           dst_stride, stride, width, y&7, 5 - p->log2_count);
        else
            p->store_slice2(dst + ((y - 8) & ~7) * dst_stride, p->temp + 8 + 0 * stride,
                            dst_stride, stride, width, y&7, 5 - p->log2_count);
    }
}

static void column_fidct_c(int16_t *thr_adr, int16_t *data, int16_t *output, int cnt)
{
    int_simd16_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    int_simd16_t tmp10, tmp11, tmp12, tmp13;
    int_simd16_t z1,z2,z3,z4,z5, z10, z11, z12, z13;
    int_simd16_t d0, d1, d2, d3, d4, d5, d6, d7;

    int16_t *dataptr;
    int16_t *wsptr;
    int16_t *threshold;
    int ctr;

    dataptr = data;
    wsptr = output;

    for (; cnt > 0; cnt -= 2) { //start positions
        threshold = (int16_t *)thr_adr;//threshold_mtx
        for (ctr = DCTSIZE; ctr > 0; ctr--) {
            // Process columns from input, add to output.
            tmp0 = dataptr[DCTSIZE * 0] + dataptr[DCTSIZE * 7];
            tmp7 = dataptr[DCTSIZE * 0] - dataptr[DCTSIZE * 7];

            tmp1 = dataptr[DCTSIZE * 1] + dataptr[DCTSIZE * 6];
            tmp6 = dataptr[DCTSIZE * 1] - dataptr[DCTSIZE * 6];

            tmp2 = dataptr[DCTSIZE * 2] + dataptr[DCTSIZE * 5];
            tmp5 = dataptr[DCTSIZE * 2] - dataptr[DCTSIZE * 5];

            tmp3 = dataptr[DCTSIZE * 3] + dataptr[DCTSIZE * 4];
            tmp4 = dataptr[DCTSIZE * 3] - dataptr[DCTSIZE * 4];

            // Even part of FDCT

            tmp10 = tmp0 + tmp3;
            tmp13 = tmp0 - tmp3;
            tmp11 = tmp1 + tmp2;
            tmp12 = tmp1 - tmp2;

            d0 = tmp10 + tmp11;
            d4 = tmp10 - tmp11;

            z1 = MULTIPLY16H((tmp12 + tmp13) << 2, FIX_0_707106781);
            d2 = tmp13 + z1;
            d6 = tmp13 - z1;

            // Even part of IDCT

            THRESHOLD(tmp0, d0, threshold[0 * 8]);
            THRESHOLD(tmp1, d2, threshold[2 * 8]);
            THRESHOLD(tmp2, d4, threshold[4 * 8]);
            THRESHOLD(tmp3, d6, threshold[6 * 8]);
            tmp0 += 2;
            tmp10 = (tmp0 + tmp2) >> 2;
            tmp11 = (tmp0 - tmp2) >> 2;

            tmp13 = (tmp1 + tmp3) >>2; //+2 !  (psnr decides)
            tmp12 = MULTIPLY16H((tmp1 - tmp3), FIX_1_414213562_A) - tmp13; //<<2

            tmp0 = tmp10 + tmp13; //->temps
            tmp3 = tmp10 - tmp13; //->temps
            tmp1 = tmp11 + tmp12; //->temps
            tmp2 = tmp11 - tmp12; //->temps

            // Odd part of FDCT

            tmp10 = tmp4 + tmp5;
            tmp11 = tmp5 + tmp6;
            tmp12 = tmp6 + tmp7;

            z5 = MULTIPLY16H((tmp10 - tmp12) << 2, FIX_0_382683433);
            z2 = MULTIPLY16H(tmp10 << 2, FIX_0_541196100) + z5;
            z4 = MULTIPLY16H(tmp12 << 2, FIX_1_306562965) + z5;
            z3 = MULTIPLY16H(tmp11 << 2, FIX_0_707106781);

            z11 = tmp7 + z3;
            z13 = tmp7 - z3;

            d5 = z13 + z2;
            d3 = z13 - z2;
            d1 = z11 + z4;
            d7 = z11 - z4;

            // Odd part of IDCT

            THRESHOLD(tmp4, d1, threshold[1 * 8]);
            THRESHOLD(tmp5, d3, threshold[3 * 8]);
            THRESHOLD(tmp6, d5, threshold[5 * 8]);
            THRESHOLD(tmp7, d7, threshold[7 * 8]);

            //Simd version uses here a shortcut for the tmp5,tmp6,tmp7 == 0
            z13 = tmp6 + tmp5;
            z10 = (tmp6 - tmp5) << 1;
            z11 = tmp4 + tmp7;
            z12 = (tmp4 - tmp7) << 1;

            tmp7  = (z11 + z13) >> 2; //+2 !
            tmp11 = MULTIPLY16H((z11 - z13) << 1, FIX_1_414213562);
            z5    = MULTIPLY16H(z10 + z12,        FIX_1_847759065);
            tmp10 = MULTIPLY16H(z12,              FIX_1_082392200) - z5;
            tmp12 = MULTIPLY16H(z10,              FIX_2_613125930) + z5; // - !!

            tmp6 = tmp12 - tmp7;
            tmp5 = tmp11 - tmp6;
            tmp4 = tmp10 + tmp5;

            wsptr[DCTSIZE * 0] +=  (tmp0 + tmp7);
            wsptr[DCTSIZE * 1] +=  (tmp1 + tmp6);
            wsptr[DCTSIZE * 2] +=  (tmp2 + tmp5);
            wsptr[DCTSIZE * 3] +=  (tmp3 - tmp4);
            wsptr[DCTSIZE * 4] +=  (tmp3 + tmp4);
            wsptr[DCTSIZE * 5] +=  (tmp2 - tmp5);
            wsptr[DCTSIZE * 6]  =  (tmp1 - tmp6);
            wsptr[DCTSIZE * 7]  =  (tmp0 - tmp7);
            //
            dataptr++; //next column
            wsptr++;
            threshold++;
        }
        dataptr += 8; //skip each second start pos
        wsptr   += 8;
    }
}

static void row_idct_c(int16_t *workspace, int16_t *output_adr, ptrdiff_t output_stride, int cnt)
{
    int_simd16_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    int_simd16_t tmp10, tmp11, tmp12, tmp13;
    int_simd16_t z5, z10, z11, z12, z13;
    int16_t *outptr;
    int16_t *wsptr;

    cnt *= 4;
    wsptr = workspace;
    outptr = output_adr;
    for (; cnt > 0; cnt--) {
        // Even part
        //Simd version reads 4x4 block and transposes it
        tmp10 = wsptr[2] +  wsptr[3];
        tmp11 = wsptr[2] -  wsptr[3];

        tmp13 = wsptr[0] +  wsptr[1];
        tmp12 = (MULTIPLY16H(wsptr[0] - wsptr[1], FIX_1_414213562_A) << 2) - tmp13;//this shift order to avoid overflow

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
        tmp10 = MULTIPLY16H(z12,       FIX_1_082392200) - z5;
        tmp12 = MULTIPLY16H(z10,       FIX_2_613125930) + z5; // - FIX_

        tmp6 = (tmp12 << 3) - tmp7;
        tmp5 = (tmp11 << 3) - tmp6;
        tmp4 = (tmp10 << 3) + tmp5;

        // Final output stage: descale and write column
        outptr[0 * output_stride] += DESCALE(tmp0 + tmp7, 3);
        outptr[1 * output_stride] += DESCALE(tmp1 + tmp6, 3);
        outptr[2 * output_stride] += DESCALE(tmp2 + tmp5, 3);
        outptr[3 * output_stride] += DESCALE(tmp3 - tmp4, 3);
        outptr[4 * output_stride] += DESCALE(tmp3 + tmp4, 3);
        outptr[5 * output_stride] += DESCALE(tmp2 - tmp5, 3);
        outptr[6 * output_stride] += DESCALE(tmp1 - tmp6, 3); //no += ?
        outptr[7 * output_stride] += DESCALE(tmp0 - tmp7, 3); //no += ?
        outptr++;

        wsptr += DCTSIZE;       // advance pointer to next row
    }
}

static void row_fdct_c(int16_t *data, const uint8_t *pixels, ptrdiff_t line_size, int cnt)
{
    int_simd16_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    int_simd16_t tmp10, tmp11, tmp12, tmp13;
    int_simd16_t z1, z2, z3, z4, z5, z11, z13;
    int16_t *dataptr;

    cnt *= 4;
    // Pass 1: process rows.

    dataptr = data;
    for (; cnt > 0; cnt--) {
        tmp0 = pixels[line_size * 0] + pixels[line_size * 7];
        tmp7 = pixels[line_size * 0] - pixels[line_size * 7];
        tmp1 = pixels[line_size * 1] + pixels[line_size * 6];
        tmp6 = pixels[line_size * 1] - pixels[line_size * 6];
        tmp2 = pixels[line_size * 2] + pixels[line_size * 5];
        tmp5 = pixels[line_size * 2] - pixels[line_size * 5];
        tmp3 = pixels[line_size * 3] + pixels[line_size * 4];
        tmp4 = pixels[line_size * 3] - pixels[line_size * 4];

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

        z1 = MULTIPLY16H((tmp12 + tmp13) << 2, FIX_0_707106781);
        dataptr[0] = tmp13 + z1;
        dataptr[1] = tmp13 - z1;

        // Odd part

        tmp10 = (tmp4 + tmp5) << 2;
        tmp11 = (tmp5 + tmp6) << 2;
        tmp12 = (tmp6 + tmp7) << 2;

        z5 = MULTIPLY16H(tmp10 - tmp12, FIX_0_382683433);
        z2 = MULTIPLY16H(tmp10,         FIX_0_541196100) + z5;
        z4 = MULTIPLY16H(tmp12,         FIX_1_306562965) + z5;
        z3 = MULTIPLY16H(tmp11,         FIX_0_707106781);

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

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_NONE
};

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    FSPPContext *fspp = ctx->priv;
    const int h = FFALIGN(inlink->h + 16, 16);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    fspp->hsub = desc->log2_chroma_w;
    fspp->vsub = desc->log2_chroma_h;

    fspp->temp_stride = FFALIGN(inlink->w + 16, 16);
    fspp->temp = av_malloc_array(fspp->temp_stride, h * sizeof(*fspp->temp));
    fspp->src  = av_malloc_array(fspp->temp_stride, h * sizeof(*fspp->src));

    if (!fspp->temp || !fspp->src)
        return AVERROR(ENOMEM);

    fspp->store_slice  = store_slice_c;
    fspp->store_slice2 = store_slice2_c;
    fspp->mul_thrmat   = mul_thrmat_c;
    fspp->column_fidct = column_fidct_c;
    fspp->row_idct     = row_idct_c;
    fspp->row_fdct     = row_fdct_c;

#if ARCH_X86
    ff_fspp_init_x86(fspp);
#endif

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    FSPPContext *fspp = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = in;

    int qp_stride = 0;
    int8_t *qp_table = NULL;
    int i, bias;
    int ret = 0;
    int custom_threshold_m[64];

    bias = (1 << 4) + fspp->strength;

    for (i = 0; i < 64; i++) //FIXME: tune custom_threshold[] and remove this !
        custom_threshold_m[i] = (int)(custom_threshold[i] * (bias / 71.0) + 0.5);

    for (i = 0; i < 8; i++) {
        fspp->threshold_mtx_noq[2 * i] = (uint64_t)custom_threshold_m[i * 8 + 2]
                                      |(((uint64_t)custom_threshold_m[i * 8 + 6]) << 16)
                                      |(((uint64_t)custom_threshold_m[i * 8 + 0]) << 32)
                                      |(((uint64_t)custom_threshold_m[i * 8 + 4]) << 48);

        fspp->threshold_mtx_noq[2 * i + 1] = (uint64_t)custom_threshold_m[i * 8 + 5]
                                          |(((uint64_t)custom_threshold_m[i * 8 + 3]) << 16)
                                          |(((uint64_t)custom_threshold_m[i * 8 + 1]) << 32)
                                          |(((uint64_t)custom_threshold_m[i * 8 + 7]) << 48);
    }

    if (fspp->qp)
        fspp->prev_q = fspp->qp, fspp->mul_thrmat((int16_t *)(&fspp->threshold_mtx_noq[0]), (int16_t *)(&fspp->threshold_mtx[0]), fspp->qp);

    /* if we are not in a constant user quantizer mode and we don't want to use
     * the quantizers from the B-frames (B-frames often have a higher QP), we
     * need to save the qp table from the last non B-frame; this is what the
     * following code block does */
    if (!fspp->qp && (fspp->use_bframe_qp || in->pict_type != AV_PICTURE_TYPE_B)) {
        ret = ff_qp_table_extract(in, &qp_table, &qp_stride, NULL, &fspp->qscale_type);
        if (ret < 0) {
            av_frame_free(&in);
            return ret;
        }

        if (!fspp->use_bframe_qp && in->pict_type != AV_PICTURE_TYPE_B) {
            av_freep(&fspp->non_b_qp_table);
            fspp->non_b_qp_table  = qp_table;
            fspp->non_b_qp_stride = qp_stride;
        }
    }

    if (fspp->log2_count && !ctx->is_disabled) {
        if (!fspp->use_bframe_qp && fspp->non_b_qp_table) {
            qp_table = fspp->non_b_qp_table;
            qp_stride = fspp->non_b_qp_stride;
        }

        if (qp_table || fspp->qp) {
            const int cw = AV_CEIL_RSHIFT(inlink->w, fspp->hsub);
            const int ch = AV_CEIL_RSHIFT(inlink->h, fspp->vsub);

            /* get a new frame if in-place is not possible or if the dimensions
             * are not multiple of 8 */
            if (!av_frame_is_writable(in) || (inlink->w & 7) || (inlink->h & 7)) {
                const int aligned_w = FFALIGN(inlink->w, 8);
                const int aligned_h = FFALIGN(inlink->h, 8);

                out = ff_get_video_buffer(outlink, aligned_w, aligned_h);
                if (!out) {
                    av_frame_free(&in);
                    ret = AVERROR(ENOMEM);
                    goto finish;
                }
                av_frame_copy_props(out, in);
                out->width = in->width;
                out->height = in->height;
            }

            filter(fspp, out->data[0], in->data[0], out->linesize[0], in->linesize[0],
                   inlink->w, inlink->h, qp_table, qp_stride, 1);
            filter(fspp, out->data[1], in->data[1], out->linesize[1], in->linesize[1],
                   cw,        ch,        qp_table, qp_stride, 0);
            filter(fspp, out->data[2], in->data[2], out->linesize[2], in->linesize[2],
                   cw,        ch,        qp_table, qp_stride, 0);
            emms_c();
        }
    }

    if (in != out) {
        if (in->data[3])
            av_image_copy_plane(out->data[3], out->linesize[3],
                                in ->data[3], in ->linesize[3],
                                inlink->w, inlink->h);
        av_frame_free(&in);
    }
    ret = ff_filter_frame(outlink, out);
finish:
    if (qp_table != fspp->non_b_qp_table)
        av_freep(&qp_table);
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FSPPContext *fspp = ctx->priv;
    av_freep(&fspp->temp);
    av_freep(&fspp->src);
    av_freep(&fspp->non_b_qp_table);
}

static const AVFilterPad fspp_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad fspp_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_fspp = {
    .name            = "fspp",
    .description     = NULL_IF_CONFIG_SMALL("Apply Fast Simple Post-processing filter."),
    .priv_size       = sizeof(FSPPContext),
    .uninit          = uninit,
    FILTER_INPUTS(fspp_inputs),
    FILTER_OUTPUTS(fspp_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class      = &fspp_class,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
