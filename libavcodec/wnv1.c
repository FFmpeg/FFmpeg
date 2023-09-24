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

#include "libavutil/thread.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"

static const uint8_t code_tab[16][2] = {
    {  7, 1 }, {  8, 3 }, {  6, 3 }, { 9, 4 }, {  5, 4 }, { 10, 5 }, {  4, 5 },
    { 11, 6 }, {  3, 6 }, { 12, 7 }, { 2, 7 }, { 13, 8 }, {  1, 8 }, { 14, 9 },
    {  0, 9 }, { 15, 8 }
};

#define CODE_VLC_BITS 9
static VLCElem code_vlc[1 << CODE_VLC_BITS];

/* returns modified base_value */
static inline int wnv1_get_code(GetBitContext *gb, int shift, int base_value)
{
    int v = get_vlc2(gb, code_vlc, CODE_VLC_BITS, 1);

    if (v == 8)
        return get_bits(gb, 8 - shift) << shift;
    else
        return base_value + v * (1 << shift);
}

static int decode_frame(AVCodecContext *avctx, AVFrame *p,
                        int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf    = avpkt->data;
    int buf_size          = avpkt->size;
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
    p->flags |= AV_FRAME_FLAG_KEY;

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

static av_cold void wnv1_init_static(void)
{
    VLC_INIT_STATIC_TABLE_FROM_LENGTHS(code_vlc, CODE_VLC_BITS, 16,
                                       &code_tab[0][1], 2,
                                       &code_tab[0][0], 2, 1,
                                       -7, VLC_INIT_OUTPUT_LE);
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;

    if (avctx->width <= 1)
        return AVERROR_INVALIDDATA;

    avctx->pix_fmt = AV_PIX_FMT_YUV422P;

    ff_thread_once(&init_static_once, wnv1_init_static);

    return 0;
}

const FFCodec ff_wnv1_decoder = {
    .p.name         = "wnv1",
    CODEC_LONG_NAME("Winnov WNV1"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WNV1,
    .init           = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
