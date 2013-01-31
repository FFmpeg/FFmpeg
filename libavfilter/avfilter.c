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
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/rational.h"
#include "libavutil/samplefmt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "audio.h"

static int ff_filter_frame_framed(AVFilterLink *link, AVFilterBufferRef *frame);

char *ff_get_ref_perms_string(char *buf, size_t buf_size, int perms)
{
    snprintf(buf, buf_size, "%s%s%s%s%s%s",
             perms & AV_PERM_READ      ? "r" : "",
             perms & AV_PERM_WRITE     ? "w" : "",
             perms & AV_PERM_PRESERVE  ? "p" : "",
             perms & AV_PERM_REUSE     ? "u" : "",
             perms & AV_PERM_REUSE2    ? "U" : "",
             perms & AV_PERM_NEG_LINESIZES ? "n" : "");
    return buf;
}

void ff_tlog_ref(void *ctx, AVFilterBufferRef *ref, int end)
{
    av_unused char buf[16];
    ff_tlog(ctx,
            "ref[%p buf:%p refcount:%d perms:%s data:%p linesize[%d, %d, %d, %d] pts:%"PRId64" pos:%"PRId64,
            ref, ref->buf, ref->buf->refcount, ff_get_ref_perms_string(buf, sizeof(buf), ref->perms), ref->data[0],
            ref->linesize[0], ref->linesize[1], ref->linesize[2], ref->linesize[3],
            ref->pts, ref->pos);

    if (ref->video) {
        ff_tlog(ctx, " a:%d/%d s:%dx%d i:%c iskey:%d type:%c",
                ref->video->sample_aspect_ratio.num, ref->video->sample_aspect_ratio.den,
                ref->video->w, ref->video->h,
                !ref->video->interlaced     ? 'P' :         /* Progressive  */
                ref->video->top_field_first ? 'T' : 'B',    /* Top / Bottom */
                ref->video->key_frame,
                av_get_picture_type_char(ref->video->pict_type));
    }
    if (ref->audio) {
        ff_tlog(ctx, " cl:%"PRId64"d n:%d r:%d",
                ref->audio->channel_layout,
                ref->audio->nb_samples,
                ref->audio->sample_rate);
    }

    ff_tlog(ctx, "]%s", end ? "\n" : "");
}

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

void ff_insert_pad(unsigned idx, unsigned *count, size_t padidx_off,
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

    if (src->nb_outputs <= srcpad || dst->nb_inputs <= dstpad ||
        src->outputs[srcpad]      || dst->inputs[dstpad])
        return -1;

    if (src->output_pads[srcpad].type != dst->input_pads[dstpad].type) {
        av_log(src, AV_LOG_ERROR,
               "Media type mismatch between the '%s' filter output pad %d (%s) and the '%s' filter input pad %d (%s)\n",
               src->name, srcpad, (char *)av_x_if_null(av_get_media_type_string(src->output_pads[srcpad].type), "?"),
               dst->name, dstpad, (char *)av_x_if_null(av_get_media_type_string(dst-> input_pads[dstpad].type), "?"));
        return AVERROR(EINVAL);
    }

    src->outputs[srcpad] =
    dst-> inputs[dstpad] = link = av_mallocz(sizeof(AVFilterLink));

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

    if ((*link)->pool)
        ff_free_pool((*link)->pool);

    avfilter_unref_bufferp(&(*link)->partial_buf);

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
    link->dst = filt;
    link->dstpad = &filt->input_pads[filt_srcpad_idx];
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
    if (link->srcpad->request_frame)
        ret = link->srcpad->request_frame(link);
    else if (link->src->inputs[0])
        ret = ff_request_frame(link->src->inputs[0]);
    if (ret == AVERROR_EOF && link->partial_buf) {
        AVFilterBufferRef *pbuf = link->partial_buf;
        link->partial_buf = NULL;
        ff_filter_frame_framed(link, pbuf);
        return 0;
    }
    if (ret == AVERROR_EOF)
        link->closed = 1;
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
            return -1;
        val = ff_poll_frame(link->src->inputs[i]);
        min = FFMIN(min, val);
    }

    return min;
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
        av_strlcatf(res, res_len, "pong from:%s %s\n", filter->filter->name, filter->name);
        return 0;
    }else if(filter->filter->process_command) {
        return filter->filter->process_command(filter, cmd, arg, res, res_len, flags);
    }
    return AVERROR(ENOSYS);
}

#define MAX_REGISTERED_AVFILTERS_NB 256

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
    int i;

    if (next_registered_avfilter_idx == MAX_REGISTERED_AVFILTERS_NB) {
        av_log(NULL, AV_LOG_ERROR,
               "Maximum number of registered filters %d reached, "
               "impossible to register filter with name '%s'\n",
               MAX_REGISTERED_AVFILTERS_NB, filter->name);
        return AVERROR(ENOMEM);
    }

    for(i=0; filter->inputs && filter->inputs[i].name; i++) {
        const AVFilterPad *input = &filter->inputs[i];
        av_assert0(     !input->filter_frame
                    || (!input->start_frame && !input->end_frame));
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

    if (!pads)
        return 0;

    for(count = 0; pads->name; count ++) pads ++;
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
    if (!prev && ctx->filter && ctx->filter->priv_class)
        return ctx->priv;
    return NULL;
}

static const AVClass *filter_child_class_next(const AVClass *prev)
{
    AVFilter **filter_ptr = NULL;

    /* find the filter that corresponds to prev */
    while (prev && *(filter_ptr = av_filter_next(filter_ptr)))
        if ((*filter_ptr)->priv_class == prev)
            break;

    /* could not find filter corresponding to prev */
    if (prev && !(*filter_ptr))
        return NULL;

    /* find next filter with specific options */
    while (*(filter_ptr = av_filter_next(filter_ptr)))
        if ((*filter_ptr)->priv_class)
            return (*filter_ptr)->priv_class;
    return NULL;
}

static const AVClass avfilter_class = {
    .class_name = "AVFilter",
    .item_name  = default_filter_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
    .child_next = filter_child_next,
    .child_class_next = filter_child_class_next,
};

const AVClass *avfilter_get_class(void)
{
    return &avfilter_class;
}

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

    ret->nb_inputs = pad_count(filter->inputs);
    if (ret->nb_inputs ) {
        ret->input_pads   = av_malloc(sizeof(AVFilterPad) * ret->nb_inputs);
        if (!ret->input_pads)
            goto err;
        memcpy(ret->input_pads, filter->inputs, sizeof(AVFilterPad) * ret->nb_inputs);
        ret->inputs       = av_mallocz(sizeof(AVFilterLink*) * ret->nb_inputs);
        if (!ret->inputs)
            goto err;
    }

    ret->nb_outputs = pad_count(filter->outputs);
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
    ret->output_count = ret->nb_outputs;
    ret->input_count  = ret->nb_inputs;
#endif

    *filter_ctx = ret;
    return 0;

err:
    av_freep(&ret->inputs);
    av_freep(&ret->input_pads);
    ret->nb_inputs = 0;
    av_freep(&ret->outputs);
    av_freep(&ret->output_pads);
    ret->nb_outputs = 0;
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

    for (i = 0; i < filter->nb_inputs; i++) {
        if ((link = filter->inputs[i])) {
            if (link->src)
                link->src->outputs[link->srcpad - link->src->output_pads] = NULL;
            ff_formats_unref(&link->in_formats);
            ff_formats_unref(&link->out_formats);
            ff_formats_unref(&link->in_samplerates);
            ff_formats_unref(&link->out_samplerates);
            ff_channel_layouts_unref(&link->in_channel_layouts);
            ff_channel_layouts_unref(&link->out_channel_layouts);
        }
        avfilter_link_free(&link);
    }
    for (i = 0; i < filter->nb_outputs; i++) {
        if ((link = filter->outputs[i])) {
            if (link->dst)
                link->dst->inputs[link->dstpad - link->dst->input_pads] = NULL;
            ff_formats_unref(&link->in_formats);
            ff_formats_unref(&link->out_formats);
            ff_formats_unref(&link->in_samplerates);
            ff_formats_unref(&link->out_samplerates);
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

    if (filter->filter->init_opaque)
        ret = filter->filter->init_opaque(filter, args, opaque);
    else if (filter->filter->init)
        ret = filter->filter->init(filter, args);
    return ret;
}

const char *avfilter_pad_get_name(AVFilterPad *pads, int pad_idx)
{
    return pads[pad_idx].name;
}

enum AVMediaType avfilter_pad_get_type(AVFilterPad *pads, int pad_idx)
{
    return pads[pad_idx].type;
}

static int default_filter_frame(AVFilterLink *link, AVFilterBufferRef *frame)
{
    return ff_filter_frame(link->dst->outputs[0], frame);
}

static int ff_filter_frame_framed(AVFilterLink *link, AVFilterBufferRef *frame)
{
    int (*filter_frame)(AVFilterLink *, AVFilterBufferRef *);
    AVFilterPad *src = link->srcpad;
    AVFilterPad *dst = link->dstpad;
    AVFilterBufferRef *out;
    int perms, ret;
    AVFilterCommand *cmd= link->dst->command_queue;
    int64_t pts;

    if (link->closed) {
        avfilter_unref_buffer(frame);
        return AVERROR_EOF;
    }

    if (!(filter_frame = dst->filter_frame))
        filter_frame = default_filter_frame;

    av_assert1((frame->perms & src->min_perms) == src->min_perms);
    frame->perms &= ~ src->rej_perms;
    perms = frame->perms;

    if (frame->linesize[0] < 0)
        perms |= AV_PERM_NEG_LINESIZES;

    /* prepare to copy the frame if the buffer has insufficient permissions */
    if ((dst->min_perms & perms) != dst->min_perms ||
        dst->rej_perms & perms) {
        av_log(link->dst, AV_LOG_DEBUG,
               "Copying data in avfilter (have perms %x, need %x, reject %x)\n",
               perms, link->dstpad->min_perms, link->dstpad->rej_perms);

        /* Maybe use ff_copy_buffer_ref instead? */
        switch (link->type) {
        case AVMEDIA_TYPE_VIDEO:
            out = ff_get_video_buffer(link, dst->min_perms,
                                      link->w, link->h);
            break;
        case AVMEDIA_TYPE_AUDIO:
            out = ff_get_audio_buffer(link, dst->min_perms,
                                      frame->audio->nb_samples);
            break;
        default: return AVERROR(EINVAL);
        }
        if (!out) {
            avfilter_unref_buffer(frame);
            return AVERROR(ENOMEM);
        }
        avfilter_copy_buffer_ref_props(out, frame);

        switch (link->type) {
        case AVMEDIA_TYPE_VIDEO:
            av_image_copy(out->data, out->linesize, frame->data, frame->linesize,
                          frame->format, frame->video->w, frame->video->h);
            break;
        case AVMEDIA_TYPE_AUDIO:
            av_samples_copy(out->extended_data, frame->extended_data,
                            0, 0, frame->audio->nb_samples,
                            av_get_channel_layout_nb_channels(frame->audio->channel_layout),
                            frame->format);
            break;
        default: return AVERROR(EINVAL);
        }

        avfilter_unref_buffer(frame);
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
    ret = filter_frame(link, out);
    ff_update_link_current_pts(link, pts);
    return ret;
}

static int ff_filter_frame_needs_framing(AVFilterLink *link, AVFilterBufferRef *frame)
{
    int insamples = frame->audio->nb_samples, inpos = 0, nb_samples;
    AVFilterBufferRef *pbuf = link->partial_buf;
    int nb_channels = frame->audio->channels;
    int ret = 0;

    /* Handle framing (min_samples, max_samples) */
    while (insamples) {
        if (!pbuf) {
            AVRational samples_tb = { 1, link->sample_rate };
            int perms = link->dstpad->min_perms | AV_PERM_WRITE;
            pbuf = ff_get_audio_buffer(link, perms, link->partial_buf_size);
            if (!pbuf) {
                av_log(link->dst, AV_LOG_WARNING,
                       "Samples dropped due to memory allocation failure.\n");
                return 0;
            }
            avfilter_copy_buffer_ref_props(pbuf, frame);
            pbuf->pts = frame->pts +
                        av_rescale_q(inpos, samples_tb, link->time_base);
            pbuf->audio->nb_samples = 0;
        }
        nb_samples = FFMIN(insamples,
                           link->partial_buf_size - pbuf->audio->nb_samples);
        av_samples_copy(pbuf->extended_data, frame->extended_data,
                        pbuf->audio->nb_samples, inpos,
                        nb_samples, nb_channels, link->format);
        inpos                   += nb_samples;
        insamples               -= nb_samples;
        pbuf->audio->nb_samples += nb_samples;
        if (pbuf->audio->nb_samples >= link->min_samples) {
            ret = ff_filter_frame_framed(link, pbuf);
            pbuf = NULL;
        }
    }
    avfilter_unref_buffer(frame);
    link->partial_buf = pbuf;
    return ret;
}

int ff_filter_frame(AVFilterLink *link, AVFilterBufferRef *frame)
{
    FF_TPRINTF_START(NULL, filter_frame); ff_tlog_link(NULL, link, 1); ff_tlog(NULL, " "); ff_tlog_ref(NULL, frame, 1);

    /* Consistency checks */
    if (link->type == AVMEDIA_TYPE_VIDEO) {
        if (strcmp(link->dst->filter->name, "scale")) {
            av_assert1(frame->format                 == link->format);
            av_assert1(frame->video->w               == link->w);
            av_assert1(frame->video->h               == link->h);
        }
    } else {
        av_assert1(frame->format                == link->format);
        av_assert1(frame->audio->channels       == link->channels);
        av_assert1(frame->audio->channel_layout == link->channel_layout);
        av_assert1(frame->audio->sample_rate    == link->sample_rate);
    }

    /* Go directly to actual filtering if possible */
    if (link->type == AVMEDIA_TYPE_AUDIO &&
        link->min_samples &&
        (link->partial_buf ||
         frame->audio->nb_samples < link->min_samples ||
         frame->audio->nb_samples > link->max_samples)) {
        return ff_filter_frame_needs_framing(link, frame);
    } else {
        return ff_filter_frame_framed(link, frame);
    }
}
