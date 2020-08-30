/*
 * Winnov WNV1 codec
 * Copyright (c) 2005 Konstantin Shishkov
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
 * Winnov WNV1 codec.
 */

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"


static const uint16_t code_tab[16][2] = {
    { 0x17F, 9 }, { 0xBF, 8 }, { 0x5F, 7 }, { 0x2F, 6 }, { 0x17, 5 }, { 0x0B, 4 }, { 0x005, 3 },
    { 0x000, 1 },
    { 0x01, 3 }, { 0x03, 4 }, { 0x07, 5 }, { 0x0F, 6 }, { 0x1F, 7 }, { 0x3F, 8 }, { 0x07F, 9 }, { 0xFF, 8 }
};

#define CODE_VLC_BITS 9
static VLC code_vlc;

/* returns modified base_value */
static inline int wnv1_get_code(GetBitContext *gb, int shift, int base_value)
{
    int v = get_vlc2(gb, code_vlc.table, CODE_VLC_BITS, 1);

    if (v == 15)
        return get_bits(gb, 8 - shift) << shift;
    else
        return base_value + ((v - 7U) << shift);
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    const uint8_t *buf    = avpkt->data;
    int buf_size          = avpkt->size;
    AVFrame * const p     = data;
    GetBitContext gb;
    unsigned char *Y,*U,*V;
    int i, j, ret, shift;
    int prev_y = 0, prev_u = 0, prev_v = 0;

    if (buf_size < 8 + avctx->height * (avctx->width/2)/8) {
        av_log(avctx, AV_LOG_ERROR, "Packet size %d is too small\n", buf_size);
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;
    p->key_frame = 1;

    if ((ret = init_get_bits8(&gb, buf + 8, buf_size - 8)) < 0)
        return ret;

    if (buf[2] >> 4 == 6)
        shift = 2;
    else {
        shift = 8 - (buf[2] >> 4);
        if (shift > 4) {
            avpriv_request_sample(avctx,
                                  "Unknown WNV1 frame header value %i",
                                  buf[2] >> 4);
            shift = 4;
        }
        if (shift < 1) {
            avpriv_request_sample(avctx,
                                  "Unknown WNV1 frame header value %i",
                                  buf[2] >> 4);
            shift = 1;
        }
    }

    Y = p->data[0];
    U = p->data[1];
    V = p->data[2];
    for (j = 0; j < avctx->height; j++) {
        for (i = 0; i < avctx->width / 2; i++) {
            Y[i * 2] = wnv1_get_code(&gb, shift, prev_y);
            prev_u = U[i] = wnv1_get_code(&gb, shift, prev_u);
            prev_y = Y[(i * 2) + 1] = wnv1_get_code(&gb, shift, Y[i * 2]);
            prev_v = V[i] = wnv1_get_code(&gb, shift, prev_v);
        }
        Y += p->linesize[0];
        U += p->linesize[1];
        V += p->linesize[2];
    }


    *got_frame      = 1;

    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    static VLC_TYPE code_table[1 << CODE_VLC_BITS][2];

    avctx->pix_fmt = AV_PIX_FMT_YUV422P;

    code_vlc.table           = code_table;
    code_vlc.table_allocated = 1 << CODE_VLC_BITS;
    init_vlc(&code_vlc, CODE_VLC_BITS, 16,
             &code_tab[0][1], 4, 2,
             &code_tab[0][0], 4, 2, INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);

    return 0;
}

AVCodec ff_wnv1_decoder = {
    .name           = "wnv1",
    .long_name      = NULL_IF_CONFIG_SMALL("Winnov WNV1"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WNV1,
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
