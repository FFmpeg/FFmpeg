/*
 * Copyright (c) 2003 Michael Niedermayer
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
 * ASUS V1/V2 decoder.
 */

#include "libavutil/attributes.h"
#include "libavutil/mem.h"

#include "asv.h"
#include "avcodec.h"
#include "blockdsp.h"
#include "idctdsp.h"
#include "internal.h"
#include "mathops.h"
#include "mpeg12data.h"

#define VLC_BITS             6
#define ASV2_LEVEL_VLC_BITS 10

static VLC ccp_vlc;
static VLC level_vlc;
static VLC dc_ccp_vlc;
static VLC ac_ccp_vlc;
static VLC asv2_level_vlc;

static av_cold void init_vlcs(ASV1Context *a)
{
    static int done = 0;

    if (!done) {
        done = 1;

        INIT_VLC_STATIC(&ccp_vlc, VLC_BITS, 17,
                        &ff_asv_ccp_tab[0][1], 2, 1,
                        &ff_asv_ccp_tab[0][0], 2, 1, 64);
        INIT_VLC_STATIC(&dc_ccp_vlc, VLC_BITS, 8,
                        &ff_asv_dc_ccp_tab[0][1], 2, 1,
                        &ff_asv_dc_ccp_tab[0][0], 2, 1, 64);
        INIT_VLC_STATIC(&ac_ccp_vlc, VLC_BITS, 16,
                        &ff_asv_ac_ccp_tab[0][1], 2, 1,
                        &ff_asv_ac_ccp_tab[0][0], 2, 1, 64);
        INIT_VLC_STATIC(&level_vlc,  VLC_BITS, 7,
                        &ff_asv_level_tab[0][1], 2, 1,
                        &ff_asv_level_tab[0][0], 2, 1, 64);
        INIT_VLC_STATIC(&asv2_level_vlc, ASV2_LEVEL_VLC_BITS, 63,
                        &ff_asv2_level_tab[0][1], 2, 1,
                        &ff_asv2_level_tab[0][0], 2, 1, 1024);
    }
}

// FIXME write a reversed bitstream reader to avoid the double reverse
static inline int asv2_get_bits(GetBitContext *gb, int n)
{
    return ff_reverse[get_bits(gb, n) << (8 - n)];
}

static inline int asv1_get_level(GetBitContext *gb)
{
    int code = get_vlc2(gb, level_vlc.table, VLC_BITS, 1);

    if (code == 3)
        return get_sbits(gb, 8);
    else
        return code - 3;
}

static inline int asv2_get_level(GetBitContext *gb)
{
    int code = get_vlc2(gb, asv2_level_vlc.table, ASV2_LEVEL_VLC_BITS, 1);

    if (code == 31)
        return (int8_t) asv2_get_bits(gb, 8);
    else
        return code - 31;
}

static inline int asv1_decode_block(ASV1Context *a, int16_t block[64])
{
    int i;

    block[0] = 8 * get_bits(&a->gb, 8);

    for (i = 0; i < 11; i++) {
        const int ccp = get_vlc2(&a->gb, ccp_vlc.table, VLC_BITS, 1);

        if (ccp) {
            if (ccp == 16)
                break;
            if (ccp < 0 || i >= 10) {
                av_log(a->avctx, AV_LOG_ERROR, "coded coeff pattern damaged\n");
                return AVERROR_INVALIDDATA;
            }

            if (ccp & 8)
                block[a->scantable.permutated[4 * i + 0]] = (asv1_get_level(&a->gb) * a->intra_matrix[4 * i + 0]) >> 4;
            if (ccp & 4)
                block[a->scantable.permutated[4 * i + 1]] = (asv1_get_level(&a->gb) * a->intra_matrix[4 * i + 1]) >> 4;
            if (ccp & 2)
                block[a->scantable.permutated[4 * i + 2]] = (asv1_get_level(&a->gb) * a->intra_matrix[4 * i + 2]) >> 4;
            if (ccp & 1)
                block[a->scantable.permutated[4 * i + 3]] = (asv1_get_level(&a->gb) * a->intra_matrix[4 * i + 3]) >> 4;
        }
    }

    return 0;
}

static inline int asv2_decode_block(ASV1Context *a, int16_t block[64])
{
    int i, count, ccp;

    count = asv2_get_bits(&a->gb, 4);

    block[0] = 8 * asv2_get_bits(&a->gb, 8);

    ccp = get_vlc2(&a->gb, dc_ccp_vlc.table, VLC_BITS, 1);
    if (ccp) {
        if (ccp & 4)
            block[a->scantable.permutated[1]] = (asv2_get_level(&a->gb) * a->intra_matrix[1]) >> 4;
        if (ccp & 2)
            block[a->scantable.permutated[2]] = (asv2_get_level(&a->gb) * a->intra_matrix[2]) >> 4;
        if (ccp & 1)
            block[a->scantable.permutated[3]] = (asv2_get_level(&a->gb) * a->intra_matrix[3]) >> 4;
    }

    for (i = 1; i < count + 1; i++) {
        const int ccp = get_vlc2(&a->gb, ac_ccp_vlc.table, VLC_BITS, 1);

        if (ccp) {
            if (ccp & 8)
                block[a->scantable.permutated[4 * i + 0]] = (asv2_get_level(&a->gb) * a->intra_matrix[4 * i + 0]) >> 4;
            if (ccp & 4)
                block[a->scantable.permutated[4 * i + 1]] = (asv2_get_level(&a->gb) * a->intra_matrix[4 * i + 1]) >> 4;
            if (ccp & 2)
                block[a->scantable.permutated[4 * i + 2]] = (asv2_get_level(&a->gb) * a->intra_matrix[4 * i + 2]) >> 4;
            if (ccp & 1)
                block[a->scantable.permutated[4 * i + 3]] = (asv2_get_level(&a->gb) * a->intra_matrix[4 * i + 3]) >> 4;
        }
    }

    return 0;
}

static inline int decode_mb(ASV1Context *a, int16_t block[6][64])
{
    int i;

    a->bdsp.clear_blocks(block[0]);

    if (a->avctx->codec_id == AV_CODEC_ID_ASV1) {
        for (i = 0; i < 6; i++) {
            if (asv1_decode_block(a, block[i]) < 0)
                return -1;
        }
    } else {
        for (i = 0; i < 6; i++) {
            if (asv2_decode_block(a, block[i]) < 0)
                return -1;
        }
    }
    return 0;
}

static inline void idct_put(ASV1Context *a, AVFrame *frame, int mb_x, int mb_y)
{
    int16_t(*block)[64] = a->block;
    int linesize = frame->linesize[0];

    uint8_t *dest_y  = frame->data[0] + (mb_y * 16 * linesize)           + mb_x * 16;
    uint8_t *dest_cb = frame->data[1] + (mb_y *  8 * frame->linesize[1]) + mb_x *  8;
    uint8_t *dest_cr = frame->data[2] + (mb_y *  8 * frame->linesize[2]) + mb_x *  8;

    a->idsp.idct_put(dest_y,                    linesize, block[0]);
    a->idsp.idct_put(dest_y + 8,                linesize, block[1]);
    a->idsp.idct_put(dest_y + 8 * linesize,     linesize, block[2]);
    a->idsp.idct_put(dest_y + 8 * linesize + 8, linesize, block[3]);

    if (!(a->avctx->flags & CODEC_FLAG_GRAY)) {
        a->idsp.idct_put(dest_cb, frame->linesize[1], block[4]);
        a->idsp.idct_put(dest_cr, frame->linesize[2], block[5]);
    }
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    ASV1Context *const a = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    AVFrame *const p = data;
    int mb_x, mb_y, ret;

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    av_fast_padded_malloc(&a->bitstream_buffer, &a->bitstream_buffer_size,
                          buf_size);
    if (!a->bitstream_buffer)
        return AVERROR(ENOMEM);

    if (avctx->codec_id == AV_CODEC_ID_ASV1) {
        a->bbdsp.bswap_buf((uint32_t *) a->bitstream_buffer,
                           (const uint32_t *) buf, buf_size / 4);
    } else {
        int i;
        for (i = 0; i < buf_size; i++)
            a->bitstream_buffer[i] = ff_reverse[buf[i]];
    }

    init_get_bits(&a->gb, a->bitstream_buffer, buf_size * 8);

    for (mb_y = 0; mb_y < a->mb_height2; mb_y++) {
        for (mb_x = 0; mb_x < a->mb_width2; mb_x++) {
            if ((ret = decode_mb(a, a->block)) < 0)
                return ret;

            idct_put(a, p, mb_x, mb_y);
        }
    }

    if (a->mb_width2 != a->mb_width) {
        mb_x = a->mb_width2;
        for (mb_y = 0; mb_y < a->mb_height2; mb_y++) {
            if ((ret = decode_mb(a, a->block)) < 0)
                return ret;

            idct_put(a, p, mb_x, mb_y);
        }
    }

    if (a->mb_height2 != a->mb_height) {
        mb_y = a->mb_height2;
        for (mb_x = 0; mb_x < a->mb_width; mb_x++) {
            if ((ret = decode_mb(a, a->block)) < 0)
                return ret;

            idct_put(a, p, mb_x, mb_y);
        }
    }

    *got_frame = 1;

    emms_c();

    return (get_bits_count(&a->gb) + 31) / 32 * 4;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    ASV1Context *const a = avctx->priv_data;
    const int scale      = avctx->codec_id == AV_CODEC_ID_ASV1 ? 1 : 2;
    int i;

    if (avctx->extradata_size < 1) {
        av_log(avctx, AV_LOG_WARNING, "No extradata provided\n");
    }

    ff_asv_common_init(avctx);
    ff_blockdsp_init(&a->bdsp, avctx);
    ff_idctdsp_init(&a->idsp, avctx);
    init_vlcs(a);
    ff_init_scantable(a->idsp.idct_permutation, &a->scantable, ff_asv_scantab);
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    if (avctx->extradata_size < 1 || (a->inv_qscale = avctx->extradata[0]) == 0) {
        av_log(avctx, AV_LOG_ERROR, "illegal qscale 0\n");
        if (avctx->codec_id == AV_CODEC_ID_ASV1)
            a->inv_qscale = 6;
        else
            a->inv_qscale = 10;
    }

    for (i = 0; i < 64; i++) {
        int index = ff_asv_scantab[i];

        a->intra_matrix[i] = 64 * scale * ff_mpeg1_default_intra_matrix[index] /
                             a->inv_qscale;
    }

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    ASV1Context *const a = avctx->priv_data;

    av_freep(&a->bitstream_buffer);
    a->bitstream_buffer_size = 0;

    return 0;
}

#if CONFIG_ASV1_DECODER
AVCodec ff_asv1_decoder = {
    .name           = "asv1",
    .long_name      = NULL_IF_CONFIG_SMALL("ASUS V1"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_ASV1,
    .priv_data_size = sizeof(ASV1Context),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
#endif

#if CONFIG_ASV2_DECODER
AVCodec ff_asv2_decoder = {
    .name           = "asv2",
    .long_name      = NULL_IF_CONFIG_SMALL("ASUS V2"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_ASV2,
    .priv_data_size = sizeof(ASV1Context),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
#endif
