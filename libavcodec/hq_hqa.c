/*
 * Canopus HQ/HQA decoder
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "canopus.h"
#include "get_bits.h"
#include "internal.h"

#include "hq_hqa.h"
#include "hq_hqadsp.h"

/* HQ/HQA slices are a set of macroblocks belonging to a frame, and
 * they usually form a pseudorandom pattern (probably because it is
 * nicer to display on partial decode).
 *
 * For HQA it just happens that each slice is on every 8th macroblock,
 * but they can be on any frame width like
 *   X.......X.
 *   ......X...
 *   ....X.....
 *   ..X.......
 * etc.
 *
 * The original decoder has special handling for edge macroblocks,
 * while lavc simply aligns coded_width and coded_height.
 */

static inline void put_blocks(HQContext *c, AVFrame *pic,
                              int plane, int x, int y, int ilace,
                              int16_t *block0, int16_t *block1)
{
    uint8_t *p = pic->data[plane] + x;

    c->hqhqadsp.idct_put(p + y * pic->linesize[plane],
                         pic->linesize[plane] << ilace, block0);
    c->hqhqadsp.idct_put(p + (y + (ilace ? 1 : 8)) * pic->linesize[plane],
                         pic->linesize[plane] << ilace, block1);
}

static int hq_decode_block(HQContext *c, GetBitContext *gb, int16_t block[64],
                           int qsel, int is_chroma, int is_hqa)
{
    const int32_t *q;
    int val, pos = 1;

    memset(block, 0, 64 * sizeof(*block));

    if (!is_hqa) {
        block[0] = get_sbits(gb, 9) * 64;
        q = ff_hq_quants[qsel][is_chroma][get_bits(gb, 2)];
    } else {
        q = ff_hq_quants[qsel][is_chroma][get_bits(gb, 2)];
        block[0] = get_sbits(gb, 9) * 64;
    }

    for (;;) {
        val = get_vlc2(gb, c->hq_ac_vlc.table, 9, 2);
        if (val < 0)
            return AVERROR_INVALIDDATA;

        pos += ff_hq_ac_skips[val];
        if (pos >= 64)
            break;
        block[ff_zigzag_direct[pos]] = (int)(ff_hq_ac_syms[val] * (unsigned)q[pos]) >> 12;
        pos++;
    }

    return 0;
}

static int hq_decode_mb(HQContext *c, AVFrame *pic,
                        GetBitContext *gb, int x, int y)
{
    int qgroup, flag;
    int i, ret;

    qgroup = get_bits(gb, 4);
    flag = get_bits1(gb);

    for (i = 0; i < 8; i++) {
        ret = hq_decode_block(c, gb, c->block[i], qgroup, i >= 4, 0);
        if (ret < 0)
            return ret;
    }

    put_blocks(c, pic, 0, x,      y, flag, c->block[0], c->block[2]);
    put_blocks(c, pic, 0, x + 8,  y, flag, c->block[1], c->block[3]);
    put_blocks(c, pic, 2, x >> 1, y, flag, c->block[4], c->block[5]);
    put_blocks(c, pic, 1, x >> 1, y, flag, c->block[6], c->block[7]);

    return 0;
}

static int hq_decode_frame(HQContext *ctx, AVFrame *pic,
                           int prof_num, size_t data_size)
{
    const HQProfile *profile;
    GetBitContext gb;
    const uint8_t *perm, *src = ctx->gbc.buffer;
    uint32_t slice_off[21];
    int slice, start_off, next_off, i, ret;

    if ((unsigned)prof_num >= NUM_HQ_PROFILES) {
        profile = &ff_hq_profile[0];
        avpriv_request_sample(ctx->avctx, "HQ Profile %d", prof_num);
    } else {
        profile = &ff_hq_profile[prof_num];
        av_log(ctx->avctx, AV_LOG_VERBOSE, "HQ Profile %d\n", prof_num);
    }

    ctx->avctx->coded_width         = FFALIGN(profile->width,  16);
    ctx->avctx->coded_height        = FFALIGN(profile->height, 16);
    ctx->avctx->width               = profile->width;
    ctx->avctx->height              = profile->height;
    ctx->avctx->bits_per_raw_sample = 8;
    ctx->avctx->pix_fmt             = AV_PIX_FMT_YUV422P;

    ret = ff_get_buffer(ctx->avctx, pic, 0);
    if (ret < 0)
        return ret;

    /* Offsets are stored from CUV position, so adjust them accordingly. */
    for (i = 0; i < profile->num_slices + 1; i++)
        slice_off[i] = bytestream2_get_be24(&ctx->gbc) - 4;

    next_off = 0;
    for (slice = 0; slice < profile->num_slices; slice++) {
        start_off = next_off;
        next_off  = profile->tab_h * (slice + 1) / profile->num_slices;
        perm = profile->perm_tab + start_off * profile->tab_w * 2;

        if (slice_off[slice] < (profile->num_slices + 1) * 3 ||
            slice_off[slice] >= slice_off[slice + 1] ||
            slice_off[slice + 1] > data_size) {
            av_log(ctx->avctx, AV_LOG_ERROR,
                   "Invalid slice size %"SIZE_SPECIFIER".\n", data_size);
            break;
        }
        init_get_bits(&gb, src + slice_off[slice],
                      (slice_off[slice + 1] - slice_off[slice]) * 8);

        for (i = 0; i < (next_off - start_off) * profile->tab_w; i++) {
            ret = hq_decode_mb(ctx, pic, &gb, perm[0] * 16, perm[1] * 16);
            if (ret < 0) {
                av_log(ctx->avctx, AV_LOG_ERROR,
                       "Error decoding macroblock %d at slice %d.\n", i, slice);
                return ret;
            }
            perm += 2;
        }
    }

    return 0;
}

static int hqa_decode_mb(HQContext *c, AVFrame *pic, int qgroup,
                         GetBitContext *gb, int x, int y)
{
    int flag = 0;
    int i, ret, cbp;

    if (get_bits_left(gb) < 1)
        return AVERROR_INVALIDDATA;

    cbp = get_vlc2(gb, c->hqa_cbp_vlc.table, 5, 1);

    for (i = 0; i < 12; i++)
        memset(c->block[i], 0, sizeof(*c->block));
    for (i = 0; i < 12; i++)
        c->block[i][0] = -128 * (1 << 6);

    if (cbp) {
        flag = get_bits1(gb);

        cbp |= cbp << 4;
        if (cbp & 0x3)
            cbp |= 0x500;
        if (cbp & 0xC)
            cbp |= 0xA00;
        for (i = 0; i < 12; i++) {
            if (!(cbp & (1 << i)))
                continue;
            ret = hq_decode_block(c, gb, c->block[i], qgroup, i >= 8, 1);
            if (ret < 0)
                return ret;
        }
    }

    put_blocks(c, pic, 3, x,      y, flag, c->block[ 0], c->block[ 2]);
    put_blocks(c, pic, 3, x + 8,  y, flag, c->block[ 1], c->block[ 3]);
    put_blocks(c, pic, 0, x,      y, flag, c->block[ 4], c->block[ 6]);
    put_blocks(c, pic, 0, x + 8,  y, flag, c->block[ 5], c->block[ 7]);
    put_blocks(c, pic, 2, x >> 1, y, flag, c->block[ 8], c->block[ 9]);
    put_blocks(c, pic, 1, x >> 1, y, flag, c->block[10], c->block[11]);

    return 0;
}

static int hqa_decode_slice(HQContext *ctx, AVFrame *pic, GetBitContext *gb,
                            int quant, int slice_no, int w, int h)
{
    int i, j, off;
    int ret;

    for (i = 0; i < h; i += 16) {
        off = (slice_no * 16 + i * 3) & 0x70;
        for (j = off; j < w; j += 128) {
            ret = hqa_decode_mb(ctx, pic, quant, gb, j, i);
            if (ret < 0) {
                av_log(ctx->avctx, AV_LOG_ERROR,
                       "Error decoding macroblock at %dx%d.\n", i, j);
                return ret;
            }
        }
    }

    return 0;
}

static int hqa_decode_frame(HQContext *ctx, AVFrame *pic, size_t data_size)
{
    GetBitContext gb;
    const int num_slices = 8;
    uint32_t slice_off[9];
    int i, slice, ret;
    int width, height, quant;
    const uint8_t *src = ctx->gbc.buffer;

    if (bytestream2_get_bytes_left(&ctx->gbc) < 8 + 4*(num_slices + 1))
        return AVERROR_INVALIDDATA;

    width  = bytestream2_get_be16(&ctx->gbc);
    height = bytestream2_get_be16(&ctx->gbc);

    ret = ff_set_dimensions(ctx->avctx, width, height);
    if (ret < 0)
        return ret;

    ctx->avctx->coded_width         = FFALIGN(width,  16);
    ctx->avctx->coded_height        = FFALIGN(height, 16);
    ctx->avctx->bits_per_raw_sample = 8;
    ctx->avctx->pix_fmt             = AV_PIX_FMT_YUVA422P;

    av_log(ctx->avctx, AV_LOG_VERBOSE, "HQA Profile\n");

    quant = bytestream2_get_byte(&ctx->gbc);
    bytestream2_skip(&ctx->gbc, 3);
    if (quant >= NUM_HQ_QUANTS) {
        av_log(ctx->avctx, AV_LOG_ERROR,
               "Invalid quantization matrix %d.\n", quant);
        return AVERROR_INVALIDDATA;
    }

    ret = ff_get_buffer(ctx->avctx, pic, 0);
    if (ret < 0)
        return ret;

    /* Offsets are stored from HQA1 position, so adjust them accordingly. */
    for (i = 0; i < num_slices + 1; i++)
        slice_off[i] = bytestream2_get_be32(&ctx->gbc) - 4;

    for (slice = 0; slice < num_slices; slice++) {
        if (slice_off[slice] < (num_slices + 1) * 3 ||
            slice_off[slice] >= slice_off[slice + 1] ||
            slice_off[slice + 1] > data_size) {
            av_log(ctx->avctx, AV_LOG_ERROR,
                   "Invalid slice size %"SIZE_SPECIFIER".\n", data_size);
            break;
        }
        init_get_bits(&gb, src + slice_off[slice],
                      (slice_off[slice + 1] - slice_off[slice]) * 8);

        ret = hqa_decode_slice(ctx, pic, &gb, quant, slice, width, height);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int hq_hqa_decode_frame(AVCodecContext *avctx, void *data,
                               int *got_frame, AVPacket *avpkt)
{
    HQContext *ctx = avctx->priv_data;
    AVFrame *pic = data;
    uint32_t info_tag;
    unsigned int data_size;
    int ret;
    unsigned tag;

    bytestream2_init(&ctx->gbc, avpkt->data, avpkt->size);
    if (bytestream2_get_bytes_left(&ctx->gbc) < 4 + 4) {
        av_log(avctx, AV_LOG_ERROR, "Frame is too small (%d).\n", avpkt->size);
        return AVERROR_INVALIDDATA;
    }

    info_tag = bytestream2_peek_le32(&ctx->gbc);
    if (info_tag == MKTAG('I', 'N', 'F', 'O')) {
        int info_size;
        bytestream2_skip(&ctx->gbc, 4);
        info_size = bytestream2_get_le32(&ctx->gbc);
        if (info_size < 0 || bytestream2_get_bytes_left(&ctx->gbc) < info_size) {
            av_log(avctx, AV_LOG_ERROR, "Invalid INFO size (%d).\n", info_size);
            return AVERROR_INVALIDDATA;
        }
        ff_canopus_parse_info_tag(avctx, ctx->gbc.buffer, info_size);

        bytestream2_skip(&ctx->gbc, info_size);
    }

    data_size = bytestream2_get_bytes_left(&ctx->gbc);
    if (data_size < 4) {
        av_log(avctx, AV_LOG_ERROR, "Frame is too small (%d).\n", data_size);
        return AVERROR_INVALIDDATA;
    }

    /* HQ defines dimensions and number of slices, and thus slice traversal
     * order. HQA has no size constraint and a fixed number of slices, so it
     * needs a separate scheme for it. */
    tag = bytestream2_get_le32(&ctx->gbc);
    if ((tag & 0x00FFFFFF) == (MKTAG('U', 'V', 'C', ' ') & 0x00FFFFFF)) {
        ret = hq_decode_frame(ctx, pic, tag >> 24, data_size);
    } else if (tag == MKTAG('H', 'Q', 'A', '1')) {
        ret = hqa_decode_frame(ctx, pic, data_size);
    } else {
        av_log(avctx, AV_LOG_ERROR, "Not a HQ/HQA frame.\n");
        return AVERROR_INVALIDDATA;
    }
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding frame.\n");
        return ret;
    }

    pic->key_frame = 1;
    pic->pict_type = AV_PICTURE_TYPE_I;

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int hq_hqa_decode_init(AVCodecContext *avctx)
{
    HQContext *ctx = avctx->priv_data;
    ctx->avctx = avctx;

    ff_hqdsp_init(&ctx->hqhqadsp);

    return ff_hq_init_vlcs(ctx);
}

static av_cold int hq_hqa_decode_close(AVCodecContext *avctx)
{
    HQContext *ctx = avctx->priv_data;

    ff_free_vlc(&ctx->hq_ac_vlc);
    ff_free_vlc(&ctx->hqa_cbp_vlc);

    return 0;
}

AVCodec ff_hq_hqa_decoder = {
    .name           = "hq_hqa",
    .long_name      = NULL_IF_CONFIG_SMALL("Canopus HQ/HQA"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_HQ_HQA,
    .priv_data_size = sizeof(HQContext),
    .init           = hq_hqa_decode_init,
    .decode         = hq_hqa_decode_frame,
    .close          = hq_hqa_decode_close,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
};
