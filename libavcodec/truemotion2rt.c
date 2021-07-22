/*
 * Duck TrueMotion 2.0 Real Time decoder
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

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"

typedef struct TrueMotion2RTContext {
    GetBitContext gb;
    int delta_size;
    int hscale;
} TrueMotion2RTContext;

static const int16_t delta_tab2[] = {
    5, -7, 36, -36,
};

static const int16_t delta_tab3[] = {
    2, -3, 8, -8, 18, -18, 36, -36,
};

static const int16_t delta_tab4[] = {
    1, -1, 2, -3, 8, -8, 18, -18, 36, -36, 54, -54, 96, -96, 144, -144,
};

static const int16_t *const delta_tabs[] = {
    delta_tab2, delta_tab3, delta_tab4,
};

/* Returns the number of bytes consumed from the bytestream, or
 * AVERROR_INVALIDDATA if there was an error while decoding the header. */
static int truemotion2rt_decode_header(AVCodecContext *avctx, const AVPacket *avpkt)
{
    TrueMotion2RTContext *s = avctx->priv_data;
    int header_size;
    uint8_t header_buffer[128] = { 0 };  /* logical maximum header size */
    const uint8_t *buf = avpkt->data;
    int size = avpkt->size;
    int width, height;
    int ret, i;

    if (size < 1) {
        av_log(avctx, AV_LOG_ERROR, "input packet too small (%d)\n", size);
        return AVERROR_INVALIDDATA;
    }

    header_size = ((buf[0] >> 5) | (buf[0] << 3)) & 0x7f;
    if (header_size < 10) {
        av_log(avctx, AV_LOG_ERROR, "invalid header size (%d)\n", header_size);
        return AVERROR_INVALIDDATA;
    }

    if (header_size + 1 > size) {
        av_log(avctx, AV_LOG_ERROR, "input packet too small (%d)\n", size);
        return AVERROR_INVALIDDATA;
    }

    /* unscramble the header bytes with a XOR operation */
    for (i = 1; i < header_size; i++)
        header_buffer[i - 1] = buf[i] ^ buf[i + 1];

    s->delta_size = header_buffer[1];
    s->hscale = 1 + !!header_buffer[3];
    if (s->delta_size < 2 || s->delta_size > 4)
        return AVERROR_INVALIDDATA;

    height = AV_RL16(header_buffer + 5);
    width  = AV_RL16(header_buffer + 7);

    ret = ff_set_dimensions(avctx, width, height);
    if (ret < 0)
        return ret;

    av_log(avctx, AV_LOG_DEBUG, "Header size: %d\n", header_size);
    return header_size;
}

static int truemotion2rt_decode_frame(AVCodecContext *avctx, void *data,
                                      int *got_frame, AVPacket *avpkt)
{
    TrueMotion2RTContext *s = avctx->priv_data;
    AVFrame * const p = data;
    GetBitContext *gb = &s->gb;
    uint8_t *dst;
    int x, y, delta_mode;
    int ret;

    ret = truemotion2rt_decode_header(avctx, avpkt);
    if (ret < 0)
        return ret;

    if ((avctx->width + s->hscale - 1)/ s->hscale * avctx->height * s->delta_size > avpkt->size * 8LL * 4)
        return AVERROR_INVALIDDATA;

    ret = init_get_bits8(gb, avpkt->data + ret, avpkt->size - ret);
    if (ret < 0)
        return ret;

    ret = ff_get_buffer(avctx, p, 0);
    if (ret < 0)
        return ret;

    skip_bits(gb, 32);
    delta_mode = s->delta_size - 2;
    dst = p->data[0];
    for (y = 0; y < avctx->height; y++) {
        int diff = 0;
        for (x = 0; x < avctx->width; x += s->hscale) {
            diff  += delta_tabs[delta_mode][get_bits(gb, s->delta_size)];
            dst[x] = av_clip_uint8((y ? dst[x - p->linesize[0]] : 0) + diff);
        }
        dst += p->linesize[0];
    }

    if (s->hscale > 1) {
        dst = p->data[0];
        for (y = 0; y < avctx->height; y++) {
            for (x = 1; x < avctx->width; x += s->hscale)
                dst[x] = dst[x - 1];
            dst += p->linesize[0];
        }
    }

    dst = p->data[0];
    for (y = 0; y < avctx->height; y++) {
        for (x = 0; x < avctx->width; x++)
            dst[x] = av_clip_uint8(dst[x] + (dst[x] - 128) / 3);
        dst += p->linesize[0];
    }

    dst = p->data[1];
    for (y = 0; y < avctx->height >> 2; y++) {
        int diff = 0;
        for (x = 0; x < avctx->width >> 2; x += s->hscale) {
            diff  += delta_tabs[delta_mode][get_bits(gb, s->delta_size)];
            dst[x] = av_clip_uint8((y ? dst[x - p->linesize[1]] : 128) + diff);
        }
        dst += p->linesize[1];
    }

    if (s->hscale > 1) {
        dst = p->data[1];
        for (y = 0; y < avctx->height >> 2; y++) {
            for (x = 1; x < avctx->width >> 2; x += s->hscale)
                dst[x] = dst[x - 1];
            dst += p->linesize[1];
        }
    }

    dst = p->data[1];
    for (y = 0; y < avctx->height >> 2; y++) {
        for (x = 0; x < avctx->width >> 2; x++)
            dst[x] += (dst[x] - 128) / 8;
        dst += p->linesize[1];
    }

    dst = p->data[2];
    for (y = 0; y < avctx->height >> 2; y++) {
        int diff = 0;
        for (x = 0; x < avctx->width >> 2; x += s->hscale) {
            diff  += delta_tabs[delta_mode][get_bits(gb, s->delta_size)];
            dst[x] = av_clip_uint8((y ? dst[x - p->linesize[2]] : 128) + diff);
        }
        dst += p->linesize[2];
    }

    if (s->hscale > 1) {
        dst = p->data[2];
        for (y = 0; y < avctx->height >> 2; y++) {
            for (x = 1; x < avctx->width >> 2; x += s->hscale)
                dst[x] = dst[x - 1];
            dst += p->linesize[2];
        }
    }

    dst = p->data[2];
    for (y = 0; y < avctx->height >> 2; y++) {
        for (x = 0; x < avctx->width >> 2; x++)
            dst[x] += (dst[x] - 128) / 8;
        dst += p->linesize[2];
    }

    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;
    *got_frame = 1;

    return avpkt->size;
}

static av_cold int truemotion2rt_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt = AV_PIX_FMT_YUV410P;
    return 0;
}

const AVCodec ff_truemotion2rt_decoder = {
    .name           = "truemotion2rt",
    .long_name      = NULL_IF_CONFIG_SMALL("Duck TrueMotion 2.0 Real Time"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_TRUEMOTION2RT,
    .priv_data_size = sizeof(TrueMotion2RTContext),
    .init           = truemotion2rt_decode_init,
    .decode         = truemotion2rt_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
