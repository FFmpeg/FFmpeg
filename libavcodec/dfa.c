/*
 * Chronomaster DFA Video Decoder
 * Copyright (c) 2011 Konstantin Shishkov
 * based on work by Vladimir "VAG" Gneushev
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

#include <inttypes.h>

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"

typedef struct DfaContext {
    uint32_t pal[256];
    uint8_t *frame_buf;
} DfaContext;

static av_cold int dfa_decode_init(AVCodecContext *avctx)
{
    DfaContext *s = avctx->priv_data;

    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    if (!avctx->width || !avctx->height || FFMAX(avctx->width, avctx->height) >= (1<<16))
        return AVERROR_INVALIDDATA;

    av_assert0(av_image_check_size(avctx->width, avctx->height, 0, avctx) >= 0);

    s->frame_buf = av_mallocz(avctx->width * avctx->height);
    if (!s->frame_buf)
        return AVERROR(ENOMEM);

    return 0;
}

static int decode_copy(GetByteContext *gb, uint8_t *frame, int width, int height)
{
    const int size = width * height;

    if (bytestream2_get_buffer(gb, frame, size) != size)
        return AVERROR_INVALIDDATA;
    return 0;
}

static int decode_tsw1(GetByteContext *gb, uint8_t *frame, int width, int height)
{
    const uint8_t *frame_start = frame;
    const uint8_t *frame_end   = frame + width * height;
    int mask = 0x10000, bitbuf = 0;
    int v, count;
    unsigned segments;
    unsigned offset;

    segments = bytestream2_get_le32(gb);
    offset   = bytestream2_get_le32(gb);
    if (segments == 0 && offset == frame_end - frame)
        return 0; // skip frame
    if (frame_end - frame <= offset)
        return AVERROR_INVALIDDATA;
    frame += offset;
    while (segments--) {
        if (bytestream2_get_bytes_left(gb) < 2)
            return AVERROR_INVALIDDATA;
        if (mask == 0x10000) {
            bitbuf = bytestream2_get_le16u(gb);
            mask = 1;
        }
        if (frame_end - frame < 2)
            return AVERROR_INVALIDDATA;
        if (bitbuf & mask) {
            v = bytestream2_get_le16(gb);
            offset = (v & 0x1FFF) << 1;
            count = ((v >> 13) + 2) << 1;
            if (frame - frame_start < offset || frame_end - frame < count)
                return AVERROR_INVALIDDATA;
            av_memcpy_backptr(frame, offset, count);
            frame += count;
        } else {
            *frame++ = bytestream2_get_byte(gb);
            *frame++ = bytestream2_get_byte(gb);
        }
        mask <<= 1;
    }

    return 0;
}

static int decode_dsw1(GetByteContext *gb, uint8_t *frame, int width, int height)
{
    const uint8_t *frame_start = frame;
    const uint8_t *frame_end   = frame + width * height;
    int mask = 0x10000, bitbuf = 0;
    int v, offset, count, segments;

    segments = bytestream2_get_le16(gb);
    while (segments--) {
        if (bytestream2_get_bytes_left(gb) < 2)
            return AVERROR_INVALIDDATA;
        if (mask == 0x10000) {
            bitbuf = bytestream2_get_le16u(gb);
            mask = 1;
        }
        if (frame_end - frame < 2)
            return AVERROR_INVALIDDATA;
        if (bitbuf & mask) {
            v = bytestream2_get_le16(gb);
            offset = (v & 0x1FFF) << 1;
            count = ((v >> 13) + 2) << 1;
            if (frame - frame_start < offset || frame_end - frame < count)
                return AVERROR_INVALIDDATA;
            av_memcpy_backptr(frame, offset, count);
            frame += count;
        } else if (bitbuf & (mask << 1)) {
            frame += bytestream2_get_le16(gb);
        } else {
            *frame++ = bytestream2_get_byte(gb);
            *frame++ = bytestream2_get_byte(gb);
        }
        mask <<= 2;
    }

    return 0;
}

static int decode_dds1(GetByteContext *gb, uint8_t *frame, int width, int height)
{
    const uint8_t *frame_start = frame;
    const uint8_t *frame_end   = frame + width * height;
    int mask = 0x10000, bitbuf = 0;
    int i, v, offset, count, segments;

    if ((width | height) & 1)
        return AVERROR_INVALIDDATA;
    segments = bytestream2_get_le16(gb);
    while (segments--) {
        if (bytestream2_get_bytes_left(gb) < 2)
            return AVERROR_INVALIDDATA;
        if (mask == 0x10000) {
            bitbuf = bytestream2_get_le16u(gb);
            mask = 1;
        }

        if (bitbuf & mask) {
            v = bytestream2_get_le16(gb);
            offset = (v & 0x1FFF) << 2;
            count = ((v >> 13) + 2) << 1;
            if (frame - frame_start < offset || frame_end - frame < count*2 + width)
                return AVERROR_INVALIDDATA;
            for (i = 0; i < count; i++) {
                frame[0] = frame[1] =
                frame[width] = frame[width + 1] = frame[-offset];

                frame += 2;
            }
        } else if (bitbuf & (mask << 1)) {
            v = bytestream2_get_le16(gb)*2;
            if (frame - frame_end < v)
                return AVERROR_INVALIDDATA;
            frame += v;
        } else {
            if (width < 4 || frame_end - frame < width + 4)
                return AVERROR_INVALIDDATA;
            frame[0] = frame[1] =
            frame[width] = frame[width + 1] =  bytestream2_get_byte(gb);
            frame += 2;
            frame[0] = frame[1] =
            frame[width] = frame[width + 1] =  bytestream2_get_byte(gb);
            frame += 2;
        }
        mask <<= 2;
    }

    return 0;
}

static int decode_bdlt(GetByteContext *gb, uint8_t *frame, int width, int height)
{
    uint8_t *line_ptr;
    int count, lines, segments;

    count = bytestream2_get_le16(gb);
    if (count >= height)
        return AVERROR_INVALIDDATA;
    frame += width * count;
    lines = bytestream2_get_le16(gb);
    if (count + lines > height)
        return AVERROR_INVALIDDATA;

    while (lines--) {
        if (bytestream2_get_bytes_left(gb) < 1)
            return AVERROR_INVALIDDATA;
        line_ptr = frame;
        frame += width;
        segments = bytestream2_get_byteu(gb);
        while (segments--) {
            if (frame - line_ptr <= bytestream2_peek_byte(gb))
                return AVERROR_INVALIDDATA;
            line_ptr += bytestream2_get_byte(gb);
            count = (int8_t)bytestream2_get_byte(gb);
            if (count >= 0) {
                if (frame - line_ptr < count)
                    return AVERROR_INVALIDDATA;
                if (bytestream2_get_buffer(gb, line_ptr, count) != count)
                    return AVERROR_INVALIDDATA;
            } else {
                count = -count;
                if (frame - line_ptr < count)
                    return AVERROR_INVALIDDATA;
                memset(line_ptr, bytestream2_get_byte(gb), count);
            }
            line_ptr += count;
        }
    }

    return 0;
}

static int decode_wdlt(GetByteContext *gb, uint8_t *frame, int width, int height)
{
    const uint8_t *frame_end   = frame + width * height;
    uint8_t *line_ptr;
    int count, i, v, lines, segments;
    int y = 0;

    lines = bytestream2_get_le16(gb);
    if (lines > height)
        return AVERROR_INVALIDDATA;

    while (lines--) {
        if (bytestream2_get_bytes_left(gb) < 2)
            return AVERROR_INVALIDDATA;
        segments = bytestream2_get_le16u(gb);
        while ((segments & 0xC000) == 0xC000) {
            unsigned skip_lines = -(int16_t)segments;
            int64_t delta = -((int16_t)segments * (int64_t)width);
            if (frame_end - frame <= delta || y + lines + skip_lines > height)
                return AVERROR_INVALIDDATA;
            frame    += delta;
            y        += skip_lines;
            segments = bytestream2_get_le16(gb);
        }

        if (frame_end <= frame)
            return AVERROR_INVALIDDATA;
        if (segments & 0x8000) {
            frame[width - 1] = segments & 0xFF;
            segments = bytestream2_get_le16(gb);
        }
        line_ptr = frame;
        if (frame_end - frame < width)
            return AVERROR_INVALIDDATA;
        frame += width;
        y++;
        while (segments--) {
            if (frame - line_ptr <= bytestream2_peek_byte(gb))
                return AVERROR_INVALIDDATA;
            line_ptr += bytestream2_get_byte(gb);
            count = (int8_t)bytestream2_get_byte(gb);
            if (count >= 0) {
                if (frame - line_ptr < count * 2)
                    return AVERROR_INVALIDDATA;
                if (bytestream2_get_buffer(gb, line_ptr, count * 2) != count * 2)
                    return AVERROR_INVALIDDATA;
                line_ptr += count * 2;
            } else {
                count = -count;
                if (frame - line_ptr < count * 2)
                    return AVERROR_INVALIDDATA;
                v = bytestream2_get_le16(gb);
                for (i = 0; i < count; i++)
                    bytestream_put_le16(&line_ptr, v);
            }
        }
    }

    return 0;
}

static int decode_tdlt(GetByteContext *gb, uint8_t *frame, int width, int height)
{
    const uint8_t *frame_end = frame + width * height;
    uint32_t segments = bytestream2_get_le32(gb);
    int skip, copy;

    while (segments--) {
        if (bytestream2_get_bytes_left(gb) < 2)
            return AVERROR_INVALIDDATA;
        copy = bytestream2_get_byteu(gb) * 2;
        skip = bytestream2_get_byteu(gb) * 2;
        if (frame_end - frame < copy + skip ||
            bytestream2_get_bytes_left(gb) < copy)
            return AVERROR_INVALIDDATA;
        frame += skip;
        bytestream2_get_buffer(gb, frame, copy);
        frame += copy;
    }

    return 0;
}

static int decode_blck(GetByteContext *gb, uint8_t *frame, int width, int height)
{
    memset(frame, 0, width * height);
    return 0;
}


typedef int (*chunk_decoder)(GetByteContext *gb, uint8_t *frame, int width, int height);

static const chunk_decoder decoder[8] = {
    decode_copy, decode_tsw1, decode_bdlt, decode_wdlt,
    decode_tdlt, decode_dsw1, decode_blck, decode_dds1,
};

static const char chunk_name[8][5] = {
    "COPY", "TSW1", "BDLT", "WDLT", "TDLT", "DSW1", "BLCK", "DDS1"
};

static int dfa_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *avpkt)
{
    DfaContext *s = avctx->priv_data;
    GetByteContext gb;
    const uint8_t *buf = avpkt->data;
    uint32_t chunk_type, chunk_size;
    uint8_t *dst;
    int ret;
    int i, pal_elems;
    int version = avctx->extradata_size==2 ? AV_RL16(avctx->extradata) : 0;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    bytestream2_init(&gb, avpkt->data, avpkt->size);
    while (bytestream2_get_bytes_left(&gb) > 0) {
        if (bytestream2_get_bytes_left(&gb) < 12)
            return AVERROR_INVALIDDATA;
        bytestream2_skip(&gb, 4);
        chunk_size = bytestream2_get_le32(&gb);
        chunk_type = bytestream2_get_le32(&gb);
        if (!chunk_type)
            break;
        if (chunk_type == 1) {
            pal_elems = FFMIN(chunk_size / 3, 256);
            for (i = 0; i < pal_elems; i++) {
                s->pal[i] = bytestream2_get_be24(&gb) << 2;
                s->pal[i] |= 0xFFU << 24 | (s->pal[i] >> 6) & 0x30303;
            }
#if FF_API_PALETTE_HAS_CHANGED
FF_DISABLE_DEPRECATION_WARNINGS
            frame->palette_has_changed = 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        } else if (chunk_type <= 9) {
            if (decoder[chunk_type - 2](&gb, s->frame_buf, avctx->width, avctx->height)) {
                av_log(avctx, AV_LOG_ERROR, "Error decoding %s chunk\n",
                       chunk_name[chunk_type - 2]);
                return AVERROR_INVALIDDATA;
            }
        } else {
            av_log(avctx, AV_LOG_WARNING,
                   "Ignoring unknown chunk type %"PRIu32"\n",
                   chunk_type);
        }
        buf += chunk_size;
    }

    buf = s->frame_buf;
    dst = frame->data[0];
    if (version == 0x100) {
        for (i = 0; i < avctx->height; i++) {
            int j;
            const uint8_t *buf1 = buf + (i&3)*(avctx->width/4) + (i/4)*avctx->width;
            int stride = (avctx->height/4)*avctx->width;
            for(j = 0; j < avctx->width/4; j++) {
                dst[4*j+0] = buf1[j + 0*stride];
                dst[4*j+1] = buf1[j + 1*stride];
                dst[4*j+2] = buf1[j + 2*stride];
                dst[4*j+3] = buf1[j + 3*stride];
            }
            j *= 4;
            for(; j < avctx->width; j++) {
                dst[j] = buf1[(j/4) + (j&3)*stride];
            }
            dst += frame->linesize[0];
        }
    } else
        av_image_copy_plane(dst, frame->linesize[0], buf, avctx->width,
                            avctx->width, avctx->height);

    memcpy(frame->data[1], s->pal, sizeof(s->pal));

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int dfa_decode_end(AVCodecContext *avctx)
{
    DfaContext *s = avctx->priv_data;

    av_freep(&s->frame_buf);

    return 0;
}

const FFCodec ff_dfa_decoder = {
    .p.name         = "dfa",
    CODEC_LONG_NAME("Chronomaster DFA"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_DFA,
    .priv_data_size = sizeof(DfaContext),
    .init           = dfa_decode_init,
    .close          = dfa_decode_end,
    FF_CODEC_DECODE_CB(dfa_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
