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

/* #define DEBUG */

#include <opencv/cv.h>
#include <opencv/cxtypes.h>
#include "libavutil/avstring.h"
#include "libavutil/file.h"
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

static int read_shape_from_file(int *cols, int *rows, int **values, const char *filename,
                                void *log_ctx)
{
    uint8_t *buf, *p, *pend;
    size_t size;
    int ret, i, j, w;

    if ((ret = av_file_map(filename, &buf, &size, 0, log_ctx)) < 0)
        return ret;

    /* prescan file to get the number of lines and the maximum width */
    w = 0;
    for (i = 0; i < size; i++) {
        if (buf[i] == '\n') {
            if (*rows == INT_MAX) {
                av_log(log_ctx, AV_LOG_ERROR, "Overflow on the number of rows in the file\n");
                return AVERROR_INVALIDDATA;
            }
            ++(*rows);
            *cols = FFMAX(*cols, w);
            w = 0;
        } else if (w == INT_MAX) {
            av_log(log_ctx, AV_LOG_ERROR, "Overflow on the number of columns in the file\n");
            return AVERROR_INVALIDDATA;
        }
        w++;
    }
    if (*rows > (FF_INTERNAL_MEM_TYPE_MAX_VALUE / (sizeof(int)) / *cols)) {
        av_log(log_ctx, AV_LOG_ERROR, "File with size %dx%d is too big\n",
               *rows, *cols);
        return AVERROR_INVALIDDATA;
    }
    if (!(*values = av_mallocz(sizeof(int) * *rows * *cols)))
        return AVERROR(ENOMEM);

    /* fill *values */
    p    = buf;
    pend = buf + size-1;
    for (i = 0; i < *rows; i++) {
        for (j = 0;; j++) {
            if (p > pend || *p == '\n') {
                p++;
                break;
            } else
                (*values)[*cols*i + j] = !!isgraph(*(p++));
        }
    }
    av_file_unmap(buf, size);

#ifdef DEBUG
    {
        char *line;
        if (!(line = av_malloc(*cols + 1)))
            return AVERROR(ENOMEM);
        for (i = 0; i < *rows; i++) {
            for (j = 0; j < *cols; j++)
                line[j] = (*values)[i * *cols + j] ? '@' : ' ';
            line[j] = 0;
            av_log(log_ctx, AV_LOG_DEBUG, "%3d: %s\n", i, line);
        }
        av_free(line);
    }
#endif

    return 0;
}

static int parse_iplconvkernel(IplConvKernel **kernel, char *buf, void *log_ctx)
{
    char shape_filename[128] = "", shape_str[32] = "rect";
    int cols = 0, rows = 0, anchor_x = 0, anchor_y = 0, shape = CV_SHAPE_RECT;
    int *values = NULL, ret;

    sscanf(buf, "%dx%d+%dx%d/%32[^=]=%127s", &cols, &rows, &anchor_x, &anchor_y, shape_str, shape_filename);

    if      (!strcmp(shape_str, "rect"   )) shape = CV_SHAPE_RECT;
    else if (!strcmp(shape_str, "cross"  )) shape = CV_SHAPE_CROSS;
    else if (!strcmp(shape_str, "ellipse")) shape = CV_SHAPE_ELLIPSE;
    else if (!strcmp(shape_str, "custom" )) {
        shape = CV_SHAPE_CUSTOM;
        if ((ret = read_shape_from_file(&cols, &rows, &values, shape_filename, log_ctx)) < 0)
            return ret;
    } else {
        av_log(log_ctx, AV_LOG_ERROR,
               "Shape unspecified or type '%s' unknown\n.", shape_str);
        return AVERROR(EINVAL);
    }

    if (rows <= 0 || cols <= 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Invalid non-positive values for shape size %dx%d\n", cols, rows);
        return AVERROR(EINVAL);
    }

    if (anchor_x < 0 || anchor_y < 0 || anchor_x >= cols || anchor_y >= rows) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Shape anchor %dx%d is not inside the rectangle with size %dx%d.\n",
               anchor_x, anchor_y, cols, rows);
        return AVERROR(EINVAL);
    }

    *kernel = cvCreateStructuringElementEx(cols, rows, anchor_x, anchor_y, shape, values);
    av_freep(&values);
    if (!*kernel)
        return AVERROR(ENOMEM);

    av_log(log_ctx, AV_LOG_INFO, "Structuring element: w:%d h:%d x:%d y:%d shape:%s\n",
           rows, cols, anchor_x, anchor_y, shape_str);
    return 0;
}

typedef struct {
    int nb_iterations;
    IplConvKernel *kernel;
} DilateContext;

static av_cold int dilate_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    OCVContext *ocv = ctx->priv;
    DilateContext *dilate = ocv->priv;
    char default_kernel_str[] = "3x3+0x0/rect";
    char *kernel_str;
    const char *buf = args;
    int ret;

    dilate->nb_iterations = 1;

    if (args)
        kernel_str = av_get_token(&buf, ":");
    if ((ret = parse_iplconvkernel(&dilate->kernel,
                                   *kernel_str ? kernel_str : default_kernel_str,
                                   ctx)) < 0)
        return ret;
    av_free(kernel_str);

    sscanf(buf, ":%d", &dilate->nb_iterations);
    av_log(ctx, AV_LOG_INFO, "iterations_nb:%d\n", dilate->nb_iterations);
    if (dilate->nb_iterations <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid non-positive value '%d' for nb_iterations\n",
               dilate->nb_iterations);
        return AVERROR(EINVAL);
    }
    return 0;
}

static av_cold void dilate_uninit(AVFilterContext *ctx)
{
    OCVContext *ocv = ctx->priv;
    DilateContext *dilate = ocv->priv;

    cvReleaseStructuringElement(&dilate->kernel);
}

static void dilate_end_frame_filter(AVFilterContext *ctx, IplImage *inimg, IplImage *outimg)
{
    OCVContext *ocv = ctx->priv;
    DilateContext *dilate = ocv->priv;
    cvDilate(inimg, outimg, dilate->kernel, dilate->nb_iterations);
}

static void erode_end_frame_filter(AVFilterContext *ctx, IplImage *inimg, IplImage *outimg)
{
    OCVContext *ocv = ctx->priv;
    DilateContext *dilate = ocv->priv;
    cvErode(inimg, outimg, dilate->kernel, dilate->nb_iterations);
}

typedef struct {
    const char *name;
    size_t priv_size;
    int  (*init)(AVFilterContext *ctx, const char *args, void *opaque);
    void (*uninit)(AVFilterContext *ctx);
    void (*end_frame_filter)(AVFilterContext *ctx, IplImage *inimg, IplImage *outimg);
} OCVFilterEntry;

static OCVFilterEntry ocv_filter_entries[] = {
    { "dilate", sizeof(DilateContext), dilate_init, dilate_uninit, dilate_end_frame_filter },
    { "erode",  sizeof(DilateContext), dilate_init, dilate_uninit, erode_end_frame_filter  },
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
