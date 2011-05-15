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
#include "libavutil/intreadwrite.h"
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

static int decode_copy(uint8_t *frame, int width, int height,
                       const uint8_t *src, const uint8_t *src_end)
{
    const int size = width * height;

    if (src_end - src < size)
        return -1;
    bytestream_get_buffer(&src, frame, size);
    return 0;
}

static int decode_tsw1(uint8_t *frame, int width, int height,
                       const uint8_t *src, const uint8_t *src_end)
{
    const uint8_t *frame_start = frame;
    const uint8_t *frame_end   = frame + width * height;
    int mask = 0x10000, bitbuf = 0;
    int v, count, segments;
    unsigned offset;

    segments = bytestream_get_le32(&src);
    offset   = bytestream_get_le32(&src);
    if (frame_end - frame <= offset)
        return -1;
    frame += offset;
    while (segments--) {
        if (mask == 0x10000) {
            if (src >= src_end)
                return -1;
            bitbuf = bytestream_get_le16(&src);
            mask = 1;
        }
        if (src_end - src < 2 || frame_end - frame < 2)
            return -1;
        if (bitbuf & mask) {
            v = bytestream_get_le16(&src);
            offset = (v & 0x1FFF) << 1;
            count = ((v >> 13) + 2) << 1;
            if (frame - frame_start < offset || frame_end - frame < count)
                return -1;
            av_memcpy_backptr(frame, offset, count);
            frame += count;
        } else {
            *frame++ = *src++;
            *frame++ = *src++;
        }
        mask <<= 1;
    }

    return 0;
}

static int decode_dsw1(uint8_t *frame, int width, int height,
                       const uint8_t *src, const uint8_t *src_end)
{
    const uint8_t *frame_start = frame;
    const uint8_t *frame_end   = frame + width * height;
    int mask = 0x10000, bitbuf = 0;
    int v, offset, count, segments;

    segments = bytestream_get_le16(&src);
    while (segments--) {
        if (mask == 0x10000) {
            if (src >= src_end)
                return -1;
            bitbuf = bytestream_get_le16(&src);
            mask = 1;
        }
        if (src_end - src < 2 || frame_end - frame < 2)
            return -1;
        if (bitbuf & mask) {
            v = bytestream_get_le16(&src);
            offset = (v & 0x1FFF) << 1;
            count = ((v >> 13) + 2) << 1;
            if (frame - frame_start < offset || frame_end - frame < count)
                return -1;
            // can't use av_memcpy_backptr() since it can overwrite following pixels
            for (v = 0; v < count; v++)
                frame[v] = frame[v - offset];
            frame += count;
        } else if (bitbuf & (mask << 1)) {
            frame += bytestream_get_le16(&src);
        } else {
            *frame++ = *src++;
            *frame++ = *src++;
        }
        mask <<= 2;
    }

    return 0;
}

static int decode_dds1(uint8_t *frame, int width, int height,
                       const uint8_t *src, const uint8_t *src_end)
{
    const uint8_t *frame_start = frame;
    const uint8_t *frame_end   = frame + width * height;
    int mask = 0x10000, bitbuf = 0;
    int i, v, offset, count, segments;

    segments = bytestream_get_le16(&src);
    while (segments--) {
        if (mask == 0x10000) {
            if (src >= src_end)
                return -1;
            bitbuf = bytestream_get_le16(&src);
            mask = 1;
        }
        if (src_end - src < 2 || frame_end - frame < 2)
            return -1;
        if (bitbuf & mask) {
            v = bytestream_get_le16(&src);
            offset = (v & 0x1FFF) << 2;
            count = ((v >> 13) + 2) << 1;
            if (frame - frame_start < offset || frame_end - frame < count*2 + width)
                return -1;
            for (i = 0; i < count; i++) {
                frame[0] = frame[1] =
                frame[width] = frame[width + 1] = frame[-offset];

                frame += 2;
            }
        } else if (bitbuf & (mask << 1)) {
            frame += bytestream_get_le16(&src) * 2;
        } else {
            frame[0] = frame[1] =
            frame[width] = frame[width + 1] =  *src++;
            frame += 2;
            frame[0] = frame[1] =
            frame[width] = frame[width + 1] =  *src++;
            frame += 2;
        }
        mask <<= 2;
    }

    return 0;
}

static int decode_bdlt(uint8_t *frame, int width, int height,
                       const uint8_t *src, const uint8_t *src_end)
{
    uint8_t *line_ptr;
    int count, lines, segments;

    count = bytestream_get_le16(&src);
    if (count >= height)
        return -1;
    frame += width * count;
    lines = bytestream_get_le16(&src);
    if (count + lines > height || src >= src_end)
        return -1;

    while (lines--) {
        line_ptr = frame;
        frame += width;
        segments = *src++;
        while (segments--) {
            if (src_end - src < 3)
                return -1;
            if (frame - line_ptr <= *src)
                return -1;
            line_ptr += *src++;
            count = (int8_t)*src++;
            if (count >= 0) {
                if (frame - line_ptr < count || src_end - src < count)
                    return -1;
                bytestream_get_buffer(&src, line_ptr, count);
            } else {
                count = -count;
                if (frame - line_ptr < count || src >= src_end)
                    return -1;
                memset(line_ptr, *src++, count);
            }
            line_ptr += count;
        }
    }

    return 0;
}

static int decode_wdlt(uint8_t *frame, int width, int height,
                       const uint8_t *src, const uint8_t *src_end)
{
    const uint8_t *frame_end   = frame + width * height;
    uint8_t *line_ptr;
    int count, i, v, lines, segments;

    lines = bytestream_get_le16(&src);
    if (lines > height || src >= src_end)
        return -1;

    while (lines--) {
        segments = bytestream_get_le16(&src);
        while ((segments & 0xC000) == 0xC000) {
            unsigned delta = -((int16_t)segments * width);
            if (frame_end - frame <= delta)
                return -1;
            frame    += delta;
            segments = bytestream_get_le16(&src);
        }
        if (segments & 0x8000) {
            frame[width - 1] = segments & 0xFF;
            segments = bytestream_get_le16(&src);
        }
        line_ptr = frame;
        frame += width;
        while (segments--) {
            if (src_end - src < 2)
                return -1;
            if (frame - line_ptr <= *src)
                return -1;
            line_ptr += *src++;
            count = (int8_t)*src++;
            if (count >= 0) {
                if (frame - line_ptr < count*2 || src_end - src < count*2)
                    return -1;
                bytestream_get_buffer(&src, line_ptr, count*2);
                line_ptr += count * 2;
            } else {
                count = -count;
                if (frame - line_ptr < count*2 || src_end - src < 2)
                    return -1;
                v = bytestream_get_le16(&src);
                for (i = 0; i < count; i++)
                    bytestream_put_le16(&line_ptr, v);
            }
        }
    }

    return 0;
}

static int decode_unk6(uint8_t *frame, int width, int height,
                       const uint8_t *src, const uint8_t *src_end)
{
    return -1;
}

static int decode_blck(uint8_t *frame, int width, int height,
                       const uint8_t *src, const uint8_t *src_end)
{
    memset(frame, 0, width * height);
    return 0;
}


typedef int (*chunk_decoder)(uint8_t *frame, int width, int height,
                             const uint8_t *src, const uint8_t *src_end);

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
    const uint8_t *buf = avpkt->data;
    const uint8_t *buf_end = avpkt->data + avpkt->size;
    const uint8_t *tmp_buf;
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

    while (buf < buf_end) {
        chunk_size = AV_RL32(buf + 4);
        chunk_type = AV_RL32(buf + 8);
        buf += 12;
        if (buf_end - buf < chunk_size) {
            av_log(avctx, AV_LOG_ERROR, "Chunk size is too big (%d bytes)\n", chunk_size);
            return -1;
        }
        if (!chunk_type)
            break;
        if (chunk_type == 1) {
            pal_elems = FFMIN(chunk_size / 3, 256);
            tmp_buf = buf;
            for (i = 0; i < pal_elems; i++) {
                s->pal[i] = bytestream_get_be24(&tmp_buf) << 2;
                s->pal[i] |= (s->pal[i] >> 6) & 0x333;
            }
            s->pic.palette_has_changed = 1;
        } else if (chunk_type <= 9) {
            if (decoder[chunk_type - 2](s->frame_buf, avctx->width, avctx->height,
                                        buf, buf + chunk_size)) {
                av_log(avctx, AV_LOG_ERROR, "Error decoding %s chunk\n",
                       chunk_name[chunk_type - 2]);
                return -1;
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
    "dfa",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_DFA,
    sizeof(DfaContext),
    dfa_decode_init,
    NULL,
    dfa_decode_end,
    dfa_decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Chronomaster DFA"),
};
