/*
 * Copyright (c) 2010 Stefano Sabatini
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
 * libopencv wrapper functions
 */

#include <opencv/cv.h>
#include <opencv/cxtypes.h>
#include "avfilter.h"

static void fill_iplimage_from_picref(IplImage *img, const AVFilterBufferRef *picref, enum PixelFormat pixfmt)
{
    IplImage *tmpimg;
    int depth, channels_nb;

    if      (pixfmt == PIX_FMT_GRAY8) { depth = IPL_DEPTH_8U;  channels_nb = 1; }
    else if (pixfmt == PIX_FMT_BGRA)  { depth = IPL_DEPTH_8U;  channels_nb = 4; }
    else if (pixfmt == PIX_FMT_BGR24) { depth = IPL_DEPTH_8U;  channels_nb = 3; }
    else return;

    tmpimg = cvCreateImageHeader((CvSize){picref->video->w, picref->video->h}, depth, channels_nb);
    *img = *tmpimg;
    img->imageData = img->imageDataOrigin = picref->data[0];
    img->dataOrder = IPL_DATA_ORDER_PIXEL;
    img->origin    = IPL_ORIGIN_TL;
    img->widthStep = picref->linesize[0];
}

static void fill_picref_from_iplimage(AVFilterBufferRef *picref, const IplImage *img, enum PixelFormat pixfmt)
{
    picref->linesize[0] = img->widthStep;
    picref->data[0]     = img->imageData;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_BGR24, PIX_FMT_BGRA, PIX_FMT_GRAY8, PIX_FMT_NONE
    };

    avfilter_set_common_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

static void null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir) { }

typedef struct {
    const char *name;
    int (*init)(AVFilterContext *ctx, const char *args, void *opaque);
    void (*uninit)(AVFilterContext *ctx);
    void (*end_frame_filter)(AVFilterContext *ctx, IplImage *inimg, IplImage *outimg);
    void *priv;
} OCVContext;

typedef struct {
    int type;
    int    param1, param2;
    double param3, param4;
} SmoothContext;

static av_cold int smooth_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    OCVContext *ocv = ctx->priv;
    SmoothContext *smooth = ocv->priv;
    char type_str[128] = "gaussian";

    smooth->param1 = 3;
    smooth->param2 = 0;
    smooth->param3 = 0.0;
    smooth->param4 = 0.0;

    if (args)
        sscanf(args, "%127[^:]:%d:%d:%lf:%lf", type_str, &smooth->param1, &smooth->param2, &smooth->param3, &smooth->param4);

    if      (!strcmp(type_str, "blur"         )) smooth->type = CV_BLUR;
    else if (!strcmp(type_str, "blur_no_scale")) smooth->type = CV_BLUR_NO_SCALE;
    else if (!strcmp(type_str, "median"       )) smooth->type = CV_MEDIAN;
    else if (!strcmp(type_str, "gaussian"     )) smooth->type = CV_GAUSSIAN;
    else if (!strcmp(type_str, "bilateral"    )) smooth->type = CV_BILATERAL;
    else {
        av_log(ctx, AV_LOG_ERROR, "Smoothing type '%s' unknown\n.", type_str);
        return AVERROR(EINVAL);
    }

    if (smooth->param1 < 0 || !(smooth->param1%2)) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid value '%d' for param1, it has to be a positive odd number\n",
               smooth->param1);
        return AVERROR(EINVAL);
    }
    if ((smooth->type == CV_BLUR || smooth->type == CV_BLUR_NO_SCALE || smooth->type == CV_GAUSSIAN) &&
        (smooth->param2 < 0 || (smooth->param2 && !(smooth->param2%2)))) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid value '%d' for param2, it has to be zero or a positive odd number\n",
               smooth->param2);
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_INFO, "type:%s param1:%d param2:%d param3:%f param4:%f\n",
           type_str, smooth->param1, smooth->param2, smooth->param3, smooth->param4);
    return 0;
}

static void smooth_end_frame_filter(AVFilterContext *ctx, IplImage *inimg, IplImage *outimg)
{
    OCVContext *ocv = ctx->priv;
    SmoothContext *smooth = ocv->priv;
    cvSmooth(inimg, outimg, smooth->type, smooth->param1, smooth->param2, smooth->param3, smooth->param4);
}

typedef struct {
    const char *name;
    size_t priv_size;
    int  (*init)(AVFilterContext *ctx, const char *args, void *opaque);
    void (*uninit)(AVFilterContext *ctx);
    void (*end_frame_filter)(AVFilterContext *ctx, IplImage *inimg, IplImage *outimg);
} OCVFilterEntry;

static OCVFilterEntry ocv_filter_entries[] = {
    { "smooth", sizeof(SmoothContext), smooth_init, NULL, smooth_end_frame_filter },
};

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    OCVContext *ocv = ctx->priv;
    char name[128], priv_args[1024];
    int i;
    char c;

    sscanf(args, "%127[^=:]%c%1023s", name, &c, priv_args);

    for (i = 0; i < FF_ARRAY_ELEMS(ocv_filter_entries); i++) {
        OCVFilterEntry *entry = &ocv_filter_entries[i];
        if (!strcmp(name, entry->name)) {
            ocv->name             = entry->name;
            ocv->init             = entry->init;
            ocv->uninit           = entry->uninit;
            ocv->end_frame_filter = entry->end_frame_filter;

            if (!(ocv->priv = av_mallocz(entry->priv_size)))
                return AVERROR(ENOMEM);
            return ocv->init(ctx, priv_args, opaque);
        }
    }

    av_log(ctx, AV_LOG_ERROR, "No libopencv filter named '%s'\n", name);
    return AVERROR(EINVAL);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    OCVContext *ocv = ctx->priv;

    if (ocv->uninit)
        ocv->uninit(ctx);
    av_free(ocv->priv);
    memset(ocv, 0, sizeof(*ocv));
}

static void end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    OCVContext *ocv = ctx->priv;
    AVFilterLink *outlink= inlink->dst->outputs[0];
    AVFilterBufferRef *inpicref  = inlink ->cur_buf;
    AVFilterBufferRef *outpicref = outlink->out_buf;
    IplImage inimg, outimg;

    fill_iplimage_from_picref(&inimg , inpicref , inlink->format);
    fill_iplimage_from_picref(&outimg, outpicref, inlink->format);
    ocv->end_frame_filter(ctx, &inimg, &outimg);
    fill_picref_from_iplimage(outpicref, &outimg, inlink->format);

    avfilter_unref_buffer(inpicref);
    avfilter_draw_slice(outlink, 0, outlink->h, 1);
    avfilter_end_frame(outlink);
    avfilter_unref_buffer(outpicref);
}

AVFilter avfilter_vf_ocv = {
    .name        = "ocv",
    .description = NULL_IF_CONFIG_SMALL("Apply transform using libopencv."),

    .priv_size = sizeof(OCVContext),

    .query_formats = query_formats,
    .init = init,
    .uninit = uninit,

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .draw_slice       = null_draw_slice,
                                    .end_frame        = end_frame,
                                    .min_perms        = AV_PERM_READ },
                                  { .name = NULL}},

    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
