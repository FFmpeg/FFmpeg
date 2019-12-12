/*
 * Copyright (c) 2014-2015 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @todo switch to dualinput
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "internal.h"

#include "lavfutils.h"

#define MAX_MIPMAPS 5

typedef struct FOCContext {
    AVClass *class;
    float threshold;
    int mipmaps;
    int xmin, ymin, xmax, ymax;
    char *obj_filename;
    int last_x, last_y;
    AVFrame *obj_frame;
    AVFrame *needle_frame[MAX_MIPMAPS];
    AVFrame *haystack_frame[MAX_MIPMAPS];
} FOCContext;

#define OFFSET(x) offsetof(FOCContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption find_rect_options[] = {
    { "object", "object bitmap filename", OFFSET(obj_filename), AV_OPT_TYPE_STRING, {.str = NULL}, .flags = FLAGS },
    { "threshold", "set threshold", OFFSET(threshold), AV_OPT_TYPE_FLOAT, {.dbl = 0.5}, 0, 1.0, FLAGS },
    { "mipmaps", "set mipmaps", OFFSET(mipmaps), AV_OPT_TYPE_INT, {.i64 = 3}, 1, MAX_MIPMAPS, FLAGS },
    { "xmin", "", OFFSET(xmin), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, FLAGS },
    { "ymin", "", OFFSET(ymin), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, FLAGS },
    { "xmax", "", OFFSET(xmax), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, FLAGS },
    { "ymax", "", OFFSET(ymax), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(find_rect);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static AVFrame *downscale(AVFrame *in)
{
    int x, y;
    AVFrame *frame = av_frame_alloc();
    uint8_t *src, *dst;
    if (!frame)
        return NULL;

    frame->format = in->format;
    frame->width  = (in->width + 1) / 2;
    frame->height = (in->height+ 1) / 2;

    if (av_frame_get_buffer(frame, 32) < 0) {
        av_frame_free(&frame);
        return NULL;
    }
    src = in   ->data[0];
    dst = frame->data[0];

    for(y = 0; y < frame->height; y++) {
        for(x = 0; x < frame->width; x++) {
            dst[x] = (  src[2*x+0]
                      + src[2*x+1]
                      + src[2*x+0 + in->linesize[0]]
                      + src[2*x+1 + in->linesize[0]]
                      + 2) >> 2;
        }
        src += 2*in->linesize[0];
        dst += frame->linesize[0];
    }
    return frame;
}

static float compare(const AVFrame *haystack, const AVFrame *obj, int offx, int offy)
{
    int x,y;
    int o_sum_v = 0;
    int h_sum_v = 0;
    int64_t oo_sum_v = 0;
    int64_t hh_sum_v = 0;
    int64_t oh_sum_v = 0;
    float c;
    int n = obj->height * obj->width;
    const uint8_t *odat = obj     ->data[0];
    const uint8_t *hdat = haystack->data[0] + offx + offy * haystack->linesize[0];
    int64_t o_sigma, h_sigma;

    for(y = 0; y < obj->height; y++) {
        for(x = 0; x < obj->width; x++) {
            int o_v = odat[x];
            int h_v = hdat[x];
            o_sum_v += o_v;
            h_sum_v += h_v;
            oo_sum_v += o_v * o_v;
            hh_sum_v += h_v * h_v;
            oh_sum_v += o_v * h_v;
        }
        odat += obj->linesize[0];
        hdat += haystack->linesize[0];
    }
    o_sigma = n*oo_sum_v - o_sum_v*(int64_t)o_sum_v;
    h_sigma = n*hh_sum_v - h_sum_v*(int64_t)h_sum_v;

    if (o_sigma == 0 || h_sigma == 0)
        return 1.0;

    c = (n*oh_sum_v - o_sum_v*(int64_t)h_sum_v) / (sqrt(o_sigma)*sqrt(h_sigma));

    return 1 - fabs(c);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    FOCContext *foc = ctx->priv;

    if (foc->xmax <= 0)
        foc->xmax = inlink->w - foc->obj_frame->width;
    if (foc->ymax <= 0)
        foc->ymax = inlink->h - foc->obj_frame->height;

    return 0;
}

static float search(FOCContext *foc, int pass, int maxpass, int xmin, int xmax, int ymin, int ymax, int *best_x, int *best_y, float best_score)
{
    int x, y;

    if (pass + 1 <= maxpass) {
        int sub_x, sub_y;
        search(foc, pass+1, maxpass, xmin>>1, (xmax+1)>>1, ymin>>1, (ymax+1)>>1, &sub_x, &sub_y, 1.0);
        xmin = FFMAX(xmin, 2*sub_x - 4);
        xmax = FFMIN(xmax, 2*sub_x + 4);
        ymin = FFMAX(ymin, 2*sub_y - 4);
        ymax = FFMIN(ymax, 2*sub_y + 4);
    }

    for (y = ymin; y <= ymax; y++) {
        for (x = xmin; x <= xmax; x++) {
            float score = compare(foc->haystack_frame[pass], foc->needle_frame[pass], x, y);
            av_assert0(score != 0);
            if (score < best_score) {
                best_score = score;
                *best_x = x;
                *best_y = y;
            }
        }
    }
    return best_score;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    FOCContext *foc = ctx->priv;
    float best_score;
    int best_x, best_y;
    int i;

    foc->haystack_frame[0] = av_frame_clone(in);
    for (i=1; i<foc->mipmaps; i++) {
        foc->haystack_frame[i] = downscale(foc->haystack_frame[i-1]);
    }

    best_score = search(foc, 0, 0,
                        FFMAX(foc->xmin, foc->last_x - 8),
                        FFMIN(foc->xmax, foc->last_x + 8),
                        FFMAX(foc->ymin, foc->last_y - 8),
                        FFMIN(foc->ymax, foc->last_y + 8),
                        &best_x, &best_y, 1.0);

    best_score = search(foc, 0, foc->mipmaps - 1, foc->xmin, foc->xmax, foc->ymin, foc->ymax,
                        &best_x, &best_y, best_score);

    for (i=0; i<MAX_MIPMAPS; i++) {
        av_frame_free(&foc->haystack_frame[i]);
    }

    if (best_score > foc->threshold) {
        return ff_filter_frame(ctx->outputs[0], in);
    }

    av_log(ctx, AV_LOG_DEBUG, "Found at %d %d score %f\n", best_x, best_y, best_score);
    foc->last_x = best_x;
    foc->last_y = best_y;

    av_frame_make_writable(in);

    av_dict_set_int(&in->metadata, "lavfi.rect.w", foc->obj_frame->width, 0);
    av_dict_set_int(&in->metadata, "lavfi.rect.h", foc->obj_frame->height, 0);
    av_dict_set_int(&in->metadata, "lavfi.rect.x", best_x, 0);
    av_dict_set_int(&in->metadata, "lavfi.rect.y", best_y, 0);

    return ff_filter_frame(ctx->outputs[0], in);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FOCContext *foc = ctx->priv;
    int i;

    for (i = 0; i < MAX_MIPMAPS; i++) {
        av_frame_free(&foc->needle_frame[i]);
        av_frame_free(&foc->haystack_frame[i]);
    }

    if (foc->obj_frame)
        av_freep(&foc->obj_frame->data[0]);
    av_frame_free(&foc->obj_frame);
}

static av_cold int init(AVFilterContext *ctx)
{
    FOCContext *foc = ctx->priv;
    int ret, i;

    if (!foc->obj_filename) {
        av_log(ctx, AV_LOG_ERROR, "object filename not set\n");
        return AVERROR(EINVAL);
    }

    foc->obj_frame = av_frame_alloc();
    if (!foc->obj_frame)
        return AVERROR(ENOMEM);

    if ((ret = ff_load_image(foc->obj_frame->data, foc->obj_frame->linesize,
                             &foc->obj_frame->width, &foc->obj_frame->height,
                             &foc->obj_frame->format, foc->obj_filename, ctx)) < 0)
        return ret;

    if (foc->obj_frame->format != AV_PIX_FMT_GRAY8) {
        av_log(ctx, AV_LOG_ERROR, "object image is not a grayscale image\n");
        return AVERROR(EINVAL);
    }

    foc->needle_frame[0] = av_frame_clone(foc->obj_frame);
    for (i = 1; i < foc->mipmaps; i++) {
        foc->needle_frame[i] = downscale(foc->needle_frame[i-1]);
        if (!foc->needle_frame[i])
            return AVERROR(ENOMEM);
    }

    return 0;
}

static const AVFilterPad foc_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad foc_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_find_rect = {
    .name            = "find_rect",
    .description     = NULL_IF_CONFIG_SMALL("Find a user specified object."),
    .priv_size       = sizeof(FOCContext),
    .init            = init,
    .uninit          = uninit,
    .query_formats   = query_formats,
    .inputs          = foc_inputs,
    .outputs         = foc_outputs,
    .priv_class      = &find_rect_class,
};
