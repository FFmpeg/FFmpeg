/*
 * Bitmap Brothers JV video decoder
 * Copyright (c) 2011 Peter Ross <pross@xvid.org>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Bitmap Brothers JV video decoder
 * @author Peter Ross <pross@xvid.org>
 */

#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "blockdsp.h"
#include "get_bits.h"
#include "internal.h"

typedef struct JvContext {
    BlockDSPContext bdsp;
    AVFrame   *frame;
    uint32_t   palette[AVPALETTE_COUNT];
    int        palette_has_changed;
} JvContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    JvContext *s = avctx->priv_data;

    if (!avctx->width || !avctx->height ||
        (avctx->width & 7) || (avctx->height & 7)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid video dimensions: %dx%d\n",
               avctx->width, avctx->height);
        return AVERROR(EINVAL);
    }

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    avctx->pix_fmt = AV_PIX_FMT_PAL8;
    ff_blockdsp_init(&s->bdsp, avctx);
    return 0;
}

/**
 * Decode 2x2 block
 */
static inline void decode2x2(GetBitContext *gb, uint8_t *dst, int linesize)
{
    int i, j, v[2];

    switch (get_bits(gb, 2)) {
    case 1:
        v[0] = get_bits(gb, 8);
        for (j = 0; j < 2; j++)
            memset(dst + j * linesize, v[0], 2);
        break;
    case 2:
        v[0] = get_bits(gb, 8);
        v[1] = get_bits(gb, 8);
        for (j = 0; j < 2; j++)
            for (i = 0; i < 2; i++)
                dst[j * linesize + i] = v[get_bits1(gb)];
        break;
    case 3:
        for (j = 0; j < 2; j++)
            for (i = 0; i < 2; i++)
                dst[j * linesize + i] = get_bits(gb, 8);
    }
}

/**
 * Decode 4x4 block
 */
static inline void decode4x4(GetBitContext *gb, uint8_t *dst, int linesize)
{
    int i, j, v[2];

    switch (get_bits(gb, 2)) {
    case 1:
        v[0] = get_bits(gb, 8);
        for (j = 0; j < 4; j++)
            memset(dst + j * linesize, v[0], 4);
        break;
    case 2:
        v[0] = get_bits(gb, 8);
        v[1] = get_bits(gb, 8);
        for (j = 2; j >= 0; j -= 2) {
            for (i = 0; i < 4; i++)
                dst[j * linesize + i] = v[get_bits1(gb)];
            for (i = 0; i < 4; i++)
                dst[(j + 1) * linesize + i] = v[get_bits1(gb)];
        }
        break;
    case 3:
        for (j = 0; j < 4; j += 2)
            for (i = 0; i < 4; i += 2)
                decode2x2(gb, dst + j * linesize + i, linesize);
    }
}

/**
 * Decode 8x8 block
 */
static inline void decode8x8(GetBitContext *gb, uint8_t *dst, int linesize,
                             BlockDSPContext *bdsp)
{
    int i, j, v[2];

    switch (get_bits(gb, 2)) {
    case 1:
        v[0] = get_bits(gb, 8);
        bdsp->fill_block_tab[1](dst, v[0], linesize, 8);
        break;
    case 2:
        v[0] = get_bits(gb, 8);
        v[1] = get_bits(gb, 8);
        for (j = 7; j >= 0; j--)
            for (i = 0; i < 8; i++)
                dst[j * linesize + i] = v[get_bits1(gb)];
        break;
    case 3:
        for (j = 0; j < 8; j += 4)
            for (i = 0; i < 8; i += 4)
                decode4x4(gb, dst + j * linesize + i, linesize);
    }
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    JvContext *s = avctx->priv_data;
    int buf_size = avpkt->size;
    const uint8_t *buf = avpkt->data;
    const uint8_t *buf_end = buf + buf_size;
    int video_size, video_type, i, j, ret;

    video_size = AV_RL32(buf);
    video_type = buf[4];
    buf += 5;

    if (video_size) {
        if ((ret = ff_reget_buffer(avctx, s->frame)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
            return ret;
        }

        if (video_type == 0 || video_type == 1) {
            GetBitContext gb;
            init_get_bits(&gb, buf, 8 * FFMIN(video_size, buf_end - buf));

            for (j = 0; j < avctx->height; j += 8)
                for (i = 0; i < avctx->width; i += 8)
                    decode8x8(&gb,
                              s->frame->data[0] + j * s->frame->linesize[0] + i,
                              s->frame->linesize[0], &s->bdsp);

            buf += video_size;
        } else if (video_type == 2) {
            if (buf + 1 <= buf_end) {
                int v = *buf++;
                for (j = 0; j < avctx->height; j++)
                    memset(s->frame->data[0] + j * s->frame->linesize[0],
                           v, avctx->width);
            }
        } else {
            av_log(avctx, AV_LOG_WARNING,
                   "unsupported frame type %i\n", video_type);
            return AVERROR_INVALIDDATA;
        }
    }

    if (buf < buf_end) {
        for (i = 0; i < AVPALETTE_COUNT && buf + 3 <= buf_end; i++) {
            s->palette[i] = AV_RB24(buf) << 2;
            buf += 3;
        }
        s->palette_has_changed = 1;
    }

    if (video_size) {
        s->frame->key_frame           = 1;
        s->frame->pict_type           = AV_PICTURE_TYPE_I;
        s->frame->palette_has_changed = s->palette_has_changed;
        s->palette_has_changed        = 0;
        memcpy(s->frame->data[1], s->palette, AVPALETTE_SIZE);

        if ((ret = av_frame_ref(data, s->frame)) < 0)
            return ret;
        *got_frame = 1;
    }

    return buf_size;
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    JvContext *s = avctx->priv_data;

    av_frame_free(&s->frame);

    return 0;
}

AVCodec ff_jv_decoder = {
    .name           = "jv",
    .long_name      = NULL_IF_CONFIG_SMALL("Bitmap Brothers JV video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_JV,
    .priv_data_size = sizeof(JvContext),
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
