/*
 * Discworld II BMV video decoder
 * Copyright (c) 2011 Konstantin Shishkov
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

#include "libavutil/avassert.h"
#include "libavutil/common.h"

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

enum BMVFlags{
    BMV_NOP = 0,
    BMV_END,
    BMV_DELTA,
    BMV_INTRA,

    BMV_SCROLL  = 0x04,
    BMV_PALETTE = 0x08,
    BMV_COMMAND = 0x10,
    BMV_AUDIO   = 0x20,
    BMV_EXT     = 0x40,
    BMV_PRINT   = 0x80
};

#define SCREEN_WIDE 640
#define SCREEN_HIGH 429

typedef struct BMVDecContext {
    AVCodecContext *avctx;

    uint8_t *frame, frame_base[SCREEN_WIDE * (SCREEN_HIGH + 1)];
    uint32_t pal[256];
    const uint8_t *stream;
} BMVDecContext;

#define NEXT_BYTE(v) (v) = forward ? (v) + 1 : (v) - 1;

static int decode_bmv_frame(const uint8_t *source, int src_len, uint8_t *frame, int frame_off)
{
    unsigned val, saved_val = 0;
    int tmplen = src_len;
    const uint8_t *src, *source_end = source + src_len;
    uint8_t *frame_end = frame + SCREEN_WIDE * SCREEN_HIGH;
    uint8_t *dst, *dst_end;
    int len, mask;
    int forward = (frame_off <= -SCREEN_WIDE) || (frame_off >= 0);
    int read_two_nibbles, flag;
    int advance_mode;
    int mode = 0;
    int i;

    if (src_len <= 0)
        return AVERROR_INVALIDDATA;

    if (forward) {
        src = source;
        dst = frame;
        dst_end = frame_end;
    } else {
        src = source + src_len - 1;
        dst = frame_end - 1;
        dst_end = frame - 1;
    }
    for (;;) {
        int shift = 0;
        flag = 0;

        /* The mode/len decoding is a bit strange:
         * values are coded as variable-length codes with nibble units,
         * code end is signalled by two top bits in the nibble being nonzero.
         * And since data is bytepacked and we read two nibbles at a time,
         * we may get a nibble belonging to the next code.
         * Hence this convoluted loop.
         */
        if (!mode || (tmplen == 4)) {
            if (src < source || src >= source_end)
                return AVERROR_INVALIDDATA;
            val = *src;
            read_two_nibbles = 1;
        } else {
            val = saved_val;
            read_two_nibbles = 0;
        }
        if (!(val & 0xC)) {
            for (;;) {
                if(shift>22)
                    return -1;
                if (!read_two_nibbles) {
                    if (src < source || src >= source_end)
                        return AVERROR_INVALIDDATA;
                    shift += 2;
                    val |= (unsigned)*src << shift;
                    if (*src & 0xC)
                        break;
                }
                // two upper bits of the nibble is zero,
                // so shift top nibble value down into their place
                read_two_nibbles = 0;
                shift += 2;
                mask = (1 << shift) - 1;
                val = ((val >> 2) & ~mask) | (val & mask);
                NEXT_BYTE(src);
                if ((val & (0xC << shift))) {
                    flag = 1;
                    break;
                }
            }
        } else if (mode) {
            flag = tmplen != 4;
        }
        if (flag) {
            tmplen = 4;
        } else {
            saved_val = val >> (4 + shift);
            tmplen = 0;
            val &= (1 << (shift + 4)) - 1;
            NEXT_BYTE(src);
        }
        advance_mode = val & 1;
        len = (val >> 1) - 1;
        av_assert0(len>0);
        mode += 1 + advance_mode;
        if (mode >= 4)
            mode -= 3;
        if (len <= 0 || FFABS(dst_end - dst) < len)
            return AVERROR_INVALIDDATA;
        switch (mode) {
        case 1:
            if (forward) {
                if (dst - frame + SCREEN_WIDE < frame_off ||
                        dst - frame + SCREEN_WIDE + frame_off < 0 ||
                        frame_end - dst < frame_off + len ||
                        frame_end - dst < len)
                    return AVERROR_INVALIDDATA;
                for (i = 0; i < len; i++)
                    dst[i] = dst[frame_off + i];
                dst += len;
            } else {
                dst -= len;
                if (dst - frame + SCREEN_WIDE < frame_off ||
                        dst - frame + SCREEN_WIDE + frame_off < 0 ||
                        frame_end - dst < frame_off + len ||
                        frame_end - dst < len)
                    return AVERROR_INVALIDDATA;
                for (i = len - 1; i >= 0; i--)
                    dst[i] = dst[frame_off + i];
            }
            break;
        case 2:
            if (forward) {
                if (source + src_len - src < len)
                    return AVERROR_INVALIDDATA;
                memcpy(dst, src, len);
                dst += len;
                src += len;
            } else {
                if (src - source < len)
                    return AVERROR_INVALIDDATA;
                dst -= len;
                src -= len;
                memcpy(dst, src, len);
            }
            break;
        case 3:
            val = forward ? dst[-1] : dst[1];
            if (forward) {
                memset(dst, val, len);
                dst += len;
            } else {
                dst -= len;
                memset(dst, val, len);
            }
            break;
        }
        if (dst == dst_end)
            return 0;
    }
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *pkt)
{
    BMVDecContext * const c = avctx->priv_data;
    AVFrame *frame = data;
    int type, scr_off;
    int i, ret;
    uint8_t *srcptr, *outptr;

    c->stream = pkt->data;
    type = bytestream_get_byte(&c->stream);
    if (type & BMV_AUDIO) {
        int blobs = bytestream_get_byte(&c->stream);
        if (pkt->size < blobs * 65 + 2) {
            av_log(avctx, AV_LOG_ERROR, "Audio data doesn't fit in frame\n");
            return AVERROR_INVALIDDATA;
        }
        c->stream += blobs * 65;
    }
    if (type & BMV_COMMAND) {
        int command_size = (type & BMV_PRINT) ? 8 : 10;
        if (c->stream - pkt->data + command_size > pkt->size) {
            av_log(avctx, AV_LOG_ERROR, "Command data doesn't fit in frame\n");
            return AVERROR_INVALIDDATA;
        }
        c->stream += command_size;
    }
    if (type & BMV_PALETTE) {
        if (c->stream - pkt->data > pkt->size - 768) {
            av_log(avctx, AV_LOG_ERROR, "Palette data doesn't fit in frame\n");
            return AVERROR_INVALIDDATA;
        }
        for (i = 0; i < 256; i++)
            c->pal[i] = 0xFFU << 24 | bytestream_get_be24(&c->stream);
    }
    if (type & BMV_SCROLL) {
        if (c->stream - pkt->data > pkt->size - 2) {
            av_log(avctx, AV_LOG_ERROR, "Screen offset data doesn't fit in frame\n");
            return AVERROR_INVALIDDATA;
        }
        scr_off = (int16_t)bytestream_get_le16(&c->stream);
    } else if ((type & BMV_INTRA) == BMV_INTRA) {
        scr_off = -640;
    } else {
        scr_off = 0;
    }

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    if (decode_bmv_frame(c->stream, pkt->size - (c->stream - pkt->data), c->frame, scr_off)) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding frame data\n");
        return AVERROR_INVALIDDATA;
    }

    memcpy(frame->data[1], c->pal, AVPALETTE_SIZE);
    frame->palette_has_changed = type & BMV_PALETTE;

    outptr = frame->data[0];
    srcptr = c->frame;

    for (i = 0; i < avctx->height; i++) {
        memcpy(outptr, srcptr, avctx->width);
        srcptr += avctx->width;
        outptr += frame->linesize[0];
    }

    *got_frame = 1;

    /* always report that the buffer was completely consumed */
    return pkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    BMVDecContext * const c = avctx->priv_data;

    c->avctx = avctx;
    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    if (avctx->width != SCREEN_WIDE || avctx->height != SCREEN_HIGH) {
        av_log(avctx, AV_LOG_ERROR, "Invalid dimension %dx%d\n", avctx->width, avctx->height);
        return AVERROR_INVALIDDATA;
    }

    c->frame = c->frame_base + 640;

    return 0;
}

const AVCodec ff_bmv_video_decoder = {
    .name           = "bmv_video",
    .long_name      = NULL_IF_CONFIG_SMALL("Discworld II BMV video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_BMV_VIDEO,
    .priv_data_size = sizeof(BMVDecContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
