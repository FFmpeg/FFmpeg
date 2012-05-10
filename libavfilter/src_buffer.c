/*
 * Copyright (c) 2008 Vitor Sessak
 * Copyright (c) 2010 S.N. Hemanth Meenakshisundaram
 * Copyright (c) 2011 Mina Nagy Zaki
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

/**
 * @file
 * memory buffer source filter
 */

#include "avfilter.h"
#include "internal.h"
#include "avcodec.h"
#include "buffersrc.h"
#include "vsrc_buffer.h"
#include "asrc_abuffer.h"
#include "libavutil/audioconvert.h"
#include "libavutil/avstring.h"
#include "libavutil/fifo.h"
#include "libavutil/imgutils.h"

typedef struct {
    AVFifoBuffer     *fifo;
    AVRational        time_base;     ///< time_base to set in the output link
    int eof;
    unsigned          nb_failed_requests;

    /* Video only */
    AVFilterContext  *scale;
    int               h, w;
    enum PixelFormat  pix_fmt;
    AVRational        sample_aspect_ratio;
    char              sws_param[256];

    /* Audio only */
    // Audio format of incoming buffers
    int sample_rate;
    unsigned int sample_format;
    int64_t channel_layout;
    int packing_format;

    // Normalization filters
    AVFilterContext *aconvert;
    AVFilterContext *aresample;
} BufferSourceContext;

#define FIFO_SIZE 8

#define CHECK_PARAM_CHANGE(s, c, width, height, format)\
    if (c->w != width || c->h != height || c->pix_fmt != format) {\
        av_log(s, AV_LOG_ERROR, "Changing frame properties on the fly is not supported.\n");\
        return AVERROR(EINVAL);\
    }

static void buf_free(AVFilterBuffer *ptr)
{
    av_free(ptr);
    return;
}

static void set_link_source(AVFilterContext *src, AVFilterLink *link)
{
    link->src       = src;
    link->srcpad    = &(src->output_pads[0]);
    src->outputs[0] = link;
}

static int reconfigure_filter(BufferSourceContext *abuffer, AVFilterContext *filt_ctx)
{
    int ret;
    AVFilterLink * const inlink  = filt_ctx->inputs[0];
    AVFilterLink * const outlink = filt_ctx->outputs[0];

    inlink->format         = abuffer->sample_format;
    inlink->channel_layout = abuffer->channel_layout;
    inlink->planar         = abuffer->packing_format;
    inlink->sample_rate    = abuffer->sample_rate;

    filt_ctx->filter->uninit(filt_ctx);
    memset(filt_ctx->priv, 0, filt_ctx->filter->priv_size);
    if ((ret = filt_ctx->filter->init(filt_ctx, NULL , NULL)) < 0)
        return ret;
    if ((ret = inlink->srcpad->config_props(inlink)) < 0)
        return ret;
    return outlink->srcpad->config_props(outlink);
}

static int insert_filter(BufferSourceContext *abuffer,
                         AVFilterLink *link, AVFilterContext **filt_ctx,
                         const char *filt_name)
{
    int ret;

    if ((ret = avfilter_open(filt_ctx, avfilter_get_by_name(filt_name), NULL)) < 0)
        return ret;

    link->src->outputs[0] = NULL;
    if ((ret = avfilter_link(link->src, 0, *filt_ctx, 0)) < 0) {
        link->src->outputs[0] = link;
        return ret;
    }

    set_link_source(*filt_ctx, link);

    if ((ret = reconfigure_filter(abuffer, *filt_ctx)) < 0) {
        avfilter_free(*filt_ctx);
        return ret;
    }

    return 0;
}

static void remove_filter(AVFilterContext **filt_ctx)
{
    AVFilterLink *outlink = (*filt_ctx)->outputs[0];
    AVFilterContext *src  = (*filt_ctx)->inputs[0]->src;

    (*filt_ctx)->outputs[0] = NULL;
    avfilter_free(*filt_ctx);
    *filt_ctx = NULL;

    set_link_source(src, outlink);
}

static inline void log_input_change(void *ctx, AVFilterLink *link, AVFilterBufferRef *ref)
{
    char old_layout_str[16], new_layout_str[16];
    av_get_channel_layout_string(old_layout_str, sizeof(old_layout_str),
                                 -1, link->channel_layout);
    av_get_channel_layout_string(new_layout_str, sizeof(new_layout_str),
                                 -1, ref->audio->channel_layout);
    av_log(ctx, AV_LOG_INFO,
           "Audio input format changed: "
           "%s:%s:%d -> %s:%s:%d, normalizing\n",
           av_get_sample_fmt_name(link->format),
           old_layout_str, (int)link->sample_rate,
           av_get_sample_fmt_name(ref->format),
           new_layout_str, ref->audio->sample_rate);
}

static int check_format_change_video(AVFilterContext *buffer_filter,
                                     AVFilterBufferRef *picref)
{
    BufferSourceContext *c = buffer_filter->priv;
    int ret;

    if (picref->video->w != c->w || picref->video->h != c->h || picref->format != c->pix_fmt) {
        AVFilterContext *scale = buffer_filter->outputs[0]->dst;
        AVFilterLink *link;
        char scale_param[1024];

        av_log(buffer_filter, AV_LOG_INFO,
               "Buffer video input changed from size:%dx%d fmt:%s to size:%dx%d fmt:%s\n",
               c->w, c->h, av_pix_fmt_descriptors[c->pix_fmt].name,
               picref->video->w, picref->video->h, av_pix_fmt_descriptors[picref->format].name);

        if (!scale || strcmp(scale->filter->name, "scale")) {
            AVFilter *f = avfilter_get_by_name("scale");

            av_log(buffer_filter, AV_LOG_INFO, "Inserting scaler filter\n");
            if ((ret = avfilter_open(&scale, f, "Input equalizer")) < 0)
                return ret;

            c->scale = scale;

            snprintf(scale_param, sizeof(scale_param)-1, "%d:%d:%s", c->w, c->h, c->sws_param);
            if ((ret = avfilter_init_filter(scale, scale_param, NULL)) < 0) {
                return ret;
            }

            if ((ret = avfilter_insert_filter(buffer_filter->outputs[0], scale, 0, 0)) < 0) {
                return ret;
            }
            scale->outputs[0]->time_base = scale->inputs[0]->time_base;

            scale->outputs[0]->format= c->pix_fmt;
        } else if (!strcmp(scale->filter->name, "scale")) {
            snprintf(scale_param, sizeof(scale_param)-1, "%d:%d:%s",
                     scale->outputs[0]->w, scale->outputs[0]->h, c->sws_param);
            scale->filter->init(scale, scale_param, NULL);
        }

        c->pix_fmt = scale->inputs[0]->format = picref->format;
        c->w       = scale->inputs[0]->w      = picref->video->w;
        c->h       = scale->inputs[0]->h      = picref->video->h;

        link = scale->outputs[0];
        if ((ret =  link->srcpad->config_props(link)) < 0)
            return ret;
    }
    return 0;
}

static int check_format_change_audio(AVFilterContext *ctx,
                                     AVFilterBufferRef *samplesref)
{
    BufferSourceContext *abuffer = ctx->priv;
    AVFilterLink *link;
    int ret, logged = 0;

    link = ctx->outputs[0];
    if (samplesref->audio->sample_rate != link->sample_rate) {

        log_input_change(ctx, link, samplesref);
        logged = 1;

        abuffer->sample_rate = samplesref->audio->sample_rate;

        if (!abuffer->aresample) {
            ret = insert_filter(abuffer, link, &abuffer->aresample, "aresample");
            if (ret < 0) return ret;
        } else {
            link = abuffer->aresample->outputs[0];
            if (samplesref->audio->sample_rate == link->sample_rate)
                remove_filter(&abuffer->aresample);
            else
                if ((ret = reconfigure_filter(abuffer, abuffer->aresample)) < 0)
                    return ret;
        }
    }

    link = ctx->outputs[0];
    if (samplesref->format                != link->format         ||
        samplesref->audio->channel_layout != link->channel_layout ||
        samplesref->audio->planar         != link->planar) {

        if (!logged) log_input_change(ctx, link, samplesref);

        abuffer->sample_format  = samplesref->format;
        abuffer->channel_layout = samplesref->audio->channel_layout;
        abuffer->packing_format = samplesref->audio->planar;

        if (!abuffer->aconvert) {
            ret = insert_filter(abuffer, link, &abuffer->aconvert, "aconvert");
            if (ret < 0) return ret;
        } else {
            link = abuffer->aconvert->outputs[0];
            if (samplesref->format                == link->format         &&
                samplesref->audio->channel_layout == link->channel_layout &&
                samplesref->audio->planar         == link->planar
               )
                remove_filter(&abuffer->aconvert);
            else
                if ((ret = reconfigure_filter(abuffer, abuffer->aconvert)) < 0)
                    return ret;
        }
    }

    return 0;
}

static int check_format_change(AVFilterContext *buffer_filter,
                               AVFilterBufferRef *picref)
{
    switch (buffer_filter->outputs[0]->type) {
    case AVMEDIA_TYPE_VIDEO:
        return check_format_change_video(buffer_filter, picref);
    case AVMEDIA_TYPE_AUDIO:
        return check_format_change_audio(buffer_filter, picref);
    default:
        return AVERROR(ENOSYS);
    }
}

static AVFilterBufferRef *copy_buffer_ref(AVFilterContext *ctx,
                                          AVFilterBufferRef *ref)
{
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterBufferRef *buf;
    int channels, data_size, i;

    switch (outlink->type) {

    case AVMEDIA_TYPE_VIDEO:
        buf = avfilter_get_video_buffer(outlink, AV_PERM_WRITE,
                                        ref->video->w, ref->video->h);
        av_image_copy(buf->data, buf->linesize,
                      (void*)ref->data, ref->linesize,
                      ref->format, ref->video->w, ref->video->h);
        break;

    case AVMEDIA_TYPE_AUDIO:
        buf = ff_get_audio_buffer(outlink, AV_PERM_WRITE,
                                        ref->audio->nb_samples);
        channels = av_get_channel_layout_nb_channels(ref->audio->channel_layout);
        data_size = av_samples_get_buffer_size(NULL, channels,
                                               ref->audio->nb_samples,
                                               ref->format, 1);
        for (i = 0; i < FF_ARRAY_ELEMS(ref->buf->data) && ref->buf->data[i]; i++)
            memcpy(buf->buf->data[i], ref->buf->data[i], data_size);
        break;

    default:
        return NULL;
    }
    avfilter_copy_buffer_ref_props(buf, ref);
    return buf;
}

int av_buffersrc_add_ref(AVFilterContext *buffer_filter,
                         AVFilterBufferRef *picref, int flags)
{
    BufferSourceContext *c = buffer_filter->priv;
    AVFilterBufferRef *buf;
    int ret;

    if (!picref) {
        c->eof = 1;
        return 0;
    } else if (c->eof)
        return AVERROR(EINVAL);

    if (!av_fifo_space(c->fifo) &&
        (ret = av_fifo_realloc2(c->fifo, av_fifo_size(c->fifo) +
                                         sizeof(buf))) < 0)
        return ret;

    if (!(flags & AV_BUFFERSRC_FLAG_NO_CHECK_FORMAT)) {
        ret = check_format_change(buffer_filter, picref);
        if (ret < 0)
            return ret;
    }
    if (flags & AV_BUFFERSRC_FLAG_NO_COPY)
        buf = picref;
    else
        buf = copy_buffer_ref(buffer_filter, picref);

    if ((ret = av_fifo_generic_write(c->fifo, &buf, sizeof(buf), NULL)) < 0) {
        if (buf != picref)
            avfilter_unref_buffer(buf);
        return ret;
    }
    c->nb_failed_requests = 0;

    return 0;
}

int av_vsrc_buffer_add_video_buffer_ref(AVFilterContext *buffer_filter,
                                        AVFilterBufferRef *picref, int flags)
{
    return av_buffersrc_add_ref(buffer_filter, picref, 0);
}

#if CONFIG_AVCODEC
#include "avcodec.h"

int av_buffersrc_add_frame(AVFilterContext *buffer_src,
                           const AVFrame *frame, int flags)
{
    AVFilterBufferRef *picref;
    int ret;

    if (!frame) /* NULL for EOF */
        return av_buffersrc_add_ref(buffer_src, NULL, flags);

    switch (buffer_src->outputs[0]->type) {
    case AVMEDIA_TYPE_VIDEO:
        picref = avfilter_get_video_buffer_ref_from_frame(frame, AV_PERM_WRITE);
        break;
    case AVMEDIA_TYPE_AUDIO:
        picref = avfilter_get_audio_buffer_ref_from_frame(frame, AV_PERM_WRITE);
        break;
    default:
        return AVERROR(ENOSYS);
    }
    if (!picref)
        return AVERROR(ENOMEM);
    ret = av_buffersrc_add_ref(buffer_src, picref, flags);
    picref->buf->data[0] = NULL;
    avfilter_unref_buffer(picref);
    return ret;
}

int av_vsrc_buffer_add_frame(AVFilterContext *buffer_src,
                             const AVFrame *frame, int flags)
{
    return av_buffersrc_add_frame(buffer_src, frame, 0);
}
#endif

unsigned av_buffersrc_get_nb_failed_requests(AVFilterContext *buffer_src)
{
    return ((BufferSourceContext *)buffer_src->priv)->nb_failed_requests;
}

unsigned av_vsrc_buffer_get_nb_failed_requests(AVFilterContext *buffer_src)
{
    return ((BufferSourceContext *)buffer_src->priv)->nb_failed_requests;
}

static av_cold int init_video(AVFilterContext *ctx, const char *args, void *opaque)
{
    BufferSourceContext *c = ctx->priv;
    char pix_fmt_str[128];
    int ret, n = 0;
    *c->sws_param = 0;

    if (!args ||
        (n = sscanf(args, "%d:%d:%127[^:]:%d:%d:%d:%d:%255c", &c->w, &c->h, pix_fmt_str,
                    &c->time_base.num, &c->time_base.den,
                    &c->sample_aspect_ratio.num, &c->sample_aspect_ratio.den, c->sws_param)) < 7) {
        av_log(ctx, AV_LOG_ERROR, "Expected at least 7 arguments, but only %d found in '%s'\n", n, args);
        return AVERROR(EINVAL);
    }

    if ((ret = ff_parse_pixel_format(&c->pix_fmt, pix_fmt_str, ctx)) < 0)
        return ret;

    if (!(c->fifo = av_fifo_alloc(sizeof(AVFilterBufferRef*))))
        return AVERROR(ENOMEM);

    av_log(ctx, AV_LOG_INFO, "w:%d h:%d pixfmt:%s tb:%d/%d sar:%d/%d sws_param:%s\n",
           c->w, c->h, av_pix_fmt_descriptors[c->pix_fmt].name,
           c->time_base.num, c->time_base.den,
           c->sample_aspect_ratio.num, c->sample_aspect_ratio.den, c->sws_param);
    return 0;
}

static av_cold int init_audio(AVFilterContext *ctx, const char *args0, void *opaque)
{
    BufferSourceContext *abuffer = ctx->priv;
    char *arg = NULL, *ptr, chlayout_str[16];
    char *args = av_strdup(args0);
    int ret;

    arg = av_strtok(args, ":", &ptr);

#define ADD_FORMAT(fmt_name)                                            \
    if (!arg)                                                           \
        goto arg_fail;                                                  \
    if ((ret = ff_parse_##fmt_name(&abuffer->fmt_name, arg, ctx)) < 0) { \
        av_freep(&args);                                                \
        return ret;                                                     \
    }                                                                   \
    if (*args)                                                          \
        arg = av_strtok(NULL, ":", &ptr)

    ADD_FORMAT(sample_rate);
    ADD_FORMAT(sample_format);
    ADD_FORMAT(channel_layout);
    ADD_FORMAT(packing_format);

    abuffer->fifo = av_fifo_alloc(FIFO_SIZE*sizeof(AVFilterBufferRef*));
    if (!abuffer->fifo) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate fifo, filter init failed.\n");
        return AVERROR(ENOMEM);
    }

    av_get_channel_layout_string(chlayout_str, sizeof(chlayout_str),
                                 -1, abuffer->channel_layout);
    av_log(ctx, AV_LOG_INFO, "format:%s layout:%s rate:%d\n",
           av_get_sample_fmt_name(abuffer->sample_format), chlayout_str,
           abuffer->sample_rate);
    av_freep(&args);

    return 0;

arg_fail:
    av_log(ctx, AV_LOG_ERROR, "Invalid arguments, must be of the form "
                              "sample_rate:sample_fmt:channel_layout:packing\n");
    av_freep(&args);
    return AVERROR(EINVAL);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BufferSourceContext *s = ctx->priv;
    while (s->fifo && av_fifo_size(s->fifo)) {
        AVFilterBufferRef *buf;
        av_fifo_generic_read(s->fifo, &buf, sizeof(buf), NULL);
        avfilter_unref_buffer(buf);
    }
    av_fifo_free(s->fifo);
    s->fifo = NULL;
    avfilter_free(s->scale);
    s->scale = NULL;
}

static int query_formats_video(AVFilterContext *ctx)
{
    BufferSourceContext *c = ctx->priv;
    enum PixelFormat pix_fmts[] = { c->pix_fmt, PIX_FMT_NONE };

    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

static int query_formats_audio(AVFilterContext *ctx)
{
    BufferSourceContext *abuffer = ctx->priv;
    AVFilterFormats *formats;

    formats = NULL;
    avfilter_add_format(&formats, abuffer->sample_format);
    avfilter_set_common_sample_formats(ctx, formats);

    formats = NULL;
    avfilter_add_format(&formats, abuffer->channel_layout);
    avfilter_set_common_channel_layouts(ctx, formats);

    formats = NULL;
    avfilter_add_format(&formats, abuffer->packing_format);
    avfilter_set_common_packing_formats(ctx, formats);

    return 0;
}

static int config_output_video(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;

    link->w = c->w;
    link->h = c->h;
    link->sample_aspect_ratio = c->sample_aspect_ratio;
    link->time_base = c->time_base;

    return 0;
}

static int config_output_audio(AVFilterLink *outlink)
{
    BufferSourceContext *abuffer = outlink->src->priv;
    outlink->sample_rate = abuffer->sample_rate;
    return 0;
}

static int request_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    AVFilterBufferRef *buf;

    if (!av_fifo_size(c->fifo)) {
        if (c->eof)
            return AVERROR_EOF;
        c->nb_failed_requests++;
        return AVERROR(EAGAIN);
    }
    av_fifo_generic_read(c->fifo, &buf, sizeof(buf), NULL);

    switch (link->type) {
    case AVMEDIA_TYPE_VIDEO:
        avfilter_start_frame(link, avfilter_ref_buffer(buf, ~0));
        avfilter_draw_slice(link, 0, link->h, 1);
        avfilter_end_frame(link);
        avfilter_unref_buffer(buf);
        break;
    case AVMEDIA_TYPE_AUDIO:
        ff_filter_samples(link, avfilter_ref_buffer(buf, ~0));
        avfilter_unref_buffer(buf);
        break;
    default:
        return AVERROR(ENOSYS);
    }
    return 0;
}

static int poll_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    int size = av_fifo_size(c->fifo);
    if (!size && c->eof)
        return AVERROR_EOF;
    return size/sizeof(AVFilterBufferRef*);
}

int av_asrc_buffer_add_audio_buffer_ref(AVFilterContext *ctx,
                                        AVFilterBufferRef *samplesref,
                                        int av_unused flags)
{
    return av_buffersrc_add_ref(ctx, samplesref, AV_BUFFERSRC_FLAG_NO_COPY);
}

int av_asrc_buffer_add_samples(AVFilterContext *ctx,
                               uint8_t *data[8], int linesize[8],
                               int nb_samples, int sample_rate,
                               int sample_fmt, int64_t channel_layout, int planar,
                               int64_t pts, int av_unused flags)
{
    AVFilterBufferRef *samplesref;

    samplesref = avfilter_get_audio_buffer_ref_from_arrays(
                     data, linesize, AV_PERM_WRITE,
                     nb_samples,
                     sample_fmt, channel_layout, planar);
    if (!samplesref)
        return AVERROR(ENOMEM);

    samplesref->buf->free  = buf_free;
    samplesref->pts = pts;
    samplesref->audio->sample_rate = sample_rate;

    AV_NOWARN_DEPRECATED(
    return av_asrc_buffer_add_audio_buffer_ref(ctx, samplesref, 0);
    )
}

int av_asrc_buffer_add_buffer(AVFilterContext *ctx,
                              uint8_t *buf, int buf_size, int sample_rate,
                              int sample_fmt, int64_t channel_layout, int planar,
                              int64_t pts, int av_unused flags)
{
    uint8_t *data[8] = {0};
    int linesize[8];
    int nb_channels = av_get_channel_layout_nb_channels(channel_layout),
        nb_samples  = buf_size / nb_channels / av_get_bytes_per_sample(sample_fmt);

    av_samples_fill_arrays(data, linesize,
                           buf, nb_channels, nb_samples,
                           sample_fmt, 16);

    AV_NOWARN_DEPRECATED(
    return av_asrc_buffer_add_samples(ctx,
                                      data, linesize, nb_samples,
                                      sample_rate,
                                      sample_fmt, channel_layout, planar,
                                      pts, flags);
    )
}

AVFilter avfilter_vsrc_buffer = {
    .name      = "buffer",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them accessible to the filterchain."),
    .priv_size = sizeof(BufferSourceContext),
    .query_formats = query_formats_video,

    .init      = init_video,
    .uninit    = uninit,

    .inputs    = (const AVFilterPad[]) {{ .name = NULL }},
    .outputs   = (const AVFilterPad[]) {{ .name      = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .request_frame   = request_frame,
                                    .poll_frame      = poll_frame,
                                    .config_props    = config_output_video, },
                                  { .name = NULL}},
};

#if CONFIG_ABUFFER_FILTER

AVFilter avfilter_asrc_abuffer = {
    .name        = "abuffer",
    .description = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them accessible to the filterchain."),
    .priv_size   = sizeof(BufferSourceContext),
    .query_formats = query_formats_audio,

    .init        = init_audio,
    .uninit      = uninit,

    .inputs      = (const AVFilterPad[]) {{ .name = NULL }},
    .outputs     = (const AVFilterPad[]) {{ .name      = "default",
                                      .type            = AVMEDIA_TYPE_AUDIO,
                                      .request_frame   = request_frame,
                                      .poll_frame      = poll_frame,
                                      .config_props    = config_output_audio, },
                                    { .name = NULL}},
};

#endif
