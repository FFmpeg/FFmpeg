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

#include "libavutil/thread.h"

#include "avcodec.h"
#include "bswapdsp.h"
#include "copy_block.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "internal.h"

typedef struct IMM4Context {
    BswapDSPContext bdsp;
    GetBitContext  gb;

    AVFrame *prev_frame;
    uint8_t *bitstream;
    int bitstream_size;

    int changed_size;
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

static const uint8_t cbplo_symbols[] = {
    3, 4, 19, 20, 35, 36, 51, 52
};

static const uint8_t cbplo_bits[] = {
    1, 4, 3, 6, 3, 6, 3, 6
};

static const uint8_t cbplo_codes[] = {
    1, 1, 1, 1, 2, 2, 3, 3
};

static const uint8_t cbphi_bits[] = {
    4, 5, 5, 4, 5, 4, 6, 4, 5, 6, 4, 4, 4, 4, 4, 2
};

static const uint8_t cbphi_codes[] = {
    3, 5, 4, 9, 3, 7, 2, 11, 2, 3, 5, 10, 4, 8, 6, 3
};

static const uint8_t blktype_symbols[] = {
    0, 1, 2, 3, 4, 16, 17, 18, 19, 20, 32, 33, 34, 35, 48, 50, 51, 52
};

static const uint8_t blktype_bits[] = {
    1, 3, 3, 5, 6, 4, 7, 7, 8, 9, 4, 7, 7, 8, 6, 8, 7, 9
};

static const uint8_t blktype_codes[] = {
    1, 3, 2, 3, 4, 3, 7, 5, 4, 4, 2, 6, 4, 3, 5, 5, 3, 2
};

static const uint16_t block_symbols[] = {
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0x81, 0x82, 0x83,
    0x84, 0x85, 0x86, 0x101, 0x102, 0x103, 0x104, 0x181, 0x182, 0x183, 0x201, 0x202,
    0x203, 0x281, 0x282, 0x283, 0x301, 0x302, 0x303, 0x381, 0x382, 0x401, 0x402,
    0x481, 0x482, 0x501, 0x502, 0x581, 0x601, 0x681, 0x701, 0x781, 0x801, 0x881,
    0x901, 0x981, 0xA01, 0xA81, 0xB01, 0xB81, 0xC01, 0xC81, 0xD01, 0x4001, 0x4002,
    0x4003, 0x4081, 0x4082, 0x4101, 0x4181, 0x4201, 0x4281, 0x4301, 0x4381, 0x4401,
    0x4481, 0x4501, 0x4581, 0x4601, 0x4681, 0x4701, 0x4781, 0x4801, 0x4881, 0x4901,
    0x4981, 0x4A01, 0x4A81, 0x4B01, 0x4B81, 0x4C01, 0x4C81, 0x4D01, 0x4D81, 0x4E01,
    0x4E81, 0x4F01, 0x4F81, 0x5001, 0x5081, 0x5101, 0x5181, 0x5201, 0x5281, 0x5301,
    0x5381, 0x5401
};

static const uint8_t block_bits[] = {
    7, 2, 4, 6, 7, 8, 9, 9, 10, 10, 11, 11, 11, 3, 6, 8, 10, 11, 12, 4, 8,
    10, 12, 5, 9, 10, 5, 9, 12, 5, 10, 12, 6, 10, 12, 6, 10, 6, 10, 6,
    10, 7, 12, 7, 7, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 11, 11, 12, 12, 4, 9,
    11, 6, 11, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9,
    9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12,
    12, 12
};

static const uint8_t block_codes[] = {
    3, 2, 15, 21, 23, 31, 37, 36, 33, 32, 7, 6, 32, 6, 20, 30, 15, 33, 80,
    14, 29, 14, 81, 13, 35, 13, 12, 34, 82, 11, 12, 83, 19, 11, 84, 18,
    10, 17, 9, 16, 8, 22, 85, 21, 20, 28, 27, 33, 32, 31, 30, 29, 28,
    27, 26, 34, 35, 86, 87, 7, 25, 5, 15, 4, 14, 13, 12, 19, 18, 17, 16,
    26, 25, 24, 23, 22, 21, 20, 19, 24, 23, 22, 21, 20, 19, 18, 17, 7,
    6, 5, 4, 36, 37, 38, 39, 88, 89, 90, 91, 92, 93, 94, 95
};

static VLC cbplo_tab;
static VLC cbphi_tab;
static VLC blktype_tab;
static VLC block_tab;

static int get_cbphi(GetBitContext *gb, int x)
{
    int value;

    value = get_vlc2(gb, cbphi_tab.table, cbphi_tab.bits, 1);
    if (value < 0)
        return AVERROR_INVALIDDATA;

    return x ? value : 15 - value;
}

static int decode_block(AVCodecContext *avctx, GetBitContext *gb,
                        int block, int factor, int flag, int offset)
{
    IMM4Context *s = avctx->priv_data;
    const uint8_t *scantable = s->intra_scantable.permutated;
    int i, last, len, factor2;

    for (i = !flag; i < 64; i++) {
        int value;

        value = get_vlc2(gb, block_tab.table, block_tab.bits, 1);
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

    return 0;
}

static int decode_blocks(AVCodecContext *avctx, GetBitContext *gb,
                         unsigned cbp, int flag, int offset)
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
            ret = decode_block(avctx, gb, i, s->factor, flag, offset);
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
        if (s->hi == 1) {
            s->factor = s->lo * 2;
        } else {
            s->factor = s->lo * 2;
        }
    }

    if (s->hi) {
        offset = s->factor;
        offset >>= 1;
        if (!(offset & 1))
            offset--;
    }

    for (y = 0; y < avctx->height; y += 16) {
        for (x = 0; x < avctx->width; x += 16) {
            unsigned cbphi, cbplo;

            cbplo = get_vlc2(gb, cbplo_tab.table, cbplo_tab.bits, 1) >> 4;
            skip_bits1(gb);

            cbphi = get_cbphi(gb, 1);

            ret = decode_blocks(avctx, gb, cbplo | (cbphi << 2), 0, offset);
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
        if (s->hi == 1) {
            s->factor = s->lo * 2;
        } else {
            s->factor = s->lo * 2;
        }
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
            unsigned cbphi, cbplo;

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

            value = get_vlc2(gb, blktype_tab.table, blktype_tab.bits, 1);
            if (value < 0)
                return AVERROR_INVALIDDATA;

            intra_block = value & 0x07;
            reverse = intra_block == 3;
            if (reverse)
                skip_bits1(gb);

            cbplo = value >> 4;
            cbphi = get_cbphi(gb, reverse);
            if (intra_block) {
                ret = decode_blocks(avctx, gb, cbplo | (cbphi << 2), 0, offset);
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
                skip_bits(gb, 2);
                ret = decode_blocks(avctx, gb, cbplo | (cbphi << 2), 1, offset);
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

    scaled = avpkt->data[8];
    if (scaled < 2) {
        int width, height;
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

        if (s->changed_size == 1 &&
            (avctx->width != width || avctx->height != height)) {
            av_log(avctx, AV_LOG_ERROR, "Frame size change is unsupported.\n");
            return AVERROR_INVALIDDATA;
        }
        avctx->width = width;
        avctx->height = height;
    }

    s->changed_size = 1;
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
    INIT_VLC_SPARSE_STATIC(&cbplo_tab, 9, FF_ARRAY_ELEMS(cbplo_bits),
                           cbplo_bits, 1, 1, cbplo_codes, 1, 1, cbplo_symbols, 1, 1, 512);

    INIT_VLC_SPARSE_STATIC(&cbphi_tab, 6, FF_ARRAY_ELEMS(cbphi_bits),
                           cbphi_bits, 1, 1, cbphi_codes, 1, 1, NULL, 0, 0, 64);

    INIT_VLC_SPARSE_STATIC(&blktype_tab, 9, FF_ARRAY_ELEMS(blktype_bits),
                           blktype_bits, 1, 1, blktype_codes, 1, 1, blktype_symbols, 1, 1, 512);

    INIT_VLC_SPARSE_STATIC(&block_tab, 12, FF_ARRAY_ELEMS(block_bits),
                           block_bits, 1, 1, block_codes, 1, 1, block_symbols, 2, 2, 4096);
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
    .capabilities     = AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE |
                        FF_CODEC_CAP_INIT_CLEANUP,
};
