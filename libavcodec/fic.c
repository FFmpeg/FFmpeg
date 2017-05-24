/*
 * Mirillis FIC decoder
 *
 * Copyright (c) 2014 Konstantin Shishkov
 * Copyright (c) 2014 Derek Buitenhuis
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
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "golomb.h"

typedef struct FICThreadContext {
    DECLARE_ALIGNED(16, int16_t, block)[64];
    uint8_t *src;
    int slice_h;
    int src_size;
    int y_off;
    int p_frame;
} FICThreadContext;

typedef struct FICContext {
    AVClass *class;
    AVCodecContext *avctx;
    AVFrame *frame;
    AVFrame *final_frame;

    FICThreadContext *slice_data;
    int slice_data_size;

    const uint8_t *qmat;

    enum AVPictureType cur_frame_type;

    int aligned_width, aligned_height;
    int num_slices, slice_h;

    uint8_t cursor_buf[4096];
    int skip_cursor;
} FICContext;

static const uint8_t fic_qmat_hq[64] = {
    1, 2, 2, 2, 3, 3, 3, 4,
    2, 2, 2, 3, 3, 3, 4, 4,
    2, 2, 3, 3, 3, 4, 4, 4,
    2, 2, 3, 3, 3, 4, 4, 5,
    2, 3, 3, 3, 4, 4, 5, 6,
    3, 3, 3, 4, 4, 5, 6, 7,
    3, 3, 3, 4, 4, 5, 7, 7,
    3, 3, 4, 4, 5, 7, 7, 7,
};

static const uint8_t fic_qmat_lq[64] = {
    1,  5,  6,  7,  8,  9,  9, 11,
    5,  5,  7,  8,  9,  9, 11, 12,
    6,  7,  8,  9,  9, 11, 11, 12,
    7,  7,  8,  9,  9, 11, 12, 13,
    7,  8,  9,  9, 10, 11, 13, 16,
    8,  9,  9, 10, 11, 13, 16, 19,
    8,  9,  9, 11, 12, 15, 18, 23,
    9,  9, 11, 12, 15, 18, 23, 27
};

static const uint8_t fic_header[7] = { 0, 0, 1, 'F', 'I', 'C', 'V' };

#define FIC_HEADER_SIZE 27

static av_always_inline void fic_idct(int16_t *blk, int step, int shift, int rnd)
{
    const int t0 =  27246 * blk[3 * step] + 18405 * blk[5 * step];
    const int t1 =  27246 * blk[5 * step] - 18405 * blk[3 * step];
    const int t2 =   6393 * blk[7 * step] + 32139 * blk[1 * step];
    const int t3 =   6393 * blk[1 * step] - 32139 * blk[7 * step];
    const unsigned t4 = 5793U * (t2 + t0 + 0x800 >> 12);
    const unsigned t5 = 5793U * (t3 + t1 + 0x800 >> 12);
    const unsigned t6 = t2 - t0;
    const unsigned t7 = t3 - t1;
    const unsigned t8 =  17734 * blk[2 * step] - 42813 * blk[6 * step];
    const unsigned t9 =  17734 * blk[6 * step] + 42814 * blk[2 * step];
    const unsigned tA = (blk[0 * step] - blk[4 * step]) * 32768 + rnd;
    const unsigned tB = (blk[0 * step] + blk[4 * step]) * 32768 + rnd;
    blk[0 * step] = (int)(  t4       + t9 + tB) >> shift;
    blk[1 * step] = (int)(  t6 + t7  + t8 + tA) >> shift;
    blk[2 * step] = (int)(  t6 - t7  - t8 + tA) >> shift;
    blk[3 * step] = (int)(  t5       - t9 + tB) >> shift;
    blk[4 * step] = (int)( -t5       - t9 + tB) >> shift;
    blk[5 * step] = (int)(-(t6 - t7) - t8 + tA) >> shift;
    blk[6 * step] = (int)(-(t6 + t7) + t8 + tA) >> shift;
    blk[7 * step] = (int)( -t4       + t9 + tB) >> shift;
}

static void fic_idct_put(uint8_t *dst, int stride, int16_t *block)
{
    int i, j;
    int16_t *ptr;

    ptr = block;
    fic_idct(ptr++, 8, 13, (1 << 12) + (1 << 17));
    for (i = 1; i < 8; i++) {
        fic_idct(ptr, 8, 13, 1 << 12);
        ptr++;
    }

    ptr = block;
    for (i = 0; i < 8; i++) {
        fic_idct(ptr, 1, 20, 0);
        ptr += 8;
    }

    ptr = block;
    for (j = 0; j < 8; j++) {
        for (i = 0; i < 8; i++)
            dst[i] = av_clip_uint8(ptr[i]);
        dst += stride;
        ptr += 8;
    }
}
static int fic_decode_block(FICContext *ctx, GetBitContext *gb,
                            uint8_t *dst, int stride, int16_t *block, int *is_p)
{
    int i, num_coeff;

    /* Is it a skip block? */
    if (get_bits1(gb)) {
        *is_p = 1;
        return 0;
    }

    memset(block, 0, sizeof(*block) * 64);

    num_coeff = get_bits(gb, 7);
    if (num_coeff > 64)
        return AVERROR_INVALIDDATA;

    for (i = 0; i < num_coeff; i++) {
        int v = get_se_golomb(gb);
        if (v < -2048 || v > 2048)
             return AVERROR_INVALIDDATA;
        block[ff_zigzag_direct[i]] = v *
                                     ctx->qmat[ff_zigzag_direct[i]];
    }

    fic_idct_put(dst, stride, block);

    return 0;
}

static int fic_decode_slice(AVCodecContext *avctx, void *tdata)
{
    FICContext *ctx        = avctx->priv_data;
    FICThreadContext *tctx = tdata;
    GetBitContext gb;
    uint8_t *src = tctx->src;
    int slice_h  = tctx->slice_h;
    int src_size = tctx->src_size;
    int y_off    = tctx->y_off;
    int x, y, p;

    init_get_bits(&gb, src, src_size * 8);

    for (p = 0; p < 3; p++) {
        int stride   = ctx->frame->linesize[p];
        uint8_t* dst = ctx->frame->data[p] + (y_off >> !!p) * stride;

        for (y = 0; y < (slice_h >> !!p); y += 8) {
            for (x = 0; x < (ctx->aligned_width >> !!p); x += 8) {
                int ret;

                if ((ret = fic_decode_block(ctx, &gb, dst + x, stride,
                                            tctx->block, &tctx->p_frame)) != 0)
                    return ret;
            }

            dst += 8 * stride;
        }
    }

    return 0;
}

static av_always_inline void fic_alpha_blend(uint8_t *dst, uint8_t *src,
                                             int size, uint8_t *alpha)
{
    int i;

    for (i = 0; i < size; i++)
        dst[i] += ((src[i] - dst[i]) * alpha[i]) >> 8;
}

static void fic_draw_cursor(AVCodecContext *avctx, int cur_x, int cur_y)
{
    FICContext *ctx = avctx->priv_data;
    uint8_t *ptr    = ctx->cursor_buf;
    uint8_t *dstptr[3];
    uint8_t planes[4][1024];
    uint8_t chroma[3][256];
    int i, j, p;

    /* Convert to YUVA444. */
    for (i = 0; i < 1024; i++) {
        planes[0][i] = (( 25 * ptr[0] + 129 * ptr[1] +  66 * ptr[2]) / 255) + 16;
        planes[1][i] = ((-38 * ptr[0] + 112 * ptr[1] + -74 * ptr[2]) / 255) + 128;
        planes[2][i] = ((-18 * ptr[0] + 112 * ptr[1] + -94 * ptr[2]) / 255) + 128;
        planes[3][i] = ptr[3];

        ptr += 4;
    }

    /* Subsample chroma. */
    for (i = 0; i < 32; i += 2)
        for (j = 0; j < 32; j += 2)
            for (p = 0; p < 3; p++)
                chroma[p][16 * (i / 2) + j / 2] = (planes[p + 1][32 *  i      + j    ] +
                                                   planes[p + 1][32 *  i      + j + 1] +
                                                   planes[p + 1][32 * (i + 1) + j    ] +
                                                   planes[p + 1][32 * (i + 1) + j + 1]) / 4;

    /* Seek to x/y pos of cursor. */
    for (i = 0; i < 3; i++)
        dstptr[i] = ctx->final_frame->data[i]                        +
                    (ctx->final_frame->linesize[i] * (cur_y >> !!i)) +
                    (cur_x >> !!i) + !!i;

    /* Copy. */
    for (i = 0; i < FFMIN(32, avctx->height - cur_y) - 1; i += 2) {
        int lsize = FFMIN(32, avctx->width - cur_x);
        int csize = lsize / 2;

        fic_alpha_blend(dstptr[0],
                        planes[0] + i * 32, lsize, planes[3] + i * 32);
        fic_alpha_blend(dstptr[0] + ctx->final_frame->linesize[0],
                        planes[0] + (i + 1) * 32, lsize, planes[3] + (i + 1) * 32);
        fic_alpha_blend(dstptr[1],
                        chroma[0] + (i / 2) * 16, csize, chroma[2] + (i / 2) * 16);
        fic_alpha_blend(dstptr[2],
                        chroma[1] + (i / 2) * 16, csize, chroma[2] + (i / 2) * 16);

        dstptr[0] += ctx->final_frame->linesize[0] * 2;
        dstptr[1] += ctx->final_frame->linesize[1];
        dstptr[2] += ctx->final_frame->linesize[2];
    }
}

static int fic_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    FICContext *ctx = avctx->priv_data;
    uint8_t *src = avpkt->data;
    int ret;
    int slice, nslices;
    int msize;
    int tsize;
    int cur_x, cur_y;
    int skip_cursor = ctx->skip_cursor;
    uint8_t *sdata;

    if ((ret = ff_reget_buffer(avctx, ctx->frame)) < 0)
        return ret;

    /* Header + at least one slice (4) */
    if (avpkt->size < FIC_HEADER_SIZE + 4) {
        av_log(avctx, AV_LOG_ERROR, "Frame data is too small.\n");
        return AVERROR_INVALIDDATA;
    }

    /* Check for header. */
    if (memcmp(src, fic_header, 7))
        av_log(avctx, AV_LOG_WARNING, "Invalid FIC Header.\n");

    /* Is it a skip frame? */
    if (src[17]) {
        if (!ctx->final_frame) {
            av_log(avctx, AV_LOG_WARNING, "Initial frame is skipped\n");
            return AVERROR_INVALIDDATA;
        }
        goto skip;
    }

    nslices = src[13];
    if (!nslices) {
        av_log(avctx, AV_LOG_ERROR, "Zero slices found.\n");
        return AVERROR_INVALIDDATA;
    }

    /* High or Low Quality Matrix? */
    ctx->qmat = src[23] ? fic_qmat_hq : fic_qmat_lq;

    /* Skip cursor data. */
    tsize = AV_RB24(src + 24);
    if (tsize > avpkt->size - FIC_HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR,
               "Packet is too small to contain cursor (%d vs %d bytes).\n",
               tsize, avpkt->size - FIC_HEADER_SIZE);
        return AVERROR_INVALIDDATA;
    }

    if (!tsize || !AV_RL16(src + 37) || !AV_RL16(src + 39))
        skip_cursor = 1;

    if (!skip_cursor && tsize < 32) {
        av_log(avctx, AV_LOG_WARNING,
               "Cursor data too small. Skipping cursor.\n");
        skip_cursor = 1;
    }

    /* Cursor position. */
    cur_x = AV_RL16(src + 33);
    cur_y = AV_RL16(src + 35);
    if (!skip_cursor && (cur_x > avctx->width || cur_y > avctx->height)) {
        av_log(avctx, AV_LOG_DEBUG,
               "Invalid cursor position: (%d,%d). Skipping cursor.\n",
               cur_x, cur_y);
        skip_cursor = 1;
    }

    if (!skip_cursor && (AV_RL16(src + 37) != 32 || AV_RL16(src + 39) != 32)) {
        av_log(avctx, AV_LOG_WARNING,
               "Invalid cursor size. Skipping cursor.\n");
        skip_cursor = 1;
    }

    /* Slice height for all but the last slice. */
    ctx->slice_h = 16 * (ctx->aligned_height >> 4) / nslices;
    if (ctx->slice_h % 16)
        ctx->slice_h = FFALIGN(ctx->slice_h - 16, 16);

    /* First slice offset and remaining data. */
    sdata = src + tsize + FIC_HEADER_SIZE + 4 * nslices;
    msize = avpkt->size - nslices * 4 - tsize - FIC_HEADER_SIZE;

    if (msize <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Not enough frame data to decode.\n");
        return AVERROR_INVALIDDATA;
    }

    /* Allocate slice data. */
    av_fast_malloc(&ctx->slice_data, &ctx->slice_data_size,
                   nslices * sizeof(ctx->slice_data[0]));
    if (!ctx->slice_data_size) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate slice data.\n");
        return AVERROR(ENOMEM);
    }
    memset(ctx->slice_data, 0, nslices * sizeof(ctx->slice_data[0]));

    for (slice = 0; slice < nslices; slice++) {
        unsigned slice_off = AV_RB32(src + tsize + FIC_HEADER_SIZE + slice * 4);
        unsigned slice_size;
        int y_off   = ctx->slice_h * slice;
        int slice_h = ctx->slice_h;

        /*
         * Either read the slice size, or consume all data left.
         * Also, special case the last slight height.
         */
        if (slice == nslices - 1) {
            slice_size   = msize;
            slice_h      = FFALIGN(avctx->height - ctx->slice_h * (nslices - 1), 16);
        } else {
            slice_size = AV_RB32(src + tsize + FIC_HEADER_SIZE + slice * 4 + 4);
        }

        if (slice_size < slice_off || slice_size > msize)
            continue;

        slice_size -= slice_off;

        ctx->slice_data[slice].src      = sdata + slice_off;
        ctx->slice_data[slice].src_size = slice_size;
        ctx->slice_data[slice].slice_h  = slice_h;
        ctx->slice_data[slice].y_off    = y_off;
    }

    if ((ret = avctx->execute(avctx, fic_decode_slice, ctx->slice_data,
                              NULL, nslices, sizeof(ctx->slice_data[0]))) < 0)
        return ret;

    ctx->frame->key_frame = 1;
    ctx->frame->pict_type = AV_PICTURE_TYPE_I;
    for (slice = 0; slice < nslices; slice++) {
        if (ctx->slice_data[slice].p_frame) {
            ctx->frame->key_frame = 0;
            ctx->frame->pict_type = AV_PICTURE_TYPE_P;
            break;
        }
    }
    av_frame_free(&ctx->final_frame);
    ctx->final_frame = av_frame_clone(ctx->frame);
    if (!ctx->final_frame) {
        av_log(avctx, AV_LOG_ERROR, "Could not clone frame buffer.\n");
        return AVERROR(ENOMEM);
    }

    /* Make sure we use a user-supplied buffer. */
    if ((ret = ff_reget_buffer(avctx, ctx->final_frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Could not make frame writable.\n");
        return ret;
    }

    /* Draw cursor. */
    if (!skip_cursor) {
        memcpy(ctx->cursor_buf, src + 59, 32 * 32 * 4);
        fic_draw_cursor(avctx, cur_x, cur_y);
    }

skip:
    *got_frame = 1;
    if ((ret = av_frame_ref(data, ctx->final_frame)) < 0)
        return ret;

    return avpkt->size;
}

static av_cold int fic_decode_close(AVCodecContext *avctx)
{
    FICContext *ctx = avctx->priv_data;

    av_freep(&ctx->slice_data);
    av_frame_free(&ctx->final_frame);
    av_frame_free(&ctx->frame);

    return 0;
}

static av_cold int fic_decode_init(AVCodecContext *avctx)
{
    FICContext *ctx = avctx->priv_data;

    /* Initialize various context values */
    ctx->avctx            = avctx;
    ctx->aligned_width    = FFALIGN(avctx->width,  16);
    ctx->aligned_height   = FFALIGN(avctx->height, 16);

    avctx->pix_fmt             = AV_PIX_FMT_YUV420P;
    avctx->bits_per_raw_sample = 8;

    ctx->frame = av_frame_alloc();
    if (!ctx->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static const AVOption options[] = {
{ "skip_cursor", "skip the cursor", offsetof(FICContext, skip_cursor), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM },
{ NULL },
};

static const AVClass fic_decoder_class = {
    .class_name = "FIC encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_fic_decoder = {
    .name           = "fic",
    .long_name      = NULL_IF_CONFIG_SMALL("Mirillis FIC"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FIC,
    .priv_data_size = sizeof(FICContext),
    .init           = fic_decode_init,
    .decode         = fic_decode_frame,
    .close          = fic_decode_close,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_SLICE_THREADS,
    .priv_class     = &fic_decoder_class,
};
