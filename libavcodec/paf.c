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

#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"

#include "libavcodec/paf.h"
#include "avcodec.h"
#include "bytestream.h"
#include "copy_block.h"
#include "internal.h"
#include "mathops.h"

static const uint8_t block_sequences[16][8] = {
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
    { 2, 4, 5, 7, 5, 7, 0, 0 },
};

typedef struct PAFVideoDecContext {
    AVFrame  *pic;
    GetByteContext gb;

    int width;
    int height;

    int current_frame;
    uint8_t *frame[4];
    int frame_size;
    int video_size;

    uint8_t *opcodes;
} PAFVideoDecContext;

static av_cold int paf_video_close(AVCodecContext *avctx)
{
    PAFVideoDecContext *c = avctx->priv_data;
    int i;

    av_frame_free(&c->pic);

    for (i = 0; i < 4; i++)
        av_freep(&c->frame[i]);

    return 0;
}

static av_cold int paf_video_init(AVCodecContext *avctx)
{
    PAFVideoDecContext *c = avctx->priv_data;
    int i;

    c->width  = avctx->width;
    c->height = avctx->height;

    if (avctx->height & 3 || avctx->width & 3) {
        av_log(avctx, AV_LOG_ERROR,
               "width %d and height %d must be multiplie of 4.\n",
               avctx->width, avctx->height);
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    c->pic = av_frame_alloc();
    if (!c->pic)
        return AVERROR(ENOMEM);

    c->frame_size = avctx->width * FFALIGN(avctx->height, 256);
    c->video_size = avctx->width * avctx->height;
    for (i = 0; i < 4; i++) {
        c->frame[i] = av_mallocz(c->frame_size);
        if (!c->frame[i]) {
            paf_video_close(avctx);
            return AVERROR(ENOMEM);
        }
    }

    return 0;
}

static void read4x4block(PAFVideoDecContext *c, uint8_t *dst, int width)
{
    int i;

    for (i = 0; i < 4; i++) {
        bytestream2_get_buffer(&c->gb, dst, 4);
        dst += width;
    }
}

static void copy_color_mask(uint8_t *dst, int width, uint8_t mask, uint8_t color)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (mask & (1 << 7 - i))
            dst[i] = color;
        if (mask & (1 << 3 - i))
            dst[width + i] = color;
    }
}

static void copy_src_mask(uint8_t *dst, int width, uint8_t mask, const uint8_t *src)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (mask & (1 << 7 - i))
            dst[i] = src[i];
        if (mask & (1 << 3 - i))
            dst[width + i] = src[width + i];
    }
}

static void set_src_position(PAFVideoDecContext *c,
                             const uint8_t **p,
                             const uint8_t **pend)
{
    int val  = bytestream2_get_be16(&c->gb);
    int page = val >> 14;
    int x    = (val & 0x7F);
    int y    = ((val >> 7) & 0x7F);

    *p    = c->frame[page] + x * 2 + y * 2 * c->width;
    *pend = c->frame[page] + c->frame_size;
}

static int decode_0(PAFVideoDecContext *c, uint8_t *pkt, uint8_t code)
{
    uint32_t opcode_size, offset;
    uint8_t *dst, *dend, mask = 0, color = 0;
    const uint8_t *src, *send, *opcodes;
    int i, j, op = 0;

    i = bytestream2_get_byte(&c->gb);
    if (i) {
        if (code & 0x10) {
            int align;

            align = bytestream2_tell(&c->gb) & 3;
            if (align)
                bytestream2_skip(&c->gb, 4 - align);
        }
        do {
            int page, val, x, y;
            val    = bytestream2_get_be16(&c->gb);
            page   = val >> 14;
            x      = (val & 0x7F) * 2;
            y      = ((val >> 7) & 0x7F) * 2;
            dst    = c->frame[page] + x + y * c->width;
            dend   = c->frame[page] + c->frame_size;
            offset = (x & 0x7F) * 2;
            j      = bytestream2_get_le16(&c->gb) + offset;
            do {
                offset++;
                if (dst + 3 * c->width + 4 > dend)
                    return AVERROR_INVALIDDATA;
                read4x4block(c, dst, c->width);
                if ((offset & 0x3F) == 0)
                    dst += c->width * 3;
                dst += 4;
            } while (offset < j);
        } while (--i);
    }

    dst  = c->frame[c->current_frame];
    dend = c->frame[c->current_frame] + c->frame_size;
    do {
        set_src_position(c, &src, &send);
        if ((src + 3 * c->width + 4 > send) ||
            (dst + 3 * c->width + 4 > dend))
            return AVERROR_INVALIDDATA;
        copy_block4(dst, src, c->width, c->width, 4);
        i++;
        if ((i & 0x3F) == 0)
            dst += c->width * 3;
        dst += 4;
    } while (i < c->video_size / 16);

    opcode_size = bytestream2_get_le16(&c->gb);
    bytestream2_skip(&c->gb, 2);

    if (bytestream2_get_bytes_left(&c->gb) < opcode_size)
        return AVERROR_INVALIDDATA;

    opcodes = pkt + bytestream2_tell(&c->gb);
    bytestream2_skipu(&c->gb, opcode_size);

    dst = c->frame[c->current_frame];

    for (i = 0; i < c->height; i += 4, dst += c->width * 3)
        for (j = 0; j < c->width; j += 4, dst += 4) {
            int opcode, k = 0;
            if (op > opcode_size)
                return AVERROR_INVALIDDATA;
            if (j & 4) {
                opcode = opcodes[op] & 15;
                op++;
            } else {
                opcode = opcodes[op] >> 4;
            }

            while (block_sequences[opcode][k]) {
                offset = c->width * 2;
                code   = block_sequences[opcode][k++];

                switch (code) {
                case 2:
                    offset = 0;
                case 3:
                    color = bytestream2_get_byte(&c->gb);
                case 4:
                    mask = bytestream2_get_byte(&c->gb);
                    copy_color_mask(dst + offset, c->width, mask, color);
                    break;
                case 5:
                    offset = 0;
                case 6:
                    set_src_position(c, &src, &send);
                case 7:
                    if (src + offset + c->width + 4 > send)
                        return AVERROR_INVALIDDATA;
                    mask = bytestream2_get_byte(&c->gb);
                    copy_src_mask(dst + offset, c->width, mask, src + offset);
                    break;
                }
            }
        }

    return 0;
}

static int paf_video_decode(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *pkt)
{
    PAFVideoDecContext *c = avctx->priv_data;
    uint8_t code, *dst, *end;
    int i, frame, ret;

    if ((ret = ff_reget_buffer(avctx, c->pic)) < 0)
        return ret;

    bytestream2_init(&c->gb, pkt->data, pkt->size);

    code = bytestream2_get_byte(&c->gb);
    if (code & 0x20) {  // frame is keyframe
        for (i = 0; i < 4; i++)
            memset(c->frame[i], 0, c->frame_size);

        memset(c->pic->data[1], 0, AVPALETTE_SIZE);
        c->current_frame  = 0;
        c->pic->key_frame = 1;
        c->pic->pict_type = AV_PICTURE_TYPE_I;
    } else {
        c->pic->key_frame = 0;
        c->pic->pict_type = AV_PICTURE_TYPE_P;
    }

    if (code & 0x40) {  // palette update
        uint32_t *out = (uint32_t *)c->pic->data[1];
        int index, count;

        index = bytestream2_get_byte(&c->gb);
        count = bytestream2_get_byte(&c->gb) + 1;

        if (index + count > 256)
            return AVERROR_INVALIDDATA;
        if (bytestream2_get_bytes_left(&c->gb) < 3 * count)
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
            *out++ = (0xFFU << 24) | (r << 16) | (g << 8) | b;
        }
        c->pic->palette_has_changed = 1;
    }

    switch (code & 0x0F) {
    case 0:
        /* Block-based motion compensation using 4x4 blocks with either
         * horizontal or vertical vectors; might incorporate VQ as well. */
        if ((ret = decode_0(c, pkt->data, code)) < 0)
            return ret;
        break;
    case 1:
        /* Uncompressed data. This mode specifies that (width * height) bytes
         * should be copied directly from the encoded buffer into the output. */
        dst = c->frame[c->current_frame];
        // possibly chunk length data
        bytestream2_skip(&c->gb, 2);
        if (bytestream2_get_bytes_left(&c->gb) < c->video_size)
            return AVERROR_INVALIDDATA;
        bytestream2_get_bufferu(&c->gb, dst, c->video_size);
        break;
    case 2:
        /* Copy reference frame: Consume the next byte in the stream as the
         * reference frame (which should be 0, 1, 2, or 3, and should not be
         * the same as the current frame number). */
        frame = bytestream2_get_byte(&c->gb);
        if (frame > 3)
            return AVERROR_INVALIDDATA;
        if (frame != c->current_frame)
            memcpy(c->frame[c->current_frame], c->frame[frame], c->frame_size);
        break;
    case 4:
        /* Run length encoding.*/
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

    av_image_copy_plane(c->pic->data[0], c->pic->linesize[0],
                        c->frame[c->current_frame], c->width,
                        c->width, c->height);

    c->current_frame = (c->current_frame + 1) & 3;
    if ((ret = av_frame_ref(data, c->pic)) < 0)
        return ret;

    *got_frame = 1;

    return pkt->size;
}

static av_cold int paf_audio_init(AVCodecContext *avctx)
{
    if (avctx->channels != 2) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of channels\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->channel_layout = AV_CH_LAYOUT_STEREO;
    avctx->sample_fmt     = AV_SAMPLE_FMT_S16;

    return 0;
}

static int paf_audio_decode(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *pkt)
{
    AVFrame *frame = data;
    int16_t *output_samples;
    const uint8_t *src = pkt->data;
    int frames, ret, i, j;
    int16_t cb[256];

    frames = pkt->size / PAF_SOUND_FRAME_SIZE;
    if (frames < 1)
        return AVERROR_INVALIDDATA;

    frame->nb_samples = PAF_SOUND_SAMPLES * frames;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    output_samples = (int16_t *)frame->data[0];
    // codebook of 256 16-bit samples and 8-bit indices to it
    for (j = 0; j < frames; j++) {
        for (i = 0; i < 256; i++)
            cb[i] = sign_extend(AV_RL16(src + i * 2), 16);
        src += 256 * 2;
        // always 2 channels
        for (i = 0; i < PAF_SOUND_SAMPLES * 2; i++)
            *output_samples++ = cb[*src++];
    }
    *got_frame = 1;

    return pkt->size;
}

#if CONFIG_PAF_VIDEO_DECODER
AVCodec ff_paf_video_decoder = {
    .name           = "paf_video",
    .long_name      = NULL_IF_CONFIG_SMALL("Amazing Studio Packed Animation File Video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PAF_VIDEO,
    .priv_data_size = sizeof(PAFVideoDecContext),
    .init           = paf_video_init,
    .close          = paf_video_close,
    .decode         = paf_video_decode,
    .capabilities   = CODEC_CAP_DR1,
};
#endif

#if CONFIG_PAF_AUDIO_DECODER
AVCodec ff_paf_audio_decoder = {
    .name         = "paf_audio",
    .long_name    = NULL_IF_CONFIG_SMALL("Amazing Studio Packed Animation File Audio"),
    .type         = AVMEDIA_TYPE_AUDIO,
    .id           = AV_CODEC_ID_PAF_AUDIO,
    .init         = paf_audio_init,
    .decode       = paf_audio_decode,
    .capabilities = CODEC_CAP_DR1,
};
#endif
