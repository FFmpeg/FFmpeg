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
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "avfilter.h"
#include "internal.h"

unsigned avfilter_version(void) {
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

static void command_queue_pop(AVFilterContext *filter)
{
    AVFilterCommand *c= filter->command_queue;
    av_freep(&c->arg);
    av_freep(&c->command);
    filter->command_queue= c->next;
    av_free(c);
}

AVFilterBufferRef *avfilter_ref_buffer(AVFilterBufferRef *ref, int pmask)
{
    AVFilterBufferRef *ret = av_malloc(sizeof(AVFilterBufferRef));
    if (!ret)
        return NULL;
    *ret = *ref;
    if (ref->type == AVMEDIA_TYPE_VIDEO) {
        ret->video = av_malloc(sizeof(AVFilterBufferRefVideoProps));
        if (!ret->video) {
            av_free(ret);
            return NULL;
        }
        *ret->video = *ref->video;
    } else if (ref->type == AVMEDIA_TYPE_AUDIO) {
        ret->audio = av_malloc(sizeof(AVFilterBufferRefAudioProps));
        if (!ret->audio) {
            av_free(ret);
            return NULL;
        }
        *ret->audio = *ref->audio;
    }
    ret->perms &= pmask;
    ret->buf->refcount ++;
    return ret;
}

static void store_in_pool(AVFilterBufferRef *ref)
{
    int i;
    AVFilterPool *pool= ref->buf->priv;

    av_assert0(ref->buf->data[0]);

    if (pool->count == POOL_SIZE) {
        AVFilterBufferRef *ref1 = pool->pic[0];
        av_freep(&ref1->video);
        av_freep(&ref1->audio);
        av_freep(&ref1->buf->data[0]);
        av_freep(&ref1->buf);
        av_free(ref1);
        memmove(&pool->pic[0], &pool->pic[1], sizeof(void*)*(POOL_SIZE-1));
        pool->count--;
        pool->pic[POOL_SIZE-1] = NULL;
    }

    for (i = 0; i < POOL_SIZE; i++) {
        if (!pool->pic[i]) {
            pool->pic[i] = ref;
            pool->count++;
            break;
        }
    }
}

void avfilter_unref_buffer(AVFilterBufferRef *ref)
{
    if (!ref)
        return;
    if (!(--ref->buf->refcount)) {
        if (!ref->buf->free) {
            store_in_pool(ref);
            return;
        }
        ref->buf->free(ref->buf);
    }
    av_freep(&ref->video);
    av_freep(&ref->audio);
    av_free(ref);
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

    if ((*link)->pool) {
        int i;
        for (i = 0; i < POOL_SIZE; i++) {
            if ((*link)->pool->pic[i]) {
                AVFilterBufferRef *picref = (*link)->pool->pic[i];
                /* free buffer: picrefs stored in the pool are not
                 * supposed to contain a free callback */
                av_freep(&picref->buf->data[0]);
                av_freep(&picref->buf);

                av_freep(&picref->audio);
                av_freep(&picref->video);
                av_freep(&(*link)->pool->pic[i]);
            }
        }
        (*link)->pool->count = 0;
//        av_freep(&(*link)->pool);
    }
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
    if (link->out_chlayouts)
        avfilter_formats_changeref(&link->out_chlayouts,
                                   &filt->outputs[filt_dstpad_idx]->out_chlayouts);
    if (link->out_packing)
        avfilter_formats_changeref(&link->out_packing,
                                   &filt->outputs[filt_dstpad_idx]->out_packing);

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

static char *ff_get_ref_perms_string(char *buf, size_t buf_size, int perms)
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

static void ff_dlog_ref(void *ctx, AVFilterBufferRef *ref, int end)
{
    av_unused char buf[16];
    av_dlog(ctx,
            "ref[%p buf:%p refcount:%d perms:%s data:%p linesize[%d, %d, %d, %d] pts:%"PRId64" pos:%"PRId64,
            ref, ref->buf, ref->buf->refcount, ff_get_ref_perms_string(buf, sizeof(buf), ref->perms), ref->data[0],
            ref->linesize[0], ref->linesize[1], ref->linesize[2], ref->linesize[3],
            ref->pts, ref->pos);

    if (ref->video) {
        av_dlog(ctx, " a:%d/%d s:%dx%d i:%c iskey:%d type:%c",
                ref->video->sample_aspect_ratio.num, ref->video->sample_aspect_ratio.den,
                ref->video->w, ref->video->h,
                !ref->video->interlaced     ? 'P' :         /* Progressive  */
                ref->video->top_field_first ? 'T' : 'B',    /* Top / Bottom */
                ref->video->key_frame,
                av_get_picture_type_char(ref->video->pict_type));
    }
    if (ref->audio) {
        av_dlog(ctx, " cl:%"PRId64"d n:%d r:%d p:%d",
                ref->audio->channel_layout,
                ref->audio->nb_samples,
                ref->audio->sample_rate,
                ref->audio->planar);
    }

    av_dlog(ctx, "]%s", end ? "\n" : "");
}

static void ff_dlog_link(void *ctx, AVFilterLink *link, int end)
{
    if (link->type == AVMEDIA_TYPE_VIDEO) {
        av_dlog(ctx,
                "link[%p s:%dx%d fmt:%-16s %-16s->%-16s]%s",
                link, link->w, link->h,
                av_pix_fmt_descriptors[link->format].name,
                link->src ? link->src->filter->name : "",
                link->dst ? link->dst->filter->name : "",
                end ? "\n" : "");
    } else {
        char buf[128];
        av_get_channel_layout_string(buf, sizeof(buf), -1, link->channel_layout);

        av_dlog(ctx,
                "link[%p r:%d cl:%s fmt:%-16s %-16s->%-16s]%s",
                link, (int)link->sample_rate, buf,
                av_get_sample_fmt_name(link->format),
                link->src ? link->src->filter->name : "",
                link->dst ? link->dst->filter->name : "",
                end ? "\n" : "");
    }
}

#define FF_DPRINTF_START(ctx, func) av_dlog(NULL, "%-16s: ", #func)

AVFilterBufferRef *avfilter_get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    AVFilterBufferRef *ret = NULL;

    av_unused char buf[16];
    FF_DPRINTF_START(NULL, get_video_buffer); ff_dlog_link(NULL, link, 0);
    av_dlog(NULL, " perms:%s w:%d h:%d\n", ff_get_ref_perms_string(buf, sizeof(buf), perms), w, h);

    if (link->dstpad->get_video_buffer)
        ret = link->dstpad->get_video_buffer(link, perms, w, h);

    if (!ret)
        ret = avfilter_default_get_video_buffer(link, perms, w, h);

    if (ret)
        ret->type = AVMEDIA_TYPE_VIDEO;

    FF_DPRINTF_START(NULL, get_video_buffer); ff_dlog_link(NULL, link, 0); av_dlog(NULL, " returning "); ff_dlog_ref(NULL, ret, 1);

    return ret;
}

AVFilterBufferRef *
avfilter_get_video_buffer_ref_from_arrays(uint8_t * const data[4], const int linesize[4], int perms,
                                          int w, int h, enum PixelFormat format)
{
    AVFilterBuffer *pic = av_mallocz(sizeof(AVFilterBuffer));
    AVFilterBufferRef *picref = av_mallocz(sizeof(AVFilterBufferRef));

    if (!pic || !picref)
        goto fail;

    picref->buf = pic;
    picref->buf->free = ff_avfilter_default_free_buffer;
    if (!(picref->video = av_mallocz(sizeof(AVFilterBufferRefVideoProps))))
        goto fail;

    pic->w = picref->video->w = w;
    pic->h = picref->video->h = h;

    /* make sure the buffer gets read permission or it's useless for output */
    picref->perms = perms | AV_PERM_READ;

    pic->refcount = 1;
    picref->type = AVMEDIA_TYPE_VIDEO;
    pic->format = picref->format = format;

    memcpy(pic->data,        data,          4*sizeof(data[0]));
    memcpy(pic->linesize,    linesize,      4*sizeof(linesize[0]));
    memcpy(picref->data,     pic->data,     sizeof(picref->data));
    memcpy(picref->linesize, pic->linesize, sizeof(picref->linesize));

    return picref;

fail:
    if (picref && picref->video)
        av_free(picref->video);
    av_free(picref);
    av_free(pic);
    return NULL;
}

AVFilterBufferRef *avfilter_get_audio_buffer(AVFilterLink *link,
                                             int perms, int nb_samples)
{
    AVFilterBufferRef *ret = NULL;

    if (link->dstpad->get_audio_buffer)
        ret = link->dstpad->get_audio_buffer(link, perms, nb_samples);

    if (!ret)
        ret = avfilter_default_get_audio_buffer(link, perms, nb_samples);

    if (ret)
        ret->type = AVMEDIA_TYPE_AUDIO;

    return ret;
}

AVFilterBufferRef *
avfilter_get_audio_buffer_ref_from_arrays(uint8_t *data[8], int linesize[8], int perms,
                                          int nb_samples, enum AVSampleFormat sample_fmt,
                                          int64_t channel_layout, int planar)
{
    AVFilterBuffer *samples = av_mallocz(sizeof(AVFilterBuffer));
    AVFilterBufferRef *samplesref = av_mallocz(sizeof(AVFilterBufferRef));

    if (!samples || !samplesref)
        goto fail;

    samplesref->buf = samples;
    samplesref->buf->free = ff_avfilter_default_free_buffer;
    if (!(samplesref->audio = av_mallocz(sizeof(AVFilterBufferRefAudioProps))))
        goto fail;

    samplesref->audio->nb_samples     = nb_samples;
    samplesref->audio->channel_layout = channel_layout;
    samplesref->audio->planar         = planar;

    /* make sure the buffer gets read permission or it's useless for output */
    samplesref->perms = perms | AV_PERM_READ;

    samples->refcount = 1;
    samplesref->type = AVMEDIA_TYPE_AUDIO;
    samplesref->format = sample_fmt;

    memcpy(samples->data,        data,     sizeof(samples->data));
    memcpy(samples->linesize,    linesize, sizeof(samples->linesize));
    memcpy(samplesref->data,     data,     sizeof(samplesref->data));
    memcpy(samplesref->linesize, linesize, sizeof(samplesref->linesize));

    return samplesref;

fail:
    if (samplesref && samplesref->audio)
        av_freep(&samplesref->audio);
    av_freep(&samplesref);
    av_freep(&samples);
    return NULL;
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

/* XXX: should we do the duplicating of the picture ref here, instead of
 * forcing the source filter to do it? */
void avfilter_start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    void (*start_frame)(AVFilterLink *, AVFilterBufferRef *);
    AVFilterPad *dst = link->dstpad;
    int perms = picref->perms;
    AVFilterCommand *cmd= link->dst->command_queue;

    FF_DPRINTF_START(NULL, start_frame); ff_dlog_link(NULL, link, 0); av_dlog(NULL, " "); ff_dlog_ref(NULL, picref, 1);

    if (!(start_frame = dst->start_frame))
        start_frame = avfilter_default_start_frame;

    if (picref->linesize[0] < 0)
        perms |= AV_PERM_NEG_LINESIZES;
    /* prepare to copy the picture if it has insufficient permissions */
    if ((dst->min_perms & perms) != dst->min_perms || dst->rej_perms & perms) {
        av_log(link->dst, AV_LOG_DEBUG,
                "frame copy needed (have perms %x, need %x, reject %x)\n",
                picref->perms,
                link->dstpad->min_perms, link->dstpad->rej_perms);

        link->cur_buf = avfilter_get_video_buffer(link, dst->min_perms, link->w, link->h);
        link->src_buf = picref;
        avfilter_copy_buffer_ref_props(link->cur_buf, link->src_buf);
    }
    else
        link->cur_buf = picref;

    while(cmd && cmd->time <= picref->pts * av_q2d(link->time_base)){
        av_log(link->dst, AV_LOG_DEBUG,
               "Processing command time:%f command:%s arg:%s\n",
               cmd->time, cmd->command, cmd->arg);
        avfilter_process_command(link->dst, cmd->command, cmd->arg, 0, 0, cmd->flags);
        command_queue_pop(link->dst);
        cmd= link->dst->command_queue;
    }

    start_frame(link, link->cur_buf);
}

void avfilter_end_frame(AVFilterLink *link)
{
    void (*end_frame)(AVFilterLink *);

    if (!(end_frame = link->dstpad->end_frame))
        end_frame = avfilter_default_end_frame;

    end_frame(link);

    /* unreference the source picture if we're feeding the destination filter
     * a copied version dues to permission issues */
    if (link->src_buf) {
        avfilter_unref_buffer(link->src_buf);
        link->src_buf = NULL;
    }
}

void avfilter_draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    uint8_t *src[4], *dst[4];
    int i, j, vsub;
    void (*draw_slice)(AVFilterLink *, int, int, int);

    FF_DPRINTF_START(NULL, draw_slice); ff_dlog_link(NULL, link, 0); av_dlog(NULL, " y:%d h:%d dir:%d\n", y, h, slice_dir);

    /* copy the slice if needed for permission reasons */
    if (link->src_buf) {
        vsub = av_pix_fmt_descriptors[link->format].log2_chroma_h;

        for (i = 0; i < 4; i++) {
            if (link->src_buf->data[i]) {
                src[i] = link->src_buf-> data[i] +
                    (y >> (i==1 || i==2 ? vsub : 0)) * link->src_buf-> linesize[i];
                dst[i] = link->cur_buf->data[i] +
                    (y >> (i==1 || i==2 ? vsub : 0)) * link->cur_buf->linesize[i];
            } else
                src[i] = dst[i] = NULL;
        }

        for (i = 0; i < 4; i++) {
            int planew =
                av_image_get_linesize(link->format, link->cur_buf->video->w, i);

            if (!src[i]) continue;

            for (j = 0; j < h >> (i==1 || i==2 ? vsub : 0); j++) {
                memcpy(dst[i], src[i], planew);
                src[i] += link->src_buf->linesize[i];
                dst[i] += link->cur_buf->linesize[i];
            }
        }
    }

    if (!(draw_slice = link->dstpad->draw_slice))
        draw_slice = avfilter_default_draw_slice;
    draw_slice(link, y, h, slice_dir);
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

void avfilter_filter_samples(AVFilterLink *link, AVFilterBufferRef *samplesref)
{
    void (*filter_samples)(AVFilterLink *, AVFilterBufferRef *);
    AVFilterPad *dst = link->dstpad;
    int i;

    FF_DPRINTF_START(NULL, filter_samples); ff_dlog_link(NULL, link, 1);

    if (!(filter_samples = dst->filter_samples))
        filter_samples = avfilter_default_filter_samples;

    /* prepare to copy the samples if the buffer has insufficient permissions */
    if ((dst->min_perms & samplesref->perms) != dst->min_perms ||
        dst->rej_perms & samplesref->perms) {

        av_log(link->dst, AV_LOG_DEBUG,
               "Copying audio data in avfilter (have perms %x, need %x, reject %x)\n",
               samplesref->perms, link->dstpad->min_perms, link->dstpad->rej_perms);

        link->cur_buf = avfilter_default_get_audio_buffer(link, dst->min_perms,
                                                          samplesref->audio->nb_samples);
        link->cur_buf->pts                = samplesref->pts;
        link->cur_buf->audio->sample_rate = samplesref->audio->sample_rate;

        /* Copy actual data into new samples buffer */
        for (i = 0; samplesref->data[i]; i++)
            memcpy(link->cur_buf->data[i], samplesref->data[i], samplesref->linesize[0]);

        avfilter_unref_buffer(samplesref);
    } else
        link->cur_buf = samplesref;

    filter_samples(link, link->cur_buf);
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

    if (filter->filter->uninit)
        filter->filter->uninit(filter);

    for (i = 0; i < filter->input_count; i++) {
        if ((link = filter->inputs[i])) {
            if (link->src)
                link->src->outputs[link->srcpad - link->src->output_pads] = NULL;
            avfilter_formats_unref(&link->in_formats);
            avfilter_formats_unref(&link->out_formats);
        }
        avfilter_link_free(&link);
    }
    for (i = 0; i < filter->output_count; i++) {
        if ((link = filter->outputs[i])) {
            if (link->dst)
                link->dst->inputs[link->dstpad - link->dst->input_pads] = NULL;
            avfilter_formats_unref(&link->in_formats);
            avfilter_formats_unref(&link->out_formats);
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
        command_queue_pop(filter);
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

