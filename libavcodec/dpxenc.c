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

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "internal.h"

typedef struct DPXContext {
    int big_endian;
    int bits_per_component;
    int num_components;
    int descriptor;
    int planar;
} DPXContext;

static av_cold int encode_init(AVCodecContext *avctx)
{
    DPXContext *s = avctx->priv_data;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);

    s->big_endian         = !!(desc->flags & AV_PIX_FMT_FLAG_BE);
    s->bits_per_component = desc->comp[0].depth_minus1 + 1;
    s->num_components     = desc->nb_components;
    s->descriptor         = (desc->flags & AV_PIX_FMT_FLAG_ALPHA) ? 51 : 50;
    s->planar             = !!(desc->flags & AV_PIX_FMT_FLAG_PLANAR);

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_ABGR:
        s->descriptor = 52;
        break;
    case AV_PIX_FMT_GRAY16BE:
    case AV_PIX_FMT_GRAY16LE:
    case AV_PIX_FMT_GRAY8:
        s->descriptor = 6;
        break;
    case AV_PIX_FMT_GBRP10BE:
    case AV_PIX_FMT_GBRP10LE:
    case AV_PIX_FMT_GBRP12BE:
    case AV_PIX_FMT_GBRP12LE:
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_RGBA64BE:
    case AV_PIX_FMT_RGBA64LE:
    case AV_PIX_FMT_RGBA:
        break;
    case AV_PIX_FMT_RGB48LE:
    case AV_PIX_FMT_RGB48BE:
        if (avctx->bits_per_raw_sample)
            s->bits_per_component = avctx->bits_per_raw_sample;
        break;
    default:
        av_log(avctx, AV_LOG_INFO, "unsupported pixel format\n");
        return -1;
    }

    return 0;
}

static av_always_inline void write16_internal(int big_endian, void *p, int value)
{
    if (big_endian) AV_WB16(p, value);
    else            AV_WL16(p, value);
}

static av_always_inline void write32_internal(int big_endian, void *p, int value)
{
    if (big_endian) AV_WB32(p, value);
    else            AV_WL32(p, value);
}

#define write16(p, value) write16_internal(s->big_endian, p, value)
#define write32(p, value) write32_internal(s->big_endian, p, value)

static void encode_rgb48_10bit(AVCodecContext *avctx, const AVPicture *pic, uint8_t *dst)
{
    DPXContext *s = avctx->priv_data;
    const uint8_t *src = pic->data[0];
    int x, y;

    for (y = 0; y < avctx->height; y++) {
        for (x = 0; x < avctx->width; x++) {
            int value;
            if (s->big_endian) {
                value = ((AV_RB16(src + 6*x + 4) & 0xFFC0U) >> 4)
                      | ((AV_RB16(src + 6*x + 2) & 0xFFC0U) << 6)
                      | ((AV_RB16(src + 6*x + 0) & 0xFFC0U) << 16);
            } else {
                value = ((AV_RL16(src + 6*x + 4) & 0xFFC0U) >> 4)
                      | ((AV_RL16(src + 6*x + 2) & 0xFFC0U) << 6)
                      | ((AV_RL16(src + 6*x + 0) & 0xFFC0U) << 16);
            }
            write32(dst, value);
            dst += 4;
        }
        src += pic->linesize[0];
    }
}

static void encode_gbrp10(AVCodecContext *avctx, const AVPicture *pic, uint8_t *dst)
{
    DPXContext *s = avctx->priv_data;
    const uint8_t *src[3] = {pic->data[0], pic->data[1], pic->data[2]};
    int x, y, i;

    for (y = 0; y < avctx->height; y++) {
        for (x = 0; x < avctx->width; x++) {
            int value;
            if (s->big_endian) {
                value = (AV_RB16(src[0] + 2*x) << 12)
                      | (AV_RB16(src[1] + 2*x) << 2)
                      | ((unsigned)AV_RB16(src[2] + 2*x) << 22);
            } else {
                value = (AV_RL16(src[0] + 2*x) << 12)
                      | (AV_RL16(src[1] + 2*x) << 2)
                      | ((unsigned)AV_RL16(src[2] + 2*x) << 22);
            }
            write32(dst, value);
            dst += 4;
        }
        for (i = 0; i < 3; i++)
            src[i] += pic->linesize[i];
    }
}

static void encode_gbrp12(AVCodecContext *avctx, const AVPicture *pic, uint16_t *dst)
{
    DPXContext *s = avctx->priv_data;
    const uint16_t *src[3] = {(uint16_t*)pic->data[0],
                              (uint16_t*)pic->data[1],
                              (uint16_t*)pic->data[2]};
    int x, y, i, pad;
    pad = avctx->width*6;
    pad = (FFALIGN(pad, 4) - pad) >> 1;
    for (y = 0; y < avctx->height; y++) {
        for (x = 0; x < avctx->width; x++) {
            uint16_t value[3];
            if (s->big_endian) {
                value[1] = AV_RB16(src[0] + x) << 4;
                value[2] = AV_RB16(src[1] + x) << 4;
                value[0] = AV_RB16(src[2] + x) << 4;
            } else {
                value[1] = AV_RL16(src[0] + x) << 4;
                value[2] = AV_RL16(src[1] + x) << 4;
                value[0] = AV_RL16(src[2] + x) << 4;
            }
            for (i = 0; i < 3; i++)
                write16(dst++, value[i]);
        }
        for (i = 0; i < pad; i++)
            *dst++ = 0;
        for (i = 0; i < 3; i++)
            src[i] += pic->linesize[i]/2;
    }
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *frame, int *got_packet)
{
    DPXContext *s = avctx->priv_data;
    int size, ret, need_align, len;
    uint8_t *buf;

#define HEADER_SIZE 1664  /* DPX Generic header */
    if (s->bits_per_component == 10)
        size = avctx->height * avctx->width * 4;
    else if (s->bits_per_component == 12) {
        // 3 components, 12 bits put on 16 bits
        len  = avctx->width*6;
        size = FFALIGN(len, 4);
        need_align = size - len;
        size *= avctx->height;
    } else {
        // N components, M bits
        len = avctx->width * s->num_components * s->bits_per_component >> 3;
        size = FFALIGN(len, 4);
        need_align = size - len;
        size *= avctx->height;
    }
    if ((ret = ff_alloc_packet2(avctx, pkt, size + HEADER_SIZE, 0)) < 0)
        return ret;
    buf = pkt->data;

    memset(buf, 0, HEADER_SIZE);

    /* File information header */
    write32(buf,       MKBETAG('S','D','P','X'));
    write32(buf +   4, HEADER_SIZE);
    memcpy (buf +   8, "V1.0", 4);
    write32(buf +  20, 1); /* new image */
    write32(buf +  24, HEADER_SIZE);
    if (!(avctx->flags & AV_CODEC_FLAG_BITEXACT))
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
    write16(buf + 804, (s->bits_per_component == 10 || s->bits_per_component == 12) ?
                       1 : 0); /* packing method */
    write32(buf + 808, HEADER_SIZE); /* data offset */

    /* Image source information header */
    write32(buf + 1628, avctx->sample_aspect_ratio.num);
    write32(buf + 1632, avctx->sample_aspect_ratio.den);

    switch(s->bits_per_component) {
    case 8:
    case 16:
        if (need_align) {
            int j;
            const uint8_t *src = frame->data[0];
            uint8_t *dst = pkt->data + HEADER_SIZE;
            size = (len + need_align) * avctx->height;
            for (j=0; j<avctx->height; j++) {
                memcpy(dst, src, len);
                memset(dst + len, 0, need_align);
                dst += len + need_align;
                src += frame->linesize[0];
            }
        } else {
            size = avpicture_layout((const AVPicture*)frame, avctx->pix_fmt,
                                    avctx->width, avctx->height,
                                    buf + HEADER_SIZE, pkt->size - HEADER_SIZE);
        }
        if (size < 0)
            return size;
        break;
    case 10:
        if (s->planar)
            encode_gbrp10(avctx, (const AVPicture*)frame, buf + HEADER_SIZE);
        else
            encode_rgb48_10bit(avctx, (const AVPicture*)frame, buf + HEADER_SIZE);
        break;
    case 12:
        encode_gbrp12(avctx, (const AVPicture*)frame, (uint16_t*)(buf + HEADER_SIZE));
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported bit depth: %d\n", s->bits_per_component);
        return -1;
    }

    size += HEADER_SIZE;

    write32(buf + 16, size); /* file size */

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

AVCodec ff_dpx_encoder = {
    .name           = "dpx",
    .long_name      = NULL_IF_CONFIG_SMALL("DPX (Digital Picture Exchange) image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DPX,
    .priv_data_size = sizeof(DPXContext),
    .init           = encode_init,
    .encode2        = encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_RGB24,    AV_PIX_FMT_RGBA, AV_PIX_FMT_ABGR,
        AV_PIX_FMT_GRAY16LE, AV_PIX_FMT_GRAY16BE,
        AV_PIX_FMT_RGB48LE,  AV_PIX_FMT_RGB48BE,
        AV_PIX_FMT_RGBA64LE, AV_PIX_FMT_RGBA64BE,
        AV_PIX_FMT_GBRP10LE, AV_PIX_FMT_GBRP10BE,
        AV_PIX_FMT_GBRP12LE, AV_PIX_FMT_GBRP12BE,
        AV_PIX_FMT_NONE},
};
