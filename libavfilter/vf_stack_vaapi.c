/*
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
 * Hardware accelerated hstack, vstack and xstack filters based on VA-API
 */

#include "config_components.h"

#include "libavutil/opt.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/eval.h"
#include "libavutil/hwcontext.h"
#include "libavutil/avstring.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/mem.h"

#include "internal.h"
#include "filters.h"
#include "formats.h"
#include "video.h"
#include "framesync.h"
#include "vaapi_vpp.h"

#define OFFSET(x) offsetof(StackVAAPIContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)

enum {
    STACK_VAAPI_H = 0,
    STACK_VAAPI_V = 1,
    STACK_VAAPI_X = 2
};

typedef struct StackVAAPIContext {
    VAAPIVPPContext vppctx; /**< must be the first field */

    FFFrameSync fs;
    int mode;
    VARectangle *rects;
    uint8_t fillcolor[4];
    int fillcolor_enable;

    /* Options */
    int nb_inputs;
    int shortest;
    int tile_width;
    int tile_height;
    int nb_grid_columns;
    int nb_grid_rows;
    char *layout;
    char *fillcolor_str;
} StackVAAPIContext;

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *avctx = fs->parent;
    AVFilterLink *outlink = avctx->outputs[0];
    StackVAAPIContext *sctx = fs->opaque;
    VAAPIVPPContext *vppctx = fs->opaque;
    AVFrame *oframe, *iframe;
    VAProcPipelineParameterBuffer *params = NULL;
    VARectangle *irect = NULL;
    int ret = 0;

    if (vppctx->va_context == VA_INVALID_ID)
        return AVERROR(EINVAL);

    oframe = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!oframe)
        return AVERROR(ENOMEM);

    irect = av_calloc(avctx->nb_inputs, sizeof(*irect));
    params = av_calloc(avctx->nb_inputs, sizeof(*params));
    if (!irect || !params) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (int i = 0; i < avctx->nb_inputs; i++) {
        ret = ff_framesync_get_frame(fs, i, &iframe, 0);
        if (ret)
            goto fail;

        if (i == 0) {
            ret = av_frame_copy_props(oframe, iframe);
            if (ret < 0)
                goto fail;
        }

        ret = ff_vaapi_vpp_init_params(avctx, &params[i], iframe, oframe);
        if (ret)
            goto fail;

        av_log(avctx, AV_LOG_DEBUG, "stack input %d: %s, %ux%u (%"PRId64").\n",
               i, av_get_pix_fmt_name(iframe->format),
               iframe->width, iframe->height, iframe->pts);
        irect[i].x = 0;
        irect[i].y = 0;
        irect[i].width = iframe->width;
        irect[i].height = iframe->height;
        params[i].surface_region = &irect[i];
        params[i].surface = (VASurfaceID)(uintptr_t)iframe->data[3];
        params[i].output_region = &sctx->rects[i];

        if (sctx->fillcolor_enable)
            params[i].output_background_color = (sctx->fillcolor[3] << 24 |
                                                 sctx->fillcolor[0] << 16 |
                                                 sctx->fillcolor[1] << 8 |
                                                 sctx->fillcolor[2]);
    }

    oframe->pts = av_rescale_q(sctx->fs.pts, sctx->fs.time_base, outlink->time_base);
    oframe->sample_aspect_ratio = outlink->sample_aspect_ratio;

    ret = ff_vaapi_vpp_render_pictures(avctx, params, avctx->nb_inputs, oframe);
    if (ret)
        goto fail;

    av_freep(&irect);
    av_freep(&params);
    return ff_filter_frame(outlink, oframe);

fail:
    av_freep(&irect);
    av_freep(&params);
    av_frame_free(&oframe);
    return ret;
}

static int init_framesync(AVFilterContext *avctx)
{
    StackVAAPIContext *sctx = avctx->priv;
    int ret;

    ret = ff_framesync_init(&sctx->fs, avctx, avctx->nb_inputs);
    if (ret < 0)
        return ret;

    sctx->fs.on_event = process_frame;
    sctx->fs.opaque = sctx;

    for (int i = 0; i < sctx->nb_inputs; i++) {
        FFFrameSyncIn *in = &sctx->fs.in[i];

        in->before = EXT_STOP;
        in->after = sctx->shortest ? EXT_STOP : EXT_INFINITY;
        in->sync = 1;
        in->time_base = avctx->inputs[i]->time_base;
    }

    return ff_framesync_configure(&sctx->fs);
}

#define SET_INPUT_REGION(rect, rx, ry, rw, rh) do {     \
        rect->x = rx;                                   \
        rect->y = ry;                                   \
        rect->width = rw;                               \
        rect->height = rh;                              \
    } while (0)

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    StackVAAPIContext *sctx = avctx->priv;
    VAAPIVPPContext *vppctx = avctx->priv;
    AVFilterLink *inlink0 = avctx->inputs[0];
    AVHWFramesContext *hwfc0 = NULL;
    int width, height, ret;

    if (inlink0->format != AV_PIX_FMT_VAAPI || !inlink0->hw_frames_ctx || !inlink0->hw_frames_ctx->data) {
        av_log(avctx, AV_LOG_ERROR, "Software pixel format is not supported.\n");
        return AVERROR(EINVAL);
    }

    hwfc0 = (AVHWFramesContext *)inlink0->hw_frames_ctx->data;

    for (int i = 1; i < sctx->nb_inputs; i++) {
        AVFilterLink *inlink = avctx->inputs[i];
        AVHWFramesContext *hwfc = NULL;

        if (inlink->format != AV_PIX_FMT_VAAPI || !inlink->hw_frames_ctx || !inlink->hw_frames_ctx->data) {
            av_log(avctx, AV_LOG_ERROR, "Software pixel format is not supported.\n");
            return AVERROR(EINVAL);
        }

        hwfc = (AVHWFramesContext *)inlink->hw_frames_ctx->data;

        if (hwfc0->sw_format != hwfc->sw_format) {
            av_log(avctx, AV_LOG_ERROR, "All inputs should have the same underlying software pixel format.\n");
            return AVERROR(EINVAL);
        }

        if (hwfc0->device_ctx != hwfc->device_ctx) {
            av_log(avctx, AV_LOG_ERROR, "All inputs should have the same underlying vaapi devices.\n");
            return AVERROR(EINVAL);
        }
    }

    ff_vaapi_vpp_config_input(inlink0);
    vppctx->output_format = hwfc0->sw_format;

    if (sctx->mode == STACK_VAAPI_H) {
        height = sctx->tile_height;
        width = 0;

        if (!height)
            height = inlink0->h;

        for (int i = 0; i < sctx->nb_inputs; i++) {
            AVFilterLink *inlink = avctx->inputs[i];
            VARectangle *rect = &sctx->rects[i];

            SET_INPUT_REGION(rect, width, 0, av_rescale(height, inlink->w, inlink->h), height);
            width += av_rescale(height, inlink->w, inlink->h);
        }
    } else if (sctx->mode == STACK_VAAPI_V) {
        height = 0;
        width = sctx->tile_width;

        if (!width)
            width = inlink0->w;

        for (int i = 0; i < sctx->nb_inputs; i++) {
            AVFilterLink *inlink = avctx->inputs[i];
            VARectangle *rect = &sctx->rects[i];

            SET_INPUT_REGION(rect, 0, height, width, av_rescale(width, inlink->h, inlink->w));
            height += av_rescale(width, inlink->h, inlink->w);
        }
    } else if (sctx->nb_grid_rows && sctx->nb_grid_columns) {
        int xpos = 0, ypos = 0;
        int ow, oh, k = 0;

        ow = sctx->tile_width;
        oh = sctx->tile_height;

        if (!ow || !oh) {
            ow = avctx->inputs[0]->w;
            oh = avctx->inputs[0]->h;
        }

        for (int i = 0; i < sctx->nb_grid_columns; i++) {
            ypos = 0;

            for (int j = 0; j < sctx->nb_grid_rows; j++) {
                VARectangle *rect = &sctx->rects[k++];

                SET_INPUT_REGION(rect, xpos, ypos, ow, oh);
                ypos += oh;
            }

            xpos += ow;
        }

        width = ow * sctx->nb_grid_columns;
        height = oh * sctx->nb_grid_rows;
    } else {
        char *arg, *p = sctx->layout, *saveptr = NULL;
        char *arg2, *p2, *saveptr2 = NULL;
        char *arg3, *p3, *saveptr3 = NULL;
        int xpos, ypos, size;
        int ow, oh;

        width = avctx->inputs[0]->w;
        height = avctx->inputs[0]->h;

        for (int i = 0; i < sctx->nb_inputs; i++) {
            AVFilterLink *inlink = avctx->inputs[i];
            VARectangle *rect = &sctx->rects[i];

            ow = inlink->w;
            oh = inlink->h;

            if (!(arg = av_strtok(p, "|", &saveptr)))
                return AVERROR(EINVAL);

            p = NULL;
            p2 = arg;
            xpos = ypos = 0;

            for (int j = 0; j < 3; j++) {
                if (!(arg2 = av_strtok(p2, "_", &saveptr2))) {
                    if (j == 2)
                        break;
                    else
                        return AVERROR(EINVAL);
                }

                p2 = NULL;
                p3 = arg2;

                if (j == 2) {
                    if ((ret = av_parse_video_size(&ow, &oh, p3)) < 0) {
                        av_log(avctx, AV_LOG_ERROR, "Invalid size '%s'\n", p3);
                        return ret;
                    }

                    break;
                }

                while ((arg3 = av_strtok(p3, "+", &saveptr3))) {
                    p3 = NULL;
                    if (sscanf(arg3, "w%d", &size) == 1) {
                        if (size == i || size < 0 || size >= sctx->nb_inputs)
                            return AVERROR(EINVAL);

                        if (!j)
                            xpos += sctx->rects[size].width;
                        else
                            ypos += sctx->rects[size].width;
                    } else if (sscanf(arg3, "h%d", &size) == 1) {
                        if (size == i || size < 0 || size >= sctx->nb_inputs)
                            return AVERROR(EINVAL);

                        if (!j)
                            xpos += sctx->rects[size].height;
                        else
                            ypos += sctx->rects[size].height;
                    } else if (sscanf(arg3, "%d", &size) == 1) {
                        if (size < 0)
                            return AVERROR(EINVAL);

                        if (!j)
                            xpos += size;
                        else
                            ypos += size;
                    } else {
                        return AVERROR(EINVAL);
                    }
                }
            }

            SET_INPUT_REGION(rect, xpos, ypos, ow, oh);
            width = FFMAX(width,  xpos + ow);
            height = FFMAX(height, ypos + oh);
        }

    }

    outlink->w = width;
    outlink->h = height;
    outlink->frame_rate = inlink0->frame_rate;
    outlink->sample_aspect_ratio = inlink0->sample_aspect_ratio;

    for (int i = 1; i < sctx->nb_inputs; i++) {
        AVFilterLink *inlink = avctx->inputs[i];
        if (outlink->frame_rate.num != inlink->frame_rate.num ||
            outlink->frame_rate.den != inlink->frame_rate.den) {
            av_log(avctx, AV_LOG_VERBOSE,
                    "Video inputs have different frame rates, output will be VFR\n");
            outlink->frame_rate = av_make_q(1, 0);
            break;
        }
    }

    ret = init_framesync(avctx);
    if (ret < 0)
        return ret;

    outlink->time_base = sctx->fs.time_base;

    vppctx->output_width = width;
    vppctx->output_height = height;

    return ff_vaapi_vpp_config_output(outlink);
}

static int vaapi_stack_init(AVFilterContext *avctx)
{
    StackVAAPIContext *sctx = avctx->priv;
    VAAPIVPPContext *vppctx = avctx->priv;
    int ret;

    if (!strcmp(avctx->filter->name, "hstack_vaapi"))
        sctx->mode = STACK_VAAPI_H;
    else if (!strcmp(avctx->filter->name, "vstack_vaapi"))
        sctx->mode = STACK_VAAPI_V;
    else {
        int is_grid;

        av_assert0(strcmp(avctx->filter->name, "xstack_vaapi") == 0);
        sctx->mode = STACK_VAAPI_X;
        is_grid = sctx->nb_grid_rows && sctx->nb_grid_columns;

        if (sctx->layout && is_grid) {
            av_log(avctx, AV_LOG_ERROR, "Both layout and grid were specified. Only one is allowed.\n");
            return AVERROR(EINVAL);
        }

        if (!sctx->layout && !is_grid) {
            if (sctx->nb_inputs == 2) {
                sctx->nb_grid_rows = 1;
                sctx->nb_grid_columns = 2;
                is_grid = 1;
            } else {
                av_log(avctx, AV_LOG_ERROR, "No layout or grid specified.\n");
                return AVERROR(EINVAL);
            }
        }

        if (is_grid)
            sctx->nb_inputs = sctx->nb_grid_rows * sctx->nb_grid_columns;

        if (strcmp(sctx->fillcolor_str, "none") &&
            av_parse_color(sctx->fillcolor, sctx->fillcolor_str, -1, avctx) >= 0) {
            sctx->fillcolor_enable = 1;
        } else {
            sctx->fillcolor_enable = 0;
        }
    }

    for (int i = 0; i < sctx->nb_inputs; i++) {
        AVFilterPad pad = { 0 };

        pad.type = AVMEDIA_TYPE_VIDEO;
        pad.name = av_asprintf("input%d", i);

        if (!pad.name)
            return AVERROR(ENOMEM);

        if ((ret = ff_append_inpad_free_name(avctx, &pad)) < 0)
            return ret;
    }

    /* stack region */
    sctx->rects = av_calloc(sctx->nb_inputs, sizeof(*sctx->rects));
    if (!sctx->rects)
        return AVERROR(ENOMEM);

    ff_vaapi_vpp_ctx_init(avctx);
    vppctx->output_format = AV_PIX_FMT_NONE;

    return 0;
}

static av_cold void vaapi_stack_uninit(AVFilterContext *avctx)
{
    StackVAAPIContext *sctx = avctx->priv;

    ff_framesync_uninit(&sctx->fs);
    av_freep(&sctx->rects);
}

static int vaapi_stack_activate(AVFilterContext *avctx)
{
    StackVAAPIContext *sctx = avctx->priv;
    return ff_framesync_activate(&sctx->fs);
}

static int vaapi_stack_query_formats(AVFilterContext *avctx)
{
    static const enum AVPixelFormat pixel_formats[] = {
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE,
    };

    return ff_set_common_formats_from_list(avctx, pixel_formats);
}

static const AVFilterPad vaapi_stack_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

#define STACK_COMMON_OPTS \
    { "inputs", "Set number of inputs", OFFSET(nb_inputs), AV_OPT_TYPE_INT, { .i64 = 2 }, 2, UINT16_MAX, .flags = FLAGS },                   \
    { "shortest", "Force termination when the shortest input terminates", OFFSET(shortest), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },

#if CONFIG_HSTACK_VAAPI_FILTER

static const AVOption hstack_vaapi_options[] = {
    STACK_COMMON_OPTS

    { "height", "Set output height (0 to use the height of input 0)", OFFSET(tile_height), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, UINT16_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(hstack_vaapi);

const AVFilter ff_vf_hstack_vaapi = {
    .name           = "hstack_vaapi",
    .description    = NULL_IF_CONFIG_SMALL("VA-API hstack."),
    .priv_size      = sizeof(StackVAAPIContext),
    .priv_class     = &hstack_vaapi_class,
    .init           = vaapi_stack_init,
    .uninit         = vaapi_stack_uninit,
    .activate       = vaapi_stack_activate,
    FILTER_QUERY_FUNC(vaapi_stack_query_formats),
    FILTER_OUTPUTS(vaapi_stack_outputs),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_DYNAMIC_INPUTS,
};

#endif

#if CONFIG_VSTACK_VAAPI_FILTER

static const AVOption vstack_vaapi_options[] = {
    STACK_COMMON_OPTS

    { "width",   "Set output width (0 to use the width of input 0)", OFFSET(tile_width), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, UINT16_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(vstack_vaapi);

const AVFilter ff_vf_vstack_vaapi = {
    .name           = "vstack_vaapi",
    .description    = NULL_IF_CONFIG_SMALL("VA-API vstack."),
    .priv_size      = sizeof(StackVAAPIContext),
    .priv_class     = &vstack_vaapi_class,
    .init           = vaapi_stack_init,
    .uninit         = vaapi_stack_uninit,
    .activate       = vaapi_stack_activate,
    FILTER_QUERY_FUNC(vaapi_stack_query_formats),
    FILTER_OUTPUTS(vaapi_stack_outputs),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_DYNAMIC_INPUTS,
};

#endif

#if CONFIG_XSTACK_VAAPI_FILTER

static const AVOption xstack_vaapi_options[] = {
    STACK_COMMON_OPTS

    { "layout", "Set custom layout", OFFSET(layout), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, .flags = FLAGS },
    { "grid",   "set fixed size grid layout", OFFSET(nb_grid_columns), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, .flags = FLAGS },
    { "grid_tile_size",   "set tile size in grid layout", OFFSET(tile_width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, .flags = FLAGS },
    { "fill",   "Set the color for unused pixels", OFFSET(fillcolor_str), AV_OPT_TYPE_STRING, {.str = "none"}, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(xstack_vaapi);

const AVFilter ff_vf_xstack_vaapi = {
    .name           = "xstack_vaapi",
    .description    = NULL_IF_CONFIG_SMALL("VA-API xstack."),
    .priv_size      = sizeof(StackVAAPIContext),
    .priv_class     = &xstack_vaapi_class,
    .init           = vaapi_stack_init,
    .uninit         = vaapi_stack_uninit,
    .activate       = vaapi_stack_activate,
    FILTER_OUTPUTS(vaapi_stack_outputs),
    FILTER_QUERY_FUNC(vaapi_stack_query_formats),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_DYNAMIC_INPUTS,
};

#endif
