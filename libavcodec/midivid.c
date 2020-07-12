/*
 * MidiVid decoder
 * Copyright (c) 2019 Paul B Mahol
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
#include "libavutil/mem.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "get_bits.h"
#include "bytestream.h"
#include "internal.h"

typedef struct MidiVidContext {
    GetByteContext gb;

    uint8_t *uncompressed;
    unsigned int uncompressed_size;
    uint8_t *skip;

    AVFrame *frame;
} MidiVidContext;

static int decode_mvdv(MidiVidContext *s, AVCodecContext *avctx, AVFrame *frame)
{
    GetByteContext *gb = &s->gb;
    GetBitContext mask;
    GetByteContext idx9;
    uint16_t nb_vectors, intra_flag;
    const uint8_t *vec;
    const uint8_t *mask_start;
    uint8_t *skip;
    uint32_t mask_size;
    int idx9bits = 0;
    int idx9val = 0;
    uint32_t nb_blocks;

    nb_vectors = bytestream2_get_le16(gb);
    intra_flag = !!bytestream2_get_le16(gb);
    if (intra_flag) {
        nb_blocks = (avctx->width / 2) * (avctx->height / 2);
    } else {
        int ret, skip_linesize, padding;

        nb_blocks = bytestream2_get_le32(gb);
        skip_linesize = avctx->width >> 1;
        mask_start = gb->buffer_start + bytestream2_tell(gb);
        mask_size = (FFALIGN(avctx->width, 32) >> 2) * (avctx->height >> 2) >> 3;
        padding = (FFALIGN(avctx->width, 32) - avctx->width) >> 2;

        if (bytestream2_get_bytes_left(gb) < mask_size)
            return AVERROR_INVALIDDATA;

        ret = init_get_bits8(&mask, mask_start, mask_size);
        if (ret < 0)
            return ret;
        bytestream2_skip(gb, mask_size);
        skip = s->skip;

        for (int y = 0; y < avctx->height >> 2; y++) {
            for (int x = 0; x < avctx->width >> 2; x++) {
                int flag = !get_bits1(&mask);

                skip[(y*2)  *skip_linesize + x*2  ] = flag;
                skip[(y*2)  *skip_linesize + x*2+1] = flag;
                skip[(y*2+1)*skip_linesize + x*2  ] = flag;
                skip[(y*2+1)*skip_linesize + x*2+1] = flag;
            }
            skip_bits_long(&mask, padding);
        }
    }

    vec = gb->buffer_start + bytestream2_tell(gb);
    if (bytestream2_get_bytes_left(gb) < nb_vectors * 12)
        return AVERROR_INVALIDDATA;
    bytestream2_skip(gb, nb_vectors * 12);
    if (nb_vectors > 256) {
        if (bytestream2_get_bytes_left(gb) < (nb_blocks + 7 * !intra_flag) / 8)
            return AVERROR_INVALIDDATA;
        bytestream2_init(&idx9, gb->buffer_start + bytestream2_tell(gb), (nb_blocks + 7 * !intra_flag) / 8);
        bytestream2_skip(gb, (nb_blocks + 7 * !intra_flag) / 8);
    }

    skip = s->skip;

    for (int y = avctx->height - 2; y >= 0; y -= 2) {
        uint8_t *dsty = frame->data[0] + y * frame->linesize[0];
        uint8_t *dstu = frame->data[1] + y * frame->linesize[1];
        uint8_t *dstv = frame->data[2] + y * frame->linesize[2];

        for (int x = 0; x < avctx->width; x += 2) {
            int idx;

            if (!intra_flag && *skip++)
                continue;
            if (bytestream2_get_bytes_left(gb) <= 0)
                return AVERROR_INVALIDDATA;
            if (nb_vectors <= 256) {
                idx = bytestream2_get_byte(gb);
            } else {
                if (idx9bits == 0) {
                    idx9val = bytestream2_get_byte(&idx9);
                    idx9bits = 8;
                }
                idx9bits--;
                idx = bytestream2_get_byte(gb) | (((idx9val >> (7 - idx9bits)) & 1) << 8);
            }
            if (idx >= nb_vectors)
                return AVERROR_INVALIDDATA;

            dsty[x  +frame->linesize[0]] = vec[idx * 12 + 0];
            dsty[x+1+frame->linesize[0]] = vec[idx * 12 + 3];
            dsty[x]                      = vec[idx * 12 + 6];
            dsty[x+1]                    = vec[idx * 12 + 9];

            dstu[x  +frame->linesize[1]] = vec[idx * 12 + 1];
            dstu[x+1+frame->linesize[1]] = vec[idx * 12 + 4];
            dstu[x]                      = vec[idx * 12 + 7];
            dstu[x+1]                    = vec[idx * 12 +10];

            dstv[x  +frame->linesize[2]] = vec[idx * 12 + 2];
            dstv[x+1+frame->linesize[2]] = vec[idx * 12 + 5];
            dstv[x]                      = vec[idx * 12 + 8];
            dstv[x+1]                    = vec[idx * 12 +11];
        }
    }

    return intra_flag;
}

static ptrdiff_t lzss_uncompress(MidiVidContext *s, GetByteContext *gb, uint8_t *dst, unsigned int size)
{
    uint8_t *dst_start = dst;
    uint8_t *dst_end = dst + size;

    for (;bytestream2_get_bytes_left(gb) >= 3;) {
        int op = bytestream2_get_le16(gb);

        for (int i = 0; i < 16; i++) {
            if (op & 1) {
                int s0 = bytestream2_get_byte(gb);
                int s1 = bytestream2_get_byte(gb);
                int offset = ((s0 & 0xF0) << 4) | s1;
                int length = (s0 & 0xF) + 3;

                if (dst + length > dst_end ||
                    dst - offset < dst_start)
                    return AVERROR_INVALIDDATA;
                if (offset > 0) {
                    for (int j = 0; j < length; j++) {
                        dst[j] = dst[j - offset];
                    }
                }
                dst += length;
            } else {
                if (dst >= dst_end)
                    return AVERROR_INVALIDDATA;
                *dst++ = bytestream2_get_byte(gb);
            }
            op >>= 1;
        }
    }

    return dst - dst_start;
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame, AVPacket *avpkt)
{
    MidiVidContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    AVFrame *frame = s->frame;
    int ret, key, uncompressed;

    if (avpkt->size <= 13)
        return AVERROR_INVALIDDATA;

    bytestream2_init(gb, avpkt->data, avpkt->size);
    bytestream2_skip(gb, 8);
    uncompressed = bytestream2_get_le32(gb);

    if ((ret = ff_reget_buffer(avctx, s->frame, 0)) < 0)
        return ret;

    if (uncompressed) {
        ret = decode_mvdv(s, avctx, frame);
    } else {
        av_fast_padded_malloc(&s->uncompressed, &s->uncompressed_size, 16LL * (avpkt->size - 12));
        if (!s->uncompressed)
            return AVERROR(ENOMEM);

        ret = lzss_uncompress(s, gb, s->uncompressed, s->uncompressed_size);
        if (ret < 0)
            return ret;
        bytestream2_init(gb, s->uncompressed, ret);
        ret = decode_mvdv(s, avctx, frame);
    }

    if (ret < 0)
        return ret;
    key = ret;

    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;

    frame->pict_type = key ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;
    frame->key_frame = key;
    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    MidiVidContext *s = avctx->priv_data;
    int ret = av_image_check_size(avctx->width, avctx->height, 0, avctx);

    if (avctx->width & 3 || avctx->height & 3)
        ret = AVERROR_INVALIDDATA;

    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid image size %dx%d.\n",
               avctx->width, avctx->height);
        return ret;
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV444P;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);
    s->skip = av_calloc(avctx->width >> 1, avctx->height >> 1);
    if (!s->skip)
        return AVERROR(ENOMEM);

    return 0;
}

static void decode_flush(AVCodecContext *avctx)
{
    MidiVidContext *s = avctx->priv_data;

    av_frame_unref(s->frame);
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    MidiVidContext *s = avctx->priv_data;

    av_frame_free(&s->frame);
    av_freep(&s->uncompressed);
    av_freep(&s->skip);

    return 0;
}

AVCodec ff_mvdv_decoder = {
    .name           = "mvdv",
    .long_name      = NULL_IF_CONFIG_SMALL("MidiVid VQ"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MVDV,
    .priv_data_size = sizeof(MidiVidContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .flush          = decode_flush,
    .close          = decode_close,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
