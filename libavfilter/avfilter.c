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

/* #define DEBUG */

#include "libavutil/pixdesc.h"
#include "libavutil/rational.h"
#include "libavutil/audioconvert.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"

unsigned avfilter_version(void) {
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

void avfilter_insert_pad(unsigned idx, unsigned *count, size_t padidx_off,
                         AVFilterPad **pads, AVFilterLink ***links,
                         AVFilterPad *newpad)
{
    unsigned i;

    idx = FFMIN(idx, *count);

    *pads  = av_realloc(*pads,  sizeof(AVFilterPad)   * (*count + 1));
    *links = av_realloc(*links, sizeof(AVFilterLink*) * (*count + 1));
    memmove(*pads +idx+1, *pads +idx, sizeof(AVFilterPad)   * (*count-idx));
    memmove(*links+idx+1, *links+idx, sizeof(AVFilterLink*) * (*count-idx));
    memcpy(*pads+idx, newpad, sizeof(AVFilterPad));
    (*links)[idx] = NULL;

    (*count)++;
    for (i = idx+1; i < *count; i++)
        if (*links[i])
            (*(unsigned *)((uint8_t *) *links[i] + padidx_off))++;
}

int avfilter_link(AVFilterContext *src, unsigned srcpad,
                  AVFilterContext *dst, unsigned dstpad)
{
    AVFilterLink *link;

    if (src->output_count <= srcpad || dst->input_count <= dstpad ||
        src->outputs[srcpad]        || dst->inputs[dstpad])
        return -1;

    if (src->output_pads[srcpad].type != dst->input_pads[dstpad].type) {
        av_log(src, AV_LOG_ERROR,
               "Media type mismatch between the '%s' filter output pad %d and the '%s' filter input pad %d\n",
               src->name, srcpad, dst->name, dstpad);
        return AVERROR(EINVAL);
    }

    src->outputs[srcpad] =
    dst-> inputs[dstpad] = link = av_mallocz(sizeof(AVFilterLink));

    link->src     = src;
    link->dst     = dst;
    link->srcpad  = &src->output_pads[srcpad];
    link->dstpad  = &dst->input_pads[dstpad];
    link->type    = src->output_pads[srcpad].type;
    assert(PIX_FMT_NONE == -1 && AV_SAMPLE_FMT_NONE == -1);
    link->format  = -1;

    return 0;
}

void avfilter_link_free(AVFilterLink **link)
{
    if (!*link)
        return;

    if ((*link)->pool)
        ff_free_pool((*link)->pool);

    av_freep(link);
}

int avfilter_insert_filter(AVFilterLink *link, AVFilterContext *filt,
                           unsigned filt_srcpad_idx, unsigned filt_dstpad_idx)
{
    int ret;
    unsigned dstpad_idx = link->dstpad - link->dst->input_pads;

    av_log(link->dst, AV_LOG_INFO, "auto-inserting filter '%s' "
           "between the filter '%s' and the filter '%s'\n",
           filt->name, link->src->name, link->dst->name);

    link->dst->inputs[dstpad_idx] = NULL;
    if ((ret = avfilter_link(filt, filt_dstpad_idx, link->dst, dstpad_idx)) < 0) {
        /* failed to link output filter to new filter */
        link->dst->inputs[dstpad_idx] = link;
        return ret;
    }

    /* re-hookup the link to the new destination filter we inserted */
    link->dst = filt;
    link->dstpad = &filt->input_pads[filt_srcpad_idx];
    filt->inputs[filt_srcpad_idx] = link;

    /* if any information on supported media formats already exists on the
     * link, we need to preserve that */
    if (link->out_formats)
        avfilter_formats_changeref(&link->out_formats,
                                   &filt->outputs[filt_dstpad_idx]->out_formats);
    if (link->out_channel_layouts)
        ff_channel_layouts_changeref(&link->out_channel_layouts,
                                     &filt->outputs[filt_dstpad_idx]->out_channel_layouts);
    if (link->out_samplerates)
        avfilter_formats_changeref(&link->out_samplerates,
                                   &filt->outputs[filt_dstpad_idx]->out_samplerates);

    return 0;
}

int avfilter_config_links(AVFilterContext *filter)
{
    int (*config_link)(AVFilterLink *);
    unsigned i;
    int ret;

    for (i = 0; i < filter->input_count; i ++) {
        AVFilterLink *link = filter->inputs[i];
        AVFilterLink *inlink = link->src->input_count ?
            link->src->inputs[0] : NULL;

        if (!link) continue;

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
                if (link->src->input_count != 1) {
                    av_log(link->src, AV_LOG_ERROR, "Source filters and filters "
                                                    "with more than one input "
                                                    "must set config_props() "
                                                    "callbacks on all outputs\n");
                    return AVERROR(EINVAL);
                }
            } else if ((ret = config_link(link)) < 0)
                return ret;

            switch (link->type) {
            case AVMEDIA_TYPE_VIDEO:
                if (!link->time_base.num && !link->time_base.den)
                    link->time_base = inlink ? inlink->time_base : AV_TIME_BASE_Q;

                if (!link->sample_aspect_ratio.num && !link->sample_aspect_ratio.den)
                    link->sample_aspect_ratio = inlink ?
                        inlink->sample_aspect_ratio : (AVRational){1,1};

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
                    if (!link->sample_rate)
                        link->sample_rate = inlink->sample_rate;
                    if (!link->time_base.num && !link->time_base.den)
                        link->time_base = inlink->time_base;
                } else if (!link->sample_rate) {
                    av_log(link->src, AV_LOG_ERROR,
                           "Audio source filters must set their output link's "
                           "sample_rate\n");
                    return AVERROR(EINVAL);
                }

                if (!link->time_base.num && !link->time_base.den)
                    link->time_base = (AVRational) {1, link->sample_rate};
            }

            if ((config_link = link->dstpad->config_props))
                if ((ret = config_link(link)) < 0)
                    return ret;

            link->init_state = AVLINK_INIT;
        }
    }

    return 0;
}

void ff_dlog_link(void *ctx, AVFilterLink *link, int end)
{
    if (link->type == AVMEDIA_TYPE_VIDEO) {
        av_dlog(ctx,
                "link[%p s:%dx%d fmt:%s %s->%s]%s",
                link, link->w, link->h,
                av_pix_fmt_descriptors[link->format].name,
                link->src ? link->src->filter->name : "",
                link->dst ? link->dst->filter->name : "",
                end ? "\n" : "");
    } else {
        char buf[128];
        av_get_channel_layout_string(buf, sizeof(buf), -1, link->channel_layout);

        av_dlog(ctx,
                "link[%p r:%d cl:%s fmt:%s %s->%s]%s",
                link, (int)link->sample_rate, buf,
                av_get_sample_fmt_name(link->format),
                link->src ? link->src->filter->name : "",
                link->dst ? link->dst->filter->name : "",
                end ? "\n" : "");
    }
}

int avfilter_request_frame(AVFilterLink *link)
{
    FF_DPRINTF_START(NULL, request_frame); ff_dlog_link(NULL, link, 1);

    if (link->srcpad->request_frame)
        return link->srcpad->request_frame(link);
    else if (link->src->inputs[0])
        return avfilter_request_frame(link->src->inputs[0]);
    else return -1;
}

int avfilter_poll_frame(AVFilterLink *link)
{
    int i, min = INT_MAX;

    if (link->srcpad->poll_frame)
        return link->srcpad->poll_frame(link);

    for (i = 0; i < link->src->input_count; i++) {
        int val;
        if (!link->src->inputs[i])
            return -1;
        val = avfilter_poll_frame(link->src->inputs[i]);
        min = FFMIN(min, val);
    }

    return min;
}

void ff_update_link_current_pts(AVFilterLink *link, int64_t pts)
{
    if (pts == AV_NOPTS_VALUE)
        return;
    link->current_pts =  pts; /* TODO use duration */
    if (link->graph && link->age_index >= 0)
        ff_avfilter_graph_update_heap(link->graph, link);
}

int avfilter_process_command(AVFilterContext *filter, const char *cmd, const char *arg, char *res, int res_len, int flags)
{
    if(!strcmp(cmd, "ping")){
        av_strlcatf(res, res_len, "pong from:%s %s\n", filter->filter->name, filter->name);
        return 0;
    }else if(filter->filter->process_command) {
        return filter->filter->process_command(filter, cmd, arg, res, res_len, flags);
    }
    return AVERROR(ENOSYS);
}

#define MAX_REGISTERED_AVFILTERS_NB 128

static AVFilter *registered_avfilters[MAX_REGISTERED_AVFILTERS_NB + 1];

static int next_registered_avfilter_idx = 0;

AVFilter *avfilter_get_by_name(const char *name)
{
    int i;

    for (i = 0; registered_avfilters[i]; i++)
        if (!strcmp(registered_avfilters[i]->name, name))
            return registered_avfilters[i];

    return NULL;
}

int avfilter_register(AVFilter *filter)
{
    if (next_registered_avfilter_idx == MAX_REGISTERED_AVFILTERS_NB) {
        av_log(NULL, AV_LOG_ERROR,
               "Maximum number of registered filters %d reached, "
               "impossible to register filter with name '%s'\n",
               MAX_REGISTERED_AVFILTERS_NB, filter->name);
        return AVERROR(ENOMEM);
    }

    registered_avfilters[next_registered_avfilter_idx++] = filter;
    return 0;
}

AVFilter **av_filter_next(AVFilter **filter)
{
    return filter ? ++filter : &registered_avfilters[0];
}

void avfilter_uninit(void)
{
    memset(registered_avfilters, 0, sizeof(registered_avfilters));
    next_registered_avfilter_idx = 0;
}

static int pad_count(const AVFilterPad *pads)
{
    int count;

    for(count = 0; pads->name; count ++) pads ++;
    return count;
}

static const char *filter_name(void *p)
{
    AVFilterContext *filter = p;
    return filter->filter->name;
}

static const AVClass avfilter_class = {
    "AVFilter",
    filter_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

int avfilter_open(AVFilterContext **filter_ctx, AVFilter *filter, const char *inst_name)
{
    AVFilterContext *ret;
    *filter_ctx = NULL;

    if (!filter)
        return AVERROR(EINVAL);

    ret = av_mallocz(sizeof(AVFilterContext));
    if (!ret)
        return AVERROR(ENOMEM);

    ret->av_class = &avfilter_class;
    ret->filter   = filter;
    ret->name     = inst_name ? av_strdup(inst_name) : NULL;
    if (filter->priv_size) {
        ret->priv     = av_mallocz(filter->priv_size);
        if (!ret->priv)
            goto err;
    }

    ret->input_count  = pad_count(filter->inputs);
    if (ret->input_count) {
        ret->input_pads   = av_malloc(sizeof(AVFilterPad) * ret->input_count);
        if (!ret->input_pads)
            goto err;
        memcpy(ret->input_pads, filter->inputs, sizeof(AVFilterPad) * ret->input_count);
        ret->inputs       = av_mallocz(sizeof(AVFilterLink*) * ret->input_count);
        if (!ret->inputs)
            goto err;
    }

    ret->output_count = pad_count(filter->outputs);
    if (ret->output_count) {
        ret->output_pads  = av_malloc(sizeof(AVFilterPad) * ret->output_count);
        if (!ret->output_pads)
            goto err;
        memcpy(ret->output_pads, filter->outputs, sizeof(AVFilterPad) * ret->output_count);
        ret->outputs      = av_mallocz(sizeof(AVFilterLink*) * ret->output_count);
        if (!ret->outputs)
            goto err;
    }

    *filter_ctx = ret;
    return 0;

err:
    av_freep(&ret->inputs);
    av_freep(&ret->input_pads);
    ret->input_count = 0;
    av_freep(&ret->outputs);
    av_freep(&ret->output_pads);
    ret->output_count = 0;
    av_freep(&ret->priv);
    av_free(ret);
    return AVERROR(ENOMEM);
}

void avfilter_free(AVFilterContext *filter)
{
    int i;
    AVFilterLink *link;

    if (!filter)
        return;

    if (filter->filter->uninit)
        filter->filter->uninit(filter);

    for (i = 0; i < filter->input_count; i++) {
        if ((link = filter->inputs[i])) {
            if (link->src)
                link->src->outputs[link->srcpad - link->src->output_pads] = NULL;
            avfilter_formats_unref(&link->in_formats);
            avfilter_formats_unref(&link->out_formats);
            avfilter_formats_unref(&link->in_samplerates);
            avfilter_formats_unref(&link->out_samplerates);
            ff_channel_layouts_unref(&link->in_channel_layouts);
            ff_channel_layouts_unref(&link->out_channel_layouts);
        }
        avfilter_link_free(&link);
    }
    for (i = 0; i < filter->output_count; i++) {
        if ((link = filter->outputs[i])) {
            if (link->dst)
                link->dst->inputs[link->dstpad - link->dst->input_pads] = NULL;
            avfilter_formats_unref(&link->in_formats);
            avfilter_formats_unref(&link->out_formats);
            avfilter_formats_unref(&link->in_samplerates);
            avfilter_formats_unref(&link->out_samplerates);
            ff_channel_layouts_unref(&link->in_channel_layouts);
            ff_channel_layouts_unref(&link->out_channel_layouts);
        }
        avfilter_link_free(&link);
    }

    av_freep(&filter->name);
    av_freep(&filter->input_pads);
    av_freep(&filter->output_pads);
    av_freep(&filter->inputs);
    av_freep(&filter->outputs);
    av_freep(&filter->priv);
    while(filter->command_queue){
        ff_command_queue_pop(filter);
    }
    av_free(filter);
}

int avfilter_init_filter(AVFilterContext *filter, const char *args, void *opaque)
{
    int ret=0;

    if (filter->filter->init)
        ret = filter->filter->init(filter, args, opaque);
    return ret;
}
