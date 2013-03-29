/*
 * Packed Animation File video and audio decoder
 * Copyright (c) 2012 Paul B Mahol
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

#include "libavutil/intreadwrite.h"
#include "libavcodec/paf.h"
#include "bytestream.h"
#include "avcodec.h"
#include "copy_block.h"
#include "internal.h"


static const uint8_t block_sequences[16][8] =
{
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    { 2, 0, 0, 0, 0, 0, 0, 0 },
    { 5, 7, 0, 0, 0, 0, 0, 0 },
    { 5, 0, 0, 0, 0, 0, 0, 0 },
    { 6, 0, 0, 0, 0, 0, 0, 0 },
    { 5, 7, 5, 7, 0, 0, 0, 0 },
    { 5, 7, 5, 0, 0, 0, 0, 0 },
    { 5, 7, 6, 0, 0, 0, 0, 0 },
    { 5, 5, 0, 0, 0, 0, 0, 0 },
    { 3, 0, 0, 0, 0, 0, 0, 0 },
    { 6, 6, 0, 0, 0, 0, 0, 0 },
    { 2, 4, 0, 0, 0, 0, 0, 0 },
    { 2, 4, 5, 7, 0, 0, 0, 0 },
    { 2, 4, 5, 0, 0, 0, 0, 0 },
    { 2, 4, 6, 0, 0, 0, 0, 0 },
    { 2, 4, 5, 7, 5, 7, 0, 0 }
};

typedef struct PAFVideoDecContext {
    AVFrame  *pic;
    GetByteContext gb;

    int     current_frame;
    uint8_t *frame[4];
    int     frame_size;
    int     video_size;

    uint8_t *opcodes;
} PAFVideoDecContext;

static av_cold int paf_vid_close(AVCodecContext *avctx)
{
    PAFVideoDecContext *c = avctx->priv_data;
    int i;

    av_frame_free(&c->pic);

    for (i = 0; i < 4; i++)
        av_freep(&c->frame[i]);

    return 0;
}

static av_cold int paf_vid_init(AVCodecContext *avctx)
{
    PAFVideoDecContext *c = avctx->priv_data;
    int i;

    if (avctx->height & 3 || avctx->width & 3) {
        av_log(avctx, AV_LOG_ERROR, "width and height must be multiplies of 4\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    c->pic = av_frame_alloc();
    if (!c->pic)
        return AVERROR(ENOMEM);

    c->frame_size = FFALIGN(avctx->height, 256) * avctx->width;
    c->video_size = avctx->height * avctx->width;
    for (i = 0; i < 4; i++) {
        c->frame[i] = av_mallocz(c->frame_size);
        if (!c->frame[i]) {
            paf_vid_close(avctx);
            return AVERROR(ENOMEM);
        }
    }

    return 0;
}

static int get_video_page_offset(AVCodecContext *avctx, uint8_t a, uint8_t b)
{
    int x, y;

    x = b & 0x7F;
    y = ((a & 0x3F) << 1) | (b >> 7 & 1);

    return y * 2 * avctx->width + x * 2;
}

static void copy4h(AVCodecContext *avctx, uint8_t *dst)
{
    PAFVideoDecContext *c = avctx->priv_data;
    int i;

    for (i = 0; i < 4; i++) {
        bytestream2_get_buffer(&c->gb, dst, 4);
        dst += avctx->width;
    }
}

static void copy_color_mask(AVCodecContext *avctx, uint8_t mask, uint8_t *dst, uint8_t color)
{
    int i;

    for (i = 0; i < 4; i++) {
        if ((mask >> 4) & (1 << (3 - i)))
            dst[i] = color;
        if ((mask & 15) & (1 << (3 - i)))
            dst[avctx->width + i] = color;
    }
}

static void copy_src_mask(AVCodecContext *avctx, uint8_t mask, uint8_t *dst, const uint8_t *src)
{
    int i;

    for (i = 0; i < 4; i++) {
        if ((mask >> 4) & (1 << (3 - i)))
            dst[i] = src[i];
        if ((mask & 15) & (1 << (3 - i)))
            dst[avctx->width + i] = src[avctx->width + i];
    }
}

static int decode_0(AVCodecContext *avctx, uint8_t code, uint8_t *pkt)
{
    PAFVideoDecContext *c = avctx->priv_data;
    uint32_t opcode_size, offset;
    uint8_t *dst, *dend, mask = 0, color = 0, a, b, p;
    const uint8_t *src, *send, *opcodes;
    int  i, j, x = 0;

    i = bytestream2_get_byte(&c->gb);
    if (i) {
        if (code & 0x10) {
            int align;

            align = bytestream2_tell(&c->gb) & 3;
            if (align)
                bytestream2_skip(&c->gb, 4 - align);
        }
        do {
            a      = bytestream2_get_byte(&c->gb);
            b      = bytestream2_get_byte(&c->gb);
            p      = (a & 0xC0) >> 6;
            dst    = c->frame[p] + get_video_page_offset(avctx, a, b);
            dend   = c->frame[p] + c->frame_size;
            offset = (b & 0x7F) * 2;
            j      = bytestream2_get_le16(&c->gb) + offset;

            do {
                offset++;
                if (dst + 3 * avctx->width + 4 > dend)
                    return AVERROR_INVALIDDATA;
                copy4h(avctx, dst);
                if ((offset & 0x3F) == 0)
                    dst += avctx->width * 3;
                dst += 4;
            } while (offset < j);
        } while (--i);
    }

    dst  = c->frame[c->current_frame];
    dend = c->frame[c->current_frame] + c->frame_size;
    do {
        a    = bytestream2_get_byte(&c->gb);
        b    = bytestream2_get_byte(&c->gb);
        p    = (a & 0xC0) >> 6;
        src  = c->frame[p] + get_video_page_offset(avctx, a, b);
        send = c->frame[p] + c->frame_size;
        if ((src + 3 * avctx->width + 4 > send) ||
            (dst + 3 * avctx->width + 4 > dend))
            return AVERROR_INVALIDDATA;
        copy_block4(dst, src, avctx->width, avctx->width, 4);
        i++;
        if ((i & 0x3F) == 0)
            dst += avctx->width * 3;
        dst += 4;
    } while (i < c->video_size / 16);

    opcode_size = bytestream2_get_le16(&c->gb);
    bytestream2_skip(&c->gb, 2);

    if (bytestream2_get_bytes_left(&c->gb) < opcode_size)
        return AVERROR_INVALIDDATA;

    opcodes = pkt + bytestream2_tell(&c->gb);
    bytestream2_skipu(&c->gb, opcode_size);

    dst = c->frame[c->current_frame];

    for (i = 0; i < avctx->height; i += 4, dst += avctx->width * 3) {
        for (j = 0; j < avctx->width; j += 4, dst += 4) {
            int opcode, k = 0;

            if (x > opcode_size)
                return AVERROR_INVALIDDATA;
            if (j & 4) {
                opcode = opcodes[x] & 15;
                x++;
            } else {
                opcode = opcodes[x] >> 4;
            }

            while (block_sequences[opcode][k]) {

                offset = avctx->width * 2;
                code   = block_sequences[opcode][k++];

                switch (code) {
                case 2:
                    offset = 0;
                case 3:
                    color  = bytestream2_get_byte(&c->gb);
                case 4:
                    mask   = bytestream2_get_byte(&c->gb);
                    copy_color_mask(avctx, mask, dst + offset, color);
                    break;
                case 5:
                    offset = 0;
                case 6:
                    a    = bytestream2_get_byte(&c->gb);
                    b    = bytestream2_get_byte(&c->gb);
                    p    = (a & 0xC0) >> 6;
                    src  = c->frame[p] + get_video_page_offset(avctx, a, b);
                    send = c->frame[p] + c->frame_size;
                case 7:
                    if (src + offset + avctx->width + 4 > send)
                        return AVERROR_INVALIDDATA;
                    mask = bytestream2_get_byte(&c->gb);
                    copy_src_mask(avctx, mask, dst + offset, src + offset);
                    break;
                }
            }
        }
    }

    return 0;
}

static int paf_vid_decode(AVCodecContext *avctx, void *data,
                          int *got_frame, AVPacket *pkt)
{
    PAFVideoDecContext *c = avctx->priv_data;
    uint8_t code, *dst, *src, *end;
    int i, frame, ret;

    if ((ret = ff_reget_buffer(avctx, c->pic)) < 0)
        return ret;

    bytestream2_init(&c->gb, pkt->data, pkt->size);

    code = bytestream2_get_byte(&c->gb);
    if (code & 0x20) {
        for (i = 0; i < 4; i++)
            memset(c->frame[i], 0, c->frame_size);

        memset(c->pic->data[1], 0, AVPALETTE_SIZE);
        c->current_frame = 0;
        c->pic->key_frame = 1;
        c->pic->pict_type = AV_PICTURE_TYPE_I;
    } else {
        c->pic->key_frame = 0;
        c->pic->pict_type = AV_PICTURE_TYPE_P;
    }

    if (code & 0x40) {
        uint32_t *out = (uint32_t *)c->pic->data[1];
        int index, count;

        index = bytestream2_get_byte(&c->gb);
        count = bytestream2_get_byte(&c->gb) + 1;

        if (index + count > 256)
            return AVERROR_INVALIDDATA;
        if (bytestream2_get_bytes_left(&c->gb) < 3*count)
            return AVERROR_INVALIDDATA;

        out += index;
        for (i = 0; i < count; i++) {
            unsigned r, g, b;

            r = bytestream2_get_byteu(&c->gb);
            r = r << 2 | r >> 4;
            g = bytestream2_get_byteu(&c->gb);
            g = g << 2 | g >> 4;
            b = bytestream2_get_byteu(&c->gb);
            b = b << 2 | b >> 4;
            *out++ = 0xFFU << 24 | r << 16 | g << 8 | b;
        }
        c->pic->palette_has_changed = 1;
    }

    switch (code & 0x0F) {
    case 0:
        if ((ret = decode_0(avctx, code, pkt->data)) < 0)
            return ret;
        break;
    case 1:
        dst = c->frame[c->current_frame];
        bytestream2_skip(&c->gb, 2);
        if (bytestream2_get_bytes_left(&c->gb) < c->video_size)
            return AVERROR_INVALIDDATA;
        bytestream2_get_bufferu(&c->gb, dst, c->video_size);
        break;
    case 2:
        frame = bytestream2_get_byte(&c->gb);
        if (frame > 3)
            return AVERROR_INVALIDDATA;
        if (frame != c->current_frame)
            memcpy(c->frame[c->current_frame], c->frame[frame], c->frame_size);
        break;
    case 4:
        dst = c->frame[c->current_frame];
        end = dst + c->video_size;

        bytestream2_skip(&c->gb, 2);

        while (dst < end) {
            int8_t code;
            int count;

            if (bytestream2_get_bytes_left(&c->gb) < 2)
                return AVERROR_INVALIDDATA;

            code  = bytestream2_get_byteu(&c->gb);
            count = FFABS(code) + 1;

            if (dst + count > end)
                return AVERROR_INVALIDDATA;
            if (code < 0)
                memset(dst, bytestream2_get_byteu(&c->gb), count);
            else
                bytestream2_get_buffer(&c->gb, dst, count);
            dst += count;
        }
        break;
    default:
        avpriv_request_sample(avctx, "unknown/invalid code");
        return AVERROR_INVALIDDATA;
    }

    dst = c->pic->data[0];
    src = c->frame[c->current_frame];
    for (i = 0; i < avctx->height; i++) {
        memcpy(dst, src, avctx->width);
        dst += c->pic->linesize[0];
        src += avctx->width;
    }

    c->current_frame = (c->current_frame + 1) & 3;
    if ((ret = av_frame_ref(data, c->pic)) < 0)
        return ret;

    *got_frame       = 1;

    return pkt->size;
}

static av_cold int paf_aud_init(AVCodecContext *avctx)
{
    if (avctx->channels != 2) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of channels\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->channel_layout = AV_CH_LAYOUT_STEREO;
    avctx->sample_fmt     = AV_SAMPLE_FMT_S16;

    return 0;
}

static int paf_aud_decode(AVCodecContext *avctx, void *data,
                          int *got_frame_ptr, AVPacket *pkt)
{
    AVFrame *frame = data;
    uint8_t *buf = pkt->data;
    int16_t *output_samples;
    const uint8_t *t;
    int frames, ret, i, j, k;

    frames = pkt->size / PAF_SOUND_FRAME_SIZE;
    if (frames < 1)
        return AVERROR_INVALIDDATA;

    frame->nb_samples = PAF_SOUND_SAMPLES * frames;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    output_samples = (int16_t *)frame->data[0];
    for (i = 0; i < frames; i++) {
        t = buf + 256 * sizeof(uint16_t);
        for (j = 0; j < PAF_SOUND_SAMPLES; j++) {
            for (k = 0; k < 2; k++) {
                *output_samples++ = AV_RL16(buf + *t * 2);
                t++;
            }
        }
        buf += PAF_SOUND_FRAME_SIZE;
    }

    *got_frame_ptr   = 1;

    return pkt->size;
}

AVCodec ff_paf_video_decoder = {
    .name           = "paf_video",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PAF_VIDEO,
    .priv_data_size = sizeof(PAFVideoDecContext),
    .init           = paf_vid_init,
    .close          = paf_vid_close,
    .decode         = paf_vid_decode,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Amazing Studio Packed Animation File Video"),
};

AVCodec ff_paf_audio_decoder = {
    .name           = "paf_audio",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_PAF_AUDIO,
    .init           = paf_aud_init,
    .decode         = paf_aud_decode,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Amazing Studio Packed Animation File Audio"),
};
