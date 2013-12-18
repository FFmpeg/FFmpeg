/*
 * filter layer
 * Copyright (c) 2007 Bobby Bingham
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/rational.h"
#include "libavutil/samplefmt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

unsigned avfilter_version(void)
{
    return LIBAVFILTER_VERSION_INT;
}

const char *avfilter_configuration(void)
{
    return LIBAV_CONFIGURATION;
}

const char *avfilter_license(void)
{
#define LICENSE_PREFIX "libavfilter license: "
    return LICENSE_PREFIX LIBAV_LICENSE + sizeof(LICENSE_PREFIX) - 1;
}

void ff_insert_pad(unsigned idx, unsigned *count, size_t padidx_off,
                   AVFilterPad **pads, AVFilterLink ***links,
                   AVFilterPad *newpad)
{
    unsigned i;

    idx = FFMIN(idx, *count);

    *pads  = av_realloc(*pads,  sizeof(AVFilterPad)   * (*count + 1));
    *links = av_realloc(*links, sizeof(AVFilterLink*) * (*count + 1));
    memmove(*pads  + idx + 1, *pads  + idx, sizeof(AVFilterPad)   * (*count - idx));
    memmove(*links + idx + 1, *links + idx, sizeof(AVFilterLink*) * (*count - idx));
    memcpy(*pads + idx, newpad, sizeof(AVFilterPad));
    (*links)[idx] = NULL;

    (*count)++;
    for (i = idx + 1; i < *count; i++)
        if (*links[i])
            (*(unsigned *)((uint8_t *) *links[i] + padidx_off))++;
}

int avfilter_link(AVFilterContext *src, unsigned srcpad,
                  AVFilterContext *dst, unsigned dstpad)
{
    AVFilterLink *link;

    if (src->nb_outputs <= srcpad || dst->nb_inputs <= dstpad ||
        src->outputs[srcpad]      || dst->inputs[dstpad])
        return -1;

    if (src->output_pads[srcpad].type != dst->input_pads[dstpad].type) {
        av_log(src, AV_LOG_ERROR,
               "Media type mismatch between the '%s' filter output pad %d and the '%s' filter input pad %d\n",
               src->name, srcpad, dst->name, dstpad);
        return AVERROR(EINVAL);
    }

    link = av_mallocz(sizeof(*link));
    if (!link)
        return AVERROR(ENOMEM);

    src->outputs[srcpad] = dst->inputs[dstpad] = link;

    link->src     = src;
    link->dst     = dst;
    link->srcpad  = &src->output_pads[srcpad];
    link->dstpad  = &dst->input_pads[dstpad];
    link->type    = src->output_pads[srcpad].type;
    assert(AV_PIX_FMT_NONE == -1 && AV_SAMPLE_FMT_NONE == -1);
    link->format  = -1;

    return 0;
}

int avfilter_insert_filter(AVFilterLink *link, AVFilterContext *filt,
                           unsigned filt_srcpad_idx, unsigned filt_dstpad_idx)
{
    int ret;
    unsigned dstpad_idx = link->dstpad - link->dst->input_pads;

    av_log(link->dst, AV_LOG_VERBOSE, "auto-inserting filter '%s' "
           "between the filter '%s' and the filter '%s'\n",
           filt->name, link->src->name, link->dst->name);

    link->dst->inputs[dstpad_idx] = NULL;
    if ((ret = avfilter_link(filt, filt_dstpad_idx, link->dst, dstpad_idx)) < 0) {
        /* failed to link output filter to new filter */
        link->dst->inputs[dstpad_idx] = link;
        return ret;
    }

    /* re-hookup the link to the new destination filter we inserted */
    link->dst                     = filt;
    link->dstpad                  = &filt->input_pads[filt_srcpad_idx];
    filt->inputs[filt_srcpad_idx] = link;

    /* if any information on supported media formats already exists on the
     * link, we need to preserve that */
    if (link->out_formats)
        ff_formats_changeref(&link->out_formats,
                             &filt->outputs[filt_dstpad_idx]->out_formats);
    if (link->out_samplerates)
        ff_formats_changeref(&link->out_samplerates,
                             &filt->outputs[filt_dstpad_idx]->out_samplerates);
    if (link->out_channel_layouts)
        ff_channel_layouts_changeref(&link->out_channel_layouts,
                                     &filt->outputs[filt_dstpad_idx]->out_channel_layouts);

    return 0;
}

int avfilter_config_links(AVFilterContext *filter)
{
    int (*config_link)(AVFilterLink *);
    unsigned i;
    int ret;

    for (i = 0; i < filter->nb_inputs; i ++) {
        AVFilterLink *link = filter->inputs[i];

        if (!link) continue;

        switch (link->init_state) {
        case AVLINK_INIT:
            continue;
        case AVLINK_STARTINIT:
            av_log(filter, AV_LOG_INFO, "circular filter chain detected\n");
            return 0;
        case AVLINK_UNINIT:
            link->init_state = AVLINK_STARTINIT;

            if ((ret = avfilter_config_links(link->src)) < 0)
                return ret;

            if (!(config_link = link->srcpad->config_props)) {
                if (link->src->nb_inputs != 1) {
                    av_log(link->src, AV_LOG_ERROR, "Source filters and filters "
                                                    "with more than one input "
                                                    "must set config_props() "
                                                    "callbacks on all outputs\n");
                    return AVERROR(EINVAL);
                }
            } else if ((ret = config_link(link)) < 0) {
                av_log(link->src, AV_LOG_ERROR,
                       "Failed to configure output pad on %s\n",
                       link->src->name);
                return ret;
            }

            if (link->time_base.num == 0 && link->time_base.den == 0)
                link->time_base = link->src && link->src->nb_inputs ?
                    link->src->inputs[0]->time_base : AV_TIME_BASE_Q;

            if (link->type == AVMEDIA_TYPE_VIDEO) {
                if (!link->sample_aspect_ratio.num && !link->sample_aspect_ratio.den)
                    link->sample_aspect_ratio = link->src->nb_inputs ?
                        link->src->inputs[0]->sample_aspect_ratio : (AVRational){1,1};

                if (link->src->nb_inputs) {
                    if (!link->w)
                        link->w = link->src->inputs[0]->w;
                    if (!link->h)
                        link->h = link->src->inputs[0]->h;
                } else if (!link->w || !link->h) {
                    av_log(link->src, AV_LOG_ERROR,
                           "Video source filters must set their output link's "
                           "width and height\n");
                    return AVERROR(EINVAL);
                }
            }

            if ((config_link = link->dstpad->config_props))
                if ((ret = config_link(link)) < 0) {
                    av_log(link->src, AV_LOG_ERROR,
                           "Failed to configure input pad on %s\n",
                           link->dst->name);
                    return ret;
                }

            link->init_state = AVLINK_INIT;
        }
    }

    return 0;
}

void ff_dlog_link(void *ctx, AVFilterLink *link, int end)
{
    if (link->type == AVMEDIA_TYPE_VIDEO) {
        av_dlog(ctx,
                "link[%p s:%dx%d fmt:%-16s %-16s->%-16s]%s",
                link, link->w, link->h,
                av_get_pix_fmt_name(link->format),
                link->src ? link->src->filter->name : "",
                link->dst ? link->dst->filter->name : "",
                end ? "\n" : "");
    } else {
        char buf[128];
        av_get_channel_layout_string(buf, sizeof(buf), -1, link->channel_layout);

        av_dlog(ctx,
                "link[%p r:%d cl:%s fmt:%-16s %-16s->%-16s]%s",
                link, link->sample_rate, buf,
                av_get_sample_fmt_name(link->format),
                link->src ? link->src->filter->name : "",
                link->dst ? link->dst->filter->name : "",
                end ? "\n" : "");
    }
}

int ff_request_frame(AVFilterLink *link)
{
    FF_DPRINTF_START(NULL, request_frame); ff_dlog_link(NULL, link, 1);

    if (link->srcpad->request_frame)
        return link->srcpad->request_frame(link);
    else if (link->src->inputs[0])
        return ff_request_frame(link->src->inputs[0]);
    else return -1;
}

int ff_poll_frame(AVFilterLink *link)
{
    int i, min = INT_MAX;

    if (link->srcpad->poll_frame)
        return link->srcpad->poll_frame(link);

    for (i = 0; i < link->src->nb_inputs; i++) {
        int val;
        if (!link->src->inputs[i])
            return -1;
        val = ff_poll_frame(link->src->inputs[i]);
        min = FFMIN(min, val);
    }

    return min;
}

static AVFilter *first_filter;

#if !FF_API_NOCONST_GET_NAME
const
#endif
AVFilter *avfilter_get_by_name(const char *name)
{
    const AVFilter *f = NULL;

    if (!name)
        return NULL;

    while ((f = avfilter_next(f)))
        if (!strcmp(f->name, name))
            return f;

    return NULL;
}

int avfilter_register(AVFilter *filter)
{
    AVFilter **f = &first_filter;
    while (*f)
        f = &(*f)->next;
    *f = filter;
    filter->next = NULL;
    return 0;
}

const AVFilter *avfilter_next(const AVFilter *prev)
{
    return prev ? prev->next : first_filter;
}

#if FF_API_OLD_FILTER_REGISTER
AVFilter **av_filter_next(AVFilter **filter)
{
    return filter ? &(*filter)->next : &first_filter;
}

void avfilter_uninit(void)
{
}
#endif

int avfilter_pad_count(const AVFilterPad *pads)
{
    int count;

    if (!pads)
        return 0;

    for (count = 0; pads->name; count++)
        pads++;
    return count;
}

static const char *filter_name(void *p)
{
    AVFilterContext *filter = p;
    return filter->filter->name;
}

static void *filter_child_next(void *obj, void *prev)
{
    AVFilterContext *ctx = obj;
    if (!prev && ctx->filter && ctx->filter->priv_class && ctx->priv)
        return ctx->priv;
    return NULL;
}

static const AVClass *filter_child_class_next(const AVClass *prev)
{
    const AVFilter *f = NULL;

    while (prev && (f = avfilter_next(f)))
        if (f->priv_class == prev)
            break;

    while ((f = avfilter_next(f)))
        if (f->priv_class)
            return f->priv_class;

    return NULL;
}

#define OFFSET(x) offsetof(AVFilterContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM
static const AVOption avfilter_options[] = {
    { "thread_type", "Allowed thread types", OFFSET(thread_type), AV_OPT_TYPE_FLAGS,
        { .i64 = AVFILTER_THREAD_SLICE }, 0, INT_MAX, FLAGS, "thread_type" },
        { "slice", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVFILTER_THREAD_SLICE }, .unit = "thread_type" },
    { NULL },
};

static const AVClass avfilter_class = {
    .class_name = "AVFilter",
    .item_name  = filter_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .child_next = filter_child_next,
    .child_class_next = filter_child_class_next,
    .option           = avfilter_options,
};

static int default_execute(AVFilterContext *ctx, avfilter_action_func *func, void *arg,
                           int *ret, int nb_jobs)
{
    int i;

    for (i = 0; i < nb_jobs; i++) {
        int r = func(ctx, arg, i, nb_jobs);
        if (ret)
            ret[i] = r;
    }
    return 0;
}

AVFilterContext *ff_filter_alloc(const AVFilter *filter, const char *inst_name)
{
    AVFilterContext *ret;

    if (!filter)
        return NULL;

    ret = av_mallocz(sizeof(AVFilterContext));
    if (!ret)
        return NULL;

    ret->av_class = &avfilter_class;
    ret->filter   = filter;
    ret->name     = inst_name ? av_strdup(inst_name) : NULL;
    if (filter->priv_size) {
        ret->priv     = av_mallocz(filter->priv_size);
        if (!ret->priv)
            goto err;
    }

    av_opt_set_defaults(ret);
    if (filter->priv_class) {
        *(const AVClass**)ret->priv = filter->priv_class;
        av_opt_set_defaults(ret->priv);
    }

    ret->internal = av_mallocz(sizeof(*ret->internal));
    if (!ret->internal)
        goto err;
    ret->internal->execute = default_execute;

    ret->nb_inputs = avfilter_pad_count(filter->inputs);
    if (ret->nb_inputs ) {
        ret->input_pads   = av_malloc(sizeof(AVFilterPad) * ret->nb_inputs);
        if (!ret->input_pads)
            goto err;
        memcpy(ret->input_pads, filter->inputs, sizeof(AVFilterPad) * ret->nb_inputs);
        ret->inputs       = av_mallocz(sizeof(AVFilterLink*) * ret->nb_inputs);
        if (!ret->inputs)
            goto err;
    }

    ret->nb_outputs = avfilter_pad_count(filter->outputs);
    if (ret->nb_outputs) {
        ret->output_pads  = av_malloc(sizeof(AVFilterPad) * ret->nb_outputs);
        if (!ret->output_pads)
            goto err;
        memcpy(ret->output_pads, filter->outputs, sizeof(AVFilterPad) * ret->nb_outputs);
        ret->outputs      = av_mallocz(sizeof(AVFilterLink*) * ret->nb_outputs);
        if (!ret->outputs)
            goto err;
    }
#if FF_API_FOO_COUNT
FF_DISABLE_DEPRECATION_WARNINGS
    ret->output_count = ret->nb_outputs;
    ret->input_count  = ret->nb_inputs;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    return ret;

err:
    av_freep(&ret->inputs);
    av_freep(&ret->input_pads);
    ret->nb_inputs = 0;
    av_freep(&ret->outputs);
    av_freep(&ret->output_pads);
    ret->nb_outputs = 0;
    av_freep(&ret->priv);
    av_freep(&ret->internal);
    av_free(ret);
    return NULL;
}

#if FF_API_AVFILTER_OPEN
int avfilter_open(AVFilterContext **filter_ctx, AVFilter *filter, const char *inst_name)
{
    *filter_ctx = ff_filter_alloc(filter, inst_name);
    return *filter_ctx ? 0 : AVERROR(ENOMEM);
}
#endif

static void free_link(AVFilterLink *link)
{
    if (!link)
        return;

    if (link->src)
        link->src->outputs[link->srcpad - link->src->output_pads] = NULL;
    if (link->dst)
        link->dst->inputs[link->dstpad - link->dst->input_pads] = NULL;

    ff_formats_unref(&link->in_formats);
    ff_formats_unref(&link->out_formats);
    ff_formats_unref(&link->in_samplerates);
    ff_formats_unref(&link->out_samplerates);
    ff_channel_layouts_unref(&link->in_channel_layouts);
    ff_channel_layouts_unref(&link->out_channel_layouts);
    av_freep(&link);
}

void avfilter_free(AVFilterContext *filter)
{
    int i;

    if (filter->graph)
        ff_filter_graph_remove_filter(filter->graph, filter);

    if (filter->filter->uninit)
        filter->filter->uninit(filter);

    for (i = 0; i < filter->nb_inputs; i++) {
        free_link(filter->inputs[i]);
    }
    for (i = 0; i < filter->nb_outputs; i++) {
        free_link(filter->outputs[i]);
    }

    if (filter->filter->priv_class)
        av_opt_free(filter->priv);

    av_freep(&filter->name);
    av_freep(&filter->input_pads);
    av_freep(&filter->output_pads);
    av_freep(&filter->inputs);
    av_freep(&filter->outputs);
    av_freep(&filter->priv);
    av_freep(&filter->internal);
    av_free(filter);
}

/* process a list of value1:value2:..., each value corresponding
 * to subsequent AVOption, in the order they are declared */
static int process_unnamed_options(AVFilterContext *ctx, AVDictionary **options,
                                   const char *args)
{
    const AVOption *o = NULL;
    const char *p = args;
    char *val;

    while (*p) {
        o = av_opt_next(ctx->priv, o);
        if (!o) {
            av_log(ctx, AV_LOG_ERROR, "More options provided than "
                   "this filter supports.\n");
            return AVERROR(EINVAL);
        }
        if (o->type == AV_OPT_TYPE_CONST)
            continue;

        val = av_get_token(&p, ":");
        if (!val)
            return AVERROR(ENOMEM);

        av_dict_set(options, o->name, val, 0);

        av_freep(&val);
        if (*p)
            p++;
    }

    return 0;
}

#if FF_API_AVFILTER_INIT_FILTER
int avfilter_init_filter(AVFilterContext *filter, const char *args, void *opaque)
{
    return avfilter_init_str(filter, args);
}
#endif

int avfilter_init_dict(AVFilterContext *ctx, AVDictionary **options)
{
    int ret = 0;

    ret = av_opt_set_dict(ctx, options);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error applying generic filter options.\n");
        return ret;
    }

    if (ctx->filter->flags & AVFILTER_FLAG_SLICE_THREADS &&
        ctx->thread_type & ctx->graph->thread_type & AVFILTER_THREAD_SLICE &&
        ctx->graph->internal->thread_execute) {
        ctx->thread_type       = AVFILTER_THREAD_SLICE;
        ctx->internal->execute = ctx->graph->internal->thread_execute;
    } else {
        ctx->thread_type = 0;
    }

    if (ctx->filter->priv_class) {
        ret = av_opt_set_dict(ctx->priv, options);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error applying options to the filter.\n");
            return ret;
        }
    }

    if (ctx->filter->init)
        ret = ctx->filter->init(ctx);
    else if (ctx->filter->init_dict)
        ret = ctx->filter->init_dict(ctx, options);

    return ret;
}

int avfilter_init_str(AVFilterContext *filter, const char *args)
{
    AVDictionary *options = NULL;
    AVDictionaryEntry *e;
    int ret = 0;

    if (args && *args) {
        if (!filter->filter->priv_class) {
            av_log(filter, AV_LOG_ERROR, "This filter does not take any "
                   "options, but options were provided: %s.\n", args);
            return AVERROR(EINVAL);
        }

#if FF_API_OLD_FILTER_OPTS
        if (!strcmp(filter->filter->name, "scale") &&
            strchr(args, ':') && strchr(args, ':') < strchr(args, '=')) {
            /* old w:h:flags=<flags> syntax */
            char *copy = av_strdup(args);
            char *p;

            av_log(filter, AV_LOG_WARNING, "The <w>:<h>:flags=<flags> option "
                   "syntax is deprecated. Use either <w>:<h>:<flags> or "
                   "w=<w>:h=<h>:flags=<flags>.\n");

            if (!copy) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            p = strrchr(copy, ':');
            if (p) {
                *p++ = 0;
                ret = av_dict_parse_string(&options, p, "=", ":", 0);
            }
            if (ret >= 0)
                ret = process_unnamed_options(filter, &options, copy);
            av_freep(&copy);

            if (ret < 0)
                goto fail;
        } else
#endif

        if (strchr(args, '=')) {
            /* assume a list of key1=value1:key2=value2:... */
            ret = av_dict_parse_string(&options, args, "=", ":", 0);
            if (ret < 0)
                goto fail;
#if FF_API_OLD_FILTER_OPTS
        } else if (!strcmp(filter->filter->name, "format")     ||
                   !strcmp(filter->filter->name, "noformat")   ||
                   !strcmp(filter->filter->name, "frei0r")     ||
                   !strcmp(filter->filter->name, "frei0r_src") ||
                   !strcmp(filter->filter->name, "ocv")) {
            /* a hack for compatibility with the old syntax
             * replace colons with |s */
            char *copy = av_strdup(args);
            char *p    = copy;
            int nb_leading = 0; // number of leading colons to skip

            if (!copy) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            if (!strcmp(filter->filter->name, "frei0r") ||
                !strcmp(filter->filter->name, "ocv"))
                nb_leading = 1;
            else if (!strcmp(filter->filter->name, "frei0r_src"))
                nb_leading = 3;

            while (nb_leading--) {
                p = strchr(p, ':');
                if (!p) {
                    p = copy + strlen(copy);
                    break;
                }
                p++;
            }

            if (strchr(p, ':')) {
                av_log(filter, AV_LOG_WARNING, "This syntax is deprecated. Use "
                       "'|' to separate the list items.\n");
            }

            while ((p = strchr(p, ':')))
                *p++ = '|';

            ret = process_unnamed_options(filter, &options, copy);
            av_freep(&copy);

            if (ret < 0)
                goto fail;
#endif
        } else {
            ret = process_unnamed_options(filter, &options, args);
            if (ret < 0)
                goto fail;
        }
    }

    ret = avfilter_init_dict(filter, &options);
    if (ret < 0)
        goto fail;

    if ((e = av_dict_get(options, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(filter, AV_LOG_ERROR, "No such option: %s.\n", e->key);
        ret = AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }

fail:
    av_dict_free(&options);

    return ret;
}

const char *avfilter_pad_get_name(const AVFilterPad *pads, int pad_idx)
{
    return pads[pad_idx].name;
}

enum AVMediaType avfilter_pad_get_type(const AVFilterPad *pads, int pad_idx)
{
    return pads[pad_idx].type;
}

static int default_filter_frame(AVFilterLink *link, AVFrame *frame)
{
    return ff_filter_frame(link->dst->outputs[0], frame);
}

int ff_filter_frame(AVFilterLink *link, AVFrame *frame)
{
    int (*filter_frame)(AVFilterLink *, AVFrame *);
    AVFilterPad *dst = link->dstpad;
    AVFrame *out = NULL;
    int ret;

    FF_DPRINTF_START(NULL, filter_frame);
    ff_dlog_link(NULL, link, 1);

    if (!(filter_frame = dst->filter_frame))
        filter_frame = default_filter_frame;

    /* copy the frame if needed */
    if (dst->needs_writable && !av_frame_is_writable(frame)) {
        av_log(link->dst, AV_LOG_DEBUG, "Copying data in avfilter.\n");

        switch (link->type) {
        case AVMEDIA_TYPE_VIDEO:
            out = ff_get_video_buffer(link, link->w, link->h);
            break;
        case AVMEDIA_TYPE_AUDIO:
            out = ff_get_audio_buffer(link, frame->nb_samples);
            break;
        default:
            ret = AVERROR(EINVAL);
            goto fail;
        }
        if (!out) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = av_frame_copy_props(out, frame);
        if (ret < 0)
            goto fail;

        switch (link->type) {
        case AVMEDIA_TYPE_VIDEO:
            av_image_copy(out->data, out->linesize, frame->data, frame->linesize,
                          frame->format, frame->width, frame->height);
            break;
        case AVMEDIA_TYPE_AUDIO:
            av_samples_copy(out->extended_data, frame->extended_data,
                            0, 0, frame->nb_samples,
                            av_get_channel_layout_nb_channels(frame->channel_layout),
                            frame->format);
            break;
        default:
            ret = AVERROR(EINVAL);
            goto fail;
        }

        av_frame_free(&frame);
    } else
        out = frame;

    return filter_frame(link, out);

fail:
    av_frame_free(&out);
    av_frame_free(&frame);
    return ret;
}

const AVClass *avfilter_get_class(void)
{
    return &avfilter_class;
}
