/*
 * Apple ProRes compatible decoder
 *
 * Copyright (c) 2010-2011 Maxim Poliakovski
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * This is a decoder for Apple ProRes 422 SD/HQ/LT/Proxy and ProRes 4444.
 * It is used for storing and editing high definition video data in Apple's Final Cut Pro.
 *
 * @see http://wiki.multimedia.cx/index.php?title=Apple_ProRes
 */

#define A32_BITSTREAM_READER // some ProRes vlc codes require up to 28 bits to be read at once

#include <stdint.h>

#include "libavutil/intmath.h"
#include "avcodec.h"
#include "proresdsp.h"
#include "get_bits.h"

typedef struct {
    const uint8_t *index;            ///< pointers to the data of this slice
    int slice_num;
    int x_pos, y_pos;
    int slice_width;
    DECLARE_ALIGNED(16, DCTELEM, blocks[8 * 4 * 64]);
} ProresThreadData;

typedef struct {
    ProresDSPContext dsp;
    AVFrame    picture;
    ScanTable  scantable;
    int        scantable_type;           ///< -1 = uninitialized, 0 = progressive, 1/2 = interlaced

    int        frame_type;               ///< 0 = progressive, 1 = top-field first, 2 = bottom-field first
    int        pic_format;               ///< 2 = 422, 3 = 444
    uint8_t    qmat_luma[64];            ///< dequantization matrix for luma
    uint8_t    qmat_chroma[64];          ///< dequantization matrix for chroma
    int        qmat_changed;             ///< 1 - global quantization matrices changed
    int        prev_slice_sf;            ///< scalefactor of the previous decoded slice
    DECLARE_ALIGNED(16, int16_t, qmat_luma_scaled[64]);
    DECLARE_ALIGNED(16, int16_t, qmat_chroma_scaled[64]);
    int        total_slices;            ///< total number of slices in a picture
    ProresThreadData *slice_data;
    int        pic_num;
    int        chroma_factor;
    int        mb_chroma_factor;
    int        num_chroma_blocks;       ///< number of chrominance blocks in a macroblock
    int        num_x_slices;
    int        num_y_slices;
    int        slice_width_factor;
    int        slice_height_factor;
    int        num_x_mbs;
    int        num_y_mbs;
    int        alpha_info;
} ProresContext;


static const uint8_t progressive_scan[64] = {
     0,  1,  8,  9,  2,  3, 10, 11,
    16, 17, 24, 25, 18, 19, 26, 27,
     4,  5, 12, 20, 13,  6,  7, 14,
    21, 28, 29, 22, 15, 23, 30, 31,
    32, 33, 40, 48, 41, 34, 35, 42,
    49, 56, 57, 50, 43, 36, 37, 44,
    51, 58, 59, 52, 45, 38, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static const uint8_t interlaced_scan[64] = {
     0,  8,  1,  9, 16, 24, 17, 25,
     2, 10,  3, 11, 18, 26, 19, 27,
    32, 40, 33, 34, 41, 48, 56, 49,
    42, 35, 43, 50, 57, 58, 51, 59,
     4, 12,  5,  6, 13, 20, 28, 21,
    14,  7, 15, 22, 29, 36, 44, 37,
    30, 23, 31, 38, 45, 52, 60, 53,
    46, 39, 47, 54, 61, 62, 55, 63
};


static av_cold int decode_init(AVCodecContext *avctx)
{
    ProresContext *ctx = avctx->priv_data;

    ctx->total_slices     = 0;
    ctx->slice_data       = NULL;

    avctx->bits_per_raw_sample = PRORES_BITS_PER_SAMPLE;
    ff_proresdsp_init(&ctx->dsp, avctx);

    avctx->coded_frame = &ctx->picture;
    avcodec_get_frame_defaults(&ctx->picture);
    ctx->picture.type      = AV_PICTURE_TYPE_I;
    ctx->picture.key_frame = 1;

    ctx->scantable_type = -1;   // set scantable type to uninitialized
    memset(ctx->qmat_luma, 4, 64);
    memset(ctx->qmat_chroma, 4, 64);
    ctx->prev_slice_sf = 0;

    return 0;
}


static int decode_frame_header(ProresContext *ctx, const uint8_t *buf,
                               const int data_size, AVCodecContext *avctx)
{
    int hdr_size, version, width, height, flags;
    const uint8_t *ptr;

    hdr_size = AV_RB16(buf);
    if (hdr_size > data_size) {
        av_log(avctx, AV_LOG_ERROR, "frame data too small\n");
        return AVERROR_INVALIDDATA;
    }

    version = AV_RB16(buf + 2);
    if (version >= 2) {
        av_log(avctx, AV_LOG_ERROR,
               "unsupported header version: %d\n", version);
        return AVERROR_INVALIDDATA;
    }

    width  = AV_RB16(buf + 8);
    height = AV_RB16(buf + 10);
    if (width != avctx->width || height != avctx->height) {
        av_log(avctx, AV_LOG_ERROR,
               "picture dimension changed: old: %d x %d, new: %d x %d\n",
               avctx->width, avctx->height, width, height);
        return AVERROR_INVALIDDATA;
    }

    ctx->frame_type = (buf[12] >> 2) & 3;
    if (ctx->frame_type > 2) {
        av_log(avctx, AV_LOG_ERROR,
               "unsupported frame type: %d\n", ctx->frame_type);
        return AVERROR_INVALIDDATA;
    }

    ctx->chroma_factor     = (buf[12] >> 6) & 3;
    ctx->mb_chroma_factor  = ctx->chroma_factor + 2;
    ctx->num_chroma_blocks = (1 << ctx->chroma_factor) >> 1;
    switch (ctx->chroma_factor) {
    case 2:
        avctx->pix_fmt = PIX_FMT_YUV422P10;
        break;
    case 3:
        avctx->pix_fmt = PIX_FMT_YUV444P10;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR,
               "unsupported picture format: %d\n", ctx->pic_format);
        return AVERROR_INVALIDDATA;
    }

    if (ctx->scantable_type != ctx->frame_type) {
        if (!ctx->frame_type)
            ff_init_scantable(ctx->dsp.idct_permutation, &ctx->scantable,
                              progressive_scan);
        else
            ff_init_scantable(ctx->dsp.idct_permutation, &ctx->scantable,
                              interlaced_scan);
        ctx->scantable_type = ctx->frame_type;
    }

    if (ctx->frame_type) {      /* if interlaced */
        ctx->picture.interlaced_frame = 1;
        ctx->picture.top_field_first  = ctx->frame_type & 1;
    }

    ctx->alpha_info = buf[17] & 0xf;
    if (ctx->alpha_info)
        av_log_missing_feature(avctx, "alpha channel", 0);

    ctx->qmat_changed = 0;
    ptr   = buf + 20;
    flags = buf[19];
    if (flags & 2) {
        if (ptr - buf > hdr_size - 64) {
            av_log(avctx, AV_LOG_ERROR, "header data too small\n");
            return AVERROR_INVALIDDATA;
        }
        if (memcmp(ctx->qmat_luma, ptr, 64)) {
            memcpy(ctx->qmat_luma, ptr, 64);
            ctx->qmat_changed = 1;
        }
        ptr += 64;
    } else {
        memset(ctx->qmat_luma, 4, 64);
        ctx->qmat_changed = 1;
    }

    if (flags & 1) {
        if (ptr - buf > hdr_size - 64) {
            av_log(avctx, AV_LOG_ERROR, "header data too small\n");
            return -1;
        }
        if (memcmp(ctx->qmat_chroma, ptr, 64)) {
            memcpy(ctx->qmat_chroma, ptr, 64);
            ctx->qmat_changed = 1;
        }
    } else {
        memset(ctx->qmat_chroma, 4, 64);
        ctx->qmat_changed = 1;
    }

    return hdr_size;
}


static int decode_picture_header(ProresContext *ctx, const uint8_t *buf,
                                 const int data_size, AVCodecContext *avctx)
{
    int   i, hdr_size, pic_data_size, num_slices;
    int   slice_width_factor, slice_height_factor;
    int   remainder, num_x_slices;
    const uint8_t *data_ptr, *index_ptr;

    hdr_size = data_size > 0 ? buf[0] >> 3 : 0;
    if (hdr_size < 8 || hdr_size > data_size) {
        av_log(avctx, AV_LOG_ERROR, "picture header too small\n");
        return AVERROR_INVALIDDATA;
    }

    pic_data_size = AV_RB32(buf + 1);
    if (pic_data_size > data_size) {
        av_log(avctx, AV_LOG_ERROR, "picture data too small\n");
        return AVERROR_INVALIDDATA;
    }

    slice_width_factor  = buf[7] >> 4;
    slice_height_factor = buf[7] & 0xF;
    if (slice_width_factor > 3 || slice_height_factor) {
        av_log(avctx, AV_LOG_ERROR,
               "unsupported slice dimension: %d x %d\n",
               1 << slice_width_factor, 1 << slice_height_factor);
        return AVERROR_INVALIDDATA;
    }

    ctx->slice_width_factor  = slice_width_factor;
    ctx->slice_height_factor = slice_height_factor;

    ctx->num_x_mbs = (avctx->width + 15) >> 4;
    ctx->num_y_mbs = (avctx->height +
                      (1 << (4 + ctx->picture.interlaced_frame)) - 1) >>
                     (4 + ctx->picture.interlaced_frame);

    remainder    = ctx->num_x_mbs & ((1 << slice_width_factor) - 1);
    num_x_slices = (ctx->num_x_mbs >> slice_width_factor) + (remainder & 1) +
                   ((remainder >> 1) & 1) + ((remainder >> 2) & 1);

    num_slices = num_x_slices * ctx->num_y_mbs;
    if (num_slices != AV_RB16(buf + 5)) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of slices\n");
        return AVERROR_INVALIDDATA;
    }

    if (ctx->total_slices != num_slices) {
        av_freep(&ctx->slice_data);
        ctx->slice_data = av_malloc((num_slices + 1) * sizeof(ctx->slice_data[0]));
        if (!ctx->slice_data)
            return AVERROR(ENOMEM);
        ctx->total_slices = num_slices;
    }

    if (hdr_size + num_slices * 2 > data_size) {
        av_log(avctx, AV_LOG_ERROR, "slice table too small\n");
        return AVERROR_INVALIDDATA;
    }

    /* parse slice table allowing quick access to the slice data */
    index_ptr = buf + hdr_size;
    data_ptr = index_ptr + num_slices * 2;

    for (i = 0; i < num_slices; i++) {
        ctx->slice_data[i].index = data_ptr;
        data_ptr += AV_RB16(index_ptr + i * 2);
    }
    ctx->slice_data[i].index = data_ptr;

    if (data_ptr > buf + data_size) {
        av_log(avctx, AV_LOG_ERROR, "out of slice data\n");
        return -1;
    }

    return pic_data_size;
}


/**
 * Read an unsigned rice/exp golomb codeword.
 */
static inline int decode_vlc_codeword(GetBitContext *gb, uint8_t codebook)
{
    unsigned int rice_order, exp_order, switch_bits;
    unsigned int buf, code;
    int log, prefix_len, len;

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf = GET_CACHE(re, gb);

    /* number of prefix bits to switch between Rice and expGolomb */
    switch_bits = (codebook & 3) + 1;
    rice_order  = codebook >> 5;        /* rice code order */
    exp_order   = (codebook >> 2) & 7;  /* exp golomb code order */

    log = 31 - av_log2(buf); /* count prefix bits (zeroes) */

    if (log < switch_bits) { /* ok, we got a rice code */
        if (!rice_order) {
            /* shortcut for faster decoding of rice codes without remainder */
            code = log;
            LAST_SKIP_BITS(re, gb, log + 1);
        } else {
            prefix_len = log + 1;
            code = (log << rice_order) + NEG_USR32(buf << prefix_len, rice_order);
            LAST_SKIP_BITS(re, gb, prefix_len + rice_order);
        }
    } else { /* otherwise we got a exp golomb code */
        len  = (log << 1) - switch_bits + exp_order + 1;
        code = NEG_USR32(buf, len) - (1 << exp_order) + (switch_bits << rice_order);
        LAST_SKIP_BITS(re, gb, len);
    }

    CLOSE_READER(re, gb);

    return code;
}

#define LSB2SIGN(x) (-((x) & 1))
#define TOSIGNED(x) (((x) >> 1) ^ LSB2SIGN(x))

#define FIRST_DC_CB 0xB8 // rice_order = 5, exp_golomb_order = 6, switch_bits = 0

static uint8_t dc_codebook[4] = {
    0x04, // rice_order = 0, exp_golomb_order = 1, switch_bits = 0
    0x28, // rice_order = 1, exp_golomb_order = 2, switch_bits = 0
    0x4D, // rice_order = 2, exp_golomb_order = 3, switch_bits = 1
    0x70  // rice_order = 3, exp_golomb_order = 4, switch_bits = 0
};


/**
 * Decode DC coefficients for all blocks in a slice.
 */
static inline void decode_dc_coeffs(GetBitContext *gb, DCTELEM *out,
                                    int nblocks)
{
    DCTELEM prev_dc;
    int     i, sign;
    int16_t delta;
    unsigned int code;

    code   = decode_vlc_codeword(gb, FIRST_DC_CB);
    out[0] = prev_dc = TOSIGNED(code);

    out   += 64; /* move to the DC coeff of the next block */
    delta  = 3;

    for (i = 1; i < nblocks; i++, out += 64) {
        code = decode_vlc_codeword(gb, dc_codebook[FFMIN(FFABS(delta), 3)]);

        sign     = -(((delta >> 15) & 1) ^ (code & 1));
        delta    = (((code + 1) >> 1) ^ sign) - sign;
        prev_dc += delta;
        out[0]   = prev_dc;
    }
}


static uint8_t ac_codebook[7] = {
    0x04, // rice_order = 0, exp_golomb_order = 1, switch_bits = 0
    0x28, // rice_order = 1, exp_golomb_order = 2, switch_bits = 0
    0x4C, // rice_order = 2, exp_golomb_order = 3, switch_bits = 0
    0x05, // rice_order = 0, exp_golomb_order = 1, switch_bits = 1
    0x29, // rice_order = 1, exp_golomb_order = 2, switch_bits = 1
    0x06, // rice_order = 0, exp_golomb_order = 1, switch_bits = 2
    0x0A, // rice_order = 0, exp_golomb_order = 2, switch_bits = 2
};

/**
 * Lookup tables for adaptive switching between codebooks
 * according with previous run/level value.
 */
static uint8_t run_to_cb_index[16] =
    { 5, 5, 3, 3, 0, 4, 4, 4, 4, 1, 1, 1, 1, 1, 1, 2 };

static uint8_t lev_to_cb_index[10] = { 0, 6, 3, 5, 0, 1, 1, 1, 1, 2 };


/**
 * Decode AC coefficients for all blocks in a slice.
 */
static inline void decode_ac_coeffs(GetBitContext *gb, DCTELEM *out,
                                    int blocks_per_slice,
                                    int plane_size_factor,
                                    const uint8_t *scan)
{
    int pos, block_mask, run, level, sign, run_cb_index, lev_cb_index;
    int max_coeffs, bits_left;

    /* set initial prediction values */
    run   = 4;
    level = 2;

    max_coeffs = blocks_per_slice << 6;
    block_mask = blocks_per_slice - 1;

    for (pos = blocks_per_slice - 1; pos < max_coeffs;) {
        run_cb_index = run_to_cb_index[FFMIN(run, 15)];
        lev_cb_index = lev_to_cb_index[FFMIN(level, 9)];

        bits_left = get_bits_left(gb);
        if (bits_left <= 0 || (bits_left <= 8 && !show_bits(gb, bits_left)))
            return;

        run = decode_vlc_codeword(gb, ac_codebook[run_cb_index]);

        bits_left = get_bits_left(gb);
        if (bits_left <= 0 || (bits_left <= 8 && !show_bits(gb, bits_left)))
            return;

        level = decode_vlc_codeword(gb, ac_codebook[lev_cb_index]) + 1;

        pos += run + 1;
        if (pos >= max_coeffs)
            break;

        sign = get_sbits(gb, 1);
        out[((pos & block_mask) << 6) + scan[pos >> plane_size_factor]] =
            (level ^ sign) - sign;
    }
}


/**
 * Decode a slice plane (luma or chroma).
 */
static void decode_slice_plane(ProresContext *ctx, ProresThreadData *td,
                               const uint8_t *buf,
                               int data_size, uint16_t *out_ptr,
                               int linesize, int mbs_per_slice,
                               int blocks_per_mb, int plane_size_factor,
                               const int16_t *qmat)
{
    GetBitContext gb;
    DCTELEM *block_ptr;
    int mb_num, blocks_per_slice;

    blocks_per_slice = mbs_per_slice * blocks_per_mb;

    memset(td->blocks, 0, 8 * 4 * 64 * sizeof(*td->blocks));

    init_get_bits(&gb, buf, data_size << 3);

    decode_dc_coeffs(&gb, td->blocks, blocks_per_slice);

    decode_ac_coeffs(&gb, td->blocks, blocks_per_slice,
                     plane_size_factor, ctx->scantable.permutated);

    /* inverse quantization, inverse transform and output */
    block_ptr = td->blocks;

    for (mb_num = 0; mb_num < mbs_per_slice; mb_num++, out_ptr += blocks_per_mb * 4) {
        ctx->dsp.idct_put(out_ptr,                    linesize, block_ptr, qmat);
        block_ptr += 64;
        if (blocks_per_mb > 2) {
            ctx->dsp.idct_put(out_ptr + 8,            linesize, block_ptr, qmat);
            block_ptr += 64;
        }
        ctx->dsp.idct_put(out_ptr + linesize * 4,     linesize, block_ptr, qmat);
        block_ptr += 64;
        if (blocks_per_mb > 2) {
            ctx->dsp.idct_put(out_ptr + linesize * 4 + 8, linesize, block_ptr, qmat);
            block_ptr += 64;
        }
    }
}


static int decode_slice(AVCodecContext *avctx, ProresThreadData *td)
{
    ProresContext *ctx = avctx->priv_data;
    int mb_x_pos  = td->x_pos;
    int mb_y_pos  = td->y_pos;
    int pic_num   = ctx->pic_num;
    int slice_num = td->slice_num;
    int mbs_per_slice = td->slice_width;
    const uint8_t *buf;
    uint8_t *y_data, *u_data, *v_data;
    AVFrame *pic = avctx->coded_frame;
    int i, sf, slice_width_factor;
    int slice_data_size, hdr_size, y_data_size, u_data_size, v_data_size;
    int y_linesize, u_linesize, v_linesize;

    buf             = ctx->slice_data[slice_num].index;
    slice_data_size = ctx->slice_data[slice_num + 1].index - buf;

    slice_width_factor = av_log2(mbs_per_slice);

    y_data     = pic->data[0];
    u_data     = pic->data[1];
    v_data     = pic->data[2];
    y_linesize = pic->linesize[0];
    u_linesize = pic->linesize[1];
    v_linesize = pic->linesize[2];

    if (pic->interlaced_frame) {
        if (!(pic_num ^ pic->top_field_first)) {
            y_data += y_linesize;
            u_data += u_linesize;
            v_data += v_linesize;
        }
        y_linesize <<= 1;
        u_linesize <<= 1;
        v_linesize <<= 1;
    }

    if (slice_data_size < 6) {
        av_log(avctx, AV_LOG_ERROR, "slice data too small\n");
        return AVERROR_INVALIDDATA;
    }

    /* parse slice header */
    hdr_size    = buf[0] >> 3;
    y_data_size = AV_RB16(buf + 2);
    u_data_size = AV_RB16(buf + 4);
    v_data_size = hdr_size > 7 ? AV_RB16(buf + 6) :
        slice_data_size - y_data_size - u_data_size - hdr_size;

    if (hdr_size + y_data_size + u_data_size + v_data_size > slice_data_size ||
        v_data_size < 0 || hdr_size < 6) {
        av_log(avctx, AV_LOG_ERROR, "invalid data size\n");
        return AVERROR_INVALIDDATA;
    }

    sf = av_clip(buf[1], 1, 224);
    sf = sf > 128 ? (sf - 96) << 2 : sf;

    /* scale quantization matrixes according with slice's scale factor */
    /* TODO: this can be SIMD-optimized alot */
    if (ctx->qmat_changed || sf != ctx->prev_slice_sf) {
        ctx->prev_slice_sf = sf;
        for (i = 0; i < 64; i++) {
            ctx->qmat_luma_scaled[ctx->dsp.idct_permutation[i]]   = ctx->qmat_luma[i]   * sf;
            ctx->qmat_chroma_scaled[ctx->dsp.idct_permutation[i]] = ctx->qmat_chroma[i] * sf;
        }
    }

    /* decode luma plane */
    decode_slice_plane(ctx, td, buf + hdr_size, y_data_size,
                       (uint16_t*) (y_data + (mb_y_pos << 4) * y_linesize +
                                    (mb_x_pos << 5)), y_linesize,
                       mbs_per_slice, 4, slice_width_factor + 2,
                       ctx->qmat_luma_scaled);

    /* decode U chroma plane */
    decode_slice_plane(ctx, td, buf + hdr_size + y_data_size, u_data_size,
                       (uint16_t*) (u_data + (mb_y_pos << 4) * u_linesize +
                                    (mb_x_pos << ctx->mb_chroma_factor)),
                       u_linesize, mbs_per_slice, ctx->num_chroma_blocks,
                       slice_width_factor + ctx->chroma_factor - 1,
                       ctx->qmat_chroma_scaled);

    /* decode V chroma plane */
    decode_slice_plane(ctx, td, buf + hdr_size + y_data_size + u_data_size,
                       v_data_size,
                       (uint16_t*) (v_data + (mb_y_pos << 4) * v_linesize +
                                    (mb_x_pos << ctx->mb_chroma_factor)),
                       v_linesize, mbs_per_slice, ctx->num_chroma_blocks,
                       slice_width_factor + ctx->chroma_factor - 1,
                       ctx->qmat_chroma_scaled);

    return 0;
}


static int decode_picture(ProresContext *ctx, int pic_num,
                          AVCodecContext *avctx)
{
    int slice_num, slice_width, x_pos, y_pos;

    slice_num = 0;

    ctx->pic_num = pic_num;
    for (y_pos = 0; y_pos < ctx->num_y_mbs; y_pos++) {
        slice_width = 1 << ctx->slice_width_factor;

        for (x_pos = 0; x_pos < ctx->num_x_mbs && slice_width;
             x_pos += slice_width) {
            while (ctx->num_x_mbs - x_pos < slice_width)
                slice_width >>= 1;

            ctx->slice_data[slice_num].slice_num   = slice_num;
            ctx->slice_data[slice_num].x_pos       = x_pos;
            ctx->slice_data[slice_num].y_pos       = y_pos;
            ctx->slice_data[slice_num].slice_width = slice_width;

            slice_num++;
        }
    }

    return avctx->execute(avctx, (void *) decode_slice,
                          ctx->slice_data, NULL, slice_num,
                          sizeof(ctx->slice_data[0]));
}


#define FRAME_ID MKBETAG('i', 'c', 'p', 'f')
#define MOVE_DATA_PTR(nbytes) buf += (nbytes); buf_size -= (nbytes)

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                        AVPacket *avpkt)
{
    ProresContext *ctx = avctx->priv_data;
    AVFrame *picture   = avctx->coded_frame;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int frame_hdr_size, pic_num, pic_data_size;

    /* check frame atom container */
    if (buf_size < 28 || buf_size < AV_RB32(buf) ||
        AV_RB32(buf + 4) != FRAME_ID) {
        av_log(avctx, AV_LOG_ERROR, "invalid frame\n");
        return AVERROR_INVALIDDATA;
    }

    MOVE_DATA_PTR(8);

    frame_hdr_size = decode_frame_header(ctx, buf, buf_size, avctx);
    if (frame_hdr_size < 0)
        return AVERROR_INVALIDDATA;

    MOVE_DATA_PTR(frame_hdr_size);

    if (picture->data[0])
        avctx->release_buffer(avctx, picture);

    picture->reference = 0;
    if (avctx->get_buffer(avctx, picture) < 0)
        return -1;

    for (pic_num = 0; ctx->picture.interlaced_frame - pic_num + 1; pic_num++) {
        pic_data_size = decode_picture_header(ctx, buf, buf_size, avctx);
        if (pic_data_size < 0)
            return AVERROR_INVALIDDATA;

        if (decode_picture(ctx, pic_num, avctx))
            return -1;

        MOVE_DATA_PTR(pic_data_size);
    }

    *data_size       = sizeof(AVPicture);
    *(AVFrame*) data = *avctx->coded_frame;

    return avpkt->size;
}


static av_cold int decode_close(AVCodecContext *avctx)
{
    ProresContext *ctx = avctx->priv_data;

    if (ctx->picture.data[0])
        avctx->release_buffer(avctx, &ctx->picture);

    av_freep(&ctx->slice_data);

    return 0;
}


AVCodec ff_prores_lgpl_decoder = {
    .name           = "prores_lgpl",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PRORES,
    .priv_data_size = sizeof(ProresContext),
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_SLICE_THREADS,
    .long_name      = NULL_IF_CONFIG_SMALL("Apple ProRes (iCodec Pro)")
};
