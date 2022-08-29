/*
 * Librsvg rasterization wrapper
 * Copyright (c) 2017 Rostislav Pehlivanov <atomnuker@gmail.com>
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
#include "decode.h"
#include "libavutil/opt.h"
#include "librsvg-2.0/librsvg/rsvg.h"

typedef struct LibRSVGContext {
    AVClass *class;

    int width;
    int height;
    int keep_ar;
} LibRSVGContext;

static int librsvg_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                                int *got_frame, AVPacket *pkt)
{
    int ret;
    LibRSVGContext *s = avctx->priv_data;

    RsvgHandle *handle;
    RsvgDimensionData unscaled_dimensions, dimensions;
    cairo_surface_t *image;
    cairo_t *crender = NULL;
    GError *error = NULL;

    *got_frame = 0;

    handle = rsvg_handle_new_from_data(pkt->data, pkt->size, &error);
    if (error) {
        av_log(avctx, AV_LOG_ERROR, "Error parsing svg!\n");
        g_error_free(error);
        return AVERROR_INVALIDDATA;
    }

    rsvg_handle_get_dimensions(handle, &dimensions);
    rsvg_handle_get_dimensions(handle, &unscaled_dimensions);
    dimensions.width  = s->width  ? s->width  : dimensions.width;
    dimensions.height = s->height ? s->height : dimensions.height;
    if (s->keep_ar && (s->width || s->height)) {
        double default_ar = unscaled_dimensions.width/(double)unscaled_dimensions.height;
        if (!s->width)
            dimensions.width  = lrintf(dimensions.height * default_ar);
        else
            dimensions.height = lrintf(dimensions.width  / default_ar);
    }

    if ((ret = ff_set_dimensions(avctx, dimensions.width, dimensions.height)))
        return ret;
    avctx->pix_fmt = AV_PIX_FMT_RGB32;

    if ((ret = ff_get_buffer(avctx, frame, 0)))
        return ret;
    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->key_frame = 1;

    image = cairo_image_surface_create_for_data(frame->data[0], CAIRO_FORMAT_ARGB32,
                                                frame->width, frame->height,
                                                frame->linesize[0]);
    if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS)
        return AVERROR_INVALIDDATA;

    crender = cairo_create(image);

    cairo_save(crender);
    cairo_set_operator(crender, CAIRO_OPERATOR_CLEAR);
    cairo_paint(crender);
    cairo_restore(crender);

    cairo_scale(crender, dimensions.width / (double)unscaled_dimensions.width,
                dimensions.height / (double)unscaled_dimensions.height);

    rsvg_handle_render_cairo(handle, crender);

    cairo_destroy(crender);
    cairo_surface_destroy(image);
    g_object_unref(handle);

    *got_frame = 1;

    return 0;
}

#define OFFSET(x) offsetof(LibRSVGContext, x)
#define DEC (AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption options[] = {
    { "width", "Width to render to (0 for default)", OFFSET(width), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, DEC },
    { "height", "Height to render to (0 for default)", OFFSET(height), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, DEC },
    { "keep_ar", "Keep aspect ratio with custom width/height", OFFSET(keep_ar), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, DEC },
    { NULL },
};

static const AVClass librsvg_decoder_class = {
    .class_name = "Librsvg",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_librsvg_decoder = {
    .p.name         = "librsvg",
    CODEC_LONG_NAME("Librsvg rasterizer"),
    .p.priv_class   = &librsvg_decoder_class,
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_SVG,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .p.wrapper_name = "librsvg",
    FF_CODEC_DECODE_CB(librsvg_decode_frame),
    .priv_data_size = sizeof(LibRSVGContext),
};
