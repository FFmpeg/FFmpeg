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
#include "libavutil/mem_internal.h"
#include "libavutil/thread.h"

#include "asv.h"
#include "avcodec.h"
#include "blockdsp.h"
#include "codec_internal.h"
#include "config_components.h"
#include "decode.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "mpeg12data.h"
#include "vlc.h"

#define CCP_VLC_BITS         5
#define DC_CCP_VLC_BITS      4
#define AC_CCP_VLC_BITS      6
#define ASV1_LEVEL_VLC_BITS  4
#define ASV2_LEVEL_VLC_BITS 10

static VLCElem ccp_vlc[32];
static VLCElem level_vlc[16];
static VLCElem dc_ccp_vlc[16];
static VLCElem ac_ccp_vlc[64];
static VLCElem asv2_level_vlc[1024];

typedef struct ASVDecContext {
    ASVCommonContext c;

    GetBitContext gb;

    BlockDSPContext bdsp;
    IDCTDSPContext idsp;
    uint8_t permutated_scantable[64];
    DECLARE_ALIGNED(32, int16_t, block)[6][64];
    uint16_t intra_matrix[64];
    uint8_t *bitstream_buffer;
    unsigned int bitstream_buffer_size;
} ASVDecContext;

static av_cold void init_vlcs(void)
{
    VLC_INIT_STATIC_TABLE(ccp_vlc, CCP_VLC_BITS, 17,
                          &ff_asv_ccp_tab[0][1], 2, 1,
                          &ff_asv_ccp_tab[0][0], 2, 1, 0);
    VLC_INIT_STATIC_TABLE(dc_ccp_vlc, DC_CCP_VLC_BITS, 8,
                          &ff_asv_dc_ccp_tab[0][1], 2, 1,
                          &ff_asv_dc_ccp_tab[0][0], 2, 1, VLC_INIT_LE);
    VLC_INIT_STATIC_TABLE(ac_ccp_vlc, AC_CCP_VLC_BITS, 16,
                          &ff_asv_ac_ccp_tab[0][1], 2, 1,
                          &ff_asv_ac_ccp_tab[0][0], 2, 1, VLC_INIT_LE);
    VLC_INIT_STATIC_TABLE(level_vlc, ASV1_LEVEL_VLC_BITS, 7,
                          &ff_asv_level_tab[0][1], 2, 1,
                          &ff_asv_level_tab[0][0], 2, 1, 0);
    VLC_INIT_STATIC_TABLE(asv2_level_vlc, ASV2_LEVEL_VLC_BITS, 63,
                          &ff_asv2_level_tab[0][1], 4, 2,
                          &ff_asv2_level_tab[0][0], 4, 2, VLC_INIT_LE);
}

static inline int asv1_get_level(GetBitContext *gb)
{
    int code = get_vlc2(gb, level_vlc, ASV1_LEVEL_VLC_BITS, 1);

    if (code == 3)
        return get_sbits(gb, 8);
    else
        return code - 3;
}

// get_vlc2() is big-endian in this file
static inline int asv2_get_vlc2(GetBitContext *gb, const VLCElem *table, int bits)
{
    unsigned int index;
    int code, n;

    OPEN_READER(re, gb);
    UPDATE_CACHE_LE(re, gb);

    index = SHOW_UBITS_LE(re, gb, bits);
    code  = table[index].sym;
    n     = table[index].len;
    LAST_SKIP_BITS(re, gb, n);

    CLOSE_READER(re, gb);

    return code;
}

static inline int asv2_get_level(GetBitContext *gb)
{
    int code = asv2_get_vlc2(gb, asv2_level_vlc, ASV2_LEVEL_VLC_BITS);

    if (code == 31)
        return (int8_t) get_bits_le(gb, 8);
    else
        return code - 31;
}

static inline int asv1_decode_block(ASVDecContext *a, int16_t block[64])
{
    int i;

    block[0] = 8 * get_bits(&a->gb, 8);

    for (i = 0; i < 11; i++) {
        const int ccp = get_vlc2(&a->gb, ccp_vlc, CCP_VLC_BITS, 1);

        if (ccp) {
            if (ccp == 16)
                break;
            if (ccp < 0 || i >= 10) {
                av_log(a->c.avctx, AV_LOG_ERROR, "coded coeff pattern damaged\n");
                return AVERROR_INVALIDDATA;
            }

            if (ccp & 8)
                block[a->permutated_scantable[4 * i + 0]] = (asv1_get_level(&a->gb) * a->intra_matrix[4 * i + 0]) >> 4;
            if (ccp & 4)
                block[a->permutated_scantable[4 * i + 1]] = (asv1_get_level(&a->gb) * a->intra_matrix[4 * i + 1]) >> 4;
            if (ccp & 2)
                block[a->permutated_scantable[4 * i + 2]] = (asv1_get_level(&a->gb) * a->intra_matrix[4 * i + 2]) >> 4;
            if (ccp & 1)
                block[a->permutated_scantable[4 * i + 3]] = (asv1_get_level(&a->gb) * a->intra_matrix[4 * i + 3]) >> 4;
        }
    }

    return 0;
}

static inline int asv2_decode_block(ASVDecContext *a, int16_t block[64])
{
    int i, count, ccp;

    count = get_bits_le(&a->gb, 4);

    block[0] = 8 * get_bits_le(&a->gb, 8);

    ccp = asv2_get_vlc2(&a->gb, dc_ccp_vlc, DC_CCP_VLC_BITS);
    if (ccp) {
        if (ccp & 4)
            block[a->permutated_scantable[1]] = (asv2_get_level(&a->gb) * a->intra_matrix[1]) >> 4;
        if (ccp & 2)
            block[a->permutated_scantable[2]] = (asv2_get_level(&a->gb) * a->intra_matrix[2]) >> 4;
        if (ccp & 1)
            block[a->permutated_scantable[3]] = (asv2_get_level(&a->gb) * a->intra_matrix[3]) >> 4;
    }

    for (i = 1; i < count + 1; i++) {
        const int ccp = asv2_get_vlc2(&a->gb, ac_ccp_vlc, AC_CCP_VLC_BITS);

        if (ccp) {
            if (ccp & 8)
                block[a->permutated_scantable[4 * i + 0]] = (asv2_get_level(&a->gb) * a->intra_matrix[4 * i + 0]) >> 4;
            if (ccp & 4)
                block[a->permutated_scantable[4 * i + 1]] = (asv2_get_level(&a->gb) * a->intra_matrix[4 * i + 1]) >> 4;
            if (ccp & 2)
                block[a->permutated_scantable[4 * i + 2]] = (asv2_get_level(&a->gb) * a->intra_matrix[4 * i + 2]) >> 4;
            if (ccp & 1)
                block[a->permutated_scantable[4 * i + 3]] = (asv2_get_level(&a->gb) * a->intra_matrix[4 * i + 3]) >> 4;
        }
    }

    return 0;
}

static inline int decode_mb(ASVDecContext *a, int16_t block[6][64])
{
    int i, ret;

    a->bdsp.clear_blocks(block[0]);

    if (a->c.avctx->codec_id == AV_CODEC_ID_ASV1) {
        for (i = 0; i < 6; i++) {
            if ((ret = asv1_decode_block(a, block[i])) < 0)
                return ret;
        }
    } else {
        for (i = 0; i < 6; i++) {
            if ((ret = asv2_decode_block(a, block[i])) < 0)
                return ret;
        }
    }
    return 0;
}

static inline void idct_put(ASVDecContext *a, AVFrame *frame, int mb_x, int mb_y)
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

    if (!(a->c.avctx->flags & AV_CODEC_FLAG_GRAY)) {
        a->idsp.idct_put(dest_cb, frame->linesize[1], block[4]);
        a->idsp.idct_put(dest_cr, frame->linesize[2], block[5]);
    }
}

static int decode_frame(AVCodecContext *avctx, AVFrame *p,
                        int *got_frame, AVPacket *avpkt)
{
    ASVDecContext *const a = avctx->priv_data;
    const ASVCommonContext *const c = &a->c;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int ret;

    if (buf_size * 8LL < c->mb_height * c->mb_width * 13LL)
        return AVERROR_INVALIDDATA;

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;
    p->pict_type = AV_PICTURE_TYPE_I;
    p->flags |= AV_FRAME_FLAG_KEY;

    if (avctx->codec_id == AV_CODEC_ID_ASV1) {
        av_fast_padded_malloc(&a->bitstream_buffer, &a->bitstream_buffer_size,
                              buf_size);
        if (!a->bitstream_buffer)
            return AVERROR(ENOMEM);

        c->bbdsp.bswap_buf((uint32_t *) a->bitstream_buffer,
                           (const uint32_t *) buf, buf_size / 4);
        ret = init_get_bits8(&a->gb, a->bitstream_buffer, buf_size);
    } else {
        ret = init_get_bits8_le(&a->gb, buf, buf_size);
    }
    if (ret < 0)
        return ret;

    for (int mb_y = 0; mb_y < c->mb_height2; mb_y++) {
        for (int mb_x = 0; mb_x < c->mb_width2; mb_x++) {
            if ((ret = decode_mb(a, a->block)) < 0)
                return ret;

            idct_put(a, p, mb_x, mb_y);
        }
    }

    if (c->mb_width2 != c->mb_width) {
        int mb_x = c->mb_width2;
        for (int mb_y = 0; mb_y < c->mb_height2; mb_y++) {
            if ((ret = decode_mb(a, a->block)) < 0)
                return ret;

            idct_put(a, p, mb_x, mb_y);
        }
    }

    if (c->mb_height2 != c->mb_height) {
        int mb_y = c->mb_height2;
        for (int mb_x = 0; mb_x < c->mb_width; mb_x++) {
            if ((ret = decode_mb(a, a->block)) < 0)
                return ret;

            idct_put(a, p, mb_x, mb_y);
        }
    }

    *got_frame = 1;

    return (get_bits_count(&a->gb) + 31) / 32 * 4;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    ASVDecContext *const a = avctx->priv_data;
    const int scale      = avctx->codec_id == AV_CODEC_ID_ASV1 ? 1 : 2;
    int inv_qscale;
    int i;

    if (avctx->extradata_size < 1) {
        av_log(avctx, AV_LOG_WARNING, "No extradata provided\n");
    }

    ff_asv_common_init(avctx);
    ff_blockdsp_init(&a->bdsp);
    ff_idctdsp_init(&a->idsp, avctx);
    ff_permute_scantable(a->permutated_scantable, ff_asv_scantab,
                         a->idsp.idct_permutation);
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    if (avctx->extradata_size < 1 || (inv_qscale = avctx->extradata[0]) == 0) {
        av_log(avctx, AV_LOG_ERROR, "illegal qscale 0\n");
        if (avctx->codec_id == AV_CODEC_ID_ASV1)
            inv_qscale = 6;
        else
            inv_qscale = 10;
    }

    for (i = 0; i < 64; i++) {
        int index = ff_asv_scantab[i];

        a->intra_matrix[i] = 64 * scale * ff_mpeg1_default_intra_matrix[index] /
                             inv_qscale;
    }

    ff_thread_once(&init_static_once, init_vlcs);

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    ASVDecContext *const a = avctx->priv_data;

    av_freep(&a->bitstream_buffer);
    a->bitstream_buffer_size = 0;

    return 0;
}

#if CONFIG_ASV1_DECODER
const FFCodec ff_asv1_decoder = {
    .p.name         = "asv1",
    CODEC_LONG_NAME("ASUS V1"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_ASV1,
    .priv_data_size = sizeof(ASVDecContext),
    .init           = decode_init,
    .close          = decode_end,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
#endif

#if CONFIG_ASV2_DECODER
const FFCodec ff_asv2_decoder = {
    .p.name         = "asv2",
    CODEC_LONG_NAME("ASUS V2"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_ASV2,
    .priv_data_size = sizeof(ASVDecContext),
    .init           = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
#endif
