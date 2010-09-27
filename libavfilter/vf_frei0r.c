/*
 * copyright (c) 2010 Stefano Sabatini
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
#include "libavutil/avstring.h"
#include "avfilter.h"
#include "parseutils.h"

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
            if (av_parse_color(rgba, param, ctx) < 0)
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

        av_log(ctx, AV_LOG_INFO,
               "idx:%d name:'%s' type:%s explanation:'%s' ",
               i, info.name,
               info.type == F0R_PARAM_BOOL     ? "bool"     :
               info.type == F0R_PARAM_DOUBLE   ? "double"   :
               info.type == F0R_PARAM_COLOR    ? "color"    :
               info.type == F0R_PARAM_POSITION ? "position" :
               info.type == F0R_PARAM_STRING   ? "string"   : "unknown",
               info.explanation);

#ifdef DEBUG
        av_log(ctx, AV_LOG_INFO, "value:");
        switch (info.type) {
            void *v;
            double d;
            char s[128];
            f0r_param_color_t col;
            f0r_param_position_t pos;

        case F0R_PARAM_BOOL:
            v = &d;
            frei0r->get_param_value(frei0r->instance, v, i);
            av_log(ctx, AV_LOG_INFO, "%s", d >= 0.5 && d <= 1.0 ? "y" : "n");
            break;
        case F0R_PARAM_DOUBLE:
            v = &d;
            frei0r->get_param_value(frei0r->instance, v, i);
            av_log(ctx, AV_LOG_INFO, "%f", d);
            break;
        case F0R_PARAM_COLOR:
            v = &col;
            frei0r->get_param_value(frei0r->instance, v, i);
            av_log(ctx, AV_LOG_INFO, "%f/%f/%f", col.r, col.g, col.b);
            break;
        case F0R_PARAM_POSITION:
            v = &pos;
            frei0r->get_param_value(frei0r->instance, v, i);
            av_log(ctx, AV_LOG_INFO, "%lf/%lf", pos.x, pos.y);
            break;
        default: /* F0R_PARAM_STRING */
            v = s;
            frei0r->get_param_value(frei0r->instance, v, i);
            av_log(ctx, AV_LOG_INFO, "'%s'\n", s);
            break;
        }
#endif
        av_log(ctx, AV_LOG_INFO, "\n");
    }

    return 0;
}

static void *load_path(AVFilterContext *ctx, const char *prefix, const char *name)
{
    char path[1024];

    snprintf(path, sizeof(path), "%s%s.so", prefix, name);
    av_log(ctx, AV_LOG_DEBUG, "Looking for frei0r effect in '%s'\n", path);
    return dlopen(path, RTLD_NOW|RTLD_LOCAL);
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    Frei0rContext *frei0r = ctx->priv;
    f0r_init_f            f0r_init;
    f0r_get_plugin_info_f f0r_get_plugin_info;
    f0r_plugin_info_t *pi;
    char dl_name[1024], *path;

    *frei0r->params = 0;

    if (args)
        sscanf(args, "%1023[^:]:%255c", dl_name, frei0r->params);

    /* see: http://piksel.org/frei0r/1.2/spec/1.2/spec/group__pluglocations.html */
    if ((path = av_strdup(getenv("FREI0R_PATH")))) {
        char *p, *ptr = NULL;
        for (p = path; p = strtok_r(p, ":", &ptr); p = NULL)
            if (frei0r->dl_handle = load_path(ctx, p, dl_name))
                break;
        av_free(path);
    }
    if (!frei0r->dl_handle && (path = getenv("HOME"))) {
        char prefix[1024];
        snprintf(prefix, sizeof(prefix), "%s/.frei0r-1/lib/", path);
        frei0r->dl_handle = load_path(ctx, prefix, dl_name);
    }
    if (!frei0r->dl_handle)
        frei0r->dl_handle = load_path(ctx, "/usr/local/lib/frei0r-1/", dl_name);
    if (!frei0r->dl_handle)
        frei0r->dl_handle = load_path(ctx, "/usr/lib/frei0r-1/", dl_name);
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
        av_log(ctx, AV_LOG_ERROR, "Could not init the frei0r module");
        return AVERROR(EINVAL);
    }

    f0r_get_plugin_info(&frei0r->plugin_info);
    pi = &frei0r->plugin_info;
    if (pi->plugin_type != F0R_PLUGIN_TYPE_FILTER) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid type '%s' for the plugin, a filter plugin was expected\n",
               pi->plugin_type == F0R_PLUGIN_TYPE_SOURCE ? "source" :
               pi->plugin_type == F0R_PLUGIN_TYPE_MIXER2 ? "mixer2" :
               pi->plugin_type == F0R_PLUGIN_TYPE_MIXER3 ? "mixer3" : "unknown");
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_INFO,
           "name:%s author:'%s' explanation:'%s' color_model:%s "
           "frei0r_version:%d version:%d.%d num_params:%d\n",
           pi->name, pi->author, pi->explanation,
           pi->color_model == F0R_COLOR_MODEL_BGRA8888 ? "bgra8888" :
           pi->color_model == F0R_COLOR_MODEL_RGBA8888 ? "rgba8888" :
           pi->color_model == F0R_COLOR_MODEL_PACKED32 ? "packed32" : "unknown",
           pi->frei0r_version, pi->major_version, pi->minor_version, pi->num_params);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    Frei0rContext *frei0r = ctx->priv;

    if (frei0r->destruct)
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
        av_log(ctx, AV_LOG_ERROR, "Impossible to load frei0r instance");
        return AVERROR(EINVAL);
    }

    return set_params(ctx, frei0r->params);
}

static int query_formats(AVFilterContext *ctx)
{
    Frei0rContext *frei0r = ctx->priv;
    AVFilterFormats *formats = NULL;

    if        (frei0r->plugin_info.color_model == F0R_COLOR_MODEL_BGRA8888) {
        avfilter_add_format(&formats, PIX_FMT_BGRA);
    } else if (frei0r->plugin_info.color_model == F0R_COLOR_MODEL_RGBA8888) {
        avfilter_add_format(&formats, PIX_FMT_RGBA);
    } else {                                   /* F0R_COLOR_MODEL_PACKED32 */
        static const enum PixelFormat pix_fmts[] = {
            PIX_FMT_BGRA, PIX_FMT_ARGB, PIX_FMT_ABGR, PIX_FMT_ARGB, PIX_FMT_NONE
        };
        formats = avfilter_make_format_list(pix_fmts);
    }

    if (!formats)
        return AVERROR(ENOMEM);

    avfilter_set_common_formats(ctx, formats);
    return 0;
}

static void null_draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir) { }

static void end_frame(AVFilterLink *inlink)
{
    Frei0rContext *frei0r = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterBufferRef  *inpicref =  inlink->cur_buf;
    AVFilterBufferRef *outpicref = outlink->out_buf;

    frei0r->update(frei0r->instance, (double)inpicref->pts / AV_TIME_BASE,
                   (const uint32_t *)inpicref->data[0],
                   (uint32_t *)outpicref->data[0]);
    avfilter_unref_buffer(inpicref);
    avfilter_draw_slice(outlink, 0, outlink->h, 1);
    avfilter_end_frame(outlink);
    avfilter_unref_buffer(outpicref);
}

AVFilter avfilter_vf_frei0r = {
    .name      = "frei0r",
    .description = NULL_IF_CONFIG_SMALL("Apply a frei0r effect."),

    .query_formats = query_formats,
    .init = init,
    .uninit = uninit,

    .priv_size = sizeof(Frei0rContext),

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .draw_slice       = null_draw_slice,
                                    .config_props     = config_input_props,
                                    .end_frame        = end_frame,
                                    .min_perms        = AV_PERM_READ },
                                  { .name = NULL}},

    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
