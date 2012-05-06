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

#include "avcodec.h"
#include "bytestream.h"
#include "libavutil/lzo.h" // for av_memcpy_backptr

typedef struct DfaContext {
    AVFrame pic;

    uint32_t pal[256];
    uint8_t *frame_buf;
} DfaContext;

static av_cold int dfa_decode_init(AVCodecContext *avctx)
{
    DfaContext *s = avctx->priv_data;

    avctx->pix_fmt = PIX_FMT_PAL8;

    s->frame_buf = av_mallocz(avctx->width * avctx->height + AV_LZO_OUTPUT_PADDING);
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
    int v, count, segments;
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
            // can't use av_memcpy_backptr() since it can overwrite following pixels
            for (v = 0; v < count; v++)
                frame[v] = frame[v - offset];
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

    segments = bytestream2_get_le16(gb);
    while (segments--) {
        if (bytestream2_get_bytes_left(gb) < 2)
            return AVERROR_INVALIDDATA;
        if (mask == 0x10000) {
            bitbuf = bytestream2_get_le16u(gb);
            mask = 1;
        }
        if (frame_end - frame < width + 2)
            return AVERROR_INVALIDDATA;
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
            frame += bytestream2_get_le16(gb) * 2;
        } else {
            if (frame_end - frame < width + 2)
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
            unsigned delta = -((int16_t)segments * width);
            if (frame_end - frame <= delta || y + lines + skip_lines > height)
                return AVERROR_INVALIDDATA;
            frame    += delta;
            y        += skip_lines;
            segments = bytestream2_get_le16(gb);
        }
        if (frame_end <= frame)
            return -1;
        if (segments & 0x8000) {
            frame[width - 1] = segments & 0xFF;
            segments = bytestream2_get_le16(gb);
        }
        line_ptr = frame;
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

static int decode_unk6(GetByteContext *gb, uint8_t *frame, int width, int height)
{
    return AVERROR_PATCHWELCOME;
}

static int decode_blck(GetByteContext *gb, uint8_t *frame, int width, int height)
{
    memset(frame, 0, width * height);
    return 0;
}


typedef int (*chunk_decoder)(GetByteContext *gb, uint8_t *frame, int width, int height);

static const chunk_decoder decoder[8] = {
    decode_copy, decode_tsw1, decode_bdlt, decode_wdlt,
    decode_unk6, decode_dsw1, decode_blck, decode_dds1,
};

static const char* chunk_name[8] = {
    "COPY", "TSW1", "BDLT", "WDLT", "????", "DSW1", "BLCK", "DDS1"
};

static int dfa_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    DfaContext *s = avctx->priv_data;
    GetByteContext gb;
    const uint8_t *buf = avpkt->data;
    uint32_t chunk_type, chunk_size;
    uint8_t *dst;
    int ret;
    int i, pal_elems;

    if (s->pic.data[0])
        avctx->release_buffer(avctx, &s->pic);

    if ((ret = avctx->get_buffer(avctx, &s->pic))) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    bytestream2_init(&gb, avpkt->data, avpkt->size);
    while (bytestream2_get_bytes_left(&gb) > 0) {
        bytestream2_skip(&gb, 4);
        chunk_size = bytestream2_get_le32(&gb);
        chunk_type = bytestream2_get_le32(&gb);
        if (!chunk_type)
            break;
        if (chunk_type == 1) {
            pal_elems = FFMIN(chunk_size / 3, 256);
            for (i = 0; i < pal_elems; i++) {
                s->pal[i] = bytestream2_get_be24(&gb) << 2;
                s->pal[i] |= 0xFF << 24 | (s->pal[i] >> 6) & 0x30303;
            }
            s->pic.palette_has_changed = 1;
        } else if (chunk_type <= 9) {
            if (decoder[chunk_type - 2](&gb, s->frame_buf, avctx->width, avctx->height)) {
                av_log(avctx, AV_LOG_ERROR, "Error decoding %s chunk\n",
                       chunk_name[chunk_type - 2]);
                return AVERROR_INVALIDDATA;
            }
        } else {
            av_log(avctx, AV_LOG_WARNING, "Ignoring unknown chunk type %d\n",
                   chunk_type);
        }
        buf += chunk_size;
    }

    buf = s->frame_buf;
    dst = s->pic.data[0];
    for (i = 0; i < avctx->height; i++) {
        memcpy(dst, buf, avctx->width);
        dst += s->pic.linesize[0];
        buf += avctx->width;
    }
    memcpy(s->pic.data[1], s->pal, sizeof(s->pal));

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->pic;

    return avpkt->size;
}

static av_cold int dfa_decode_end(AVCodecContext *avctx)
{
    DfaContext *s = avctx->priv_data;

    if (s->pic.data[0])
        avctx->release_buffer(avctx, &s->pic);

    av_freep(&s->frame_buf);

    return 0;
}

AVCodec ff_dfa_decoder = {
    .name           = "dfa",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_DFA,
    .priv_data_size = sizeof(DfaContext),
    .init           = dfa_decode_init,
    .close          = dfa_decode_end,
    .decode         = dfa_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Chronomaster DFA"),
};
