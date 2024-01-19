/*
 * Copyright (c) 2021 Paul B Mahol
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

/**
 * @file
 * OpenEXR encoder
 */

#include <float.h>
#include <zlib.h>

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/float2half.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"

enum ExrCompr {
    EXR_RAW,
    EXR_RLE,
    EXR_ZIP1,
    EXR_ZIP16,
    EXR_NBCOMPR,
};

enum ExrPixelType {
    EXR_UINT,
    EXR_HALF,
    EXR_FLOAT,
    EXR_UNKNOWN,
};

static const char abgr_chlist[4] = { 'A', 'B', 'G', 'R' };
static const char bgr_chlist[4] = { 'B', 'G', 'R', 'A' };
static const char y_chlist[4] = { 'Y' };
static const uint8_t gbra_order[4] = { 3, 1, 0, 2 };
static const uint8_t gbr_order[4] = { 1, 0, 2, 0 };
static const uint8_t y_order[4] = { 0 };

typedef struct EXRScanlineData {
    uint8_t *compressed_data;
    unsigned int compressed_size;

    uint8_t *uncompressed_data;
    unsigned int uncompressed_size;

    uint8_t *tmp;
    unsigned int tmp_size;

    int64_t actual_size;
} EXRScanlineData;

typedef struct EXRContext {
    const AVClass *class;

    int compression;
    int pixel_type;
    int planes;
    int nb_scanlines;
    int scanline_height;
    float gamma;
    const char *ch_names;
    const uint8_t *ch_order;
    PutByteContext pb;

    EXRScanlineData *scanline;

    Float2HalfTables f2h_tables;
} EXRContext;

static av_cold int encode_init(AVCodecContext *avctx)
{
    EXRContext *s = avctx->priv_data;

    ff_init_float2half_tables(&s->f2h_tables);

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_GBRPF32:
        s->planes = 3;
        s->ch_names = bgr_chlist;
        s->ch_order = gbr_order;
        break;
    case AV_PIX_FMT_GBRAPF32:
        s->planes = 4;
        s->ch_names = abgr_chlist;
        s->ch_order = gbra_order;
        break;
    case AV_PIX_FMT_GRAYF32:
        s->planes = 1;
        s->ch_names = y_chlist;
        s->ch_order = y_order;
        break;
    default:
        av_assert0(0);
    }

    switch (s->compression) {
    case EXR_RAW:
    case EXR_RLE:
    case EXR_ZIP1:
        s->scanline_height = 1;
        s->nb_scanlines = avctx->height;
        break;
    case EXR_ZIP16:
        s->scanline_height = 16;
        s->nb_scanlines = (avctx->height + s->scanline_height - 1) / s->scanline_height;
        break;
    default:
        av_assert0(0);
    }

    s->scanline = av_calloc(s->nb_scanlines, sizeof(*s->scanline));
    if (!s->scanline)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int encode_close(AVCodecContext *avctx)
{
    EXRContext *s = avctx->priv_data;

    for (int y = 0; y < s->nb_scanlines && s->scanline; y++) {
        EXRScanlineData *scanline = &s->scanline[y];

        av_freep(&scanline->tmp);
        av_freep(&scanline->compressed_data);
        av_freep(&scanline->uncompressed_data);
    }

    av_freep(&s->scanline);

    return 0;
}

static void reorder_pixels(uint8_t *dst, const uint8_t *src, ptrdiff_t size)
{
    const ptrdiff_t half_size = (size + 1) / 2;
    uint8_t *t1 = dst;
    uint8_t *t2 = dst + half_size;

    for (ptrdiff_t i = 0; i < half_size; i++) {
        t1[i] = *(src++);
        t2[i] = *(src++);
    }
}

static void predictor(uint8_t *src, ptrdiff_t size)
{
    int p = src[0];

    for (ptrdiff_t i = 1; i < size; i++) {
        int d = src[i] - p + 384;

        p = src[i];
        src[i] = d;
    }
}

static int64_t rle_compress(uint8_t *out, int64_t out_size,
                            const uint8_t *in, int64_t in_size)
{
    int64_t i = 0, o = 0, run = 1, copy = 0;

    while (i < in_size) {
        while (i + run < in_size && in[i] == in[i + run] && run < 128)
            run++;

        if (run >= 3) {
            if (o + 2 >= out_size)
                return -1;
            out[o++] = run - 1;
            out[o++] = in[i];
            i += run;
        } else {
            if (i + run < in_size)
                copy += run;
            while (i + copy < in_size && copy < 127 && in[i + copy] != in[i + copy - 1])
                copy++;

            if (o + 1 + copy >= out_size)
                return -1;
            out[o++] = -copy;

            for (int x = 0; x < copy; x++)
                out[o + x] = in[i + x];

            o += copy;
            i += copy;
            copy = 0;
        }

        run = 1;
    }

    return o;
}

static int encode_scanline_rle(EXRContext *s, const AVFrame *frame)
{
    const int64_t element_size = s->pixel_type == EXR_HALF ? 2LL : 4LL;

    for (int y = 0; y < frame->height; y++) {
        EXRScanlineData *scanline = &s->scanline[y];
        int64_t tmp_size = element_size * s->planes * frame->width;
        int64_t max_compressed_size = tmp_size * 3 / 2;

        av_fast_padded_malloc(&scanline->uncompressed_data, &scanline->uncompressed_size, tmp_size);
        if (!scanline->uncompressed_data)
            return AVERROR(ENOMEM);

        av_fast_padded_malloc(&scanline->tmp, &scanline->tmp_size, tmp_size);
        if (!scanline->tmp)
            return AVERROR(ENOMEM);

        av_fast_padded_malloc(&scanline->compressed_data, &scanline->compressed_size, max_compressed_size);
        if (!scanline->compressed_data)
            return AVERROR(ENOMEM);

        switch (s->pixel_type) {
        case EXR_FLOAT:
            for (int p = 0; p < s->planes; p++) {
                int ch = s->ch_order[p];

                memcpy(scanline->uncompressed_data + frame->width * 4 * p,
                       frame->data[ch] + y * frame->linesize[ch], frame->width * 4);
            }
            break;
        case EXR_HALF:
            for (int p = 0; p < s->planes; p++) {
                int ch = s->ch_order[p];
                uint16_t *dst = (uint16_t *)(scanline->uncompressed_data + frame->width * 2 * p);
                const uint32_t *src = (const uint32_t *)(frame->data[ch] + y * frame->linesize[ch]);

                for (int x = 0; x < frame->width; x++)
                    dst[x] = float2half(src[x], &s->f2h_tables);
            }
            break;
        }

        reorder_pixels(scanline->tmp, scanline->uncompressed_data, tmp_size);
        predictor(scanline->tmp, tmp_size);
        scanline->actual_size = rle_compress(scanline->compressed_data,
                                             max_compressed_size,
                                             scanline->tmp, tmp_size);

        if (scanline->actual_size <= 0 || scanline->actual_size >= tmp_size) {
            FFSWAP(uint8_t *, scanline->uncompressed_data, scanline->compressed_data);
            FFSWAP(int, scanline->uncompressed_size, scanline->compressed_size);
            scanline->actual_size = tmp_size;
        }
    }

    return 0;
}

static int encode_scanline_zip(EXRContext *s, const AVFrame *frame)
{
    const int64_t element_size = s->pixel_type == EXR_HALF ? 2LL : 4LL;

    for (int y = 0; y < s->nb_scanlines; y++) {
        EXRScanlineData *scanline = &s->scanline[y];
        const int scanline_height = FFMIN(s->scanline_height, frame->height - y * s->scanline_height);
        int64_t tmp_size = element_size * s->planes * frame->width * scanline_height;
        int64_t max_compressed_size = tmp_size * 3 / 2;
        unsigned long actual_size, source_size;

        av_fast_padded_malloc(&scanline->uncompressed_data, &scanline->uncompressed_size, tmp_size);
        if (!scanline->uncompressed_data)
            return AVERROR(ENOMEM);

        av_fast_padded_malloc(&scanline->tmp, &scanline->tmp_size, tmp_size);
        if (!scanline->tmp)
            return AVERROR(ENOMEM);

        av_fast_padded_malloc(&scanline->compressed_data, &scanline->compressed_size, max_compressed_size);
        if (!scanline->compressed_data)
            return AVERROR(ENOMEM);

        switch (s->pixel_type) {
        case EXR_FLOAT:
            for (int l = 0; l < scanline_height; l++) {
                const int scanline_size = frame->width * 4 * s->planes;

                for (int p = 0; p < s->planes; p++) {
                    int ch = s->ch_order[p];

                    memcpy(scanline->uncompressed_data + scanline_size * l + p * frame->width * 4,
                           frame->data[ch] + (y * s->scanline_height + l) * frame->linesize[ch],
                           frame->width * 4);
                }
            }
            break;
        case EXR_HALF:
            for (int l = 0; l < scanline_height; l++) {
                const int scanline_size = frame->width * 2 * s->planes;

                for (int p = 0; p < s->planes; p++) {
                    int ch = s->ch_order[p];
                    uint16_t *dst = (uint16_t *)(scanline->uncompressed_data + scanline_size * l + p * frame->width * 2);
                    const uint32_t *src = (const uint32_t *)(frame->data[ch] + (y * s->scanline_height + l) * frame->linesize[ch]);

                    for (int x = 0; x < frame->width; x++)
                        dst[x] = float2half(src[x], &s->f2h_tables);
                }
            }
            break;
        }

        reorder_pixels(scanline->tmp, scanline->uncompressed_data, tmp_size);
        predictor(scanline->tmp, tmp_size);
        source_size = tmp_size;
        actual_size = max_compressed_size;
        compress(scanline->compressed_data, &actual_size,
                 scanline->tmp, source_size);

        scanline->actual_size = actual_size;
        if (scanline->actual_size >= tmp_size) {
            FFSWAP(uint8_t *, scanline->uncompressed_data, scanline->compressed_data);
            FFSWAP(int, scanline->uncompressed_size, scanline->compressed_size);
            scanline->actual_size = tmp_size;
        }
    }

    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *frame, int *got_packet)
{
    EXRContext *s = avctx->priv_data;
    PutByteContext *pb = &s->pb;
    int64_t offset;
    int ret;
    int64_t out_size = 2048LL + avctx->height * 16LL +
                      av_image_get_buffer_size(avctx->pix_fmt,
                                               avctx->width,
                                               avctx->height, 64) * 3LL / 2;

    if ((ret = ff_get_encode_buffer(avctx, pkt, out_size, 0)) < 0)
        return ret;

    bytestream2_init_writer(pb, pkt->data, pkt->size);

    bytestream2_put_le32(pb, 20000630);
    bytestream2_put_byte(pb, 2);
    bytestream2_put_le24(pb, 0);
    bytestream2_put_buffer(pb, "channels\0chlist\0", 16);
    bytestream2_put_le32(pb, s->planes * 18 + 1);

    for (int p = 0; p < s->planes; p++) {
        bytestream2_put_byte(pb, s->ch_names[p]);
        bytestream2_put_byte(pb, 0);
        bytestream2_put_le32(pb, s->pixel_type);
        bytestream2_put_le32(pb, 0);
        bytestream2_put_le32(pb, 1);
        bytestream2_put_le32(pb, 1);
    }
    bytestream2_put_byte(pb, 0);

    bytestream2_put_buffer(pb, "compression\0compression\0", 24);
    bytestream2_put_le32(pb, 1);
    bytestream2_put_byte(pb, s->compression);

    bytestream2_put_buffer(pb, "dataWindow\0box2i\0", 17);
    bytestream2_put_le32(pb, 16);
    bytestream2_put_le32(pb, 0);
    bytestream2_put_le32(pb, 0);
    bytestream2_put_le32(pb, avctx->width - 1);
    bytestream2_put_le32(pb, avctx->height - 1);

    bytestream2_put_buffer(pb, "displayWindow\0box2i\0", 20);
    bytestream2_put_le32(pb, 16);
    bytestream2_put_le32(pb, 0);
    bytestream2_put_le32(pb, 0);
    bytestream2_put_le32(pb, avctx->width - 1);
    bytestream2_put_le32(pb, avctx->height - 1);

    bytestream2_put_buffer(pb, "lineOrder\0lineOrder\0", 20);
    bytestream2_put_le32(pb, 1);
    bytestream2_put_byte(pb, 0);

    bytestream2_put_buffer(pb, "screenWindowCenter\0v2f\0", 23);
    bytestream2_put_le32(pb, 8);
    bytestream2_put_le64(pb, 0);

    bytestream2_put_buffer(pb, "screenWindowWidth\0float\0", 24);
    bytestream2_put_le32(pb, 4);
    bytestream2_put_le32(pb, av_float2int(1.f));

    if (avctx->sample_aspect_ratio.num && avctx->sample_aspect_ratio.den) {
        bytestream2_put_buffer(pb, "pixelAspectRatio\0float\0", 23);
        bytestream2_put_le32(pb, 4);
        bytestream2_put_le32(pb, av_float2int(av_q2d(avctx->sample_aspect_ratio)));
    }

    if (avctx->framerate.num && avctx->framerate.den) {
        bytestream2_put_buffer(pb, "framesPerSecond\0rational\0", 25);
        bytestream2_put_le32(pb, 8);
        bytestream2_put_le32(pb, avctx->framerate.num);
        bytestream2_put_le32(pb, avctx->framerate.den);
    }

    bytestream2_put_buffer(pb, "gamma\0float\0", 12);
    bytestream2_put_le32(pb, 4);
    bytestream2_put_le32(pb, av_float2int(s->gamma));

    bytestream2_put_buffer(pb, "writer\0string\0", 14);
    bytestream2_put_le32(pb, 4);
    bytestream2_put_buffer(pb, "lavc", 4);
    bytestream2_put_byte(pb, 0);

    switch (s->compression) {
    case EXR_RAW:
        /* nothing to do */
        break;
    case EXR_RLE:
        encode_scanline_rle(s, frame);
        break;
    case EXR_ZIP16:
    case EXR_ZIP1:
        encode_scanline_zip(s, frame);
        break;
    default:
        av_assert0(0);
    }

    switch (s->compression) {
    case EXR_RAW:
        offset = bytestream2_tell_p(pb) + avctx->height * 8LL;

        if (s->pixel_type == EXR_FLOAT) {

            for (int y = 0; y < avctx->height; y++) {
                bytestream2_put_le64(pb, offset);
                offset += avctx->width * s->planes * 4 + 8;
            }

            for (int y = 0; y < avctx->height; y++) {
                bytestream2_put_le32(pb, y);
                bytestream2_put_le32(pb, s->planes * avctx->width * 4);
                for (int p = 0; p < s->planes; p++) {
                    int ch = s->ch_order[p];
                    bytestream2_put_buffer(pb, frame->data[ch] + y * frame->linesize[ch],
                                           avctx->width * 4);
                }
            }
        } else {
            for (int y = 0; y < avctx->height; y++) {
                bytestream2_put_le64(pb, offset);
                offset += avctx->width * s->planes * 2 + 8;
            }

            for (int y = 0; y < avctx->height; y++) {
                bytestream2_put_le32(pb, y);
                bytestream2_put_le32(pb, s->planes * avctx->width * 2);
                for (int p = 0; p < s->planes; p++) {
                    int ch = s->ch_order[p];
                    const uint32_t *src = (const uint32_t *)(frame->data[ch] + y * frame->linesize[ch]);

                    for (int x = 0; x < frame->width; x++)
                        bytestream2_put_le16(pb, float2half(src[x], &s->f2h_tables));
                }
            }
        }
        break;
    case EXR_ZIP16:
    case EXR_ZIP1:
    case EXR_RLE:
        offset = bytestream2_tell_p(pb) + s->nb_scanlines * 8LL;

        for (int y = 0; y < s->nb_scanlines; y++) {
            EXRScanlineData *scanline = &s->scanline[y];

            bytestream2_put_le64(pb, offset);
            offset += scanline->actual_size + 8;
        }

        for (int y = 0; y < s->nb_scanlines; y++) {
            EXRScanlineData *scanline = &s->scanline[y];

            bytestream2_put_le32(pb, y * s->scanline_height);
            bytestream2_put_le32(pb, scanline->actual_size);
            bytestream2_put_buffer(pb, scanline->compressed_data,
                                   scanline->actual_size);
        }
        break;
    default:
        av_assert0(0);
    }

    av_shrink_packet(pkt, bytestream2_tell_p(pb));

    *got_packet = 1;

    return 0;
}

#define OFFSET(x) offsetof(EXRContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "compression", "set compression type", OFFSET(compression), AV_OPT_TYPE_INT,   {.i64=0}, 0, EXR_NBCOMPR-1, VE, "compr" },
    { "none",        "none",                 0,                   AV_OPT_TYPE_CONST, {.i64=EXR_RAW}, 0, 0, VE, "compr" },
    { "rle" ,        "RLE",                  0,                   AV_OPT_TYPE_CONST, {.i64=EXR_RLE}, 0, 0, VE, "compr" },
    { "zip1",        "ZIP1",                 0,                   AV_OPT_TYPE_CONST, {.i64=EXR_ZIP1}, 0, 0, VE, "compr" },
    { "zip16",       "ZIP16",                0,                   AV_OPT_TYPE_CONST, {.i64=EXR_ZIP16}, 0, 0, VE, "compr" },
    { "format", "set pixel type", OFFSET(pixel_type), AV_OPT_TYPE_INT,   {.i64=EXR_FLOAT}, EXR_HALF, EXR_UNKNOWN-1, VE, "pixel" },
    { "half" ,       NULL,                   0,                   AV_OPT_TYPE_CONST, {.i64=EXR_HALF},  0, 0, VE, "pixel" },
    { "float",       NULL,                   0,                   AV_OPT_TYPE_CONST, {.i64=EXR_FLOAT}, 0, 0, VE, "pixel" },
    { "gamma", "set gamma", OFFSET(gamma), AV_OPT_TYPE_FLOAT, {.dbl=1.f}, 0.001, FLT_MAX, VE },
    { NULL},
};

static const AVClass exr_class = {
    .class_name = "exr",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_exr_encoder = {
    .p.name         = "exr",
    CODEC_LONG_NAME("OpenEXR image"),
    .priv_data_size = sizeof(EXRContext),
    .p.priv_class   = &exr_class,
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_EXR,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .init           = encode_init,
    FF_CODEC_ENCODE_CB(encode_frame),
    .close          = encode_close,
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
                                                 AV_PIX_FMT_GRAYF32,
                                                 AV_PIX_FMT_GBRPF32,
                                                 AV_PIX_FMT_GBRAPF32,
                                                 AV_PIX_FMT_NONE },
};
