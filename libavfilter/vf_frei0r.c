/*
 * Copyright (c) 2010 Stefano Sabatini
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
 * frei0r wrapper
 */

#include <dlfcn.h>
#include <frei0r.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef f0r_instance_t (*f0r_construct_f)(unsigned int width, unsigned int height);
typedef void (*f0r_destruct_f)(f0r_instance_t instance);
typedef void (*f0r_deinit_f)(void);
typedef int (*f0r_init_f)(void);
typedef void (*f0r_get_plugin_info_f)(f0r_plugin_info_t *info);
typedef void (*f0r_get_param_info_f)(f0r_param_info_t *info, int param_index);
typedef void (*f0r_update_f)(f0r_instance_t instance, double time, const uint32_t *inframe, uint32_t *outframe);
typedef void (*f0r_update2_f)(f0r_instance_t instance, double time, const uint32_t *inframe1, const uint32_t *inframe2, const uint32_t *inframe3, uint32_t *outframe);
typedef void (*f0r_set_param_value_f)(f0r_instance_t instance, f0r_param_t param, int param_index);
typedef void (*f0r_get_param_value_f)(f0r_instance_t instance, f0r_param_t param, int param_index);

typedef struct Frei0rContext {
    const AVClass *class;
    f0r_update_f update;
    void *dl_handle;            /* dynamic library handle   */
    f0r_instance_t instance;
    f0r_plugin_info_t plugin_info;

    f0r_get_param_info_f  get_param_info;
    f0r_get_param_value_f get_param_value;
    f0r_set_param_value_f set_param_value;
    f0r_construct_f       construct;
    f0r_destruct_f        destruct;
    f0r_deinit_f          deinit;

    char *dl_name;
    char *params;
    AVRational framerate;

    /* only used by the source */
    int w, h;
    AVRational time_base;
    uint64_t pts;
} Frei0rContext;

static void *load_sym(AVFilterContext *ctx, const char *sym_name)
{
    Frei0rContext *s = ctx->priv;
    void *sym = dlsym(s->dl_handle, sym_name);
    if (!sym)
        av_log(ctx, AV_LOG_ERROR, "Could not find symbol '%s' in loaded module.\n", sym_name);
    return sym;
}

static int set_param(AVFilterContext *ctx, f0r_param_info_t info, int index, char *param)
{
    Frei0rContext *s = ctx->priv;
    union {
        double d;
        f0r_param_color_t col;
        f0r_param_position_t pos;
        f0r_param_string str;
    } val;
    char *tail;
    uint8_t rgba[4];

    switch (info.type) {
    case F0R_PARAM_BOOL:
        if      (!strcmp(param, "y")) val.d = 1.0;
        else if (!strcmp(param, "n")) val.d = 0.0;
        else goto fail;
        break;

    case F0R_PARAM_DOUBLE:
        val.d = av_strtod(param, &tail);
        if (*tail || val.d == HUGE_VAL)
            goto fail;
        break;

    case F0R_PARAM_COLOR:
        if (sscanf(param, "%f/%f/%f", &val.col.r, &val.col.g, &val.col.b) != 3) {
            if (av_parse_color(rgba, param, -1, ctx) < 0)
                goto fail;
            val.col.r = rgba[0] / 255.0;
            val.col.g = rgba[1] / 255.0;
            val.col.b = rgba[2] / 255.0;
        }
        break;

    case F0R_PARAM_POSITION:
        if (sscanf(param, "%lf/%lf", &val.pos.x, &val.pos.y) != 2)
            goto fail;
        break;

    case F0R_PARAM_STRING:
        val.str = param;
        break;
    }

    s->set_param_value(s->instance, &val, index);
    return 0;

fail:
    av_log(ctx, AV_LOG_ERROR, "Invalid value '%s' for parameter '%s'.\n",
           param, info.name);
    return AVERROR(EINVAL);
}

static int set_params(AVFilterContext *ctx, const char *params)
{
    Frei0rContext *s = ctx->priv;
    int i;

    if (!params)
        return 0;

    for (i = 0; i < s->plugin_info.num_params; i++) {
        f0r_param_info_t info;
        char *param;
        int ret;

        s->get_param_info(&info, i);

        if (*params) {
            if (!(param = av_get_token(&params, "|")))
                return AVERROR(ENOMEM);
            if (*params)
                params++;               /* skip ':' */
            ret = set_param(ctx, info, i, param);
            av_free(param);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static int load_path(AVFilterContext *ctx, void **handle_ptr, const char *prefix, const char *name)
{
    char *path = av_asprintf("%s%s%s", prefix, name, SLIBSUF);
    if (!path)
        return AVERROR(ENOMEM);
    av_log(ctx, AV_LOG_DEBUG, "Looking for frei0r effect in '%s'.\n", path);
    *handle_ptr = dlopen(path, RTLD_NOW|RTLD_LOCAL);
    av_free(path);
    return 0;
}

static av_cold int frei0r_init(AVFilterContext *ctx,
                               const char *dl_name, int type)
{
    Frei0rContext *s = ctx->priv;
    f0r_init_f            f0r_init;
    f0r_get_plugin_info_f f0r_get_plugin_info;
    f0r_plugin_info_t *pi;
    char *path;
    int ret = 0;
    int i;
    static const char* const frei0r_pathlist[] = {
        "/usr/local/lib/frei0r-1/",
        "/usr/lib/frei0r-1/",
        "/usr/local/lib64/frei0r-1/",
        "/usr/lib64/frei0r-1/"
    };

    if (!dl_name) {
        av_log(ctx, AV_LOG_ERROR, "No filter name provided.\n");
        return AVERROR(EINVAL);
    }

    /* see: http://frei0r.dyne.org/codedoc/html/group__pluglocations.html */
    if ((path = av_strdup(getenv("FREI0R_PATH")))) {
#ifdef _WIN32
        const char *separator = ";";
#else
        const char *separator = ":";
#endif
        char *p, *ptr = NULL;
        for (p = path; p = av_strtok(p, separator, &ptr); p = NULL) {
            /* add additional trailing slash in case it is missing */
            char *p1 = av_asprintf("%s/", p);
            if (!p1) {
                ret = AVERROR(ENOMEM);
                goto check_path_end;
            }
            ret = load_path(ctx, &s->dl_handle, p1, dl_name);
            av_free(p1);
            if (ret < 0)
                goto check_path_end;
            if (s->dl_handle)
                break;
        }

    check_path_end:
        av_free(path);
        if (ret < 0)
            return ret;
    }
    if (!s->dl_handle && (path = getenv("HOME"))) {
        char *prefix = av_asprintf("%s/.frei0r-1/lib/", path);
        if (!prefix)
            return AVERROR(ENOMEM);
        ret = load_path(ctx, &s->dl_handle, prefix, dl_name);
        av_free(prefix);
        if (ret < 0)
            return ret;
    }
    for (i = 0; !s->dl_handle && i < FF_ARRAY_ELEMS(frei0r_pathlist); i++) {
        ret = load_path(ctx, &s->dl_handle, frei0r_pathlist[i], dl_name);
        if (ret < 0)
            return ret;
    }
    if (!s->dl_handle) {
        av_log(ctx, AV_LOG_ERROR, "Could not find module '%s'.\n", dl_name);
        return AVERROR(EINVAL);
    }

    if (!(f0r_init                = load_sym(ctx, "f0r_init"           )) ||
        !(f0r_get_plugin_info     = load_sym(ctx, "f0r_get_plugin_info")) ||
        !(s->get_param_info  = load_sym(ctx, "f0r_get_param_info" )) ||
        !(s->get_param_value = load_sym(ctx, "f0r_get_param_value")) ||
        !(s->set_param_value = load_sym(ctx, "f0r_set_param_value")) ||
        !(s->update          = load_sym(ctx, "f0r_update"         )) ||
        !(s->construct       = load_sym(ctx, "f0r_construct"      )) ||
        !(s->destruct        = load_sym(ctx, "f0r_destruct"       )) ||
        !(s->deinit          = load_sym(ctx, "f0r_deinit"         )))
        return AVERROR(EINVAL);

    if (f0r_init() < 0) {
        av_log(ctx, AV_LOG_ERROR, "Could not init the frei0r module.\n");
        return AVERROR(EINVAL);
    }

    f0r_get_plugin_info(&s->plugin_info);
    pi = &s->plugin_info;
    if (pi->plugin_type != type) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid type '%s' for this plugin\n",
               pi->plugin_type == F0R_PLUGIN_TYPE_FILTER ? "filter" :
               pi->plugin_type == F0R_PLUGIN_TYPE_SOURCE ? "source" :
               pi->plugin_type == F0R_PLUGIN_TYPE_MIXER2 ? "mixer2" :
               pi->plugin_type == F0R_PLUGIN_TYPE_MIXER3 ? "mixer3" : "unknown");
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_VERBOSE,
           "name:%s author:'%s' explanation:'%s' color_model:%s "
           "frei0r_version:%d version:%d.%d num_params:%d\n",
           pi->name, pi->author, pi->explanation,
           pi->color_model == F0R_COLOR_MODEL_BGRA8888 ? "bgra8888" :
           pi->color_model == F0R_COLOR_MODEL_RGBA8888 ? "rgba8888" :
           pi->color_model == F0R_COLOR_MODEL_PACKED32 ? "packed32" : "unknown",
           pi->frei0r_version, pi->major_version, pi->minor_version, pi->num_params);

    return 0;
}

static av_cold int filter_init(AVFilterContext *ctx)
{
    Frei0rContext *s = ctx->priv;

    return frei0r_init(ctx, s->dl_name, F0R_PLUGIN_TYPE_FILTER);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    Frei0rContext *s = ctx->priv;

    if (s->destruct && s->instance)
        s->destruct(s->instance);
    if (s->deinit)
        s->deinit();
    if (s->dl_handle)
        dlclose(s->dl_handle);
}

static int config_input_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    Frei0rContext *s = ctx->priv;

    if (s->destruct && s->instance)
        s->destruct(s->instance);
    if (!(s->instance = s->construct(inlink->w, inlink->h))) {
        av_log(ctx, AV_LOG_ERROR, "Impossible to load frei0r instance.\n");
        return AVERROR(EINVAL);
    }

    return set_params(ctx, s->params);
}

static int query_formats(AVFilterContext *ctx)
{
    Frei0rContext *s = ctx->priv;
    AVFilterFormats *formats = NULL;
    int ret;

    if        (s->plugin_info.color_model == F0R_COLOR_MODEL_BGRA8888) {
        if ((ret = ff_add_format(&formats, AV_PIX_FMT_BGRA)) < 0)
            return ret;
    } else if (s->plugin_info.color_model == F0R_COLOR_MODEL_RGBA8888) {
        if ((ret = ff_add_format(&formats, AV_PIX_FMT_RGBA)) < 0)
            return ret;
    } else {                                   /* F0R_COLOR_MODEL_PACKED32 */
        static const enum AVPixelFormat pix_fmts[] = {
            AV_PIX_FMT_BGRA, AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR, AV_PIX_FMT_NONE
        };
        formats = ff_make_format_list(pix_fmts);
    }

    if (!formats)
        return AVERROR(ENOMEM);

    return ff_set_common_formats(ctx, formats);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    Frei0rContext *s = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *out = ff_default_get_video_buffer2(outlink, outlink->w, outlink->h, 16);
    if (!out)
        goto fail;

    av_frame_copy_props(out, in);

    if (in->linesize[0] != out->linesize[0]) {
        AVFrame *in2 = ff_default_get_video_buffer2(outlink, outlink->w, outlink->h, 16);
        if (!in2)
            goto fail;
        av_frame_copy(in2, in);
        av_frame_free(&in);
        in = in2;
    }

    s->update(s->instance, in->pts * av_q2d(inlink->time_base) * 1000,
                   (const uint32_t *)in->data[0],
                   (uint32_t *)out->data[0]);

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return AVERROR(ENOMEM);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    Frei0rContext *s = ctx->priv;
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return set_params(ctx, s->params);
}

#define OFFSET(x) offsetof(Frei0rContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
#define TFLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM
static const AVOption frei0r_options[] = {
    { "filter_name",   NULL, OFFSET(dl_name), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "filter_params", NULL, OFFSET(params),  AV_OPT_TYPE_STRING, .flags = TFLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(frei0r);

static const AVFilterPad avfilter_vf_frei0r_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_props,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_frei0r_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_frei0r = {
    .name          = "frei0r",
    .description   = NULL_IF_CONFIG_SMALL("Apply a frei0r effect."),
    .query_formats = query_formats,
    .init          = filter_init,
    .uninit        = uninit,
    .priv_size     = sizeof(Frei0rContext),
    .priv_class    = &frei0r_class,
    .inputs        = avfilter_vf_frei0r_inputs,
    .outputs       = avfilter_vf_frei0r_outputs,
    .process_command = process_command,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};

static av_cold int source_init(AVFilterContext *ctx)
{
    Frei0rContext *s = ctx->priv;

    s->time_base.num = s->framerate.den;
    s->time_base.den = s->framerate.num;

    return frei0r_init(ctx, s->dl_name, F0R_PLUGIN_TYPE_SOURCE);
}

static int source_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    Frei0rContext *s = ctx->priv;

    if (av_image_check_size(s->w, s->h, 0, ctx) < 0)
        return AVERROR(EINVAL);
    outlink->w = s->w;
    outlink->h = s->h;
    outlink->time_base = s->time_base;
    outlink->frame_rate = av_inv_q(s->time_base);
    outlink->sample_aspect_ratio = (AVRational){1,1};

    if (s->destruct && s->instance)
        s->destruct(s->instance);
    if (!(s->instance = s->construct(outlink->w, outlink->h))) {
        av_log(ctx, AV_LOG_ERROR, "Impossible to load frei0r instance.\n");
        return AVERROR(EINVAL);
    }
    if (!s->params) {
        av_log(ctx, AV_LOG_ERROR, "frei0r filter parameters not set.\n");
        return AVERROR(EINVAL);
    }

    return set_params(ctx, s->params);
}

static int source_request_frame(AVFilterLink *outlink)
{
    Frei0rContext *s = outlink->src->priv;
    AVFrame *frame = ff_default_get_video_buffer2(outlink, outlink->w, outlink->h, 16);

    if (!frame)
        return AVERROR(ENOMEM);

    frame->sample_aspect_ratio = (AVRational) {1, 1};
    frame->pts = s->pts++;

    s->update(s->instance, av_rescale_q(frame->pts, s->time_base, (AVRational){1,1000}),
                   NULL, (uint32_t *)frame->data[0]);

    return ff_filter_frame(outlink, frame);
}

static const AVOption frei0r_src_options[] = {
    { "size",          "Dimensions of the generated video.", OFFSET(w),         AV_OPT_TYPE_IMAGE_SIZE, { .str = "320x240" }, .flags = FLAGS },
    { "framerate",     NULL,                                 OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, { .str = "25" }, 0, INT_MAX, .flags = FLAGS },
    { "filter_name",   NULL,                                 OFFSET(dl_name),   AV_OPT_TYPE_STRING,                  .flags = FLAGS },
    { "filter_params", NULL,                                 OFFSET(params),    AV_OPT_TYPE_STRING,                  .flags = FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(frei0r_src);

static const AVFilterPad avfilter_vsrc_frei0r_src_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = source_request_frame,
        .config_props  = source_config_props
    },
    { NULL }
};

AVFilter ff_vsrc_frei0r_src = {
    .name          = "frei0r_src",
    .description   = NULL_IF_CONFIG_SMALL("Generate a frei0r source."),
    .priv_size     = sizeof(Frei0rContext),
    .priv_class    = &frei0r_src_class,
    .init          = source_init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = NULL,
    .outputs       = avfilter_vsrc_frei0r_src_outputs,
};
