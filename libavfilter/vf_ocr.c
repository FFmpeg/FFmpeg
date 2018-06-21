/*
 * Copyright (c) 2015 Paul B Mahol
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

#include <tesseract/capi.h>

#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct OCRContext {
    const AVClass *class;

    char *datapath;
    char *language;
    char *whitelist;
    char *blacklist;

    TessBaseAPI *tess;
} OCRContext;

#define OFFSET(x) offsetof(OCRContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption ocr_options[] = {
    { "datapath",  "set datapath",            OFFSET(datapath),  AV_OPT_TYPE_STRING, {.str=NULL},  0, 0, FLAGS },
    { "language",  "set language",            OFFSET(language),  AV_OPT_TYPE_STRING, {.str="eng"}, 0, 0, FLAGS },
    { "whitelist", "set character whitelist", OFFSET(whitelist), AV_OPT_TYPE_STRING, {.str="0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.:;,-+_!?\"'[]{}()<>|/\\=*&%$#@!~"}, 0, 0, FLAGS },
    { "blacklist", "set character blacklist", OFFSET(blacklist), AV_OPT_TYPE_STRING, {.str=""},    0, 0, FLAGS },
    { NULL }
};

static av_cold int init(AVFilterContext *ctx)
{
    OCRContext *s = ctx->priv;

    s->tess = TessBaseAPICreate();
    if (TessBaseAPIInit3(s->tess, s->datapath, s->language) == -1) {
        av_log(ctx, AV_LOG_ERROR, "failed to init tesseract\n");
        return AVERROR(EINVAL);
    }

    if (!TessBaseAPISetVariable(s->tess, "tessedit_char_whitelist", s->whitelist)) {
        av_log(ctx, AV_LOG_ERROR, "failed to set whitelist\n");
        return AVERROR(EINVAL);
    }

    if (!TessBaseAPISetVariable(s->tess, "tessedit_char_blacklist", s->blacklist)) {
        av_log(ctx, AV_LOG_ERROR, "failed to set blacklist\n");
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_DEBUG, "Tesseract version: %s\n", TessVersion());

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVDictionary **metadata = &in->metadata;
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    OCRContext *s = ctx->priv;
    char *result;

    result = TessBaseAPIRect(s->tess, in->data[0], 1,
                             in->linesize[0], 0, 0, in->width, in->height);
    av_dict_set(metadata, "lavfi.ocr.text", result, 0);
    TessDeleteText(result);

    return ff_filter_frame(outlink, in);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    OCRContext *s = ctx->priv;

    TessBaseAPIEnd(s->tess);
    TessBaseAPIDelete(s->tess);
}

AVFILTER_DEFINE_CLASS(ocr);

static const AVFilterPad ocr_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad ocr_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_ocr = {
    .name          = "ocr",
    .description   = NULL_IF_CONFIG_SMALL("Optical Character Recognition."),
    .priv_size     = sizeof(OCRContext),
    .priv_class    = &ocr_class,
    .query_formats = query_formats,
    .init          = init,
    .uninit        = uninit,
    .inputs        = ocr_inputs,
    .outputs       = ocr_outputs,
};
