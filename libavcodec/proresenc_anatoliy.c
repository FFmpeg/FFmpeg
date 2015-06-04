/*
 * Apple ProRes encoder
 *
 * Copyright (c) 2011 Anatoliy Wasserman
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

/**
 * @file
 * Apple ProRes encoder (Anatoliy Wasserman version)
 * Known FOURCCs: 'apch' (HQ), 'apcn' (SD), 'apcs' (LT), 'acpo' (Proxy)
 */

#include "avcodec.h"
#include "dct.h"
#include "internal.h"
#include "put_bits.h"
#include "bytestream.h"
#include "fdctdsp.h"

#define DEFAULT_SLICE_MB_WIDTH 8

#define FF_PROFILE_PRORES_PROXY     0
#define FF_PROFILE_PRORES_LT        1
#define FF_PROFILE_PRORES_STANDARD  2
#define FF_PROFILE_PRORES_HQ        3

static const AVProfile profiles[] = {
    { FF_PROFILE_PRORES_PROXY,    "apco"},
    { FF_PROFILE_PRORES_LT,       "apcs"},
    { FF_PROFILE_PRORES_STANDARD, "apcn"},
    { FF_PROFILE_PRORES_HQ,       "apch"},
    { FF_PROFILE_UNKNOWN }
};

static const int qp_start_table[4] = { 4, 1, 1, 1 };
static const int qp_end_table[4]   = { 8, 9, 6, 6 };
static const int bitrate_table[5]  = { 1000, 2100, 3500, 5400 };

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

static const uint8_t QMAT_LUMA[4][64] = {
    {
         4,  7,  9, 11, 13, 14, 15, 63,
         7,  7, 11, 12, 14, 15, 63, 63,
         9, 11, 13, 14, 15, 63, 63, 63,
        11, 11, 13, 14, 63, 63, 63, 63,
        11, 13, 14, 63, 63, 63, 63, 63,
        13, 14, 63, 63, 63, 63, 63, 63,
        13, 63, 63, 63, 63, 63, 63, 63,
        63, 63, 63, 63, 63, 63, 63, 63
    }, {
         4,  5,  6,  7,  9, 11, 13, 15,
         5,  5,  7,  8, 11, 13, 15, 17,
         6,  7,  9, 11, 13, 15, 15, 17,
         7,  7,  9, 11, 13, 15, 17, 19,
         7,  9, 11, 13, 14, 16, 19, 23,
         9, 11, 13, 14, 16, 19, 23, 29,
         9, 11, 13, 15, 17, 21, 28, 35,
        11, 13, 16, 17, 21, 28, 35, 41
    }, {
         4,  4,  5,  5,  6,  7,  7,  9,
         4,  4,  5,  6,  7,  7,  9,  9,
         5,  5,  6,  7,  7,  9,  9, 10,
         5,  5,  6,  7,  7,  9,  9, 10,
         5,  6,  7,  7,  8,  9, 10, 12,
         6,  7,  7,  8,  9, 10, 12, 15,
         6,  7,  7,  9, 10, 11, 14, 17,
         7,  7,  9, 10, 11, 14, 17, 21
    }, {
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  5,
         4,  4,  4,  4,  4,  4,  5,  5,
         4,  4,  4,  4,  4,  5,  5,  6,
         4,  4,  4,  4,  5,  5,  6,  7,
         4,  4,  4,  4,  5,  6,  7,  7
    }
};

static const uint8_t QMAT_CHROMA[4][64] = {
    {
         4,  7,  9, 11, 13, 14, 63, 63,
         7,  7, 11, 12, 14, 63, 63, 63,
         9, 11, 13, 14, 63, 63, 63, 63,
        11, 11, 13, 14, 63, 63, 63, 63,
        11, 13, 14, 63, 63, 63, 63, 63,
        13, 14, 63, 63, 63, 63, 63, 63,
        13, 63, 63, 63, 63, 63, 63, 63,
        63, 63, 63, 63, 63, 63, 63, 63
    }, {
         4,  5,  6,  7,  9, 11, 13, 15,
         5,  5,  7,  8, 11, 13, 15, 17,
         6,  7,  9, 11, 13, 15, 15, 17,
         7,  7,  9, 11, 13, 15, 17, 19,
         7,  9, 11, 13, 14, 16, 19, 23,
         9, 11, 13, 14, 16, 19, 23, 29,
         9, 11, 13, 15, 17, 21, 28, 35,
        11, 13, 16, 17, 21, 28, 35, 41
    }, {
         4,  4,  5,  5,  6,  7,  7,  9,
         4,  4,  5,  6,  7,  7,  9,  9,
         5,  5,  6,  7,  7,  9,  9, 10,
         5,  5,  6,  7,  7,  9,  9, 10,
         5,  6,  7,  7,  8,  9, 10, 12,
         6,  7,  7,  8,  9, 10, 12, 15,
         6,  7,  7,  9, 10, 11, 14, 17,
         7,  7,  9, 10, 11, 14, 17, 21
    }, {
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  5,
         4,  4,  4,  4,  4,  4,  5,  5,
         4,  4,  4,  4,  4,  5,  5,  6,
         4,  4,  4,  4,  5,  5,  6,  7,
         4,  4,  4,  4,  5,  6,  7,  7
    }
};


typedef struct {
    FDCTDSPContext fdsp;
    uint8_t* fill_y;
    uint8_t* fill_u;
    uint8_t* fill_v;

    int qmat_luma[16][64];
    int qmat_chroma[16][64];
} ProresContext;

static void encode_codeword(PutBitContext *pb, int val, int codebook)
{
    unsigned int rice_order, exp_order, switch_bits, first_exp, exp, zeros;

    /* number of bits to switch between rice and exp golomb */
    switch_bits = codebook & 3;
    rice_order  = codebook >> 5;
    exp_order   = (codebook >> 2) & 7;

    first_exp = ((switch_bits + 1) << rice_order);

    if (val >= first_exp) { /* exp golomb */
        val -= first_exp;
        val += (1 << exp_order);
        exp = av_log2(val);
        zeros = exp - exp_order + switch_bits + 1;
        put_bits(pb, zeros, 0);
        put_bits(pb, exp + 1, val);
    } else if (rice_order) {
        put_bits(pb, (val >> rice_order), 0);
        put_bits(pb, 1, 1);
        put_sbits(pb, rice_order, val);
    } else {
        put_bits(pb, val, 0);
        put_bits(pb, 1, 1);
    }
}

#define QSCALE(qmat,ind,val) ((val) / ((qmat)[ind]))
#define TO_GOLOMB(val) (((val) << 1) ^ ((val) >> 31))
#define DIFF_SIGN(val, sign) (((val) >> 31) ^ (sign))
#define IS_NEGATIVE(val) ((((val) >> 31) ^ -1) + 1)
#define TO_GOLOMB2(val,sign) ((val)==0 ? 0 : ((val) << 1) + (sign))

static av_always_inline int get_level(int val)
{
    int sign = (val >> 31);
    return (val ^ sign) - sign;
}

#define FIRST_DC_CB 0xB8

static const uint8_t dc_codebook[7] = { 0x04, 0x28, 0x28, 0x4D, 0x4D, 0x70, 0x70};

static void encode_dc_coeffs(PutBitContext *pb, int16_t *in,
        int blocks_per_slice, int *qmat)
{
    int prev_dc, code;
    int i, sign, idx;
    int new_dc, delta, diff_sign, new_code;

    prev_dc = QSCALE(qmat, 0, in[0] - 16384);
    code = TO_GOLOMB(prev_dc);
    encode_codeword(pb, code, FIRST_DC_CB);

    code = 5; sign = 0; idx = 64;
    for (i = 1; i < blocks_per_slice; i++, idx += 64) {
        new_dc    = QSCALE(qmat, 0, in[idx] - 16384);
        delta     = new_dc - prev_dc;
        diff_sign = DIFF_SIGN(delta, sign);
        new_code  = TO_GOLOMB2(get_level(delta), diff_sign);

        encode_codeword(pb, new_code, dc_codebook[FFMIN(code, 6)]);

        code      = new_code;
        sign      = delta >> 31;
        prev_dc   = new_dc;
    }
}

static const uint8_t run_to_cb[16] = { 0x06, 0x06, 0x05, 0x05, 0x04, 0x29,
        0x29, 0x29, 0x29, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x4C };
static const uint8_t lev_to_cb[10] = { 0x04, 0x0A, 0x05, 0x06, 0x04, 0x28,
        0x28, 0x28, 0x28, 0x4C };

static void encode_ac_coeffs(AVCodecContext *avctx, PutBitContext *pb,
        int16_t *in, int blocks_per_slice, int *qmat)
{
    int prev_run = 4;
    int prev_level = 2;

    int run = 0, level, code, i, j;
    for (i = 1; i < 64; i++) {
        int indp = progressive_scan[i];
        for (j = 0; j < blocks_per_slice; j++) {
            int val = QSCALE(qmat, indp, in[(j << 6) + indp]);
            if (val) {
                encode_codeword(pb, run, run_to_cb[FFMIN(prev_run, 15)]);

                prev_run   = run;
                run        = 0;
                level      = get_level(val);
                code       = level - 1;

                encode_codeword(pb, code, lev_to_cb[FFMIN(prev_level, 9)]);

                prev_level = level;

                put_bits(pb, 1, IS_NEGATIVE(val));
            } else {
                ++run;
            }
        }
    }
}

static void get(uint8_t *pixels, int stride, int16_t* block)
{
    int i;

    for (i = 0; i < 8; i++) {
        AV_WN64(block, AV_RN64(pixels));
        AV_WN64(block+4, AV_RN64(pixels+8));
        pixels += stride;
        block += 8;
    }
}

static void fdct_get(FDCTDSPContext *fdsp, uint8_t *pixels, int stride, int16_t* block)
{
    get(pixels, stride, block);
    fdsp->fdct(block);
}

static int encode_slice_plane(AVCodecContext *avctx, int mb_count,
        uint8_t *src, int src_stride, uint8_t *buf, unsigned buf_size,
        int *qmat, int chroma)
{
    ProresContext* ctx = avctx->priv_data;
    FDCTDSPContext *fdsp = &ctx->fdsp;
    LOCAL_ALIGNED(16, int16_t, blocks, [DEFAULT_SLICE_MB_WIDTH << 8]);
    int16_t *block;
    int i, blocks_per_slice;
    PutBitContext pb;

    block = blocks;
    for (i = 0; i < mb_count; i++) {
        fdct_get(fdsp, src,                  src_stride, block + (0 << 6));
        fdct_get(fdsp, src + 8 * src_stride, src_stride, block + ((2 - chroma) << 6));
        if (!chroma) {
            fdct_get(fdsp, src + 16,                  src_stride, block + (1 << 6));
            fdct_get(fdsp, src + 16 + 8 * src_stride, src_stride, block + (3 << 6));
        }

        block += (256 >> chroma);
        src   += (32  >> chroma);
    }

    blocks_per_slice = mb_count << (2 - chroma);
    init_put_bits(&pb, buf, buf_size);

    encode_dc_coeffs(&pb, blocks, blocks_per_slice, qmat);
    encode_ac_coeffs(avctx, &pb, blocks, blocks_per_slice, qmat);

    flush_put_bits(&pb);
    return put_bits_ptr(&pb) - pb.buf;
}

static av_always_inline unsigned encode_slice_data(AVCodecContext *avctx,
        uint8_t *dest_y, uint8_t *dest_u, uint8_t *dest_v, int luma_stride,
        int chroma_stride, unsigned mb_count, uint8_t *buf, unsigned data_size,
        unsigned* y_data_size, unsigned* u_data_size, unsigned* v_data_size,
        int qp)
{
    ProresContext* ctx = avctx->priv_data;

    *y_data_size = encode_slice_plane(avctx, mb_count, dest_y, luma_stride,
            buf, data_size, ctx->qmat_luma[qp - 1], 0);

    if (!(avctx->flags & CODEC_FLAG_GRAY)) {
        *u_data_size = encode_slice_plane(avctx, mb_count, dest_u,
                chroma_stride, buf + *y_data_size, data_size - *y_data_size,
                ctx->qmat_chroma[qp - 1], 1);

        *v_data_size = encode_slice_plane(avctx, mb_count, dest_v,
                chroma_stride, buf + *y_data_size + *u_data_size,
                data_size - *y_data_size - *u_data_size,
                ctx->qmat_chroma[qp - 1], 1);
    }

    return *y_data_size + *u_data_size + *v_data_size;
}

static void subimage_with_fill(uint16_t *src, unsigned x, unsigned y,
        unsigned stride, unsigned width, unsigned height, uint16_t *dst,
        unsigned dst_width, unsigned dst_height)
{

    int box_width = FFMIN(width - x, dst_width);
    int box_height = FFMIN(height - y, dst_height);
    int i, j, src_stride = stride >> 1;
    uint16_t last_pix, *last_line;

    src += y * src_stride + x;
    for (i = 0; i < box_height; ++i) {
        for (j = 0; j < box_width; ++j) {
            dst[j] = src[j];
        }
        last_pix = dst[j - 1];
        for (; j < dst_width; j++)
            dst[j] = last_pix;
        src += src_stride;
        dst += dst_width;
    }
    last_line = dst - dst_width;
    for (; i < dst_height; i++) {
        for (j = 0; j < dst_width; ++j) {
            dst[j] = last_line[j];
        }
        dst += dst_width;
    }
}

static int encode_slice(AVCodecContext *avctx, const AVFrame *pic, int mb_x,
        int mb_y, unsigned mb_count, uint8_t *buf, unsigned data_size,
        int unsafe, int *qp)
{
    int luma_stride, chroma_stride;
    int hdr_size = 6, slice_size;
    uint8_t *dest_y, *dest_u, *dest_v;
    unsigned y_data_size = 0, u_data_size = 0, v_data_size = 0;
    ProresContext* ctx = avctx->priv_data;
    int tgt_bits   = (mb_count * bitrate_table[avctx->profile]) >> 2;
    int low_bytes  = (tgt_bits - (tgt_bits >> 3)) >> 3; // 12% bitrate fluctuation
    int high_bytes = (tgt_bits + (tgt_bits >> 3)) >> 3;

    luma_stride   = pic->linesize[0];
    chroma_stride = pic->linesize[1];

    dest_y = pic->data[0] + (mb_y << 4) * luma_stride   + (mb_x << 5);
    dest_u = pic->data[1] + (mb_y << 4) * chroma_stride + (mb_x << 4);
    dest_v = pic->data[2] + (mb_y << 4) * chroma_stride + (mb_x << 4);

    if (unsafe) {

        subimage_with_fill((uint16_t *) pic->data[0], mb_x << 4, mb_y << 4,
                luma_stride, avctx->width, avctx->height,
                (uint16_t *) ctx->fill_y, mb_count << 4, 16);
        subimage_with_fill((uint16_t *) pic->data[1], mb_x << 3, mb_y << 4,
                chroma_stride, avctx->width >> 1, avctx->height,
                (uint16_t *) ctx->fill_u, mb_count << 3, 16);
        subimage_with_fill((uint16_t *) pic->data[2], mb_x << 3, mb_y << 4,
                chroma_stride, avctx->width >> 1, avctx->height,
                (uint16_t *) ctx->fill_v, mb_count << 3, 16);

        encode_slice_data(avctx, ctx->fill_y, ctx->fill_u, ctx->fill_v,
                mb_count << 5, mb_count << 4, mb_count, buf + hdr_size,
                data_size - hdr_size, &y_data_size, &u_data_size, &v_data_size,
                *qp);
    } else {
        slice_size = encode_slice_data(avctx, dest_y, dest_u, dest_v,
                luma_stride, chroma_stride, mb_count, buf + hdr_size,
                data_size - hdr_size, &y_data_size, &u_data_size, &v_data_size,
                *qp);

        if (slice_size > high_bytes && *qp < qp_end_table[avctx->profile]) {
            do {
                *qp += 1;
                slice_size = encode_slice_data(avctx, dest_y, dest_u, dest_v,
                        luma_stride, chroma_stride, mb_count, buf + hdr_size,
                        data_size - hdr_size, &y_data_size, &u_data_size,
                        &v_data_size, *qp);
            } while (slice_size > high_bytes && *qp < qp_end_table[avctx->profile]);
        } else if (slice_size < low_bytes && *qp
                > qp_start_table[avctx->profile]) {
            do {
                *qp -= 1;
                slice_size = encode_slice_data(avctx, dest_y, dest_u, dest_v,
                        luma_stride, chroma_stride, mb_count, buf + hdr_size,
                        data_size - hdr_size, &y_data_size, &u_data_size,
                        &v_data_size, *qp);
            } while (slice_size < low_bytes && *qp > qp_start_table[avctx->profile]);
        }
    }

    buf[0] = hdr_size << 3;
    buf[1] = *qp;
    AV_WB16(buf + 2, y_data_size);
    AV_WB16(buf + 4, u_data_size);

    return hdr_size + y_data_size + u_data_size + v_data_size;
}

static int prores_encode_picture(AVCodecContext *avctx, const AVFrame *pic,
        uint8_t *buf, const int buf_size)
{
    int mb_width = (avctx->width + 15) >> 4;
    int mb_height = (avctx->height + 15) >> 4;
    int hdr_size, sl_size, i;
    int mb_y, sl_data_size, qp;
    int unsafe_bot, unsafe_right;
    uint8_t *sl_data, *sl_data_sizes;
    int slice_per_line = 0, rem = mb_width;

    for (i = av_log2(DEFAULT_SLICE_MB_WIDTH); i >= 0; --i) {
        slice_per_line += rem >> i;
        rem &= (1 << i) - 1;
    }

    qp = qp_start_table[avctx->profile];
    hdr_size = 8; sl_data_size = buf_size - hdr_size;
    sl_data_sizes = buf + hdr_size;
    sl_data = sl_data_sizes + (slice_per_line * mb_height * 2);
    for (mb_y = 0; mb_y < mb_height; mb_y++) {
        int mb_x = 0;
        int slice_mb_count = DEFAULT_SLICE_MB_WIDTH;
        while (mb_x < mb_width) {
            while (mb_width - mb_x < slice_mb_count)
                slice_mb_count >>= 1;

            unsafe_bot = (avctx->height & 0xf) && (mb_y == mb_height - 1);
            unsafe_right = (avctx->width & 0xf) && (mb_x + slice_mb_count == mb_width);

            sl_size = encode_slice(avctx, pic, mb_x, mb_y, slice_mb_count,
                    sl_data, sl_data_size, unsafe_bot || unsafe_right, &qp);

            bytestream_put_be16(&sl_data_sizes, sl_size);
            sl_data           += sl_size;
            sl_data_size      -= sl_size;
            mb_x              += slice_mb_count;
        }
    }

    buf[0] = hdr_size << 3;
    AV_WB32(buf + 1, sl_data - buf);
    AV_WB16(buf + 5, slice_per_line * mb_height);
    buf[7] = av_log2(DEFAULT_SLICE_MB_WIDTH) << 4;

    return sl_data - buf;
}

static int prores_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                               const AVFrame *pict, int *got_packet)
{
    int header_size = 148;
    uint8_t *buf;
    int pic_size, ret;
    int frame_size = FFALIGN(avctx->width, 16) * FFALIGN(avctx->height, 16)*16 + 500 + FF_MIN_BUFFER_SIZE; //FIXME choose tighter limit


    if ((ret = ff_alloc_packet2(avctx, pkt, frame_size + FF_MIN_BUFFER_SIZE)) < 0)
        return ret;

    buf = pkt->data;
    pic_size = prores_encode_picture(avctx, pict, buf + header_size + 8,
            pkt->size - header_size - 8);

    bytestream_put_be32(&buf, pic_size + 8 + header_size);
    bytestream_put_buffer(&buf, "icpf", 4);

    bytestream_put_be16(&buf, header_size);
    bytestream_put_be16(&buf, 0);
    bytestream_put_buffer(&buf, "fmpg", 4);
    bytestream_put_be16(&buf, avctx->width);
    bytestream_put_be16(&buf, avctx->height);
    *buf++ = 0x83; // {10}(422){00}{00}(frame){11}
    *buf++ = 0;
    *buf++ = 2;
    *buf++ = 2;
    *buf++ = 6;
    *buf++ = 32;
    *buf++ = 0;
    *buf++ = 3;

    bytestream_put_buffer(&buf, QMAT_LUMA[avctx->profile],   64);
    bytestream_put_buffer(&buf, QMAT_CHROMA[avctx->profile], 64);

    pkt->flags |= AV_PKT_FLAG_KEY;
    pkt->size = pic_size + 8 + header_size;
    *got_packet = 1;

    return 0;
}

static void scale_mat(const uint8_t* src, int* dst, int scale)
{
    int i;
    for (i = 0; i < 64; i++)
        dst[i] = src[i] * scale;
}

static av_cold int prores_encode_init(AVCodecContext *avctx)
{
    int i;
    ProresContext* ctx = avctx->priv_data;

    if (avctx->pix_fmt != AV_PIX_FMT_YUV422P10) {
        av_log(avctx, AV_LOG_ERROR, "need YUV422P10\n");
        return AVERROR_PATCHWELCOME;
    }
    avctx->bits_per_raw_sample = 10;

    if (avctx->width & 0x1) {
        av_log(avctx, AV_LOG_ERROR,
                "frame width needs to be multiple of 2\n");
        return AVERROR(EINVAL);
    }

    if (avctx->width > 65534 || avctx->height > 65535) {
        av_log(avctx, AV_LOG_ERROR,
                "The maximum dimensions are 65534x65535\n");
        return AVERROR(EINVAL);
    }

    if ((avctx->height & 0xf) || (avctx->width & 0xf)) {
        ctx->fill_y = av_malloc(4 * (DEFAULT_SLICE_MB_WIDTH << 8));
        if (!ctx->fill_y)
            return AVERROR(ENOMEM);
        ctx->fill_u = ctx->fill_y + (DEFAULT_SLICE_MB_WIDTH << 9);
        ctx->fill_v = ctx->fill_u + (DEFAULT_SLICE_MB_WIDTH << 8);
    }

    if (avctx->profile == FF_PROFILE_UNKNOWN) {
        avctx->profile = FF_PROFILE_PRORES_STANDARD;
        av_log(avctx, AV_LOG_INFO,
                "encoding with ProRes standard (apcn) profile\n");

    } else if (avctx->profile < FF_PROFILE_PRORES_PROXY
            || avctx->profile > FF_PROFILE_PRORES_HQ) {
        av_log(
                avctx,
                AV_LOG_ERROR,
                "unknown profile %d, use [0 - apco, 1 - apcs, 2 - apcn (default), 3 - apch]\n",
                avctx->profile);
        return AVERROR(EINVAL);
    }

    ff_fdctdsp_init(&ctx->fdsp, avctx);

    avctx->codec_tag = AV_RL32((const uint8_t*)profiles[avctx->profile].name);

    for (i = 1; i <= 16; i++) {
        scale_mat(QMAT_LUMA[avctx->profile]  , ctx->qmat_luma[i - 1]  , i);
        scale_mat(QMAT_CHROMA[avctx->profile], ctx->qmat_chroma[i - 1], i);
    }

    avctx->coded_frame = av_frame_alloc();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);
    avctx->coded_frame->key_frame = 1;
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;

    return 0;
}

static av_cold int prores_encode_close(AVCodecContext *avctx)
{
    ProresContext* ctx = avctx->priv_data;
    av_frame_free(&avctx->coded_frame);
    av_freep(&ctx->fill_y);

    return 0;
}

AVCodec ff_prores_aw_encoder = {
    .name           = "prores_aw",
    .long_name      = NULL_IF_CONFIG_SMALL("Apple ProRes"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PRORES,
    .priv_data_size = sizeof(ProresContext),
    .init           = prores_encode_init,
    .close          = prores_encode_close,
    .encode2        = prores_encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]){AV_PIX_FMT_YUV422P10, AV_PIX_FMT_NONE},
    .capabilities   = CODEC_CAP_FRAME_THREADS | CODEC_CAP_INTRA_ONLY,
    .profiles       = profiles
};

AVCodec ff_prores_encoder = {
    .name           = "prores",
    .long_name      = NULL_IF_CONFIG_SMALL("Apple ProRes"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PRORES,
    .priv_data_size = sizeof(ProresContext),
    .init           = prores_encode_init,
    .close          = prores_encode_close,
    .encode2        = prores_encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]){AV_PIX_FMT_YUV422P10, AV_PIX_FMT_NONE},
    .capabilities   = CODEC_CAP_FRAME_THREADS | CODEC_CAP_INTRA_ONLY,
    .profiles       = profiles
};
