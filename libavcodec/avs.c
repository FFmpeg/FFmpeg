/*
 * AVS video decoder.
 * Copyright (c) 2006  Aurelien Jacobs <aurel@gnuage.org>
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

#include "avcodec.h"
#include "get_bits.h"


typedef struct {
    AVFrame picture;
} AvsContext;

typedef enum {
    AVS_VIDEO     = 0x01,
    AVS_AUDIO     = 0x02,
    AVS_PALETTE   = 0x03,
    AVS_GAME_DATA = 0x04,
} AvsBlockType;

typedef enum {
    AVS_I_FRAME     = 0x00,
    AVS_P_FRAME_3X3 = 0x01,
    AVS_P_FRAME_2X2 = 0x02,
    AVS_P_FRAME_2X3 = 0x03,
} AvsVideoSubType;


static int
avs_decode_frame(AVCodecContext * avctx,
                 void *data, int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    AvsContext *const avs = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame *const p = (AVFrame *) & avs->picture;
    const uint8_t *table, *vect;
    uint8_t *out;
    int i, j, x, y, stride, vect_w = 3, vect_h = 3;
    AvsVideoSubType sub_type;
    AvsBlockType type;
    GetBitContext change_map;

    if (avctx->reget_buffer(avctx, p)) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }
    p->reference = 1;
    p->pict_type = FF_P_TYPE;
    p->key_frame = 0;

    out = avs->picture.data[0];
    stride = avs->picture.linesize[0];

    sub_type = buf[0];
    type = buf[1];
    buf += 4;

    if (type == AVS_PALETTE) {
        int first, last;
        uint32_t *pal = (uint32_t *) avs->picture.data[1];

        first = AV_RL16(buf);
        last = first + AV_RL16(buf + 2);
        buf += 4;
        for (i=first; i<last; i++, buf+=3)
            pal[i] = (buf[0] << 18) | (buf[1] << 10) | (buf[2] << 2);

        sub_type = buf[0];
        type = buf[1];
        buf += 4;
    }

    if (type != AVS_VIDEO)
        return -1;

    switch (sub_type) {
    case AVS_I_FRAME:
        p->pict_type = FF_I_TYPE;
        p->key_frame = 1;
    case AVS_P_FRAME_3X3:
        vect_w = 3;
        vect_h = 3;
        break;

    case AVS_P_FRAME_2X2:
        vect_w = 2;
        vect_h = 2;
        break;

    case AVS_P_FRAME_2X3:
        vect_w = 2;
        vect_h = 3;
        break;

    default:
      return -1;
    }

    table = buf + (256 * vect_w * vect_h);
    if (sub_type != AVS_I_FRAME) {
        int map_size = ((318 / vect_w + 7) / 8) * (198 / vect_h);
        init_get_bits(&change_map, table, map_size);
        table += map_size;
    }

    for (y=0; y<198; y+=vect_h) {
        for (x=0; x<318; x+=vect_w) {
            if (sub_type == AVS_I_FRAME || get_bits1(&change_map)) {
                vect = &buf[*table++ * (vect_w * vect_h)];
                for (j=0; j<vect_w; j++) {
                    out[(y + 0) * stride + x + j] = vect[(0 * vect_w) + j];
                    out[(y + 1) * stride + x + j] = vect[(1 * vect_w) + j];
                    if (vect_h == 3)
                        out[(y + 2) * stride + x + j] =
                            vect[(2 * vect_w) + j];
                }
            }
        }
        if (sub_type != AVS_I_FRAME)
            align_get_bits(&change_map);
    }

    *picture = *(AVFrame *) & avs->picture;
    *data_size = sizeof(AVPicture);

    return buf_size;
}

static av_cold int avs_decode_init(AVCodecContext * avctx)
{
    avctx->pix_fmt = PIX_FMT_PAL8;
    return 0;
}

AVCodec avs_decoder = {
    "avs",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_AVS,
    sizeof(AvsContext),
    avs_decode_init,
    NULL,
    NULL,
    avs_decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("AVS (Audio Video Standard) video"),
};
