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
 * ASUS V1/V2 encoder.
 */

#include "config_components.h"

#include "libavutil/attributes.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "aandcttab.h"
#include "asv.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "fdctdsp.h"
#include "mpeg12data.h"
#include "pixblockdsp.h"
#include "put_bits.h"

typedef struct ASVEncContext {
    ASVCommonContext c;

    PutBitContext pb;

    void (*get_pixels)(int16_t *restrict block,
                       const uint8_t *pixels,
                       ptrdiff_t stride);

    PixblockDSPContext pdsp;
    FDCTDSPContext fdsp;
    DECLARE_ALIGNED(32, int16_t, block)[6][64];
    int q_intra_matrix[64];
} ASVEncContext;

enum {
    ASV1_MAX_BLOCK_SIZE = 8 + 10 * FFMAX(2 /* skip */, 5 /* ccp */ + 4 * 11 /* level */) + 5,
    ASV1_MAX_MB_SIZE    = 6 * ASV1_MAX_BLOCK_SIZE,
    ASV2_MAX_BLOCK_SIZE = 4 + 8 + 16 * (6 /* ccp */ + 4 * 13 /* level */),
    ASV2_MAX_MB_SIZE    = 6 * ASV2_MAX_BLOCK_SIZE,
    MAX_MB_SIZE         = (FFMAX(ASV1_MAX_MB_SIZE, ASV2_MAX_MB_SIZE) + 7) / 8
};

static inline void asv1_put_level(PutBitContext *pb, int level)
{
    unsigned int index = level + 3;
    unsigned n, code;

    if (index <= 6) {
        n    = ff_asv_level_tab[index][1];
        code = ff_asv_level_tab[index][0];
    } else {
        n    = 3 + 8;
        code = (0 /* Escape code */ << 8)  | (level & 0xFF);
    }
    put_bits(pb, n, code);
}

static inline void asv2_put_level(ASVEncContext *a, PutBitContext *pb, int level)
{
    unsigned int index = level + 31;
    unsigned n, code;

    if (index <= 62) {
        n    = ff_asv2_level_tab[index][1];
        code = ff_asv2_level_tab[index][0];
    } else {
        if (level < -128 || level > 127) {
            av_log(a->c.avctx, AV_LOG_WARNING, "Clipping level %d, increase qscale\n", level);
            level = av_clip_int8(level);
        }
        n    = 5 + 8;
        code = (level & 0xFF) << 5 | /* Escape code */ 0;
    }
    put_bits_le(pb, n, code);
}

static inline void asv1_encode_block(ASVEncContext *a, int16_t block[64])
{
    put_bits(&a->pb, 8, (block[0] + 32) >> 6);
    block[0] = 0;

    for (unsigned i = 0, nc_bits = 0, nc_val = 0; i < 10; i++) {
        const int index = ff_asv_scantab[4 * i];
        int ccp         = 0;

        if ((block[index + 0] = (block[index + 0] *
                                 a->q_intra_matrix[index + 0] + (1 << 15)) >> 16))
            ccp |= 8;
        if ((block[index + 8] = (block[index + 8] *
                                 a->q_intra_matrix[index + 8] + (1 << 15)) >> 16))
            ccp |= 4;
        if ((block[index + 1] = (block[index + 1] *
                                 a->q_intra_matrix[index + 1] + (1 << 15)) >> 16))
            ccp |= 2;
        if ((block[index + 9] = (block[index + 9] *
                                 a->q_intra_matrix[index + 9] + (1 << 15)) >> 16))
            ccp |= 1;

        if (ccp) {
            put_bits(&a->pb, nc_bits + ff_asv_ccp_tab[ccp][1],
                             nc_val << ff_asv_ccp_tab[ccp][1] /* Skip */ |
                             ff_asv_ccp_tab[ccp][0]);
            nc_bits = 0;
            nc_val  = 0;

            if (ccp & 8)
                asv1_put_level(&a->pb, block[index + 0]);
            if (ccp & 4)
                asv1_put_level(&a->pb, block[index + 8]);
            if (ccp & 2)
                asv1_put_level(&a->pb, block[index + 1]);
            if (ccp & 1)
                asv1_put_level(&a->pb, block[index + 9]);
        } else {
            nc_bits += 2;
            nc_val   = (nc_val << 2) | 2;
        }
    }
    put_bits(&a->pb, 5, 0xF); /* End of block */
}

static inline void asv2_encode_block(ASVEncContext *a, int16_t block[64])
{
    int i;
    int count = 0;

    for (count = 63; count > 3; count--) {
        const int index = ff_asv_scantab[count];
        if ((block[index] * a->q_intra_matrix[index] + (1 << 15)) >> 16)
            break;
    }

    count >>= 2;

    put_bits_le(&a->pb, 4 + 8, count /* 4 bits */ |
                               (/* DC */(block[0] + 32) >> 6) << 4);
    block[0] = 0;

    for (i = 0; i <= count; i++) {
        const int index = ff_asv_scantab[4 * i];
        int ccp         = 0;

        if ((block[index + 0] = (block[index + 0] *
                                 a->q_intra_matrix[index + 0] + (1 << 15)) >> 16))
            ccp |= 8;
        if ((block[index + 8] = (block[index + 8] *
                                 a->q_intra_matrix[index + 8] + (1 << 15)) >> 16))
            ccp |= 4;
        if ((block[index + 1] = (block[index + 1] *
                                 a->q_intra_matrix[index + 1] + (1 << 15)) >> 16))
            ccp |= 2;
        if ((block[index + 9] = (block[index + 9] *
                                 a->q_intra_matrix[index + 9] + (1 << 15)) >> 16))
            ccp |= 1;

        av_assert2(i || ccp < 8);
        if (i)
            put_bits_le(&a->pb, ff_asv_ac_ccp_tab[ccp][1], ff_asv_ac_ccp_tab[ccp][0]);
        else
            put_bits_le(&a->pb, ff_asv_dc_ccp_tab[ccp][1], ff_asv_dc_ccp_tab[ccp][0]);

        if (ccp) {
            if (ccp & 8)
                asv2_put_level(a, &a->pb, block[index + 0]);
            if (ccp & 4)
                asv2_put_level(a, &a->pb, block[index + 8]);
            if (ccp & 2)
                asv2_put_level(a, &a->pb, block[index + 1]);
            if (ccp & 1)
                asv2_put_level(a, &a->pb, block[index + 9]);
        }
    }
}

static inline int encode_mb(ASVEncContext *a, int16_t block[6][64])
{
    int i;

    av_assert0(put_bytes_left(&a->pb, 0) >= MAX_MB_SIZE);

    if (a->c.avctx->codec_id == AV_CODEC_ID_ASV1) {
        for (i = 0; i < 6; i++)
            asv1_encode_block(a, block[i]);
    } else {
        for (i = 0; i < 6; i++) {
            asv2_encode_block(a, block[i]);
        }
    }
    return 0;
}

static inline void dct_get(ASVEncContext *a, const AVFrame *frame,
                           int mb_x, int mb_y)
{
    int16_t (*block)[64] = a->block;
    int linesize = frame->linesize[0];
    int i;

    const uint8_t *ptr_y  = frame->data[0] + (mb_y * 16 * linesize)           + mb_x * 16;
    const uint8_t *ptr_cb = frame->data[1] + (mb_y *  8 * frame->linesize[1]) + mb_x *  8;
    const uint8_t *ptr_cr = frame->data[2] + (mb_y *  8 * frame->linesize[2]) + mb_x *  8;

    a->get_pixels(block[0], ptr_y,                    linesize);
    a->get_pixels(block[1], ptr_y + 8,                linesize);
    a->get_pixels(block[2], ptr_y + 8 * linesize,     linesize);
    a->get_pixels(block[3], ptr_y + 8 * linesize + 8, linesize);
    for (i = 0; i < 4; i++)
        a->fdsp.fdct(block[i]);

    if (!(a->c.avctx->flags & AV_CODEC_FLAG_GRAY)) {
        a->get_pixels(block[4], ptr_cb, frame->linesize[1]);
        a->get_pixels(block[5], ptr_cr, frame->linesize[2]);
        for (i = 4; i < 6; i++)
            a->fdsp.fdct(block[i]);
    }
}

static void handle_partial_mb(ASVEncContext *a, const uint8_t *const data[3],
                              const int linesizes[3],
                              int valid_width, int valid_height)
{
    const int nb_blocks = a->c.avctx->flags & AV_CODEC_FLAG_GRAY ? 4 : 6;
    static const struct Descriptor {
        uint8_t x_offset, y_offset;
        uint8_t component, subsampling;
    } block_descriptor[] = {
        { 0, 0, 0, 0 }, { 8, 0, 0, 0 }, { 0, 8, 0, 0 }, { 8, 8, 0, 0 },
        { 0, 0, 1, 1 }, { 0, 0, 2, 1 },
    };

    for (int i = 0; i < nb_blocks; ++i) {
        const struct Descriptor *const desc = block_descriptor + i;
        int width_avail  = AV_CEIL_RSHIFT(valid_width,  desc->subsampling) - desc->x_offset;
        int height_avail = AV_CEIL_RSHIFT(valid_height, desc->subsampling) - desc->y_offset;

        if (width_avail <= 0 || height_avail <= 0) {
            // This block is outside of the visible part; don't replicate pixels,
            // just zero the block, so that only the dc value will be coded.
            memset(a->block[i], 0, sizeof(a->block[i]));
            continue;
        }
        width_avail  = FFMIN(width_avail,  8);
        height_avail = FFMIN(height_avail, 8);

        ptrdiff_t linesize = linesizes[desc->component];
        const uint8_t *src = data[desc->component] + desc->y_offset * linesize + desc->x_offset;
        int16_t *block = a->block[i];

        for (int h = 0;; block += 8, src += linesize) {
            int16_t last;
            for (int w = 0; w < width_avail; ++w)
                last = block[w] = src[w];
            for (int w = width_avail; w < 8; ++w)
                block[w] = last;
            if (++h == height_avail)
                break;
        }
        const int16_t *const last_row = block;
        for (int h = height_avail; h < 8; ++h) {
            block += 8;
            AV_COPY128(block, last_row);
        }

        a->fdsp.fdct(a->block[i]);
    }

    encode_mb(a, a->block);
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pict, int *got_packet)
{
    ASVEncContext *const a = avctx->priv_data;
    const ASVCommonContext *const c = &a->c;
    int size, ret;

    ret = ff_alloc_packet(avctx, pkt, c->mb_height * c->mb_width * MAX_MB_SIZE + 3);
    if (ret < 0)
        return ret;

    if (!PIXBLOCKDSP_8BPP_GET_PIXELS_SUPPORTS_UNALIGNED &&
        ((uintptr_t)pict->data[0] & 7 || pict->linesize[0] & 7 ||
         (uintptr_t)pict->data[1] & 7 || pict->linesize[1] & 7 ||
         (uintptr_t)pict->data[2] & 7 || pict->linesize[2] & 7))
        a->get_pixels = a->pdsp.get_pixels_unaligned;
    else
        a->get_pixels = a->pdsp.get_pixels;

    init_put_bits(&a->pb, pkt->data, pkt->size);

    for (int mb_y = 0; mb_y < c->mb_height2; mb_y++) {
        for (int mb_x = 0; mb_x < c->mb_width2; mb_x++) {
            dct_get(a, pict, mb_x, mb_y);
            encode_mb(a, a->block);
        }
    }

    if (avctx->width & 15) {
        const uint8_t *src[3] = {
            pict->data[0] + c->mb_width2 * 16,
            pict->data[1] + c->mb_width2 *  8,
            pict->data[2] + c->mb_width2 *  8,
        };
        int available_width = avctx->width & 15;

        for (int mb_y = 0; mb_y < c->mb_height2; mb_y++) {
            handle_partial_mb(a, src, pict->linesize, available_width, 16);
            src[0] += 16 * pict->linesize[0];
            src[1] +=  8 * pict->linesize[1];
            src[2] +=  8 * pict->linesize[2];
        }
    }

    if (avctx->height & 15) {
        const uint8_t *src[3] = {
            pict->data[0] + c->mb_height2 * 16 * pict->linesize[0],
            pict->data[1] + c->mb_height2 *  8 * pict->linesize[1],
            pict->data[2] + c->mb_height2 *  8 * pict->linesize[2],
        };
        int available_height = avctx->height & 15;

        for (int remaining = avctx->width;; remaining -= 16) {
            handle_partial_mb(a, src, pict->linesize, remaining, available_height);
            if (remaining <= 16)
                break;
            src[0] += 16;
            src[1] +=  8;
            src[2] +=  8;
        }
    }

    if (avctx->codec_id == AV_CODEC_ID_ASV1)
        flush_put_bits(&a->pb);
    else
        flush_put_bits_le(&a->pb);
    AV_WN32(put_bits_ptr(&a->pb), 0);
    size = (put_bytes_output(&a->pb) + 3) / 4;

    if (avctx->codec_id == AV_CODEC_ID_ASV1) {
        c->bbdsp.bswap_buf((uint32_t *) pkt->data,
                           (uint32_t *) pkt->data, size);
    }

    pkt->size   = size * 4;
    *got_packet = 1;

    return 0;
}

static av_cold int encode_init(AVCodecContext *avctx)
{
    ASVEncContext *const a = avctx->priv_data;
    int i;
    const int scale = avctx->codec_id == AV_CODEC_ID_ASV1 ? 1 : 2;
    int inv_qscale;

    ff_asv_common_init(avctx);
    ff_fdctdsp_init(&a->fdsp, avctx);
    ff_pixblockdsp_init(&a->pdsp, 8);

    if (avctx->global_quality <= 0)
        avctx->global_quality = 4 * FF_QUALITY_SCALE;

    inv_qscale = (32 * scale * FF_QUALITY_SCALE +
                     avctx->global_quality / 2) / avctx->global_quality;

    avctx->extradata                   = av_mallocz(8);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);
    avctx->extradata_size              = 8;
    AV_WL32A(avctx->extradata, inv_qscale);
    AV_WL32A(avctx->extradata + 4, MKTAG('A', 'S', 'U', 'S'));

    for (i = 0; i < 64; i++) {
        if (a->fdsp.fdct == ff_fdct_ifast) {
            int q = 32LL * scale * ff_mpeg1_default_intra_matrix[i] * ff_aanscales[i];
            a->q_intra_matrix[i] = (((int64_t)inv_qscale << 30) + q / 2) / q;
        } else {
            int q = 32 * scale * ff_mpeg1_default_intra_matrix[i];
            a->q_intra_matrix[i] = ((inv_qscale << 16) + q / 2) / q;
        }
    }

    return 0;
}

#if CONFIG_ASV1_ENCODER
const FFCodec ff_asv1_encoder = {
    .p.name         = "asv1",
    CODEC_LONG_NAME("ASUS V1"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_ASV1,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size = sizeof(ASVEncContext),
    .init           = encode_init,
    FF_CODEC_ENCODE_CB(encode_frame),
    CODEC_PIXFMTS(AV_PIX_FMT_YUV420P),
    .color_ranges   = AVCOL_RANGE_MPEG,
};
#endif

#if CONFIG_ASV2_ENCODER
const FFCodec ff_asv2_encoder = {
    .p.name         = "asv2",
    CODEC_LONG_NAME("ASUS V2"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_ASV2,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size = sizeof(ASVEncContext),
    .init           = encode_init,
    FF_CODEC_ENCODE_CB(encode_frame),
    CODEC_PIXFMTS(AV_PIX_FMT_YUV420P),
    .color_ranges   = AVCOL_RANGE_MPEG,
};
#endif
