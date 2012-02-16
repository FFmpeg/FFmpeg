/*
 * Copyright (c) 2010-2011 Maxim Poliakovski
 * Copyright (c) 2010-2011 Elvis Presley
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
 * Known FOURCCs: 'apch' (HQ), 'apcn' (SD), 'apcs' (LT), 'acpo' (Proxy), 'ap4h' (4444)
 */

//#define DEBUG

#define LONG_BITSTREAM_READER

#include "avcodec.h"
#include "get_bits.h"
#include "simple_idct.h"
#include "proresdec.h"

static void permute(uint8_t *dst, const uint8_t *src, const uint8_t permutation[64])
{
    int i;
    for (i = 0; i < 64; i++)
        dst[i] = permutation[src[i]];
}

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
    46, 39, 47, 54, 61, 62, 55, 63,
};

static av_cold int decode_init(AVCodecContext *avctx)
{
    ProresContext *ctx = avctx->priv_data;
    uint8_t idct_permutation[64];

    avctx->bits_per_raw_sample = 10;

    ff_dsputil_init(&ctx->dsp, avctx);
    ff_proresdsp_init(&ctx->prodsp, avctx);

    avctx->coded_frame = &ctx->frame;
    ctx->frame.type = AV_PICTURE_TYPE_I;
    ctx->frame.key_frame = 1;

    ff_init_scantable_permutation(idct_permutation,
                                  ctx->prodsp.idct_permutation_type);

    permute(ctx->progressive_scan, progressive_scan, idct_permutation);
    permute(ctx->interlaced_scan, interlaced_scan, idct_permutation);

    return 0;
}

static int decode_frame_header(ProresContext *ctx, const uint8_t *buf,
                               const int data_size, AVCodecContext *avctx)
{
    int hdr_size, width, height, flags;
    int version;
    const uint8_t *ptr;

    hdr_size = AV_RB16(buf);
    av_dlog(avctx, "header size %d\n", hdr_size);
    if (hdr_size > data_size) {
        av_log(avctx, AV_LOG_ERROR, "error, wrong header size\n");
        return -1;
    }

    version = AV_RB16(buf + 2);
    av_dlog(avctx, "%.4s version %d\n", buf+4, version);
    if (version > 1) {
        av_log(avctx, AV_LOG_ERROR, "unsupported version: %d\n", version);
        return -1;
    }

    width  = AV_RB16(buf + 8);
    height = AV_RB16(buf + 10);
    if (width != avctx->width || height != avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "picture resolution change: %dx%d -> %dx%d\n",
               avctx->width, avctx->height, width, height);
        return -1;
    }

    ctx->frame_type = (buf[12] >> 2) & 3;

    av_dlog(avctx, "frame type %d\n", ctx->frame_type);

    if (ctx->frame_type == 0) {
        ctx->scan = ctx->progressive_scan; // permuted
    } else {
        ctx->scan = ctx->interlaced_scan; // permuted
        ctx->frame.interlaced_frame = 1;
        ctx->frame.top_field_first = ctx->frame_type == 1;
    }

    avctx->pix_fmt = (buf[12] & 0xC0) == 0xC0 ? PIX_FMT_YUV444P10 : PIX_FMT_YUV422P10;

    ptr   = buf + 20;
    flags = buf[19];
    av_dlog(avctx, "flags %x\n", flags);

    if (flags & 2) {
        permute(ctx->qmat_luma, ctx->prodsp.idct_permutation, ptr);
        ptr += 64;
    } else {
        memset(ctx->qmat_luma, 4, 64);
    }

    if (flags & 1) {
        permute(ctx->qmat_chroma, ctx->prodsp.idct_permutation, ptr);
    } else {
        memset(ctx->qmat_chroma, 4, 64);
    }

    return hdr_size;
}

static int decode_picture_header(AVCodecContext *avctx, const uint8_t *buf, const int buf_size)
{
    ProresContext *ctx = avctx->priv_data;
    int i, hdr_size, slice_count;
    unsigned pic_data_size;
    int log2_slice_mb_width, log2_slice_mb_height;
    int slice_mb_count, mb_x, mb_y;
    const uint8_t *data_ptr, *index_ptr;

    hdr_size = buf[0] >> 3;
    if (hdr_size < 8 || hdr_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "error, wrong picture header size\n");
        return -1;
    }

    pic_data_size = AV_RB32(buf + 1);
    if (pic_data_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "error, wrong picture data size\n");
        return -1;
    }

    log2_slice_mb_width  = buf[7] >> 4;
    log2_slice_mb_height = buf[7] & 0xF;
    if (log2_slice_mb_width > 3 || log2_slice_mb_height) {
        av_log(avctx, AV_LOG_ERROR, "unsupported slice resolution: %dx%d\n",
               1 << log2_slice_mb_width, 1 << log2_slice_mb_height);
        return -1;
    }

    ctx->mb_width  = (avctx->width  + 15) >> 4;
    if (ctx->frame_type)
        ctx->mb_height = (avctx->height + 31) >> 5;
    else
        ctx->mb_height = (avctx->height + 15) >> 4;

    slice_count = AV_RB16(buf + 5);

    if (ctx->slice_count != slice_count || !ctx->slices) {
        av_freep(&ctx->slices);
        ctx->slices = av_mallocz(slice_count * sizeof(*ctx->slices));
        if (!ctx->slices)
            return AVERROR(ENOMEM);
        ctx->slice_count = slice_count;
    }

    if (!slice_count)
        return AVERROR(EINVAL);

    if (hdr_size + slice_count*2 > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "error, wrong slice count\n");
        return -1;
    }

    // parse slice information
    index_ptr = buf + hdr_size;
    data_ptr  = index_ptr + slice_count*2;

    slice_mb_count = 1 << log2_slice_mb_width;
    mb_x = 0;
    mb_y = 0;

    for (i = 0; i < slice_count; i++) {
        SliceContext *slice = &ctx->slices[i];

        slice->data = data_ptr;
        data_ptr += AV_RB16(index_ptr + i*2);

        while (ctx->mb_width - mb_x < slice_mb_count)
            slice_mb_count >>= 1;

        slice->mb_x = mb_x;
        slice->mb_y = mb_y;
        slice->mb_count = slice_mb_count;
        slice->data_size = data_ptr - slice->data;

        if (slice->data_size < 6) {
            av_log(avctx, AV_LOG_ERROR, "error, wrong slice data size\n");
            return -1;
        }

        mb_x += slice_mb_count;
        if (mb_x == ctx->mb_width) {
            slice_mb_count = 1 << log2_slice_mb_width;
            mb_x = 0;
            mb_y++;
        }
        if (data_ptr > buf + buf_size) {
            av_log(avctx, AV_LOG_ERROR, "error, slice out of bounds\n");
            return -1;
        }
    }

    if (mb_x || mb_y != ctx->mb_height) {
        av_log(avctx, AV_LOG_ERROR, "error wrong mb count y %d h %d\n",
               mb_y, ctx->mb_height);
        return -1;
    }

    return pic_data_size;
}

#define DECODE_CODEWORD(val, codebook)                                  \
    do {                                                                \
        unsigned int rice_order, exp_order, switch_bits;                \
        unsigned int q, buf, bits;                                      \
                                                                        \
        UPDATE_CACHE(re, gb);                                           \
        buf = GET_CACHE(re, gb);                                        \
                                                                        \
        /* number of bits to switch between rice and exp golomb */      \
        switch_bits =  codebook & 3;                                    \
        rice_order  =  codebook >> 5;                                   \
        exp_order   = (codebook >> 2) & 7;                              \
                                                                        \
        q = 31 - av_log2(buf);                                          \
                                                                        \
        if (q > switch_bits) { /* exp golomb */                         \
            bits = exp_order - switch_bits + (q<<1);                    \
            val = SHOW_UBITS(re, gb, bits) - (1 << exp_order) +         \
                ((switch_bits + 1) << rice_order);                      \
            SKIP_BITS(re, gb, bits);                                    \
        } else if (rice_order) {                                        \
            SKIP_BITS(re, gb, q+1);                                     \
            val = (q << rice_order) + SHOW_UBITS(re, gb, rice_order);   \
            SKIP_BITS(re, gb, rice_order);                              \
        } else {                                                        \
            val = q;                                                    \
            SKIP_BITS(re, gb, q+1);                                     \
        }                                                               \
    } while (0)

#define TOSIGNED(x) (((x) >> 1) ^ (-((x) & 1)))

#define FIRST_DC_CB 0xB8

static const uint8_t dc_codebook[7] = { 0x04, 0x28, 0x28, 0x4D, 0x4D, 0x70, 0x70};

static av_always_inline void decode_dc_coeffs(GetBitContext *gb, DCTELEM *out,
                                              int blocks_per_slice)
{
    DCTELEM prev_dc;
    int code, i, sign;

    OPEN_READER(re, gb);

    DECODE_CODEWORD(code, FIRST_DC_CB);
    prev_dc = TOSIGNED(code);
    out[0] = prev_dc;

    out += 64; // dc coeff for the next block

    code = 5;
    sign = 0;
    for (i = 1; i < blocks_per_slice; i++, out += 64) {
        DECODE_CODEWORD(code, dc_codebook[FFMIN(code, 6U)]);
        if(code) sign ^= -(code & 1);
        else     sign  = 0;
        prev_dc += (((code + 1) >> 1) ^ sign) - sign;
        out[0] = prev_dc;
    }
    CLOSE_READER(re, gb);
}

// adaptive codebook switching lut according to previous run/level values
static const uint8_t run_to_cb[16] = { 0x06, 0x06, 0x05, 0x05, 0x04, 0x29, 0x29, 0x29, 0x29, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x4C };
static const uint8_t lev_to_cb[10] = { 0x04, 0x0A, 0x05, 0x06, 0x04, 0x28, 0x28, 0x28, 0x28, 0x4C };

static av_always_inline void decode_ac_coeffs(AVCodecContext *avctx, GetBitContext *gb,
                                              DCTELEM *out, int blocks_per_slice)
{
    ProresContext *ctx = avctx->priv_data;
    int block_mask, sign;
    unsigned pos, run, level;
    int max_coeffs, i, bits_left;
    int log2_block_count = av_log2(blocks_per_slice);

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);                                           \
    run   = 4;
    level = 2;

    max_coeffs = 64 << log2_block_count;
    block_mask = blocks_per_slice - 1;

    for (pos = block_mask;;) {
        bits_left = gb->size_in_bits - re_index;
        if (!bits_left || (bits_left < 32 && !SHOW_UBITS(re, gb, bits_left)))
            break;

        DECODE_CODEWORD(run, run_to_cb[FFMIN(run,  15)]);
        pos += run + 1;
        if (pos >= max_coeffs) {
            av_log(avctx, AV_LOG_ERROR, "ac tex damaged %d, %d\n", pos, max_coeffs);
            return;
        }

        DECODE_CODEWORD(level, lev_to_cb[FFMIN(level, 9)]);
        level += 1;

        i = pos >> log2_block_count;

        sign = SHOW_SBITS(re, gb, 1);
        SKIP_BITS(re, gb, 1);
        out[((pos & block_mask) << 6) + ctx->scan[i]] = ((level ^ sign) - sign);
    }

    CLOSE_READER(re, gb);
}

static void decode_slice_luma(AVCodecContext *avctx, SliceContext *slice,
                              uint16_t *dst, int dst_stride,
                              const uint8_t *buf, unsigned buf_size,
                              const int16_t *qmat)
{
    ProresContext *ctx = avctx->priv_data;
    LOCAL_ALIGNED_16(DCTELEM, blocks, [8*4*64]);
    DCTELEM *block;
    GetBitContext gb;
    int i, blocks_per_slice = slice->mb_count<<2;

    for (i = 0; i < blocks_per_slice; i++)
        ctx->dsp.clear_block(blocks+(i<<6));

    init_get_bits(&gb, buf, buf_size << 3);

    decode_dc_coeffs(&gb, blocks, blocks_per_slice);
    decode_ac_coeffs(avctx, &gb, blocks, blocks_per_slice);

    block = blocks;
    for (i = 0; i < slice->mb_count; i++) {
        ctx->prodsp.idct_put(dst, dst_stride, block+(0<<6), qmat);
        ctx->prodsp.idct_put(dst             +8, dst_stride, block+(1<<6), qmat);
        ctx->prodsp.idct_put(dst+4*dst_stride  , dst_stride, block+(2<<6), qmat);
        ctx->prodsp.idct_put(dst+4*dst_stride+8, dst_stride, block+(3<<6), qmat);
        block += 4*64;
        dst += 16;
    }
}

static void decode_slice_chroma(AVCodecContext *avctx, SliceContext *slice,
                                uint16_t *dst, int dst_stride,
                                const uint8_t *buf, unsigned buf_size,
                                const int16_t *qmat, int log2_blocks_per_mb)
{
    ProresContext *ctx = avctx->priv_data;
    LOCAL_ALIGNED_16(DCTELEM, blocks, [8*4*64]);
    DCTELEM *block;
    GetBitContext gb;
    int i, j, blocks_per_slice = slice->mb_count << log2_blocks_per_mb;

    for (i = 0; i < blocks_per_slice; i++)
        ctx->dsp.clear_block(blocks+(i<<6));

    init_get_bits(&gb, buf, buf_size << 3);

    decode_dc_coeffs(&gb, blocks, blocks_per_slice);
    decode_ac_coeffs(avctx, &gb, blocks, blocks_per_slice);

    block = blocks;
    for (i = 0; i < slice->mb_count; i++) {
        for (j = 0; j < log2_blocks_per_mb; j++) {
            ctx->prodsp.idct_put(dst,              dst_stride, block+(0<<6), qmat);
            ctx->prodsp.idct_put(dst+4*dst_stride, dst_stride, block+(1<<6), qmat);
            block += 2*64;
            dst += 8;
        }
    }
}

static int decode_slice_thread(AVCodecContext *avctx, void *arg, int jobnr, int threadnr)
{
    ProresContext *ctx = avctx->priv_data;
    SliceContext *slice = &ctx->slices[jobnr];
    const uint8_t *buf = slice->data;
    AVFrame *pic = avctx->coded_frame;
    int i, hdr_size, qscale, log2_chroma_blocks_per_mb;
    int luma_stride, chroma_stride;
    int y_data_size, u_data_size, v_data_size;
    uint8_t *dest_y, *dest_u, *dest_v;
    int16_t qmat_luma_scaled[64];
    int16_t qmat_chroma_scaled[64];
    int mb_x_shift;

    //av_log(avctx, AV_LOG_INFO, "slice %d mb width %d mb x %d y %d\n",
    //       jobnr, slice->mb_count, slice->mb_x, slice->mb_y);

    // slice header
    hdr_size = buf[0] >> 3;
    qscale = av_clip(buf[1], 1, 224);
    qscale = qscale > 128 ? qscale - 96 << 2: qscale;
    y_data_size = AV_RB16(buf + 2);
    u_data_size = AV_RB16(buf + 4);
    v_data_size = slice->data_size - y_data_size - u_data_size - hdr_size;
    if (hdr_size > 7) v_data_size = AV_RB16(buf + 6);

    if (y_data_size < 0 || u_data_size < 0 || v_data_size < 0
        || hdr_size+y_data_size+u_data_size+v_data_size > slice->data_size){
        av_log(avctx, AV_LOG_ERROR, "invalid plane data size\n");
        return -1;
    }

    buf += hdr_size;

    for (i = 0; i < 64; i++) {
        qmat_luma_scaled  [i] = ctx->qmat_luma  [i] * qscale;
        qmat_chroma_scaled[i] = ctx->qmat_chroma[i] * qscale;
    }

    if (ctx->frame_type == 0) {
        luma_stride   = pic->linesize[0];
        chroma_stride = pic->linesize[1];
    } else {
        luma_stride   = pic->linesize[0] << 1;
        chroma_stride = pic->linesize[1] << 1;
    }

    if (avctx->pix_fmt == PIX_FMT_YUV444P10) {
        mb_x_shift = 5;
        log2_chroma_blocks_per_mb = 2;
    } else {
        mb_x_shift = 4;
        log2_chroma_blocks_per_mb = 1;
    }

    dest_y = pic->data[0] + (slice->mb_y << 4) * luma_stride + (slice->mb_x << 5);
    dest_u = pic->data[1] + (slice->mb_y << 4) * chroma_stride + (slice->mb_x << mb_x_shift);
    dest_v = pic->data[2] + (slice->mb_y << 4) * chroma_stride + (slice->mb_x << mb_x_shift);

    if (ctx->frame_type && ctx->first_field ^ ctx->frame.top_field_first) {
        dest_y += pic->linesize[0];
        dest_u += pic->linesize[1];
        dest_v += pic->linesize[2];
    }

    decode_slice_luma(avctx, slice, (uint16_t*)dest_y, luma_stride,
                      buf, y_data_size, qmat_luma_scaled);

    if (!(avctx->flags & CODEC_FLAG_GRAY)) {
        decode_slice_chroma(avctx, slice, (uint16_t*)dest_u, chroma_stride,
                            buf + y_data_size, u_data_size,
                            qmat_chroma_scaled, log2_chroma_blocks_per_mb);
        decode_slice_chroma(avctx, slice, (uint16_t*)dest_v, chroma_stride,
                            buf + y_data_size + u_data_size, v_data_size,
                            qmat_chroma_scaled, log2_chroma_blocks_per_mb);
    }

    return 0;
}

static int decode_picture(AVCodecContext *avctx)
{
    ProresContext *ctx = avctx->priv_data;
    int i, threads_ret[ctx->slice_count];

    avctx->execute2(avctx, decode_slice_thread, NULL, threads_ret, ctx->slice_count);

    for (i = 0; i < ctx->slice_count; i++)
        if (threads_ret[i] < 0)
            return threads_ret[i];

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                        AVPacket *avpkt)
{
    ProresContext *ctx = avctx->priv_data;
    AVFrame *frame = avctx->coded_frame;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int frame_hdr_size, pic_size;

    if (buf_size < 28 || AV_RL32(buf + 4) != AV_RL32("icpf")) {
        av_log(avctx, AV_LOG_ERROR, "invalid frame header\n");
        return -1;
    }

    ctx->first_field = 1;

    buf += 8;
    buf_size -= 8;

    frame_hdr_size = decode_frame_header(ctx, buf, buf_size, avctx);
    if (frame_hdr_size < 0)
        return -1;

    buf += frame_hdr_size;
    buf_size -= frame_hdr_size;

    if (frame->data[0])
        avctx->release_buffer(avctx, frame);

    if (avctx->get_buffer(avctx, frame) < 0)
        return -1;

 decode_picture:
    pic_size = decode_picture_header(avctx, buf, buf_size);
    if (pic_size < 0) {
        av_log(avctx, AV_LOG_ERROR, "error decoding picture header\n");
        return -1;
    }

    if (decode_picture(avctx)) {
        av_log(avctx, AV_LOG_ERROR, "error decoding picture\n");
        return -1;
    }

    buf += pic_size;
    buf_size -= pic_size;

    if (ctx->frame_type && buf_size > 0 && ctx->first_field) {
        ctx->first_field = 0;
        goto decode_picture;
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = *frame;

    return avpkt->size;
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    ProresContext *ctx = avctx->priv_data;

    AVFrame *frame = avctx->coded_frame;
    if (frame->data[0])
        avctx->release_buffer(avctx, frame);
    av_freep(&ctx->slices);

    return 0;
}

AVCodec ff_prores_decoder = {
    .name           = "prores",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PRORES,
    .priv_data_size = sizeof(ProresContext),
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .long_name      = NULL_IF_CONFIG_SMALL("ProRes"),
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_SLICE_THREADS,
};
