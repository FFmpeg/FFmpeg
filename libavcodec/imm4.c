/*
 * Infinity IMM4 decoder
 *
 * Copyright (c) 2018 Paul B Mahol
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/mem_internal.h"
#include "libavutil/thread.h"

#include "avcodec.h"
#include "bswapdsp.h"
#include "copy_block.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "internal.h"

#define CBPLO_VLC_BITS   6
#define CBPHI_VLC_BITS   6
#define BLKTYPE_VLC_BITS 9
#define BLOCK_VLC_BITS  12

typedef struct IMM4Context {
    BswapDSPContext bdsp;
    GetBitContext  gb;

    AVFrame *prev_frame;
    uint8_t *bitstream;
    int bitstream_size;

    int factor;
    unsigned lo;
    unsigned hi;

    ScanTable intra_scantable;
    DECLARE_ALIGNED(32, int16_t, block)[6][64];
    IDCTDSPContext idsp;
} IMM4Context;

static const uint8_t intra_cb[] = {
    24, 18, 12
};

static const uint8_t inter_cb[] = {
    30, 20, 15
};

static const uint8_t cbplo[][2] = {
    {    0,-6 }, { 0x01, 6 }, { 0x02, 6 }, { 0x03, 6 }, { 0x00, 4 },
    { 0x01, 3 }, { 0x02, 3 }, { 0x03, 3 }, { 0x00, 1 },
};

static const uint8_t cbphi_bits[] = {
    4, 5, 5, 4, 5, 4, 6, 4, 5, 6, 4, 4, 4, 4, 4, 2
};

static const uint8_t cbphi_codes[] = {
    3, 5, 4, 9, 3, 7, 2, 11, 2, 3, 5, 10, 4, 8, 6, 3
};

static const uint8_t blktype[][2] = {
    {    0,-8 }, { 0x34, 9 }, {    0,-9 }, { 0x14, 9 }, {    0,-9 },
    { 0x23, 8 }, { 0x13, 8 }, { 0x32, 8 }, { 0x33, 7 }, { 0x22, 7 },
    { 0x12, 7 }, { 0x21, 7 }, { 0x11, 7 }, { 0x04, 6 }, { 0x30, 6 },
    { 0x03, 5 }, { 0x20, 4 }, { 0x10, 4 }, { 0x02, 3 }, { 0x01, 3 },
    { 0x00, 1 },
};

static const uint16_t block_symbols[] = {
         0, 0x4082, 0x4003, 0x000B, 0x000A, 0x4E01, 0x4D81, 0x4D01, 0x4C81,
    0x0482, 0x0402, 0x0382, 0x0302, 0x0282, 0x0183, 0x0103, 0x0084, 0x000C,
    0x0085, 0x0B81, 0x0C01, 0x4E81, 0x4F01, 0x4F81, 0x5001, 0x0086, 0x0104,
    0x0203, 0x0283, 0x0303, 0x0502, 0x0C81, 0x0D01, 0x5081, 0x5101, 0x5181,
    0x5201, 0x5281, 0x5301, 0x5381, 0x5401, 0x0000, 0x0009, 0x0008, 0x4C01,
    0x4B81, 0x4B01, 0x4A81, 0x4A01, 0x4981, 0x4901, 0x4881, 0x4002, 0x0B01,
    0x0A81, 0x0A01, 0x0981, 0x0901, 0x0881, 0x0801, 0x0781, 0x0202, 0x0182,
    0x0007, 0x0006, 0x4801, 0x4781, 0x4701, 0x4681, 0x4601, 0x4581, 0x4501,
    0x4481, 0x0701, 0x0681, 0x0102, 0x0083, 0x0005, 0x4401, 0x4381, 0x4301,
    0x4281, 0x0601, 0x0581, 0x0501, 0x0004, 0x4201, 0x4181, 0x4101, 0x4081,
    0x0481, 0x0401, 0x0381, 0x0301, 0x0082, 0x0003, 0x0281, 0x0201, 0x0181,
    0x4001, 0x0001, 0x0081, 0x0101, 0x0002,
};

static const uint8_t block_bits[] = {
    -9, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11,
    11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12,  7, 10, 10,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
     8,  8,  7,  7,  7,  7,  7,  7,  7,  7,  6,  6,  6,  6,  6,  6,  6,  6,  6,
     6,  5,  5,  5,  4,  2,  3,  4,  4,
};

static VLC cbplo_tab;
static VLC cbphi_tab;
static VLC blktype_tab;
static VLC block_tab;

static int get_cbphi(GetBitContext *gb, int x)
{
    int value;

    value = get_vlc2(gb, cbphi_tab.table, CBPHI_VLC_BITS, 1);
    if (value < 0)
        return AVERROR_INVALIDDATA;

    return x ? value : 15 - value;
}

static int decode_block(AVCodecContext *avctx, GetBitContext *gb,
                        int block, int factor, int flag, int offset, int flag2)
{
    IMM4Context *s = avctx->priv_data;
    const uint8_t *scantable = s->intra_scantable.permutated;
    int i, last, len, factor2;

    for (i = !flag; i < 64; i++) {
        int value;

        value = get_vlc2(gb, block_tab.table, BLOCK_VLC_BITS, 1);
        if (value < 0)
            return AVERROR_INVALIDDATA;
        if (value == 0) {
            last = get_bits1(gb);
            len = get_bits(gb, 6);
            factor2 = get_sbits(gb, 8);
        } else {
            factor2 = value & 0x7F;
            last = (value >> 14) & 1;
            len = (value >> 7) & 0x3F;
            if (get_bits1(gb))
                factor2 = -factor2;
        }
        i += len;
        if (i >= 64)
            break;
        s->block[block][scantable[i]] = offset * (factor2 < 0 ? -1 : 1) + factor * factor2;
        if (last)
            break;
    }

    if (s->hi == 2 && flag2 && block < 4) {
        if (flag)
            s->block[block][scantable[0]]  *= 2;
        s->block[block][scantable[1]]  *= 2;
        s->block[block][scantable[8]]  *= 2;
        s->block[block][scantable[16]] *= 2;
    }

    return 0;
}

static int decode_blocks(AVCodecContext *avctx, GetBitContext *gb,
                         unsigned cbp, int flag, int offset, unsigned flag2)
{
    IMM4Context *s = avctx->priv_data;
    const uint8_t *scantable = s->intra_scantable.permutated;
    int ret, i;

    memset(s->block, 0, sizeof(s->block));

    for (i = 0; i < 6; i++) {
        if (!flag) {
            int x = get_bits(gb, 8);

            if (x == 255)
                x = 128;
            x *= 8;

            s->block[i][scantable[0]] = x;
        }

        if (cbp & (1 << (5 - i))) {
            ret = decode_block(avctx, gb, i, s->factor, flag, offset, flag2);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static int decode_intra(AVCodecContext *avctx, GetBitContext *gb, AVFrame *frame)
{
    IMM4Context *s = avctx->priv_data;
    int ret, x, y, offset = 0;

    if (s->hi == 0) {
        if (s->lo > 2)
            return AVERROR_INVALIDDATA;
        s->factor = intra_cb[s->lo];
    } else {
        s->factor = s->lo * 2;
    }

    if (s->hi) {
        offset = s->factor;
        offset >>= 1;
        if (!(offset & 1))
            offset--;
    }

    for (y = 0; y < avctx->height; y += 16) {
        for (x = 0; x < avctx->width; x += 16) {
            unsigned flag, cbphi, cbplo;

            cbplo = get_vlc2(gb, cbplo_tab.table, CBPLO_VLC_BITS, 1);
            flag = get_bits1(gb);

            cbphi = get_cbphi(gb, 1);

            ret = decode_blocks(avctx, gb, cbplo | (cbphi << 2), 0, offset, flag);
            if (ret < 0)
                return ret;

            s->idsp.idct_put(frame->data[0] + y * frame->linesize[0] + x,
                             frame->linesize[0], s->block[0]);
            s->idsp.idct_put(frame->data[0] + y * frame->linesize[0] + x + 8,
                             frame->linesize[0], s->block[1]);
            s->idsp.idct_put(frame->data[0] + (y + 8) * frame->linesize[0] + x,
                             frame->linesize[0], s->block[2]);
            s->idsp.idct_put(frame->data[0] + (y + 8) * frame->linesize[0] + x + 8,
                             frame->linesize[0], s->block[3]);
            s->idsp.idct_put(frame->data[1] + (y >> 1) * frame->linesize[1] + (x >> 1),
                             frame->linesize[1], s->block[4]);
            s->idsp.idct_put(frame->data[2] + (y >> 1) * frame->linesize[2] + (x >> 1),
                             frame->linesize[2], s->block[5]);
        }
    }

    return 0;
}

static int decode_inter(AVCodecContext *avctx, GetBitContext *gb,
                        AVFrame *frame, AVFrame *prev)
{
    IMM4Context *s = avctx->priv_data;
    int ret, x, y, offset = 0;

    if (s->hi == 0) {
        if (s->lo > 2)
            return AVERROR_INVALIDDATA;
        s->factor = inter_cb[s->lo];
    } else {
        s->factor = s->lo * 2;
    }

    if (s->hi) {
        offset = s->factor;
        offset >>= 1;
        if (!(offset & 1))
            offset--;
    }

    for (y = 0; y < avctx->height; y += 16) {
        for (x = 0; x < avctx->width; x += 16) {
            int reverse, intra_block, value;
            unsigned cbphi, cbplo, flag2 = 0;

            if (get_bits1(gb)) {
                copy_block16(frame->data[0] + y * frame->linesize[0] + x,
                             prev->data[0] + y * prev->linesize[0] + x,
                             frame->linesize[0], prev->linesize[0], 16);
                copy_block8(frame->data[1] + (y >> 1) * frame->linesize[1] + (x >> 1),
                            prev->data[1] + (y >> 1) * prev->linesize[1] + (x >> 1),
                            frame->linesize[1], prev->linesize[1], 8);
                copy_block8(frame->data[2] + (y >> 1) * frame->linesize[2] + (x >> 1),
                            prev->data[2] + (y >> 1) * prev->linesize[2] + (x >> 1),
                            frame->linesize[2], prev->linesize[2], 8);
                continue;
            }

            value = get_vlc2(gb, blktype_tab.table, BLKTYPE_VLC_BITS, 1);
            if (value < 0)
                return AVERROR_INVALIDDATA;

            intra_block = value & 0x07;
            reverse = intra_block == 3;
            if (reverse)
                flag2 = get_bits1(gb);

            cbplo = value >> 4;
            cbphi = get_cbphi(gb, reverse);
            if (intra_block) {
                ret = decode_blocks(avctx, gb, cbplo | (cbphi << 2), 0, offset, flag2);
                if (ret < 0)
                    return ret;

                s->idsp.idct_put(frame->data[0] + y * frame->linesize[0] + x,
                                 frame->linesize[0], s->block[0]);
                s->idsp.idct_put(frame->data[0] + y * frame->linesize[0] + x + 8,
                                 frame->linesize[0], s->block[1]);
                s->idsp.idct_put(frame->data[0] + (y + 8) * frame->linesize[0] + x,
                                 frame->linesize[0], s->block[2]);
                s->idsp.idct_put(frame->data[0] + (y + 8) * frame->linesize[0] + x + 8,
                                 frame->linesize[0], s->block[3]);
                s->idsp.idct_put(frame->data[1] + (y >> 1) * frame->linesize[1] + (x >> 1),
                                 frame->linesize[1], s->block[4]);
                s->idsp.idct_put(frame->data[2] + (y >> 1) * frame->linesize[2] + (x >> 1),
                                 frame->linesize[2], s->block[5]);
            } else {
                flag2 = get_bits1(gb);
                skip_bits1(gb);
                ret = decode_blocks(avctx, gb, cbplo | (cbphi << 2), 1, offset, flag2);
                if (ret < 0)
                    return ret;

                copy_block16(frame->data[0] + y * frame->linesize[0] + x,
                             prev->data[0] + y * prev->linesize[0] + x,
                             frame->linesize[0], prev->linesize[0], 16);
                copy_block8(frame->data[1] + (y >> 1) * frame->linesize[1] + (x >> 1),
                            prev->data[1] + (y >> 1) * prev->linesize[1] + (x >> 1),
                            frame->linesize[1], prev->linesize[1], 8);
                copy_block8(frame->data[2] + (y >> 1) * frame->linesize[2] + (x >> 1),
                            prev->data[2] + (y >> 1) * prev->linesize[2] + (x >> 1),
                            frame->linesize[2], prev->linesize[2], 8);

                s->idsp.idct_add(frame->data[0] + y * frame->linesize[0] + x,
                                 frame->linesize[0], s->block[0]);
                s->idsp.idct_add(frame->data[0] + y * frame->linesize[0] + x + 8,
                                 frame->linesize[0], s->block[1]);
                s->idsp.idct_add(frame->data[0] + (y + 8) * frame->linesize[0] + x,
                                 frame->linesize[0], s->block[2]);
                s->idsp.idct_add(frame->data[0] + (y + 8) * frame->linesize[0] + x + 8,
                                 frame->linesize[0], s->block[3]);
                s->idsp.idct_add(frame->data[1] + (y >> 1) * frame->linesize[1] + (x >> 1),
                                 frame->linesize[1], s->block[4]);
                s->idsp.idct_add(frame->data[2] + (y >> 1) * frame->linesize[2] + (x >> 1),
                                 frame->linesize[2], s->block[5]);
            }
        }
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame, AVPacket *avpkt)
{
    IMM4Context *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    AVFrame *frame = data;
    int width, height;
    unsigned type;
    int ret, scaled;

    if (avpkt->size <= 32)
        return AVERROR_INVALIDDATA;

    av_fast_padded_malloc(&s->bitstream, &s->bitstream_size,
                          FFALIGN(avpkt->size, 4));
    if (!s->bitstream)
        return AVERROR(ENOMEM);

    s->bdsp.bswap_buf((uint32_t *)s->bitstream,
                      (uint32_t *)avpkt->data,
                      (avpkt->size + 3) >> 2);

    if ((ret = init_get_bits8(gb, s->bitstream, FFALIGN(avpkt->size, 4))) < 0)
        return ret;

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    avctx->color_range = AVCOL_RANGE_JPEG;

    width = avctx->width;
    height = avctx->height;

    scaled = avpkt->data[8];
    if (scaled < 2) {
        int mode = avpkt->data[10];

        switch (mode) {
        case 1:
            width = 352;
            height = 240;
            break;
        case 2:
            width = 704;
            height = 240;
            break;
        case 4:
            width = 480;
            height = 704;
            break;
        case 17:
            width = 352;
            height = 288;
            break;
        case 18:
            width = 704;
            height = 288;
            break;
        default:
            width = 704;
            height = 576;
            break;
        }
    }

    skip_bits_long(gb, 24 * 8);
    type = get_bits_long(gb, 32);
    s->hi = get_bits(gb, 16);
    s->lo = get_bits(gb, 16);

    switch (type) {
    case 0x19781977:
        frame->key_frame = 1;
        frame->pict_type = AV_PICTURE_TYPE_I;
        break;
    case 0x12250926:
        frame->key_frame = 0;
        frame->pict_type = AV_PICTURE_TYPE_P;
        break;
    default:
        avpriv_request_sample(avctx, "type %X", type);
        return AVERROR_PATCHWELCOME;
    }

    if (avctx->width  != width ||
        avctx->height != height) {
        if (!frame->key_frame) {
            av_log(avctx, AV_LOG_ERROR, "Frame size change is unsupported.\n");
            return AVERROR_INVALIDDATA;
        }
        av_frame_unref(s->prev_frame);
    }

    ret = ff_set_dimensions(avctx, width, height);
    if (ret < 0)
        return ret;

    if ((ret = ff_get_buffer(avctx, frame, frame->key_frame ? AV_GET_BUFFER_FLAG_REF : 0)) < 0)
        return ret;

    if (frame->key_frame) {
        ret = decode_intra(avctx, gb, frame);
        if (ret < 0)
            return ret;

        av_frame_unref(s->prev_frame);
        if ((ret = av_frame_ref(s->prev_frame, frame)) < 0)
            return ret;
    } else {
        if (!s->prev_frame->data[0]) {
            av_log(avctx, AV_LOG_ERROR, "Missing reference frame.\n");
            return AVERROR_INVALIDDATA;
        }

        ret = decode_inter(avctx, gb, frame, s->prev_frame);
        if (ret < 0)
            return ret;
    }

    *got_frame = 1;

    return avpkt->size;
}

static av_cold void imm4_init_static_data(void)
{
    INIT_VLC_STATIC_FROM_LENGTHS(&cbplo_tab, CBPLO_VLC_BITS, FF_ARRAY_ELEMS(cbplo),
                                 &cbplo[0][1], 2, &cbplo[0][0], 2, 1,
                                 0, 0, 1 << CBPLO_VLC_BITS);

    INIT_VLC_SPARSE_STATIC(&cbphi_tab, CBPHI_VLC_BITS, FF_ARRAY_ELEMS(cbphi_bits),
                           cbphi_bits, 1, 1, cbphi_codes, 1, 1, NULL, 0, 0, 64);

    INIT_VLC_STATIC_FROM_LENGTHS(&blktype_tab, BLKTYPE_VLC_BITS, FF_ARRAY_ELEMS(blktype),
                                 &blktype[0][1], 2, &blktype[0][0], 2, 1,
                                 0, 0, 1 << BLKTYPE_VLC_BITS);

    INIT_VLC_STATIC_FROM_LENGTHS(&block_tab, BLOCK_VLC_BITS, FF_ARRAY_ELEMS(block_bits),
                                 block_bits, 1, block_symbols, 2, 2,
                                 0, 0, 1 << BLOCK_VLC_BITS);
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    IMM4Context *s = avctx->priv_data;
    uint8_t table[64];

    for (int i = 0; i < 64; i++)
        table[i] = i;

    ff_bswapdsp_init(&s->bdsp);
    ff_idctdsp_init(&s->idsp, avctx);
    ff_init_scantable(s->idsp.idct_permutation, &s->intra_scantable, table);

    s->prev_frame = av_frame_alloc();
    if (!s->prev_frame)
        return AVERROR(ENOMEM);

    ff_thread_once(&init_static_once, imm4_init_static_data);

    return 0;
}

static void decode_flush(AVCodecContext *avctx)
{
    IMM4Context *s = avctx->priv_data;

    av_frame_unref(s->prev_frame);
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    IMM4Context *s = avctx->priv_data;

    av_frame_free(&s->prev_frame);
    av_freep(&s->bitstream);
    s->bitstream_size = 0;

    return 0;
}

AVCodec ff_imm4_decoder = {
    .name             = "imm4",
    .long_name        = NULL_IF_CONFIG_SMALL("Infinity IMM4"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_IMM4,
    .priv_data_size   = sizeof(IMM4Context),
    .init             = decode_init,
    .close            = decode_close,
    .decode           = decode_frame,
    .flush            = decode_flush,
    .capabilities     = AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE |
                        FF_CODEC_CAP_INIT_CLEANUP,
};
