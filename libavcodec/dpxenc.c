/*
 * DPX (.dpx) image encoder
 * Copyright (c) 2011 Peter Ross <pross@xvid.org>
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
#include "libavutil/imgutils.h"
#include "avcodec.h"

typedef struct DPXContext {
    AVFrame picture;
    int big_endian;
    int bits_per_component;
    int descriptor;
} DPXContext;

static av_cold int encode_init(AVCodecContext *avctx)
{
    DPXContext *s = avctx->priv_data;

    avctx->coded_frame = &s->picture;
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
    avctx->coded_frame->key_frame = 1;

    s->big_endian         = 1;
    s->bits_per_component = 8;
    s->descriptor         = 50; /* RGB */

    switch (avctx->pix_fmt) {
    case PIX_FMT_RGB24:
        break;
    case PIX_FMT_RGBA:
        s->descriptor = 51; /* RGBA */
        break;
    case PIX_FMT_RGB48LE:
        s->big_endian = 0;
    case PIX_FMT_RGB48BE:
        s->bits_per_component = avctx->bits_per_raw_sample ? avctx->bits_per_raw_sample : 16;
        break;
    default:
        av_log(avctx, AV_LOG_INFO, "unsupported pixel format\n");
        return -1;
    }

    return 0;
}

#define write16(p, value) \
do { \
    if (s->big_endian) AV_WB16(p, value); \
    else               AV_WL16(p, value); \
} while(0)

#define write32(p, value) \
do { \
    if (s->big_endian) AV_WB32(p, value); \
    else               AV_WL32(p, value); \
} while(0)

static void encode_rgb48_10bit(AVCodecContext *avctx, const AVPicture *pic, uint8_t *dst)
{
    DPXContext *s = avctx->priv_data;
    const uint8_t *src = pic->data[0];
    int x, y;

    for (y = 0; y < avctx->height; y++) {
        for (x = 0; x < avctx->width; x++) {
            int value;
            if ((avctx->pix_fmt & 1)) {
                value = ((AV_RB16(src + 6*x + 4) & 0xFFC0) >> 4)
                      | ((AV_RB16(src + 6*x + 2) & 0xFFC0) << 6)
                      | ((AV_RB16(src + 6*x + 0) & 0xFFC0) << 16);
            } else {
                value = ((AV_RL16(src + 6*x + 4) & 0xFFC0) >> 4)
                      | ((AV_RL16(src + 6*x + 2) & 0xFFC0) << 6)
                      | ((AV_RL16(src + 6*x + 0) & 0xFFC0) << 16);
            }
            write32(dst, value);
            dst += 4;
        }
        src += pic->linesize[0];
    }
}

static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data)
{
    DPXContext *s = avctx->priv_data;
    int size;

#define HEADER_SIZE 1664  /* DPX Generic header */
    if (buf_size < HEADER_SIZE)
        return -1;

    memset(buf, 0, HEADER_SIZE);

    /* File information header */
    write32(buf,       MKBETAG('S','D','P','X'));
    write32(buf +   4, HEADER_SIZE);
    memcpy (buf +   8, "V1.0", 4);
    write32(buf +  20, 1); /* new image */
    write32(buf +  24, HEADER_SIZE);
    memcpy (buf + 160, LIBAVCODEC_IDENT, FFMIN(sizeof(LIBAVCODEC_IDENT), 100));
    write32(buf + 660, 0xFFFFFFFF); /* unencrypted */

    /* Image information header */
    write16(buf + 768, 0); /* orientation; left to right, top to bottom */
    write16(buf + 770, 1); /* number of elements */
    write32(buf + 772, avctx->width);
    write32(buf + 776, avctx->height);
    buf[800] = s->descriptor;
    buf[801] = 2; /* linear transfer */
    buf[802] = 2; /* linear colorimetric */
    buf[803] = s->bits_per_component;
    write16(buf + 804, s->bits_per_component == 10 ? 1 : 0); /* packing method */

    /* Image source information header */
    write32(buf + 1628, avctx->sample_aspect_ratio.num);
    write32(buf + 1632, avctx->sample_aspect_ratio.den);

    switch(s->bits_per_component) {
    case 8:
    case 16:
        size = avpicture_layout(data, avctx->pix_fmt,
                                avctx->width, avctx->height,
                                buf + HEADER_SIZE, buf_size - HEADER_SIZE);
        if (size < 0)
            return size;
        break;
    case 10:
        size = avctx->height * avctx->width * 4;
        if (buf_size < HEADER_SIZE + size)
            return -1;
        encode_rgb48_10bit(avctx, data, buf + HEADER_SIZE);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported bit depth: %d\n", s->bits_per_component);
        return -1;
    }

    size += HEADER_SIZE;

    write32(buf + 16, size); /* file size */
    return size;
}

AVCodec ff_dpx_encoder = {
    .name = "dpx",
    .type = AVMEDIA_TYPE_VIDEO,
    .id   = CODEC_ID_DPX,
    .priv_data_size = sizeof(DPXContext),
    .init   = encode_init,
    .encode = encode_frame,
    .pix_fmts = (const enum PixelFormat[]){
        PIX_FMT_RGB24,
        PIX_FMT_RGBA,
        PIX_FMT_RGB48LE,
        PIX_FMT_RGB48BE,
        PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("DPX image"),
};
