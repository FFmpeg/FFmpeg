/*
 * RTJpeg decoding functions
 * Copyright (c) 2006 Reimar Doeffinger
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
#include "get_bits.h"
#include "rtjpeg.h"

#define PUT_COEFF(c) \
    i = scan[coeff--]; \
    block[i] = (c) * quant[i];

/// aligns the bitstream to the given power of two
#define ALIGN(a) \
    n = (-get_bits_count(gb)) & (a - 1); \
    if (n) {skip_bits(gb, n);}

/**
 * @brief read one block from stream
 * @param gb contains stream data
 * @param block where data is written to
 * @param scan array containing the mapping stream address -> block position
 * @param quant quantization factors
 * @return 0 means the block is not coded, < 0 means an error occurred.
 *
 * Note: GetBitContext is used to make the code simpler, since all data is
 * aligned this could be done faster in a different way, e.g. as it is done
 * in MPlayer libmpcodecs/native/rtjpegn.c.
 */
static inline int get_block(GetBitContext *gb, int16_t *block, const uint8_t *scan,
                            const uint32_t *quant) {
    int coeff, i, n;
    int8_t ac;
    uint8_t dc = get_bits(gb, 8);

    // block not coded
    if (dc == 255)
       return 0;

    // number of non-zero coefficients
    coeff = get_bits(gb, 6);
    if (get_bits_left(gb) < (coeff << 1))
        return AVERROR_INVALIDDATA;

    // normally we would only need to clear the (63 - coeff) last values,
    // but since we do not know where they are we just clear the whole block
    memset(block, 0, 64 * sizeof(int16_t));

    // 2 bits per coefficient
    while (coeff) {
        ac = get_sbits(gb, 2);
        if (ac == -2)
            break; // continue with more bits
        PUT_COEFF(ac);
    }

    // 4 bits per coefficient
    ALIGN(4);
    if (get_bits_left(gb) < (coeff << 2))
        return AVERROR_INVALIDDATA;
    while (coeff) {
        ac = get_sbits(gb, 4);
        if (ac == -8)
            break; // continue with more bits
        PUT_COEFF(ac);
    }

    // 8 bits per coefficient
    ALIGN(8);
    if (get_bits_left(gb) < (coeff << 3))
        return AVERROR_INVALIDDATA;
    while (coeff) {
        ac = get_sbits(gb, 8);
        PUT_COEFF(ac);
    }

    PUT_COEFF(dc);
    return 1;
}

/**
 * @brief decode one rtjpeg YUV420 frame
 * @param c context, must be initialized via ff_rtjpeg_decode_init
 * @param f AVFrame to place decoded frame into. If parts of the frame
 *          are not coded they are left unchanged, so consider initializing it
 * @param buf buffer containing input data
 * @param buf_size length of input data in bytes
 * @return number of bytes consumed from the input buffer
 */
int ff_rtjpeg_decode_frame_yuv420(RTJpegContext *c, AVFrame *f,
                                  const uint8_t *buf, int buf_size) {
    GetBitContext gb;
    int w = c->w / 16, h = c->h / 16;
    int x, y, ret;
    uint8_t *y1 = f->data[0], *y2 = f->data[0] + 8 * f->linesize[0];
    uint8_t *u = f->data[1], *v = f->data[2];

    if ((ret = init_get_bits8(&gb, buf, buf_size)) < 0)
        return ret;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
#define BLOCK(quant, dst, stride) do { \
    int res = get_block(&gb, block, c->scan, quant); \
    if (res < 0) \
        return res; \
    if (res > 0) \
        c->dsp->idct_put(dst, stride, block); \
} while (0)
            int16_t *block = c->block;
            BLOCK(c->lquant, y1, f->linesize[0]);
            y1 += 8;
            BLOCK(c->lquant, y1, f->linesize[0]);
            y1 += 8;
            BLOCK(c->lquant, y2, f->linesize[0]);
            y2 += 8;
            BLOCK(c->lquant, y2, f->linesize[0]);
            y2 += 8;
            BLOCK(c->cquant, u,  f->linesize[1]);
            u += 8;
            BLOCK(c->cquant, v,  f->linesize[2]);
            v += 8;
        }
        y1 += 2 * 8 * (f->linesize[0] - w);
        y2 += 2 * 8 * (f->linesize[0] - w);
        u += 8 * (f->linesize[1] - w);
        v += 8 * (f->linesize[2] - w);
    }
    return get_bits_count(&gb) / 8;
}

/**
 * @brief initialize an RTJpegContext, may be called multiple times
 * @param c context to initialize
 * @param dsp specifies the idct to use for decoding
 * @param width width of image, will be rounded down to the nearest multiple
 *              of 16 for decoding
 * @param height height of image, will be rounded down to the nearest multiple
 *              of 16 for decoding
 * @param lquant luma quantization table to use
 * @param cquant chroma quantization table to use
 */
void ff_rtjpeg_decode_init(RTJpegContext *c, DSPContext *dsp,
                           int width, int height,
                           const uint32_t *lquant, const uint32_t *cquant) {
    int i;
    c->dsp = dsp;
    for (i = 0; i < 64; i++) {
        int z = ff_zigzag_direct[i];
        int p = c->dsp->idct_permutation[i];
        z = ((z << 3) | (z >> 3)) & 63; // rtjpeg uses a transposed variant

        // permute the scan and quantization tables for the chosen idct
        c->scan[i] = c->dsp->idct_permutation[z];
        c->lquant[p] = lquant[i];
        c->cquant[p] = cquant[i];
    }
    c->w = width;
    c->h = height;
}
