/*
 * V210 decoder
 *
 * Copyright (C) 2009 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
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
#include "codec_internal.h"
#include "v210dec.h"
#include "v210dec_init.h"
#include "libavutil/bswap.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "thread.h"

typedef struct ThreadData {
    AVFrame *frame;
    const uint8_t *buf;
    int stride;
} ThreadData;

static av_cold int decode_init(AVCodecContext *avctx)
{
    V210DecContext *s = avctx->priv_data;

    avctx->pix_fmt             = AV_PIX_FMT_YUV422P10;
    avctx->bits_per_raw_sample = 10;

    s->thread_count  = av_clip(avctx->thread_count, 1, avctx->height/4);
    s->aligned_input = 0;
    ff_v210dec_init(s);

    return 0;
}

static void decode_row(const uint32_t *src, uint16_t *y, uint16_t *u, uint16_t *v, const int width,
                       void (*unpack_frame)(const uint32_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int width))
{
    uint32_t val;
    int w = (FFMAX(0, width - 12) / 12) * 12;

    unpack_frame(src, y, u, v, w);

    y += w;
    u += w >> 1;
    v += w >> 1;
    src += (w << 1) / 3;

    while (w < width - 5) {
        READ_PIXELS(u, y, v);
        READ_PIXELS(y, u, y);
        READ_PIXELS(v, y, u);
        READ_PIXELS(y, v, y);
        w += 6;
    }

    if (w++ < width) {
        READ_PIXELS(u, y, v);

        if (w++ < width) {
            val  = av_le2ne32(*src++);
            *y++ =  val & 0x3FF;

            if (w++ < width) {
                *u++ = (val >> 10) & 0x3FF;
                *y++ = (val >> 20) & 0x3FF;
                val  = av_le2ne32(*src++);
                *v++ =  val & 0x3FF;

                if (w++ < width) {
                    *y++ = (val >> 10) & 0x3FF;

                    if (w++ < width) {
                        *u++ = (val >> 20) & 0x3FF;
                        val  = av_le2ne32(*src++);
                        *y++ =  val & 0x3FF;
                        *v++ = (val >> 10) & 0x3FF;

                        if (w++ < width)
                            *y++ = (val >> 20) & 0x3FF;
                    }
                }
            }
        }
    }
}

static int v210_decode_slice(AVCodecContext *avctx, void *arg, int jobnr, int threadnr)
{
    V210DecContext *s = avctx->priv_data;
    ThreadData *td = arg;
    AVFrame *frame = td->frame;
    int stride = td->stride;
    int slice_start = (avctx->height *  jobnr) / s->thread_count;
    int slice_end = (avctx->height * (jobnr+1)) / s->thread_count;
    const uint8_t *psrc = td->buf + stride * slice_start;
    int16_t *py = (uint16_t*)frame->data[0] + slice_start * frame->linesize[0] / 2;
    int16_t *pu = (uint16_t*)frame->data[1] + slice_start * frame->linesize[1] / 2;
    int16_t *pv = (uint16_t*)frame->data[2] + slice_start * frame->linesize[2] / 2;

    for (int h = slice_start; h < slice_end; h++) {
        decode_row((const uint32_t *)psrc, py, pu, pv, avctx->width, s->unpack_frame);
        psrc += stride;
        py += frame->linesize[0] / 2;
        pu += frame->linesize[1] / 2;
        pv += frame->linesize[2] / 2;
    }

    return 0;
}

static int v210_stride(int width, int align) {
    int aligned_width = ((width + align - 1) / align) * align;
    return aligned_width * 8 / 3;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *pic,
                        int *got_frame, AVPacket *avpkt)
{
    V210DecContext *s = avctx->priv_data;
    ThreadData td;
    int ret, stride, aligned_input;
    const uint8_t *psrc = avpkt->data;

    if (s->custom_stride )
        stride = s->custom_stride > 0 ? s->custom_stride : 0;
    else {
        stride = v210_stride(avctx->width, 48);
        if (avpkt->size < stride * avctx->height) {
            int align;
            for (align = 24; align >= 6; align >>= 1) {
                int small_stride = v210_stride(avctx->width, align);
                if (avpkt->size == small_stride * avctx->height) {
                    stride = small_stride;
                    if (!s->stride_warning_shown)
                        av_log(avctx, AV_LOG_WARNING, "Broken v210 with too small padding (%d byte) detected\n", align * 8 / 3);
                    s->stride_warning_shown = 1;
                    break;
                }
            }
            if (align < 6 && avctx->codec_tag == MKTAG('b', 'x', 'y', '2'))
                stride = 0;
        }
    }

    if (stride == 0 && ((avctx->width & 1) || (int64_t)avctx->width * avctx->height > INT_MAX / 6)) {
        av_log(avctx, AV_LOG_ERROR, "Strideless v210 is not supported for size %dx%d\n", avctx->width, avctx->height);
        return AVERROR_INVALIDDATA;
    }

    if (stride  > 0 && avpkt->size < (int64_t)stride * avctx->height ||
        stride == 0 && avpkt->size < v210_stride(avctx->width * avctx->height, 6)) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }
    if (   avctx->codec_tag == MKTAG('C', '2', '1', '0')
        && avpkt->size > 64
        && AV_RN32(psrc) == AV_RN32("INFO")
        && avpkt->size - 64 >= stride * avctx->height)
        psrc += 64;

    aligned_input = !((uintptr_t)psrc & 0x1f) && !(stride & 0x1f);
    if (aligned_input != s->aligned_input) {
        s->aligned_input = aligned_input;
        ff_v210dec_init(s);
    }

    if ((ret = ff_thread_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    pic->pict_type = AV_PICTURE_TYPE_I;
    pic->flags |= AV_FRAME_FLAG_KEY;

    if (stride) {
        td.stride = stride;
        td.buf = psrc;
        td.frame = pic;
        avctx->execute2(avctx, v210_decode_slice, &td, NULL, s->thread_count);
    } else {
        uint8_t *pointers[4];
        int linesizes[4];
        int ret = av_image_alloc(pointers, linesizes, avctx->width, avctx->height, avctx->pix_fmt, 1);
        if (ret < 0)
            return ret;
        decode_row((const uint32_t *)psrc, (uint16_t *)pointers[0], (uint16_t *)pointers[1], (uint16_t *)pointers[2], avctx->width * avctx->height, s->unpack_frame);
        av_image_copy2(pic->data, pic->linesize, pointers, linesizes,
                       avctx->pix_fmt, avctx->width, avctx->height);
        av_freep(&pointers[0]);
    }

    if (avctx->field_order > AV_FIELD_PROGRESSIVE) {
        /* we have interlaced material flagged in container */
        pic->flags |= AV_FRAME_FLAG_INTERLACED;
        if (avctx->field_order == AV_FIELD_TT || avctx->field_order == AV_FIELD_TB)
            pic->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
    }

    *got_frame      = 1;

    return avpkt->size;
}

#define V210DEC_FLAGS AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption v210dec_options[] = {
    {"custom_stride", "Custom V210 stride", offsetof(V210DecContext, custom_stride), AV_OPT_TYPE_INT,
     {.i64 = 0}, -1, INT_MAX, V210DEC_FLAGS},
    {NULL}
};

static const AVClass v210dec_class = {
    .class_name = "V210 Decoder",
    .item_name  = av_default_item_name,
    .option     = v210dec_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_v210_decoder = {
    .p.name         = "v210",
    CODEC_LONG_NAME("Uncompressed 4:2:2 10-bit"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_V210,
    .priv_data_size = sizeof(V210DecContext),
    .init           = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_SLICE_THREADS |
                      AV_CODEC_CAP_FRAME_THREADS,
    .p.priv_class   = &v210dec_class,
};
