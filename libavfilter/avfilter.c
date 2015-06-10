/*
 * filter layer
 * Copyright (c) 2007 Bobby Bingham
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

#include "libavutil/atomic.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
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

#include "libavutil/ffversion.h"
const char av_filter_ffversion[] = "FFmpeg version " FFMPEG_VERSION;

static int ff_filter_frame_framed(AVFilterLink *link, AVFrame *frame);

void ff_tlog_ref(void *ctx, AVFrame *ref, int end)
{
    av_unused char buf[16];
    ff_tlog(ctx,
            "ref[%p buf:%p data:%p linesize[%d, %d, %d, %d] pts:%"PRId64" pos:%"PRId64,
            ref, ref->buf, ref->data[0],
            ref->linesize[0], ref->linesize[1], ref->linesize[2], ref->linesize[3],
            ref->pts, av_frame_get_pkt_pos(ref));

    if (ref->width) {
        ff_tlog(ctx, " a:%d/%d s:%dx%d i:%c iskey:%d type:%c",
                ref->sample_aspect_ratio.num, ref->sample_aspect_ratio.den,
                ref->width, ref->height,
                !ref->interlaced_frame     ? 'P' :         /* Progressive  */
                ref->top_field_first ? 'T' : 'B',    /* Top / Bottom */
                ref->key_frame,
                av_get_picture_type_char(ref->pict_type));
    }
    if (ref->nb_samples) {
        ff_tlog(ctx, " cl:%"PRId64"d n:%d r:%d",
                ref->channel_layout,
                ref->nb_samples,
                ref->sample_rate);
    }

    ff_tlog(ctx, "]%s", end ? "\n" : "");
}

unsigned avfilter_version(void)
{
    av_assert0(LIBAVFILTER_VERSION_MICRO >= 100);
    return LIBAVFILTER_VERSION_INT;
}

const char *avfilter_configuration(void)
{
    return FFMPEG_CONFIGURATION;
}

const char *avfilter_license(void)
{
#define LICENSE_PREFIX "libavfilter license: "
    return LICENSE_PREFIX FFMPEG_LICENSE + sizeof(LICENSE_PREFIX) - 1;
}

void ff_command_queue_pop(AVFilterContext *filter)
{
    AVFilterCommand *c= filter->command_queue;
    av_freep(&c->arg);
    av_freep(&c->command);
    filter->command_queue= c->next;
    av_free(c);
}

int ff_insert_pad(unsigned idx, unsigned *count, size_t padidx_off,
                   AVFilterPad **pads, AVFilterLink ***links,
                   AVFilterPad *newpad)
{
    AVFilterLink **newlinks;
    AVFilterPad *newpads;
    unsigned i;

    idx = FFMIN(idx, *count);

    newpads  = av_realloc_array(*pads,  *count + 1, sizeof(AVFilterPad));
    newlinks = av_realloc_array(*links, *count + 1, sizeof(AVFilterLink*));
    if (newpads)
        *pads  = newpads;
    if (newlinks)
        *links = newlinks;
    if (!newpads || !newlinks)
        return AVERROR(ENOMEM);

    memmove(*pads  + idx + 1, *pads  + idx, sizeof(AVFilterPad)   * (*count - idx));
    memmove(*links + idx + 1, *links + idx, sizeof(AVFilterLink*) * (*count - idx));
    memcpy(*pads + idx, newpad, sizeof(AVFilterPad));
    (*links)[idx] = NULL;

    (*count)++;
    for (i = idx + 1; i < *count; i++)
        if ((*links)[i])
            (*(unsigned *)((uint8_t *) (*links)[i] + padidx_off))++;

    return 0;
}

int avfilter_link(AVFilterContext *src, unsigned srcpad,
                  AVFilterContext *dst, unsigned dstpad)
{
    AVFilterLink *link;

    if (src->nb_outputs <= srcpad || dst->nb_inputs <= dstpad ||
        src->outputs[srcpad]      || dst->inputs[dstpad])
        return AVERROR(EINVAL);

    if (src->output_pads[srcpad].type != dst->input_pads[dstpad].type) {
        av_log(src, AV_LOG_ERROR,
               "Media type mismatch between the '%s' filter output pad %d (%s) and the '%s' filter input pad %d (%s)\n",
               src->name, srcpad, (char *)av_x_if_null(av_get_media_type_string(src->output_pads[srcpad].type), "?"),
               dst->name, dstpad, (char *)av_x_if_null(av_get_media_type_string(dst-> input_pads[dstpad].type), "?"));
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
    av_assert0(AV_PIX_FMT_NONE == -1 && AV_SAMPLE_FMT_NONE == -1);
    link->format  = -1;

    return 0;
}

void avfilter_link_free(AVFilterLink **link)
{
    if (!*link)
        return;

    av_frame_free(&(*link)->partial_buf);

    av_freep(link);
}

int avfilter_link_get_channels(AVFilterLink *link)
{
    return link->channels;
}

void avfilter_link_set_closed(AVFilterLink *link, int closed)
{
    link->closed = closed;
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
        AVFilterLink *inlink;

        if (!link) continue;
        if (!link->src || !link->dst) {
            av_log(filter, AV_LOG_ERROR,
                   "Not all input and output are properly linked (%d).\n", i);
            return AVERROR(EINVAL);
        }

        inlink = link->src->nb_inputs ? link->src->inputs[0] : NULL;
        link->current_pts = AV_NOPTS_VALUE;

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

            switch (link->type) {
            case AVMEDIA_TYPE_VIDEO:
                if (!link->time_base.num && !link->time_base.den)
                    link->time_base = inlink ? inlink->time_base : AV_TIME_BASE_Q;

                if (!link->sample_aspect_ratio.num && !link->sample_aspect_ratio.den)
                    link->sample_aspect_ratio = inlink ?
                        inlink->sample_aspect_ratio : (AVRational){1,1};

                if (inlink && !link->frame_rate.num && !link->frame_rate.den)
                    link->frame_rate = inlink->frame_rate;

                if (inlink) {
                    if (!link->w)
                        link->w = inlink->w;
                    if (!link->h)
                        link->h = inlink->h;
                } else if (!link->w || !link->h) {
                    av_log(link->src, AV_LOG_ERROR,
                           "Video source filters must set their output link's "
                           "width and height\n");
                    return AVERROR(EINVAL);
                }
                break;

            case AVMEDIA_TYPE_AUDIO:
                if (inlink) {
                    if (!link->time_base.num && !link->time_base.den)
                        link->time_base = inlink->time_base;
                }

                if (!link->time_base.num && !link->time_base.den)
                    link->time_base = (AVRational) {1, link->sample_rate};
            }

            if ((config_link = link->dstpad->config_props))
                if ((ret = config_link(link)) < 0) {
                    av_log(link->dst, AV_LOG_ERROR,
                           "Failed to configure input pad on %s\n",
                           link->dst->name);
                    return ret;
                }

            link->init_state = AVLINK_INIT;
        }
    }

    return 0;
}

void ff_tlog_link(void *ctx, AVFilterLink *link, int end)
{
    if (link->type == AVMEDIA_TYPE_VIDEO) {
        ff_tlog(ctx,
                "link[%p s:%dx%d fmt:%s %s->%s]%s",
                link, link->w, link->h,
                av_get_pix_fmt_name(link->format),
                link->src ? link->src->filter->name : "",
                link->dst ? link->dst->filter->name : "",
                end ? "\n" : "");
    } else {
        char buf[128];
        av_get_channel_layout_string(buf, sizeof(buf), -1, link->channel_layout);

        ff_tlog(ctx,
                "link[%p r:%d cl:%s fmt:%s %s->%s]%s",
                link, (int)link->sample_rate, buf,
                av_get_sample_fmt_name(link->format),
                link->src ? link->src->filter->name : "",
                link->dst ? link->dst->filter->name : "",
                end ? "\n" : "");
    }
}

int ff_request_frame(AVFilterLink *link)
{
    int ret = -1;
    FF_TPRINTF_START(NULL, request_frame); ff_tlog_link(NULL, link, 1);

    if (link->closed)
        return AVERROR_EOF;
    av_assert0(!link->frame_requested);
    link->frame_requested = 1;
    while (link->frame_requested) {
        if (link->srcpad->request_frame)
            ret = link->srcpad->request_frame(link);
        else if (link->src->inputs[0])
            ret = ff_request_frame(link->src->inputs[0]);
        if (ret == AVERROR_EOF && link->partial_buf) {
            AVFrame *pbuf = link->partial_buf;
            link->partial_buf = NULL;
            ret = ff_filter_frame_framed(link, pbuf);
        }
        if (ret < 0) {
            link->frame_requested = 0;
            if (ret == AVERROR_EOF)
                link->closed = 1;
        } else {
            av_assert0(!link->frame_requested ||
                       link->flags & FF_LINK_FLAG_REQUEST_LOOP);
        }
    }
    return ret;
}

int ff_poll_frame(AVFilterLink *link)
{
    int i, min = INT_MAX;

    if (link->srcpad->poll_frame)
        return link->srcpad->poll_frame(link);

    for (i = 0; i < link->src->nb_inputs; i++) {
        int val;
        if (!link->src->inputs[i])
            return AVERROR(EINVAL);
        val = ff_poll_frame(link->src->inputs[i]);
        min = FFMIN(min, val);
    }

    return min;
}

static const char *const var_names[] = {
    "t",
    "n",
    "pos",
    "w",
    "h",
    NULL
};

enum {
    VAR_T,
    VAR_N,
    VAR_POS,
    VAR_W,
    VAR_H,
    VAR_VARS_NB
};

static int set_enable_expr(AVFilterContext *ctx, const char *expr)
{
    int ret;
    char *expr_dup;
    AVExpr *old = ctx->enable;

    if (!(ctx->filter->flags & AVFILTER_FLAG_SUPPORT_TIMELINE)) {
        av_log(ctx, AV_LOG_ERROR, "Timeline ('enable' option) not supported "
               "with filter '%s'\n", ctx->filter->name);
        return AVERROR_PATCHWELCOME;
    }

    expr_dup = av_strdup(expr);
    if (!expr_dup)
        return AVERROR(ENOMEM);

    if (!ctx->var_values) {
        ctx->var_values = av_calloc(VAR_VARS_NB, sizeof(*ctx->var_values));
        if (!ctx->var_values) {
            av_free(expr_dup);
            return AVERROR(ENOMEM);
        }
    }

    ret = av_expr_parse((AVExpr**)&ctx->enable, expr_dup, var_names,
                        NULL, NULL, NULL, NULL, 0, ctx->priv);
    if (ret < 0) {
        av_log(ctx->priv, AV_LOG_ERROR,
               "Error when evaluating the expression '%s' for enable\n",
               expr_dup);
        av_free(expr_dup);
        return ret;
    }

    av_expr_free(old);
    av_free(ctx->enable_str);
    ctx->enable_str = expr_dup;
    return 0;
}

void ff_update_link_current_pts(AVFilterLink *link, int64_t pts)
{
    if (pts == AV_NOPTS_VALUE)
        return;
    link->current_pts = av_rescale_q(pts, link->time_base, AV_TIME_BASE_Q);
    /* TODO use duration */
    if (link->graph && link->age_index >= 0)
        ff_avfilter_graph_update_heap(link->graph, link);
}

int avfilter_process_command(AVFilterContext *filter, const char *cmd, const char *arg, char *res, int res_len, int flags)
{
    if(!strcmp(cmd, "ping")){
        char local_res[256] = {0};

        if (!res) {
            res = local_res;
            res_len = sizeof(local_res);
        }
        av_strlcatf(res, res_len, "pong from:%s %s\n", filter->filter->name, filter->name);
        if (res == local_res)
            av_log(filter, AV_LOG_INFO, "%s", res);
        return 0;
    }else if(!strcmp(cmd, "enable")) {
        return set_enable_expr(filter, arg);
    }else if(filter->filter->process_command) {
        return filter->filter->process_command(filter, cmd, arg, res, res_len, flags);
    }
    return AVERROR(ENOSYS);
}

static AVFilter *first_filter;
static AVFilter **last_filter = &first_filter;

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
            return (AVFilter *)f;

    return NULL;
}

int avfilter_register(AVFilter *filter)
{
    AVFilter **f = last_filter;
    int i;

    /* the filter must select generic or internal exclusively */
    av_assert0((filter->flags & AVFILTER_FLAG_SUPPORT_TIMELINE) != AVFILTER_FLAG_SUPPORT_TIMELINE);

    for(i=0; filter->inputs && filter->inputs[i].name; i++) {
        const AVFilterPad *input = &filter->inputs[i];
        av_assert0(     !input->filter_frame
                    || (!input->start_frame && !input->end_frame));
    }

    filter->next = NULL;

    while(*f || avpriv_atomic_ptr_cas((void * volatile *)f, NULL, filter))
        f = &(*f)->next;
    last_filter = &filter->next;

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

static const char *default_filter_name(void *filter_ctx)
{
    AVFilterContext *ctx = filter_ctx;
    return ctx->name ? ctx->name : ctx->filter->name;
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

    /* find the filter that corresponds to prev */
    while (prev && (f = avfilter_next(f)))
        if (f->priv_class == prev)
            break;

    /* could not find filter corresponding to prev */
    if (prev && !f)
        return NULL;

    /* find next filter with specific options */
    while ((f = avfilter_next(f)))
        if (f->priv_class)
            return f->priv_class;

    return NULL;
}

#define OFFSET(x) offsetof(AVFilterContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption avfilter_options[] = {
    { "thread_type", "Allowed thread types", OFFSET(thread_type), AV_OPT_TYPE_FLAGS,
        { .i64 = AVFILTER_THREAD_SLICE }, 0, INT_MAX, FLAGS, "thread_type" },
        { "slice", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVFILTER_THREAD_SLICE }, .unit = "thread_type" },
    { "enable", "set enable expression", OFFSET(enable_str), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { NULL },
};

static const AVClass avfilter_class = {
    .class_name = "AVFilter",
    .item_name  = default_filter_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
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
        ret->input_pads   = av_malloc_array(ret->nb_inputs, sizeof(AVFilterPad));
        if (!ret->input_pads)
            goto err;
        memcpy(ret->input_pads, filter->inputs, sizeof(AVFilterPad) * ret->nb_inputs);
        ret->inputs       = av_mallocz_array(ret->nb_inputs, sizeof(AVFilterLink*));
        if (!ret->inputs)
            goto err;
    }

    ret->nb_outputs = avfilter_pad_count(filter->outputs);
    if (ret->nb_outputs) {
        ret->output_pads  = av_malloc_array(ret->nb_outputs, sizeof(AVFilterPad));
        if (!ret->output_pads)
            goto err;
        memcpy(ret->output_pads, filter->outputs, sizeof(AVFilterPad) * ret->nb_outputs);
        ret->outputs      = av_mallocz_array(ret->nb_outputs, sizeof(AVFilterLink*));
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
    avfilter_link_free(&link);
}

void avfilter_free(AVFilterContext *filter)
{
    int i;

    if (!filter)
        return;

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
    while(filter->command_queue){
        ff_command_queue_pop(filter);
    }
    av_opt_free(filter);
    av_expr_free(filter->enable);
    filter->enable = NULL;
    av_freep(&filter->var_values);
    av_freep(&filter->internal);
    av_free(filter);
}

static int process_options(AVFilterContext *ctx, AVDictionary **options,
                           const char *args)
{
    const AVOption *o = NULL;
    int ret, count = 0;
    char *av_uninit(parsed_key), *av_uninit(value);
    const char *key;
    int offset= -1;

    if (!args)
        return 0;

    while (*args) {
        const char *shorthand = NULL;

        o = av_opt_next(ctx->priv, o);
        if (o) {
            if (o->type == AV_OPT_TYPE_CONST || o->offset == offset)
                continue;
            offset = o->offset;
            shorthand = o->name;
        }

        ret = av_opt_get_key_value(&args, "=", ":",
                                   shorthand ? AV_OPT_FLAG_IMPLICIT_KEY : 0,
                                   &parsed_key, &value);
        if (ret < 0) {
            if (ret == AVERROR(EINVAL))
                av_log(ctx, AV_LOG_ERROR, "No option name near '%s'\n", args);
            else
                av_log(ctx, AV_LOG_ERROR, "Unable to parse '%s': %s\n", args,
                       av_err2str(ret));
            return ret;
        }
        if (*args)
            args++;
        if (parsed_key) {
            key = parsed_key;
            while ((o = av_opt_next(ctx->priv, o))); /* discard all remaining shorthand */
        } else {
            key = shorthand;
        }

        av_log(ctx, AV_LOG_DEBUG, "Setting '%s' to value '%s'\n", key, value);

        if (av_opt_find(ctx, key, NULL, 0, 0)) {
            ret = av_opt_set(ctx, key, value, 0);
            if (ret < 0) {
                av_free(value);
                av_free(parsed_key);
                return ret;
            }
        } else {
        av_dict_set(options, key, value, 0);
        if ((ret = av_opt_set(ctx->priv, key, value, 0)) < 0) {
            if (!av_opt_find(ctx->priv, key, NULL, 0, AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ)) {
            if (ret == AVERROR_OPTION_NOT_FOUND)
                av_log(ctx, AV_LOG_ERROR, "Option '%s' not found\n", key);
            av_free(value);
            av_free(parsed_key);
            return ret;
            }
        }
        }

        av_free(value);
        av_free(parsed_key);
        count++;
    }

    if (ctx->enable_str) {
        ret = set_enable_expr(ctx, ctx->enable_str);
        if (ret < 0)
            return ret;
    }
    return count;
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

    if (ctx->filter->init_opaque)
        ret = ctx->filter->init_opaque(ctx, NULL);
    else if (ctx->filter->init)
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
            if (   !strcmp(filter->filter->name, "format")     ||
                   !strcmp(filter->filter->name, "noformat")   ||
                   !strcmp(filter->filter->name, "frei0r")     ||
                   !strcmp(filter->filter->name, "frei0r_src") ||
                   !strcmp(filter->filter->name, "ocv")        ||
                   !strcmp(filter->filter->name, "pan")        ||
                   !strcmp(filter->filter->name, "pp")         ||
                   !strcmp(filter->filter->name, "aevalsrc")) {
            /* a hack for compatibility with the old syntax
             * replace colons with |s */
            char *copy = av_strdup(args);
            char *p    = copy;
            int nb_leading = 0; // number of leading colons to skip
            int deprecated = 0;

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

            deprecated = strchr(p, ':') != NULL;

            if (!strcmp(filter->filter->name, "aevalsrc")) {
                deprecated = 0;
                while ((p = strchr(p, ':')) && p[1] != ':') {
                    const char *epos = strchr(p + 1, '=');
                    const char *spos = strchr(p + 1, ':');
                    const int next_token_is_opt = epos && (!spos || epos < spos);
                    if (next_token_is_opt) {
                        p++;
                        break;
                    }
                    /* next token does not contain a '=', assume a channel expression */
                    deprecated = 1;
                    *p++ = '|';
                }
                if (p && *p == ':') { // double sep '::' found
                    deprecated = 1;
                    memmove(p, p + 1, strlen(p));
                }
            } else
            while ((p = strchr(p, ':')))
                *p++ = '|';

            if (deprecated)
                av_log(filter, AV_LOG_WARNING, "This syntax is deprecated. Use "
                       "'|' to separate the list items.\n");

            av_log(filter, AV_LOG_DEBUG, "compat: called with args=[%s]\n", copy);
            ret = process_options(filter, &options, copy);
            av_freep(&copy);

            if (ret < 0)
                goto fail;
#endif
        } else {
            ret = process_options(filter, &options, args);
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

static int ff_filter_frame_framed(AVFilterLink *link, AVFrame *frame)
{
    int (*filter_frame)(AVFilterLink *, AVFrame *);
    AVFilterContext *dstctx = link->dst;
    AVFilterPad *dst = link->dstpad;
    AVFrame *out = NULL;
    int ret;
    AVFilterCommand *cmd= link->dst->command_queue;
    int64_t pts;

    if (link->closed) {
        av_frame_free(&frame);
        return AVERROR_EOF;
    }

    if (!(filter_frame = dst->filter_frame))
        filter_frame = default_filter_frame;

    /* copy the frame if needed */
    if (dst->needs_writable && !av_frame_is_writable(frame)) {
        av_log(link->dst, AV_LOG_DEBUG, "Copying data in avfilter.\n");

        /* Maybe use ff_copy_buffer_ref instead? */
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
            av_image_copy(out->data, out->linesize, (const uint8_t **)frame->data, frame->linesize,
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

    while(cmd && cmd->time <= out->pts * av_q2d(link->time_base)){
        av_log(link->dst, AV_LOG_DEBUG,
               "Processing command time:%f command:%s arg:%s\n",
               cmd->time, cmd->command, cmd->arg);
        avfilter_process_command(link->dst, cmd->command, cmd->arg, 0, 0, cmd->flags);
        ff_command_queue_pop(link->dst);
        cmd= link->dst->command_queue;
    }

    pts = out->pts;
    if (dstctx->enable_str) {
        int64_t pos = av_frame_get_pkt_pos(out);
        dstctx->var_values[VAR_N] = link->frame_count;
        dstctx->var_values[VAR_T] = pts == AV_NOPTS_VALUE ? NAN : pts * av_q2d(link->time_base);
        dstctx->var_values[VAR_W] = link->w;
        dstctx->var_values[VAR_H] = link->h;
        dstctx->var_values[VAR_POS] = pos == -1 ? NAN : pos;

        dstctx->is_disabled = fabs(av_expr_eval(dstctx->enable, dstctx->var_values, NULL)) < 0.5;
        if (dstctx->is_disabled &&
            (dstctx->filter->flags & AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC))
            filter_frame = default_filter_frame;
    }
    ret = filter_frame(link, out);
    link->frame_count++;
    link->frame_requested = 0;
    ff_update_link_current_pts(link, pts);
    return ret;

fail:
    av_frame_free(&out);
    av_frame_free(&frame);
    return ret;
}

static int ff_filter_frame_needs_framing(AVFilterLink *link, AVFrame *frame)
{
    int insamples = frame->nb_samples, inpos = 0, nb_samples;
    AVFrame *pbuf = link->partial_buf;
    int nb_channels = av_frame_get_channels(frame);
    int ret = 0;

    link->flags |= FF_LINK_FLAG_REQUEST_LOOP;
    /* Handle framing (min_samples, max_samples) */
    while (insamples) {
        if (!pbuf) {
            AVRational samples_tb = { 1, link->sample_rate };
            pbuf = ff_get_audio_buffer(link, link->partial_buf_size);
            if (!pbuf) {
                av_log(link->dst, AV_LOG_WARNING,
                       "Samples dropped due to memory allocation failure.\n");
                return 0;
            }
            av_frame_copy_props(pbuf, frame);
            pbuf->pts = frame->pts;
            if (pbuf->pts != AV_NOPTS_VALUE)
                pbuf->pts += av_rescale_q(inpos, samples_tb, link->time_base);
            pbuf->nb_samples = 0;
        }
        nb_samples = FFMIN(insamples,
                           link->partial_buf_size - pbuf->nb_samples);
        av_samples_copy(pbuf->extended_data, frame->extended_data,
                        pbuf->nb_samples, inpos,
                        nb_samples, nb_channels, link->format);
        inpos                   += nb_samples;
        insamples               -= nb_samples;
        pbuf->nb_samples += nb_samples;
        if (pbuf->nb_samples >= link->min_samples) {
            ret = ff_filter_frame_framed(link, pbuf);
            pbuf = NULL;
        }
    }
    av_frame_free(&frame);
    link->partial_buf = pbuf;
    return ret;
}

int ff_filter_frame(AVFilterLink *link, AVFrame *frame)
{
    FF_TPRINTF_START(NULL, filter_frame); ff_tlog_link(NULL, link, 1); ff_tlog(NULL, " "); ff_tlog_ref(NULL, frame, 1);

    /* Consistency checks */
    if (link->type == AVMEDIA_TYPE_VIDEO) {
        if (strcmp(link->dst->filter->name, "scale") &&
            strcmp(link->dst->filter->name, "idet")) {
            av_assert1(frame->format                 == link->format);
            av_assert1(frame->width               == link->w);
            av_assert1(frame->height               == link->h);
        }
    } else {
        av_assert1(frame->format                == link->format);
        av_assert1(av_frame_get_channels(frame) == link->channels);
        av_assert1(frame->channel_layout        == link->channel_layout);
        av_assert1(frame->sample_rate           == link->sample_rate);
    }

    /* Go directly to actual filtering if possible */
    if (link->type == AVMEDIA_TYPE_AUDIO &&
        link->min_samples &&
        (link->partial_buf ||
         frame->nb_samples < link->min_samples ||
         frame->nb_samples > link->max_samples)) {
        return ff_filter_frame_needs_framing(link, frame);
    } else {
        return ff_filter_frame_framed(link, frame);
    }
}

const AVClass *avfilter_get_class(void)
{
    return &avfilter_class;
}
