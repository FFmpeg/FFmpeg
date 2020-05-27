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

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/buffer.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/rational.h"
#include "libavutil/samplefmt.h"
#include "libavutil/thread.h"

#define FF_INTERNAL_FIELDS 1
#include "framequeue.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "internal.h"

#include "libavutil/ffversion.h"
const char av_filter_ffversion[] = "FFmpeg version " FFMPEG_VERSION;

void ff_tlog_ref(void *ctx, AVFrame *ref, int end)
{
    av_unused char buf[16];
    ff_tlog(ctx,
            "ref[%p buf:%p data:%p linesize[%d, %d, %d, %d] pts:%"PRId64" pos:%"PRId64,
            ref, ref->buf, ref->data[0],
            ref->linesize[0], ref->linesize[1], ref->linesize[2], ref->linesize[3],
            ref->pts, ref->pkt_pos);

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
    return &LICENSE_PREFIX FFMPEG_LICENSE[sizeof(LICENSE_PREFIX) - 1];
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

    av_assert0(src->graph);
    av_assert0(dst->graph);
    av_assert0(src->graph == dst->graph);

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
    ff_framequeue_init(&link->fifo, &src->graph->internal->frame_queues);

    return 0;
}

void avfilter_link_free(AVFilterLink **link)
{
    if (!*link)
        return;

    av_frame_free(&(*link)->partial_buf);
    ff_framequeue_free(&(*link)->fifo);
    ff_frame_pool_uninit((FFFramePool**)&(*link)->frame_pool);

    av_freep(link);
}

#if FF_API_FILTER_GET_SET
int avfilter_link_get_channels(AVFilterLink *link)
{
    return link->channels;
}
#endif

void ff_filter_set_ready(AVFilterContext *filter, unsigned priority)
{
    filter->ready = FFMAX(filter->ready, priority);
}

/**
 * Clear frame_blocked_in on all outputs.
 * This is necessary whenever something changes on input.
 */
static void filter_unblock(AVFilterContext *filter)
{
    unsigned i;

    for (i = 0; i < filter->nb_outputs; i++)
        filter->outputs[i]->frame_blocked_in = 0;
}


void ff_avfilter_link_set_in_status(AVFilterLink *link, int status, int64_t pts)
{
    if (link->status_in == status)
        return;
    av_assert0(!link->status_in);
    link->status_in = status;
    link->status_in_pts = pts;
    link->frame_wanted_out = 0;
    link->frame_blocked_in = 0;
    filter_unblock(link->dst);
    ff_filter_set_ready(link->dst, 200);
}

void ff_avfilter_link_set_out_status(AVFilterLink *link, int status, int64_t pts)
{
    av_assert0(!link->frame_wanted_out);
    av_assert0(!link->status_out);
    link->status_out = status;
    if (pts != AV_NOPTS_VALUE)
        ff_update_link_current_pts(link, pts);
    filter_unblock(link->dst);
    ff_filter_set_ready(link->src, 200);
}

void avfilter_link_set_closed(AVFilterLink *link, int closed)
{
    ff_avfilter_link_set_out_status(link, closed ? AVERROR_EOF : 0, AV_NOPTS_VALUE);
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
        link->current_pts =
        link->current_pts_us = AV_NOPTS_VALUE;

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

                if (inlink) {
                    if (!link->frame_rate.num && !link->frame_rate.den)
                        link->frame_rate = inlink->frame_rate;
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

            if (link->src->nb_inputs && link->src->inputs[0]->hw_frames_ctx &&
                !(link->src->filter->flags_internal & FF_FILTER_FLAG_HWFRAME_AWARE)) {
                av_assert0(!link->hw_frames_ctx &&
                           "should not be set by non-hwframe-aware filter");
                link->hw_frames_ctx = av_buffer_ref(link->src->inputs[0]->hw_frames_ctx);
                if (!link->hw_frames_ctx)
                    return AVERROR(ENOMEM);
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
    FF_TPRINTF_START(NULL, request_frame); ff_tlog_link(NULL, link, 1);

    av_assert1(!link->dst->filter->activate);
    if (link->status_out)
        return link->status_out;
    if (link->status_in) {
        if (ff_framequeue_queued_frames(&link->fifo)) {
            av_assert1(!link->frame_wanted_out);
            av_assert1(link->dst->ready >= 300);
            return 0;
        } else {
            /* Acknowledge status change. Filters using ff_request_frame() will
               handle the change automatically. Filters can also check the
               status directly but none do yet. */
            ff_avfilter_link_set_out_status(link, link->status_in, link->status_in_pts);
            return link->status_out;
        }
    }
    link->frame_wanted_out = 1;
    ff_filter_set_ready(link->src, 100);
    return 0;
}

static int64_t guess_status_pts(AVFilterContext *ctx, int status, AVRational link_time_base)
{
    unsigned i;
    int64_t r = INT64_MAX;

    for (i = 0; i < ctx->nb_inputs; i++)
        if (ctx->inputs[i]->status_out == status)
            r = FFMIN(r, av_rescale_q(ctx->inputs[i]->current_pts, ctx->inputs[i]->time_base, link_time_base));
    if (r < INT64_MAX)
        return r;
    av_log(ctx, AV_LOG_WARNING, "EOF timestamp not reliable\n");
    for (i = 0; i < ctx->nb_inputs; i++)
        r = FFMIN(r, av_rescale_q(ctx->inputs[i]->status_in_pts, ctx->inputs[i]->time_base, link_time_base));
    if (r < INT64_MAX)
        return r;
    return AV_NOPTS_VALUE;
}

static int ff_request_frame_to_filter(AVFilterLink *link)
{
    int ret = -1;

    FF_TPRINTF_START(NULL, request_frame_to_filter); ff_tlog_link(NULL, link, 1);
    /* Assume the filter is blocked, let the method clear it if not */
    link->frame_blocked_in = 1;
    if (link->srcpad->request_frame)
        ret = link->srcpad->request_frame(link);
    else if (link->src->inputs[0])
        ret = ff_request_frame(link->src->inputs[0]);
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN) && ret != link->status_in)
            ff_avfilter_link_set_in_status(link, ret, guess_status_pts(link->src, ret, link->time_base));
        if (ret == AVERROR_EOF)
            ret = 0;
    }
    return ret;
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
    link->current_pts = pts;
    link->current_pts_us = av_rescale_q(pts, link->time_base, AV_TIME_BASE_Q);
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

#if FF_API_CHILD_CLASS_NEXT
static const AVClass *filter_child_class_next(const AVClass *prev)
{
    void *opaque = NULL;
    const AVFilter *f = NULL;

    /* find the filter that corresponds to prev */
    while (prev && (f = av_filter_iterate(&opaque)))
        if (f->priv_class == prev)
            break;

    /* could not find filter corresponding to prev */
    if (prev && !f)
        return NULL;

    /* find next filter with specific options */
    while ((f = av_filter_iterate(&opaque)))
        if (f->priv_class)
            return f->priv_class;

    return NULL;
}
#endif

static const AVClass *filter_child_class_iterate(void **iter)
{
    const AVFilter *f;

    while ((f = av_filter_iterate(iter)))
        if (f->priv_class)
            return f->priv_class;

    return NULL;
}

#define OFFSET(x) offsetof(AVFilterContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption avfilter_options[] = {
    { "thread_type", "Allowed thread types", OFFSET(thread_type), AV_OPT_TYPE_FLAGS,
        { .i64 = AVFILTER_THREAD_SLICE }, 0, INT_MAX, FLAGS, "thread_type" },
        { "slice", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVFILTER_THREAD_SLICE }, .flags = FLAGS, .unit = "thread_type" },
    { "enable", "set enable expression", OFFSET(enable_str), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "threads", "Allowed number of threads", OFFSET(nb_threads), AV_OPT_TYPE_INT,
        { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "extra_hw_frames", "Number of extra hardware frames to allocate for the user",
        OFFSET(extra_hw_frames), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, FLAGS },
    { NULL },
};

static const AVClass avfilter_class = {
    .class_name = "AVFilter",
    .item_name  = default_filter_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
    .child_next = filter_child_next,
#if FF_API_CHILD_CLASS_NEXT
    .child_class_next = filter_child_class_next,
#endif
    .child_class_iterate = filter_child_class_iterate,
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
    int preinited = 0;

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
    if (filter->preinit) {
        if (filter->preinit(ret) < 0)
            goto err;
        preinited = 1;
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

    return ret;

err:
    if (preinited)
        filter->uninit(ret);
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

static void free_link(AVFilterLink *link)
{
    if (!link)
        return;

    if (link->src)
        link->src->outputs[link->srcpad - link->src->output_pads] = NULL;
    if (link->dst)
        link->dst->inputs[link->dstpad - link->dst->input_pads] = NULL;

    av_buffer_unref(&link->hw_frames_ctx);

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

    av_buffer_unref(&filter->hw_device_ctx);

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

int ff_filter_get_nb_threads(AVFilterContext *ctx)
{
    if (ctx->nb_threads > 0)
        return FFMIN(ctx->nb_threads, ctx->graph->nb_threads);
    return ctx->graph->nb_threads;
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
        if ((ret = av_opt_set(ctx->priv, key, value, AV_OPT_SEARCH_CHILDREN)) < 0) {
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

int ff_filter_process_command(AVFilterContext *ctx, const char *cmd,
                              const char *arg, char *res, int res_len, int flags)
{
    const AVOption *o;

    if (!ctx->filter->priv_class)
        return 0;
    o = av_opt_find2(ctx->priv, cmd, NULL, AV_OPT_FLAG_RUNTIME_PARAM | AV_OPT_FLAG_FILTERING_PARAM, AV_OPT_SEARCH_CHILDREN, NULL);
    if (!o)
        return AVERROR(ENOSYS);
    return av_opt_set(ctx->priv, cmd, arg, 0);
}

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
        ret = av_opt_set_dict2(ctx->priv, options, AV_OPT_SEARCH_CHILDREN);
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

#if FF_API_OLD_FILTER_OPTS_ERROR
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

            if (deprecated) {
                av_log(filter, AV_LOG_ERROR, "This syntax is deprecated. Use "
                       "'|' to separate the list items ('%s' instead of '%s')\n",
                       copy, args);
                ret = AVERROR(EINVAL);
            } else {
                ret = process_options(filter, &options, copy);
            }
            av_freep(&copy);

            if (ret < 0)
                goto fail;
        } else
#endif
        {
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
    int ret;

    if (!(filter_frame = dst->filter_frame))
        filter_frame = default_filter_frame;

    if (dst->needs_writable) {
        ret = ff_inlink_make_frame_writable(link, &frame);
        if (ret < 0)
            goto fail;
    }

    ff_inlink_process_commands(link, frame);
    dstctx->is_disabled = !ff_inlink_evaluate_timeline_at_frame(link, frame);

    if (dstctx->is_disabled &&
        (dstctx->filter->flags & AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC))
        filter_frame = default_filter_frame;
    ret = filter_frame(link, frame);
    link->frame_count_out++;
    return ret;

fail:
    av_frame_free(&frame);
    return ret;
}

int ff_filter_frame(AVFilterLink *link, AVFrame *frame)
{
    int ret;
    FF_TPRINTF_START(NULL, filter_frame); ff_tlog_link(NULL, link, 1); ff_tlog(NULL, " "); ff_tlog_ref(NULL, frame, 1);

    /* Consistency checks */
    if (link->type == AVMEDIA_TYPE_VIDEO) {
        if (strcmp(link->dst->filter->name, "buffersink") &&
            strcmp(link->dst->filter->name, "format") &&
            strcmp(link->dst->filter->name, "idet") &&
            strcmp(link->dst->filter->name, "null") &&
            strcmp(link->dst->filter->name, "scale")) {
            av_assert1(frame->format                 == link->format);
            av_assert1(frame->width               == link->w);
            av_assert1(frame->height               == link->h);
        }
    } else {
        if (frame->format != link->format) {
            av_log(link->dst, AV_LOG_ERROR, "Format change is not supported\n");
            goto error;
        }
        if (frame->channels != link->channels) {
            av_log(link->dst, AV_LOG_ERROR, "Channel count change is not supported\n");
            goto error;
        }
        if (frame->channel_layout != link->channel_layout) {
            av_log(link->dst, AV_LOG_ERROR, "Channel layout change is not supported\n");
            goto error;
        }
        if (frame->sample_rate != link->sample_rate) {
            av_log(link->dst, AV_LOG_ERROR, "Sample rate change is not supported\n");
            goto error;
        }
    }

    link->frame_blocked_in = link->frame_wanted_out = 0;
    link->frame_count_in++;
    filter_unblock(link->dst);
    ret = ff_framequeue_add(&link->fifo, frame);
    if (ret < 0) {
        av_frame_free(&frame);
        return ret;
    }
    ff_filter_set_ready(link->dst, 300);
    return 0;

error:
    av_frame_free(&frame);
    return AVERROR_PATCHWELCOME;
}

static int samples_ready(AVFilterLink *link, unsigned min)
{
    return ff_framequeue_queued_frames(&link->fifo) &&
           (ff_framequeue_queued_samples(&link->fifo) >= min ||
            link->status_in);
}

static int take_samples(AVFilterLink *link, unsigned min, unsigned max,
                        AVFrame **rframe)
{
    AVFrame *frame0, *frame, *buf;
    unsigned nb_samples, nb_frames, i, p;
    int ret;

    /* Note: this function relies on no format changes and must only be
       called with enough samples. */
    av_assert1(samples_ready(link, link->min_samples));
    frame0 = frame = ff_framequeue_peek(&link->fifo, 0);
    if (!link->fifo.samples_skipped && frame->nb_samples >= min && frame->nb_samples <= max) {
        *rframe = ff_framequeue_take(&link->fifo);
        return 0;
    }
    nb_frames = 0;
    nb_samples = 0;
    while (1) {
        if (nb_samples + frame->nb_samples > max) {
            if (nb_samples < min)
                nb_samples = max;
            break;
        }
        nb_samples += frame->nb_samples;
        nb_frames++;
        if (nb_frames == ff_framequeue_queued_frames(&link->fifo))
            break;
        frame = ff_framequeue_peek(&link->fifo, nb_frames);
    }

    buf = ff_get_audio_buffer(link, nb_samples);
    if (!buf)
        return AVERROR(ENOMEM);
    ret = av_frame_copy_props(buf, frame0);
    if (ret < 0) {
        av_frame_free(&buf);
        return ret;
    }
    buf->pts = frame0->pts;

    p = 0;
    for (i = 0; i < nb_frames; i++) {
        frame = ff_framequeue_take(&link->fifo);
        av_samples_copy(buf->extended_data, frame->extended_data, p, 0,
                        frame->nb_samples, link->channels, link->format);
        p += frame->nb_samples;
        av_frame_free(&frame);
    }
    if (p < nb_samples) {
        unsigned n = nb_samples - p;
        frame = ff_framequeue_peek(&link->fifo, 0);
        av_samples_copy(buf->extended_data, frame->extended_data, p, 0, n,
                        link->channels, link->format);
        ff_framequeue_skip_samples(&link->fifo, n, link->time_base);
    }

    *rframe = buf;
    return 0;
}

static int ff_filter_frame_to_filter(AVFilterLink *link)
{
    AVFrame *frame = NULL;
    AVFilterContext *dst = link->dst;
    int ret;

    av_assert1(ff_framequeue_queued_frames(&link->fifo));
    ret = link->min_samples ?
          ff_inlink_consume_samples(link, link->min_samples, link->max_samples, &frame) :
          ff_inlink_consume_frame(link, &frame);
    av_assert1(ret);
    if (ret < 0) {
        av_assert1(!frame);
        return ret;
    }
    /* The filter will soon have received a new frame, that may allow it to
       produce one or more: unblock its outputs. */
    filter_unblock(dst);
    /* AVFilterPad.filter_frame() expect frame_count_out to have the value
       before the frame; ff_filter_frame_framed() will re-increment it. */
    link->frame_count_out--;
    ret = ff_filter_frame_framed(link, frame);
    if (ret < 0 && ret != link->status_out) {
        ff_avfilter_link_set_out_status(link, ret, AV_NOPTS_VALUE);
    } else {
        /* Run once again, to see if several frames were available, or if
           the input status has also changed, or any other reason. */
        ff_filter_set_ready(dst, 300);
    }
    return ret;
}

static int forward_status_change(AVFilterContext *filter, AVFilterLink *in)
{
    unsigned out = 0, progress = 0;
    int ret;

    av_assert0(!in->status_out);
    if (!filter->nb_outputs) {
        /* not necessary with the current API and sinks */
        return 0;
    }
    while (!in->status_out) {
        if (!filter->outputs[out]->status_in) {
            progress++;
            ret = ff_request_frame_to_filter(filter->outputs[out]);
            if (ret < 0)
                return ret;
        }
        if (++out == filter->nb_outputs) {
            if (!progress) {
                /* Every output already closed: input no longer interesting
                   (example: overlay in shortest mode, other input closed). */
                ff_avfilter_link_set_out_status(in, in->status_in, in->status_in_pts);
                return 0;
            }
            progress = 0;
            out = 0;
        }
    }
    ff_filter_set_ready(filter, 200);
    return 0;
}

static int ff_filter_activate_default(AVFilterContext *filter)
{
    unsigned i;

    for (i = 0; i < filter->nb_inputs; i++) {
        if (samples_ready(filter->inputs[i], filter->inputs[i]->min_samples)) {
            return ff_filter_frame_to_filter(filter->inputs[i]);
        }
    }
    for (i = 0; i < filter->nb_inputs; i++) {
        if (filter->inputs[i]->status_in && !filter->inputs[i]->status_out) {
            av_assert1(!ff_framequeue_queued_frames(&filter->inputs[i]->fifo));
            return forward_status_change(filter, filter->inputs[i]);
        }
    }
    for (i = 0; i < filter->nb_outputs; i++) {
        if (filter->outputs[i]->frame_wanted_out &&
            !filter->outputs[i]->frame_blocked_in) {
            return ff_request_frame_to_filter(filter->outputs[i]);
        }
    }
    return FFERROR_NOT_READY;
}

/*
   Filter scheduling and activation

   When a filter is activated, it must:
   - if possible, output a frame;
   - else, if relevant, forward the input status change;
   - else, check outputs for wanted frames and forward the requests.

   The following AVFilterLink fields are used for activation:

   - frame_wanted_out:

     This field indicates if a frame is needed on this input of the
     destination filter. A positive value indicates that a frame is needed
     to process queued frames or internal data or to satisfy the
     application; a zero value indicates that a frame is not especially
     needed but could be processed anyway; a negative value indicates that a
     frame would just be queued.

     It is set by filters using ff_request_frame() or ff_request_no_frame(),
     when requested by the application through a specific API or when it is
     set on one of the outputs.

     It is cleared when a frame is sent from the source using
     ff_filter_frame().

     It is also cleared when a status change is sent from the source using
     ff_avfilter_link_set_in_status().

   - frame_blocked_in:

     This field means that the source filter can not generate a frame as is.
     Its goal is to avoid repeatedly calling the request_frame() method on
     the same link.

     It is set by the framework on all outputs of a filter before activating it.

     It is automatically cleared by ff_filter_frame().

     It is also automatically cleared by ff_avfilter_link_set_in_status().

     It is also cleared on all outputs (using filter_unblock()) when
     something happens on an input: processing a frame or changing the
     status.

   - fifo:

     Contains the frames queued on a filter input. If it contains frames and
     frame_wanted_out is not set, then the filter can be activated. If that
     result in the filter not able to use these frames, the filter must set
     frame_wanted_out to ask for more frames.

   - status_in and status_in_pts:

     Status (EOF or error code) of the link and timestamp of the status
     change (in link time base, same as frames) as seen from the input of
     the link. The status change is considered happening after the frames
     queued in fifo.

     It is set by the source filter using ff_avfilter_link_set_in_status().

   - status_out:

     Status of the link as seen from the output of the link. The status
     change is considered having already happened.

     It is set by the destination filter using
     ff_avfilter_link_set_out_status().

   Filters are activated according to the ready field, set using the
   ff_filter_set_ready(). Eventually, a priority queue will be used.
   ff_filter_set_ready() is called whenever anything could cause progress to
   be possible. Marking a filter ready when it is not is not a problem,
   except for the small overhead it causes.

   Conditions that cause a filter to be marked ready are:

   - frames added on an input link;

   - changes in the input or output status of an input link;

   - requests for a frame on an output link;

   - after any actual processing using the legacy methods (filter_frame(),
     and request_frame() to acknowledge status changes), to run once more
     and check if enough input was present for several frames.

   Examples of scenarios to consider:

   - buffersrc: activate if frame_wanted_out to notify the application;
     activate when the application adds a frame to push it immediately.

   - testsrc: activate only if frame_wanted_out to produce and push a frame.

   - concat (not at stitch points): can process a frame on any output.
     Activate if frame_wanted_out on output to forward on the corresponding
     input. Activate when a frame is present on input to process it
     immediately.

   - framesync: needs at least one frame on each input; extra frames on the
     wrong input will accumulate. When a frame is first added on one input,
     set frame_wanted_out<0 on it to avoid getting more (would trigger
     testsrc) and frame_wanted_out>0 on the other to allow processing it.

   Activation of old filters:

   In order to activate a filter implementing the legacy filter_frame() and
   request_frame() methods, perform the first possible of the following
   actions:

   - If an input has frames in fifo and frame_wanted_out == 0, dequeue a
     frame and call filter_frame().

     Rationale: filter frames as soon as possible instead of leaving them
     queued; frame_wanted_out < 0 is not possible since the old API does not
     set it nor provides any similar feedback; frame_wanted_out > 0 happens
     when min_samples > 0 and there are not enough samples queued.

   - If an input has status_in set but not status_out, try to call
     request_frame() on one of the outputs in the hope that it will trigger
     request_frame() on the input with status_in and acknowledge it. This is
     awkward and fragile, filters with several inputs or outputs should be
     updated to direct activation as soon as possible.

   - If an output has frame_wanted_out > 0 and not frame_blocked_in, call
     request_frame().

     Rationale: checking frame_blocked_in is necessary to avoid requesting
     repeatedly on a blocked input if another is not blocked (example:
     [buffersrc1][testsrc1][buffersrc2][testsrc2]concat=v=2).

     TODO: respect needs_fifo and remove auto-inserted fifos.

 */

int ff_filter_activate(AVFilterContext *filter)
{
    int ret;

    /* Generic timeline support is not yet implemented but should be easy */
    av_assert1(!(filter->filter->flags & AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC &&
                 filter->filter->activate));
    filter->ready = 0;
    ret = filter->filter->activate ? filter->filter->activate(filter) :
          ff_filter_activate_default(filter);
    if (ret == FFERROR_NOT_READY)
        ret = 0;
    return ret;
}

int ff_inlink_acknowledge_status(AVFilterLink *link, int *rstatus, int64_t *rpts)
{
    *rpts = link->current_pts;
    if (ff_framequeue_queued_frames(&link->fifo))
        return *rstatus = 0;
    if (link->status_out)
        return *rstatus = link->status_out;
    if (!link->status_in)
        return *rstatus = 0;
    *rstatus = link->status_out = link->status_in;
    ff_update_link_current_pts(link, link->status_in_pts);
    *rpts = link->current_pts;
    return 1;
}

size_t ff_inlink_queued_frames(AVFilterLink *link)
{
    return ff_framequeue_queued_frames(&link->fifo);
}

int ff_inlink_check_available_frame(AVFilterLink *link)
{
    return ff_framequeue_queued_frames(&link->fifo) > 0;
}

int ff_inlink_queued_samples(AVFilterLink *link)
{
    return ff_framequeue_queued_samples(&link->fifo);
}

int ff_inlink_check_available_samples(AVFilterLink *link, unsigned min)
{
    uint64_t samples = ff_framequeue_queued_samples(&link->fifo);
    av_assert1(min);
    return samples >= min || (link->status_in && samples);
}

static void consume_update(AVFilterLink *link, const AVFrame *frame)
{
    ff_update_link_current_pts(link, frame->pts);
    ff_inlink_process_commands(link, frame);
    link->dst->is_disabled = !ff_inlink_evaluate_timeline_at_frame(link, frame);
    link->frame_count_out++;
}

int ff_inlink_consume_frame(AVFilterLink *link, AVFrame **rframe)
{
    AVFrame *frame;

    *rframe = NULL;
    if (!ff_inlink_check_available_frame(link))
        return 0;

    if (link->fifo.samples_skipped) {
        frame = ff_framequeue_peek(&link->fifo, 0);
        return ff_inlink_consume_samples(link, frame->nb_samples, frame->nb_samples, rframe);
    }

    frame = ff_framequeue_take(&link->fifo);
    consume_update(link, frame);
    *rframe = frame;
    return 1;
}

int ff_inlink_consume_samples(AVFilterLink *link, unsigned min, unsigned max,
                            AVFrame **rframe)
{
    AVFrame *frame;
    int ret;

    av_assert1(min);
    *rframe = NULL;
    if (!ff_inlink_check_available_samples(link, min))
        return 0;
    if (link->status_in)
        min = FFMIN(min, ff_framequeue_queued_samples(&link->fifo));
    ret = take_samples(link, min, max, &frame);
    if (ret < 0)
        return ret;
    consume_update(link, frame);
    *rframe = frame;
    return 1;
}

AVFrame *ff_inlink_peek_frame(AVFilterLink *link, size_t idx)
{
    return ff_framequeue_peek(&link->fifo, idx);
}

int ff_inlink_make_frame_writable(AVFilterLink *link, AVFrame **rframe)
{
    AVFrame *frame = *rframe;
    AVFrame *out;
    int ret;

    if (av_frame_is_writable(frame))
        return 0;
    av_log(link->dst, AV_LOG_DEBUG, "Copying data in avfilter.\n");

    switch (link->type) {
    case AVMEDIA_TYPE_VIDEO:
        out = ff_get_video_buffer(link, link->w, link->h);
        break;
    case AVMEDIA_TYPE_AUDIO:
        out = ff_get_audio_buffer(link, frame->nb_samples);
        break;
    default:
        return AVERROR(EINVAL);
    }
    if (!out)
        return AVERROR(ENOMEM);

    ret = av_frame_copy_props(out, frame);
    if (ret < 0) {
        av_frame_free(&out);
        return ret;
    }

    switch (link->type) {
    case AVMEDIA_TYPE_VIDEO:
        av_image_copy(out->data, out->linesize, (const uint8_t **)frame->data, frame->linesize,
                      frame->format, frame->width, frame->height);
        break;
    case AVMEDIA_TYPE_AUDIO:
        av_samples_copy(out->extended_data, frame->extended_data,
                        0, 0, frame->nb_samples,
                        frame->channels,
                        frame->format);
        break;
    default:
        av_assert0(!"reached");
    }

    av_frame_free(&frame);
    *rframe = out;
    return 0;
}

int ff_inlink_process_commands(AVFilterLink *link, const AVFrame *frame)
{
    AVFilterCommand *cmd = link->dst->command_queue;

    while(cmd && cmd->time <= frame->pts * av_q2d(link->time_base)){
        av_log(link->dst, AV_LOG_DEBUG,
               "Processing command time:%f command:%s arg:%s\n",
               cmd->time, cmd->command, cmd->arg);
        avfilter_process_command(link->dst, cmd->command, cmd->arg, 0, 0, cmd->flags);
        ff_command_queue_pop(link->dst);
        cmd= link->dst->command_queue;
    }
    return 0;
}

int ff_inlink_evaluate_timeline_at_frame(AVFilterLink *link, const AVFrame *frame)
{
    AVFilterContext *dstctx = link->dst;
    int64_t pts = frame->pts;
    int64_t pos = frame->pkt_pos;

    if (!dstctx->enable_str)
        return 1;

    dstctx->var_values[VAR_N] = link->frame_count_out;
    dstctx->var_values[VAR_T] = pts == AV_NOPTS_VALUE ? NAN : pts * av_q2d(link->time_base);
    dstctx->var_values[VAR_W] = link->w;
    dstctx->var_values[VAR_H] = link->h;
    dstctx->var_values[VAR_POS] = pos == -1 ? NAN : pos;

    return fabs(av_expr_eval(dstctx->enable, dstctx->var_values, NULL)) >= 0.5;
}

void ff_inlink_request_frame(AVFilterLink *link)
{
    av_assert1(!link->status_in);
    av_assert1(!link->status_out);
    link->frame_wanted_out = 1;
    ff_filter_set_ready(link->src, 100);
}

void ff_inlink_set_status(AVFilterLink *link, int status)
{
    if (link->status_out)
        return;
    link->frame_wanted_out = 0;
    link->frame_blocked_in = 0;
    ff_avfilter_link_set_out_status(link, status, AV_NOPTS_VALUE);
    while (ff_framequeue_queued_frames(&link->fifo)) {
           AVFrame *frame = ff_framequeue_take(&link->fifo);
           av_frame_free(&frame);
    }
    if (!link->status_in)
        link->status_in = status;
}

int ff_outlink_get_status(AVFilterLink *link)
{
    return link->status_in;
}

const AVClass *avfilter_get_class(void)
{
    return &avfilter_class;
}

int ff_filter_init_hw_frames(AVFilterContext *avctx, AVFilterLink *link,
                             int default_pool_size)
{
    AVHWFramesContext *frames;

    // Must already be set by caller.
    av_assert0(link->hw_frames_ctx);

    frames = (AVHWFramesContext*)link->hw_frames_ctx->data;

    if (frames->initial_pool_size == 0) {
        // Dynamic allocation is necessarily supported.
    } else if (avctx->extra_hw_frames >= 0) {
        frames->initial_pool_size += avctx->extra_hw_frames;
    } else {
        frames->initial_pool_size = default_pool_size;
    }

    return 0;
}
