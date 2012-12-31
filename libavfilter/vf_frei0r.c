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

/* #define DEBUG */

#include <dlfcn.h>
#include <frei0r.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
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
    char params[256];

    /* only used by the source */
    int w, h;
    AVRational time_base;
    uint64_t pts;
} Frei0rContext;

static void *load_sym(AVFilterContext *ctx, const char *sym_name)
{
    Frei0rContext *frei0r = ctx->priv;
    void *sym = dlsym(frei0r->dl_handle, sym_name);
    if (!sym)
        av_log(ctx, AV_LOG_ERROR, "Could not find symbol '%s' in loaded module\n", sym_name);
    return sym;
}

static int set_param(AVFilterContext *ctx, f0r_param_info_t info, int index, char *param)
{
    Frei0rContext *frei0r = ctx->priv;
    union {
        double d;
        f0r_param_color_t col;
        f0r_param_position_t pos;
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
        val.d = strtod(param, &tail);
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
    }

    frei0r->set_param_value(frei0r->instance, &val, index);
    return 0;

fail:
    av_log(ctx, AV_LOG_ERROR, "Invalid value '%s' for parameter '%s'\n",
           param, info.name);
    return AVERROR(EINVAL);
}

static int set_params(AVFilterContext *ctx, const char *params)
{
    Frei0rContext *frei0r = ctx->priv;
    int i;

    for (i = 0; i < frei0r->plugin_info.num_params; i++) {
        f0r_param_info_t info;
        char *param;
        int ret;

        frei0r->get_param_info(&info, i);

        if (*params) {
            if (!(param = av_get_token(&params, ":")))
                return AVERROR(ENOMEM);
            params++;               /* skip ':' */
            ret = set_param(ctx, info, i, param);
            av_free(param);
            if (ret < 0)
                return ret;
        }

        av_log(ctx, AV_LOG_VERBOSE,
               "idx:%d name:'%s' type:%s explanation:'%s' ",
               i, info.name,
               info.type == F0R_PARAM_BOOL     ? "bool"     :
               info.type == F0R_PARAM_DOUBLE   ? "double"   :
               info.type == F0R_PARAM_COLOR    ? "color"    :
               info.type == F0R_PARAM_POSITION ? "position" :
               info.type == F0R_PARAM_STRING   ? "string"   : "unknown",
               info.explanation);

#ifdef DEBUG
        av_log(ctx, AV_LOG_DEBUG, "value:");
        switch (info.type) {
            void *v;
            double d;
            char s[128];
            f0r_param_color_t col;
            f0r_param_position_t pos;

        case F0R_PARAM_BOOL:
            v = &d;
            frei0r->get_param_value(frei0r->instance, v, i);
            av_log(ctx, AV_LOG_DEBUG, "%s", d >= 0.5 && d <= 1.0 ? "y" : "n");
            break;
        case F0R_PARAM_DOUBLE:
            v = &d;
            frei0r->get_param_value(frei0r->instance, v, i);
            av_log(ctx, AV_LOG_DEBUG, "%f", d);
            break;
        case F0R_PARAM_COLOR:
            v = &col;
            frei0r->get_param_value(frei0r->instance, v, i);
            av_log(ctx, AV_LOG_DEBUG, "%f/%f/%f", col.r, col.g, col.b);
            break;
        case F0R_PARAM_POSITION:
            v = &pos;
            frei0r->get_param_value(frei0r->instance, v, i);
            av_log(ctx, AV_LOG_DEBUG, "%f/%f", pos.x, pos.y);
            break;
        default: /* F0R_PARAM_STRING */
            v = s;
            frei0r->get_param_value(frei0r->instance, v, i);
            av_log(ctx, AV_LOG_DEBUG, "'%s'\n", s);
            break;
        }
#endif
        av_log(ctx, AV_LOG_VERBOSE, "\n");
    }

    return 0;
}

static int load_path(AVFilterContext *ctx, void **handle_ptr, const char *prefix, const char *name)
{
    char *path = av_asprintf("%s%s%s", prefix, name, SLIBSUF);
    if (!path)
        return AVERROR(ENOMEM);
    av_log(ctx, AV_LOG_DEBUG, "Looking for frei0r effect in '%s'\n", path);
    *handle_ptr = dlopen(path, RTLD_NOW|RTLD_LOCAL);
    av_free(path);
    return 0;
}

static av_cold int frei0r_init(AVFilterContext *ctx,
                               const char *dl_name, int type)
{
    Frei0rContext *frei0r = ctx->priv;
    f0r_init_f            f0r_init;
    f0r_get_plugin_info_f f0r_get_plugin_info;
    f0r_plugin_info_t *pi;
    char *path;
    int ret = 0;

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
            ret = load_path(ctx, &frei0r->dl_handle, p1, dl_name);
            av_free(p1);
            if (ret < 0)
                goto check_path_end;
            if (frei0r->dl_handle)
                break;
        }

    check_path_end:
        av_free(path);
        if (ret < 0)
            return ret;
    }
    if (!frei0r->dl_handle && (path = getenv("HOME"))) {
        char *prefix = av_asprintf("%s/.frei0r-1/lib/", path);
        if (!prefix)
            return AVERROR(ENOMEM);
        ret = load_path(ctx, &frei0r->dl_handle, prefix, dl_name);
        av_free(prefix);
        if (ret < 0)
            return ret;
    }
    if (!frei0r->dl_handle) {
        ret = load_path(ctx, &frei0r->dl_handle, "/usr/local/lib/frei0r-1/", dl_name);
        if (ret < 0)
            return ret;
    }
    if (!frei0r->dl_handle) {
        ret = load_path(ctx, &frei0r->dl_handle, "/usr/lib/frei0r-1/", dl_name);
        if (ret < 0)
            return ret;
    }
    if (!frei0r->dl_handle) {
        av_log(ctx, AV_LOG_ERROR, "Could not find module '%s'\n", dl_name);
        return AVERROR(EINVAL);
    }

    if (!(f0r_init                = load_sym(ctx, "f0r_init"           )) ||
        !(f0r_get_plugin_info     = load_sym(ctx, "f0r_get_plugin_info")) ||
        !(frei0r->get_param_info  = load_sym(ctx, "f0r_get_param_info" )) ||
        !(frei0r->get_param_value = load_sym(ctx, "f0r_get_param_value")) ||
        !(frei0r->set_param_value = load_sym(ctx, "f0r_set_param_value")) ||
        !(frei0r->update          = load_sym(ctx, "f0r_update"         )) ||
        !(frei0r->construct       = load_sym(ctx, "f0r_construct"      )) ||
        !(frei0r->destruct        = load_sym(ctx, "f0r_destruct"       )) ||
        !(frei0r->deinit          = load_sym(ctx, "f0r_deinit"         )))
        return AVERROR(EINVAL);

    if (f0r_init() < 0) {
        av_log(ctx, AV_LOG_ERROR, "Could not init the frei0r module\n");
        return AVERROR(EINVAL);
    }

    f0r_get_plugin_info(&frei0r->plugin_info);
    pi = &frei0r->plugin_info;
    if (pi->plugin_type != type) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid type '%s' for the plugin\n",
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

static av_cold int filter_init(AVFilterContext *ctx, const char *args)
{
    Frei0rContext *frei0r = ctx->priv;
    char dl_name[1024], c;
    *frei0r->params = 0;

    if (args)
        sscanf(args, "%1023[^:=]%c%255c", dl_name, &c, frei0r->params);

    return frei0r_init(ctx, dl_name, F0R_PLUGIN_TYPE_FILTER);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    Frei0rContext *frei0r = ctx->priv;

    if (frei0r->destruct && frei0r->instance)
        frei0r->destruct(frei0r->instance);
    if (frei0r->deinit)
        frei0r->deinit();
    if (frei0r->dl_handle)
        dlclose(frei0r->dl_handle);

    memset(frei0r, 0, sizeof(*frei0r));
}

static int config_input_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    Frei0rContext *frei0r = ctx->priv;

    if (!(frei0r->instance = frei0r->construct(inlink->w, inlink->h))) {
        av_log(ctx, AV_LOG_ERROR, "Impossible to load frei0r instance\n");
        return AVERROR(EINVAL);
    }

    return set_params(ctx, frei0r->params);
}

static int query_formats(AVFilterContext *ctx)
{
    Frei0rContext *frei0r = ctx->priv;
    AVFilterFormats *formats = NULL;

    if        (frei0r->plugin_info.color_model == F0R_COLOR_MODEL_BGRA8888) {
        ff_add_format(&formats, AV_PIX_FMT_BGRA);
    } else if (frei0r->plugin_info.color_model == F0R_COLOR_MODEL_RGBA8888) {
        ff_add_format(&formats, AV_PIX_FMT_RGBA);
    } else {                                   /* F0R_COLOR_MODEL_PACKED32 */
        static const enum AVPixelFormat pix_fmts[] = {
            AV_PIX_FMT_BGRA, AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR, AV_PIX_FMT_ARGB, AV_PIX_FMT_NONE
        };
        formats = ff_make_format_list(pix_fmts);
    }

    if (!formats)
        return AVERROR(ENOMEM);

    ff_set_common_formats(ctx, formats);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *in)
{
    Frei0rContext *frei0r = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *out;

    out = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
    if (!out) {
        avfilter_unref_bufferp(&in);
        return AVERROR(ENOMEM);
    }
    avfilter_copy_buffer_ref_props(out, in);

    frei0r->update(frei0r->instance, in->pts * av_q2d(inlink->time_base) * 1000,
                   (const uint32_t *)in->data[0],
                   (uint32_t *)out->data[0]);

    avfilter_unref_bufferp(&in);

    return ff_filter_frame(outlink, out);
}

static const AVFilterPad avfilter_vf_frei0r_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_props,
        .filter_frame = filter_frame,
        .min_perms    = AV_PERM_READ
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

AVFilter avfilter_vf_frei0r = {
    .name      = "frei0r",
    .description = NULL_IF_CONFIG_SMALL("Apply a frei0r effect."),

    .query_formats = query_formats,
    .init = filter_init,
    .uninit = uninit,

    .priv_size = sizeof(Frei0rContext),

    .inputs    = avfilter_vf_frei0r_inputs,

    .outputs   = avfilter_vf_frei0r_outputs,
};

static av_cold int source_init(AVFilterContext *ctx, const char *args)
{
    Frei0rContext *frei0r = ctx->priv;
    char dl_name[1024], c;
    char frame_size[128] = "";
    char frame_rate[128] = "";
    AVRational frame_rate_q;

    memset(frei0r->params, 0, sizeof(frei0r->params));

    if (args)
        sscanf(args, "%127[^:]:%127[^:]:%1023[^:=]%c%255c",
               frame_size, frame_rate, dl_name, &c, frei0r->params);

    if (av_parse_video_size(&frei0r->w, &frei0r->h, frame_size) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame size: '%s'\n", frame_size);
        return AVERROR(EINVAL);
    }

    if (av_parse_video_rate(&frame_rate_q, frame_rate) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: '%s'\n", frame_rate);
        return AVERROR(EINVAL);
    }
    frei0r->time_base.num = frame_rate_q.den;
    frei0r->time_base.den = frame_rate_q.num;

    return frei0r_init(ctx, dl_name, F0R_PLUGIN_TYPE_SOURCE);
}

static int source_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    Frei0rContext *frei0r = ctx->priv;

    if (av_image_check_size(frei0r->w, frei0r->h, 0, ctx) < 0)
        return AVERROR(EINVAL);
    outlink->w = frei0r->w;
    outlink->h = frei0r->h;
    outlink->time_base = frei0r->time_base;
    outlink->sample_aspect_ratio = (AVRational){1,1};

    if (!(frei0r->instance = frei0r->construct(outlink->w, outlink->h))) {
        av_log(ctx, AV_LOG_ERROR, "Impossible to load frei0r instance\n");
        return AVERROR(EINVAL);
    }

    return set_params(ctx, frei0r->params);
}

static int source_request_frame(AVFilterLink *outlink)
{
    Frei0rContext *frei0r = outlink->src->priv;
    AVFilterBufferRef *picref = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);

    if (!picref)
        return AVERROR(ENOMEM);

    picref->video->sample_aspect_ratio = (AVRational) {1, 1};
    picref->pts = frei0r->pts++;
    picref->pos = -1;

    frei0r->update(frei0r->instance, av_rescale_q(picref->pts, frei0r->time_base, (AVRational){1,1000}),
                   NULL, (uint32_t *)picref->data[0]);

    return ff_filter_frame(outlink, picref);
}

static const AVFilterPad avfilter_vsrc_frei0r_src_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = source_request_frame,
        .config_props  = source_config_props
    },
    { NULL }
};

AVFilter avfilter_vsrc_frei0r_src = {
    .name        = "frei0r_src",
    .description = NULL_IF_CONFIG_SMALL("Generate a frei0r source."),

    .priv_size = sizeof(Frei0rContext),
    .init      = source_init,
    .uninit    = uninit,

    .query_formats = query_formats,

    .inputs    = NULL,

    .outputs   = avfilter_vsrc_frei0r_src_outputs,
};
