/*
 * Canopus Lossless Codec decoder
 *
 * Copyright (c) 2012-2013 Derek Buitenhuis
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

#include <inttypes.h>

#include "libavutil/intreadwrite.h"
#include "bswapdsp.h"
#include "canopus.h"
#include "get_bits.h"
#include "avcodec.h"
#include "internal.h"

typedef struct CLLCContext {
    AVCodecContext *avctx;
    BswapDSPContext bdsp;

    uint8_t *swapped_buf;
    int      swapped_buf_size;
} CLLCContext;

static int read_code_table(CLLCContext *ctx, GetBitContext *gb, VLC *vlc)
{
    uint8_t symbols[256];
    uint8_t bits[256];
    uint16_t codes[256];
    int num_lens, num_codes, num_codes_sum, prefix;
    int i, j, count;

    prefix        = 0;
    count         = 0;
    num_codes_sum = 0;

    num_lens = get_bits(gb, 5);

    for (i = 0; i < num_lens; i++) {
        num_codes      = get_bits(gb, 9);
        num_codes_sum += num_codes;

        if (num_codes_sum > 256) {
            vlc->table = NULL;

            av_log(ctx->avctx, AV_LOG_ERROR,
                   "Too many VLCs (%d) to be read.\n", num_codes_sum);
            return AVERROR_INVALIDDATA;
        }

        for (j = 0; j < num_codes; j++) {
            symbols[count] = get_bits(gb, 8);
            bits[count]    = i + 1;
            codes[count]   = prefix++;

            count++;
        }

        prefix <<= 1;
    }

    return ff_init_vlc_sparse(vlc, 7, count, bits, 1, 1,
                              codes, 2, 2, symbols, 1, 1, 0);
}

/*
 * Unlike the RGB24 read/restore, which reads in a component at a time,
 * ARGB read/restore reads in ARGB quads.
 */
static int read_argb_line(CLLCContext *ctx, GetBitContext *gb, int *top_left,
                          VLC *vlc, uint8_t *outbuf)
{
    uint8_t *dst;
    int pred[4];
    int code;
    int i;

    OPEN_READER(bits, gb);

    dst     = outbuf;
    pred[0] = top_left[0];
    pred[1] = top_left[1];
    pred[2] = top_left[2];
    pred[3] = top_left[3];

    for (i = 0; i < ctx->avctx->width; i++) {
        /* Always get the alpha component */
        UPDATE_CACHE(bits, gb);
        GET_VLC(code, bits, gb, vlc[0].table, 7, 2);

        pred[0] += code;
        dst[0]   = pred[0];

        /* Skip the components if they are  entirely transparent */
        if (dst[0]) {
            /* Red */
            UPDATE_CACHE(bits, gb);
            GET_VLC(code, bits, gb, vlc[1].table, 7, 2);

            pred[1] += code;
            dst[1]   = pred[1];

            /* Green */
            UPDATE_CACHE(bits, gb);
            GET_VLC(code, bits, gb, vlc[2].table, 7, 2);

            pred[2] += code;
            dst[2]   = pred[2];

            /* Blue */
            UPDATE_CACHE(bits, gb);
            GET_VLC(code, bits, gb, vlc[3].table, 7, 2);

            pred[3] += code;
            dst[3]   = pred[3];
        } else {
            dst[1] = 0;
            dst[2] = 0;
            dst[3] = 0;
        }

        dst += 4;
    }

    CLOSE_READER(bits, gb);

    top_left[0]  = outbuf[0];

    /* Only stash components if they are not transparent */
    if (top_left[0]) {
        top_left[1] = outbuf[1];
        top_left[2] = outbuf[2];
        top_left[3] = outbuf[3];
    }

    return 0;
}

static int read_rgb24_component_line(CLLCContext *ctx, GetBitContext *gb,
                                     int *top_left, VLC *vlc, uint8_t *outbuf)
{
    uint8_t *dst;
    int pred, code;
    int i;

    OPEN_READER(bits, gb);

    dst  = outbuf;
    pred = *top_left;

    /* Simultaneously read and restore the line */
    for (i = 0; i < ctx->avctx->width; i++) {
        UPDATE_CACHE(bits, gb);
        GET_VLC(code, bits, gb, vlc->table, 7, 2);

        pred  += code;
        dst[0] = pred;
        dst   += 3;
    }

    CLOSE_READER(bits, gb);

    /* Stash the first pixel */
    *top_left = outbuf[0];

    return 0;
}

static int read_yuv_component_line(CLLCContext *ctx, GetBitContext *gb,
                                   int *top_left, VLC *vlc, uint8_t *outbuf,
                                   int is_chroma)
{
    int pred, code;
    int i;

    OPEN_READER(bits, gb);

    pred = *top_left;

    /* Simultaneously read and restore the line */
    for (i = 0; i < ctx->avctx->width >> is_chroma; i++) {
        UPDATE_CACHE(bits, gb);
        GET_VLC(code, bits, gb, vlc->table, 7, 2);

        pred     += code;
        outbuf[i] = pred;
    }

    CLOSE_READER(bits, gb);

    /* Stash the first pixel */
    *top_left = outbuf[0];

    return 0;
}

static int decode_argb_frame(CLLCContext *ctx, GetBitContext *gb, AVFrame *pic)
{
    AVCodecContext *avctx = ctx->avctx;
    uint8_t *dst;
    int pred[4];
    int ret;
    int i, j;
    VLC vlc[4];

    pred[0] = 0;
    pred[1] = 0x80;
    pred[2] = 0x80;
    pred[3] = 0x80;

    dst = pic->data[0];

    skip_bits(gb, 16);

    /* Read in code table for each plane */
    for (i = 0; i < 4; i++) {
        ret = read_code_table(ctx, gb, &vlc[i]);
        if (ret < 0) {
            for (j = 0; j <= i; j++)
                ff_free_vlc(&vlc[j]);

            av_log(ctx->avctx, AV_LOG_ERROR,
                   "Could not read code table %d.\n", i);
            return ret;
        }
    }

    /* Read in and restore every line */
    for (i = 0; i < avctx->height; i++) {
        read_argb_line(ctx, gb, pred, vlc, dst);

        dst += pic->linesize[0];
    }

    for (i = 0; i < 4; i++)
        ff_free_vlc(&vlc[i]);

    return 0;
}

static int decode_rgb24_frame(CLLCContext *ctx, GetBitContext *gb, AVFrame *pic)
{
    AVCodecContext *avctx = ctx->avctx;
    uint8_t *dst;
    int pred[3];
    int ret;
    int i, j;
    VLC vlc[3];

    pred[0] = 0x80;
    pred[1] = 0x80;
    pred[2] = 0x80;

    dst = pic->data[0];

    skip_bits(gb, 16);

    /* Read in code table for each plane */
    for (i = 0; i < 3; i++) {
        ret = read_code_table(ctx, gb, &vlc[i]);
        if (ret < 0) {
            for (j = 0; j <= i; j++)
                ff_free_vlc(&vlc[j]);

            av_log(ctx->avctx, AV_LOG_ERROR,
                   "Could not read code table %d.\n", i);
            return ret;
        }
    }

    /* Read in and restore every line */
    for (i = 0; i < avctx->height; i++) {
        for (j = 0; j < 3; j++)
            read_rgb24_component_line(ctx, gb, &pred[j], &vlc[j], &dst[j]);

        dst += pic->linesize[0];
    }

    for (i = 0; i < 3; i++)
        ff_free_vlc(&vlc[i]);

    return 0;
}

static int decode_yuv_frame(CLLCContext *ctx, GetBitContext *gb, AVFrame *pic)
{
    AVCodecContext *avctx = ctx->avctx;
    uint8_t block;
    uint8_t *dst[3];
    int pred[3];
    int ret;
    int i, j;
    VLC vlc[2];

    pred[0] = 0x80;
    pred[1] = 0x80;
    pred[2] = 0x80;

    dst[0] = pic->data[0];
    dst[1] = pic->data[1];
    dst[2] = pic->data[2];

    skip_bits(gb, 8);

    block = get_bits(gb, 8);
    if (block) {
        avpriv_request_sample(ctx->avctx, "Blocked YUV");
        return AVERROR_PATCHWELCOME;
    }

    /* Read in code table for luma and chroma */
    for (i = 0; i < 2; i++) {
        ret = read_code_table(ctx, gb, &vlc[i]);
        if (ret < 0) {
            for (j = 0; j <= i; j++)
                ff_free_vlc(&vlc[j]);

            av_log(ctx->avctx, AV_LOG_ERROR,
                   "Could not read code table %d.\n", i);
            return ret;
        }
    }

    /* Read in and restore every line */
    for (i = 0; i < avctx->height; i++) {
        read_yuv_component_line(ctx, gb, &pred[0], &vlc[0], dst[0], 0); /* Y */
        read_yuv_component_line(ctx, gb, &pred[1], &vlc[1], dst[1], 1); /* U */
        read_yuv_component_line(ctx, gb, &pred[2], &vlc[1], dst[2], 1); /* V */

        for (j = 0; j < 3; j++)
            dst[j] += pic->linesize[j];
    }

    for (i = 0; i < 2; i++)
        ff_free_vlc(&vlc[i]);

    return 0;
}

static int cllc_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_picture_ptr, AVPacket *avpkt)
{
    CLLCContext *ctx = avctx->priv_data;
    AVFrame *pic = data;
    uint8_t *src = avpkt->data;
    uint32_t info_tag, info_offset;
    int data_size;
    GetBitContext gb;
    int coding_type, ret;

    if (avpkt->size < 4 + 4) {
        av_log(avctx, AV_LOG_ERROR, "Frame is too small %d.\n", avpkt->size);
        return AVERROR_INVALIDDATA;
    }

    info_offset = 0;
    info_tag    = AV_RL32(src);
    if (info_tag == MKTAG('I', 'N', 'F', 'O')) {
        info_offset = AV_RL32(src + 4);
        if (info_offset > UINT32_MAX - 8 || info_offset + 8 > avpkt->size) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid INFO header offset: 0x%08"PRIX32" is too large.\n",
                   info_offset);
            return AVERROR_INVALIDDATA;
        }
        ff_canopus_parse_info_tag(avctx, src + 8, info_offset);

        info_offset += 8;
        src         += info_offset;
    }

    data_size = (avpkt->size - info_offset) & ~1;

    /* Make sure our bswap16'd buffer is big enough */
    av_fast_padded_malloc(&ctx->swapped_buf,
                          &ctx->swapped_buf_size, data_size);
    if (!ctx->swapped_buf) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate swapped buffer.\n");
        return AVERROR(ENOMEM);
    }

    /* bswap16 the buffer since CLLC's bitreader works in 16-bit words */
    ctx->bdsp.bswap16_buf((uint16_t *) ctx->swapped_buf, (uint16_t *) src,
                          data_size / 2);

    if ((ret = init_get_bits8(&gb, ctx->swapped_buf, data_size)) < 0)
        return ret;

    /*
     * Read in coding type. The types are as follows:
     *
     * 0 - YUY2
     * 1 - BGR24 (Triples)
     * 2 - BGR24 (Quads)
     * 3 - BGRA
     */
    coding_type = (AV_RL32(src) >> 8) & 0xFF;
    av_log(avctx, AV_LOG_DEBUG, "Frame coding type: %d\n", coding_type);

    switch (coding_type) {
    case 0:
        avctx->pix_fmt             = AV_PIX_FMT_YUV422P;
        avctx->bits_per_raw_sample = 8;

        if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
            return ret;

        ret = decode_yuv_frame(ctx, &gb, pic);
        if (ret < 0)
            return ret;

        break;
    case 1:
    case 2:
        avctx->pix_fmt             = AV_PIX_FMT_RGB24;
        avctx->bits_per_raw_sample = 8;

        if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
            return ret;

        ret = decode_rgb24_frame(ctx, &gb, pic);
        if (ret < 0)
            return ret;

        break;
    case 3:
        avctx->pix_fmt             = AV_PIX_FMT_ARGB;
        avctx->bits_per_raw_sample = 8;

        if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
            return ret;

        ret = decode_argb_frame(ctx, &gb, pic);
        if (ret < 0)
            return ret;

        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown coding type: %d.\n", coding_type);
        return AVERROR_INVALIDDATA;
    }

    pic->key_frame = 1;
    pic->pict_type = AV_PICTURE_TYPE_I;

    *got_picture_ptr = 1;

    return avpkt->size;
}

static av_cold int cllc_decode_close(AVCodecContext *avctx)
{
    CLLCContext *ctx = avctx->priv_data;

    av_freep(&ctx->swapped_buf);

    return 0;
}

static av_cold int cllc_decode_init(AVCodecContext *avctx)
{
    CLLCContext *ctx = avctx->priv_data;

    /* Initialize various context values */
    ctx->avctx            = avctx;
    ctx->swapped_buf      = NULL;
    ctx->swapped_buf_size = 0;

    ff_bswapdsp_init(&ctx->bdsp);

    return 0;
}

AVCodec ff_cllc_decoder = {
    .name           = "cllc",
    .long_name      = NULL_IF_CONFIG_SMALL("Canopus Lossless Codec"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CLLC,
    .priv_data_size = sizeof(CLLCContext),
    .init           = cllc_decode_init,
    .decode         = cllc_decode_frame,
    .close          = cllc_decode_close,
    .capabilities   = AV_CODEC_CAP_DR1,
};
